# 사고 기록 — 2026-08-16 `git clean` 으로 untracked 파일 대량 삭제

> 작성: 루트(root), 2026-08-16 16:00 · 재기동 직후 조사
> 이 문서가 **피해 범위와 복구 재고(在庫)의 원본**이다. 조치 전에 이것부터 읽어라.

## 1. 무슨 일이 있었나

`team/bin/req.sh new --body "..."` 에 들어간 **셸 명령 치환이 실제로 실행**됐다.
그중에 `git clean` 이 있었고, **추적되지 않던(untracked) 파일이 통째로 사라졌다.**

두 가지가 겹쳐 피해를 키웠다.

1. **`.gitignore` 44행이 `조별과제샘플/server_*` 였다.** 바이너리만 막으려던 규칙이
   `server_wire.h`·`server_seam.h`·`server_device.h` 같은 **소스까지 무시**했다.
   `git status` 에 안 보이니 아무도 추적 누락을 몰랐고, `clean` 이 돌자 같이 지워졌다.
   (지금은 확장자를 가려 바이너리만 막도록 좁혀져 있다.)
2. **오늘(08-16) 만든 것은 체크포인트에도 없다.** `refs/claude/checkpoint-*` 는
   **08-15 16:06 이 마지막**이다. 그 이후 산출물은 git 어디에도 없었다.

15:48 에 팀 전체(tmux `learnteam`)가 재기동됐다. 사고 이전 세션의 **대화 문맥은 사라졌다** —
monitor 만 재기동 전에 인수인계를 남겼고, socket·arduino 는 남기지 못했다.

## 2. 지금 살아 있는 것 — **건드리지 마라**

| 대상 | 상태 | 주의 |
|---|---|---|
| 주차 서버 프로세스 | **pid 36998 `/tmp/srv_parking`, 21시간째 가동** · 9991·9900·5500 리슨 | 🔴 **재기동 금지.** 소스가 아직 안 돌아왔으므로 지금 죽이면 **다시 못 띄운다.** 21시간 소크도 같이 끊긴다 |
| ESP/UNO 실장 펌웨어 | EspLink 판본이 칩에 올라가 있음 | 저장소에는 없던 판본이다. 재플래싱은 소스 복구 후에 |
| `조별과제샘플/server.cpp` | 2244행(리팩터 후) — 추적 파일이라 살아남음 | 🔴 **`server_device.h` 를 include 한다 → 지금은 컴파일 불가** |
| `조별과제샘플/client.ino` | 08-15 판본(EspLink 아님), **uncommitted** | 지난 세션 최고가치 발견(SERVER_IP 앞 공백 → DNS Fail)이 여기에만 있다 |

## 3. 잃은 것과 복구 재고

복구본은 전부 **`requests/recovery-2026-08-16/`** 에 모아 뒀다(공용 경로).
출처는 셋이다 — ① 세션 트랜스크립트(`~/.claude/projects/-Users-idong-u-learn/*.jsonl`)의
`Write`/`Edit` 도구 호출을 시간순 재생, ② 아두이노 빌드 임시 디렉터리 `/private/tmp/ngbuild/`
(**실제 플래싱된 실물**), ③ git 체크포인트 ref.

| 잃은 것 | 복구 | 어디에 |
|---|---|---|
| `monitor/**` 집계 도구 9개 | ✅ 트랜스크립트 재생(Write + 이후 Edit 적용) | `recovery-2026-08-16/monitor-*.py` |
| `조별과제샘플/nextgen/client.ino` | ✅ **플래싱된 실물** (11:26) | `recovery-2026-08-16/nextgen-client.ino` |
| `arduino/esplink/EspLink.h` | ✅ **플래싱된 실물** (11:26) | `recovery-2026-08-16/EspLink.h` |
| `조별과제샘플/server_wire.h` | ✅ 실물 사본 (11:21) · **원저작과 바이트 일치 확인** | `recovery-2026-08-16/server_wire.h` |
| `조별과제샘플/server_seam.h` | ✅ Write 원문 + **뒤늦은 Bash 수정 1건 적용** | `recovery-2026-08-16/server_seam.h` |
| `arduino/aptest/aptest.ino` | ✅ 체크포인트 | `git show refs/claude/checkpoint-7fb8eb20:arduino/aptest/aptest.ino` |
| `arduino/baudscan/baudscan.ino` | ✅ 체크포인트 | 위와 같음 |
| `requests/2026-08-13,14/` (REQ-0001~0061) | ⚠ 일부만 | 체크포인트에 남은 것만. 원장 `requests/INDEX.md` 는 추적 파일이라 무사 |
| **`조별과제샘플/server_device.h` (175행)** | ⚠ **원문 없음 · 재생성 절차는 확보** | 아래 4절 |

### 복구 시 알아둘 것

- `monitor-tick.py` 는 마지막 Edit 1건(02:37Z, 228B)이 적용되지 않았다. 나머지는 전부 적용됐다.
- 트랜스크립트 재생본은 **도구 호출 기록**이지 파일 실물이 아니다. Bash 로 고친 부분은 잡히지 않는다.
  실행해서 확인해라.
- **그 함정이 실제로 하나 있었다.** `server_seam.h` 는 Write(12:03) 이후 `Edit` 이 0건이라
  완전해 보였지만, 14:08 에 **python heredoc(Bash)으로 주석 블록이 더 붙었다**
  (`363b40c4` jsonl 3347행). 지금 복원본에는 그 변경까지 적용해 뒀다.
  → **"Edit 이 없으니 완전하다"는 추론은 틀린다.** 같은 세션의 Bash 호출까지 훑어야 한다.
- 반대로 `server_wire.h` 는 안전이 확인됐다. socket 이 08-15 20:24 에 쓴 원저작과
  아두이노 빌드 트리의 11:21 사본을 **diff 해서 완전 일치**(rc=0)했다 — 소비자 쪽 사본이
  갈라진 게 아니라 같은 파일이다.

## 4. `server_device.h` — 원문은 없지만 **만든 절차가 남아 있다**

socket-engineer 세션 `363b40c4` 의 12:01(KST) Bash 호출에 **생성 스크립트 전문**이 있다.
헤더는 손으로 쓴 게 아니라 **그때의 `server.cpp`(2391행)에서 줄 구간을 잘라낸 것**이다.

```
python3 - <<'PYEOF'
L=open("server.cpp").read().splitlines(keepends=True)
def grab(a,b): return "".join(L[a-1:b])
sha1 = grab(193,254)   # SHA1
b64  = grab(255,284)   # base64 + ws_accept
cks  = grab(326,380)   # cksum/build_line/verify_line
...
out = L[:190] + [include 한 줄] + L[191:192] + L[284:325] + L[380:]
```

즉 **잘라낸 조각 = 지금 `server.cpp` 에 없는 부분**이고, 그 조각들은
SHA-1·base64·체크섬 같은 **오래 안 변한 유틸**이다. 체크포인트의 `server.cpp`(2073행)에
같은 함수가 그대로 있으므로, **줄 번호가 아니라 함수 이름으로 떠서** 헤더를 재구성하면 된다.

⚠ **행 수 두 개가 어긋난다.** socket 의 진행 보고는 "175행"이라 했지만, 위 스크립트로 계산하면
`server.cpp` 에서 빠진 것은 **147행**이다(2391 − 2244 = 147, `L[:190]+1+1+41+나머지` 와 일치).
차이는 헤더에 새로 쓴 주석·include·가드다. **175 를 맞춰야 할 목표치로 삼지 마라** — 기준은 행 수가
아니라 **컴파일이 되고 자가검증이 통과하는가**다.

- 재구성 원본 후보: `git show refs/claude/checkpoint-7fb8eb20:조별과제샘플/server.cpp`
- 검증: 재구성 후 `server.cpp`(2244행)와 함께 컴파일 → 경고 0 + 기존 자가검증 통과
- 전문(全文)은 다음으로 다시 꺼낼 수 있다:
  `~/.claude/projects/-Users-idong-u-learn/363b40c4-*.jsonl` 의 3270행 tool_use

## 5. 재발 방지 — 이미 한 것과 남은 것

- [x] `.gitignore` 44행을 좁혀 소스가 무시되지 않게 함(REQ-0054)
- [x] `.claude/hooks/req_protocol_guard.py` — `--body` 셸 치환 차단 훅
- [x] 살아남은 untracked 파일을 **인덱스에 올림**(`git add`) — 훅 자신도 untracked 였다
- [ ] **커밋**. 인덱스에 올린 것은 `git clean` 은 피하지만 `git checkout`·`reset --hard` 는 못 피한다
- [ ] REQ ID 카운터가 **파일 스캔 기반**이라 파일이 사라지자 번호가 되감겼다
      (0054~0057 이 08-15 것과 중복). 추적되는 카운터로 바꿔야 한다

## 6. 원장(ledger) 정합성

`requests/2026-08-15/REQ-00xx.md` 는 **08-15 16:06 스냅샷 복원본**이라 그 이후 기록이 없다.
그래서 `team-status.sh` 가 **이미 끝난 요청을 `claimed`·`stalled` 로 보고**한다.

**규칙: `requests/INDEX.md`(추적됨)가 복원된 md 프런트매터를 이긴다.**
- REQ-0077 / REQ-0078 → INDEX 상 08-15 에 **완료**. 파일만 옛날 상태다.
- 08-16 의 REQ-0054~0057 은 **08-15 의 같은 번호와 다른 요청**이다. 번호를 고치지 않는다
  (원장은 append-only). 파일에 주석으로 구분만 남긴다.
