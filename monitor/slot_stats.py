#!/usr/bin/env python3
"""슬롯 구조 전용 집계 — `[SLOT]` · `[SLOT-OOW]` · `[CNT]` 새 칸을 **분모와 함께** 낸다.

왜 새로 쓰나 (옛 도구를 고치지 않는 이유)
  `soak_stats.py`·`win2_pulse.py` 는 **창4 이전 계열**의 표지를 센다. 슬롯 판에서는
  `[TX] A,` 가 아예 없어지고(ACK 가 상태 프레임에 실린다) `esprst`·`okto` 의 **정의가 바뀌었다**
  (arduino 통보: `443a352`). 같은 이름으로 이어 세면 조용히 다른 것을 비교한다.
  → **옛 도구는 옛 창의 정본으로 남기고, 슬롯 판은 이 파일로 센다.**

무엇을 세나 — 전부 **분모와 같이** (원장 7.44 · zeroguard 규약)
  `[SLOT] n= tx= ack= due= r=`   슬롯마다 한 줄. `ack=0` 도 찍힌다 → 분모가 실재한다
  `[SLOT-OOW] +<ms>`             하행이 "송신 창 구간"에 도착 — 🔴 **충돌이 아니다**(아래)
  `[CNT] slot= oow= smiss= ssovf= skip=`  누적. `DEBUG` 밖이라 항상 센다

🔴 `oow` 를 충돌로 읽지 마라 — 오탐과 누락이 **동시에** 있다 (arduino 자진 정정 · 사전등록 §2.6.2)
  판정식은 `sinceSlot < 600` 인데 장치가 UART 를 실제 쓰는 구간은 **`[due, due+txTime]`** 뿐이다.
  `due` 는 **송신 시작 시각**이고 송신 완료가 아니다(arduino 확정) — 33B 페이로드면 `txTime≈45ms`.
    과대: `due=0` 에 45ms 만 쓰고 끝냈는데 145ms 도착 → 안전한데 `oow++`
    누락: 진짜 충돌한 바이트는 사라져 `handleLine` 에 도달조차 못 한다
  → 이 도구는 도착 오프셋을 **`due + txTime` 과 대조해 `위험`/`안전`으로 갈라 찍는다.**

🔴 `smiss` = **송신 창 침범 가드 발동 계수**(온라인인데 그 슬롯에 시도조차 못 함).
  `slot = 실제 송신 + smiss + 오프라인` 이고 **`n` 번호 건너뜀 수와 맞아야 한다** — 그 대조를 찍는다.
  🔑 `smiss` 가 오른 구간의 **2.4초 공백은 링크 이상이 아니라 가드다.**

⚠ `ssovf` 의 분모는 **"한 거래에 몇 바이트가 오는가"** 이지 하행 건수가 아니다(arduino).
  링버퍼가 64B 라 28B 단발 주입으로는 **넘칠 조건 자체가 없다.** 그래서 `도착 버스트 바이트`를 같이 센다.

사용: python3 monitor/slot_stats.py [시리얼로그] [서버로그]
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter

SER = "monitor/serial-winA.log"
SRV = os.path.expanduser("~/parking-logs/parking-server.log")
TXTIME_MS = 45          # 33B 페이로드 실측 근거(arduino). 최악 배치 178B ≈ 185ms
DAY = "2026-08-17"

RE_SLOT = re.compile(r"\[SLOT\] n=(\d+) tx=(\d+) ack=(\d+) due=(\d+) r=(\d)")
RE_OOW = re.compile(r"\[SLOT-OOW\] \+(\d+)")
RE_CNT = re.compile(r"\[CNT\] (.+)")


def hms(t: str) -> int:
    h, m, s = (int(x) for x in t.split(":"))
    return h * 3600 + m * 60 + s


def main() -> int:
    ser = sys.argv[1] if len(sys.argv) > 1 else SER
    srv = sys.argv[2] if len(sys.argv) > 2 else SRV

    slots: list[tuple[int, int, int, int, int, int]] = []   # t, n, tx, ack, due, r
    oow: list[tuple[int, int]] = []                          # t, offset
    cnt_last = None
    lines = 0
    for line in open(ser, encoding="utf-8", errors="replace"):
        lines += 1
        if len(line) < 9 or line[2] != ":" or line[5] != ":":
            continue
        try:
            t = hms(line[:8])
        except ValueError:
            continue
        m = RE_SLOT.search(line)
        if m:
            slots.append((t, *(int(x) for x in m.groups())))
            continue
        m = RE_OOW.search(line)
        if m:
            oow.append((t, int(m.group(1))))
            continue
        m = RE_CNT.search(line)
        if m:
            cnt_last = (line[:8], m.group(1).strip())

    print("# 슬롯 집계 — 시리얼 %s" % ser)
    print("# 훑은 줄 %d · 찾은 문자열 '[SLOT] n=' '[SLOT-OOW] +' '[CNT] '" % lines)
    print("# 🔴 0 을 읽기 전에 분모를 봐라: 슬롯 %d · OOW %d · 하행(서버) 아래"
          % (len(slots), len(oow)))
    if not slots:
        print("# 🔴 `[SLOT]` 0건 — 표지가 안 붙었거나(펌웨어) 로그가 빈 것이다. **판정 불가**")
        return 0

    # 하행: 서버 로그에서 창 구간만
    lo, hi = slots[0][0], slots[-1][0]
    dl: list[tuple[int, str, int]] = []      # t, kind, bytes(대략)
    seen = set()
    for line in open(srv, encoding="utf-8", errors="replace"):
        if "→ARD" not in line or not line.startswith(DAY):
            continue
        t = hms(line[11:19])
        if not (lo <= t <= hi):
            continue
        body = line.split("→ARD", 1)[1].strip()
        parts = body.split(",")
        key = ",".join(parts[:2])
        dl.append((t, parts[0], len(body) + 2))          # +2 = CRLF 어림
        seen.add(key)
    newdl = len(seen)

    print()
    print("## 분모")
    print("  구간            %s ~ %s (%.3fh)" % (
        "%02d:%02d:%02d" % (lo // 3600, lo % 3600 // 60, lo % 60),
        "%02d:%02d:%02d" % (hi // 3600, hi % 3600 // 60, hi % 60),
        (hi - lo) / 3600))
    print("  슬롯 줄        %d" % len(slots))
    print("  하행 줄        %d  (신규 seq %d · 재전송 %d)" % (len(dl), newdl, len(dl) - newdl))
    if not dl:
        print("  🔴 하행 0건 — `oow`/`ssovf` 의 0 은 **자극 부재**다. 판정하지 마라(사전등록 §2.3)")

    # n 건너뜀 ↔ smiss 대조
    gaps = 0
    for a, b in zip(slots, slots[1:]):
        d = b[1] - a[1]
        if d > 1:
            gaps += d - 1
    print()
    print("## 슬롯 위상")
    print("  `n` 번호 건너뜀 합계   %d   ← `[CNT] smiss=` 와 맞아야 한다(가드 발동)" % gaps)
    due = Counter(s[4] for s in slots)
    tx = Counter(s[2] for s in slots)
    ack = Counter(s[3] for s in slots)
    def brief(c: Counter, n: int = 6) -> str:
        return ", ".join("%s×%d" % (k, v) for k, v in c.most_common(n))
    print("  `due` 분포(ms)         %s" % brief(due))
    print("     ⚠ `due` 는 **송신 시작**이다. UART 점유는 `[due, due+%dms]`" % TXTIME_MS)
    print("     ⚠ 이론 상한 400ms(가드)와 실측을 섞어 인용하지 마라 — 부하에서 커진다")
    print("  `tx` 바이트 분포       %s" % brief(tx))
    print("  `ack` 묶음 분포        %s" % brief(ack))

    # OOW 재분류
    print()
    print("## OOW 재분류 — `도착 오프셋 > due + txTime` 이면 **안전**")
    if not oow:
        print("  OOW 0건 · 하행 %d건 → %s" % (
            len(dl), "분모 부재(자극 없음)" if not dl else "하행은 있었는데 OOW 0 = 창을 지켰다는 뜻"))
    else:
        risky = safe = 0
        for t, off in oow:
            near = [s for s in slots if s[0] <= t]
            d = near[-1][4] if near else 0
            if off > d + TXTIME_MS:
                safe += 1
            else:
                risky += 1
        print("  OOW %d건 = 위험 %d · 안전(재분류) %d" % (len(oow), risky, safe))
        print("  오프셋 분포 %s" % brief(Counter(o for _, o in oow), 10))
        print("  🔴 '위험' 도 확정 충돌이 아니다 — 진짜 충돌 바이트는 세지지 않는다(누락 편향)")

    print()
    print("## 마지막 `[CNT]`")
    print("  %s  %s" % (cnt_last[0], cnt_last[1]) if cnt_last else "  🔴 없음 — 60초 이상 기다려라")
    print()
    print("## 읽는 법")
    print("· 🔴 `oow=0`·`ssovf=0`·`smiss=0` 은 **분모를 보고 나서** 읽어라. 0 은 네 가지다(원장 1.1·7.44·7.57)")
    print("· ⚠ `ssovf` 의 분모는 **한 거래의 바이트 수**다. 28B 단발이면 64B 링버퍼는 시험되지 않는다")
    print("· ❌ 창4 이전 창과 `esprst`·`okto`·`skip` 이름으로 잇지 마라 — 정의가 바뀌었다(`443a352`)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
