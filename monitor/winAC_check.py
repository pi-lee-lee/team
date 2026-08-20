# -*- coding: utf-8 -*-
"""창 AC 판정기 — 비율이 아니라 항목별 ✅/❌ 를 낸다.
🔴 원칙: 합격선을 숫자로 박지 않는다. **값을 뽑아 굽기 보고와 대조**한다(§7.191).
   그래서 이 스크립트는 '틀렸다'고 말하지 않고 '무엇이 나왔다'를 먼저 적는다.
"""
import io, re, sys

SER = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-winAC.log"
try:
    L = io.open(SER, encoding="utf-8", errors="replace").read().splitlines()
except IOError:
    print("❌ 로그 없음: %s" % SER); sys.exit(1)

def g(rx, n=1):
    r = re.compile(rx)
    return [m.group(n) for l in L for m in [r.search(l)] if m]
def c(rx):
    r = re.compile(rx); return sum(1 for l in L if r.search(l))

print("# 창 AC 판정 — %s (%d줄)" % (SER, len(L)))
print("⚠ 값을 먼저 적고, 대조는 사람이 한다. 이 도구는 합격선을 모른다.\n")

# ── ①  붙는가
ip  = g(r"IP 확보: ([0-9.]+)")
print("① 장치가 붙는가")
print("   CWJAP  %d · IP 확보 %d %s · CIPSTART %d · online %d"
      % (c(r"CWJAP="), len(ip), ("("+ip[0]+")" if ip else ""), c(r"CIPSTART"), c(r"online")))
print("   → %s" % ("✅ 붙었다" if ip else "❌ IP 못 잡았다"))

# ── ②③  배너 · 등록 — 🔑 배너가 모듈수와 자리수를 직접 찍는다
ban = [l for l in L if "PARKING NODE" in l]
print("\n②③ 배너 (🔑 모듈수·자리수의 1차 출처)")
if ban:
    print("   %s" % ban[0][:120])
    s = re.search(r"(\d+) slots", ban[0]); m = re.search(r"(\d+) modules", ban[0])
    print("   → 자리 **%s** · 모듈 **%s**" % (s.group(1) if s else "미표시",
                                              m.group(1) if m else "미표시"))
else:
    print("   🔴 배너 없음 — 부팅을 못 봤다는 뜻이다(탭이 리셋 뒤에 열렸나?)")
reg = g(r"등록 전송 (\d+)B")
print("   등록 크기 : %s  ← 굽기 보고 65B 와 대조(12모듈 판은 143B 였다)"
      % (reg[0]+"B" if reg else "🔴 [REG] 줄 없음"))

# ── ❌ 부재 선언 : 가상 모듈
vm = sorted(set(g(r"(D,(?:E|X)\d)")))
print("\n❌ 가상 모듈 부재 확인 (VIRTUAL_MODULES=0)")
print("   D,E*/D,X* : %s" % (", ".join(vm) if vm else "**0건 — 선언대로 없다** ✅"))
mods = sorted(set(g(r"D,([A-Z]\d|[A-Z]{2})(?=,)")))
print("   등록에 실린 모듈 : %s  (%d종)" % (", ".join(mods) if mods else "없음", len(mods)))

# ── ③b  S 자리 필드 폭  — 🔴 숫자를 박지 않는다. 관측값을 적는다
occ = g(r'\[AT\] " S,\d+,([0-9A-Fa-f]+),')
if occ:
    w = sorted(set(len(x) for x in occ))
    print("\n③b `S` occ 필드 폭 : **%s** (표본 %d · 예: %s)"
          % ("/".join(str(x) for x in w), len(occ), occ[0]))
    print("   ⚠ 폭은 자리 수에서 파생된다. **굽기 보고 값과 대조하고, 안 맞으면 물어라**")
else:
    print("\n③b `S` occ 필드 : 🔴 상행 에코 0건")

# ── ④  왕복
tx, ok = c(r"AT\+CIPSEND"), c(r"SEND OK")
print("\n④ 왕복이 되는가")
print("   CIPSEND %d · SEND OK %d · 상행 에코 %d · 하행 +IPD %d" % (tx, ok, len(occ), c(r"\+IPD")))
print("   → %s" % ("✅ 상행 왕복 성립" if ok else "❌ SEND OK 0"))

# ── ⑤  G 확장 인자 — 넷으로 갈라 본다
gf = [l for l in L if re.search(r"G,", l)]
cb = {}
for t in ("LD", "LC", "DR", "A1", "B1"):
    n = c(r"\[%s\]" % t)
    if n: cb[t] = n
print("\n⑤ `G` 확장 인자 — 넷으로 갈라 잰다")
print("   ⑤-a 전선에 나갔나 : `G,` **%d건**%s" % (len(gf), "" if gf else "  ⚠ 0 — 안 쏜 것인지 못 본 것인지 갈라라"))
for l in gf[:4]: print("        · %s" % l[:110])
print("   ⑤-b 콜백이 불렸나 : %s" % (" · ".join("%s=%d" % kv for kv in sorted(cb.items()))
                                     if cb else "**0건**  ⚠ 위와 같은 갈림"))
print("   ⑤-c ACK result=0  : 🔒 **서버 로그 — 내 계측기 밖**(socket 몫)")
print("   ⑤-d 거절 갈래(8자리 → result=3) : 🔒 **서버 로그 · 양성만 재지 마라**")
print("   ❌ 에코 확인 : **미측정**. 실물 액추에이터 occ 에코는 계약이 없다 —")
print("      **0 을 고장으로 읽지 마라. 빈칸이 아니라 의도적으로 안 잰 칸이다**")

# ── 배경 두 축 (판정 안 함 · 계속 쌓는 값)
print("\n📌 배경 두 축 — **창 AB 와 크기 비교하지 않는다**(분모가 통째로 바뀐다)")
cnt = [l for l in L if "[CNT]" in l]
def f(k):
    for l in reversed(cnt):
        m = re.search(k + r"=(\d+)", l)
        if m: return m.group(1)
    return "미집계"
print("   결합축 : esprst=%s · busy p %d · CIFSR 0.0.0.0 %d · LADDER실패 %d · T2 %d · okto=%s"
      % (f("esprst"), c(r"busy p"), c(r'"0\.0\.0\.0"'), c(r"LADDER\] 실패"), c(r"★★ T2 초과"), f("okto")))
print("   UART축 : `uart_damage.py %s` 로 따로 센다 (두 기준 · 분모 같이)" % SER)
print("   [CNT] %d줄%s" % (len(cnt), " · 🔴 0줄이면 60초를 못 넘겼다" if not cnt else ""))
