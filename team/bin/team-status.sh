#!/usr/bin/env bash
# team-status.sh — 팀 현황 한 장. 루트 에이전트가 관리감독에 쓰는 기본 도구.
#
#   AGENTS   에이전트별 기동 상태 + 등록 여부 + 미결 요청 수
#   OPEN     날짜를 가로지르는 미결 요청 목록
#   RECENT   최근 요청 이벤트(원장 꼬리)
#
# 사용법: team/bin/team-status.sh [-n <최근 이벤트 수>]
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

n=10
[ "${1:-}" = "-n" ] && n="${2:-10}"

echo "══ AGENTS ══════════════════════════════════════════════════"
claude agents --cwd "$PROJECT_DIR" --json 2>/dev/null \
  | python3 - "$PROJECT_DIR" <<'PY'
import json, os, sys, glob

root = sys.argv[1]
try:
    rows = json.load(sys.stdin)
except Exception:
    rows = []

live = {r.get("name"): r for r in rows if r.get("kind") == "background"}

try:
    with open(os.path.join(root, ".claude", "team", "registry.json")) as f:
        reg = (json.load(f).get("agents") or {})
except Exception:
    reg = {}
registered_roles = {v.get("role") for v in reg.values() if isinstance(v, dict)}

names = sorted(
    os.path.basename(p)[:-3]
    for p in glob.glob(os.path.join(root, ".claude", "agents", "*.md"))
)

# 에이전트별 미결(open/claimed) 요청 수
pending = {}
for f in glob.glob(os.path.join(root, "requests", "*", "REQ-*.md")):
    to = st = None
    with open(f) as fh:
        for line in fh:
            if line.startswith("to: "):
                to = line[4:].strip()
            elif line.startswith("status: "):
                st = line[8:].strip()
            elif line.strip() == "---" and to and st:
                break
    if st in ("open", "claimed") and to:
        pending[to] = pending.get(to, 0) + 1

if not names:
    print("  (.claude/agents/ 에 에이전트가 없다)")
for nm in names:
    r = live.get(nm)
    if r:
        state = r.get("status", "?")
        wf = r.get("waitingFor")
        mark = "●"
        if state == "waiting":
            mark, state = "⏸", "%s(%s)" % (state, wf or "")
        detail = "%s %-9s short=%s" % (mark, state, r.get("id", "?"))
    else:
        detail = "○ 미기동      —  team/bin/team-up.sh 로 기동"
    ok = "등록✓" if nm in registered_roles else "등록✗(소유권 미적용!)"
    p = pending.get(nm, 0)
    print("  %-18s %-34s %-22s 미결 %d" % (nm, detail, ok, p))
PY

echo
echo "══ OPEN (미결 요청) ════════════════════════════════════════"
if [ -d "$OPEN_DIR" ] && [ -n "$(ls -A "$OPEN_DIR" 2>/dev/null)" ]; then
  bash "$BIN_DIR/req.sh" list
else
  echo "  (없음)"
fi

echo
echo "══ RECENT (최근 이벤트) ════════════════════════════════════"
if [ -f "$INDEX" ]; then
  tail -n "$n" "$INDEX"
else
  echo "  (원장 없음 — 아직 요청이 발행되지 않았다)"
fi
