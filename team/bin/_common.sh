#!/usr/bin/env bash
# _common.sh — 팀 스크립트 공용 경로/헬퍼. 다른 스크립트가 source 한다.
# 부수효과 없음(변수와 함수 정의만).

set -o pipefail

BIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEAM_DIR="$(cd "$BIN_DIR/.." && pwd)"
PROJECT_DIR="$(cd "$TEAM_DIR/.." && pwd)"

REQ_DIR="$PROJECT_DIR/requests"      # 요청 관리 폴더 — 날짜별 하위 폴더로 분리
SEQ_DIR="$REQ_DIR/.seq"              # 전역 단조 ID 원자적 할당용 락 디렉터리
OPEN_DIR="$REQ_DIR/open"             # 미결 요청 심볼릭 링크(날짜를 가로질러 한눈에)
INDEX="$REQ_DIR/INDEX.md"            # append-only 이벤트 원장

REG_DIR="$PROJECT_DIR/.claude/team"
REGISTRY="$REG_DIR/registry.json"    # session_id → 역할 (소유권 훅의 판정 근거)
AGENTS_DIR="$PROJECT_DIR/.claude/agents"
OWNERSHIP="$PROJECT_DIR/.claude/ownership.json"

# 에이전트 세션의 권한 모드.
#
# 사용자 결정(2026-08-13): **bypassPermissions** — 완전 무인 운전.
# 배경: acceptEdits 에서는 파일 편집만 자동 승인되고 셸·MCP 호출 일부가 승인 프롬프트에
# 걸린다. 백그라운드 세션은 TTY 가 없어 스스로 승인하지 못하므로 그 자리에서 멈추고,
# 사람이 알아채 재기동해야 진행된다 — "상시 기동·자율 수행"이 사실상 성립하지 않았다.
# (실제로 android 에이전트가 복합 셸 명령과 MCP 호출에서 두 번 멈췄다.)
#
# 대가: 권한 검사라는 2차 관문이 사라진다. 다만 **소유권 훅은 그대로 작동한다** —
# 훅은 권한 시스템과 별개 계층이고, 실제 에이전트 세션에서 차단이 일어나는 것을 확인했다.
# 즉 "무엇을 실행해도 되는가"는 열리지만 "어느 파일을 고칠 수 있는가"는 여전히 강제된다.
#
# 되돌리려면: TEAM_PERM=acceptEdits team/bin/team-up.sh --restart
TEAM_PERM="${TEAM_PERM:-bypassPermissions}"

die() { echo "[team] 오류: $*" >&2; exit 1; }
now_iso() { date '+%Y-%m-%dT%H:%M:%S%z'; }
today() { date '+%Y-%m-%d'; }

# 요청 ID 를 원자적으로 하나 할당한다.
#
# 왜 카운터 파일이 아니라 mkdir 인가: 두 에이전트가 동시에 요청을 발행하면
# "읽고 +1 하고 쓰기"는 같은 번호를 두 번 내준다. mkdir 은 POSIX 에서 원자적이라
# 진 쪽만 실패하고 다음 번호로 넘어간다 — 락 파일도 정리 대상도 없다.
alloc_id() {
  mkdir -p "$SEQ_DIR" || die "seq 디렉터리를 만들 수 없다: $SEQ_DIR"
  local n m
  n="$(ls -1 "$SEQ_DIR" 2>/dev/null | grep -E '^[0-9]+$' | sort -n | tail -1)"
  # .seq 는 빈 디렉터리라 git 이 추적하지 않는다 → clone/복원 후 사라져 있을 수 있다.
  # 그때 1번부터 다시 내주면 기존 REQ 파일과 번호가 충돌한다. 그래서 실제 요청
  # 파일에서 읽은 최대 번호와 비교해 항상 더 큰 쪽에서 출발한다.
  m="$(find "$REQ_DIR" -maxdepth 2 -name 'REQ-*.md' -type f 2>/dev/null \
        | sed -E 's#.*/REQ-0*([0-9]+).*#\1#' | sort -n | tail -1)"
  [ "${m:-0}" -gt "${n:-0}" ] 2>/dev/null && n="$m"
  n=$(( ${n:-0} + 1 ))
  while ! mkdir "$SEQ_DIR/$n" 2>/dev/null; do
    n=$(( n + 1 ))
  done
  printf 'REQ-%04d\n' "$n"
}

# req_file <REQ-0007> — ID 로 실제 md 파일 경로를 찾는다(날짜 폴더를 가로질러).
# open/ 안의 항목은 심볼릭 링크라 `-type f` 에 걸리지 않는다 → 중복 없이 원본만 나온다.
req_file() {
  local id="$1" f
  [ -n "$id" ] || return 1
  f="$(find "$REQ_DIR" -maxdepth 2 -name "$id-*.md" -type f 2>/dev/null | head -1)"
  [ -n "$f" ] || return 1
  printf '%s\n' "$f"
}

# front-matter 한 항목 읽기: fm_get <file> <key>
fm_get() {
  awk -v k="$2" '
    NR==1 && $0=="---" { infm=1; next }
    infm && $0=="---"   { exit }
    infm { if (index($0, k ":") == 1) { sub("^" k ": *", ""); print; exit } }
  ' "$1"
}

# front-matter 한 항목 교체: fm_set <file> <key> <value>
# (mktemp + mv 로 원자 교체 — 동시 갱신 중에도 반쪽 파일이 보이지 않는다)
fm_set() {
  local f="$1" k="$2" v="$3" tmp
  tmp="$(mktemp)" || return 1
  awk -v k="$k" -v v="$v" '
    NR==1 && $0=="---" { infm=1; print; next }
    infm && $0=="---"   { infm=0; print; next }
    infm && index($0, k ":") == 1 { print k ": " v; done=1; next }
    { print }
  ' "$f" >"$tmp" && mv "$tmp" "$f" || { rm -f "$tmp"; return 1; }
}

# INDEX.md 에 이벤트 한 줄 추가(append-only).
# 상태를 되쓰지 않고 사건만 쌓는 이유: 동시 갱신에도 줄이 섞이지 않고,
# "언제 무슨 일이 있었나"가 그대로 감사 기록이 된다. 현재 상태는 각 md 의
# front-matter 가 원천이고 open/ 링크가 미결 목록이다.
index_append() {
  mkdir -p "$REQ_DIR"
  if [ ! -f "$INDEX" ]; then
    {
      echo "# 요청 원장 (append-only)"
      echo
      echo "모든 요청 이벤트가 시간순으로 쌓인다. 현재 상태의 원천은 각 요청 md 의"
      echo "front-matter 이고, 미결 목록은 \`requests/open/\` 이다. 이 파일은 고쳐 쓰지 않는다."
      echo
      echo "| 시각 | 이벤트 | ID | 행위자 → 담당 | 제목 |"
      echo "|---|---|---|---|---|"
    } >"$INDEX"
  fi
  printf '| %s | %s | %s | %s → %s | %s |\n' \
    "$(now_iso)" "$1" "$2" "$3" "$4" "$5" >>"$INDEX"
}
