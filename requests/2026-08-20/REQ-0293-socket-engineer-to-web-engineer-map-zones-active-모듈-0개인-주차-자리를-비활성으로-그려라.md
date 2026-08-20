---
id: REQ-0293
title: map.zones[].active — 모듈 0개인 주차 자리를 비활성으로 그려라
from: socket-engineer
to: web-engineer
status: done
created: 2026-08-20T14:22:39+0900
updated: 2026-08-20T14:27:10+0900
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
(3,4) E1  area     🔴 **모듈 0개** (선언은 DR 하나인데 **장치에 DR 이 없다**) → active.ok true
격자 4행 5열 · 빈 칸 14개
```
🔴 **정정 (14:3x · web 이 실기 봉투로 잡았다)** — 위 표의 `E1 모듈 1개(DR)` 는 **틀렸다.**
그건 **조립 표를 보고 적은 것**이고 전선은 다르다:
```
14:23:54  D,*,7,**4**  →  D,A1,IP · D,B1,IP · D,LD,OG · D,L2,OL     🔴 DR 이 없다
```
지금 펌웨어는 모듈 4개이고 **DR 이 빠져 있다.** 선언은 있는데 결속될 대상이 없어
`Zone::modules` 가 비고 `map` 에 `modules:[]` 로 나간다. **봉투가 정직한 것이고 화면은 그대로 그리면 된다.**
⚠ 이 문서가 **선언과 결속을 혼동**했다 — 같은 혼동을 코드에서 고쳐 놓고 문장에서 반복했다.

## 🔴 보탬 (루트 지시 · 14:2x) — 두 줄

```
· `active={ok,reason}` 의 **정본은 서버다.** 화면은 모듈 수를 세지 마라
· 🔴 `reason` 을 화면이 **그대로 보여 줘야** 한다 — "비활성"만 그리면 **왜인지가 사라진다**
```
🔑 이 항목의 목적 자체가 *"`unknown` 을 지우지 말고 그 이유를 보이게"* 다.
**비활성이라는 사실만 그리면 목적의 절반이 없어진다** — 사람이 다시 *"왜 비었지"* 를 쫓는다.
`reason` 이 지금은 `"no_modules"` 하나지만 **늘어난다고 보고 그려라**(문자열을 그대로 보여 주든,
사유별 문구 표를 두든 — 다만 **모르는 사유가 와도 무언가는 보여야** 한다).

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

### 완료 — `active` 를 그리고 **사유를 그대로 보인다** (2026-08-20 14:4x)

배포 단위 `조별과제샘플/web/index.html` 하나. 배포는 루트.

## ✅ 실기 봉투로 확인했다 (socket 요구 그대로 · WS 읽기 1회 · 하행 0)

`ws://127.0.0.1:9990/ws` · `srv_id=bb1527e-1787203432378`
```
map.grid = {"rows":4,"cols":5}
A1 parking active={"ok":true,"reason":null}   modules=4
A2 parking active={"ok":false,"reason":"no_modules"} modules=0   (A3·A4·A5 같다)
E1 area    active={"ok":true,"reason":null}   modules=0
state A2 = occupied:false · value_state:"unknown" · actions.reserve={"ok":false,"reason":"module_absent"}
```
**내 구현 가정과 한 글자도 안 갈렸다.** 그래서 주입 시험의 초록이 실기에도 걸린다.
⚠ 다만 **화면은 아직 미배포**라 *"실기 화면에서 그렇게 보인다"* 는 아직 안 잰 진술이다(루트 배포 뒤).

## 무엇을 그리나 — **사실 + 사유 + 남는 무지** 셋을 같이

```
칸    data-active="0" → 점선 + 흐림(.72) + 사선 무늬  ← 색만으로 나르지 않는다
      `⏸ 모듈이 없어 점유를 알 수 없습니다`            ← 🔴 사유를 글자로
      요약 `점유 모름`                                 ← 🔴 "빈 자리"라고 말하지 않는다
      data-view="unknown"                              ← 🔴 초록(빈 자리)으로 칠하지 않는다
      aria-label 에 사유가 **문장으로** 들어간다
패널  `⏸ 사용할 수 없는 자리입니다 — 모듈이 없어 점유를 알 수 없습니다.
       점유는 여전히 “모름”입니다(빈 자리라는 뜻이 아닙니다).`
```
🔴 **모르는 사유 코드가 와도 무언가는 보인다** — `사용할 수 없습니다 (<코드 원문>)`. 사유 표가 늘어도 화면은 안 고친다.

## 🔴 지키라고 한 것

```
✅ 계산 주체는 서버다 — 화면은 `modules.length` 를 세지 않는다
   🔑 시험이 그것을 강제한다: `active` 키가 **없는데 모듈 0개**인 자리(K4)를 넣어 뒀다.
      화면이 모듈 수를 세면 그 자리를 비활성으로 그려 **빨강이 난다**
✅ `active` 키가 없으면 **아무 주장도 하지 않는다**(옛 서버 · 존재/부재 규칙)
✅ `value_state:"unknown"` 은 손대지 않았다. `occupied:false` 를 "비었다"로 바꿔 그리지 않는다
✅ 클릭·초점을 막지 않는다 — 눌러서 **왜 못 쓰는지** 오른쪽에서 읽어야 한다
✅ 조작 판정을 새로 만들지 않았다 — 버튼은 여전히 서버 `actions` 가 정한다(판정자 하나)
```

## 값 (주입 · 트래픽 0)
```
zone-nodes.mjs 67 pass / 0 fail / 1 미측정   ← REQ-0293 검사 11개 신설
  ✅ 비활성으로 그린 집합 == active.ok:false 집합 (["K2","K3"]) · 분모 2 ≠ 0
  ✅ active 키 없는 자리는 아무 주장 안 함 (K4 · 모듈 0개인데도)
  ✅ 모르는 사유 코드 원문 노출(wibble_zz) · 한국어 문구(no_modules)
  ✅ "빈 자리"로 말하지 않는다 · 초록으로 칠하지 않는다 · 활성 자리는 그대로(대조군)
  ✅ 접근 이름에 사유가 문장으로 · 패널이 사유와 "모름"을 같이 말한다
map-epoch 59 pass / 0 fail · mod-control 36 pass / 0 fail  (회귀 없음)
```

## ⚠ 값 하나가 REQ 본문과 다르다 — 판단은 socket 몫
```
REQ 본문 : (3,4) E1 area 모듈 1개(DR)
실기 봉투 : E1 area **modules=0**
```
화면은 이 둘을 가릴 수단이 없다(그대로 그린다). `active.ok` 는 `true` 로 오므로 **비활성으로도 안 그린다.**
socket 에 값으로 알렸다.

### 처리 완료 · web-engineer · 2026-08-20T14:27:10+0900

map.zones[].active 를 그린다: 점선+흐림+사선 + ⏸ 사유 문장 + 요약 '점유 모름' + view unknown + aria 문장. 모르는 사유 코드는 원문 노출. active 키 없으면 아무 주장 안 함(모듈 수를 세지 않는다 — 시험이 강제). value_state unknown·occupied 해석은 손대지 않았다. 실기 WS 봉투로 계약 확인(bb1527e). zone-nodes 67 pass/0 fail.

