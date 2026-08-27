#!/usr/bin/env bash
# team-status.sh — 팀 현황 한 장. 루트 에이전트가 관리감독에 쓰는 기본 도구.
#
#   AGENTS     에이전트별 기동 상태 + 등록 여부 + 미결 요청 수
#   OPEN       날짜를 가로지르는 미결 요청 목록
#   STALLED    claimed 인데 오래 조용한 요청 — 멈춘 담당을 찾는 유일한 신호
#   DECISIONS  .team/DECISIONS.md 의 "사용자 결정 대기" 제목 줄
#
# 사용법
#   team/bin/team-status.sh                한 번 그리고 끝. **자르지 않는다**(원문 그대로)
#   team/bin/team-status.sh --watch [초]   반복 렌더(기본 10초). tmux 상태 pane 이 이것을 쓴다
#
# ── 인수인계 · 2026-08-25 REQ-0411 (임시 tmux-engineer 가 남긴다) ────────────────
# 🔴 RECENT(최근 이벤트) 절은 **삭제됐다. 되살리지 마라** — 사용자 지시다.
#    루트는 요청 이벤트를 메시지로 이미 다 받는다. 화면에 있어야 하는 것은
#    "사용자가 답해야 끝나는 것" 뿐이라 그 자리를 DECISIONS 가 차지했다.
#    원장 자체는 그대로 있다 → requests/INDEX.md (`tail` 로 언제든 본다)
# 🔴 깜빡임의 원인은 이 스크립트가 아니라 **그리는 순서**였다 → 아래 `--watch` 주석
# 🔑 고칠 자리 안내
#    · DECISIONS 형식이 바뀌어 항목이 안 보이면 → 이 파일의 DECISIONS 절 파서 한 곳
#    · 화면이 넘치거나 잘리는 게 마음에 안 들면  → `_fit`(폭·높이 자르기) 한 곳
#    · 새로 그릴 절을 넣으려면 → 아래 본문에 `echo` 절을 하나 더 붙이면 된다.
#      `--watch` 는 이 스크립트의 **출력만** 보므로 렌더 쪽은 손댈 필요가 없다
# ────────────────────────────────────────────────────────────────────────────────
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

# 사용자 결정 대기 원장. 루트가 관리한다 — 여기서는 **읽기만** 한다.
# 🔑 경로를 환경변수로 바꿀 수 있게 둔 이유: 아래 DECISIONS 절은 네 가지 상태를 갈라
#    찍는데, 그것이 정말 갈라지는지 보려면 **일부러 깨진 파일을 물려 봐야** 한다.
#    §"검사가 통과했다고 말하기 전에 한 번은 빨간불을 내 봐라".
#      DECISIONS_MD=/없는/경로 bash team/bin/team-status.sh
DECISIONS_MD="${DECISIONS_MD:-$PROJECT_DIR/.team/DECISIONS.md}"

# `-n <수>` 는 RECENT 전용 옵션이었다. 절이 사라져 하는 일이 없지만 문서와 손버릇에
# 남아 있다 → 인수를 먹고 조용히 무시한다. 여기서 오류로 죽으면 상태판이 통째로 빈다.
if [ "${1:-}" = "-n" ]; then
  shift
  [ $# -gt 0 ] && shift
fi

# ══ 프레임 맞추기 ═══════════════════════════════════════════════════════════════
# _fit <cols> <rows> — 환경변수 TEAM_FRAME 의 한 프레임을 pane 안에 들어가게 자르고,
#                      각 줄 끝에 ESC[K(그 줄의 옛 꼬리 지우기)를 붙여 내보낸다.
#
# 🔴 왜 파이프(stdin)가 아니라 환경변수인가 — **이 파일이 이미 경고해 둔 함정**이다.
#   아래 AGENTS 절 주석과 같은 것: `python3 - <<'PYEOF'` 는 heredoc 이 **stdin 을
#   프로그램 소스로 점유**한다. 그래서 파이프로 넣은 자료는 `sys.stdin.read()` 로
#   절대 안 들어오고, 오류도 없이 **빈 문자열**이 나온다.
#   처음에 파이프로 짰다가 화면이 통째로 비었다(2026-08-25 실측). 증상이 "빈 화면"이라
#   고치려던 깜빡임과 구분이 안 됐다 — 같은 실수를 세 번째 반복하지 마라.
#
# 왜 자르나: 줄이 pane 폭을 넘으면 접혀서 줄 수가 늘고, 프레임이 pane 높이를 넘으면
#            **스크롤이 난다.** 스크롤이 나면 아래 `--watch` 의 "커서를 홈으로" 가
#            어긋나서 다음 프레임부터 화면이 어그러진다. 그래서 넘기지 않는다.
# 폭 계산  : 한글·전각·이모지는 2칸(East_Asian_Width W/F). ● ⚠ ▸ 같은 Ambiguous 는
#            이 표가 지금까지 맞춰 온 대로 1칸으로 센다.
# ⚠ 자르는 것은 **화면용**이다. 인수 없이 돌리면(=사람이 직접 실행) 원문이 그대로 나온다.
_fit() {
  python3 - "$1" "$2" <<'PYEOF'
import os, sys, unicodedata

cols = max(20, int(sys.argv[1]))
rows = max(5,  int(sys.argv[2]))
EOL  = "\x1b[K"          # 그 줄의 커서 이후만 지운다(새 줄이 더 짧을 때 옛 꼬리 제거)

def _w(ch):
    if unicodedata.combining(ch):
        return 0
    return 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1

def cut(s, limit):
    if sum(_w(c) for c in s) <= limit:
        return s
    out, n = [], 0
    for ch in s:
        w = _w(ch)
        if n + w > limit - 1:      # … 자리 한 칸을 남긴다
            break
        out.append(ch); n += w
    return "".join(out) + "…"

lines = os.environ.get("TEAM_FRAME", "").split("\n")   # ⚠ stdin 이 아니다. 위 주석 참조
if lines and lines[-1] == "":
    lines.pop()

keep = rows - 1                    # 마지막 줄까지 채우면 그 자리에서 스크롤이 난다
body = [cut(l, cols - 1) for l in lines[:keep]]
if len(lines) > keep and body:
    body[-1] = cut("  … %d줄 더 있다 — pane 을 넓히거나 team/bin/team-status.sh 를 직접 실행해라"
                   % (len(lines) - keep + 1), cols - 1)
# 줄마다 "본문 + ESC[K" 로 끝내고 개행으로 잇는다.
# ⚠ EOL 로만 join 하면 개행이 없어 한 줄로 이어 붙는다(2026-08-25 실측: 화면이 뭉갰다).
sys.stdout.write("\n".join(l + EOL for l in body))
PYEOF
}

# ══ --watch : 깜빡이지 않는 반복 렌더 ═══════════════════════════════════════════
#
# 🔴 왜 `clear` 를 쓰지 않나 — 사용자가 지적한 깜빡임의 정체가 그것이었다(REQ-0411)
#   예전 pane 명령: `while :; do clear; team-status.sh; sleep 10; done`
#   `clear` 가 **먼저** 돌고 그 다음에 이 스크립트가 git·파일·프로세스를 훑는다.
#   그 사이(실측 2.0초) 화면이 비어 있었다. 10초 주기 중 약 1/6 이 깨진 화면이다.
#   실측 2026-08-25 · 0.25초 간격 60프레임: 미완성 14 · 그중 4프레임은 **한 줄뿐**이었다.
#
#   그래서 **먼저 만들고, 그 다음에 그린다**:
#     ① out="$(전체 렌더)"   ← 이 2초 동안 화면은 **이전 프레임 그대로** 남아 있다
#     ② ESC[H                ← 커서만 홈으로. 지우지 않는다
#     ③ 줄마다 ESC[K         ← 새 줄이 더 짧을 때 그 줄의 옛 꼬리만 지운다(_fit 이 붙인다)
#     ④ 끝에 ESC[J           ← 줄 수가 줄었을 때 아래 남은 옛 줄을 지운다
#   ②~④ 는 **printf 한 번**으로 나간다 → 화면이 비는 순간이 없다.
#
# 🔑 매 주기 자기 자신을 다시 실행한다 → **이 파일의 렌더 절을 고치면 pane 을 다시
#    띄우지 않아도 다음 주기에 반영된다.** (단 이 `--watch` 루프 자체를 고쳤을 때는
#    루프가 이미 메모리에 있으므로 pane 재기동이 필요하다.)
if [ "${1:-}" = "--watch" ]; then
  interval="${2:-10}"
  printf '\033[2J\033[H'            # 시작할 때 딱 한 번만 비운다
  while :; do
    # 화면 크기는 매 주기 다시 잰다(pane 은 언제든 리사이즈된다).
    # ⚠ `tput cols` 는 stdout 이 파이프면 80 을 답할 수 있다(ncurses 가 ioctl 을 못 건다).
    #   그래서 제어 tty 에서 직접 읽는 stty 를 먼저 쓴다.
    sz="$(stty size </dev/tty 2>/dev/null)"
    rows="${sz%% *}"; cols="${sz##* }"
    case "$rows" in ''|*[!0-9]*) rows="$(tput lines 2>/dev/null)";; esac
    case "$cols" in ''|*[!0-9]*) cols="$(tput cols  2>/dev/null)";; esac
    case "$rows" in ''|*[!0-9]*) rows=40;; esac
    case "$cols" in ''|*[!0-9]*) cols=80;; esac

    out="$(bash "$BIN_DIR/team-status.sh")"
    frame="$(TEAM_FRAME="$out" _fit "$cols" "$rows")"
    # 🔴 렌더가 실패해도 **화면을 비우지 마라.** 빈 화면은 "괜찮다"와 "죽었다"가 같은
    #   모양이라 아무도 못 알아챈다 — 고장은 글자로 말해야 한다.
    [ -n "$frame" ] || frame="  🔴 상태판 렌더 실패 — bash team/bin/team-status.sh 를 직접 실행해 원인을 봐라"
    printf '\033[H%s\033[J' "$frame"
    sleep "$interval"
  done
fi

# ══ 여기부터가 한 프레임의 내용이다 ═════════════════════════════════════════════

echo "══ AGENTS ══════════════════════════════════════════════════"
# 파이썬을 heredoc 으로 넘기면 stdin 이 스크립트로 점유되어 파이프로 들어온 JSON 을
# 못 읽는다(초기 버전의 버그: 전원 살아 있는데 "미기동"으로 보였다).
# 그래서 목록 조회 자체를 teamctl.py 안에서 하도록 옮겼다.
python3 "$BIN_DIR/teamctl.py" agents-table

echo
echo "══ OPEN (미결 요청) ════════════════════════════════════════"
if [ -d "$OPEN_DIR" ] && [ -n "$(ls -A "$OPEN_DIR" 2>/dev/null)" ]; then
  bash "$BIN_DIR/req.sh" list
else
  echo "  (없음)"
fi

echo
echo "══ STALLED (조용한 클레임) ═════════════════════════════════"
# 왜 있는가: claimed 인데 updated 가 오래된 요청은 "일하는 중"과 "멈춤"이 구분되지 않는다.
# 2026-08-16 에 socket-engineer 가 계획만 내고 2시간 서 있었는데 아무도 몰랐다.
# 들어오는 보고는 눈에 띄지만 **침묵은 아무도 알려주지 않는다.** 그래서 침묵을 표로 만든다.
#
# ⚠ 인수는 원래 `$ROOT` 였는데 그런 변수는 정의된 적이 없다(늘 빈 문자열이었다).
#   cwd 가 프로젝트 폴더일 때만 우연히 맞았다 — 다른 곳에서 부르면 조용히 "(없음)" 이 된다.
#   §"분모 없는 0 은 건강처럼 보인다" 그대로다. 2026-08-25 에 $PROJECT_DIR 로 고쳤다.
python3 - "$PROJECT_DIR" <<'PYEOF'
import sys, os, glob, re, datetime
root = sys.argv[1] if len(sys.argv) > 1 else "."
now = datetime.datetime.now(datetime.timezone.utc).astimezone()
rows = []
seen = set()
for f in glob.glob(os.path.join(root, "requests", "*", "REQ-*.md")):
    if os.path.islink(f):
        continue   # requests/open/ 은 심볼릭 링크라 원본과 중복된다
    head = open(f, encoding="utf-8", errors="replace").read(1200)
    def fm(k):
        m = re.search(rf"^{k}:\s*(.+)$", head, re.M)
        return m.group(1).strip() if m else ""
    if fm("status") != "claimed":
        continue
    try:
        up = datetime.datetime.fromisoformat(fm("updated"))
    except Exception:
        continue
    mins = int((now - up).total_seconds() // 60)
    rid = fm("id")
    if rid in seen:
        continue
    seen.add(rid)
    rows.append((mins, rid, fm("to"), fm("title")[:52]))
rows.sort(reverse=True)
if not rows:
    print("  (없음)")
for mins, rid, to, title in rows:
    mark = "🔴" if mins >= 60 else ("⚠ " if mins >= 30 else "  ")
    print(f"  {mark} {rid}  {mins:>4}분 조용  {to:<18} {title}")
if any(m >= 30 for m, *_ in rows):
    print()
    print("  ⚠ 30분 넘게 조용하면 확인하라 — 일하는 중일 수도, 멈춰 있을 수도 있다.")
    print("    담당은 team/bin/req.sh progress <ID> --by <이름> --note \"...\" 로 알려야 한다.")
PYEOF

echo
echo "══ DECISIONS (사용자 결정 대기) ════════════════════════════"
# 🔴 여기가 옛 RECENT 자리다. 사용자 지시(2026-08-25): "내 의사결정이 필요한 내용들만 출력".
#
# 원천은 `.team/DECISIONS.md` — **루트가 넣고 뺀다.** 이 스크립트는 읽기만 한다.
# 화면(우측 pane)이 좁으므로 **제목 줄(`## `)만** 뽑는다. 본문까지 그리면 넘친다.
#
# ⚠ 파서가 형식에 의존한다 → 형식이 바뀌면 조용히 빈 화면이 된다. 그래서 네 가지를
#   **갈라서** 찍는다: 파일없음 / 못읽음 / 형식어긋남 / 진짜 비어있음.
#   §"빈 것과 못 읽은 것을 갈라라" — 둘이 같은 모양이면 파일이 깨진 걸 영영 모른다.
# 🔑 형식을 바꿨다면 고칠 곳은 아래 `startswith("## ")` 한 줄이다.
# ⚠ 세는 것은 **제목 줄 수**이지 결정 항목 수가 아니다(한 줄이 `②~⑥` 처럼 여럿을 묶는다).
#   그래서 "N건" 이 아니라 "제목 N줄" 이라고 적는다 — §"숫자 둘을 비교하기 전에".
python3 - "$DECISIONS_MD" <<'PYEOF'
import sys, os

p = sys.argv[1]
if not os.path.exists(p):
    print("  (DECISIONS.md 없음 — 루트가 아직 만들지 않았다)")
    print("     %s" % p)
    raise SystemExit
try:
    raw = open(p, encoding="utf-8").read()
except Exception as e:
    # 🔴 "없음" 으로 뭉뚱그리지 마라. 못 읽은 것은 고장이지 상태가 아니다.
    print("  🔴 DECISIONS.md 를 **못 읽었다** — %s: %s" % (e.__class__.__name__, e))
    print("     빈 것이 아니다. 파일 권한·인코딩을 확인하라: %s" % p)
    raise SystemExit

items = [l.rstrip() for l in raw.splitlines() if l.startswith("## ")]
if not items:
    if raw.strip():
        print("  ⚠ 제목 줄('## ')을 못 찾았다 — 형식이 바뀌었나 (%d바이트 · %d줄)"
              % (len(raw.encode("utf-8")), len(raw.splitlines())))
        print("     고칠 곳: team/bin/team-status.sh 의 DECISIONS 절 파서")
    else:
        print("  결정 대기 없음")
    raise SystemExit

for l in items:
    print("  ▸ " + l[3:].replace("**", "").strip())
print()
print("  … 자세한 것은 .team/DECISIONS.md (제목 %d줄)" % len(items))
PYEOF
