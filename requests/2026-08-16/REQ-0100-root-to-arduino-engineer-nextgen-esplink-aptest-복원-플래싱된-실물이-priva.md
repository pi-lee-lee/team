---
id: REQ-0100
title: nextgen·EspLink·aptest 복원 — 플래싱된 실물이 /private/tmp/ngbuild 에 남아 있다 (REQ-0078 충돌 해소)
from: root
to: arduino-engineer
status: done
created: 2026-08-16T16:02:35+0900
updated: 2026-08-16T16:06:53+0900
files: ["조별과제샘플/nextgen/client.ino", "arduino/esplink/EspLink.h", "arduino/aptest/aptest.ino", "arduino/baudscan/baudscan.ino"]
parent: none
---

# REQ-0100 · nextgen·EspLink·aptest 복원 — 플래싱된 실물이 /private/tmp/ngbuild 에 남아 있다 (REQ-0078 충돌 해소)

**요청자** `root` → **담당** `arduino-engineer`

## 요청 내용

사고로 사라진 아두이노 쪽 산출물을 되살린다. **전부 원문이 확보돼 있다** — 다시 쓰지 마라.

전체 피해 범위·복구 재고는 `docs/incident-2026-08-16-git-clean.md` 에 있다. 먼저 읽어라.

## 1. 복원할 것과 그 원본

| 복원 대상 | 원본 | 비고 |
|---|---|---|
| `조별과제샘플/nextgen/client.ino` | `requests/recovery-2026-08-16/nextgen-client.ino` | 11:26 실물 |
| `arduino/esplink/EspLink.h` | `requests/recovery-2026-08-16/EspLink.h` | 11:26 실물 |
| `arduino/aptest/aptest.ino` | `git show refs/claude/checkpoint-7fb8eb20:arduino/aptest/aptest.ino` | 08-15 16:06 |
| `arduino/baudscan/baudscan.ino` | 같은 체크포인트 | 위와 같음 |

앞의 둘은 **아두이노 빌드 임시 디렉터리 `/private/tmp/ngbuild/client/` 에서 회수한 실물**이다.
같은 폴더의 `out*/client.ino.hex` 가 **지금 칩에 올라가 있는 바이너리**이므로,
이 소스는 "복원본"이 아니라 **실장 펌웨어와 일치하는 원본**으로 취급해도 된다.
(트랜스크립트에도 08-15 판 EspLink.h 9800자가 있지만 **더 옛것이다. 쓰지 마라.**)

`/private/tmp` 는 언제 비워질지 모른다. **저장소 안으로 옮기는 것이 이 요청의 핵심**이다.

## 2. 확인할 것

1. 복원 후 `git status` 에 **보이는지** 확인하고 `git add` 로 인덱스에 올려라.
   `.gitignore` 는 이미 고쳐졌지만, 추적되지 않으면 다음 `clean` 에 또 사라진다.
   **`docs/` 안이라 안전한 게 아니라 추적돼서 안전한 것**이다(monitor 가 같은 교훈을 남겼다).
2. 복원한 `nextgen/client.ino` 를 **빌드해서 통과하는지** 확인해라. 업로드는 하지 마라 —
   지금 칩에 도는 것과 같은 소스이므로 재플래싱할 이유가 없고, 소크 중이다.
3. `조별과제샘플/client.ino`(EspLink 이전 판, uncommitted)는 **건드리지 마라.**
   커밋 여부는 루트가 사용자에게 확인 중이다.

## 3. 네가 올린 질문에 대한 답

- **REQ-0078 과의 충돌은 해소됐다.** REQ-0078 은 08-15 에 이미 **완료**된 요청이고,
  네가 본 `claimed` 는 16:06 스냅샷 복원본이라 그 뒤 기록이 없어서 그렇게 보인 것이다.
  원장(`requests/INDEX.md`)이 복원본을 이긴다 — 파일 상태를 `done` 으로 바로잡아 뒀다.
  **REQ-0078 은 무시하고 이 요청을 하라.**
- **동결 판단은 옳았다.** 충돌하는 지시 둘을 받았을 때 멈추고 물은 것은 정확한 처신이다.

## 4. socket-engineer 에게 전할 것

DNS Fail 의 원인이 서버가 아니라 **펌웨어의 SERVER_IP 앞 공백**이었다는 발견 — 네가 직접
`req.sh new --from arduino-engineer --to socket-engineer` 로 넘겨라. 루트를 거칠 필요 없다.
다만 socket 은 지금 헤더 복구 중이니 **급하지 않다고 명시**해라.

## 왜 필요한가

지금 칩에 도는 펌웨어의 소스가 저장소에 없다. /private/tmp 사본은 언제 비워질지 모르고, 사라지면 실장 펌웨어를 재현할 방법이 없다.

## 대상 파일

- `조별과제샘플/nextgen/client.ino`
- `arduino/esplink/EspLink.h`
- `arduino/aptest/aptest.ino`
- `arduino/baudscan/baudscan.ino`
## 완료 기준

네 파일이 제자리에 있고 git status 에 보이며 git add 로 인덱스에 올라 있다 · nextgen/client.ino 빌드 통과(업로드는 하지 않는다) · 조별과제샘플/client.ino 는 손대지 않았다

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0100 --by arduino-engineer --note "<한 줄 요약>" -->

**완료 · arduino-engineer · 2026-08-16**

### ★ 실장 펌웨어와의 일치를 *증명*했다 — 믿는 게 아니라 확인한 것

복원본을 다시 컴파일해서 나온 hex 가 **실제로 칩에 구워진 hex 와 바이트 단위로 동일**하다.

```
diff /private/tmp/ngbuild/out4/client.ino.hex  /tmp/ngverify/out/client.ino.hex → 차이 없음
diff /private/tmp/ngbuild/client/client.ino    조별과제샘플/nextgen/client.ino   → 차이 없음
diff /private/tmp/ngbuild/client/EspLink.h     arduino/esplink/EspLink.h        → 차이 없음
```

소스가 같을 뿐 아니라 **같은 바이너리로 재현된다**는 뜻이다. 즉 이제 저장소만으로
지금 도는 펌웨어를 언제든 다시 만들 수 있다. `/private/tmp` 가 비워져도 잃을 것이 없다.

### 복원한 네 파일 (전부 인덱스에 올림)

```
A  조별과제샘플/nextgen/client.ino    19383B   ← /private/tmp 실물 (11:26)
A  arduino/esplink/EspLink.h          22395B   ← /private/tmp 실물 (11:26)
A  arduino/aptest/aptest.ino          18039B   ← refs/claude/checkpoint-7fb8eb20
A  arduino/baudscan/baudscan.ino       5346B   ← 같은 체크포인트
```

`git status` 에 정상적으로 보였고(`??` → `git add` → `A`), **더 이상 clean 에 쓸려나가지 않는다.**
EspLink.h 는 22395B 판을 썼다 — 요청이 경고한 트랜스크립트의 9800자 08-15 판이 아니다.

### 빌드 통과 ✅ (업로드는 하지 않았다)

```
arduino-cli compile --fqbn arduino:avr:uno
스케치   12052 / 32256 (37%)
전역변수   823 /  2048 (40%)   지역 여유 1225
```

참고로 **EspLink 이전 판(`조별과제샘플/client.ino`)은 19706B(61%)·1017B(49%)** 였다.
새 판이 플래시 24%p·RAM 9%p 더 가볍다.

**업로드하지 않았다.** 지시대로이며, 소크(pid 36998, 21시간째)를 건드리지 않았다.

### ⚠ 보고할 구조 문제 — 이 스케치는 저장소 경로 그대로는 빌드되지 않는다

`nextgen/client.ino:25` 가 `#include "EspLink.h"` (**따옴표 = 스케치 폴더 기준 로컬 include**)인데
요청이 지정한 복원 위치는 `arduino/esplink/EspLink.h` 라 **서로 다른 디렉터리에 있다.**
게다가 arduino-cli 는 **폴더명 == 스케치명**을 요구하는데 `nextgen/client.ino` 는 이것도 어긴다.

그래서 위 빌드는 `/tmp/ngverify/client/` 에 **둘을 같은 폴더로 모아서** 얻은 것이다
(원래 `/private/tmp/ngbuild/client/` 도 정확히 그 배치였다 — 그래서 hex 가 재현됐다).

즉 **지금 배치는 "보관은 되지만 그대로는 빌드가 안 되는" 상태**다. 요청이 지정한 경로를
임의로 바꾸지 않았고 `조별과제샘플/` 은 공용 표면이라 더더욱 건드리지 않았다.
정리 방향(심볼릭 링크 / 스케치 폴더 통합 / `--library` 사용)은 **루트가 정할 일**이라 남겨 둔다.

### 지시 준수 확인

- `조별과제샘플/client.ino` — **손대지 않았다.** 여전히 `M`(uncommitted) 그대로다.
- 서버 프로세스(pid 36998)·시리얼 캡처 — 건드리지 않았다.
- 파괴적 명령(`git clean`·`git checkout -- .`·`rm -rf`) — 하나도 쓰지 않았다.
- `git add` 까지만 했고 **커밋은 하지 않았다**(요청 범위가 인덱스까지였다).
  다만 사고 기록 5절이 지적했듯 인덱스는 `clean` 은 피해도 `checkout`·`reset --hard` 는 못 피한다.

### socket-engineer 통보

DNS Fail 원인(SERVER_IP 앞 공백) 건은 **REQ-0102 로 발행**했다. 급하지 않다고 명시했다.

### 루트가 치워 줄 것 하나 — `requests/body-tmp-arduino-0101.md`

REQ-0102 본문을 `--body-file` 로 넘기려고 만든 임시 파일이다(규약대로 `--body` 를 쓰지 않았다).
발행 후 지우려 했으나 **소유권 훅이 `rm` 을 차단**했다(`requests/` 의 상위 판정이 root).
**우회하지 않고 그대로 뒀다.** 내용은 REQ-0102 에 이미 들어갔으므로 이 파일은 지워도 된다.

참고로 `/tmp` 에 본문을 쓰려던 첫 시도도 훅이 막았다("프로젝트 밖 경로 = root 소유").
에이전트 정의는 `--body-file /tmp/...` 를 예시로 들고 있는데 **실제로는 /tmp 에 쓸 수 없다.**
정의를 고치거나 훅을 조정하는 편이 좋겠다 — 다음 에이전트도 똑같이 막힌다.

### 처리 완료 · arduino-engineer · 2026-08-16T16:06:53+0900

4파일 복원+git add 완료. 재컴파일 hex가 실장 hex와 바이트 동일-실장펌웨어 재현 증명. 빌드 12052/32256(37%). 업로드 안함. client.ino 미변경

