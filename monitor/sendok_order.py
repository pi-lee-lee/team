#!/usr/bin/env python3
"""`AT+CIPSEND=n` 과 페이로드 에코의 **순서**를 세고, 그 뒤 `SEND OK` 가 왔는지 짝짓는다.

왜 이 도구인가 (2026-08-18)
  T2(8초 침묵) 세 건 전부에서 **페이로드 에코가 `AT+CIPSEND` 보다 먼저** 찍혔다.
  arduino 가설: `inSend` 중 첫 줄은 `pendLine` 에 담겨 **나중에** 출력되고(→ 순서가 뒤집혀 보인다)
  **둘째 줄은 버려진다**(→ 그 둘째가 `SEND OK` 면 게이트가 안 풀려 T2).

🔴 분모를 반드시 같이 낸다 — "뒤집힘 3건"이 몇 건 중 3건인가가 결론을 가른다.
  뒤집힘이 T2 에서만 나오면 기전 후보이고, 정상 슬롯에도 흔하면 **뒤집힘은 T2 의 원인이 아니다.**

⚠ 이 도구가 볼 수 없는 것: **버려진 줄은 출력되지 않으므로 로그에 없다.**
  그래서 "SEND OK 가 안 보인다"는 *ESP 가 안 보냈다* 와 *우리가 버렸다* 를 **못 가른다**(원장 7.109).
  갈리는 것은 **순서 뒤집힘**뿐이다 — 그건 버려진 게 아니라 *미뤄진* 줄이라 보인다.

사용: python3 monitor/sendok_order.py [로그]
"""
import re, sys
from collections import Counter

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-winF.log"
RE_CIPSEND = re.compile(r'\[AT\] "AT\+CIPSEND=(\d+)"')
RE_ECHO    = re.compile(r'\[AT\] " (S|[RM]),')          # 페이로드 에코는 앞에 공백이 붙는다
RE_SENDOK  = re.compile(r'\[AT\] "SEND OK')
RE_T2      = re.compile(r'★★ T2 초과')

ev = []
for ln in open(LOG, encoding="utf-8", errors="replace"):
    if len(ln) < 9 or ln[2] != ":" :
        continue
    t = ln[:8]
    m = RE_CIPSEND.search(ln)
    if m: ev.append((t, "cmd", int(m.group(1)))); continue
    if RE_ECHO.search(ln): ev.append((t, "echo", len(ln))); continue
    if RE_SENDOK.search(ln): ev.append((t, "sendok", 0)); continue
    if RE_T2.search(ln): ev.append((t, "t2", 0)); continue

# 거래 단위로 자른다: cmd 또는 echo 가 나오면 새 거래 시작 후보
norm = inv = noecho = 0
inv_t2 = norm_t2 = noecho_t2 = 0
inv_times, invsizes, normsizes = [], Counter(), Counter()
# 🔴 2026-08-19 추가(덧붙이기만) — 기존 계수·출력은 한 글자도 안 바꿨다.
#    창 O 판정에서 "`>=64B` 대역의 T2 수"를 못 내 미상으로 남긴 구멍을 메운다.
t2_by_size, all_by_size = Counter(), Counter()
noecho_times, noechosizes = [], Counter()
i = 0
while i < len(ev):
    k = ev[i][1]
    if k == "echo" and i + 1 < len(ev) and ev[i+1][1] == "cmd":
        inv += 1; inv_times.append(ev[i][0]); invsizes[ev[i+1][2]] += 1
        _sz = ev[i+1][2]; all_by_size[_sz] += 1
        j = i + 2
        seen_t2 = False
        while j < len(ev) and ev[j][1] not in ("cmd", "echo"):
            if ev[j][1] == "t2": seen_t2 = True
            j += 1
        if seen_t2: inv_t2 += 1; t2_by_size[_sz] += 1
        i = j
    elif k == "cmd" and i + 1 < len(ev) and ev[i+1][1] == "echo":
        norm += 1; normsizes[ev[i][2]] += 1
        _sz = ev[i][2]; all_by_size[_sz] += 1
        j = i + 2
        seen_t2 = False
        while j < len(ev) and ev[j][1] not in ("cmd", "echo"):
            if ev[j][1] == "t2": seen_t2 = True
            j += 1
        if seen_t2: norm_t2 += 1; t2_by_size[_sz] += 1
        i = j
    elif k == "cmd":
        # 🔴 `cmd` 뒤에 에코가 안 붙는다 = **에코 실종/지연**. 종전 판은 이것을 분모에서
        #    조용히 뺐다(창 G 16:40:15 원문에서 발각). 세 번째 갈래로 센다.
        j = i + 1
        seen_t2 = False
        while j < len(ev) and ev[j][1] != "cmd":
            if ev[j][1] == "t2": seen_t2 = True
            if ev[j][1] == "echo": break
            j += 1
        noecho += 1; noecho_times.append(ev[i][0]); noechosizes[ev[i][2]] += 1
        _sz = ev[i][2]; all_by_size[_sz] += 1
        if seen_t2: noecho_t2 += 1; t2_by_size[_sz] += 1
        i = j if j > i else i + 1
    else:
        i += 1

tot = norm + inv + noecho
print("# %s — 찾은 문자열 '[AT] \"AT+CIPSEND='  '[AT] \" S,'  '[AT] \"SEND OK'  '★★ T2 초과'" % LOG)
print("# 🔴 분모: CIPSEND 거래 %d건 (정상순서 %d · 뒤집힘 %d · **에코실종/지연 %d**)" % (tot, norm, inv, noecho))
print("#    ⚠ 세 번째 갈래는 2026-08-18 에 추가됐다 — 그 전 판은 이것을 분모에서 조용히 뺐다")
if tot == 0:
    print("# 🔴 0건 — 표지가 없거나 패턴이 안 맞는다. **판정 불가**"); raise SystemExit(0)
print()
print("## 순서 뒤집힘 (페이로드 에코가 AT+CIPSEND 보다 먼저)")
print("  뒤집힘 %d / %d = %.2f%%" % (inv, tot, 100.0*inv/tot))
print("  뒤집힌 거래의 CIPSEND 크기 분포 : %s" % dict(invsizes.most_common(8)))
print("  정상  거래의 CIPSEND 크기 분포 : %s" % dict(normsizes.most_common(8)))
print()
print("## 그 뒤 T2 가 났는가 — **이것이 판정선이다**")
print("  뒤집힌 거래 중 T2   : %d / %d" % (inv_t2, inv))
print("  에코실종 거래 중 T2 : %d / %d   ← 🔴 이 갈래가 새로 보인다" % (noecho_t2, noecho))
print("  에코실종 크기 분포  : %s" % dict(noechosizes.most_common(8)))
print("  정상  거래 중 T2   : %d / %d" % (norm_t2, norm))
print()
print("## 🔴 대역별 T2 — **크기 대역으로 귀속한다** (2026-08-19 추가)")
BANDS = [("<48", lambda n: n < 48), ("48~63", lambda n: 48 <= n < 64), (">=64", lambda n: n >= 64)]
for name, f in BANDS:
    tot_b = sum(v for k, v in all_by_size.items() if f(k))
    t2_b  = sum(v for k, v in t2_by_size.items() if f(k))
    if tot_b == 0:
        print("  %-6s 거래 0 — 🔴 **표본 없음. 판정 불가**(건강 아님)" % name)
    else:
        print("  %-6s T2 **%d / %d** = %.2f%%" % (name, t2_b, tot_b, 100.0 * t2_b / tot_b))
print("  ⚠ 분모는 **CIPSEND 거래**(정상순서+뒤집힘+에코실종 전부)다. 갈래별 표와 같은 모집단이다")
print("  ⚠ 크기는 `AT+CIPSEND=<n>` 의 n 이다 — 상행 프레임 크기이지 하행이 아니다")
print()
print("## 읽는 법")
print("· 뒤집힘이 T2 거래에만 몰리면 **기전 후보**다. 정상 거래에도 흔하면 원인이 아니다")
print("· ⚠ 버려진 줄은 로그에 없다 — 'SEND OK 부재'로는 *안 왔다* 와 *버렸다* 를 못 가른다(7.109)")
print("· 시각:", " ".join(inv_times[:12]), "…" if len(inv_times) > 12 else "")
