---
id: REQ-0265
title: 펌웨어 소스를 조별과제샘플/ardu/ 로 이사 — hex 0 을 유지해라 (사용자 지시)
from: root
to: arduino-engineer
status: done
created: 2026-08-19T11:16:39+0900
updated: 2026-08-19T12:46:19+0900
files: ["조별과제샘플/client.ino"]
parent: none
---

# REQ-0265 · 펌웨어 소스를 조별과제샘플/ardu/ 로 이사 — hex 0 을 유지해라 (사용자 지시)

**요청자** `root` → **담당** `arduino-engineer`

## 요청 내용

# 펌웨어 소스를 `조별과제샘플/ardu/` 로 옮긴다 (사용자 지시)

> 사용자: *"폴더를 생성하여 **server, ardu** 로 해당 폴더 내에 빌드파일들이 위치하도록 개선해달라."* / *"코드"*

**지금 `조별과제샘플/` 에 세 도메인 소스가 평면으로 섞여 있다.** 각자 자기 것을 옮긴다.

## 옮길 것
```
조별과제샘플/client.ino          →  조별과제샘플/ardu/client.ino
조별과제샘플/EspLink_state.h     →  조별과제샘플/ardu/EspLink_state.h
조별과제샘플/EspLink_gate.h      →  조별과제샘플/ardu/EspLink_gate.h
조별과제샘플/EspLink_tx.h        →  조별과제샘플/ardu/EspLink_tx.h
조별과제샘플/EspLink_ladder.h    →  조별과제샘플/ardu/EspLink_ladder.h
```
✅ **소유권은 루트가 미리 열어 뒀다** — `조별과제샘플/ardu/**` → `arduino-engineer`. 옛 경로도 아직 열려 있다(이사 중이라).

## 🔴 `git mv` 를 써라 — 그리고 커밋 함정을 기억해라

```bash
git mv <옛> <새>
git commit -m "..." -- 조별과제샘플/ardu 조별과제샘플/client.ino 조별과제샘플/EspLink_state.h …
```
⚠ **CLAUDE.md §"커밋은 `commit` 에 경로를 준다"의 *삭제* 판본이 여기 걸린다** — 네가 오늘 밟은 그것이다.
`git mv` 는 인덱스에 **삭제+추가**를 올린다. **경로 명시 커밋이 작업 트리를 취하므로 옛 경로가 되살아날 수 있다.**
🔑 **커밋 뒤 반드시 조회해라**: `git ls-files 조별과제샘플/ | grep -E 'client\.ino|EspLink'`
**옛 경로가 하나도 안 남아야 한다.** ← **상태를 바꾸는 명령 뒤에는 상태를 조회하는 명령**(네 규칙)

## 🔴 그리고 이사가 `hex` 를 바꾸면 안 된다

**넷을 다 같이 옮기므로 `#include "EspLink_*.h"` 는 그대로 풀린다** — 전처리 결과가 같으면 산출물이 같다(네 §31).
**그런데 그것은 예측이다. 재라.**
```
① 이사 뒤 빌드 → cmp 로 지금 칩과 대조
   ✅ exit=0  → 이사가 축이 아니다. 굽기 불필요
   🔴 다르면  → **왜인지 적고 멈춰라.** 루트에 올려라
```
⚠ **`arduino-cli` 는 스케치 디렉터리 이름과 `.ino` 이름이 맞아야 한다**(`X/X.ino`).
`ardu/client.ino` 는 그 규칙과 어긋나지만 **굽기 입력은 `arduino/.burn/client/` 로 복사되므로** 문제가 없을 것이다 —
🔑 **"없을 것"이 아니라 실제로 복사해서 빌드해 보고 확인해라.**

## 같이 고칠 것

```
· BURN-CHECKLIST 의 cp/cmp/git 블록 — 원본 경로가 바뀐다
· 원장 §23 불변식 — "굽기 입력 = 그 커밋의 조별과제샘플/client.ino" → 새 경로로
  🔴 **또 취소선으로 남겨라.** 08-19 이전 굽기는 옛 문장이 맞다 — 판본마다 참인 문장이 다르다
· 네 도구·문서 안의 옛 경로
```
⚠ **`arduino/client/`(잔재)와 `arduino/.basefw/`(기준선)는 건드리지 마라.** 이번 이사 대상이 아니다.

## 끝나면 알려라
**루트가 `.claude/protected.json` 의 잠금 경로를 새 경로로 바꾼다.** 그 전까지는 잠금이 옛 경로를 보고 있어
**노랑이 안 뜬다** — 이사 구간에는 잠금이 사실상 꺼져 있다는 뜻이다. **빨리 알려라.**

## 왜 필요한가

사용자가 server·ardu 폴더로 코드를 가르라고 지시했고 지금 세 도메인 소스가 평면으로 섞여 있다

## 대상 파일

- `조별과제샘플/client.ino`
## 완료 기준

git mv · 옛 경로 잔존 조회 · 이사 뒤 cmp 로 hex 0 확인 · BURN-CHECKLIST 와 §23 불변식 갱신 · 새 경로를 루트에 알림

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0265 --by arduino-engineer --note "<한 줄 요약>" -->

_(미처리)_

---

## ✅ 완료 — **이사 완료 · hex 차이 0 · 237 PASS** (2026-08-19)

```
이사 커밋  9d3c0f4 계열 → `git show --stat` 이 **5 files changed, 0 insertions(+), 0 deletions(-)**
           = git 이 **순수 rename 으로 인식**했다. 내용 무변경의 가장 강한 증거다
뒷정리     `8518a65` (경로 갱신 — 이사와 **갈라서** 커밋했다)
```

### 확인 ①~⑤ 전부

```
① git ls-files 옛 경로 → **0건** · 새 경로 → **5개** · rename 인식(0 insert/0 delete)
   sha256 다섯 개 이사 전후 동일: 1f7a498e · 11b7116e · 83eff77e · d1364abf · c78fe9a8
② hex 대조 → **cmp exit=0** (지금 칩 `move0819` 와 동일). 뒷정리 뒤 **한 번 더** 쟀다
   🔑 `__FILE__` 사용 0건이고 빌드는 `arduino/.burn/client` 스테이징에서 하므로
      **컴파일러가 보는 경로 자체가 안 바뀐다** — socket 의 `__FILE__` 사례는 여기 해당 없음
③ §23 불변식 → **재갱신**(취소선 겹쳐서). 하루에 두 번 바뀐 것도 적었다
④ BURN-CHECKLIST → `cp`/`cmp`/`git` 블록 + **0단계까지** 경로 갱신(11곳)
⑤ 전수 grep → 아래
```

### ⑤ 전수 grep — **셋으로 갈랐다**

```
✅ 고쳤다(내 것 · 지금 읽히는 것)
   arduino/test/host_test.cpp · stage1_test.cpp · avr/wdt.h   🔴 **시험이 실제로 깨져 있었다**
   docs/arduino/BURN-CHECKLIST.md · RECOVERY-after-move.md
   docs/arduino/LEDGER.md(§23) · 조별과제샘플/ardu/EspLink_*.h 넷(자기 주석)
🔒 안 고친다(이력) REQ 파일 다수 · 원장 과거 항목 · handoff · finding
   → **그때는 그게 참이었다. 고치면 이력이 거짓이 된다**
🔴 남의 소유(루트가 판단) — 아래
```

### 🔴 남의 소유라 손대지 않은 것 — **하나는 지금 고장이다**

```
🔴 .claude/protected.json   보호 목록이 **옛 경로**다 → **잠금이 조용히 무효**일 수 있다 (긴급)
🔴 .claude/ownership.json   `조별과제샘플/EspLink*` glob 이 새 경로를 안 잡는다
                            (`조별과제샘플/ardu/**` 규칙이 있으면 덮이지만 **확인이 필요**하다)
   CLAUDE.md · docs/*.md · docs/net/** · monitor/** · 조별과제샘플/README.md · server/server.cpp
```

### 발견 — **시험 하네스가 실제로 깨졌다**

`SKETCH_PATH` 가 옛 경로라 **컴파일이 멈췄다.** 이사 직후 시험을 안 돌렸으면
**다음 사람이 굽기 직전에 만났을 것이다.** 고쳐서 **237 PASS / 0 FAIL** 복구.

### 발견 — `RECOVERY-after-move.md` 에 **REQ-0264 에서 고친 문제가 남아 있었다**

살아 있는 절차인데(*"망이 바뀌면 이것만 보고 5분에 끝낸다"*) **헤더를 복사하지 않는 옛 형태**였다.
**BURN-CHECKLIST 만 고치고 이 문서를 놓쳤던 것이다** — 같은 결함이 문서 둘에 있었다(오늘 두 번째).

### 🔴 안 섞은 것 (루트 지시대로)

```
[ ] ESP_RST_WIRED 0 → 1   (사용자가 A2↔ESP RST 를 물렸다고 확인)
[ ] 배너 "10 slots" → 파생값
```
**둘 다 이번 커밋에 없다.** 창 R 판정 뒤 굽기와 함께 한다.

**굽지 않았고 포트도 안 잡았다.**

### 처리 완료 · arduino-engineer · 2026-08-19T12:46:19+0900

ardu/ 이사 완료 — git 이 순수 rename 인식(0 insert/0 delete), sha256 5개 동일, hex cmp exit=0, 237 PASS. 시험 하네스가 실제로 깨져 있어 고쳤다. .claude/protected.json 이 옛 경로라 잠금이 무효일 수 있다(루트 몫)

