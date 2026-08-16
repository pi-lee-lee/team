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
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>

// ⚠ `#ifndef` 로 감싼 이유 (REQ-0116): 후보 ④(진단 출력이 AT 타이밍을 미는가)를 재려면
//   **소스를 고치지 않고** `DEBUG=0` 빌드를 만들 수 있어야 한다. 소스를 고쳐 가며 구우면
//   두 팔의 차이가 DEBUG 뿐이라는 보장이 깨진다.
//     arduino-cli compile --build-property "compiler.cpp.extra_flags=-DDEBUG=0" ...
//   기본값은 1 그대로이므로 평소 빌드는 바이트가 변하지 않는다(확인함).
#ifndef DEBUG
#define DEBUG 1
#endif

// ─────────────────────────────────────────────────────────────────────────
// 부팅 원인 기록 (REQ-0071 사실 4)
//
// 지난 소크에서 아두이노가 2회 리셋됐는데(up 4149s→3s, 201s→3s) **왜 리셋됐는지 아무도
// 몰랐다.** 브라운아웃인지, 워치독인지, 사람이 USB 를 건드린 것인지 로그로 가릴 수단이
// 없어서 "브라운아웃 부트루프"라는 추측만 남았다. 그 추측을 **다음 판에는 사실로 바꾼다.**
//
// MCUSR 은 리셋 원인 비트를 들고 있지만 **부트로더가 지우고 갈 수 있다.** 그래서 앱 코드가
// 아니라 `.init3`(스택은 잡혔고 .data/.bss 초기화 전)에서 가장 먼저 복사한다.
// 복사본은 `.noinit` 에 둬야 한다 — 일반 전역이면 그 뒤 .bss 초기화가 0 으로 덮어쓴다.
//
// ⚠ optiboot 는 MCUSR 을 읽고 **0 으로 지운 뒤** 앱으로 넘어간다. 그 경우 여기서 0 이 읽히고
//   원인을 알 수 없다. 그래서 0 을 "원인 없음"이 아니라 **"부트로더가 지웠다(불명)"** 으로
//   찍는다. 없는 정보를 있는 것처럼 말하지 않기 위해서다.
//
// 곁다리로 `wdt_disable()` 도 여기서 한다. 워치독 리셋으로 들어왔다면 WDT 가 켜진 채
// 짧은 주기로 남아 있을 수 있는데, 그대로 두면 부팅을 마치기 전에 또 리셋된다(벽돌 루프).
// 6단(ENABLE_WDT)을 켜지 않아도 무해하므로 항상 넣어 둔다 — 방어는 켜기 전부터 있어야 한다.
// ─────────────────────────────────────────────────────────────────────────
uint8_t mcusrMirror __attribute__((section(".noinit")));
void earlyInitCapture(void) __attribute__((naked, used, section(".init3")));
void earlyInitCapture(void) {
  mcusrMirror = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

// ── 6단: AVR 워치독 — **기본값 꺼짐. 이유를 읽고 나서 켜라.** ──────────────────
// 무엇을 고치는가: **아두이노 자신이 행(hang)에 빠졌을 때만**이다.
// 무엇을 못 고치는가: **ESP 다.** AVR 리셋은 ESP 모듈을 리셋하지 않는다(전원도 RST 도 그대로다).
//   이번 REQ-0071 에서 관측된 고장은 전부 ESP 쪽이었으므로 이 단의 기대효과는 낮다.
//
// 반면 위험은 실재한다: WDT 를 안전하게 처리하지 못하는 옛 부트로더(ATmegaBOOT 계열)에서는
//   워치독 리셋이 **부트로더가 앱으로 넘어가기 전에 또 리셋을 걸어** 무한 리셋이 된다.
//   그 상태는 ISP 프로그래머 없이는 되돌릴 수 없다 — 즉 보드가 죽는다.
//
// 이 보드가 안전한가: Uno 는 optiboot 를 쓰고 optiboot 는 WDT 안전이다. arduino-cli 가
//   115200 으로 업로드에 성공한 것이 optiboot 라는 정황 증거다. **그러나 확인하지 않았다.**
//   낮은 기대효과와 벽돌 위험을 맞바꾸지 않는다 — 그래서 잠가 둔다.
//
// 켜기 전 확인 절차(여유 있을 때):
//   1) 아래를 1 로 바꾸고 굽는다 → 정상 부팅하고 [BOOT] 줄이 나오는지 본다
//   2) loop() 를 9초 막는 코드를 일부러 넣어 리셋되는지, **그 뒤 정상 부팅하는지** 본다
//   2)에서 다시 안 올라오면 ISP 가 필요하다. 그래서 2)는 예비 보드로 하는 것이 맞다.
#define ENABLE_WDT 0

// ─────────────────────────────────────────────────────────────────────────
// 배선 · 네트워크 상수
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t PIN_ESP_RX = 8;   // ESP TX → Uno
static const uint8_t PIN_ESP_TX = 7;   // Uno → ESP RX
SoftwareSerial wifi(PIN_ESP_RX, PIN_ESP_TX);

#define WIFI_SSID    "SK_WiFiGIGA50DC_2.4G"   // 2026-08-15 새 AP 로 이전(옛 공유기가 원인이었다)
#define WIFI_PASS    "2011050796"
// ⚠⚠ **앞뒤 공백을 절대 넣지 마라.** 구운 펌웨어에 `" 192.168.35.21"` 로 앞 공백이 들어가 있었고,
//    그래서 ESP 가 IP 리터럴로 못 읽고 **호스트명으로 해석해 `DNS Fail`** 을 냈다.
//    (기기 플래시를 읽어 확인한 실물 문자열: AT+CIPSTART="TCP"," 192.168.35.21",9991)
//    이 매크로는 그대로 AT 명령에 이어붙으므로 공백 하나가 곧 고장이다.
#define SERVER_IP    "192.168.35.21"   // §11 — 명세는 주소를 가정하지 않는다. 현장에서 바꾼다
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
// RX_CAP 을 72 → 96 으로 올린 근거 (REQ-0064):
//   수신 줄은 **`+IPD` 접두를 포함한 통짜**로 이 버퍼에 담긴 뒤에야 접두가 벗겨진다.
//   · `+IPD,63:`  (8자) + 프레임 63자 = 71 = 옛 상한(RX_CAP-1)과 **정확히 같다. 여유 0.**
//   · `+IPD,0,63:`(10자) + 63자 = 73 → **넘쳐서 프레임이 통째로 버려진다.**
//   ✅ 2026-08-16 17:23 **실측으로 답이 나왔다** — 더 이상 가정이 아니다(REQ-0115, socket 확인):
//        [AT] "+IPD,19:R,1,A1,00000000,13"
//      → **`+IPD,<n>:` 첫 번째 형식**이다. 연결 id 가 붙지 않는다.
//      근거: 부팅 사다리가 `AT+CIPMUX=0`(단일 연결)을 넣기 때문이다.
//      즉 **넘침 시나리오는 지금 구성에서 발생하지 않는다.** 하행 5/5 도달·ACK 5/5 로 확인됐다.
//   ⚠ **단, "CIPMUX=0 인 지금 구성에서"라는 조건이 붙는다.** 멀티 연결(`CIPMUX=1`)로 바꾸면
//      ESP 는 `+IPD,<id>,<n>:` 형식으로 바꿔 보내고, 그러면 위 두 번째 줄의 계산대로
//      **73바이트가 되어 옛 상한이었다면 프레임이 통째로 버려졌을 것**이다.
//      RX_CAP=96 은 그 경우까지 덮는다 — 그래서 이 여유는 그대로 둔다. 줄이지 마라.
//   비용은 버퍼 3개 × 24B = 72B 이고 RAM 여유는 1100B 이상이다. 이 교환은 명백히 남는다.
//   ⚠ 여유가 실제로 얼마인지는 아래 ramLow 계측이 소크에서 답한다 — 추정으로 두지 않는다.
static const uint8_t RX_CAP = 96;
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
// RAM 여유 계측 (REQ-0064) — **버퍼 크기 판단을 추정이 아니라 관측으로 하기 위해서다**
//
// RX_CAP 을 72→96 으로 올릴 때 "RAM 여유가 충분하다"는 근거가 필요했는데, 컴파일러가 알려 주는
// 887B 는 **정적 사용량**일 뿐 실행 중 스택이 얼마나 깊이 내려가는지는 말해 주지 않는다.
// (`-fstack-usage` 를 붙여 봤으나 이 툴체인에서는 .su 파일이 비어 나왔다.)
//
// 그래서 **가장 깊은 호출 지점에서 직접 잰다.** 힙을 쓰지 않으므로(String·malloc 없음)
// `__heap_start` 부터 현재 스택 포인터까지가 그대로 미사용 영역이다.
// ⚠ 정확히 말하면 이것은 "계측을 심은 지점에서 관측된 최저 여유"이지 이론적 최악값이 아니다.
//   그래도 추정보다 훨씬 낫고, 2시간 소크 동안 실제 최악에 매우 가까워진다.
// ─────────────────────────────────────────────────────────────────────────
extern uint8_t __heap_start;
static uint16_t ramLow = 0xFFFF;

static void ramProbe(void) {
  uint8_t here;
  uint16_t freeNow = (uint16_t)(&here - (uint8_t*)&__heap_start);
  if (freeNow < ramLow) ramLow = freeNow;
}

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

// ─────────────────────────────────────────────────────────────────────────
// REQ-0064 — **타이머 구동에서 응답 구동으로.**
//
// 실기에서 step=4 정체 + `no ip` 무한반복이 났다. 실측 로그가 원인을 정확히 보여 줬다:
//
//   [NET] 2 CWJAP
//   [AT] "AT+CWJAP="unknowns_network2.4","..."" (55)
//   [DIAG] offline step=3 rx=170 ... up=6s      ← 응답 없음
//   [DIAG] offline step=3 rx=170 ... up=9s      ← 응답 없음 (rx 가 12초간 고정)
//   [NET] 3 CIPMUX=0
//   [AT] "busy p..." (9)                        ← ★ 거부됐다
//   [NET] 4 CIPSTART
//   [AT] "busy p..." (9)                        ← ★ 또 거부
//   [AT] "FAIL" (4)                             ← ★★ up≈20s, 이제야 CWJAP 의 결과
//   [AT] "no ip" (5)                            ← 이후 영원히 반복
//
// 읽어야 할 것 셋:
//   1. **이 펌웨어의 CWJAP 는 약 16초 걸린다.** NET_WAIT 의 9000ms 는 턱없이 짧았다.
//   2. 그 사이 사다리가 타이머만 보고 전진해 CIPMUX·CIPSTART 를 쐈고 **전부 `busy p...` 로 거부**됐다.
//      → **`AT+CIPMUX=0` 은 한 번도 적용된 적이 없다.** `OK` 가 로그에 없다. 조용한 두 번째 고장이다.
//   3. `no ip` 는 원인이 아니라 **결과**다. 상류는 CWJAP 실패다.
//
// 그래서 각 단계는 이제 **응답이 올 때까지 기다린다.** NET_WAIT 는 "다음 단계로 가는 시각"이 아니라
// **"이만큼 기다려도 답이 없으면 재시도"** 라는 뜻으로 바뀌었다. 전진은 handleLine() 이
// netAdvance() 로 시킨다 — 응답을 본 쪽이 다음 단계를 정한다.
//
// ⚠ 이 보드의 펌웨어는 **구형 ai-thinker** 다([System Ready, Vendor:www.ai-thinker.com]).
//   ESP-AT v2.2 문서와 어휘가 다르다. 실측으로 확인된 차이:
//     · `WIFI CONNECTED` / `WIFI GOT IP` 를 **아예 내지 않는다**
//     · `+CWJAP:<n>` 사유 코드도 없다 — 성공은 `OK`, 실패는 `FAIL` 한 줄이 전부다
//   그래서 IP 확보 판정을 문구에 기대지 않고 **`AT+CIFSR` 로 직접 물어본다.**
//   이것이 펌웨어 세대와 무관하게 동작하는 유일한 방법이다. 신형 문구(GOT IP 등)도
//   같이 받아 두되, 그것만 믿지는 않는다.
// ─────────────────────────────────────────────────────────────────────────
enum {
  NET_RST = 0, NET_CWMODE, NET_CWJAP,
  NET_CIFSR,                          // ★ IP 를 실제로 받았는지 물어본다 (REQ-0064 ②)
  NET_CIPMUX, NET_CIPSTART,
  NET_CIPCLOSE,                       // 복구 전용 — 낡은 소켓을 닫는다(REQ-0051)
  // ── 아래 셋은 **진단 전용 사슬**이다. CWJAP 2회 실패 때 한 번만, 순서대로 지나간다.
  //    주 사다리(부팅·복구)는 여기를 절대 지나지 않는다 — 검증된 경로를 건드리지 않으려는 것이다.
  //      GMR → CWCOUNTRY → CWLAP → (CWJAP 재시도)
  NET_GMR,                            // 펌웨어 판 확정 — ecn 열거 범위와 CWCOUNTRY 지원 여부가 여기서 갈린다
  NET_CWCOUNTRY,                      // 규제도메인 1~13 우회 시도 (구형이면 ERROR — 그 자체가 결론이다)
  NET_CWLAP,                          // 주변 AP 목록 (REQ-0064 ⑤)
  // ⓘ `AT+CWJAP?`(질의형) 단계를 넣었다가 뺐다 — **이 펌웨어는 `ERROR` 로 답한다(실측).**
  //   미지원 명령을 매 실패마다 쏘면 로그만 더럽힌다. 같은 질문(결합했는가)은
  //   `AT+CIFSR` 이 실주소를 답하는지로 더 확실하게 답할 수 있다.
  NET_CWQAP,                          // ★ REQ-0071 2단 — 명시적 결합 해제 후 다시 붙는다
  NET_STEP_COUNT
};
// 각 단계의 **응답 대기 상한**. 넘으면 그 단계를 다시 시도한다(전진이 아니다).
//   CWJAP 30000 : 실측 16초 + 여유. 짧게 잡은 것이 이번 사고의 직접 원인이었다
//   CWLAP  9000 : 스캔은 채널을 훑으므로 오래 걸린다
//   CWQAP  3000 : 모듈 로컬 동작이라 왕복이 없다. 3초면 넉넉하다
static const uint16_t NET_WAIT[] = { 2500, 800, 30000, 2500, 1200, 8000, 800, 1500, 1500, 9000, 3000 };
static_assert(sizeof(NET_WAIT) / sizeof(NET_WAIT[0]) == NET_STEP_COUNT,
              "NET_WAIT 길이가 단계 수와 다르다 — 새 단계를 넣고 대기시간을 빠뜨렸다");

// 마지막으로 **실제로 쏜** 명령. netStep 은 이미 "답이 없을 때 갈 곳"으로 앞서 가 있으므로,
// 도착한 응답이 무엇에 대한 답인지는 이 변수로 판정해야 한다.
static uint8_t netLastSent = 0xFF;

static bool    netHasIp = false;      // ★ CIPSTART 의 전제조건 (REQ-0064 ②)
static uint8_t cwjapFails = 0;        // CWJAP 연속 실패 횟수 — 2회면 CWLAP 진단을 켠다
static uint8_t cifsrTries = 0;        // CIFSR 로 IP 를 못 본 횟수 — 3회면 CWJAP 로 되돌아간다
static bool    lapDone = false;       // CWLAP 은 **한 번만** 쏜다(스캔 중엔 접속을 못 한다)
static bool    lapFound = false;      // 그 스캔에서 우리 SSID 가 보였는가

static void netAdvance(uint8_t step, uint16_t wait) {
  netStep = step;
  netStepAt = millis();
  netStepWait = wait;
}

// ─────────────────────────────────────────────────────────────────────────
// 복구 사다리 상태 (REQ-0051)
//
// 실기 증상: 끊긴 뒤 `ALREADY CONNECTED` 만 무한 반복되고 연결이 안 된다. 원인은
// **CIPSTART 로는 ESP 의 낡은 소켓을 지울 수 없다**는 것이다. ESP 는 죽은 소켓을 아직
// 열려 있다고 믿고 CIPSTART 에 `ALREADY CONNECTED` 로 답하고, 그걸 온라인으로 받으면
//   goOffline → CIPSTART → ALREADY → online → 3회 실패 → goOffline → …
// 로 영원히 돈다. 감지(REQ-0049)는 맞았고 **복구가 반쪽이었다.**
//
// 그래서 사다리를 만든다: **CIPCLOSE → (CLOSED) → CIPSTART → 안 되면 AT+RST.**
//
// staleSocket: "ESP 가 낡은 소켓을 붙들고 있다고 의심한다". 전송 실패로 오프라인이 될 때 선다.
//   이 플래그가 서 있는 동안의 `ALREADY CONNECTED` 는 **"붙어 있다"가 아니라 정반대 신호**이므로
//   온라인으로 올리지 않는다. 실제 `CONNECT` 나 `CLOSED` 에서 내린다.
//   ⚠ 플래그가 없을 때(=초기 접속 경합)의 `ALREADY CONNECTED` 는 진짜로 "이미 붙었다"는 뜻이라
//     그대로 온라인으로 받고 **캐시도 비우지 않는다**(REQ-0035 [18]-4 불변식).
//
// CLOSE_ATTEMPT_LIMIT = 3 의 근거:
//   CIPCLOSE 는 **모듈 로컬 동작**이라 네트워크 왕복이 없다 — 정상 모듈이면 대기 창(800ms)
//   안에 반드시 응답한다. 즉 한 번 실패는 `busy p...` 같은 일시적 사정일 수 있지만
//   **세 번 연속 로컬 명령이 안 먹으면 그건 AT 계층 자체가 꼬인 것**이고, 그때 올바른
//   대응은 재시도가 아니라 모듈 리셋이다. 1회는 너무 이르고(일시적 busy 하나로 14초 RST 사이클),
//   더 크게 잡으면 무한 루프 시간만 길어진다. 3회면 약 2.5초 안에 사다리를 다 올라간다.
// ─────────────────────────────────────────────────────────────────────────
static bool    staleSocket = false;
static uint8_t closeAttempts = 0;
static const uint8_t CLOSE_ATTEMPT_LIMIT = 3;

// ═════════════════════════════════════════════════════════════════════════
// 복구 사다리 (REQ-0071) — **같은 자극을 반복하는 것은 사다리가 아니다**
//
// 실측이 말한 것: 45분 정상동작 뒤 한 번 불안해지자 **18분 동안 한 번도 복구하지 못했다.**
// 그동안 코드가 한 일은 CWJAP 재시도 91회(꼬리에서 19회 연속)가 전부였다.
// 라우터 관리화면이 이유를 설명했다 — **IP 할당까지는 매번 성공하고 약 12초 뒤 결합이
// 소실된다.** 즉 `CWJAP FAIL` 은 "AP 에 못 붙었다"가 아니라 **"붙어서 DHCP 까지 받았는데
// 12초 뒤 끊어졌다"** 이고, 그렇다면 **같은 CWJAP 를 다시 쏘는 것은 같은 12초를 다시
// 만드는 것**이다. 재시도 횟수를 늘려도 원리적으로 낫지 않는다. 단을 올려야 한다.
//
// 원칙 셋(REQ-0071):
//   1. 실패가 쌓이면 반드시 **더 강한 단**으로 올라간다 — 같은 자극 무한 반복 금지
//   2. 아래 단을 건너뛰지 않는다 — 건강한 모듈을 하드웨어 리셋으로 두들기지 않는다
//   3. 모든 단 전이를 시리얼 한 줄(`[LADDER]`)로 남긴다 — 어느 칸에서 죽었는지 로그로 재구성된다
//
//   단 0  계측만                        — 항상. CWJAP 소요시간·연속 실패수·**결합 유지시간**
//   단 1  시리얼 드레인 + CIFSR 재확인   — 쓰레기·동기 어긋남을 씻는다
//   단 2  AT+CWQAP 로 명시적 결합 해제 후 CWJAP
//   단 3  AT+RST (모듈 소프트 리셋)
//   단 4  ESP 하드웨어 리셋선            — 배선 필요(ESP_RST_WIRED). REQ-0063 ② 가 여기 들어왔다
//   단 5  ESP 전원 재투입                — **부품 미보유로 미구현.** 아래 applyRung() 참조
//   단 6  AVR 워치독                     — 파일 상단 ENABLE_WDT. REQ-0063 ① 이 여기 들어왔다
//
// ── 왜 단이 올라갈수록 쉬는 시간을 늘리는가 ──
// 근본원인 1순위 후보가 **전원**이다(ESP-01 송신 피크 300~430mA vs Uno 3.3V 핀 공급 50mA).
// 전원이 원인이라면 **쉬는 시간 자체가 조치**다 — 결합을 시도하지 않는 동안 RF 부하가 사라져
// 레귤레이터와 커패시터가 회복한다. 백오프는 예의가 아니라 치료의 일부다.
//   ⚠ 이것은 **정황이지 확정이 아니다.** 전원을 정상화한 뒤 12초 소실이 사라지는지 봐야 확정된다.
//
// ── 사다리를 언제 0단으로 되돌리는가 (이 규칙이 없으면 사다리가 무력해진다) ──
// **온라인이 되자마자 되돌리면 안 된다.** 관측된 고장은 "붙었다가 12초 뒤 끊김"의 반복이라,
// 붙는 순간 초기화하면 사다리는 영원히 0~1단을 오가며 절대 위로 못 올라간다 —
// 고치려는 바로 그 무한반복이 이름만 바꿔 되살아난다. 그래서 **연속 30초 온라인**을 요구한다.
// 30초는 관측된 12초 주기보다 넉넉히 위라, 깜빡이는 링크는 **증명 가능하게** 사다리를 못 내린다.
// ═════════════════════════════════════════════════════════════════════════
enum { RUNG_MEASURE = 0, RUNG_RESYNC, RUNG_CWQAP, RUNG_SOFTRST, RUNG_HWRST,
       RUNG_POWER, RUNG_MAX = RUNG_POWER };

// 그 단에서 몇 번 실패하면 위로 올라가는가.
//   0단 1회 : 첫 실패는 곧바로 씻어 본다. 가장 싼 조치라 늦출 이유가 없다
//   1~4단 2회: 한 번은 우연일 수 있지만 두 번이면 그 단으로는 안 되는 것이다
//              (REQ-0071 표의 "N단이 2회 실패하면 N+1단" 을 그대로 옮겼다)
//   5단 255 : 종착역이다. 더 올라갈 곳이 없다
static const uint8_t  RUNG_LIMIT[]      = {    1,    2,    2,     2,     2,   255 };
// 그 단으로 올라간 뒤 다음 시도까지 쉬는 시간(ms). REQ-0071 권고를 그대로 썼다.
static const uint16_t RUNG_BACKOFF_MS[] = {    0,    0, 5000, 15000, 30000, 60000 };
static_assert(sizeof(RUNG_LIMIT) / sizeof(RUNG_LIMIT[0]) == RUNG_MAX + 1,
              "RUNG_LIMIT 길이가 단 수와 다르다");
static_assert(sizeof(RUNG_BACKOFF_MS) / sizeof(RUNG_BACKOFF_MS[0]) == RUNG_MAX + 1,
              "RUNG_BACKOFF_MS 길이가 단 수와 다르다");

static uint8_t       rung = RUNG_MEASURE;
static uint8_t       rungFails = 0;              // 현재 단에서 쌓인 실패 수
static bool          ladderEverFailed = false;   // 아래 이중계수 가드의 첫 호출 예외
static unsigned long lastLadderFailAt = 0;

// 사다리를 0단으로 되돌리기 위해 요구하는 연속 온라인 시간(위 주석 참조)
static const uint16_t LADDER_RESET_MS = 30000;
static unsigned long  onlineSince = 0;

// ── 0단 계측 ──
// cwjapPending 은 **"CWJAP 를 쏴 놓고 아직 OK 도 FAIL 도 못 받았다"** 는 뜻이다.
// 이게 없으면 응답이 아예 없는 CWJAP(=사고 당시 자주 있었다)가 30초마다 조용히
// 되풀이될 뿐 실패로 세어지지 않아 **사다리가 영원히 0단에 머문다.** 사다리를 실제로
// 굴리는 것은 이 한 개의 불리언이다.
static bool          cwjapPending = false;
static unsigned long cwjapSentAt = 0;
// 결합(IP 확보) 시각. 0 이면 결합 없음. **다음 실패 때 "결합 유지 N초"를 찍는 근거**이고,
// 그 숫자가 12초 근처로 계속 찍히면 사용자 관측(라우터 화면)이 장치 로그로 확증된다.
static unsigned long assocAt = 0;

// ── 4단 배선: ESP 하드웨어 리셋선 ─────────────────────────────────────────
// **핀 A2 를 쓴다.** 남는 핀을 다시 세어서 고른 것이지 임의로 뺏은 것이 아니다
// (위 배선 주석의 금지목록 + SLOT_PIN[] 을 그대로 대조했다):
//   D0,D1 USB시리얼 / D2~D6 자리 A1~A5 / D7,D8 ESP 시리얼 / D9~D12 자리 B1~B4 /
//   D13 온보드LED / A0 자리 B5 / A1 난수시드 / A4,A5 I2C 예약
//   → 남는 것은 **A2 와 A3 둘뿐**이고 그중 A2 를 썼다. **센서 칸은 하나도 옮기지 않았다.**
// ⚠ 이름 혼동: 여기의 **핀 A2** 는 **자리 A2**(= 핀 D3)와 아무 상관이 없다. 위 §1 주석 참조.
//
// ⚠⚠ 전기적으로 가장 중요한 것 — **이 핀을 OUTPUT HIGH 로 만들지 마라.**
//   Uno 의 HIGH 는 5V 이고 ESP-01 의 RST 는 3.3V 로직이다. 5V 를 밀어넣으면 모듈이 상한다.
//   그래서 **오픈드레인처럼** 쓴다:
//     누름(리셋) = pinMode(OUTPUT) + digitalWrite(LOW)
//     놓음       = pinMode(INPUT)   ← 하이임피던스. 라인은 ESP 쪽 풀업이 HIGH 로 잡는다
//   (AVR 코어의 pinMode(INPUT) 은 DDR 과 PORT 를 함께 0 으로 만든다 → 내부 풀업도 꺼진다.
//    즉 이 상태에서 우리 쪽은 라인에 아무 전압도 걸지 않는다.)
//   ⚠ 그러므로 **RST 쪽에 풀업이 있어야 한다.** 놓았을 때 라인이 뜨면 모듈이 불확정 상태가
//     되어 4단이 없느니만 못해진다. 모듈에 풀업이 없으면 10kΩ 을 RST↔3.3V 사이에 달아라.
//   ⚠ 리셋과 놓음의 GND 기준이 같아야 한다 — Uno GND 와 ESP GND 는 이미 공통이어야 정상이다.
#define ESP_RST_WIRED 0                 // ★ 2026-08-15 A2↔ESP RST 선을 **물리적으로 분리했다** → 되돌림.
                                        //   1 로 두면 없는 배선을 전제해 4단이 "리셋했다"고 거짓 로그를 남긴다.
static const uint8_t PIN_ESP_RST = A2;

static bool          espRstHeld = false;
static unsigned long espRstReleaseAt = 0;

// 무엇을 보냈는지 찍는다(REQ-0042 3순위). 이게 없으면 5초마다 CIPSTART 를 재시도하는지조차
// 로그로 확인할 수 없다.
#if DEBUG
#define DBG_NET(n, name) do { Serial.print(F("[NET] " n " " name)); Serial.println(); } while (0)
#else
#define DBG_NET(n, name) do {} while (0)
#endif

static void netSendStep(uint8_t s) {
  switch (s) {
    case NET_RST:     wifi.print(F("AT+RST\r\n"));      DBG_NET("0", "RST");      break;
    case NET_CWMODE:  wifi.print(F("AT+CWMODE=1\r\n")); DBG_NET("1", "CWMODE=1"); break;
    case NET_CWJAP:   wifi.print(F("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n"));
                      DBG_NET("2", "CWJAP (응답까지 최대 30초 기다린다)"); break;
    case NET_CIFSR:   wifi.print(F("AT+CIFSR\r\n"));    DBG_NET("3", "CIFSR (IP 를 실제로 받았는가)"); break;
    case NET_CIPMUX:  wifi.print(F("AT+CIPMUX=0\r\n")); DBG_NET("4", "CIPMUX=0"); break;
    case NET_CIPSTART:wifi.print(F("AT+CIPSTART=\"TCP\",\"" SERVER_IP "\"," SERVER_PORT "\r\n"));
                      DBG_NET("5", "CIPSTART"); break;
    case NET_CIPCLOSE:wifi.print(F("AT+CIPCLOSE\r\n")); DBG_NET("6", "CIPCLOSE (낡은 소켓 닫기)"); break;
    case NET_GMR:     wifi.print(F("AT+GMR\r\n"));      DBG_NET("7", "GMR (펌웨어 판 — 진단)"); break;
    case NET_CWCOUNTRY:
                      // 규제도메인을 KR(1~13)로 넓혀 채널 12/13 결합을 열어 보려는 시도.
                      // 구형 펌웨어에는 이 명령이 없다 → ERROR. 그 ERROR 가 곧 "코드로는 못 고친다"의 확증이다.
                      wifi.print(F("AT+CWCOUNTRY_DEF=0,\"KR\",1,13\r\n"));
                      DBG_NET("8", "CWCOUNTRY (채널 12/13 우회 시도 — 진단)"); break;
    case NET_CWLAP:   wifi.print(F("AT+CWLAP\r\n"));    DBG_NET("9", "CWLAP (주변 AP 목록 — 진단)"); break;
    case NET_CWQAP:   wifi.print(F("AT+CWQAP\r\n"));    DBG_NET("10", "CWQAP (결합 해제 — 사다리 2단)"); break;
    default: break;
  }
  // ★ REQ-0071 — CWJAP 의 "답이 오지 않는 경우"를 실패로 세기 위한 표식(위 cwjapPending 주석).
  //   RST·CWQAP 는 진행 중인 결합 시도를 무효로 만드는 명령이므로 표식을 내린다.
  //   내리지 않으면 리셋 뒤 첫 틱에서 "무응답"으로 오인되어 한 칸이 공짜로 올라간다.
  if (s == NET_CWJAP)                        { cwjapPending = true; cwjapSentAt = millis(); }
  else if (s == NET_RST || s == NET_CWQAP)   { cwjapPending = false; }
  netLastSent = s;
}

// "이 줄에 쓸 만한 IPv4 가 들어 있는가" — CIFSR 응답 판정용.
//   구형: `192.168.0.7` 만 덜렁 온다 / 신형: `+CIFSR:STAIP,"192.168.0.7"`
// 둘 다 받으려고 문구가 아니라 **숫자 모양**으로 찾는다. 0.0.0.0 은 IP 가 아직 없다는 뜻이다.
static bool hasUsableIp(const char* s) {
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') continue;
    if (p != s && p[-1] >= '0' && p[-1] <= '9') continue;   // 숫자의 중간이면 건너뛴다
    uint8_t oct = 0, dots = 0, first = 0;
    const char* q = p;
    for (;;) {
      if (*q >= '0' && *q <= '9') { oct = (uint8_t)(oct * 10 + (*q - '0')); q++; continue; }
      if (dots == 0) first = oct;
      if (*q == '.') { dots++; oct = 0; q++; continue; }
      break;
    }
    if (dots == 3 && first != 0) return true;               // 0.x.x.x = 아직 IP 없음
  }
  return false;
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

// ═════════════════════════════════════════════════════════════════════════
// 복구 사다리 — 조치부 (상태·상수는 위 "복구 사다리 (REQ-0071)" 블록에 있다)
// ═════════════════════════════════════════════════════════════════════════

// ── 1단 조치: 시리얼 스트림을 씻는다 ──
// 쓰레기(\xFF\xFE\xFC 계열)나 [DROP-OVF] 가 보인 뒤에는 **줄 경계 자체를 믿을 수 없다.**
// 남아 있는 바이트를 파싱하지 않고 버리고 조립 상태를 초기화한다.
// ⚠ 120ms 상한을 둔 이유: 모듈이 끝없이 토하는 상황에서 여기 갇히면 loop() 가 멈춘다(= 행).
//   오프라인 경로에서만 불리므로 이 정도 정지는 하트비트에 영향이 없다.
static void drainSerial(void) {
  unsigned long t0 = millis();
  uint16_t dropped = 0;
  while (millis() - t0 < 120UL) {
    while (wifi.available()) { (void)wifi.read(); if (dropped < 65535) dropped++; }
  }
  rxLen = 0;
  rxOverflow = false;
  pendReady = false;
#if DEBUG
  Serial.print(F("[LADDER] 1단 조치: 시리얼 드레인 "));
  Serial.print(dropped);
  Serial.println(F("바이트 버림 + 줄 조립 초기화"));
#endif
}

// ── 4단 조치: 하드웨어 리셋선을 잡는다/놓는다 ──
// 잡고 있는 동안 ESP 는 사실상 꺼진 것과 같아 3.3V 레일이 가장 잘 회복한다 —
// 그래서 백오프 시간을 **그대로 리셋 유지시간으로 쓴다.** 쉬는 것과 리셋이 한 동작이 된다.
// static 이 아닌 이유: ESP_RST_WIRED=0 이면 호출자가 전부 사라져 -Wunused-function 이 뜬다.
// 전역이면 링커의 --gc-sections 가 최종 이미지에서 빼 주므로 플래시도 먹지 않는다
// (같은 이유로 slotOverrideSet() 등도 전역이다 — 위 "수동 오버라이드" 주석 참조).
void espRstAssert(uint16_t holdMs) {
#if ESP_RST_WIRED
  pinMode(PIN_ESP_RST, OUTPUT);
  digitalWrite(PIN_ESP_RST, LOW);      // ★ 절대 OUTPUT HIGH 로 놓지 않는다(5V 가 3.3V 핀에 실린다)
  espRstHeld = true;
  espRstReleaseAt = millis() + holdMs;
  netOnline = false;
  netHasIp = false;
  cwjapPending = false;
#else
  (void)holdMs;
#endif
}

// 놓기 — 하이임피던스로 되돌린다. 부팅 직후 ESP 는 다른 보율로 쓰레기를 토하므로 버린다.
static void espRstService(unsigned long now) {
  if (!espRstHeld) return;
  if ((long)(now - espRstReleaseAt) < 0) return;
#if ESP_RST_WIRED
  pinMode(PIN_ESP_RST, INPUT);         // 하이임피던스 (위 배선 주석 참조)
#endif
  espRstHeld = false;
  drainSerial();
  netAdvance(NET_CWMODE, 1500);        // 모듈이 부팅할 시간을 주고 처음부터
#if DEBUG
  Serial.println(F("[LADDER] 4단: ESP 리셋 해제 — 부팅을 기다렸다가 CWMODE 부터 다시"));
#endif
}

// ── 단이 정한 조치를 실제로 수행한다 ──
static void applyRung(void) {
  uint16_t back = RUNG_BACKOFF_MS[rung];
  switch (rung) {
    case RUNG_MEASURE:
      // 조치 없음(계측만). FAIL 이어도 IP 가 살아 있을 수 있으므로 CIFSR 로 확인부터 한다
      // — REQ-0064 에서 확인된 동작이라 그대로 둔다. 0단은 "지금까지의 코드" 그 자체다.
      cifsrTries = 0;
      netAdvance(NET_CIFSR, 300);
      break;

    case RUNG_RESYNC:
      drainSerial();
      cifsrTries = 0;
      netAdvance(NET_CIFSR, 300);
      break;

    case RUNG_CWQAP:
#if DEBUG
      Serial.print(F("[LADDER] 2단 조치: "));
      Serial.print(back / 1000U);
      Serial.println(F("초 쉬고 AT+CWQAP 로 결합을 명시적으로 끊은 뒤 다시 붙는다"));
#endif
      netHasIp = false;
      netAdvance(NET_CWQAP, back);
      break;

    case RUNG_SOFTRST:
#if DEBUG
      Serial.print(F("[LADDER] 3단 조치: "));
      Serial.print(back / 1000U);
      Serial.println(F("초 쉬고 AT+RST (모듈 소프트 리셋)"));
#endif
      netHasIp = false;
      netAdvance(NET_RST, back);
      break;

    case RUNG_HWRST:
#if ESP_RST_WIRED
#if DEBUG
      Serial.print(F("[LADDER] 4단 조치: ESP RST 선을 "));
      Serial.print(back / 1000U);
      Serial.println(F("초 동안 잡는다 (그동안 모듈은 꺼진 것과 같다)"));
#endif
      espRstAssert(back);
#else
      // 배선이 없으면 **있는 척하지 않는다.** 없는 조치를 로그에 성공으로 남기면
      // 다음 사람이 "4단까지 해 봤는데 안 되더라"는 틀린 결론을 얻는다.
#if DEBUG
      // ⚠ 문구를 약하게 쓰지 마라. 20분치 로그를 훑는 사람이 바로 위의 `↑↑ 단 상승 → 4단`
      //   만 보고 "하드웨어 리셋까지 해 봤는데 안 되더라"는 **틀린 결론**을 얻으면 안 된다.
      //   그래서 실제로 조치하지 못한 단은 전부 `미실행` 을 달아 둔다 — `grep 미실행` 한 번에 걸린다.
      Serial.println(F("[LADDER] ⚠4단 미실행: 하드웨어 리셋선이 배선되지 않았다(ESP_RST_WIRED=0)"));
      Serial.println(F("[LADDER]   → Uno A2 를 ESP 의 RST 에 물리고 ESP_RST_WIRED 를 1 로 바꿔 다시 구워라"));
      Serial.print(F("[LADDER]   지금은 "));
      Serial.print(back / 1000U);
      Serial.println(F("초 쉬고 AT+RST 만 되풀이한다"));
#endif
      netHasIp = false;
      netAdvance(NET_RST, back);
#endif
      break;

    default:  // RUNG_POWER
      // ★ 5단은 **구현하지 않았다.** 전원을 코드로 끊으려면 로우사이드 MOSFET(또는 트랜지스터)
      //   스위치가 필요한데 그 부품이 없다. 없는 하드웨어를 있는 것처럼 코드만 넣으면
      //   로그가 거짓말을 하게 된다. 그래서 **단은 이름과 전이만 남기고 조치는 4단을
      //   최장 백오프로 되풀이하는 것**으로 정직하게 대체한다.
#if DEBUG
      Serial.println(F("[LADDER] ⚠5단 미실행: 전원 재투입은 **부품 미보유로 미구현**이다"));
      Serial.println(F("[LADDER]   필요 부품: 로우사이드 MOSFET 스위치(예: 2N7000/AO3400 + 10k)"));
      Serial.print(F("[LADDER]   대신 4단 조치를 "));
      Serial.print(back / 1000U);
      Serial.println(F("초 백오프로 되풀이한다(종착역)"));
#endif
      netHasIp = false;
#if ESP_RST_WIRED
      espRstAssert(back);
#else
      netAdvance(NET_RST, back);
#endif
      break;
  }
}

// ── 실패 사건이 들어오는 유일한 문 ──
// why: 무엇이 실패했는가. 계측 한 줄을 정확히 쓰기 위해서만 쓰인다(단 선택에는 3번만 관여).
//
// ⚠ **netTick() 의 switch 안에서 이 함수를 부를 때 주의할 것** — 새 호출 지점을 추가할 사람에게.
//   그 시점에는 이미 `netSendStep(sent)` 로 명령이 나간 뒤다. 그런데 1단 조치는 `drainSerial()`
//   이라 **방금 보낸 명령의 응답을 그 자리에서 버린다.** 지금 있는 두 호출 지점은 둘 다 안전하다:
//     · NET_CIFSR  — 버려도 되는 응답이다(cifsrTries 를 이미 소진해 쓸모없음이 확정됐다)
//     · NET_CIPCLOSE — LF_AT_JAMMED 는 rung 을 최소 3단으로 올리므로 1단(드레인)에 닿지 않는다
//   세 번째 호출 지점을 넣는다면 이 두 성질 중 하나가 성립하는지 먼저 확인하라.
enum { LF_CWJAP_FAIL = 0, LF_CWJAP_TIMEOUT, LF_AT_JAMMED };

static void ladderFail(uint8_t why) {
  unsigned long now = millis();

  // ★ 이중 계수 방지. 한 번의 실패가 두 경로로 들어오면(예: 응답 FAIL 과 무응답 타임아웃이
  //   겹치면) 모든 단의 한계가 사실상 절반이 되어 1분 만에 4단까지 치솟는다.
  //   "건너뛰지 않는다"(원칙 2)를 지키려면 세는 것부터 정확해야 한다.
  if (ladderEverFailed && (now - lastLadderFailAt) < 1000UL) {
#if DEBUG
    Serial.println(F("[LADDER] 1초 안에 들어온 중복 실패 신호 — 세지 않는다"));
#endif
    return;
  }
  ladderEverFailed = true;
  lastLadderFailAt = now;

#if DEBUG
  // ── 0단: 계측. 이 한 줄이 "12초 결합 소실" 가설을 판에서 직접 검증한다 ──
  Serial.print(F("[LADDER] 실패("));
  switch (why) {
    case LF_CWJAP_TIMEOUT: Serial.print(F("CWJAP 무응답")); break;
    case LF_AT_JAMMED:     Serial.print(F("AT 계층 잠김"));  break;
    default:               Serial.print(F("CWJAP FAIL"));    break;
  }
  Serial.print(F(") CWJAP소요 "));
  Serial.print((now - cwjapSentAt) / 1000UL);
  Serial.print(F("s · 연속 "));
  Serial.print(cwjapFails);
  Serial.print(F("회 · 결합유지 "));
  if (assocAt) { Serial.print((now - assocAt) / 1000UL); Serial.print('s'); }
  else         { Serial.print(F("없음")); }
  Serial.print(F(" · 현재 "));
  Serial.print(rung);
  Serial.print(F("단("));
  Serial.print(rungFails);
  Serial.println(F("회 누적)"));
#endif
  assocAt = 0;

  // ── 단 올리기 ──
  if (why == LF_AT_JAMMED && rung < RUNG_SOFTRST) {
    // AT 명령 자체가 안 먹는 상태다. 결합의 문제가 아니라 모듈이 꼬인 것이므로
    // 씻기(1단)·결합해제(2단)로는 원리적으로 못 낫는다 → 리셋 단으로 직행한다.
    // 이것은 "아래 단을 건너뛰지 않는다"의 위반이 아니라 **다른 사다리에 올라탄 것**이다.
    rung = RUNG_SOFTRST;
    rungFails = 0;
#if DEBUG
    Serial.println(F("[LADDER] ↑ 3단(AT+RST) 직행 — AT 계층이 잠겼으면 아래 단은 의미가 없다"));
#endif
  } else {
    if (rungFails < 254) rungFails++;
    if (rungFails >= RUNG_LIMIT[rung] && rung < RUNG_MAX) {
      rung++;
      rungFails = 0;
#if DEBUG
      Serial.print(F("[LADDER] ↑↑ 단 상승 → "));
      Serial.print(rung);
      Serial.println(F("단"));
#endif
    }
  }

  // 진단 사슬(GMR→CWCOUNTRY→CWLAP)은 REQ-0064 그대로 **딱 한 번만** 끼워 넣는다.
  // 사다리와 경쟁시키지 않는다 — 한 번 지나가면 lapDone 이 서서 다시는 오지 않는다.
  if (cwjapFails >= 2 && !lapDone) { netAdvance(NET_GMR, 500); return; }

  applyRung();
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

// ── 살아있음(liveness) 불변식 (REQ-0116) ─────────────────────────────────────
// **실패 모드를 세지 않고 결과를 본다: "온라인이라면서 T초 동안 한 줄도 못 내보냈으면 끊는다."**
//
// 왜 이 형태인가 — 오늘 하루에 원인이 세 번 옮겨 다녔다(프롬프트 제한시간 → `busy` → ?).
// 실패 종류마다 카운터를 다는 방식은 **아는 실패만** 막는다. 그런데 오늘 배운 것은
// **모르는 실패가 더 많다**는 쪽이다. 그래서 이유를 묻지 않는 불변식을 둔다 —
// `busy` 든 `ERROR` 든 침묵이든 **아직 이름이 없는 무엇이든** 여기에 걸린다.
//
// ⚠ 이 불변식이 없으면 REQ-0116 의 "busy 를 세지 않는다"가 **조용한 영구 정지**를 만든다:
//   busy 만 계속 오면 연속 실패 카운터가 영원히 3에 도달하지 않아 회복 경로가 사라진다.
//   고치기 전에는 (엉뚱한 이유였지만) 3초 만에 끊고 다시 붙었다. 그 길을 되살리는 것이다.
//
// T = 10초의 근거: 서버의 무프레임 판정(§3.4, 3.5초)과 기존 3연속 실패 경로(약 3초)보다
//   **확실히 길어서** 평범한 문제는 그쪽이 먼저 잡고, 무한 정지보다는 **확실히 짧다.**
//   ⚠ 이 값은 보수적 **기본값**이다 — A 구간의 `busy` 연속 런 분포가 나오면 근거를 붙여 다듬는다.
static const uint16_t TX_STALL_MS = 10000;
static uint32_t      lastTxOkAt   = 0;     // 마지막으로 **실제로 나간** 프레임의 시각
// 아래 셋은 **진단 전용이다. 제어에 쓰지 않는다.**
// 불변식이 발동했을 때 "무엇 때문에 못 나갔나"를 로그로 남기려는 것뿐이다 —
// 원인별로 따로 끊는 제어 경로를 만들면 그게 다시 "아는 실패만 막는" 구조가 된다.
static uint8_t       stallBusy = 0, stallTimeout = 0, stallReject = 0;

// 프롬프트를 놓쳐 스트림을 강제 복구한 횟수(아래 sendLine 참조).
//
// ✏️ 2026-08-16 정정 (REQ-0126) — ~~"서버의 `버린줄(모름)` 카운터와 짝을 이룬다"~~ ❌ **틀렸다.**
//   여기 *"둘이 맞아떨어져야 정상이고 어긋나면 다른 원인이 있다"* 고 적혀 있었다.
//   **실측이 정반대다: 기대값은 0 이고, 맞아떨어지면 오히려 이상하다.**
//
//   실측 2026-08-16(`monitor/serial-newbase.log` 21건 전수, 18건 직접 증거):
//   프롬프트를 놓치는 경우는 **거의 전부 ESP 가 명령 모드**였다((b) 갈래). 그러면 더미는
//   소켓이 아니라 **AT 해석기**로 들어가므로 **서버에는 줄이 도달조차 하지 않는다.**
//   근거는 `[AT] "#####…"` 가 **에코로 되돌아온 것**이다(데이터 모드였다면 에코 없이
//   TCP 로 나가고 `SEND OK` 가 떴을 것이다 — `SEND OK` 는 21건 중 한 번도 없었다).
//
//   ⚠ 그래서 이 카운터와 서버 카운터의 **크기 비교로 무엇을 판정하지 마라.** 둘은 같은
//     사건을 두 번 보는 것이 아니라 **애초에 다른 사건**이다(CLAUDE.md "정의를 맞춰라").
//     교차검증을 하려면 **갈래를 먼저 가른 뒤** (a) 갈래만 서버 카운터와 견줘야 한다.
static uint16_t      promptResyncs = 0;

// ── 2단계 (REQ-0116 2단계 · `docs/arduino/design-stage2-sendok.md`) ──────────
// **`SEND OK` 를 실제로 기다린다.** 지금까지는 `SEND_GAP_MS=80` 이라는 고정 추측으로
// "앞 전송이 끝났겠지" 하고 다음 CIPSEND 를 쐈다. 그 80ms 는 **우리가 쓰기를 끝낸 시각**부터
// 재는 것이지 **ESP 가 WiFi 로 다 보낸 시각**이 아니다. 망이 느리면 ESP 는 아직 보내는 중이고
// 그때 `AT+CIPSEND` 를 쏘면 `busy s...` 로 거부된다 — 그게 기전 A 다.
//
// ⚠ **비블로킹이어야 한다.** 여기서 `SEND OK` 를 눌러 기다리면 하트비트(1Hz)와 하행(`+IPD`)
//   처리가 통째로 밀린다. 그래서 "기다린다"가 아니라 **"아직이면 이번 주기를 건너뛴다"** 다.
static bool          awaitingSendOk  = false;  // 앞 전송의 SEND OK 를 아직 못 봤다
static uint32_t      sendOkWaitFrom  = 0;      // 그 기다림이 시작된 시각
// 안전망: `SEND OK` 가 **영영 안 올 수도 있다.** AT 펌웨어 판본에 따라 안 주거나,
// SoftwareSerial 이 반이중 + RX 64B 라 우리가 쓰는 동안 도착한 응답이 통째로 사라질 수 있다
// (원장 §2.5). 상한을 두지 않으면 2단계가 1단계보다 **더 나쁜 정지**를 만든다.
// ⚠⚠ **이 상한은 공짜가 아니다 — ESP 리셋(기전 B) 탐지가 그만큼 늦어진다.**
//   ESP 가 죽으면 `SEND OK` 가 영영 안 오는데, 그동안 우리는 **건너뛰기만 하고 아무것도
//   세지 않는다.** 즉 상한이 풀릴 때까지 3연속 실패 판정이 **시작되지도 못한다.**
//
//   | | 오프라인 전환까지 |
//   |---|---|
//   | 2단계 이전 | 프롬프트 놓침 ×3 ≈ **3초** (하트비트 1Hz) |
//   | 2단계 이후 | **상한 + 약 3초** |
//
//   루트 지시(REQ-0125 3번)가 **"침묵 탐지를 잃지 마라"** 인데, 상한을 크게 잡으면
//   탐지를 없애지는 않아도 **늦춘다.** 그래서 설계 초안의 3000ms 에서 **2000ms 로 줄였다.**
//
// ── 2초의 근거 — **실측이다**(monitor REQ-0131 · `MEASURE-2026-08-16-sendok-delay.md`) ──
//
// **이 상한이 지키는 바로 그 구간**(페이로드 쓰기 완료 → `SEND OK`)을 직접 쟀다.
// 상한이 아니라 정확히 같은 구간이다 — 로그에 페이로드 줄이 따로 찍혀 경계가 보였다.
//
//   표본 6,880 · Δ=0s 6,859(99.695%) · Δ=1s 21(0.305%) · **Δ≥2s 0건**
//   분모: 정상 6,881 · 비정상 2 · **`SEND OK` 미도달 23** ← 이 상한이 존재하는 이유
//
// ⚠ **측정 조건(다른 조건의 값과 비교할 때 반드시 맞춰야 한다 — §5.5 "누가 만들었는가")**:
//   이 값은 **같은 기계에 시험 서버 인스턴스가 11회 뜬 창**에서 나왔다(17:50~19:15, 루트 승인).
//   포트·보드는 분리돼 있었지만 **CPU·네트워크를 함께 썼다.** 시리얼 타임스탬프는
//   **"줄을 읽은 시점"** 이라 경합이 있으면 Δ 가 **실제보다 부풀려질 수 있다.**
//   → 즉 이 값은 **보수적인(=더 큰) 쪽으로 치우쳤을 수 있다.** 임계값 정하기에는 안전한 방향이다.
//   ⚠ 오염 흔적은 안 보인다(개입이 가장 빽빽한 구간에 `busy` 가 오히려 적고, 개입 0 인 구간에
//     20건이 몰렸다 — 경합과 반대 방향). **그래도 조건이 달랐다는 사실은 지운다고 없어지지 않는다.**
//
// ⚠ **말할 수 있는 것과 없는 것을 구분한다:**
//   · 말할 수 있다 — **2000ms 는 관측된 정상 분포를 자르지 않는다.**
//   · 말할 수 **없다** — 여유가 얼마인지. 로그 해상도가 1초라 `Δ=1s` 21건의 실제 경과는
//     (0,2)초 어딘가이고, **1.9초짜리 정상 송신이 있었을 가능성을 배제하지 못한다.**
//     → 그래서 "안전하다"가 아니라 **"실측과 모순 없다"** 까지만 주장한다.
//
// **그럼에도 3000 이 아니라 2000 을 고른 이유 — 두 실수의 값이 비대칭이다:**
//   · 너무 일찍 자르면(정상인데 상한 초과) → **2단계 이전 동작으로 되돌아갈 뿐**이고
//     `sendOkTimeouts` 에 계상되어 **스스로 드러난다.** 손실이 작고 관측된다.
//   · 너무 늦게 풀면 → **ESP 가 죽었는데 그만큼 눈이 먼다.** 루트 지시 #3 이 막으려는 것.
//   비가역·비관측 쪽 실수를 피하는 것이 맞다.
//
// ★ **그리고 이 선택은 자기교정된다**: `okto`(=`sendOkTimeouts`)가 `[CNT]` 줄로 항상 나간다.
//   구운 뒤 `okto` 가 미도달 기대치(≈0.33%)보다 **크게 나오면 2000 이 빡빡하다는 증거**이므로
//   그때 근거를 갖고 올린다. **추측으로 미리 넉넉히 잡는 것보다 낫다 — 재고 고치는 길이 있다.**
static const uint16_t SEND_OK_TIMEOUT_MS = 2000;
static uint16_t      sendOkTimeouts = 0;       // 진단: 상한 초과 횟수 — 잦으면 그 자체가 발견이다
static uint16_t      sendSkips      = 0;       // 진단: SEND OK 대기로 건너뛴 주기 수
static uint16_t      sendFails      = 0;       // 진단: SEND FAIL 수신 횟수

// ── ESP 모듈 리셋 탐지 (REQ-0125 · **진단 전용. 제어에 쓰지 않는다**) ─────────
// 실측 2026-08-16: 링크 끊김 4건 중 **2건이 ESP-01 모듈 자체의 재부팅**이었다.
// 우리가 시킨 것이 아니다 — `AT+RST` 는 그 로그 전체에 부팅 사다리 1회뿐이었고,
// 재부팅 직전까지 `SEND OK` 가 1초마다 정상이었다(예고 없음).
// **Uno 는 그동안 한 번도 재부팅하지 않았다**(uptime 324→1290→2458→5078s 단조 증가).
//   → 즉 "재부팅 없는 링크 단절"의 실체 중 최소 절반이 이것이다.
//
// ⚠ **일부러 제어 경로를 만들지 않았다.** 기존 사다리가 이미 잘 처리한다 —
//   실측에서 `CIFSR` 단이 `0.0.0.0` 을 보고 전진을 거부했고, 2초 뒤 IP 를 얻어 복귀했다
//   (오프라인 전환 → 온라인까지 4초). 사다리가 **원인이 아니라 결과(IP 가 있는가)** 를
//   보기 때문에 이름 모를 고장에도 그대로 동작한 것이다(§6.1).
//   배너를 보고 즉시 복구를 걸면 2~3초를 아낄 수 있지만, 그건 **다음 변경**이다 —
//   한 번에 하나씩 넣어야 효과를 귀속할 수 있다(§6.3).
//
// ★ 판별자로 **부트 배너를 쓰지 않는다.** monitor 가 전수 검증해 정정해 줬다(REQ-0125 정정본):
//
//   | 판별자 | 전수 | 리셋 사건 일치 | 정상 부팅에서도 뜨나 |
//   |---|---|---|---|
//   | 부트 배너 `[System Ready...]` | 4건 | 리셋 3 + 정상부팅 1 | **뜬다 → 오탐** |
//   | `CIFSR` 응답이 `0.0.0.0`      | 3건 | **3/3**        | **안 뜬다**        |
//
//   배너는 **매 기동에 뜨므로** 그것만 세면 정상 부팅을 리셋으로 센다.
//   `0.0.0.0` 이 뜨는 기전: ESP 가 제멋대로 리셋되면 우리는 아직 자신을 online 이라 믿고
//   있다가 사다리에 **CIFSR 단(3단)부터 재진입**하는데, 그 시점엔 ESP 가 아직 AP 에
//   재결합하지 못해 주소가 없다. **정상 부팅은 CWJAP 를 끝낸 뒤 CIFSR 로 오므로 절대 0 이 아니다.**
//
// ✏️ **2026-08-16 19:0x 재정정 (monitor 2차 검증) — 판별자 하나로는 부족하다. OR 로 본다.**
//   내가 처음엔 배너를 썼다가 `0.0.0.0` 단독으로 바꿨는데, **그것도 놓치는 경우가 있다:**
//
//   | 판별자 | 실측 약점 |
//   |---|---|
//   | 부트 배너 | 정상 부팅(16:59:57)에도 뜬다 → **오탐** |
//   | `CIFSR → 0.0.0.0` | 18:48:12 리셋에서 **CIFSR 응답 자체가 가비지**라 문자열이 안 나왔다 → **누락** |
//
//   ⚠ **리셋이 심할수록 `0.0.0.0` 을 놓치는 방향**이라 특히 나쁘다 — 제일 중요한 사건을 놓친다.
//   → **IP 소실 = (`0.0.0.0` 을 봤다) OR (CIFSR 을 3회 물어도 쓸 IP 가 없었다)**
//     뒤쪽은 응답이 깨져도 성립하므로 앞쪽의 구멍을 정확히 메운다.
//
// ⚠ **걸쇠(latch)가 필요한 이유**: 한 번의 리셋에서 `0.0.0.0` 이 여러 번 오고 그 뒤 3회 소진까지
//   가면 **한 사건이 네 번 세진다.** 그러면 이 카운터는 "사건 수"가 아니라 "증상 수"가 되어
//   monitor 의 사건 계수와 **정의가 어긋난다**(CLAUDE.md "무엇을 세는가").
//   그래서 **IP 를 잃은 순간 한 번만** 세고, 실제로 IP 를 되찾을 때 걸쇠를 푼다.
static uint16_t      espResets = 0;      // ESP 가 IP 를 잃은 **사건 수** (증상 수가 아니다)
static bool          ipLossLatched = false;
static uint16_t      linkDrops = 0;      // 링크가 실제로 끊겨 재수립에 들어간 횟수(누적)

// ⚠⚠ **다른 도메인 계수와 짝지을 때 — 이름이 비슷한 칸끼리 붙이지 마라** (monitor 실측 확인)
//
//   `espResets` 는 **IP 소실**에서 센다. 그런데 monitor 의 `리셋계열` 은 `busy` 표지가
//   같이 있는 사건을 **`혼합` 칸으로 따로 뺀다.** 그래서 같은 구간에서
//   **`espResets`=5 인데 `리셋계열`=4** 가 나온다(차이는 17:41 혼합 1건).
//
//   | 내 칸 | monitor 의 짝 |
//   |---|---|
//   | `drop`   | **전체 사건 수**(모든 갈래 합) |
//   | `esprst` | **`리셋계열` + `혼합`** ← `리셋계열` 만 붙이면 어긋난다 |
//
//   > **둘 다 맞는 값이고 정의가 다를 뿐이다.** 이걸 모르고 5 대 4 를 보면
//   > **"계수기 하나가 고장났다"** 로 읽는다 — 실제로 그럴 뻔했다.

// ── 🔴 운영 계수기 출력 — **`DEBUG` 와 무관하게 항상 나간다** ────────────────
//
// 왜 `#if DEBUG` 밖인가 (루트 지시 · 2026-08-16):
//   계수기 **변수**는 원래도 DEBUG 밖이었다. 문제는 **찍는 줄이 전부 안에 있었던 것**이다.
//   → `DEBUG=0` 으로 구우면 3칸이 1칸으로 주는 게 아니라 **관측이 0칸**이 된다.
//     장치가 **자기 ESP 리셋에 대해 눈이 먼 채로** 시연장에 서게 된다.
//   **셀 수만 있고 못 읽으면 안 뺀 것과 같다.** 그래서 읽는 경로를 같이 둔다.
//
// 왜 S 프레임(전선)이 아니라 시리얼인가:
//   전선에 실으면 서버 로그에 영구 보관되어 더 낫다. **그러나 프로토콜 변경이라
//   socket·web 이 걸리고**, 펌웨어 수정과 같은 굽기에 섞으면 변수가 둘이 된다(§6.3).
//   → **별건으로 올린다.** 지금은 도메인 안에서 닫히는 방법을 쓴다.
//
// 비용: 60초에 한 줄(약 60바이트). 115200bps 에서 약 5ms. 플래시도 수십 바이트다.
//   ⚠ `[AT]` 원문 로깅과 달리 **대역을 먹지 않는다** — 그래서 이건 밖에 둬도 된다.
//     경계는 "정수 카운터는 밖 · 장문 진단은 안"이다. 이 경계를 흐리지 마라.
//
// ⚠ 형식을 바꾸면 monitor 파서가 깨진다. 칸을 **추가**하되 기존 칸 이름·순서는 유지해라.
static uint32_t      cntLastAt = 0;
static const uint32_t CNT_PERIOD_MS = 60000;

static void cntTick(uint32_t now) {
  if ((uint32_t)(now - cntLastAt) < CNT_PERIOD_MS) return;
  cntLastAt = now;
  // 전부 **부팅 이후 누적**이다. 구간값이 아니다 — 창을 잡으려면 두 줄을 빼서 써라.
  Serial.print(F("[CNT] up="));      Serial.print(now / 1000);
  Serial.print(F(" drop="));         Serial.print(linkDrops);
  Serial.print(F(" esprst="));       Serial.print(espResets);
  Serial.print(F(" resync="));       Serial.print(promptResyncs);
  Serial.print(F(" sendfail="));     Serial.print(sendFails);
  Serial.print(F(" okto="));         Serial.print(sendOkTimeouts);
  Serial.print(F(" skip="));         Serial.print(sendSkips);
  Serial.print(F(" online="));       Serial.println(netOnline ? 1 : 0);
}

// IP 소실을 한 번만 세는 자리. 두 판별자 어느 쪽이든 여기로 들어온다.
static void noteIpLoss(void) {
  if (ipLossLatched) return;             // 같은 사건의 두 번째 증상 — 세지 않는다
  ipLossLatched = true;
  if (espResets < 65535) espResets++;
#if DEBUG
  Serial.print(F("[NET] ★ ESP 가 IP 를 잃었다 — 모듈 리셋으로 본다. 누적 사건 "));
  Serial.println(espResets);
#endif
}

// 소켓 복구 진입 — **전송이 안 되는 것을 이유로 오프라인이 되는 모든 경로**가 여기를 통과해야 한다.
// (연속 실패 카운터 / `link is not valid` 둘 다.) 한 곳이라도 CIPSTART 로 바로 가면
// 그 경로에서 REQ-0051 의 무한 루프가 되살아난다.
static void startSocketRecovery(void) {
  netOnline = false;
  sendFailStreak = 0;
  // ★ 2단계: 대기를 **반드시 푼다.** 소켓이 사라지는 마당에 앞 전송의 `SEND OK` 는 영영 오지
  //   않는다. 안 풀면 다시 온라인이 된 뒤 첫 송신이 통째로 건너뛰어지고, 최악에는 상한(3초)을
  //   태우고 나서야 첫 프레임이 나간다. **복구 직후가 가장 급한 순간인데 거기서 늦어진다.**
  awaitingSendOk = false;
  // ★ 운영 계수 — **전송이 안 되어 링크를 다시 세우는 모든 경로가 여기를 지난다.**
  //   그래서 여기 한 곳에서만 세면 중복도 누락도 없다(이 함수의 존재 이유 그대로다).
  if (linkDrops < 65535) linkDrops++;
  // ⚠ 여기서 멱등 캐시를 비우지 않는다. 이 판정은 **추정**이고, 링크가 실은 살아 있었다면
  //   재시도에 ALREADY CONNECTED 가 와서 그대로 복귀한다 — 그 경우 캐시를 비웠다면
  //   살아 있는 연결의 멱등성(REQ-0035 [18]-4)이 깨진다.
  //   캐시는 실제 연결 생명주기 신호인 CLOSED / CONNECT 에서만 비운다(REQ-0036).

  // ★ REQ-0051: **CIPSTART 가 아니라 CIPCLOSE 부터** 간다.
  //   CIPSTART 는 ESP 의 낡은 소켓을 절대 못 지운다 — ALREADY CONNECTED 만 돌아온다.
  //   닫히면 ESP 가 CLOSED 를 내고, 그건 기존 경로가 이미 올바르게 처리한다
  //   (오프라인 확정 + 캐시 비움 + CIPSTART 재시도) → **정상 생명주기 신호에 다시 올라탄다.**
  staleSocket = true;
  netStep = NET_CIPCLOSE;
  netStepAt = millis();
  netStepWait = 0;                   // 즉시 닫기를 시도한다
}

static void goOffline(void) {
#if DEBUG
  Serial.print(F("[NET] 전송 "));
  Serial.print(sendFailStreak);
  Serial.println(F("회 연속 실패 → 오프라인 전환. 낡은 소켓부터 닫는다(CIPCLOSE)"));
#endif
  startSocketRecovery();
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
// ── 프롬프트 대기 결과는 **셋이 아니라 넷**이다 (REQ-0116) ──
// 예전에는 bool 이었다 — `>` 를 봤는가 아닌가. 그래서 **ESP 가 "지금 바쁘다"고 명시적으로
// 답한 것**과 **아무 답도 없는 것**이 같은 값(false)으로 뭉개졌고, 둘 다 똑같이 실패로 세어
// 3번이면 멀쩡한 소켓을 끊었다. 링크는 문제가 없는데 우리가 끊는 것 — **자해**였다.
// (실측 2026-08-16: `busy s...` 거부가 `send_fail` 로 계상되는 것을 AT 로그로 확인)
static const uint8_t PROMPT_OK      = 0;   // '>' 수신 — ESP 는 **데이터 모드**다
static const uint8_t PROMPT_BUSY    = 1;   // "busy" — 거부됐다. ESP 는 **명령 모드**다
static const uint8_t PROMPT_REJECT  = 2;   // "ERROR" — 거부됐다. ESP 는 **명령 모드**다
static const uint8_t PROMPT_TIMEOUT = 3;   // 무응답 — **데이터 모드일 수 있다**(더미로 마감해야 한다)

static inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool waitForPromptIsReject(char c, uint8_t* mBusy, uint8_t* mErr, uint8_t* out) {
  // ⚠ `pendLine` 을 보지 않고 **바이트 흐름에서 직접** 찾는 이유:
  //   송신 중에는 완성된 줄이 pendLine 에 **첫 줄 하나만** 담기고 나머지는 버려진다
  //   (feedRxChar). 앞선 줄이 자리를 차지하고 있으면 정작 이 CIPSEND 의 `busy` 응답이
  //   버려져 못 본다. 그래서 기존 버퍼 의미를 건드리지 않고 여기서 따로 훑는다.
  // ⚠ 대소문자를 가리지 않는 이유: 실측에서 `ERROR` 와 `Error` 가 **둘 다** 관측됐다.
  static const char W_BUSY[] = "busy";
  static const char W_ERR[]  = "error";
  char lc = lowerAscii(c);

  *mBusy = (lc == W_BUSY[*mBusy]) ? (uint8_t)(*mBusy + 1) : (uint8_t)(lc == W_BUSY[0] ? 1 : 0);
  if (W_BUSY[*mBusy] == '\0') { *out = PROMPT_BUSY; return true; }

  *mErr  = (lc == W_ERR[*mErr])  ? (uint8_t)(*mErr + 1)  : (uint8_t)(lc == W_ERR[0]  ? 1 : 0);
  if (W_ERR[*mErr] == '\0')  { *out = PROMPT_REJECT; return true; }

  return false;
}

static uint8_t waitForPrompt(void) {
  unsigned long t0 = millis();
  uint8_t mBusy = 0, mErr = 0, verdict = PROMPT_TIMEOUT;
  while (millis() - t0 < PROMPT_TIMEOUT_MS) {
    while (wifi.available()) {
      char c = (char)wifi.read();
      if (c == '>') return PROMPT_OK;
      feedRxChar(c);                 // 대기 중 들어온 데이터도 버리지 않는다
      // 거부가 확인되면 **더 기다리지 않는다.** 이 CIPSEND 에 `>` 는 오지 않는다.
      if (waitForPromptIsReject(c, &mBusy, &mErr, &verdict)) return verdict;
    }
  }
  return PROMPT_TIMEOUT;              // 포기는 정상 동작이다 — 다음 하트비트가 곧 온다
}

// line 은 LF 없는 문자열. LF 는 여기서 붙인다(전선 종단은 LF 하나 — §2.1)
static bool sendLine(const char* line) {
  // ★ `ramProbe()` 는 **조기 반환보다 앞**에 있어야 한다. 뒤에 두면 오프라인 동안 한 번도 안 불려
  //   `[RAM]` 이 초기값 65535 를 찍고, 소크 로그를 나중에 읽는 사람이 계측 고장으로 오해한다.
  //   (실제로 그렇게 찍혔다 — REQ-0064 관측 중 발견.)
  ramProbe();
  if (!netOnline) return false;
  uint8_t len = (uint8_t)strlen(line);
  if (len == 0 || len > 63) return false;                  // §2.1 한 줄 최대 64바이트(LF 포함)

  // ── 2단계: 앞 전송이 아직 안 끝났으면 **이번 주기는 보내지 않는다** ────────
  // ⚠ 이것은 **실패가 아니다.** ESP 가 아직 보내는 중이라 우리가 스스로 양보한 것이다.
  //   그래서 `noteSendResult()` 를 부르지 않는다 — 부르면 정상 동작을 자해로 세게 된다.
  //   (이 갈래는 `sendLine` 의 나머지를 통째로 건너뛰므로 카운터에 닿지 않는다.)
  // ⚠ 그럼에도 **막히면 반드시 빠져나온다**: 여기서 계속 건너뛰면 `lastTxOkAt` 이 갱신되지
  //   않아 살아있음 불변식(10초)이 발동한다. 아래 상한(3초)은 그보다 먼저 푸는 1차 방어다.
  if (awaitingSendOk) {
    if ((uint32_t)(millis() - sendOkWaitFrom) < SEND_OK_TIMEOUT_MS) {
      if (sendSkips < 65535) sendSkips++;
#if DEBUG
      Serial.println(F("[TX-WAIT] 앞 전송의 SEND OK 를 아직 못 봤다 — 이번 주기는 건너뛴다"));
#endif
      return false;
    }
    // 상한 초과 — `SEND OK` 를 못 받고 있다. 풀어 주고 **예전 동작(SEND_GAP_MS)으로 되돌아간다.**
    awaitingSendOk = false;
    if (sendOkTimeouts < 65535) sendOkTimeouts++;
#if DEBUG
    Serial.println(F("[TX-WAIT] ★ SEND OK 상한 초과 → 대기를 푼다(SEND_GAP_MS 시절 동작으로 복귀)"));
#endif
  }

  inSend = true;
  // ★ `SEND_GAP_MS` 는 **없애지 않는다. 하한으로 남긴다.**
  //   `SEND OK` 가 즉시 와도 연속 CIPSEND 를 너무 촘촘히 쏘면 `busy p...`(앞 **명령** 처리 중)가
  //   난다 — 이 값은 원래 그걸 막으려고 있던 것이고 2단계가 겨냥하는 `busy s...`(앞 **전송**
  //   처리 중)와 **다른 사건**이다. 둘을 같은 것으로 보고 지우면 옛 결함이 되살아난다.
  while (millis() - lastSendEndAt < SEND_GAP_MS) pumpSerialRaw();   // 연속 CIPSEND 간격

  wifi.print(F("AT+CIPSEND="));
  wifi.print((unsigned int)(len + 1));                     // +1 = LF 도 전선에 나간다
  wifi.print(F("\r\n"));

  uint8_t pr = waitForPrompt();
  bool ok = (pr == PROMPT_OK);
  if (ok) {
    wifi.write((const uint8_t*)line, (size_t)len);
    wifi.write('\n');
    // ★ 2단계: 여기서부터 ESP 가 **실제로 WiFi 로 보내는 중**이다. 그 끝을 알려주는 것이
    //   `SEND OK` 다. 추측(80ms) 대신 그 신호를 기다린다 — 단, 비블로킹으로.
    awaitingSendOk = true;
    sendOkWaitFrom = millis();
  } else if (pr != PROMPT_TIMEOUT) {
    // ─────────────────────────────────────────────────────────────────────
    // ★ REQ-0116 — **거부됐을 때는 더미를 넣지 않는다.**
    //
    // `busy`/`ERROR` 는 ESP 가 **명령을 받지 않았다는 확답**이다. 즉 ESP 는 **명령 모드**에
    // 그대로 있고 **아무 바이트도 기다리지 않는다.** 여기에 더미를 쓰면 그 바이트가
    // **AT 명령으로 해석되어** `ERROR` 를 낳고 스트림을 더 어지럽힌다.
    // (실측 2026-08-16: `[AT] "#####…"` 가 **에코로 되돌아오고** 뒤에 `Error`/`busy s...` 가 붙었다.
    //  `SEND OK` 는 한 번도 없었다 — 더미가 소켓이 아니라 AT 해석기로 갔다는 직접 증거다.)
    //
    // 더미는 **아래 PROMPT_TIMEOUT 갈래에서만 옳다.** 거기서는 ESP 가 데이터 모드에 있을
    // 수 있어 약속한 길이를 채워야 하지만, 여기서는 채울 약속 자체가 없다.
    // ─────────────────────────────────────────────────────────────────────
#if DEBUG
    if (pr == PROMPT_BUSY) {
      // ★ 이 줄은 monitor 가 세는 지표다. `busy` 는 고장이 아니라 "나중에"라는 뜻이다.
      Serial.println(F("[TX-BUSY] ESP 가 바쁘다(거부) — 실패로 세지 않는다. 다음 주기에 다시 보낸다"));
    } else {
      Serial.println(F("[TX-REJECT] CIPSEND 거부(ERROR) — 더미를 넣지 않는다"));
    }
#endif
  } else {
    // ─────────────────────────────────────────────────────────────────────
    // ★ REQ-0064 — **프롬프트를 놓쳤을 때 스트림 동기를 회복한다.**
    //
    // 실기에서 4분마다 링크가 끊긴 원인이 정확히 여기였다. 관측된 사슬:
    //   `AT+CIPSEND=38` 송신 → 300ms 안에 `>` 못 받음 → 페이로드를 **안 씀**
    //   → **그런데 ESP 는 여전히 38바이트를 기다린다**
    //   → 다음 루프의 `AT+CIPSEND=38\r\n` 이 **페이로드로 먹힌다**
    //   → 로그에 `"...P1,33AT+CIPSEND=38"` 처럼 **두 줄이 붙어 찍힌다**(그 증거)
    //   → 이후 연쇄 실패 3회 → goOffline → 링크 재수립
    //
    // 고치는 방향은 타임아웃 상향이 **아니다.** 그건 증상만 늦출 뿐 같은 사슬이 남는다.
    // **약속한 길이만큼 버릴 바이트를 채워 진행 중인 CIPSEND 를 끝내고 명령 모드로 되돌린다.**
    //
    // 채움 문자를 `#` 으로 고른 이유: 서버가 **반드시 버리는** 바이트라 우연히 유효 프레임으로
    // 파싱될 가능성이 없다.
    //
    // ✏️ 2026-08-16 정정 — 예전에 여기 *"체크섬 이전에 **타입**에서 걸린다"* 고 적혀 있었는데
    //   **사실과 반대다.** 서버의 검사 순서는 **길이(64B) → 체크섬 → 타입** 이고
    //   (`server.cpp:906/1429/1541`, socket-engineer 확인), `###…` 은 쉼표가 없어
    //   `verify_line()` 의 체크섬 단계에서 즉시 걸린다. **타입 검사까지 가지도 못한다.**
    //   → 결론(반드시 버려진다)은 그대로지만 **근거가 틀렸었다.** 그리고 그 말은
    //     **채움 문자를 무엇으로 고르든 들어가는 칸이 같다**는 뜻이다.
    //   ⚠ 교훈: 이 주석은 **펌웨어가 서버 동작을 확인 없이 가정한 것**이었다.
    //     남의 도메인 동작을 근거로 쓸 때는 그쪽에 확인하고 출처를 적어라.
    //
    // ⚠ ESP 가 실은 명령 모드였다면(CIPSEND 가 안 먹힌 경우) 이 바이트들은 AT 명령으로
    //   해석된다. 예전 주석은 이것을 **"무해하다"** 고 단정했는데 **그것도 낙관이었다** —
    //   실측에서 스트림이 더 엉키는 것이 관측됐다. 그래서 REQ-0116 부터는 **거부가 확인되면
    //   아예 이 갈래로 오지 않는다**(위 PROMPT_BUSY/PROMPT_REJECT 처리).
    //   여기 남은 것은 **정말로 아무 답이 없었던 경우**뿐이고, 그때는 ESP 가 데이터 모드일
    //   수 있으므로 채우는 쪽이 여전히 안전하다.
    // ─────────────────────────────────────────────────────────────────────
    for (uint8_t i = 0; i < len; i++) wifi.write('#');
    wifi.write('\n');                                      // 합계 len+1 = 약속한 길이와 정확히 같다
    if (promptResyncs < 65535) promptResyncs++;
#if DEBUG
    Serial.print(F("[TX-RESYNC] 프롬프트 놓침 → 더미 "));
    Serial.print((unsigned int)(len + 1));
    Serial.println(F("바이트로 스트림 복구(서버는 이 줄을 버린다)"));
#endif
  }
  lastSendEndAt = millis();
  inSend = false;

#if DEBUG
  Serial.print(ok ? F("[TX] ") : F("[TX-DROP] "));
  Serial.println(line);
#endif
  // ★ REQ-0049: 이 결과가 유일한 연속 실패 신호다. 여기서만 센다.
  //   goOffline() 이 netOnline 을 내릴 수 있으므로 inSend 를 내린 **뒤**에 부른다.
  //
  // ★ REQ-0116: **`busy` 는 세지 않는다.** 연속 실패 카운터의 뜻은 "링크가 죽었을 것이다"인데,
  //   `busy` 는 ESP 가 **살아서 응답한** 것이라 링크 상태에 대한 증거가 아니다. 그걸 세면
  //   ESP 가 바쁜 몇 초 동안 3회가 채워져 **멀쩡한 소켓을 우리가 끊는다**(= 자해).
  //   ⚠ 성공도 아니므로 **0 으로 되돌리지도 않는다.** 앞선 진짜 실패가 있었다면 그대로 남는다
  //     — 그래서 `noteSendResult(true)` 가 아니라 **호출 자체를 건너뛴다.**
  //   `ERROR`(PROMPT_REJECT)는 그대로 센다. 명령이 실제로 거부된 것이고 링크 이상일 수 있다.
  if (pr != PROMPT_BUSY) noteSendResult(ok);

  // ★ REQ-0116 살아있음 불변식의 기준점 갱신.
  //   `lastTxOkAt` 은 **실제로 나간** 프레임에서만 갱신된다 — `busy` 를 안 세는 것과 달리
  //   여기서는 `busy` 도 "못 나갔다"로 취급한다. 세는 것과 나간 것은 다른 문제다.
  if (ok) {
    lastTxOkAt = millis();
    stallBusy = stallTimeout = stallReject = 0;
  } else if (pr == PROMPT_BUSY)   { if (stallBusy    < 255) stallBusy++;    }
    else if (pr == PROMPT_REJECT) { if (stallReject  < 255) stallReject++;  }
    else                          { if (stallTimeout < 255) stallTimeout++; }
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
  ramProbe();                     // 수신 경로의 가장 깊은 지점 — 여기서 재는 것이 의미가 있다
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
// ─────────────────────────────────────────────────────────────────────────
// ⚠⚠ 구형 ai-thinker 펌웨어(AT v0.018 / SDK 0.9.2)의 **어휘가 다르다.** 실측으로 확인했다.
//     문서(ESP-AT v2.x)만 보고 문자열을 정하면 이 보드에서는 그 분기가 **영원히 안 걸린다.**
//
//   문서/신형          이 보드의 실제 출력        확인된 로그
//   ─────────────      ────────────────────      ─────────────────────────
//   ALREADY CONNECT →  **ALREAY CONNECT**        [AT] "ALREAY CONNECT" (14)   ← D 가 빠진 펌웨어 오타
//   CLOSED          →  **Unlink**                [AT] "Unlink" (6)
//   CONNECT         →  Linked                    [AT] "Linked" (6)            (이미 처리돼 있었다)
//
// 앞의 둘을 못 잡으면 REQ-0051 의 낡은 소켓 사다리와 REQ-0036 의 캐시 비우기가 **둘 다 죽는다.**
// 실제로 죽어 있었다 — 복구가 되긴 했지만 의도한 즉시 분기가 아니라 CIPSTART 대기 상한(5초)이
// 만료되는 경로로 우회해서였다. 즉 매 복구마다 불필요한 5초를 태우고 있었다.
// ─────────────────────────────────────────────────────────────────────────
static bool isAlreadyConnectLine(const char* s) {
  // `ALREADY` 와 `ALREAY` 를 함께 받는다. 접두를 요구하므로 평범한 `CONNECT` 는 여기 안 걸린다.
  return strstr(s, "ALREA") != NULL && strstr(s, "CONNECT") != NULL;
}

static bool isClosedLine(const char* s) {
  return strstr(s, "CLOSED") != NULL || strstr(s, "Unlink") != NULL || strstr(s, "UNLINK") != NULL;
}

static bool isConnectLine(const char* s) {
  while (*s == ' ' || *s == '\t') s++;                       // 앞 공백
  if (s[0] >= '0' && s[0] <= '4' && s[1] == ',') s += 2;     // "<링크ID>," 접두
  uint8_t n = (uint8_t)strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;  // 뒤 공백

  if (n == 7 && eqNoCase(s, "CONNECT", 7)) return true;
  if (n == 6 && eqNoCase(s, "LINKED",  6)) return true;
  return false;
}

// 프레임 후보 한 줄을 검사해서 처리한다 (§6.2 3·4단계). `+IPD` 경로와 평문 경로가 **공유**한다.
static void handleFrameLine(char* cand) {
  uint8_t len = (uint8_t)strlen(cand);
  if (len == 0 || len > 63) return;                    // §2.1 한 줄 최대 64바이트(LF 포함)

  // 타입 문자. 모르는 타입은 조용히 버린다(§2.1-7)
  if (cand[0] != 'R' && cand[0] != 'C' && cand[0] != 'T' && cand[0] != 'M') return;

  // 체크섬. AT 잡음이 우연히 R 로 시작해도 여기서 걸린다
  if (!checksumOk(cand, len)) {
#if DEBUG
    Serial.print(F("[CKSUM NG] ")); Serial.println(cand);
#endif
    return;
  }
  processCommand(cand);
}

static void handleLine(char* s) {
#if DEBUG
  // ★ REQ-0042 1순위: **받은 줄을 전부 찍는다.** 예전에는 WIFI 로 시작하는 줄만 찍어서
  //   모듈이 실제로 무엇을 응답하는지 아무도 볼 수 없었다.
  dbgLineCnt++;
  Serial.print(F("[AT] "));
  dbgLine(s, (uint8_t)strlen(s));
#endif

  // ═══════════════════════════════════════════════════════════════════════
  // ★ 데이터 줄이면 **AT 해석을 일절 거치지 않고** 곧장 프레임 처리로 간다.
  //
  // 왜 맨 앞인가 — 이건 결함 하나가 아니라 **결함 유형**을 닫는 자리다.
  // 아래 AT 해석부는 `Unlink`·`CLOSED`·`busy`·`no ip`·`ERROR`·`SEND FAIL` 같은 문자열을
  // **부분일치**로 찾는다. 서버가 보낸 프레임 안에 그 단어가 섞이면(특히 userid 같은 자유 필드)
  // 프레임이 AT 응답으로 오인돼 **통째로 삼켜지고**, 심하면 `netOnline` 까지 뒤집힌다.
  // 소문자인 `busy`·`no ip` 는 사람이 쓰는 문자열에 섞일 확률이 특히 높다.
  //
  // 키워드마다 가드를 하나씩 붙이는 방식은 **다음에 키워드를 추가할 때 또 뚫린다.**
  // 그래서 개별 방어가 아니라 순서로 막는다: 데이터 줄은 애초에 AT 해석부에 도달하지 않는다.
  //
  // 트레이드오프(작지만 기록해 둔다): `Unlink+IPD,...` 처럼 AT 응답과 데이터가 한 줄에 붙어 오면
  // 이제 AT 쪽을 놓친다. 그러나 링크 이벤트를 놓치는 것은 전송 실패 감지(REQ-0049)가 받아 주는 반면,
  // 데이터를 링크 이벤트로 오인하는 것은 **상태를 오염시킨다.** 놓치는 쪽이 안전하다.
  // ═══════════════════════════════════════════════════════════════════════
  {
    char* ipd = strstr(s, "+IPD,");
    if (ipd) {
      char* colon = strchr(ipd, ':');
      if (!colon) return;                 // 길이 필드가 안 끝났다 — 쓸 수 없는 줄
      handleFrameLine(colon + 1);         // `+IPD,<len>:` 도 `+IPD,<id>,<len>:` 도 첫 `:` 뒤가 본문이다
      return;
    }
  }

  // (a) 접속 상태 키워드. "WIFI CONNECTED" 를 TCP CONNECT 로 오인하지 않는다.
  //     ⚠ 이 제외는 되살리면 안 되는 버그를 막는 자리다 — 지우지 마라.
  //
  //     ★ REQ-0064 ①: **버리기 전에 읽는다.** 예전에는 여기서 곧장 return 이라
  //     `WIFI GOT IP`(성공)와 `WIFI DISCONNECT`(끊김)까지 같이 버렸고, 그래서 코드에
  //     "와이파이가 붙었는가"를 알 수단이 아예 없었다. return 은 그대로 두고 — 지우면
  //     `WIFI CONNECTED` 가 isConnectLine 까지 흘러가 옛 오인 버그가 되살아난다 —
  //     그 앞에서 상태만 걷어 간다.
  //     (⚠ 이 보드의 구형 펌웨어는 WIFI * 줄을 내지 않는다. 신형 펌웨어 대비 경로다.)
  if (strncmp(s, "WIFI", 4) == 0) {
    if (strstr(s, "GOT IP")) {
      netHasIp = true;
      cwjapFails = 0;
      cwjapPending = false;             // 결합이 끝났다 — 무응답 판정 대상에서 뺀다
      assocAt = millis();               // 결합 유지시간 계측 시작 (REQ-0071 0단)
#if DEBUG
      Serial.println(F("[NET] WIFI GOT IP — IP 확보. 9초 대기창을 끝까지 기다리지 않고 바로 다음 단계로"));
#endif
      if (!netOnline) netAdvance(NET_CIFSR, 200);
    } else if (strstr(s, "DISCONNECT")) {
      netHasIp = false;                 // ★ 전제조건이 깨졌다 — CIPSTART 를 막는다
#if DEBUG
      Serial.println(F("[NET] WIFI DISCONNECT — IP 를 잃었다"));
#endif
      // ★ REQ-0071: 여기서 곧장 CWJAP 로 되돌아가던 것이 **같은 자극의 반복**이었다.
      //   결합이 끊긴 것은 그 자체로 실패 사건이므로 사다리가 다음 조치를 정하게 한다.
      //   (이 보드의 구형 펌웨어는 WIFI 줄을 내지 않는다 — 신형 대비 경로다.)
      if (!netOnline) { cwjapPending = false; ladderFail(LF_CWJAP_FAIL); }
    }
    return;                             // ★ 유지 (위 경고 참조)
  }

  // ── (a2) 사다리 응답 처리 (REQ-0064) ─────────────────────────────────────
  // 어떤 명령에 대한 답인지는 netStep 이 아니라 **netLastSent** 로 본다.
  // netStep 은 이미 "답이 없으면 갈 곳"으로 앞서 가 있기 때문이다.
  if (!netOnline && netLastSent < NET_STEP_COUNT) {

    // `busy p...` = **앞 명령이 아직 안 끝났다.** 이번 사고의 핵심 증거다.
    // 전진하면 안 된다 — 전진했기 때문에 CIPMUX 가 통째로 씹혔다.
    if (strstr(s, "busy")) {
#if DEBUG
      Serial.println(F("[NET] busy — ESP 가 앞 명령을 처리 중이다. 전진하지 않는다"));
#endif
      // CWJAP 대기 중이면 재전송도 하지 않는다. 진행 중인 결합을 방해할 뿐이고,
      // 결과(OK/FAIL)는 어차피 곧 온다.
      // ⚠ 되쏘아도 되는 단계만 되쏜다.
      //   · CWJAP  — 진행 중인 결합을 방해할 뿐이다. 결과(OK/FAIL)는 곧 온다
      //   · CWLAP  — 재스캔은 9초를 통째로 태우고 그동안 결합이 불가능하다.
      //              lapDone 이 이미 서 있어 "한 번만 스캔한다"는 규칙도 깨진다
      //   · CIPCLOSE — 사다리 상승 계수(closeAttempts)를 우회해 무한 반복이 된다
      // ⚠⚠ **`NET_RST` 은 여기 있으면 안 된다. 실측으로 자해가 확인됐다.**
      //   `busy` 대응의 뜻은 **"기다렸다 다시 물어본다"** 인데, `AT+RST` 는 물어보는 게 아니라
      //   **"처음부터 다시 시작해라"** 다. 성질이 다른 명령을 같은 목록에 둔 것이 결함이었다.
      //   실측(REQ-0064): 부팅 직후 ESP 가 앞선 CWJAP 를 처리하는 12초 동안
      //   **`AT+RST` 를 1.5초마다 8회** 되쏘고 있었다 — **진행 중인 결합을 매번 죽인 것**이다.
      //   RST 는 되쏘지 않는다. 응답이 없으면 NET_WAIT[NET_RST](2500ms)가 알아서 재시도한다.
      switch (netLastSent) {
        case NET_CIFSR: case NET_CIPMUX: case NET_CIPSTART:
          netAdvance(netLastSent, 1500);   // 이 셋은 되쏴도 상태를 망가뜨리지 않는 질의/설정이다
          break;
        default:
          break;                        // 기다린다 — 해당 단계의 대기 상한이 알아서 처리한다
      }
      return;
    }

    // CWJAP 의 결과
    if (netLastSent == NET_CWJAP) {
      // 신형 펌웨어의 사유 코드. 이 보드에는 안 오지만 오면 그대로 사람 말로 찍는다.
      if (strncmp(s, "+CWJAP:", 7) == 0) {
#if DEBUG
        Serial.print(F("[NET] ★ CWJAP 실패 사유: "));
        switch (s[7]) {
          case '1': Serial.println(F("접속 시간초과")); break;
          case '2': Serial.println(F("비밀번호가 틀렸다")); break;
          case '3': Serial.println(F("그 SSID 의 AP 를 못 찾았다")); break;
          case '4': Serial.println(F("접속 실패")); break;
          default:  Serial.println(s); break;
        }
#endif
        return;                          // 계수는 뒤따라오는 FAIL 에서 한다
      }
      if (strstr(s, "FAIL") || strstr(s, "ERROR")) {
        if (cwjapFails < 250) cwjapFails++;
        netHasIp = false;
        cwjapPending = false;            // 답이 왔다 — 무응답 판정 대상에서 뺀다
#if DEBUG
        Serial.print(F("[NET] ★ CWJAP 실패 "));
        Serial.print(cwjapFails);
        Serial.println(F("회 (구형 펌웨어는 사유를 주지 않는다)"));
#endif
        // ★ REQ-0071 — 여기가 사다리의 주 입구다.
        //   예전에는 이 자리에서 곧장 CIFSR→CWJAP 로 돌아갔고, 그래서 91회를 되풀이했다.
        //   이제는 **몇 번째 실패인지에 따라 조치가 달라진다.** 진단 사슬 1회 삽입도
        //   ladderFail() 안으로 옮겼다(판정이 한 곳에만 있어야 어긋나지 않는다).
        ladderFail(LF_CWJAP_FAIL);
        return;
      }
      if (strcmp(s, "OK") == 0) {
#if DEBUG
        Serial.println(F("[NET] CWJAP OK — 결합됐다. IP 를 실제로 받았는지 CIFSR 로 확인한다"));
#endif
        cwjapFails = 0;
        cifsrTries = 0;
        cwjapPending = false;
        assocAt = millis();              // 결합 유지시간 계측 시작 (REQ-0071 0단)
        netAdvance(NET_CIFSR, 300);
        return;
      }
    }

    // 2단(CWQAP)의 결과. 구형 펌웨어는 결합이 없으면 `no ap` 를 먼저 내기도 한다 — 셋 다 완료다.
    if (netLastSent == NET_CWQAP &&
        (strcmp(s, "OK") == 0 || strstr(s, "ERROR") || strstr(s, "no ap"))) {
#if DEBUG
      Serial.println(F("[LADDER] 2단: 결합 해제 완료 → 깨끗한 상태에서 CWJAP 재시도"));
#endif
      netAdvance(NET_CWJAP, 500);
      return;
    }

    // CIFSR 의 결과 — **여기가 IP 확보의 단일 판정점이다.**
    if (netLastSent == NET_CIFSR) {
      if (strncmp(s, "AT+", 3) != 0 && hasUsableIp(s)) {   // 명령 에코는 제외
        netHasIp = true;
        cifsrTries = 0;
        ipLossLatched = false;              // ★ IP 를 실제로 되찾았다 — 다음 소실은 새 사건이다
        if (!assocAt) assocAt = millis();   // 결합 유지시간 계측 시작 (REQ-0071 0단)
#if DEBUG
        Serial.print(F("[NET] ★ IP 확보: "));
        Serial.println(s);
#endif
        netAdvance(NET_CIPMUX, 200);
        return;
      }
      // ★ REQ-0125 — **ESP 가 상태를 통째로 잃은 지문.** (진단 전용, 제어에 쓰지 않는다)
      //   `hasUsableIp()` 는 `0.0.0.0` 을 IP 로 치지 않으므로 위 갈래에서 이미 떨어진다.
      //   여기서는 **그것을 세기만** 한다 — 흐름은 기존 `cifsrTries` 경로가 그대로 처리한다.
      //   ⚠ 세는 것과 끊는 것을 섞지 않는다. 사다리는 **원인이 아니라 결과(IP 가 있는가)** 를
      //     보기 때문에 이름 모를 고장에도 동작한다(§6.1). 그 성질을 깨지 않으려는 것이다.
      else if (strncmp(s, "AT+", 3) != 0 && strcmp(s, "0.0.0.0") == 0) {
        noteIpLoss();                    // 판별자 ① — 응답이 멀쩡할 때 잡힌다
      }
    }

    // CIPMUX 의 결과. 구형 펌웨어는 이미 그 값이면 `no change` 로 답한다 — 둘 다 성공이다.
    if (netLastSent == NET_CIPMUX && (strcmp(s, "OK") == 0 || strstr(s, "no change"))) {
#if DEBUG
      Serial.println(F("[NET] CIPMUX=0 적용됨 → CIPSTART"));
#endif
      netAdvance(NET_CIPSTART, 200);
      return;
    }

    // CWCOUNTRY 우회 시도의 결과. **ERROR 가 나오는 것이 정상이자 결론이다.**
    if (netLastSent == NET_CWCOUNTRY && (strcmp(s, "OK") == 0 || strstr(s, "ERROR"))) {
#if DEBUG
      if (strcmp(s, "OK") == 0) {
        Serial.println(F("[NET] ★ CWCOUNTRY 가 먹었다 — 규제도메인 1~13. 채널 12 결합이 열렸을 수 있다"));
      } else {
        Serial.println(F("[NET] CWCOUNTRY 미지원(ERROR) — 이 펌웨어로는 채널 12/13 을 코드로 못 연다."));
        Serial.println(F("[NET]   → 공유기 채널을 1/6/11 로 바꾸는 것이 확정 해법이다"));
      }
#endif
      netAdvance(NET_CWLAP, 300);
      return;
    }

    // CWLAP 스캔 종료. 이 판정 한 줄이 "비번이냐 AP 냐" 를 가른다.
    if (netLastSent == NET_CWLAP) {
      if (strncmp(s, "+CWLAP:", 7) == 0) {
        if (strstr(s, WIFI_SSID)) lapFound = true;
        return;                          // 목록 자체는 위 [AT] 로그가 이미 다 찍었다
      }
      if (strcmp(s, "OK") == 0 || strstr(s, "ERROR")) {
        lapDone = true;
#if DEBUG
        Serial.println(F("[NET] ── AP 스캔 판정 ──────────────────────────"));
        if (lapFound) {
          Serial.println(F("[NET] 설정한 SSID 가 목록에 **있다** → AP 는 보인다. 결합만 거부된다."));
          // ⚠ 순서 주의: 위 +CWLAP 줄의 **마지막 숫자가 채널**이다. 12 나 13 이면 그것이 1순위다.
          //   ESP8266 은 12/13 을 스캔으로는 보면서 결합만 막히는 실패 양상이 알려져 있다
          //   (기본 규제도메인 US=1~11). "보였으니 붙을 수 있다"는 추론은 성립하지 않는다.
          Serial.println(F("[NET]   1순위: 위 줄의 **마지막 숫자(채널)** 를 봐라. 12/13 이면 공유기를 1/6/11 로 바꿔라"));
          Serial.println(F("[NET]   2순위: 비밀번호   3순위: PMF(802.11w)/WPA3 전환모드 끄기"));
        } else {
          Serial.println(F("[NET] 설정한 SSID 가 목록에 **없다** → 비번 문제가 아니다."));
          Serial.println(F("[NET]   2.4GHz 인가 / 전파가 닿는가 / SSID 철자가 맞는가를 봐라."));
        }
#endif
        netAdvance(NET_CWJAP, 1000);
        return;
      }
    }

    // `no ip` = IP 없이 CIPSTART 를 쏜 것이다. **재시도가 아니라 CWJAP 로 되돌아간다** (REQ-0064 ④).
    if (strstr(s, "no ip")) {
      netHasIp = false;
#if DEBUG
      Serial.println(F("[NET] no ip — IP 가 없다. CIPSTART 재시도를 접고 CWJAP 로 되돌아간다"));
#endif
      netAdvance(NET_CWJAP, 1000);
      return;
    }
  }
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
  // 여기부터는 **데이터가 아닌 줄**만 도달한다(위 +IPD 조기 처리가 걸러 냈다).
  if (isAlreadyConnectLine(s)) {
    // ★ REQ-0051: 이 응답의 의미가 **상황에 따라 정반대**다. 둘을 갈라야 한다.
    if (staleSocket) {
      // 전송 실패로 오프라인이 된 뒤라면 "붙어 있다"가 아니라
      // **"ESP 가 낡은 소켓을 붙들고 있다"** 는 신호다. 온라인으로 올리면 무한 루프가 된다.
#if DEBUG
      Serial.println(F("[NET] ALREADY CONNECTED — 낡은 소켓 의심 중이므로 믿지 않는다 → CIPCLOSE"));
#endif
      netStep = NET_CIPCLOSE;
      netStepAt = millis();
      netStepWait = 0;
      return;
    }
    // 낡은 소켓 의심이 없을 때 = **초기 접속 경합.** CIPSTART 가 두 번 나가고 첫 번째가
    // 실제로 성공한 경우라 진짜로 "이미 붙었다"는 뜻이다. 여기서는 그대로 온라인으로 받고
    // **캐시를 비우지 않는다** — 새 연결이 아니므로(REQ-0035 [18]-4 불변식).
    netOnline = true;
    onlineSince = millis();            // 사다리 복귀 판정의 기준 시각 (REQ-0071)
    lastTxOkAt  = millis();            // ★ 살아있음 불변식의 출발점 — 붙자마자 발동하지 않게 한다
    sendFailStreak = 0;
    awaitingSendOk = false;            // ★ 2단계: 새 소켓이다 — 앞 소켓의 SEND OK 를 기다리지 않는다
    closeAttempts = 0;
#if DEBUG
    Serial.println(F("[NET] online (ALREADY CONNECTED · 초기 경합)"));
#endif
    return;
  }
  if (isConnectLine(s)) {
    netOnline = true;
    onlineSince = millis();            // 사다리 복귀 판정의 기준 시각 (REQ-0071)
    lastTxOkAt  = millis();            // ★ 살아있음 불변식의 출발점 — 붙자마자 발동하지 않게 한다
    sendFailStreak = 0;
    awaitingSendOk = false;            // ★ 2단계: 새 소켓이다 — 앞 소켓의 SEND OK 를 기다리지 않는다
    staleSocket = false;               // 진짜로 새로 붙었다 — 낡은 소켓이 아니다
    closeAttempts = 0;
    // TCP 까지 올라왔다는 것은 그 아래(결합·IP)가 전부 성립했다는 뜻이다 — 진단 카운터를 되돌린다.
    cwjapFails = 0;
    cifsrTries = 0;
    netHasIp = true;
    if (!assocAt) assocAt = millis();
    cacheClear();
#if DEBUG
    Serial.println(F("[NET] online (CONNECT) + 캐시 비움"));
#endif
    return;
  }
  if (isClosedLine(s)) {
    netOnline = false;
    sendFailStreak = 0;
    awaitingSendOk = false;            // ★ 2단계: 소켓이 닫혔다 — 그 SEND OK 는 영영 오지 않는다
    // 닫혔다는 통보다 — 낡은 소켓 의심이 해소됐다. 사다리도 내려온다.
    staleSocket = false;
    closeAttempts = 0;
    cacheClear();                     // ★ 주 방어선 — 위 주석 참조
    // ★ REQ-0064 — 예전에는 여기서 곧장 CIPSTART 로 갔다. 그런데 **소켓이 죽은 이유가
    //   와이파이가 끊긴 것일 수 있다**(공유기 재부팅 — 상시가동에서 가장 흔한 경우다).
    //   이 펌웨어는 `WIFI DISCONNECT` 를 내지 않으므로 netHasIp 는 참으로 남아 있고,
    //   그러면 문지기를 통과해 IP 없이 CIPSTART 가 나간다 — 고치려던 바로 그 상황이다.
    //   CIFSR 은 로컬 질의라 300ms 면 끝난다. **재진입 때마다 IP 를 다시 확인한다.**
    netStep = NET_CIFSR; netStepAt = millis(); netStepWait = 300;
    cifsrTries = 0;
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
      Serial.println(F("[NET] link is not valid → 즉시 오프라인, 낡은 소켓 닫기(CIPCLOSE)"));
#endif
      // ★ 여기도 CIPSTART 로 바로 가면 REQ-0051 의 무한 루프에 빠진다 — 같은 복구 경로를 탄다.
      startSocketRecovery();
      return;
    }
    // ── 2단계: `SEND OK` — ESP 가 **앞 전송을 실제로 마쳤다**고 알려주는 신호 ──────
    // 지금까지 이 줄을 **아무도 안 봤다.** 그래서 고정 80ms 추측으로 대신했고, 그 추측이
    // 틀리는 순간이 `busy s...` 였다. 여기서 대기를 푸는 것이 2단계의 전부다.
    // ⚠ `strstr` 이 아니라 접두 비교인 이유: 서버 프레임에 우연히 "SEND OK" 가 섞여도
    //   여기까지 오지 않지만(데이터 줄은 맨 앞에서 갈린다), 판정은 좁을수록 좋다.
    if (strncmp(s, "SEND OK", 7) == 0) {
      awaitingSendOk = false;
      return;
    }
    // ── 2단계: `SEND FAIL` — **원래 있던 구멍이다** ──────────────────────────────
    // 이건 `busy`(거부)와도 무응답(프롬프트 놓침)과도 **완전히 다른 사건**이다:
    // 프롬프트까지 정상으로 받고 페이로드도 다 썼는데 **전송이 실패한 것** = 진짜 송신 실패.
    // 그런데 sendLine 은 `>` 를 봤다는 이유로 이미 `noteSendResult(true)` 를 불러
    // **연속 카운터를 0 으로 되돌린 뒤**였다. 즉 프레임이 안 나갔는데 성공으로 세고 있었다.
    // ⚠ 이중 계수가 아니다 — 그 성공 계상을 **여기서 되돌리는** 것이다.
    if (strncmp(s, "SEND FAIL", 9) == 0) {
      awaitingSendOk = false;
      if (sendFails < 65535) sendFails++;
#if DEBUG
      Serial.println(F("[NET] ★ SEND FAIL — 페이로드까지 썼는데 전송이 실패했다. 실패로 센다"));
#endif
      noteSendResult(false);
      return;
    }
    if (strstr(s, "ERROR") || strstr(s, "busy")) {
#if DEBUG
      Serial.print(F("[NET] 송신 오류 응답(로그만, 카운터는 sendLine 이 센다): "));
      Serial.println(s);
#endif
      return;
    }
  }

  // (b) `+IPD` 가 없는 평문 줄. 한 TCP 세그먼트에 프레임이 여러 개 실려 오면 두 번째 줄부터는
  //     `+IPD` 접두가 없으므로 이 경로로 들어온다 — 그래서 같은 검사를 그대로 태운다.
  //     (T 는 개정 3, M 은 개정 5. 넷 다 R/C 와 **같은 파서**를 탄다 — §2.4 · §12B.4)
  handleFrameLine(s);
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
// 복구 사다리(REQ-0051):
//   전송 실패 → CIPCLOSE → (CLOSED 오면 정상 경로) → CIPSTART
//              → 그래도 ALREADY CONNECTED 만 오면 다시 CIPCLOSE
//              → CIPCLOSE 3회가 안 먹으면 **AT+RST 로 올라가 전체 초기화**
// 마지막 층이 없으면 또 무한 루프가 된다.
static void netTick(unsigned long now) {
  if (netOnline) {
    // ★ REQ-0071 — 사다리는 **연속 30초 온라인**이라야 0단으로 내려온다.
    //   붙는 즉시 내리면 "붙었다가 12초 뒤 끊김"이 반복될 때 영원히 0~1단만 오간다.
    if (rung != RUNG_MEASURE && (long)(now - onlineSince) >= (long)LADDER_RESET_MS) {
#if DEBUG
      Serial.print(F("[LADDER] ↓ 30초 연속 온라인 — 사다리를 0단으로 내린다 (직전 "));
      Serial.print(rung);
      Serial.println(F("단)"));
#endif
      rung = RUNG_MEASURE;
      rungFails = 0;
    }
    return;
  }

  // ★ 4단이 ESP 를 리셋으로 잡고 있는 동안에는 어떤 AT 명령도 내보내지 않는다.
  //   (잡고 있는 시간 자체가 백오프다 — 위 espRstAssert 주석 참조)
  espRstService(now);
  if (espRstHeld) return;

  if (now - netStepAt < netStepWait) return;

  // ★ REQ-0064 ② — **IP 없이는 CIPSTART 를 쏘지 않는다.**
  //   문지기를 "보내기 직전" 한 곳에 두는 이유: 사다리로 들어오는 길이 여럿(부팅·CLOSED 복구·
  //   전송실패 복구)이라 각 진입점에서 막으면 언젠가 한 곳을 빠뜨린다. 여기 하나면 전부 지난다.
  if (netStep == NET_CIPSTART && !netHasIp) {
#if DEBUG
    Serial.println(F("[NET] IP 가 없다 → CIPSTART 를 보내지 않고 CWJAP 로 되돌아간다"));
#endif
    netStep = NET_CWJAP;
  }

  // ★★ REQ-0071 — **답이 오지 않은 CWJAP 도 실패다.**
  //   여기까지 왔는데 아직 cwjapPending 이면, 직전 CWJAP 가 대기 상한(30초)을 넘도록
  //   OK 도 FAIL 도 내지 않았다는 뜻이다. 예전 코드는 이 경우 아무 계수 없이 CWJAP 를
  //   다시 쐈다 — **사다리가 절대 올라가지 못하는 구멍**이 정확히 여기였다.
  //   사다리가 다음 단계를 정하므로 이번 틱은 전송하지 않고 넘긴다.
  if (netStep == NET_CWJAP && cwjapPending) {
    cwjapPending = false;
    if (cwjapFails < 250) cwjapFails++;
    ladderFail(LF_CWJAP_TIMEOUT);
    return;
  }

  uint8_t sent = netStep;
  netSendStep(sent);
  netStepAt = now;
  netStepWait = NET_WAIT[sent];

  // ⚠ 아래 분기가 정하는 것은 **"응답이 안 왔을 때 갈 곳"** 이다(예전처럼 무조건 갈 곳이 아니다).
  //   정상 전진은 handleLine() 이 응답을 보고 netAdvance() 로 시킨다.
  switch (sent) {
    case NET_CWJAP:
      // 답이 없으면 그냥 다시 묻는다. **전진하지 않는다** — 그게 이번 사고의 원인이었다.
      netStep = NET_CWJAP;
      break;

    case NET_CIFSR:
      // CIFSR 은 로컬 질의라 답이 빠르다. 세 번 물어도 쓸 IP 가 없으면 결합부터 다시.
      if (++cifsrTries >= 3) {
        cifsrTries = 0;
        // ★ 판별자 ② — **응답이 가비지여도 성립한다.** 18:48:12 리셋에서 CIFSR 응답이
        //   통째로 깨져 `0.0.0.0` 문자열이 안 나왔고, 판별자 ① 만으로는 놓쳤다(monitor 실측).
        //   "세 번 물어도 쓸 IP 가 없었다"는 **문자열이 아니라 우리 쪽 상태**라 안 깨진다.
        noteIpLoss();
#if DEBUG
        Serial.println(F("[NET] CIFSR 3회에도 IP 가 없다 → CWJAP 부터 다시"));
#endif
        netStep = NET_CWJAP;
        // ★★ REQ-0071 — 여기가 사다리의 **두 번째 구멍**이었다.
        //   `CWJAP 는 OK 인데 CIFSR 은 IP 가 없다`가 계속되면 CWJAP↔CIFSR 사이를 영원히 돈다.
        //   CWJAP 가 FAIL 을 내지 않으므로 위의 두 입구(FAIL·무응답) 어느 쪽도 열리지 않는다.
        //   이것도 명백한 실패 사건이므로 사다리에 알린다.
        ladderFail(LF_CWJAP_FAIL);
        // 0·1단의 조치는 "CIFSR 로 다시 확인"인데, 방금 세 번 물어서 없다는 것을 확인했다.
        // 그 경우에만 기존 동작(결합부터 다시)으로 되돌린다. 2단 이상의 강한 조치는 그대로 둔다.
        if (netStep == NET_CIFSR) netAdvance(NET_CWJAP, 1000);
      } else {
        netStep = NET_CIFSR;
      }
      break;

    case NET_CIPMUX:
      // 응답이 없어도 CIPSTART 로는 간다(구형 펌웨어가 조용한 경우가 있다).
      // 대신 `busy` 였다면 handleLine 이 이미 같은 단계로 되돌려 놓는다.
      netStep = NET_CIPSTART;
      break;

    // ── 진단 사슬은 응답이 없어도 다음 칸으로 넘어간다(진단이 결합을 오래 막으면 안 된다) ──
    case NET_GMR:       netStep = NET_CWCOUNTRY; break;
    case NET_CWCOUNTRY: netStep = NET_CWLAP;     break;


    case NET_CWLAP:
      // 스캔이 조용히 끝나는 펌웨어도 있다. 한 번 쐈으면 진단은 끝난 것으로 본다.
      lapDone = true;
      netStep = NET_CWJAP;
      break;

    case NET_CWQAP:
      // ⚠ 이 case 는 반드시 있어야 한다. 없으면 아래 default 가 `sent + 1` 로 가는데
      //   CWQAP 는 열거의 **마지막**이라 NET_STEP_COUNT 가 되어 NET_WAIT[] 를 범위 밖으로
      //   읽는다(조용한 메모리 오류). 답이 없어도 결합 해제는 시도된 것으로 보고 넘어간다.
      netStep = NET_CWJAP;
      break;

    case NET_CIPCLOSE:
      closeAttempts++;
      if (closeAttempts >= CLOSE_ATTEMPT_LIMIT) {
        // 로컬 명령이 세 번 연속 안 먹었다 = AT 계층이 꼬였다 → 사다리 상승
#if DEBUG
        Serial.print(F("[NET] CIPCLOSE "));
        Serial.print(closeAttempts);
        Serial.println(F("회 실패 → AT+RST 로 전체 초기화(사다리 상승)"));
#endif
        netStep = NET_RST;
        staleSocket = false;         // RST 가 모듈 상태를 통째로 지운다
        closeAttempts = 0;
        netStepWait = 200;           // 곧 RST 를 쏜다 (사다리가 더 긴 백오프로 덮어쓸 수 있다)
        // ★ REQ-0071 — 로컬 명령이 세 번 연속 안 먹은 것은 **AT 계층이 잠긴 것**이다.
        //   이것도 실패 사건이므로 사다리에 알린다. 이 통보가 없으면 소켓 복구는
        //   CIPCLOSE↔RST 사이를 자기들끼리 영원히 돌 뿐 3단 위로 올라가지 못한다.
        //   (사다리가 이미 3단 이상이면 여기서 4단으로 올라간다 = 소프트 리셋이 안 먹었다는 뜻)
        ladderFail(LF_AT_JAMMED);
      } else {
        // CLOSED 가 오면 handleLine 이 CIFSR 로 보낸다.
        // 안 오더라도 **다시 올라가는 길은 남겨 둔다**(닫힘 통보가 없는 모듈도 있다).
        // 여기도 CIPSTART 가 아니라 CIFSR 이다 — 이유는 위 CLOSED 분기의 주석과 같다.
        netStep = NET_CIFSR;
        cifsrTries = 0;
      }
      break;

    case NET_CIPSTART:
      // 응답(CONNECT / ALREADY CONNECTED / CLOSED)은 handleLine 이 처리한다.
      // 아무 응답도 안 오면: 낡은 소켓 의심 중이면 닫기부터, 아니면 다시 CIPSTART.
      netStep = staleSocket ? (uint8_t)NET_CIPCLOSE : (uint8_t)NET_CIPSTART;
      break;

    default:
      netStep = (uint8_t)(sent + 1);   // 부팅 순서 진행 (RST→CWMODE→CWJAP→CIPMUX→CIPSTART)
      break;
  }
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

  // ★ REQ-0116 — 살아있음 불변식. **이유를 묻지 않는다.**
  //   "온라인이라면서 TX_STALL_MS 동안 한 줄도 못 내보냈다"면 그 자체로 링크 이상이다.
  //   `busy`·`ERROR`·침묵·**아직 이름 없는 무엇**이든 전부 여기에 걸린다.
  //   ⚠ 아래 조기 반환(`return`)보다 **앞**에 있어야 한다. 뒤에 두면 하트비트가 뜨지 않는
  //     순간에는 검사가 건너뛰어져, 정작 아무것도 못 보내는 상황에서 발동하지 못한다.
  if (netOnline && (uint32_t)(now - lastTxOkAt) >= TX_STALL_MS) {
#if DEBUG
    // 원인은 **진단으로만** 남긴다 — 원인별 제어 경로를 만들면 다시 "아는 실패만" 막게 된다.
    Serial.print(F("[NET] ★ 정지 감지: "));
    // ⚠ `(uint32_t)` 캐스트는 실기에서는 무의미하지만(unsigned long 이 곧 32비트)
    //   호스트 테스트에서는 필요하다 — 64비트 `unsigned long` 과 32비트 `lastTxOkAt` 을
    //   섞으면 언더플로가 감싸이지 않아 1.8e19 같은 값이 찍힌다. 판정식과 같은 폭으로 맞춘다.
    Serial.print((unsigned long)(uint32_t)(now - lastTxOkAt));
    Serial.print(F("ms 동안 한 줄도 못 나갔다 (busy "));
    Serial.print(stallBusy);
    Serial.print(F(" / 무응답 ")); Serial.print(stallTimeout);
    Serial.print(F(" / 거부 "));   Serial.print(stallReject);
    // ★ 2단계 진단 — 정지의 원인이 **2단계 자신**일 수 있다. 그것을 숨기지 않는다.
    //   `건너뜀` 이 크고 `SENDOK상한` 이 0 이면 ESP 가 계속 전송 중이라는 뜻이고,
    //   `SENDOK상한` 이 크면 **`SEND OK` 를 못 받고 있다**는 뜻이라 원인이 정반대다.
    //
    // ⚠⚠ **누적 창이 다르다. 한 줄에 있다고 같은 기준으로 비교하지 마라.**
    //   위 셋(busy/무응답/거부)은 **이 정지 구간만** 센다 — 바로 아래에서 0 으로 비워진다.
    //   아래 넷은 **부팅 이후 누적**이다 — 어디서도 비우지 않는다.
    //   그래서 `건너뜀 17` 과 `busy 7` 을 나란히 놓고 크기를 비교하면 **틀린다.**
    //   (CLAUDE.md "숫자 둘을 비교하기 전에 — 어디서 시작하는가")
    //   ★ 라벨에 `누적` 을 박아 두는 이유가 이것이다. 지우지 마라.
    Serial.print(F(") · 누적[건너뜀 ")); Serial.print(sendSkips);
    Serial.print(F(" / SENDOK상한 "));  Serial.print(sendOkTimeouts);
    Serial.print(F(" / SENDFAIL "));    Serial.print(sendFails);
    Serial.print(F(" / ESP리셋 "));     Serial.print(espResets);
    Serial.println(F("] → 링크를 다시 세운다"));
#endif
    stallBusy = stallTimeout = stallReject = 0;
    startSocketRecovery();             // netOnline 을 내리고 CIPCLOSE 사다리로 간다
    return;
  }

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

  // ★ REQ-0071 4단 — 리셋선은 **놓은 상태(하이임피던스)로 시작**한다.
  //   전원 인가 직후 AVR 핀은 원래 INPUT 이라 이미 떠 있지만, "여기서 명시적으로 놓는다"를
  //   코드로 남겨 둔다. 실수로 OUTPUT LOW 로 두면 ESP 가 영원히 리셋에 잡혀 아무 일도 안 난다.
#if ESP_RST_WIRED
  pinMode(PIN_ESP_RST, INPUT);
#endif

  netStep = 0;
  netStepAt = millis();
  netStepWait = 500;

#if DEBUG
  Serial.println(F("\n[PARKING NODE] proto v1 / 10 slots / dev=" DEVICE_ID));

  // ── 부팅 원인 (REQ-0071 사실 4) — 추측을 사실로 바꾸는 한 줄 ──
  Serial.print(F("[BOOT] 리셋 원인: "));
  if (mcusrMirror == 0) {
    Serial.println(F("불명 (부트로더가 MCUSR 을 지우고 넘어왔다)"));
  } else {
    if (mcusrMirror & _BV(PORF))  Serial.print(F("전원인가(POR) "));
    if (mcusrMirror & _BV(EXTRF)) Serial.print(F("외부리셋(버튼/DTR) "));
    if (mcusrMirror & _BV(BORF))  Serial.print(F("**브라운아웃(전원부족)** "));
    if (mcusrMirror & _BV(WDRF))  Serial.print(F("워치독 "));
    Serial.println();
  }
  Serial.print(F("[BOOT] 사다리 4단(ESP 하드리셋선) "));
  Serial.print(ESP_RST_WIRED ? F("배선됨(A2)") : F("미배선 — A2 를 ESP RST 에 물리고 ESP_RST_WIRED=1"));
  Serial.print(F(" · 6단(워치독) "));
  Serial.println(ENABLE_WDT ? F("켬") : F("끔"));
#endif

#if ENABLE_WDT
  // 8초. SoftwareSerial 비트뱅잉과 waitForPrompt(300ms)를 넉넉히 덮는다.
  // (가장 긴 정지는 drainSerial 의 120ms 다 — 8초와는 두 자릿수 차이라 오발이 없다.)
  wdt_enable(WDTO_8S);
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
  // ★ REQ-0071 — 사다리의 현재 칸을 같이 찍는다. 이게 없으면 3초마다 같은 줄이 흘러갈 뿐
  //   "지금 무엇을 하며 기다리는 중인가"를 로그에서 알 수 없다.
  Serial.print(F(" 사다리="));              Serial.print(rung);
  Serial.print('/');                        Serial.print(rungFails);
  if (espRstHeld) Serial.print(F(" [ESP리셋유지중]"));
  Serial.print(F(" rx="));                  Serial.print(dbgRxBytes);
  Serial.print(F(" lines="));               Serial.print(dbgLineCnt);
  Serial.print(F(" up="));                  Serial.print(now / 1000UL);
  Serial.println(F("s"));
}
#endif

#if DEBUG
// RAM 최저 여유를 1분에 한 줄. **온라인일 때도 찍는다** — [DIAG] 는 오프라인 전용이라
// 정작 2시간 소크(=계속 온라인) 동안 아무것도 안 보이기 때문이다.
static unsigned long lastRamReport = 0;
static void ramTick(unsigned long now) {
  if (now - lastRamReport < 60000UL) return;
  lastRamReport = now;
  Serial.print(F("[RAM] 최저 여유 "));
  Serial.print(ramLow);
  Serial.print(F(" B · 프롬프트 재동기 "));
  Serial.print(promptResyncs);
  Serial.println(F("회 (서버의 '버린줄' 과 맞아야 한다)"));
}
#endif

void loop() {
#if ENABLE_WDT
  wdt_reset();                      // 6단 — 여기 못 오면(=행) 8초 뒤 AVR 이 리셋된다
#endif
  unsigned long now = millis();
  netTick(now);
  pumpSerialRaw();
  drainPending();
  sensorTick();
  statusTick(now);
  cntTick(now);                     // ★ DEBUG 밖 — 운영 빌드에서도 관측이 남는다
#if DEBUG
  diagTick(now);
  ramTick(now);
#endif
}
