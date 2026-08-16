#!/usr/bin/env python3
"""상행 S 프레임의 **도착 간격**을 재고, 큰 공백이 주입 시각에 몰리는지 본다.

왜 있나 (socket 요청 2026-08-17 00:3x)
  주입기 초판은 WS 소켓을 붙들고 안 읽어서 서버 송신 버퍼를 채웠고,
  `send_raw` 가 **블로킹 1초**를 기다렸다 끊었다. 서버가 **단일 스레드 select 루프**라
  그 1초 동안 9991 아두이노 처리도 같이 멈춘다.
  → **주입이 5분마다 1초짜리 프레임 공백을 만들어 넣을 뻔했다.**
  그건 최대공백·3500ms 오프라인 판정에 그대로 먹이는 값이고,
  **TX 증가와 달리 순수한 계측 결함이라 인과 분석으로도 못 걸러낸다.**

  socket 이 고쳤고 시험 인스턴스에서 확인했다. **그러나 실물 부하는 다를 수 있다.**
  그래서 "보이면 알려 달라"를 **약속이 아니라 계측**으로 바꾼 것이 이 도구다.

🔴 해상도와 맥놀이 — 먼저 읽어라 (실측으로 문턱이 바뀌었다)
  서버 로그 타임스탬프는 **초 단위**다. 서브초는 원리적으로 못 잰다.

  ⚠ **장치 주기는 1.000s 가 아니라 약 1.11s 다**(국면1 실측: 663s / 596 간격 = 1.112s).
  그래서 **1초 해상도로 표본화하면 9프레임마다 한 번 `2s` 로 읽힌다** — 맥놀이(aliasing)다.
  국면1 무주입 11분에서 `>=2s` 가 **68건(11.45%)** 나왔는데, `2s` 사이 프레임 수가
  **9 에 56/67 로 몰려** 있었다. **대부분은 멈춤이 아니다.**

  🔴 **정정(00:5x, arduino) — "전부 아티팩트"는 과했다.** `2s` 칸에는 **`skip` 도 섞인다**
     (아래 `SKIP` 주석). 그래서 이 도구는 **맥놀이 검정 전에 `skip` 을 빼고, 뺀 건수를 찍는다.**
     조용히 빼면 다음 사람이 모른다.

  → 그러므로 **`>=2s` 를 그대로 멈춤으로 세면 대부분이 가짜 양성**이다(원장 2.5).
     이 도구는 **`>=3s`** 를 후보로 본다 — 다만 **문턱만으로는 안 갈린다.**
     🔴 **정정(01:4x): `skip` 은 `3s` 도 만든다.** 주기 1.118s 에서 한 주기를 건너뛰면
     2×1.118 = **2.24s** 이고 1초 해상도에서 **위상에 따라 `2s` 로도 `3s` 로도** 읽힌다.
     실증: 국면1 유일한 `>=3s`(01:01:53→01:01:56)가 **skip 그 자체**였다.
     → **`>=3s` 도 skip 대조를 거친다.** 이 도구는 걸러 내고 **걸러낸 건수를 찍는다.**
     WS 블로킹 1초 정지는 (1.11+1.0) → **`>=3s` 로 튀고 9프레임 리듬을 깬다.**

  🔴🔴 **거꾸로 읽기 경고 — 판정문에도 넣어라:**
     **링크가 나빠질수록 `skip` 이 늘고 그만큼 `2s` 가 는다.**
     그것을 *"맥놀이가 더 보인다"* 로 읽으면 **정확히 거꾸로다** —
     **병리가 시작되는 신호를 아티팩트로 처리하는 것이다.**

  ⚠ 그래도 **이 도구 하나로 판정하지 마라.** 국면1(무주입) 기저와 대조해야 뜻이 생긴다.

⚠ `0` 주의: 큰 공백 0건이 "안 난다"인지 "구간이 짧아 아직 안 났다"인지 갈라 적어라(원장 1.1).

사용:
  python3 monitor/frame_gaps.py <서버로그> <시작ISO> <끝ISO> [주입시각] [반경초]

  주입시각 인자:
    (없음)  공백 분포만 낸다 — 국면1 기저 측정용
    auto    🔑 **권장.** 같은 서버 로그의 `→ARD T,<seq>,S,…` 줄에서 뽑는다
    <파일>  ISO 한 줄에 하나(`#`·빈 줄 무시)

🔴 **왜 `auto` 가 기본 권장인가 — socket 이 짚은 것이고 맞다**
  근접을 재려면 **주입 시각과 끊김 시각을 빼야** 한다. 끊김 줄은 **서버 로그**에 있다.
  주입 시각을 **socket 의 주입기 로그**에서 가져오면 **서로 다른 출처의 시각을 빼는 것**이 되고,
  그건 CLAUDE.md 가 경고하는 바로 그 형태다(같은 단위라고 비교 가능한 것이 아니다).
  게다가 주입기의 `→ 주입` 은 **WS 요청을 보낸 시각**이고 서버가 전선에 실제로 내보낸 시각은
  그 뒤다 — 보통 같은 초지만 **같다는 보장이 없다.**
  → **분자(시각)는 서버 로그에서. 같은 파일·같은 프로세스면 시계 문제가 원천적으로 없다.**

  ⚠ 그러면 socket 의 주입기 로그는 무엇에 쓰나 — **분모다.**
     서버 로그에 `→ARD T` 가 **없을 때** 그 이유를 말해 주는 유일한 기록이다:
     `= 미주입`(서버가 거절 · D0~D3 어디도 아니다) / `! 이상` / **주입기가 죽어 시도조차 없었나**.
     `~/parking-logs/downlink-inject.log` · 형식 `YYYY-MM-DD HH:MM:SS␣␣<표지>␣<본문>`
     (ISO 로: `line[:10]+"T"+line[11:19]`)
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter
from datetime import datetime
from math import comb

TS = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s")
SFRAME = "←ARD S,"
WSUP = "+WS 업그레이드 완료"
DOWNT = "→ARD T,"      # 하행 주입이 **전선에 나간** 시각 (근접 계산의 ①)

# 🔴 2026-08-17 00:5x — arduino 가 잡은 것. `2s` 칸에 **두 가지가 섞인다.**
#   ① 맥놀이   1초 해상도 × 1.113s 주기 → 약 9프레임마다. **계측 아티팩트, 사건 아님**
#   ② `skip`   2단계 게이트가 실제로 한 주기를 건너뜀. **진짜 사건**
#              (`64fc70b` 부터 새로 생겼다. 2단계 없는 칩 자료에는 없다)
#
#   🔴 **무늬가 같아서 맥놀이 설명이 ②를 조용히 흡수한다.** 그리고 서버 로그만으로는
#      **원리적으로 못 가른다** — `Δuptime` 도 같은 맥놀이를 타기 때문에 표지가 겹친다.
#      가르는 유일한 표지는 **시리얼 로그의 `[TX-WAIT] … 건너뛴다`** 줄이다(원장 6.10 계열).
#
#   빈도(국면1 실측): `[TX] ` 1158건 중 `skip` **1건**. 지금은 맥놀이 검정을 망칠 수준이 아니다.
#   ⚠ 그러나 **링크가 나빠질수록 `skip` 이 는다.** 그때 무늬가 흐려진다.
SKIP = "이번 주기는 건너뛴다"
# 🔴 문턱 `>=3s` 의 근거 — **두 번 고쳤다. 경위를 남긴다.**
#
#   1차(00:3x): 국면1 11분에서 `>=2s` 가 68건(11.45%). 구조를 보니 `2s` 사이 프레임 수가
#     9 에 몰려 있었다 → **맥놀이**(주기 1.11s 를 1초 해상도로 표본화). 문턱을 2 → 3 으로.
#     ⚠ 그때 "장치는 한 번도 안 멈췄다 / 기저 `>=3s` 는 0건" 이라고 적었다. **둘 다 과했다.**
#
#   2차(00:5x, arduino): `2s` 칸에 **`skip` 도 섞인다.** "전부 아티팩트"가 틀렸다.
#
#   3차(01:4x, 루트가 다른 구간을 세어 불일치를 지적 → 확인): **`skip` 은 `3s` 도 만든다.**
#     주기 1.118s 에서 한 주기를 건너뛰면 2×1.118 = 2.24s → 위상에 따라 `2s`/`3s` 로 읽힌다.
#     실증: 국면1 유일한 `>=3s`(01:01:53→01:01:56)가 **skip 목록의 01:01:56 그 자체**였다.
#     ⚠ 그리고 "기저 `>=3s` 0건" 은 **내가 짧은 구간(00:24:57~00:55)만 보고 쓴 것**이었다.
#       루트와 같은 구간(~01:25:04)으로 세니 **1건**으로 정확히 일치했다.
#       **구간을 안 밝히고 `0` 을 인용한 내 잘못이다**(원장 1.1 · 1.5).
#
#   → **결론: 문턱만으로는 안 갈린다. `>=3s` 도 반드시 skip 대조를 거친다.**
#     남는 것(skip 아닌 `>=3s`)이 WS 블로킹 같은 진짜 정지의 후보다.
BIG = 3  # 초 — 맥놀이(2s)를 넘어선 후보. **skip 대조를 거쳐야 뜻이 있다**


def load(path: str) -> list:
    out = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if ln and not ln.startswith("#"):
                out.append(datetime.fromisoformat(ln))
    return out


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    log, since, until = sys.argv[1], datetime.fromisoformat(sys.argv[2]), datetime.fromisoformat(sys.argv[3])
    injf = sys.argv[4] if len(sys.argv) > 4 else None
    radius = int(sys.argv[5]) if len(sys.argv) > 5 else 5
    serlog = sys.argv[6] if len(sys.argv) > 6 else "monitor/serial-win3.log"

    # 시리얼에서 `skip` 시각을 긁는다 — 서버 로그로는 맥놀이와 못 가른다(위 주석)
    skips = []
    if os.path.exists(serlog):
        base = since.date()
        with open(serlog, "rb") as f:
            for raw in f:
                s2 = raw.decode("utf-8", "replace")
                if SKIP not in s2:
                    continue
                m2 = re.match(r"^(\d\d):(\d\d):(\d\d)\s", s2)
                if not m2:
                    continue
                t2 = datetime(base.year, base.month, base.day,
                              int(m2[1]), int(m2[2]), int(m2[3]))
                if since <= t2 <= until:
                    skips.append(t2)

    frames, ws, sent = [], [], []
    with open(log, "rb") as f:
        for raw in f:
            s = raw.decode("utf-8", "replace")
            m = TS.match(s)
            if not m:
                continue
            t = datetime.fromisoformat(m.group(1))
            if not (since <= t <= until):
                continue
            if SFRAME in s:
                frames.append(t)
            elif WSUP in s:
                ws.append(t)
            elif DOWNT in s:
                sent.append(t)   # 서버가 전선에 실제로 내보낸 시각 — 근접 계산의 ①

    print(f"# 원본: {os.path.abspath(log)}")
    print(f"# 구간 {since} ~ {until}  ({(until-since).total_seconds()/3600:.2f}h)")
    print("# ⚠ 초 단위 해상도 · 장치 주기 **약 1.11s**(1Hz 아님) → `2s` 는 맥놀이로도 나온다")
    print(f"# ⚠ 그래서 후보 문턱은 `>={BIG}s` 다. 아래 맥놀이 진단을 먼저 읽어라.")
    print(f"\n## 상행 S 프레임 {len(frames):,}건")
    if len(frames) < 2:
        print("   ⚠ 2건 미만이라 간격을 못 낸다. **'공백 없음'이 아니라 '못 셈'이다**(원장 1.1).")
        return 0

    gaps = [(frames[i + 1], int((frames[i + 1] - frames[i]).total_seconds()))
            for i in range(len(frames) - 1)]
    h = Counter(g for _, g in gaps)
    print("\n## 도착 간격 분포 (초)")
    for k in sorted(h):
        if k < 0:
            mark = "  ← 🔴 **시각 역행**(아래 참조)"
        elif k <= 1:
            mark = "  ← 정상"
        else:
            mark = ""
        print(f"   {k:>3}s  {h[k]:>6}건{mark}")

    # 🔴 2026-08-17 07:5x — 음수 간격을 "정상" 으로 찍고 있었다. **표시 결함이자 진짜 이상이다.**
    #   창3 실측: 02:57:34 → 02:57:31 (−3s) · 04:01:31 → 04:01:30 (−1s).
    #   같은 자리에서 장치 `seq` 와 `uptime` 은 **단조 증가**였다(8273→8274, 9249→9250).
    #   → **장치가 아니라 호스트 벽시계가 뒤로 갔다**(NTP 보정으로 보인다 · 추정).
    #   영향: 벽시계 기반 간격·근접 계산이 그 지점에서 최대 수 초 어긋난다.
    #   ⚠ **±5초 근접 분석에 직접 걸린다.** 장치 쪽 시간축이 필요하면 `uptime` 을 써라 — 그건 단조다.
    back = [(t, g) for t, g in gaps if g < 0]
    if back:
        print(f"\n## 🔴 **시각 역행 {len(back)}건** — 호스트 벽시계가 뒤로 갔다")
        for t, g in back:
            print(f"   {t:%H:%M:%S}  {g}s")
        print("   · 장치 `seq`·`uptime` 은 같은 자리에서 단조 증가였다 → **장치 문제가 아니다.**")
        print("   · 벽시계 기반 간격·±5초 근접 계산이 이 지점에서 어긋난다. **판정문에 적어라.**")
        print("   · 장치 시간축이 필요하면 `uptime`(단조)을 써라.")
    # 맥놀이 진단 — `2s` 가 규칙적으로 흩뿌려지면 멈춤이 아니라 표본화 결함이다
    mean_gap = sum(g for _, g in gaps) / len(gaps)
    two = [i for i, (_, g) in enumerate(gaps) if g == 2]
    sp = Counter(two[i + 1] - two[i] for i in range(len(two) - 1))
    print(f"\n## 맥놀이 진단 — `2s` 를 멈춤으로 세기 전에 본다")
    print(f"   간격 평균 = **{mean_gap:.3f}s** (장치 주기가 1.000s 가 아니면 여기서 드러난다)")
    # ⚠ `skip` 과 겹치는 `2s` 는 맥놀이 검정에서 빼고 본다 — 안 빼면 설명이 사건을 흡수한다
    if skips:
        def is_skip(t):
            return any(abs((t - k).total_seconds()) <= 1 for k in skips)
        two_skip = [i for i in two if is_skip(gaps[i][0])]
        print(f"   ⚠ 시리얼 `[TX-WAIT] 건너뛴다` **{len(skips)}건** — 그중 `2s` 칸과 겹치는 것 "
              f"**{len(two_skip)}건**은 **맥놀이가 아니라 진짜 사건**이다(arduino, `64fc70b` 부터).")
        for k in skips:
            print(f"      skip {k:%H:%M:%S}")
        two = [i for i in two if i not in set(two_skip)]
        sp = Counter(two[i + 1] - two[i] for i in range(len(two) - 1))
    else:
        print(f"   (시리얼 `{serlog}` 에서 `skip` 0건 — 이 구간의 `2s` 는 전부 맥놀이 후보다)")

    if sp:
        top, cnt = sp.most_common(1)[0]
        share = 100 * cnt / sum(sp.values())
        print(f"   `2s` 사이 프레임 수: 최빈 **{top}** ({share:.0f}%)  분포 {dict(sorted(sp.items()))}")
        if share >= 60:
            print(f"   → 🔑 **규칙적이다 = 맥놀이(aliasing).** 주기 {mean_gap:.2f}s 를 1초 해상도로")
            print(f"      표본화한 결과이지 멈춤이 아니다. **`2s` 를 공백으로 세지 마라.**")
        else:
            print("   → ⚠ 흩어져 있다. 맥놀이로 설명되지 않는다 — `2s` 도 살펴봐라.")

    # 🔴 2026-08-17 01:4x 정정 — **`skip` 은 `3s` 도 만든다.** 내 앞선 근거가 틀렸다.
    #   나는 "`skip` 은 `2s` 를 만들지 `3s` 를 안 만든다"를 문턱의 근거로 적었다. **반증됐다:**
    #   국면1 유일한 `>=3s`(01:01:53→01:01:56)가 **skip 목록의 01:01:56 과 같은 건**이었다.
    #   당연하다 — 주기 1.118s 에서 한 주기를 건너뛰면 2×1.118 = **2.24s** 이고,
    #   1초 해상도에서는 **위상에 따라 `2s` 로도 `3s` 로도** 읽힌다.
    #   → 그러므로 **`>=3s` 도 skip 대조를 반드시 거쳐야 한다.** 문턱만으로는 안 갈린다.
    big_all = [(t, g) for t, g in gaps if g >= BIG]
    if skips:
        def _is_skip(t):
            return any(abs((t - k).total_seconds()) <= 1 for k in skips)
        big = [(t, g) for t, g in big_all if not _is_skip(t)]
        bs = [(t, g) for t, g in big_all if _is_skip(t)]
        if bs:
            print(f"\n## ⚠ `>={BIG}s` 중 **`skip` 으로 설명되는 것 {len(bs)}건** — 정지가 아니다")
            for t, g in bs:
                print(f"   {t:%H:%M:%S}  간격 {g}s  ← `[TX-WAIT]` 과 같은 초")
    else:
        big = big_all
    print(f"\n## `>={BIG}s` 공백 (skip 제외) {len(big)}건 / 전체 간격 {len(gaps)}건"
          f" = {100*len(big)/len(gaps):.2f}%")
    if not big:
        print(f"   ⚠ 0 건이다. **'안 난다'가 아니라 '이 {(until-since).total_seconds()/3600:.2f}h 에서 안 났다'** 이다.")

    print(f"\n## `+WS 업그레이드 완료` {len(ws)}건")
    if ws:
        for i, t in enumerate(ws):
            d = "" if i == 0 else f"  (앞과 {int((t-ws[i-1]).total_seconds())}s)"
            print(f"   {t:%H:%M:%S}{d}")
        print("   ⓘ 주입기는 주입할 때마다 WS 를 새로 열고 곧바로 닫는다(socket, 판본 8d09cee).")
        print("   🔴 **간격이 300s 로 규칙적이지 않은 줄이 있으면 그것은 주입기가 아니다**")
        print("      (web 이 붙었거나 다른 것이다). 그런 줄이 있으면 창에 남의 부하가 들어온 것이다.")

    if not injf:
        print("\n(주입 시각 파일이 없어 근접 분석은 건너뛴다 — 국면1 기저율 측정에는 이대로 충분하다)")
        return 0

    if injf == "auto":
        inj = list(sent)
        print(f"\n(주입 시각을 **같은 서버 로그의 `{DOWNT}`** 에서 뽑았다 — 시계가 하나다)")
    else:
        inj = [t for t in load(injf) if since <= t <= until]
        print(f"\n⚠ 주입 시각을 외부 파일에서 읽었다({injf}). **서버 로그와 다른 출처다** —")
        print("   끊김 시각과 빼면 서로 다른 시계를 빼는 것이다. 가능하면 `auto` 를 써라.")
    print(f"\n## 주입 {len(inj)}건 · 반경 ±{radius}s 근접 분석")
    if not inj:
        print("   구간 안에 주입이 없다.")
        return 0

    def near(t):
        return any(abs((t - i).total_seconds()) <= radius for i in inj)

    nb = [(t, g) for t, g in big if near(t)]
    span = (until - since).total_seconds()
    # 창 안으로 클립한 ±radius 합집합 (겹침 제거 — 안 하면 p 가 부풀어 '우연'이 과대평가된다)
    segs = sorted((max(since.timestamp(), i.timestamp() - radius),
                   min(until.timestamp(), i.timestamp() + radius)) for i in inj)
    uni = []
    for a, b in segs:
        if uni and a <= uni[-1][1]:
            uni[-1][1] = max(uni[-1][1], b)
        else:
            uni.append([a, b])
    p = sum(b - a for a, b in uni) / span
    k, n = len(nb), len(big)
    pk = sum(comb(n, i) * p**i * (1 - p) ** (n - i) for i in range(k, n + 1)) if n else 1.0

    print(f"   근접 구간이 창의 {100*p:.2f}% 를 덮는다 (p={p:.4f})")
    print(f"   `>={BIG}s` 공백 {n}건 중 주입 ±{radius}s 안: **{k}건** · 우연 기대 {n*p:.2f}건")
    if n:
        print(f"   P(X >= {k} | n={n}, p={p:.4f}) = {pk:.4f}")
    for t, g in nb:
        print(f"     {t:%H:%M:%S}  간격 {g}s  ← 주입 근접")
    print("\n## 읽는 법")
    print("   · 🔴 **이 도구 하나로 판정하지 마라.** `2s` 는 평시 지터로도 나온다.")
    print("     **국면1(무주입) 기저율과 대조**해야 뜻이 생긴다. 두 국면을 각각 돌려라.")
    print("   · 근접이 있어도 **우연 기대치를 같이 적어라**(원장 1.13).")
    print("   · 공백이 주입에 몰리면 **socket 에게 알려라 — 계측 결함이지 장치 고장이 아니다.**")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
