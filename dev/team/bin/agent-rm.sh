#!/usr/bin/env bash
# agent-rm.sh <이름> — 에이전트 하나를 **완전히** 지운다. 창 · 등록 · 정의 · 소유권 · 표.
#
# 🔴 규약상 상시 에이전트는 지우지 않는다. 이 스크립트는 **사용자가 명시적으로 삭제를 지시한 경우**
#    (예: 2026-08-26 검토 전용 에이전트 다섯)에만 쓴다. 습관적으로 쓰지 마라.
#
# 순서가 중요하다 — 등록을 먼저 지우면 그 세션이 root 로 취급되어 마지막 순간에 남의 파일을 쓸 수 있다.
#   ① 창을 죽인다(세션 종료) → ② 등록 제거 → ③ 정의 파일 제거 → ④ 소유권 규칙 제거 → ⑤ 표 재생성
set -u
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

name="${1:-}"
[ -n "$name" ] || die "사용법: agent-rm.sh <에이전트이름>"
cd "$PROJECT_DIR" || die "프로젝트 디렉터리로 이동 실패"

# ① 창 — 세션이 먼저 죽어야 한다
if tmux list-windows -t learnteam -F '#{window_name}' 2>/dev/null | grep -qx "$name"; then
  tmux kill-window -t "learnteam:$name" && echo "① 창 종료: $name"
else
  echo "① 창 없음(이미 닫힘): $name"
fi
sleep 1

# ② 등록 — registry.json 에서 이 역할의 항목을 지운다
python3 - "$name" <<'PY'
import json, io, sys
name = sys.argv[1]
p = ".claude/team/registry.json"
try:
    reg = json.load(io.open(p, encoding="utf-8"))
except Exception:
    print("② 등록부 없음"); sys.exit(0)
agents = reg.get("agents") or {}
gone = [sid for sid, e in agents.items() if isinstance(e, dict) and e.get("role") == name]
for sid in gone:
    del agents[sid]
reg["agents"] = agents
io.open(p, "w", encoding="utf-8").write(json.dumps(reg, ensure_ascii=False, indent=2) + "\n")
print(f"② 등록 제거: {len(gone)}건")
PY

# ③ 정의 파일
if [ -f "$AGENTS_DIR/$name.md" ]; then
  rm -f "$AGENTS_DIR/$name.md" && echo "③ 정의 제거: $AGENTS_DIR/$name.md"
else
  echo "③ 정의 없음"
fi

# ④ 소유권 규칙 — 이 에이전트가 owner 인 규칙을 전부 지운다
python3 - "$name" <<'PY'
import json, io, sys
name = sys.argv[1]
p = ".claude/ownership.json"
d = json.load(io.open(p, encoding="utf-8"))
before = len(d.get("path_rules", [])) + len(d.get("ext_rules", []))
# ⚠ ext_rules 항목이 dict 가 아닐 수 있다(문자열) — dict 인 것만 owner 를 본다
keep = lambda r: not (isinstance(r, dict) and r.get("owner") == name)
d["path_rules"] = [r for r in d.get("path_rules", []) if keep(r)]
d["ext_rules"]  = [r for r in d.get("ext_rules", [])  if keep(r)]
after = len(d["path_rules"]) + len(d["ext_rules"])
io.open(p, "w", encoding="utf-8").write(json.dumps(d, ensure_ascii=False, indent=2) + "\n")
print(f"④ 소유권 규칙 제거: {before-after}건")
PY

# ⑤ 표
python3 "$BIN_DIR/teamctl.py" render-table >/dev/null && echo "⑤ CLAUDE.md 표 재생성"

# 검산 — 남은 흔적이 0 이어야 한다
left=$( (tmux list-windows -t learnteam -F '#{window_name}' 2>/dev/null | grep -cx "$name";
         grep -c "\"role\": \"$name\"" .claude/team/registry.json 2>/dev/null;
         ls "$AGENTS_DIR/$name.md" 2>/dev/null | wc -l) | paste -sd+ - | bc )
[ "${left:-0}" -eq 0 ] && echo "✅ 흔적 0 — $name 삭제 완료" || echo "🔴 흔적 ${left}개 남음 — 손으로 확인해라"
