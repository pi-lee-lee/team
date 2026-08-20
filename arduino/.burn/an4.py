import re
from collections import Counter
d = open('/Users/idong-u/learn/arduino/.burn/boot-2303.txt','rb').read().decode('utf-8','replace')
lines = [l.strip() for l in d.replace('\r','\n').split('\n')]
ss = [l for l in lines if l.startswith('[TX] S,') or l.startswith('S,')]
occ = []
for l in ss:
    p = l.replace('[TX] ','').split(',')
    if len(p) >= 3: occ.append(p[2])
print('S 프레임 %d건 · occ 값 분포:' % len(occ))
for v, c in Counter(occ).most_common():
    try: iv = int(v, 16)
    except: continue
    who = []
    if iv & 0x8: who.append('A1')
    if iv & 0x4: who.append('B1')
    if iv & 0x2: who.append('LD')
    if iv & 0x1: who.append('L2')
    print('  occ=%-3s ×%3d → %s' % (v, c, '+'.join(who) if who else '없음'))
print()
a1 = sum(c for v,c in Counter(occ).items() if v.isalnum() and (int(v,16) & 0x8))
print('🔴 **A1 비트(0x8)가 든 S 프레임 : %d건**' % a1)
t = 0
prev = None
for v in occ:
    try: cur = (int(v,16) >> 3) & 1
    except: continue
    if prev is not None and cur != prev: t += 1
    prev = cur
print('   A1 비트 전이 : **%d회**' % t)
