#!/usr/bin/env python3
"""창 차분 — 동결 기준선과 지금을 빼서 **창 안의 값만** 낸다.

왜 필요한가: 서버 요약 계수는 **서버 기동부터의 누적**이다. 그대로 인용하면
여러 창이 한 분모에 섞인다(창 AG 의 배선 정착 구간까지 딸려 온다).

사용: python3 monitor/win_diff.py monitor/winAH-t0-freeze.txt [로그경로]

⚠ 이 도구가 **답하지 않는 질문**:
  - 왜 끊겼나 (기전) — 사유 문자열을 세어 줄 뿐이다
  - 시리얼에만 있는 축(CWJAP·IP취득·UART손상·사다리) — 이 로그에 아예 없다. `0` 이 아니라 **미계측**
"""
import re, sys, io
from collections import Counter

FREEZE = sys.argv[1]
LOG    = sys.argv[2] if len(sys.argv) > 2 else "/tmp/srv_prod.out"

fz = io.open(FREEZE, encoding="utf-8", errors="replace").read()
m = re.search(r"t0 동결 — (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)", fz)
if not m:
    sys.exit("🔴 동결 파일에서 t0 를 못 찾았다 — 형식이 바뀌었나? 판정하지 말고 멈춰라")
T0 = m.group(1)

# 기준선 요약(동결 시점 누적)
base = {}
bm = re.search(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\s+⏱ 소크.*$", fz, re.M)
BASE_AT = bm.group(1) if bm else "(없음)"
def grab(text, pat):
    g = re.search(pat, text)
    return g.group(1) if g else None
if bm:
    b = bm.group(0)
    base = {
        "세션":   grab(b, r"세션 (\d+)회"),
        "프레임": grab(b, r"프레임 (\d+)"),
        "재부팅감지": grab(b, r"재부팅감지 (\d+)"),
        "오프라인": grab(b, r"오프라인 (\d+)회"),
        "등록완료": grab(b, r"등록 완료 (\d+)"),
        "링크없음초": grab(b, r"링크없음 (\d\d):(\d\d):(\d\d)"),
    }

lines = io.open(LOG, encoding="utf-8", errors="replace").read().splitlines()

# 창 안의 세션 사건
# 🔴 세션 번호는 **서버 재기동마다 1 부터 재사용된다**(로그 한 파일에 여러 프로세스가 섞인다).
#    그래서 번호를 키로 쓰면 옛 프로세스의 같은 번호와 뒤섞인다 — 2026-08-20 에 실제로 그랬다
#    ("진행 중 없음" 이라고 냈는데 30분째 살아 있는 세션이 있었다).
#    고침: 시간순으로 훑으며 **직전에 열린 같은 번호**와 짝지어 소비한다.
live_open, closes = {}, []
for ln in lines:
    ts = ln[:19]
    if len(ts) < 19 or not ts[0].isdigit():
        continue
    mo = re.search(r"\+ARD 주차 노드 접속 — 세션#(\d+)", ln)
    if mo:
        live_open[int(mo.group(1))] = ts
        continue
    mc = re.search(r"-ARD 세션#(\d+) 종료\(([^)]*(?:\([^)]*\))?[^)]*)\) — 지속 (\d\d):(\d\d):(\d\d) · 프레임 (\d+)", ln)
    if mc:
        n = int(mc.group(1))
        closes.append(dict(n=n, reason=mc.group(2), ts=ts, opened=live_open.pop(n, None),
                           dur=int(mc.group(3))*3600+int(mc.group(4))*60+int(mc.group(5)),
                           frames=int(mc.group(6))))

# 마지막 요약(지금 누적)
now, NOW_AT = {}, "(없음)"
for ln in reversed(lines):
    if "⏱ 소크" in ln:
        NOW_AT = ln[:19]
        now = {
            "세션":   grab(ln, r"세션 (\d+)회"),
            "프레임": grab(ln, r"프레임 (\d+)"),
            "재부팅감지": grab(ln, r"재부팅감지 (\d+)"),
            "오프라인": grab(ln, r"오프라인 (\d+)회"),
            "등록완료": grab(ln, r"등록 완료 (\d+)"),
        }
        break

def hhmmss(s): return f"{s//3600:02d}:{(s%3600)//60:02d}:{s%60:02d}"

print(f"# 창 차분 — t0 {T0}")
print(f"  기준선 요약 {BASE_AT}  →  지금 요약 {NOW_AT}")
print(f"  로그 {LOG}")
print()
print("## 누적 계수 차분 (창 안의 값)")
for k in ("세션","프레임","재부팅감지","오프라인","등록완료"):
    b, n = base.get(k), now.get(k)
    if b is None or n is None:
        print(f"  {k:<10} 🔴 못 읽었다 — 요약 형식이 바뀌었을 수 있다. 값으로 쓰지 마라")
    else:
        print(f"  {k:<10} {int(n)-int(b):>6}   (기준선 {b} → 지금 {n})")
print()

# t0 이전에 시작해 t0 이후에 끝난 세션 = 부분 표본
part = [c for c in closes if c["ts"] > T0 and (c["opened"] or "9") < T0]
full = [c for c in closes if c["ts"] > T0 and (c["opened"] or "0") >= T0]
live = sorted((ts, n) for n, ts in live_open.items() if ts >= T0)

print("## 세션 표 — 창 안에서 **끝난** 것")
if part:
    print(f"  ⚠ 부분 표본 {len(part)}건(시작이 t0 앞) — **지속 분포에서 뺀다**: "
          + ", ".join(f"#{c['n']} {hhmmss(c['dur'])}" for c in part))
if not full:
    print("  🔴 창 안에서 시작해 끝난 세션이 **0건**이다.")
    print("     → '유지된다'인지 '아직 표본이 없다'인지 **가르지 못한다.** 지속 판정 금지")
else:
    for c in full:
        print(f"  #{c['n']:<3} {hhmmss(c['dur'])}  프레임 {c['frames']:<5} {c['reason']}")
    ds = sorted(c["dur"] for c in full)
    print(f"  n={len(ds)} · 최소 {hhmmss(ds[0])} · 중앙 {hhmmss(ds[len(ds)//2])} · 최대 {hhmmss(ds[-1])}")
    print("  ⚠ 분포로 읽어라. 순서대로 나열하면 없는 추세가 보인다(2026-08-20 실제로 그랬다)")
print()
print("## 🔴 종료 사유별 — 갈라 센다. 뭉치면 '끊겼다' 하나가 된다")
cnt = Counter(c["reason"] for c in full)
if not cnt:
    print("  (창 안에서 끝난 세션 없음)")
for r, k in cnt.most_common():
    print(f"  {k:>3}건  {r}")
if part:
    print("  " + " " * 3 + "   ⚠ 위 부분 표본의 사유는 이 표에 안 넣었다(창 밖에서 시작했다)")
print()
print('## 진행 중 : ' + (', '.join(f'세션#{n} (시작 {ts})' for ts, n in live) if live else '없음'))
print()
print("## 미계측 — `0` 이 아니다")
print("  CWJAP 거동 · IP 취득 · UART 손상 · busy p · 사다리 단계 · 재부팅 기전")
print("  → 시리얼 탭에만 있다. 이 창은 탭을 안 열었다")
