#!/usr/bin/env python3
"""개입-사건 근접의 **우연 기대치** — 놀라기 전에 계산한다 (원장 1.13 · RECIPE §4).

방법:
  1. 개입마다 ±R 초 구간을 잡고 **창 안으로 클립**한 뒤 **합집합** 길이를 구한다.
     (겹침을 중복으로 더하면 p 가 부풀려져 "우연이다"가 과대평가된다)
  2. p = 합집합 / 창 길이
  3. 사건이 창 안에 균등하게 떨어진다는 귀무가설 아래
     n 건 중 k 건 이상이 근접 구간에 들 이항 확률 P(X>=k)

⚠ 이 계산은 **"우연으로 설명되는가"만 말한다.** 우연 범위라는 것이
   "개입이 원인이 아니다"의 증명은 아니다. 표본이 작으면 둘 다 못 가른다.

사용:
  python3 monitor/coincidence.py                       # A 창 기본값(재현용 · 아래 상수)
  python3 monitor/coincidence.py <반경초> <창시작ISO> <창끝ISO> <개입파일> <사건파일>
      개입파일·사건파일 = ISO 시각 한 줄에 하나. `#` 로 시작하는 줄과 빈 줄은 무시한다.

⚠ **기본값(A 창)을 고치지 마라.** 인자 없이 돌리면 A 판정을 그대로 재현해야 한다 —
   그것이 이 도구가 거짓말을 안 한다는 유일한 확인 수단이다(원장 2.6, 교정 먼저).

🔴 창3 국면 2 용 반경은 **5초**다(루트 확정 2026-08-17 00:2x).
   A 는 개입이 창의 16% 를 덮어 인과를 못 갈랐는데(p=0.161), 주입은 **순간 사건**이라
   60건 × ±5초면 창의 약 1% 다. **몰리면 압도적이고 안 몰리면 무죄다.**
"""
from __future__ import annotations

import sys
from datetime import datetime
from math import comb


def _load(path: str) -> list:
    out = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if ln and not ln.startswith("#"):
                out.append(ln)
    return out


WIN_START = datetime.fromisoformat("2026-08-16T17:53:07")
WIN_END = datetime.fromisoformat("2026-08-16T21:04:50")
RADIUS = 120  # 초

INTERVENTIONS = [
    "2026-08-16T17:50:28",
    "2026-08-16T18:07:13",
    "2026-08-16T18:08:44",
    "2026-08-16T18:11:25",
    "2026-08-16T18:41:55",
    "2026-08-16T18:43:20",
    "2026-08-16T18:47:58",
    "2026-08-16T18:56:49",
    "2026-08-16T19:01:43",
    "2026-08-16T19:14:22",
    "2026-08-16T19:15:32",
]

EVENTS = [
    "2026-08-16T18:24:55",
    "2026-08-16T18:44:56",
    "2026-08-16T18:48:12",
    "2026-08-16T19:25:48",
    "2026-08-16T19:29:32",
    "2026-08-16T19:37:18",
    "2026-08-16T20:09:14",
    "2026-08-16T20:26:32",
    "2026-08-16T20:40:34",
    "2026-08-16T20:54:06",
]


def main() -> int:
    global WIN_START, WIN_END, RADIUS, INTERVENTIONS, EVENTS
    src = "A 창 기본값(내장 상수)"
    if len(sys.argv) == 6:
        RADIUS = int(sys.argv[1])
        WIN_START = datetime.fromisoformat(sys.argv[2])
        WIN_END = datetime.fromisoformat(sys.argv[3])
        INTERVENTIONS = _load(sys.argv[4])
        EVENTS = _load(sys.argv[5])
        src = f"인자 — 개입 {sys.argv[4]} · 사건 {sys.argv[5]}"
    elif len(sys.argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    print(f"# 입력: {src}")

    w0, w1 = WIN_START.timestamp(), WIN_END.timestamp()
    span = w1 - w0
    iv = [datetime.fromisoformat(s).timestamp() for s in INTERVENTIONS]
    ev = [datetime.fromisoformat(s).timestamp() for s in EVENTS]

    # 창 안으로 클립한 ±R 구간
    segs = []
    for t in iv:
        a, b = max(w0, t - RADIUS), min(w1, t + RADIUS)
        if b > a:
            segs.append((a, b))
    segs.sort()
    union = []
    for a, b in segs:
        if union and a <= union[-1][1]:
            union[-1][1] = max(union[-1][1], b)
        else:
            union.append([a, b])
    ulen = sum(b - a for a, b in union)
    p = ulen / span

    near = [(e, min(abs(e - t) for t in iv)) for e in ev]
    k = sum(1 for _, d in near if d <= RADIUS)
    n = len(ev)
    pk = sum(comb(n, i) * p**i * (1 - p) ** (n - i) for i in range(k, n + 1))

    print(f"# 창 {WIN_START} ~ {WIN_END}  ({span/3600:.2f}h · {span:.0f}s)")
    print(f"# 개입 {len(iv)}회 (창 밖은 클립되어 {len(segs)}개 구간) · 반경 ±{RADIUS}s")
    print()
    print(f"## 근접 구간 합집합 {ulen:.0f}s / {span:.0f}s → p = {p:.3f}")
    print(f"   (단순 합 {2*RADIUS*len(segs)}s 였다면 p={2*RADIUS*len(segs)/span:.3f} — 겹침 제거 전후 차이)")
    print()
    print(f"## 사건 {n}건 중 근접 {k}건")
    for e, d in near:
        mark = " 🔴근접" if d <= RADIUS else ""
        print(f"   {datetime.fromtimestamp(e):%m-%d %H:%M:%S}  최근접 개입 {d:6.0f}s{mark}")
    print()
    print(f"## P(X >= {k} | n={n}, p={p:.3f}) = {pk:.3f}")
    print(f"   기대 근접 건수 = n·p = {n*p:.2f}")
    print()
    print("## 읽는 법")
    print("   · **근접이 있었다**와 **우연 범위였다**를 둘 다 적어라.")
    print("     앞만 적으면 다음 사람이 놀라고, 뒤만 적으면 근접 사실이 사라진다.")
    print("   · p 가 크다는 것은 '개입이 창의 상당 부분을 덮었다'는 뜻이다 —")
    print("     그만큼 이 창에서는 개입-사건 인과를 **가를 수 없다.**")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
