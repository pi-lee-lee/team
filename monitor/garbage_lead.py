#!/usr/bin/env python3
"""부트 배너 **앞의 UART 가비지**가 얼마나 앞서는가 — 사건 시각 보정의 근거.

왜 (arduino 주장 2026-08-17 08:0x · 루트 전달)
  가비지의 정체가 **ESP 부트 ROM 의 74880bps 출력**을 우리 보율로 읽은 것이라면,
  **가비지가 리셋의 시작이고 배너는 그 끝**이다. 창4 에서 5초 차이가 관측됐다.
  → 사실이면 **배너 시각을 사건 시각으로 쓴 모든 판정이 몇 초씩 늦게 잡혀 있다**(A 창 포함).

🔴 **그래서 옮기기 전에 잰다.** 선행 시간이 **일정하면** 일괄 보정할 수 있고,
   **들쭉날쭉하면** 건별로 봐야 한다. 그리고 **정상 부팅에도 가비지가 뜨는지**를 같이 본다 —
   뜬다면 **가비지 단독은 리셋의 증거가 아니다**(배너와 정확히 같은 함정 · 원장 3.7-b).

가비지 판정: `[AT] "…"` 줄에서 따옴표 안 payload 가 8바이트 이상이고
             **비인쇄/비ASCII 비율이 30% 이상**인 것. (보율이 어긋나면 그렇게 보인다)

사용: python3 monitor/garbage_lead.py [로그...]
"""
from __future__ import annotations

import os
import re
import sys

# ⚠ 어느 디렉터리에서 실행해도 되게 한다 — `cd monitor` 를 강요하면 다음 사람이 걸린다.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zeroguard import calib, z  # noqa: E402

AT = re.compile(rb'^(\d\d):(\d\d):(\d\d)\s+\[AT\] "(.*)"\s*\(\d+\)\s*$')
TS = re.compile(rb"^(\d\d):(\d\d):(\d\d)\s")
# 🔴 표지 정정 — 처음에 `[PARKING NODE]` 를 썼는데 **그건 Uno 펌웨어 배너**다.
#   기전 B 가 말하는 배너는 **ESP 모듈**의 것: `[System Ready, Vendor:www.ai-thinker.com]`.
#   둘을 헷갈리면 "배너 1건" 만 잡히고 리셋을 통째로 놓친다(실제로 그랬다).
BANNER = b"System Ready"

# 🔴 2026-08-17 08:3x — **가비지는 한 종류가 아니라 둘이다**(arduino 가 자기 판독을 정정).
#   ① 부트 ROM  : 74880bps 출력을 9600 으로 읽은 것. **손상이 아니다.** 모든 부팅에 뜬다
#                 (우리 DTR 포함). 안정된 앵커가 있다 — `@\xF9` (그리고 뒤에 `\x89:\x97`).
#                 실측 예: `bB\xD6\x86@\xF9Pc\xE2\xFER\x89:\x97…`
#                 ⚠ 앞 바이트는 `\x86` 도 `\x84` 도 나온다(표본화가 매번 다르다).
#                   그래서 앵커를 `86 40 F9` 로 잡으면 놓친다 — **`@\xF9` 로 잡는다.**
#   ② 진짜 UART 비트 손상 : 앵커가 **없다.** 에코가 한 비트에서 어긋나고 프레이밍이 깨진다.
#                 실측 예(19:29:32, 기대 `AT+CIFSR`): `AT+CHU\x94Q\xA9BR\xD4%\xD5`
#                 → `0x49`(I) → `0x48`(H) **단일 비트**.
#   🔴 **한 칸으로 세면 둘이 섞인다.** ①은 사건이 아니고 ②는 사건이다.
BOOT_ANCHOR = b"@\\xF9"      # 로그가 이스케이프 텍스트라 `\xF9` 는 문자 4개다
WINDOW_S = 30          # 배너 앞 몇 초까지 훑을 것인가
MIN_LEN = 8
BAD_NUM, BAD_DEN = 3, 10   # 비인쇄 비율 문턱 = 3/10


def sec(h, m, s) -> int:
    return int(h) * 3600 + int(m) * 60 + int(s)


def hms(t: int) -> str:
    return f"{t // 3600:02d}:{t // 60 % 60:02d}:{t % 60:02d}"


def scan(path: str):
    rows = []
    nlines = 0
    with open(path, "rb") as f:
        for raw in f:
            nlines += 1
            line = raw.rstrip(b"\r\n")
            # 🔴 배너를 **먼저** 본다. `[System Ready…]` 는 그 자체가 `[AT] "…"` 줄이라,
            #   AT 분기에서 `continue` 하면 **배너가 0건으로 나온다**(밟았다).
            t = TS.match(line)
            if t and BANNER in line:
                rows.append((sec(t[1], t[2], t[3]), "BANNER"))
                continue
            m = AT.match(line)
            if m:
                p = m.group(4)
                # 🔴 로그는 원시 바이트가 아니라 **이스케이프된 텍스트**(`\x86`)로 저장된다.
                #   바이트 값으로 검사하면 전부 인쇄 가능 ASCII 라 **가비지가 0건으로 나온다**(밟았다).
                #   그래서 `\xNN` 조각 수를 센다.
                esc = p.count(b"\\x")
                is_garb = len(p) >= MIN_LEN and esc * BAD_DEN >= (len(p) // 4) * BAD_NUM and esc >= 3
                if is_garb:
                    kind = "BOOTROM" if BOOT_ANCHOR in p else "CORRUPT"
                else:
                    kind = "at"
                rows.append((sec(m[1], m[2], m[3]), kind))
    return rows, nlines


def main() -> int:
    paths = sys.argv[1:] or ["monitor/serial-newbase.log",
                             "monitor/serial-win3.log",
                             "monitor/serial-win4.log"]
    for path in paths:
        try:
            rows, nlines = scan(path)
        except FileNotFoundError:
            print(f"\n== {path} — 없음")
            continue
        banners = [t for t, k in rows if k == "BANNER"]
        boots = [t for t, k in rows if k == "BOOTROM"]
        corrs = [t for t, k in rows if k == "CORRUPT"]
        garbs = sorted(boots + corrs)
        print(f"\n== {path}")
        # 🔴 `0` 규약(zeroguard) — 분모와 표지를 같이 찍는다.
        #   이 도구는 표지를 세 번 틀리고도 조용히 `0건` 을 냈다(원장 7.43). 그래서 여기가 필수다.
        print("   " + z("배너", len(banners), nlines, BANNER.decode()))
        print("   " + z("① 부트ROM(사건 아님 · 모든 부팅에 뜬다)", len(boots), nlines, "앵커 @\\xF9"))
        print("   " + z("② UART 손상(사건 후보 · 앵커 없음)", len(corrs), nlines, "앵커 없는 이스케이프 뭉치"))
        print("   " + calib("배너 탐지", len(banners)))
        leads = []
        cleads = []
        for bt in banners:
            pre = [g for g in garbs if 0 <= bt - g <= WINDOW_S]
            pc = [g for g in corrs if 0 <= bt - g <= WINDOW_S]
            lead = bt - min(pre) if pre else None
            if lead is not None:
                leads.append(lead)
            ctxt = ""
            if pc:
                cl = bt - min(pc)
                cleads.append(cl)
                ctxt = f"  · 🔴 **② 손상 선행 {cl}s**({len(pc)}건)"
            print(f"   배너 {hms(bt)}  ← 가비지 {len(pre)}건"
                  f"{f' · 최대 선행 {lead}s' if lead is not None else ' (없음)'}{ctxt}")
        # 배너와 짝이 없는 가비지 = 리셋 아닌 곳에서도 뜨는가
        orphan = [g for g in garbs if not any(0 <= bt - g <= WINDOW_S for bt in banners)]
        print(f"   ⚠ 배너와 짝 없는 가비지: **{len(orphan)}건**"
              f"{'  → 가비지 단독은 리셋의 증거가 아니다' if orphan else ''}")
        if orphan[:5]:
            print("      예:", " ".join(hms(t) for t in orphan[:5]))
        if cleads:
            print(f"\n   🔴 **② UART 손상**이 배너에 선행: {sorted(cleads)} 초"
                  f" — {'일정하다' if len(set(cleads)) == 1 else '**흩어진다**'}"
                  f" ({len(cleads)}/{len(banners)} 배너에서 관측)")
        if leads:
            print(f"   ①+② 합쳐서: 최소 {min(leads)}s · 최대 {max(leads)}s · "
                  f"{'일정하다 → 일괄 보정 가능' if min(leads) == max(leads) else '🔴 들쭉날쭉 → 건별로 봐라'}")
    print("\n## 읽는 법")
    print("  · **짝 없는 가비지가 있으면 가비지 단독은 판별자가 될 수 없다**(배너와 같은 함정).")
    print("  · 선행 시간이 들쭉날쭉하면 **일괄 보정하지 마라** — 건별 원문 확인이다.")
    print("  · 이 도구는 **보율 어긋남을 가정한 휴리스틱**이다. 원문을 대신하지 않는다.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
