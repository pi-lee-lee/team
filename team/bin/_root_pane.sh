#!/usr/bin/env bash
# _root_pane.sh — tmux 0번 창 좌측에서 **루트 세션**을 띄우고, 루트가 끝나면 팀을 내린다.
#
# 왜 래퍼가 필요한가
#   `claude` 를 그냥 실행하면 `/exit` 로 루트가 끝나도 tmux 세션과 에이전트 5창은
#   그대로 살아남는다. 그러면 "에이전트는 작업 세션이 사는 동안만 상주한다"가 깨진다
#   — 루트는 없는데 에이전트만 남아 아무도 지시하지 않는 상태가 된다.
#   그래서 루트 종료를 팀 종료로 잇는다.
#
# 구분해야 할 두 가지
#   분리(Ctrl-b d) : claude 는 계속 살아있다 → 팀 유지. 나중에 team/team.sh 로 재진입.
#   종료(/exit)    : claude 프로세스가 끝난다 → 여기로 내려와 팀 전체를 내린다.
#
# 실수로 /exit 했을 때를 위해 확인을 한 번 받는다(무응답 15초면 종료로 본다 —
# 아무도 안 보고 있는데 루트가 죽었다면 팀을 남겨둘 이유가 없다).
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

cd "$PROJECT_DIR" || exit 1
claude "$@"

n="$(python3 "$BIN_DIR/teamctl.py" roster 2>/dev/null | grep -c . || echo 0)"

echo
echo "──────────────────────────────────────────────────────────────"
echo " 루트 세션이 종료됐다."
echo " 이 팀의 에이전트($n명)는 작업 세션이 사는 동안만 상주한다."
echo "──────────────────────────────────────────────────────────────"
printf " 팀 전체를 종료할까? [Y/n] (15초 무응답이면 종료) "
read -t 15 -r ans
echo

case "$ans" in
  n|N|no|NO|No)
    echo " 팀을 유지한다. 루트만 다시 열려면 이 창에서:"
    echo "   tmux respawn-pane -k -t '$TEAM_SESSION:root.0' '$BIN_DIR/_root_pane.sh'"
    echo " 나중에 전체를 내리려면: team/bin/stop.sh"
    exec "$SHELL"
    ;;
  *)
    echo " 팀을 내린다…"
    exec bash "$BIN_DIR/stop.sh"
    ;;
esac
