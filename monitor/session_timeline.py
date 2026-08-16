#!/usr/bin/env python3
"""세션 개시/종료 타임라인 — '언제부터 나빠졌나'를 경계와 무관하게 본다.

구간을 먼저 정해 놓고 대조하면, **경계 이전부터 진행되던 변화**를 새 펌웨어 탓으로
돌리는 오류가 난다(옛 펌웨어는 플래싱 직전 이미 errno=54 로 무너지고 있었다 — REQ-0092).
그래서 이 도구는 구간을 나누지 않고 **시간순으로 그대로 늘어놓는다.**

사용: python3 monitor/session_timeline.py --since 2026-08-16T08:00:00 > monitor/out-timeline.txt
"""
from __future__ import annotations

import argparse
import sys
from datetime import date, datetime

sys.path.insert(0, "monitor")
from soak_stats import parse_log  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--log", default="/tmp/parking-soak.log")
ap.add_argument("--base-date", default="2026-08-16")
ap.add_argument("--since", default="2026-08-16T08:00:00")
ap.add_argument("--marks", action="append", default=[], help="ISO=설명 — 경계 표시")
args = ap.parse_args()

events, meta = parse_log(args.log, date.fromisoformat(args.base_date))
since = datetime.fromisoformat(args.since)
marks = []
for m in args.marks:
    iso, _, label = m.partition("=")
    marks.append((datetime.fromisoformat(iso), label))
marks.sort()

rows = []
for e in events:
    if e.ts < since:
        continue
    if e.kind == "sess_close":
        rows.append((e.ts, "close", e.data))
    elif e.kind == "sess_open":
        rows.append((e.ts, "open", e.data))
    elif e.kind == "srv_start":
        rows.append((e.ts, "srv_start", {}))

print(f"# 세션 타임라인 — {since} 이후")
print("# 'dur' 은 그 세션이 살아 있던 시간. 짧아지면 링크가 불안정해지고 있다는 뜻.")
print()

mi = 0
for ts, kind, d in rows:
    while mi < len(marks) and marks[mi][0] <= ts:
        print(f"  ────────── {marks[mi][0]}  ★ {marks[mi][1]} ──────────")
        mi += 1
    if kind == "open":
        print(f"{ts}  + 세션#{d['n']} 개시")
    elif kind == "srv_start":
        print(f"{ts}  ⏱ 서버 기동")
    else:
        m, s = divmod(d["dur_s"], 60)
        print(f"{ts}  - 세션#{d['n']} 종료  {m:3d}m{s:02d}s  프레임{d['frames']:5d}  ← {d['reason']}")
while mi < len(marks):
    print(f"  ────────── {marks[mi][0]}  ★ {marks[mi][1]} ──────────")
    mi += 1

# 시간별 종료사유 버킷
print()
print("# 시간대별 세션 종료 사유")
buckets: dict[str, dict[str, int]] = {}
for ts, kind, d in rows:
    if kind != "close":
        continue
    key = ts.strftime("%m-%d %H시")
    buckets.setdefault(key, {})
    buckets[key][d["reason"]] = buckets[key].get(d["reason"], 0) + 1
for k in sorted(buckets):
    parts = ", ".join(f"{r}×{n}" for r, n in sorted(buckets[k].items(), key=lambda kv: -kv[1]))
    print(f"  {k}  총{sum(buckets[k].values()):2d}  {parts}")
