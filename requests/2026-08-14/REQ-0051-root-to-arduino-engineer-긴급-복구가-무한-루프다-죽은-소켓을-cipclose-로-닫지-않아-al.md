---
id: REQ-0051
title: 긴급: 복구가 무한 루프다 — 죽은 소켓을 CIPCLOSE 로 닫지 않아 ALREADY CONNECTED 만 반복된다
from: root
to: arduino-engineer
status: done
created: 2026-08-14T17:06:40+0900
updated: 2026-08-14T17:15:32+0900
files: ["조별과제샘플/client.ino"]
parent: none
---

# REQ-0051 · 긴급: 복구가 무한 루프다 — 죽은 소켓을 CIPCLOSE 로 닫지 않아 ALREADY CONNECTED 만 반복된다

**요청자** `root` → **담당** `arduino-engineer`

## 요청 내용

## 실기 증상 (사용자 보고 — REQ-0049 빌드)

> 커넥션이 끊기면 **`ALREADY CONNECTED` 가 계속 호출된다. 그리고 연결은 형성되지 않는다.**

## 원인 — 루트가 지시를 잘못 썼다

REQ-0049 에서 내가 "N회 연속 실패 → `netOnline = false` → **CIPSTART 재시도**"라고 지시했다.
**그게 반쪽이었다.** 루트가 코드로 확인한 것:

- `AT+CIPCLOSE` 가 **파일 어디에도 없다**
- `goOffline()` 은 `netStep = 4`(CIPSTART)로만 돌아간다
- **ESP 는 죽은 소켓을 아직 열려 있다고 믿는다** → CIPSTART 에 `ALREADY CONNECTED` 로 답한다
- 927행이 그걸 받아 `netOnline = true` → 다시 전송 → 또 3회 실패 → `goOffline()` → …

```
goOffline() → CIPSTART → "ALREADY CONNECTED" → online → 3회 실패 → goOffline() → 무한
```

**감지는 맞게 만들었다. 복구가 안 되는 것이다.** CIPSTART 는 ESP 의 낡은 소켓을 절대 못 지운다.

## 고칠 것

### ① `goOffline()` 은 먼저 소켓을 닫아야 한다

**`AT+CIPCLOSE` 를 CIPSTART 앞에 넣어라.** 닫으면 ESP 가 `CLOSED` 를 내고, 그건 기존 경로가
이미 올바르게 처리한다(`netOnline=false` + 멱등 캐시 비움 + CIPSTART 재시도).
즉 **CIPCLOSE 하나로 정상 생명주기 신호에 다시 올라탄다.**

단계 번호를 어떻게 배치할지는 네가 정해라(새 단계로 넣든, `goOffline()` 이 직접 쏘든).
`CLOSED` 가 안 올 수도 있으니 **CIPCLOSE 뒤에도 CIPSTART 로 넘어가는 길**은 남겨 둬라.

### ② `ALREADY CONNECTED` 를 무조건 믿지 마라 — 다만 지우지도 마라 ⚠

이 분기는 **초기 접속 경합에서는 옳다.** CIPSTART 가 두 번 나가고 첫 번째가 실제로 성공한 경우,
`ALREADY CONNECTED` 는 진짜로 "이미 붙었다"는 뜻이다. 그리고 그때 **캐시를 비우면 안 된다**
— REQ-0035 [18]-4 가 지키는 불변식이다. **그 동작은 유지해라.**

문제는 **전송 실패로 오프라인이 된 뒤**의 `ALREADY CONNECTED` 다. 그건 "붙어 있다"가 아니라
**"ESP 가 낡은 소켓을 붙들고 있다"** 는 뜻이고, 정반대 신호다.

**둘을 구분해라.** 예를 들어 `goOffline()` 이 "ESP 소켓이 낡았다고 의심한다"는 플래그를 세우고,
그 플래그가 서 있는 동안의 `ALREADY CONNECTED` 는 **온라인으로 올리지 않고 CIPCLOSE(또는 ③)로
보낸다.** 플래그는 실제 `CONNECT` 나 `CLOSED` 에서 내린다.
구체적 형태는 네가 정하되 **초기 경합 경로가 깨지지 않는다는 것을 하네스로 지켜라.**

### ③ 그래도 안 풀리면 올라가는 사다리 — 없으면 또 무한 루프다

CIPCLOSE 도 안 먹는 상황이 실제로 있다(ESP-01 AT 펌웨어가 꼬이는 경우). **그때 멈추면 안 된다.**

**CIPCLOSE 시도가 N회 실패하면 `AT+RST` 로 올라가 전체 초기화(step 0)부터 다시 하라.**
`netStep = 0` 으로 돌리면 기존 상태 기계가 그대로 처리한다.

N 을 지어내지 마라. 근거를 잡을 수 있다: CIPCLOSE 는 로컬 동작이라 즉시 응답해야 하므로
**두세 번이면 충분히 "안 먹는다"고 볼 수 있다.** 네가 정하고 근거를 주석에 남겨라.
그리고 재설정 사다리를 **로그로 보이게** 해라 — 어느 층까지 올라갔는지가 현장에서 유일한 단서다.

## 하지 말 것

- `ALREADY CONNECTED` 분기를 통째로 지우지 마라 — 초기 경합 경로가 깨지고 REQ-0035 불변식도 깨진다
- 주기적 `AT+CIPSTATUS` 폴링으로 대체하지 마라 — 9600bps 에 왕복을 더한다
- 타이밍·핀·보율은 건드리지 마라

## 검증

하네스로:
- 전송 실패 3회 → **CIPCLOSE 가 나간다** (CIPSTART 만 나가면 안 된다)
- CIPCLOSE 후 `CLOSED` 가 오면 정상 재접속으로 이어진다
- **`ALREADY CONNECTED` 만 계속 오는 상황에서 무한 루프에 빠지지 않고 사다리를 올라간다** ← 핵심
- **초기 접속 경합의 `ALREADY CONNECTED` 는 여전히 온라인으로 받아들이고 캐시를 비우지 않는다**(회귀)
- 컴파일 + 플래시·RAM

실기 검증은 사용자 몫이다. 지금 사용자가 이것 때문에 막혀 있으니 **최우선**이다.

**명령을 엮지 마라(a && b).** 하나씩 실행한다.

## 왜 필요한가

REQ-0049 의 자동 복구가 실기에서 무한 루프에 빠진다. 감지는 되지만 ESP 의 낡은 소켓을 닫지 않아 CIPSTART 가 영원히 ALREADY CONNECTED 만 받는다. 사용자가 이것 때문에 장시간 시연을 못 한다. 루트가 REQ-0049 를 지시할 때 CIPCLOSE 를 빠뜨린 것이 원인이다.

## 대상 파일

- `조별과제샘플/client.ino`
## 완료 기준

1) goOffline 경로에서 AT+CIPCLOSE 가 CIPSTART 앞에 나간다
2) 전송 실패 기인 오프라인 뒤의 ALREADY CONNECTED 를 온라인으로 받아들이지 않는다
3) 초기 접속 경합의 ALREADY CONNECTED 는 여전히 온라인으로 받아들이고 캐시를 비우지 않는다 (REQ-0035 [18]-4 회귀 없음)
4) CIPCLOSE 가 안 먹으면 AT+RST 로 올라가 전체 초기화한다 — N 의 근거를 주석에
5) 사다리의 각 층이 로그로 보인다
6) 하네스에 무한 루프 방지 케이스가 있고 PASS/FAIL 수를 적는다
7) 컴파일 통과 + 플래시·RAM
실기는 사용자 몫이다 — 했다고 적지 마라.

---

## 처리 결과

담당 `arduino-engineer` · 2026-08-14

사다리를 만들었다. **하네스가 실기 증상(무한 루프) 자체를 재현하고, 5.7초 만에 사다리를 끝까지
올라가 벗어나는 것을 확인했다.** 컴파일 통과, **138 PASS / 0 FAIL**. 바로 올려도 된다.

### 완료 기준 대조

| # | 기준 | 결과 |
|---|---|---|
| 1 | `goOffline` 경로에서 CIPCLOSE 가 CIPSTART 앞에 나간다 | **완료** — [30] 이 **AT 로그의 순서**로 단언 |
| 2 | 전송 실패 기인 오프라인 뒤의 ALREADY CONNECTED 를 안 믿는다 | **완료** — [31][32] |
| 3 | 초기 경합의 ALREADY CONNECTED 는 유지 + 캐시 안 비움 | **완료** — [32] 회귀 |
| 4 | CIPCLOSE 안 먹으면 AT+RST — N 근거 주석 | **완료** — N=3, 아래 근거 |
| 5 | 사다리 각 층이 로그로 보인다 | **완료** — 아래 |
| 6 | 무한 루프 방지 하네스 케이스 | **완료** — [31] 이 핵심 |
| 7 | 컴파일 + 플래시·RAM | **12162 B(37%) / 792 B(38%)** |

### 사다리 구조

```
전송 3회 연속 실패(또는 link is not valid)
   → staleSocket = true,  netStep = CIPCLOSE          ← ★ CIPSTART 가 아니다
   → AT+CIPCLOSE
        ├ CLOSED 옴  → 기존 경로가 처리(오프라인 확정 + 캐시 비움 + CIPSTART)  ← 정상 생명주기 복귀
        └ 안 옴      → CIPSTART 로 넘어감(길을 남겨 둔다)
   → CIPSTART
        ├ CONNECT          → 온라인, staleSocket 해제
        ├ ALREADY CONNECTED → **믿지 않는다.** staleSocket 서 있으면 다시 CIPCLOSE
        └ 무응답            → staleSocket 서 있으면 CIPCLOSE, 아니면 CIPSTART 재시도
   → CIPCLOSE 3회 실패 → **AT+RST → step 0 부터 전체 초기화**       ← 최상층
```

`NET_CIPCLOSE` 는 **복구 전용 단계(5번)** 로 넣었다 — 부팅 순서(0~4)에는 끼우지 않았다.
부팅 때 CIPCLOSE 를 지나갈 이유가 없고, 끼우면 첫 접속이 느려진다.

### N = 3 (CLOSE_ATTEMPT_LIMIT) 의 근거 (기준 4)

**CIPCLOSE 는 모듈 로컬 동작이라 네트워크 왕복이 없다.** 정상 모듈이면 대기 창(800ms) 안에
반드시 응답한다. 그래서:

- **1회는 이르다** — `busy p...` 같은 일시적 사정 하나로 14초짜리 RST 사이클에 들어간다.
- **3회 연속 로컬 명령이 안 먹으면 그건 AT 계층 자체가 꼬인 것**이고, 올바른 대응은
  재시도가 아니라 모듈 리셋이다. 더 크게 잡으면 무한 루프 구간만 길어진다.
- 실측: 하네스에서 **5.7초 만에** CIPCLOSE 3회 → AT+RST 까지 올라갔다.

### ② ALREADY CONNECTED — 같은 문자열, 정반대 의미

`staleSocket` 플래그 하나로 갈랐다.

| 상황 | 의미 | 처리 |
|---|---|---|
| `staleSocket` **없음** (초기 접속 경합) | 진짜로 "이미 붙었다" | 온라인으로 받고 **캐시를 비우지 않는다**(REQ-0035 [18]-4) |
| `staleSocket` **있음** (전송 실패 뒤) | "ESP 가 낡은 소켓을 붙들고 있다" | **온라인으로 올리지 않고** CIPCLOSE 로 보낸다 |

플래그는 `goOffline()`/`link is not valid` 에서 서고, 실제 `CONNECT` 나 `CLOSED` 에서 내린다.
분기를 지우지 않았으므로 초기 경합 경로는 그대로다 — [32] 가 그걸 지킨다.

### ⚠ 요청에 없던 것 하나를 같이 고쳤다 — `link is not valid` 경로

`goOffline()` 만 고치면 **`link is not valid` 경로가 같은 무한 루프에 그대로 남는다.**
그 핸들러도 `netStep = 4`(CIPSTART)로 직접 갔다.

그래서 `startSocketRecovery()` 로 공통화하고 둘 다 그것을 부르게 했다.
주석에 **"전송이 안 되는 것을 이유로 오프라인이 되는 모든 경로가 여기를 통과해야 한다"** 를
박아 뒀다 — 한 곳이라도 CIPSTART 로 바로 가면 그 경로에서 이 버그가 되살아난다.

### 로그 (기준 5)

```
[NET] 전송 3회 연속 실패 → 오프라인 전환. 낡은 소켓부터 닫는다(CIPCLOSE)
[NET] 5 CIPCLOSE (낡은 소켓 닫기)
[NET] ALREADY CONNECTED — 낡은 소켓 의심 중이므로 믿지 않는다 → CIPCLOSE
[NET] CIPCLOSE 3회 실패 → AT+RST 로 전체 초기화(사다리 상승)
[NET] 0 RST
[NET] 2 CWJAP        ← 부팅 순서를 다시 타는 것이 보인다
[NET] online (CONNECT) + 캐시 비움
```

어느 층까지 올라갔는지가 현장에서 유일한 단서라 각 층을 다 찍는다.

### 하네스 — **138 PASS / 0 FAIL** (120 → 138)

가짜 ESP 에 **`stickySocket` 모드**를 넣어 실기 증상을 그대로 만들었다:
CIPSTART 에 `ALREADY CONNECTED` 로 답하고 **CIPCLOSE 도 안 먹는다**(`CLOSED` 안 옴).
`AT+RST` 를 받으면 풀린다 — 사다리 최상층이 실제로 듣는지 볼 수 있다.
그리고 보낸 AT 명령을 `atLog` 에 기록해 **순서**를 검사할 수 있게 했다.

```
[30] 전송 실패 → CIPCLOSE 가 나가고, **CIPSTART 보다 먼저** 나간다(로그 위치 비교)
     → CIPCLOSE 가 먹으면 CLOSED → CIPSTART → 재접속, staleSocket 해제
[31] ★ refusePrompt + stickySocket 동시 → 5721ms 만에 CIPCLOSE 3회 → AT+RST 1회
     그 사이 ALREADY CONNECTED 를 한 번도 온라인으로 받지 않았다
     RST 뒤 CWJAP 부터 다시 진행해 재접속
[32] 초기 경합의 ALREADY CONNECTED 는 여전히 온라인 + 캐시 유지(REQ-0035 [18]-4)
     반대로 staleSocket 중이면 안 믿는다
```

**[31] 이 이 요청의 핵심이다** — 무한 루프였다면 `AT+RST` 가 영원히 안 나오므로 그 단언이 실패한다.

**하네스에서 두 군데를 손봤다(스케치 결함 아님).**
1. `[25]`/`[27]` 의 `netStep == 4` 단언이 **설계 변경으로 낡았다.** 이제 CIPCLOSE(5)로 간다.
2. `[27]` 은 **순간 상태로 단언하고 있어서** 틀렸다 — CIPCLOSE 가 즉시 나가고 `CLOSED` 가 바로 와서
   단언 시점엔 이미 다음 단계였다. **AT 로그 순서**로 바꿨다. `[30]` 도 같은 이유로
   `atLog[0]` 대신 "CIPCLOSE 위치 < CIPSTART 위치"로 바꿨다(선두에는 실패한 `AT+CIPSEND` 들이 있다).
   순간 상태보다 로그 순서가 기준 1을 더 직접 검증한다.

### 사용량

| 빌드 | 플래시 | 전역 RAM |
|---|---|---|
| REQ-0047 직후 | 11654 B (36%) | 788 B (38%) |
| **지금** | **12162 B (37%)** | **792 B (38%)** |
| 차이 | +508 B | +4 B (`staleSocket` 1 + `closeAttempts` 1 + 정렬) |

### 검증하지 않은 것

- ⚠ **실기 검증은 사용자 몫이다.** 실제 ESP-01 이 `AT+CIPCLOSE` 에 무엇으로 답하는지,
  꼬인 상태에서 정말 `AT+RST` 로 풀리는지는 보드에서만 확인된다.
- **`CLOSED` 가 CIPCLOSE 의 응답으로 온다는 전제**에 기대고 있다. 안 오면 CIPSTART 로
  넘어가는 길을 남겨 뒀으므로 막히지는 않지만, 캐시 비움은 그 경로에서 일어나지 않는다.
  (그때는 이어지는 `CONNECT` 가 비운다 — REQ-0036.)
- **왜 17분 뒤 TCP 가 죽는지는 여전히 모른다.** 이건 복구 사다리이고 원인 규명이 아니다.

### 처리 완료 · arduino-engineer · 2026-08-14T17:15:32+0900

복구 사다리: goOffline → AT+CIPCLOSE(신규 복구전용 step 5) → CLOSED 오면 기존 정상경로 → CIPSTART → staleSocket 중 ALREADY CONNECTED 는 안 믿고 다시 CIPCLOSE → 3회 실패면 AT+RST 로 전체 초기화. N=3 근거: CIPCLOSE 는 로컬 동작이라 왕복이 없어 3회 연속 불응이면 AT 계층이 꼬인 것. 초기 경합 ALREADY CONNECTED 경로와 캐시 미비움(REQ-0035 [18]-4)은 staleSocket 플래그로 갈라 유지. ⚠ 요청에 없던 link is not valid 경로도 같은 무한루프였어서 startSocketRecovery() 로 공통화. 컴파일 12162B/37%·RAM 792B/38%, 하네스 138 PASS/0 FAIL — stickySocket 모드로 실기 증상을 재현해 5.7초 만에 AT+RST 까지 올라가는 것을 확인

