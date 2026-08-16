#!/usr/bin/env bash
# team-status.sh — 팀 현황 한 장. 루트 에이전트가 관리감독에 쓰는 기본 도구.
#
#   AGENTS   에이전트별 기동 상태 + 등록 여부 + 미결 요청 수
#   OPEN     날짜를 가로지르는 미결 요청 목록
#   RECENT   최근 요청 이벤트(원장 꼬리)
#
# 사용법: team/bin/team-status.sh [-n <최근 이벤트 수>]
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

n=10
[ "${1:-}" = "-n" ] && n="${2:-10}"

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
python3 - "$ROOT" <<'PYEOF'
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
echo "══ RECENT (최근 이벤트) ════════════════════════════════════"
if [ -f "$INDEX" ]; then
  tail -n "$n" "$INDEX"
else
  echo "  (원장 없음 — 아직 요청이 발행되지 않았다)"
fi
