#!/usr/bin/env python3
"""**병리 구간**의 `SEND OK` 공백 — `SEND_OK_TIMEOUT_MS` 상한을 올릴지 말지를 가른다 (REQ-0139).

왜 있나 — `sendok_delay.py`(REQ-0131)는 **정상 전송**의 분포를 낸다. 그런데 상한이 실제로
걸리는 순간은 정상 전송이 아니다. 요청자(arduino)가 스스로 잡아낸 모집단 오류다.
**정상 분포로 비정상 상한을 정하고 있었다**(원장 1.17).

세는 방법
---------
**송신 주기**는 둘 중 하나로 시작한다:

    [TX] S,…              프롬프트 '>' 를 받고 페이로드를 실제로 쓴 것 = **성공**
    [TX-RESYNC] …         프롬프트 놓침 → 더미 40바이트로 스트림 복구 = **병리**

⚠ `[TX-DROP]` 은 앵커가 아니다. `[TX]` 와 한 글자 차이이고, 요청자가 이 구분을 놓쳐
   결론을 한 번 뒤집었다. 이 도구는 `[TX] ` (뒤에 공백)만 앵커로 잡는다.

**앵커** = `[TX] ` 줄. **측정값** = 그 앵커 이후 **다음 `SEND OK` 가 나타날 때까지의 경과**.

**모집단은 그 앵커의 *다음* 송신 주기가 무엇이냐로 가른다:**

    N (정상)  다음 주기가 `[TX] `        (= 성공)
    P (병리)  다음 주기가 `[TX-RESYNC]`  (= 프롬프트 놓침)

🔴 귀속하지 마라 — 이 도구가 **말할 수 없는 것**
------------------------------------------------
로그는 `SEND OK` 가 **어느 명령의 응답인지 표시하지 않는다.** 사이에 재시도 `CIPSEND` 가 낀다.
그러니 *"그 CIPSEND 의 응답이 N 초 걸렸다"* 고 쓰면 안 된다.

받칠 수 있는 표현은 여기까지다:
    **"앵커 이후 다음 `SEND OK` 가 나타나기까지 N 초 동안 어떤 `SEND OK` 도 없었다."**

⚠ **1초 해상도**다. 두 정수 초의 차가 Δ 이면 실제 경과는 **(Δ-1, Δ+1)** 구간 어딘가다.
   Δ=0 은 "지연 0" 이 아니라 "같은 초 안"이다. **평균·백분위수를 만들지 않는다** —
   해상도가 1초인데 백분위수를 내면 없는 정밀도가 생긴다(요청자가 명시적으로 금했고 옳다).

`SEND OK` 가 "안 왔다" 는 세 가지다 — 갈라서 낸다
--------------------------------------------------
    (a) 왔다                      앵커 뒤에 `SEND OK` 가 나타났고 그 사이 링크가 안 죽었다
    (b) 링크 붕괴가 먼저 왔다      `전송 3회 연속 실패` 또는 `[DIAG] offline step=` 이 `SEND OK` 보다 먼저
                                  → 그 뒤의 `SEND OK` 는 회복 사다리의 것이지 이 전송의 것이 아니다
    (c) 창 끝까지 없었다           로그가 거기서 끝났다(관측 종료). **'안 온 것'이 아니다**

🔴 2026-08-16 정정 — (b) 의 표지가 `전송 3회 연속 실패` 하나였을 때 **N 칸이 오염됐다.**
   pre-A `17:06:42` 앵커가 `Δ=9s` 로 N 최대값이 됐는데, 원문을 여니 `ERROR`/`Unlink` 로
   **링크가 죽고 회복 사다리가 돈 9초**였다. 3진아웃을 거치지 않았으므로 (b) 에 안 걸렸고,
   다음 주기가 `[TX]` 라서 N 으로 분류됐다.
   → `[DIAG] offline step=` 을 표지에 더했다. **A 구간의 N 최대값(19:25:33 Δ=2s)은
     원문 확인 결과 깨끗하다** — 이 정정으로 A 의 결론은 바뀌지 않는다.

사용: python3 monitor/resync_gap.py [로그] [시작ISO] [끝ISO]
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter
from datetime import datetime, timedelta

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-newbase.log"
SINCE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 16, 17, 53, 7)
UNTIL = datetime.fromisoformat(sys.argv[3]) if len(sys.argv) > 3 else datetime(2026, 8, 16, 21, 4, 50)

TS = re.compile(rb"^(\d{2}):(\d{2}):(\d{2})\s+(.*)$")
HDR_DATE = re.compile(rb"date=(\d{4})-(\d{2})-(\d{2})")

ANCHOR = re.compile(rb"\[TX\] ")            # 뒤 공백 필수 — [TX-DROP]/[TX-RESYNC] 를 배제한다
RESYNC = b"[TX-RESYNC]"
SENDOK = b'"SEND OK"'
BUSY = b'"busy '
OFFLINE = ("전송 3회 연속 실패".encode("utf-8"), b"[DIAG] offline step=")

# 개입(같은 기계에 시험 서버 인스턴스가 뜬 시각) — mech_timeline.py 와 같은 목록
LAST_INTERVENTION = datetime(2026, 8, 16, 19, 15, 32)
QUIET_FROM = LAST_INTERVENTION + timedelta(seconds=120)


def parse(path: str):
    """(시각, 원문바이트). 날짜 없는 로그의 자정 넘김 처리(원장 2.2)."""
    base = None
    prev = None
    day = 0
    with open(path, "rb") as f:
        for raw in f:
            if base is None:
                m = HDR_DATE.search(raw)
                if m:
                    base = datetime(int(m[1]), int(m[2]), int(m[3]))
            m = TS.match(raw)
            if not m:
                continue
            cur = int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3])
            if prev is not None and cur < prev - 3600:
                day += 1
            prev = cur
            b = base or datetime(2026, 8, 16)
            yield b + timedelta(days=day, seconds=cur), raw


def main() -> int:
    p = os.path.abspath(LOG)
    print(f"# 원본 로그: {p}")
    print(f"# 구간 {SINCE} ~ {UNTIL}  ({(UNTIL-SINCE).total_seconds()/3600:.2f}h)")
    print("# 앵커 = `[TX] ` (성공한 전송) · 측정 = 앵커 이후 다음 `SEND OK` 까지")
    print("# ⚠ 1초 해상도. Δ 의 실제 경과는 (Δ-1, Δ+1) 구간이다. 평균·백분위수 없음.")
    print("# ⚠ `SEND OK` 를 특정 명령에 귀속하지 않는다 — 로그가 주인을 표시하지 않는다.")

    rows = [(t, b) for t, b in parse(LOG) if SINCE <= t <= UNTIL]
    if not rows:
        print("\n구간에 자료가 없다.", file=sys.stderr)
        return 1

    # 송신 주기 시작 지점을 순서대로 모은다
    cycles = []  # (인덱스, 시각, 'TX'|'RESYNC')
    for i, (t, b) in enumerate(rows):
        if ANCHOR.search(b):
            cycles.append((i, t, "TX"))
        elif RESYNC in b:
            cycles.append((i, t, "RESYNC"))

    recs = []
    for c in range(len(cycles) - 1):
        i, t, kind = cycles[c]
        if kind != "TX":
            continue
        nxt_kind = cycles[c + 1][2]
        pop = "P" if nxt_kind == "RESYNC" else "N"

        gap = None
        outcome = "c.창끝까지없음"
        busy_seen = False
        for j in range(i + 1, len(rows)):
            tj, bj = rows[j]
            if BUSY in bj:
                busy_seen = True
            if any(k in bj for k in OFFLINE):
                outcome = "b.링크붕괴먼저"
                gap = int((tj - t).total_seconds())
                break
            if SENDOK in bj:
                outcome = "a.왔다"
                gap = int((tj - t).total_seconds())
                break
        recs.append({"t": t, "pop": pop, "gap": gap, "out": outcome, "busy": busy_seen})

    for pop, title in (("P", "P (병리 — 다음 주기가 `[TX-RESYNC]`)"),
                       ("N", "N (정상 — 다음 주기가 `[TX]`)")):
        sub = [r for r in recs if r["pop"] == pop]
        print(f"\n{'='*66}\n## {title} — **{len(sub)}건**")
        if not sub:
            print("   0건. ⚠ '안 난다'가 아니라 '이 구간에 없었다'이다(원장 1.1).")
            continue
        out = Counter(r["out"] for r in sub)
        for k in sorted(out):
            print(f"   {k:<18} {out[k]:>5}건")
        arrived = [r for r in sub if r["out"] == "a.왔다"]
        if arrived:
            h = Counter(r["gap"] for r in arrived)
            print(f"\n   ### Δ 히스토그램 — '왔다' {len(arrived)}건 (1초 단위, 누계 아님)")
            for k in sorted(h):
                print(f"     Δ={k}s  {h[k]:>5}건  {'█'*min(h[k],40)}")
            mx = max(arrived, key=lambda r: r["gap"])
            print(f"   → 최대 Δ = {mx['gap']}s  (앵커 {mx['t']:%m-%d %H:%M:%S})"
                  f"  · 실제 경과는 ({mx['gap']-1}, {mx['gap']+1})초")
        blocked = [r for r in sub if r["out"] == "b.링크붕괴먼저"]
        if blocked:
            print(f"\n   ### 링크 붕괴가 먼저 온 {len(blocked)}건 — **이 전송의 `SEND OK` 는 오지 않았다**")
            for r in blocked:
                print(f"     앵커 {r['t']:%m-%d %H:%M:%S} → {r['gap']}s 뒤 링크 붕괴"
                      f" · busy {'있음' if r['busy'] else '없음'}")
        nb = sum(1 for r in sub if r["busy"])
        print(f"\n   busy 동반: {nb}/{len(sub)}건")
        quiet = [r for r in sub if r["t"] >= QUIET_FROM]
        if quiet:
            qa = [r for r in quiet if r["out"] == "a.왔다"]
            qm = max((r["gap"] for r in qa), default=None)
            print(f"   개입 없는 구간({QUIET_FROM:%H:%M:%S} 이후)만: {len(quiet)}건"
                  f" · '왔다' {len(qa)}건 · 최대 Δ {qm}s")

    print(f"\n{'='*66}\n## 읽는 법")
    print("  · **P 의 '왔다' 는 상한을 올릴 근거이고, '링크붕괴먼저' 는 올려도 소용없다는 근거다.**")
    print("    둘의 비율이 설계를 가른다 — 어느 한쪽만 인용하지 마라.")
    print("  · Δ 를 '그 CIPSEND 의 응답 시간' 으로 부르지 마라. 그 귀속은 로그가 못 받친다.")
    print("  · 표본이 적으면 건수를 그대로 적어라. 비율로 바꾸지 마라.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
