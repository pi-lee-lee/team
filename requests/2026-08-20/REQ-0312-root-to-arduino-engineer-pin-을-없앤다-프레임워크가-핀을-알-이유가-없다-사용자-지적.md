---
id: REQ-0312
title: 🔴 pin() 을 없앤다 — 프레임워크가 핀을 알 이유가 없다 (사용자 지적)
from: root
to: arduino-engineer
status: done
created: 2026-08-20T18:22:00+0900
updated: 2026-08-20T18:52:30+0900
files: ["조별과제샘플/ardu/client.ino"]
parent: none
---

# REQ-0312 · 🔴 pin() 을 없앤다 — 프레임워크가 핀을 알 이유가 없다 (사용자 지적)

**요청자** `root` → **담당** `arduino-engineer`

## 요청 내용

사용자 지적(정본):
> *"훅은 **명칭과 함수의 매칭**이다. 서버에서 해당 명칭으로 아두이노 내의 함수를 호출하는 것이다.
> 그리고 해당 함수내에서 특정 핀의 정보를 읽어 정보를 처리하는 방식 아닌가?
> 그러면 **setup 영역에서 아두이노 기본설정인 pinmode로 설정해두고 함수에서 설정된 핀을 이용하면**
> 되는것 아닌가? **꼭 센서나 액츄에이터에서 핀정보를 알아야하는가?**"*

## 🔴 결론 — `pin()` 을 없앤다. 프레임워크가 핀을 알 이유가 없다

```
`pin()` 이 하는 일 : ① `pinMode` 설정  ② 핀 번호 저장 → 기본 `digitalRead(pin)`
→ ① 은 기여자가 `pinMode` 로 한다 (아두이노 기본)
→ ② 는 함수가 `digitalRead(9)` 로 한다
🔑 **둘 다 기여자가 하는 일이다. 프레임워크가 중간에서 들고 있을 이유가 없다**
```

## 목표 형태 — **모양이 하나다**

```c
bool readUltrasonic() { /* Trig 2 · Echo 4 · pulseIn */ }
bool readB1()         { return digitalRead(9) == LOW; }
bool cmdLed(uint32_t arg) { digitalWrite(13, arg ? HIGH : LOW); return true; }

void setup() {
  Serial.begin(115200);
  node.begin();

  pinMode(2, OUTPUT);  pinMode(4, INPUT);   // 🔓 내 핀. 아두이노 기본 설정
  pinMode(9, INPUT);
  pinMode(13, OUTPUT);

  node.sensor  ("A1").on(readUltrasonic);   // 🔓 이름 ↔ 함수
  node.sensor  ("B1").on(readB1);
  node.actuator("LD").on(cmdLed);
  node.actuator("L2").on(cmdL2);
}
void loop() { node.tick(); }
```

## 얻는 것 — 값으로 재라

```
✅ RAM **감소** : `pin` 필드가 사라진다 (모듈당 1바이트)
✅ 프레임워크 감소 : `modPin` · `pinMode` 루프 · `PIN_NONE` · `digitalRead(pin)` 분기 전부 소멸
✅ 전선 **불변** : `pin` 은 전선에 안 나간다
✅ 핀 개수 무관 : 1개도 2개도 5개도 함수 안에서 끝난다 — 🔑 REQ-0311(pin 다핀)이 **불필요해진다**
✅ 모양이 하나 : `.on()` 만 있다
```

## 🔴 시그니처도 정리해라

```c
지금 : typedef bool (*SensorFn)(uint8_t pin);   ← 🔴 인자를 받는데 함수가 안 쓴다
후   : typedef bool (*SensorFn)();              ← 인자 없음
      typedef bool (*CommandFn)(uint32_t arg);  ← 그대로(값은 서버가 준다)
```
⚠ **안 쓰는 인자를 남기면 기여자가 "이걸 써야 하나" 를 묻는다.** 없애라.

## 값으로 답할 것

```
① RAM·flash 증분 (줄어야 한다. 늘면 그것이 신호다)
② `PIN_NONE` 이 다른 데 쓰이나 — 없애도 되나
🔴 ③ 잃는 것 하나를 명시해라 : 단순 디지털 센서도 **함수를 하나 써야 한다**
   `return digitalRead(9) == LOW;` 한 줄이다. **그것이 부담인지 네 판단을 값으로 대라**
   🔑 근거 : 센서마다 HIGH/LOW·임계값·필터가 다르다. 함수가 있는 것이 오히려 자연스럽다
   ⚠ 부담이라고 판단되면 그 자리에서 멈추고 알려라 — 사용자에게 되묻는다
④ 액추에이터도 같은가 — `cmdLed` 가 이미 `digitalWrite` 를 직접 한다. 확인해라
```

## 범위
```
✅ 조별과제샘플/ardu/** — **기존 파일만. 새 파일 금지**(사용자 확정)
✅ client.ino 에 초음파 샘플을 **켠다**(Trig 2 · Echo 4 실물) · GUIDE.md
❌ 라이브러리 추가 · 전선·서버·화면 계약
⚠ 굽기 필요. REQ-0309(주석 정리) · D7 과 **같은 굽기에 묶어라**
🔴 "훅" 이라는 낱말도 걷어라 — 사용자가 그 말을 몰랐다(10곳).
   `.on(내함수)` 가 이미 "붙인다"를 말한다. **별도 용어를 만들지 마라**
```

## 성공 조건
```
🔴 `.pin()` 이 없다. `setup()` 의 모든 등록이 `.on(함수)` 하나로 통일
🔴 기여자가 **client.ino 만** 고쳐 자기 센서를 붙인다 (핀이 몇 개든)
🔴 실물 초음파로 A1 점유가 화면에 뜬다
🔴 전선 바이트 불변 — 🔑 **등록 프레임 전체를 대조해라. 줄 하나하나 적지 마라**(§96)
```

## 왜 필요한가

pin() 의 두 역할(pinMode 설정 · 핀 저장) 모두 기여자가 하는 일이다. 프레임워크가 중간에서 들고 있어서 2핀 센서에서 모양이 갈리고 핀 개수마다 API 가 필요해진다

## 대상 파일

- `조별과제샘플/ardu/client.ino`
## 완료 기준

setup 의 모든 등록이 .on(함수) 하나로 통일된다. pin 필드가 사라져 RAM 이 줄고 전선은 불변이다. 기여자가 client.ino 만 고쳐 핀 몇 개든 붙인다. 실물 초음파로 A1 점유가 화면에 뜬다

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0312 --by arduino-engineer --note "<한 줄 요약>" -->

_(미처리)_

### 처리 완료 · arduino-engineer · 2026-08-20T18:52:30+0900

pin() 제거 완료 · 414a8a6 을 18:43:58 굽기 IDENTICAL. flash -122B · RAM -8B(AVR sizeof(Mod) 8→7 실측) · 훅 낱말 10→0. 전선 불변은 등록 프레임 전체 바이트 대조로 확인(cmp 0). 표지 [SENS] A1=함수있음 B1=함수있음 실기 확인. 시험 321 PASS/0 FAIL. 원장 §98~§108.

