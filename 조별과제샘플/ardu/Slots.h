#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Slots.h — 자리·센서 소스(실물/시뮬)·수동 오버라이드·시뮬 한 걸음   (REQ-0273 리팩터링 · 2026-08-19)
//   원본 `조별과제샘플/ardu/client.ino` 의 **154~396 행을 원문 그대로** 옮긴 것이다.
//   ⚠ 행 번호는 **이 분리 이전 판(2980줄) 기준**이다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라. 순서 보존이 `hex` 차이 0 의 조건이다.**
//   이 파일은 `client.ino` 의 **원래 그 자리에서** `#include` 된다. 다른 자리로 옮기거나
//   헤더들의 순서를 바꾸면 **그 순간 거동 변경이 되고 굽기 축이 하나 는다**(원장 §19·§31).

// ─────────────────────────────────────────────────────────────────────────
// 자리 (§1)
// ─────────────────────────────────────────────────────────────────────────
// 🔴 **센서 칸 수**(자리 수가 아니다). `A_i` 와 `B_i` 는 **같은 자리의 두 센서**다 —
//   그래서 자리 수 = `SENSOR_N / 2` 다. 표(`MODULE_TABLE`)의 센서 줄 수와 **같아야 한다.**
//   ⚠ 늘리려면 `SLOT_PIN[]` 과 `MODULE_TABLE` 을 **같이** 늘려라. 셋이 어긋나면 컴파일이 막는다.
// 🔴 **셋 다 다른 값이다. 이름이 그것을 말하게 해 뒀다:**
//     `SENSOR_N`  = 센서 칸 수   · `SPOT_N` = 자리 수   · `moduleCount()` = 모듈 수
//   ⚠ 그리고 `SLOT_MS`·`slotNo` 의 "slot" 은 **또 다른 뜻**이다 — 1.2초짜리 **반송파 슬롯**이다.
//     한 낱말이 세 가지를 가리키고 있었다. 세는 값 둘만 이름을 갈랐다.
static const uint8_t SENSOR_N = 2;                  // 🔓 센서 칸 수 (지금: A1 + B1)
static const uint8_t SPOT_N   = SENSOR_N / 2;       // 자리 수 — 자리마다 센서가 둘이다
static_assert(SENSOR_N % 2 == 0, "SENSOR_N 은 짝수여야 한다 — 자리마다 센서가 둘이다");

// ✏️ 옛 판은 `5`(행 수)를 **세 곳에 박아** 뒀다. `SPOT_N` 로 묶었다 —
//   안 그러면 칸 수를 바꿀 때 **이름 규칙만 옛 값으로 남아 조용히 어긋난다.**
static inline char slotCol(uint8_t i) { return (i < SPOT_N) ? 'A' : 'B'; }
static inline char slotRow(uint8_t i) { return (char)('1' + (i % SPOT_N)); }

// 자리 문자 2개 → 인덱스. 없으면 0xFF
static uint8_t slotIndexOf(char c0, char c1) {
  if (c1 < '1' || c1 >= (char)('1' + SPOT_N)) return 0xFF;
  if (c0 == 'A') return (uint8_t)(c1 - '1');
  if (c0 == 'B') return (uint8_t)(c1 - '1' + SPOT_N);
  return 0xFF;
}

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
static const uint8_t SLOT_PIN[SENSOR_N] PROGMEM = {
  2,      // 인덱스 0 = A1 (자리 1 의 첫째 센서)
  9       // 인덱스 1 = B1 (자리 1 의 둘째 센서)
  // 🔓 늘리려면 여기에 핀을 더하고 `SENSOR_N` 과 `MODULE_TABLE` 도 같이 늘려라
  //    옛 10칸 배선 : A1~A5 = 2,3,4,5,6 · B1~B5 = 9,10,11,12,A0
};
// 🔴 **범위 가드** (2026-08-19) — `SLOT_PIN[]` 은 크기가 `SENSOR_N`(실물 자리) 인데
//   `moduleCount()` 는 가상 모듈까지 세므로 **그 값으로 루프를 돌면 배열 밖을 읽는다.**
//   ⚠ 지금 모든 호출부가 `SENSOR_N` 까지만 돌지만 **방어가 없으면 다음 사람이 밟는다** —
//     PROGMEM 범위 밖 읽기는 **오류 없이 쓰레기를 돌려준다.**
static inline uint8_t slotPin(uint8_t i) {
  if (i >= SENSOR_N) return PIN_NONE;          // 가상 모듈에는 핀이 없다
  return pgm_read_byte(&SLOT_PIN[i]);
}

// 센서 극성. INPUT_PULLUP 을 쓰므로 "차량 감지 시 접점이 GND 로 당기는" 형식을 기본으로 본다.
// 반대 극성 센서(감지 시 HIGH)면 이 값만 0 으로 바꾸면 된다.
#define SENSOR_ACTIVE_LOW 1

// 칸별 센서 소스. 비트 i 가 1 이면 그 칸은 실물(REAL), 0 이면 시뮬(SIM).
// **실제 설치는 10칸이 한꺼번에 되지 않는다.** 배선이 끝난 칸만 1 로 올리면
// 나머지는 그대로 시뮬로 돈다. 예) A1·A2·B3 만 배선했다면 → 0x0083
//   (A1=bit0, A2=bit1, A3=bit2, A4=bit3, A5=bit4, B1=bit5 … B5=bit9)
// 🔓 **센서를 실물로 읽을 것인가** — 기본은 **읽는다**(모듈 표에 핀을 적었으면 그 뜻이다).
//   🔴 **이 값을 1 로 하면 실물 센서를 안 읽고 시뮬레이터가 자리를 움직인다.**
//     하드웨어가 아직 없는 사람이 화면을 보려고 쓰는 탈출구다.
//   ⚠ 시뮬이면 **`sensors.on()` 으로 등록한 훅도 안 불린다** — 실물 경로 안에 있기 때문이다.
//     부팅 `[SENS]` 줄이 지금 어느 쪽인지 말해 준다.
#ifndef SAMPLE_SIM_SENSORS
#define SAMPLE_SIM_SENSORS 0
#endif
static inline uint8_t simPair(uint8_t i) { return (uint8_t)((i + SENSOR_N / 2) % SENSOR_N); }

// ═════════════════════════════════════════════════════════════════════════
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
//   등록된 칸은 `applySlotPinMode()` 가 **손대지 않는다.**
//
// ⚠ **이 함수는 매 `loop()` 마다 불린다.** 오래 걸리는 측정을 그대로 넣으면 슬롯이 밀린다 —
//   `pulseIn` 은 최악 타임아웃만큼 **블로킹**한다. **간격을 두고 값을 캐시해라**(아래 예시).
// ═════════════════════════════════════════════════════════════════════════
typedef bool (*SensorFn)(uint8_t pin);
// 🔴 선언만 여기 있고 **정의는 `Modules.h`** 다 — 등록표가 `MODULE_N` 을 알아야 하는데
//   그 값은 `client.ino` 의 모듈 표에서 나오고, 그 표는 이 파일보다 **뒤에** 온다.
static SensorFn sensorFnOf(uint8_t idx);

// 🔴 `ParkingNode` — **자리의 상태를 한 캡슐로**
//   위의 것들은 **일부러 밖에 뒀다**: `SENSOR_N`·`slotCol/Row/IndexOf`·`SLOT_PIN`·`slotPin`·`simPair` 는
//   **상태를 안 만지는 순수·표 함수**다. 클래스에 넣으면 `this` 를 얻는 대신 아무것도 안 준다.
//   ★ **클래스는 상태를 가진 것만 가져간다.**
//   ⚠ `SLOT_PIN` 은 `PROGMEM` 이라 멤버로 두면 AVR 에서 다루기 나빠진다 — 그것도 밖에 두는 이유다.
//
// ⚠ **생성자를 두지 않는다.** AVR 전역 객체의 생성자는 `main()` 전에 돌고, NSDMI(`= 0`)를 쓰면
//   **`.bss` 영타 대신 생성자 코드가 생긴다.** 전역은 0 으로 시작하므로 아무것도 안 쓰는 것이 싸다.
//   초기화가 필요하면 `begin()` 에 둔다(§AVR 전역 생성자 함정).
// ═════════════════════════════════════════════════════════════════════════
class ParkingNode {
 public:
  // 🔴 **초기화는 여기다. 생성자가 아니다.**
  //   전역 객체의 생성자는 `main()` 전에 돌아 `Serial`·`millis()` 가 없다(§AVR 함정).
  //   ⚠ `srcReal` 만 0 이 아닐 수 있다(`SLOT_SRC_DEFAULT` 를 바꾸면). 그래서 여기서 넣는다.
  void begin() {
    // 🔴 **소스는 모듈 표에서 파생한다.** 핀을 적었으면 그 핀을 읽겠다는 뜻이다.
    //   ⚠ 예전에는 `SLOT_SRC_DEFAULT` 라는 **두 번째 진실**이 따로 있었고 기본이 `0x0000`
    //     이라 **핀을 적어도 한 번도 안 읽었다.** 값이 두 곳에 있으면 갈린다 — 하나로 모았다.
    srcReal = 0;
#if !SAMPLE_SIM_SENSORS
    for (uint8_t i = 0; i < SENSOR_N; i++)
      if (slotPin(i) != PIN_NONE) srcReal |= (uint16_t)1 << i;
#endif
    for (uint8_t i = 0; i < SENSOR_N; i++) applySlotPinMode(i);
  }

  // 센서를 한 번 훑어 점유 비트를 갱신한다 (옛 `sensorTick`)
  void readSensors() {
    uint16_t m = 0;
    for (uint8_t i = 0; i < SENSOR_N; i++) if (readSlotSensor(i)) m |= (uint16_t)1 << i;
    occMask = m;
  }

  uint16_t occMask;   // 점유 비트 (센서가 주인 — §7.4)
  uint16_t resMask;   // 예약 비트 (서버가 주인 — §7.4. R 로 켜고 C 로만 끈다)

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
  uint16_t simOcc;


  uint16_t srcReal;

  uint8_t readRealSensor(uint8_t i) {
    // 🔓 기여자 핸들러가 등록돼 있으면 **그것이 답이다.** 없으면 아래 기본 경로로 간다.
    SensorFn f = sensorFnOf(i);
    if (f) return f(slotPin(i)) ? 1 : 0;
    uint8_t raw = digitalRead(slotPin(i));
  #if SENSOR_ACTIVE_LOW
    return (raw == LOW) ? 1 : 0;
  #else
    return (raw == HIGH) ? 1 : 0;
  #endif
  }

  // 그 칸을 실물로 돌리기로 했으면 입력 모드를 잡아 준다. setup() 과 소스 변경 시에 부른다.
  void applySlotPinMode(uint8_t i) {
    // 🔴 기여자 핸들러가 있는 칸은 **건드리지 않는다.** 그 센서에 맞는 핀 모드는 그쪽이 안다.
    if (sensorFnOf(i)) return;
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
  uint16_t ovrActive;   // 비트 i = 이 칸에 값을 강제하고 있다  ( = S 프레임의 tmask)
  uint16_t ovrValue;   // 비트 i = 강제할 값 (ovrActive 가 1 일 때만 의미 있다)

  // 테스트 모드 무장 여부 (§12A.2). 무장 중일 때만 칸별 주입(T,...,S,..)이 먹는다.
  // **재부팅하면 해제 상태로 시작한다** — 예약(§7.4)과 정반대로 서버가 재하달하지 않는다(§12A.3).
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

  // 칸별 소스 전환. REAL 로 바꿀 때 입력 모드까지 같이 잡는다.
  void slotSourceSet(uint8_t i, uint8_t useReal) {
    if (i >= SENSOR_N) return;
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
  bool simCandidate(uint8_t i) {
    uint16_t bit = (uint16_t)1 << i;
    if (srcReal & bit) return false;                       // (a) 실물 칸
    if (testArmed && (ovrActive & bit)) return false;      // (b) 오버라이드가 가리는 칸
    return true;
  }

  // 🔴 **짝 센서 인덱스** (REQ-0270 · 지형 확정 2026-08-19)
  //   `A_i` 와 `B_i` 는 **같은 자리의 두 센서**다(이중화). A1..A5 = bit0..4 · B1..B5 = bit5..9.
  //   ⚠ **한쪽만 움직이면 서버가 한 자리에서 모순된 두 값을 본다** — `센서갈림` 이 그 표지다.
  //   그래서 시뮬은 **항상 짝을 함께** 바꾼다. 실물 센서가 오면 그쪽은 각자 값을 내므로
  //   이 규칙은 **시뮬에만** 적용된다(`simCandidate` 로 걸러진다).

  // 바뀐 칸 인덱스를 돌려준다. 바꿀 칸이 없으면 0xFF.
  //   ⚠ 반환값은 **대표 칸 하나**다. 짝도 같이 바뀌었다는 것은 이 함수만 안다 —
  //     호출부는 "무엇이 바뀌었나"를 `occMask` 전체로 다시 읽으므로 문제가 없다.
  uint8_t simStep(void) {
    // 1순위: 예약됐지만 비어 있는 시뮬 칸을 채운다
    for (uint8_t i = 0; i < SENSOR_N; i++) {
      uint16_t bit = (uint16_t)1 << i;
      if (!simCandidate(i)) continue;
      if ((resMask & bit) && !(simOcc & bit)) {
        simOcc |= bit;
        const uint8_t j = simPair(i);                      // 🔴 짝을 함께 채운다(REQ-0270)
        if (simCandidate(j)) simOcc |= (uint16_t)1 << j;
        return i;
      }
    }
    // 2순위: 시뮬 칸 중 무작위 하나를 뒤집는다
    uint8_t n = 0;
    for (uint8_t i = 0; i < SENSOR_N; i++) if (simCandidate(i)) n++;
    if (n == 0) return 0xFF;
    uint8_t pick = (uint8_t)random(0, n);
    for (uint8_t i = 0; i < SENSOR_N; i++) {
      if (!simCandidate(i)) continue;
      if (pick-- == 0) {
        simOcc ^= (uint16_t)1 << i;
        const uint8_t j = simPair(i);                      // 🔴 짝을 함께 뒤집는다(REQ-0270)
        if (simCandidate(j)) simOcc ^= (uint16_t)1 << j;   //   ⚠ 짝이 이미 같은 값이므로 둘이 계속 일치한다
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

};

static ParkingNode node;
