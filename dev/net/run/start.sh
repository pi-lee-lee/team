#!/bin/sh
# start.sh — 서버를 **세션과 무관하게** 띄우고 로그를 파일로 남긴다.
#
# ═══════════════════════════════════════════════════════════════════════════
# 🔴 이 스크립트가 없애는 함정 셋 (전부 실측으로 나왔다 · 2026-08-25)
#
#   ① **세션에 묶인 프로세스** — `run_in_background` 로 띄우면 ppid 가 내 세션이다.
#      세션이 죽으면 서버도 죽고, 그러면 **로그 파일은 남는데 그 뒤가 안 남는다.**
#      → `nohup` + `&` 로 분리한다. 판별자는 **`ps -o ppid=` 가 `1`** 이다.
#
#   ② **파일 이름이 t0 와 어긋난다** — 사람이 손으로 이름을 적으면 실제 기동과 벌어진다.
#      실측: 이름 `0950` · 실제 `09:48:07` — **1분 53초.** monitor 가 그것을 t0 로 읽을 뻔했다.
#      → 이름을 `date` 로 만든다. 오차가 초 단위로 줄어든다.
#      ⚠ 그래도 **정본은 로그 첫 줄**이다. 이름은 찾기 쉬우라고 있는 것이지 근거가 아니다.
#
#   ③ **로그가 조각난다** — 재기동할 때마다 파일이 하나 더 생기고, 이으면 경계에서
#      세션 번호·uptime 이 리셋되는데 **그게 안 보인다.**
#      → 헤더에 **이전 로그 경로**를 적는다. 조각이 스스로 이어진다.
#      🔑 pid 는 서버가 자기 기동 줄에 이미 찍는다(`개발용 주차 서버 (pid …)`).
#
# 쓰는 법 :  net/run/start.sh [서버 인자…]
#            인자를 안 주면 기본 포트(9900/9991/5500)로 뜬다.
# ═══════════════════════════════════════════════════════════════════════════
set -e

# 🔑 **자리 기준(SCRIPT_DIR)과 로그 자리(RUN_DIR)를 가른다.**
#   전에는 하나였는데, 그러면 **회전을 시험할 방법이 없다** — 시험이 실제 판정 원자료를 gzip 한다.
#   ★ 오늘(2026-08-27) 그 이유로 시험을 못 할 뻔했다. **시험할 수 없는 처방은 검증되지 않은 처방이다.**
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
RUN_DIR="${RUN_DIR:-$SCRIPT_DIR}"        # ⚠ 시험할 때만 바꾼다. 운영은 기본값이다

# 🔑 트리를 바꿔 띄울 수 있다 — `SRV_TREE=five_connect_test net/run/start.sh`
# 🔴🔴 **`summary` 가 지금(2026-08-27~) 정본이다.** `서머리/server` 트리다.
#   ⚠ 전에는 이 스크립트에 **그 갈래가 아예 없었다** — 그래서 도는 서버는 **수동 기동**이었고,
#   ★ 그 결과 **여기 있는 로그 회전·기동 헤더·prev-log 가 한 번도 안 돌았다.**
#     사용자가 *"로그 용량이 크다"* 고 해서 넣은 처방이 **호출 경로가 없어 죽어 있었다.**
#   🔑 §"만들어 놓고 호출 경로를 안 봤다" — 같은 날 `soak_line()` 에서도 같은 것을 찾았다.
SRV_TREE="${SRV_TREE:-summary}"
case "$SRV_TREE" in
    # ⚠ `BIN` 을 덮을 수 있게 둔다 — **배포 전 새 빌드를 같은 절차로 시험**하기 위해서다.
    #   ★ 안 그러면 시험이 **옛 바이너리**를 띄우고, 그러면 *"절차는 됐는데 기능이 안 보인다"* 가
    #     나온다(오늘 실제로 한 번 그랬다 — 45초를 기다리고서야 알았다).
    summary)      SRV_DIR="$REPO_DIR/서머리/server" ; BIN="${BIN:-/tmp/srv-summary}" ;;
    server_multi) SRV_DIR="$REPO_DIR/조별과제샘플/VS_server_multi/$SRV_TREE" ; BIN="$SCRIPT_DIR/srv" ;;
    *)            SRV_DIR="$REPO_DIR/조별과제샘플/VS_server_multi/$SRV_TREE" ; BIN="$SCRIPT_DIR/srv-$SRV_TREE" ;;
esac

if [ ! -x "$BIN" ]; then
    echo "🔴 $BIN 이 없다. 먼저 빌드해라:"
    echo "   cd \"$SRV_DIR\" && c++ -std=c++11 -O1 server.cpp lot.cpp -o \"$BIN\""
    exit 1
fi

# 🔑 **이전 로그를 먼저 찾는다** — 새 파일을 만들기 전에. 안 그러면 자기 자신을 가리킨다.
PREV="$(ls -t "$RUN_DIR"/srv-*.log 2>/dev/null | head -1 || true)"
LOG="$RUN_DIR/srv-$(date '+%Y-%m-%d-%H%M').log"

# ═══ 🔴🔴 **로그 회전 — 기동 시점에만 한다** (REQ-0499 ① · 2026-08-27) ═══════════
#
#   왜 여기인가 : 🔴 **도는 프로세스의 로그를 건드리면 안 된다.**
#     이 스크립트는 `nohup "$BIN" >> "$LOG"` 로 **셸이 fd 를 연다.** 그 파일을 `mv` 하면
#     프로세스는 **옮겨진 inode 에 계속 쓴다** — 새 파일은 영영 안 생긴다.
#     ⚠ 08-16 에 그 기전으로 **113KB 를 통째로 잃었다**(`runtime.h` 의 `g_logfile` 주석).
#     ✅ **기동 시점에는 앞 서버가 이미 내려가 있다.** 그때만 안전하다.
#
#   ⚠ 이 트리(`VS_server_multi`)는 서버가 파일을 안 연다 — 출력은 stdout 이고
#     **fd 를 쥐는 것은 셸**이다. 그래서 in-process 회전이 아니라 **여기**가 자리다.
#
#   🔴 **삭제하지 않는다. gzip 한다.** 로그는 한 번 지우면 못 되돌린다.
#     그리고 **최근 것은 건드리지 않는다** — monitor 의 판정이 평문 원자료를 읽는다.
#     (실측 2026-08-27 : 확정 기저선이 `srv-2026-08-26-*.log` **여섯 파일**에 걸쳐 있었다)
ROT_KEEP_DAYS="${ROT_KEEP_DAYS:-3}"      # 이 일수 안의 로그는 **평문 그대로** 둔다
ROT_TOTAL_MB="${ROT_TOTAL_MB:-40}"       # 폴더 합이 이걸 넘을 때만 압축을 시작한다

rotate_logs() {
    _total_kb="$(du -sk "$RUN_DIR" 2>/dev/null | awk '{print $1}')"
    [ -z "$_total_kb" ] && return 0
    if [ "$_total_kb" -le $((ROT_TOTAL_MB * 1024)) ]; then return 0; fi
    _n=0
    # 🔑 `-mtime +N` 이 아니라 **`find -mtime`** 을 쓰는 이유 : 파일명의 날짜는
    #   **기동 시각**이고, 한 판이 자정을 넘기면 이름과 내용이 갈린다. mtime 이 사실에 가깝다.
    for f in $(find "$RUN_DIR" -maxdepth 1 -name 'srv-*.log' -mtime "+$ROT_KEEP_DAYS" 2>/dev/null); do
        gzip -9 "$f" 2>/dev/null && _n=$((_n + 1))
    done
    if [ "$_n" -gt 0 ]; then
        # 🔴 **회전 사실을 새 로그에 남긴다.** 안 남기면 다음 사람이 *"왜 잘렸나"* 를 못 풀고,
        #   monitor 의 파서는 **파일이 바뀐 것**과 **사건이 없는 것**을 못 가른다.
        ROT_NOTE="# 🗜 로그 회전 : ${ROT_KEEP_DAYS}일 넘은 srv-*.log **${_n}개를 gzip** 했다"
        ROT_NOTE="$ROT_NOTE (폴더 $((_total_kb / 1024))MB > 상한 ${ROT_TOTAL_MB}MB). **삭제 아님** — .gz 로 있다"
    fi
}
rotate_logs

# 🔴🔴 **포트를 인자로 안 준다 — `config.h` 가 유일한 원천이 되게 한다.**
#   arduino 의 굽기 검사(`arduino/.burn/check-net.py`)가 `config.h` 의 `PORT_ARDUINO` 를 읽어
#   펌웨어 산출물과 대조하고, 갈리면 **굽기를 막는다**(REQ-0435).
#   ⚠ 여기서 `--port-ardu=` 로 덮으면 **그 검사가 보는 값과 실제 값이 갈린다** —
#     검사는 초록인데 칩이 없는 포트로 붙으러 간다. 증상은 *"안 붙는다"* 하나뿐이다.
#   🔑 기본값은 이미 우리가 쓰는 값과 같다: 아두이노 9991 · 웹 9900 · 폰 5500
if [ $# -gt 0 ]; then
    echo "⚠ 포트를 인자로 덮는다 — arduino 굽기 검사는 config.h 기본값을 읽는다."
    echo "   값이 갈리면 검사는 통과하는데 장치가 못 붙는다. 시험 목적이 아니면 인자를 빼라."
fi

# 🔴🔴 **`prev-log` 는 "파일이 이어진다" 이지 "같은 조립표가 이어진다" 가 아니다** (monitor).
#   트리가 바뀌면 사슬만 따라가는 사람에게 **다른 시스템의 로그가 하나로 보인다.**
#   🔑 지금도 `# cwd` 를 대조하면 알 수 있지만 — **대조를 안 하는 사람이 사슬을 따라간다.**
#   그래서 바뀐 그 순간에 **눈에 띄는 한 줄**을 찍는다. 한 줄이면 그 사람이 멈춘다.
PREV_TREE=""
if [ -n "$PREV" ]; then
    PREV_TREE="$(sed -n 's|^# cwd *: .*/VS_server_multi/||p' "$PREV" | head -1)"
fi

{
    echo "# ==== srv start $(date '+%F %T') ===="
    echo "# prev-log : ${PREV:-none}"
    echo "# cwd      : $SRV_DIR"
    if [ -n "$PREV_TREE" ] && [ "$PREV_TREE" != "$SRV_TREE" ]; then
        echo "# ⚠⚠ TREE CHANGED : $PREV_TREE → $SRV_TREE"
        echo "#     🔴 앞 로그와 **조립표가 다르다.** 사슬을 이어 읽지 마라 —"
        echo "#        자리 수·모듈 구성·흐름이 갈린다. 수치를 합치면 틀린다"
    fi
    # 🔑 **버릴 트리는 로그 자신이 말한다** — 파일명·README 는 로그를 옮기면 떨어져 나간다
    #   (오늘 시험 트래픽 표지를 로그 안에 넣은 것과 같은 이유다)
    # 🔴 **`summary` 를 여기 빠뜨리지 마라.** 빠지면 지금 정본 트리의 로그에
    #   *"임시 시험용이다. 판정에 인용하지 마라"* 가 찍힌다 — **멀쩡한 판정 자료가 스스로를 부정한다.**
    #   ★ 갈래를 더할 때는 **그 갈래를 아는 자리를 전부 세라**(오늘 이 줄을 하마터면 놓쳤다).
    if [ "$SRV_TREE" != "summary" ] && [ "$SRV_TREE" != "server_multi" ] && [ "$SRV_TREE" != "five" ]; then
        echo "# 🔴 TEMP TREE : $SRV_TREE — **임시 시험용이다. 판정에 인용하지 마라**"
    fi
    [ -n "$ROT_NOTE" ] && echo "$ROT_NOTE"
    echo "# args     : $*"
    echo "# 🔑 pid 는 아래 서버 기동 줄에 있다. 이 파일의 t0 는 **첫 서버 줄의 시각**이다"
} > "$LOG"

cd "$SRV_DIR"
# 🔴🔴 **`--web-root` 를 명시로 넘긴다**(REQ-0497 · 2026-08-27).
#   서버는 이제 **cwd 로 화면 뿌리를 정하지 않는다** — 기본값은 `<실행파일>/../web` 인데
#   개발 배치는 바이너리가 `/tmp` 에 있어 그 기본값이 안 맞는다(`/tmp/web` 은 없다).
#   ⚠ 이 줄이 없으면 **9900·8080·8081 화면이 전부 404** 가 된다. 서버는 살아 있어서
#     *"배포는 됐는데 화면만 안 나온다"* 로 보인다 — **가장 헷갈리는 실패 모양**이다.
#   🔑 릴리즈 배치(`릴리즈/server/srv` + `릴리즈/web/`)에서는 기본값이 맞으므로 이 인자가 필요 없다.
nohup "$BIN" --web-root="$SRV_DIR" --data-dir="$SRV_DIR" "$@" >> "$LOG" 2>&1 &
NEWPID=$!

echo "떴다 — pid $NEWPID"
echo "  로그 : $LOG"
echo "  이전 : ${PREV:-none}"
echo "🔴 확인해라 : ps -o ppid= -p $NEWPID   → **1 이어야** 세션과 무관하다"
