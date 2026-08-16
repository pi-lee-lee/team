#!/usr/bin/env python3
"""A/B 구간을 **얼마나 쌓아야** 차이를 잡아낼 수 있나 — 관측 전에 계산한다.

왜 필요한가: 표본을 얼마나 모을지 정하지 않고 시작하면
  · 짧으면 "차이 없음"이 **차이가 없어서인지 표본이 없어서인지** 못 가른다
  · 길면 이미 답이 나온 실험을 계속 돌린다
오늘 이 프로젝트에서 `0` 이 "건강"인지 "한 번도 안 돌았다"인지 못 갈라 생긴 사고가
여러 번이었다. 그 사고의 사전 대응이 이 계산이다.

모형: 각 팔의 사건 수 ~ 포아송(λ·T).
검정: 총계 N 을 조건으로 한 **이항 단측검정**(H0: 두 팔의 비율이 같다).
      포아송 두 표본의 정확검정이고 근사에 기대지 않는다.

사용: python3 monitor/power_plan.py [반복수]
"""
from __future__ import annotations

import math
import random
import sys
from math import comb

N_TRIAL = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
ALPHA = 0.05
TARGET = 0.80
HOURS = (2, 4, 6, 8, 12, 16, 24, 36)
random.seed(20260816)


def pois(lam: float) -> int:
    if lam > 30:  # 큰 λ 는 정규근사로 (속도)
        return max(0, int(random.gauss(lam, math.sqrt(lam)) + 0.5))
    L, k, p = math.exp(-lam), 0, 1.0
    while True:
        p *= random.random()
        if p <= L:
            return k
        k += 1


def power(lam_a: float, ratio: float, T: float) -> float:
    lam_b = lam_a / ratio
    hit = 0
    for _ in range(N_TRIAL):
        a, b = pois(lam_a * T), pois(lam_b * T)
        n = a + b
        if n == 0:
            continue          # 양쪽 다 0 이면 아무 말도 못 한다
        pv = sum(comb(n, i) for i in range(b + 1)) / (2 ** n)
        if pv < ALPHA:
            hit += 1
    return hit / N_TRIAL


def main() -> int:
    print(f"# 양팔 동일 길이 · 단측 이항검정 α={ALPHA} · 반복 {N_TRIAL:,}회")
    print(f"# 표의 값은 검정력(차이가 실제로 있을 때 잡아낼 확률). 목표 {TARGET:.0%}")
    print()
    head = "  λA(/h)  배율 " + "".join(f"{h:6d}h" for h in HOURS)
    print(head)
    print("  " + "-" * (len(head) - 2))
    # 3.02 = **새 기준선 실측**(2026-08-16 16:59:54~18:33, 1.56h, 재부팅없는 재연결 4건).
    #        출처: monitor/out-newbase-link.txt · 원본 ~/parking-logs/parking-server.log
    #        ⚠ 표본 1.56h 라 λ 자체의 오차가 크다(4건 기준 95% 구간 대략 0.8~7.7/h).
    #           이 행은 "대략 이 정도면 된다"의 눈금이지 확정 계획이 아니다.
    for lam_a in (3.02, 2.59, 1.5, 1.0, 0.5):
        for ratio in (3.0, 5.0):
            vals = [power(lam_a, ratio, t) for t in HOURS]
            row = "".join(f"{v:7.2f}" for v in vals)
            print(f"  {lam_a:6.2f} {ratio:3.0f}x " + row)
    print()
    print("  읽는 법: 값이 0.80 을 처음 넘는 칸이 '이 정도면 충분하다'는 길이다.")
    print("  ⚠ λA 는 **지금 A 구간에서 실제로 측정해야 한다.** 옛 구간 2.59/h 는")
    print("     캐패시터가 다른 시절 값이라 그대로 쓰면 계획이 틀어진다.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
