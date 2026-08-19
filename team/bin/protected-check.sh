#!/bin/bash
# 잠긴 영역이 바뀌었는지 본다 — 🔴 **차단이 아니라 설명 요구다.**
#
#   team/bin/protected-check.sh              작업 트리 대 HEAD
#   team/bin/protected-check.sh <범위>       예: fceadf3..HEAD
#
# 통과가 뜻하는 것: "이 축에서 신호가 없다". **거동 동일 보증이 아니다.**
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
RANGE="${1:-}"
python3 - "$RANGE" <<'PY'
import json, io, subprocess, sys, os
rng = sys.argv[1] if len(sys.argv) > 1 else ""
cfg = json.load(io.open(".claude/protected.json", encoding="utf-8"))

def sh(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout

changed = set()
if rng:
    # 🔴 core.quotepath=false · -z : git 은 기본으로 비ASCII 경로를 이스케이프해 따옴표로 싼다.
    #   그대로 비교하면 **한글 경로가 한 번도 안 맞아 검사가 영원히 초록이다.**
    #   (2026-08-19 루트가 이 검사를 만들면서 실제로 그 거짓 초록을 냈다)
    out = sh("git","-c","core.quotepath=false","diff","--name-only","-z",rng)
    changed = set(x for x in out.split("\0") if x)
    label = f"범위 {rng}"
else:
    out = sh("git","-c","core.quotepath=false","status","--porcelain","-z")
    changed = set()
    for rec in out.split("\0"):
        if len(rec) > 3: changed.add(rec[3:])
    label = "작업 트리 대 HEAD"

# 🔴 **목록의 경로가 실재하는가** — 없으면 통과가 아니라 경보다.
#   파일이 옮겨지면 목록이 낡고, 낡은 목록은 "변화 없음"으로 조용히 통과한다.
#   (2026-08-19 실제로 그렇게 됐다 — ardu/ 이사 뒤 다섯 경로가 전부 사라졌는데 검사는 초록이었다)
missing = []
for a in cfg["areas"]:
    for p in a["paths"] + ([a["include_host"]] if a.get("include_host") else []):
        if not os.path.exists(p): missing.append((a["concern"], p))

hits, ok = [], []
for a in cfg["areas"]:
    got = [p for p in a["paths"] if p in changed]
    host = a.get("include_host")
    host_hit = bool(host and host in changed)
    (hits if (got or host_hit) else ok).append((a, got, host_hit))

print(f"══ 잠긴 영역 검사 ({label}) ══")
print(f"   기준선: {cfg['baseline_note']}\n")

if missing:
    print("  🔴🔴 **목록이 낡았다 — 이 검사는 지금 아무것도 지키지 않는다**")
    for c, p in missing:
        print(f"     · {c}: 경로 없음 {p}")
    print("     → .claude/protected.json 의 paths 를 실제 위치로 고쳐라.")
    print("     🔑 파일이 옮겨졌는데 목록을 안 고치면 **영원히 초록**이다.\n")

for a, got, host_hit in ok:
    print(f"  ✅ {a['concern']:22s} 변화 없음  ({len(a['paths'])}개 파일)")

for a, got, host_hit in hits:
    print(f"\n  🟡 {a['concern']} — **설명이 필요하다** (막지 않는다)")
    print(f"     소유 {a['owner']} · 관련 {a['req']}")
    for p in got:  print(f"     · 바뀜 {p}")
    if host_hit:
        print(f"     · 🔴 include 숙주가 바뀜 {a['include_host']}")
        print(f"       {a['note']}")
    print("     → 필요한 것: 이 변경이 **슬롯/링크 자체를 고치는 일인지**,")
    print("       아니면 **다른 작업의 부수 효과인지** 를 커밋 메시지나 원장에 한 줄.")
    print("       🔴 부수 효과라면 그 자리에서 멈추고 루트에 올려라.")

if not hits:
    print("\n  ⚠ 통과는 **'이 축에서 신호가 없다'** 는 뜻이다.")
    print("     전역 상태·호출 순서는 이 파일들 밖에서 바뀔 수 있으므로 **거동 동일 보증이 아니다.**")

for note in cfg.get("_pending", []):
    print(f"\n  ⏳ {note}")
sys.exit(0)
PY
