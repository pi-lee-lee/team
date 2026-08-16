#!/usr/bin/env python3
"""시리얼 로그를 **시각 구간으로 잘라** 새 파일로 낸다.

왜 있나 — 동결 파서 `frozen-A/busy_runs.py` 는 구간 인자를 받지 않는다(파일 전체를 센다).
동결본은 **고칠 수 없다**(고치면 A 판정이 두 판본으로 갈린다 · frozen-A/MANIFEST).
그래서 **도구가 아니라 입력을 자른다** — 계측기는 바이트 동일한 채로 남는다.

⚠ 경계 효과: 잘린 자리에서 `AT+CIPSEND` 하나가 응답과 갈라질 수 있다.
   그 시도는 `unknown` 으로 떨어진다. 시도 12,000건 규모에서 최대 1~2건이다.

⚠ 이 로그에는 날짜가 없다(HH:MM:SS 뿐). 자정을 넘는 구간에는 쓰지 마라 — 원장 2.2.

사용: python3 monitor/slice_window.py <입력로그> <HH:MM:SS 시작> <HH:MM:SS 끝> <출력경로>
"""
from __future__ import annotations

import re
import sys

TS = re.compile(rb"^(\d\d):(\d\d):(\d\d)\s")


def sec(t: str) -> int:
    h, m, s = (int(x) for x in t.split(":"))
    return h * 3600 + m * 60 + s


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    src, t0, t1, dst = sys.argv[1:5]
    a, b = sec(t0), sec(t1)
    if b <= a:
        print("끝이 시작보다 앞이다 — 자정 넘김에는 이 도구를 쓰지 마라(원장 2.2).", file=sys.stderr)
        return 2

    kept = dropped = notime = 0
    with open(src, "rb") as f, open(dst, "wb") as g:
        for raw in f:
            m = TS.match(raw)
            if not m:
                notime += 1
                continue
            t = int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3])
            if a <= t <= b:
                g.write(raw)
                kept += 1
            else:
                dropped += 1
    print(f"# {src} → {dst}")
    print(f"# 구간 {t0} ~ {t1}")
    print(f"# 남김 {kept:,}줄 · 구간 밖 {dropped:,}줄 · 타임스탬프 없음 {notime:,}줄(버림)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
