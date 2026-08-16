#!/usr/bin/env python3
"""15:27 이후 붕괴 구간 정량화 — Uno 재부팅의 '선행 사건'을 찾는다.

두 가지 물음에 답한다.

  (A) arduino-engineer 의 판별 관측(REQ-0103 §4):
      Uno 재부팅 **직전** 10~30초에 재접속 폭주가 선행하는가?
        선행한다  → 되먹임 루프(소프트웨어가 스스로 무너뜨린다)
        선행 안 함 → 전원 쪽(바깥에서 꺼진다)

  (B) socket-engineer 의 44초 사이클(REQ-0106 §3-4)과 같은 사건인가?
      각 접속 사이클 직후 첫 S 프레임의 uptime 이 작으면 같은 사건이다.

⚠ 이 로그에는 서버 인스턴스가 여러 개 섞여 있다(`▣ 소크 종료` 표식).
   그래서 **누적 카운터를 쓰지 않고**, 장치가 보낸 `uptime` 과 이벤트 시각만 쓴다.
   uptime 은 Uno 의 millis()/1000 이다(arduino-engineer 확인) — ESP 가 아니라 Uno 의 시간이다.

사용: python3 monitor/collapse_probe.py [로그] [시작ISO] [끝ISO]
"""
from __future__ import annotations

import re
import sys
from datetime import datetime, date, timedelta

LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/parking-soak.log"
SINCE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 16, 15, 27, 0)
UNTIL = datetime.fromisoformat(sys.argv[3]) if len(sys.argv) > 3 else datetime(2026, 8, 16, 23, 59, 59)
BASE = date(2026, 8, 16)

TS = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\s+(.*)$")
SFRAME = re.compile(r"←ARD\s+S,(\d+),([01]+),([01]+),(\d+),(\w+),(\d+)")


def parse():
    """(시각, 종류, 상세) 열.

    ⚠ 날짜는 **뒤에서 앞으로** 앵커링한다(soak_stats.py 와 같은 방식).
       로그에는 시:분:초만 있고 날짜가 없다. 앞에서부터 BASE 로 잡으면
       08-15 에 시작한 로그가 통째로 하루씩 밀려 구간 질의가 전부 빈다.
       마지막 줄이 BASE 라고 보고 거슬러 올라가며 자정을 만나면 하루 뺀다.
    """
    with open(LOG, "rb") as f:
        raw = f.read()
    rows = []
    for line in raw.decode("utf-8", "replace").splitlines():
        m = TS.match(line)
        if not m:
            continue
        rows.append((int(m[1]), int(m[2]), int(m[3]), m[4]))

    stamps = [None] * len(rows)
    day = BASE
    nxt_hms = None
    for i in range(len(rows) - 1, -1, -1):
        h, mi, s, _ = rows[i]
        hms = (h, mi, s)
        if nxt_hms is not None and hms > nxt_hms:
            day = day - timedelta(days=1)
        nxt_hms = hms
        stamps[i] = datetime.combine(day, datetime.min.time()).replace(hour=h, minute=mi, second=s)

    out = []
    for (h, mi, s, rest), ts in zip(rows, stamps):
        sm = SFRAME.search(rest)
        if sm:
            out.append((ts, "S", {"seq": int(sm[1]), "uptime": int(sm[4]), "dev": sm[5]}))
        elif "+? 연결 수락" in rest:
            out.append((ts, "ACCEPT", {}))
        elif "id 미상 소켓 마감" in rest:
            out.append((ts, "NOID_CLOSE", {}))
        elif "+ARD" in rest:
            out.append((ts, "SESS_OPEN", {"raw": rest[:70]}))
        elif "-ARD" in rest:
            out.append((ts, "SESS_CLOSE", {"raw": rest[:70]}))
        elif "오프라인 판정" in rest:
            out.append((ts, "OFFLINE", {}))
        elif "▣ 소크 종료" in rest:
            out.append((ts, "SRV_STOP", {"raw": rest[:60]}))
    return out


CONTAM_SINCE = datetime(2026, 8, 16, 15, 26, 0)


def contam_guard():
    """🔴 오염 구간을 말없이 집계하지 않는다.

    2026-08-16 15:26 이후는 사용자가 아두이노를 물리적으로 뽑았다 끼운 구간이다
    (사용자 확인 · REQ-0108). 거기서 나오는 재부팅·단절은 **고장이 아니다.**
    막지는 않는다 — 그 구간을 일부러 볼 일이 있다. 다만 **조용히 지나가지 않는다.**
    """
    if UNTIL <= CONTAM_SINCE:
        return
    print("=" * 72)
    print("🔴 경고 — 요청한 구간이 오염 구간(2026-08-16 15:26~)과 겹친다.")
    print("   그 구간의 재부팅·세션단절·프레임공백은 **사용자가 보드를 뽑았다 끼운 결과**다.")
    print("   장비/펌웨어 판정의 근거로 인용하지 마라. (출처: 사용자 확인 · REQ-0108)")
    if SINCE < CONTAM_SINCE:
        print(f"   깨끗한 부분만 보려면: --until 로 {CONTAM_SINCE.isoformat()} 을 지정해라.")
    print("=" * 72)
    print()


def main() -> int:
    contam_guard()
    ev = [e for e in parse() if SINCE <= e[0] <= UNTIL]
    if not ev:
        print("구간에 이벤트 없음 — 로그가 이 구간을 담고 있지 않다.", file=sys.stderr)
        return 1

    frames = [e for e in ev if e[1] == "S"]
    print(f"# 구간 {SINCE} ~ {min(UNTIL, ev[-1][0])}")
    print(f"# 이벤트 {len(ev)}건 · S프레임 {len(frames)}건")
    srv_stop = [e for e in ev if e[1] == "SRV_STOP"]
    if srv_stop:
        print(f"# ⚠ 이 구간에 서버 인스턴스 종료 {len(srv_stop)}건 — 누적 카운터는 쓰지 않았다")
    print()

    # ── Uno 재부팅 검출: 연속 S 프레임 사이에서 uptime 이 줄면 재부팅 ──
    # ⚠ 재부팅의 물리적 시각은 알 수 없다. 우리가 보는 것은
    #   t0 = 재부팅 전 **마지막** 프레임, t1 = 재부팅 후 **첫** 프레임 이고
    #   진짜 재부팅은 그 사이 공백 어딘가에 있다.
    #   따라서 '선행 사건'은 반드시 **t0 이전**에서 세야 한다.
    #   t1 기준으로 세면 재부팅 **뒤**의 재접속을 '선행'으로 오계상한다.
    reboots = []
    for (t0, _, a), (t1, _, b) in zip(frames, frames[1:]):
        if b["uptime"] < a["uptime"]:
            reboots.append((t1, a["uptime"], b["uptime"], (t1 - t0).total_seconds(), t0))

    print(f"## A. Uno 재부팅 {len(reboots)}건 — 직전 재접속이 선행하는가")
    print()
    if not reboots:
        print("  재부팅 0건.")
        print("  ⚠ 이 0 은 '안정적'이 아니라 **'S 프레임이 끊겨 uptime 을 못 봤다'** 일 수 있다.")
        print(f"     이 구간 S 프레임 {len(frames)}건 — 10건 미만이면 판별력이 없다.")
    else:
        print("  기준: '선행'은 **재부팅 전 마지막 프레임(t0) 이전**에서만 센다.")
        print("        재부팅은 t0~t1 공백 안에 있으므로 t1 기준으로 세면 재부팅 뒤 재접속을 잘못 센다.")
        print()
        print("  마지막프레임 t0  복귀 t1     uptime변화     공백 | t0이전 ACCEPT/NOID (10s/30s/60s) | 판정")

        def before(t0, w):
            return sum(1 for e in ev if e[1] in ("ACCEPT", "NOID_CLOSE")
                       and 0 < (t0 - e[0]).total_seconds() <= w)

        n_storm = 0
        for rt, u0, u1, gap, t0 in reboots:
            c10, c30, c60 = before(t0, 10), before(t0, 30), before(t0, 60)
            verdict = "폭주 선행" if c30 >= 3 else ("일부 선행" if c30 >= 1 else "선행 없음")
            if c30 >= 3:
                n_storm += 1
            print(f"  {t0.strftime('%H:%M:%S')}        {rt.strftime('%H:%M:%S')}  "
                  f"{u0:>6} → {u1:<5} {gap:>6.0f}s | {c10:>3} /{c30:>3} /{c60:>3}"
                  f"                     | {verdict}")
        print()
        print(f"  → 폭주 선행 {n_storm}/{len(reboots)}건")
        print()
        print("  ⚠ 'ACCEPT 0' 만으로는 약하다 — 세션이 살아 있는 동안엔 원래 ACCEPT 가 안 생긴다.")
        print("     그래서 **멈추기 직전에 프레임이 정상으로 흐르고 있었는지**를 따로 본다.")
        print("     정상 흐름 뒤 예고 없이 끊기면 = 바깥에서 꺼진 모양(전원 쪽).")
        print("     끊기기 전에 프레임이 성겨지면 = 장치가 먼저 힘들어한 모양(소프트/링크 쪽).")
        print()
        print("  t0 직전 60초 프레임수 (판정창 정상치 ≈ 53/분)   seq 연속성")
        for rt, u0, u1, gap, t0 in reboots:
            win = [f for f in frames if 0 <= (t0 - f[0]).total_seconds() <= 60]
            n = len(win)
            seqs = [f[2]["seq"] for f in win]
            jumps = sum(1 for a, b in zip(seqs, seqs[1:]) if b != a + 1)
            health = "정상 흐름" if n >= 40 else ("성김" if n >= 15 else "이미 끊긴 상태")
            print(f"  {t0.strftime('%H:%M:%S')}   {n:>3}프레임/60s   seq점프 {jumps}   → {health}")
        if len(reboots) < 5:
            print(f"  ⚠ 표본 {len(reboots)}건. 이 수로는 가설을 확정하지 못한다 — 경향만 본다.")

    # ── B. 접속 사이클 주기와, 사이클 직후 첫 S 프레임의 uptime ──
    print()
    print("## B. 접속 사이클 — socket 의 '44초 주기'와 대조")
    print()
    acc = [e[0] for e in ev if e[1] == "ACCEPT"]
    if len(acc) < 2:
        print(f"  ACCEPT {len(acc)}건 — 주기를 낼 수 없다(2건 이상 필요).")
    else:
        gaps = [(b - a).total_seconds() for a, b in zip(acc, acc[1:])]
        gaps_sorted = sorted(gaps)
        med = gaps_sorted[len(gaps_sorted) // 2]
        print(f"  ACCEPT {len(acc)}건 · 간격 중앙 {med:.0f}s · 최소 {min(gaps):.0f}s · 최대 {max(gaps):.0f}s")
        print(f"  간격 목록: {', '.join(f'{g:.0f}' for g in gaps[:20])}")
        near44 = sum(1 for g in gaps if 38 <= g <= 50)
        print(f"  38~50s 구간에 든 간격: {near44}/{len(gaps)}")
        print()
        print("  각 ACCEPT 직후 첫 S 프레임의 uptime (작으면 = 방금 부팅한 Uno):")
        shown = 0
        small = 0
        for t in acc:
            nxt = next((f for f in frames if f[0] >= t), None)
            if not nxt:
                continue
            dt = (nxt[0] - t).total_seconds()
            if dt > 120:
                continue
            flag = "← 방금 부팅" if nxt[2]["uptime"] <= 60 else ""
            if nxt[2]["uptime"] <= 60:
                small += 1
            if shown < 15:
                print(f"    {t.strftime('%H:%M:%S')} → +{dt:>5.0f}s  uptime={nxt[2]['uptime']:>6}  {flag}")
            shown += 1
        print(f"  → 첫 프레임 uptime<=60s 인 사이클 {small}/{shown}")
        if shown == 0:
            print("  ⚠ ACCEPT 뒤 120초 안에 S 프레임이 한 번도 안 왔다 —")
            print("     즉 **붙어도 프레임을 못 보낸다.** 이것 자체가 소견이다.")

    # ── 무프레임 구간 ──
    print()
    print("## C. 프레임 공백")
    if len(frames) >= 2:
        gaps = [((b[0] - a[0]).total_seconds(), a[0], b[0]) for a, b in zip(frames, frames[1:])]
        gaps.sort(reverse=True)
        print(f"  최대 공백 상위 5:")
        for g, a, b in gaps[:5]:
            print(f"    {g:>7.0f}s   {a.strftime('%H:%M:%S')} → {b.strftime('%H:%M:%S')}")
        total = (frames[-1][0] - frames[0][0]).total_seconds()
        dead = sum(g for g, _, _ in gaps if g > 10)
        print(f"  프레임 구간 {total/3600:.2f}h 중 10초 초과 공백 합계 {dead/3600:.2f}h "
              f"({100*dead/total if total else 0:.0f}%)")
    else:
        print(f"  S 프레임 {len(frames)}건 — 공백을 낼 수 없다.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
