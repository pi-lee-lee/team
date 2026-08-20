#!/usr/bin/env python3
"""UART 손상 계수 — **셸 이스케이프를 안 거친다.**

왜 있나 (2026-08-19 · 하루에 네 번 밟았다)
  손상 표지는 로그에 **리터럴 `\\x88` 문자열**로 적힌다(탭이 비출력 바이트를 이스케이프한다).
  그 패턴을 `grep` 에 넘기면 **heredoc·subprocess·배경 스크립트를 거치며 백슬래시가 사라져**
  아무것도 안 잡고 **`0`** 을 낸다.
  🔴 **그 `0` 이 판정문에 실렸다** — 창 V 는 실제 6 인데 `0` 으로 적혔다(16:4x 정정).
  → **셸을 안 거치는 파이썬 파일**로 고정한다. 인자는 로그 경로뿐이다.

🔴 두 부류를 **갈라 센다** — 하나만 세면 절반을 놓친다
  ① 비출력 손상   : `AT+CIPSEN\\x8C=21`  → 리터럴 `\\x` 가 붙는다
  🔴 ② 출력가능 손상 : `AT+CIPSEND!24`     → `=`(0x3D)→`!`(0x21). **`\\x` 가 안 붙는다**
     **①만 세면 이 부류를 통째로 못 본다**(창 W 에서 드러났다)

⚠ 분모를 항상 같이 낸다 — **작은 분모의 `0` 은 아무것도 증명하지 않는다**
   창 T `0/3,470` 은 값이 있고, 창 W `6/222` 는 그 자체로는 약하다.

사용: python3 monitor/uart_damage.py <시리얼로그> [...]
"""
from __future__ import annotations

import re
import sys

RE_ESC_AT = re.compile(r'\[AT\] "AT\+C[^"]*\\x')  # ① 비출력 손상 (나가는 AT 명령)
RE_BAD_SEND = re.compile(r'\[AT\] "AT\+CIPSEND[^=0-9"]')  # ② 출력가능 손상
RE_ANY_SEND = re.compile(r"AT\+CIPSEND")  # 분모
RE_ESC_ANY = re.compile(r"\\x")  # 참고: 이스케이프 포함 줄 전체
RE_MIX = re.compile(r'" S,[^"]*AT\+')  # 상행에 AT 혼재


def scan(path: str) -> dict:
    c = {"esc_at": 0, "bad_send": 0, "send": 0, "esc_any": 0, "mix": 0, "lines": 0}
    samples: list = []
    try:
        with open(path, "rb") as f:
            for raw in f:
                s = raw.decode("utf-8", "replace")
                c["lines"] += 1
                if RE_ANY_SEND.search(s):
                    c["send"] += 1
                if RE_ESC_ANY.search(s):
                    c["esc_any"] += 1
                hit = False
                if RE_ESC_AT.search(s):
                    c["esc_at"] += 1
                    hit = True
                if RE_BAD_SEND.search(s):
                    c["bad_send"] += 1
                    hit = True
                if RE_MIX.search(s):
                    c["mix"] += 1
                if hit and len(samples) < 6:
                    samples.append(s.strip()[:100])
    except OSError as e:
        print("🔴 못 읽음: %s" % e, file=sys.stderr)
    c["samples"] = samples
    return c


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    for path in sys.argv[1:]:
        c = scan(path)
        tot = c["esc_at"] + c["bad_send"]
        d = c["send"]
        print("# %s  (%d줄)" % (path, c["lines"]))
        if d == 0:
            print("  🔴 CIPSEND **0건** — **분모가 없다. 판정 불가**(건강 아님)")
        else:
            print(
                "  🔴 깨진 나가는 AT **%d / %d** = %.2f%%   (①비출력 %d + ②출력가능 %d)"
                % (tot, d, 100.0 * tot / d, c["esc_at"], c["bad_send"])
            )
            if d < 1000:
                print(
                    "     ⚠ **분모 %d 은 작다.** `0` 이 나와도 근거로 쓰지 마라"
                    " (창 T 는 3,470 이라 값이 있었다)" % d
                )
        print("  상행 AT 혼재 %d · 이스케이프 포함 줄 %d" % (c["mix"], c["esc_any"]))
        for s in c["samples"]:
            print("     · " + s)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
