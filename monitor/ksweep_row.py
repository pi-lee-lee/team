#!/usr/bin/env python3
"""k-스윕 표 한 줄 — 구간을 주면 그 k 의 행을 낸다.

사용: python3 monitor/ksweep_row.py <k> <시작 HH:MM:SS> <끝 HH:MM:SS|now> [로그]

⚠ 이 도구가 **답하지 않는** 것:
  · 지연(latency) — **arduino 의 정의를 받고 나서** 넣는다. 지금은 칸만 비워 둔다
    🔴 벽시계로 재지 마라: 로그 초 해상도 1초 · 슬롯 1.2초 → 오늘 이미 데였다
  · ssovf · ramLow · flash — **시리얼 전용.** 내 계측기에 없다 → `미계측` 으로 남긴다
"""
import io,re,sys
from collections import Counter
k=sys.argv[1]; LO=sys.argv[2]; HI=sys.argv[3] if len(sys.argv)>3 else "now"
LOG=sys.argv[4] if len(sys.argv)>4 else "/tmp/srv_prod.out"
if HI=="now": HI="99:99:99"
S=V=0; blank=Counter(); tr=Counter(); last=None; sess=0; off=None
for ln in io.open(LOG,encoding='utf-8',errors='replace'):
    ts=ln[11:19]
    if len(ln)<20 or not ln[0].isdigit() or not (LO<=ts<HI): continue
    if '←ARD S,' in ln: S+=1
    if '←ARD V,' in ln:
        V+=1
        f=ln.split('←ARD V,')[1].split(' ')[0].rstrip(')').split(',')
        for i,v in enumerate(f[:-1]):
            if not v: blank[i]+=1
    m=re.search(r'모듈 (\w+) 점유 →',ln)
    if m: tr[m.group(1)]+=1
    if '+ARD 주차' in ln: sess+=1
    if '⏱ 소크' in ln: last=ln
def g(p,d='?'):
    m=re.search(p,last or ''); return m.group(1) if m else d
print(f"# k={k}  구간 {LO}~{HI if HI!='99:99:99' else 'now'}")
print(f"  S {S} · V {V}" + (f" · V/S {V/S*100:.1f}%" if S else ""))
print(f"  빈칸률 : " + " · ".join(f"칸{i} {n}/{V}={n/V*100:.1f}%" for i,n in sorted(blank.items())) if V else "  빈칸률 : (V 없음)")
print(f"  전이   : {dict(tr)}")
print(f"  smiss  : 🔴 미계측(시리얼 전용)   ssovf : 🔴 미계측   ramLow/flash : 🔴 미계측")
print(f"  묶음미룸/창놓침/배치 : {g(r'묶음미룸 (\d+)')} / {g(r'창놓침 (\d+)')} / {g(r'배치 (\d+)')}")
print(f"  왕복최대 : {g(r'왕복 최대 (\d+)ms')}ms · 최대공백 {g(r'최대공백 ([0-9.]+)초')}초")
print(f"  🔑 링크(귀속 방지용) : 세션 이 구간 {sess}회 · 오프라인누계 {g(r'오프라인 (\d+)회')} · 링크없음 {g(r'링크없음 ([0-9:]+)')}")
print(f"  ⚠ R(진동 지표) : **쓰지 않는다** — 핀 미배선이라 빈칸률이 구조적으로 100% 근처다")
