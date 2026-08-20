import re
from collections import Counter
d = open('/Users/idong-u/learn/arduino/.burn/boot-2200.txt','rb').read().decode('utf-8','replace')
# 🔴 [TX] 줄만 센다 — 에코 중복을 없앤다
tx = [l for l in d.split('\n') if l.startswith('[TX]') or l.startswith('V,')]
vs = re.findall(r'^V,([^,]*),([^,]*),([0-9A-F]+)$', d, re.M)
print('V 프레임(줄 시작 기준) %d건' % len(vs))
a1 = [x[0] for x in vs]; b1 = [x[1] for x in vs]
print('  A1 칸 : 값 있음 **%d** · 빈 칸 %d' % (sum(1 for v in a1 if v), sum(1 for v in a1 if not v)))
print('  B1 칸 : 값 있음 **%d** · 빈 칸 %d' % (sum(1 for v in b1 if v), sum(1 for v in b1 if not v)))
print()
av = [v for v in a1 if v]
if av:
    print('🔴 A1 이 읽은 값 :', Counter(av).most_common())
    print('   10진 :', sorted(set(int(v,16) for v in av)))
    print('   🔑 60cm(0x3C) 미만 :', sum(1 for v in av if int(v,16) < 60), '건')
else:
    print('A1 값 0건')
print()
print('=== A1 의 **못쟀다 ↔ 값 있음 전이** 수')
t = 0
for i in range(1, len(a1)):
    if bool(a1[i]) != bool(a1[i-1]): t += 1
print('  전이 **%d회**' % t)
print()
print('=== [USD] 줄 전수 (A1 표본이 있던 구간)')
for l in d.split('\n'):
    if '[USD]' in l: print('  ' + l.strip())
