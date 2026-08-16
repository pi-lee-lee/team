---
id: REQ-0099
title: server_device.h 복구 — 원문은 없지만 생성 스크립트가 트랜스크립트에 남아 있다 (서버 재기동 금지)
from: root
to: socket-engineer
status: claimed
created: 2026-08-16T16:02:25+0900
updated: 2026-08-16T16:03:09+0900
files: ["조별과제샘플/server_device.h", "조별과제샘플/server_wire.h", "조별과제샘플/server_seam.h"]
parent: none
---

# REQ-0099 · server_device.h 복구 — 원문은 없지만 생성 스크립트가 트랜스크립트에 남아 있다 (서버 재기동 금지)

**요청자** `root` → **담당** `socket-engineer`

## 요청 내용

사고로 사라진 서버 쪽 헤더를 되살린다. **원문 또는 생성 절차가 이미 확보돼 있다** — 새로 설계하지 마라.

전체 피해 범위·복구 재고는 `docs/incident-2026-08-16-git-clean.md` 에 있다. 먼저 읽어라.

## 🔴 절대 조건

`pid 36998` (`/tmp/srv_parking`, 21시간째 가동, 9991·9900·5500 리슨)은 **재현 불가능한 유일본**이다.
monitor-engineer 의 21시간 소크도 이 프로세스에 걸려 있다.
**헤더 복구가 끝나고 새 바이너리가 빌드·검증되기 전까지 이 프로세스를 죽이지 마라.**
교체가 필요해지면 그때 monitor-engineer 와 창을 잡아라(REQ-0097 때처럼).

## 1. `server_device.h` — 원문은 없지만 **만든 스크립트가 남아 있다**

네 이전 세션(`363b40c4`)의 12:01 Bash 호출에 생성 스크립트 전문이 있다. 손으로 쓴 헤더가 아니라
**그때의 `server.cpp`(2391행)에서 줄 구간을 잘라 붙인 것**이다:

```
L=open("server.cpp").read().splitlines(keepends=True)
def grab(a,b): return "".join(L[a-1:b])
sha1 = grab(193,254)   # 주석 + SHA1 struct
b64  = grab(255,284)   # base64 + ws_accept
cks  = grab(326,380)   # 체크섬 주석 + cksum/build_line/verify_line
guid = 'static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";'
# 헤더 = 주석블록 + guid + sha1 + b64 + cks  (가드: SERVER_DEVICE_H)
# server.cpp = L[:190] + [include 한 줄] + L[191:192] + L[284:325] + L[380:]
```

- 스크립트 전문(주석·헤더 서두 포함)을 다시 꺼내려면:
  `~/.claude/projects/-Users-idong-u-learn/363b40c4-6681-4c11-b2e9-762ec7a60627.jsonl` 의 **3270행** tool_use.
  같은 파일 3240·3278·3352·3383·3393행에 단계 A·B 진행 기록이 더 있다 — 인수인계로 써라.
- 잘라낸 조각(2391행 판)은 어디에도 없다. **줄 번호가 아니라 함수 이름으로** 떠서 재구성해라.
  후보 원본: `git show refs/claude/checkpoint-7fb8eb20:조별과제샘플/server.cpp` (2073행, 08-15 16:06).
- 헤더 서두 주석은 위 트랜스크립트에 그대로 있으니 **네가 쓴 원문을 그대로 복원**해라.
  왜 `.cpp` 가 아니라 헤더 전용인지(윈도우 빌드 명령을 모른다) 그 근거가 거기 적혀 있다.

## 2. `server_wire.h` · `server_seam.h` — 원문 확보됨

- `requests/recovery-2026-08-16/server_wire.h` — 11:21 실물 사본(`/private/tmp/ngbuild/client/` 에서 회수).
  아두이노가 실제로 이 판본으로 빌드·플래싱했으므로 **와이어 규약의 사실상 원본**이다.
- `requests/recovery-2026-08-16/server_seam.h` — 네 트랜스크립트의 Write 원문(3925자, 이후 Edit 없음).

지금 `server.cpp` 가 include 하는 것은 `server_device.h` 하나뿐이다(네가 검증한 대로).
나머지 둘은 제자리에 복원해 두되 include 를 새로 걸지는 마라 — 리팩터 단계 C 는 별도 판단이다.

## 3. 검증

1. `조별과제샘플/` 에 세 헤더가 있고 `git status` 에 **보이는지**(이제 무시되지 않는다) 확인.
2. 현재 `server.cpp`(2244행)와 함께 컴파일 — **경고 0**.
3. 기존 자가검증 + 실왕복 통과.
4. 빌드 산출물은 `/tmp` 등 저장소 밖에 두고, **운영 프로세스는 교체하지 마라**(위 절대 조건).

## 4. 끝나면

`req.sh done` 뒤 루트에 포인터 한 줄. 재구성이 원문과 다를 수밖에 없는 부분이 있으면
**어디가 왜 다른지** 처리 결과에 남겨라 — 나중에 이 헤더를 읽는 사람이 판단할 근거가 된다.

## 왜 필요한가

server.cpp 가 사라진 server_device.h 를 include 해서 빌드 자체가 안 된다. 지금 도는 pid 36998 이 죽으면 서버를 다시 못 띄운다 — 21시간 소크와 시연 경로가 동시에 사라진다.

## 대상 파일

- `조별과제샘플/server_device.h`
- `조별과제샘플/server_wire.h`
- `조별과제샘플/server_seam.h`
## 완료 기준

세 헤더가 조별과제샘플/ 에 있고 git status 에 보인다 · server.cpp 와 함께 경고 0 으로 컴파일된다 · 자가검증/실왕복 통과 · 운영 pid 36998 은 교체하지 않았다

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0099 --by socket-engineer --note "<한 줄 요약>" -->

_(미처리)_
