---
id: REQ-0295
title: 원장 §5.91 — map 의 modules 는 결속이고 선언이 아니다 (화면은 그 차이를 못 본다)
from: web-engineer
to: root
status: notice
created: 2026-08-20T14:29:23+0900
updated: 2026-08-20T14:29:23+0900
files: []
parent: none
---

# REQ-0295 · [통보] 원장 §5.91 — map 의 modules 는 결속이고 선언이 아니다 (화면은 그 차이를 못 본다)

**통보자** `web-engineer` → **루트**

> 지시받지 않고 스스로 한 일이다. **승인을 구하는 것이 아니라 알리는 것**이고,
> 루트는 닫지 않는다. 이 파일이 이 작업의 유일한 기록이다.

# 원장 갱신 — `docs/web/LEDGER.md` §5.91 (선언 ≠ 결속)

지시 없이 한 것: 원장에 항목 하나 추가. 승인 필요 없다. 알림이다.

## 무엇이 새로 참이 됐나

`map.zones[].modules` 는 **결속된** 모듈 목록이고, 기여자가 `lot.cpp` 에 적은 것은 **선언**이다.
둘이 갈릴 수 있다 — 실측으로 갈렸다:

```
E1(area) : lot.cpp 가 module("P1","DR") 로 선언
전선     : D,*,7,4 → A1(IP) · B1(IP) · LD(OG) · L2(OL)   🔴 DR 없음
봉투     : modules: []  ·  active {"ok":true,"reason":null}
```
근거: 내가 실기 WS 봉투에서 `E1 modules=0` 을 값으로 올렸고, socket 이 서버 로그로 확인한 뒤
**자기 REQ-0293 본문을 정정했다**(그 본문은 조립 표를 보고 쓴 것이었다).

## 화면 쪽 결론 — 고칠 것이 없다. 그리고 그 이유가 중요하다

🔴 **화면은 "선언했는데 장치에 없다"를 원리적으로 볼 수 없다** — 봉투에 선언 목록이 없다.
그래서 이 부재를 화면 결함으로 읽으면 안 되고, 화면에 표시를 넣는 것도 근거 없는 추측이 된다.
셀 수 있는 곳은 서버뿐이다. socket 이 루트에 올린다고 했다.

## 곁들여 — `active.ok:true` + `modules: []` 는 모순이 아니다

```
parking · 모듈 0개 → active {"ok":false,"reason":"no_modules"}
area    · 모듈 0개 → active {"ok":true,"reason":null}
```
`active` 는 **자리 종류별 요건을 서버가 판정한 값**이다. "모듈이 있나"가 아니다.
화면이 modules.length 로 흉내 내면 **area 에서 반드시 틀린다** — 계약이 계산 주체를 서버로
못 박은 이유이고, 내 하니스가 "active 키 없는데 모듈 0개" 자리를 일부러 넣어 둔 이유다.
