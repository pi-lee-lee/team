#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Slots.h — 센서 칸 · 수동 오버라이드 · 센서 읽기
//   ⚙ 응용 성질 — 잠금 대상이 아니다
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `Config.h` 의 **정해진 자리에서** `#include` 된다.
//   헤더 순서가 바뀌면 선언·초기화 순서가 같이 바뀌어 **산출물이 달라진다.**

// ⚠ **`slot` 이라는 낱말이 이 저장소에서 셋을 가리킨다. 이름을 갈라 뒀다:**
//     `sensorPin` · `readSensor` · `sensorIndexOf` · `applySensorPinMode`
//         → **센서 칸.** 이 파일이 다루는 것이고 인덱스는 전선의 `idx` 다
//     `slotName0/1` · `slotOverride*`
//         → **전선의 자리 토큰**(두 글자). 명세 용어라 그대로 둔다 — `R`/`C`/`T` 가 쓴다
//     `slotNo` · `slotStart` · `SLOT_MS`  (TxState.h)
//         → **1.2초 반송파 슬롯.** 시간이다. 위 둘과 아무 관계가 없다
//   🔑 셋 다 `slot` 이었을 때는 코드를 읽어도 구분이 안 됐다.

// ─────────────────────────────────────────────────────────────────────────
// 센서 칸 — 🔴 **전부 모듈 표에서 파생된다. 여기에 진실을 새로 만들지 마라**
//
// 표(`client.ino` 의 `MODULE_TABLE`)가 유일한 원천이다:
//     센서 수  = 표 앞쪽에서 `kind[0] == 'I'` 인 연속 줄 수
//     핀       = 그 줄의 `pin`
//     자리 토큰 = 그 줄의 `name` 두 글자   ← 전선 ACK 이 되비추는 값
//
// 🔴 **센서는 표 앞쪽에 연속으로 있어야 한다.** 인덱스가 곧 전선의 `idx` 이고
//   `occMask` 비트 위치이기 때문이다. 중간에 액추에이터를 끼우면 그 뒤 센서가
//   센서 수 밖으로 밀려 **조용히 안 읽힌다.**
//
// ⚠ 장치는 **자리 배치를 모른다.** 어느 모듈이 어느 자리에 붙는지는 서버 조립 표
//   (`lot.cpp` 의 `.module()`)가 정한다. 장치가 아는 것은 자기 모듈 목록뿐이다.
// ─────────────────────────────────────────────────────────────────────────
// 표에서 센서 수를 센다. `constexpr` 재귀인 이유: 이 툴체인은 `-std=gnu++11` 이라
//   `constexpr` 함수 본문에 루프를 못 쓴다(C++14 부터 된다).
static constexpr uint8_t countSensors(uint8_t i = 0) {
  return (i >= MODULE_N || MODULE_TABLE[i].kind[0] != 'I')
       ? i : countSensors((uint8_t)(i + 1));
}
static constexpr uint8_t SENSOR_N = countSensors();
static_assert(SENSOR_N >= 1, "모듈 표에 센서(`I` 로 시작하는 종류)가 하나도 없다");
static_assert(SENSOR_N <= 16, "occMask 가 uint16_t 다 — 센서는 16칸까지다");

// ─────────────────────────────────────────────────────────────────────────
// 핀 — 표의 `pin` 칸이 그대로 답이다
//
// Uno 에서 쓸 수 없는 핀:
//   D0, D1  : USB 시리얼(하드웨어 UART). 물리면 업로드·디버그 출력이 깨진다
//   D7, D8  : SoftwareSerial 로 ESP-01 과 통신 중 (PIN_ESP_RX / PIN_ESP_TX)
//   D13     : 온보드 LED 가 저항+LED 로 GND 쪽으로 약하게 당긴다. INPUT_PULLUP 과
//             분압이 되어 HIGH 가 확실히 안 읽힌다 — "가끔 점유로 읽히는" 최악의 고장이 된다
//   A4, A5  : Uno 의 I2C(SDA/SCL). 지금 안 쓰더라도 채우면 나중에 I2C 장치를 붙일 길이 막힌다
// 남는 디지털 핀은 D2~D6, D9~D12 로 아홉 개뿐이다. 더 필요하면 아날로그를 디지털로 쓴다
//   (A0~A5 는 디지털 입출력으로 그대로 쓸 수 있다. A0 == 14).
// ⚠ 난수 시드를 A1 에서 뽑는다(`setup()`) — 어디에도 안 물린 핀이어야 노이즈가 나온다.
//   그래서 **핀 A1 은 비워 둔다.** (핀 `A1` 과 모듈 이름 `A1` 은 아무 상관이 없다.)
// ─────────────────────────────────────────────────────────────────────────
static inline uint8_t sensorPin(uint8_t i) {
  if (i >= SENSOR_N) return PIN_NONE;              // 센서가 아닌 칸에는 핀이 없다
  return pgm_read_byte(&MODULE_TABLE[i].pin);
}

// 전선 ACK 이 되비추는 자리 토큰 두 글자 — 표의 이름이 그 값이다.
static inline char slotName0(uint8_t i) { return (char)pgm_read_byte(&MODULE_TABLE[i].name[0]); }
static inline char slotName1(uint8_t i) { return (char)pgm_read_byte(&MODULE_TABLE[i].name[1]); }

// 자리 토큰 두 글자 → 센서 인덱스. 없으면 0xFF.
// 🔴 **센서만 찾는다** — 루프가 `SENSOR_N` 까지만 돈다. 액추에이터 이름(`LD` 등)으로는
//   예약(R)·취소(C)를 걸 수 없고, 그것이 옳다.
static uint8_t sensorIndexOf(char c0, char c1) {
  for (uint8_t i = 0; i < SENSOR_N; i++)
    if (slotName0(i) == c0 && slotName1(i) == c1) return i;
  return 0xFF;
}

// 센서 극성. INPUT_PULLUP 을 쓰므로 "차량 감지 시 접점이 GND 로 당기는" 형식이 기본이다.
// 반대 극성 센서(감지 시 HIGH)면 이 값만 0 으로 바꾼다.
#define SENSOR_ACTIVE_LOW 1

// ═════════════════════════════════════════════════════════════════════════
// 🔓 **센서 읽기 훅** — 기여자가 자기 센서를 붙이는 자리
//
//   모양 :  `bool 이름(uint8_t pin)`   — 반환 **true = 찼다** · false = 비었다
//   등록 :  `client.ino` 의 `setup()` 에서 `sensors.on("모듈이름", 핸들러);`
//   🔑 **명령 쪽 `router.on` 과 같은 모양이다.** 한 번만 배우면 양쪽에 쓴다.
//
// 🔴 **문턱 판정은 핸들러가 한다.** 초음파는 거리(숫자)를 내는데 자리 상태는 참/거짓이다 —
//   *"몇 cm 아래면 찼다고 볼 것인가"* 는 **장치를 단 사람만 안다.** 그래서 여기서 정한다.
//   ⚠ 이 자리를 비워 두면 그 판정이 서버나 화면으로 새어 나가 **두 곳에 생긴다.**
//
// ⚠ **핸들러를 등록하면 `pinMode` 도 네가 잡아라.** 기본 경로는 `INPUT_PULLUP` 을 거는데
//   초음파처럼 trig(OUTPUT)/echo(INPUT)가 갈린 센서에는 그것이 틀리기 때문이다.
//   등록된 칸은 `applySensorPinMode()` 가 **손대지 않는다.**
//
// ⚠ **이 함수는 매 `loop()` 마다 불린다.** 오래 걸리는 측정을 그대로 넣으면 슬롯이 밀린다 —
//   `pulseIn` 은 최악 타임아웃만큼 **블로킹**한다. **간격을 두고 값을 캐시해라.**
// ═════════════════════════════════════════════════════════════════════════
// `SensorFn` typedef 는 모듈 표 앞(`client.ino`)에 있다 — 표가 그 타입을 쓰기 때문이다.
// 🔴 선언만 여기 있고 **정의는 `Modules.h`** 다 — 등록표가 이름 검색을 쓰는데
//   그 코드가 이 파일보다 뒤에 온다.
static SensorFn sensorFnOf(uint8_t idx);

// 🔴 `ParkingNode` — **상태를 한 캡슐로**
//   위의 것들은 **일부러 밖에 뒀다**: `SENSOR_N`·`sensorPin`·`slotName*`·`sensorIndexOf` 는
//   **상태를 안 만지는 순수·표 함수**다. 클래스에 넣으면 `this` 를 얻는 대신 아무것도 안 준다.
//   ★ **클래스는 상태를 가진 것만 가져간다.**
//
// ⚠ **생성자를 두지 않는다.** AVR 전역 객체의 생성자는 `main()` 전에 돌고, NSDMI(`= 0`)를 쓰면
//   **`.bss` 영타 대신 생성자 코드가 생긴다.** 전역은 0 으로 시작하므로 아무것도 안 쓰는 것이 싸다.
//   초기화가 필요하면 `begin()` 에 둔다.
// ═════════════════════════════════════════════════════════════════════════
class ParkingNode {
 public:
  // 🔴 **시작과 한 박자 — 정의는 `Runtime.h` 다**(이 파일보다 뒤에 온다).
  //   여기서 본문을 쓸 수 없다: `begin()` 은 `espInit()` 을, `tick()` 은 `espRead()` 를 부르고
  //   그 함수들이 **이 파일 뒤에** 정의되기 때문이다.
  //   ⚠ **생성자가 아니라 `begin()` 인 이유**: 전역 객체의 생성자는 `main()` 전에 돌아
  //     `Serial`·`millis()`·`pinMode` 가 아직 없다.
  void begin();
  void tick();

  // 배너를 첫 `tick()` 에서 한 번만 찍기 위한 표시.
  // 🔴 **왜 `begin()` 이 아니라 첫 `tick()` 인가**: 배너의 `[SENS]` 줄이 **기여자가 등록한 훅**을
  //   보고 `훅`/`핀N` 을 가른다. `begin()` 에서 찍으면 등록 전이라 **거짓말을 한다.**
  //   `tick()` **맨 앞**에 두므로 어떤 AT 로그보다 먼저 나간다 — 부팅 로그 첫 줄이 그대로다.
  bool bannerDone;

  // 센서를 한 번 훑어 점유 비트를 갱신한다
  void readSensors() {
    uint16_t m = 0;
    for (uint8_t i = 0; i < SENSOR_N; i++) if (readSensor(i)) m |= (uint16_t)1 << i;
    occMask = m;
  }

  uint16_t occMask;   // 점유 비트 (센서가 주인)
  uint16_t resMask;   // 예약 비트 (서버가 주인 — R 로 켜고 C 로만 끈다)

  uint8_t readRealSensor(uint8_t i) {
    // 🔓 기여자 핸들러가 등록돼 있으면 **그것이 답이다.** 없으면 아래 기본 경로로 간다.
    SensorFn f = sensorFnOf(i);
    if (f) return f(sensorPin(i)) ? 1 : 0;
    uint8_t raw = digitalRead(sensorPin(i));
  #if SENSOR_ACTIVE_LOW
    return (raw == LOW) ? 1 : 0;
  #else
    return (raw == HIGH) ? 1 : 0;
  #endif
  }

  // 핀이 있는 칸의 입력 모드를 잡는다.
  void applySensorPinMode(uint8_t i) {
    // 🔴 기여자 핸들러가 있는 칸은 **건드리지 않는다.** 그 센서에 맞는 핀 모드는 그쪽이 안다.
    if (sensorFnOf(i)) return;
    if (sensorPin(i) != PIN_NONE) pinMode(sensorPin(i), INPUT_PULLUP);
  }

  // ─────────────────────────────────────────────────────────────────────────
  // 수동 오버라이드 — 칸별. 실제 센서값보다 **우선**한다.
  //   차 없이 화면·서버를 시험할 수단이고, 전선의 `T` 프레임이 이 함수들을 부른다.
  //
  // static 이 아니라 클래스 멤버인 이유: 상태(`ovrActive`·`ovrValue`·`testArmed`)를 가진다.
  // ─────────────────────────────────────────────────────────────────────────
  uint16_t ovrActive;   // 비트 i = 이 칸에 값을 강제하고 있다  ( = S 프레임의 tmask)
  uint16_t ovrValue;    // 비트 i = 강제할 값 (ovrActive 가 1 일 때만 의미 있다)

  // 테스트 모드 무장 여부. 무장 중일 때만 칸별 주입(`T,...,S,..`)이 먹는다.
  // **재부팅하면 해제 상태로 시작한다** — 예약과 달리 서버가 재하달하지 않는다.
  // 가짜 값이 재부팅 뒤 되살아나는 것이 더 위험하기 때문이다.
  bool testArmed;

  void slotOverrideSet(uint8_t i, uint8_t value) {
    if (i >= SENSOR_N) return;
    uint16_t bit = (uint16_t)1 << i;
    ovrActive |= bit;
    if (value) ovrValue |= bit; else ovrValue &= (uint16_t)~bit;
  }

  void slotOverrideClear(uint8_t i) {
    if (i >= SENSOR_N) return;
    uint16_t bit = (uint16_t)1 << i;
    ovrActive &= (uint16_t)~bit;
    ovrValue  &= (uint16_t)~bit;
  }

  void slotOverrideClearAll(void) {
    ovrActive = 0;
    ovrValue  = 0;
  }

  /* ── ★ 센서가 도착했을 때 무엇을 하면 되는가 ★ ────────────────────────────
   * 1. 모듈 표에 그 센서 줄을 적고 **핀 번호를 쓴다.** 핀을 적으면 그 핀을 읽는다 —
   *    따로 켜는 스위치가 없다. 접점은 그 핀과 GND 사이에 문다(INPUT_PULLUP).
   *    극성이 반대인 센서면 위의 `SENSOR_ACTIVE_LOW` 를 0 으로 바꾼다.
   * 2. 값을 직접 계산해야 하는 센서(초음파 등)는 `sensors.on("이름", 핸들러)` 로 훅을 붙인다.
   *    그러면 핀 모드도 그쪽이 잡는다.
   * 3. 굽고 나서 시리얼(115200)의 `[SENS]` 줄과 `[TX] S,...` 의 해당 비트를 본다.
   *    센서를 손으로 가려 보며 0/1 이 따라 바뀌면 배선이 맞은 것이다.
   * 4. 차 없이 상태를 만들어 봐야 하면 화면의 테스트 모드를 쓴다. 전선으로 `T` 가 내려온다:
   *      T,<rid>,A,??,-          무장 (이걸 먼저 해야 주입이 먹는다)
   *      T,<rid>,S,<slot>,<0|1>  그 칸 occupied 를 주입
   *      T,<rid>,X,<slot>,-      그 칸만 원래 값으로
   *      T,<rid>,D,??,-          해제 (전 칸 오버라이드가 한 번에 사라진다)
   *    무장 중에는 S 프레임에 tmask 필드가 붙어 "이 값은 주입된 것"임을 서버·화면에 알린다.
   *    ⚠ 벤치에서 코드로 직접 부를 거면 `slotOverrideSet(i,1)` 만으로는 **아무 일도 안 난다.**
   *      `testArmed` 도 같이 세워야 한다.
   */

  // ★ 센서값의 단일 진입점 ★
  //   우선순위: 수동 오버라이드 > 실제 센서
  uint8_t readSensor(uint8_t i) {
    // 무장 중일 때만 주입이 적용된다 — `testArmed` 를 여기서 한 번 더 본다.
    // 실사용에서는 중복이다(`T,S` 가 무장을 검사하고 `T,D` 가 전 칸을 지운다). 그래도 두는 이유:
    // 해제 상태에서 주입값이 occupied 에 실리면 **tmask 가 안 붙은 채로** 전선에 나가고,
    // 서버·화면은 그 값을 실측으로 믿는다. 여기서 막으면 "해제 = 현실로 복귀"가
    // ClearAll 호출에 기대지 않고 **구조적으로** 성립한다.
    if (testArmed && (ovrActive & ((uint16_t)1 << i))) return (uint8_t)((ovrValue >> i) & 1);

    // 실제 센서는 **무장 중에도 계속 읽는다.** 그건 진실이고 가릴 이유가 없다.
    if (sensorPin(i) != PIN_NONE) return readRealSensor(i);

    // 🔴 핀이 없는 센서 칸은 **늘 0(비었다)** 이다.
    //   장치는 가짜 점유 상태를 만들지 않는다 — 차 없이 시험할 수단은 위의 오버라이드(`T`)다.
    return 0;
  }

};

static ParkingNode node;
