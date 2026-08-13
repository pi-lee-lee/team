#!/usr/bin/env bash
# stop.sh — 팀 전체를 끝낸다(루트 + 에이전트 전원).
#
# 에이전트는 **작업 세션이 사는 동안만** 상주한다. 일이 끝나면 통째로 내린다.
# tmux 세션을 죽이면 그 안의 claude 프로세스도 함께 끝나므로 이것으로 충분하지만,
# 과거에 백그라운드(`--bg`)로 띄운 잔여 세션이 있을 수 있어 그것도 함께 정리한다.
#
# 사용법
#   team/bin/stop.sh          팀 종료
#   team/bin/stop.sh --check  무엇이 살아있는지 보여주기만 하고 종료하지 않음
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

check=0
[ "${1:-}" = "--check" ] && check=1

echo "이 프로젝트에서 살아있는 세션:"
claude agents --cwd "$PROJECT_DIR" --json 2>/dev/null | python3 -c '
import json,sys
try: rows=json.load(sys.stdin)
except Exception: rows=[]
if not rows: print("  (없음)")
for r in rows:
    print("  %-18s %-12s %s" % (r.get("name","(무명)"), r.get("kind",""), (r.get("sessionId") or "")[:8]))
'

if [ "$check" = 1 ]; then
  exit 0
fi

# 1) 백그라운드 세션은 tmux 밖에 있으므로 개별로 내린다.
claude agents --cwd "$PROJECT_DIR" --json 2>/dev/null | python3 -c '
import json,sys
try: rows=json.load(sys.stdin)
except Exception: rows=[]
for r in rows:
    if r.get("kind")=="background" and r.get("id"): print(r["id"])
' | while read -r id; do
  [ -n "$id" ] && claude stop "$id" >/dev/null 2>&1 && echo "  · bg 세션 $id 정지"
done

# 2) tmux 세션을 죽이면 루트 창과 에이전트 창이 모두 함께 끝난다.
if tmux has-session -t "$TEAM_SESSION" 2>/dev/null; then
  tmux kill-session -t "$TEAM_SESSION"
  echo "  · tmux 세션 '$TEAM_SESSION' 종료 (루트 + 에이전트 전원)"
else
  echo "  · tmux 세션 '$TEAM_SESSION' 없음"
fi

# 레지스트리는 지우지 않는다. session_id → 역할 매핑이라 죽은 항목은 무해하고,
# 다음 기동 때 team.sh 가 역할별로 새 id 를 덮어쓴다(teamctl registry-set 이
# 같은 역할의 옛 항목을 지운다).
echo
echo "팀 종료됨. 다시 시작: team/team.sh"
