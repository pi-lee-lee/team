#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Module.h — 🔓 **모듈 등록**: `node.sensor("A1").on(내함수)` · `node.actuator("LD").on(내함수)`
//   ⚙ 응용 성질 — 잠금 대상이 아니다
//
// 🔓 **기여자는 이 파일을 열지 않아도 된다.** `setup()` 에서 위 한 가지 모양만 쓴다.
//
// 🔴 **왜 `client.ino` 가 아니라 여기인가**: 아두이노는 `.ino` 의 함수 정의마다 프로토타입을
//   파일 머리에 자동 삽입한다. 타입·빌더가 `.ino` 에 있으면 그 선언이 `struct` 앞에 끼어들어
//   `'Mod' does not name a type` 이 난다. **타입과 빌더는 헤더에.**
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 `#include` 목록 **맨 앞**이다 —
//   `Slots.h`(`ParkingNode`)와 `Modules.h`(라우터)가 이 표를 읽는다.

// 🔓 모듈이 하는 일. 이름에 함수를 붙이면 그것이 등록이다.
// 🔴 **핀은 여기 없다.** 등록은 *이름 ↔ 함수* 의 짝이고, 핀은 그 함수 안의 일이다 —
//   기여자가 `setup()` 에서 `pinMode` 로 잡고 함수에서 `digitalRead(9)` 로 읽는다.
//   🔑 그래서 핀이 하나든 둘이든(초음파) **등록 모양이 같다.**
typedef bool (*CommandFn)(uint32_t arg);   // 액추에이터: 반환 true → ACK `result=0` · false → `3`
typedef bool (*SensorFn)(void);            // 센서: 반환 true = 찼다
// 🔓 **값을 내는 센서**(초음파처럼 거리를 아는 것). 문턱 판정은 `.near(cm)` 이 한다.
//   반환 : 측정값(cm 등) · **못 쟀으면 `SENSOR_NO_READING`**
typedef long (*SensorValueFn)(void);
// 🔴 **마법값에 이름을 준다.** `0` 은 "0cm" 와 안 갈리고 `-1` 은 이름 없는 마법값이다.
//   `pulseIn` 이 0 을 돌려주는 것은 *"반사가 없다"* 이고 **"거리가 0"이 아니다.**
#define SENSOR_NO_READING (-1L)

// 🔴🔴 **히스테리시스 여유** — 문턱 하나로는 원리적으로 진동을 못 막는다.
//   찼다 : `v < near`   ·   비었다 : `v > near + 이 값`   ·   그 사이 : **상태 유지**
//
// 🔑 **실측에서 정했다**(2026-08-20 22:00 굽기 · `[USD]`):
//     거리 흔들림 폭 = **70cm**(첫 구간) · **32** · **23**
//     문턱 60cm 를 세 구간 다 넘나들었고 → 서버 로그에 **1초 간격 전이**가 찍혔다
//     (22:02:11 비었다 → 22:02:12 찼다. 슬롯이 1.2초니 **연속 슬롯**이다)
//   ⚠ **1초 간격은 사람 손으로 못 만든다** — 고정 물체에서도 튄다는 증거다.
//
// 🔴 **왜 40 인가**:
//     안정 구간 폭 **23~32** 를 덮어야 한다 → 여유 ≥ 32
//     그리고 해제 문턱(`near + 여유`)이 **측정 상한 안**이어야 한다 —
//       타임아웃 `near × 58 × 2` 는 `near` 의 **2배 거리**까지만 잰다(60cm → 120cm).
//       여유 40 → 해제 100cm ✅ · 여유 70 → 해제 130cm 🔴 **측정 밖이라 불가능**
//   ⚠ **첫 구간 폭 70 은 이 여유로 못 덮는다.** 그 구간의 성질은 **미상**이다
//     (캡처 시작 직후라 물체 이동 중이었을 수 있다). **덮었다고 적지 않는다.**
//
// 🔑 **RAM 비용 0** — `occMask` 가 이미 "지금 찼나"를 들고 있어서 그것을 읽는다.
#ifndef NEAR_RELEASE_CM
#define NEAR_RELEASE_CM 40
#endif

// ─────────────────────────────────────────────────────────────────────────
// 🔴 **모듈 상한 8** — 실측으로 정했다(2026-08-20)
//   `ramLow`(실행 중 최저 여유) **602B** 대비 : 8모듈 **10.6%** · 16모듈 21.2%
//   ⚠ 컴파일러가 말하는 정적 여유 834B 는 **예산이 아니다** — 스택이 232B 내려간다
//   상한을 늘리려면 **`ramLow` 를 다시 재라**(부팅 70초 뒤 `[RAM]` 줄).
// ─────────────────────────────────────────────────────────────────────────
// ⚠ `#ifndef` 인 이유: **회귀 시험이 더 많은 모듈로 지형을 만든다.** 호스트에는 RAM 제약이 없다.
//   🔴 실기 빌드에서는 이 값을 바꾸지 마라 — 위 실측이 8 을 정했다.
//
// 🔴 **RAM 말고 두 번째 상한이 있다 — 센서 블로킹이다.**
//   센서 함수가 블로킹하면(초음파 `pulseIn` 6.96ms) 그동안 `espRead()` 가 안 돈다.
//   게이트는 **함수마다** 따로 만료되고 🔴 **부팅 직후에는 전부 만료 상태**라
//   첫 `readSensors()` 에서 N개가 **한꺼번에** 막힌다. 그건 우연이 아니라 늘 일어난다.
//     실측(시험 [45]) : 8개 전부 초음파 → 55.7ms · 유입 **53B / 링버퍼 64B**
//     🔴 넘치는 경계  : **10개**
//   ⚠ 그래서 이 값을 10 이상으로 올리면 **모듈이 다 초음파일 때 링버퍼가 넘친다.**
//     RAM 이 남아도 그렇다. **두 상한 중 작은 쪽이 답이다.**
//   🔑 타임아웃을 늘리면 경계가 그만큼 내려간다 — `client.ino` 가 타임아웃을
//     **문턱에서 유도**하는 이유가 이것이다(4m 로 두면 경계가 3개가 된다).
#ifndef MODULE_CAP
#define MODULE_CAP 8
#endif

// 회귀 시험만 쓰는 확장점. 기본은 **비어 있다** — 샘플에는 아무 영향이 없다.
//   시험 하네스가 이것을 정의해 자기 모듈을 더 등록한다. 그래야 `client.ino` 에
//   시험용 `#if` 를 두지 않고도 명령 경로를 계속 밟을 수 있다.
//   ⚠ **샘플 코드에서는 이 이름을 쓰지 마라.** 자기 모듈은 `setup()` 에 직접 적는다.
#ifndef SAMPLE_EXTRA_MODULES
#define SAMPLE_EXTRA_MODULES
#endif

struct Mod {
  char      name[2];   // 전선에 나가는 두 글자
  uint8_t   isAct;     // 1 = 액추에이터(전선 kind `OG`) · 0 = 센서(`IP`)
  CommandFn cmd;       // 액추에이터만 쓴다
  SensorFn  sense;     // 센서만 쓴다 — true/false 를 직접 내는 센서
  SensorValueFn val;   // 🔓 값을 내는 센서(둘 중 하나만 쓴다)
  uint16_t  nearCm;    // 🔓 `.near(cm)` 문턱. 0 = **판정 안 함**(값만 보낸다)
};
// 🔴 **표는 RAM 이다**(런타임 등록이므로). 전역이라 `.bss` 에서 0 으로 시작한다 —
//   생성자를 두지 않는 이유는 AVR 전역 생성자가 `main()` 전에 돌기 때문이다.
// 🔴 **핀 칸이 되살아나면 여기서 막힌다.** AVR 은 정렬이 1바이트라 패딩이 없어
//   필드 하나가 그대로 모듈당 1바이트다(실측: 8 → 7, 8모듈에서 RAM 8B).
//   ⚠ 호스트 시험은 이것을 못 잡는다 — 64비트에서는 포인터 정렬 패딩이 그 자리를 채워
//     `sizeof` 가 24 로 같다. **그래서 이 검사는 실기 빌드에만 있다.**
#ifdef __AVR__
// 🔴 **11 의 내역** — 안 적으면 다음 사람이 "원래 11" 로 읽는다:
//   name[2] 2 + isAct 1 + cmd 2 + sense 2 + **val 2** + **nearCm 2** = 11
//   (AVR 은 정렬이 1바이트라 패딩이 없다. 필드 크기가 그대로 구조체 크기다)
//   옛 값 7 → 11 : 값 훅(`val`)과 문턱(`nearCm`)이 들어왔다. 실측 RAM **+32B**(8모듈 × 4B)
static_assert(sizeof(Mod) == 11, "sizeof(Mod) 가 바뀌었다 — 필드를 더했나? 모듈당 RAM 이 는다");
#endif

static Mod     MODULE_TABLE[MODULE_CAP];
static uint8_t MODULE_N;        // 등록된 수. `D,*,7,<n>` 의 `n` 이고 `occ` 비트열 폭이다
static uint8_t modOverflowed;   // 상한을 넘겨 버린 등록 수 — 부팅에서 문장으로 말한다

// ── 접근자 — 🔴 **`cmd`/`sense` 를 만지는 곳은 이 둘뿐이다** ────────────────
//   센서 칸의 `cmd` 나 액추에이터 칸의 `sense` 는 **늘 0** 이지만(전역 0 시작),
//   그래도 여기서 종류를 확인한다. **판정이 한 곳에 있으면 두 곳이 갈릴 일이 없다.**
static inline bool isSensor  (uint8_t i) { return i < MODULE_N && !MODULE_TABLE[i].isAct; }
static inline bool isActuator(uint8_t i) { return i < MODULE_N &&  MODULE_TABLE[i].isAct; }
static inline CommandFn cmdOf (uint8_t i) { return isActuator(i) ? MODULE_TABLE[i].cmd   : (CommandFn)0; }
static inline SensorFn  senseOf(uint8_t i) { return isSensor(i)  ? MODULE_TABLE[i].sense : (SensorFn)0; }
static inline SensorValueFn valOf (uint8_t i) { return isSensor(i) ? MODULE_TABLE[i].val : (SensorValueFn)0; }
static inline uint16_t  nearOf  (uint8_t i) { return (i < MODULE_N) ? MODULE_TABLE[i].nearCm : 0; }
static inline char modName0(uint8_t i) { return (i < MODULE_N) ? MODULE_TABLE[i].name[0] : 0; }
static inline char modName1(uint8_t i) { return (i < MODULE_N) ? MODULE_TABLE[i].name[1] : 0; }
static inline uint8_t moduleCount(void) { return MODULE_N; }

// ─────────────────────────────────────────────────────────────────────────
// 🔓 빌더 — `node.sensor("A1").on(내함수)` 의 `.on(...)` 을 받는 임시 객체
//   🔑 **정적 RAM 을 안 쓴다.** 내부가 인덱스 하나이고 스택에 산다.
//   ⚠ `on()` 은 **오버로드**다 — 센서에는 `SensorFn`, 액추에이터에는 `CommandFn` 이 붙는다.
//     기여자는 `on` 하나만 기억하면 되고 **틀린 종류를 주면 컴파일이 막는다.**
// ─────────────────────────────────────────────────────────────────────────
class ModRef {
 public:
  explicit ModRef(uint8_t i) : i_(i) {}
  ModRef& on(CommandFn f) { if (i_ < MODULE_N) MODULE_TABLE[i_].cmd   = f; return *this; }
  // 🔴 **값 훅과 bool 훅은 배타다.** 하나를 붙이면 다른 하나를 떼어 낸다 —
  //   둘 다 있으면 `readRealSensor` 가 값 훅을 먼저 보므로 bool 훅이 **조용히 무시된다.**
  //   🔑 배타로 만들면 그 상태가 **원리적으로 생기지 않는다**
  //     (§"조건을 확인하는 것보다 조건이 성립할 수밖에 없게 만드는 쪽이 낫다").
  ModRef& on(SensorFn  f) { if (i_ < MODULE_N) { MODULE_TABLE[i_].sense = f; MODULE_TABLE[i_].val = 0; } return *this; }
  // 🔓 값을 내는 센서. **오버로드다** — 함수 모양이 다르면 컴파일러가 골라 준다.
  //   ⚠ `bool(*)()` 와 `long(*)()` 는 함수 포인터라 암시적 변환이 없다 →
  //     **틀린 종류를 주면 컴파일이 막는다.**
  ModRef& on(SensorValueFn f) { if (i_ < MODULE_N) { MODULE_TABLE[i_].val = f; MODULE_TABLE[i_].sense = 0; } return *this; }
  // 🔓 **문턱** — "이 값보다 작으면 찼다". 값 훅에만 뜻이 있다.
  //   🔑 옛 판은 이 판정이 핸들러 안 `#define` 에 숨어 있었다. 여기 있으면 **등록 줄에 보인다.**
  ModRef& near(uint16_t cm) { if (i_ < MODULE_N) MODULE_TABLE[i_].nearCm = cm; return *this; }
  uint8_t idx() const { return i_; }
 private:
  uint8_t i_;
};

// 등록 — 🔴 **호출 순서가 전선 `idx` 이고 `occ` 비트 위치다.**
//   ⚠ 센서·액추에이터를 섞어 적어도 된다(순서 불변식이 없다).
static ModRef modAdd(const char (&name)[3], uint8_t isAct) {
  if (MODULE_N >= MODULE_CAP) { if (modOverflowed < 255) modOverflowed++; return ModRef(0xFF); }
  const uint8_t i = MODULE_N++;
  MODULE_TABLE[i].name[0] = name[0];
  MODULE_TABLE[i].name[1] = name[1];
  MODULE_TABLE[i].isAct   = isAct;
  MODULE_TABLE[i].cmd     = 0;
  MODULE_TABLE[i].sense   = 0;
  MODULE_TABLE[i].val     = 0;
  MODULE_TABLE[i].nearCm  = 0;
  return ModRef(i);
}
