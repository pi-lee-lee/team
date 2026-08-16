#!/usr/bin/env python3
"""상행 S 프레임의 **도착 간격**을 재고, 큰 공백이 주입 시각에 몰리는지 본다.

왜 있나 (socket 요청 2026-08-17 00:3x)
  주입기 초판은 WS 소켓을 붙들고 안 읽어서 서버 송신 버퍼를 채웠고,
  `send_raw` 가 **블로킹 1초**를 기다렸다 끊었다. 서버가 **단일 스레드 select 루프**라
  그 1초 동안 9991 아두이노 처리도 같이 멈춘다.
  → **주입이 5분마다 1초짜리 프레임 공백을 만들어 넣을 뻔했다.**
  그건 최대공백·3500ms 오프라인 판정에 그대로 먹이는 값이고,
  **TX 증가와 달리 순수한 계측 결함이라 인과 분석으로도 못 걸러낸다.**

  socket 이 고쳤고 시험 인스턴스에서 확인했다. **그러나 실물 부하는 다를 수 있다.**
  그래서 "보이면 알려 달라"를 **약속이 아니라 계측**으로 바꾼 것이 이 도구다.

🔴 해상도 — 먼저 읽어라
  서버 로그 타임스탬프는 **초 단위**다. 평시 상행은 **1Hz** 이므로 정상 간격이 `1s` 다.
  **1초짜리 정지는 간격 `2s` 로 보인다.** 서브초는 원리적으로 못 잰다.
  → 그래서 이 도구는 **`>=2s` 를 후보로 보고**, 평시에도 나오는 `2s` 지터와
     **구별하지 않는다.** 구별은 **국면1(무주입) 기저율과의 대조**로 한다.
     즉 이 도구 하나로는 판정할 수 없다. **두 국면을 각각 돌려서 비교하라.**

⚠ `0` 주의: 큰 공백 0건이 "안 난다"인지 "구간이 짧아 아직 안 났다"인지 갈라 적어라(원장 1.1).

사용:
  python3 monitor/frame_gaps.py <서버로그> <시작ISO> <끝ISO> [주입시각파일] [반경초]
  주입시각파일 = ISO 한 줄에 하나(`#`·빈 줄 무시). 없으면 공백 분포만 낸다.
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter
from datetime import datetime
from math import comb

TS = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s")
SFRAME = "←ARD S,"
WSUP = "+WS 업그레이드 완료"
BIG = 2  # 초 — 후보 문턱(1Hz·1초해상도에서 '정상 1s' 를 넘는 첫 칸)


def load(path: str) -> list:
    out = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if ln and not ln.startswith("#"):
                out.append(datetime.fromisoformat(ln))
    return out


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    log, since, until = sys.argv[1], datetime.fromisoformat(sys.argv[2]), datetime.fromisoformat(sys.argv[3])
    injf = sys.argv[4] if len(sys.argv) > 4 else None
    radius = int(sys.argv[5]) if len(sys.argv) > 5 else 5

    frames, ws = [], []
    with open(log, "rb") as f:
        for raw in f:
            s = raw.decode("utf-8", "replace")
            m = TS.match(s)
            if not m:
                continue
            t = datetime.fromisoformat(m.group(1))
            if not (since <= t <= until):
                continue
            if SFRAME in s:
                frames.append(t)
            elif WSUP in s:
                ws.append(t)

    print(f"# 원본: {os.path.abspath(log)}")
    print(f"# 구간 {since} ~ {until}  ({(until-since).total_seconds()/3600:.2f}h)")
    print("# ⚠ 초 단위 해상도 · 평시 1Hz → 정상 간격 1s · **1초 정지는 2s 로 보인다**")
    print(f"\n## 상행 S 프레임 {len(frames):,}건")
    if len(frames) < 2:
        print("   ⚠ 2건 미만이라 간격을 못 낸다. **'공백 없음'이 아니라 '못 셈'이다**(원장 1.1).")
        return 0

    gaps = [(frames[i + 1], int((frames[i + 1] - frames[i]).total_seconds()))
            for i in range(len(frames) - 1)]
    h = Counter(g for _, g in gaps)
    print("\n## 도착 간격 분포 (초)")
    for k in sorted(h):
        mark = "  ← 정상" if k <= 1 else ""
        print(f"   {k:>3}s  {h[k]:>6}건{mark}")
    big = [(t, g) for t, g in gaps if g >= BIG]
    print(f"\n## `>={BIG}s` 공백 {len(big)}건 / 전체 간격 {len(gaps)}건"
          f" = {100*len(big)/len(gaps):.2f}%")
    if not big:
        print(f"   ⚠ 0 건이다. **'안 난다'가 아니라 '이 {(until-since).total_seconds()/3600:.2f}h 에서 안 났다'** 이다.")

    print(f"\n## `+WS 업그레이드 완료` {len(ws)}건")
    if ws:
        for i, t in enumerate(ws):
            d = "" if i == 0 else f"  (앞과 {int((t-ws[i-1]).total_seconds())}s)"
            print(f"   {t:%H:%M:%S}{d}")
        print("   ⓘ 주입기는 주입할 때마다 WS 를 새로 열고 곧바로 닫는다(socket, 판본 8d09cee).")
        print("   🔴 **간격이 300s 로 규칙적이지 않은 줄이 있으면 그것은 주입기가 아니다**")
        print("      (web 이 붙었거나 다른 것이다). 그런 줄이 있으면 창에 남의 부하가 들어온 것이다.")

    if not injf:
        print("\n(주입 시각 파일이 없어 근접 분석은 건너뛴다 — 국면1 기저율 측정에는 이대로 충분하다)")
        return 0

    inj = [t for t in load(injf) if since <= t <= until]
    print(f"\n## 주입 {len(inj)}건 · 반경 ±{radius}s 근접 분석")
    if not inj:
        print("   구간 안에 주입이 없다.")
        return 0

    def near(t):
        return any(abs((t - i).total_seconds()) <= radius for i in inj)

    nb = [(t, g) for t, g in big if near(t)]
    span = (until - since).total_seconds()
    # 창 안으로 클립한 ±radius 합집합 (겹침 제거 — 안 하면 p 가 부풀어 '우연'이 과대평가된다)
    segs = sorted((max(since.timestamp(), i.timestamp() - radius),
                   min(until.timestamp(), i.timestamp() + radius)) for i in inj)
    uni = []
    for a, b in segs:
        if uni and a <= uni[-1][1]:
            uni[-1][1] = max(uni[-1][1], b)
        else:
            uni.append([a, b])
    p = sum(b - a for a, b in uni) / span
    k, n = len(nb), len(big)
    pk = sum(comb(n, i) * p**i * (1 - p) ** (n - i) for i in range(k, n + 1)) if n else 1.0

    print(f"   근접 구간이 창의 {100*p:.2f}% 를 덮는다 (p={p:.4f})")
    print(f"   `>={BIG}s` 공백 {n}건 중 주입 ±{radius}s 안: **{k}건** · 우연 기대 {n*p:.2f}건")
    if n:
        print(f"   P(X >= {k} | n={n}, p={p:.4f}) = {pk:.4f}")
    for t, g in nb:
        print(f"     {t:%H:%M:%S}  간격 {g}s  ← 주입 근접")
    print("\n## 읽는 법")
    print("   · 🔴 **이 도구 하나로 판정하지 마라.** `2s` 는 평시 지터로도 나온다.")
    print("     **국면1(무주입) 기저율과 대조**해야 뜻이 생긴다. 두 국면을 각각 돌려라.")
    print("   · 근접이 있어도 **우연 기대치를 같이 적어라**(원장 1.13).")
    print("   · 공백이 주입에 몰리면 **socket 에게 알려라 — 계측 결함이지 장치 고장이 아니다.**")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
