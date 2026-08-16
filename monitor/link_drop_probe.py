#!/usr/bin/env python3
"""**재부팅 없이 링크만 끊긴** 사례를 센다 — 두 번째 메커니즘이 있는지 확인한다.

물음(루트 REQ 지시):
  1. 10:51:02 같은 양상이 **몇 번** 있나? 1회뿐이면 조사 가치가 낮다.
     **표본 1건에 기전을 붙이지 않는다.**
  2. 끊은 주체가 장치가 아니라면 **서버 쪽 회수(유휴 마감·keepalive)**인가 **TCP 레벨**인가?

판별 원리 — `uptime` 은 Uno 의 millis()/1000 이다(arduino-engineer 확인).
  새 연결 직후 첫 S 프레임의 uptime 이
    작다(≲120s)  → 방금 부팅했다 = **재부팅**
    크다(>120s)  → 장치는 계속 살아 있었다 = **링크만 끊겼다**  ← 찾는 것

두 양상을 가른다:
  (A) 재연결형  — 연결이 끊겼다가 새로 붙었는데 장치는 안 죽었다
  (B) 같은연결형 — TCP 는 유지된 채 프레임만 멈췄다(서버가 `같은 연결` 복구로 기록)

⚠ 오염 구간(2026-08-16 15:26~)은 기본으로 보지 않는다. 사용자가 보드를 뽑았다 끼운 구간이다.

사용: python3 monitor/link_drop_probe.py [로그] [시작ISO] [끝ISO]
"""
from __future__ import annotations

import re
import sys
from datetime import date, datetime, timedelta

LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/parking-soak.log"
SINCE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 15, 20, 32, 16)
UNTIL = datetime.fromisoformat(sys.argv[3]) if len(sys.argv) > 3 else datetime(2026, 8, 16, 15, 26, 0)
BASE = date(2026, 8, 16)
CONTAM = datetime(2026, 8, 16, 15, 26, 0)

# 재부팅으로 볼 uptime 상한. 이보다 크면 "장치는 살아 있었다".
BOOT_UPTIME_MAX = 120

TS = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\s+(.*)$")
SFRAME = re.compile(r"←ARD\s+S,(\d+),([01]+),([01]+),(\d+),(\w+),(\d+)")


def parse(path: str):
    """뒤에서 앞으로 날짜 앵커링(soak_stats.py 와 같은 방식). 앞에서 잡으면 하루씩 밀린다."""
    with open(path, "rb") as f:
        raw = f.read()
    rows = []
    for line in raw.decode("utf-8", "replace").splitlines():
        m = TS.match(line)
        if m:
            rows.append((int(m[1]), int(m[2]), int(m[3]), m[4]))
    stamps = [None] * len(rows)
    day, nxt = BASE, None
    for i in range(len(rows) - 1, -1, -1):
        h, mi, s, _ = rows[i]
        hms = (h, mi, s)
        if nxt is not None and hms > nxt:
            day -= timedelta(days=1)
        nxt = hms
        stamps[i] = datetime.combine(day, datetime.min.time()).replace(hour=h, minute=mi, second=s)
    return [(ts, r[3]) for r, ts in zip(rows, stamps)]


def main() -> int:
    if UNTIL > CONTAM:
        print(f"⚠ 요청 구간이 오염 경계({CONTAM})를 넘는다 — 그 뒤는 사람이 보드를 만진 구간이다.\n")

    ev = [(t, s) for t, s in parse(LOG) if SINCE <= t <= UNTIL]
    frames = [(t, int(SFRAME.search(s).group(4))) for t, s in ev if SFRAME.search(s)]
    print(f"# 구간 {SINCE} ~ {UNTIL}   ({(UNTIL - SINCE).total_seconds()/3600:.2f}h)")
    print(f"# S프레임 {len(frames):,}건")
    if len(frames) < 100:
        print("⚠ 프레임이 너무 적다 — 이 구간으로는 판별할 수 없다.")
        return 1

    accepts = [t for t, s in ev if "+? 연결 수락" in s]
    same_conn = [(t, s) for t, s in ev if "온라인 복귀" in s and "같은 연결" in s]
    reconn = [(t, s) for t, s in ev if "온라인 복귀" in s and "재연결" in s]

    # ── (A) 재연결형: ACCEPT 직후 첫 프레임의 uptime 이 크면 장치는 안 죽었다 ──
    print()
    print("## (A) 재연결형 — 붙었는데 장치는 이미 오래 살아 있던 경우")
    print(f"   ACCEPT {len(accepts)}건 검사 · uptime > {BOOT_UPTIME_MAX}s 이면 '재부팅 아님'")
    print()
    alive, booted, nodata = [], 0, 0
    for t in accepts:
        nxt = next((f for f in frames if f[0] >= t), None)
        if not nxt or (nxt[0] - t).total_seconds() > 180:
            nodata += 1
            continue
        if nxt[1] > BOOT_UPTIME_MAX:
            alive.append((t, nxt[0], nxt[1]))
        else:
            booted += 1
    print(f"   재부팅(uptime 작음)        {booted}건")
    print(f"   🔑 재부팅 아님(uptime 큼)  {len(alive)}건")
    print(f"   판별불가(직후 프레임 없음) {nodata}건")
    if alive:
        print()
        print("   ACCEPT시각   첫프레임   uptime   직전 60초 서버 이벤트")
        for t, ft, up in alive[:20]:
            pre = [s for tt, s in ev if 0 < (t - tt).total_seconds() <= 60
                   and any(k in s for k in ("회수", "종료", "errno", "오프라인"))]
            tag = "; ".join(x[:46] for x in pre[-2:]) or "(없음)"
            print(f"   {t.strftime('%m-%d %H:%M:%S')}  +{(ft-t).total_seconds():>4.0f}s  {up:>6}   {tag}")

    # ── (B) 같은연결형: TCP 는 살아 있는데 프레임만 멈춘 경우 ──
    print()
    print("## (B) 같은연결형 — TCP 는 유지된 채 프레임만 멈춘 경우")
    print(f"   같은 연결 복구 {len(same_conn)}건 · 재연결 복구 {len(reconn)}건")

    # ── 끊은 주체 분류: 서버 회수인가 TCP 레벨인가 ──
    print()
    print("## 끊은 주체 — 서버가 회수했나, TCP 가 끊겼나")
    def count(*keys):
        return sum(1 for _, s in ev if any(k in s for k in keys))
    rows = [
        ("서버 회수(유휴 마감)", count("유휴 마감")),
        ("서버 회수(keepalive)", count("keepalive 시간초과", "회수 — keepalive")),
        ("TCP 리셋 errno=54", count("errno=54")),
        ("keepalive errno=60", count("errno=60")),
        ("id 미상 마감", count("id 미상 소켓 마감")),
        ("오프라인 판정", count("오프라인 판정")),
    ]
    for name, n in rows:
        print(f"   {name:24} {n:>5}")

    print()
    print("## 판정")
    n = len(alive)
    if n == 0:
        print("   재부팅 없는 재연결 0건 — 이 구간에는 두 번째 메커니즘의 증거가 없다.")
        print("   ⚠ 단 이 0 은 'ACCEPT 자체가 드물다'일 수도 있다. ACCEPT 총계를 함께 보라.")
    elif n == 1:
        print("   🟡 1건뿐이다. **기전을 붙이지 마라** — 표본 1로는 반복성을 말할 수 없다.")
    else:
        print(f"   🟢 {n}건 반복된다. 우연 1회가 아니라 **되풀이되는 양상**이다.")
        span = (alive[-1][0] - alive[0][0]).total_seconds() / 3600
        if span > 0:
            print(f"   {alive[0][0].strftime('%m-%d %H:%M')} ~ {alive[-1][0].strftime('%m-%d %H:%M')}"
                  f" ({span:.1f}h) 에 걸쳐 {n}건 = {n/span:.2f}건/h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
