# -*- coding: utf-8 -*-
"""창 AD 판정기 — 묶음 하행. 각본별 ✅/❌.
🔴 원칙 : 합격선을 숫자로 안 박는다. 값을 뽑고 대조는 사람이 한다(§7.191).
🔴 `[G]` 부족은 **세 갈래** 다 — penddrop / ACK 유무로 가른다(§7.199).
"""
import io, re, sys

SER = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-winAD.log"
try:
    L = io.open(SER, encoding="utf-8", errors="replace").read().splitlines()
except IOError:
    print("❌ 로그 없음: %s" % SER); sys.exit(1)

def c(rx):
    r = re.compile(rx); return sum(1 for l in L if r.search(l))
def rows(rx, n=0):
    r = re.compile(rx)
    return [(l[:8], m.group(n) if n else m.group(0)) for l in L for m in [r.search(l)] if m]

print("# 창 AD 판정 — %s (%d줄)\n" % (SER, len(L)))

# ── 표지 (무조건 떠야 하는 것)
print("== 표지 ==")
ban = [l for l in L if "PARKING NODE" in l]
print("  배너 : %s" % (ban[0][:110] if ban else "🔴 없음 — 부팅을 못 봤다"))
reg = rows(r"등록 전송 (\d+)B", 1)
print("  등록 : %s  ← 예고 **76B**(구조식 10+11n)" % (reg[0][1]+"B" if reg else "🔴 [REG] 없음"))
# 🔴 `D,LD,OG` 안에 `D,OG` 라는 가짜 일치가 숨어 있다(`L`+`D,OG`).
#    종류 필드(IP/IN/OG/OL/OB)까지 요구해서 막는다. findall 로 한 줄의 전부를 잡는다.
RE_MOD = re.compile(r"D,([A-Z][A-Z0-9]),(?:IP|IN|OG|OL|OB),")
mods = sorted(set(m for l in L for m in RE_MOD.findall(l)))
print("  모듈 : %s  (%d종)  ← 예고 6종(A1 B1 LD LC DR **L2**)" % (", ".join(mods), len(mods)))
occ = rows(r'\[AT\] " S,\d+,([0-9A-Fa-f]+),', 1)
print("  S 폭 : %s" % ("/".join(sorted(set(str(len(v)) for _, v in occ))) if occ else "🔴 에코 0건"))
print("== ❌목록 (0건이어야 한다) ==")
for pat, lab in ((r"2 slots","2 slots"), (r"5 modules","5 modules"), (r"12 modules","12 modules"),
                 (r"D,E\d","D,E*"), (r"D,X\d","D,X*")):
    n = c(pat); print("  %-12s %s" % (lab, "**%d건** 🔴" % n if n else "0 ✅"))

# ── 하행 구조 — 🔑 ㉠ 의 1차 판정자
print("\n== 🔑 하행 구조 (㉠ 1차 판정자 = 한 `+IPD` 에 몇 건인가) ==")
ipd = rows(r'\+IPD,(\d+):(.*?)"', 0)
for t, raw in ipd:
    body = re.search(r"\+IPD,(\d+):(.*)", raw)
    if not body: continue
    ncmd = body.group(2).count("G,")
    tag = " 🔑 **묶음 %d건**" % ncmd if ncmd > 1 else ""
    print("  %s  n=%-4s G%d%s  %s" % (t, body.group(1), ncmd, tag, body.group(2)[:52]))
print("  +IPD **%d건** · 그 안의 `G,` 총 **%d건**"
      % (len(ipd), sum(r.count("G,") for _, r in ipd)))

# ── 콜백 · 거절 — 사유를 갈라 센다
print("\n== 콜백 / 거절 (🔴 사유를 뭉뚱그리지 않는다 — 고치는 곳이 다르다) ==")
g_ok = rows(r"\[G\] idx=(\d+) arg=(\d+)")
print("  `[G]` 성공 : **%d건**" % len(g_ok))
for t, v in g_ok[:8]: print("     %s %s" % (t, v))
for pat, lab in ((r"인자 해독 실패","인자 해독 실패 → 프레임·서버"),
                 (r"등록 없음","등록 없음 → setup() router.on"),
                 (r"콜백이 거절","콜백이 거절 → 기여자 핸들러")):
    r = rows(r"\[G\] 거절[^\n]*" + pat)
    print("  `[G] 거절` %-28s **%d건**" % (lab, len(r)))
    for t, v in r[:3]: print("     %s %s" % (t, v[:74]))
for tag in ("LC", "L2", "DR"):
    r = rows(r"\[%s\] 거절[^\n]*" % tag)
    if r:
        print("  `[%s] 거절` **%d건**" % (tag, len(r)))
        for t, v in r[:3]: print("     %s %s" % (t, v[:74]))

# ── ACK — 🔑 멱등 재수신을 가르는 축
print("\n== 🔑 ACK (§7.199 · ②멱등 재수신을 가르는 축) ==")
ack = rows(r"A,(\d+),([A-Za-z0-9]+),(\d+)")
for t, v in ack[:14]: print("  %s %s" % (t, v))
print("  ACK **%d건**" % len(ack))

# ── 🔴 ㉠ 실패 시 세 갈래
print("\n== 🔴 `[G]` 부족이면 — **세 갈래로 가른다** ==")
cnt = [l for l in L if "[CNT]" in l]
def f(k, i=0):
    for l in reversed(cnt):
        m = re.search(k + r"=(\d+)", l)
        if m: return int(m.group(1))
    return None
pd, pf = f("penddrop"), f("pendfill")
print("  penddrop **%s** / pendfill **%s**   %s"
      % (pd, pf, "⚠ 분모 없이 `0` 을 쓰지 마라" if pd == 0 else ""))
print("""  ① `[G]` 부족 + penddrop **+3**            → **알려진 묶음 손실.** 크기만 적는다
  ② `[G]` 부족 + penddrop **0** + ACK 나감  → **멱등 재수신.** 결함 아니다
  🔴 ③ `[G]` 부족 + penddrop **0** + ACK 없음 → **이때만 새 결함**""")

# ── 배경 (판정 안 함)
print("\n== 📌 배경 — **판정하지 않는다**(자극 창 · n=1) ==")
print("  ssovf=%s ⚠ **하한이다**(읽으면 지워진다) · oow=%s · SLOT-OOW %d · +IPD %d"
      % (f("ssovf"), f("oow"), c(r"SLOT-OOW"), len(ipd)))
print("  esprst=%s · busy p %d · CIFSR0 %d · LADDER실패 %d · T2 %d"
      % (f("esprst"), c(r"busy p"), c(r'"0\.0\.0\.0"'), c(r"LADDER\] 실패"), c(r"★★ T2 초과")))
print("  UART : `python3 monitor/uart_damage.py %s` 로 따로 센다" % SER)
