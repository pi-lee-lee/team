#!/usr/bin/env python3
"""상행 프레임의 **크기 구성**을 분해한다 — `rid` 자릿수 · ACK 개수 `N` · ACK 실측 폭 `B`.

왜 있나 (2026-08-19 · REQ-0238)
  socket 의 A[1](=`rid` 폭 고정) 대조군을 뜨려면 **그 전 기준선**이 필요하다.
  창 J 에서 `rid` 3→4자리 전이가 `>=64B` 진입률에 섞여 들어갔는데 **아무도 자릿수를 안 셌다.**
  자릿수를 같이 적지 않으면 A[1] 판정 때 "왜 달라졌지"를 못 가른다(원장 §다음 창이 볼 것 ②).

무엇을 세나
  상행 에코 ` S,<seq>,<occ>,<flg>,<up>,<devid>,<ck>` 뒤에 **구분자 없이** 이어 붙는
  `A,<rid>,<slot>,<res>,<ck>` 를 세고, 직전 `AT+CIPSEND=<tx>` 와 짝지어 크기를 분해한다.

🔴 이 도구는 **판정하지 않는다.** 분포와 분모를 낼 뿐이다.
   비율에는 반드시 **"무엇 분의 무엇 · 어느 구간"** 이 붙어 나온다(원장 §분모).

⚠ `0` 을 건강으로 읽지 마라 — 표본이 0 이면 그렇게 찍는다.
⚠ `grep` 이 아니라 이 파서를 써라. 이 로그의 계수 필드는 **줄이 아니라 `[CNT]` 안의 필드**다(원장 7.174.1).

사용: python3 monitor/rid_width.py <시리얼로그> [HH:MM:SS 시작] [HH:MM:SS 끝]
"""
from __future__ import annotations

import re
import sys
from collections import Counter

RE_CIPSEND = re.compile(r'\[AT\] "AT\+CIPSEND=(\d+)"')
RE_ECHO    = re.compile(r'\[AT\] " (S,[^"]*)"')
# ACK 레코드: A,<rid>,<slot>,<res>,<cksum2>  — 앞 필드와 붙어 있어도 'A,' 에서 잡힌다
RE_ACK     = re.compile(r'A,(\d{1,6}),([0-9A-Za-z]{2}),(\d+),([0-9A-Fa-f]{2})')

HEAD_BASE = 23   # ' S,<seq>,<occ>,<flg>,<up>,<devid>,<ck>' 기준폭 (REQ-0238 표기와 맞춘다)


def sec(t: str) -> int:
    h, m, s = (int(x) for x in t.split(":"))
    return h * 3600 + m * 60 + s


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    log = sys.argv[1]
    t_lo = sec(sys.argv[2]) if len(sys.argv) > 2 else None
    t_hi = sec(sys.argv[3]) if len(sys.argv) > 3 else None

    pending_tx = None
    first_ts = last_ts = ""
    rid_digits = Counter()
    n_dist = Counter()
    tx_dist = Counter()
    bwidth = Counter()
    rid_lo, rid_hi = None, None
    rid_seen = set()
    frames = 0
    frames_ge64 = 0
    acks = 0
    tx_seen = 0          # CIPSEND 줄 수 (분모 후보 ①)
    unmatched_tx = 0     # 에코와 못 짝지은 CIPSEND

    with open(log, "rb") as f:
        for raw in f:
            try:
                ln = raw.decode("utf-8", "replace")
            except Exception:
                continue
            if len(ln) < 9 or ln[2] != ":":
                continue
            ts = ln[:8]
            try:
                cur = sec(ts)
            except Exception:
                continue
            if t_lo is not None and not (t_lo <= cur <= t_hi):
                continue
            if not first_ts:
                first_ts = ts
            last_ts = ts

            m = RE_CIPSEND.search(ln)
            if m:
                if pending_tx is not None:
                    unmatched_tx += 1
                pending_tx = int(m.group(1))
                tx_seen += 1
                continue

            e = RE_ECHO.search(ln)
            if e:
                payload = e.group(1)
                ack = RE_ACK.findall(payload)
                n = len(ack)
                frames += 1
                acks += n
                n_dist[n] += 1
                for rid, _slot, _res, _ck in ack:
                    rid_digits[len(rid)] += 1
                    v = int(rid)
                    rid_seen.add(v)
                    rid_lo = v if rid_lo is None else min(rid_lo, v)
                    rid_hi = v if rid_hi is None else max(rid_hi, v)
                if pending_tx is not None:
                    tx_dist[pending_tx] += 1
                    if pending_tx >= 64:
                        frames_ge64 += 1
                    if n > 0:
                        bwidth[round((pending_tx - HEAD_BASE) / n, 2)] += 1
                    pending_tx = None
                continue

    print("# %s" % log)
    print("# 구간 : %s ~ %s%s" % (first_ts or "(없음)", last_ts or "(없음)",
                                  "  (인자로 자름)" if t_lo is not None else "  (파일 전체)"))
    print("#")
    if frames == 0:
        print("🔴 상행 에코 0건 — **판정 불가**. 표지가 없거나 구간이 비었다(건강 아님, 원장 1.1).")
        return 0

    print("## 분모 — 무엇 분의 무엇인가")
    print("  상행 에코 프레임 : %d" % frames)
    print("  AT+CIPSEND 줄    : %d   (에코와 못 짝지음 %d)" % (tx_seen, unmatched_tx))
    print("  ACK 레코드 총합  : %d" % acks)
    print("  ⚠ 위 셋은 **다른 것을 센다.** 크기 비교하지 마라(원장 §숫자 둘을 비교하기 전에)")
    print()
    print("## `>=64B` 진입률 — **분모는 '에코와 짝지은 상행 프레임'이다**")
    matched = sum(tx_dist.values())
    if matched:
        print("  %d / %d = **%.1f%%**" % (frames_ge64, matched, 100.0 * frames_ge64 / matched))
    else:
        print("  🔴 짝지은 프레임 0 — 판정 불가")
    print()
    print("## 🔴 `rid` 자릿수 분포 — A[1] 대조군의 핵심")
    if rid_digits:
        tot = sum(rid_digits.values())
        for d in sorted(rid_digits):
            print("  %d자리 : %6d / %d = %5.1f%%" % (d, rid_digits[d], tot, 100.0 * rid_digits[d] / tot))
        print("  rid 범위 : %d ~ %d" % (rid_lo, rid_hi))
        if len(rid_digits) > 1:
            print("  ⚠ **이 구간 안에서 자릿수가 바뀐다.** 진입률을 자릿수와 갈라 읽어라 —")
            print("     한 창 안에서 전이가 나면 창 평균은 두 국면의 혼합이다")
    else:
        print("  ACK 0건 — `rid` 표본 없음. **하행 자극이 없었다는 뜻이지 건강이 아니다**")
    print()
    print("## 🔴 `rid` 유일성 — 중복(재전송)과 누락을 가른다")
    if rid_seen:
        span = rid_hi - rid_lo + 1
        uniq = len(rid_seen)
        dup = acks - uniq
        missing = span - uniq
        print("  관측 범위 %d~%d → 폭 %d" % (rid_lo, rid_hi, span))
        forced = max(0, acks - span)   # 🔴 공간보다 ACK 이 많으면 비둘기집으로 반복이 강제된다
        print("  고유 rid %d · ACK 레코드 %d → **중복 %d**" % (uniq, acks, dup))
        if forced > 0:
            print("  🔴 **이 `중복` 을 재전송으로 읽지 마라** — 관측 폭 %d 보다 ACK 이 많아"
                  " **최소 %d 건은 비둘기집으로 강제된다**(rid 공간을 다 돌았다)." % (span, forced))
            print("     재전송 수는 이 값이 아니라 **서버 쪽 `재전송` 계수**로 봐라."
                  " 공간이 감기는 판(A[1] 이후)에서는 두 값이 자릿수부터 다르다")
        print("  **누락 %d / %d = %.2f%%**  (범위 안에서 한 번도 안 온 rid)" % (missing, span, 100.0*missing/span))
        print("  ⚠ 누락은 **이 창 로그 기준**이다 — 창 밖에서 온 것은 누락으로 보인다.")
        print("     창 경계에 걸친 rid 는 양끝에서 각각 몇 건씩 그렇게 새므로 **양끝 여유를 보고 읽어라**")
    else:
        print("  ACK 0건 — 표본 없음(건강 아님)")
    print()
    print("## ACK 개수 `N` 분포 (프레임당)")
    for k in sorted(n_dist):
        print("  N=%d : %6d 프레임 (%5.1f%%)" % (k, n_dist[k], 100.0 * n_dist[k] / frames))
    print()
    print("## ACK 실측 폭 `B` = (tx - %d) / N" % HEAD_BASE)
    if bwidth:
        for k in sorted(bwidth):
            print("  B=%5.2fB : %6d 프레임" % (k, bwidth[k]))
        print("  ⚠ `B` 는 **파생값**이다. 머리 기준폭 %dB 가 틀리면 통째로 틀린다 —" % HEAD_BASE)
        print("     머리 폭이 바뀌는 배포가 있었으면 이 값을 창끼리 비교하지 마라")
    else:
        print("  N>0 프레임이 없어 계산 안 됨 (ACK 미동봉 구간)")
    print()
    print("## 상행 크기 `tx` 상위")
    for k, v in sorted(tx_dist.items(), key=lambda x: -x[1])[:8]:
        print("  tx=%3d : %6d 프레임" % (k, v))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
