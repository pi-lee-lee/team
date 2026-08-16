#!/usr/bin/env python3
"""로그 라인 '형태' 조사기 — 원문을 문맥에 올리지 않고 무엇이 들어있는지만 센다.

사용: python3 monitor/log_shapes.py [로그경로]

각 줄에서 타임스탬프를 떼고, 남은 부분을 숫자/시퀀스를 마스킹해 '형태'로 접은 뒤
형태별 건수와 대표 예시 1건을 출력한다. 출력은 작게 유지한다(문맥 보호).
"""
import re
import sys
from collections import Counter

LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/parking-soak.log"

TS = re.compile(r"^\d{2}:\d{2}:\d{2}\s+")
NUM = re.compile(r"\d+")


def shape(body: str) -> str:
    # 프레임 본문처럼 긴 것은 앞부분만
    s = body.strip()
    s = NUM.sub("#", s)
    return s[:70]


def main() -> None:
    counts: Counter = Counter()
    sample: dict[str, str] = {}
    total = 0
    no_ts = 0
    with open(LOG, "rb") as f:
        for raw in f:
            total += 1
            line = raw.decode("utf-8", errors="replace").rstrip("\n")
            m = TS.match(line)
            if not m:
                no_ts += 1
                body = "«타임스탬프없음» " + line
            else:
                body = line[m.end():]
            sh = shape(body)
            counts[sh] += 1
            if sh not in sample:
                sample[sh] = line[:120]

    print(f"# 총 {total}줄 · 타임스탬프 없는 줄 {no_ts}")
    print(f"# 서로 다른 형태 {len(counts)}종")
    print()
    for sh, n in counts.most_common(80):
        print(f"{n:8d}  {sh}")
        if n < 200:
            print(f"          예: {sample[sh]}")


if __name__ == "__main__":
    main()
