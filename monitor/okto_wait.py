#!/usr/bin/env python3
"""게이트가 포기한 뒤 **얼마나 더 기다렸으면 됐나** — `SEND_OK_TIMEOUT_MS` 근거 (REQ-0154).

앵커 : `[TX-WAIT] ★ SEND OK 상한 초과 → 대기를 푼다`
측정 : 그 앵커 **이후** 처음 나타나는 `SEND OK` 까지의 경과

🔴 귀속하지 마라 — 로그는 `SEND OK` 가 어느 명령의 응답인지 안 적는다.
   받칠 수 있는 표현: **"앵커 이후 다음 `SEND OK` 가 나타나기까지 N 초 동안 어떤 `SEND OK` 도 없었다"**

갈래 셋(요청자가 측정 전에 고정한 규칙 — 결과를 보고 고치지 않는다):
    (a) 곧 왔다        앵커 뒤 `SEND OK` 가 나타났다 (그 사이 링크 붕괴 없음)
    (b) 끝내 안 왔다   `SEND OK` 전에 **링크 붕괴**(`전송 3회 연속 실패` / `★ 정지 감지`
                       / `[DIAG] offline step=`) 가 먼저 왔다
    (c) 짝을 못 지었다 `SEND OK` 전에 **다음 `AT+CIPSEND`** 가 먼저 왔다 → 구분 불가
⚠ **(b)와 (c)를 합치지 마라.** 다른 칸이다(REQ-0131 정정).

⚠ **1초 해상도.** Δ 의 실제 경과는 (Δ-1, Δ+1) 이다. **평균·백분위수를 만들지 않는다.**
⚠ 집계로 뭉개지 말 것 — **시각을 전부 남긴다**(요청자 지적: 22/25 가 50분에 몰려 있다).

사용: python3 monitor/okto_wait.py [시리얼로그]
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zeroguard import calib, z  # noqa: E402

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-win4.log"

TS = re.compile(rb"^(\d\d):(\d\d):(\d\d)\s+(.*)$")
ANCHOR = "상한 초과".encode()
SENDOK = b'"SEND OK"'
CIPSEND = b"AT+CIPSEND"
COLLAPSE = ["전송 3회 연속 실패".encode(), "★ 정지 감지".encode(), b"[DIAG] offline step="]

# 개입 기록(monitor/HANDOFF-win4.md §6.35) — 겹침 대조용
INTERV = [("사진 촬영 1", "08:14:00", "08:14:59"),
          ("사진 촬영 2", "08:23:00", "08:23:59"),
          ("그라운드선 이탈", "11:25:00", "11:40:00")]


def sec(x: str) -> int:
    h, m, s = (int(v) for v in x.split(":"))
    return h * 3600 + m * 60 + s


def hms(t: int) -> str:
    return f"{t // 3600:02d}:{t // 60 % 60:02d}:{t % 60:02d}"


def main() -> int:
    rows, n = [], 0
    with open(LOG, "rb") as f:
        for raw in f:
            n += 1
            m = TS.match(raw.rstrip(b"\r\n"))
            if m:
                rows.append((sec(f"{int(m[1])}:{int(m[2])}:{int(m[3])}"
                                 .replace(":", ":")) if False else
                             int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3]), m[4]))

    print(f"# 원본: {os.path.abspath(LOG)}")
    print("# 앵커 = `[TX-WAIT] ★ SEND OK 상한 초과` · 측정 = 그 뒤 첫 `SEND OK` 까지")
    print("# ⚠ 1초 해상도 · 평균/백분위수 없음 · `SEND OK` 를 특정 명령에 귀속하지 않는다")

    idx = [i for i, (_, b) in enumerate(rows) if ANCHOR in b]
    print("\n" + z("앵커(상한 초과)", len(idx), n, "상한 초과"))
    print(calib("앵커 탐지", len(idx), 1))
    if not idx:
        return 0

    recs = []
    for i in idx:
        t0 = rows[i][0]
        kind, gap = "c.짝못지음", None
        for j in range(i + 1, len(rows)):
            tj, bj = rows[j]
            if any(k in bj for k in COLLAPSE):
                kind, gap = "b.끝내안옴", tj - t0
                break
            if SENDOK in bj:
                kind, gap = "a.곧왔다", tj - t0
                break
            if CIPSEND in bj:
                kind, gap = "c.짝못지음", tj - t0
                break
        recs.append((t0, kind, gap))

    cnt = Counter(k for _, k, _ in recs)
    print("\n## 갈래별")
    for k in ("a.곧왔다", "b.끝내안옴", "c.짝못지음"):
        print(f"   {k:<12} {cnt.get(k, 0):>3}건")

    a = [(t, g) for t, k, g in recs if k == "a.곧왔다"]
    if a:
        h = Counter(g for _, g in a)
        print(f"\n## (a) Δ 히스토그램 — {len(a)}건 (1초 단위)")
        for kk in sorted(h):
            print(f"   Δ={kk}s  {h[kk]:>3}건  {'█' * min(h[kk], 40)}")
        mx = max(a, key=lambda x: x[1])
        print(f"   → 최대 Δ = {mx[1]}s ({hms(mx[0])}) · 실제 경과 ({mx[1]-1}, {mx[1]+1})초")

    print("\n## 전 건 시각 (🔴 집계로 뭉개지 마라 — 요청자 지적)")
    for t, k, g in recs:
        w = [nm for nm, a1, b1 in INTERV if sec(a1) <= t <= sec(b1)]
        tag = f"  ⚠ 개입겹침: {w[0]}" if w else ""
        print(f"   {hms(t)}  {k:<12} Δ={g if g is not None else '-'}{tag}")

    # 🔴 보조 측정 — **등록된 규칙은 안 고쳤다.** 왜 보조가 필요한지 같이 찍는다.
    #   등록 규칙에서 (c)"다음 CIPSEND 가 먼저 옴"이 24/25 로 나온다. 이유는 결함이 아니라
    #   **펌웨어 거동**이다: 상한이 만료되면 게이트가 풀리고 **즉시 CIPSEND 를 쏜다**(요청자 설명).
    #   → **조용한 창이 아예 안 생기므로 등록 규칙만으로는 "얼마나 더 기다렸으면 됐나"를 못 잰다.**
    #   그래서 **CIPSEND 를 지나쳐서** 다음 `SEND OK` 까지를 따로 잰다(링크 붕괴에서는 멈춘다).
    #   ⚠ 귀속은 여전히 불가 — "그 사이 어떤 SEND OK 도 없었다" 까지만 말한다.
    print("\n## 🔑 보조 측정 — CIPSEND 를 지나쳐 다음 `SEND OK` 까지 (등록 규칙 변경 아님)")
    sup = []
    for i in idx:
        t0 = rows[i][0]
        g, kind = None, "b.끝내안옴"
        for j in range(i + 1, len(rows)):
            tj, bj = rows[j]
            if any(k in bj for k in COLLAPSE):
                kind, g = "b.끝내안옴", tj - t0
                break
            if SENDOK in bj:
                kind, g = "a.왔다", tj - t0
                break
        sup.append((t0, kind, g))
    sc = Counter(k for _, k, _ in sup)
    for k in ("a.왔다", "b.끝내안옴"):
        print(f"   {k:<12} {sc.get(k, 0):>3}건")
    sa = [(t, g) for t, k, g in sup if k == "a.왔다"]
    if sa:
        hh = Counter(g for _, g in sa)
        print(f"\n   Δ 히스토그램 ({len(sa)}건):")
        for kk in sorted(hh):
            print(f"     Δ={kk}s  {hh[kk]:>3}건  {'█' * min(hh[kk], 40)}")
        mx2 = max(sa, key=lambda x: x[1])
        print(f"   → 최대 Δ = {mx2[1]}s ({hms(mx2[0])}) · 실제 경과 ({mx2[1]-1}, {mx2[1]+1})초")
    sb = [(t, g) for t, k, g in sup if k == "b.끝내안옴"]
    if sb:
        print(f"\n   🔴 링크 붕괴가 먼저 온 {len(sb)}건:")
        for t, g in sb:
            print(f"     {hms(t)}  → {g}s 뒤 붕괴")

    print("\n## 개입 대조 (monitor/HANDOFF-win4.md §6.35 기준)")
    for nm, a1, b1 in INTERV:
        c = sum(1 for t, _, _ in recs if sec(a1) <= t <= sec(b1))
        print(f"   {nm:<14} {a1}~{b1}  겹치는 앵커 **{c}건**")
    print("\n## 읽는 법")
    print("  · (a)=상한을 올리면 살아난다 / (b)=올려도 소용없다 / (c)=구분 불가. **(b)와 (c)를 합치지 마라.**")
    print("  · 시각이 몰려 있으면 **삽화**다. 상한 문제와 별개로 '무엇이 그 구간을 만들었나'가 남는다.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
