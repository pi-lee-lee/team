#!/usr/bin/env python3
"""ESP 리셋이 **전송 진행 중**에 시작했나, **유휴 구간**에 시작했나.

사전 등록: `monitor/PREREG-2026-08-17-reset-onset.md` (예측: **`CIPSEND`~`SEND OK` 사이**).
🔴 그 문서의 §2 규칙을 그대로 코드로 옮긴 것이다. **판정 규칙을 여기서 바꾸지 마라** —
   바꾸려면 사전 등록 문서를 먼저 고치고 **고친 시각과 이유를 적어야** 한다.

리셋 시작 표지(이른 것부터):
    1. **부트 ROM 가비지** — `[AT] "…"` 에 **앵커 `@\\xF9`** 가 있는 것
       🔴 2026-08-17 08:3x 정정: 옛 판은 **`\\xNN` 뭉치를 전부** 리셋 표지로 썼다.
          그런데 가비지는 **둘**이다(원장 7.52): **①부트ROM**(앵커 있음 = 리셋) ·
          **②UART 손상**(앵커 없음 = 리셋이 아니라 손상 사건).
          ②를 리셋 시작으로 잡으면 **최대 6초 앞으로 잘못 당겨진다**
          (실측 `20:40:31` ② vs `20:40:34` ① — 3초 차이).
          → **①만 리셋 표지로 쓴다. ②는 맥락으로 따로 찍는다.**
          ⚠ 이는 사전등록 §2.1 의 "부트 가비지"를 **정확히 구현한 것**이지 규칙 변경이 아니다
            (사전등록 §4.5 에 그렇게 기록했다).
    2. ESP 배너     `[System Ready`
    3. IP 소실      `"0.0.0.0"`
⚠ 가비지 선행은 0~6초로 일정하지 않다(원장 7.41) — **시작의 하한**이지 정확한 시각이 아니다.
   그래서 판정은 **"구간 안인가"** 로만 하고 시각을 주장하지 않는다.

🔴 **1초 해상도의 한계**: `CIPSEND` 와 `SEND OK` 가 **같은 초**에 있으면 시각으로 순서를 못 가른다.
   그때는 **로그 줄 순서**로 가르는데 그건 *탭이 읽은 순서*이지 타임스탬프가 아니다.
   → 그런 판정에는 **`?` 와 "같은 초" 경고**를 붙인다. **확정으로 쓰지 마라.**

판정 삼분(🔴 "보류"를 없애지 마라 — 두 칸이면 애매한 것이 한쪽으로 밀린다):
    적중  : 직전에 `CIPSEND` 가 있고 대응 `SEND OK` 가 아직 없다  (전송 진행 중)
    빗나감: 직전 사건이 `SEND OK` 이고 다음 `CIPSEND` 가 아직 없다 (유휴)
    보류  : 위 둘로 못 가름

사용:
  python3 monitor/reset_onset.py <시리얼로그> [시작HH:MM:SS] [끝HH:MM:SS]
  python3 monitor/reset_onset.py --calib      # 아는 사례로 교정부터 (권장 · 먼저)
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zeroguard import calib, z  # noqa: E402

TS = re.compile(rb"^(\d\d):(\d\d):(\d\d)\s+(.*)$")
ATQ = re.compile(rb'\[AT\] "(.*)"\s*\(\d+\)\s*$')
CIPSEND = b"AT+CIPSEND"
SENDOK = b'"SEND OK"'
ESPBANNER = b"System Ready"
ZEROIP = b'"0.0.0.0"'
BOOT_ANCHOR = b"@\\xF9"   # ①부트ROM 앵커. ⚠ `86 40 F9` 로 잡으면 앞 바이트 변동에 놓친다
GAP_S = 20          # 리셋 표지들을 한 사건으로 묶는 폭


def sec(h, m, s) -> int:
    return int(h) * 3600 + int(m) * 60 + int(s)


def hms(t: int) -> str:
    return f"{t // 3600:02d}:{t // 60 % 60:02d}:{t % 60:02d}"


def load(path: str):
    """(초, 종류, 원문) 목록. 종류: GARB/BANNER/ZEROIP/CIPSEND/SENDOK/other"""
    out, n = [], 0
    with open(path, "rb") as f:
        for raw in f:
            n += 1
            line = raw.rstrip(b"\r\n")
            m = TS.match(line)
            if not m:
                continue
            t = sec(m[1], m[2], m[3])
            body = m[4]
            kind = "other"
            if ESPBANNER in body:
                kind = "BANNER"
            elif ZEROIP in body:
                kind = "ZEROIP"
            elif SENDOK in body:
                kind = "SENDOK"
            elif CIPSEND in body:
                kind = "CIPSEND"
            else:
                q = ATQ.search(body)
                if q and q.group(1).count(b"\\x") >= 3 and len(q.group(1)) >= 8:
                    kind = "GARB" if BOOT_ANCHOR in q.group(1) else "CORRUPT"
            out.append((t, kind, body))
    return out, n


def onsets(rows):
    """리셋 표지를 사건으로 묶는다. 각 사건의 **가장 이른 표지**를 시작으로 본다."""
    marks = [(t, k) for t, k, _ in rows if k in ("GARB", "BANNER", "ZEROIP")]
    ev = []
    for t, k in marks:
        if ev and t - ev[-1][-1][0] <= GAP_S:
            ev[-1].append((t, k))
        else:
            ev.append([(t, k)])
    return ev


def judge(rows, start_i):
    """start_i 직전의 CIPSEND/SENDOK 상태로 삼분 판정."""
    last_cip = last_ok = None
    for i in range(start_i - 1, -1, -1):
        k = rows[i][1]
        if k == "CIPSEND" and last_cip is None:
            last_cip = i
        elif k == "SENDOK" and last_ok is None:
            last_ok = i
        if last_cip is not None and last_ok is not None:
            break
    if last_cip is None and last_ok is None:
        return "보류", "직전에 CIPSEND 도 SEND OK 도 없다", last_cip, last_ok
    # 🔴 1초 해상도의 한계 — 둘이 **같은 초**면 순서를 시각으로 못 가른다.
    #   지금은 로그 줄 순서로 가르는데, 그건 **탭이 읽은 순서**이지 타임스탬프가 아니다.
    #   창4 07:54:36 건이 정확히 이 경우였다(CIPSEND 와 SEND OK 둘 다 07:54:35).
    #   → **판정을 내되 "같은 초" 표시를 반드시 붙인다.** 조용히 한쪽으로 밀면 안 된다.
    same_sec = (last_cip is not None and last_ok is not None
                and rows[last_cip][0] == rows[last_ok][0])
    tag = "  ⚠ **같은 초 — 줄 순서로 갈랐다(시각으로는 못 가름)**" if same_sec else ""
    if last_cip is not None and (last_ok is None or last_cip > last_ok):
        return "적중" + ("?" if same_sec else ""), \
            "CIPSEND 뒤 대응 SEND OK 가 아직 없다(전송 진행 중)" + tag, last_cip, last_ok
    if last_ok is not None and (last_cip is None or last_ok > last_cip):
        return "빗나감" + ("?" if same_sec else ""), \
            "마지막이 SEND OK 다(유휴 구간)" + tag, last_cip, last_ok
    return "보류", "순서를 못 가름", last_cip, last_ok


def run(path: str, lo=None, hi=None, label=""):
    rows, nlines = load(path)
    idx = {}
    for i, (t, k, _) in enumerate(rows):
        idx.setdefault(t, i)
    ev = onsets(rows)
    if lo is not None:
        ev = [e for e in ev if lo <= e[0][0] <= hi]
    print(f"\n== {label or path}")
    ncorr = sum(1 for _, k, _ in rows if k == "CORRUPT")
    print("   " + z("리셋 사건", len(ev), nlines, "①부트ROM(@\\xF9)|System Ready|\"0.0.0.0\""))
    print("   " + z("② UART 손상(맥락 · 리셋 표지 아님)", ncorr, nlines, "앵커 없는 이스케이프 뭉치"))
    res = []
    for group in ev:
        t0, k0 = group[0]
        i = idx.get(t0, 0)
        verdict, why, ci, oi = judge(rows, i)
        res.append(verdict)
        kinds = "+".join(sorted({k for _, k in group}))
        print(f"   {hms(t0)}  시작표지 {kinds:<18} → **{verdict}**  ({why})")
        if ci is not None:
            print(f"       직전 CIPSEND {hms(rows[ci][0])}", end="")
        if oi is not None:
            print(f" · 직전 SEND OK {hms(rows[oi][0])}", end="")
        print()
    return res


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--calib":
        print("## 교정 — 아는 사례로 먼저 맞춘다 (사전등록 §3)")
        print("   A 창 `B.리셋계열` 6건: 18:24:55 · 18:44:56 · 18:48:12 · 20:09:14 · 20:26:32 · 20:40:34")
        a = run("monitor/serial-newbase.log", sec(17, 53, 7), sec(21, 4, 50), "A 창(교정)")
        w = run("monitor/serial-win4.log", sec(7, 51, 49), sec(23, 59, 59), "창4(교정 · 07:54 건 포함)")
        print("\n" + calib("리셋 사건 탐지(A 창)", len(a), 6))
        print(calib("리셋 사건 탐지(창4)", len(w), 1))
        print("\n🔴 교정이 통과해야 새 사건에 쓴다. 건수가 안 맞으면 **표지부터 고쳐라.**")
        print("⚠ A 창 건들은 **사후 분석**이다 — 예측 검증에 쓰지 마라(사전등록 §2.3).")
        return 0
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    lo = sec(*sys.argv[2].split(":")) if len(sys.argv) > 2 else None
    hi = sec(*sys.argv[3].split(":")) if len(sys.argv) > 3 else (86399 if lo is not None else None)
    run(sys.argv[1], lo, hi)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
