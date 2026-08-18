#!/bin/bash
# 지정 시각까지 기다렸다가 재개 준비를 한다.
#
#   team/bin/resume-at.sh            # 04:50 까지 대기 후 점검
#   team/bin/resume-at.sh 06:30      # 시각 지정
#   team/bin/resume-at.sh 04:50 --up # 점검 후 팀까지 기동
#
# ⚠ 이 스크립트는 루트 에이전트를 대신 지시하지 못한다.
#   환경을 점검하고 **무엇부터 할지**를 찍어 줄 뿐이다. 지시는 사람이 한다.
set -u
cd "$(dirname "$0")/../.." || exit 1

TARGET="${1:-04:50}"
UP=""
[ "${2:-}" = "--up" ] && UP=1
[ "${1:-}" = "--up" ] && { UP=1; TARGET="04:50"; }

# ── 목표 시각까지 대기 (지났으면 다음 날)
now=$(date +%s)
today=$(date -j -f "%Y-%m-%d %H:%M" "$(date +%F) $TARGET" +%s 2>/dev/null) || {
  echo "시각 형식이 틀렸다: $TARGET (예: 04:50)"; exit 1; }
goal=$today
[ "$goal" -le "$now" ] && goal=$((today + 86400))

echo "지금 $(date '+%F %H:%M:%S') · 목표 $(date -r "$goal" '+%F %H:%M') · $(( (goal-now)/60 ))분 대기"
while [ "$(date +%s)" -lt "$goal" ]; do sleep 20; done

echo
echo "════════ 재개 점검 $(date '+%F %H:%M:%S') ════════"

# ── 1) 서버
srv=$(pgrep -f srv_parking | head -1)
if [ -n "$srv" ]; then
  echo "서버   ● pid $srv · $(ps -p "$srv" -o etime= | tr -d ' ') 가동"
  grep -a '서버 인스턴스 id' ~/parking-logs/parking-server.log 2>/dev/null | tail -1 | sed 's/^/       /' | cut -c1-90
else
  echo "서버   ○ 죽어 있다 — 재기동 필요 (socket 에 지시)"
fi

# ── 2) 장치 링크
last=$(grep -a '⏱ 소크' ~/parking-logs/parking-server.log 2>/dev/null | tail -1)
[ -n "$last" ] && echo "링크   $(echo "$last" | grep -oE '세션 [0-9]+회|연결중 [0-9:]+|프레임 [0-9]+' | tr '\n' ' ')"
grep -a '+ARD' ~/parking-logs/parking-server.log 2>/dev/null | tail -1 | sed 's/^/       /' | cut -c1-90

# ── 3) 보드·포트 (⚠ 이름이 재연결마다 바뀐다)
board=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
echo "보드   ${board:-없음}"

# ── 4) 팀
echo "에이전트"
team/bin/team-status.sh 2>/dev/null | grep -aE '● |⏸ ' | sed 's/^/       /' | head -8

# ── 5) 다음 할 일 — 인수인계 문서에서 그대로 꺼낸다
echo
echo "════════ 재개하면 이 순서 ════════"
awk '/^### 재개하면 이 순서/{f=1} f&&/^```$/{n++} f{print} f&&n==2{exit}' \
  docs/HANDOFF-2026-08-18-move.md 2>/dev/null | sed 's/^/  /'

echo
echo "  전문: docs/HANDOFF-2026-08-18-move.md"

if [ -n "$UP" ]; then
  echo
  echo "════════ 팀 기동 ════════"
  team/team.sh
else
  echo
  echo "  팀을 띄우려면: team/team.sh   (또는 이 스크립트에 --up)"
fi
