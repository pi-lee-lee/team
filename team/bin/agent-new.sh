#!/usr/bin/env bash
# agent-new.sh — 새 전문 에이전트를 만든다. **루트 에이전트 전용.**
#
# 사용자의 요청에 맞는 담당이 없을 때 루트가 이 스크립트로 새 에이전트를 만든다.
# 한 번의 실행으로 네 가지가 동시에 갱신되므로 어긋날 여지가 없다:
#   1) .claude/agents/<이름>.md      — 에이전트 정의(규약 포함)
#   2) .claude/ownership.json        — 소유 경로/확장자 규칙
#   3) CLAUDE.md 의 소유권 표         — ownership.json 에서 자동 재생성
#   4) 상시 기동 + 레지스트리 등록     — team-up.sh --only <이름>
#
# 사용법
#   team/bin/agent-new.sh <이름> --desc "<한 줄 설명>" \
#        [--paths "glob1,glob2"] [--exts ".a,.b"] [--brief "<전문 기준 문단>"] [--no-start]
#
# 예)
#   team/bin/agent-new.sh python-engineer --desc "파이썬 도구 담당" \
#        --paths "tools/**,scripts/**" --exts ".py"
#
# ── 삭제 스크립트는 일부러 만들지 않았다 ────────────────────────────────
# 사용자 규약: "생성된 에이전트는 삭제되지 않는다."
# 에이전트를 지우면 그 에이전트가 소유하던 파일이 주인 없는 상태가 되고,
# 과거 요청 원장에 남은 이름이 아무 데도 닿지 않게 된다. 필요 없어진 에이전트는
# 지우는 게 아니라 그냥 대기 상태로 둔다(비용은 유휴 세션 하나뿐이다).
# ────────────────────────────────────────────────────────────────────
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

name="${1:-}"
[ -n "$name" ] || die '사용법: agent-new.sh <이름> --desc "<설명>" [--paths ...] [--exts ...]'
case "$name" in
  -*|*/*|*' '*) die "에이전트 이름이 올바르지 않다: $name (영문 소문자와 하이픈만 권장)";;
esac
shift

desc=""; paths=""; exts=""; brief=""; start=1
while [ $# -gt 0 ]; do
  case "$1" in
    --desc) desc="$2"; shift 2;;
    --paths) paths="$2"; shift 2;;
    --exts) exts="$2"; shift 2;;
    --brief) brief="$2"; shift 2;;
    --no-start) start=0; shift;;
    *) die "알 수 없는 옵션: $1";;
  esac
done
[ -n "$desc" ] || die '--desc "<한 줄 설명>" 이 필요하다'

f="$AGENTS_DIR/$name.md"
[ -e "$f" ] && die "이미 있는 에이전트다: $f  (에이전트는 덮어쓰지 않는다)"
mkdir -p "$AGENTS_DIR"

owned_list=""
[ -n "$paths" ] && owned_list="$owned_list$(printf '%s' "$paths" | tr ',' '\n' | sed -e 's/^ *//' -e 's/^/- `/' -e 's/$/`/')
"
[ -n "$exts" ]  && owned_list="$owned_list$(printf '%s' "$exts"  | tr ',' '\n' | sed -e 's/^ *//' -e 's/^/- 확장자 `/' -e 's/$/`/')
"
[ -n "$owned_list" ] || owned_list="- _(아직 없음 — 루트가 .claude/ownership.json 에 추가한다)_"

cat >"$f" <<EOF
---
name: $name
description: $desc
---

너는 이 팀의 **$name** 이다. 상시 기동 상태로 유지되며, 종료되지 않는다.

## 네가 소유한 것

\`.claude/ownership.json\` 이 소유권의 유일한 원천이다. 요약하면 너의 영역은:

$owned_list
- \`docs/$name/**\`

## 절대 규칙

1. **네 영역 밖의 파일은 절대 직접 고치지 않는다.** 훅이 기계적으로 막는다.
   필요하면 담당 에이전트에게 md 요청을 발행한다.
2. **하위 에이전트를 만들지 않는다.** 할당된 작업은 네가 직접 끝낸다.
3. 요청 내용을 문장으로 주고받지 않는다. md 파일로 남기고 포인터만 보낸다.
4. 판정이 틀렸다고 생각되면 우회하지 말고 루트에게 규칙 수정을 요청하라.

## 요청을 받았을 때

1. \`team/bin/req.sh show <ID>\` → 2. \`req.sh claim <ID> --by $name\` →
3. 직접 구현 → 4. 요청 md 의 \`## 처리 결과\` 에 변경점·검증 방법 기록 →
5. \`req.sh done <ID> --by $name --note "..."\` → 6. \`SendMessage\` 로 요청자에게 ID 통보.
못 하겠으면 \`req.sh reject <ID> --by $name --reason "<사유>"\` — 사유 없이 반려하지 않는다.

## 남에게 요청할 때

\`\`\`bash
team/bin/req.sh new --from $name --to <담당> --title "<제목>" \\
  --files "<대상>" --body "<구체적 요구>" --why "<이유>" --accept "<완료 기준>"
\`\`\`
담당을 모르겠으면 \`--to root\`.

## 기술 기준

${brief:-- 검증하지 않은 것을 검증했다고 말하지 않는다. 실제로 실행·확인한 것과 코드만 읽고 판단한 것을 보고에서 구분한다.
- 라이브러리 API 가 확실하지 않으면 \`context7\` 로 현재 문서를 확인하고 답한다.}

## 무인 세션 실행 수칙 (중요)

너는 TTY 가 없는 백그라운드 세션이다. **승인 프롬프트가 뜨면 스스로 승인할 수 없어 그 자리에서 멈춘다.**
멈추면 루트가 알아채고 재기동해야 하므로 진행이 통째로 지연된다. 그래서:

- **명령을 \`&&\` / \`||\` / 파이프로 엮지 마라.** 한 번에 하나씩 실행한다.
  복합 명령은 하위 명령이 전부 허용돼야 통과하는데, 하나만 빠져도 전체가 멈춘다.
- \`2>/dev/null\` 같은 리다이렉션을 습관적으로 붙이지 마라. 필요할 때만 쓴다.
- 파일을 읽을 때는 Bash(\`cat\`) 보다 **Read 도구**를, 검색은 **Grep/Glob 도구**를 우선 쓴다.
  도구 호출은 셸 승인 대상이 아니라 훨씬 안정적이다.
- 그럼에도 막히면 그 명령을 포기하고 다른 방법을 찾되, 같은 명령을 반복 시도하지 마라.
EOF

echo "✓ 에이전트 정의 생성: $f"

if [ -n "$paths" ] || [ -n "$exts" ]; then
  python3 "$BIN_DIR/teamctl.py" ownership-add "$name" \
    ${paths:+--paths "$paths"} ${exts:+--exts "$exts"} || die "소유권 등록 실패"
fi

python3 "$BIN_DIR/teamctl.py" render-table || echo "⚠ CLAUDE.md 표 갱신 실패 — 수동 확인 필요"

if [ "$start" = 1 ]; then
  bash "$BIN_DIR/team-up.sh" --only "$name"
else
  echo "(--no-start 지정됨. 기동하려면: team/bin/team-up.sh --only $name)"
fi
