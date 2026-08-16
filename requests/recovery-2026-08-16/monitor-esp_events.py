#!/usr/bin/env python3
"""ESP 단독 사건 판별기 — 'uptime 은 이어지는데 세션만 끊긴' 구간을 찾는다.

루트 정정(REQ-0093): WiFi 모듈만 전원이 끊긴 사건이 있었다. 그 서명은
  · MCU `uptime` 은 **이어진다**(아두이노는 계속 돌았다)
  · 세션은 끊긴다, 사유는 `errno=54`(ESP 가 FIN 없이 사라진다)가 전형
보드 전체 전원 사건이면 `uptime` 이 0 부근으로 리셋된다.

S 프레임 5번째 필드가 uptime(초)이다. 세션이 끊긴 자리마다 **끊기기 직전 프레임의 uptime** 과
**다시 붙은 뒤 첫 프레임의 uptime** 을 벽시계 경과와 비교하면 둘이 갈린다:

    이어짐(ESP 단독)  : uptime_after ≈ uptime_before + 벽시계경과
    리셋(MCU 재부팅)  : uptime_after ≈ 벽시계경과보다 훨씬 작음(0 부근에서 다시 셈)

이 판별은 **시리얼 캡처가 없는 과거 구간까지 소급된다** — 그래서 캡처보다 덮는 기간이 훨씬 넓다.

사용: python3 monitor/esp_events.py > monitor/out-esp-events.txt
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
ap.add_argument("--since", default="2026-08-15T18:42:00")
ap.add_argument("--tol", type=float, default=25.0, help="uptime 연속 판정 허용오차(초)")
ap.add_argument("--min-gap", type=float, default=0.0, help="이 초 이상 공백만 본다")
ap.add_argument("--team-reset", action="append", default=[],
                help="ISO=설명 — 팀이 만든 리셋. 공백 안에 들어 있으면 판정하지 않는다")
ap.add_argument("--team-range", action="append", default=[],
                help="ISO..ISO=설명 — 팀이 장치를 만지고 있던 구간. 겹치면 판정하지 않는다")
ap.add_argument("--from-interventions", action="store_true",
                help="arduino/INTERVENTIONS.md 를 직접 읽어 팀 개입을 채운다(권장)")
args = ap.parse_args()

TEAM = []
for _tr in args.team_reset:
    _iso, _, _lab = _tr.partition("=")
    TEAM.append((datetime.fromisoformat(_iso), _lab or "팀 활동"))

TEAM_RANGES = []
for _rr in args.team_range:
    _rng, _, _lab = _rr.partition("=")
    _s, _, _e = _rng.partition("..")
    TEAM_RANGES.append((datetime.fromisoformat(_s), datetime.fromisoformat(_e), _lab or "팀 작업"))

if args.from_interventions:
    # 목록을 두 곳에 두면 갈라진다. 원본은 arduino/INTERVENTIONS.md 하나다.
    from interventions import parse as _parse_iv
    _pts, _rngs = _parse_iv()
    for _t, _a, _approx in _pts:
        TEAM.append((_t, _a + ("(대략)" if _approx else "")))
    for _s, _e, _a in _rngs:
        TEAM_RANGES.append((_s, _e, _a))

events, meta = parse_log(args.log, date.fromisoformat(args.base_date))
since = datetime.fromisoformat(args.since)

frames = [e for e in events if e.kind == "frame" and e.data["uptime"] is not None]
closes = [e for e in events if e.kind == "sess_close" and e.ts >= since]

print("# ESP 단독 사건 판별 — 'uptime 이 이어지는데 세션만 끊긴' 구간")
print(f"# 대상: {since} 이후 세션 종료 {len(closes)}건 · 허용오차 {args.tol}초")
print("#")
print("# 판정 기호:  ESP단독 = MCU 는 계속 돌았다(ESP/링크 사건)")
print("#             MCU재부팅 = 보드가 다시 켜졌다")
print("#             판정불가 = 앞뒤 프레임이 없어 비교 불가 (표본 없음 — 건강의 증거가 아니다)")
print()

rows = []
for c in closes:
    before = None
    for f in frames:
        if f.ts <= c.ts:
            before = f
        else:
            break
    after = None
    for f in frames:
        if f.ts > c.ts:
            after = f
            break
    if before is None or after is None:
        rows.append((c, None, None, None, None, "판정불가(앞뒤 프레임 없음)"))
        continue

    wall = (after.ts - before.ts).total_seconds()
    if wall < args.min_gap:
        continue
    ub, ua = before.data["uptime"], after.data["uptime"]
    expect = ub + wall
    diff = ua - expect
    # 공백 안에 팀이 만든 리셋(재플래싱·DTR)이 있으면 그 재부팅은 **우리가 만든 것**이다.
    # 이걸 안 걸러내면 장치가 스스로 죽은 것처럼 집계된다 — REQ-0092 §② 가 겪은 오염이다.
    inside = [lab for t, lab in TEAM if before.ts < t <= after.ts]
    if inside:
        rows.append((c, ub, ua, wall, None, f"판정보류(공백 안에 팀 리셋: {inside[0]})"))
        continue

    if ua < ub:
        # uptime 이 **뒤로 갔다** — 허용오차와 무관하게 재부팅이다.
        # (짧은 공백에서는 tol 이 이 경우를 삼켜 버려 실제로 오분류가 났다)
        verdict = "MCU재부팅"
    elif abs(diff) <= args.tol:
        verdict = "ESP단독"
    elif ua < expect - args.tol:
        # 앞으로 갔지만 벽시계보다 덜 갔다 = 중간에 재부팅하고 조금 돌았다
        verdict = "MCU재부팅"
    else:
        verdict = "이상(uptime 이 기대보다 큼)"
    rows.append((c, ub, ua, wall, diff, verdict))

counts: dict[str, int] = {}
for c, ub, ua, wall, diff, verdict in rows:
    counts[verdict] = counts.get(verdict, 0) + 1
    if ub is None:
        print(f"{c.ts}  세션#{c.data['n']:<3d} {c.data['reason'][:34]:<34s}  {verdict}")
        continue
    dtxt = f"차 {diff:+.0f}" if diff is not None else "차 —"
    print(f"{c.ts}  세션#{c.data['n']:<3d} {c.data['reason'][:34]:<34s}  "
          f"공백{wall:7.1f}s  uptime {ub}→{ua} (기대 {ub + wall:.0f}, {dtxt})  {verdict}")

print()
print("# 판정 집계")
for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
    print(f"  {k:<28s} {v}")

print()
print("# 사유별 × 판정 교차")
cross: dict[tuple[str, str], int] = {}
for c, ub, ua, wall, diff, verdict in rows:
    cross[(c.data["reason"], verdict)] = cross.get((c.data["reason"], verdict), 0) + 1
reasons = sorted({r for r, _ in cross})
verdicts = sorted({v for _, v in cross})
print("  " + " " * 38 + "  ".join(f"{v:>16s}" for v in verdicts))
for r in reasons:
    cells = "  ".join(f"{cross.get((r, v), 0):>16d}" for v in verdicts)
    print(f"  {r[:36]:<38s}{cells}")
