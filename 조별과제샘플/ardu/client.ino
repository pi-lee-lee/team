/*
 * client.ino — 주차 관제 노드 (장치 쪽 샘플)
 *
 * 🔴 **기여자가 여는 파일은 이것 하나다.** 자기 것으로 바꿀 자리에 🔓 표시가 있다:
 *      DEVICE_ID · 망 설정(Config.h) · MODULE_TABLE · 명령 핸들러 · setup() 의 router.on
 *
 * 타깃 보드 : Arduino Uno   (FQBN arduino:avr:uno)
 * 무선 모듈 : ESP-01, AT 펌웨어, SoftwareSerial 9600bps
 * 배선      : D7 = ESP TX → Uno(RX) / D8 = Uno(TX) → ESP RX
 *
 * 프로토콜 : docs/net/parking-protocol.md 가 정본이다.
 *            이 파일과 명세가 어긋나면 **명세가 이긴다.**
 *   §2.2  체크섬 = 첫 바이트 ~ 체크섬 앞 쉼표(포함) XOR, 대문자 2자리 hex
 *   §2.4  S(상태)·D(등록)·A(ACK) 송신 / R·C·G·T 수신
 *   §3.3  AT+UART_DEF 를 쓰지 않는다 — ESP-01 플래시에 영구 기록되기 때문
 *   §4.2  rid 멱등 캐시 — 같은 rid 를 다시 받으면 재적용 없이 같은 ACK 를 다시 보낸다
 *   §6.2  수신 4단계: LF 분할 → +IPD,<n>: 뒤 → 타입 문자 → 체크섬
 *
 * 🔴 `String` 을 쓰지 마라(AVR 힙 단편화). 모든 프레임은 고정 char[] + snprintf 로 만든다.
 */

#include <SoftwareSerial.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>

// 진단 출력 스위치. `#ifndef` 인 이유: **소스를 고치지 않고** DEBUG=0 빌드를 만들 수 있어야
//   두 빌드의 차이가 DEBUG 하나뿐임이 보장된다. 소스를 고쳐 가며 구우면 그 보장이 깨진다.
//     arduino-cli compile --build-property "compiler.cpp.extra_flags=-DDEBUG=0" ...
#ifndef DEBUG
#define DEBUG 1
#endif

// ██████████████████████████████████████████████████████████████████████████
// █  🔴🔴  **이 노드의 이름 — 반드시 자기 것으로 바꿔라**                    █
// ██████████████████████████████████████████████████████████████████████████
//
//   규격 : **1~8자** · 영문자·숫자·`_`·`-` 만 (명세 §2.3 `devid ::= 1*8자`)
//   예   : "P1"  "P2"  "kim"  "lab-3"
//
// 🔴 **안 바꾸면 무슨 일이 벌어지나 — 이게 이 칸이 여기 있는 이유다:**
//   · **로컬에서 혼자 시험할 때는 아무 문제가 없다.** 충돌할 상대가 없기 때문이다
//   · 🔴 **여러 사람의 노드를 한 서버에 모으는 순간 같은 이름끼리 서로를 쫓아낸다.**
//     서버는 `devid` 로 노드를 가르고 **같은 이름이 오면 앞의 것을 대체**한다
//   · 증상이 "내 자리가 가끔 사라진다"로 나타나 **원인을 찾기 매우 어렵다** —
//     각자 로컬에서는 잘 되던 것이라 아무도 자기를 의심하지 않는다
//
// ⚠ 값을 바꾸면 **다시 구워야** 한다(컴파일 상수다).
// ██████████████████████████████████████████████████████████████████████████
#define DEVICE_ID    "P1"        // ← 🔴 **여기를 바꿔라**

// 길이를 컴파일이 막는다 — 주석은 안 읽혀도 이건 못 지나간다.
//   `sizeof` 는 NUL 을 포함하므로 1~8자 = 2~9 바이트다.
static_assert(sizeof(DEVICE_ID) > 1 && sizeof(DEVICE_ID) <= 9,
              "DEVICE_ID 는 1~8자여야 한다 (명세 §2.3). 빈 값도 9자 이상도 안 된다");

// ─────────────────────────────────────────────────────────────────────────
// 🔴🔴 **이 include 들의 순서와 위치를 바꾸지 마라.**
//   스케치는 전처리된 하나의 파일로 컴파일된다 — 순서를 바꾸면 **선언 순서와
//   전역 초기화 순서가 같이 바뀌고, 산출물이 달라진다.** 아래 표 사이에 끼워 넣는
//   것도 마찬가지다(모듈 표가 `FrameCodec.h` 와 `Modules.h` 사이에 있는 이유다).
//   🔑 새 헤더를 더할 자리는 **이 목록의 끝**이다.
//
//   🔒 = 링크 계층(ESP 와의 통신 안정성) · 👁 = 관측
// ─────────────────────────────────────────────────────────────────────────
#include "Boot.h"              // 부팅 원인 기록(MCUSR 미러)
#include "Config.h"            // 🔓 배선·망 설정·타이밍·반송파 슬롯 — **SSID/IP 를 여기서 바꾼다**
#include "RxBuf.h"             // 🔒 수신 버퍼 셋(rxLine·workLine·pendLine)
#include "Diag.h"              // 👁 현장 진단·RAM 계측
#include "TxState.h"           // 송신 상태·슬롯 상태
#include "Checksum.h"          // 체크섬(§2.2) — 순수 함수
#include "RxLine.h"            // 🔒 수신 줄 조립(§6.2 1단계)
#include "LinkGate.h"          // 🔒 살아있음 불변식·SEND OK 게이트
#include "Counters.h"          // 👁 운영 계수기 [CNT] — 계수기를 더하는 것은 정상 작업이다
#include "LinkRecovery.h"      // 🔒 IP 소실·소켓 복구·오프라인 전이
#include "FrameCodec.h"        // S·D·A 프레임 · hex 인코딩 · 슬롯 배치

// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **모듈 표 — 이 장치의 자기 구성. 자기 것으로 바꿔라**              █
// ██████████████████████████████████████████████████████████████████████████
//
// 🔴 **아래 내용은 *우리 장치의 예시* 다. 지우고 자기 것을 써라.**
//   ⚠ **예시를 그대로 두고 자기 것을 덧붙이지 마라** — 이름이 곧 자리 결속 키라
//     같은 이름이 여럿이면 **등록은 성공하고 자리에는 아무것도 안 붙는다. 오류가 안 뜬다.**
//
//   한 줄 = 모듈 하나 :  {"이름", 종류, 핀}
//     이름 : **자리 id 와 같아야 한다**(2자 + NUL). `A1`·`B3`·`E1` …
//     종류 : KIND_PARK_SENSOR(`IP` 관측) · KIND_GATE_SENSOR(`IX`) · KIND_BARRIER(`OB` 명령)
//     핀   : 실물이면 핀 번호, 가상이면 `PIN_NONE`
//
// ⚠ **이 표는 `FrameCodec.h` 와 `Modules.h` 사이에 있어야 한다** —
//   앞에서 `KIND_*` 가 정의되고, 뒤에서 이 표를 읽는 코드가 온다.
// ██████████████████████████████████████████████████████████████████████████
struct ModuleDef {
  char    name[3];      // "A1" + NUL — 명칭이자 **지금은 자리 결속 키다**(위 경고)
  char    kind[4];      // KIND_* 2글자 + 선택적 `V` 접미(가상) + NUL
  uint8_t pin;
};
// 🔴 **가상 모듈 스위치 — 기본값 0(끔)**
//   가상 차단봉 `E1`·`X1` 은 **시험용**이다. 실물 없이 명령 사슬을 밟아 보려면 1 로 켠다.
//   ⚠ **실물 모듈이 있으면 0 으로 둬라** — 켠 채 실물을 붙이면 같은 자리에 둘이 붙는다.
//   🔑 회귀 시험은 이 값을 1 로 켜서 빌드한다 — 그래야 콜백 경로가 실제로 밟힌다.
#ifndef VIRTUAL_MODULES
#define VIRTUAL_MODULES 0
#endif
// 🔓 **샘플 액추에이터 스위치 — 기본값 1(켬)**
//   `LD`(LED) · `LC`(표시기) · `DR`(문) 셋. **명령 왕복 세 꼴이 이 샘플의 본문**이라 켜 둔다.
//   실물이 안 달려 있어도 `digitalWrite` 는 성공하고 ACK `result=0` 이 돌아온다 —
//   **왕복 자체는 그대로 검증된다.**
//   ⚠ **자기 장치에 이 셋이 없으면 0 으로 꺼라.** 없는 모듈을 선언하면 화면에 유령이 뜬다.
#ifndef SAMPLE_ACTUATORS
#define SAMPLE_ACTUATORS 1
#endif
// 샘플 액추에이터의 핀. 🔴 D7·D8 은 ESP 가 쓴다 — 겹치면 통신이 죽는다.
#define PIN_SAMPLE_LED   5
#define PIN_SAMPLE_DOOR  6

static const ModuleDef MODULE_TABLE[] PROGMEM = {
  // 🔓 **자리 하나 · 센서 둘** — 서버 샘플과 짝이다:
  //      lot.spot("A1").sensor("P1","A1").sensor("P1","B1");
  //   🔴 **서버가 A1 하나만 보는데 장치가 더 보내면 나머지는 "미결속"으로 뜬다.**
  //     늘릴 때는 **여기 한 줄 + 서버 조립 표 한 줄**, 둘 다다.
  {"A1", KIND_PARK_SENSOR, 2},    // 자리 A1 의 첫째 센서 — 2번 핀
  {"B1", KIND_PARK_SENSOR, 9},    // 자리 A1 의 둘째 센서 — 9번 핀

  // 🔓 **늘리려면 주석을 풀고 `SENSOR_N`·`SLOT_PIN[]` 도 같이 늘려라**(셋이 어긋나면 컴파일이 막는다)
  // {"A2", KIND_PARK_SENSOR,  3}, {"B2", KIND_PARK_SENSOR, 10},
  // {"A3", KIND_PARK_SENSOR,  4}, {"B3", KIND_PARK_SENSOR, 11},
  // {"A4", KIND_PARK_SENSOR,  5}, {"B4", KIND_PARK_SENSOR, 12},
  // {"A5", KIND_PARK_SENSOR,  6}, {"B5", KIND_PARK_SENSOR, A0},

  // 🔓 **액추에이터(명령을 받는 모듈)** — 종류를 `O` 로 시작하는 것으로 하고
  //   아래 `setup()` 에서 `router.on()` 으로 핸들러를 붙인다.
  //   쓸 수 있는 종류: `KIND_GUIDE_LIGHT`(OG 안내등) · `KIND_LEAD_LIGHT`(OL 유도등) ·
  //                    `KIND_BARRIER`(OB 차단봉)
  //   🔑 **첫 글자 `O` 가 "명령을 받는다"를 뜻한다.** 서버는 그 한 글자만 본다.
  //
  // 🔴🔴 **이름은 정확히 2글자다.** `char name[3]` 이고 전선에도 2글자만 나간다 —
  //   `"LED1"` 처럼 3글자 이상을 적으면 **컴파일이 막는다**(초기화 문자열이 배열보다 길다).
  //   ⚠ 서버 조립 표(`main.cpp`)에 적는 이름과 **글자 그대로** 같아야 붙는다.
  // 🔴 **중간에 끼워 넣지 마라.** 전선의 `idx` 는 **이 표의 순서**다 —
  //   중간 삽입은 뒤의 `idx` 를 전부 밀어 **지금 되는 결속을 조용히 깬다.** 끝에 붙여라.
#if SAMPLE_ACTUATORS
  {"LD", KIND_GUIDE_LIGHT, PIN_SAMPLE_LED},    // ① on/off
  {"LC", KIND_LEAD_LIGHT,  PIN_NONE},          // ② 7자리 숫자 (표시기 — 핀은 라이브러리가 잡는다)
  {"DR", KIND_BARRIER,     PIN_SAMPLE_DOOR},   // ③ 동작 명령
#endif
#if VIRTUAL_MODULES
  // ⚠ **시험용 가상 차단봉.** 기본값은 꺼져 있다 — `VIRTUAL_MODULES` 를 1 로 켤 때만 실린다.
  //   회귀 시험이 이것으로 **명령 수신 콜백 경로를 실제로 밟는다.**
  {"E1", KIND_BARRIER_V, PIN_NONE},   // 입구 차단봉 (가상)
  {"X1", KIND_BARRIER_V, PIN_NONE},   // 출구 차단봉 (가상)
#endif
};
static const uint8_t MODULE_N = (uint8_t)(sizeof(MODULE_TABLE) / sizeof(MODULE_TABLE[0]));
// ██████████████████████████████████████████████████████████████████████████
// █  ⚠  아래부터는 **고쳐도 된다. 다만 비용이 있다**                        █
// ██████████████████████████████████████████████████████████████████████████
//
// 🔴 **금지가 아니다.** 아래(그리고 🔒 표시된 헤더들)는 **링크 안정성이 걸린 코드**다:
//   · ESP 가 꼬였을 때 **자력으로 돌아오는 경로**가 여기서 나온다.
//     이 코드가 도는 덕분에 **하드웨어 리셋선이 필요 없다.**
//   · 고치면 **그 안정성 기준선을 다시 재야 한다.** 기준선은 이렇게 생겼다 —
//     무자극 41분 동안 `CIFSR 0.0.0.0` 0회 · `LADDER 실패` 0회 · `T2` 0회 ·
//     `esprst` 0회 · 깨진 나가는 AT 0건
//
// 🔑 **그러니 "만지지 마라"가 아니라 "만지면 그 비용을 안다"이다.**
//   **비용을 알고 고치는 것은 정상 작업이다.** 고쳤으면 **알려라** — 기준선을 다시 재면 된다.
// ██████████████████████████████████████████████████████████████████████████
#include "Modules.h"           // 모듈 표를 쓰는 코드 · 명령 라우터
#include "FrameCodec2.h"       // ACK 발행·슬롯 배치
#include "Commands.h"          // R / C / G / T 프레임 처리
#include "Session.h"           // 주기 처리(statusTick·cntTick)
// ─────────────────────────────────────────────────────────────────────────
// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **명령 수신 핸들러 — 자기 액추에이터를 여기 붙인다**                █
// ██████████████████████████████████████████████████████████████████████████
//
//   모양 :  `bool 이름(uint32_t arg)`   — 반환 **true = 성공**(ACK `result=0`) · false = 수행 불가(3)
//   등록 :  아래 `setup()` 에서 `router.on("모듈이름", 핸들러);`
//
// ██ 🔴 **`arg` 의 뜻은 *네가* 정한다 — 그리고 그 표를 여기 적어라** ██████████████
//   **서버도 프로토콜도 `arg` 의 뜻을 모른다.** 숫자를 나르기만 한다. 32비트라 7자리도 그대로 온다.
//   🔴 **그 뜻을 서버 쪽 사람과 맞춰야 한다** — 안 맞으면 **조용히 다른 동작을 한다.**
//     ⚠ **의미는 값으로 검증되지 않는다.** 프레임은 멀쩡하고 체크섬도 맞는데 동작만 다르다.
//
//   ── 세 가지 꼴 (전부 같은 통로다. 다른 것은 **네가 정한 뜻**뿐이다) ──
//   ① **on/off**   `bool led(uint32_t a) { digitalWrite(PIN_LED, a ? HIGH : LOW); return true; }`
//   ② **숫자 전달** `bool lcd(uint32_t a) { lcdPrint(a); return true; }`   // 예: 1234567 (7자리)
//   ③ **동작 명령** — **뜻을 정하는 표를 여기 적는다. 이 표가 곧 "명령 작성 방법"이다**
//        ```
//        arg 1 = 열기 · 2 = 닫기 · 3 = 잠금 · 4 = 해제      ← 예시일 뿐이다. 네 것을 적어라
//        bool door(uint32_t a) {
//          switch (a) { case 1: open(); break; case 2: close(); break;
//                       default: return false; }              // 🔴 모르는 값은 false
//          return true;
//        }
//        ```
//     ⚠ **모르는 값에 `true` 를 돌려주지 마라** — 서버는 성공으로 알고 사람은 왜 안 되는지 모른다.
//
// ██ 🔴 **거절할 때도 한 줄 남겨라 — 이게 규칙이다** ██████████████████████████
//   `false` 를 내는 자리마다 로그를 하나 둬라. 안 두면 **"거절했다"와 "안 불렸다"가
//   장치 쪽에서 같은 모양**이 된다 — 둘 다 아무 줄도 안 나오기 때문이다.
//   ⚠ 그리고 그것이 **기여자가 가장 먼저 겪는 실패**다: 값을 보냈는데 `result=3` 이 왔고,
//     시리얼에는 아무것도 없다. *"내 콜백이 안 불린 건가, 불렸는데 거절한 건가?"* — 답이 없다.
//   🔑 **판별자**: *"내 콜백이 `false` 를 낼 수 있는 모든 자리에 로그가 있는가."*
//   (라우터도 `[G] 거절 … — 등록 없음 / 콜백이 거절` 로 갈라 준다. 이 둘이 겹쳐야 완전하다.)
// ████████████████████████████████████████████████████████████████████████
//
// ⚠ **등록 안 한 모듈에 명령이 오면 `result=3`(수행 불가)로 답한다** — 조용히 성공하지 않는다.
// 🔑 **이름으로 등록한다.** 전선은 `idx`(표 순서)로 오는데 표를 고치면 idx 가 밀린다 —
//   **이름은 안 밀린다.** 그 변환은 `CommandRouter` 가 한다.
#if SAMPLE_ACTUATORS
// ═══════════════════════════════════════════════════════════════════════════
// 🔓 **샘플 — 명령 왕복 세 꼴.** 서버 쪽 `srv.send(devid, 모듈이름, 값)` 과 짝이다.
//   셋 다 **같은 통로**다. 다른 것은 **네가 정한 값의 뜻**뿐이다.
// ═══════════════════════════════════════════════════════════════════════════

// ── ① on/off ───────────────────────────────────────────────────────────────
//   [LD 명령표]  0 = 끔 · 그 외 = 켬
//   서버:  srv.send("P1", "LD", 1);
static bool cmdLed(uint32_t arg) {
  digitalWrite(PIN_SAMPLE_LED, arg ? HIGH : LOW);
  // 🔑 **거절 로그가 없는 이유**: 이 핸들러는 `false` 를 낼 자리가 없다.
  //   모든 값에 뜻이 있다(0=끔 · 그 외=켬). 거절이 없으면 남길 것도 없다.
  return true;
}

// ── ② 7자리 숫자를 그대로 전달 ─────────────────────────────────────────────
//   [LC 명령표]  값 = 표시할 수 (0 ~ 9999999)
//   서버:  srv.send("P1", "LC", 1234567);
//   🔴 `arg` 는 **부호 없는 32비트**다. 7자리는 여유 있게 들어간다.
//     ⚠ 옛 파서는 16비트라 65,535 를 넘으면 `result=3` 이 나갔다. 지금은 32비트로 받는다.
// 🔴 **실물 LCD 없이도 안전하다 — 라이브러리를 안 붙였기 때문이다.**
//   여기서 하는 일은 USB 시리얼에 한 줄 찍는 것뿐이고, ESP 링크(SoftwareSerial)는 안 건드린다.
//   ⚠ **실물 LCD 라이브러리를 붙일 때 그 안전이 사라진다:**
//     `LiquidCrystal` 류는 `delayMicroseconds` 로 수십~수백 µs 를 **블로킹**하고,
//     I2C 판은 버스가 죽어 있으면 **타임아웃까지 멈춘다.**
//     🔴 이 핸들러는 **수신 창 안에서 불린다** — 여기서 오래 멈추면 그 슬롯의 수신을 잃는다.
//     🔑 붙일 거면 **값만 저장하고 실제 출력은 `loop()` 의 송신 창에서** 해라.
static bool cmdLcd(uint32_t arg) {
  if (arg > 9999999UL) {             // 🔴 7자리를 넘는 값은 **거절한다**(표시할 수 없다)
#if DEBUG
    // 🔴 **거절도 남긴다.** 이 줄이 없으면 "안 불렸다"와 구분이 안 된다.
    Serial.print(F("[LC] 거절 — 7자리 초과: ")); Serial.println(arg);
#endif
    return false;
  }
#if DEBUG
  Serial.print(F("[LC] "));  Serial.println(arg);
#endif
  return true;
}

// ── ③ 동작 명령 — **뜻을 정하는 표를 여기 적는다. 이 표가 곧 '명령 작성 방법'이다** ──
//
//   [DR 명령표]   (P1 / 모듈 "DR" · 정한 사람: ____ )
//     1 = 열기    2 = 닫기    3 = 잠금    4 = 잠금해제
//     ⚠ 그 밖의 값은 `result=3`(수행 불가)로 답한다
//   서버:  srv.send("P1", "DR", 1);   // 열기
//
// 🔴 **이 표를 서버 호출부에도 똑같이 적어라.** 서버도 프로토콜도 값의 뜻을 모른다 —
//   그냥 32비트 정수를 실어 나른다. **표가 어긋나면 조용히 다른 동작을 한다.**
//   ⚠ 오류도 안 뜬다: 서버는 `2` 를 보냈고 장치는 `2` 를 받았으니 **양쪽 다 성공**이다.
//   🔑 **의미는 값으로 검증되지 않는다. 사람이 맞추는 수밖에 없다.**
// ❌ 표 없이 `1,2,3,4` 만 쓰지 마라 — 반년 뒤에 아무도 못 읽는다.
static bool cmdDoor(uint32_t arg) {
  switch (arg) {
    case 1: digitalWrite(PIN_SAMPLE_DOOR, HIGH); break;   // 열기
    case 2: digitalWrite(PIN_SAMPLE_DOOR, LOW);  break;   // 닫기
    case 3: /* 잠금   — 자기 장치에 맞게 채운다 */         break;
    case 4: /* 잠금해제 */                                 break;
    default:
#if DEBUG
      // 🔴 **거절도 남긴다.** 그리고 **무엇이 왔는지** 같이 찍는다 —
      //   표를 서버 쪽과 잘못 맞춘 경우가 가장 흔하고, 그때 필요한 것이 이 숫자다.
      Serial.print(F("[DR] 거절 — 명령표에 없는 값: ")); Serial.println(arg);
#endif
      return false;        // 🔴 모르는 값에 true 를 돌려주지 마라 —
                           //   서버는 성공으로 알고 사람은 왜 안 되는지 모른다
  }
#if DEBUG
  Serial.print(F("[DR] arg=")); Serial.println(arg);
#endif
  return true;
}
#endif  // SAMPLE_ACTUATORS

// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **센서 읽기 핸들러 — 자기 센서를 여기 붙인다**                      █
// ██████████████████████████████████████████████████████████████████████████
//
//   모양 :  `bool 이름(uint8_t pin)`   — 반환 **true = 찼다**
//   등록 :  아래 `setup()` 에서 `sensors.on("모듈이름", 핸들러);`
//   🔑 **명령 쪽 `router.on` 과 같은 모양이다.** 한 번만 배우면 양쪽에 쓴다.
//
//   안 붙이면 기본값은 `digitalRead(핀)` 이다 — 리드 스위치·적외선 모듈처럼
//   **켜짐/꺼짐을 그대로 내는 센서**는 아무것도 안 해도 된다.
//
// ── 예시: HC-SR04 초음파 거리 센서 ─────────────────────────────────────────
//   🔴 **이 예시가 여기 있는 이유**: 초음파는 **거리(숫자)** 를 내는데 자리 상태는 참/거짓이다.
//     *"몇 cm 아래면 찼다고 볼 것인가"* 를 **장치를 단 사람이** 정해야 한다.
//     그 판정을 비워 두면 서버나 화면이 대신 정하게 되고 **규칙이 두 곳에 생긴다.**
//
//   쓰려면: ① 아래 핀 두 개를 자기 배선에 맞추고
//           ② `setup()` 의 `sensors.on("A1", ultrasonicRead);` 주석을 풀고
//           ③ `setup()` 에서 `pinMode(PIN_US_TRIG, OUTPUT); pinMode(PIN_US_ECHO, INPUT);`
//              🔴 **등록한 칸은 기본 `INPUT_PULLUP` 을 안 걸어 준다** — 네가 잡아라
#define PIN_US_TRIG 3
#define PIN_US_ECHO 4
static const uint16_t US_OCCUPIED_CM = 60;   // 🔓 이보다 가까우면 "찼다" — **네가 정하는 문턱**

static bool ultrasonicRead(uint8_t pin) {
  (void)pin;                      // 이 센서는 핀을 둘 쓴다 — 모듈 표의 핀은 안 쓴다
  // 🔴 **이 함수는 매 `loop()` 마다 불린다.** 매번 재면 `pulseIn` 의 블로킹이 슬롯을 민다.
  //   그래서 **간격을 두고 값을 캐시한다.** 200ms 면 차가 들어오는 속도에 충분하다.
  static unsigned long lastAt  = 0;
  static bool          lastVal = false;
  const unsigned long now = millis();
  if (now - lastAt < 200UL) return lastVal;
  lastAt = now;

  digitalWrite(PIN_US_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_US_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);
  // ⚠ **타임아웃을 반드시 줘라.** 안 주면 기본값이 1초라 반향이 없을 때 장치가 1초 멈춘다.
  //   6,000µs ≈ 100cm. 이 값이 곧 **최악 블로킹 시간(6ms)** 이다 — 슬롯 600ms 의 1%.
  const unsigned long us = pulseIn(PIN_US_ECHO, HIGH, 6000UL);
  if (us == 0) { lastVal = false; return false; }   // 반향 없음 = 범위 밖 = 비었다
  lastVal = (us / 58UL) < US_OCCUPIED_CM;           // 왕복이라 58 로 나누면 cm
  return lastVal;
}

#if VIRTUAL_MODULES
// 시험용 가상 차단봉의 핸들러. **실물 액추에이터도 똑같은 모양으로 쓴다** —
//   실물이 붙으면 아래 `setup()` 의 등록 한 줄만 바꾸면 된다.
static bool virtualGate(uint8_t k, uint32_t arg) {
  gates.latch(GATE_N, slotNo);      // 첫 명령이 자율 토글을 영구 정지시킨다
  gates.set(k, arg != 0);
  return true;
}
static bool gateE1(uint32_t arg) { return virtualGate(0, arg); }
static bool gateX1(uint32_t arg) { return virtualGate(1, arg); }
#endif

void setup() {
  Serial.begin(115200);
  espInit();                        // UART 를 열고 접속 사다리를 시작만 한다 (여기서 기다리지 않는다)

  // 슬롯 위상의 원점. 🔴 **반드시 여기서 잡는다** — 0 으로 두면 첫 `statusTick` 에서
  //   `now - 0` 만큼의 슬롯을 한꺼번에 소진하느라 while 이 헛돈다.
  slotStart = millis();

  // 난수 시드는 **아무 데도 안 물린 아날로그 핀**에서 뽑는다 — 물려 있으면 노이즈가 안 나온다.
  // ⚠ `MODULE_TABLE` 에서 아날로그 핀을 센서로 쓰면 그 핀을 여기 쓰지 마라.
  randomSeed((unsigned long)analogRead(A1) ^ micros());

  // 자리 초기화 — 실물로 지정된 칸의 입력 모드까지 **`node` 가 스스로 잡는다**.
  //   ⚠ 생성자가 아니라 여기다: 전역 생성자는 `main()` 전에 돌아 `pinMode` 를 부를 수 없다.
  node.begin();

  // 🔓 **센서 읽기 등록 — 자기 센서를 여기 붙인다**
  //   안 붙이면 `digitalRead(핀)` 이 기본값이다. **지금 되는 것은 그대로 된다.**
  // sensors.on("A1", ultrasonicRead);
  // pinMode(PIN_US_TRIG, OUTPUT);  pinMode(PIN_US_ECHO, INPUT);   // ← 같이 풀어라

  // 🔓 **명령 수신 등록 — 자기 액추에이터를 여기 붙인다**
  //   예)  `router.on("G1", myGate);`   ← 표에 `{"G1", KIND_BARRIER, 7}` 을 더한 뒤
  //   ⚠ 등록 안 한 모듈에 명령이 오면 `result=3`(수행 불가)로 답한다. 조용히 성공하지 않는다.
#if SAMPLE_ACTUATORS
  pinMode(PIN_SAMPLE_LED,  OUTPUT);
  pinMode(PIN_SAMPLE_DOOR, OUTPUT);
  router.on("LD", cmdLed);      // ① on/off
  router.on("LC", cmdLcd);      // ② 7자리 숫자
  router.on("DR", cmdDoor);     // ③ 동작 명령
#endif
#if VIRTUAL_MODULES
  router.on("E1", gateE1);      // 시험용 가상 차단봉 — 실물이 오면 이 줄만 바꾼다
  router.on("X1", gateX1);
#endif

  // 재부팅하면 테스트 오버라이드는 사라진다 — 서버가 다시 내려보내지 않는다(예약과 정반대).
  // 전역이라 어차피 0 이지만, **"여기서 버린다"를 코드로 남겨 둔다.**
  node.testArmed = false;
  node.slotOverrideClearAll();

  // 🔓 모의 점유의 시작 상태. 이 값은 **트리거를 받기 전까지 그대로 유지된다**(자율 전진 없음).
  node.simOcc = 0;   // 샘플은 **빈 자리로 시작**한다.
                     //   채우려면 `(1U<<0) | (1U<<1)` 처럼 **한 자리의 센서를 짝으로** 넣어라.
                     //   🔴 한쪽만 넣으면 **한 자리에서 두 센서가 모순된 값**을 내고,
                     //     서버는 그 자리에서 갈린 두 값을 보게 된다.

  // ESP 리셋선은 **놓은 상태(하이임피던스)로 시작**한다.
  //   전원 인가 직후 AVR 핀은 원래 INPUT 이라 이미 떠 있지만, **"여기서 명시적으로 놓는다"**를
  //   코드로 남긴다. 🔴 실수로 OUTPUT LOW 로 두면 ESP 가 영원히 리셋에 잡혀 아무 일도 안 난다.
#if ESP_RST_WIRED
  pinMode(PIN_ESP_RST, INPUT);
#endif

#if DEBUG
  // 🔴 **셋이 서로 다른 값이다. 하나만 찍으면 반드시 오독된다.**
  //     `SPOT_N`        = **자리 수**  — 서버 조립 표의 `spot` 수와 비교할 값
  //     `SENSOR_N`      = **센서 수**  — 자리마다 둘이므로 자리 수의 2배다
  //     `moduleCount()` = **모듈 수**  — 센서 + 액추에이터. `D,*,<drain>,<n>` 의 `n` 이 이것이다
  //   ⚠ 옛 판은 센서 수 하나만 `slots` 라고 찍었다 — **센서 수를 자리 수로 읽게 만들었다.**
  Serial.print(F("\n[PARKING NODE] proto v1 / "));
  Serial.print(SPOT_N);         Serial.print(F(" spots / "));
  Serial.print(SENSOR_N);            Serial.print(F(" sensors / "));
  Serial.print(moduleCount());     Serial.println(F(" modules / dev=" DEVICE_ID));

  // ── 부팅 원인 — **추측을 사실로 바꾸는 한 줄**. 왜 재부팅했는지는 여기서만 알 수 있다 ──
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
// 오프라인인 동안 3초마다 한 줄. 🔴 **이 한 줄이 원인을 셋으로 가른다:**
//   rx=0                 → ESP→Uno 로 바이트가 아예 안 온다. 배선(D7)·레벨·모듈 전원을 봐라
//   rx>0, lines=0        → 바이트는 오는데 줄이 안 끊긴다. 줄 종단이 LF 가 아닐 수 있다
//   lines>0, online=0    → 줄은 오는데 접속 문구를 못 알아본다. [AT] 로그에서 실제 문구를 봐라
// 셋 중 무엇인지 모르는 채로 고치면 또 빗나간다.
static void diagTick(unsigned long now) {
  if (netOnline) return;
  if (now - dbgLastDiag < DIAG_PERIOD_MS) return;
  dbgLastDiag = now;
  Serial.print(F("[DIAG] offline step="));  Serial.print(netStep);
  // 사다리의 현재 칸을 같이 찍는다. 없으면 3초마다 같은 줄이 흘러갈 뿐
  //   **"지금 무엇을 하며 기다리는 중인가"**를 로그에서 알 수 없다.
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
  wdt_reset();                      // 여기 못 오면(=행) 8초 뒤 AVR 이 스스로 리셋된다
#endif
  unsigned long now = millis();
  espReset(now);
  espRead();
  drainPending();
  // 🔴 **보류 ACK 를 여기서 따로 내보내지 마라.** 슬롯 배치(`sendSlotBatch`)가 같이 싣는다 —
  //   따로 내보내면 **슬롯당 1거래 규칙이 깨지고 수신 창을 침범한다.**
  node.readSensors();               // 자리 상태를 훑는다
  statusTick(now);
  cntTick(now);                     // 🔴 DEBUG 밖이다 — 운영 빌드에서도 관측이 남아야 한다
#if DEBUG
  diagTick(now);
  ramTick(now);
#endif
}
