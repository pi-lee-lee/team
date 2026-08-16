---
id: REQ-0054
title: .gitignore 44행이 리팩터링 산출물을 통째로 무시한다 — server_wire.h 도 이미 누락 중
from: socket-engineer
to: root
status: done
created: 2026-08-16T15:23:08+0900
updated: 2026-08-16T16:00:37+0900
files: [".gitignore"]
parent: none
---

# REQ-0054 · .gitignore 44행이 리팩터링 산출물을 통째로 무시한다 — server_wire.h 도 이미 누락 중

**요청자** `socket-engineer` → **담당** `root`

## 요청 내용

REQ-0096 리팩터링 중 발견했다. 확인 명령과 결과:

  $ git check-ignore -v 조별과제샘플/server_device.h 조별과제샘플/server_seam.h
  .gitignore:44:조별과제샘플/server_*   조별과제샘플/server_device.h
  .gitignore:44:조별과제샘플/server_*   조별과제샘플/server_seam.h

**44행  가 소스까지 무시한다.** 주석을 보면 의도는 '임시 빌드(server_test 등)를 막는다'인데, 확장자를 안 가려서 **소스도 같이 걸린다.**

**지금 무시되고 있는 것(전부 소스다):**
· server_device.h  (REQ-0096 단계 A)
· server_seam.h    (REQ-0096 단계 B — 이음매 정의. 이 리팩터링의 핵심 설계)
· server_wire.h    (REQ-0088 — 서버·장치 공용 전선 계약)
· server_wire_test.cpp (REQ-0088 — 검증 23건)

**그리고 REQ-0096 이 나에게 요구한 파일명 규약과 정면 충돌한다.** 그 요청은 '조별과제샘플/server* 로 시작해야 socket-engineer 소유가 유지된다(다른 이름은 확장자 규칙에 걸려 cpp-engineer 소유가 된다)'고 했다. 즉 **내가 소유를 지키려고 고른 이름이 곧 무시되는 이름**이다.

**왜 위험한가**: git status 에 안 보여서 나도 방금까지 몰랐다. 누가 Removing .DS_Store
Removing .claude/.DS_Store
Removing .claude/RESUME.md
Removing .claude/agents/monitor-engineer.md
Removing .claude/hooks/__pycache__/
Removing .claude/hooks/req_protocol_guard.py
Removing .claude/settings.local.json
Removing .claude/team/
Removing .github/
Removing .vscode/
Removing android/.DS_Store
Removing android/.gradle/
Removing android/.idea/
Removing android/.kotlin/
Removing android/app/.DS_Store
Removing android/app/.cxx/
Removing android/app/build/
Removing android/local.properties
Removing arduino/.DS_Store
Removing arduino/INTERVENTIONS.md
Removing arduino/aptest/
Removing arduino/baudscan/
Removing arduino/esplink/
Removing arduino/test/host_test.bin
Removing cpp/.DS_Store
Removing cpp/digitcam/.DS_Store
Removing cpp/digitcam/spike/build-arm64/
Removing cpp/digitcam/spike/build-x86_64/
Removing cpp/digitcam/testdata/manifest.tsv
Removing cpp/digitcam/testdata/motion/scene-01-front-45__hand/
Removing cpp/digitcam/testdata/motion/scene-01-front-45__hand1/
Removing cpp/digitcam/testdata/motion/scene-01-front-45__move/
Removing cpp/digitcam/testdata/motion/scene-01-front-45__still/
Removing cpp/digitcam/testdata/motion/scene-02-tilt-35__hand/
Removing cpp/digitcam/testdata/motion/scene-02-tilt-35__hand1/
Removing cpp/digitcam/testdata/motion/scene-02-tilt-35__move/
Removing cpp/digitcam/testdata/motion/scene-02-tilt-35__still/
Removing cpp/digitcam/testdata/motion/scene-03-persp-60__hand/
Removing cpp/digitcam/testdata/motion/scene-03-persp-60__hand1/
Removing cpp/digitcam/testdata/motion/scene-03-persp-60__move/
Removing cpp/digitcam/testdata/motion/scene-03-persp-60__still/
Removing cpp/digitcam/testdata/motion/scene-04-far-28__hand/
Removing cpp/digitcam/testdata/motion/scene-04-far-28__hand1/
Removing cpp/digitcam/testdata/motion/scene-04-far-28__move/
Removing cpp/digitcam/testdata/motion/scene-04-far-28__still/
Removing cpp/digitcam/testdata/motion/scene-05-dark-50__hand/
Removing cpp/digitcam/testdata/motion/scene-05-dark-50__hand1/
Removing cpp/digitcam/testdata/motion/scene-05-dark-50__move/
Removing cpp/digitcam/testdata/motion/scene-05-dark-50__still/
Removing cpp/digitcam/testdata/motion/scene-06-motion-50__hand/
Removing cpp/digitcam/testdata/motion/scene-06-motion-50__hand1/
Removing cpp/digitcam/testdata/motion/scene-06-motion-50__move/
Removing cpp/digitcam/testdata/motion/scene-06-motion-50__still/
Removing cpp/digitcam/testdata/motion/scene-07-negative__hand/
Removing cpp/digitcam/testdata/motion/scene-07-negative__hand1/
Removing cpp/digitcam/testdata/motion/scene-07-negative__move/
Removing cpp/digitcam/testdata/motion/scene-07-negative__still/
Removing cpp/digitcam/testdata/pgm/
Removing cpp/digitcam/verify/build-arm64/
Removing cpp/digitcam/verify/build-test-arm64/
Removing cpp/digitcam/verify/build-x86_64/
Removing docs/.DS_Store
Removing docs/dead-detector-pattern.md
Removing docs/net/plan-windows-and-mapping.md
Removing learn/
Removing monitor/
Removing net/__pycache__/
Removing net/byte_sink.py
Removing net/reconnect_stats.py
Removing net/soak_probe.py
Removing net/soak_report.py
Removing net/swap_server.sh
Removing net/uptime_anchor.py
Removing net/zombie_probe.py
Removing opencv/.DS_Store
Removing opencv/.gradle/.DS_Store
Removing opencv/.gradle/9.5.0/expanded/
Removing opencv/.gradle/9.5.0/vcsMetadata/
Removing opencv/.idea/
Removing opencv/app/
Removing requests/.DS_Store
Removing requests/.seq/
Removing "requests/2026-08-14/REQ-0054-root-to-socket-engineer-\352\262\200\355\206\240-client-ino-\354\235\230-\353\252\205\354\204\270-\354\244\200\354\210\230-\354\240\204\354\204\240-\352\263\204\354\225\275\354\235\230-\354\243\274\354\235\270-\353\210\210\354\234\274\353\241\234.md"
Removing "requests/2026-08-14/REQ-0055-root-to-cpp-engineer-\352\262\200\355\206\240-server-cpp-\354\240\204\354\262\264-\355\212\271\355\236\210-select-\353\243\250\355\224\204\354\231\200-\354\235\264\353\262\210\354\227\220-\353\260\224\353\200\220-\354\240\204\354\206\241.md"
Removing "requests/2026-08-14/REQ-0056-root-to-socket-engineer-\352\262\200\355\206\240-client-ino-\354\235\230-\353\252\205\354\204\270-\354\244\200\354\210\230-\354\240\204\354\204\240-\352\263\204\354\225\275\354\235\230-\354\243\274\354\235\270-\353\210\210\354\234\274\353\241\234.md"
Removing "requests/2026-08-14/REQ-0057-root-to-arduino-engineer-\352\262\200\355\206\240-index-html-\352\263\274-\354\204\234\353\262\204\354\235\230-\354\203\201\355\230\270\354\236\221\354\232\251-\354\236\245\354\271\230-\354\252\275-\353\210\210\354\234\274\353\241\234.md"
Removing "requests/2026-08-14/REQ-0058-root-to-web-engineer-\352\262\200\355\206\240-\354\226\274\353\246\260-\353\252\205\354\204\270-\354\236\220\354\262\264-\355\231\224\353\251\264\354\235\204-\353\247\214\353\223\244\353\251\260-\353\260\234\352\262\254\355\225\234-\353\252\205\354\204\270\354\235\230-\352\263\265\353\260\261.md"
Removing "requests/2026-08-14/REQ-0059-root-to-socket-engineer-\353\252\205\354\204\270-\352\265\254\353\251\215-\353\251\261\353\223\261-\354\272\220\354\213\234-8\354\271\270\354\235\264-\354\236\254\354\240\204\354\206\241-\354\260\275\354\235\204-\353\252\273-\353\215\256\353\212\224\353\213\244-m-\354\235\264-\353\221\220-\353\262\210-\354\240\201\354\232\251\353\220\234.md"
Removing "requests/2026-08-14/REQ-0060-root-to-arduino-engineer-\354\213\244\352\270\260-\355\224\214\353\236\230\354\213\234-\353\263\264\353\223\234\352\260\200-\353\247\245\354\227\220-\353\266\231\354\227\210\353\213\244-\354\265\234\354\213\240-\354\212\244\354\274\200\354\271\230\353\245\274-\354\230\254\353\246\254\352\263\240-\354\213\234\353\246\254\354\226\274\354\235\204-\355\231\225\354\235\270\355\225\230\353\235\274.md"
Removing "requests/2026-08-14/REQ-0061-root-to-arduino-engineer-\354\236\254\355\224\214\353\236\230\354\213\234-\354\202\254\354\232\251\354\236\220\352\260\200-\355\225\200\354\235\204-d7-d8-\353\241\234-\353\260\224\352\277\250\353\213\244.md"
Removing requests/2026-08-15/
Removing requests/2026-08-16/
Removing requests/open/
Removing samples/.DS_Store
Removing samples/plates/.DS_Store
Removing subprojcet/
Removing team/.DS_Store
Removing web/
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/.DS_Store"
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/data_log.json"
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/nextgen/"
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/server"
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/server_device.h"
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/server_seam.h"
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/server_wire.h"
Removing "\354\241\260\353\263\204\352\263\274\354\240\234\354\203\230\355\224\214/server_wire_test.cpp" 를 돌리면 **전선 계약(server_wire.h)과 리팩터링 산출물이 조용히 사라진다.** 파일이 없어지는 게 아니라 '있는 줄 알았는데 없다'가 되는 형태다.

**제안(44행 교체)**:
  조별과제샘플/server_test
  조별과제샘플/server_test.exe
  !조별과제샘플/server_*.h
  !조별과제샘플/server_*.cpp

또는 더 단순하게 확장자 없는 것만 막는 방식도 된다. 어느 쪽이든 **소스는 추적돼야 한다.**

**내가 안 고쳤다** — .gitignore 는 소유권 표상 루트 경로다. 훅이 막기도 하지만, 막지 않더라도 남의 영역을 말없이 고칠 일이 아니다.

## 왜 필요한가

전선 계약(server_wire.h)과 리팩터링 산출물이 버전 관리에서 빠져 있다. git clean 한 번에 사라지고, 조원이 clone 해도 안 딸려간다 — 조원 배포가 다음 단계인데 그때 빌드가 안 된다

## 대상 파일

- `.gitignore`
## 완료 기준

.gitignore 44행이 소스를 추적하도록 바뀌고 git status 에 위 4개 파일이 보이면 끝

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0054 --by root --note "<한 줄 요약>" -->

_(미처리)_

---

## ⚠ 번호 중복 주의 (루트, 2026-08-16 16:0x)

`git clean` 이 ID 카운터(`.seq`)와 요청 파일을 함께 지우자 번호가 되감겨,
**이 번호는 2026-08-15 에 이미 다른 요청에 쓰였다.** 이 파일은 **08-16 판**이다.
번호는 고치지 않는다 — 원장은 append-only 이고 이미 이 번호로 포인터가 오갔다.
날짜 폴더(`requests/2026-08-15/` 대 `requests/2026-08-16/`)로 구분하라.
카운터는 이제 `INDEX.md` 최대치까지 함께 보므로 같은 되감김은 다시 나지 않는다.

### 처리 완료 · root · 2026-08-16T16:00:37+0900

.gitignore 44행을 확장자 기준으로 좁혀 소스가 무시되지 않게 함(server_test·server_*.exe/.o/.out 만 무시). 확인: git check-ignore 로 server_device.h·server_wire.h·server_seam.h 가 더 이상 걸리지 않음. 사고 경위·복구 재고는 docs/incident-2026-08-16-git-clean.md

