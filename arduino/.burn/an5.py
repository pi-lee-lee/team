import re
from collections import Counter
d = open('/Users/idong-u/learn/arduino/.burn/boot-2303.txt','rb').read().decode('utf-8','replace')
lines = [l.strip() for l in d.replace('\r','\n').split('\n')]
occ = []
for l in lines:
    if l.startswith('[TX] S,') or l.startswith('S,'):
        p = l.replace('[TX] ','').split(',')
        if len(p) >= 3: occ.append(p[2])
def bit(v, m):
    try: return 1 if (int(v,16) & m) else 0
    except: return None
for name, m in [('A1', 0x8), ('B1', 0x4)]:
    seq = [bit(v, m) for v in occ if bit(v, m) is not None]
    t = sum(1 for i in range(1,len(seq)) if seq[i] != seq[i-1])
    print('%s : 1인 프레임 %d/%d · **전이 %d회**' % (name, sum(seq), len(seq), t))
print()
# V 의 B1 칸 빈칸 연속 길이 — monitor 지표와 같은 것
vs = [l for l in lines if l.startswith('V,')]
b1 = [l.split(',')[2] if len(l.split(','))>3 else '' for l in vs]
print('V %d건 · B1 칸 빈칸 %d (%.1f%%)' % (len(b1), sum(1 for v in b1 if not v),
      sum(1 for v in b1 if not v)/max(1,len(b1))*100))
runs, cur = [], 0
for v in b1:
    if not v: cur += 1
    else:
        if cur: runs.append(cur); 
        cur = 0
if cur: runs.append(cur)
print('  빈칸 연속 길이 분포 :', dict(Counter(runs)))
