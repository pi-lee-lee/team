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

#include "Slots.h"   // ← 자리 (REQ-0273). **위치를 옮기지 마라**

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

#include "AckQueue.h"   // ← rid 멱등 캐시 (REQ-0273). **위치를 옮기지 마라**

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
  // 🔴 **조치가 실제로 실행된 횟수.** `[LADDER]` 문구는 DEBUG 안이라 DEBUG=0 이면 사라진다.
  //   계수는 밖에 둬서 **"4단이 있다"와 "4단이 돌았다"를 가를 자리**를 만든다(§30 선언 vs 결과).
  if (hwRstAsserts < 65535) hwRstAsserts++;
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
  Serial.print(F(" hwrst="));        Serial.print(hwRstAsserts);   // 4단 실제 실행 수
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

#include "Modules.h"   // ← 모듈 표(자리 유동화의 단일 원천) (REQ-0273). **위치를 옮기지 마라**

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

// 🔴 **멱등 커밋 + ACK 예약을 한 번에.** 순서(캐시 먼저)를 호출부에서 빼앗아 여기 가둔다.
//   ⚠ 둘을 따로 부르면 **캐시를 빠뜨린 재전송이 *다른 답*을 받는다**(§4.2 멱등 파손) — 조용히 틀린다.
//   주석으로만 지키던 순서를 **함수로** 지킨다. 호출부는 이것만 부른다(5곳).
//   🔑 캐시에서 되보내는 재전송 경로(2곳)는 `sendAck` 을 그대로 쓴다 — 그건 이미 캐시에 있다.
static void commitAck(uint16_t rid, char s0, char s1, uint8_t result) {
  cachePut(rid, s0, s1, result);
  sendAck(rid, s0, s1, result);
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
    commitAck(rid, s0, s1, result);
    return;
  }

  if (type == 'T') {
    processTest(f, &s0, &s1, &result);
    commitAck(rid, s0, s1, result);      // §4.2 멱등은 T 에도 그대로 적용된다
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
    commitAck(rid, s0, s1, result);
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

  commitAck(rid, s0, s1, result);
}

#include "EspLink_at.h"   // ← AT 응답 어휘 해석 (REQ-0273). **위치를 옮기지 마라**

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
  // 🔴 하드코딩 "10 slots" 를 **파생값**으로 바꿨다 (§30 · REQ-0273).
  //   ⚠ **"12 slots" 가 아니다.** 12 는 **모듈 수**(`D,*,<drain>,<n>` 의 `n`)이고 자리 수는 10(`SLOT_N`)이다.
  //     socket 이 2026-08-19 에 `n` 은 모듈 수임을 명세에 못 박았다. 둘은 다른 값이다.
  //   🔑 **따로** 찍어야 판본 판별에도 쓰인다 — 옛 판은 "10 slots" 만 찍었다.
  Serial.print(F("\n[PARKING NODE] proto v1 / "));
  Serial.print(SLOT_N);            Serial.print(F(" slots / "));
  Serial.print(moduleCount());     Serial.println(F(" modules / dev=" DEVICE_ID));

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
