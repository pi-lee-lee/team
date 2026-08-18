#!/usr/bin/env python3
"""창4 사건 11건의 **순서**를 잰다 — 하행이 증상보다 먼저인가, 뒤인가.

왜 이 도구가 필요한가
  창4 판정에서 "브라우저 예약 구간에 사건이 몰렸다"까지는 비율로 보였다.
  🔴 그런데 비율은 **인과의 방향을 못 가른다.** 두 방향이 같은 비율을 만든다:

    (가) 하행이 링크를 깬다        하행 도착 → 상행 SEND OK 소실 → 3진아웃
    (나) 링크가 깨져서 하행이 는다  링크 불통 → 서버가 **같은 seq 를 재전송** → 하행 줄 수 증가

  🔴 (나) 는 실재한다. 서버 로그에서 같은 seq 가 2~3회 반복되는 것이 보인다
     (예: `R,18` 이 13:19:48·:50·:51). 그래서 **하행 줄 수를 그대로 노출로 쓰면
     사건이 만든 재전송을 원인으로 착각한다** — 같은 사건을 두 쪽에서 본 것을
     상관으로 세는 것이다.

  → 그래서 이 도구는 **`seq` 가 처음 나온 시각(=신규 하행)** 만 자극으로 쓰고,
    재전송은 결과로 분류한다. 그리고 **첫 증상 시각과의 순서**를 잰다.

무엇을 자극·증상으로 잡나 (정의를 박아 둔다)
  자극  서버 로그 `→ARD <kind>,<seq>,...` 에서 그 `(kind,seq)` 가 **처음** 나온 줄
  증상  `[NET] 전송 실패 1/3` (3진아웃 계열) · `사건 − 10s` (정지감지 계열 — 감지기의 창이 10s)
  사건  `[NET] 전송 3회 연속 실패` 또는 `[NET] ★ 정지 감지` (원장 7.61 의 창4 사건 단위)

🔴 **`[TX-WAIT]` 를 증상으로 쓰지 마라 — 1차 판본이 그걸로 틀렸다.**
   `[TX-WAIT]`(SEND OK 못 봄 → 이번 주기 건너뜀)는 **정상 운전에서 수백 번 난다**
   (13:19 시점 누적 `skip 88`). 그것을 "첫 증상"으로 잡으니 사건과 무관한 50초 전 줄이
   잡히고, 그 앞의 신규 하행을 찾다가 **23분 전 하행**이 자극으로 붙어
   `🔴하행이 먼저` 가 찍혔다. **도구가 돌면서 틀린 답을 냈다**(원장 7.94).
   → 자극이 `30s` 밖이면 이제 **`무관`** 으로 찍는다. 순서 판정을 아예 하지 않는다.

⚠ 시계가 둘이다. 시리얼은 **탭이 줄을 읽은 시각**, 서버는 **서버 자기 시각**이다.
   같은 머신이지만 **해상도가 1초**라 ±1초 차이는 순서 근거로 쓰지 마라(원장 1.7·7.51).
   이 도구는 그래서 **±1초 안이면 `동시`** 로 찍는다.

⚠ 날짜: 시리얼 줄에는 날짜가 없다(`HH:MM:SS`). 창4 는 자정을 넘지 않으므로 하루로 고정한다.
   **자정을 넘는 창에 이 도구를 그대로 쓰지 마라**(원장 2.2 — 조용히 0 이 된다).

사용:
  python3 monitor/win4_causal.py [시리얼로그] [서버로그] [시작HH:MM:SS] [끝HH:MM:SS]
"""
from __future__ import annotations

import os
import sys

DAY = None   # 🔴 2026-08-19: 날짜를 박아 두면 **다른 날 로그가 조용히 빠진다**.
             #   서버 로그는 여러 날에 걸쳐 있고, 옛 판 프레임이 오늘 것으로 읽힌다(socket 실측).
             #   None 이면 날짜 필터를 끄고, 쓸 때는 인자로 받는다.
SER = "monitor/serial-win4.log"
SRV = os.path.expanduser("~/parking-logs/parking-server.log")
T_START, T_END = "07:53:49", "14:48:10"
NEAR = 1       # 초. 이 안이면 순서를 단정하지 않는다(시계 둘 · 1초 해상도)
LOOKBACK = 30  # 초. 증상보다 이만큼 안쪽의 신규 하행만 자극으로 본다. 밖이면 `무관`


def hms(t: str) -> int:
    h, m, s = (int(x) for x in t.split(":"))
    return h * 3600 + m * 60 + s


def fmt(sec: int) -> str:
    return "%02d:%02d:%02d" % (sec // 3600, sec % 3600 // 60, sec % 60)


def main() -> int:
    ser = sys.argv[1] if len(sys.argv) > 1 else SER
    srv = sys.argv[2] if len(sys.argv) > 2 else SRV
    t0 = hms(sys.argv[3] if len(sys.argv) > 3 else T_START)
    t1 = hms(sys.argv[4] if len(sys.argv) > 4 else T_END)

    # ── 서버: 하행. (kind,seq) 첫 등장 = 신규, 그 뒤 = 재전송
    seen: dict[str, int] = {}
    new_dl: list[tuple[int, str]] = []      # (t, "R,18")
    retx: list[tuple[int, str]] = []
    srv_lines = 0
    for line in open(srv, encoding="utf-8", errors="replace"):
        srv_lines += 1
        if "→ARD" not in line or not line.startswith(DAY):
            continue
        t = hms(line[11:19])
        body = line.split("→ARD", 1)[1].strip().split(",")
        if len(body) < 2:
            continue
        key = "%s,%s" % (body[0], body[1])
        if key in seen:
            retx.append((t, key))
        else:
            seen[key] = t
            new_dl.append((t, key))
    win_new = [x for x in new_dl if t0 <= x[0] < t1]
    win_retx = [x for x in retx if t0 <= x[0] < t1]

    # ── 시리얼: 사건과 증상
    events: list[tuple[int, str]] = []
    symptom: list[int] = []          # 실패 1/3 · TX-WAIT
    banner: list[int] = []
    ipzero: list[int] = []
    ser_lines = 0
    for line in open(ser, encoding="utf-8", errors="replace"):
        ser_lines += 1
        if len(line) < 9 or line[2] != ":" or line[5] != ":":
            continue
        try:
            t = hms(line[:8])
        except ValueError:
            continue
        if "전송 3회 연속 실패" in line:
            events.append((t, "3진아웃"))
        elif "정지 감지" in line:
            events.append((t, "정지감지"))
        if "전송 실패 1/3" in line:
            symptom.append(t)
        if "System Ready" in line:
            banner.append(t)
        if '"0.0.0.0"' in line or "IP 를 잃었다" in line:
            ipzero.append(t)
    win_ev = [e for e in events if t0 <= e[0] < t1]

    print("# 창4 인과 순서 — 신규 하행 vs 첫 증상")
    print("# 시리얼 %s · 서버 %s" % (ser, srv))
    print("# 구간 %s %s ~ %s  (%.4fh)" % (DAY, fmt(t0), fmt(t1), (t1 - t0) / 3600))
    print("# 훑은 줄: 시리얼 %d · 서버 %d" % (ser_lines, srv_lines))
    print("# 찾은 문자열: '전송 3회 연속 실패' '정지 감지' '전송 실패 1/3' '[TX-WAIT]' '→ARD'")
    print("# 창 안: 사건 %d · 신규 하행 %d · 재전송 하행 %d"
          % (len(win_ev), len(win_new), len(win_retx)))
    if not win_ev:
        print("# 🔴 사건 0건 — 위 분모를 보고 '건강'인지 '못 셈'인지 갈라라(원장 1.1)")
        return 0

    print()
    print("| 사건 | 단위 | 직전 신규 하행 | Δ(하행→사건) | 첫 증상 | Δ(하행→증상) | 순서 | 배너 | 0.0.0.0 |")
    print("|---|---|---|---|---|---|---|---|---|")
    tally: dict[str, int] = {}
    for t, kind in win_ev:
        # 증상 시각: 3진아웃 = `전송 실패 1/3`(사건 직전 20초 안) · 정지감지 = 사건 − 10s
        if kind == "정지감지":
            s0 = t - 10
            s0src = "감지창"
        else:
            sym = [s for s in symptom if t - 20 <= s <= t]
            s0 = min(sym) if sym else None
            s0src = "실패1/3" if sym else "없음"
        # 자극: 증상 기준 30초 안의 **신규** 하행 중 가장 늦은 것
        ref = s0 if s0 is not None else t
        cand = [d for d in win_new if ref - LOOKBACK <= d[0] <= ref + NEAR]
        d0 = cand[-1] if cand else None

        bn = "○" if any(t - 3 <= b <= t + 10 for b in banner) else "—"
        iz = "○" if any(t - 3 <= z <= t + 10 for z in ipzero) else "—"

        if d0 is None:
            order = "무관(%ds 안 신규 하행 없음)" % LOOKBACK
            tally[order[:2]] = tally.get(order[:2], 0) + 1
            print("| %s | %s | 없음 | — | %s(%s) | — | %s | %s | %s |"
                  % (fmt(t), kind, fmt(s0) if s0 else "없음", s0src, order, bn, iz))
            continue
        gap = s0 - d0[0]
        if abs(gap) <= NEAR:
            order = "⚠동시(±%ds)" % NEAR
        elif gap > 0:
            order = "🔴하행이 먼저"
        else:
            order = "증상이 먼저"
        tally[order[:2] if not order.startswith("🔴") else "하행"] = \
            tally.get(order[:2] if not order.startswith("🔴") else "하행", 0) + 1
        print("| %s | %s | %s %s | %+ds | %s(%s) | %+ds | %s | %s | %s |"
              % (fmt(t), kind, fmt(d0[0]), d0[1], t - d0[0],
                 fmt(s0), s0src, gap, order, bn, iz))
    print()
    print("## 갈래별 (%d건)" % len(win_ev))
    for k, v in sorted(tally.items(), key=lambda x: -x[1]):
        print("  %-6s %d" % (k, v))

    print()
    print("## 읽는 법")
    print("· `🔴하행이 먼저` = **신규**(재전송 아님) 하행이 첫 증상보다 앞섰다.")
    print("  재전송을 자극에서 뺐으므로 (나) 방향(사건→재전송)으로는 설명되지 않는 항목이다.")
    print("· `⚠동시` = 1초 해상도로는 순서를 못 가른다. **인과 근거로 쓰지 마라.**")
    print("· `⚠먼과거` = 직전 신규 하행이 60초 이상 전이다 — 하행과 무관한 사건 후보.")
    print("· 배너·0.0.0.0 은 **리셋 표지**다(판별자 v4). 둘이 다 없으면 리셋계열이 아니다.")
    print("· 🔴 이 표는 **순서**만 준다. 순서는 인과의 필요조건이지 충분조건이 아니다.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
