#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Slots.h — 센서 값 · 수동 오버라이드
//   ⚙ 응용 성질 — 잠금 대상이 아니다
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `Config.h` 의 **정해진 자리에서** `#include` 된다.
//   헤더 순서가 바뀌면 선언·초기화 순서가 같이 바뀌어 **산출물이 달라진다.**

// ⚠ **`slot` 이라는 낱말이 이 저장소에서 셋을 가리킨다:**
//     `modPin` · `readSensor` · `sensorIndexOf`   → **모듈/센서 칸.** 인덱스가 전선의 `idx` 다
//     `slotName*` · `slotOverride*`               → **전선의 자리 토큰**(두 글자). 명세 용어다
//     `slotNo` · `slotStart` · `SLOT_MS`          → **1.2초 반송파 슬롯.** 시간이다
//   🔑 셋 다 `slot` 이었을 때는 코드를 읽어도 구분이 안 됐다.

// ─────────────────────────────────────────────────────────────────────────
// 🔴 **센서 수를 세는 상수가 없다.** 표를 훑어 `isSensor(i)` 로 고른다.
//
//   그래서 **센서를 표 앞쪽에 모을 필요가 없다** — 기여자가 `setup()` 에 적은 순서 그대로다.
//   🔑 옛 판은 `for (i = 0; i < SENSOR_N; i++)` 로 돌아서 "센서는 앞쪽 연속" 불변식을
//     요구했다. **그 불변식은 지키는 것이 아니라 그 루프가 만든 짐이었다.**
//   ⚠ `occ` 비트 위치는 **모듈 인덱스 그대로**다(액추에이터도 에코로 자기 비트를 갖는다).
//     그래서 비트가 겹치지 않는다 — 순서가 아니라 **누가 그 비트를 세우는가**가 가른다.
// ─────────────────────────────────────────────────────────────────────────

// 전선 ACK 이 되비추는 자리 토큰 두 글자 — 표의 이름이 그 값이다.
static inline char slotName0(uint8_t i) { return modName0(i); }
static inline char slotName1(uint8_t i) { return modName1(i); }

// 자리 토큰 두 글자 → 인덱스. 없으면 0xFF.
// 🔴 **센서만 찾는다** — 액추에이터 이름으로는 예약(R)·취소(C)를 걸 수 없고, 그것이 옳다.
static uint8_t sensorIndexOf(char c0, char c1) {
  for (uint8_t i = 0; i < MODULE_N; i++)
    if (isSensor(i) && modName0(i) == c0 && modName1(i) == c1) return i;
  return 0xFF;
}

// 센서 극성. INPUT_PULLUP 을 쓰므로 "차량 감지 시 접점이 GND 로 당기는" 형식이 기본이다.
#define SENSOR_ACTIVE_LOW 1

// ═════════════════════════════════════════════════════════════════════════
// 🔓 **센서 읽기 훅** — 기여자가 자기 센서를 붙이는 자리
//
//   모양 :  `bool 이름(uint8_t pin)`   — 반환 **true = 찼다** · false = 비었다
//   등록 :  `node.sensor("A1").pin(2).on(내함수);`
//   🔑 **명령 쪽과 같은 `on` 이다.** 한 번만 배우면 양쪽에 쓴다.
//
// 🔴 **문턱 판정은 핸들러가 한다.** 초음파는 거리(숫자)를 내는데 자리 상태는 참/거짓이다 —
//   *"몇 cm 아래면 찼다고 볼 것인가"* 는 **장치를 단 사람만 안다.**
//   ⚠ 이 자리를 비워 두면 그 판정이 서버나 화면으로 새어 나가 **두 곳에 생긴다.**
//
// ⚠ **핸들러를 등록하면 핀 모드도 네가 잡아라.** `.pin()` 은 `INPUT_PULLUP` 을 거는데
//   초음파처럼 trig(OUTPUT)/echo(INPUT)가 갈린 센서에는 그것이 틀리다 —
//   `.pin()` 을 안 쓰고 핸들러 안에서 잡으면 된다.
//
// ⚠ **이 함수는 매 `loop()` 마다 불린다.** 오래 걸리는 측정을 그대로 넣으면 슬롯이 밀린다 —
//   `pulseIn` 은 최악 타임아웃만큼 **블로킹**한다. **간격을 두고 값을 캐시해라.**
// ═════════════════════════════════════════════════════════════════════════

// 🔴 `ParkingNode` — **상태를 한 캡슐로**
//   ⚠ **생성자를 두지 않는다.** AVR 전역 객체의 생성자는 `main()` 전에 돌고, NSDMI(`= 0`)를 쓰면
//   **`.bss` 영타 대신 생성자 코드가 생긴다.** 전역은 0 으로 시작하므로 아무것도 안 쓰는 것이 싸다.
// ═════════════════════════════════════════════════════════════════════════
class ParkingNode {
 public:
  // 🔴 **시작과 한 박자 — 정의는 `Runtime.h`** 다(이 파일보다 뒤에 온다).
  //   `begin()` 은 `espInit()` 을, `tick()` 은 `espRead()` 를 부르고 그 함수들이 뒤에 정의된다.
  void begin();
  void tick();

  // 🔓 **모듈 등록** — `setup()` 에서 부른다. 정의는 `Runtime.h`.
  //   ⚠ **호출 순서가 전선 `idx` 이고 `occ` 비트 위치다.** 센서·액추에이터를 섞어도 된다.
  ModRef sensor  (const char (&name)[3]);
  ModRef actuator(const char (&name)[3]);

  // 배너를 첫 `tick()` 에서 한 번만 찍기 위한 표시.
  // 🔴 **왜 `begin()` 이 아니라 첫 `tick()` 인가**: 배너의 `[SENS]` 줄이 **기여자가 등록한 것**을
  //   보고 값을 가른다. `begin()` 에서 찍으면 등록 전이라 **거짓말을 한다.**
  bool bannerDone;

  // 센서를 한 번 훑어 점유 비트를 갱신한다
  void readSensors() {
    uint16_t m = 0;
    for (uint8_t i = 0; i < MODULE_N; i++)
      if (isSensor(i) && readSensor(i)) m |= (uint16_t)1 << i;
    occMask = m;
  }

  uint16_t occMask;   // 점유 비트 (센서가 주인)
  uint16_t resMask;   // 예약 비트 (서버가 주인 — R 로 켜고 C 로만 끈다)

  uint8_t readRealSensor(uint8_t i) {
    // 🔓 기여자 핸들러가 등록돼 있으면 **그것이 답이다.** 없으면 아래 기본 경로로 간다.
    SensorFn f = senseOf(i);
    if (f) return f(modPin(i)) ? 1 : 0;
    uint8_t raw = digitalRead(modPin(i));
  #if SENSOR_ACTIVE_LOW
    return (raw == LOW) ? 1 : 0;
  #else
    return (raw == HIGH) ? 1 : 0;
  #endif
  }

  // ─────────────────────────────────────────────────────────────────────────
  // 수동 오버라이드 — 칸별. 실제 센서값보다 **우선**한다.
  //   차 없이 화면·서버를 시험할 수단이고, 전선의 `T` 프레임이 이 함수들을 부른다.
  // ─────────────────────────────────────────────────────────────────────────
  uint16_t ovrActive;   // 비트 i = 이 칸에 값을 강제하고 있다  ( = S 프레임의 tmask)
  uint16_t ovrValue;    // 비트 i = 강제할 값 (ovrActive 가 1 일 때만 의미 있다)

  // 테스트 모드 무장 여부. 무장 중일 때만 칸별 주입(`T,...,S,..`)이 먹는다.
  // **재부팅하면 해제 상태로 시작한다** — 가짜 값이 되살아나는 것이 더 위험하다.
  bool testArmed;

  void slotOverrideSet(uint8_t i, uint8_t value) {
    if (!isSensor(i)) return;
    uint16_t bit = (uint16_t)1 << i;
    ovrActive |= bit;
    if (value) ovrValue |= bit; else ovrValue &= (uint16_t)~bit;
  }

  void slotOverrideClear(uint8_t i) {
    if (!isSensor(i)) return;
    uint16_t bit = (uint16_t)1 << i;
    ovrActive &= (uint16_t)~bit;
    ovrValue  &= (uint16_t)~bit;
  }

  void slotOverrideClearAll(void) {
    ovrActive = 0;
    ovrValue  = 0;
  }

  // ★ 센서값의 단일 진입점 ★
  //   우선순위: 수동 오버라이드 > 실제 센서
  uint8_t readSensor(uint8_t i) {
    // 무장 중일 때만 주입이 적용된다 — 해제 상태에서 주입값이 실리면 **tmask 가 안 붙은 채로**
    // 전선에 나가고 서버·화면이 그것을 실측으로 믿는다. 여기서 막으면 "해제 = 현실로 복귀"가
    // 호출 순서에 기대지 않고 **구조적으로** 성립한다.
    if (testArmed && (ovrActive & ((uint16_t)1 << i))) return (uint8_t)((ovrValue >> i) & 1);

    // 실제 센서는 **무장 중에도 계속 읽는다.** 그건 진실이고 가릴 이유가 없다.
    if (modPin(i) != PIN_NONE || senseOf(i)) return readRealSensor(i);

    // 🔴 핀도 훅도 없는 센서는 **늘 0(비었다)** 이다.
    //   장치는 가짜 점유를 만들지 않는다 — 차 없이 시험할 수단은 위의 오버라이드(`T`)다.
    return 0;
  }

};

static ParkingNode node;
