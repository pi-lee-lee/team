#!/bin/bash
# 🔴 밀렸다 — `tmux pipe-pane` 을 써라. 이건 **폴링**이라 팝업을 놓칠 수 있다.
#    2026-08-19 실측: Claude Code TUI 는 스크롤백을 안 남긴다(요청 5000줄 → 실재 59줄).
#    그래서 폴링은 "떠 있는 순간"에만 맞고, 배너는 그보다 짧을 수 있다.
#    ⚠ 그리고 이 스크립트는 monitor 영역(monitor/*.log)에 썼다 — 소유권 훅의 Bash 그물을
#      **리다이렉션이 스크립트 안에 있어서** 빠져나갔다. 남긴다: 그 구멍의 실물 사례다.
# 한계 배너를 화면에서 잡는다 — 그 값은 디스크에 없고 tmux pane 에만 있다(2026-08-19 실측).
#
# 🔴 이 도구가 답하지 않는 것:
#   · 배너가 **어떤 모양인지 아직 못 봤다.** 그래서 필터로 거르고 끝내지 않고
#     맞는 순간의 **화면 전체를 통째로 얼려 둔다** — 먼저 눈으로 보고 그 다음에 센다.
#   · `❯` 로 시작하는 줄은 **사용자 입력**이다(2026-08-19 에 그 오탐을 실제로 봤다). 뺀다.
PANE="${1:-%0}"
OUT="${2:-monitor/limit-banner.log}"
RAW="${OUT%.log}-raw.txt"
END=$(( $(date +%s) + 3600 ))
last=""
echo "$(date '+%F %T') «catch» 시작 pane=$PANE → $OUT (최대 60분)" >> "$OUT"
while [ "$(date +%s)" -lt "$END" ]; do
  snap=$(tmux capture-pane -p -S -60 -t "$PANE" 2>/dev/null)
  [ -z "$snap" ] && { echo "$(date '+%F %T') 🔴 pane 없음 — 중단" >> "$OUT"; exit 1; }
  hit=$(printf '%s\n' "$snap" | grep -a 'session limit\|resets [0-9]' | grep -av '❯' | tail -1)
  if [ -n "$hit" ] && [ "$hit" != "$last" ]; then
    echo "$(date '+%F %T') ✅ $hit" >> "$OUT"
    { echo "===== $(date '+%F %T') 화면 전체 ====="; printf '%s\n' "$snap"; echo; } >> "$RAW"
    last="$hit"
  fi
  sleep 2
done
echo "$(date '+%F %T') «catch» 60분 경과 — 종료(자연 종료 표지)" >> "$OUT"
