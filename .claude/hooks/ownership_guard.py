#!/usr/bin/env python3
"""PreToolUse 소유권 가드 — 파일은 담당 에이전트만 고친다.

## 왜 필요한가
사용자 요구: "c++로 작성된 코드는 c++ 에이전트만 생성·수정·편집할 수 있다."
프롬프트로만 적어두면 지켜지는지 아무도 모른다. 이 훅은 그 규칙을 **기계적으로**
집행한다. 규칙을 어긴 도구 호출은 exit 2 로 실제로 실패하고, 모델은 stderr 로
"대신 이렇게 하라"는 지시를 돌려받는다.

## 역할(role)은 어떻게 아는가
훅 입력 JSON 에는 에이전트 이름이 없다. 대신 `session_id` 가 항상 들어온다.
그래서 `.claude/team/registry.json` 이 session_id → 역할 을 매핑한다.
team-up.sh 가 에이전트를 띄우면서 이 매핑을 기록한다.

  * 등록되지 않은 세션 = 루트 취급 = **도메인 파일 쓰기 금지**.
    (환경변수 상속을 쓰지 않는 이유: `claude --bg` 는 데몬이 세션을 띄우므로
     기동 셸의 env 가 전달되지 않는다 — 실측으로 확인됨.)

## 기본값은 '거부'다
루트 에이전트도 도메인 파일을 못 고친다. 루트가 예외였다면 가장 바쁜 세션이
규칙을 통과해버려 구조 전체가 장식이 된다. 루트는 요청을 발행해 담당에게 넘긴다.

## 한계 (정직하게)
Bash 검사는 셸 파서가 아니라 휴리스틱이다. `>` 리다이렉션·sed -i 같은 명백한
파일 변조는 잡지만, 빌드 도구가 내부적으로 쓰는 파일까지는 못 막는다.
주 방어선은 Edit/Write/NotebookEdit 차단이고 Bash 검사는 실수를 잡는 그물이다.
"""
import json
import os
import re
import shlex
import sys

PROJECT_ROOT = "/Users/idong-u/learn"
OWNERSHIP = os.path.join(PROJECT_ROOT, ".claude", "ownership.json")
REGISTRY = os.path.join(PROJECT_ROOT, ".claude", "team", "registry.json")

ROOT_ROLE = "root"
EDIT_TOOLS = ("Write", "Edit", "NotebookEdit", "MultiEdit")


# --- glob → 정규식 -----------------------------------------------------------
# `**` 는 디렉터리 경계를 넘고 `*` 는 넘지 않는다. fnmatch 는 이 구분을 못 해서
# `*.md` 가 `docs/a.md` 까지 잡아버린다 — 소유권 판정에서는 치명적이라 직접 만든다.
def glob_to_re(pat):
    out, i, n = ["^"], 0, len(pat)
    while i < n:
        c = pat[i]
        if pat.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
        elif pat.startswith("**", i):
            out.append(".*")
            i += 2
        elif c == "*":
            out.append("[^/]*")
            i += 1
        elif c == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(c))
            i += 1
    out.append("$")
    return re.compile("".join(out))


_re_cache = {}


def match(pat, rel):
    r = _re_cache.get(pat)
    if r is None:
        r = _re_cache[pat] = glob_to_re(pat)
    return bool(r.match(rel))


# --- 설정 로드 ---------------------------------------------------------------
def load_json(path, default):
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return default


def role_of_session(sid):
    """session_id → 역할. 미등록 세션은 루트로 본다(= 도메인 파일 쓰기 금지)."""
    reg = load_json(REGISTRY, {})
    ent = (reg.get("agents") or {}).get(sid or "")
    if isinstance(ent, dict):
        return ent.get("role") or ROOT_ROLE
    return ROOT_ROLE


def rel_path(p, cwd):
    """절대/상대 경로를 프로젝트 루트 기준 상대 POSIX 경로로. 밖이면 None."""
    if not p:
        return None
    if not os.path.isabs(p):
        p = os.path.join(cwd or PROJECT_ROOT, p)
    p = os.path.normpath(p)
    root = os.path.normpath(PROJECT_ROOT)
    if p == root:
        return "."
    if not p.startswith(root + os.sep):
        return None
    return p[len(root) + 1:].replace(os.sep, "/")


def owner_of(rel, own):
    """소유자와 판정 근거를 (owner, reason) 으로 돌려준다."""
    if rel is None:
        return own.get("outside_project_owner", ROOT_ROLE), "프로젝트 밖 경로"
    for g in own.get("open_paths", []):
        if match(g, rel):
            return None, f"공용 경로({g}) — 전원 쓰기 가능"
    for g in own.get("root_paths", []):
        if match(g, rel):
            return ROOT_ROLE, f"루트 전용 경로({g})"
    for rule in own.get("path_rules", []):
        g = rule.get("glob", "")
        if g and match(g, rel):
            return rule.get("owner"), f"경로 규칙({g})"
    ext = os.path.splitext(rel)[1].lower()
    ext_owner = (own.get("ext_rules") or {}).get(ext)
    if ext_owner:
        return ext_owner, f"확장자 규칙({ext})"
    return own.get("default_owner", ROOT_ROLE), "규칙 없음 → 기본 소유자"


def deny(role, rel, owner, reason, extra=""):
    if owner == ROOT_ROLE:
        how = (
            "→ 이 경로는 루트 에이전트 소유다. 직접 고치지 말고 루트에게 보고하라.\n"
            '   team/bin/req.sh new --from %s --to root --title "<요청 제목>" '
            "--files %s" % (role, rel)
        )
    else:
        how = (
            "→ 담당 에이전트에게 md 요청을 발행하라(문장 통신 금지, 파일로 남긴다):\n"
            '   team/bin/req.sh new --from %s --to %s --title "<요청 제목>" --files %s\n'
            "   그 다음 SendMessage 로 상대에게 '해당 REQ 파일을 읽어라'고 포인터만 보낸다.\n"
            "   상대가 처리하고 같은 파일에 결과를 적는다." % (role, owner, rel)
        )
    msg = (
        "[소유권 차단] 너의 역할=%s · 이 파일의 소유자=%s\n"
        "  대상: %s\n  근거: %s%s\n%s\n"
        "  (소유권 원천: .claude/ownership.json — 판정이 틀렸다고 판단되면 "
        "루트에게 규칙 수정을 요청하라. 우회하지 마라.)"
        % (role, owner, rel, reason, extra, how)
    )
    print(msg, file=sys.stderr)
    sys.exit(2)


# --- Bash 휴리스틱 -----------------------------------------------------------
# 🔴 비ASCII 를 포함해야 한다. 우리 트리의 대부분이 한글 경로(`조별과제샘플/**`)인데
#   ASCII 만 두면 `> 조별과제샘플/x/y.cpp` 가 **경로로 인식조차 안 되어 검사를 통째로 빠져나간다**
#   (2026-08-20 실측: 루트가 socket 소유 경로에 리다이렉션했는데 안 막혔다).
_PATHY = re.compile(r"[A-Za-z0-9_./~$\- -￿]{2,}")

# `sed` 치환식은 경로가 아니다.
#
# ⚠ 2026-08-16: `sed -i '' s/a/b/ arduino/x.ino` 가 **자기 파일을 고치는 담당에게도
#   차단**됐다. `s/a/b/` 에 슬래시가 있어 경로 후보로 잡히고, 어느 규칙에도 안 걸리니
#   기본 소유자(root)로 떨어져 전문 에이전트가 전부 막힌다.
#   증상이 "왜 내 파일을 내가 못 고치지"라서 훅 결함으로 오인하기 쉽다.
#
#   구분자를 바꾸면(`s|a|b|`) 통과하는데, 그건 우회이지 해결이 아니다 —
#   우회를 알아야만 일할 수 있는 규칙은 결국 규칙을 안 읽은 사람을 막는다.
#
#   범위를 좁게 잡았다: `s`/`y` 로 시작하고 **슬래시 구분자 셋**에 뒤가 플래그뿐인 것만.
#   같은 모양의 실제 경로(`s/a/b/`)는 사실상 존재하지 않고, 이 저장소에는 최상위 `s/`
#   디렉터리 자체가 없다. **`sed` 뒤에 오는 파일 인자는 그대로 검사된다** — 아래 시험으로
#   확인했다(`/private/tmp/.../hooktest.py`, 남의 파일 수정은 여전히 차단).
_SED_EXPR = re.compile(r"^[sy]/(?:[^/\\]|\\.)*/(?:[^/\\]|\\.)*/[A-Za-z0-9]*$")


_SEGSPLIT = re.compile(r"(?:\|\||&&|[;|&\n]|\$\(|`)")


_SEG_SEPS = {";", ";;", "&", "&&", "|", "||", "(", ")", "\n"}


_HEREDOC_START = re.compile(r"<<-?\s*([\"\']?)([A-Za-z_][A-Za-z0-9_]*)\1")


def _strip_heredocs(cmd):
    """히어독 **본문**을 검사 대상에서 뺀다. 여는 줄과 리다이렉션 대상은 남긴다.

    ⚠ 2026-08-18: 본문이 그대로 검사돼 **문서·설명·예시에 적힌 경로가 차단 대상**이 됐다.
    루트가 훅 패치를 설명하는 주석을 히어독에 담았다가 자기 명령이 막혔고, socket 도
    같은 계열로 막혔다. **본문은 데이터이지 그 명령이 고치는 대상이 아니다.**

    🔴 **집행은 유지된다** — 여는 줄(`cat > <파일> <<EOF`)의 리다이렉션 대상은
    본문 밖이라 그대로 검사된다. 시험에 통과/차단 짝으로 박아 뒀다.

    ⚠ 이걸로 미탐이 하나 생긴다: 본문에 담긴 코드를 해석기가 실행하는 형태
    (`python3 - <<PY … PY`)는 본문이 안 보이므로 못 잡는다. **그건 원래도 못 잡았다** —
    CLAUDE.md 가 명시한 대로 이 훅은 실수를 잡는 그물이지 샌드박스가 아니다.
    """
    lines = (cmd or "").split("\n")
    out, i = [], 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        m = _HEREDOC_START.search(line)
        i += 1
        if not m:
            continue
        delim = m.group(2)
        while i < len(lines) and lines[i].strip() != delim:
            i += 1
        i += 1                      # 종료 구분자 줄도 버린다
    return "\n".join(out)


def _segment_tokens(cmd):
    """명령을 **구획별 토큰 목록**으로 나눈다 — 셸 인용을 존중한다.

    ⚠ 2026-08-18: 예전에는 정규식으로 `;`/`&&`/`|` 를 먼저 자르고 각 조각을
    `seg.split()` 했다. **둘 다 따옴표를 안 봤다.** 그래서 치환식 안의 `;` 와 공백에서
    표현식이 부서졌고, 남은 조각 `'s/` 가 `_PATHY` 에 걸려 **경로 후보**가 됐다.
    차단 메시지는 `대상: s` — **무엇이 막힌 것인지 알 수 없는 형태**였다(arduino 보고).

    `_SED_EXPR` 예외가 있었지만 소용없었다: **치환식이 통째로 와야 매치되는데
    이미 부서진 뒤**였다. 방어선을 정규식이 아니라 **토큰화**로 옮긴다.

    🔴 **집행은 안 약해진다** — 인용은 인용으로만 처리하고, 뒤따르는 파일 인자는
    그대로 토큰으로 남아 검사된다. 리다이렉션 기호는 **구획 경계로 쓰지 않는다**:
    경계로 쓰면 대상 파일이 다음 구획의 첫 토큰(=실행 파일 자리)이 되어 **건너뛰어진다.**
    (시험: "cd 뒤 남의 파일에 리다이렉션" 이 그것을 지킨다)

    ⚠ 따옴표가 안 닫힌 명령은 shlex 가 예외를 낸다 → **예전 방식으로 되돌린다.**
    검사를 건너뛰면 무사통과하므로, 거칠어도 나누는 쪽이 안전하다.
    """
    cmd = _strip_heredocs(cmd)
    try:
        lex = shlex.shlex(cmd or "", posix=True, punctuation_chars=True)
        lex.whitespace_split = True
        toks = list(lex)
    except ValueError:
        return [seg.split() for seg in _SEGSPLIT.split(cmd or "")]

    segs, cur = [], []
    for t in toks:
        if t in _SEG_SEPS:
            segs.append(cur)
            cur = []
        else:
            cur.append(t)
    segs.append(cur)
    return segs


def bash_candidates(cmd, own):
    """명령어 문자열에서 '파일 경로처럼 생긴' 토큰을 뽑는다.

    각 명령 구획의 **첫 토큰(실행 파일)은 제외한다.** 스크립트를 실행하는 것과
    스크립트를 수정하는 것은 다르다. 이 구분이 없으면 에이전트가 루트 소유인
    `team/bin/req.sh` 를 호출하는 것만으로 차단된다 — 실제로 겪은 사고다.
    (`req.sh done ... --note "cp 로 복사함"` 처럼 인자에 변조 낱말이 섞이면 발동했다.)
    """
    out = []
    for toks in _segment_tokens(cmd):
        out.extend(_candidates_from_toks(toks, own))
    return out


def _candidates_from_toks(toks, own):
    """한 구획의 토큰에서 경로 후보를 뽑는다."""
    exts = set((own.get("ext_rules") or {}).keys())
    out = []
    for _ in (0,):
        # 디렉터리 이동 구획은 통째로 건너뛴다. `cd` 의 인자는 **가려는 곳**이지
        # 고치려는 대상이 아니다.
        #
        # ⚠ 2026-08-16: 이게 없어서 `cd <프로젝트 루트>; <변조명령>` 이 **대상이
        #   무엇이든 무조건 차단**됐다. 첫 토큰(`cd`)만 건너뛰고 인자는 경로 후보로
        #   남으므로 rel_path() 가 "." 이 되고, "." 은 어느 규칙에도 안 걸려
        #   기본 소유자(root)로 떨어진다. 자기 소유 파일을 고치는 담당도 막힌다.
        #   web-engineer 가 자기 것을 만지다 막혀 훅 결함을 의심했고, 훅을 읽어
        #   원인을 갈랐다(REQ-0134 §4).
        #
        #   차단 메시지가 진짜 소유권 위반과 **똑같이 나오는 것**이 특히 나빴다 —
        #   같은 시각에 루트는 남의 영역이라 정당하게 막혔는데, 메시지가 같아서
        #   둘을 같은 원인으로 읽을 뻔했다.
        #
        #   집행은 안 약해진다. 뒤따르는 구획은 그대로 검사되므로
        #   `cd x; sed -i ... cpp/y.cpp` 의 `sed` 구획은 여전히 걸린다.
        if toks and os.path.basename(toks[0]) in ("cd", "pushd", "popd"):
            continue
        for i, tok in enumerate(toks):
            tok = tok.strip("'\"()")
            if i == 0:
                continue  # 실행 파일 자리 — 호출은 수정이 아니다
            if not tok or tok.startswith("-"):
                continue
            if "$" in tok:
                # 미치환 셸 변수 — 어디를 가리키는지 **알 수 없다.**
                # `$r/$ROUNDS` 같은 진행 표시가 경로로 잡혀 차단됐다(socket 보고).
                # 값을 모르는 채 차단하면 오탐이고, 통과시키면 미탐이다.
                # 🔴 오탐 쪽을 고른다 — CLAUDE.md 가 정한 성격(그물이지 샌드박스가 아니다)에 맞다.
                continue
            if not _PATHY.fullmatch(tok):
                continue
            if _SED_EXPR.match(tok):
                continue  # sed 치환식 — 고치려는 대상이 아니라 고치는 방법이다
            if "/" in tok or os.path.splitext(tok)[1].lower() in exts:
                out.append(tok)
    return out


def _mutation_hit(cmd, pats):
    """변조 패턴을 찾는다. 없으면 None.

    단순 부분문자열 매칭은 못 쓴다: 'platform' 안에 'rm ' 이 들어있어서
    `arduino-cli compile --fqbn platform ...` 같은 정상 명령이 차단된다.
    그래서 낱말로 시작하는 패턴은 명령 위치(줄머리·공백·; & | 뒤)에서만,
    기호 패턴(>, >>)은 그대로 부분문자열로 찾는다.
    """
    for p in pats:
        s = p.strip()
        if not s:
            continue
        if s[0].isalpha():
            if re.search(r"(?:^|[\s;&|(`])" + re.escape(s) + r"(?![\w-])", cmd or ""):
                return p
        elif p in (cmd or ""):
            return p
    return None


# 리다이렉션의 '대상'만 뽑는다.
#   `> out.txt`, `>>out.txt`, `2> err.log`  → 대상 파일
#   `2>&1`, `>&2`                           → 파일이 아니라 fd 복제이므로 잡히면 안 된다
#     ('&' 를 대상 문자에서 제외했으므로 `>&…` 는 아예 매치되지 않는다)
#
# `>` 앞 한 글자를 같이 붙잡아 두는 이유는 아래 _is_redirect_target 을 보라.
_REDIR = re.compile(r"(.?)>>?\s*([^\s;|&<>=]+)")

# `>` 는 리다이렉션 기호이기도 하지만 **거의 모든 언어의 비교 연산자**이기도 하다.
# 그래서 코드를 실행하기만 하는 명령이 "남의 파일에 쓴다"로 차단되는 사고가 있었다(REQ-0009):
#
#   python3 -c "... if ch >= 0xE0: ..."   →  `>` 뒤의 `=` 를 대상 파일로 보고 차단
#
# 파일을 전혀 건드리지 않는 명령이 막히면 에이전트가 우회 습관을 들인다. 그게 이 구조에서
# 가장 위험하다 — 오탐은 미탐보다 비싸다. 그래서 대상 토큰을 한 번 더 거른다.
_OPERATOR_LEAD = set("-=<!+*/%&|>")  # ->  =>  <>  !>  +>  >>= … 앞글자가 이러면 연산자다

# 슬래시가 없는 리다이렉션 대상을 파일로 인정하는 확장자.
# 넓히면 오탐이 돌아오고 좁히면 미탐이 는다 — 이 훅은 **오탐을 더 비싸게** 친다.
_FILE_EXTS = {
    "txt", "log", "md", "json", "jsonl", "csv", "yml", "yaml", "xml", "html", "htm",
    "css", "scss", "js", "mjs", "cjs", "ts", "tsx", "jsx", "vue", "py", "sh", "bash",
    "c", "cc", "cpp", "cxx", "h", "hpp", "hxx", "inl", "ino", "kt", "kts", "java",
    "hex", "bin", "o", "d", "out", "err", "diff", "patch", "conf", "ini", "cfg", "lock",
}


def _is_redirect_target(lead, tok):
    """`>` 뒤에 잡힌 토큰이 정말 '쓰기 대상 파일'인가.

    걸러내는 것:
      - 연산자의 일부  (`->`, `>=`, `=>`)  — 앞글자로 판별. `>=` 는 정규식에서 이미 제외
      - 파일 이름 같지 않은 토큰 (`5`, `n`, `0xE0`) — 비교식의 우변이다

      - 슬래시 없는 `이름.이름` (`ctl.max`, `.znode`) — CSS 자손 선택자와 멤버 비교가
        전부 이 꼴이라 파일과 구별이 안 된다. **아는 확장자일 때만** 파일로 본다

    통과시키는 것: `out.txt`, `../a/b.log`, `cpp/x.cpp`, `~/tmp/z.md`
    확장자 없는 `> outfile` 은 놓치지만, 이 훅은 선언대로 실수를 잡는 그물이지
    샌드박스가 아니다 — 미탐 하나와 정상 코드 실행 차단을 맞바꾸지 않는다.
    """
    if lead in _OPERATOR_LEAD:
        return False
    tok = tok.strip("'\"()[]{},")
    if not tok or not _PATHY.fullmatch(tok):
        return False
    # 전개 안 된 변수가 든 경로는 **값을 모른다.** 모르는 것을 남의 것으로 단정할 근거가 없다.
    # (`> "$TMP/x.h"` 가 `조별과제샘플/$TMP/x.h` 로 해석돼 차단된 사고가 있었다)
    # 진짜로 막아야 하는 것 — 남의 파일에 직접 쓰기 — 은 거의 언제나 리터럴 경로다.
    if "$" in tok or "`" in tok:
        return False
    if "/" in tok:
        return True                      # 경로는 모호하지 않다
    ext = tok.rsplit(".", 1)[-1].lower() if "." in tok else ""
    return ext in _FILE_EXTS


def check_bash(cmd, cwd, role, own):
    cfg = own.get("bash_mutation_guard") or {}
    if not cfg.get("enabled"):
        return
    cmd = cmd or ""
    pats = cfg.get("mutating_patterns") or []

    def verdict(tok, why):
        rel = rel_path(tok, cwd)
        if rel is None:
            return
        # ⚠ `requests/` 처럼 **슬래시로 끝나는 디렉토리 인자**는 정규화되면 `requests` 가
        #   되는데, 소유 규칙은 `requests/**` 라 **디렉토리 자신은 어느 규칙에도 안 걸린다.**
        #   그래서 공용 디렉토리인데 기본 소유자(root)로 떨어졌다.
        #   **디렉토리에 쓰는 것은 그 안의 파일에 쓰는 것**이므로 자식으로 판정한다.
        probe = rel + "/x" if tok.rstrip("'\"").endswith("/") else rel
        owner, reason = owner_of(probe, own)
        if owner is None or owner == role:
            return
        deny(role, rel, owner, reason, extra="\n  탐지: %s" % why)

    # 1) 리다이렉션은 '쓰는 대상'이 명확하다 → 그 대상만 본다.
    #    명령줄 전체의 경로 토큰을 훑으면 `req.sh --files <남의경로> ... 2>&1` 같은
    #    정상 호출이 막힌다(실제로 겪은 오탐). 대상만 보면 그 오탐이 사라진다.
    for m in _REDIR.finditer(cmd):
        lead, tok = m.group(1), m.group(2)
        if not _is_redirect_target(lead, tok):
            continue  # 비교 연산자이거나 파일 이름이 아니다 — REQ-0009
        verdict(tok.strip("'\"()[]{},"), "출력 리다이렉션으로 남의 영역 파일에 쓴다")

    # 2) 낱말형 변조 명령(rm/mv/cp/sed -i …)은 대상을 특정하기 어렵다 →
    #    그런 명령이 실제로 있을 때만 경로 토큰을 전수 검사한다.
    #
    # 🔴 2026-08-18 — **탐지와 대상 추출을 같은 구획 안에 묶는다.**
    #   예전에는 명령 **전체**에서 패턴을 찾고 **전체**에서 경로를 뽑았다. 그래서
    #   변조 명령 하나가 있으면 **무관한 다른 하위 명령의 인자까지 대상**이 됐다:
    #
    #     rm -f requests/.body-x.md ; git add requests/ && git commit …
    #        └ 패턴 rm 은 여기            └ 🔴 대상으로 잡힌 것은 여기(`requests/`)
    #
    #   차단 메시지가 `대상: requests` 로 떠서 **무엇이 막혔는지 알 수 없었다**(arduino 보고).
    #   `rm` 이 있는 줄에 다른 명령의 경로가 있으면 **언제나** 오탐이므로 범위가 넓다.
    #
    #   ⚠ 집행은 안 약해진다 — 변조 명령 **자신의 인자**는 같은 구획에 있다.
    #     `cd cpp; rm y.cpp` 도 `rm` 구획에 `y.cpp` 가 있어 그대로 걸린다(시험 있음).
    word_pats = [p for p in pats if p.strip() and p.strip()[0].isalpha()]
    for toks in _segment_tokens(cmd):
        if not toks:
            continue
        hit = _mutation_hit(" ".join(toks), word_pats)
        if not hit:
            continue
        for tok in _candidates_from_toks(toks, own):
            verdict(tok, "Bash 명령에 파일 변조 패턴 '%s' 이 있고 대상이 남의 영역이다." % hit)


def main():
    try:
        data = json.load(sys.stdin)
    except Exception:
        sys.exit(0)  # 입력을 못 읽으면 통과 — 가드 버그로 팀 전체를 멈추지 않는다

    own = load_json(OWNERSHIP, None)
    if not own:
        sys.exit(0)  # 설정이 없으면 판정 근거가 없다 → 통과

    tool = data.get("tool_name", "")
    cwd = data.get("cwd") or PROJECT_ROOT
    role = role_of_session(data.get("session_id"))
    ti = data.get("tool_input") or {}

    if tool in EDIT_TOOLS:
        rel = rel_path(ti.get("file_path") or ti.get("notebook_path"), cwd)
        owner, reason = owner_of(rel, own)
        if owner is None or owner == role:
            sys.exit(0)
        deny(role, rel if rel else str(ti.get("file_path")), owner, reason)

    elif tool == "Bash":
        check_bash(ti.get("command", ""), cwd, role, own)

    sys.exit(0)


if __name__ == "__main__":
    main()
