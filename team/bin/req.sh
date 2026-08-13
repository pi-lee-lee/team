#!/usr/bin/env bash
# req.sh — 에이전트 간 md 요청 프로토콜.
#
# 이 팀은 문장으로 일을 주고받지 않는다. 요청은 언제나 md 파일 하나로 정리되고,
# 상대에게는 "그 파일을 읽어라"는 포인터만 보낸다. 요청과 처리 결과가 같은 파일에
# 남으므로 나중에 누가 무엇을 왜 요청했는지 그대로 재구성된다.
#
# 사용법
#   req.sh new --from <나> --to <상대> --title "<제목>" [--files a,b] [--parent REQ-0003]
#              [--body "<본문>"] [--why "<이유>"] [--accept "<완료 기준>"]
#   req.sh list [--to X] [--from X] [--status open|claimed|done|rejected] [--all]
#   req.sh path <REQ-0007>          # md 파일 절대경로
#   req.sh show <REQ-0007>          # 내용 출력
#   req.sh claim <REQ-0007> --by <나>
#   req.sh done  <REQ-0007> --by <나> [--note "<처리 요약>"]
#   req.sh reject <REQ-0007> --by <나> --reason "<사유>"
#
# 저장 위치:  requests/<YYYY-MM-DD>/REQ-0007-<from>-to-<to>-<슬러그>.md
# 미결 목록:  requests/open/REQ-0007.md  (원본을 가리키는 심볼릭 링크)
# 원장:       requests/INDEX.md          (append-only 이벤트 로그)
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

usage() { sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

slug() {
  printf '%s' "$1" \
    | tr '[:upper:]' '[:lower:]' \
    | sed -e 's/[^a-z0-9가-힣]\{1,\}/-/g' -e 's/^-//' -e 's/-$//' \
    | cut -c1-40
}

cmd_new() {
  local from="" to="" title="" files="" parent="" body="" why="" accept=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --from) from="$2"; shift 2;;
      --to) to="$2"; shift 2;;
      --title) title="$2"; shift 2;;
      --files) files="$2"; shift 2;;
      --parent) parent="$2"; shift 2;;
      --body) body="$2"; shift 2;;
      --why) why="$2"; shift 2;;
      --accept) accept="$2"; shift 2;;
      *) die "알 수 없는 옵션: $1";;
    esac
  done
  [ -n "$from" ] && [ -n "$to" ] && [ -n "$title" ] || die 'new 에는 --from --to --title 이 모두 필요하다'

  local id day dir f files_yaml
  id="$(alloc_id)"; day="$(today)"; dir="$REQ_DIR/$day"
  mkdir -p "$dir" "$OPEN_DIR" || die "요청 폴더를 만들 수 없다"
  f="$dir/$id-$from-to-$to-$(slug "$title").md"

  files_yaml="[]"
  if [ -n "$files" ]; then
    files_yaml="[$(printf '%s' "$files" | sed -e 's/ *, */", "/g' -e 's/^/"/' -e 's/$/"/')]"
  fi

  {
    echo "---"
    echo "id: $id"
    echo "title: $title"
    echo "from: $from"
    echo "to: $to"
    echo "status: open"
    echo "created: $(now_iso)"
    echo "updated: $(now_iso)"
    echo "files: $files_yaml"
    echo "parent: ${parent:-none}"
    echo "---"
    echo
    echo "# $id · $title"
    echo
    echo "**요청자** \`$from\` → **담당** \`$to\`"
    echo
    echo "## 요청 내용"
    echo
    echo "${body:-<!-- 무엇을 해야 하는지 구체적으로. 담당자가 코드를 직접 고칠 수 있을 만큼. -->}"
    echo
    echo "## 왜 필요한가"
    echo
    echo "${why:-<!-- 이 변경이 없으면 요청자 쪽에서 무엇이 안 되는지. -->}"
    echo
    echo "## 대상 파일"
    echo
    if [ -n "$files" ]; then
      printf '%s' "$files" | tr ',' '\n' | sed -e 's/^ *//' -e 's/ *$//' -e 's/^/- `/' -e 's/$/`/'
    else
      echo "- <!-- 담당자가 판단 -->"
    fi
    echo
    echo "## 완료 기준"
    echo
    echo "${accept:-<!-- 무엇이 되면 이 요청이 끝난 것인가. 검증 방법까지. -->}"
    echo
    echo "---"
    echo
    echo "## 처리 결과"
    echo
    echo "<!-- 담당자가 여기에 적는다. 처리 후 반드시:"
    echo "     team/bin/req.sh done $id --by $to --note \"<한 줄 요약>\" -->"
    echo
    echo "_(미처리)_"
  } >"$f" || die "요청 파일을 쓸 수 없다: $f"

  ln -sfn "../$day/$(basename "$f")" "$OPEN_DIR/$id.md"
  index_append "발행" "$id" "$from" "$to" "$title"

  echo "$id"
  echo "$f"
  echo
  echo "다음: SendMessage 로 $to 에게 포인터만 보내라(내용을 문장으로 옮기지 마라)."
  echo "  예) to: \"$to\", message: \"[$id] 요청 발행. requests/open/$id.md 를 읽고 처리하라.\""
}

cmd_list() {
  local ft="" ff="" fs="" all=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --to) ft="$2"; shift 2;;
      --from) ff="$2"; shift 2;;
      --status) fs="$2"; shift 2;;
      --all) all=1; shift;;
      *) die "알 수 없는 옵션: $1";;
    esac
  done
  [ -d "$REQ_DIR" ] || { echo "(요청 없음)"; return 0; }

  local found=0 f id st fr to ti
  printf '%-10s %-9s %-18s %-18s %s\n' ID 상태 FROM TO 제목
  printf '%-10s %-9s %-18s %-18s %s\n' ---------- --------- ------------------ ------------------ ------------------------
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    st="$(fm_get "$f" status)"
    [ "$all" = 1 ] || [ -n "$fs" ] || [ "$st" = open ] || [ "$st" = claimed ] || continue
    [ -z "$fs" ] || [ "$st" = "$fs" ] || continue
    fr="$(fm_get "$f" from)"; to="$(fm_get "$f" to)"
    [ -z "$ft" ] || [ "$to" = "$ft" ] || continue
    [ -z "$ff" ] || [ "$fr" = "$ff" ] || continue
    id="$(fm_get "$f" id)"; ti="$(fm_get "$f" title)"
    printf '%-10s %-9s %-18s %-18s %s\n' "$id" "$st" "$fr" "$to" "$ti"
    found=1
  done <<EOF
$(find "$REQ_DIR" -maxdepth 2 -name 'REQ-*.md' -type f 2>/dev/null | sort)
EOF
  [ "$found" = 1 ] || echo "(조건에 맞는 요청 없음)"
}

# 상태 전이 공통부. transit <id> <새상태> <이벤트명> <처리자> [본문에 덧붙일 절]
transit() {
  local id="$1" new="$2" ev="$3" by="$4" append="$5" f cur
  [ -n "$id" ] || die "요청 ID 가 필요하다"
  [ -n "$by" ] || die "--by <에이전트 이름> 이 필요하다"
  f="$(req_file "$id")" || die "그런 요청이 없다: $id"
  cur="$(fm_get "$f" status)"
  [ "$cur" != "$new" ] || die "$id 는 이미 $new 상태다"

  fm_set "$f" status "$new" || die "상태를 바꿀 수 없다: $f"
  fm_set "$f" updated "$(now_iso)"
  [ -n "$append" ] && printf '%s\n' "$append" >>"$f"

  case "$new" in
    done|rejected) rm -f "$OPEN_DIR/$id.md" ;;   # 미결 목록에서 내린다(원본은 날짜 폴더에 남는다)
  esac
  index_append "$ev" "$id" "$by" "$(fm_get "$f" to)" "$(fm_get "$f" title)"
  echo "$id → $new  ($f)"
}

case "${1:-}" in
  new)   shift; cmd_new "$@";;
  list)  shift; cmd_list "$@";;
  path)  shift; req_file "${1:-}" || die "그런 요청이 없다: ${1:-}";;
  show)  shift; f="$(req_file "${1:-}")" || die "그런 요청이 없다: ${1:-}"; cat "$f";;
  claim)
    id="$2"; shift 2; by=""
    while [ $# -gt 0 ]; do case "$1" in --by) by="$2"; shift 2;; *) die "알 수 없는 옵션: $1";; esac; done
    transit "$id" claimed "착수" "$by" ""
    ;;
  done)
    id="$2"; shift 2; by=""; note=""
    while [ $# -gt 0 ]; do
      case "$1" in --by) by="$2"; shift 2;; --note) note="$2"; shift 2;; *) die "알 수 없는 옵션: $1";; esac
    done
    transit "$id" done "완료" "$by" "
### 처리 완료 · $by · $(now_iso)

${note:-(요약 없음)}
"
    ;;
  reject)
    id="$2"; shift 2; by=""; reason=""
    while [ $# -gt 0 ]; do
      case "$1" in --by) by="$2"; shift 2;; --reason) reason="$2"; shift 2;; *) die "알 수 없는 옵션: $1";; esac
    done
    [ -n "$reason" ] || die "--reason 이 필요하다 — 왜 못 하는지 남기지 않으면 요청자가 다음 수를 못 둔다"
    transit "$id" rejected "반려" "$by" "
### 반려 · $by · $(now_iso)

$reason
"
    ;;
  ""|-h|--help) usage 0;;
  *) die "알 수 없는 명령: $1  (req.sh --help)";;
esac
