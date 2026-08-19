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

#define WIFI_SSID    "3F_302"                 // 2026-08-19 이동(REQ-0252) — 서브넷 192.168.0.x
// 🔴 **대소문자 주의.** 사용자가 `3f_302`(소문자)와 `3F_302`(대문자)를 둘 다 줬고 나중 값을 쓴다.
//    **WiFi SSID 는 대소문자를 구분한다** — 안 붙으면 이것이 후보 하나다.
// 🔴🔴 **이 SSID 에는 대역 표시가 없다.** 옛 이름은 `..._2.4G` 로 대역이 박혀 있었다.
//    **ESP8266 은 2.4GHz 전용이다.** 공유기가 대역별로 이름을 가르면 2.4GHz 쪽 이름이 따로 있고,
//    그러면 이 값으로는 **영영 안 붙는다.** ⚠ **안 붙을 때 펌웨어를 먼저 의심하지 마라**(§29).
#define WIFI_PASS    "0424719222!!"           // 2026-08-19 이동(REQ-0252)
// 🔴 **앞 판 주석의 전제가 뒤집혔다.** 그때는 `!` 가 없어서 "히스토리 확장 걱정 없음"이라고 적혀
//    있었는데 **이번 값에는 `!!` 가 있다.** 이 값을 셸에 그대로 치지 마라 —
//    대화형 bash 의 큰따옴표 안에서 `!!` 는 **직전 명령으로 치환된다.**
//    ✅ 작은따옴표 · 인용된 heredoc(<<'EOF') · 편집기로 직접 (이 줄은 편집기로 넣었다)
// ⚠⚠ **앞뒤 공백을 절대 넣지 마라.** 구운 펌웨어에 `" 192.168.35.21"` 로 앞 공백이 들어가 있었고,
//    그래서 ESP 가 IP 리터럴로 못 읽고 **호스트명으로 해석해 `DNS Fail`** 을 냈다.
//    (기기 플래시를 읽어 확인한 실물 문자열: AT+CIPSTART="TCP"," 192.168.35.21",9991)
//    이 매크로는 그대로 AT 명령에 이어붙으므로 공백 하나가 곧 고장이다.
#define SERVER_IP    "192.168.0.29"   // 2026-08-19 이동(REQ-0252) · 루트가 `ipconfig getifaddr en0` 로 실측
                                      // §11 — 명세는 주소를 가정하지 않는다. 현장에서 바꾼다
                                      // ⚠ 서브넷이 192.168.35.x → **192.168.0.x** 로 통째로 바뀌었다(게이트웨이 .0.1)
#define SERVER_PORT  "9991"
#define DEVICE_ID    "P1"               // §2.3 devid ::= 1*8자. 옛 "ARD_NODE_01"(11자)은 BNF 위반이었다

// ─────────────────────────────────────────────────────────────────────────
// 타이밍 (§3.4, §6.3)
// ─────────────────────────────────────────────────────────────────────────
static const uint16_t HEARTBEAT_MS     = 1000;  // §3.4 1Hz — ⚠ 슬롯 구조에서는 안 쓴다(아래)
static const uint16_t DEBOUNCE_MS      = 100;   // §3.4 변화 디바운스
static const uint16_t PROMPT_TIMEOUT_MS= 300;   // '>' 프롬프트 대기 상한 ("수백 ms")
static const uint16_t SEND_FAIL_BACKOFF_MS = 500; // 전송 실패 후 재시도 최소 간격
static const uint8_t  SEND_GAP_MS      = 80;    // 연속 CIPSEND 사이 최소 간격 (busy p... 회피)

// ─────────────────────────────────────────────────────────────────────────
// 🔴 반송파 슬롯 (2026-08-17 · 사용자 결정 · `docs/DESIGN-slot-carrier-2026-08-17.md`)
//
//        슬롯 1200ms
//   ├───────────────┬───────────────┤
//   │  0 ~ 600ms    │  600 ~ 1200ms │
//   │  장치 송신     │  수신 전용     │
//   └───────────────┴───────────────┘
//
// **이벤트가 전송을 만들지 않는다. 전송이 상수이고 이벤트는 화물이다.**
//   옛 구조: 이벤트↑ → 전송↑ → 귀 막힘↑ → ACK 실패↑ → 서버 재전송↑ → (처음으로)
//            ⇒ 양의 되먹임. 임계를 넘으면 ESP 리셋까지 간다.
//   새 구조: 이벤트↑ → 전송 횟수 그대로 → **한 프레임의 크기만↑**
//            ⇒ 구조적으로 커질 수 없다.
//
// 🔑 **수신 창에는 한 바이트도 쓰지 않는다.** 이것이 손실 0 의 직접 원인이다
//   (fdtest 1판 `gap=36 bad=32` → 2판 `gap=0 bad=0`).
//   `SoftwareSerial` 은 송신 중 `cli()` 로 인터럽트를 끄고 수신은 그 인터럽트로만 받는다 —
//   **겹치는 순간 우리는 귀가 닫힌다.** 슬롯은 그 겹침 자체를 없앤다.
//
// ⚠ 슬롯 시작은 **시계가 아니라 사건**이다 — 장치의 프레임 도착이 서버의 t0 다.
//   그래서 시계 동기가 필요 없고, 장치가 느려지면 서버 슬롯도 같이 밀린다(자기교정).
static const uint16_t SLOT_MS          = 1200;  // 한 슬롯
static const uint16_t TX_WINDOW_MS     = 600;   // 0~600 우리 차례 · 600~1200 수신 전용
// 🔴 `[CNT]` 진단 줄이 창을 넘지 않도록 남겨 두는 시간 (REQ-0187 ②)
//   `[CNT]` 약 142B @115200 = 12.3ms 이고, TX 링버퍼(64B)를 넘는 78B 가 블로킹으로 나간다.
//   `[RAM]` 이 뒤따르는 경우까지 덮도록 여유를 준다. **송신 창 600ms 대비 8% 라 배치를 안 밀어낸다.**
static const uint16_t SLOT_TX_RESERVE_CNT_MS = 50;
// 배치 버퍼 — **한 거래에 여러 줄**을 담는다. 줄 하나의 64B 상한(§2.1)은 그대로다.
//   최대 = S 프레임 63 + LF + ACKQ_N × (ACK 최대 ~20 + LF)
//   ⚠ `AT+CIPSEND` 는 300B 까지 조용한 잘림이 없음이 실측 확인됐다(커밋 `1299286`, 18/18).
static const uint8_t  BATCH_CAP        = 160;

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
#define PIN_NONE 0xFF              // 핀 없음(가상 모듈)
static const uint8_t SLOT_PIN[SLOT_N] PROGMEM = {
  2,  3,  4,  5,  6,      // 인덱스 0..4 = A1 A2 A3 A4 A5
  9, 10, 11, 12, A0       // 인덱스 5..9 = B1 B2 B3 B4 B5   (B5 만 아날로그 핀)
};
// 🔴 **범위 가드** (2026-08-19) — `SLOT_PIN[]` 은 크기가 `SLOT_N`(실물 자리) 인데
//   `moduleCount()` 는 가상 모듈까지 세므로 **그 값으로 루프를 돌면 배열 밖을 읽는다.**
//   ⚠ 지금 모든 호출부가 `SLOT_N` 까지만 돌지만 **방어가 없으면 다음 사람이 밟는다** —
//     PROGMEM 범위 밖 읽기는 **오류 없이 쓰레기를 돌려준다.**
static inline uint8_t slotPin(uint8_t i) {
  if (i >= SLOT_N) return PIN_NONE;          // 가상 모듈에는 핀이 없다
  return pgm_read_byte(&SLOT_PIN[i]);
}

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
static bool    rxOverflow = false;     // ★ 우리 **줄 조립 버퍼**(RX_CAP) 넘침 — 아래와 다른 계층이다
static bool    pendReady = false;
// 🔴 2026-08-17 (REQ-0167) — **SoftwareSerial 링버퍼(64B) 넘침 수.**
//   `wifi.overflow()` 는 라이브러리가 이미 들고 있던 플래그인데 이 스케치가 한 번도 안 읽었다.
//   ⚠ 읽으면 지워지므로 **하한**이다(여러 번 넘쳐도 확인 지점 사이에서는 1로 뭉친다).
//   ⚠ `rxOverflow` 와 **다른 것**이다: 저건 "줄이 너무 길다", 이건 "우리가 늦게 꺼냈다".
static uint16_t ssOverflows = 0;
// ★ REQ-0218 ② — 아래 셋은 `feedRxChar()` 보다 **앞에 있어야 한다**(그 안에서 쓴다).
//   뜻과 설계 근거는 원래 자리(3단 게이트 블록)에 그대로 있다.
static bool     awaitingSendOk = false;   // 앞 전송의 SEND OK 를 아직 못 봤다
static bool     sendOkT1Passed = false;   // 이 라운드에서 T1 을 넘어 `okto` 를 이미 셌다
// ★ REQ-0218 ② — `SEND OK` 바이트 흐름 매칭 상태(1B)와 그 경로로 잡은 수.
//   `okstream` 이 크면 **줄 경로로는 놓치고 있었다는 뜻**이다 — 이 수정의 효과가 그 칸에 보인다.
static uint8_t  sendOkMatch = 0;
static uint16_t sendOkByStream = 0;
// ★ monitor 요청 — `inSend` 중 `pendLine` 이 가득 차 **버린 줄 수.**
//   **버린 줄은 로그에 안 나오므로 시리얼로는 (가)/(나)를 못 가른다.** 이 칸이 그것을 가른다.
static uint16_t pendDrops = 0;

// 🔴 2026-08-18 — **`penddrop=0` 을 읽을 수 있게 만드는 분모와 대역.**
//
// 창 I 에서 `>=64B` 1,067 거래에 `penddrop=0` 이 나왔다. **그 0 이 두 가지로 읽힌다:**
//   (A) `pendLine` 이 안 넘쳤다 (수정이 먹었다)
//   (B) `inSend` 중에 줄이 **아예 안 왔다** (셀 일이 없었다)
// ⚠ **분모가 없으면 둘을 못 가른다.** 우리 원장 §1.1 이 그 얘기다 —
//   **`0` 은 "안 일어났다"와 "못 셌다"를 구별하지 않는다.**
// → `pendFills` 가 그 분모다: `penddrop / (pendFills + pendDrops)` = 넘침률.
//   **`pendFills == 0` 이면 (B) 이고, 그때 `penddrop=0` 은 아무 뜻도 없다.**
static uint16_t pendFills = 0;

// ⚠ monitor 요청 — **대역(`<64` / `>=64`)과 함께 찍는다.**
//   `_SS_MAX_RX_BUFF = 64` 가 기전 경계이고 창 G 에서 그 위아래로 T2 율이 갈렸다
//   (`48~63` 0/43 · `>=64` 9/34). **누적 총계로는 그 갈림을 못 본다.**
// 🔑 `curTxLen` 은 **지금 보내는 중인 배치 길이**다. 수신 계수는 `inSend` 중에 일어나므로
//   이 값으로 "그때 어느 대역이었나"를 귀속할 수 있다.
static uint8_t  curTxLen     = 0;
static uint16_t pendFillsBig = 0;   // curTxLen >= 64 인 동안 담은 수
static uint16_t pendDropsBig = 0;   // curTxLen >= 64 인 동안 버린 수
static uint16_t okStreamBig  = 0;   // curTxLen >= 64 인 동안 바이트매칭이 구한 수
// 🔴 **대책 ②(바이트 매칭)의 진짜 분자.** `okstream` 이 아니다 — 위 주석 참고.
//   `oklost > 0` 이면 **줄 경로만 있었을 때 실제로 잃었을 SEND OK 가 있었다**는 뜻이고,
//   그것이 곧 T2 였다. **이 칸이 0 이면 대책 ②는 이 창에서 한 일이 없다.**
static uint16_t okLostByLine = 0;
static uint16_t okLostBig    = 0;
static const uint8_t RXBUF_THRESHOLD = 64;   // = _SS_MAX_RX_BUFF. 기전 경계이지 표본 기준이 아니다

// 🔴 2026-08-17 (REQ-0174 ①) — **줄의 첫 바이트가 도착한 시점의 슬롯 오프셋.**
//
// 왜 필요한가 — **앞 판본은 도착이 아니라 *처리* 시각을 찍고 있었다.**
//   `[SLOT-OOW]` 를 `handleLine` 안에서 뜨는데, **송신 중 완성된 줄은 `pendLine` 에 갇혔다가
//   송신이 끝난 뒤에야 처리된다**(아래 `feedRxChar`). 즉 **위험 구간에 도착한 것이
//   바로 그 이유로 늦게 찍혀** 판정에서 빠졌다. **재려던 사건을 측정 지점이 밀어냈다.**
//   실측 반증: 서버 재전송이 난 구간에서 `oow` 가 0 이었다(socket, 2차 주입).
//
// ⚠ **오프셋을 그 자리에서 계산해 저장한다.** 시각(`millis()`)을 저장했다가 나중에
//   `slotStart` 와 빼면, 그 사이 `statusTick` 이 `slotStart` 를 전진시켜 **언더플로**가 난다
//   (원장 §8.7 이 `lastTxOkAt` 에서 겪은 그것 — "앞 함수가 갱신하는 시각 변수"의 함정).
//   **뺄셈을 도착 순간에 끝내면 그 위험이 원천적으로 없다.**
static uint16_t rxLineOff   = 0;   // 지금 조립 중인 줄
static uint16_t workLineOff = 0;   // handleLine 이 지금 보는 줄
static uint16_t pendLineOff = 0;   // 송신 중이라 미뤄 둔 줄

// ★ REQ-0174 ② — 체크섬 불일치 수. **`DEBUG` 밖에서 읽혀야 한다.**
//   없으면 창 B 에서 "무손실"이 서버 계수만으로 선언된다 — 창 A 에서 실제로 그럴 뻔했다
//   (서버는 무손실, 시리얼에는 `CKSUM NG` 4건).
static uint16_t cksumNg = 0;
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
// 🔴 2026-08-18 (REQ-0204) — **8 → 16.** `ACKQ_N` 보다 커야 한다.
//   큐에 든 `rid` 의 **내용은 이 캐시에만 있다**(큐는 rid 만 담는다).
//   **캐시가 큐보다 작으면 큐에 있는데 내용이 사라져** `sendSlotBatch` 가 그것을 버린다 —
//   **큐를 늘려도 캐시가 병목이면 같은 손실이 다른 칸에서 난다.**
static const uint8_t CACHE_N = 16;
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
// 🔴 ACK 보류 큐 — **게이트가 닫혀 있을 때 ACK 가 조용히 사라지던 것을 막는다**
//    (2026-08-17 · 사용자 재현 13:20:22 · 설계서 §5-U)
//
// 무엇이 문제였나:
//   `handleLine()` 이 `cachePut()` 으로 **상태를 커밋한 뒤** `sendAck()` 를 부르는데
//   **반환값을 안 썼다.** 그리고 `sendLine()` 은 게이트가 닫혀 있으면 `false` 를 돌려준다.
//   → **장치는 하행을 받고 적용까지 했는데 ACK 만 사라진다. 아무도 재시도하지 않는다.**
//   실측: 서버가 같은 `rid` 를 3번 재전송했고 **3번 다 같은 닫힌 게이트를 만났다.**
//
// ⚠ 그래서 **서버측 재전송 강화는 이 결함의 해법이 아니다.**
// ⚠ 그리고 **게이트에서 ACK 를 면제하는 것도 해법이 아니다** — 게이트가 닫힌 이유가
//   "ESP 가 아직 전송 중"이라서다. 면제해도 `busy` 로 거부된다. **미루는 것 말고 답이 없다.**
//
// 왜 이렇게 작은가 — **내용은 이미 멱등 캐시가 갖고 있다.**
//   잃는 것은 "무엇을 보낼지"가 아니라 **"보내야 한다는 사실"** 하나뿐이다.
//   그래서 큐는 `rid` 목록이면 되고, 보낼 때 캐시에서 다시 만든다.
//   ⚠ RAM 이 빠듯하다(실측 최저 여유 782B). 프레임을 통째로 담으면 한 칸 64바이트라 32배다.
//
// 멱등 캐시와 겹치지 않는다 — **직교한다:**
//   · 캐시 = 같은 `rid` 가 **또 오면** 상태를 다시 안 바꾼다 (중복 **수신**)
//   · 큐   = 만든 답을 **못 보냈으면** 나중에 보낸다      (송신 **지연**)
// 🔴 2026-08-18 (REQ-0204) — **4 → 12.** 옛 근거가 틀렸다.
//   ~~"60초 간격이면 1이면 충분. 사람이 연달아 누르는 경우를 위해 4"~~
//   **사용자는 20~30번 연타한다.** 실측: `ackdrop +31`(서로 다른 rid 31개 — 아래 중복 방지가 있으므로).
//   서버는 그 rid 를 3회 재전송하고 `ack_timeout` 을 냈고, 화면에 **"장치가 응답하지 않았습니다"** 가 떴다.
//
// ★ 새 값의 근거는 **"몇 명이 누르나"가 아니라 "배출이 멈추는 최악 구간"** 이다:
//   배출은 슬롯마다 도는데(1.2초) **오프라인·게이트 닫힘 구간에는 0 이다.**
//   `TX_STALL_MS = 10000` 이 그 최악을 10초로 묶는다 → **10초 × 1건/초(web 전역 아이들) = 10건.**
//   여유 2 를 더해 **12**. ⚠ **유입률이 바뀌면(아이들 제거 등) 이 값도 다시 계산해야 한다.**
static const uint8_t ACKQ_N = 12;

// 🔴 **불변식: `CACHE_N >= ACKQ_N`.** 주석이 아니라 **컴파일이 막는다.**
//   큐는 `rid` 만 담고 **내용은 캐시에만 있다.** 캐시가 더 작으면 **큐에 있는데 내용이 사라져**
//   `sendSlotBatch` 가 그것을 버린다(`ackstale`) — **큐만 늘리면 손실이 다른 칸으로 옮겨갈 뿐이다.**
//   ★ 원장 §8.2-14: **규칙을 적어 두는 것과 그 규칙이 적용되게 하는 것은 다른 물건이다.**
//     그래서 문장이 아니라 **다음 사람이 큐만 올리면 빌드가 깨지는 형태**로 둔다.
static_assert(CACHE_N >= ACKQ_N,
              "CACHE_N 은 ACKQ_N 이상이어야 한다 — 큐는 rid 만 담고 내용은 캐시에 있다");
static uint16_t      ackq[ACKQ_N];
static uint8_t       ackqCount = 0;
static uint8_t       ackqHead  = 0;   // 다음에 내보낼 자리
static uint16_t      ackqDrops = 0;   // ★ **큐가 가득 차서** 밀어낸 수 (유입 초과)
// 🔴 2026-08-18 (REQ-0204) — **원인을 갈라 센다.** 예전에는 아래 둘이 같은 칸이었다:
//     ① 큐가 가득 참        → 유입이 배출보다 빠르다      → 대책: 큐/배출을 키운다
//     ② 캐시에서 밀려남      → 캐시가 큐보다 작다          → 대책: 캐시를 키운다
//   **대책이 다른데 한 칸이었다.** `ackdrop 31` 이 어느 쪽인지 아무도 몰랐다.
//   ★ 원장 §5.1 과 같은 뿌리 — **합쳐 세면 "무엇을 고쳐야 하나"를 못 읽는다.**
static uint16_t      ackqStale = 0;   // ★ **캐시에서 밀려나** 내용을 만들 수 없어 버린 수

static void ackqPush(uint16_t rid) {
  for (uint8_t i = 0; i < ackqCount; i++)                    // 같은 rid 가 이미 있으면 안 넣는다
    if (ackq[(uint8_t)((ackqHead + i) % ACKQ_N)] == rid) return;
  if (ackqCount == ACKQ_N) {
    ackqHead = (uint8_t)((ackqHead + 1) % ACKQ_N);           // 가장 오래된 것을 버린다
    ackqCount--;
    if (ackqDrops < 65535) ackqDrops++;
#if DEBUG
    Serial.println(F("[ACKQ] 가득 참 — 가장 오래된 보류 ACK 를 버린다"));
#endif
  }
  ackq[(uint8_t)((ackqHead + ackqCount) % ACKQ_N)] = rid;
  ackqCount++;
}

// ★ 소켓이 끊기면 비운다 — **새 소켓에서 옛 `rid` 에 답하면 안 된다.**
//   이 설계가 새로 만드는 유일한 위험이 그것이라 `awaitingSendOk=false` 와 같은 자리에 둔다.
static void ackqClear(void) { ackqCount = 0; ackqHead = 0; }

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
// 슬롯 상태
// ─────────────────────────────────────────────────────────────────────────
// ⚠ `slotStart` 는 **더해서** 전진시킨다(`+= SLOT_MS`). `= now` 로 하면 매 슬롯 오차가
//   누적돼 주기가 스스로 늘어난다 — 원장 §3.4 가 `lastStatusAt = millis()` 로 겪은 그것이다.
//   ★ 그 결함이 실측 주기를 1.000s 가 아니라 **1.113s** 로 만들었다. 같은 실수를 반복하지 않는다.
static uint32_t slotStart   = 0;
static uint32_t slotNo      = 0;
static bool     slotSent    = false;   // 이번 슬롯에서 이미 보냈다(슬롯당 정확히 1거래)
static uint16_t slotOow     = 0;       // 수신 창 **밖**에 하행이 도착한 수 — 설계 위반 계수
static uint16_t slotMissed  = 0;       // 송신 창을 통째로 놓친 슬롯 수(보낼 기회를 못 씀)

#include "EspLink_state.h"   // ← EspLink 링크 계층 (REQ-0264). **위치를 옮기지 마라**
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
      // ★ REQ-0174 — 줄과 **그 줄의 도착 오프셋**을 같이 미룬다. 여기서 오프셋을 안 들고 가면
      //   나중에 처리 시각으로 계산돼 위험 구간이 통째로 보이지 않는다(위 `rxLineOff` 주석).
      if (!pendReady) {
        memcpy(pendLine, rxLine, (size_t)n + 1); pendLineOff = rxLineOff; pendReady = true;
        // ★ **`penddrop` 의 분모다.** 이것이 0 이면 `penddrop=0` 은 "안 넘쳤다"가 아니라
        //   "셀 일이 없었다"이고, 그 둘은 완전히 다른 결론으로 이어진다.
        if (pendFills < 65535) pendFills++;
        if (curTxLen >= RXBUF_THRESHOLD && pendFillsBig < 65535) pendFillsBig++;
      }
      else {
        // 🔴 2026-08-18 (monitor 요청) — **버린 줄을 센다.**
        //   monitor 는 (가)"우리가 페이로드를 먼저 보냈다" 와 (나)"pendLine 이 첫 줄을 미뤄
        //   순서가 뒤집혔다" 를 **시리얼로 구분할 수 없다** — 버린 줄은 출력이 안 되기 때문이다.
        //   **이 계수기 하나가 그것을 가른다: (가)면 0, (나)면 T2 마다 오른다.**
        //
        // ⚠ 이 칸은 **바이트 흐름 매칭(REQ-0218 ②)과 독립이다.** 줄 조립 경로는 그대로이므로
        //   **매칭이 `SEND OK` 를 구해도 이 수는 오른다.**
        //   🔑 그래서 **고치면서 동시에 원인을 확정할 수 있다** — 읽는 법:
        //     `okstream > 0` **그리고** `penddrop > 0`  →  **(나) 확정**
        //        (버려질 뻔한 `SEND OK` 를 바이트 매칭이 구했다는 뜻)
        //     `okstream == 0`                          →  매칭이 할 일이 없었다 → **(가) 쪽**
        //
        // ⚠ 옛 주석 *"서버가 재전송하므로 잃지 않는다"* 는 **데이터 줄에만 참이다.**
        //   `SEND OK` 는 ESP 가 한 번만 보내는 제어 응답이라 **재전송이 없다.**
        // 🔴 2026-08-18 정정 — **`okstream` 은 "구조"를 못 센다.**
        //   바이트 매처는 7번째 글자에서 발화한다 — **줄이 완성되기 전이다.**
        //   즉 줄 경로와의 경주에서 **항상 이긴다.** 그래서 `okstream` 은
        //   "줄 경로가 놓쳤을 것"이 아니라 **탐지된 SEND OK 총수**다.
        //   ⚠ 그 결과 아래 판정표의 `okstream > 0 → (나)` 는 **뭐든 성공하면 참**이라 쓸 수 없다.
        //
        // ★ **진짜 분자는 이것이다: 버려진 줄 안에 `SEND OK` 가 있었나.**
        //   여기서만 "줄 경로였으면 영영 잃었을 SEND OK" 를 셀 수 있다 —
        //   `rxLine` 에 완성된 줄이 아직 그대로 있다.
        if (strstr(rxLine, "SEND OK") != NULL) {
          if (okLostByLine < 65535) okLostByLine++;
          if (curTxLen >= RXBUF_THRESHOLD && okLostBig < 65535) okLostBig++;
        }
        if (pendDrops < 65535) pendDrops++;
        if (curTxLen >= RXBUF_THRESHOLD && pendDropsBig < 65535) pendDropsBig++;
      }
    } else {
      memcpy(workLine, rxLine, (size_t)n + 1);
      workLineOff = rxLineOff;
      handleLine(workLine);
    }
    return;
  }
  // 🔴 2026-08-18 (REQ-0218 ②) — **`SEND OK` 를 줄이 아니라 바이트 흐름에서 찾는다.**
  //
  // 줄 단위로 찾으면 세 곳에서 놓친다:
  //   ① `inSend` 중 완성된 **두 번째 줄부터는 버려진다**(`pendLine` 은 깊이 1)
  //      ⚠ 그 자리의 주석 *"서버가 재전송하므로 잃지 않는다"* 는 **`SEND OK` 에는 안 통한다** —
  //        **ESP 가 한 번만 보내는 제어 응답**이라 버려지면 영영 안 온다
  //   ② 앞에 다른 바이트가 붙어 한 줄이 되면 `strncmp` **접두 비교**가 실패한다
  //   ③ 줄이 `RX_CAP` 을 넘으면 통째로 버려진다
  // **셋 다 "게이트가 안 풀림 → T2 8초 → 링크 재수립"으로 끝난다.**
  //
  // ★ 슬라이딩 매칭은 **상태 1바이트**면 된다. 줄 조립과 무관하므로 위 셋을 전부 지난다.
  // ⚠ `awaitingSendOk` 일 때만 찾는다 — **기다리지 않을 때는 찾을 이유가 없고**,
  //   그만큼 하행 페이로드에 우연히 `SEND OK` 가 섞여 생기는 오탐 창이 좁아진다.
  if (awaitingSendOk) {
    static const char SENDOK[] = "SEND OK";
    if (c == SENDOK[sendOkMatch]) {
      if (++sendOkMatch == 7) {                    // 찾았다
        sendOkMatch = 0;
        awaitingSendOk = false; sendOkT1Passed = false;
        if (sendOkByStream < 65535) sendOkByStream++;
        if (curTxLen >= RXBUF_THRESHOLD && okStreamBig < 65535) okStreamBig++;
      }
    } else {
      sendOkMatch = (c == SENDOK[0]) ? 1 : 0;      // 겹침 재시작(`SSEND OK` 같은 경우)
    }
  } else {
    sendOkMatch = 0;
  }

  if (c == '\r') return;                                   // AT 응답의 CR. 명세는 CR 을 보내지 않는다
  if (rxLen >= RX_CAP - 1) { rxOverflow = true; return; }  // 넘치는 줄은 통째로 버린다
  // ★ REQ-0174 — **줄의 첫 바이트**에서 슬롯 오프셋을 확정한다. 이것이 진짜 도착 시각이다.
  if (rxLen == 0) rxLineOff = (uint16_t)((uint32_t)millis() - slotStart);
  rxLine[rxLen++] = c;
}

static void espRead(void) {
  while (wifi.available()) feedRxChar((char)wifi.read());
  // ─────────────────────────────────────────────────────────────────────────
  // 🔴 2026-08-17 (REQ-0167) — **SoftwareSerial 링버퍼가 넘쳤는가.** 계측기가 이미 있었다.
  //   `SoftwareSerial::overflow()` 는 라이브러리가 들고 있는 플래그인데
  //   **이 스케치에서 호출이 0건**이었다. RAM 0바이트로 답이 나오는 자리다.
  //
  // ⚠ **읽으면 플래그가 지워진다**(`if (ret) _buffer_overflow = false`, 라이브러리 확인).
  //   그래서 이 수는 **사건 수가 아니라 "내가 확인한 지점들 사이에 한 번이라도 넘쳤나"의 수**다.
  //   → **하한이다.** `0 이 아니다`는 강한 신호지만 `0` 은 "그 지점들 사이엔 안 넘쳤다"까지만 말한다.
  //
  // ★ 위치를 여기로 고른 이유: **읽기 루프 직후**라 "우리가 다 빼낸 뒤에도 넘쳐 있었나"를 본다.
  //   그리고 `espRead` 는 `loop()`·`waitForPrompt()` 양쪽에서 자주 불려 확인 간격이 짧다 —
  //   플래그가 지워지는 계측기라 **자주 볼수록 뭉침이 줄어든다.**
  //
  // ⚠⚠ **이름을 `rxOverflow` 와 헷갈리지 마라 — 다른 계층이다.**
  //   `rxOverflow`(L402) = 우리 **줄 조립 버퍼**(`RX_CAP`=96) 가 넘쳤다 → 그 줄을 버린다
  //   `ssOverflows`(이것) = **SoftwareSerial 링버퍼**(64B) 가 넘쳤다 → 바이트가 사라졌다
  //   전자는 "너무 긴 줄", 후자는 "우리가 늦게 꺼냈다". **원인도 대응도 다르다.**
  if (wifi.overflow()) { if (ssOverflows < 65535) ssOverflows++; }
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
// ⚠ **espReset() 의 switch 안에서 이 함수를 부를 때 주의할 것** — 새 호출 지점을 추가할 사람에게.
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
// 그 통보가 안 오면 espReset() 이 첫 줄에서 빠져 **재접속을 아예 시도하지 않는다.**
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
// ⚠ `awaitingSendOk`·`sendOkT1Passed` 의 **선언은 파일 앞쪽으로 옮겼다**(REQ-0218) —
//   `feedRxChar()` 가 `SEND OK` 를 바이트 흐름에서 잡으려면 그보다 앞에 있어야 한다.
//   **뜻과 주석은 여기 그대로 둔다.**   ↓ 아래 설명은 옮긴 변수에 대한 것이다
static uint32_t      sendOkWaitFrom  = 0;      // 그 기다림이 시작된 시각
//   ⚠ 없으면 T1~T2 구간의 **매 주기마다** `okto` 가 오른다 — 사건 수가 아니라 증상 수가 되어
//     monitor 계수와 정의가 어긋난다(원장 §8.2-1 이 `espResets` 에서 겪은 그것).
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

#include "EspLink_gate.h"   // ← EspLink 링크 계층 (REQ-0264). **위치를 옮기지 마라**

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

  // 🔴 2026-08-18 (REQ-0187 ②) — **슬롯 규율을 디버그 출력에도 적용한다.**
  //
  // 왜: 이 줄이 **손실원이었다.** 60초마다 하행 프레임 하나가 깨졌고, 창 B·C·D 에서
  //   **`[CKSUM NG]` 시각이 `[CNT]` 시각과 1~2초 안에 정합**했다(monitor 전수 대조).
  //     창 B 5건 · 창 C 4건(위상 `:56` 고정) · 창 D 2건 — **셋 다 정합.**
  //
  // 기전: `[CNT]` 는 약 142B 인데 하드웨어 UART TX 링버퍼는 **64B**(`SERIAL_TX_BUFFER_SIZE`)다.
  //   → 넘치는 78B 만큼 **블로킹**하고, 그동안 `espRead()` 가 안 돌아
  //     **도착 중인 하행 바이트가 SoftwareSerial 64B 링버퍼에서 사라진다.**
  //
  // ★ **UART 를 쓰는 모든 것이 슬롯 규율의 대상이다** — 프레임 송신만이 아니라 진단 출력도.
  //   지금까지 "송신"을 프로토콜 프레임으로만 좁게 봤다(원장 §11.2).
  //
  // ⚠ **60초 주기에서 최대 1.2초 지연은 무해하다.** 다음 송신 창까지 미룰 뿐이고
  //   `cntLastAt` 을 **찍을 때** 갱신하므로 주기가 밀리지 않는다(§3.4 의 재장전 교훈).
  //
  // ❌ **줄을 쪼개는 것은 대책이 아니다** — 총 바이트가 같아 점유가 안 줄고,
  //   오히려 **여러 창에 흩어져 더 많은 하행과 겹친다.**
  if (!netOnline) {
    // 오프라인이면 하행이 없다 → 겹칠 대상이 없으므로 창을 기다리지 않는다.
    // (안 그러면 링크가 죽은 동안 [CNT] 가 통째로 멈춰 진단이 사라진다)
  } else if ((uint32_t)(now - slotStart) + SLOT_TX_RESERVE_CNT_MS > TX_WINDOW_MS) {
    return;                      // 우리 송신 창이 아니거나 남은 시간이 부족하다 → 다음 슬롯에
  }

  cntLastAt = now;
  // 전부 **부팅 이후 누적**이다. 구간값이 아니다 — 창을 잡으려면 두 줄을 빼서 써라.
  Serial.print(F("[CNT] up="));      Serial.print(now / 1000);
  Serial.print(F(" drop="));         Serial.print(linkDrops);
  Serial.print(F(" esprst="));       Serial.print(espResets);
  Serial.print(F(" resync="));       Serial.print(promptResyncs);
  Serial.print(F(" sendfail="));     Serial.print(sendFails);
  Serial.print(F(" okto="));         Serial.print(sendOkTimeouts);
  // ★ 2026-08-17 신설 — ⚠ **칸을 뒤에 붙인다. 기존 이름·순서는 그대로 둔다**(monitor 파서).
  //   ⚠ 옛 로그에는 이 칸들이 없다. **굽기 전후를 같은 표에 넣지 마라** —
  //     그 칸의 0 은 "안 났다"가 아니라 "그 칩엔 없었다"다.
  Serial.print(F(" stuck="));        Serial.print(sendOkGiveups);   // T2 초과 → 링크 재수립
  Serial.print(F(" ackq="));         Serial.print(ackqCount);       // 지금 보류 중인 ACK
  Serial.print(F(" ackdrop="));      Serial.print(ackqDrops);       // 큐가 넘쳐 버린 ACK(유입 초과)
  // ★ REQ-0204 — 캐시에서 밀려 버린 ACK. **대책이 다르므로 칸을 가른다.**
  //   ⚠ 칸을 더해 이 줄이 길어지는 것은 이제 안전하다 — `cntTick` 이 송신 창 안에서만 나간다(§11.2-2).
  Serial.print(F(" ackstale="));     Serial.print(ackqStale);
  // ★ 2026-08-17 슬롯 — **정수 계수는 `DEBUG` 밖**이다(원장 §8.4-2 의 경계).
  //   `[SLOT]` 줄은 매 슬롯 나가 대역을 먹으므로 `DEBUG` 안에 두지만, **누적은 여기 남긴다.**
  //   그래야 `DEBUG=0` 시연 빌드에서도 슬롯이 지켜졌는지 셀 수 있다.
  Serial.print(F(" slot="));         Serial.print(slotNo);          // 지난 슬롯 수 = **분모**
  Serial.print(F(" oow="));          Serial.print(slotOow);         // 수신 창 밖 하행 = 설계 위반
  Serial.print(F(" smiss="));        Serial.print(slotMissed);      // 송신 기회를 놓친 슬롯
  // ★ REQ-0167 — SoftwareSerial 링버퍼(64B) 넘침. **하한이다**(읽으면 플래그가 지워진다).
  Serial.print(F(" ssovf="));        Serial.print(ssOverflows);
  // ★ REQ-0174 — 체크섬 불일치(하행 프레임 파괴). 서버 계수로는 안 보이는 손실이다.
  Serial.print(F(" cksumng="));      Serial.print(cksumNg);
  // ★ REQ-0218 — 바이트 흐름으로 잡은 `SEND OK` 수. **줄 경로가 놓치던 양**이다.
  // 🔴 2026-08-18 — **okstream 계열은 스냅샷으로 찍는다.**
  //   이 줄을 찍는 도중 `espRead()` 가 돈다. `cntTick` 중에 `inSend` 는 false 지만
  //   **`awaitingSendOk` 는 흔히 true 다**(CIPSEND 직후의 정상 상태).
  //   그 pump 가 `SEND OK` 를 매칭하면 `okstream`/`bigokst` 가 **출력 도중에 늘어나서**
  //   먼저 찍힌 `okstream` 은 옛 값, 나중 찍힌 `bigokst` 는 새 값이 된다.
  //   → **`bigokst > okstream` 이라는 불가능한 출력**이 나오고, 읽는 쪽은 펌웨어 결함으로 읽는다.
  //   ⚠ `pendfill`/`bigfill`/`bigdrop` 은 `inSend` 가 필요해서 이 문제가 없다.
  //     **이 계열만 해당한다** — 그래서 여기만 스냅샷한다.
  // 🔴 **출력 도중에는 `espRead()` 를 부르지 않는다.** 한 번 넣었다가 뺐다.
  //   이유: pump 가 `[AT] "..."` 를 찍어 **`[CNT]` 줄 한가운데로 끼어든다.**
  //   시험에서 실제로 그렇게 나왔다: `… pendfill=0[AT] "SEND OK" (7)\n bigfill=0 …`
  //   **monitor 의 파서가 그 줄을 못 읽는다.** 계측기를 고치려다 계측기를 깬 꼴이다.
  //   ⚠ 그러면 블로킹은 어떻게 되나 — **계산으로 안전하다:**
  //     `[CNT]` 약 220B @115200 ≈ 19ms · TX 링버퍼 64B 를 넘는 156B 만큼 ≈ 13.5ms 블로킹.
  //     그동안 들어올 하행은 9600bps 에서 **약 13B** 이고, SoftwareSerial 링버퍼 64B 에 여유가 있다.
  //   🔑 그리고 `cntTick` 은 **송신 창 안에서만** 나간다(§11.2-2) — 하행이 오는 시간대가 아니다.
  //   ⚠ 줄이 이보다 크게 길어지면 이 계산을 다시 해라. **300B 시험이 그 경보다**(시험 [28]).
  const uint16_t okStreamSnap = sendOkByStream;
  const uint16_t okLostSnap   = okLostByLine;
  const uint16_t okStreamBigSnap = okStreamBig;
  const uint16_t okLostBigSnap   = okLostBig;
  Serial.print(F(" okstream="));     Serial.print(okStreamSnap);
  // ★ monitor 요청 — `okstream` 과 **짝으로 읽는다**(위 feedRxChar 주석의 판정표).
  Serial.print(F(" penddrop="));     Serial.print(pendDrops);
  // 🔴 2026-08-18 — **`penddrop` 의 분모.** 이것이 0 이면 위 `penddrop=0` 은
  //   "안 넘쳤다"가 아니라 **"셀 일이 없었다"** 이고, 그 둘은 다른 결론으로 이어진다.
  Serial.print(F(" pendfill="));     Serial.print(pendFills);
  // ⚠ monitor 요청 — **대역(`>=64B`)별.** 누적 총계로는 창 G 의 갈림(`48~63` 0/43 · `>=64` 9/34)이 안 보인다.
  //   🔑 `64` 는 `_SS_MAX_RX_BUFF` = **기전 경계**다. monitor 의 `48` 은 표본 추출 기준이라 **다른 축이다.**
  Serial.print(F(" bigfill="));      Serial.print(pendFillsBig);
  Serial.print(F(" bigdrop="));      Serial.print(pendDropsBig);
  Serial.print(F(" bigokst="));      Serial.print(okStreamBigSnap);
  // ★ **대책 ②의 진짜 분자.** `okstream` 이 아니다 — 바이트 매처는 줄 완성 **전**에 발화해
  //   항상 줄 경로를 이기므로, `okstream` 은 그냥 탐지 총수다.
  //   **`oklost` 는 "줄 경로였으면 영영 잃었을 SEND OK"** 이고 그것이 곧 T2 였다.
  Serial.print(F(" oklost="));       Serial.print(okLostSnap);
  Serial.print(F(" bigoklost="));    Serial.print(okLostBigSnap);
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
  awaitingSendOk = false; sendOkT1Passed = false;
  // ★ 2026-08-17 — **보류된 ACK 도 여기서 버린다.** 소켓이 사라지면 그 `rid` 에 대한 답은
  //   새 소켓에서 의미가 없다. **이것이 ACK 큐가 새로 만드는 유일한 위험이고**, 그래서
  //   `awaitingSendOk` 를 푸는 바로 이 자리에 둔다 — 한 곳에서만 처리하면 빠뜨릴 수 없다.
  //   ⚠ 멱등 캐시는 **비우지 않는다**(아래 이유 참조). 큐만 비운다 — 둘은 다른 물건이다.
  ackqClear();
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

#include "EspLink_tx.h"   // ← EspLink 링크 계층 (REQ-0264). **위치를 옮기지 마라**

// ─────────────────────────────────────────────────────────────────────────
// S — 상태 프레임 (§2.4)
// ─────────────────────────────────────────────────────────────────────────
// ✏️ 2026-08-18 — `bitsToStr`(10진 1칸=1비트)를 **지웠다.** `bitsToHex` 로 전부 옮겼고
//   호출부가 하나도 안 남았다. 🔴 남겨 두면 위험하다: 그 함수는 `SLOT_N`(컴파일 상수)을 돌고
//   새 함수는 `n`(모듈 수)을 받는다 — **나중에 누가 옆에 있는 쪽을 부르면 유동화한 날 조용히 틀린다.**

// ─────────────────────────────────────────────────────────────────────────
// 🔴 자리 비트열의 **hex 인코딩** (socket 명세 §5 · 2026-08-18 확정)
//
// 왜: 10진 1칸=1비트라 폭이 `n` 에 비례한다. hex 면 `n=10` 에서 60B → 39B 로 줄고
//     배출률 `D` 가 6 → 7 이 된다. `n=15` 에서도 `D=7` 이 유지된다.
//
// 🔴 **비트 순서가 우리 `mask` 와 반대다. 이것이 이 함수의 존재 이유다:**
//     `occMask` 안 : 슬롯 i = **비트 i** (LSB 쪽이 슬롯 0)
//     전선 hex     : 슬롯 i = **비트 (n−1−i)** (= 10진 문자열을 그대로 이진수로 읽은 값)
//   ⚠ **뒤집지 않으면 길이도 체크섬도 통과하고 자리만 뒤집힌다.** 명세가 ③으로 못 박은 함정이다.
//   검산: 슬롯 {1,2,6,8,9} → mask 0x346 → 뒤집으면 0x18B → 문자열 "0110001011" 과 일치 ✅
//
// ⚠ **`0000…`·`1111…` 로 시험하지 마라** — 뒤집혀도 같아서 순서를 검증하지 못한다.
//   **비대칭 패턴만이 검증자다.**
//
// ⚠ **`n` 은 `SLOT_N` 이 아니다.** 자리 유동화 뒤에는 **등록된 모듈 수**다.
//   지금은 값이 같지만 **이름을 갈라 둔다** — 안 그러면 유동화하는 날 조용히 틀린다.
//   폭도 뒤집기 축도 둘 다 `n` 을 쓴다. `SLOT_N` 을 여기 쓰면 오늘은 맞고 내일 틀린다.
// 🔴 **`n` 의 단일 원천.** 자리 유동화의 축이 여기 하나로 모인다.
//
// 지금은 `SLOT_N` 을 돌려준다 — **거동 변화 0 이 이 단계의 기대다**(축 3).
// 🔮 유동화하면 **등록된 모듈 수**를 돌려주게 되고, 그때 `S` 폭·hex 폭·뒤집기 축이
//    **전부 이 함수 하나를 따라 움직인다.** 그것이 이 함수를 지금 만드는 이유다.
// ⚠ **`SLOT_N` 을 인코더에 직접 쓰면 오늘은 맞고 유동화하는 날 조용히 틀린다** —
//   길이도 체크섬도 통과하고 자리만 어긋나는 그 부류다.
// ⚠ 그리고 **`n` 은 입력+출력 합이다**(명세 위험 다섯째) — 차단봉·안내등도 비트열에 들어간다.
//   ACK 은 "받았다"이지 "됐다"가 아니므로, **도달 확인은 다음 `S` 의 마스크 변화로 한다.**
static uint8_t moduleCount(void);   // ★ 표가 원천이다 — 정의는 MODULE_TABLE 뒤에 있다

// ─────────────────────────────────────────────────────────────────────────
// 🔴 등록(`D`) — 접속하면 자기가 무엇을 가졌는지 먼저 알린다 (socket 명세 §5)
//
//   D,*,<drain>          ← **묶음의 맨 앞.** 자기 완결적이라 언제 와도 같은 뜻이다
//   D,<name>,<kind>      ← 이후 모듈들. 전부 같은 필드 수
//
// 🔑 **`*` 를 맨 앞에 둔 이유는 비용이 아니라 확장이다.** "마지막 줄에 붙이기"·"첫 줄에
//   붙이기"는 **파서가 몇 번째 줄인지 알아야 해서**, 모듈이 동적으로 추가돼 `D` 를 다시
//   보낼 때 *"이것도 첫 줄인가"* 가 애매해진다. 비용 차이는 6B·접속당 1회로 무시할 수준이었다.
//
// ⚠ **`kind` 전선 코드는 명세에 없어서 내가 붙였다**(2026-08-18). `docs/net/`·`docs/web/`·
//   당일 REQ 를 grep 해 **기존 이름이 없음을 확인한 뒤**에 만들었다. socket 합의 대기 중이고,
//   🔑 **다르면 이 표 하나만 고치면 된다** — 그래서 한 곳에 모아 둔다.
//
// 🔴 **첫 글자의 뜻** (socket 정정 2026-08-18 — 내가 "입력/출력"이라 했던 것을 고친다):
//   ~~입력 대 출력~~ ← **분할이 아니다.** 출력 모듈도 상태를 비트열로 보고한다(명세 위험 다섯째).
//   ✅ **`O` = 하행 명령을 받는다 · `I` = 관측 전용. 둘 다 비트열에는 들어간다.**
//   🔑 서버가 이 글자로 실제로 정하는 것은 **"이 모듈에 명령을 보내도 되는가"** 하나다.
//   ⚠ 실패 방향: **첫 글자가 `O` 가 아니면 명령을 안 보낸다.** 모르는 글자도 금지 쪽으로 떨어진다 —
//     **모르는 장치에 명령을 보내는 것이 안 보내는 것보다 위험하다.**
//   ⚠ 그래도 **모르는 `kind` 를 거절하지는 않는다** — 거절하면 새 모듈 하나가 옛 서버에서
//     **노드 전체를 미등록으로** 만든다.
#define KIND_PARK_SENSOR  "IP"   // 관측 전용 · 주차확인센서
#define KIND_GATE_SENSOR  "IX"   // 관측 전용 · 입출차센서
#define KIND_GUIDE_LIGHT  "OG"   // 명령 받음 · 안내등
#define KIND_LEAD_LIGHT   "OL"   // 명령 받음 · 유도등
#define KIND_BARRIER      "OB"   // 명령 받음 · 차단봉
// 🔴 **`V` 는 종류가 아니라 접미다** (socket 정의 2026-08-19):
//   `kind[0]` = 명령 가능 여부 · `kind[0..1]` = 종류 · **길이 3 이고 끝이 `V` 면 가상**
//   ⚠ "3글자 종류"로 정의하면 `OBV`·`OGV`·`OLV`·`IPV`… 로 **표가 두 배**가 된다.
//     **접미로 두면 표는 다섯 그대로이고 `V` 는 직교하는 한 비트다.**
//   ⚠ `tmask` 를 못 쓴 이유: 그건 **조건부 존재**(무장 중에만)이고 가상성은 **영구 속성**이다.
//     **영구 속성을 사라지는 필드에 실을 수 없다.**
#define KIND_BARRIER_V    "OBV"  // 명령 받음 · 차단봉 · **가상**

// 🔴 **배출률 선언값.** 이 노드가 한 주기에 배출할 수 있는 ACK 개수의 **보장 하한**이다.
//   ⚠ **관측 최대가 아니다.** 2026-08-18 실측 `8` 은 `S` 가 짧고 rid 3자리일 때만 성립하는
//     조건부였고 보장은 `6` 이었다. **조건부 실측을 보장으로 승격시키지 않는다.**
//   🔑 이 값이 장치 쪽에 있는 이유: 서버가 `(BATCH_CAP − S_worst − 1) ÷ (ACK_worst + 1)` 을
//     들고 있으면 **`BATCH_CAP` 을 우리가 바꿀 때 두 곳이 갈린다.**
//     **파생값은 원본을 가진 쪽이 계산한다.**
// 🔴 2026-08-18 **hex 전환으로 6 → 7 로 올린다.** `S` 가 짧아진 만큼 배치에 ACK 이 더 들어간다.
//   계산(최악값 기준):
//     S_worst = "S," + seq5 + occ4 + res4 + up10 + devid8 + tmask4 + 구분자6 + ck2 ≈ **45B**
//              (hex 폭 4 = n<=16 · 10진이었으면 폭 10~16 이라 S_worst 가 60B 대였다)
//     ACK_worst = "A," + rid5 + slot2 + result1 + 구분자3 + ck2 ≈ **15B**
//     D = (BATCH_CAP − S_worst − 1) ÷ (ACK_worst + 1) = (160 − 45 − 1) ÷ 16 = 7.1 → **7**
//   ⚠ **이건 계산이지 실측이 아니다.** socket 에 검산을 요청했다.
//   ⚠ 그리고 **보장 하한**이므로 실측이 더 크게 나와도 올리지 않는다 —
//     2026-08-18 실측 `8` 은 `S` 가 짧고 rid 3자리일 때만인 **조건부**였다.
//     **조건부 실측을 보장으로 승격시키지 않는다.**
static const uint8_t DRAIN_DECL = 7;

// 등록 상태 — ⚠ **온라인 전이가 두 곳이라 반드시 함수로 모은다.**
//   한 곳만 고치면 어긋나고, 그건 오늘 여러 번 밟은 형태다.
// 🔴 **첫 슬롯은 `S`, 둘째 슬롯부터 `D`** (socket 명세 확정 2026-08-18 · 커밋 9ffe8e5)
//
//   왜 `D` 가 먼저가 아닌가 — **`D` 에는 devid 가 없다.**
//   서버의 소켓→노드 **승격**은 devid 를 가진 유효 프레임으로 일어나는데,
//   `D,*,<drain>` 도 `D,<name>,<kind>` 도 devid 를 안 싣는다.
//   → 🔴 **첫 슬롯에 `D` 를 보내면 승격 전 버퍼에서 11줄이 통째로 죽는다.**
//
//   ⚠ 대안이었던 `D,*,<devid>,<drain>`(내 제안)은 socket 이 물렸다:
//     **`D,*` 가 반드시 맨 앞이어야 승격이 되므로 순서 의존이 파싱에서 승격으로 옮겨 갈 뿐**이다.
//     🔑 **"신원 없는 프레임이 승격 전에 도착하는 상황" 자체를 없애는 쪽**이 낫다 —
//        예외를 만드는 것보다 상황을 없애는 것이 낫다.
//
//   🔑 그리고 `S` 는 원래 **반송파·생존 신호·슬롯 시작 통보** 셋을 겸한다 —
//      첫 프레임이어야 할 이유가 이미 있었다.
static bool     regPending  = false;   // 다음 송신 창에 `D` 를 보내야 한다
static bool     regAfterS   = false;   // ★ 첫 `S` 가 나가면 그때 `D` 를 예약한다
static uint16_t regSends    = 0;       // 보낸 횟수(재전송 포함) — 진단용

// 새 소켓이 섰다. **바로 `D` 를 예약하지 않는다** — 승격이 먼저다.
static void markNeedsRegistration(void) { regPending = false; regAfterS = true; }

// 🔴 `Q` 를 받았다 — **이건 다르다. 바로 `D` 를 예약한다.**
//   `Q` 가 왔다는 것은 **서버가 이 소켓을 이미 노드로 승격했다**는 뜻이다
//   (승격 안 됐으면 어디로 보낼지 모른다). **그래서 `S` 를 한 번 더 보낼 이유가 없다.**
//   ⚠ 두 경우에 같은 함수를 쓰면 `Q` 응답이 한 슬롯 늦어지고, 서버의 3회 상한을 앞당긴다.
static void requestRegistrationNow(void) { regPending = true; regAfterS = false; }

// ─────────────────────────────────────────────────────────────────────────
// 🔴 **모듈 표 — 자리 유동화의 단일 원천** (2026-08-18 · 축 3)
//
// 왜: 이름·종류·핀이 **세 곳에 흩어져** 있었다. 모듈을 하나 더 달려면 세 곳을 맞춰 고쳐야 하고
//     **하나만 어긋나도 길이·체크섬은 통과하고 자리만 틀린다** — 오늘 `occMask` 에서 잡은 그 부류다.
//     표로 모으면 **한 줄만 고친다.**
//
// ⚠ **이 단계의 기대는 "거동 변화 0"** 이다(PLAN-axes 축 3). 값이 하나라도 달라지면 그것이 결함이다.
// ⚠ **표를 런타임에 바꾸지 않는다.** N:1 도 지금 안 만든다 —
//    🔑 **쓰이지 않는 일반화는 검증되지 않은 코드다**(socket 의 `cells` 규율과 같다).
// 🔴🔴 **`name` 을 바꾸지 마라 — 서버의 자리 결속이 여기에 걸려 있다** (socket 통보 2026-08-18)
//
//   서버가 모듈을 자리에 붙이는 규칙이 **지금은 이것뿐이다:**
//   ```
//   D,<name>,<kind> 의 name 이 **자리 id 와 같으면** 그 자리에 붙는다
//   ```
//   우리가 `A1`~`A5`·`B1`~`B5` 를 그대로 쓰기 때문에 성립한다.
//
// 🔴 **바꾸면 조용히 끊긴다: 등록은 성공하고(`등록 완료` 오름) 자리에는 아무것도 안 붙는다.**
//   **화면에 모듈이 안 보이고, 모듈 기반 조작이 생기면 "조작 불가"가 된다. ⚠ 오류로 안 뜬다.**
//   ⚠ **장치 쪽에서는 이걸 볼 수 없다** — 결속은 서버에서 일어난다.
//     그래서 **시험 [34] 가 이름 열 개를 리터럴로 못 박는다.** 바꾸면 시험이 깨진다.
//
// ⚠ 명세는 *"`name` 은 고유값이 아니라 명칭"* 이라 했는데 **지금 구현은 그보다 강하게 쓴다** —
//   설정 파일이 없어서 **이름을 결속으로 쓰는 부트스트랩**이다. socket 이 그 간극을 밝혔다.
// 🔮 **설정 적재가 들어오면 이 종속이 사라진다.** 그 전에 이름을 바꿀 계획이 생기면
//   **socket 에 먼저 말해라** — 설정을 먼저 넣으면 안전해진다.
struct ModuleDef {
  char    name[3];      // "A1" + NUL — 명칭이자 **지금은 자리 결속 키다**(위 경고)
  char    kind[4];      // KIND_* 2글자 + 선택적 `V` 접미(가상) + NUL
  uint8_t pin;
};
// 🔴 **가상 모듈 스위치** (REQ-0227 · 2026-08-19)
//   실기에 `O*`(명령 가능) 모듈이 하나도 없어 **조작 사슬이 한 번도 안 돌았다.**
//   ⚠ **실물 모듈이 생기면 반드시 0 으로 꺼라** — 켜 둔 채 실물을 붙이면 **같은 자리에 둘이 붙는다.**
#ifndef VIRTUAL_MODULES
#define VIRTUAL_MODULES 1
#endif

static const ModuleDef MODULE_TABLE[] PROGMEM = {
  {"A1", KIND_PARK_SENSOR,  2}, {"A2", KIND_PARK_SENSOR,  3}, {"A3", KIND_PARK_SENSOR,  4},
  {"A4", KIND_PARK_SENSOR,  5}, {"A5", KIND_PARK_SENSOR,  6},
  {"B1", KIND_PARK_SENSOR,  9}, {"B2", KIND_PARK_SENSOR, 10}, {"B3", KIND_PARK_SENSOR, 11},
  {"B4", KIND_PARK_SENSOR, 12}, {"B5", KIND_PARK_SENSOR, A0},
#if VIRTUAL_MODULES
  // 🔴 **끝에만 붙인다. 중간 삽입 금지.**
  //   `idx` 는 **등록 순서**이고 **서버의 자리 결속과 `G,<rid>,<idx>,<op>` 가 둘 다 그것을 쓴다.**
  //   중간에 넣으면 **기존 자리의 idx 가 전부 밀려 지금 되는 결속이 조용히 깨진다.**
  // ⚠ 이름은 **자리 id 와 같아야 한다**(원장 §20) — `E1`·`X1` 말고 다른 이름을 쓰면
  //   등록은 성공하고 자리에는 아무것도 안 붙는다. **오류가 안 뜬다.**
  {"E1", KIND_BARRIER_V, PIN_NONE},   // 입구 차단봉 (가상)
  {"X1", KIND_BARRIER_V, PIN_NONE},   // 출구 차단봉 (가상)
#endif
};
static const uint8_t MODULE_N = (uint8_t)(sizeof(MODULE_TABLE) / sizeof(MODULE_TABLE[0]));

// 🔴 **상한을 컴파일 시점에 박는다.** `occMask`·`resMask`·`ovrActive` 가 전부 `uint16_t` 다.
//   표에 17번째가 들어오는 순간 `1u << 16` 이 **아무 일도 안 하고** 그 자리가 조용히 사라진다 —
//   **길이도 체크섬도 통과한다.** 표를 만든 이득("한 줄만 고치면 된다")이
//   그대로 결함의 배달 경로가 되는 자리라, 여기서 빌드를 깨는 것이 유일한 방어다.
static_assert(MODULE_N <= 16,
              "마스크가 uint16_t 다 — 17번째 모듈은 조용히 사라진다. 마스크 폭을 먼저 늘려라");
// ⚠ 유동화 이행 중에는 둘이 같아야 한다. 달라지면 핀 표·초기값이 어긋난 것이다.
// ✏️ 2026-08-19 — `MODULE_N == SLOT_N` 이었다. **가상 모듈이 들어와 깨졌고, 그게 가드가 작동한 것이다.**
//   지키던 것: `SLOT_PIN[]`·`SLOT_SRC_DEFAULT` 가 `SLOT_N` 크기라 표가 그보다 크면 배열 밖을 읽는다.
//   ✅ 해결: **실물은 앞 `SLOT_N` 개로 고정**하고 `slotPin()` 에 범위 가드를 넣었다.
//   🔴 **가상 모듈은 반드시 표의 뒤쪽에만** 온다 — 앞에 끼면 실물 인덱스가 밀려
//     `SLOT_PIN`·`occMask` 비트·서버 자리 결속이 **한꺼번에 어긋난다.**
static_assert(MODULE_N >= SLOT_N,
              "표는 실물 자리 SLOT_N 개를 **앞쪽에** 전부 포함해야 한다");

// 🔴 **등록 배치가 한 슬롯에 들어가야 한다.** `buildRegistration` 은 넘치면 **통째로 0 을 돌려주고**,
//   그러면 **등록이 영영 안 된다**(잘린 등록을 내보내지 않는 것이 옳지만, 대안이 없으면 굶는다).
//   실측 계산: 머리 `D,*,<drain>,<n>,<ck>` ≈ 11B · 모듈 줄 `D,A1,IP,<ck>` + LF = 11B
//     n=13 → 154B (OK)   ·   🔴 n=14 → 165B (BATCH_CAP 160 초과)
//   ⚠ **`MODULE_N <= 16`(마스크 폭)보다 이쪽이 먼저 걸린다.** 둘 다 필요하다.
//   🔮 `n > 13` 이 필요해지면 **등록을 두 슬롯에 나눠 보내는 구현**이 먼저다. 지금은 안 만든다 —
//      쓰이지 않는 일반화는 검증되지 않은 코드다.
static_assert(11 * MODULE_N + 11 <= 160,
              "등록이 한 배치(BATCH_CAP)에 안 들어간다 — n<=13 이거나 두 슬롯 분할이 먼저다");

// ★ **표 하나가 `n` 의 원천이다.** 폭·뒤집기 축·등록 줄 수가 전부 이 값을 따라간다.
static uint8_t moduleCount(void) { return MODULE_N; }

#if VIRTUAL_MODULES
// ─────────────────────────────────────────────────────────────────────────
// 🔴 가상 차단봉의 상태와 값 패턴 (REQ-0227 · 설계문서 §3)
//
// **자율 모드** : 명령을 한 번도 안 받았으면 **슬롯 번호로 토글**한다
// **수동 모드** : 🔴 **첫 명령을 받으면 자율을 영구 정지**하고 명령대로만
//   ⚠ 자율이 남아 있으면 **명령 효과가 나중에 되돌려져 "안 먹었다"로 보인다.**
//     명령 반응이 주(主)이고 자율은 그것이 오기 전까지의 대용이다.
//
// ⚠ **무작위를 쓰지 않는다** — 재현이 안 되면 어긋났을 때 원인을 못 찾는다.
// 🔑 주기를 **서로 소**로 잡았다(20 과 14):
//     lcm = 140슬롯 ≈ 168초 안에 **네 조합(00·01·10·11)이 전부 나타난다.**
//     같은 주기면 둘이 항상 같이 움직여 **"하나만 도는지"를 못 가른다.**
static bool     vGateManual = false;      // 첫 명령을 받았나 (받으면 자율 정지)
static uint16_t vGateState  = 0;          // 비트 i = MODULE_TABLE 의 SLOT_N+i 번째가 열렸나

// 자율 패턴 — `slotNo` 는 부팅부터 세므로 리셋하면 위상이 처음으로 돌아간다(재현 가능)
static bool vGateAuto(uint8_t k) {
  return (k == 0) ? ((slotNo % 20) < 10)     // E1 : 24.1초 주기
                  : ((slotNo % 14) <  7);    // X1 : 16.9초 주기
}

// 지금 열려 있나 — `S` 의 비트열에 실릴 값
static bool vGateOpen(uint8_t k) {
  return vGateManual ? ((vGateState >> k) & 1) : vGateAuto(k);
}
#endif

static void moduleNameOf(uint8_t i, char* out4) {
  out4[0] = (char)pgm_read_byte(&MODULE_TABLE[i].name[0]);
  out4[1] = (char)pgm_read_byte(&MODULE_TABLE[i].name[1]);
  out4[2] = '\0';
}

// 슬롯 i 의 종류. 지금은 전부 주차확인센서다.
//   ⚠ **출력 모듈도 이 비트열에 들어가야 한다**(명세 위험 다섯째) — 차단봉·안내등이 생기면
//     여기서 종류를 돌려주고 `moduleCount()` 가 그만큼 커진다. **`n` 은 입력+출력 합이다.**
//   🔑 이유: ACK 은 "받았다"이지 "됐다"가 아니다. **도달 확인은 다음 `S` 의 마스크 변화로 한다.**
// ⚠ `out4` 는 **4바이트**여야 한다 — 가상 접미(`OBV`)가 3글자다.
static void moduleKindOf(uint8_t i, char* out4) {
  for (uint8_t k = 0; k < 3; k++) {
    const char c = (char)pgm_read_byte(&MODULE_TABLE[i].kind[k]);
    out4[k] = c;
    if (c == '\0') return;
  }
  out4[3] = '\0';
}

// 등록 배치를 만든다. 성공하면 길이, 실패하면 0.
//   ⚠ **`D` 여러 줄 + `S` 는 상한을 넘는다.** 그래서 명세가 *"첫 슬롯은 `D` 만"* 으로 정했다.
//     여기서도 `BATCH_CAP` 을 넘으면 **만들다 말고 0 을 돌려준다** — 잘린 등록을 내보내지 않는다.
//   🔴 잘린 등록은 **서버가 `n` 개를 못 채워 미완료로 두고**, 그 상태는 `Q` 로 복구된다.
//     하지만 **잘린 줄이 유효 프레임처럼 보이면** 그 복구조차 안 걸린다. 그래서 통째로 버린다.
static uint16_t buildRegistration(char* buf, uint16_t cap) {
  const uint8_t mn = moduleCount();
  uint16_t used = 0;

  // ① 배출률 선언 — **맨 앞.** 서버가 `n` 개를 다 받기 전에 유도식을 세울 수 있다
  // 🔴 `n`(모듈 수)을 같이 싣는다 — **명세가 `n` 으로 완료를 판정하는데 전선에 없었다.**
  //   ⚠ `S` 의 hex 폭에서 유도할 수 없다: 폭 `w` 는 **`n ∈ [4w−3, 4w]`** 만 준다(ceil 때문).
  //   ⚠ "다음 `S` 가 오면 완료"도 안 된다: `n` 이 크면 `D` 가 두 슬롯에 걸릴 수 있어
  //     **첫 슬롯에서 거짓 완료**가 된다.
  //   🔑 그리고 `n` 이 있으면 **①선언값 ②실제 D 줄 수 ③hex 폭** 셋이 서로를 *정확히* 못 박는다.
  //     ③만으로는 ±3 여유가 있어 ②를 못 잡는다. **다중 표현의 일치**가
  //     "오류 신호 없이 값만 틀리는" 부류에 가장 잘 듣는 방어다.
  int w = snprintf(buf, cap, "D,*,%u,%u,", (unsigned int)DRAIN_DECL, (unsigned int)mn);
  if (w <= 0 || (uint16_t)w + 3 > cap) return 0;
  appendChecksum(buf, (uint8_t)w);
  used = (uint16_t)strlen(buf);

  // ② 모듈들 — 전부 같은 필드 수
  for (uint8_t i = 0; i < mn; i++) {
    char nm[4];
    moduleNameOf(i, nm);
    char line[24];
    char kd[4];
    moduleKindOf(i, kd);
    int lw = snprintf(line, sizeof line, "D,%s,%s,", nm, kd);
    if (lw <= 0 || (unsigned)lw + 3 > sizeof line) return 0;
    appendChecksum(line, (uint8_t)lw);
    const uint16_t ll = (uint16_t)strlen(line);
    // ⚠ `+1` 은 줄 사이 LF. **넘치면 통째로 버린다 — 잘린 등록을 내보내지 않는다.**
    if (used + 1 + ll + 1 > cap || used + 1 + ll > BATCH_CAP) return 0;
    buf[used++] = '\n';
    memcpy(buf + used, line, ll + 1);
    used += ll;
  }
  return used;
}

// 🔑 폭 = ceil(n/4). 왼쪽 0 채움 고정폭 — **가변이면 길이로 n 을 검증할 수 없다.**
static uint8_t hexWidthFor(uint8_t n) { return (uint8_t)((n + 3) / 4); }
// mask 가 uint16_t 라 n ≤ 16 이고, 그때 폭은 4다. **버퍼를 이 값으로 잡는다.**
static const uint8_t HEX_W_MAX = 4;
static_assert(HEX_W_MAX >= (16 + 3) / 4,
              "HEX_W_MAX 는 n=16 의 폭 이상이어야 한다 — mask 가 uint16_t 이므로 n 의 상한이 16 이다");

// mask(슬롯 i = 비트 i) → 대문자 hex 고정폭. out 은 최소 hexWidthFor(n)+1 바이트.
static void bitsToHex(uint16_t mask, uint8_t n, char* out) {
  uint16_t v = 0;
  for (uint8_t i = 0; i < n; i++)
    if (mask & (uint16_t)(1u << i)) v |= (uint16_t)(1u << (n - 1 - i));   // ★ 뒤집기
  const uint8_t w = hexWidthFor(n);
  static const char HEXD[] = "0123456789ABCDEF";                          // ① 대문자만
  for (uint8_t k = 0; k < w; k++)
    out[w - 1 - k] = HEXD[(v >> (4 * k)) & 0x0F];
  out[w] = '\0';
}

// 전선 hex → mask. 실패하면 false — **받는 쪽도 검사한다**(명세 ④: 한쪽만 검사하면 그쪽 버그만 잡힌다).
//   ⚠ 거부 사유 셋: 폭이 다르다 · hex 가 아니다 · **상위 패딩 비트가 0 이 아니다.**
//   셋째가 특히 중요하다 — **길이도 체크섬도 통과하는데 값만 틀리는 경로**다.
static bool hexToBits(const char* in, uint8_t n, uint16_t* out) {
  const uint8_t w = hexWidthFor(n);
  uint16_t v = 0;
  for (uint8_t k = 0; k < w; k++) {
    const char c = in[k];
    uint8_t d;
    if      (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
    else if (c >= 'A' && c <= 'F') d = (uint8_t)(c - 'A' + 10);
    else return false;                       // ① 소문자도 거부한다 — 정본은 대문자 하나다
    v = (uint16_t)((v << 4) | d);
  }
  if (in[w] != '\0' && in[w] != ',') return false;   // ② 폭이 더 길다
  // ④ 남는 상위 비트는 반드시 0
  if (n < 16 && (v >> n) != 0) return false;
  uint16_t m = 0;
  for (uint8_t i = 0; i < n; i++)
    if (v & (uint16_t)(1u << (n - 1 - i))) m |= (uint16_t)(1u << i);      // ★ 되뒤집기
  *out = m;
  return true;
}

// buf[64] 인 근거 (§2.1-6 · §2.5):
//   전선 한 줄은 LF 포함 최대 64B → 문자열은 63자 + NUL = 64. 즉 이 버퍼가 규격 상한과 정확히 같다.
//   실제 최장은 tmask 를 실은 S 프레임이고, devid 8자 기준 61B(LF 포함) = 60자 + NUL = **61바이트**.
//   → 여유 3바이트. 우리 devid 는 "P1"(2자)이라 실제로는 55B(LF 포함)까지만 나간다.
//   넘칠 일은 없지만 snprintf 반환값을 검사해 넘치면 프레임을 버린다(잘린 줄을 내보내지 않는다).
// S 프레임 **본문만** 만든다(체크섬 포함, LF 없음). 배치가 이것을 첫 줄로 쓴다.
//   길이를 돌려준다. 0 이면 만들지 못한 것이다.
static uint8_t buildStatus(char* buf, uint8_t cap) {
  // 🔴 2026-08-18 — **hex 로 바꿨다** (socket 명세 §5 확정). `n=10` 에서 폭 10 → 3.
  //   ⚠ **셋을 같이 바꾼다.** `occ`·`res`·`tm` 중 하나만 바꾸면 **같은 슬롯을 두고 두 필드가
  //     어긋나고**, 길이도 체크섬도 통과한다 — 명세가 경고한 바로 그 부류다.
  //   ⚠ 버퍼는 `HEX_W_MAX + 1`. `n ≤ 16` 이므로 폭은 최대 4다.
  const uint8_t mn = moduleCount();                 // ★ 여기 하나가 n 의 원천이다
  char occ[HEX_W_MAX + 1], res[HEX_W_MAX + 1];
  // 🔴 **출력 모듈도 비트열에 들어간다**(명세 위험 다섯째 · 설계문서 §6).
  //   `occ` 비트 0~9 = 자리 점유 · 비트 10·11 = 차단봉이 **열려 있다**.
  //   ⚠ **같은 비트열인데 의미가 다르다** — 앞은 "차가 있다", 뒤는 "열려 있다". `kind` 로 구분한다.
  //   🔑 **완료 판정이 이 에코로 이뤄진다.** `ACK` 은 "받았다"이지 "됐다"가 아니다 —
  //     서버는 다음 `S` 에서 그 비트가 바뀌는 것으로 조작 성공을 안다.
  uint16_t occOut = occMask;
#if VIRTUAL_MODULES
  for (uint8_t k = 0; k + SLOT_N < mn; k++)
    if (vGateOpen(k)) occOut |= (uint16_t)(1u << (SLOT_N + k));
#endif
  bitsToHex(occOut, mn, occ);
  bitsToHex(resMask, mn, res);

  int n;
  if (testArmed) {
    // §2.4 tmask — 무장 중에만 붙는 선택 필드. 각 비트 = 그 칸의 occupied 가 주입된 값인가.
    // occupied 에는 주입값이 이미 반영돼 있고, tmask 는 "그게 진짜인가"만 알려준다.
    char tm[HEX_W_MAX + 1];
    bitsToHex(ovrActive, mn, tm);                   // ★ 셋째도 같은 변환 — 빠뜨리면 어긋난다
    n = snprintf(buf, cap, "S,%u,%s,%s,%lu,%s,%s,",
                 (unsigned int)seqNo, occ, res,
                 (unsigned long)(millis() / 1000UL), DEVICE_ID, tm);
  } else {
    // 해제 상태면 필드를 통째로 생략한다(옛 형식과 같다 — §2.4 "필드 없음 = 해제").
    n = snprintf(buf, cap, "S,%u,%s,%s,%lu,%s,",
                 (unsigned int)seqNo, occ, res,
                 (unsigned long)(millis() / 1000UL), DEVICE_ID);
  }
  if (n <= 0 || (unsigned)n + 3 > cap) return 0;
  appendChecksum(buf, (uint8_t)n);
  return (uint8_t)strlen(buf);
}

static bool sendStatus(void) {
  char buf[64];
  const uint8_t n = buildStatus(buf, sizeof(buf));
  if (n == 0) return false;
  return espWrite(buf, n);
}

// ─────────────────────────────────────────────────────────────────────────
// A — ACK (§2.4). R 과 C 둘 다에 대한 응답이다
// ─────────────────────────────────────────────────────────────────────────
static bool sendAck(uint16_t rid, char s0, char s1, uint8_t result) {
  // ✏️ 2026-08-18 — 여기서 프레임을 만들던 코드를 **뺐다.** 배치가 캐시에서 재생성하므로
  //   같은 문자열을 두 곳에서 만들 이유가 없다(`sendSlotBatch` 가 그 일을 한다).
  //   ★ 옛 주석("반환값을 여기서 쓴다 — 못 보냈으면 담아 둔다")의 **목적은 살아 있다**:
  //     ACK 을 잃지 않는 것. **이제는 아예 보내지 않으므로 "못 보냄"이라는 상태가 없다.**
  // 🔴 2026-08-18 (REQ-0185 ①) — **직접 보내지 않는다. 큐에 담기만 한다.**
  //
  // 왜: 여기서 `sendLine` 을 직접 부르면 **슬롯 창을 안 본다.** 게이트만 열려 있으면
  //   ACK 이 **수신 창 한복판에서도 나가고**, 그 송신이 도착 중인 하행과 겹쳐 프레임을 깬다.
  //   창 B 실측: 하행 파괴 5건(`cksumng=5`). 서버가 창을 지켜도 **장치가 그 창에서 송신**했다.
  //   ⚠ 그리고 이 위반은 `[SLOT-OOW]`·`[SLOT]`·`smiss`·`skip` **어디에도 안 잡혔다** —
  //     계측기는 하행 도착과 `statusTick` 경로만 본다. **조용히 새는 형태였다.**
  //
  // ★ 설계 문서가 손실 0 의 직접 원인으로 적은 문장이
  //   **"수신 창에는 한 바이트도 쓰지 않는다"** 인데 **ACK 이 그 문장 밖에 있었다.**
  //   이 수정이 그 문장을 처음으로 참으로 만든다.
  //
  // 🔑 그리고 **유실 경로가 오히려 준다**: 예전에는 "즉시 보내고 실패하면 push" 라
  //   **성공 경로와 실패 경로가 둘**이었다. 이제 **하나**다 — 담고, 배치가 보낸다.
  //   실제 송신은 `sendSlotBatch` 가 하고 그쪽은 **성공했을 때만 큐에서 소비**한다.
  //
  // ⚠ 내용(`s0`·`s1`·`result`)은 담지 않는다 — **호출부가 `cachePut` 을 먼저 부르므로
  //   멱등 캐시가 이미 갖고 있고**, 배치가 보낼 때 거기서 **재생성**한다(재생이 아니다).
  (void)s0; (void)s1; (void)result;
  ackqPush(rid);
  return true;          // "보냈다"가 아니라 **"담았다"**. 호출부 5곳 모두 반환값을 안 쓴다(확인함)
}

// ─────────────────────────────────────────────────────────────────────────
// 🔴 슬롯 배치 — **S 프레임 + 밀린 ACK 를 한 거래로 묶는다** (2026-08-17 · REQ-0164)
//
//   [CIPSEND]  S,912,0110...,P1,3F\nA,7,A1,1,2C\nA,8,B3,1,5D
//
// 왜 묶나: **슬롯당 정확히 1거래**가 규칙이다. 3건을 보내려면 묶는 것 말고 방법이 없다
//   (안 묶으면 3건에 3.6초가 걸린다). **배치는 최적화가 아니라 구조적 필수다.**
//   ⚠ 속도 이득은 1.4배이지 1.9배가 아니다(설계문서 정정). **근거는 속도가 아니라 구조다.**
//
// ⚠ **ACK 는 캐시에서 재생성한다 — 재생(replay)이 아니다.** 큐에는 `rid` 만 있고
//   내용은 멱등 캐시가 갖고 있다. 옛 바이트를 그대로 다시 보내는 것이 아니라 지금 다시 만든다.
//
// 🔑 **성공했을 때만 큐에서 소비한다.** 만들 때는 `peek` 만 하고, `espWrite` 가 참을
//   돌려준 뒤에 그만큼 뺀다. **실패하면 ACK 가 큐에 그대로 남아 다음 슬롯에 다시 나간다.**
//   (옛 `ackqDrain` 은 먼저 빼고 실패 시 되넣는 방식이었다 — 배치에서는 순서가 뒤집힐 수 있어
//    쓸 수 없다. 이쪽이 손실 경로가 아예 없다.)
static bool sendSlotBatch(uint8_t* ackOut, uint16_t* bytesOut) {
  char buf[BATCH_CAP + 1];

  // ── 0) 🔴 등록이 밀려 있으면 **이 슬롯은 `D` 만 보낸다** (socket 명세 §5) ──
  //   ⚠ **`regPending` 은 첫 `S` 가 나간 뒤에야 선다**(`regAfterS` → 아래 성공 처리부).
  //     그래서 이 갈래는 **둘째 슬롯부터** 걸린다. 첫 슬롯은 아래 `S` 경로로 간다.
  //   왜 `S` 와 같이 안 보내나: `D` 여러 줄 + `S` 는 상행 배치 상한을 넘는다.
  //   **슬롯을 가르는 것이 명세의 답이고, 그래서 순서가 구조적으로 보장된다**
  //   (접속 → 첫 슬롯 `D` → 둘째 슬롯부터 `S`).
  //
  //   ⚠ **이 슬롯의 `S` 는 안 나간다.** 서버 입장에서 "첫 프레임까지의 시간"이 한 슬롯
  //     (1.2초) 늘어난다 — **고장이 아니라 등록 축의 예상된 값이다**(PLAN-axes 축 2).
  //
  //   ⚠ **등록 성공을 장치는 모른다**(ACK 이 없다). 그래도 그대로 간다 —
  //     **알아도 할 일이 없기 때문**이다. 서버가 `Q` 를 보내면 다시 보내고, 그것이 유일한 행동이다.
  //     🔑 사람은 정확히 안다(`node_unregistered` · 화면 `⏱`). **모르는 것은 장치뿐이고
  //     장치가 그걸 알아서 바꿀 동작이 없다** → 공백이 아니라 **명시된 비대칭**이다.
  if (regPending) {
    const uint16_t rn = buildRegistration(buf, sizeof buf);
    if (rn == 0) {
#if DEBUG
      Serial.println(F("[REG] 등록 배치를 만들지 못했다 — 잘린 등록을 내보내지 않는다"));
#endif
      return false;                       // ⚠ 다음 슬롯에 다시 시도한다(regPending 유지)
    }
    const bool okReg = espWrite(buf, rn);
    if (okReg) {
      regPending = false;                 // ★ 성공했을 때만 내린다 — ACK 큐와 같은 규율이다
      if (regSends < 65535) regSends++;
    }
    if (ackOut)   *ackOut = 0;
    if (bytesOut) *bytesOut = rn;
#if DEBUG
    Serial.print(F("[REG] 등록 "));
    Serial.print(okReg ? F("전송 ") : F("실패 "));
    Serial.print(rn); Serial.println(F("B"));
#endif
    return okReg;
  }

  // ── 1) 반송파: S 프레임은 **보낼 게 없어도 나간다** ─────────────────────
  //   이 한 프레임이 셋을 겸한다: 반송파 · 생존 신호 · **슬롯 시작 통보**(서버의 t0).
  const uint8_t sn = buildStatus(buf, 64);
  if (sn == 0) return false;
  uint16_t used = sn;

  // ── 2) head 쪽에서 **만들 수 없는 것**부터 걷어낸다 ──────────────────────
  //   캐시에서 밀려났으면 내용을 만들 방법이 없다. 큐도 캐시도 FIFO 라 오래된 쪽에서 난다.
  while (ackqCount > 0 && cacheFind(ackq[ackqHead]) < 0) {
    ackqHead = (uint8_t)((ackqHead + 1) % ACKQ_N);
    ackqCount--;
    if (ackqStale < 65535) ackqStale++;      // ★ REQ-0204 — 큐 넘침(ackdrop)과 **다른 사건**이다
#if DEBUG
    Serial.println(F("[ACKQ] 캐시에서 밀려나 만들 수 없다 — 버린다"));
#endif
  }

  // ── 3) 담을 수 있는 만큼 이어 붙인다 (아직 큐에서 빼지 않는다) ───────────
  uint8_t take = 0;
  while (take < ackqCount) {
    const uint16_t rid = ackq[(uint8_t)((ackqHead + take) % ACKQ_N)];
    const int8_t   hit = cacheFind(rid);
    if (hit < 0) break;                       // 중간 미스 — 다음 슬롯에서 걷어낸다

    char one[24];
    int m = snprintf(one, sizeof(one), "A,%u,%c%c,%u,",
                     (unsigned int)cache[hit].rid,
                     cache[hit].slot[0], cache[hit].slot[1],
                     (unsigned int)cache[hit].result);
    if (m <= 0 || (unsigned)m + 3 > sizeof(one)) break;
    appendChecksum(one, (uint8_t)m);
    const uint8_t ol = (uint8_t)strlen(one);

    if (used + 1 + ol > BATCH_CAP) break;     // 이번 배치엔 자리가 없다 — 다음 슬롯으로 민다
    buf[used++] = '\n';                       // 줄 구분자. 서버 파서는 LF 로 가른다
    memcpy(buf + used, one, ol);
    used += ol;
    take++;
  }

  const bool ok = espWrite(buf, used);

  // ── 4) **성공했을 때만** 소비한다 ────────────────────────────────────────
  if (ok && take) {
    ackqHead = (uint8_t)((ackqHead + take) % ACKQ_N);
    ackqCount = (uint8_t)(ackqCount - take);
  }
  // 🔴 **`S` 가 실제로 나갔으면 그때 등록을 예약한다.**
  //   이 `S` 가 서버에서 **승격**(소켓 → 노드)을 만든다. 그 뒤라야 `D` 가 처리된다.
  //   ⚠ **성공했을 때만** 세운다 — ACK 큐를 성공 시에만 소비하는 것과 같은 규율이다.
  //     실패했으면 승격이 안 됐을 수 있고, 그러면 다음 슬롯의 `S` 가 다시 시도한다.
  if (ok && regAfterS) { regAfterS = false; regPending = true; }
  if (ackOut)   *ackOut   = take;
  if (bytesOut) *bytesOut = used;
  return ok;
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
  //   🔴 2026-08-18 `Q` 추가 — 서버가 "등록을 다시 보내라"고 묻는 프레임(명세 §5 ③).
  //     ⚠ **없으면 등록이 한 번 실패했을 때 복구 경로가 아예 없다.** 서버는 미완료로 두고
  //       장치는 자기가 실패한 줄 모른다(등록에 ACK 이 없다) → 그 노드는 영영 미등록이다.
  if (cand[0] != 'R' && cand[0] != 'C' && cand[0] != 'T' && cand[0] != 'M' && cand[0] != 'Q' && cand[0] != 'G') return;

  // 체크섬. AT 잡음이 우연히 R 로 시작해도 여기서 걸린다
  if (!checksumOk(cand, len)) {
    // ★ REQ-0174 ② — **계수는 `DEBUG` 밖.** 원문 출력만 안이다(원장 §8.4-2 의 경계:
    //   정수 계수는 밖 · 장문 진단은 안). 이 칸이 없으면 창 B 에서 손으로 못 찾고,
    //   그러면 "무손실"이 서버 계수만으로 선언된다.
    if (cksumNg < 65535) cksumNg++;
#if DEBUG
    Serial.print(F("[CKSUM NG] ")); Serial.println(cand);
#endif
    return;
  }

  // ── 🔴 `G` — 조작 명령 `G,<rid>,<idx>,<op>,<ck>` (socket 확정 2026-08-19) ──
  //   `idx` = 등록 순서(= 비트열 자리) · `op` = 1(열다) / 0(닫다)
  //   🔑 **`name` 이 아니라 `idx` 인 이유**: 이미 합의된 순서이고(등록 순서가 비트 자리를 정한다),
  //     삼중 검산으로 지켜지며, 우리 쪽은 **배열 인덱스라 조회가 없다.**
  //   ⚠ **조용히 버리지 않는다** — 거절도 ACK 을 보낸다. 안 그러면 서버가 `ack_timeout` 을 내고
  //     **"안 갔다"와 "갔는데 거절됐다"가 같은 칸에 섞인다.**
  if (cand[0] == 'G') {
    char*   gf[6];
    uint8_t gn = splitFields(cand, gf, 6);
    uint16_t grid = 0;
    if (gn == 0xFF || gn < 4 || !parseU16(gf[1], &grid)) return;   // rid 를 모르면 ACK 를 못 만든다
    uint16_t gidx = 0, gop = 0;
    const bool okIdx = parseU16(gf[2], &gidx) && parseU16(gf[3], &gop);

    // §4.2 멱등 — 이미 본 rid 면 같은 ACK 를 다시 보낸다
    int8_t ghit = cacheFind(grid);
    if (ghit >= 0) {
      sendAck(cache[ghit].rid, cache[ghit].slot[0], cache[ghit].slot[1], cache[ghit].result);
      return;
    }

    uint8_t gres = 3;                       // 기본 = 수행할 수 없다(§2.4 result=3)
#if VIRTUAL_MODULES
    if (okIdx && gidx >= SLOT_N && gidx < moduleCount()) {
      const uint8_t k = (uint8_t)(gidx - SLOT_N);
      // 🔴 **첫 명령이 자율 토글을 영구 정지시킨다.**
      //   안 그러면 명령 효과가 다음 주기에 되돌려져 "안 먹었다"로 보인다.
      if (!vGateManual) {
        vGateManual = true;
        for (uint8_t j = 0; j + SLOT_N < moduleCount(); j++)   // 지금 상태를 그대로 굳힌다
          if (vGateAuto(j)) vGateState |= (uint16_t)(1u << j);
      }
      if (gop) vGateState |=  (uint16_t)(1u << k);
      else     vGateState &= (uint16_t)~(1u << k);
      gres = 0;
    }
#endif
#if DEBUG
    if (gres != 0) { Serial.print(F("[G] 거절 idx=")); Serial.println(gidx); }
    else           { Serial.print(F("[G] idx=")); Serial.print(gidx);
                     Serial.print(F(" op=")); Serial.println(gop); }
#endif
    // ⚠ 캐시에 넣어야 재전송(같은 rid)이 같은 답을 받는다 — 멱등이 여기서도 성립해야 한다
    cachePut(grid, 'G', (char)('0' + (gidx % 10)), gres);
    sendAck(grid, 'G', (char)('0' + (gidx % 10)), gres);
    return;
  }

  // ── 🔴 `Q` — 서버가 등록 재전송을 요구한다 (명세 §5 ③) ────────────────────
  //   ⚠ **`processCommand` 를 태우지 않는다.** 그쪽은 `rid` 를 요구하는데 `Q` 에는 없다
  //     (재전송 요구이지 명령이 아니다). 태우면 `rid` 파싱에서 조용히 버려진다.
  //   ⚠ **ACK 를 보내지 않는다.** 응답은 **다음 자기 송신 창의 `D`** 그 자체다.
  //   🔑 무한 반복 방지는 **서버 몫**이다(슬롯당 1회 · 3회 무응답이면 포기하고
  //     `node_unregistered` 로 굳힌다). 장치가 자체 상한을 두면 **교착**이 된다 —
  //     장치는 안 보내고 서버는 계속 묻는다. 여기서는 단순히 응답하고 `regSends` 로 진단만 남긴다.
  if (cand[0] == 'Q') {
    requestRegistrationNow();      // ★ 이미 승격됐다 — S 를 다시 보낼 이유가 없다
#if DEBUG
    Serial.println(F("[REG] Q 수신 — 다음 송신 창에 등록을 다시 보낸다"));
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
      // ── 🔴 슬롯 위반 표지 (2026-08-17 · REQ-0164 ②) ───────────────────────
      //   설계상 하행은 **수신 창(600~1200ms)** 에만 와야 한다. 우리 송신 창에 오면
      //   `SoftwareSerial` 반이중 때문에 **우리가 귀를 닫고 있는 동안** 도착한 것이다.
      //
      //   ⚠⚠ **이 줄이 없으면 설계 위반이 조용하다.** 슬롯이 성공하면 지표는 `0` 이 되는데,
      //     `0` 은 "설계가 먹었다"와 "못 셌다"를 구별하지 못한다(원장 §5.1·§8.2-5).
      //     **이 표지가 그 둘을 가르는 유일한 줄이다.**
      //
      //   🔴 **2026-08-17 (REQ-0174) — 여기서 `millis()` 를 뜨면 안 된다.**
      //     송신 중 도착한 줄은 `pendLine` 에 갇혔다가 **송신이 끝난 뒤** 이 함수에 온다.
      //     그러면 **위험 구간에 도착한 것이 바로 그 이유로 늦게 찍혀 판정에서 빠진다** —
      //     재려던 사건을 측정 지점이 밀어내는 것이다.
      //     실측 반증: 서버 재전송이 난 구간에서 `oow` 가 0 이었다(socket, 2차 주입).
      //   ✅ 이제 **줄의 첫 바이트를 받은 순간 확정된 오프셋**을 쓴다(`feedRxChar` 참조).
      const uint32_t sinceSlot = (uint32_t)workLineOff;
      if (sinceSlot < TX_WINDOW_MS) {
        if (slotOow < 65535) slotOow++;
#if DEBUG
        //   ⚠ 문구에 다른 표지 문자열을 넣지 마라(`SEND OK`·`busy`·`[TX]`).
        Serial.print(F("[SLOT-OOW] +"));
        Serial.print(sinceSlot);
        Serial.println(F("ms"));
#endif
      }
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
      // ★ 2026-08-17 — 이 `busy` 가 CIFSR 에 대한 것이면 **소진 사유가 "IP 없음"이 아니다.**
      //   여기서 표시해 두고, 3회 소진 지점에서 `noteIpLoss()` 를 건너뛴다(§8.2-12).
      if (netLastSent == NET_CIFSR) cifsrRefused = true;
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
        cifsrRefused  = false;              // ★ 라운드가 끝났다 — 묵은 `busy` 표시가 다음 판정을 가리면 안 된다
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
  //   재접속은 netOnline==false 를 요구하고(espReset 첫 줄), netOnline 이 런타임에 false 가 되는
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
  //   그건 **기존 연결이 그대로 살아 있다**는 응답이다(서버도 그대로다). espReset() 이 CIPSTART 를
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
    markNeedsRegistration();           // ★ 새 소켓 = 서버가 우리를 모른다. **세 곳 전부**에서 예약한다(아래)
    awaitingSendOk = false; sendOkT1Passed = false;            // ★ 2단계: 새 소켓이다 — 앞 소켓의 SEND OK 를 기다리지 않는다
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
    markNeedsRegistration();           // ★ 새 소켓 = 서버가 우리를 모른다. **세 곳 전부**에서 예약한다(아래)
    awaitingSendOk = false; sendOkT1Passed = false;            // ★ 2단계: 새 소켓이다 — 앞 소켓의 SEND OK 를 기다리지 않는다
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
    // 🔴 **이 호출은 세 곳에 있다. 세다 틀리기 쉬워 여기 적어 둔다** (2026-08-18 정정):
    //   ① `isConnectLine`      정상 접속 (CONNECT / Linked)
    //   ② `isAlreadyConnectLine` 초기 경합 (ALREADY / **ALREAY** CONNECT — 구형 펌웨어 오타)
    //   ③ 여기                 소켓 닫힘 (CLOSED / Unlink)
    //   ⚠ 옛 주석이 *"두 전이 모두에서"* 였다. **거동은 맞았고 주석만 틀렸는데**,
    //     그 문구가 "두 곳만 보면 된다"로 읽혀 **다음 사람이 셋째를 빠뜨린다** —
    //     실제로 내가 셀 때도 처음에 두 곳으로 알았다.
    //   🔑 빠뜨리면 **조용히 안 된다**: 재접속 후 등록이 안 되고 시리얼에는 아무것도 안 나온다.
    //     서버만 `Q` 3회 뒤 `node_unregistered` 로 굳힌다. → 시험 [35] 가 세 경로를 전부 밟는다.
    markNeedsRegistration();
    awaitingSendOk = false; sendOkT1Passed = false;            // ★ 2단계: 소켓이 닫혔다 — 그 SEND OK 는 영영 오지 않는다
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
  // (espReset() 이 `if (netOnline) return;` 으로 시작하므로 온라인 중엔 다른 명령이 안 나간다).
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
      awaitingSendOk = false; sendOkT1Passed = false;
      return;
    }
    // ── 2단계: `SEND FAIL` — **원래 있던 구멍이다** ──────────────────────────────
    // 이건 `busy`(거부)와도 무응답(프롬프트 놓침)과도 **완전히 다른 사건**이다:
    // 프롬프트까지 정상으로 받고 페이로드도 다 썼는데 **전송이 실패한 것** = 진짜 송신 실패.
    // 그런데 sendLine 은 `>` 를 봤다는 이유로 이미 `noteSendResult(true)` 를 불러
    // **연속 카운터를 0 으로 되돌린 뒤**였다. 즉 프레임이 안 나갔는데 성공으로 세고 있었다.
    // ⚠ 이중 계수가 아니다 — 그 성공 계상을 **여기서 되돌리는** 것이다.
    if (strncmp(s, "SEND FAIL", 9) == 0) {
      awaitingSendOk = false; sendOkT1Passed = false;
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
  // ★ REQ-0174 — **도착 오프셋을 같이 넘긴다.** 이 경로가 정확히 "송신 중 도착"이라
  //   여기서 오프셋을 안 들고 오면 `[SLOT-OOW]` 가 위험 구간을 통째로 놓친다.
  workLineOff = pendLineOff;
  pendReady = false;
  handleLine(workLine);
}

// ─────────────────────────────────────────────────────────────────────────
// 주기 처리
#include "EspLink_ladder.h"   // ← EspLink 링크 계층 (REQ-0264). **위치를 옮기지 마라**

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

  // ── 🔴 슬롯 경계 (2026-08-17) ──────────────────────────────────────────
  //   ⚠ `slotStart += SLOT_MS` — **더한다.** `= now` 로 하면 매 슬롯 오차가 누적돼
  //     주기가 스스로 늘어난다(원장 §3.4 의 `lastStatusAt = millis()` 가 그것이다 —
  //     그 결함이 실측 주기를 1.000s 가 아니라 **1.113s** 로 만들었다).
  //   ⚠ while 인 이유: 오래 막혀 여러 슬롯이 지났으면 **전부 소진**해야 위상이 맞는다.
  //     그렇지 않으면 밀린 만큼 계속 뒤처진 위상으로 돈다.
  while ((uint32_t)(now - slotStart) >= SLOT_MS) {
    slotStart += SLOT_MS;
    slotNo++;
    if (netOnline && !slotSent) { if (slotMissed < 65535) slotMissed++; }  // 보낼 기회를 못 썼다
    slotSent = false;
  }
  const uint32_t slotUsed = (uint32_t)(now - slotStart);
  // **우리 차례는 0~600ms 뿐이다.** 그 뒤는 수신 전용이라 한 바이트도 쓰지 않는다.
  //
  // 🔴 2026-08-17 — **"창 안에서 시작한다"로는 부족하다. 창 안에서 *끝나야* 한다.**
  //   (socket 이 flush 위치를 물어보다가 드러났다 — 내 구현의 실제 구멍이었다.)
  //   창 끝자락(예: 590ms)에 시작하면 배치가 흐르는 동안 **수신 창을 침범한다:**
  //     최악 배치 `BATCH_CAP`(160B) + `AT+CIPSEND=..` 약 18B = 178B @9600bps ≈ **185ms**
  //     → 590 + 185 = 775ms. **175ms 를 남의 창에서 쓴다.**
  //   그러면 그 순간 도착하는 하행과 정확히 겹치고, 그것이 우리가 없애려는 바로 그 조합이다.
  //   ⚠ fdtest 에는 이 가드가 있었는데(`canSend`) 실기로 옮기며 빠뜨렸다.
  //     **최소 표본에서 옳았던 것이 본체로 오면서 사라지는 것** — 흔한 자리다.
  static const uint16_t SLOT_TX_RESERVE_MS = 200;   // 최악 배치 실측 상한(185ms)에 여유를 더한 값
  const bool inTxWindow = (slotUsed + SLOT_TX_RESERVE_MS <= TX_WINDOW_MS);
  bool heartbeatDue = inTxWindow && !slotSent;
  // changeAt 은 전송 실패 시 **미래 시각**으로 밀린다. unsigned 뺄셈으로 비교하면
  // 언더플로로 곧장 참이 되어 백오프가 통째로 무력화된다 → 부호 있는 비교로 본다.
  // ⚠ 2026-08-17 — `debounced` 는 **더 이상 전송을 트리거하지 않는다**(슬롯이 주기를 정한다).
  //   `changePending`/`changeAt` 자체는 남긴다 — 실패 백오프와 `sentOcc` 갱신이 아직 쓴다.
  (void)DEBOUNCE_MS;

  // ★ REQ-0116 — 살아있음 불변식. **이유를 묻지 않는다.**
  //   "온라인이라면서 TX_STALL_MS 동안 한 줄도 못 내보냈다"면 그 자체로 링크 이상이다.
  //   `busy`·`ERROR`·침묵·**아직 이름 없는 무엇**이든 전부 여기에 걸린다.
  //   ⚠ 아래 조기 반환(`return`)보다 **앞**에 있어야 한다. 뒤에 두면 하트비트가 뜨지 않는
  //     순간에는 검사가 건너뛰어져, 정작 아무것도 못 보내는 상황에서 발동하지 못한다.
  // ⚠ `changeAt`(위)과 **같은 함정이고 같은 관용구로 막는다.** 2026-08-17 실기에서 터졌다:
  //   `now` 는 loop() 맨 위에서 한 번만 뜨는데, 그 뒤 `espRead()`/`drainPending()` 이
  //   `lastTxOkAt` 을 **더 나중 시각**으로 갱신한다(하행 ACK 송신 약 64ms · CONNECT 처리).
  //   그러면 `now - lastTxOkAt` 이 음수가 되고 unsigned 로 읽으면 `2^32-64`(=49.7일)이 되어
  //   **어떤 임계값도 즉시 넘어 멀쩡한 링크를 끊는다.** 실제 로그: `정지 감지: 4294967232ms`.
  //   → 부호 있는 비교면 미래 시각은 음수라 발동하지 않는다.
  //
  // ⚠⚠ **폭을 먼저 32비트로 맞춘 뒤 부호를 준다.** 위 `changeAt` 처럼 `(long)` 만 쓰면
  //   **실기에서는 맞고 호스트 시험에서는 틀린다** — AVR 은 `long` 이 32비트라 자연히
  //   감싸이지만, 호스트는 64비트라 감싸이지 않아 **정상적인 millis() 되감김이 음수로 읽힌다.**
  //   실제로 `(long)` 만 썼다가 [7]·[8] 이 깨졌다(58/61). `(uint32_t)` 뺄셈으로 감싸고
  //   `(int32_t)` 로 해석하면 **두 환경에서 같은 값**이 나온다.
  if (netOnline && (int32_t)((uint32_t)now - lastTxOkAt) >= (int32_t)TX_STALL_MS) {
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

  // 🔴 **이벤트는 전송을 만들지 않는다.** `debounced`(변화 감지)는 이제 "지금 보내라"가
  //   아니라 "이번 슬롯 화물에 실려 나간다"는 뜻이다 — 어차피 S 프레임이 매 슬롯 나가므로
  //   변화는 다음 슬롯에 자동으로 실린다. **그래서 조건에서 뺀다.**
  //   ⚠ 이것이 설계의 핵심이다: 부하가 바꿀 수 있는 것은 **화물의 크기**뿐이고 전송 횟수가 아니다.
  //     옛 구조에서는 여기서 이벤트가 추가 전송을 만들어 양의 되먹임 고리가 열렸다.
  if (!netOnline || !heartbeatDue) return;

  uint16_t occSnap = occMask, resSnap = resMask, tmaskSnap = tmaskNow;
  uint8_t  batchAcks = 0;
  uint16_t batchBytes = 0;
  const uint32_t sendAt = (uint32_t)(millis() - slotStart);   // 슬롯 시작 기준 실제 송신 시각
  bool ok = sendSlotBatch(&batchAcks, &batchBytes);

  // ★ 슬롯당 정확히 1거래 — 성패와 무관하게 이번 슬롯의 기회는 썼다.
  //   ⚠ 실패했다고 같은 슬롯에서 다시 쏘면 **슬롯당 1거래 규칙이 깨지고** 수신 창을 침범한다.
  //     재시도는 다음 슬롯이다. 그것이 이 설계가 폭주를 막는 방식이다.
  slotSent = true;

#if DEBUG
  // ★ monitor 계수용 표지 — **매 슬롯 찍는다. `ack=0` 인 슬롯도 찍는다.**
  //   ⚠ 그래야 `0` 이 "묶을 것이 없었다"이지 "못 셌다"가 아니게 된다(원장 §5.1).
  //   ⚠ 문구에 다른 표지의 문자열(`SEND OK`·`busy`·`+IPD`·`[TX]`)을 넣지 마라 —
  //     2026-08-17 에 monitor 파서가 우리 산문 안의 `SEND OK` 를 세어 `0/7` 을 냈다.
  Serial.print(F("[SLOT] n="));   Serial.print(slotNo);
  Serial.print(F(" tx="));        Serial.print(batchBytes);
  Serial.print(F(" ack="));       Serial.print(batchAcks);
  Serial.print(F(" due="));       Serial.print(sendAt);
  Serial.println(ok ? F(" r=1") : F(" r=0"));
#endif

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
  espInit();                        // ★ UART 를 열고 사다리를 시작만 한다 (설계문서 §1.2)

  // ★ 슬롯 위상의 원점. **반드시 여기서 잡는다** — 0 으로 두면 첫 `statusTick` 에서
  //   `now - 0` 만큼의 슬롯을 한꺼번에 소진하느라 while 이 헛돈다(부팅 몇 초면 수 회).
  slotStart = millis();

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

  // ✏️ 사다리 초기화는 `espInit()` 안으로 옮겼다 — **UART 열기와 같이 있어야 하는 것**이다.
  //   떨어져 있으면 한쪽만 고치는 실수가 난다.

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
  espReset(now);
  espRead();
  drainPending();
  // ⚠ 2026-08-17 — `ackqDrain()` 을 뺐다. **보류 ACK 는 이제 슬롯 배치에 실려 나간다**
  //   (`sendSlotBatch`). 여기서 따로 내보내면 슬롯당 1거래 규칙이 깨지고 수신 창을 침범한다.
  sensorTick();
  statusTick(now);
  cntTick(now);                     // ★ DEBUG 밖 — 운영 빌드에서도 관측이 남는다
#if DEBUG
  diagTick(now);
  ramTick(now);
#endif
}
