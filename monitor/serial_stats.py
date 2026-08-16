#!/usr/bin/env python3
"""시리얼 캡처 집계기 — **서버가 원리적으로 못 보는 것**만 센다.

REQ-0092 가 "서버 로그로 답할 수 없는 것" 으로 목록화한 항목들이다:

  · `[TX-DROP]`  장치가 보내려다 버린 프레임. **seqNo 는 성공 시에만 증가**하므로
                 서버 seq 연속성에 구멍을 안 남긴다 → **상행 소실률은 서버로 못 잰다.**
  · `[LINK] closed reason=`  왜 끊었는지. 서버는 `errno=54`(상대가 RST)까지만 안다.
  · `[AT] resync`  프롬프트 재동기 횟수.
  · `[RAM] free/min`  RAM 여유와 **최저치** — 누수는 최저치에서만 보인다.
  · `[BOOT] n=`  EEPROM 부팅 카운터. 서버가 안 보던 구간의 재부팅도 소급된다.

TX-DROP 은 속도 제한 형식이라 두 줄로 온다(arduino-engineer, REQ-0094):
    [107]   [TX-DROP] seq=0 run=1      ← 시작
    [10114] [TX-DROP] seq=0 run=21     ← 10초마다 누적
    [36769] [TX-DROP] end run=73       ← 복구하며 닫는 줄(최종 누적)
따라서 **`end run=N` 의 N 을 더한 것이 그 에피소드의 실제 드롭 수**다. 시작줄을 세면 안 된다.
아직 안 닫힌 에피소드는 마지막 `run=` 값을 쓰되 **진행중으로 따로 표기**한다.

사용: python3 monitor/serial_stats.py --since 2026-08-16T11:27:00
"""
from __future__ import annotations

import argparse
import re
from datetime import date, datetime, time as dtime, timedelta

TS = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\s\s?(.*)$")
MILLIS = re.compile(r"^\[(\d+)\]\s*(.*)$")
TXD_RUN = re.compile(r"^\[TX-DROP\](?:\s+seq=(\d+))?\s+run=(\d+)")
TXD_END = re.compile(r"^\[TX-DROP\]\s+end\s+run=(\d+)")
BOOT = re.compile(r"^\[BOOT\]\s+n=(\d+)")
RAM = re.compile(r"^\[RAM\]\s+free=(\d+)\s+min=(\d+)")
LINK = re.compile(r"^\[LINK\]\s+(.*)$")
ATL = re.compile(r"^\[AT\]\s+(.*)$")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default="monitor/serial-esplink.log")
    ap.add_argument("--base-date", default="2026-08-16")
    ap.add_argument("--since", default=None)
    ap.add_argument("--until", default=None)
    args = ap.parse_args()

    base = date.fromisoformat(args.base_date)
    since = datetime.fromisoformat(args.since) if args.since else None
    until = datetime.fromisoformat(args.until) if args.until else None

    prev_secs = None
    day = 0
    rows = []
    with open(args.log, "rb") as f:
        for raw in f:
            line = raw.decode("utf-8", "replace").rstrip("\n")
            m = TS.match(line)
            if not m:
                continue
            hh, mm, ss, body = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)
            secs = hh * 3600 + mm * 60 + ss
            if prev_secs is not None and secs < prev_secs - 3600:
                day += 1
            prev_secs = secs
            rows.append((day, secs, body))

    maxday = day
    events = []
    for d, secs, body in rows:
        ts = datetime.combine(base - timedelta(days=(maxday - d)), dtime()) + timedelta(seconds=secs)
        events.append((ts, body))

    sel = [(ts, b) for ts, b in events
           if (since is None or ts >= since) and (until is None or ts < until)]

    boots, rams, links, resyncs, taps = [], [], [], 0, 0
    txd_episodes = []          # (마지막 run 값, 닫혔는가, 시각)
    cur_run = None
    cur_start = None

    for ts, body in sel:
        if body.startswith("«tap»"):
            taps += 1
            continue
        mm = MILLIS.match(body)
        payload = mm.group(2) if mm else body
        ms = int(mm.group(1)) if mm else None

        if TXD_END.match(payload):
            n = int(TXD_END.match(payload).group(1))
            txd_episodes.append({"drops": n, "closed": True,
                                 "start": cur_start, "end": ts})
            cur_run, cur_start = None, None
            continue
        m2 = TXD_RUN.match(payload)
        if m2:
            n = int(m2.group(2))
            if cur_run is None:
                cur_start = ts
            cur_run = n
            continue
        if BOOT.match(payload):
            boots.append((ts, int(BOOT.match(payload).group(1))))
            continue
        m3 = RAM.match(payload)
        if m3:
            rams.append((ts, int(m3.group(1)), int(m3.group(2))))
            continue
        m4 = LINK.match(payload)
        if m4:
            links.append((ts, m4.group(1)))
            continue
        m5 = ATL.match(payload)
        if m5 and "resync" in m5.group(1):
            resyncs += 1

    if cur_run is not None:
        txd_episodes.append({"drops": cur_run, "closed": False,
                             "start": cur_start, "end": None})

    # ── `resync 연발` 이 정말 끊김의 선행 지표인가 ────────────────────────
    # 12:17 의 끊김 직전 7초에 resync 4회가 몰렸다. 그런데 **표본 1건으로 선행 지표라 부르면 안 된다.**
    # 연발이 몇 번 있었고 그 중 몇 번이 실제 끊김으로 갔는지 세야 한다 —
    # 연발이 흔한데 끊김이 드물면 그 신호는 거짓양성투성이라 쓸모가 없다.
    BURST_N, BURST_W, LEAD = 3, 15.0, 30.0
    rs = [ts for ts, b in sel if ATL.match(MILLIS.match(b).group(2) if MILLIS.match(b) else b)
          and "resync" in b]
    bursts = []
    i = 0
    while i < len(rs):
        j = i
        while j + 1 < len(rs) and (rs[j + 1] - rs[i]).total_seconds() <= BURST_W:
            j += 1
        if j - i + 1 >= BURST_N:
            bursts.append((rs[i], rs[j], j - i + 1))
            i = j + 1
        else:
            i += 1
    closed_ts = [ts for ts, v in links if v.startswith("closed")]
    burst_hit = 0
    for b0, b1, n in bursts:
        if any(0 <= (c - b1).total_seconds() <= LEAD or b0 <= c <= b1 for c in closed_ts):
            burst_hit += 1

    total_drops = sum(e["drops"] for e in txd_episodes)
    open_ep = [e for e in txd_episodes if not e["closed"]]

    print(f"# 시리얼 집계 — {args.log}")
    print(f"# 구간: {since or '(처음)'} ~ {until or '(끝)'} · 캡처줄 {len(sel)}")
    print()
    print(f"[TX-DROP] 에피소드 {len(txd_episodes)}회 · **버린 프레임 합계 {total_drops}**")
    print("          ↑ 서버 로그로는 원리적으로 못 세는 값(seqNo 는 성공 시에만 증가)")
    if open_ep:
        print(f"          ⚠ 아직 안 닫힌 에피소드 {len(open_ep)}개 — 지금 전송이 실패 중일 수 있다")
    for e in txd_episodes[-8:]:
        dur = f"{(e['end'] - e['start']).total_seconds():.0f}s" if e["closed"] and e["start"] else "진행중"
        print(f"            {e['start']} ~ {e['end'] or '…'}  {dur}  drops={e['drops']}")
    print()
    print(f"[BOOT] {len(boots)}회 " + (", ".join(f"{t}(n={n})" for t, n in boots) if boots else "— 없음"))

    # ── 장치가 링크를 포기한 횟수 vs 재연결 소요 ──────────────────────────
    # 서버 세션 종료만 세면 장치측 링크 포기를 과소 계상한다. 실제로 이 구간에서 4:1 로 갈렸다 —
    # 재연결이 빠르면(≈3초) TCP 세션이 살아남아 서버 로그에 흔적이 거의 안 남는다.
    closes = [(t, v) for t, v in links if v.startswith("closed")]
    oks = [(t, int(re.search(r"reconnect ok (\d+)", v).group(1)))
           for t, v in links if v.startswith("reconnect ok")]
    print(f"[LINK] **장치측 링크 포기 {len(closes)}회** "
          f"(사유: {', '.join(sorted({v.split('reason=')[-1] for _, v in closes})) or '-'})")
    if oks:
        ds = sorted(d for _, d in oks)
        print(f"       재연결 성공 {len(oks)}회 · 소요(ms) {ds}")
        fast = [d for d in ds if d < 10000]
        slow = [d for d in ds if d >= 10000]
        print(f"       빠름(<10초) {len(fast)}회 · 느림(≥10초) {len(slow)}회"
              + (f" ← 느린 쪽이 서버 세션 단절로 이어진다" if slow else ""))
    print(f"[LINK] {len(links)}건 " + (", ".join(f"{t.strftime('%H:%M:%S')} {v}" for t, v in links[-6:]) if links else "— 없음"))
    print(f"[AT] resync {resyncs}회")
    print(f"     연발(≤{BURST_W:.0f}초에 {BURST_N}회 이상) {len(bursts)}건 · "
          f"그 중 {LEAD:.0f}초 안에 [LINK] closed 로 간 것 **{burst_hit}건**")
    if bursts:
        prec = 100.0 * burst_hit / len(bursts)
        # ⚠ 표본이 몇 건인지를 비율보다 **먼저** 말한다.
        #   `2/3 = 67%` 를 "선행 지표로 쓸 만하다"로 적었다가 스스로 걸렀다 —
        #   오늘 이 프로젝트에서 4건 표본으로 결론 낼 뻔한 사고와 같은 형태다.
        if len(bursts) < 10:
            print(f"     → {burst_hit}/{len(bursts)} ({prec:.0f}%) — "
                  f"🔴 **표본 {len(bursts)}건. 비율로 말하지 마라.** "
                  f"선행 지표인지 판정하려면 연발이 최소 10건은 쌓여야 한다")
        else:
            print(f"     → 연발이 끊김을 맞힌 비율 {prec:.0f}% (n={len(bursts)}) "
                  + ("— 거짓양성이 많다. 단독 경보로 쓰면 안 된다" if prec < 50
                     else "— 선행 지표로 쓸 만하다"))
        print(f"     → 끊김 {len(closed_ts)}건 중 연발이 앞선 것 "
              f"{sum(1 for c in closed_ts if any(0 <= (c - b1).total_seconds() <= LEAD or b0 <= c <= b1 for b0, b1, _ in bursts))}건 (재현율)")
        for b0, b1, n in bursts[-5:]:
            hit = any(0 <= (c - b1).total_seconds() <= LEAD or b0 <= c <= b1 for c in closed_ts)
            print(f"       {b0.strftime('%H:%M:%S')}~{b1.strftime('%H:%M:%S')} {n}회 "
                  f"{'→ 끊김' if hit else '→ 회복(끊기지 않음)'}")
    if rams:
        f0, m0 = rams[0][1], rams[0][2]
        f1, m1 = rams[-1][1], rams[-1][2]
        lowest = min(r[2] for r in rams)
        print(f"[RAM] 표본 {len(rams)} · 처음 free={f0} min={m0} → 마지막 free={f1} min={m1} · 최저 {lowest}")
        if lowest < m0:
            print(f"      ⚠ 최저치가 {m0} → {lowest} 로 내려갔다 — 누수 후보. 추세를 더 봐야 한다")
        else:
            print(f"      ✔ 최저치가 안 내려갔다 — 이 구간에서 누수 징후 없음")
    else:
        print("[RAM] 표본 없음 — **건강의 증거가 아니라 관측이 없는 것**")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
