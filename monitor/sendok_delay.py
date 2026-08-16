#!/usr/bin/env python3
"""`AT+CIPSEND` → `SEND OK` 지연 분포 — `SEND_OK_TIMEOUT_MS` 임계값의 실측 근거 (REQ-0131).

🔴 이 도구가 **못 하는 것**을 먼저 적는다. 이걸 모르고 숫자를 쓰면 틀린다.

  시리얼 로그의 타임스탬프는 **초 단위**이고, 게다가 **줄을 읽은 시점**에 찍힌다.
  → **서브초 지연은 원리적으로 못 잰다.** "평균 0.34초" 같은 값을 만들지 마라 —
     없는 정밀도가 생긴다. 그래서 이 도구는 **평균을 아예 계산하지 않는다.**
     낼 수 있는 것은 **정수 초 버킷의 건수**와 **꼬리가 어디서 끊기는가** 뿐이다.

  `Δ=0s` 는 "지연 0"이 아니라 **"같은 초 안"**(0 이상 1초 미만)이라는 뜻이다.

⚠ 구간 정의 — 요청자의 타이머보다 **약간 길다**(요청자 확인 사항, REQ-0131)
     이 도구:   `AT+CIPSEND=<n>` 줄        → `SEND OK` 줄
     펌웨어:    **페이로드를 다 쓴 뒤**부터 → `SEND OK`
  → 여기서 나온 값은 펌웨어 값의 **상한**이다. 임계값을 정하는 데는 **안전한 방향**이다
     (상한으로 잡으면 임계값이 넉넉해지지 빠듯해지지 않는다).

⚠ 갈래를 반드시 갈라라
     정상   : `CIPSEND` → 페이로드 → `SEND OK` 가 끝까지 간 것
     비정상 : 사이에 `busy` · `Error` · `[TX-RESYNC]` 가 낀 것
  임계값 근거로 쓸 것은 **정상뿐**이다. 비정상은 늦는 게 당연해서 섞으면 임계값이 부풀려진다.
  다만 비정상·미도달 건수도 낸다 — **분모가 없으면 꼬리를 말할 수 없다.**

사용: python3 monitor/sendok_delay.py [로그] [시작ISO] [끝ISO]
"""
from __future__ import annotations

import os
import re
import sys
from datetime import datetime, timedelta

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-newbase.log"
SINCE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 16, 17, 53, 7)
UNTIL = datetime.fromisoformat(sys.argv[3]) if len(sys.argv) > 3 else datetime(2026, 8, 17, 1, 53, 0)

TS = re.compile(rb"^(\d{2}):(\d{2}):(\d{2})\s+(.*)$")
HDR_DATE = re.compile(rb"date=(\d{4})-(\d{2})-(\d{2})")

START = re.compile(rb'\[AT\] "AT\+CIPSEND=(\d+)"')
DONE = b'"SEND OK"'
BAD = (b"busy", b"Error", b"TX-RESYNC")


def parse(path: str):
    """(시각, 원문바이트). 자정 넘김 처리 — A 창은 01:53 에 끝나므로 필수다(LEDGER 2.2)."""
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


def main() -> int:
    p = os.path.abspath(LOG)
    print(f"# 원본 로그: {p}")
    print("# ⚠ 타임스탬프 해상도 = 1초(줄을 읽은 시점). 서브초는 못 잰다. 평균을 내지 않는다.")
    print("#   Δ=0s 는 '지연 0' 이 아니라 '같은 초 안(0~1초 미만)' 이다.")
    print("# ⚠ 이 구간은 펌웨어 타이머보다 길다(페이로드 쓰기 포함) — 즉 **상한**이다.")

    rows = [(t, r) for t, r in parse(p)]
    if not rows:
        print("⚠ 파싱된 줄이 0 이다.")
        return 1
    first, last = rows[0][0], rows[-1][0]
    lo, hi = max(SINCE, first), min(UNTIL, last)   # 분모에 미래를 넣지 않는다(LEDGER 1.2)
    if hi <= lo:
        print(f"⚠ 요청 구간에 자료가 없다 ({SINCE} ~ {UNTIL}).")
        return 1
    print(f"# 구간 {lo} ~ {hi}  ({(hi - lo).total_seconds()/3600:.2f}h)")
    if UNTIL > last:
        print(f"  ⓘ 분모 보정: 요청 끝 {UNTIL} → 자료 끝 {last}")

    win = [(t, r) for t, r in rows if lo <= t <= hi]

    ok_norm: list[tuple[datetime, int]] = []   # (시작시각, Δ초) — 전체 구간, 정상
    ok_bad: list[tuple[datetime, int]] = []    # 비정상 표지가 낀 것
    fw_norm: list[tuple[datetime, int]] = []   # 🔑 페이로드→SEND OK = **펌웨어 타이머와 같은 구간**
    unpaired = 0                               # SEND OK 없이 다음 CIPSEND 가 온 것

    start_t = None
    pay_t = None      # 페이로드를 다 쓴 시점(그 줄이 읽힌 시각)
    dirty = False
    for t, r in win:
        if START.search(r):
            if start_t is not None:
                unpaired += 1          # 앞의 것은 SEND OK 를 못 봤다
            start_t, pay_t, dirty = t, None, False
            continue
        if start_t is None:
            continue
        if any(b in r for b in BAD):
            dirty = True
            continue
        if DONE in r:
            d = int((t - start_t).total_seconds())
            if dirty:
                ok_bad.append((start_t, d))
            else:
                ok_norm.append((start_t, d))
                if pay_t is not None:
                    fw_norm.append((pay_t, int((t - pay_t).total_seconds())))
            start_t, pay_t, dirty = None, None, False
            continue
        # CIPSEND 와 SEND OK 사이의 `[AT] "…"` 줄 = 페이로드. 마지막 것을 쓴다.
        if b'[AT] "' in r:
            pay_t = t
    if start_t is not None:
        unpaired += 1

    def tally(rec):
        b = {"0": 0, "1": 0, "2": 0, "3": 0, "4+": 0}
        for _, d in rec:
            k = str(d) if d <= 3 else "4+"
            b[k] += 1
        return b

    n_norm, n_bad = len(ok_norm), len(ok_bad)
    print(f"\n## 표본  정상 {n_norm:,} · 비정상(busy/Error/RESYNC 낌) {n_bad:,} · SEND OK 미도달 {unpaired:,}")
    if n_norm < 100:
        print("  ⚠ 정상 표본이 100 미만이다 — 꼬리를 말하기에 부족하다. 구간을 늘려라.")

    print("\n## 🔑🔑 페이로드→`SEND OK` — **`SEND_OK_TIMEOUT_MS` 가 실제로 재는 구간**")
    print("   (아래 '전체 구간'은 프롬프트 `>` 대기까지 포함해 더 길다. 임계값은 이 표로 정해라.)")
    tf = tally(fw_norm)
    nf = len(fw_norm)
    for k in ("0", "1", "2", "3", "4+"):
        n = tf[k]
        pct = f"{100.0 * n / nf:.3f}%" if nf else "-"
        bar = "█" * min(40, int(40 * n / max(1, nf)))
        label = f"Δ={k}s" if k != "4+" else "Δ≥4s"
        print(f"  {label:<7} {n:7,}  {pct:>9}  {bar}")
    if fw_norm:
        mt, md = max(fw_norm, key=lambda x: x[1])
        print(f"  → 최대 Δ = {md}s (페이로드 {mt:%m-%d %H:%M:%S}) · 표본 {nf:,}")

    print("\n## 전체 구간(`CIPSEND`→`SEND OK`) 정상 갈래 — 프롬프트 대기 포함, 상한")
    tn = tally(ok_norm)
    for k in ("0", "1", "2", "3", "4+"):
        n = tn[k]
        pct = f"{100.0 * n / n_norm:.3f}%" if n_norm else "-"
        bar = "█" * min(40, int(40 * n / max(1, n_norm)))
        label = f"Δ={k}s" if k != "4+" else "Δ≥4s"
        print(f"  {label:<7} {n:7,}  {pct:>9}  {bar}")

    print("\n## 비정상 갈래 Δ 분포 (참고 — 임계값에 쓰지 마라)")
    tb = tally(ok_bad)
    for k in ("0", "1", "2", "3", "4+"):
        label = f"Δ={k}s" if k != "4+" else "Δ≥4s"
        print(f"  {label:<7} {tb[k]:7,}")

    for name, rec in (("정상", ok_norm), ("비정상", ok_bad)):
        if not rec:
            continue
        mt, md = max(rec, key=lambda x: x[1])
        print(f"\n## {name} 최대 Δ = {md}s  (시작 {mt:%m-%d %H:%M:%S})")
        if name == "정상":
            tail = sorted([x for x in rec if x[1] >= 2], key=lambda x: -x[1])[:10]
            if tail:
                print("   꼬리(Δ≥2s) 상위 — **직접 원문을 열어 확인하라**:")
                for tt, dd in tail:
                    print(f"     {tt:%m-%d %H:%M:%S}  Δ={dd}s")
            else:
                print("   Δ≥2s 인 정상 건이 **하나도 없다.**")

    print("\n## 읽는 법")
    print("  · 꼬리가 끊기는 지점이 임계값의 하한이다. 거기에 여유를 얹어라.")
    print("  · Δ=0s 가 압도적이어도 그것이 '항상 빠르다'를 뜻하지 않는다 —")
    print("    해상도가 1초라 0.9초와 0.0초가 같은 칸에 들어간다.")
    print("  · 이 값은 펌웨어 타이머의 **상한**이다. 임계값을 이보다 작게 잡지 마라.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
