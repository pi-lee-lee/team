# 화면 은닉화 분해안 — 🔴 **중단됨 (착수 전)**

> ## 🔴 **2026-08-19 · 사용자 지시로 중단. 코드는 한 줄도 안 건드렸다.**
> 사용자 원문: *"**웹은 리팩토링 안 해도 된다.** 했다면 취소해라."*
>
> **이 문서를 지우지 않는다.** 나중에 누가 같은 것을 제안하면 **이미 검토했고 어디까지 갔는지**를
> 이 문서가 답한다. 되돌릴 코드는 없다 — 승인 대기 중이었다.
>
> ✅ **살아남은 판단 하나** (리팩터링과 무관하게 참이다 · CLAUDE.md `2415ba0` 에 실렸다):
> ### **"파일로 나눈다"의 비용은 언어가 아니라 *배포* 가 정한다.**
> ### **컴파일 언어에서는 파일이 사라지고, 웹에서는 파일이 남는다.**
> 같은 날 셋이 같은 지시를 받았는데 arduino·socket 은 헤더로 쪼갰고 **화면은 안 쪼갠다**로 갈렸다.
> 근거는 그날 실제로 난 사고다 — `serve_file()` 이 cwd 상대라 **이틀 낡은 화면이 서빙됐다**(원장 §5.85).
> **배포 단위가 1→N 이 되면 그 사고가 N배가 된다.**


> 루트 요구: ① 현황 ② **코드로** 된 목표 모양 ③ 단계 + **미리 정한 판별자** ④ **안 할 것**
> **착수는 루트 승인 뒤다.** 이 문서는 승인 대상이지 작업 기록이 아니다.

## ① 현황 — 실측

```
조별과제샘플/web/index.html   3,721줄
  CSS      389줄 (31–420)
  HTML     115줄
  JS     3,184줄 · 최상위 함수 71개
```

⚠ **아래 "줄 수"는 다음 함수 선언까지의 거리**라 사이의 주석이 포함된다. **정확한 본문 길이가 아니다** —
크기 순서를 보려는 것이지 절대값이 아니다.

```
467  handleServerMessage     ← 🔴 가장 큰 덩어리
197  createDemoTransport
188  wsTargetUrl
161  renderZone
121  buildGrid
120  applyZoneState
101  renderZoneGrid
```

### 무엇이 뭉쳐 있나 — 함수는 갈려 있는데 **경계가 없다**

71개 함수는 이름으로는 이미 갈린다. 문제는 **그 사이에 벽이 없다**는 것이다:

```
🔴 전역 가변 state 를 아무 함수나 직접 읽고 쓴다
   → 그리기 함수가 state 를 읽고, 조작 함수가 state 를 쓰고, 수신 함수도 쓴다
   → **"이 값을 누가 바꾸나"를 파일 전체를 읽어야 안다**
🔴 그리기 함수가 계약 해석을 겸한다 (renderZone 이 zs.modules 를 뒤져 짝을 찾는다)
🔴 경로가 둘이다 — 옛 slots[] 와 새 zones 가 공존한다(의도된 안전장치)
```

**관심사로 묶으면 이렇다** (71개 전부 분류했다):

| 관심사 | 함수 | 수 |
|---|---|---|
| **전송** | resolveWsPort · wsPortFromPage · wsTargetUrl · createDemoTransport | 4 |
| **수신·계약 해석** | handleServerMessage · applyMap · applyZoneState · applySnapshot · normalizeSnapshot · snapshotFromLog · snapshotFingerprint · mapUsable | 8 |
| **파생 판정** | currentSlot · isMine · pendingForSlot · canAct · actionBlockedReason · zoneStateById · zoneSummary · familyOf · baseKind · isVirtualKind · isOutputKind · orderedModules · zoneBlockedByFault · zoneBlockedByUnknown · zoneLabel · zoneHasLabel · isArmed · simPendingCount | 18 |
| **그리기** | render · buildGrid · tileView · renderSlotGap · renderSim · renderTestMode · renderConnection · renderFreshness · renderDevice · renderFrameAge · renderAge · renderZoneGrid · renderZoneBanner · renderZone · renderModuleRow · blockedMetaText · staleNote · showMessage | 18 |
| **조작 발행** | newRid · sendCommand · markPending · clearPending · rollback · reconcilePending · onQueueExpired · onSelfTimeout · confirmDialog · clearCmdPending · afterConfirmBlockedText | 11 |
| **시간·포맷** | fmtAge · fmtUptime · silenceMs · isUnconfirmed · cooldownLeftMs · startCooldownTicker | 6 |
| **데모·시험** | makeOfflineFileLog · makeArmedFileLog · buildDemoTools | 3 |
| **사용자** | randomToken · loadUser | 2 |

🔑 **결론: 쪼갤 것이 없다. 이미 갈려 있다. 없는 것은 *벽* 이다.**
그래서 이 작업은 **"흩어진 것을 모으기"가 아니라 "경계를 세우기"** 다.
⚠ **그 구분이 중요하다** — CLAUDE.md §"흩어진 것을 모으면 그것이 곧 거동 변경이다" 가 경고하는 것은
**모으는 쪽**이다. 벽만 세우면 실행 순서가 안 바뀐다. **그래서 이 작업은 거동 변화 0 을 목표로 걸 수 있다.**

## ② 목표 모양 — 코드로

### 부르는 자리 (사용자가 요구한 "흐름이 보인다")

```js
const screen = ParkingScreen({
  transport: WsTransport(wsTargetUrl()),   // 또는 DemoTransport()
  mount:     document.getElementById('app'),
  user:      loadUser(),
});
screen.start();
```

### 안쪽 — 이음매 셋

```js
// 1) 수신: 전선 봉투 → 상태.  **여기서만 state 를 쓴다**
store.apply(frame)            // frame: map | state | snapshot | ack | error
store.get()                   // 읽기 전용 스냅샷을 준다

// 2) 그리기: 상태 → DOM.  **읽기만 한다. state 를 못 쓴다**
view.render(model)            // model = derive(store.get())

// 3) 발행: 사용자 조작 → 전선.  **여기서만 transport.send 를 부른다**
commands.reserve(zoneId)
commands.cancel(zoneId)
commands.gate(zoneId, 'open_gate' | 'close_gate')
```

**파생은 따로 둔다** — 지금 18개가 여기저기 흩어져 불린다:

```js
derive(snapshot) -> {
  zones: [{ id, label, rawId, kind, summary, actions, nodes: [{ devid, modules: [...] }] }],
  banner, connection, freshness
}
```
🔑 **`derive` 가 순수 함수면 그리기가 계약을 안 뒤진다.** 지금 `renderZone` 이
`zs.modules.find(...)` 로 짝을 찾는 것이 정확히 그 위반이다.

### 🔴 파일은 안 쪼갠다 — 그 이유가 오늘 배운 것이다

```
서버는 serve_file() 로 **cwd 상대 단일 파일**을 낸다. 번들러가 없다.
파일을 쪼개면 배포 단위가 1개 → N개가 되고, 그 순간 §5.85(화면이 cwd 를 따라간다)가
**N배로** 재현된다 — 한 파일만 낡아도 조용히 섞인다.
```
→ **한 파일 안에서 IIFE 로 경계를 세운다.** 벽은 모듈 시스템이 아니라 **스코프**로 만든다.
⚠ 이건 타협이 아니라 **배포 구조에 맞춘 설계**다. 번들러를 들이면 그때 다시 판단한다.

## ③ 단계와 판별자 — 🔴 **판별자를 먼저 만든다**

### 🔑 내 "hex 0" 은 무엇인가 — **골든 DOM**

arduino 는 `hex` 바이트, socket 은 자가검증 출력과 기동 로그를 썼다. **화면에는 그런 게 없다.**
그래서 만든다:

```
고정된 프레임 묶음을 주입 → #zone-grid 등 관심 영역의 outerHTML 을 뽑아 정규화 → sha256
🔴 리팩터링 전후로 **그 해시가 같아야 통과**다.
```
**이것이 "산출물 바이트가 그대로면 끝"의 화면 판본이다.**

⚠ **시간이 섞이면 못 쓴다.** `fmtAge`·`silenceMs`·아이들 티커가 DOM 에 시각을 넣는다.
→ **주입 전에 `Date.now` 를 고정**하고, 그래도 흔들리는 자리는 **정규화에서 제외하고 그 사실을 출력에 적는다.**
🔴 **제외한 것을 조용히 빼면 그 해시는 "같다"를 과장한다** — 제외 목록을 매 실행에 찍는다.

### 단계

| # | 하는 일 | 🔴 판별자 (미리 정함) |
|---|---|---|
| **0** | **골든 DOM 하니스**(`web/tools/golden-dom.mjs`) | 같은 프레임으로 **두 번 돌려 해시가 같다**(자기 안정성). 🔴 **그리고 일부러 한 글자 바꿔 해시가 달라지는 것**을 본다 — 안 그러면 이 판별자가 무엇을 잡는지 모른다 |
| **1** | 파생 18개를 `derive()` 한 곳으로 (호출부는 그대로) | **골든 DOM 해시 동일** + `zone-nodes` 주입·실기 동일 |
| **2** | `store` 도입 — `state` 직접 쓰기를 `store.apply` 하나로 | **골든 DOM 해시 동일** · `map-epoch` 통과 · 🔴 `state` 를 쓰는 곳이 `store` 밖에 0곳(grep) |
| **3** | `view.render(model)` — 그리기가 `store` 를 못 보게 | **골든 DOM 해시 동일** · 🔴 그리기 함수에서 `state.` 참조 0건(grep) |
| **4** | `commands` 분리 — `transport.send` 호출을 한 곳으로 | 🔴 **골든 DOM 으로는 못 잡는다**(발행은 DOM 이 아니다) → `queue-contract`·`e2e` 주입 + **실기 1회 왕복** |
| **5** | 부르는 자리를 위 모양으로 | 실기 `--live` 동일 · `deploy-screen --check` 2 pass |

🔑 **1~3 은 골든 DOM 해시로 "거동 변화 0"을 값으로 주장할 수 있다.**
🔴 **4 는 아니다** — 그래서 4 를 마지막 근처에 두고 **실기 왕복을 판별자로 박았다.**
⚠ **각 단계는 독립 커밋**이다. 오늘 배운 대로 **분리와 변경을 한 커밋에 안 섞는다.**

## ④ 🔴 안 할 것 — 정해서 적는다

```
❌ 파일 분리 · 번들러 · 프레임워크        → 배포 단위가 늘면 §5.85 가 N배가 된다
❌ 옛 slots[] 경로 제거                  → 안전장치이고 **별건**이다. 지우려면 따로 승인받는다
❌ CSS 재구성                            → 이 작업의 목표가 아니다. 손대면 골든 DOM 이 통째로 흔들려
                                           판별자를 잃는다
❌ 데모 트랜스포트 재작성                 → 시험 경로다. 제품 경로를 고치는 중에 같이 만지지 않는다
❌ 문구·라벨·표시 변경                    → 거동 변화 0 을 주장하려면 표시가 그대로여야 한다
❌ 접근성 구조 개선(있는 결함이라도)       → 발견하면 **적어 두고 별건으로** 낸다
```

⚠ **"하는 김에" 를 막는 것이 이 목록의 전부다.** 오늘 `cells` 중복 렌더 결함처럼 **도중에 결함을
발견하면 고치고 싶어진다** — 그때는 **멈추고 별건으로 내라.** 섞으면 골든 DOM 이 달라지고,
달라진 이유가 리팩터링인지 수정인지 **아무도 못 가른다.**

## ⑤ 위험 — 미리 적는다

```
🔴 골든 DOM 이 시간·순서에 흔들리면 판별자가 죽는다 → 단계 0 에서 그것부터 증명한다
🔴 렌더가 매번 격자를 통째로 다시 만든다(포커스 보존 코드가 그 증거) — 구조를 바꾸면
   그 보존이 깨질 수 있다. **포커스 보존은 골든 DOM 이 못 잡는다**(DOM 모양은 같다)
   → 단계 3 에 **포커스 보존 검사**를 따로 넣는다
⚠ 지금 서버가 계속 바뀐다(오늘만 판본 셋). **리팩터링 중 실기 판정은 srv_id 를 같이 적는다**
```

## ⑥ 승인받을 것

1. 이 분해안 전체
2. **단계 0 을 먼저 하는 것**(판별자 없이 1단계를 시작하지 않는다)
3. `④ 안 할 것` 목록 — 특히 **옛 `slots[]` 경로를 안 건드리는 것**
