#!/usr/bin/env python3
"""ESP `busy` 응답이 **연속 몇 번까지 이어지는가** — 안전 상한 N 의 근거를 만든다.

왜 필요한가 (arduino-engineer 요청):
  `busy` 를 실패로 세지 않게 고치면 회복 경로 하나가 같이 사라진다.
  전에는 busy 가 계속 와도 3회 실패 → goOffline() 으로 빠져나갔는데,
  이제는 카운터가 영영 3에 도달하지 않아 **조용한 영구 정지**가 가능해진다.
  그래서 busy 에도 상한을 두는데, **그 N 을 추측으로 정하지 않기 위한 실측**이 이 도구다.

  · N 이 너무 짧으면 → busy 를 다시 실패처럼 취급하게 되어 수정이 무효가 된다
  · N 이 너무 길면 → 정지 위험이 남는다
  → **정상 운전에서 관측된 최대 런보다 충분히 크고, 영구 정지보다 확실히 짧은 값**을 잡는다.

세는 방법:
  송신 시도는 `[AT] "AT+CIPSEND=<n>"` 로 시작한다. 그 다음 시도 전까지의 응답을 보고
  busy / error / sendok / 기타로 분류하고, **연속된 busy 의 길이**를 런으로 모은다.

⚠ 지금 칩은 `[TX-BUSY]` 를 안 찍는다(그건 아직 안 구운 새 로그다).
   그래서 **원시 `[AT] "busy…"` 줄로만** 센다.

사용: python3 monitor/busy_runs.py [시리얼로그]
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-newbase.log"

TS = re.compile(r"^(\d\d):(\d\d):(\d\d)\s+(.*)$")
CIPSEND = re.compile(r'\[AT\] "AT\+CIPSEND=(\d+)"')
BUSY = re.compile(r'\[AT\] "busy ([sp])\.\.\."')
SENDOK = re.compile(r'\[AT\] "SEND OK"')
ERRLINE = re.compile(r'\[AT\] "(Error|ERROR)"')


def main() -> int:
    p = os.path.abspath(LOG)
    if any(k in os.path.basename(p).lower() for k in ("fake", "fixture", "sample")):
        print("#" * 68)
        print("# ⚠⚠ 합성 픽스처 — 실물 관측이 아니다. 상수 근거로 쓰지 마라.")
        print("#" * 68)
    print(f"# 원본 로그: {p}")

    lines = []
    with open(LOG, "rb") as f:
        for raw in f.read().decode("utf-8", "replace").splitlines():
            m = TS.match(raw)
            if m:
                lines.append((f"{m[1]}:{m[2]}:{m[3]}", m[4]))
    if not lines:
        print("타임스탬프 줄이 없다 — 로그 형식을 확인하라.", file=sys.stderr)
        return 1

    # 송신 시도 경계로 자른다
    attempts = []          # (시각, 결과)
    cur_start = None
    cur_result = None
    for ts, body in lines:
        if CIPSEND.search(body):
            if cur_start is not None:
                attempts.append((cur_start, cur_result or "unknown"))
            cur_start, cur_result = ts, None
            continue
        if cur_start is None:
            continue
        if cur_result is None:                 # 첫 결론만 취한다
            if BUSY.search(body):
                cur_result = "busy"
            elif SENDOK.search(body):
                cur_result = "sendok"
            elif ERRLINE.search(body):
                cur_result = "error"
    if cur_start is not None:
        attempts.append((cur_start, cur_result or "unknown"))

    n = len(attempts)
    kinds = Counter(r for _, r in attempts)
    span_h = 0.0
    if lines:
        def sec(t):
            h, m, s = (int(x) for x in t.split(":"))
            return h * 3600 + m * 60 + s
        d = sec(lines[-1][0]) - sec(lines[0][0])
        if d < 0:
            d += 86400
        span_h = d / 3600.0

    print(f"# 구간 {lines[0][0]} ~ {lines[-1][0]}  ({span_h:.2f}h)")
    print(f"# 송신 시도 {n:,}건 · " + " · ".join(f"{k} {v}" for k, v in kinds.most_common()))
    print()

    # 연속 busy 런
    runs = []
    cur = 0
    for _, r in attempts:
        if r == "busy":
            cur += 1
        else:
            if cur:
                runs.append(cur)
            cur = 0
    if cur:
        runs.append(cur)

    print("## 연속 busy 런 길이 분포")
    if not runs:
        print("  런 0건 — 이 구간에 busy 가 없었다.")
        print(f"  ⚠ 이 0 은 '안 난다'가 아니라 **'{span_h:.2f}h 동안 안 났다'** 이다.")
        print("     상한 N 을 이 자료로 정하지 마라. 표본이 없다.")
        return 0
    c = Counter(runs)
    for k in sorted(c):
        print(f"  길이 {k:>2} : {c[k]:>4}건  {'█' * min(c[k], 40)}")
    rs = sorted(runs)
    med = rs[len(rs) // 2]
    print()
    print(f"  런 총 {len(runs)}건 · 중앙 {med} · 최대 **{max(rs)}** · 평균 {sum(rs)/len(rs):.2f}")
    print(f"  busy 시도 {sum(rs)}건 / 전체 {n}건 = {100*sum(rs)/n:.1f}%")
    print(f"  런 발생률 {len(runs)/span_h:.1f}회/h" if span_h else "")
    print()
    print("## 상한 N 제안")
    mx = max(rs)
    print(f"  관측 최대 런 = {mx}")
    print(f"  → N 은 최소 {mx + 2} 이상이어야 정상 운전을 실패로 되돌리지 않는다"
          f" (관측 최대 + 여유 2)")
    print(f"  ⚠ 표본 {len(runs)}런 / {span_h:.2f}h 다. 꼬리는 관측 시간에 비례해 길어진다 —")
    print(f"     더 오래 재면 최대값은 커질 수 있다. **이 값을 상한의 하한으로만 써라.**")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
