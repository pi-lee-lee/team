#!/usr/bin/env python3
"""A 창의 **시간축** — 총계로 접기 전에 그 안에서 성격이 변했는지 본다 (원장 1.12).

왜 필요한가
  긴 창은 표본을 늘리지만 **그 안에서 조건이 변할 여지도 키운다.**
  총계만 내면 서로 다른 두 구간을 하나로 평균하게 되고, 그건 CLAUDE.md 가 금한 비교를
  **판정문 안쪽에서** 저지르는 것이다. 실측으로 A 창 앞 2.4시간에 그 변화가 보였다.

🔴 버킷 규칙 — **자료를 보고 경계를 고르지 않는다** (루트 지시 2026-08-16 19:45)
  갈라진 자리에 맞춰 자르면 **그 갈라짐이 자료가 아니라 선택이 된다.**
  그래서 **벽시계 정렬 균등 30분** 버킷을 미리 정하고 그대로 낸다.
  (A 시작 시각에 맞추는 것도 선택이므로 피했다. 벽시계가 가장 중립적이다.)

🔴 이 도구는 **분류하지 않는다** — 표지 밀도와 사건 시각만 낸다.
  갈래는 `frozen-A/mech_split-v3.py` 가 정본이다. 분류기를 두 곳에 두면 **언젠가 갈라지고**,
  그때 어느 쪽이 맞는지 다투게 된다(오늘 여러 번 밟은 형태). 그래서 일부러 나눠 뒀다.

⚠ 개입 표시 — 원인 지목이 아니라 **기록**이다
  같은 기계에서 시험 서버 인스턴스가 여러 번 떴다. 운영 포트·보드와는 분리돼 있지만
  **CPU·네트워크를 함께 쓴다.** 표시 없이 두면 나중에 아무도 모른다(원장 4.4).
  ⚠ 그리고 시리얼 타임스탬프는 **줄을 읽은 시점**이라 CPU 경합이 지연 측정을 부풀릴 수 있다.

사용: python3 monitor/mech_timeline.py [로그] [시작ISO] [끝ISO]
"""
from __future__ import annotations

import os
import re
import sys
from datetime import datetime, timedelta

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-newbase.log"
SINCE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 16, 17, 53, 7)
UNTIL = datetime.fromisoformat(sys.argv[3]) if len(sys.argv) > 3 else datetime(2026, 8, 17, 1, 53, 0)

BUCKET_MIN = 30            # ⚠ 미리 정한 값. 자료를 보고 바꾸지 마라

TS = re.compile(rb"^(\d{2}):(\d{2}):(\d{2})\s+(.*)$")
HDR_DATE = re.compile(rb"date=(\d{4})-(\d{2})-(\d{2})")

EV = "전송 3회 연속 실패".encode()
BUSY = b"busy"
BANNER = b"System Ready"
NOIP = b"0.0.0.0"
SFRAME = b"[TX] S,"

# 선언된 개입 — 같은 기계에 뜬 시험 서버 인스턴스(루트 승인).
# 출처: ~/parking-logs/parking-server.test*.log 의 `=== INSTANCE … start=` 줄 전수.
# ⚠ **원인으로 지목하는 것이 아니다.** 창 안에 있었다는 사실을 자료에 남기는 것이다.
INTERVENTIONS = [
    ("2026-08-16T17:50:28", "시험 인스턴스 pid=71321"),
    ("2026-08-16T18:07:13", "시험 인스턴스 pid=4255"),
    ("2026-08-16T18:08:44", "시험 인스턴스 pid=7963"),
    ("2026-08-16T18:11:25", "시험 인스턴스 pid=13619"),
    ("2026-08-16T18:41:55", "시험 인스턴스 pid=72788"),
    ("2026-08-16T18:43:20", "시험 인스턴스 pid=75775"),
    ("2026-08-16T18:47:58", "시험 인스턴스 pid=85025"),
    ("2026-08-16T18:56:49", "시험 인스턴스 pid=5501"),
    ("2026-08-16T19:01:43", "시험 인스턴스 pid=16279"),
    ("2026-08-16T19:14:22", "시험 인스턴스 pid=43488"),
    ("2026-08-16T19:15:32", "시험 인스턴스 pid=46361"),
]


def parse(path: str):
    """(시각, 원문바이트). 자정 넘김 처리 — A 는 01:53 에 끝난다(원장 2.2)."""
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
                day += 1
            prev = cur
            if base is None:
                continue
            yield base + timedelta(days=day, seconds=cur), raw


def floor_bucket(t: datetime) -> datetime:
    """벽시계 정렬 — 30분이면 :00 과 :30 에 경계가 선다."""
    return t.replace(minute=(t.minute // BUCKET_MIN) * BUCKET_MIN, second=0, microsecond=0)


def main() -> int:
    p = os.path.abspath(LOG)
    print(f"# 원본 로그: {p}")
    print(f"# 버킷: **벽시계 정렬 균등 {BUCKET_MIN}분** — 자료를 보고 경계를 고르지 않았다")
    print("# 이 도구는 분류하지 않는다. 갈래는 frozen-A/mech_split-v3.py 가 정본이다.")

    rows = list(parse(p))
    if not rows:
        print("⚠ 파싱된 줄이 0 이다.")
        return 1
    first, last = rows[0][0], rows[-1][0]
    lo, hi = max(SINCE, first), min(UNTIL, last)     # 분모에 미래를 넣지 않는다(1.2)
    if hi <= lo:
        print(f"⚠ 요청 구간에 자료가 없다 ({SINCE} ~ {UNTIL}).")
        return 1
    print(f"# 구간 {lo} ~ {hi}  ({(hi - lo).total_seconds()/3600:.2f}h)")
    if UNTIL > last:
        print(f"  ⓘ 분모 보정: 요청 끝 {UNTIL} → 자료 끝 {last}")

    buckets: dict[datetime, dict[str, int]] = {}
    events: list[datetime] = []
    b = floor_bucket(lo)
    while b <= hi:
        buckets[b] = {"ev": 0, "busy": 0, "banner": 0, "noip": 0, "frame": 0}
        b += timedelta(minutes=BUCKET_MIN)

    for t, r in rows:
        if not (lo <= t <= hi):
            continue
        k = floor_bucket(t)
        if k not in buckets:
            continue
        if EV in r:
            buckets[k]["ev"] += 1
            events.append(t)
        if BUSY in r:
            buckets[k]["busy"] += 1
        if BANNER in r:
            buckets[k]["banner"] += 1
        if NOIP in r:
            buckets[k]["noip"] += 1
        if SFRAME in r:
            buckets[k]["frame"] += 1

    iv = [(datetime.fromisoformat(a), d) for a, d in INTERVENTIONS]

    print(f"\n## 시간축 ({BUCKET_MIN}분 균등 · 벽시계 정렬)\n")
    print("  버킷        사건  busy  배너  0.0.0.0   S프레임   개입")
    print("  " + "-" * 66)
    for k in sorted(buckets):
        v = buckets[k]
        n_iv = sum(1 for t, _ in iv if k <= t < k + timedelta(minutes=BUCKET_MIN))
        mark = "◆" * n_iv
        flag = " ←사건" if v["ev"] else ""
        print(f"  {k:%m-%d %H:%M}  {v['ev']:4d}  {v['busy']:4d}  {v['banner']:4d}  {v['noip']:6d}  {v['frame']:7d}   {mark}{flag}")

    print("\n  ◆ = 같은 기계에 시험 서버 인스턴스가 뜬 횟수(선언된 개입 · **원인 지목 아님**)")

    print(f"\n## 사건 시각 {len(events)}건 — 갈래는 mech_split-v3.py 로 확인하라")
    for t in events:
        near = [(abs((t - it).total_seconds()), it, d) for it, d in iv]
        near.sort()
        gap, it, d = near[0]
        warn = "  🔴 개입과 근접" if gap <= 120 else ""
        print(f"  {t:%m-%d %H:%M:%S}   가장 가까운 개입 {it:%H:%M:%S} ({int(gap)}초 차){warn}")

    print("\n## 읽는 법")
    print("  · 버킷 경계는 **미리 정한 균등 간격**이다. 자료가 갈라진 자리에 맞춘 것이 아니다.")
    print("  · 개입(◆)이 **양쪽 구간에 다 있으면** 그것만으로 구간 차이를 설명할 수 없다.")
    print("    반대로 한쪽에만 몰려 있으면 그 상관을 **반드시 판정문에 적어라.**")
    print("  · 사건 수가 버킷당 0~2 이면 추세를 말할 수 없다. **모양만 보고 기전을 붙이지 마라.**")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
