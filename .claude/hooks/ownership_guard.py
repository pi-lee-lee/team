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
_PATHY = re.compile(r"[A-Za-z0-9_./~$-]{2,}")


_SEGSPLIT = re.compile(r"(?:\|\||&&|[;|&\n]|\$\(|`)")


def bash_candidates(cmd, own):
    """명령어 문자열에서 '파일 경로처럼 생긴' 토큰을 뽑는다.

    각 명령 구획의 **첫 토큰(실행 파일)은 제외한다.** 스크립트를 실행하는 것과
    스크립트를 수정하는 것은 다르다. 이 구분이 없으면 에이전트가 루트 소유인
    `team/bin/req.sh` 를 호출하는 것만으로 차단된다 — 실제로 겪은 사고다.
    (`req.sh done ... --note "cp 로 복사함"` 처럼 인자에 변조 낱말이 섞이면 발동했다.)
    """
    exts = set((own.get("ext_rules") or {}).keys())
    out = []
    for seg in _SEGSPLIT.split(cmd or ""):
        toks = seg.split()
        for i, tok in enumerate(toks):
            tok = tok.strip("'\"()")
            if i == 0:
                continue  # 실행 파일 자리 — 호출은 수정이 아니다
            if not tok or tok.startswith("-"):
                continue
            if not _PATHY.fullmatch(tok):
                continue
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
_REDIR = re.compile(r">>?\s*([^\s;|&<>]+)")


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
        owner, reason = owner_of(rel, own)
        if owner is None or owner == role:
            return
        deny(role, rel, owner, reason, extra="\n  탐지: %s" % why)

    # 1) 리다이렉션은 '쓰는 대상'이 명확하다 → 그 대상만 본다.
    #    명령줄 전체의 경로 토큰을 훑으면 `req.sh --files <남의경로> ... 2>&1` 같은
    #    정상 호출이 막힌다(실제로 겪은 오탐). 대상만 보면 그 오탐이 사라진다.
    for m in _REDIR.finditer(cmd):
        verdict(m.group(1).strip("'\""), "출력 리다이렉션으로 남의 영역 파일에 쓴다")

    # 2) 낱말형 변조 명령(rm/mv/cp/sed -i …)은 대상을 특정하기 어렵다 →
    #    그런 명령이 실제로 있을 때만 경로 토큰을 전수 검사한다.
    word_pats = [p for p in pats if p.strip() and p.strip()[0].isalpha()]
    hit = _mutation_hit(cmd, word_pats)
    if not hit:
        return
    for tok in bash_candidates(cmd, own):
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
