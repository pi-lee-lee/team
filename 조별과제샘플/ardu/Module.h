#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Module.h — 🔓 **모듈 표에 쓰는 것들**: `SENSOR` · `ACTUATOR` · `PIN_NONE`
//   ⚙ 응용 성질 — 잠금 대상이 아니다
//
// 🔓 **기여자는 이 파일을 열지 않아도 된다.** `client.ino` 의 표에 이 둘을 쓸 뿐이다.
//
// 🔴 **왜 `client.ino` 가 아니라 여기인가**: 아두이노는 `.ino` 의 함수 정의마다
//   **프로토타입을 파일 머리에 자동 삽입한다.** `SENSOR`/`ACTUATOR` 가 `.ino` 에 있으면
//   그 선언이 `struct ModuleDef` **앞**에 끼어들어 `'ModuleDef' does not name a type` 이 난다.
//   → **타입과 팩토리는 헤더에.** 표와 핸들러만 `.ino` 에 남는다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 **모듈 표 바로 앞**에서 `#include` 된다.

#define PIN_NONE 0xFF     // 핀 없음(가상 모듈·표시기처럼 핀이 필요 없는 것)

// 회귀 시험만 쓰는 확장점. 기본은 **비어 있다** — 샘플에는 아무 영향이 없다.
//   시험 하네스가 이것을 정의해 자기 모듈을 더 넣는다. 그래야 `client.ino` 에
//   시험용 `#if` 를 두지 않고도 명령 경로를 계속 밟을 수 있다.
//   ⚠ **샘플 코드에서는 이 이름을 쓰지 마라.** 자기 모듈은 표에 직접 적는다.
#ifndef SAMPLE_EXTRA_MODULES
#define SAMPLE_EXTRA_MODULES
#endif

// 표에 적는 두 종류의 함수. 🔴 **표보다 앞에 있어야 한다** — 표가 이 타입을 쓴다.
typedef bool (*CommandFn)(uint32_t arg);   // `O*` 모듈: 반환 true → ACK `result=0` · false → `3`
typedef bool (*SensorFn)(uint8_t pin);     // `I*` 모듈: 반환 true = 찼다 (비우면 `digitalRead`)

struct ModuleDef {
  char    name[3];      // "A1" + NUL — 명칭이자 **지금은 자리 결속 키다**(위 경고)
  char    kind[4];      // 🔴 **첫 글자만 뜻이 있다**: `I`=관측 전용 · `O`=명령 받음
                        //   둘째 글자부터는 자유다(서버는 안 본다). 2~3글자 + NUL
  uint8_t pin;
  // 🔓 **이 모듈이 하는 일.** 비워 두면(`0`) 기본 동작이다 —
  //   `cmd` 가 없으면 명령에 `result=3`(수행 불가), `sense` 가 없으면 `digitalRead(pin)`.
  //   🔑 표가 `PROGMEM` 이라 **함수 포인터도 플래시다. RAM 을 먹지 않는다.**
  CommandFn cmd;
  SensorFn  sense;
};

// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **표에 쓰는 것은 둘이다 — 아두이노의 INPUT / OUTPUT 과 1:1 이다**    █
// ██████████████████████████████████████████████████████████████████████████
//
//   SENSOR  ("A1", 2)                 ← 핀을 **읽는다**.  `pinMode(2, INPUT_PULLUP)` 을 대신한다
//   ACTUATOR("LD", 13, cmdLed)        ← 핀에 **쓴다**.    `pinMode(13, OUTPUT)` 을 대신한다
//
// 🔑 **이 둘만 알면 된다.** 종류가 늘어도 여기는 안 바뀐다 —
//   화면에 보일 이름은 **서버 조립 표**가 정한다: `lot.label("P1", "LD", "안내등")`.
//
// ⚠ **이름은 정확히 2글자다.** 3글자를 주면 **컴파일이 막는다** —
//   `const char (&)[3]` 이라 타입이 그것을 강제한다(주석이 아니라 컴파일러가 말해 준다).
//
// 🔴 센서 훅은 **선택**이다: `SENSOR("A1", 2, myRead)` — 안 주면 `digitalRead(핀)` 이 기본이다.
//   액추에이터 핸들러는 **필수**다 — 없으면 명령에 `result=3`(수행 불가)로 답할 뿐이다.
//
// 🔮 그 밖의 종류가 필요하면 리터럴로도 쓸 수 있다: `{"E1", "OB", PIN_NONE, gateE1, 0}`.
//   전선 `kind` 를 직접 정하는 것이고, **화면 라벨 폴백**에만 쓰인다(정본은 위 `lot.label`).
// ██████████████████████████████████████████████████████████████████████████
static constexpr ModuleDef SENSOR(const char (&name)[3], uint8_t pin, SensorFn sense = 0) {
  return ModuleDef{ {name[0], name[1], 0}, {'I','P',0,0}, pin, 0, sense };
}
static constexpr ModuleDef ACTUATOR(const char (&name)[3], uint8_t pin, CommandFn cmd) {
  return ModuleDef{ {name[0], name[1], 0}, {'O','G',0,0}, pin, cmd, 0 };
}
