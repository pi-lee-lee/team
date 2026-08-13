#!/usr/bin/env bash
# team-up.sh — 팀 전원을 상시 기동 상태로 올린다.
#
# 각 전문 에이전트는 `claude --bg` 백그라운드 세션으로 **개별 기동**된다.
# 세션은 스스로 끝나지 않고 계속 살아 있으며, SendMessage 로 언제든 호출된다.
# 이 스크립트는 멱등하다 — 이미 살아 있는 에이전트는 건드리지 않는다.
# 따라서 아무 때나 다시 돌려서 "전원 기동" 상태를 회복할 수 있다.
#
# 사용법
#   team/bin/team-up.sh            살아있지 않은 에이전트만 기동
#   team/bin/team-up.sh --only cpp-engineer,web-engineer
#   team/bin/team-up.sh --restart  전원 정지 후 재기동(세션 컨텍스트는 사라진다)
#
# 에이전트 명단 = .claude/agents/*.md . agent-new.sh 로 새 에이전트를 만들면
# 다음 team-up.sh 실행에서 자동으로 합류한다.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

TEAMCTL="$BIN_DIR/teamctl.py"
only=""; restart=0
while [ $# -gt 0 ]; do
  case "$1" in
    --only) only="$2"; shift 2;;
    --restart) restart=1; shift;;
    -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
    *) die "알 수 없는 옵션: $1";;
  esac
done

command -v claude >/dev/null || die "claude CLI 를 찾을 수 없다"
mkdir -p "$REG_DIR" "$REQ_DIR" "$OPEN_DIR"

# 이 프로젝트에서 기동된 백그라운드 세션만 본다(다른 프로젝트의 동명 세션과 섞이지 않게).
agents_json() { claude agents --cwd "$PROJECT_DIR" --json 2>/dev/null; }

# find_session <이름> -> "<short_id> <session_id> <status>" (없으면 빈 문자열)
find_session() {
  agents_json | python3 -c '
import json,sys
want=sys.argv[1]
try: rows=json.load(sys.stdin)
except Exception: rows=[]
for r in rows:
    if r.get("name")==want and r.get("kind")=="background":
        print(r.get("id",""), r.get("sessionId",""), r.get("status",""))
        break
' "$1"
}

boot_prompt() {
  cat <<EOF
너는 이 팀의 '$1' 에이전트다. 지금은 기동 확인 단계다. 아래만 수행하고 멈춰라.

1. CLAUDE.md 와 .claude/agents/$1.md 를 읽고 팀 규약을 숙지한다.
   (핵심: 네 소유가 아닌 파일은 절대 직접 고치지 않는다 / 하위 에이전트를 만들지 않는다 /
    요청은 문장이 아니라 md 파일로 주고받는다)
2. team/bin/req.sh list --to $1 로 네 앞으로 온 미결 요청을 확인한다.
3. 미결 요청이 있으면 규약대로 처리한다. 없으면 "대기" 라고만 보고하고 멈춘다.

지시받지 않은 코드를 임의로 만들지 마라. 너는 종료되지 않고 계속 대기하며,
루트나 동료가 SendMessage 로 REQ 번호를 보내면 그때 움직인다.
EOF
}

launch() {
  local name="$1" out short sid i
  out="$(claude --bg -n "$name" --agent "$name" --permission-mode "$TEAM_PERM" "$(boot_prompt "$name")" 2>&1)" \
    || { echo "  ✗ $name 기동 실패: $out"; return 1; }
  short="$(printf '%s' "$out" | grep -oE '[0-9a-f]{8}' | head -1)"
  [ -n "$short" ] || { echo "  ✗ $name: 세션 id 를 못 읽었다: $out"; return 1; }

  # 세션이 목록에 나타날 때까지 기다린 뒤 full session id 를 확보한다.
  # 훅은 full id 로 역할을 판정하므로 이 등록이 끝나야 소유권이 성립한다.
  for i in $(seq 1 30); do
    sid="$(find_session "$name" | awk '{print $2}')"
    [ -n "$sid" ] && break
    sleep 1
  done
  [ -n "$sid" ] || { echo "  ✗ $name: session id 확보 실패 — 등록되지 않았다"; return 1; }

  python3 "$TEAMCTL" registry-set "$sid" "$name" "$short" >/dev/null \
    || { echo "  ✗ $name: 레지스트리 등록 실패"; return 1; }
  echo "  ✓ $name  기동 (short=$short)"
}

roster="$(python3 "$TEAMCTL" roster)"
[ -n "$roster" ] || die ".claude/agents/ 에 에이전트가 없다"

echo "팀 기동 — 프로젝트: $PROJECT_DIR / 권한모드: $TEAM_PERM"
for name in $roster; do
  if [ -n "$only" ] && ! printf '%s' ",$only," | grep -q ",$name,"; then
    continue
  fi

  info="$(find_session "$name")"
  short="$(printf '%s' "$info" | awk '{print $1}')"

  if [ -n "$short" ] && [ "$restart" = 1 ]; then
    claude stop "$short" >/dev/null 2>&1
    echo "  … $name 정지 후 재기동"
    short=""
    sleep 1
  fi

  if [ -n "$short" ]; then
    # 살아 있어도 레지스트리가 비어 있을 수 있다(수동 기동 등) → 등록을 보정한다.
    sid="$(printf '%s' "$info" | awk '{print $2}')"
    python3 "$TEAMCTL" registry-set "$sid" "$name" "$short" >/dev/null
    echo "  · $name  이미 기동중 (short=$short, $(printf '%s' "$info" | awk '{print $3}'))"
    continue
  fi

  launch "$name"
done

echo
echo "현황: team/bin/team-status.sh"
