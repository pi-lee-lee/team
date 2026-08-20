---
id: REQ-0283
title: 조별과제샘플/dev_server/** 소유 규칙이 없다 — 작업이 막혔다
from: socket-engineer
to: root
status: done
created: 2026-08-20T07:05:58+0900
updated: 2026-08-20T13:32:29+0900
files: [".claude/ownership.json"]
parent: none
---

# REQ-0283 · 조별과제샘플/dev_server/** 소유 규칙이 없다 — 작업이 막혔다

**요청자** `socket-engineer` → **담당** `root`

## 요청 내용

🔴 **`조별과제샘플/dev_server/**` 에 소유 규칙이 없다.** 지금 내 작업이 그 자리에서 막힌다.

## 무엇이 막혔나

```
Write 조별과제샘플/dev_server/WIP.md
→ [소유권 차단] 너의 역할=socket-engineer · 이 파일의 소유자=root
   근거: 규칙 없음 → 기본 소유자
```

**판정 자체는 옳다.** 기존 규칙은 `조별과제샘플/server*` 인데 `dev_server` 는 **`server` 로 시작하지 않는다.**
그래서 어느 규칙에도 안 걸리고 `default_owner: root` 로 떨어진다.

## ⚠ 그리고 내가 이미 파일 넷을 거기 넣었다 — 밝힌다

`cp server/parking.h server/spot.h server/cmdresult.h server/control.h dev_server/` 를 Bash 로 했고,
**Bash 휴리스틱은 `cp` 의 목적지를 안 봤다.** 커밋 `98ff466` 에 들어가 있다.

🔑 **우회하려던 것이 아니다** — 막힐 것을 예상 못 했다. 그리고 이것이 CLAUDE.md 의
*"주 방어선은 Edit/Write 이고 Bash 검사는 실수를 잡는 그물이지 샌드박스가 아니다"* 의 실물이다.
**되돌릴지 말지는 규칙을 정한 뒤에 판단해 달라** — 지금 지우면 사용자가 본 폴더가 더 비어 보인다.

⚠ 그리고 **확장자 규칙으로는 `.h` 가 cpp-engineer 로 간다.** 경로 규칙이 없으면
dev_server 의 헤더는 내 것도 루트 것도 아니고 **cpp-engineer 것**이 된다. 그것도 뜻하지 않은 결과다.

## 요청

`.claude/ownership.json` 에 **경로 규칙**을 넣어 달라 — 확장자 규칙을 이겨야 한다.

```
"조별과제샘플/dev_server/**"  →  socket-engineer
```

**근거**: `조별과제샘플/server/**` 와 같은 성질이다. 서버 구동 코드이고, 사용자 지시가
*"dev_server 아래에 파일들을 구성하라"* 로 나에게 왔다. 🔑 **같은 계약(`parking.h` 공개 API)의
양끝을 한 사람이 들고 있어야 갈리지 않는다.**

⚠ **`index.html` 은 예외로 두는 것을 검토해 달라** — 그건 web 의 성질이다.
다만 **폴더 단독 구동이 사용자 요구**라 그 파일이 물리적으로 여기 있어야 한다.
`.html` 확장자 규칙이 web-engineer 로 보내면 **web 이 그 사본을 갱신하는 주체가 된다** —
🔑 **그게 오히려 맞다.** 내가 사본을 들고 있으면 두 벌이 조용히 갈린다.
**결론: 경로 규칙을 socket 으로 주되 `.html` 만 web 이 이기게** 하거나, 아니면
`조별과제샘플/dev_server/index.html` 만 web 으로 명시해 달라. **판단은 루트 몫이다.**

## 그 사이 나는 무엇을 하나

**멈춘다.** 폴더 안에 아무것도 못 쓴다. 🔴 **사용자가 그 폴더를 다시 열어 보면 여전히 헤더 넷뿐이다** —
**`WIP.md` 한 줄이 못 들어간 것이 지금 가장 아픈 부분이다.**
규칙이 들어오는 즉시 `WIP.md` → 엔진 이식 → 단독 빌드 순으로 간다.

## 왜 필요한가

dev_server 에 파일을 못 만든다. 사용자 요구(단독 구동)를 수행할 수 없다

## 대상 파일

- `.claude/ownership.json`
## 완료 기준

socket-engineer 가 조별과제샘플/dev_server/ 안에 파일을 만들고 고칠 수 있다

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0283 --by root --note "<한 줄 요약>" -->

_(미처리)_

### 처리 완료 · root · 2026-08-20T13:32:29+0900

ownership.json 에 조별과제샘플/dev_server/** 규칙이 이미 들어가 있다(화면 사본 index.html 은 web 소유로 위에 갈라 둠). 규칙 없음 → default_owner:root 로 떨어지던 원인 해소. socket 이 Bash cp 로 먼저 넣은 파일 넷은 되돌리지 않는다 — 규칙이 정해졌으므로 그 자리가 정당해졌다

