#!/usr/bin/env python3
"""`arduino/INTERVENTIONS.md` 를 파싱한다 — 팀이 만든 리셋을 손으로 옮겨 적지 않기 위해.

**왜 파일을 직접 읽는가**: 나는 2026-08-16 에 개입 시각을 내 스크립트에 손으로 박아 넣었다가
`10:27:44`(arduino-engineer 의 시리얼 열기)를 빠뜨려 **그것을 "전원 사건 시작"으로 오판했다.**
목록을 두 곳에 두면 반드시 갈라진다. 원본은 하나여야 한다.

형식 계약(INTERVENTIONS.md 에 명시):
  | 시각 (KST)        | 행위 | 비고 |
  | 08-16 **10:34:15** | 플래싱 | ...        ← 점: 리셋이 확실한 단발
  | 08-15 17:49~19:31  | 시리얼 캡처 | ...   ← 범위: 그 동안 일어난 것(내 탓일 수도 아닐 수도)
  | 08-15 ~16:00       | ...                ← `~` 접두 = 대략. 점으로 쓰되 오차를 크게 준다

⚠ **깨진 것을 조용히 넘기지 않는다.** 표는 있는데 시각을 하나도 못 뽑으면 예외를 던진다.
   "개입 없음"과 "못 읽었음"은 다르다 — 후자를 전자로 읽으면 전부 장치 탓이 된다.
"""
from __future__ import annotations

import re
import sys
from datetime import datetime

PATH = "arduino/INTERVENTIONS.md"

# 08-16 **10:34:15**  /  08-15 17:49~19:31  /  08-16 ~10:29–10:31
ROW = re.compile(r"^\|\s*(\d{2})-(\d{2})\s+(.+?)\s*\|\s*(.+?)\s*\|\s*(.*?)\s*\|\s*$")
CLEAN = re.compile(r"[*~\s]")
TIME = re.compile(r"(\d{1,2}):(\d{2})(?::(\d{2}))?")


def parse(path: str = PATH, year: int = 2026):
    """→ (points, ranges)
    points: [(datetime, 행위, 대략여부)]
    ranges: [(start, end, 행위)]
    """
    points, ranges = [], []
    rows_seen = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = ROW.match(line.rstrip("\n"))
            if not m:
                continue
            mm, dd, tspec, action, note = m.groups()
            if not mm.isdigit():
                continue
            rows_seen += 1
            approx = "~" in tspec
            spec = CLEAN.sub("", tspec)
            # 범위 구분자: ~ 또는 – 또는 -
            parts = re.split(r"[–—]|(?<=\d)-(?=\d)", spec)
            times = TIME.findall(spec)
            if len(times) >= 2:
                a, b = times[0], times[1]
                s = datetime(year, int(mm), int(dd), int(a[0]), int(a[1]), int(a[2] or 0))
                e = datetime(year, int(mm), int(dd), int(b[0]), int(b[1]), int(b[2] or 0))
                if e < s:  # 자정 넘김
                    e = e.replace(day=e.day + 1)
                ranges.append((s, e, action))
            elif len(times) == 1:
                a = times[0]
                t = datetime(year, int(mm), int(dd), int(a[0]), int(a[1]), int(a[2] or 0))
                points.append((t, action, approx))

    if rows_seen and not points and not ranges:
        raise SystemExit(
            f"🔴 {path} 의 표 줄은 {rows_seen}개 읽었는데 시각을 하나도 못 뽑았다 — "
            f"형식이 바뀐 것으로 보인다. '개입 없음'으로 넘어가면 전부 장치 탓이 되므로 여기서 멈춘다.")
    return points, ranges


def as_team_reset_args(path: str = PATH):
    """soak_stats.py / esp_events.py 의 --team-reset 인자 목록으로."""
    points, ranges = parse(path)
    out = []
    for t, action, approx in points:
        tag = "(대략)" if approx else ""
        out.append(f"{t.isoformat()}={action}{tag}")
    # 범위는 시작/끝을 각각 점으로도 넣어 둔다(그 경계에서 리셋이 났을 수 있다)
    for s, e, action in ranges:
        out.append(f"{s.isoformat()}={action} 개시")
        out.append(f"{e.isoformat()}={action} 종료")
    return out, ranges


if __name__ == "__main__":
    pts, rngs = parse()
    print(f"# 개입 점 {len(pts)}건 · 구간 {len(rngs)}건  ({PATH})")
    for t, a, ap in sorted(pts):
        print(f"  점   {t}  {'~' if ap else ' '} {a}")
    for s, e, a in sorted(rngs):
        print(f"  구간 {s} ~ {e}  {a}")
