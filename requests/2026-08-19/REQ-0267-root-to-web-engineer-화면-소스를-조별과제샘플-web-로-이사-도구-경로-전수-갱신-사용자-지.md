---
id: REQ-0267
title: 화면 소스를 조별과제샘플/web/ 로 이사 — 도구 경로 전수 갱신 (사용자 지시)
from: root
to: web-engineer
status: done
created: 2026-08-19T11:18:31+0900
updated: 2026-08-19T11:28:07+0900
files: ["조별과제샘플/index.html"]
parent: none
---

# REQ-0267 · 화면 소스를 조별과제샘플/web/ 로 이사 — 도구 경로 전수 갱신 (사용자 지시)

**요청자** `root` → **담당** `web-engineer`

## 요청 내용

# 화면 소스를 `조별과제샘플/web/` 로 옮긴다 (사용자 지시)

> 사용자: *"폴더를 생성하여 **server, ardu** 로 … 코드"* → *"웹은 server 에 포함된다"* → *"**그냥 web 폴더 생성하라**"*

**확정: 형제 폴더 셋이다.**
```
조별과제샘플/server/   socket    (server.cpp · server_*.h)
조별과제샘플/ardu/     arduino   (client.ino · EspLink_*.h)
조별과제샘플/web/      web       ← 🔴 너
```

## 옮길 것
```
조별과제샘플/index.html  →  조별과제샘플/web/index.html
```
✅ **소유권은 루트가 열어 뒀다** — `조별과제샘플/web/**` → `web-engineer`. 옛 경로도 아직 열려 있다.

⚠ **저장소 루트의 `web/`(네 `tools/`·`artifacts/`)와 다른 폴더다.** 헷갈리지 마라 —
저건 도구, 이건 제품 화면이다. **둘을 합치지 마라**(사용자가 `조별과제샘플` 안에 두라고 했다).

## 🔴 `git mv` · 그리고 커밋 뒤 조회

```bash
git mv 조별과제샘플/index.html 조별과제샘플/web/index.html
git commit -m "..." -- 조별과제샘플/web 조별과제샘플/index.html
git ls-files 조별과제샘플/ | grep index.html      # 🔴 새 경로만 남아야 한다
```
⚠ **§"커밋은 `commit` 에 경로를 준다"의 삭제 판본** — `git mv` 는 삭제+추가이고
**경로 명시 커밋이 작업 트리를 취하므로 옛 경로가 되살아날 수 있다.** arduino 가 오늘 그것을 밟았다.

## 🔴 네 도구가 그 경로를 박고 있다 — 같이 고쳐라

```
web/tools/deploy-screen.mjs    원본 경로
web/tools/zone-nodes.mjs       SOURCE_HTML
web/tools/live-map.mjs         SOURCE_HTML (../../조별과제샘플/index.html)
web/tools/map-epoch.mjs · queue-contract.mjs
docs/web/DEPLOY-SCREEN.md      절차서
```
**전수로 잡아라**: `grep -rn "조별과제샘플/index.html" web/ docs/web/`

🔴 **그리고 옮긴 뒤 `--check` 를 돌려 *실제로* 통과하는지 봐라.**
**경로가 안 고쳐진 도구는 "파일이 없다"로 죽거나 — 더 나쁘게 — 옛 파일이 남아 있으면 조용히 옛 것을 읽는다.**
🔑 **그래서 옛 경로가 저장소에 안 남는 것을 먼저 확인해라.**

## ⚠ 배포본과는 무관하다

실기 서빙본은 `~/parking-bin/index.html` 이고 **거기는 안 바뀐다.**
**바뀌는 것은 "원본이 어디 있는가" 하나**이고, 그것을 읽는 것은 네 배포 도구다.
✅ **이사 뒤 `--check` 가 `2 pass` 면 원본↔서빙본 동일성이 그대로라는 뜻이다.**

## 완료 기준
```
① git mv · 옛 경로 잔존 없음(조회로 확인)
② 도구·문서의 경로 전수 갱신 (grep 으로 0건 확인)
③ 🔴 deploy-screen --check 가 실제로 통과 — 돌려서 확인하고 결과를 적어라
④ 원장에 이사 기록
```

## 왜 필요한가

사용자가 server·ardu·web 형제 폴더로 코드를 가르라고 확정했고 지금 세 도메인 소스가 평면으로 섞여 있다

## 대상 파일

- `조별과제샘플/index.html`
## 완료 기준

git mv · 옛 경로 잔존 조회 · 도구와 문서 경로 전수 갱신(grep 0건) · deploy-screen --check 실제 통과 확인

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0267 --by web-engineer --note "<한 줄 요약>" -->

### 완료 — **순수 이동으로 증명했다** (커밋 `2b8cd44`)

```
이동 전 sha256 : fee93e2649325b7a8b42262f28ba5e7084e52d52ae730fecae7eb61b45a8c4be
이동 후 sha256 : fee93e2649325b7a8b42262f28ba5e7084e52d52ae730fecae7eb61b45a8c4be   ← 같다
git show --stat : ".../web/index.html" | 0      ← 내용 변경 0줄(git 이 rename 으로 인식)
```
🔑 **그래서 이 커밋은 "옮겼다" 하나만 주장한다.** 내용 변경은 앞 커밋(`77ca6a2`·`4ecccb3`)에 있다.
루트 지시대로 **REQ-0268(내용)을 먼저 커밋하고 이사를 따로** 했다 —
섞었으면 *"바이트가 그대로면 끝"* 이라는 이 대조 자체가 성립하지 않는다.

### 완료 기준별

| # | 요구 | 결과 |
|---|---|---|
| ① | `git mv` · 옛 경로 잔존 없음 | `git ls-files 조별과제샘플/ \| grep -c 조별과제샘플/index.html` → **0** |
| ② | 도구·문서 경로 전수 갱신 | **도구 8개** 갱신 · `grep -rn "조별과제샘플/index.html" web/tools/` → **0건** |
| ③ | `--check` 실제로 돌려 확인 | 돌렸다 (아래) |
| ④ | 원장 기록 | §5.87 |

### 🔴 루트가 센 것은 5개였는데 **실제로는 8개**였다

`fallback-age.mjs` · `queue-contract.mjs` · `check-index.mjs` 가 목록에 없었다.
🔑 **전수 grep 을 안 돌리고 목록을 믿었으면 셋이 조용히 깨졌다** — 그리고 그 셋은
**옛 파일이 남아 있었으면 "없다"로 죽지도 않고 옛 것을 조용히 읽었을 것**이다(요청서의 그 경고 그대로).

### ③ `--check` 결과 — **2 pass 가 아니다. 그리고 그게 맞다**

```
원본  : /Users/idong-u/learn/조별과제샘플/web/index.html   fee93e264932   ← 새 경로에서 읽는다 ✅
서빙본: /Users/idong-u/parking-bin/index.html              24de89f51300
        표지 src=24de89f51300 git=7b65377 at=2026-08-19T10:55:50
```
⚠ 요청서는 *"이사 뒤 `--check` 가 2 pass 면 동일성이 그대로"* 라고 했는데 **지금은 1 fail 이다.**
**이사 때문이 아니다** — 그 사이 **REQ-0268(인덱스 상시 표시)로 원본이 바뀌었고**,
서빙본은 `10:55:50` 에 배포된 **REQ-0261 판**이다. 즉 **차이는 인덱스 표시 한 건**이다.
🔑 **이 도구가 재는 것은 "원본을 찾았나"가 아니라 "원본과 서빙본이 같나"다.**
**원본을 새 경로에서 정확히 읽고 있다는 것**이 이사의 성공 판정이고, 그건 첫 줄이 보여 준다.

### 검증 — 실행한 것

```
node --check web/tools/*.mjs        → 전부 통과
node web/tools/deploy-screen.mjs --check  → 새 경로에서 원본을 읽는다(위)
node web/tools/zone-nodes.mjs             → 22 pass / 0 fail / 1 미측정
```

### ⚠ 옛 경로를 적은 문서는 **안 고쳤다**

`LEDGER`(4곳) · `FINDING-link-blink` · `VERIFY-integration` · `ANALYSIS-redesign-impact`.
**그 시점의 기록이라 고치면 지난 기록이 거짓이 된다.** 경로가 바뀐 사실은 원장 §5.87 에 적었고,
그것이 옛 기록을 읽는 사람에게 필요한 전부다.

### 🔴 덤 — 앞 커밋에서 `git commit -- <경로>` 가 **새 파일 넷을 조용히 빠뜨렸다**

`77ca6a2` 를 경로 지정으로 올렸는데 **untracked 였던 도구 3개 + 문서 1개가 안 들어갔다.**
`commit` 에 경로를 줘도 **tracked 만 취한다.** 실패가 아니라 **성공으로 보인다** —
`git show --stat` 을 안 봤으면 못 봤다. `4ecccb3` 으로 따로 올렸다. 원장 §5.87 에 적었다.

### 처리 완료 · web-engineer · 2026-08-19T11:28:07+0900

조별과제샘플/web/index.html 로 이사 — sha256 동일(순수 이동). 도구 8개 경로 갱신(루트 목록은 5개였다). 옛 경로 잔존 0, grep 0건, --check 가 새 경로에서 원본을 읽는다. 곁들여 앞 커밋이 새 파일 4개를 조용히 빠뜨린 것을 찾아 4ecccb3 으로 보완

