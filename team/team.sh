#!/usr/bin/env bash
# team.sh — 팀을 tmux 포그라운드로 기동한다.
#
# 창 구성
#   0: status   — team-status.sh 주기 갱신(누가 살아있고 무엇이 미결인지)
#   1..N: 에이전트 이름별 창 — 각 창에 대화형 claude 하나
#
# 왜 포그라운드인가
#   백그라운드(`claude --bg`)로 돌렸을 때 밟은 함정 셋 중 둘이 **bg 세션 전용 기본값**이었다.
#     · worktree 격리로 파일을 못 씀        → 대화형에는 해당 없음
#     · 루트가 보낸 메시지 미도달(계급 불일치) → 대화형은 루트와 같은 계급
#   남은 하나(승인 프롬프트)도 성격이 바뀐다: TTY 가 없어 **조용히 멈추던** 것이
#   창에 떠서 **사람이 답할 수 있는 대기**가 된다. 무인 운전은 어차피 불가능하므로
#   (분류기가 권한 모드와 무관하게 명령을 판정한다) 보이는 편이 낫다.
#
#   대가: tmux 세션이 죽으면 팀이 죽는다. 헤드리스가 필요하면 team-up.sh(bg)를 쓴다.
#
# 사용법
#   team/team.sh              기동(이미 있으면 attach)
#   team/team.sh --fresh      기존 세션 종료 후 새로 기동
#   team/team.sh --no-attach  기동만 하고 붙지 않음(스크립트용)
#
# 분리: Ctrl-b d · 재진입: team/team.sh · 창 이동: Ctrl-b <숫자> 또는 Ctrl-b w
#
# 루트 에이전트 창은 만들지 않는다. **네가 지금 쓰고 있는 터미널이 루트다.**
# 여기에 루트를 하나 더 띄우면 루트가 둘이 되어 흐름 제어 주체가 모호해진다.
source "$(dirname "${BASH_SOURCE[0]}")/bin/_common.sh"

TEAM_SESSION="${TEAM_SESSION:-learnteam}"

fresh=0; attach=1
while [ $# -gt 0 ]; do
  case "$1" in
    --fresh) fresh=1; shift;;
    --no-attach) attach=0; shift;;
    -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
    *) die "알 수 없는 옵션: $1";;
  esac
done

command -v tmux   >/dev/null || die "tmux 를 찾을 수 없다"
command -v claude >/dev/null || die "claude CLI 를 찾을 수 없다"

[ "$fresh" = 1 ] && tmux kill-session -t "$TEAM_SESSION" 2>/dev/null

roster="$(python3 "$BIN_DIR/teamctl.py" roster)"
[ -n "$roster" ] || die ".claude/agents/ 에 에이전트가 없다"
mkdir -p "$REG_DIR" "$REQ_DIR" "$OPEN_DIR"

echo "팀 기동(tmux) — 프로젝트: $PROJECT_DIR / 권한모드: $TEAM_PERM"

if ! tmux has-session -t "$TEAM_SESSION" 2>/dev/null; then
  # 0번 창: 상태판.
  tmux new-session -d -s "$TEAM_SESSION" -n status -c "$PROJECT_DIR" \
    "while :; do clear; bash '$BIN_DIR/team-status.sh'; sleep 10; done"
  # 크래시한 에이전트 창이 소리 없이 사라지면 관리감독이 안 된다 → 시신을 남긴다.
  tmux set-option -t "$TEAM_SESSION" -g remain-on-exit on 2>/dev/null || true
fi

# live_sid <이름> — 그 이름으로 살아있는 대화형 세션의 session_id (없으면 빈 문자열)
live_sid() {
  claude agents --cwd "$PROJECT_DIR" --json 2>/dev/null | python3 -c '
import json,sys
want=sys.argv[1]
try: rows=json.load(sys.stdin)
except Exception: rows=[]
for r in rows:
    if r.get("name")==want and r.get("kind")=="interactive":
        print(r.get("sessionId","")); break
' "$1"
}

# 에이전트별 멱등 복구.
#
# 단순히 "tmux 세션이 있으면 끝"으로 처리하면 안 된다. launchd 하트비트(30분)의
# 존재 이유가 **죽은 에이전트를 되살리는 것**인데, 세션만 보고 넘어가면 창 하나가
# 크래시해도 영원히 복구되지 않는다. 그래서 이름별로 살아있는지 각각 확인한다.
for name in $roster; do
  sid="$(live_sid "$name")"
  if [ -n "$sid" ]; then
    python3 "$BIN_DIR/teamctl.py" registry-set "$sid" "$name" "${sid%%-*}" >/dev/null
    echo "  · $name  이미 기동중 (${sid%%-*})"
    continue
  fi

  # 살아있지 않다 → 시신 창이 남아 있으면 치우고 새로 만든다.
  tmux kill-window -t "$TEAM_SESSION:$name" 2>/dev/null
  tmux new-window -d -t "$TEAM_SESSION" -n "$name" -c "$PROJECT_DIR" \
    "'$BIN_DIR/_agent_pane.sh' '$name'"

  # 세션 id 를 확보해 등록해야 소유권 훅이 역할을 판정할 수 있다
  # (등록 전에는 루트로 취급되어 자기 파일조차 못 고친다).
  for i in $(seq 1 40); do
    sid="$(live_sid "$name")"
    [ -n "$sid" ] && break
    sleep 1
  done
  if [ -n "$sid" ]; then
    python3 "$BIN_DIR/teamctl.py" registry-set "$sid" "$name" "${sid%%-*}" >/dev/null
    echo "  ✓ $name  기동 (${sid%%-*})"
  else
    echo "  ✗ $name  세션 id 확보 실패 — 소유권 미적용 상태다. 창을 열어 확인하라."
  fi
done

tmux select-window -t "$TEAM_SESSION:0"
echo
echo "창 이동: Ctrl-b <숫자> / Ctrl-b w   ·   분리: Ctrl-b d"
# `[ ... ] && exec` 로 끝내면 --no-attach 일 때 마지막 테스트가 거짓이라
# 스크립트가 종료코드 1 을 낸다(정상 기동인데 실패로 보인다).
if [ "$attach" = 1 ]; then
  exec tmux attach -t "$TEAM_SESSION"
fi
exit 0
