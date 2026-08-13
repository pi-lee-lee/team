#!/usr/bin/env python3
"""teamctl.py — 팀 상태 파일(JSON/마크다운)을 다루는 헬퍼.

셸에서 JSON 을 안전하게 고치기는 어렵다(따옴표·유니코드·순서). 그래서 파일을
건드리는 일은 전부 여기로 모은다. team-up.sh / agent-new.sh 가 호출한다.

  teamctl.py roster                         에이전트 이름 목록(.claude/agents/*.md)
  teamctl.py registry-set <sid> <role> <short>   session_id → 역할 등록
  teamctl.py registry-role <sid>            역할 조회
  teamctl.py ownership-add <owner> [--paths g1,g2] [--exts .a,.b]
  teamctl.py render-table                   CLAUDE.md 의 소유권 표를 ownership.json 로부터 재생성
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AGENTS_DIR = os.path.join(ROOT, ".claude", "agents")
REGISTRY = os.path.join(ROOT, ".claude", "team", "registry.json")
OWNERSHIP = os.path.join(ROOT, ".claude", "ownership.json")
CLAUDE_MD = os.path.join(ROOT, "CLAUDE.md")

BEGIN = "<!-- OWNERSHIP-TABLE:BEGIN -->"
END = "<!-- OWNERSHIP-TABLE:END -->"


def load(path, default):
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return default


def save(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp, path)  # 원자 교체 — 반쯤 쓰인 설정 파일을 훅이 읽는 사고를 막는다


def roster():
    if not os.path.isdir(AGENTS_DIR):
        return []
    return sorted(
        f[:-3] for f in os.listdir(AGENTS_DIR)
        if f.endswith(".md") and not f.startswith("_")
    )


def cmd_registry_set(sid, role, short):
    reg = load(REGISTRY, {})
    reg.setdefault("_설명", (
        "session_id → 역할 매핑. .claude/hooks/ownership_guard.py 가 이 파일로 "
        "'지금 도구를 부른 세션이 누구인가'를 판정한다. 여기에 없는 세션은 루트로 "
        "취급되고 도메인 파일을 쓸 수 없다(기본 거부). team-up.sh 가 갱신한다."
    ))
    reg.setdefault("agents", {})
    reg["agents"][sid] = {"role": role, "short": short}
    save(REGISTRY, reg)
    print("등록: %s → %s (%s)" % (sid[:8], role, short))


def cmd_registry_role(sid):
    reg = load(REGISTRY, {})
    ent = (reg.get("agents") or {}).get(sid)
    print(ent.get("role") if isinstance(ent, dict) else "root")


def cmd_ownership_add(owner, paths, exts):
    own = load(OWNERSHIP, None)
    if own is None:
        sys.exit("ownership.json 을 읽을 수 없다")
    rules = own.setdefault("path_rules", [])
    existing = {r.get("glob") for r in rules}
    added = []
    for g in [p.strip() for p in (paths or "").split(",") if p.strip()]:
        if g in existing:
            print("이미 있음(건너뜀): %s" % g)
            continue
        # 새 규칙은 앞에 넣는다. 뒤에 붙이면 android/** 같은 넓은 기존 규칙에
        # 먼저 잡혀서 새 에이전트가 자기 경로를 영영 못 갖는다.
        rules.insert(0, {"glob": g, "owner": owner})
        added.append(g)
    ext_rules = own.setdefault("ext_rules", {})
    for e in [x.strip() for x in (exts or "").split(",") if x.strip()]:
        if not e.startswith("."):
            e = "." + e
        if e in ext_rules:
            print("확장자 %s 는 이미 %s 소유 — 건너뜀" % (e, ext_rules[e]))
            continue
        ext_rules[e] = owner
        added.append(e)
    save(OWNERSHIP, own)
    print("소유권 추가: %s ← %s" % (owner, ", ".join(added) if added else "(없음)"))


def cmd_agents_table():
    """에이전트별 기동 상태 + 등록 여부 + 미결 요청 수를 한 줄씩 출력."""
    import glob
    import subprocess

    try:
        out = subprocess.run(
            ["claude", "agents", "--cwd", ROOT, "--json"],
            capture_output=True, text=True, timeout=30,
        ).stdout
        rows = json.loads(out)
    except Exception:
        rows = []
    live = {r.get("name"): r for r in rows if r.get("kind") == "background"}

    reg = (load(REGISTRY, {}).get("agents") or {})
    registered = {v.get("role") for v in reg.values() if isinstance(v, dict)}

    # 에이전트별 미결(open/claimed) 요청 수
    pending = {}
    for f in glob.glob(os.path.join(ROOT, "requests", "*", "REQ-*.md")):
        # requests/open/ 안은 원본을 가리키는 심볼릭 링크다. 걸러내지 않으면
        # 미결 요청이 두 번 세어진다(1건인데 "미결 2"로 보였던 버그).
        if os.path.islink(f):
            continue
        to = st = None
        try:
            with open(f) as fh:
                for line in fh:
                    if line.startswith("to: "):
                        to = line[4:].strip()
                    elif line.startswith("status: "):
                        st = line[8:].strip()
                    elif line.strip() == "---" and to and st:
                        break
        except OSError:
            continue
        if st in ("open", "claimed") and to:
            pending[to] = pending.get(to, 0) + 1

    names = roster()
    if not names:
        print("  (.claude/agents/ 에 에이전트가 없다)")
        return
    for nm in names:
        r = live.get(nm)
        if r:
            state = r.get("status", "?")
            if state == "waiting":
                state = "⏸ 대기(%s)" % (r.get("waitingFor") or "승인")
            else:
                state = "● %s" % state
            detail = "%-22s short=%s" % (state, r.get("id", "?"))
        else:
            detail = "%-22s %s" % ("○ 미기동", "team-up.sh 로 기동")
        ok = "등록✓" if nm in registered else "등록✗ 소유권미적용!"
        print("  %-18s %-40s %-20s 미결 %d" % (nm, detail, ok, pending.get(nm, 0)))


def cmd_render_table():
    own = load(OWNERSHIP, None)
    if own is None:
        sys.exit("ownership.json 을 읽을 수 없다")

    by_owner = {}
    for r in own.get("path_rules", []):
        by_owner.setdefault(r.get("owner"), []).append("`%s`" % r.get("glob"))
    for e, o in (own.get("ext_rules") or {}).items():
        by_owner.setdefault(o, []).append("`%s`" % e)

    lines = [BEGIN, "", "| 담당 에이전트 | 소유 경로·확장자 |", "|---|---|"]
    for name in roster():
        pats = by_owner.pop(name, [])
        lines.append("| `%s` | %s |" % (name, ", ".join(pats) if pats else "_(아직 없음)_"))
    for name, pats in sorted(by_owner.items()):  # 에이전트 md 는 없는데 규칙만 있는 경우
        lines.append("| `%s` ⚠에이전트 없음 | %s |" % (name, ", ".join(pats)))
    lines += [
        "| `root` | %s |" % ", ".join("`%s`" % p for p in own.get("root_paths", [])),
        "| _(전원 공용)_ | %s |" % ", ".join("`%s`" % p for p in own.get("open_paths", [])),
        "",
        "그 밖의 모든 파일 → `%s` 소유(= 전문 에이전트는 손대지 못함)." % own.get("default_owner", "root"),
        "",
        "_이 표는 `team/bin/teamctl.py render-table` 이 `.claude/ownership.json` 에서 자동 생성한다. 직접 고치지 마라._",
        "",
        END,
    ]
    table = "\n".join(lines)

    try:
        with open(CLAUDE_MD) as f:
            doc = f.read()
    except FileNotFoundError:
        sys.exit("CLAUDE.md 가 없다 — 먼저 만들어라")

    if BEGIN in doc and END in doc:
        doc = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), lambda _: table, doc, flags=re.S)
    else:
        doc = doc.rstrip() + "\n\n" + table + "\n"
    with open(CLAUDE_MD, "w") as f:
        f.write(doc)
    print("CLAUDE.md 소유권 표 갱신 완료")


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    c = a[0]
    if c == "roster":
        print("\n".join(roster()))
    elif c == "registry-set":
        cmd_registry_set(a[1], a[2], a[3] if len(a) > 3 else "")
    elif c == "registry-role":
        cmd_registry_role(a[1])
    elif c == "ownership-add":
        owner, paths, exts = a[1], "", ""
        i = 2
        while i < len(a):
            if a[i] == "--paths":
                paths = a[i + 1]; i += 2
            elif a[i] == "--exts":
                exts = a[i + 1]; i += 2
            else:
                sys.exit("알 수 없는 옵션: %s" % a[i])
        cmd_ownership_add(owner, paths, exts)
    elif c == "agents-table":
        cmd_agents_table()
    elif c == "render-table":
        cmd_render_table()
    else:
        sys.exit("알 수 없는 명령: %s" % c)


if __name__ == "__main__":
    main()
