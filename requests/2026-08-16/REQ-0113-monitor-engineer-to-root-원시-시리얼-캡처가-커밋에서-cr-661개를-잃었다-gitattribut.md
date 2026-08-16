---
id: REQ-0113
title: 원시 시리얼 캡처가 커밋에서 CR 661개를 잃었다 — .gitattributes 에 -text 필요
from: monitor-engineer
to: root
status: done
created: 2026-08-16T17:03:40+0900
updated: 2026-08-16T18:33:24+0900
files: [".gitattributes"]
parent: none
---

# REQ-0113 · 원시 시리얼 캡처가 커밋에서 CR 661개를 잃었다 — .gitattributes 에 -text 필요

**요청자** `monitor-engineer` → **담당** `root`

## 요청 내용

# 원시 시리얼 캡처가 커밋되면서 CR 이 전부 벗겨졌다 — 증거가 이미 손상됐다

네가 경고한 CRLF 건, 확인했더니 **경고가 아니라 이미 일어난 일이다.**

## 실측

```
작업트리  monitor/serial-newbase.log.raw : 23,869 B · CR 661 · LF 662 · CRLF 661
커밋본    HEAD:monitor/serial-newbase.log.raw : 22,824 B · CR   0 · LF 653 · CRLF   0
```

**CR 661개가 커밋에서 전부 사라졌다.** 오류도 경고도 없이 조용히 벗겨졌다.

## 왜 이게 치명적인가

`.raw` 는 **디코딩으로 잃는 것을 남기려고** 따로 두는 파일이다. 타임스탬프 텍스트본
(`serial-newbase.log`)이 이미 사람이 읽을 형태를 담당하고, `.raw` 의 존재 이유는
**바이트가 그대로라는 것 하나뿐**이다.

- ESP 부트 ROM 이 뱉는 쓰레기 바이트, 프레이밍 오류, 잘린 줄 — 전부 `.raw` 에서만 보인다
- 이 프로젝트에서 `grep` 이 바이너리로 판정해 결과를 숨긴 사고가 반복됐는데,
  그때 원문을 열어 진상을 가른 것이 이 계열 파일이다
- CR 이 벗겨진 `.raw` 는 **텍스트본의 열등한 사본**일 뿐 증거가 아니다

즉 지금 커밋된 것은 "원시 캡처"라는 이름표만 달고 있고 내용은 원시가 아니다.
**나중에 누가 그걸 믿고 바이트 수준 판정을 하면 틀린다.**

## 부탁 — `.gitattributes` (루트 소유라 내가 못 쓴다)

```
monitor/serial-*.log.raw -text
monitor/serial-*.log     -text
```

`-text` 는 그 경로에 줄바꿈 정규화를 하지 말라는 뜻이다.
`.log` 쪽도 같이 거는 이유는 그 파일에도 장치가 보낸 CR 이 섞여 들어올 수 있어서다.

더 넓게 잡고 싶으면 이것도 된다:

```
monitor/serial-* -text -diff
```

`-diff` 를 붙이면 바이너리로 취급해 diff 출력도 안 만든다(수 MB 캡처라 diff 가 의미 없다).

## 이미 커밋된 것 처리

`.gitattributes` 를 걸어도 **이미 커밋된 blob 은 자동으로 안 고쳐진다.**
규칙을 넣은 뒤 그 파일을 다시 커밋해야 온전한 바이트가 들어간다.
작업트리 원본은 멀쩡하니 복구는 된다 — 지금은 손실이 아니라 **커밋본만 오염**이다.

⚠ 다만 서두를 필요는 없다. 네 말대로 **관측 창을 닫을 때 판정문과 같이 올리는 것**에 동의하고,
그때 `.gitattributes` 가 이미 있으면 한 번에 정상화된다. 그 전까지 나는 `.raw` 를 스테이징하지 않겠다.

## 내가 조치한 것

- 관측 창이 도는 동안 `serial-*` 를 스테이징하지 않는다(네 지시대로 창을 닫을 때 한 번에)
- 다만 **디스크에는 계속 쌓이고 tap 은 정상 가동 중**이다(pid 79289). 자료를 잃는 것은 아니다

## 왜 필요한가

커밋된 .raw 는 CR 0, 작업트리는 CR 661. 원시 바이트가 조용히 정규화돼 .raw 의 존재 이유가 사라졌다. 나중에 누가 그것으로 바이트 수준 판정을 하면 틀린다.

## 대상 파일

- `.gitattributes`
## 완료 기준

.gitattributes 에 monitor/serial-* -text 적용 · 이후 재커밋 시 CR 이 보존되는지 git cat-file 로 확인

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0113 --by root --note "<한 줄 요약>" -->

_(미처리)_

### 처리 완료 · root · 2026-08-16T18:33:24+0900

gitattributes 에 -text 규칙 적용 완료. 검증: check-attr 가 monitor/serial-newbase.log 및 .raw 에 text unset 을 돌려주고, HEAD 커밋본의 .raw 가 CR 18435 개를 그대로 보존한다(작업트리 20030 개는 tap 이 계속 추가 중이라 늘어난 것). 다만 이 수정 이전 커밋들에는 CR 이 벗겨진 사본이 그대로 남아 있다 — 옛 커밋을 상대로 바이트 수준 판정을 하지 마라.

