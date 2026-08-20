# -*- coding: utf-8 -*-
"""상행 `S` 의 occ 필드 전이를 재서 시뮬 주기를 검정한다.
🔴 예측(arduino) : A1 훅 = (slotNo % 20) < 10 · 슬롯 1.2초 → **24.0초 주기**(12 찼다/12 비었다)
   occ 비트 : A1=0x20 · B1=0x10   → `20`↔`00` 오감 · LD 명령 뒤 `30`
"""
import io, re, sys
SER = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-winAE.log"
L = io.open(SER, encoding="utf-8", errors="replace").read().splitlines()
RE_S    = re.compile(r'\[AT\] " S,\d+,([0-9A-Fa-f]+),')
# 🔴 `tmask` 는 **맨 끝 · 체크섬 바로 앞** 이다 (arduino · 소스 `Modules.h buildStatus`)
#    S,<seq>,<occ>,<res>,<up>,<dev>[,<tmask>],<ck>     ← `occ` 는 **언제나 필드 2**. 안 밀린다
#    ACK 이 뒤에 붙어 필드가 +4 씩 는다 → 평소 7+4k(%4==3) · 무장 8+4k(%4==0)
#    ⚠ %4 는 **교차 확인용만**. LF 소실로 `0B`+`A` 가 `0BA` 로 붙는 형태라 분할이 바뀌면 깨진다
RE_SFULL = re.compile(r'\[AT\] " (S,[^"]*)"')
RE_T = re.compile(r"^(\d\d):(\d\d):(\d\d)")

# 🔴 occ 비트 (n=6 · 모듈 idx i → 비트 5-i) : A1=0x20 B1=0x10 LD=0x08 LC=0x04 DR=0x02 L2=0x01
BIT = {"A1": 0x20, "B1": 0x10, "LD": 0x08, "LC": 0x04, "DR": 0x02, "L2": 0x01}
def decode(v):
    on = [k for k, b in sorted(BIT.items(), key=lambda x: -x[1]) if v & b]
    return ",".join(on) if on else "(없음)"

pts, prev = [], None
for l in L:
    m, t = RE_S.search(l), RE_T.match(l)
    if m and t:
        sec = int(t.group(1))*3600 + int(t.group(2))*60 + int(t.group(3))
        if prev is not None and sec < prev - 3600:   # 🔴 자정 넘김 — 안 고치면 음수 구간이 나온다
            sec += 86400
        prev = sec
        pts.append((sec, m.group(1).upper()))
if not pts:
    print("🔴 상행 `S` 에코 0건 — 잴 것이 없다"); sys.exit(0)

vals = {}
for _, v in pts: vals[v] = vals.get(v, 0) + 1
# 🔴 주기 검정은 **A1 비트만** 본다 — 액추에이터를 켜면 하위 비트가 같이 서서
#    `20↔00` 이 `2F↔0F` 가 된다. 마스크 안 하면 전이가 안 잡힌다
_RAW = list(pts)   # 불변식 검사에 원본이 필요하다
pts = [(t, "A1" if (int(v, 16) & BIT["A1"]) else "--") for t, v in pts if re.fullmatch(r"[0-9A-F]+", v)]
print("== occ 값 분포 (표본 %d) — 🔑 비트 해독 ==" % len(pts))
for v, n in sorted(vals.items(), key=lambda x: -x[1]):
    try:    note = decode(int(v, 16))
    except: note = "🔴 해독 불가"
    print("   `%s` ×%-5d  %s" % (v, n, note))

# 전이 구간 길이
runs, cur, st = [], pts[0][1], pts[0][0]
for tsec, v in pts[1:]:
    if v != cur:
        runs.append((cur, tsec - st)); cur, st = v, tsec
runs.append((cur, pts[-1][0] - st))
print("\n== 전이 구간 (🔴 **A1 비트만** 마스크해서 잰다) ==")
for v, d in runs[:14]: print("   `%s` %3d초" % (v, d))

# ── 🔴 교차 불변식 (arduino 가 소스에서 냈다 · 2026-08-20)
#    B1(0x10) = 센서 훅이 `digitalRead(13)` 으로 **읽은** 값
#    LD(0x08) = 라우터가 명령 인자에서 **만든** 에코
#    🔑 경로가 완전히 다른데 **같은 물리량(핀 13)** 이다 → 항상 같아야 한다
#    ⚠ 적용 범위 : **LED 되읽기 훅이 있는 판(창 AE~)만.** 옛 판은 훅이 없어 B1 이 늘 0 이다
#
#    형식 : S,<seq>,<occ>,<res>,<up>,<dev>[,<tmask>],<ck>   ← tmask 는 **맨 끝·ck 앞**. occ 는 안 밀린다
#    ACK 이 뒤에 concat 되어 +4 씩 → 평소 7+4k · 무장 8+4k
#    🔴🔴 그런데 **필드 수를 판별자로 쓰면 안 된다** — 실측으로 반증했다:
#      전 로그에서 %4 가 0/1/2 인 S 프레임이 322건 나왔는데 **전부 UART 손상**이다
#      (`S,1434,011`000010X0000000000X17hoXP1,3h` — **쉼표 자체가 `X` 로 깨진다**)
#      → 손상이 필드 수를 흔들어 **멀쩡한 프레임 35건이 "무장"으로 오검출** 됐다
#    ✅ 그래서 : ① **형식 검사를 먼저** 해서 손상 프레임을 빼고
#                ② 남은 것 중 **8+4k 꼴일 때만** tmask 를 읽고
#                🔴 ③ **`tmask & 0x10` 인 프레임만** 뺀다 (무장 구간을 통째로 빼지 않는다 —
#                   A1 만 주입했으면 B1↔LD 는 그대로 검사해야 한다)
RE_FLD = re.compile(r"^[0-9A-Fa-f]+$")
n_T = sum(1 for l in L if re.search(r'\+IPD[^"]*[:,]T,', l) or re.search(r'^\S+\s+T,', l))
ok, n_dmg, n_armed, n_skip = [], 0, 0, 0
for l in L:
    m, t = RE_SFULL.search(l), RE_T.match(l)
    if not (m and t): continue
    fs = m.group(1).split(",")
    if len(fs) < 7 or not (RE_FLD.match(fs[2]) and RE_FLD.match(fs[3]) and fs[4].isdigit()):
        n_dmg += 1; continue                      # 🔴 손상 프레임 — 판정에 안 쓴다
    tm = None
    if len(fs) % 4 == 0:
        n_armed += 1
        try: tm = int(fs[6], 16)
        except ValueError: n_dmg += 1; continue
    if tm is not None and (tm & 0x10):
        n_skip += 1; continue                     # 🔴 B1 주입 프레임만 뺀다
    ok.append((int(fs[2], 16), l[:8]))

print("\n== 🔴 교차 불변식 : `B1(0x10)` == `LD(0x08)` ==")
print("   (센서 훅의 `digitalRead(13)` ↔ 라우터의 명령 에코 — **다른 경로 · 같은 물리량**)")
print("   주입 `T` %d건 · 무장 꼴 %d건 · B1주입 제외 %d건 · 🔴 **손상 제외 %d건**"
      % (n_T, n_armed, n_skip, n_dmg))
if n_T == 0 and n_armed == 0:
    print("   ✅ **주입 0 · 무장 0** — 예외가 없다. **남은 프레임 전부가 검사 대상**이다")
elif n_T == 0 and n_armed:
    print("   ⚠ **`T` 는 0인데 무장 꼴이 %d건이다** — 두 각도가 안 맞는다." % n_armed)
    print("      🔑 대개 **손상이 필드 수를 흔든 것**이다. `T` 쪽을 믿어라(§7.213)")
bad = [(v, t) for v, t in ok if ((v & 0x10) != 0) != ((v & 0x08) != 0)]
if not ok:
    print("   ⚠ 표본 0 — **못 쟀다**")
elif not bad:
    print("   ✅ **어긋남 0 / %d** — 두 경로가 일치한다" % len(ok))
else:
    print("   🔴 **어긋남 %d / %d**" % (len(bad), len(ok)))
    for v, t in bad[:4]:
        print("      %s  occ=`%02X` → B1=%d LD=%d  %s" % (t, v, (v>>4)&1, (v>>3)&1, decode(v)))
    print("   ⚠ **옛 판(되읽기 훅 없음)에서는 이 어긋남이 정상이다** — B1 이 구조적으로 늘 0")

mid = [r for r in runs[1:-1]]
          # 양끝은 잘렸을 수 있다 — 뺀다
if len(mid) >= 2:
    print("\n== 🔴 주기 검정 (양끝 구간은 잘렸을 수 있어 뺐다 · 표본 %d) ==" % len(mid))
    tot = [mid[i][1] + mid[i+1][1] for i in range(len(mid)-1)]
    if tot:
        avg = sum(tot) / float(len(tot))
        ok = abs(avg - 24.0) <= 2.0
        print("   구간 길이 : %s" % " · ".join("%d초" % d for _, d in mid[:8]))
        print("   한 주기(연속 두 구간) 평균 : **%.1f초**  (표본 %d)" % (avg, len(tot)))
        print("   → **%s** (예측 24.0±2초)" % ("✅ 성립" if ok else "🔴 벗어남 — 슬롯 길이나 훅이 바뀐 것"))
else:
    print("\n⚠ 전이가 **%d개**뿐이다 — 주기를 못 잰다. **'주기가 없다'가 아니라 '표본이 없다'** 다" % len(mid))
    print("   🔑 24초 주기를 두 번 보려면 최소 **50초** 는 관측해야 한다")
