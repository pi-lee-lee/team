#!/usr/bin/env bash
# _agent_pane.sh <에이전트이름> — tmux 창 하나에서 대화형 에이전트를 띄운다.
#
# team.sh 가 tmux new-window 로 이걸 실행한다. 별도 스크립트로 뺀 이유는 따옴표다:
# 부팅 프롬프트를 tmux 명령 문자열 안에 인라인으로 넣으면 셸 인용이 세 겹으로 중첩돼
# 조용히 깨진다. 여기서 조립하면 인용은 한 겹뿐이다.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

name="${1:-}"
[ -n "$name" ] || die "사용법: _agent_pane.sh <에이전트이름>"
[ -f "$AGENTS_DIR/$name.md" ] || die "그런 에이전트가 없다: $AGENTS_DIR/$name.md"

read -r -d '' boot <<EOF
너는 이 팀의 '$name' 에이전트다. 지금은 기동 확인 단계다. 아래만 수행하고 멈춰라.

1. CLAUDE.md 와 .claude/agents/$name.md 를 읽고 팀 규약을 숙지한다.
   (핵심: 네 소유가 아닌 파일은 절대 직접 고치지 않는다 / 하위 에이전트를 만들지 않는다 /
    요청은 문장이 아니라 md 파일로 주고받는다)
2. team/bin/req.sh list --to $name 로 네 앞으로 온 미결 요청을 확인한다.
3. 미결 요청이 있으면 규약대로 처리한다. 없으면 "대기" 라고만 보고하고 멈춘다.

지시받지 않은 코드를 임의로 만들지 마라. 너는 종료되지 않고 계속 대기하며,
루트나 동료가 SendMessage 로 REQ 번호를 보내면 그때 움직인다.
EOF

cd "$PROJECT_DIR" || die "프로젝트 디렉터리로 이동 실패"

# 대화형(포그라운드)이므로 승인 프롬프트가 이 창에 그대로 뜨고 사람이 답할 수 있다.
# 백그라운드에서 조용히 멈추던 문제를 여기서 해소한다.
exec claude -n "$name" --agent "$name" --permission-mode "$TEAM_PERM" \
  --settings '{"crossSessionInbound":"accept"}' "$boot"
