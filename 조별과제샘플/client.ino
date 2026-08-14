/*
 * client.ino — 주차 관제 노드 (2열 × 5행 = 10칸)
 *
 * 근거 문서: docs/net/parking-protocol.md v1 (얼림).  요청: REQ-0018
 * 이 파일과 명세가 어긋나면 **명세가 이긴다.** 명세를 바꿔야 하면 루트에게 요청을 발행한다(§12).
 *
 * 타깃 보드 : Arduino Uno   (FQBN arduino:avr:uno)
 * 무선 모듈 : ESP-01, AT 펌웨어, SoftwareSerial 9600bps
 * 배선      : D7 = ESP TX → Uno(RX) / D8 = Uno(TX) → ESP RX   (현행 배선 유지)
 *
 * 명세 대응표
 *   §1    자리 인덱스 0..9 = A1..A5,B1..B5. 비트열 왼쪽 끝이 인덱스 0
 *   §2.2  체크섬 = 첫 바이트 ~ 체크섬 앞 쉼표(포함) XOR, 대문자 2자리 hex
 *   §2.4  S(상태) 송신 / R,C 수신 / A(ACK) 송신
 *   §3.3  AT+UART_DEF 를 쓰지 않는다 — ESP-01 플래시에 영구 기록되기 때문
 *   §3.4  하트비트 1Hz + 변화 시 100ms 디바운스, 타이머는 하나·전송하면 리셋
 *   §4.2  rid 멱등 캐시 8건 — 같은 rid 재수신 시 재적용 없이 같은 ACK 재전송
 *   §6.2  수신 4단계: LF 분할 → +IPD,<n>: 뒤 → 타입 문자 → 체크섬
 *
 * String 을 쓰지 않는다(AVR 힙 단편화). 모든 프레임은 고정 char[] + snprintf 로 만든다.
 */

#include <SoftwareSerial.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <string.h>

#define DEBUG 1

// ─────────────────────────────────────────────────────────────────────────
// 배선 · 네트워크 상수
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t PIN_ESP_RX = 7;   // ESP TX → Uno
static const uint8_t PIN_ESP_TX = 8;   // Uno → ESP RX
SoftwareSerial wifi(PIN_ESP_RX, PIN_ESP_TX);

#define WIFI_SSID    "3F_302"
#define WIFI_PASS    "0424719222!!"
#define SERVER_IP    "192.168.0.29"     // §11 — 명세는 주소를 가정하지 않는다. 현장에서 바꾼다
#define SERVER_PORT  "9991"
#define DEVICE_ID    "P1"               // §2.3 devid ::= 1*8자. 옛 "ARD_NODE_01"(11자)은 BNF 위반이었다

// ─────────────────────────────────────────────────────────────────────────
// 타이밍 (§3.4, §6.3)
// ─────────────────────────────────────────────────────────────────────────
static const uint16_t HEARTBEAT_MS     = 1000;  // §3.4 1Hz
static const uint16_t DEBOUNCE_MS      = 100;   // §3.4 변화 디바운스
static const uint16_t PROMPT_TIMEOUT_MS= 300;   // '>' 프롬프트 대기 상한 ("수백 ms")
static const uint16_t SEND_FAIL_BACKOFF_MS = 500; // 전송 실패 후 재시도 최소 간격
static const uint8_t  SEND_GAP_MS      = 80;    // 연속 CIPSEND 사이 최소 간격 (busy p... 회피)

// ─────────────────────────────────────────────────────────────────────────
// 자리 (§1)
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t SLOT_N = 10;      // 인덱스 0..4 = A1..A5, 5..9 = B1..B5

static inline char slotCol(uint8_t i) { return (i < 5) ? 'A' : 'B'; }
static inline char slotRow(uint8_t i) { return (char)('1' + (i % 5)); }

// 자리 문자 2개 → 인덱스. 없으면 0xFF
static uint8_t slotIndexOf(char c0, char c1) {
  if (c1 < '1' || c1 > '5') return 0xFF;
  if (c0 == 'A') return (uint8_t)(c1 - '1');
  if (c0 == 'B') return (uint8_t)(c1 - '1' + 5);
  return 0xFF;
}

static uint16_t occMask = 0;   // 점유 비트 (센서가 주인 — §7.4)
static uint16_t resMask = 0;   // 예약 비트 (서버가 주인 — §7.4. R 로 켜고 C 로만 끈다)

// ─────────────────────────────────────────────────────────────────────────
// 가상 센서 — 실물 센서가 오면 readSlotSensor() 본문만 바꾼다
// ─────────────────────────────────────────────────────────────────────────
// §12B.1 **자율 전진을 없앴다.** 센서가 배정되지 않은 칸은 트리거(M 프레임)를 받기 전까지
// 값이 바뀌지 않는다. 예전에는 칸마다 타이머를 두고 알아서 뒤집었는데, 그 "주차장처럼 보이게"
// 하려던 연출의 대가가 **실물 시험 불가**였다 — 화면 전환이 너무 빨라 한 칸을 고정해 놓고
// 관찰하는 것 자체가 안 됐다. 보기 좋게 만들려던 것이 확인을 불가능하게 만들었다.
//
// 그래서 사라진 것: simNextAt[10](40바이트), simLastChangeAt, SIM_MIN_GAP_MS,
//   SIM_OCC/EMPTY_MIN/MAX_MS, ARRIVE_MIN/MAX_MS, simAdvance(), simResume().
//   `ARRIVE_*` 가 하던 일(예약된 칸에 차가 들어오게 만드는 것)은 §12B.2 의
//   **"예약된 빈칸 우선"** 규칙이 대신한다 — 아래 simStep() 참조.
static uint16_t simOcc = 0;


// ─────────────────────────────────────────────────────────────────────────
// 실물 센서 배선 — 핀 배정과 그 근거
//
// Uno 에서 쓸 수 없는 핀:
//   D0, D1  : USB 시리얼(하드웨어 UART). 여기에 물리면 업로드·디버그 출력이 깨진다
//   D7, D8  : SoftwareSerial 로 ESP-01 과 통신 중 (PIN_ESP_RX / PIN_ESP_TX)
//   D13     : 온보드 LED 가 저항+LED 로 GND 쪽으로 약하게 당긴다.
//             INPUT_PULLUP(내부 20~50kΩ)과 분압이 되어 HIGH 가 확실히 안 읽힌다.
//             "가끔 점유로 읽히는" 최악의 고장이 되므로 처음부터 뺀다
//   A4, A5  : Uno 의 I2C(SDA/SCL). 지금 I2C 를 안 쓰더라도 여기를 채우면
//             나중에 I2C 장치(디스플레이 등)를 붙일 길이 막힌다. 비워 둔다
//
// 남는 디지털 핀은 D2~D6, D9~D12 로 **아홉 개뿐**이라 10번째 칸은 아날로그를 디지털로 쓴다.
//   → B5 에 A0 을 배정했다. A0~A5 는 디지털 입출력으로 그대로 쓸 수 있다(A0 == 14).
//   ⚠ 헷갈리기 쉬운 지점: 여기서 말하는 **핀 `A0`** 은 **자리 `A1`~`A5`** 와 아무 상관이 없다.
//      자리 이름과 아날로그 핀 이름이 우연히 같은 글자를 쓴다. 자리 `A0` 은 존재하지 않는다.
//   난수 시드는 A1 에서 뽑는다(아래 setup()). 어디에도 안 물린 핀이라 노이즈가 필요하다 —
//   그래서 A1 은 자리에 배정하지 않았다.
//
// 참고: D10~D12 는 SPI(SS/MOSI/MISO)다. 지금은 안 쓰지만 SD 카드·이더넷 실드를 붙이려면
//       그 세 칸을 다른 핀으로 옮겨야 한다. 옮길 때는 아래 표 한 줄씩만 고치면 된다.
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t SLOT_PIN[SLOT_N] PROGMEM = {
  2,  3,  4,  5,  6,      // 인덱스 0..4 = A1 A2 A3 A4 A5
  9, 10, 11, 12, A0       // 인덱스 5..9 = B1 B2 B3 B4 B5   (B5 만 아날로그 핀)
};
static inline uint8_t slotPin(uint8_t i) { return pgm_read_byte(&SLOT_PIN[i]); }

// 센서 극성. INPUT_PULLUP 을 쓰므로 "차량 감지 시 접점이 GND 로 당기는" 형식을 기본으로 본다.
// 반대 극성 센서(감지 시 HIGH)면 이 값만 0 으로 바꾸면 된다.
#define SENSOR_ACTIVE_LOW 1

// 칸별 센서 소스. 비트 i 가 1 이면 그 칸은 실물(REAL), 0 이면 시뮬(SIM).
// **실제 설치는 10칸이 한꺼번에 되지 않는다.** 배선이 끝난 칸만 1 로 올리면
// 나머지는 그대로 시뮬로 돈다. 예) A1·A2·B3 만 배선했다면 → 0x0083
//   (A1=bit0, A2=bit1, A3=bit2, A4=bit3, A5=bit4, B1=bit5 … B5=bit9)
#ifndef SLOT_SRC_DEFAULT
#define SLOT_SRC_DEFAULT 0x0000      // 기본: 센서가 아직 하나도 없으므로 10칸 전부 시뮬
#endif
static uint16_t srcReal = SLOT_SRC_DEFAULT;

static uint8_t readRealSensor(uint8_t i) {
  uint8_t raw = digitalRead(slotPin(i));
#if SENSOR_ACTIVE_LOW
  return (raw == LOW) ? 1 : 0;
#else
  return (raw == HIGH) ? 1 : 0;
#endif
}

// 그 칸을 실물로 돌리기로 했으면 입력 모드를 잡아 준다. setup() 과 소스 변경 시에 부른다.
static void applySlotPinMode(uint8_t i) {
  if (srcReal & ((uint16_t)1 << i)) pinMode(slotPin(i), INPUT_PULLUP);
}

// ─────────────────────────────────────────────────────────────────────────
// 수동 오버라이드 — 칸별. 활성 소스(REAL/SIM)보다 **우선**한다.
//
// ⚠ 지금 이 함수들을 부르는 곳은 **아무 데도 없다. 그것이 의도다.**
//    오버라이드를 설정하는 제어는 브라우저에서 전선(TCP)을 타고 내려올 예정이고,
//    그건 프로토콜 개정이라 docs/net/parking-protocol.md 가 먼저 얼어야 한다(REQ-0028).
//    여기 있는 것은 **그릇**이다 — 명세가 확정되면 수신 처리부가 이 함수들을 부르면 된다.
//    USB 시리얼 콘솔로 만들지 않은 이유: 같은 상태를 건드리는 제어 경로가 둘이면 어긋난다(REQ-0027 정정).
//
// static 이 아니라 전역으로 둔 이유: 호출자가 아직 없어서 static 이면 -Wunused-function 이 뜬다.
// 전역이면 링커의 --gc-sections 가 최종 이미지에서 빼 주므로 지금은 플래시도 먹지 않는다.
// ─────────────────────────────────────────────────────────────────────────
static uint16_t ovrActive = 0;   // 비트 i = 이 칸에 값을 강제하고 있다  ( = S 프레임의 tmask)
static uint16_t ovrValue  = 0;   // 비트 i = 강제할 값 (ovrActive 가 1 일 때만 의미 있다)

// 테스트 모드 무장 여부 (§12A.2). 무장 중일 때만 칸별 주입(T,...,S,..)이 먹는다.
// **재부팅하면 해제 상태로 시작한다** — 예약(§7.4)과 정반대로 서버가 재하달하지 않는다(§12A.3).
// 가짜 값이 재부팅 뒤 되살아나는 것이 더 위험하기 때문이다.
static bool testArmed = false;

void slotOverrideSet(uint8_t i, uint8_t value) {
  if (i >= SLOT_N) return;
  uint16_t bit = (uint16_t)1 << i;
  ovrActive |= bit;
  if (value) ovrValue |= bit; else ovrValue &= (uint16_t)~bit;
}

void slotOverrideClear(uint8_t i) {
  if (i >= SLOT_N) return;
  uint16_t bit = (uint16_t)1 << i;
  ovrActive &= (uint16_t)~bit;
  ovrValue  &= (uint16_t)~bit;
}

void slotOverrideClearAll(void) {
  ovrActive = 0;
  ovrValue  = 0;
}

// 칸별 소스 전환. REAL 로 바꿀 때 입력 모드까지 같이 잡는다.
void slotSourceSet(uint8_t i, uint8_t useReal) {
  if (i >= SLOT_N) return;
  uint16_t bit = (uint16_t)1 << i;
  if (useReal) { srcReal |= bit; applySlotPinMode(i); }
  else         { srcReal &= (uint16_t)~bit; }
}

// ─────────────────────────────────────────────────────────────────────────
// 시뮬 한 걸음 (§12B.2) — M 프레임이 이걸 부른다
//
// 규칙: **트리거 한 번 = 시뮬 칸 하나의 occupied 를 뒤집는다.** 그 이상 바꾸지 않는다.
//   여러 칸이 한꺼번에 바뀌면 "눌렀다 → 저 칸이 바뀌었다"의 1:1 대응이 깨져
//   지금 문제(무엇이 언제 바뀌었는지 못 따라감)가 그대로 재현된다.
//
// 고르는 순서:
//   1순위 — `reserved=1 ∧ occupied=0` 인 시뮬 칸을 **채운다(0→1)**.
//           이게 `occupied=1 ∧ reserved=1`(§1.1 마지막 행 = 이 시스템이 성공한 모습)에
//           도달하는 유일한 경로다. 순수 무작위면 평균 열 번쯤 눌러야 보이는데,
//           **도달 가능하지만 사실상 안 보이는 상태는 도달 불가와 실질적으로 같다.**
//   2순위 — 시뮬 칸 중 무작위로 하나를 뒤집는다.
//   없으면 — 0xFF 를 돌려준다 → 호출자가 result=5 로 응답한다.
//
// ⚠ **후보에서 빼는 칸이 둘 있고, 두 번째는 내 판단이다:**
//   (a) 실물 센서 칸(`srcReal`) — 명세가 명시했다. 진실이고 사람이 흔들 것이 아니다(§12B.3).
//   (b) **지금 오버라이드가 먹고 있는 칸**(`testArmed && ovrActive`) — 명세에 없다.
//       그 칸을 고르면 simOcc 는 바뀌는데 보고되는 occupied 는 오버라이드에 가려 안 바뀐다.
//       ACK 는 "그 칸이 바뀌었다"고 말하는데 화면은 그대로 → §12B.2 의 1:1 대응이 깨진다.
//       후보에서 빼면 후보의 simOcc 가 곧 보고되는 occupied 라 1순위 판정도 모호함이 없다.
//       (전 시뮬 칸이 오버라이드 중이면 관측 가능한 변화가 없으므로 result=5 로 본다.)
//       루트에게 명세 보완을 올려 둔다.
// ─────────────────────────────────────────────────────────────────────────
static bool simCandidate(uint8_t i) {
  uint16_t bit = (uint16_t)1 << i;
  if (srcReal & bit) return false;                       // (a) 실물 칸
  if (testArmed && (ovrActive & bit)) return false;      // (b) 오버라이드가 가리는 칸
  return true;
}

// 바뀐 칸 인덱스를 돌려준다. 바꿀 칸이 없으면 0xFF.
static uint8_t simStep(void) {
  // 1순위: 예약됐지만 비어 있는 시뮬 칸을 채운다
  for (uint8_t i = 0; i < SLOT_N; i++) {
    uint16_t bit = (uint16_t)1 << i;
    if (!simCandidate(i)) continue;
    if ((resMask & bit) && !(simOcc & bit)) {
      simOcc |= bit;
      return i;
    }
  }
  // 2순위: 시뮬 칸 중 무작위 하나를 뒤집는다
  uint8_t n = 0;
  for (uint8_t i = 0; i < SLOT_N; i++) if (simCandidate(i)) n++;
  if (n == 0) return 0xFF;
  uint8_t pick = (uint8_t)random(0, n);
  for (uint8_t i = 0; i < SLOT_N; i++) {
    if (!simCandidate(i)) continue;
    if (pick-- == 0) {
      simOcc ^= (uint16_t)1 << i;
      return i;
    }
  }
  return 0xFF;                                           // 도달하지 않는다
}

/* ── ★ 센서가 도착했을 때 무엇을 하면 되는가 (6개월 뒤에 읽을 사람에게) ★ ──────
 * 1. 위 SLOT_PIN[] 표에서 그 칸의 핀을 확인하고, 센서 접점을 그 핀과 GND 사이에 문다.
 *    INPUT_PULLUP 을 쓰므로 "차량이 있으면 GND 로 당긴다" 형식이면 배선은 그것으로 끝이다.
 *    극성이 반대인 센서면 위의 SENSOR_ACTIVE_LOW 를 0 으로 바꾼다.
 * 2. **배선을 끝낸 칸의 비트만** SLOT_SRC_DEFAULT 에 올린다(A1=bit0 … B5=bit9).
 *    나머지 칸은 그대로 시뮬로 돈다 — 10칸을 한꺼번에 배선할 필요가 없다.
 *    예) A1·A2·B3 세 칸만 배선했다면 `#define SLOT_SRC_DEFAULT 0x0083`.
 * 3. 다시 올리고 시리얼 모니터(115200)의 `[TX] S,...` 줄에서 그 칸 비트를 본다.
 *    센서를 손으로 가려 보며 해당 자리의 0/1 이 따라 바뀌면 배선이 맞은 것이다.
 *    (자리 순서는 §1 대로 왼쪽 끝이 A1 이다.)
 * 4. 차 없이 상태를 만들어 봐야 하면 **브라우저 화면의 테스트 모드**를 쓴다. 전선으로 T 프레임이 내려온다:
 *      T,<rid>,A,??,-        무장 (이걸 먼저 해야 주입이 먹는다 — §12A.2)
 *      T,<rid>,S,<slot>,<0|1>  그 칸 occupied 를 주입
 *      T,<rid>,X,<slot>,-    그 칸만 원래 소스로
 *      T,<rid>,D,??,-        해제 (전 칸 오버라이드가 한 번에 사라진다)
 *    무장 중에는 S 프레임에 tmask 필드가 붙어 "이 값은 주입된 것"임을 서버·화면에 알린다.
 *    ⚠ 벤치에서 코드로 직접 부를 거면 slotOverrideSet(i,1) 만으로는 **아무 일도 일어나지 않는다.**
 *      testArmed 도 같이 세워야 한다 — 해제 상태에서는 주입이 적용되지 않는다(§12A.2).
 * 5. **readSlotSensor() 본문은 고치지 마라.** 층이 이미 나뉘어 있어서 고칠 이유가 없고,
 *    고치면 차 없이 시험할 수단을 그 순간 잃는다(이 구조를 만든 이유가 그것이다).
 */

// ★ 센서값의 단일 진입점 — 시그니처와 호출부는 바뀌지 않는다 ★
//   우선순위: 수동 오버라이드 > 칸별 소스(실물 / 시뮬)
uint8_t readSlotSensor(uint8_t i) {
  // §12A.2 "무장 중일 때만 칸별 주입이 적용된다" — testArmed 를 여기서 한 번 더 본다.
  // 실사용에서는 중복이다(T,S 가 무장 검사를 하고 T,D 가 전 칸을 지운다). 그래도 두는 이유:
  // 해제 상태에서 주입값이 occupied 에 실리면 **tmask 가 안 붙은 채로** 전선에 나간다.
  // 그러면 서버·화면은 그 값을 실측으로 믿는다 — §12A.6 이 막으려는 바로 그 사고다.
  // 여기서 막으면 "해제 = 현실로 복귀"가 ClearAll 호출에 기대지 않고 구조적으로 성립한다.
  if (testArmed && (ovrActive & ((uint16_t)1 << i))) return (uint8_t)((ovrValue >> i) & 1);

  // 실물 센서 칸은 **무장 중에도 계속 읽는다.** 그건 진실이고 가릴 이유가 없다(REQ-0043).
  // 그래서 "3칸 실물 + 7칸 시뮬" 상태로 무장하면 실물 3칸은 살아 움직이고 시뮬 7칸만 얼어붙는다.
  if (srcReal & ((uint16_t)1 << i)) return readRealSensor(i);

  // ★ 시뮬 칸은 **트리거(M 프레임)를 받을 때만** 바뀐다(§12B.1). 여기서는 그냥 읽는다.
  //   무장 여부와 무관하다 — 시뮬 트리거는 테스트 모드와 별개다(§12B.3).
  //   ⚠ 시뮬 값은 tmask 에 넣지 않는다. tmask 는 "S 로 주입된 값인가"를 말하는 것이고,
  //     시뮬 값은 주입된 게 아니라 원래 시뮬이었던 값이 한 걸음 간 것이다(§12B.3).
  return (uint8_t)((simOcc >> i) & 1);
}

// ─────────────────────────────────────────────────────────────────────────
// 수신 버퍼 — 셋은 반드시 서로 다른 버퍼여야 한다
//   rxLine   : 시리얼 누적 전용 (송신 중에도 계속 채워진다)
//   workLine : 파싱 전용 (그 자리에서 쉼표를 NUL 로 바꾼다)
//   pendLine : 송신 중 도착한 줄을 미뤄 두는 곳
// 하나로 합치면 ACK 송신 중 들어온 바이트가 파싱 중인 버퍼를 덮어써서 조용히 깨진다.
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t RX_CAP = 72;
static char    rxLine[RX_CAP];
static char    workLine[RX_CAP];
static char    pendLine[RX_CAP];
static uint8_t rxLen = 0;
static bool    rxOverflow = false;
static bool    pendReady = false;
static bool    inSend = false;         // 송신 중에는 줄을 처리하지 않고 미룬다(재진입 방지)

// ─────────────────────────────────────────────────────────────────────────
// 현장 진단 (REQ-0042) — "추측하기 전에 보이게 만든다"
//
// 실기에서 TCP 는 붙는데(서버에 +ARD) netOnline 이 안 켜지는 증상이 나왔다.
// 그때 우리가 모르는 것은 셋 중 어느 것인가였다:
//   (A) ESP→Uno 로 바이트가 아예 안 온다        → 배선/레벨/RX 핀 문제
//   (B) 바이트는 오는데 줄이 안 끊긴다           → 줄 종단 문자가 예상과 다르다(LF 없음 등)
//   (C) 줄은 오는데 접속 문구를 못 알아본다      → 판정 문자열 문제
// 아래 세 카운터가 이 셋을 **로그 한 줄로 가른다.** 추측을 줄이는 것이 목적이다.
// ─────────────────────────────────────────────────────────────────────────
#if DEBUG
static unsigned long dbgRxBytes  = 0;   // 시리얼에서 읽은 총 바이트 (A 를 가른다)
static unsigned long dbgLineCnt  = 0;   // 완성된 줄 수            (B 를 가른다)
static unsigned long dbgLastDiag = 0;
static const uint16_t DIAG_PERIOD_MS = 3000;
#endif

// ─────────────────────────────────────────────────────────────────────────
// rid 멱등 캐시 (§4.2) — 최근 8건, 결과까지 같이 들고 있는다
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t CACHE_N = 8;
struct AckRec {
  uint16_t rid;
  char     slot[2];
  uint8_t  result;
};
static AckRec  cache[CACHE_N];
static uint8_t cacheHead = 0;   // 다음에 덮어쓸 자리
static uint8_t cacheCount = 0;

static int8_t cacheFind(uint16_t rid) {
  for (uint8_t k = 0; k < cacheCount; k++) if (cache[k].rid == rid) return (int8_t)k;
  return -1;
}
// REQ-0032 판정 (a): **새 TCP 연결이 맺어지면 캐시를 비운다.**
// wire_rid 는 서버가 발급하고 서버 재시작 시 1부터 다시 시작한다. 아두이노는 재부팅하지 않았으므로
// 옛 세션의 rid 가 캐시에 남아 새 서버의 1,2,3… 과 충돌하고, 최대 8개 명령이 "재수신"으로 삼켜진다.
// 가장 나쁜 점은 result=0 이라 **성공으로 보인다**는 것이다 — 타임아웃도 오류도 안 난다.
// 비워도 잃는 것이 없다: §7.3 재전송(1500ms·2회)은 전부 살아 있는 연결 위에서 끝나고,
// 연결이 끊기면 §7.4 가 새 wire_rid 로 재하달한다. rid 를 가로질러 재전송이 이어지는 경로가 없다.
static void cacheClear(void) {
  cacheHead = 0;
  cacheCount = 0;
}

static void cachePut(uint16_t rid, char s0, char s1, uint8_t result) {
  cache[cacheHead].rid = rid;
  cache[cacheHead].slot[0] = s0;
  cache[cacheHead].slot[1] = s1;
  cache[cacheHead].result = result;
  cacheHead = (uint8_t)((cacheHead + 1) % CACHE_N);
  if (cacheCount < CACHE_N) cacheCount++;
}

// ─────────────────────────────────────────────────────────────────────────
// 송신 상태
// ─────────────────────────────────────────────────────────────────────────
static uint16_t      seqNo = 0;                 // §2.4 uint16 순환. 재부팅하면 0
static unsigned long lastStatusAt = 0;          // §3.4 타이머는 하나
static unsigned long lastSendEndAt = 0;
static uint16_t      sentOcc = 0xFFFF, sentRes = 0xFFFF;  // 아직 아무것도 안 보냈다는 표시
static bool          changePending = false;
static unsigned long changeAt = 0;

// tmask 도 변화 감지에 넣는다(REQ-0035 ②). 실제 tmask 는 10비트뿐이라 0xFFFF 를
// "필드 없음(=해제)" 표식으로 쓸 수 있다 — 어떤 실제 값과도 겹치지 않는다.
static const uint16_t TMASK_ABSENT = 0xFFFF;
static uint16_t       sentTmask = 0xFFFE;   // 실제 값·ABSENT 어느 쪽과도 다른 초기값

// ─────────────────────────────────────────────────────────────────────────
// 접속 상태 기계 (논블로킹 — loop() 를 막지 않는다)
// ─────────────────────────────────────────────────────────────────────────
static bool          netOnline = false;
static uint8_t       netStep = 0;
static unsigned long netStepAt = 0;
static uint16_t      netStepWait = 500;
static const uint8_t NET_STEP_N = 5;
static const uint16_t NET_WAIT[NET_STEP_N] = { 2500, 800, 9000, 800, 5000 };

// 무엇을 보냈는지 찍는다(REQ-0042 3순위). 이게 없으면 5초마다 CIPSTART 를 재시도하는지조차
// 로그로 확인할 수 없다.
#if DEBUG
#define DBG_NET(n, name) do { Serial.print(F("[NET] " n " " name)); Serial.println(); } while (0)
#else
#define DBG_NET(n, name) do {} while (0)
#endif

static void netSendStep(uint8_t s) {
  switch (s) {
    case 0: wifi.print(F("AT+RST\r\n"));        DBG_NET("0", "RST");      break;
    case 1: wifi.print(F("AT+CWMODE=1\r\n"));   DBG_NET("1", "CWMODE=1"); break;
    case 2: wifi.print(F("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n"));
                                                DBG_NET("2", "CWJAP");    break;
    case 3: wifi.print(F("AT+CIPMUX=0\r\n"));   DBG_NET("3", "CIPMUX=0"); break;
    case 4: wifi.print(F("AT+CIPSTART=\"TCP\",\"" SERVER_IP "\"," SERVER_PORT "\r\n"));
                                                DBG_NET("4", "CIPSTART"); break;
    default: break;
  }
}

// ─────────────────────────────────────────────────────────────────────────
// 체크섬 (§2.2)
// ─────────────────────────────────────────────────────────────────────────
static const char HEXD[] = "0123456789ABCDEF";

static uint8_t xorRange(const char* s, uint8_t n) {
  uint8_t x = 0;
  for (uint8_t i = 0; i < n; i++) x ^= (uint8_t)s[i];
  return x;
}

// 대문자 hex 만 받는다 (§2.2 "소문자를 쓰지 않는다")
static bool hexVal(char c, uint8_t* out) {
  if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0');      return true; }
  if (c >= 'A' && c <= 'F') { *out = (uint8_t)(c - 'A' + 10); return true; }
  return false;
}

// buf 에 담긴 len 바이트(체크섬 앞 쉼표까지)의 뒤에 대문자 2자리 체크섬을 붙인다
static uint8_t appendChecksum(char* buf, uint8_t len) {
  uint8_t ck = xorRange(buf, len);
  buf[len]     = HEXD[ck >> 4];
  buf[len + 1] = HEXD[ck & 0x0F];
  buf[len + 2] = '\0';
  return (uint8_t)(len + 2);
}

static bool checksumOk(const char* s, uint8_t len) {
  int lastComma = -1;
  for (uint8_t i = 0; i < len; i++) if (s[i] == ',') lastComma = (int)i;
  if (lastComma < 0) return false;
  if ((int)len - lastComma - 1 != 2) return false;            // 체크섬은 정확히 2자리
  uint8_t hi, lo;
  if (!hexVal(s[lastComma + 1], &hi)) return false;
  if (!hexVal(s[lastComma + 2], &lo)) return false;
  // 대상: 첫 바이트 ~ 체크섬 바로 앞 쉼표(포함) = 길이 lastComma+1
  return xorRange(s, (uint8_t)(lastComma + 1)) == (uint8_t)((hi << 4) | lo);
}

// ─────────────────────────────────────────────────────────────────────────
// 수신 — 줄 조립 (§6.2 1단계)
// ─────────────────────────────────────────────────────────────────────────
static void handleLine(char* s);

#if DEBUG
// 줄을 **보이지 않는 문자까지 보이게** 찍는다. 눈으로 같아 보여도 뒤에 공백이나 다른 바이트가
// 붙어 있으면 정확일치가 깨지므로, 인쇄 불가 문자는 \xHH 로 펴고 길이를 같이 낸다.
//   예) [AT] "CONNECT" (7)      [AT] "CONNECT\x20" (8)   ← 이 둘은 다르다
// (태그는 호출자가 먼저 찍는다 — F() 의 타입이 실기와 호스트 테스트에서 달라 인자로 못 넘긴다)
static void dbgLine(const char* s, uint8_t n) {
  Serial.print(F("\""));
  for (uint8_t i = 0; i < n; i++) {
    char c = s[i];
    if (c >= 32 && c <= 126) {
      Serial.print(c);
    } else {
      Serial.print(F("\\x"));
      Serial.print(HEXD[((uint8_t)c) >> 4]);
      Serial.print(HEXD[((uint8_t)c) & 0x0F]);
    }
  }
  Serial.print(F("\" ("));
  Serial.print(n);
  Serial.println(')');
}
#endif

static void feedRxChar(char c) {
#if DEBUG
  dbgRxBytes++;
#endif
  if (c == '\n') {
    if (rxOverflow) {
#if DEBUG
      // 넘쳐서 버린 줄도 **버렸다는 사실이 보여야 한다.** 조용히 사라지면
      // "아무것도 안 왔다"와 구분이 안 된다.
      Serial.print(F("[DROP-OVF] 줄이 ")); Serial.print(RX_CAP);
      Serial.println(F("바이트를 넘어 버렸다"));
#endif
      rxLen = 0; rxOverflow = false; return;
    }
    if (rxLen == 0) { return; }
    rxLine[rxLen] = '\0';
    uint8_t n = rxLen;
    rxLen = 0;                       // ★ 파싱 전에 먼저 비운다 — 아래에서 다시 채워질 수 있다
    if (inSend) {
      if (!pendReady) { memcpy(pendLine, rxLine, (size_t)n + 1); pendReady = true; }
      // 두 번째 줄은 버린다. 서버가 재전송하므로(§7.3) 잃지 않는다
    } else {
      memcpy(workLine, rxLine, (size_t)n + 1);
      handleLine(workLine);
    }
    return;
  }
  if (c == '\r') return;                                   // AT 응답의 CR. 명세는 CR 을 보내지 않는다
  if (rxLen >= RX_CAP - 1) { rxOverflow = true; return; }  // 넘치는 줄은 통째로 버린다
  rxLine[rxLen++] = c;
}

static void pumpSerialRaw(void) {
  while (wifi.available()) feedRxChar((char)wifi.read());
}

// ─────────────────────────────────────────────────────────────────────────
// 연결 사망 감지 (REQ-0049) — 통보가 안 올 때의 그물
//
// 실기 증상: 17분쯤 뒤 TCP 가 죽는데 아두이노가 모르고 계속 죽은 소켓에 쓴다.
// 결정적 단서는 **아두이노가 seq 1734 를 반복 전송하고 서버는 1733 에서 멈춘 것**이었다.
// `statusTick()` 은 성공했을 때만 seq 를 올리므로, 같은 seq 반복 = `sendStatus()` 가 계속 false
// = `waitForPrompt()` 가 매번 타임아웃이라는 뜻이다. **실패는 이미 정확히 감지되고 있었고
// 그 값을 버리고 있었을 뿐이다.**
//
// 구조적 원인: `netOnline = false` 가 되는 곳이 파일 전체에 `CLOSED` 하나뿐이었다.
// 그 통보가 안 오면 netTick() 이 첫 줄에서 빠져 **재접속을 아예 시도하지 않는다.**
//
// ── N(연속 실패 한계)을 3 으로 정한 근거 ──
// 명세 §3.4: **서버는 3.5초 무프레임이면 device.online=false 로 본다.**
// 즉 서버가 이미 우리를 죽었다고 볼 시점이면 우리도 그렇게 봐야 한다. 실패 1회의 주기는:
//   · 하트비트만 있을 때 : 대기 1000ms + 프롬프트 타임아웃 300ms  = 약 1300ms
//   · 변화가 밀려 있을 때: 백오프  500ms + 프롬프트 타임아웃 300ms = 약  800ms
// → 3회 연속이면 마지막 성공 프레임으로부터 **약 2.4초(변화 주도) ~ 3.9초(하트비트만)**.
//   서버의 3.5초 판정을 정확히 걸치는 구간이다.
//   2회(1.6~2.6초)는 서버가 아직 살아 있다고 보는 동안 링크를 스스로 끊는 것이고,
//   4회(3.2~5.2초)는 서버가 이미 포기한 뒤에도 죽은 소켓에 계속 쓰는 것이다.
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t SEND_FAIL_LIMIT = 3;
static uint8_t       sendFailStreak = 0;

static void goOffline(void) {
#if DEBUG
  Serial.print(F("[NET] 전송 "));
  Serial.print(sendFailStreak);
  Serial.println(F("회 연속 실패 → 오프라인 전환, 재접속 시도"));
#endif
  netOnline = false;
  sendFailStreak = 0;
  // ⚠ 여기서 멱등 캐시를 비우지 않는다. 이 판정은 **추정**이고, 링크가 실은 살아 있었다면
  //   재시도에 ALREADY CONNECTED 가 와서 그대로 복귀한다 — 그 경우 캐시를 비웠다면
  //   살아 있는 연결의 멱등성(REQ-0035 [18]-4)이 깨진다.
  //   캐시는 실제 연결 생명주기 신호인 CLOSED / CONNECT 에서만 비운다(REQ-0036).
  netStep = 4;                       // CIPSTART 부터 다시
  netStepAt = millis();
  netStepWait = 1000;
}

// 실패를 세는 곳은 **sendLine() 한 곳뿐이다.** 수신된 오류 문구로도 세면 한 번의 실패가
// 두 번 계수되어(sendLine 이 false + 오류 줄 도착) N 이 사실상 절반이 된다. 그래서 아래
// handleLine() 의 오류 문구 처리는 **세지 않고 로그만** 남긴다(link is not valid 는 예외).
static void noteSendResult(bool ok) {
  if (ok) { sendFailStreak = 0; return; }
  if (sendFailStreak < 255) sendFailStreak++;
#if DEBUG
  Serial.print(F("[NET] 전송 실패 "));
  Serial.print(sendFailStreak);
  Serial.print('/');
  Serial.println(SEND_FAIL_LIMIT);
#endif
  if (sendFailStreak >= SEND_FAIL_LIMIT) goOffline();
}

// ─────────────────────────────────────────────────────────────────────────
// 송신 — AT+CIPSEND 뒤 '>' 프롬프트를 실제로 기다린다
// ─────────────────────────────────────────────────────────────────────────
static bool waitForPrompt(void) {
  unsigned long t0 = millis();
  while (millis() - t0 < PROMPT_TIMEOUT_MS) {
    while (wifi.available()) {
      char c = (char)wifi.read();
      if (c == '>') return true;
      feedRxChar(c);                 // 대기 중 들어온 데이터도 버리지 않는다
    }
  }
  return false;                      // 포기는 정상 동작이다 — 다음 하트비트가 곧 온다
}

// line 은 LF 없는 문자열. LF 는 여기서 붙인다(전선 종단은 LF 하나 — §2.1)
static bool sendLine(const char* line) {
  if (!netOnline) return false;
  uint8_t len = (uint8_t)strlen(line);
  if (len == 0 || len > 63) return false;                  // §2.1 한 줄 최대 64바이트(LF 포함)

  inSend = true;
  while (millis() - lastSendEndAt < SEND_GAP_MS) pumpSerialRaw();   // 연속 CIPSEND 간격

  wifi.print(F("AT+CIPSEND="));
  wifi.print((unsigned int)(len + 1));                     // +1 = LF 도 전선에 나간다
  wifi.print(F("\r\n"));

  bool ok = waitForPrompt();
  if (ok) {
    wifi.write((const uint8_t*)line, (size_t)len);
    wifi.write('\n');
  }
  lastSendEndAt = millis();
  inSend = false;

#if DEBUG
  Serial.print(ok ? F("[TX] ") : F("[TX-DROP] "));
  Serial.println(line);
#endif
  // ★ REQ-0049: 이 결과가 유일한 연속 실패 신호다. 여기서만 센다.
  //   goOffline() 이 netOnline 을 내릴 수 있으므로 inSend 를 내린 **뒤**에 부른다.
  noteSendResult(ok);
  return ok;
}

// ─────────────────────────────────────────────────────────────────────────
// S — 상태 프레임 (§2.4)
// ─────────────────────────────────────────────────────────────────────────
static void bitsToStr(uint16_t mask, char* out11) {
  for (uint8_t i = 0; i < SLOT_N; i++) out11[i] = ((mask >> i) & 1) ? '1' : '0';
  out11[SLOT_N] = '\0';
}

// buf[64] 인 근거 (§2.1-6 · §2.5):
//   전선 한 줄은 LF 포함 최대 64B → 문자열은 63자 + NUL = 64. 즉 이 버퍼가 규격 상한과 정확히 같다.
//   실제 최장은 tmask 를 실은 S 프레임이고, devid 8자 기준 61B(LF 포함) = 60자 + NUL = **61바이트**.
//   → 여유 3바이트. 우리 devid 는 "P1"(2자)이라 실제로는 55B(LF 포함)까지만 나간다.
//   넘칠 일은 없지만 snprintf 반환값을 검사해 넘치면 프레임을 버린다(잘린 줄을 내보내지 않는다).
static bool sendStatus(void) {
  char buf[64];
  char occ[SLOT_N + 1], res[SLOT_N + 1];
  bitsToStr(occMask, occ);
  bitsToStr(resMask, res);

  int n;
  if (testArmed) {
    // §2.4 tmask — 무장 중에만 붙는 선택 필드. 각 비트 = 그 칸의 occupied 가 주입된 값인가.
    // occupied 에는 주입값이 이미 반영돼 있고, tmask 는 "그게 진짜인가"만 알려준다.
    char tm[SLOT_N + 1];
    bitsToStr(ovrActive, tm);
    n = snprintf(buf, sizeof(buf), "S,%u,%s,%s,%lu,%s,%s,",
                 (unsigned int)seqNo, occ, res,
                 (unsigned long)(millis() / 1000UL), DEVICE_ID, tm);
  } else {
    // 해제 상태면 필드를 통째로 생략한다(옛 형식과 같다 — §2.4 "필드 없음 = 해제").
    n = snprintf(buf, sizeof(buf), "S,%u,%s,%s,%lu,%s,",
                 (unsigned int)seqNo, occ, res,
                 (unsigned long)(millis() / 1000UL), DEVICE_ID);
  }
  if (n <= 0 || (unsigned)n + 3 > sizeof(buf)) return false;
  appendChecksum(buf, (uint8_t)n);
  return sendLine(buf);
}

// ─────────────────────────────────────────────────────────────────────────
// A — ACK (§2.4). R 과 C 둘 다에 대한 응답이다
// ─────────────────────────────────────────────────────────────────────────
static bool sendAck(uint16_t rid, char s0, char s1, uint8_t result) {
  char buf[24];
  int n = snprintf(buf, sizeof(buf), "A,%u,%c%c,%u,",
                   (unsigned int)rid, s0, s1, (unsigned int)result);
  if (n <= 0 || (unsigned)n + 3 > sizeof(buf)) return false;
  appendChecksum(buf, (uint8_t)n);
  return sendLine(buf);
}

// ─────────────────────────────────────────────────────────────────────────
// R / C 처리
// ─────────────────────────────────────────────────────────────────────────
static bool parseU16(const char* s, uint16_t* out) {
  if (!s || !*s) return false;
  unsigned long v = 0;
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10UL + (unsigned long)(*p - '0');
    if (v > 65535UL) return false;
  }
  *out = (uint16_t)v;
  return true;
}

// workLine 을 그 자리에서 쪼갠다(쉼표 → NUL)
static uint8_t splitFields(char* s, char* out[], uint8_t maxF) {
  uint8_t n = 0;
  out[n++] = s;
  for (char* p = s; *p; p++) {
    if (*p == ',') {
      *p = '\0';
      if (n >= maxF) return 0xFF;
      out[n++] = p + 1;
    }
  }
  return n;
}

// ─────────────────────────────────────────────────────────────────────────
// T — 테스트 모드 제어 (§2.4 / §12A, 개정 3). 필드: T,rid,top,slot,tval,cksum
//   top=A 무장 · D 해제(전 칸 소멸) · S 주입 · X 그 칸만 해제
// 결과를 (s0,s1,result) 로 돌려준다. 호출자가 ACK 를 만들고 멱등 캐시에 넣는다.
// ─────────────────────────────────────────────────────────────────────────
static void processTest(char* f[], char* s0, char* s1, uint8_t* result) {
  *s0 = '?';                        // A/D 의 ACK 는 slot 이 ?? 다. 오류일 때도 ?? 다
  *s1 = '?';

  char top = f[2][0];
  if (f[2][1] != '\0') { *result = 3; return; }          // top 은 한 글자

  if (top == 'A' || top == 'D') {
    if (top == 'A') {
      testArmed = true;
    } else {
      // §12A.2 "현실로 복귀"는 하나의 동작이어야 한다 — 칸마다 따로 풀게 만들지 않는다
      testArmed = false;
      slotOverrideClearAll();
    }
    // ⚠ 무장/해제는 시뮬레이터에 아무 영향이 없다(§12B.3). 시뮬은 자율 전진을 하지 않으므로
    //   멈출 것도 재개할 것도 없다. REQ-0043 의 "무장 중 시뮬 정지"는 여기서 사라졌다.
    *result = 0;
    return;
  }

  if (top != 'S' && top != 'X') { *result = 3; return; }  // 모르는 top

  // 여기부터 S / X — 자리 ID 가 필요하다
  const char* slotTok = f[3];
  const char* tval    = f[4];
  uint8_t idx = (strlen(slotTok) == 2) ? slotIndexOf(slotTok[0], slotTok[1]) : 0xFF;
  if (idx == 0xFF) { *result = 3; return; }               // slot 은 ?? 로 남는다

  // 값 검사를 무장 검사보다 **먼저** 한다. 깨진 프레임은 장치 상태와 무관하게 깨진 프레임이다.
  if (top == 'S' && ((tval[0] != '0' && tval[0] != '1') || tval[1] != '\0')) {
    *result = 3;                                          // slot 은 ?? 로 남는다 (§2.4 result=3 규칙)
    return;
  }

  // 프레임이 성립했으므로 이제 ACK 에 그 자리를 담는다
  *s0 = slotCol(idx);
  *s1 = slotRow(idx);

  // §12A.2 무장하지 않은 채 S/X 가 오면 조용히 무시하지 않고 result=4 로 거절한다
  if (!testArmed) { *result = 4; return; }

  if (top == 'S') slotOverrideSet(idx, (uint8_t)(tval[0] - '0'));
  else            slotOverrideClear(idx);
  *result = 0;
}

static void processCommand(char* cand) {
  char*   f[7];
  uint8_t nf = splitFields(cand, f, 7);
  if (nf == 0xFF) return;

  char type = f[0][0];
  //  R,rid,slot,userid,cksum (5) / C,rid,slot,cksum (4)
  //  T,rid,top,slot,tval,cksum (6) / M,rid,cksum (3)
  uint8_t want;
  if      (type == 'R') want = 5;
  else if (type == 'T') want = 6;
  else if (type == 'M') want = 3;
  else                  want = 4;

  uint16_t rid;
  if (nf < 3 || !parseU16(f[1], &rid)) return;   // rid 를 모르면 ACK 를 만들 수 없다 → 버린다

  // §4.2 멱등: 이미 본 rid 면 상태를 다시 바꾸지 말고 같은 ACK 를 다시 보낸다
  int8_t hit = cacheFind(rid);
  if (hit >= 0) {
#if DEBUG
    Serial.print(F("[DUP rid] ")); Serial.println(rid);
#endif
    sendAck(cache[hit].rid, cache[hit].slot[0], cache[hit].slot[1], cache[hit].result);
    return;
  }

  uint8_t result;
  char s0, s1;

  if (nf != want) {
    // 필드 개수가 안 맞으면 해석 불가 — 타입과 무관하게 result=3
    s0 = '?'; s1 = '?'; result = 3;
#if DEBUG
    Serial.print(F("[BAD FIELDS] rid=")); Serial.println(rid);
#endif
    cachePut(rid, s0, s1, result);
    sendAck(rid, s0, s1, result);
    return;
  }

  if (type == 'T') {
    processTest(f, &s0, &s1, &result);
    cachePut(rid, s0, s1, result);       // §4.2 멱등은 T 에도 그대로 적용된다
    sendAck(rid, s0, s1, result);
    return;
  }

  if (type == 'M') {
    // §12B.4 시뮬 한 걸음. **무장 여부로 막지 않는다** — 테스트 모드와 별개다(§12B.3).
    // 멱등이 특히 중요하다: 재전송이 새 걸음으로 처리되면 한 번 눌렀는데 두 칸이 바뀐다.
    // (위쪽 cacheFind 가 이미 걸러 준다 — M 도 R/C/T 와 같은 기계장치를 탄다.)
    uint8_t idx = simStep();
    if (idx == 0xFF) {
      s0 = '?'; s1 = '?'; result = 5;    // 바꿀 시뮬 칸이 없다
#if DEBUG
      Serial.println(F("[SIM] 바꿀 시뮬 칸이 없다 → result=5"));
#endif
    } else {
      s0 = slotCol(idx); s1 = slotRow(idx); result = 0;
#if DEBUG
      Serial.print(F("[SIM] 한 걸음: ")); Serial.print(s0); Serial.print(s1);
      Serial.print(F(" → occupied="));
      Serial.println((simOcc >> idx) & 1);
#endif
    }
    cachePut(rid, s0, s1, result);
    sendAck(rid, s0, s1, result);
    return;
  }

  // ── 여기부터 R / C ──
  const char* slotTok = f[2];
  uint8_t idx = (strlen(slotTok) == 2) ? slotIndexOf(slotTok[0], slotTok[1]) : 0xFF;

  if (idx == 0xFF) {
    // §2.4 result=3 — 잘못된 자리 ID / 해석 불가.
    // REQ-0020 ② 판정: slot ::= ("A"/"B")("1".."5") / "??". 고정 표식을 쓰고 ACK 를 생략하지 않는다.
    // 되비추지 않는 이유: 상관 키는 rid 이고, 서버가 §4.1 매핑표에 원래 자리를 이미 들고 있다.
    // 침묵하지 않는 이유: 재전송 3회·4.5초를 태울 뿐 아니라, 의도적 거절과 링크 장애를
    //                     서버가 구분할 수 없게 되어 "센서가 죽었다"고 오해한다.
    s0 = '?';
    s1 = '?';
    result = 3;
#if DEBUG
    Serial.print(F("[BAD SLOT] rid=")); Serial.println(rid);
#endif
  } else {
    s0 = slotCol(idx);
    s1 = slotRow(idx);
    uint16_t bit = (uint16_t)1 << idx;

    if (type == 'R') {
      if (occMask & bit)      result = 1;              // 이미 점유
      else if (resMask & bit) result = 2;              // 이미 예약
      else {
        resMask |= bit;
        result = 0;
        // ★ occupied=1,reserved=1 경로(§1.1 마지막 행)는 이제 여기서 만들지 않는다.
        //   예전에는 예약이 잡히면 몇 초 뒤 시뮬이 그 칸에 차를 넣었다(ARRIVE_*).
        //   자율 전진이 없어졌으므로 §12B.2 의 **"예약된 빈칸 우선"** 규칙이 그 일을 한다 —
        //   다음 시뮬 트리거가 이 칸을 가장 먼저 채운다. simStep() 1순위가 그것이다.
      }
    } else {                                            // 'C' — 취소
      resMask &= (uint16_t)~bit;                        // 예약을 끄는 유일한 경로 (§7.4)
      result = 0;
    }
  }

  cachePut(rid, s0, s1, result);
  sendAck(rid, s0, s1, result);
}

// ─────────────────────────────────────────────────────────────────────────
// 한 줄 처리 — §6.2 의 4단계
// ─────────────────────────────────────────────────────────────────────────
// 대소문자 무시 n바이트 비교
static bool eqNoCase(const char* a, const char* b, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    char x = a[i], y = b[i];
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return false;
  }
  return true;
}

// TCP 접속 성공 줄인가? (REQ-0042 2순위 — 판정을 넓히되 옛 버그는 되살리지 않는다)
//
// ⚠ **부분문자열 검색으로 되돌리지 마라.** 옛 원본은 원시 스트림에서 "CONNECT" 를 찾아서
//    `WIFI CONNECTED` 안의 것에 걸렸다. 그건 와이파이 연결일 뿐 TCP 접속이 아니다 —
//    그 오인 때문에 "붙지도 않았는데 online" 이 되고 프레임이 허공에 나간다.
//    여기서는 (1) 호출 전에 WIFI 로 시작하는 줄을 걸러내고 (2) **길이를 정확히 맞춰** 판정한다.
//    길이 검사가 핵심이다 — "CONNECTED"(9)는 "CONNECT"(7)와 길이가 달라 통과하지 못한다.
//
// 받아들이는 변형과 근거:
//   "CONNECT"      표준 AT 펌웨어(ESP8266 AT ≥ 0.50)의 CIPSTART 성공 응답
//   "Linked"       구형 AT 펌웨어(0.2x 대)가 같은 자리에서 내는 응답 — 대소문자 무시
//   "<n>,CONNECT"  CIPMUX=1 형식의 링크ID 접두. 우리는 CIPMUX=0 이지만 모듈 상태가
//                  남아 있을 수 있어 벗겨 준다
//   앞뒤 공백/CR   모듈에 따라 붙어 온다
static bool isConnectLine(const char* s) {
  while (*s == ' ' || *s == '\t') s++;                       // 앞 공백
  if (s[0] >= '0' && s[0] <= '4' && s[1] == ',') s += 2;     // "<링크ID>," 접두
  uint8_t n = (uint8_t)strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;  // 뒤 공백

  if (n == 7 && eqNoCase(s, "CONNECT", 7)) return true;
  if (n == 6 && eqNoCase(s, "LINKED",  6)) return true;
  return false;
}

static void handleLine(char* s) {
#if DEBUG
  // ★ REQ-0042 1순위: **받은 줄을 전부 찍는다.** 예전에는 WIFI 로 시작하는 줄만 찍어서
  //   모듈이 실제로 무엇을 응답하는지 아무도 볼 수 없었다.
  dbgLineCnt++;
  Serial.print(F("[AT] "));
  dbgLine(s, (uint8_t)strlen(s));
#endif

  // (a) 접속 상태 키워드. "WIFI CONNECTED" 를 TCP CONNECT 로 오인하지 않는다.
  //     ⚠ 이 제외는 되살리면 안 되는 버그를 막는 자리다 — 지우지 마라.
  if (strncmp(s, "WIFI", 4) == 0) return;
  // ── 멱등 캐시를 비우는 지점이 아래 **두 곳**이다. 중복처럼 보이지만 지우지 마라. ──────
  //
  // 지켜야 할 성질: 서버가 재시작하면 wire_rid 가 1부터 다시 시작하므로, 옛 세션의 rid 가
  // 캐시에 남아 있으면 새 서버의 명령이 "재수신"으로 삼켜진다. 그것도 result=0 이라
  // **성공으로 보인다** — 오류도 타임아웃도 안 난다(REQ-0032).
  //
  // 왜 CLOSED 가 주 방어선인가:
  //   재접속은 netOnline==false 를 요구하고(netTick 첫 줄), netOnline 이 런타임에 false 가 되는
  //   곳은 아래 CLOSED 분기 하나뿐이다. 즉 **재접속이 실제로 일어났다면 CLOSED 는 반드시
  //   탐지된 것이다.** CLOSED 문구가 틀리면 재접속 자체가 안 되므로(= 눈에 보이는 고장)
  //   스테일 캐시가 생길 조건이 애초에 만들어지지 않는다.
  //
  // 왜 CONNECT 쪽도 남기는가:
  //   부팅 후 첫 연결은 CLOSED 를 거치지 않는다(그때 캐시는 어차피 비어 있지만 무해하다).
  //   그리고 이중 방어다 — 한쪽 문구 가정이 깨져도 다른 쪽이 받는다.
  //   비우는 비용은 정수 두 개를 0 으로 되돌리는 것뿐이라 중복이 손해가 아니다.
  //
  // 왜 ALREADY CONNECTED 에서는 비우면 안 되는가:
  //   그건 **기존 연결이 그대로 살아 있다**는 응답이다(서버도 그대로다). netTick() 이 CIPSTART 를
  //   5초마다 재시도하므로, 여기서 비우면 살아 있는 연결의 멱등성이 재시도마다 깨진다.
  // ─────────────────────────────────────────────────────────────────────────
  if (strstr(s, "ALREADY CONNECT")) {                                 // 새 연결이 아니다 → 캐시 유지
    netOnline = true;
#if DEBUG
    Serial.println(F("[NET] online (ALREADY CONNECTED)"));
#endif
    return;
  }
  if (isConnectLine(s)) {
    netOnline = true;
    cacheClear();
#if DEBUG
    Serial.println(F("[NET] online (CONNECT) + 캐시 비움"));
#endif
    return;
  }
  if (strstr(s, "CLOSED")) {
    netOnline = false;
    sendFailStreak = 0;
    cacheClear();                     // ★ 주 방어선 — 위 주석 참조
    netStep = 4; netStepAt = millis(); netStepWait = 1000;   // CIPSTART 만 다시
    return;
  }

  // ── REQ-0049 ② 송신 오류 응답 (보조 경로) ────────────────────────────────
  // 온라인 중에 우리가 보내는 AT 명령은 **`AT+CIPSEND=` 하나뿐**이다
  // (netTick() 이 `if (netOnline) return;` 으로 시작하므로 온라인 중엔 다른 명령이 안 나간다).
  // 따라서 **온라인 중에 오는 오류 응답은 반드시 CIPSEND 에 대한 것**이다 — 그래서 여기서
  // 판정해도 "관계없는 AT 실패를 연결 문제로 오인"하는 사고가 구조적으로 생기지 않는다.
  //
  // 다만 세기는 하지 않는다(위 noteSendResult 주석의 이중 계수 문제). 처리를 둘로 나눈다:
  //   · `link is not valid` → **즉시 오프라인.** 링크가 무효라고 모듈이 명시한 것이라 모호하지 않다
  //   · `SEND FAIL` / `ERROR` / `busy`  → **로그만.** 이 송신이 실패했다는 뜻이지 링크가 죽었다는
  //     확증은 아니다. 대응되는 sendLine() 실패가 이미 카운터를 올리고 있으므로 3회면 잡힌다.
  //   ⚠ 실제 문구는 아직 추정이다. REQ-0042 의 [AT] 전체 로깅이 올라간 빌드에서 그 순간의
  //     로그가 오면 어느 문구인지 확정하고 좁힐 수 있다.
  if (netOnline) {
    if (strstr(s, "link is not valid")) {
#if DEBUG
      Serial.println(F("[NET] link is not valid → 즉시 오프라인"));
#endif
      netOnline = false;
      sendFailStreak = 0;
      netStep = 4; netStepAt = millis(); netStepWait = 1000;
      return;
    }
    if (strstr(s, "SEND FAIL") || strstr(s, "ERROR") || strstr(s, "busy")) {
#if DEBUG
      Serial.print(F("[NET] 송신 오류 응답(로그만, 카운터는 sendLine 이 센다): "));
      Serial.println(s);
#endif
      return;
    }
  }

  // (b) §6.2 2단계 — +IPD,<n>: 이 있으면 그 뒤부터가 후보
  char* cand = s;
  char* ipd = strstr(s, "+IPD,");
  if (ipd) {
    char* colon = strchr(ipd, ':');
    if (!colon) return;
    cand = colon + 1;
  }

  uint8_t len = (uint8_t)strlen(cand);
  if (len == 0 || len > 63) return;                    // §2.1 한 줄 최대 64바이트(LF 포함)

  // (c) 3단계 — 타입 문자. 모르는 타입은 조용히 버린다(§2.1-7)
  //     T 는 개정 3, M 은 개정 5 에서 추가됐다.
  //     둘 다 R/C 와 **같은 파서**를 탄다 — 따로 만들지 않는다(§2.4 · §12B.4)
  if (cand[0] != 'R' && cand[0] != 'C' && cand[0] != 'T' && cand[0] != 'M') return;

  // (d) 4단계 — 체크섬. AT 잡음이 우연히 R 로 시작해도 여기서 걸린다
  if (!checksumOk(cand, len)) {
#if DEBUG
    Serial.print(F("[CKSUM NG] ")); Serial.println(cand);
#endif
    return;
  }

  processCommand(cand);
}

static void drainPending(void) {
  if (!pendReady) return;
  memcpy(workLine, pendLine, RX_CAP);
  pendReady = false;
  handleLine(workLine);
}

// ─────────────────────────────────────────────────────────────────────────
// 주기 처리
// ─────────────────────────────────────────────────────────────────────────
static void netTick(unsigned long now) {
  if (netOnline) return;
  if (now - netStepAt < netStepWait) return;

  if (netStep < NET_STEP_N) {
    netSendStep(netStep);
    netStepWait = NET_WAIT[netStep];
    netStepAt = now;
    netStep++;
    return;
  }
  netStep = 4;                                   // CIPSTART 부터 다시 시도
  netStepAt = now;
  netStepWait = 0;
}

static void sensorTick(void) {
  uint16_t m = 0;
  for (uint8_t i = 0; i < SLOT_N; i++) if (readSlotSensor(i)) m |= (uint16_t)1 << i;
  occMask = m;
}

static void statusTick(unsigned long now) {
  // §12A.4 무장 여부의 진실은 tmask 다. 그래서 tmask 변화도 occupied/reserved 와 **같은 경로**로
  // 즉시 전송을 트리거해야 한다. 안 그러면 무장 직후 최대 1초 동안 화면이 무장 사실을 모르고,
  // 그 사이에 주입이 들어오면 **주입값이 경고 없이 그려지는 프레임**이 생긴다(§12A.6 위반).
  // 비용은 사실상 0 이다 — 무장·해제·주입은 사람이 누르는 드문 사건이라 전송 횟수가 늘지 않는다.
  uint16_t tmaskNow = testArmed ? ovrActive : TMASK_ABSENT;

  bool changed = (occMask != sentOcc) || (resMask != sentRes) || (tmaskNow != sentTmask);
  if (changed && !changePending) { changePending = true; changeAt = now; }
  if (!changed) changePending = false;

  bool heartbeatDue = (now - lastStatusAt >= HEARTBEAT_MS);
  // changeAt 은 전송 실패 시 **미래 시각**으로 밀린다. unsigned 뺄셈으로 비교하면
  // 언더플로로 곧장 참이 되어 백오프가 통째로 무력화된다 → 부호 있는 비교로 본다.
  bool debounced    = changePending && ((long)(now - changeAt) >= (long)DEBOUNCE_MS);
  if (!netOnline || !(heartbeatDue || debounced)) return;

  uint16_t occSnap = occMask, resSnap = resMask, tmaskSnap = tmaskNow;
  bool ok = sendStatus();

  lastStatusAt = millis();          // §3.4 어떤 이유로든 S 를 보내면 타이머 리셋 (타이머는 하나)
  if (ok) {
    seqNo++;                        // 나가지 못한 프레임은 번호를 소비하지 않는다
    sentOcc = occSnap;
    sentRes = resSnap;
    sentTmask = tmaskSnap;
    changePending = false;
  } else {
    changeAt = lastStatusAt + SEND_FAIL_BACKOFF_MS - DEBOUNCE_MS;   // 실패 후 재시도 간격 확보
  }
}

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  wifi.begin(9600);                 // §3.3 AT+UART_DEF 로 보율을 바꾸지 않는다

  // 시드는 A1 에서 뽑는다 — 어디에도 안 물린 핀이라야 노이즈가 나온다.
  // (A0 은 자리 B5 의 센서 입력으로 배정했으므로 쓰면 안 된다.)
  randomSeed((unsigned long)analogRead(A1) ^ micros());

  // 실물로 지정된 칸만 입력 모드를 잡는다. 시뮬 칸의 핀은 건드리지 않는다.
  for (uint8_t i = 0; i < SLOT_N; i++) applySlotPinMode(i);

  // §12A.3 재부팅하면 테스트 오버라이드는 사라진다 — 서버가 재하달하지 않는다(예약과 정반대).
  // 전역이라 어차피 0 이지만, "여기서 버린다"는 것을 코드로 남겨 둔다.
  testArmed = false;
  slotOverrideClearAll();

  // 시작 시 몇 칸은 차 있는 편이 주차장답다: A2, A3, B4
  // 이 값은 **트리거를 받기 전까지 그대로 유지된다**(§12B.1 — 자율 전진 없음).
  simOcc = (uint16_t)((1U << 1) | (1U << 2) | (1U << 8));

  netStep = 0;
  netStepAt = millis();
  netStepWait = 500;

#if DEBUG
  Serial.println(F("\n[PARKING NODE] proto v1 / 10 slots / dev=" DEVICE_ID));
#endif
}

#if DEBUG
// 오프라인인 동안 3초마다 한 줄. **이 한 줄이 원인을 셋으로 가른다**(REQ-0042):
//   rx=0                 → ESP→Uno 로 바이트가 아예 안 온다. 배선(D7)·레벨·모듈 전원을 봐라
//   rx>0, lines=0        → 바이트는 오는데 줄이 안 끊긴다. 줄 종단이 LF 가 아닐 수 있다
//   lines>0, online=0    → 줄은 오는데 접속 문구를 못 알아본다. [AT] 로그에서 실제 문구를 봐라
// 셋 중 무엇인지 모르는 채로 고치면 또 빗나간다.
static void diagTick(unsigned long now) {
  if (netOnline) return;
  if (now - dbgLastDiag < DIAG_PERIOD_MS) return;
  dbgLastDiag = now;
  Serial.print(F("[DIAG] offline step="));  Serial.print(netStep);
  Serial.print(F(" rx="));                  Serial.print(dbgRxBytes);
  Serial.print(F(" lines="));               Serial.print(dbgLineCnt);
  Serial.print(F(" up="));                  Serial.print(now / 1000UL);
  Serial.println(F("s"));
}
#endif

void loop() {
  unsigned long now = millis();
  netTick(now);
  pumpSerialRaw();
  drainPending();
  sensorTick();
  statusTick(now);
#if DEBUG
  diagTick(now);
#endif
}
