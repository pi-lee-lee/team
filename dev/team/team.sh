#!/usr/bin/env bash
# team.sh — 이 폴더에서 팀을 통째로 기동한다. **진입점은 이것 하나다.**
#
# 창 구성
#   0: root   — 좌: 루트 세션(여기에 작업을 지시한다) / 우: 상태판
#   1..N: 에이전트 이름별 창 — 각 창에 대화형 claude 하나
#
# 수명주기: 팀 전체가 이 tmux 세션 하나 안에 있다. **세션이 끝나면 전원 종료된다.**
#   OS 부팅 시 자동 기동 같은 건 하지 않는다 — 에이전트는 작업 세션이 사는 동안만
#   상주하면 된다(작업마다 워커를 만들고 죽이지 않는다는 뜻이지, 머신이 켜져 있는
#   내내 떠 있으라는 뜻이 아니다).
#   끝낼 때: team/bin/stop.sh  (또는 tmux kill-session -t learnteam)
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
# 종료: team/bin/stop.sh
#
# 루트는 0번 창 좌측 패널이다. 루트를 이 세션 밖(별도 터미널)에서 또 띄우면
# 흐름 제어 주체가 둘이 되므로 그러지 마라 — 지시는 언제나 0번 창에서 한다.
source "$(dirname "${BASH_SOURCE[0]}")/bin/_common.sh"   # TEAM_SESSION 포함

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
  # 0번 창 = 루트. 좌측이 루트 세션(여기에 작업을 지시한다), 우측이 상태판.
  #
  # 팀 전체가 이 tmux 세션 하나 안에 있다는 점이 중요하다. 세션을 끝내면 루트도
  # 에이전트도 함께 끝난다 — "작업 세션이 사는 동안만 상주"가 구조로 보장된다.
  # 루트는 래퍼로 띄운다. `claude` 를 직접 띄우면 /exit 로 루트가 끝나도 에이전트 창이
  # 남아 "루트 없이 에이전트만 떠 있는" 상태가 된다(_root_pane.sh 주석 참조).
  tmux new-session -d -s "$TEAM_SESSION" -n root -c "$PROJECT_DIR" "'$BIN_DIR/_root_pane.sh'"
  # 🔴 상태판 pane — `clear` 를 쓰지 마라 (2026-08-25 REQ-0411 · 사용자가 깜빡임을 지적했다)
  #   예전: "while :; do clear; team-status.sh; sleep 10; done"
  #   `clear` 가 **먼저** 돌고 그 다음 스크립트가 2.0초 동안 git·파일·프로세스를 훑는다.
  #   그 2초 동안 화면이 비어 있다 — 그것이 깜빡임이었다(실측: 60프레임 중 14가 미완성).
  #   지금은 team-status.sh --watch 가 **먼저 만들고 그 다음에 덮어쓴다**(ESC[H/K/J).
  #   왜 그 로직이 여기가 아니라 team-status.sh 안에 있나:
  #     ① 이 pane 명령은 tmux 로 넘어가는 **문자열**이라 여러 줄 로직을 담기에 위험하다
  #     ② 그 안에 두면 **team-status.sh 를 고쳐도 pane 재기동이 필요 없다**
  #        (--watch 가 매 주기 스크립트를 다시 실행한다)
  # ⚠ 반대로 **이 줄을 고치면 이미 도는 pane 에는 반영되지 않는다.** while 루프가
  #   이미 메모리에 있기 때문이다. 적용하려면 그 pane 만 다시 띄운다:
  #     tmux kill-pane -t learnteam:root.1
  #     tmux split-window -h -d -t learnteam:root -l 32% -c "$PWD" \
  #       "bash team/bin/team-status.sh --watch 10"
  tmux split-window -h -d -t "$TEAM_SESSION:root" -l "$TEAM_STATUS_WIDTH" -c "$PROJECT_DIR" \
    "bash '$BIN_DIR/team-status.sh' --watch 10"
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
