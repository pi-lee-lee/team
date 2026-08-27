#!/bin/bash
# 루트 창을 **미결 REQ 가 도는 동안만** 주기적으로 깨운다.
#
#   team/bin/poke-loop.sh [간격초] [문장]        기본 1200초(20분)
#
# 🔴 **스스로 꺼진다.** 이것이 이 판의 핵심이다:
#   · 활성 REQ 가 **0** 이면 → 종료. 할 일이 없는데 깨우면 **토큰만 쓴다**
#   · **대기 표지**가 붙은 REQ 만 남아도 → 종료. 그것들은 사람이나 사건을 기다리는 것이지
#     에이전트가 도는 중이 아니다
#
# 대기 표지(제목 안) : [실기 뒤] · [게이트 · [대기] · [보류] · [우산] · [차단]
#   ⚠ 표지를 늘리려면 아래 DEFER 를 고쳐라.
#     🔑 낱말이 아니라 **"에이전트가 지금 돌 수 있나"** 로 판단해라 —
#        사람·부품·다음 실기를 기다리는 것이면 대기다.
#
# 중복 기동 안 한다(이미 돌면 그냥 나간다). 취소 : pkill -f poke-loop.sh
set -u
IVL="${1:-1200}"
MSG="${2:-20분 점검 — 에이전트 상태와 미결 REQ 를 확인하고 멈춘 것을 되살려라}"
PANE="${PANE_ID:-%0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 1

DEFER='\[실기 뒤\]|\[게이트|\[대기\]|\[보류\]|\[우산\]|\[차단\]'

# 이미 돌고 있으면 나간다 — 둘이 돌면 10분마다 깨우는 셈이 된다
if pgrep -f 'poke-loop\.sh' | grep -qv "^$$\$"; then
  echo "이미 돌고 있다 — 새로 안 띄운다"
  exit 0
fi

# 활성 REQ 수 = 미결 중 대기 표지가 없는 것
active() {
  team/bin/req.sh list 2>/dev/null | grep -E '^REQ-' | grep -Ev "$DEFER" | wc -l | tr -d ' '
}

n=$(active)
if [ "$n" -eq 0 ]; then
  echo "활성 REQ 0 — 띄우지 않는다"
  exit 0
fi
echo "활성 REQ ${n}건 — 점검 루프 시작(${IVL}초 간격)"

poke() {
  tmux send-keys -t "$PANE" "$1" 2>/dev/null || exit 0
  sleep 1
  tmux send-keys -t "$PANE" Enter 2>/dev/null || exit 0
}

while :; do
  s=0
  while [ "$s" -lt "$IVL" ]; do sleep 10; s=$((s + 10)); done   # 잘게 쪼갠다 — 죽일 때 반응이 빠르다
  n=$(active)
  if [ "$n" -eq 0 ]; then
    poke "점검 루프 종료 — 활성 REQ 0. 전부 닫혔거나 대기 표지만 남았다"
    exit 0
  fi
  poke "$MSG (활성 REQ ${n}건)"
done
