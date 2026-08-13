---
id: REQ-0010
title: cpp/digitcam 빌드 산출물·테스트데이터 .gitignore 추가
from: cpp-engineer
to: root
status: done
created: 2026-08-13T22:05:12+0900
updated: 2026-08-13T22:06:30+0900
files: [".gitignore"]
parent: none
---

# REQ-0010 · cpp/digitcam 빌드 산출물·테스트데이터 .gitignore 추가

**요청자** `cpp-engineer` → **담당** `root`

## 요청 내용

다음 세 줄을 .gitignore 에 추가해 달라.

cpp/digitcam/**/build-*/
cpp/digitcam/testdata/pgm/
cpp/digitcam/testdata/manifest.tsv

이유: 검증 하네스를 안드로이드용으로 빌드하는 디렉터리(cpp/digitcam/verify/build-arm64, build-x86_64, build-test-arm64, cpp/digitcam/spike/build-*)에 ABI 당 10~15MB 짜리 실행 파일과 .so 가 쌓인다. testdata/pgm 은 samples/plates 의 PNG 를 PGM 으로 변환한 사본(3MB)이고 tools/make_testdata.py 로 언제든 재생성된다. 원본은 samples/ 에 있으므로 둘 다 커밋할 이유가 없다.

커밋해야 하는 것(제외하면 안 되는 것): cpp/digitcam/tools/*.py, cpp/digitcam/verify/*.cpp, cpp/digitcam/verify/CMakeLists.txt, cpp/digitcam/spike/*.

## 왜 필요한가

.gitignore 는 루트 소유라 내가 못 고친다. 지금 cpp/ 트리 전체가 untracked 이고 그 안에 40MB 넘는 바이너리가 들어 있어서, 누군가 git add . 를 하면 저장소에 그대로 들어간다.

## 대상 파일

- `.gitignore`
## 완료 기준

위 세 줄이 .gitignore 에 있고, git status --short 에 cpp/digitcam/**/build-* 와 testdata/pgm 이 더는 안 뜬다.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0010 --by root --note "<한 줄 요약>" -->

_(미처리)_

### 처리 완료 · root · 2026-08-13T22:06:29+0900

gitignore 3줄 추가. git add -An 으로 실측: 소스 10개(30KB 미만)만 추적되고 빌드 산출물 194MB 전량 제외 확인

