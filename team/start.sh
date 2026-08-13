#!/usr/bin/env bash
# start.sh — 이 프로젝트를 쓰는 **단 하나의 진입점**.
#
#   1) 에이전트 5명이 tmux 창에 떠 있는지 보장한다(멱등 — 이미 떠 있으면 안 건드림,
#      죽은 창만 되살림).
#   2) 현황을 한 번 보여준다.
#   3) 이 터미널을 **루트 에이전트** 세션으로 만든다(exec claude).
#
# 왜 루트를 여기서 띄우나: team.sh 는 일부러 루트 창을 만들지 않는다(루트가 둘이 되면
# 흐름 제어 주체가 모호해진다). 그래서 "에이전트 보장 + 루트 시작"을 한 번에 하는
# 진입점이 따로 필요하다. 사용자가 기억할 명령은 이것 하나면 된다.
#
# 사용법
#   team/start.sh            에이전트 보장 후 루트 세션 시작
#   team/start.sh --status   현황만 출력하고 종료(루트를 띄우지 않음)
#   team/start.sh -c         루트 세션을 이전 대화에 이어서 시작(claude --continue)
#
# 에이전트 창을 직접 보려면(승인 프롬프트에 답할 때):
#   tmux attach -t learnteam      · 창 이동 Ctrl-b <숫자> · 분리 Ctrl-b d
source "$(dirname "${BASH_SOURCE[0]}")/bin/_common.sh"

mode=start
case "${1:-}" in
  --status) mode=status ;;
  -c|--continue) mode=continue ;;
  -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  "") ;;
  *) die "알 수 없는 옵션: $1" ;;
esac

echo "▶ 에이전트 확인·복구"
bash "$TEAM_DIR/team.sh" --no-attach || die "에이전트 기동 실패"

echo
bash "$BIN_DIR/team-status.sh"

if [ "$mode" = status ]; then
  exit 0
fi

echo
echo "▶ 루트 세션 시작 — 이 터미널이 루트다. 작업 지시를 여기에 하면 된다."
echo "  에이전트 창을 보려면 다른 터미널에서: tmux attach -t $TEAM_SESSION"
echo

cd "$PROJECT_DIR" || die "프로젝트 디렉터리로 이동 실패"
if [ "$mode" = continue ]; then
  exec claude --continue
fi
exec claude
