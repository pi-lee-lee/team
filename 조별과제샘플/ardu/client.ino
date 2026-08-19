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

#include "Boot.h"              // ← 부팅 원인 기록(MCUSR 미러). **위치를 옮기지 마라**
#include "Config.h"            // ← 배선·네트워크 상수·타이밍·반송파 슬롯. **위치를 옮기지 마라**
#include "RxBuf.h"             // ← 수신 버퍼 셋(rxLine·workLine·pendLine)  🔒링크. **위치를 옮기지 마라**
#include "Diag.h"              // ← 현장 진단·RAM 계측  👁관측. **위치를 옮기지 마라**
#include "TxState.h"           // ← 송신 상태·슬롯 상태. **위치를 옮기지 마라**
#include "Checksum.h"          // ← 체크섬(§2.2) — 순수 함수. **위치를 옮기지 마라**
#include "RxLine.h"            // ← 수신 줄 조립(§6.2 1단계)  🔒링크. **위치를 옮기지 마라**
#include "LinkGate.h"          // ← 살아있음 불변식·SEND OK 게이트  🔒링크. **위치를 옮기지 마라**
#include "Counters.h"          // ← 운영 계수기 [CNT]  👁관측 — 🔴잠그지 마라. **위치를 옮기지 마라**
#include "LinkRecovery.h"      // ← IP 소실·소켓 복구·오프라인 전이  🔒링크. **위치를 옮기지 마라**
#include "FrameCodec.h"        // ← S·D·A 프레임 · hex 인코딩 · 슬롯 배치. **위치를 옮기지 마라**

// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **모듈 표 — 기여자가 고치는 곳**                                    █
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
// ⚠ **왜 여기 있나**: `FrameCodec.h` 와 `Modules.h` **사이**여야 한다 —
//   앞에서 `KIND_*` 가, 뒤에서 이 표를 쓰는 코드가 온다. **위치를 옮기지 마라.**
// ██████████████████████████████████████████████████████████████████████████
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
// ██████████████████████████████████████████████████████████████████████████
// █  🔒  아래는 만지지 마라 — **링크 안정성이 여기 걸려 있다**               █
// █     이 코드가 하드웨어 리셋을 불필요하게 만든 물건이다(사용자 확정)      █
// ██████████████████████████████████████████████████████████████████████████
#include "Modules.h"           // ← 모듈 표를 쓰는 코드. **위치를 옮기지 마라**
#include "FrameCodec2.h"       // ← ACK 발행·슬롯 배치. **위치를 옮기지 마라**
#include "Commands.h"          // ← R / C / T 프레임 처리. **위치를 옮기지 마라**
#include "Session.h"           // ← 주기 처리(sensorTick·statusTick·cntTick·diag·ram). **위치를 옮기지 마라**
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

  // 자리 초기화 — 실물로 지정된 칸의 입력 모드까지 **`node` 가 스스로 잡는다**.
  //   ⚠ 생성자가 아니라 여기다: 전역 생성자는 `main()` 전에 돌아 `pinMode` 를 부를 수 없다.
  node.begin();

  // §12A.3 재부팅하면 테스트 오버라이드는 사라진다 — 서버가 재하달하지 않는다(예약과 정반대).
  // 전역이라 어차피 0 이지만, "여기서 버린다"는 것을 코드로 남겨 둔다.
  node.testArmed = false;
  node.slotOverrideClearAll();

  // 시작 시 몇 칸은 차 있는 편이 주차장답다: A2, A3, B4
  // 이 값은 **트리거를 받기 전까지 그대로 유지된다**(§12B.1 — 자율 전진 없음).
  // 🔴 REQ-0270 — **짝 단위로 채운다.** 옛 값 `A2·A3·B4` 는 지형과 어긋났다:
  //   `A2` 의 짝 `B2` 가 비고 `B4` 의 짝 `A4` 가 비어 **한 자리에서 두 센서가 모순**이었다.
  //   지금은 **자리 2·3·4 를 통째로** 채운다(A2·B2 · A3·B3 · A4·B4). 자리 1·5 는 빈다.
  node.simOcc = (uint16_t)((1U << 1) | (1U << 6)     // 자리 2 = A2 + B2
                    | (1U << 2) | (1U << 7)     // 자리 3 = A3 + B3
                    | (1U << 3) | (1U << 8));   // 자리 4 = A4 + B4

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
  node.readSensors();          // 자리 상태를 훑는다 (옛 sensorTick)
  statusTick(now);
  cntTick(now);                     // ★ DEBUG 밖 — 운영 빌드에서도 관측이 남는다
#if DEBUG
  diagTick(now);
  ramTick(now);
#endif
}
