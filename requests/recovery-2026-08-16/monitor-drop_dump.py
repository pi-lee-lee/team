#!/usr/bin/env python3
"""체크섬 드롭의 **원문 바이트**를 덤프한다 — 집계 숫자를 믿기 전에 원문을 본다.

REQ-0092 에서 '체크섬 실패 100배 악화' 가 허상이었던 사고의 재발 방지 장치.
서버는 비프로토콜 텍스트도 `! 체크섬 불일치` 로 센다. 그래서 **버려진 줄 직전의 `←ARD` 원문**을
16진수까지 찍어 '바이트가 깨진 것' 과 'AT/부트 잡음이 샌 것' 을 눈으로 가른다.

사용: python3 monitor/drop_dump.py [로그] > monitor/out-drops.txt
"""
from __future__ import annotations

import re
import sys
from datetime import date

sys.path.insert(0, "monitor")
from soak_stats import parse_log, classify_drop  # noqa: E402

LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/parking-soak.log"
BASE = date.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else date(2026, 8, 16)

events, meta = parse_log(LOG, BASE)
drops = [e for e in events if e.kind == "drop"]

print(f"# 체크섬 드롭 총 {len(drops)}건 — 직전 ←ARD 원문 바이트")
print()
kinds = {}
for e in drops:
    k = classify_drop(e.data["prev_raw"], e.data["prev_txt"])
    kinds.setdefault(k, []).append(e)

for k, evs in sorted(kinds.items(), key=lambda kv: -len(kv[1])):
    print(f"## {k} — {len(evs)}건  ({evs[0].ts} ~ {evs[-1].ts})")
    print()
    for e in evs:
        prev = e.data["prev_raw"]
        payload = prev[len("←ARD".encode()):].strip() if prev.startswith("←ARD".encode()) else prev
        hexs = payload.hex(" ")
        if len(hexs) > 150:
            hexs = hexs[:150] + " …"
        printable = "".join(chr(b) if 0x20 <= b <= 0x7E else "." for b in payload)[:60]
        print(f"  {e.ts}  len={len(payload):3d}  ascii='{printable}'")
        print(f"                       hex= {hexs}")
    print()
