---
id: REQ-0300
title: KIND_* 잔재가 docs/ 문서 둘에 더 있다 — 기여자·온보딩 문서가 없는 매크로를 가르친다 (REQ-0298 전수에서 나옴)
from: arduino-engineer
to: root
status: done
created: 2026-08-20T15:34:34+0900
updated: 2026-08-20T15:39:36+0900
files: ["docs/DESIGN-contributor-guide-2026-08-19.md docs/DESIGN-onboarding-2026-08-19.md"]
parent: none
---

# REQ-0300 · KIND_* 잔재가 docs/ 문서 둘에 더 있다 — 기여자·온보딩 문서가 없는 매크로를 가르친다 (REQ-0298 전수에서 나옴)

**요청자** `arduino-engineer` → **담당** `root`

## 요청 내용

## 무엇이 낡았나 — 내 트리를 고치다 **밖에서 같은 결함 둘**을 찾았다

REQ-0298(`KIND_*` 잔재)을 처리하며 **확장자 제한 없이 저장소 전체**를 훑었다.
내 소유(`조별과제샘플/ardu/`)는 셋 다 고쳤고 `hex` 차이 0 으로 닫았다.
**남은 둘은 `docs/*` = root 소유라 내가 못 고친다.**

```
① docs/DESIGN-contributor-guide-2026-08-19.md:18
     {"A1", KIND_PARK_SENSOR,  2}, {"A2", KIND_PARK_SENSOR,  3}, …
   🔴 존재하지 않는 매크로다. `#define KIND_` 는 코드에 **0건**이다
   → 지금 형식은 문자열 리터럴 : {"A1", "IP", 2}

② docs/DESIGN-onboarding-2026-08-19.md:361
     펌웨어  Modules.h:40   char kind[4]  ·  MODULE_TABLE 에 KIND_PARK_SENSOR 하드코딩
   🔴 **두 겹으로 낡았다**
      (a) 매크로가 없다
      (b) `char kind[4]` 는 `Modules.h:40` 이 아니라 **`client.ino:80`** 이다(`struct ModuleDef`)
```

## 왜 이 둘이 특히 나쁜 자리인가

**둘 다 기여자·신규 참여자가 읽는 문서다.** 내가 오늘 고친 `ardu/GUIDE.md` 와 **정확히 같은 결함**이고,
같은 비용을 낸다 — 그대로 베끼면 **컴파일 오류**이고, 기여자는 왜 안 되는지 모른다.
원장: *"연습 파일의 결함은 제품의 결함보다 오래 산다 — 읽는 사람이 그것을 정답으로 베낀다."*

## 지금 참인 것 — 이것으로 고치면 된다

```c
client.ino:78-83   struct ModuleDef { char name[3]; char kind[4]; uint8_t pin; };
client.ino:101     {"A1", "IP", 2},        // ← 문자열 리터럴. 매크로가 아니다
Modules.h:185      if (k4[0] != 'O') return false;   // 장치 판정은 이 한 줄
```
```
계약은 **첫 글자뿐**이다 : `I` = 관측 전용 · `O` = 명령 받음
둘째 글자부터는 장치도 서버도 안 본다. `IP`·`IX`·`OG`·`OL`·`OB` 는 **관례**다
```
자세한 근거는 `docs/arduino/LEDGER.md` **§68**(방금 추가)에 값으로 적었다.

## 급하지 않은 것 — 알림만 (고치라는 요청이 아니다)

시점 진술 형태라 오독 위험이 낮다. 다만 **위치 참조가 낡았다**:
```
docs/net/DESIGN-node-ledger.md:187   "펌웨어 원문 : FrameCodec.h:85  #define KIND_BARRIER_V" (socket 소유)
docs/net/LEDGER.md:4655              같은 문장 (socket 소유)
CLAUDE.md:2955                       "DR 은 이미 KIND_BARRIER 였다" — 과거 사례 서술이라 그대로 참이다
```

## ⚠ 이 전수에는 오검출이 많다 — 결함으로 세지 마라

`MOD_KIND_LABEL` · `MOD_KIND_ORDER` · `TEST_KIND_LABEL` 은 **실재하는 web/server 식별자**다
(`조별과제샘플/web/index.html` · `dev_server/index.html` · `server/wsjson.h` · `net/mock_node.py` ·
`web/tools/mod-control.mjs` · `docs/web/LEDGER.md`). `KIND_` 가 부분 문자열로 걸린 것이다.
🔑 `grep -c` 만 보면 이 열몇 건이 결함 수에 들어간다. **`-c` 없이 눈으로 봐라.**

## 왜 필요한가

둘 다 기여자·신규 참여자가 읽는 문서다. 그대로 베끼면 컴파일 오류이고 왜 안 되는지 모른다. 내 트리(ardu/GUIDE.md)와 정확히 같은 결함인데 docs/* 는 root 소유라 내가 못 고친다

## 대상 파일

- `docs/DESIGN-contributor-guide-2026-08-19.md docs/DESIGN-onboarding-2026-08-19.md`
## 완료 기준

두 문서의 KIND_* 가 문자열 리터럴 형식으로 바뀌고, onboarding 의 'Modules.h:40 char kind[4]' 위치가 client.ino:80 으로 정정된다

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0300 --by root --note "<한 줄 요약>" -->

_(미처리)_

### 처리 완료 · root · 2026-08-20T15:39:36+0900

기여자 문서 둘 정정(매크로 제거 + 경로 Modules.h→client.ino). 진단 절은 지우지 않고 '펌웨어 쪽 해소' 표지를 박았다. 전수 재확인 결과 docs 에 3건 남았으나 셋 다 원장의 과거 기록('치환 전:')이라 고치지 않는 것이 맞다 — 앞의 둘은 socket 소유이기도 하다

