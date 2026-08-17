#!/usr/bin/env bash
# 지정 시각에 팀 루트 창으로 재개 프롬프트를 넣는다.
#
# 왜 이 방식인가 (2026-08-17):
#   · macOS 는 `atrun` 이 기본 비활성이라 `at` 이 **조용히 안 돈다**(실측: `atrun not found`).
#     조용히 안 도는 예약은 "예약했다고 믿는 것"이라 가장 나쁘다.
#   · `cron` 은 매일 도는 것이라 일회성 재개에 맞지 않고, 지운다는 것을 또 기억해야 한다.
#   · 그래서 `sleep` + `tmux send-keys` 다. **프로세스가 눈에 보인다** — `ps` 로 확인 가능하고
#     `kill` 로 취소된다. 예약이 살아 있는지 물어볼 수 있는 것이 이 방식의 값이다.
#
# ⚠ 이 스크립트는 **tmux 세션이 살아 있어야** 동작한다. 세션이 죽으면 넣을 창이 없다.
#    `--check` 로 지금 넣을 수 있는 상태인지 먼저 본다.
#
# 사용:
#   team/bin/resume-at.sh --check                 지금 상태만 본다(예약 안 한다)
#   team/bin/resume-at.sh --in 20 --msg "테스트"  20초 뒤 · 짧은 시험용
#   team/bin/resume-at.sh --at 00:20              그 시각에 기본 재개 프롬프트
#   team/bin/resume-at.sh --cancel                예약 취소
set -u

SESSION="learnteam"
TARGET_PANE="${SESSION}:0.0"          # 0번 창 왼쪽 = 루트 세션
PIDFILE="/tmp/learnteam-resume.pid"
LOGFILE="/tmp/learnteam-resume.log"

DEFAULT_MSG='docs/PLAN-QUEUE-2026-08-17.md 를 읽고 남은 작업을 순서대로 진행하라. ① monitor 에게 3차 ①(inj4 · 21:23:13~21:24:17 · 시도 51 중단) 판정 REQ 를 발행한다 — 표본 51 로 판정 가능한지도 monitor 판단이다. ② 창 B 배포: docs/net/RUNBOOK-winB-2026-08-17.md 대로 루트가 직접 친다(22f788d 백업 필수 · cd ~/parking-bin 필수). 배포 직후 왕복 최대 값을 arduino 에 넘긴다. ③ 3차 ②(inj5) → 창 B 판정. 그 뒤 커밋 정리와 설계 계획 REQ 발행. 관측은 내려가 있으므로 배포 전에 monitor 에게 새 t0 로 열라고 신호해야 한다.'

msg="$DEFAULT_MSG"
delay=""
mode=""

while [ $# -gt 0 ]; do
  case "$1" in
    --check)  mode=check; shift ;;
    --cancel) mode=cancel; shift ;;
    --in)     delay="$2"; shift 2 ;;
    --at)     mode=at; at_time="$2"; shift 2 ;;
    --msg)    msg="$2"; shift 2 ;;
    *) echo "알 수 없는 인자: $1" >&2; exit 2 ;;
  esac
done

# ── 상태 점검 ────────────────────────────────────────────────────────────
# 예약을 걸기 전에 **넣을 곳이 있는지** 본다. 없으면 예약은 조용히 실패한다.
check_target() {
  if ! tmux has-session -t "$SESSION" 2>/dev/null; then
    echo "🔴 tmux 세션 '$SESSION' 이 없다 — 넣을 창이 없다. team/team.sh 로 먼저 띄워라."
    return 1
  fi
  if ! tmux list-panes -t "$TARGET_PANE" >/dev/null 2>&1; then
    echo "🔴 대상 창 '$TARGET_PANE' 이 없다."
    return 1
  fi
  echo "🟢 대상 창 $TARGET_PANE 살아 있다."
  return 0
}

show_pending() {
  if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    echo "⏳ 예약 살아 있음 — pid $(cat "$PIDFILE")"
    ps -p "$(cat "$PIDFILE")" -o pid,etime,command 2>/dev/null | tail -1 | cut -c1-100
  else
    echo "· 대기 중인 예약 없음"
  fi
}

case "$mode" in
  check)
    check_target
    show_pending
    echo "· 로그: $LOGFILE"
    exit 0 ;;
  cancel)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
      kill "$(cat "$PIDFILE")" && echo "✅ 예약 취소 — pid $(cat "$PIDFILE")"
      rm -f "$PIDFILE"
    else
      echo "· 취소할 예약이 없다"
    fi
    exit 0 ;;
esac

# ── 지연 초 계산 ─────────────────────────────────────────────────────────
if [ "${mode:-}" = "at" ]; then
  # HH:MM → 지금부터의 초. 이미 지난 시각이면 다음 날로 넘긴다.
  delay=$(python3 - "$at_time" <<'PY'
import sys, datetime as dt
h, m = map(int, sys.argv[1].split(':'))
now = dt.datetime.now()
tgt = now.replace(hour=h, minute=m, second=0, microsecond=0)
if tgt <= now:
    tgt += dt.timedelta(days=1)
print(int((tgt - now).total_seconds()))
PY
)
  [ -z "$delay" ] && { echo "🔴 시각 해석 실패: $at_time" >&2; exit 1; }
fi

[ -z "$delay" ] && { echo "🔴 --in <초> 또는 --at HH:MM 이 필요하다" >&2; exit 2; }

check_target || exit 1

if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
  echo "🔴 이미 예약이 있다(pid $(cat "$PIDFILE")). --cancel 로 지우고 다시 걸어라." >&2
  exit 1
fi

fire_at=$(python3 -c "import datetime as dt,sys; print((dt.datetime.now()+dt.timedelta(seconds=int(sys.argv[1]))).strftime('%m-%d %H:%M:%S'))" "$delay")

# ── 예약 ─────────────────────────────────────────────────────────────────
# ⚠ send-keys 를 **두 번**에 나눈다: 본문을 넣고, 잠깐 쉬고, Enter 를 따로 보낸다.
#    한 번에 붙이면 붙여넣기 처리 중에 Enter 가 먹혀 프롬프트가 안 나가는 일이 있다.
(
  sleep "$delay"
  {
    echo "=== $(date '+%F %T') 재개 시도 ==="
    if tmux list-panes -t "$TARGET_PANE" >/dev/null 2>&1; then
      tmux send-keys -t "$TARGET_PANE" -l "$msg"
      sleep 1
      tmux send-keys -t "$TARGET_PANE" Enter
      echo "✅ 프롬프트 전송 완료 → $TARGET_PANE"
    else
      echo "🔴 대상 창이 사라졌다 — 전송 실패. 세션이 죽었는지 확인해라."
    fi
  } >> "$LOGFILE" 2>&1
  rm -f "$PIDFILE"
) &

echo $! > "$PIDFILE"
echo "✅ 예약됨 — $fire_at (${delay}초 뒤) · pid $(cat "$PIDFILE")"
echo "   대상: $TARGET_PANE"
echo "   로그: $LOGFILE"
echo "   취소: team/bin/resume-at.sh --cancel"
