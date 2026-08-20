/*
 * client.ino — 주차 관제 노드 (장치 쪽 샘플)
 *
 * 🔴 **기여자가 여는 파일은 이것 하나다.** 고칠 자리에 🔓 표시가 있다:
 *      DEVICE_ID · 망 설정(Config.h) · MODULE_TABLE · 명령/센서 핸들러 · setup() 의 등록
 * 📖 설명서 : **같은 폴더의 `GUIDE.md`** — 🔴 **처음이면 §0 부터 읽어라**
 *      (프로젝트 이름을 `client` 로 안 만들면 **첫 컴파일에서 막힌다**. §0 이 그 이유다)
 *
 * 보드 : Arduino Uno (`arduino:avr:uno`) · ESP-01 AT 펌웨어 · SoftwareSerial 9600bps
 * 배선 : D7 = ESP TX → Uno / D8 = Uno → ESP RX
 *
 * 🔴 `String` 을 쓰지 마라(AVR 힙 단편화). 프레임은 고정 char[] + snprintf 로 만든다.
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
//   규격 : **1~8자** · 영문자·숫자·`_`·`-` 만 · 예: "P1" "kim" "lab-3"
//
// 🔴 **안 바꿔도 로컬에서는 잘 된다. 여럿을 한 서버에 모으는 순간 서로를 쫓아낸다.**
//   증상이 "내 자리가 가끔 사라진다"라 원인을 찾기 매우 어렵다 → `GUIDE.md` §1
// ⚠ 값을 바꾸면 **다시 구워야** 한다(컴파일 상수다).
// ██████████████████████████████████████████████████████████████████████████
#define DEVICE_ID    "P1"        // ← 🔴 **여기를 바꿔라**

// 길이를 컴파일이 막는다 — 주석은 안 읽혀도 이건 못 지나간다.
//   `sizeof` 는 NUL 을 포함하므로 1~8자 = 2~9 바이트다.
static_assert(sizeof(DEVICE_ID) > 1 && sizeof(DEVICE_ID) <= 9,
              "DEVICE_ID 는 1~8자여야 한다 (명세 §2.3). 빈 값도 9자 이상도 안 된다");

// ─────────────────────────────────────────────────────────────────────────
// 🔴🔴 **순서와 위치를 바꾸지 마라** — 전처리 결과가 바뀌면 산출물이 달라진다.
//   ⚠ 모듈 표가 `FrameCodec.h` 와 `Modules.h` 사이에 있는 것도 그 때문이다.
//   🔑 새 헤더는 **목록 끝**에.   🔒 = 링크 계층 · 👁 = 관측
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
//     종류 : "IP"(`IP` 관측) · "IX"(`IX`) · "OB"(`OB` 명령)
//     핀   : 실물이면 핀 번호, 가상이면 `PIN_NONE`
//
// ⚠ **이 표는 `FrameCodec.h` 와 `Modules.h` 사이에 있어야 한다** —
//   뒤에서 이 표를 읽는 코드가 온다.
// ██████████████████████████████████████████████████████████████████████████
struct ModuleDef {
  char    name[3];      // "A1" + NUL — 명칭이자 **지금은 자리 결속 키다**(위 경고)
  char    kind[4];      // 🔴 **첫 글자만 뜻이 있다**: `I`=관측 전용 · `O`=명령 받음
                        //   둘째 글자부터는 자유다(서버는 안 본다). 2~3글자 + NUL
  uint8_t pin;
};
// 🔴 **가상 모듈 스위치 — 기본값 0(끔)**
//   가상 차단봉 `E1`·`X1` 은 **시험용**이다. 실물 없이 명령 사슬을 밟아 보려면 1 로 켠다.
//   ⚠ **실물 모듈이 있으면 0 으로 둬라** — 켠 채 실물을 붙이면 같은 자리에 둘이 붙는다.
//   🔑 회귀 시험은 이 값을 1 로 켜서 빌드한다 — 그래야 콜백 경로가 실제로 밟힌다.
#ifndef VIRTUAL_MODULES
#define VIRTUAL_MODULES 0
#endif
// 샘플 액추에이터의 핀. 🔴 D7·D8 은 ESP 가 쓴다 — 겹치면 통신이 죽는다.
// 🔓 **LED 는 보드에 이미 달려 있는 13번**(`LED_BUILTIN`). **아무것도 안 사도 된다.**
//   🔑 보드가 바뀌어도 `LED_BUILTIN` 이 따라간다 — 숫자를 박지 않는 이유다.
#define PIN_SAMPLE_LED   LED_BUILTIN

static const ModuleDef MODULE_TABLE[] PROGMEM = {
  // 🔓 **자리 하나 · 센서 둘** — 서버 샘플과 짝이다:
  //      lot.spot("A1").sensor("P1","A1").sensor("P1","B1");
  //   🔴 **서버가 A1 하나만 보는데 장치가 더 보내면 나머지는 "미결속"으로 뜬다.**
  //     늘릴 때는 **여기 한 줄 + 서버 조립 표 한 줄**, 둘 다다.
  {"A1", "IP", 2},    // 자리 A1 의 첫째 센서 — 2번 핀
  {"B1", "IP", 9},    // 자리 A1 의 둘째 센서 — 9번 핀

  // 🔓 **늘리려면 주석을 풀고 `SENSOR_N`·`SLOT_PIN[]` 도 같이 늘려라**(셋이 어긋나면 컴파일이 막는다)
  // {"A2", "IP",  3}, {"B2", "IP", 10},
  // {"A3", "IP",  4}, {"B3", "IP", 11},
  // {"A4", "IP",  5}, {"B4", "IP", 12},
  // {"A5", "IP",  6}, {"B5", "IP", A0},

  // 🔓 **액추에이터** — 종류를 **`O` 로 시작**하게 적으면 명령을 받는다.
  //   🔑 첫 글자 `O` 가 "명령을 받는다"를 뜻한다. 아래 `setup()` 에서 `router.on()` 으로 붙인다.
  // 🔴🔴 **이름은 정확히 2글자다.** 3글자 이상은 컴파일이 막는다.
  //   ⚠ 서버 조립 표(`main.cpp`)의 이름과 **글자 그대로** 같아야 붙는다.
  // 🔴 **중간에 끼워 넣지 마라. 끝에 붙여라** — 전선의 `idx` 가 이 표의 순서라
  //   중간 삽입은 뒤의 idx 를 전부 밀어 **지금 되는 결속을 조용히 깬다.**
  {"LD", "OG", PIN_SAMPLE_LED},   // 🔓 보드 내장 LED. **아무것도 안 사도 된다**
  {"L2", "OL", PIN_NONE},         // 🔓 숫자를 받는 표시기 — 화면의 **입력 칸**이 이것 때문에 뜬다
  SAMPLE_EXTRA_MODULES                        // 회귀 시험만 쓴다. 평소엔 비어 있다
#if VIRTUAL_MODULES
  // ⚠ **시험용 가상 차단봉.** 기본값은 꺼져 있다 — `VIRTUAL_MODULES` 를 1 로 켤 때만 실린다.
  //   회귀 시험이 이것으로 **명령 수신 콜백 경로를 실제로 밟는다.**
  {"E1", "OB", PIN_NONE},   // 입구 차단봉 (가상)
  {"X1", "OB", PIN_NONE},   // 출구 차단봉 (가상)
#endif
};
static const uint8_t MODULE_N = (uint8_t)(sizeof(MODULE_TABLE) / sizeof(MODULE_TABLE[0]));
// ██████████████████████████████████████████████████████████████████████████
// █  ⚠  아래부터는 **고쳐도 된다. 다만 비용이 있다**                        █
// ██████████████████████████████████████████████████████████████████████████
//
// 🔴 **금지가 아니다.** 아래(와 🔒 헤더들)는 **링크 안정성이 걸린 코드**다 —
//   ESP 가 꼬였을 때 자력으로 돌아오는 경로가 여기서 나온다.
//   고치면 **안정성 기준선을 다시 재야 한다.** 기준선 수치는 `GUIDE.md` §5.
// 🔑 **"만지지 마라"가 아니라 "만지면 그 비용을 안다"이다.** 고쳤으면 **알려라.**
// ██████████████████████████████████████████████████████████████████████████
#include "Modules.h"           // 모듈 표를 쓰는 코드 · 명령 라우터
#include "FrameCodec2.h"       // ACK 발행·슬롯 배치
#include "Commands.h"          // R / C / G / T 프레임 처리
#include "Session.h"           // 주기 처리(statusTick·cntTick)
// ─────────────────────────────────────────────────────────────────────────
// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **명령 수신 핸들러 — 자기 액추에이터를 여기 붙인다**                █
// ██████████████████████████████████████████████████████████████████████████
//   모양 :  `bool 이름(uint32_t arg)`   — **true = 성공**(ACK `result=0`) · false = 수행 불가(3)
//   등록 :  아래 `setup()` 에서 `router.on("모듈이름", 핸들러);`
//   🔑 **이름으로 등록한다** — 전선은 `idx`(표 순서)로 오는데 이름은 표를 고쳐도 안 밀린다.
//
// 🔴 **`arg` 의 뜻은 *네가* 정한다.** 서버도 프로토콜도 모른다 — 32비트 정수를 나를 뿐이다.
//   **같은 표를 서버 호출부에도 적어라.** 어긋나면 **조용히 다른 동작을 한다**(오류도 안 뜬다).
// 🔴 **거절할 때도 한 줄 남겨라.** 안 남기면 "거절했다"와 "안 불렸다"가 같은 모양이다.
//   판별자: *"내 콜백이 `false` 를 낼 수 있는 모든 자리에 로그가 있는가."*
// ⚠ 등록 안 한 모듈에 명령이 오면 `result=3` — 조용히 성공하지 않는다.
//
//
// 📖 세 가지 꼴의 본보기 · 에코 규약과 한계 · 묶음 하행 → `GUIDE.md` §3

// ═══════════════════════════════════════════════════════════════════════════
// 🔓 **샘플 — 명령 왕복 세 꼴.** 서버 쪽 `srv.send(devid, 모듈이름, 값)` 과 짝이다.
//   셋 다 **같은 통로**다. 다른 것은 **네가 정한 값의 뜻**뿐이다.
// ═══════════════════════════════════════════════════════════════════════════

// ── ② 7자리 숫자를 받는 표시기 ─────────────────────────────────────────────
//   [L2 명령표]  값 = 표시할 수 (0 ~ 9999999)
//   서버:  srv.send("P1", "L2", 1234567);   ← 화면의 **숫자 입력 칸**이 이것을 보낸다
//   🔑 **아래 주석 블록의 예시를 그대로 켠 것이다.** 실물 LCD 를 달면 `Serial.print` 자리에
//     네 라이브러리를 넣으면 된다 — 그 한 줄만 바뀐다.
static bool cmdL2(uint32_t arg) {
  if (arg > 9999999UL) {                 // 🔴 표시할 수 없는 값은 **거절한다**
#if DEBUG
    // 🔴 **거절도 남긴다.** 이 줄이 없으면 "안 불렸다"와 구분이 안 된다.
    Serial.print(F("[L2] 거절 — 7자리 초과: ")); Serial.println(arg);
#endif
    return false;
  }
#if DEBUG
  Serial.print(F("[L2] ")); Serial.println(arg);   // 실물 LCD 라이브러리는 여기에 붙인다
#endif
  return true;
}

// ── ① on/off ───────────────────────────────────────────────────────────────
//   [LD 명령표]  0 = 끔 · 그 외 = 켬
//   서버:  srv.send("P1", "LD", 1);
static bool cmdLed(uint32_t arg) {
  digitalWrite(PIN_SAMPLE_LED, arg ? HIGH : LOW);
  // 🔑 **거절 로그가 없는 이유**: 이 핸들러는 `false` 를 낼 자리가 없다.
  //   모든 값에 뜻이 있다(0=끔 · 그 외=켬). 거절이 없으면 남길 것도 없다.
  return true;
}

// ══ 🔓 **다른 모듈을 붙이려면 — 아래 꼴로 쓴다** ═══════════════════════════
//   📖 자세한 것은 `GUIDE.md` §3·§4
//
// 🔴 **화면에 뜰 이름은 장치가 안 정한다.** 서버의 `lot.label(...)` 이 정한다:
//     lot.label("P1", "A1", "왼쪽 센서");     lot.label("A1", "1번 자리");
//   🔑 그래서 `kind` 에 "주차확인센서" 같은 뜻을 담지 마라 — **정본이 둘이 된다.**
//     `kind` 는 **첫 글자**(`I`/`O`)만 서버가 본다. 나머지는 네 메모다.
//
// ── 센서 하나 더 (모듈 표 한 줄 + setup 한 줄) ─────────────────────────────
//   표 :  {"A2", "IP", 3},               ← 이름 2글자 · `I`=관측 · 자기 핀
//   훅 :  static bool readA2(uint8_t pin) { return digitalRead(pin) == LOW; }
//   등록:  sensors.on("A2", readA2);            ← setup() 에서
//   ⚠ 훅을 안 붙이면 `digitalRead(핀)` 이 기본이다. **켜짐/꺼짐 센서는 그대로 된다**
//
// ── 7자리 숫자를 받는 표시기 ───────────────────────────────────────────────
//   표 :  {"LC", "OL", PIN_NONE},        ← `O`=명령 받음
//   static bool cmdLcd(uint32_t arg) {
//     if (arg > 9999999UL) { Serial.print(F("[LC] 거절: ")); Serial.println(arg); return false; }
//     lcdPrint(arg); return true;               // 네 LCD 라이브러리를 여기에
//   }
//   ⚠ **거절할 때도 한 줄 남겨라** — 안 남기면 "안 불렸다"와 구분이 안 된다
//
// ── 동작 명령 (열기/닫기 같은 것) ──────────────────────────────────────────
//   표 :  {"DR", "OB", 6},              ← `O`=명령 받음
//   // [DR 명령표] 1=열기 2=닫기   ← 🔴 **같은 표를 서버 호출부에도 적어라**
//   static bool cmdDoor(uint32_t arg) {
//     router.echoIs(arg == 1);                  // 🔴 켜진 상태가 0 이 아닌 값이면 **반드시** 부른다
//     switch (arg) { case 1: open(); break; case 2: close(); break; default: return false; }
//     return true;
//   }
//   ⚠ `echoIs` 를 안 부르면 기본이 `arg != 0` 이라 **닫아도 서버는 열린 줄 안다**
// ═════════════════════════════════════════════════════════════════════════

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

  // 🔓 **센서 훅 등록** — 안 붙이면 `digitalRead(핀)` 이 기본이다.
  //   sensors.on("A1", readA1);       ← 자기 센서를 붙일 때. 위 주석 블록 참조

  // 🔓 **명령 수신 등록 — 자기 액추에이터를 여기 붙인다**
  //   예)  `router.on("G1", myGate);`   ← 표에 `{"G1", "OB", 7}` 을 더한 뒤
  //   ⚠ 등록 안 한 모듈에 명령이 오면 `result=3`(수행 불가)로 답한다. 조용히 성공하지 않는다.
  pinMode(PIN_SAMPLE_LED,  OUTPUT);
  router.on("LD", cmdLed);      // 🔓 명령 등록. 모듈마다 한 줄
  router.on("L2", cmdL2);       // 🔓 숫자 표시기
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
  // 🔴 **이 보드가 어느 서버를 보는가** — 포트 세트가 둘이라 자주 물어지는 것이다.
  //   ⚠ 소스를 읽어 짐작하지 마라. **빌드 시점에 덮일 수 있다.** 이 줄이 칩의 진실이다.
  Serial.print(F("\n[NET] 대상 " SERVER_IP ":" SERVER_PORT));
  Serial.println();

  // 🔴 **내 센서가 실제로 읽히는가** — 이 줄이 그 답이다.
  //   ⚠ 시뮬이면 `sensors.on()` 훅도 안 불린다(실물 경로 안에 있다). 그 사실이 여기 보인다.
  Serial.print(F("\n[SENS] "));
  for (uint8_t i = 0; i < SENSOR_N; i++) {
    char nm[4]; moduleNameOf(i, nm);
    Serial.print(nm); Serial.print('=');
    if (!(node.srcReal & ((uint16_t)1 << i)))  Serial.print(F("안읽음"));
    else if (sensors.at(i))                    Serial.print(F("훅"));
    else { Serial.print(F("핀")); Serial.print(slotPin(i)); }
    Serial.print(' ');
  }
  Serial.println();

  Serial.print(F("\n[PARKING NODE] proto v1 / "));
  Serial.print(SPOT_N);         Serial.print(F(" spots / "));
  Serial.print(SENSOR_N);            Serial.print(F(" sensors / "));
  Serial.print(moduleCount());     Serial.println(F(" modules / dev=" DEVICE_ID));

  // ── 부팅 원인 — **추측을 사실로 바꾸는 한 줄**. 왜 재부팅했는지는 여기서만 알 수 있다 ──
  Serial.print(F("[BOOT] 리셋 원인: "));
  if (mcusrMirror == 0) {
    // 🔴 이 보드의 부트로더(optiboot 4.4)는 MCUSR 을 지우고 넘어온다.
    //   `.init3` 에서 가장 먼저 읽어도 이미 0 이다 — **알 수 있는 방법이 없다.**
    //   ⚠ "불명"이라고만 쓰면 가끔은 알 수 있을 것처럼 읽혀 다음 사람이 기다린다.
    Serial.println(F("알 수 없음 — 이 부트로더가 MCUSR 을 지우고 넘어온다"));
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
