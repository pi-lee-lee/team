---
id: REQ-0266
title: 서버 소스를 조별과제샘플/server/ 로 이사 — ②-b 커밋 뒤 온전한 지점에서 (사용자 지시)
from: root
to: socket-engineer
status: done
created: 2026-08-19T11:16:39+0900
updated: 2026-08-19T15:15:57+0900
files: ["조별과제샘플/server.cpp"]
parent: none
---

# REQ-0266 · 서버 소스를 조별과제샘플/server/ 로 이사 — ②-b 커밋 뒤 온전한 지점에서 (사용자 지시)

**요청자** `root` → **담당** `socket-engineer`

## 요청 내용

# 서버 소스를 `조별과제샘플/server/` 로 옮긴다 (사용자 지시)

> 사용자: *"폴더를 생성하여 **server, ardu** 로 해당 폴더 내에 빌드파일들이 위치하도록 개선해달라."* / *"코드"*

## 옮길 것 (네 소유만)
```
조별과제샘플/server.cpp        →  조별과제샘플/server/server.cpp
조별과제샘플/server_wire.h     →  조별과제샘플/server/server_wire.h
조별과제샘플/server_device.h   →  조별과제샘플/server/server_device.h
조별과제샘플/server_seam.h     →  조별과제샘플/server/server_seam.h
```
✅ **소유권은 루트가 미리 열어 뒀다** — `조별과제샘플/server/**` → `socket-engineer`. 옛 경로도 아직 열려 있다.

⚠ **산출물·심볼 파일은 네 판단이다** — `server`·`server_test`·`server_test2`·`server.dSYM`.
**사용자가 "코드"라고 했으므로 소스가 대상이다.** 🔑 **다만 산출물이 저장소에 추적되고 있으면 그것부터 문제다** —
`server_test2` 가 추적 중이다. **무시목록으로 뺄지 네가 판단하고 근거를 적어라**(arduino 가 `.burn` 사본에서 한 것과 같은 형태).

## 🔴 시점 — **②-b 작업 중이면 지금 하지 마라**

**파일 이동은 진행 중인 편집과 최악으로 섞인다.** 반쯤 고친 상태로 옮기면 **어느 상태도 아닌 것**이 남는다.
```
✅ ②-b 를 커밋해서 트리가 온전한 지점 → 그때 이사 → 그 다음 ②-d
```
**순서는 네가 정해라. 다만 편집 중에는 하지 마라.**

## 🔴 `git mv` · 그리고 커밋 함정

```bash
git mv <옛> <새>
git commit -m "..." -- <경로들>
git ls-files 조별과제샘플/ | grep -E 'server\.cpp|server_.*\.h'   # 🔴 옛 경로가 없어야 한다
```
⚠ **§"커밋은 `commit` 에 경로를 준다"의 삭제 판본** — `git mv` 는 삭제+추가이고 **경로 명시 커밋이 작업 트리를 취하므로 옛 경로가 되살아날 수 있다.** **상태를 바꾼 뒤 조회해라.**

## 같이 고칠 것
```
· 빌드 명령·스크립트 안의 경로 (네 것 · 시험 인스턴스 기동 포함)
· #include 상대 경로 — 넷이 같이 움직이므로 그대로일 것이다. **확인해라**
· 네 원장·명세의 경로
· 🔴 **배포 절차서** — 빌드 원본 경로가 바뀐다
```
⚠ **`data_log.json`·`index.html` 은 네 것이 아니다. 건드리지 마라.**

## 🔴 산출물이 바뀌면 안 된다
**경로만 바뀌고 내용이 그대로면 산출물도 그대로여야 한다.** 다만 **빌드 명령의 `-I`·상대 경로가 바뀌면
`__FILE__`·디버그 경로가 산출물에 박힐 수 있다.** **재고, 다르면 왜인지 적어라** — 네가 오늘 세운 그 규율이다.

## 끝나면 알려라
루트가 `.claude/ownership.json` 의 옛 평면 규칙(`조별과제샘플/server*`)을 정리하고,
`0263` 추출이 끝나면 그 경로를 `protected.json` 에 넣는다.

## 왜 필요한가

사용자가 server·ardu 폴더로 코드를 가르라고 지시했고 지금 세 도메인 소스가 평면으로 섞여 있다

## 대상 파일

- `조별과제샘플/server.cpp`
## 완료 기준

편집 중 아닌 지점에서 git mv · 옛 경로 잔존 조회 · 산출물 대조(다르면 왜) · 빌드·배포 절차 경로 갱신 · 추적된 산출물 처리 판단

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0266 --by socket-engineer --note "<한 줄 요약>" -->

_(미처리)_

### 처리 완료 · socket-engineer · 2026-08-19T15:15:57+0900

이사 완료 — 조별과제샘플/server/ 로 git mv. 🔴 '.o 가 0 차이'로 아무것도 안 바뀐 것을 증명했다(실행파일은 LC_UUID+서명으로 매 링크 다르다). 지금 그 디렉터리에 server.cpp·main.cpp·node.h·zone.h·server_seam.h·server_device.h·server_wire.h

