#!/usr/bin/env python3
"""사슬을 **사건마다 줄 단위로** 확인한다 — 표지 공존을 사슬로 읽지 않기 위해.

왜 필요한가
  창4 판정 §0 의 사슬(하행 → `SEND OK` 소실 → 부트 배너 → `0.0.0.0`)은
  `13:19:4x` **한 건만** 원문으로 확인했다. 나머지는 집계 도구 둘이
  **표지가 창 안에 있다**는 것만 봤다 — `mech_split-v4`(리셋 표지 OR),
  `win4_causal`(신규 하행이 증상보다 앞선다). **둘 다 순서를 안 본다.**
  원장 7.42: 표지가 있다는 것은 그 사건이라는 뜻이 아니다.

무엇을 확인하나 — 순서 세 칸이 **그 순서로** 있는가
  ① `AT+CIPSEND` 뒤에 `SEND OK` 가 안 오는 자리 (전송 실패 1/3 로 확정)
  ② 그 뒤 `[System Ready...]` 배너
  ③ 그 뒤 `"0.0.0.0"` 또는 `IP 를 잃었다`
  → 셋이 순서대로면 `사슬확인`. 하나라도 없으면 **무엇이 없는지 그대로 찍는다.**

사용: python3 monitor/chain_verify.py [로그] [사건시각들...]
"""
from __future__ import annotations

import sys

LOG = "monitor/serial-win4.log"
EVENTS = ["07:54:40", "13:19:54", "13:20:28", "13:43:44", "13:44:03", "14:45:49", "14:47:15"]
PRE, POST = 20, 20


def hms(t: str) -> int:
    h, m, s = (int(x) for x in t.split(":"))
    return h * 3600 + m * 60 + s


def main() -> int:
    log = sys.argv[1] if len(sys.argv) > 1 else LOG
    evs = sys.argv[2:] or EVENTS
    rows = []
    scanned = 0
    for line in open(log, encoding="utf-8", errors="replace"):
        scanned += 1
        if len(line) < 9 or line[2] != ":" or line[5] != ":":
            continue
        try:
            t = hms(line[:8])
        except ValueError:
            continue
        tok = None
        # 🔴 표지를 따옴표까지 포함해 맞춘다. `SEND OK` 라는 글자는 **산문 안에도** 나온다:
        #    `[TX-WAIT] 앞 전송의 SEND OK 를 아직 못 봤다` · `[TX-WAIT] ★ SEND OK 상한 초과`
        #    맨문자열로 맞추면 **"SEND OK 가 왔다"로 읽혀** ① 조건이 전 사건에서 거짓이 된다
        #    (1차 판본이 그래서 `0/7` 을 냈다 — 원장 7.43·7.100 과 같은 형태).
        if '"AT+CIPSEND' in line:
            tok = "CIPSEND"
        elif '"SEND OK"' in line:
            tok = "SENDOK"
        elif "전송 실패 1/3" in line:
            tok = "실패1/3"
        elif "전송 3회 연속 실패" in line:
            tok = "3진아웃"
        elif "System Ready" in line:
            tok = "배너"
        elif '"0.0.0.0"' in line or "IP 를 잃었다" in line:
            tok = "0.0.0.0"
        elif "online (CONNECT)" in line or "online (first)" in line:
            tok = "복귀"
        if tok:
            rows.append((t, tok))

    print("# 사슬 확인 — 훑은 줄 %d · 사건 %d건" % (scanned, len(evs)))
    print("# 찾은 문자열: AT+CIPSEND · SEND OK · 전송 실패 1/3 · System Ready · \"0.0.0.0\" · online")
    print("# 판정: ①CIPSEND→SENDOK 없음  ②그 뒤 배너  ③그 뒤 0.0.0.0  — **순서까지** 본다")
    print()
    ok = 0
    for e in evs:
        te = hms(e)
        seq = [(t, k) for t, k in rows if te - PRE <= t <= te + POST]
        # ① 실패1/3 직전의 CIPSEND
        fail = next((t for t, k in seq if k == "실패1/3"), None)
        c1 = None
        if fail is not None:
            cs = [t for t, k in seq if k == "CIPSEND" and t <= fail]
            if cs:
                # 그 CIPSEND 뒤 fail 전에 SENDOK 이 없어야 한다
                last = cs[-1]
                so = [t for t, k in seq if k == "SENDOK" and last <= t <= fail]
                c1 = last if not so else None
        c2 = next((t for t, k in seq if k == "배너" and (c1 is None or t >= c1)), None)
        c3 = next((t for t, k in seq if k == "0.0.0.0" and (c2 is None or t >= c2)), None)
        verdict = ("✅사슬확인" if (c1 is not None and c2 is not None and c3 is not None)
                   else "⚠부분(" + ",".join(n for n, v in
                                            (("①", c1), ("②배너", c2), ("③0.0.0.0", c3)) if v is None) + " 없음)")
        if verdict.startswith("✅"):
            ok += 1
        trail = " ".join("%s%s" % (k, "" if i == 0 else "") for i, (t, k) in enumerate(seq)
                         if k in ("실패1/3", "배너", "0.0.0.0", "3진아웃", "복귀"))
        print("%s  %-28s  ①%s ②%s ③%s   | %s"
              % (e, verdict,
                 "%02d:%02d:%02d" % (c1 // 3600, c1 % 3600 // 60, c1 % 60) if c1 else "—",
                 "%02d:%02d:%02d" % (c2 // 3600, c2 % 3600 // 60, c2 % 60) if c2 else "—",
                 "%02d:%02d:%02d" % (c3 // 3600, c3 % 3600 // 60, c3 % 60) if c3 else "—",
                 trail))
    print()
    print("## 사슬이 순서까지 확인된 사건: **%d / %d**" % (ok, len(evs)))
    print("⚠ 나머지는 '아니다'가 아니라 **'이 셋으로는 확인되지 않았다'** 다.")
    print("  배너 없이 부트ROM 만 남는 리셋이 있고(원장 7.53), UART 가 깨지면 `0.0.0.0` 을 놓친다(7.52).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
