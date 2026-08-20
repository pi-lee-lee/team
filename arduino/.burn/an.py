import re
from collections import Counter
d = open('/Users/idong-u/learn/arduino/.burn/boot-2200.txt','rb').read().decode('utf-8','replace')
vs = re.findall(r'V,([0-9A-F]*),([0-9A-F]*),[0-9A-F]+', d)
b1 = [x[1] for x in vs]; a1 = [x[0] for x in vs]
tot = len(b1)
mb = sum(1 for v in b1 if v == ''); ma = sum(1 for v in a1 if v == '')
print('B1 칸 총 %d회 · 빈 칸 **%d회 = %.1f%%**' % (tot, mb, mb/tot*100))
print('A1 칸 총 %d회 · 빈 칸 %d회 = %.1f%%' % (tot, ma, ma/tot*100))
print()
print('=== 🔴 B1 못쟀다의 **연속 길이** — 이것이 N 을 정한다')
runs, cur = [], 0
for v in b1:
    if v == '': cur += 1
    else:
        if cur: runs.append(cur)
        cur = 0
if cur: runs.append(cur)
c = Counter(runs)
for ln in sorted(c): print('  연속 %2d회 × %d번' % (ln, c[ln]))
print('  → 최대 연속 **%d회** · 구간 %d개' % (max(runs) if runs else 0, len(runs)))
