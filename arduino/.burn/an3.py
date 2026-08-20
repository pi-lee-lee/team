import re
from collections import Counter
d = open('/Users/idong-u/learn/arduino/.burn/boot-2220.txt','rb').read().decode('utf-8','replace')
print('줄 끝 문자 확인 : \\r 개수 =', d.count('\r'), '· \\n 개수 =', d.count('\n'))
lines = [l.strip() for l in d.replace('\r','\n').split('\n')]
vs = [l for l in lines if l.startswith('V,')]
print('V 로 시작하는 줄 : **%d건**' % len(vs))
par = []
for l in vs:
    p = l.split(',')
    if len(p) >= 4: par.append((p[1], p[2]))
print('파싱된 것 %d건' % len(par))
a1 = [x[0] for x in par]; b1 = [x[1] for x in par]
print('  A1 칸 : 값 **%d** · 빈 칸 %d' % (sum(1 for v in a1 if v), sum(1 for v in a1 if not v)))
print('  B1 칸 : 값 **%d** · 빈 칸 %d  (**%.1f%% 못 쟀다**)' % (
    sum(1 for v in b1 if v), sum(1 for v in b1 if not v), sum(1 for v in b1 if not v)/len(b1)*100))
av = [v for v in a1 if v]
print()
if av:
    print('🔴 A1 이 읽은 값 :', Counter(av).most_common(5))
    print('   🔑 60cm(0x3C) 미만 :', sum(1 for v in av if int(v,16) < 60), '건')
else:
    print('✅ A1 값 **0건** — V 가 나간 전 구간에서 한 번도 안 읽었다')
t = sum(1 for i in range(1,len(a1)) if bool(a1[i]) != bool(a1[i-1]))
print('   A1 못쟀다↔값 전이 : **%d회**' % t)
print()
print('=== B1 못쟀다 연속 길이 (중복 없는 슬롯 단위)')
runs, cur = [], 0
for v in b1:
    if not v: cur += 1
    else:
        if cur: runs.append(cur)
        cur = 0
if cur: runs.append(cur)
c = Counter(runs)
for ln in sorted(c): print('  연속 %2d슬롯 × %d번' % (ln, c[ln]))
