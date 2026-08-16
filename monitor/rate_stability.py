#!/usr/bin/env python3
"""끊김 **총 발생률**이 창 안에서 일정한가 — 갈래와 무관하게 (루트 질문 2026-08-16 20:15).

## 왜 이 물음인가

시간축에서 이런 그림이 나왔다: **갈래(표지)는 크게 흔들리는데 사건 수는 버킷당 1~2건으로 고르다.**
그렇다면 해석이 둘로 갈린다:

  (가) **두 기전이 진짜로 교대한다** — 자해와 리셋이 다른 원인이고 우세가 시간에 따라 바뀐다
       → REQ-0116 은 그중 하나를 줄인다
  (나) **원인은 하나인데 표지가 변한다** — 끊김을 만드는 무엇이 일정하게 있고
       `busy`·배너·`0.0.0.0` 은 그 아래에서 갈리는 **증상**이다
       → **REQ-0116 을 구워도 총 끊김은 안 준다.** 자해 0 · 리셋 증가로 나타나고 총계는 그대로

**(나)를 미리 예상하지 않으면 그 결과를 개선으로도 회귀로도 읽을 수 있다.**
이 도구는 (가)/(나)를 **가르지 못한다.** 다만 그 첫 자료인 "총 발생률이 흔들리나"를 잰다.

## 검정 방법 — 왜 이걸 골랐나

균질 포아송 과정에서 **사건 수 N 을 조건으로 하면 사건 시각은 창 위 균등분포의 순서통계량**이다.
그래서 `u_i = (t_i - 시작)/(창 길이)` 가 U(0,1) 인지 **KS 검정**한다.
버킷을 쪼개 카이제곱을 하지 않는 이유: **버킷 경계를 골라야 하고, 그 선택이 결과를 만든다**
(원장 1.12 에서 경계한 그 형태). KS 는 경계가 필요 없다.

## 🔴 이 도구가 **못 하는 것** — 결론에 반드시 같이 적어라

**"기각 못 했다"는 "일정하다"가 아니다.** 사건이 7건이면 어지간한 변화는 못 잡는다.
그래서 이 도구는 **검정 결과와 함께 검정력을 같이 계산해 찍는다** — 무엇을 잡을 수 있었는지
모르면 "차이 없음"이 차이가 없어서인지 표본이 없어서인지 못 가른다(원장 1.4).

사용: python3 monitor/rate_stability.py [로그] [시작ISO] [끝ISO]
"""
from __future__ import annotations

import os
import re
import sys
from datetime import datetime, timedelta

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-newbase.log"
SINCE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 16, 17, 53, 7)
UNTIL = datetime.fromisoformat(sys.argv[3]) if len(sys.argv) > 3 else datetime(2026, 8, 17, 1, 53, 0)

TS = re.compile(rb"^(\d{2}):(\d{2}):(\d{2})\s+(.*)$")
HDR_DATE = re.compile(rb"date=(\d{4})-(\d{2})-(\d{2})")
EV = "전송 3회 연속 실패".encode()

N_TRIAL = 4000
ALPHA = 0.05
SEED = 20260816


def parse(path: str):
    base = None
    prev = None
    day = 0
    with open(path, "rb") as f:
        for raw in f:
            if base is None:
                m = HDR_DATE.search(raw)
                if m:
                    base = datetime(int(m.group(1)), int(m.group(2)), int(m.group(3)))
            m = TS.match(raw)
            if not m:
                continue
            cur = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
            if prev is not None and cur < prev - 3600:
                day += 1          # 자정 넘김 (원장 2.2)
            prev = cur
            if base is None:
                continue
            yield base + timedelta(days=day, seconds=cur), raw


def ks_stat(us: list[float]) -> float:
    """U(0,1) 에 대한 양측 KS 통계량."""
    n = len(us)
    s = sorted(us)
    d = 0.0
    for i, u in enumerate(s, 1):
        d = max(d, i / n - u, u - (i - 1) / n)
    return d


def ks_pvalue(d: float, n: int, rng) -> float:
    """몬테카를로 p 값 — n 이 작아 점근식(Kolmogorov 분포)을 믿지 않는다."""
    hit = 0
    for _ in range(N_TRIAL):
        us = [rng.random() for _ in range(n)]
        if ks_stat(us) >= d:
            hit += 1
    return hit / N_TRIAL


def crit_value(n: int, rng) -> float:
    """유의수준 ALPHA 의 임계값 — 검정력 계산에 쓴다."""
    ds = sorted(ks_stat([rng.random() for _ in range(n)]) for _ in range(N_TRIAL))
    return ds[int((1 - ALPHA) * N_TRIAL)]


def power_step(n: int, ratio: float, crit: float, rng) -> float:
    """창 후반의 발생률이 `ratio` 배로 바뀌었을 때 이 검정이 잡아낼 확률.

    사건 수 n 을 고정한 조건부 검정이므로, 계단형 강도에서 뽑은 균등 표본을 만든다.
    (전반 강도 1, 후반 강도 ratio → 후반에 뽑힐 확률 = ratio/(1+ratio))
    """
    p_late = ratio / (1.0 + ratio)
    hit = 0
    for _ in range(N_TRIAL):
        us = []
        for _ in range(n):
            if rng.random() < p_late:
                us.append(0.5 + 0.5 * rng.random())
            else:
                us.append(0.5 * rng.random())
        if ks_stat(us) >= crit:
            hit += 1
    return hit / N_TRIAL


def main() -> int:
    import random
    rng = random.Random(SEED)

    p = os.path.abspath(LOG)
    print(f"# 원본 로그: {p}")
    print("# 물음: **총 발생률**이 창 안에서 일정한가 (갈래와 무관)")
    print("# 검정: 사건 시각의 균등성 KS · 몬테카를로 p · 버킷 경계를 고르지 않는다")

    rows = list(parse(p))
    if not rows:
        print("⚠ 파싱된 줄이 0 이다.")
        return 1
    last = rows[-1][0]
    lo, hi = max(SINCE, rows[0][0]), min(UNTIL, last)   # 미래를 분모에 넣지 않는다(1.2)
    if hi <= lo:
        print("⚠ 요청 구간에 자료가 없다.")
        return 1
    span = (hi - lo).total_seconds()
    print(f"# 구간 {lo} ~ {hi}  ({span/3600:.2f}h)")
    if UNTIL > last:
        print(f"  ⓘ 분모 보정: 요청 끝 {UNTIL} → 자료 끝 {last}")

    ev = [t for t, r in rows if EV in r and lo <= t <= hi]
    n = len(ev)
    print(f"\n## 사건 {n}건 · 평균 발생률 {n/(span/3600):.2f}/h")
    if n < 3:
        print("⚠ 사건이 3건 미만이면 발생률의 안정성을 논할 수 없다. 여기서 멈춘다.")
        return 0

    gaps = [(ev[i] - ev[i-1]).total_seconds() / 60 for i in range(1, n)]
    mean_g = sum(gaps) / len(gaps)
    var_g = sum((g - mean_g) ** 2 for g in gaps) / len(gaps)
    cv = (var_g ** 0.5) / mean_g if mean_g else 0
    print(f"\n## 사건 간격 (분)")
    print("   " + " · ".join(f"{g:.1f}" for g in gaps))
    print(f"   평균 {mean_g:.1f} · 변동계수 CV = {cv:.2f}")
    print("   ⓘ 균질 포아송이면 CV≈1.0 이 기대값이다. 1 보다 크게 크면 **뭉침**,")
    print("      작으면 **규칙적**이라는 뜻이다. (표본이 적으면 CV 자체가 크게 흔들린다)")

    us = [((t - lo).total_seconds()) / span for t in ev]
    d = ks_stat(us)
    pv = ks_pvalue(d, n, rng)
    print(f"\n## 균등성 KS 검정")
    print(f"   D = {d:.3f} · p = {pv:.3f}")
    verdict = "기각하지 못한다" if pv >= ALPHA else "기각한다"
    print(f"   → 유의수준 {ALPHA} 에서 '발생률이 일정하다'를 **{verdict}**")

    print(f"\n## 🔴 그런데 이 검정이 무엇을 잡을 수 있었나 (사건 {n}건 기준)")
    crit = crit_value(n, rng)
    print(f"   임계값 D* = {crit:.3f} · 반복 {N_TRIAL:,}회")
    print("   후반 발생률이 X배로 바뀌었을 때 잡아낼 확률:")
    for ratio in (2.0, 3.0, 5.0, 10.0):
        pw = power_step(n, ratio, crit, rng)
        flag = "  ← 잡을 수 있다" if pw >= 0.80 else ""
        print(f"     {ratio:4.0f}배   {pw:.2f}{flag}")

    print("\n## 읽는 법 — ⚠ 여기가 제일 중요하다")
    print("   · **'기각 못 했다'는 '일정하다'가 아니다.** 위 검정력 표에서 0.80 을 넘는 칸이")
    print("     하나도 없으면, 이 자료는 **어떤 변화도 배제하지 못한다.**")
    print("   · 이 도구는 (가)두 기전 교대 / (나)한 원인·표지 변화 를 **가르지 못한다.**")
    print("     총 발생률이 흔들리지 않는데 표지 비율만 크게 흔들리면 (나) 쪽이 세지지만,")
    print("     **그것도 방향의 증거이지 확정이 아니다.** 원인 확정은 실물 확인이 필요하다.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
