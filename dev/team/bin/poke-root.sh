#!/bin/bash
# 지정 시각에 루트 창(tmux pane)에 문장을 타이핑하고 엔터를 친다.
#   team/bin/poke-root.sh 04:50 "작업재개하라"
# ⚠ 새 세션을 안 띄운다. **지금 도는 그 창**이 이어서 작업한다.
set -u
T="${1:-04:50}"; MSG="${2:-작업재개하라}"; PANE="${PANE_ID:-%0}"
now=$(date +%s)
goal=$(date -j -f "%Y-%m-%d %H:%M" "$(date +%F) $T" +%s) || exit 1
[ "$goal" -le "$now" ] && goal=$((goal + 86400))
while [ "$(date +%s)" -lt "$goal" ]; do sleep 20; done
tmux send-keys -t "$PANE" "$MSG"
sleep 1
tmux send-keys -t "$PANE" Enter
