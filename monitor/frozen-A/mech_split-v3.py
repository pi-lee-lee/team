#!/usr/bin/env python3
"""링크 끊김을 **기전별로 갈라 센다** — `busy` 자해인가, ESP 모듈 리셋인가.

왜 필요한가
  2026-08-16 관측에서 "링크 끊김"이 **한 가지가 아님**이 드러났다
  (monitor/FINDING-2026-08-16-1845-two-mechanisms.md). 두 기전은 고치는 방법이 다르다:

    기전 A(자해)   ESP 는 살아서 `busy s...` 로 대답하는데 펌웨어가 3번 세고 끊는다
                   → 카운터 규칙을 고치면 사라진다 (REQ-0116)
    기전 B(리셋)   ESP 모듈 자체가 리셋된다. 부트 배너 + `CIFSR → 0.0.0.0`(IP 소실)
                   → 카운터를 고쳐도 **안 사라진다**. 원인이 다른 곳에 있다

  두 기전을 합쳐 "끊김 N회/h"로 보고하면 **굽는 판단이 틀린다.** 그래서 갈라 센다.

🔴 판별자 선택의 근거 (전수 검증, 같은 문서 §2.1)

  | 판별자                | 리셋 사건 일치 | 정상 부팅에서 |
  |---|---|---|
  | 부트 배너 `System Ready` | 3/3          | **뜬다** ← 단독 사용시 오탐 |
  | **`CIFSR → 0.0.0.0`**   | **3/3**      | **안 뜬다**                |

  → **1차 판별자는 `0.0.0.0` 이다.** 배너는 보조로만 쓰고 단독으로 판정하지 않는다.
     (우리 자신의 16:59:57 DTR 리셋이 반례였다 — 배너는 떴고 `0.0.0.0` 은 안 떴다.)

⚠ `busy` 가 났다고 반드시 끊기는 것이 아니다(18:03 의 2건은 3진아웃에 도달하지 않았다).
   그래서 **`busy` 자체가 아니라 `전송 3회 연속 실패` 를 사건의 단위**로 잡는다.

🔴 자정 넘김 — 이 파일이 존재하는 가장 큰 이유 중 하나
   시리얼 로그의 각 줄에는 **날짜가 없다**(`HH:MM:SS` 뿐). 관측 창 A 는 01:53 까지라
   **자정을 넘는다.** 날짜를 안 붙이면 00:00 부터 모든 사건이 하루 전으로 읽혀
   창 밖으로 밀려나고 **조용히 0 이 된다**(LEDGER 2.2 — 하루에 세 명이 이걸로 틀렸다).
   → 시:분:초가 뒤로 감기면 날짜를 하루 올린다. 기준일은 tap 헤더가 스스로 말한 값을 쓴다.

사용:
  python3 monitor/mech_split.py [로그] [시작ISO] [끝ISO]
기본값은 새 기준선 시리얼 로그와 관측 창 A 다.
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
# tap 헤더가 기준일을 스스로 말한다 — 사람의 기억이나 파일명에 기대지 않는다(LEDGER 1.5).
HDR_DATE = re.compile(rb"date=(\d{4})-(\d{2})-(\d{2})")

EV_CUT = "전송 3회 연속 실패".encode()        # 사건의 단위
MARK_BUSY = b"busy"
MARK_BANNER = b"System Ready"
MARK_NOIP = b"0.0.0.0"
# 🔑 펌웨어 **자신의 결론** 줄. 위 셋과 성격이 다르다 —
#    ESP 의 원시 응답을 우리가 해석한 것이 아니라, 펌웨어가 3회 확인 끝에 내린 판정이다.
#    그래서 UART 가 가비지를 뿜는 순간에도 이 줄은 온전하다.
#    실측 반례가 이걸 찾게 했다: 18:48:12 리셋은 `AT+CIFSR` **응답 자체가 손상**돼
#    `0.0.0.0` 문자열이 아예 안 찍혔다. `0.0.0.0` 은 정밀하지만 **완전하지 않다.**
MARK_NOIP_FW = "CIFSR 3회에도 IP 가 없다".encode()
MARK_REJOIN = b"CWJAP OK"                     # Wi-Fi 재결합
MARK_IPOK = "IP 확보".encode()                # 🔑 반증 표지 — 그 순간 IP 가 있었다

# 🔴🔴 2026-08-16 19:30 정정 — `MARK_NOIP_FW` 를 **분류에서 뺐다**(표시는 유지).
#
#   왜: 19:25:48 사건 원문에서
#         19:25:50  ★ IP 확보: 192.168.35.79     ← IP 가 있었다
#         19:25:54  CIFSR 3회에도 IP 가 없다      ← 4초 뒤 이 줄
#       배너도 없다. 즉 이 줄은 **복구 사다리가 도는 소리**이지 모듈 리셋의 증거가 아니다.
#
#   이 표지는 18:48 누락(가비지로 `0.0.0.0` 이 안 찍힘)을 메우려고 2차 정정에서 넣은 것이다.
#   **누락을 고치면서 오탐을 들여왔다.** 18:48 은 배너(손상됐지만 존재)로 여전히 잡히므로
#   이 표지 없이도 누락되지 않는다.
#
#   ⚠ 교훈: 판별자를 넓힐 때는 **넓힌 쪽에서 무엇이 새로 걸리는지**를 반드시 확인하라.
#      좁힐 때(누락)만 보고 넓힐 때(오탐)를 안 봤다.

# 사건 시각 기준 탐색 창.
#  뒤(-)로 넓게 보는 이유: 실패 1/3 은 3/3 보다 2~4초 앞서고 `busy`·배너가 그 사이에 온다.
#  앞(+)으로 보는 이유: `CIFSR → 0.0.0.0` 은 오프라인 전환 **뒤에** 나온다(실측 +1~2초).
BACK_S = 12
FWD_S = 12


def parse(path: str):
    """(시각, 원문바이트) 를 순서대로 낸다. 자정 넘김을 처리한다.

    바이트로 읽는다 — ESP 부트 ROM 쓰레기 바이트가 섞여 있어 텍스트로 디코드하면
    줄이 통째로 날아가거나 예외가 난다(LEDGER 2.1 의 `grep -a` 와 같은 이유).
    """
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
            hh, mm, ss = int(m.group(1)), int(m.group(2)), int(m.group(3))
            cur = hh * 3600 + mm * 60 + ss
            if prev is not None and cur < prev - 3600:
                # 한 시간 이상 뒤로 감겼다 = 자정을 넘었다.
                # 3600 여유를 둔 이유: 초 단위 역행(버퍼 순서 뒤바뀜)을 날짜 롤오버로 오해하면
                # 그 뒤 전 구간이 하루씩 밀린다. 되돌릴 수 없는 오류라 보수적으로 잡는다.
                day += 1
            prev = cur
            if base is None:
                continue
            yield base + timedelta(days=day, seconds=cur), raw


def main() -> int:
    p = os.path.abspath(LOG)
    print(f"# 원본 로그: {p}")           # LEDGER 1.5 — 출력은 스스로 출처를 밝힌다
    print(f"# 판별자: 1차 `CIFSR → 0.0.0.0` · 보조 부트 배너(단독 판정 금지)")

    rows = list(parse(p))
    if not rows:
        print("⚠ 파싱된 줄이 0 이다. 로그 형식이나 헤더(date=)를 확인하라.")
        return 1

    first, last = rows[0][0], rows[-1][0]
    print(f"# 자료 범위 {first} ~ {last}")
    lo = max(SINCE, first)
    hi = min(UNTIL, last)                 # 🔴 분모에 미래를 넣지 않는다 (LEDGER 1.2)
    if hi <= lo:
        print(f"⚠ 요청 구간에 자료가 없다 ({SINCE} ~ {UNTIL}).")
        return 1
    hours = (hi - lo).total_seconds() / 3600.0
    print(f"# 구간 {lo} ~ {hi}  ({hours:.2f}h)")
    if UNTIL > last:
        print(f"  ⓘ 분모 보정: 요청 끝 {UNTIL} → 자료 끝 {last}")

    events = [(t, r) for t, r in rows if EV_CUT in r and lo <= t <= hi]

    # 배너 전수 — 사건과 짝이 안 맞는 배너는 '정상 부팅'이다. 반드시 따로 보여 준다.
    banners = [t for t, r in rows if MARK_BANNER in r and lo <= t <= hi]

    out = []
    for t, _ in events:
        a, b = t - timedelta(seconds=BACK_S), t + timedelta(seconds=FWD_S)
        near = [r for tt, r in rows if a <= tt <= b]
        n_busy = sum(1 for r in near if MARK_BUSY in r)
        has_banner = any(MARK_BANNER in r for r in near)
        # 🔑 표지를 **접지 말고 따로 낸다**(원장 1.10 을 한 단계 아래에 적용).
        #    앞 판은 이 둘을 `IP소실` 한 칸으로 접었고, 그래서 오탐이 드러났을 때
        #    **도구를 고쳐야만** 재분류할 수 있었다. 따로 내면 다음엔 출력만 보고 다시 셀 수 있다.
        has_noip_raw = any(MARK_NOIP in r for r in near)    # ESP 의 실제 응답 `0.0.0.0`
        has_noip_fw = any(MARK_NOIP_FW in r for r in near)  # 펌웨어 결론 — ⚠ 분류에 쓰지 않는다
        has_rejoin = any(MARK_REJOIN in r for r in near)
        has_ipok = any(MARK_IPOK in r for r in near)        # 반증 — 그 창 안에 IP 가 있었다

        # ⚠ 라벨을 강제하지 않는다. 2026-08-16 18:55 에 이진 분류가 실제로 틀렸다 —
        #   17:41 사건은 `busy`×12 와 IP 소실 표지를 **둘 다** 가졌는데
        #   앞선 판(자해/리셋 택일)은 그것을 `자해` 한 칸에 밀어 넣고 IP 소실을 지웠다.
        #   증거가 겹치면 겹친 대로 적는다. 라벨은 표지의 **요약**이지 그 반대가 아니다.
        # ⚠ `has_noip_fw` 는 **의도적으로 빠져 있다** — 위 정정 주석 참조.
        reset_like = has_banner or has_noip_raw
        if reset_like and n_busy:
            mech = "AB.혼합"     # 두 기전의 표지가 같이 있다. 한쪽으로 세면 안 된다
        elif reset_like:
            mech = "B.리셋계열"
        elif n_busy:
            mech = "A.자해"
        else:
            mech = "?.미상"

        ev = []
        if n_busy:
            ev.append(f"busy×{n_busy}")
        if has_banner:
            ev.append("배너")
        if has_noip_raw:
            ev.append("0.0.0.0")
        if has_noip_fw:
            ev.append("CIFSR3소진*")      # * = 분류에 쓰지 않는 참고 표지
        if has_rejoin:
            ev.append("재결합")
        if has_ipok:
            ev.append("⚠IP확보(리셋 반증)")
        why = " + ".join(ev) if ev else "표지 없음"
        out.append((t, mech, why, n_busy))

    print(f"\n## 사건(`전송 3회 연속 실패`) {len(events)}건")
    if not events:
        print("  ⚠ 0 건이다. **이것이 '건강'인지 '자료가 없다'인지 갈라라**(LEDGER 1.1):")
        print(f"     이 구간의 파싱된 줄 {sum(1 for t, _ in rows if lo <= t <= hi):,}개.")
        print("     줄이 있는데 사건이 0 이면 건강, 줄도 없으면 관측이 안 된 것이다.")
    for t, mech, why, _ in out:
        print(f"  {t:%m-%d %H:%M:%S}  {mech:<12} ({why})")

    tally: dict[str, int] = {}
    for _, mech, _, _ in out:
        tally[mech] = tally.get(mech, 0) + 1

    print("\n## 갈래별 집계")
    for k in ("A.자해", "B.리셋계열", "AB.혼합", "?.미상"):
        n = tally.get(k, 0)
        rate = f"{n / hours:.2f}/h" if hours > 0 else "-"
        print(f"  {k:<12} {n:3d}   {rate}")

    print(f"\n## 부트 배너 전수 {len(banners)}건")
    ev_times = [t for t, _ in events]
    for bt in banners:
        paired = any(abs((bt - et).total_seconds()) <= max(BACK_S, FWD_S) for et in ev_times)
        tag = "사건과 짝지음" if paired else "🔑 짝 없음 = 정상 부팅(리셋 아님)"
        print(f"  {bt:%m-%d %H:%M:%S}  {tag}")

    print("\n## 읽는 법")
    print("  · `A.자해` 는 REQ-0116(busy 를 실패로 세지 않기)으로 사라질 것으로 기대되는 사건이다.")
    print("  · `B.리셋계열` 은 카운터를 고쳐도 **안 사라진다.** 원인이 펌웨어 밖에 있다.")
    print("  · 🔴 `AB.혼합` 을 어느 한쪽으로 밀어 넣지 마라. 표지가 겹친 것이고,")
    print("       겹친 이유(리셋이 busy 를 유발했나, 그 반대인가)는 아직 안 밝혀졌다.")
    print("  · IP소실 표지는 둘이다: `0.0.0.0`(정밀·불완전) + 펌웨어의 `CIFSR 3회에도 IP 없다`.")
    print("       UART 가 가비지를 뿜으면 앞의 것은 놓친다 — 그래서 둘을 OR 로 본다.")
    print("  · 표본이 작으면 비율로 인용하지 마라. 건수와 구간 길이를 같이 적어라.")
    print("  · pre-A(16:59:54~17:53:07) 와 A 를 합산하지 마라 — 서버 인스턴스 수가 다르다.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
