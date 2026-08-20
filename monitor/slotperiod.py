#!/usr/bin/env python3
"""k 구간별 슬롯 주기 — **서버 로그로** 재는 독립 측정 (arduino `up/slot` 과 교차용)

🔴 추정량 = **절사 평균**(3초 초과 간격 제외 후 평균). 두 번 갈아탔다:
   ❌ 생평균  : 리셋 21초 공백 하나가 130프레임 평균을 0.17초 밀었다 (1.388)
   ❌ 중앙값  : 로그 해상도 1초 · 슬롯 1.2초 → 간격이 1초/2초뿐이라 **중앙값이 항상 1.000**
              **양자화가 신호를 통째로 죽인다**
   ✅ 절사평균: 1초와 2초의 **비율**이 1.2 를 만든다. 공백만 빼면 그 비율이 남는다
🔑 그리고 내 값과 arduino `up/slot` 의 **차이가 곧 손실**이다 —
   그는 **장치가 센 슬롯**을 세고 나는 **도착한 프레임 간격**을 잰다. 같은 물음, 다른 계층.
⚠ 못 내는 것: ssovf · smiss · ramLow (시리얼 전용)
"""
import io,sys,statistics as st
from datetime import datetime as D, timedelta as T
BOOT=D.strptime(sys.argv[1],"%Y-%m-%d %H:%M:%S") if len(sys.argv)>1 else None
KS=[2,4,6,8,10,11]
S=[]
for ln in io.open("/tmp/srv_prod.out",encoding='utf-8',errors='replace'):
    if '←ARD S,' in ln:
        t=D.strptime(ln[:19],"%Y-%m-%d %H:%M:%S")
        if t>=BOOT: S.append(t)
print(f"# 정박 {BOOT} · S {len(S)}건 · 이상 슬롯 1.200초")
for i,k in enumerate(KS):
    lo=BOOT+T(seconds=180*i); hi=lo+T(seconds=180)
    sub=[t for t in S if lo<=t<hi]
    if len(sub)<5: print(f"  k={k:<3} S {len(sub):>3}  (표본 부족)"); continue
    d=[(sub[j]-sub[j-1]).total_seconds() for j in range(1,len(sub))]
    keep=[x for x in d if x<=3]; big=len(d)-len(keep)
    done=S[-1]>=hi
    tm=st.mean(keep) if keep else 0
    print(f"  k={k:<3} S {len(sub):>3} · **절사평균 {tm:.3f}초** (생 {st.mean(d):.3f}) "
          f"· 제외공백 {big} · 1.200 대비 {(tm/1.2-1)*100:+.1f}%" + ("" if done else "  ⚠ 미완"))
print("⚠ 개별 간격은 1초/2초로 양자화된다. **비율이 1.2 를 만드므로 표본이 커야 한다**(구간당 ~150)")
