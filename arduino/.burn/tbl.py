import re
d = open('/Users/idong-u/learn/arduino/.burn/ks-full.txt','rb').read().decode('utf-8','replace')
lines = [l.strip() for l in d.replace('\r','\n').split('\n')]
def g(l, k):
    m = re.search(k + r'=(\d+)', l); return int(m.group(1)) if m else None
ks = [(g(l,'k'), g(l,'up'), g(l,'slot')) for l in lines if l.startswith('[KSWEEP]')]
cnt = [(g(l,'up'), l) for l in lines if l.startswith('[CNT]')]
ram = [(i, int(re.search(r'최저 여유 (\d+)',l).group(1))) for i,l in enumerate(lines)
       if l.startswith('[RAM]') and re.search(r'최저 여유 (\d+)',l)]
print('| k(활성) | **up/slot (초)** | ssovf | smiss | oow | drop | ramLow |')
print('|---|---|---|---|---|---|---|')
for i in range(1, len(ks)):
    k0,u0,s0 = ks[i-1]; k1,u1,s1 = ks[i]
    per = (u1-u0)/(s1-s0)
    # 구간 끝 시점의 CNT (u0 < up <= u1)
    seg = [l for up,l in cnt if up is not None and u0 < up <= u1]
    c = seg[-1] if seg else ''
    prev = [l for up,l in cnt if up is not None and up <= u0]
    p = prev[-1] if prev else ''
    def dl(key):
        a, b = g(p,key), g(c,key)
        if a is None or b is None: return '?'
        return str(b - a)
    kk = min(k0, 11)     # 🔴 센서 상한 11
    note = '' if k0 <= 11 else ' ⚠k=13→**11**'
    print('| **%d**%s | **%.3f** | %s | %s | %s | %s | %s |' % (
        kk, note, per, dl('ssovf'), dl('smiss'), dl('oow'), dl('drop'),
        '?'))
print()
print('구간별 delta 다(직전 CNT 대비). ramLow 는 누적 최저라 아래에 따로.')
print('ramLow 추이:', [v for _,v in ram])
