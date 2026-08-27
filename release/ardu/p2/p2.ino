 /*  
 * P2 - IR 센서 3개 조절(주차자리 4, 주차자리 5, 출구센서)
 * D2 = IR_4, D3 = IR_5, D4 = IR_EXIT_FRONT, D5 = IR_EXIT_REAR
 *
 * DEVICE_ID · 망 설정(Config.h) · 명령/센서 핸들러 · setup() 의 등록 한 줄
 * 보드 : Arduino Uno (`arduino:avr:uno`) · ESP-01 AT 펌웨어
 *        ESP 링크(SoftSerialBig) **9600bps** — `EspLink_ladder.h` 의 `wifi.begin(9600)`
 *        ⚠ `Serial.begin(115200)` 은 **USB 시리얼(진단 출력)** 이다. 둘은 다른 선이다
 * 배선 : **D8 ← ESP TX** (Uno 가 듣는다) / **D7 → ESP RX** (Uno 가 말한다)
 *        🔴 정본은 `Config.h` 의 `PIN_ESP_RX = 8` · `PIN_ESP_TX = 7` 이다. 여기가 아니다
 */

#include "SoftSerialBig.h"   // 🔴 코어 SoftwareSerial 대체 — 수신 링 64B → **128B**
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include "SensorFilters.h"
#include "LcdI2C.h"        // 🔓 I2C LCD 자작 드라이버(Wire 를 안 쓴다)

#ifndef DEBUG
#define DEBUG 1
#endif
#define DEVICE_ID "P2"
#define ULTRASONIC_TRACE 0
static_assert(sizeof(DEVICE_ID) > 1 && sizeof(DEVICE_ID) <= 9,
              "DEVICE_ID must be 1..8 chars");

// Parking IR sensor
#define IR_4 2
#define IR_5 3

#define IR_EXIT_FRONT 4
#define IR_EXIT_REAR 5

// ═══════════════════════════════════════════════════════════════════════════
// 🔓 **LCD I2C 주소** — 화면이 안 뜨면 여기부터 본다. 이 한 줄이다.
//
//   **0x27** — 이 보드들은 백팩 **점퍼를 모두 제거**했다(사용자 확정).
//   점퍼가 다 열리면 PCF8574 의 기본값이 0x27 이다. 그래서 이 값이다.
//
// 🔴 **이 값은 코드의 성질이 아니라 *기판의 성질* 이다.**
//   ⚠ **하루 사이에도 바뀔 수 있다** — 이 보드는 `0x27` → `0x21` → `0x27` 로 바뀐 적이 있다.
//   ★ 그러니 이 줄을 "확정값" 으로 읽지 마라 — **기판을 만지면 이 줄이 낡는다.**
//     그리고 낡았는지는 **코드를 봐서는 모른다.**
//
// 🔴 **"0x27 아니면 0x3F" 는 틀린 통념이다.** 주소는 A0·A1·A2 점퍼가 정하고 **여덟 자리**다:
//       PCF8574   →  0x20 | A2A1A0  →  **0x20 ~ 0x27**
//       PCF8574A  →  0x38 | A2A1A0  →  **0x38 ~ 0x3F**
//   🔑 모르면 추측하지 말고 **스캔해라** — 0x08~0x77 에 START+주소를 걸어 ACK 오는 데가 답이다.
//   ⚠ **다섯 대가 같은 주소라는 보장은 사람의 기억뿐이다.** 보드마다 확인해라.
//
// ⚠ 주소가 틀리면 **조용하다**: 백팩이 응답을 안 하니 화면만 비고, `cmdLcd` 는 표시 실패로
//   거절하지 않으므로 **서버에는 계속 성공(result=0)으로 보인다.**
//   ★ 전선 ✅ 서버 ✅ 핸들러 ✅ 인데 **화면만 ❌** 이면 — 주소부터 의심해라.
// ═══════════════════════════════════════════════════════════════════════════
#define LCD_ADDR 0x27
#define LCD_NAME "C1"
static_assert(sizeof(LCD_NAME)    == 3, "LCD_NAME 은 정확히 2글자여야 한다 (전선 표가 char name[2])");

#define LCD_CODE_WELCOME   0UL
#define LCD_CODE_NONHUMAN  1UL
#define LCD_CODE_SHOOTING  2UL
#define LCD_PLATE_MAX      9999999UL      // 7자리 — 서버 `lot.control(...).number(0, 9999999)` 과 한 벌
#define LCD_W              8              // "ddd dddd" · "welcome " · "NonHuman" · "shooting"

static char    lcdWant[LCD_W + 1];   // 그리려는 8글자
static uint8_t lcdStep = 0xFF;       // 0xFF = 할 일 없음 · 0 = 커서 · 1..8 = 글자

static void lcdFormatPlate(uint32_t v, char* b) {
  uint32_t n = v;
  for (int8_t i = LCD_W - 1; i >= 0; i--) {
    if (i == 3) { b[i] = ' '; continue; }        // `ddd dddd` 의 빈칸
    b[i] = (char)('0' + (uint8_t)(n % 10UL));
    n /= 10UL;
  }
}

static bool readIR4() {
  static DigitalDebounceState s = {false, false, false, 0};
  return readDebouncedActiveLow(IR_4, s);
  //input_pullup 이라 찼으면 LOW 반환
}
static bool readIR5() {
  static DigitalDebounceState s = {false, false, false, 0};
  return readDebouncedActiveLow(IR_5, s); //static bool 반환
}

static bool readIRExitFront() {
  static DigitalDebounceState s = {false, false, false, 0};
  return readDebouncedActiveLow(IR_EXIT_FRONT, s);
}

static bool readIRExitRear() {
  static DigitalDebounceState s = {false, false, false, 0};
  return readDebouncedActiveLow(IR_EXIT_REAR, s);
}

// static bool readExitVehicle() {
//   static DigitalDebounceState frontFilter = {false, false, false, 0};
//   static DigitalDebounceState rearFilter = {false, false, false, 0};
//   enum PassagePhase {WAIT_FRONT, WAIT_REAR, WAIT_CLEAR};   // 다음 차량 대기, 앞 센서 통과 후 뒤 센서 대기, 차량 통과 후 두 센서 해제 대기
//   static PassagePhase phase = WAIT_FRONT;
//   const bool frontDetected = readDebouncedActiveLow(IR_EXIT_FRONT, frontFilter);
//   const bool rearDetected = readDebouncedActiveLow(IR_EXIT_REAR, rearFilter);
//   switch(phase){
//     case WAIT_FRONT:
//       if(frontDetected){    // 차량 접근이 서버로 전달되면
//         phase = WAIT_REAR;
//       }
//       break;
//     case WAIT_REAR:
//       if(rearDetected){
//         phase = WAIT_CLEAR;
//       }
//       break;
//     case WAIT_CLEAR:
//       if(!frontDetected && !rearDetected){
//         phase = WAIT_FRONT;
//       }
//       break;
//   }
//   return phase == WAIT_REAR; // WAIT_REAR 상태에서만 서버에게 열렸다고 전달.
// }

static bool cmdLcd(uint32_t arg) {
  if (arg > LCD_PLATE_MAX) {
#if DEBUG
    Serial.print(F("[LCD] 거절 — 7자리 초과: ")); Serial.println(arg);
#endif
    return false;                                 // 🔴 표시할 수 없는 값은 거절한다(result=3)
  }
  if      (arg == LCD_CODE_WELCOME)  memcpy(lcdWant, "welcome ", LCD_W);
  else if (arg == LCD_CODE_NONHUMAN) memcpy(lcdWant, "NonHuman", LCD_W);
  else if (arg == LCD_CODE_SHOOTING) memcpy(lcdWant, "shooting", LCD_W);
  else                               lcdFormatPlate(arg, lcdWant);
  lcdWant[LCD_W] = '\0';
  lcdStep = 0;                                    // 펌프가 집어간다
  // ⚠ 화면이 죽어 있어도 **거절하지 않는다** — 표시 실패는 노드 가용성 문제가 아니다.
  //   🔑 실패는 `lcdFail` 로 남는다(`LcdI2C.h`). 거절로 바꾸면 서버가 명령을 재시도해 전선만 먹는다.
  return true;
}

static void lcdPump(void) {
  if (lcdStep == 0xFF) return;
  if (lcdStep == 0) lcdSetCursor(0, 0);
  else              lcdChar((uint8_t)lcdWant[lcdStep - 1]);
  if (++lcdStep > LCD_W) lcdStep = 0xFF;
}

// ═════════════════════════════════════════════════════════════════════════
// 🔑 **`lcdFail` 이 오르는 순간만 찍는다.** 주기 출력이 아니다 — 이유가 있다.
//
// 🔴 `[CNT]` 에 칸을 붙이지 않았다: 그 줄은 이미 약 142B 인데 하드웨어 UART TX 링은
//   **64B** 다. 넘치는 만큼 **블로킹**하고 그동안 `espRead()` 가 안 돌아 **하행 바이트가
//   사라진다.** 실제로 그것이 손실원이었다(`Counters.h` 의 주석 · 창 B/C/D 정합).
//   ★ 진단을 늘리려다 **관측 대상을 깨뜨리는** 전형이다. 그래서 칸을 안 늘렸다.
//
// 🔑 그리고 `fail=0` **하나로는 "실패 안 했다" 와 "안 쟀다" 가 같은 모양**이다.
//   분모는 **부팅 줄**이 준다(`[LCD] init=? fail=0`) — 그것이 "재고 있다" 의 기준선이고,
//   여기서는 **그 기준선에서 움직인 순간**만 알린다. 평상시 바이트는 **0** 이라 창을 안 먹는다.
// ═════════════════════════════════════════════════════════════════════════
static void lcdFailTick(void) {
#if DEBUG
  static uint16_t lcdFailSeen = 0;
  if (lcdFail == lcdFailSeen) return;
  lcdFailSeen = lcdFail;
  // 짧게 유지해라 — 이 줄이 64B 를 넘으면 위에 적은 그 손실이 여기서 다시 난다
  Serial.print(F("[LCD] fail=")); Serial.println(lcdFail);
#endif
}

// Framework include order must remain unchanged.
#include "Module.h"
#include "Boot.h"
#include "Config.h"
#include "RxBuf.h"
#include "Diag.h"
#include "TxState.h"
#include "Checksum.h"
#include "RxLine.h"
#include "LinkGate.h"
#include "Counters.h"
#include "LinkRecovery.h"
#include "FrameCodec.h"
#include "Modules.h"
#include "FrameCodec2.h"
#include "Commands.h"
#include "Session.h"
#include "Runtime.h"

void setup() {
  Serial.begin(115200);
  node.begin();
  // 🔓 **I2C LCD** — 자작 드라이버(`LcdI2C.h`). 주소·핀배치·행수는 그 파일의 `#define` 이다.
  //   🔴 **모듈로 등록하지 않는다.** 서버 조립표(`lot.cpp` 의 `MODULE_HOME[]`)에 `LC` 가 없어서
  //     등록하면 자리에 안 붙는 모듈이 된다. 지금은 **이 보드가 아는 값만 로컬로 표시**한다.
  //   ⚠ 실패해도 노드는 계속 돈다 — `LcdI2C.h` 의 TWI 상한이 그것을 보장한다.
  //     ★ LCD 를 못 쓰는 것과 노드가 죽는 것은 급이 다르다.
  //   🔑 LCD 를 안 다는 보드는 **이 블록과 위 include 한 줄만 지우면** 비용이 0 이 된다.
  const bool lcdOk = lcdInit();
  if (lcdOk) { lcdSetCursor(0, 0); lcdPrintStr(DEVICE_ID); }
#if DEBUG
  // 🔑 `init` 과 `fail` 을 **같이** 찍는다: 화면이 안 나올 때
  //   fail 이 오르면 **배선/주소**, 안 오르는데 안 나오면 **대비/전원**이다. 둘은 다른 원인이다.
  Serial.print(F("[LCD] init=")); Serial.print(lcdOk ? 1 : 0);
  Serial.print(F(" fail="));      Serial.println(lcdFail);
#endif

  // Active-low IR module. If the physical sensor requires an internal pull-up,
  // change INPUT to INPUT_PULLUP after checking its output circuit.
  // 🔴 **자리 센서도 `INPUT_PULLUP` 이다** (입·출구와 같은 근거).
  //   능동 LOW 센서라 선이 빠지면 핀이 떠서 **없는 차를 본다.** 풀업이 있으면 끊긴 자리의
  //   기본값이 HIGH(=미검출) 라 **고장이 '차가 있다' 가 아니라 '차가 없다' 로 나타난다.**
  //   ⚠ 모듈 출력이 푸시풀이면 이 풀업은 **아무 영향이 없다**(약한 풀업이 구동을 못 이긴다).
  //     즉 손해가 없고 **끊겼을 때만** 값이 달라진다. 컴파일 바이트 변화 0.
  pinMode(IR_4, INPUT_PULLUP);  pinMode(IR_5, INPUT_PULLUP);
  // ═══════════════════════════════════════════════════════════════════════
  // 🔴 **출구 센서만 `INPUT_PULLUP` 이다** — 자리 센서(A*)는 `INPUT` 그대로 둔다.
  //   왜 : 이 IR 모듈은 **능동 LOW**(가리면 LOW)다. 핀이 `INPUT` 인데 선이 빠지거나
  //     모듈 출력이 오픈컬렉터면 핀이 **뜬다(floating)** → 잡음을 읽어 **없는 차를 본다.**
  //   🔑 풀업을 켜면 **연결이 끊긴 자리의 기본값이 HIGH(=미검출)** 가 된다.
  //     고장이 '차가 계속 있다' 가 아니라 '차가 없다' 로 나타난다 — **조용히 틀린 자료를 안 만든다.**
  //   ⚠ 모듈 출력이 푸시풀이면 이 풀업은 **아무 영향이 없다**(약한 풀업이 구동을 못 이긴다).
  //     즉 이 변경은 **손해가 없고, 끊겼을 때만 값이 달라진다.**
  // ⚠ 자리 센서를 같이 안 바꾼 이유 : 지금 **실기 중이고 자리 판정은 돌고 있다.**
  //   한 번에 축을 하나만 움직인다. 필요해지면 그때 같은 근거로 바꾼다.
  // ═══════════════════════════════════════════════════════════════════════
  pinMode(IR_EXIT_FRONT, INPUT_PULLUP);   pinMode(IR_EXIT_REAR, INPUT_PULLUP);

  // 🔴 **이름이 곧 자리 결속 키다.** 서버 조립표(`서머리/server/lot.cpp`)에 있는 이름과
  //   글자 하나까지 같아야 한다 — 다르면 등록은 되는데 **자리에 안 붙고, 그 실패는 조용하다.**
  node.sensor  ("A4").on(readIR4);
  node.sensor  ("A5").on(readIR5);
  node.sensor  ("XF").on(readIRExitFront);
  node.sensor  ("XR").on(readIRExitRear);
  node.actuator(LCD_NAME).on(cmdLcd);
  // node.sensor("XV").on(readExitVehicle);

#if DEBUG
  Serial.println(F("\n=============================="));
  Serial.println(F("SMART PARKING NODE READY"));
  Serial.print(F("DEVICE ID : "));
  Serial.println(F(DEVICE_ID));
  Serial.println(F("------------------------------"));
  Serial.println(F("[SENSOR]"));
  Serial.println(F("A4 : Parking Slot 4"));
  Serial.println(F("A5 : Parking Slot 5"));
  Serial.println(F("XF : Exit Front Check"));
  Serial.println(F("XR : Exit Rear Check"));
  Serial.println();
  Serial.println(F("[ACTUATOR]"));
  Serial.println(F("LC : LCD"));
  Serial.println(F("=============================="));
#endif
}

void loop() {
  node.tick();
  lcdPump();
  lcdFailTick();
}
