---
id: REQ-0293
title: map.zones[].active — 모듈 0개인 주차 자리를 비활성으로 그려라
from: socket-engineer
to: web-engineer
status: claimed
created: 2026-08-20T14:22:39+0900
updated: 2026-08-20T14:23:10+0900
files: ["web/index.html"]
parent: none
---

# REQ-0293 · map.zones[].active — 모듈 0개인 주차 자리를 비활성으로 그려라

**요청자** `socket-engineer` → **담당** `web-engineer`

## 요청 내용

서버가 `map` 봉투에 **`zones[].active`** 를 싣기 시작한다. 화면이 그것을 보고 **비활성**으로 그려라.

## 계약 (정본: `docs/net/SPEC-assembly-v2.md` §5 — `map.zones[].active`)

```json
{"id":"A2","kind":"parking","active":{"ok":false,"reason":"no_modules"},"cells":[[0,1]],"modules":[]}
{"id":"A1","kind":"parking","active":{"ok":true,"reason":null},  ...}
{"id":"E1","kind":"area",   "active":{"ok":true,"reason":null},  ...}
```

| 항목 | 값 |
|---|---|
| 계산 주체 | **서버**. 화면이 `modules.length` 를 세지 마라 — 같은 규칙이 두 곳에 생긴다 |
| 전달 시점 | `map` (접속 시). 조립 시점 사실이라 런타임에 안 바뀐다 |
| 모든 자리에 | 항상 실린다. 선택적이 아니다 |
| 모양 | `actions.*` 와 같은 `{ok, reason}` — 이미 아는 꼴이다 |
| reason 닫힌 집합 | 지금은 `"no_modules"` 하나. `ok:true` 면 `null` |

## 무엇을 그리나

`active.ok === false` 인 자리를 **비활성으로 구분되게** 그려 달라. 모양은 네 판단이다 —
흐리게·점선·"모듈 없음" 배지 무엇이든 좋다. 요구는 **평범한 자리와 눈으로 갈리는 것**이다.

## 왜 — 이건 우리가 이미 아는 함정이다

모듈이 없는 주차 자리는 점유가 **영원히 `unknown`** 인데 화면에는 평범한 자리로 보인다.
그러면 사람이 **"센서가 고장났나"** 를 쫓는다. 원장에 적혀 있는 그 함정이고, 이제 보이게 만드는 것이다.

## 안 하는 것 — 중요하다

```
`value_state:"unknown"` 을 없애지 마라. 그 자리의 점유는 여전히 모르는 것이 맞다
`active` 는 그 무지를 지우는 것이 아니라 **이유**를 붙이는 것이다
occupied:false 를 "비었다"로 바꿔 그리지 마라 — 여전히 "모른다"이다
```

## 지금 지형 (배포 뒤)

```
(0,0) A1  parking  모듈 4개(P1 A1/B1/LD/L2)  → active.ok true
(0,1) A2  parking  모듈 0개                   → active.ok false / no_modules
(0,2) A3  parking  모듈 0개                   → active.ok false / no_modules
(0,3) A4  parking  모듈 0개                   → active.ok false / no_modules
(0,4) A5  parking  모듈 0개                   → active.ok false / no_modules
(3,4) E1  area     모듈 1개(DR)               → active.ok true
격자 4행 5열 · 빈 칸 14개
```

## 확인

서버 자가검증에 계약 리터럴 대조를 넣어 뒀다(세 갈래: 모듈있는주차 / 모듈0개주차 / 모듈0개 일반영역).
화면 쪽은 네 하니스로 봐 달라. 배포 시각은 따로 한 줄 보낸다.

## 왜 필요한가

모듈 없는 주차 자리는 점유가 영원히 unknown 인데 화면에 평범한 자리로 보여서 센서 고장을 쫓게 된다

## 대상 파일

- `web/index.html`
## 완료 기준

active.ok===false 인 자리가 화면에서 평범한 자리와 눈으로 갈린다. value_state unknown 은 그대로 둔다

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0293 --by web-engineer --note "<한 줄 요약>" -->

_(미처리)_
