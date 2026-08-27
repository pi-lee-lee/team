#!/bin/bash
# 한계 해제 시각을 화면 흐름에서 뽑는다.
#
# 🔴 왜 파일 탐색이 아니라 화면인가 (2026-08-19 실측)
#    · 해제 시각은 ~/.claude 어디에도 안 남는다. **화면에만 있다**
#    · tmux 스크롤백도 못 쓴다 — Claude Code TUI 는 이력을 안 남긴다(실측 59줄)
#    → 그래서 폴링이 아니라 `tmux pipe-pane` 으로 **흐름을 통째로** 받는다
#      폴링은 배너가 떠 있는 순간을 놓칠 수 있다(§"계측 주기 > 사건 지속")
#
# 🔴 이 도구가 답하지 않는 것
#    · 배너가 안 뜬 동안은 **아무것도 못 찾는다.** 그건 "0" 이 아니라 "아직 없음"이다
#    · 흐름 부착 전에 뜬 배너는 못 잡는다. 부착 시각을 같이 찍는 이유다
STREAM="${1:-.claude/limit/pane0-stream.raw}"

# --at 2:50pm | 14:50  : 사람이 화면에서 본 시각을 넣는다.
#   🔑 이게 **보조가 아니라 주 경로다** — 배너가 매번 뜬다는 보장이 없다
#      (2026-08-19: 97% 는 떴고 98% 는 **안 떴다**)
if [ "$1" = "--at" ]; then
  raw="$2"; plus="${3:-2}"
  t=$(echo "$raw" | grep -ao '[0-9][0-9]*:[0-9][0-9]' | head -1)
  ap=$(echo "$raw" | grep -aoi '[ap]m' | head -1 | tr A-Z a-z)
  [ -z "$t" ] && { echo "🔴 시각을 못 읽었다: '$raw' (보기: 2:50pm 또는 14:50)"; exit 2; }
  h=${t%%:*}; m=${t##*:}; h=$((10#$h)); m=$((10#$m))
  [ "$ap" = "pm" ] && [ "$h" -lt 12 ] && h=$((h+12))
  [ "$ap" = "am" ] && [ "$h" = 12 ] && h=0
  fm=$((m+plus)); fh=$h
  [ "$fm" -ge 60 ] && fm=$((fm-60)) && fh=$(((fh+1)%24))
  fire=$(printf '%02d:%02d' "$fh" "$fm")
  echo "해제 $(printf '%02d:%02d' "$h" "$m")  + ${plus}분  →  발화 $fire"
  echo
  echo "nohup team/bin/poke-root.sh $fire \"작업재개하라. docs/STATUS-2026-08-19-B.md 를 먼저 읽어라.\" >/dev/null 2>&1 &"
  exit 0
fi

# --probe : 같은 오버레이 영역에 뜨는 **복사 토스트**를 찾는다.
#   🔑 사용자가 마음대로 띄울 수 있으므로 **배너를 기다리지 않고 방법을 검증**할 수 있다
#      (§"판별 시험은 고치기 전에" — 98% 를 기다리면 한 번뿐이고 놓치면 끝이다)
if [ "$1" = "--probe" ] || [ "$2" = "--probe" ]; then
  STREAM=".claude/limit/pane0-stream.raw"
  echo "== 오버레이 영역 포착 시험 =="
  LC_ALL=C sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' "$STREAM" | LC_ALL=C tr -cd '[:print:]\n' \
    | LC_ALL=C grep -aoi 'copied[^ ]*\|[0-9]* char[a-z]* copied\|복사[^ ]*\|clipboard' | sort | uniq -c | tail -5
  n=$(LC_ALL=C sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' "$STREAM" | LC_ALL=C tr -cd '[:print:]\n' | LC_ALL=C grep -aci 'copied\|복사')
  echo "  → 토스트 흔적 ${n}건 · 흐름 $(wc -c < "$STREAM" | tr -d ' ')B"
  [ "$n" -gt 0 ] && echo "  ✅ **오버레이 영역이 흐름에 들어온다 — 배너도 잡힌다**" \
                 || echo "  ⚠ 아직 없다. 화면에서 글자를 드래그해 복사 토스트를 띄워라"
  exit 0
fi
[ -f "$STREAM" ] || { echo "🔴 흐름 파일이 없다: $STREAM"; echo "   붙여라: tmux pipe-pane -o -t %0 'cat >> \$PWD/$STREAM'"; exit 2; }

hit=$(LC_ALL=C sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' "$STREAM" \
      | LC_ALL=C tr -cd '[:print:]\n' \
      | LC_ALL=C grep -ao "resets [0-9][0-9]*:[0-9][0-9][apm]*[^·]*" | tail -1)

if [ -z "$hit" ]; then
  echo "⚠ 아직 배너가 안 잡혔다 — **0건이 아니라 '아직 안 떴다'** 이다"
  echo "   흐름 크기: $(wc -c < "$STREAM" | tr -d ' ')B  ·  부착 이후만 담긴다"
  exit 1
fi
echo "✅ 잡힘: $hit"
t=$(echo "$hit" | grep -ao '[0-9][0-9]*:[0-9][0-9]' | head -1)
ap=$(echo "$hit" | grep -aoi '[ap]m' | head -1)
h=${t%%:*}; m=${t##*:}
[ "$(echo "$ap" | tr A-Z a-z)" = "pm" ] && [ "$h" -lt 12 ] && h=$((h+12))
echo "   해제 = $(printf '%02d:%02d' "$h" "$m")  →  발화 = $(printf '%02d:%02d' "$h" "$((10#$m+2))")"
echo
echo "nohup team/bin/poke-root.sh $(printf '%02d:%02d' "$h" "$((10#$m+2))") \"작업재개하라. docs/STATUS-2026-08-19-B.md 를 먼저 읽어라.\" >/dev/null 2>&1 &"
