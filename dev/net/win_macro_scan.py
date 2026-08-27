#!/usr/bin/env python3
# net/win_macro_scan.py — 🔴 **`windows.h` 가 매크로로 쓰는 이름을 우리가 식별자로 쓰나**
# ═══════════════════════════════════════════════════════════════════════════
# 🔴 왜 이 축이 따로 필요한가 (2026-08-27)
#   맥에서 `-std=c++20 -pedantic-errors -Wall -Wextra` 가 **0 오류 0 경고**를 냈다.
#   그런데 사용자의 Visual Studio 는 오류를 낸다.
#   ★ **clang 이 원리적으로 못 보는 축이 있다** — `windows.h` 가 뿌리는 매크로다.
#     그것은 우리 소스에 없고, **MSVC 가 include 하는 순간에 생긴다.**
#
# > ### ★★ 증상이 가장 헷갈린다 — 매크로에 먹힌 이름은
# > ### **"정의되어 있지 않습니다"** 로 뜬다. **선언은 멀쩡히 있는데도.**
#
#   실제 사례가 이 저장소에 있다 : `near`/`far` 가 빌드를 막았다
#   (`릴리즈/VS_server/server/README.md` — cpp 가 윈도우 파싱 하니스로 찾았다).
#
# ⚠ **이 도구는 후보를 좁히는 것이지 판정이 아니다.**
#   맞다고 확정하는 유일한 길은 **MSVC 의 첫 오류**다.
#
# 쓰기 : python3 net/win_macro_scan.py [트리…]      기본 = 서머리/server
#        python3 net/win_macro_scan.py --self-test  🔴 **빨간불이 나는지** 보인다
# ═══════════════════════════════════════════════════════════════════════════

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TREE = os.path.join(ROOT, "서머리", "server")

# 🔴 **위험도 순으로 나눈다.** 전부 같은 무게로 찍으면 아무도 안 읽는다
#   (§"과잉 경보도 결함이다 — 더 잡으면 아무도 안 본다").
HIGH = {
    # 소문자라 **우리 이름과 겹칠 확률이 가장 높다.** near/far 는 실제로 겹쳤다.
    "near", "far", "pascal", "small", "min", "max", "interface",
}
MID = {
    # 대문자 매크로 — 상수·열거자 이름으로 자주 쓰는 것들
    "IN", "OUT", "OPTIONAL", "CONST", "VOID", "ERROR", "DELETE",
    "TRUE", "FALSE", "NO_ERROR", "INFINITE", "STATUS", "NULL_",
}
LOW = {
    # 함수형 매크로(A/W 접미사 치환) — 같은 이름의 **자유 함수**를 만들면 겹친다
    "GetMessage", "SendMessage", "PostMessage", "GetObject", "DrawText",
    "GetCurrentTime", "GetTempPath", "GetUserName", "GetClassName",
    "CreateFile", "DeleteFile", "CopyFile", "MoveFile", "OpenFile",
    "GetFileTime", "SetFileTime", "GetVersion", "FormatMessage",
    "CreateEvent", "CreateMutex", "CreateSemaphore", "LoadImage",
    "GetEnvironmentVariable", "SetEnvironmentVariable", "GetCommandLine",
    "GetModuleFileName", "GetModuleHandle", "LoadLibrary", "MessageBox",
    "Rectangle", "Polygon", "Ellipse", "Yield",
}
BUCKETS = (("🔴 높음", HIGH), ("⚠ 중간", MID), ("🔵 낮음", LOW))

# 🔑 **주석은 뺀다.** 안 빼면 우리 코드가 주석이 많아서 잡음이 실제의 몇 배가 된다
#   — 그러면 이 도구는 만들자마자 안 읽히는 도구가 된다.
_LINE_COMMENT = re.compile(r"//.*$")
_BLOCK_OPEN = re.compile(r"/\*")
_BLOCK_CLOSE = re.compile(r"\*/")
_STRING = re.compile(r'"(?:[^"\\]|\\.)*"')


def code_lines(path):
    """(줄번호, 코드만 남긴 줄) — 주석과 문자열 리터럴을 지운 것."""
    out = []
    in_block = False
    try:
        raw = open(path, encoding="utf-8").read().split("\n")
    except Exception:
        return out
    for i, ln in enumerate(raw, 1):
        s = ln
        if in_block:
            m = _BLOCK_CLOSE.search(s)
            if not m:
                continue
            s = s[m.end():]
            in_block = False
        # 한 줄 안에서 열고 닫는 것까지 처리한다
        while True:
            mo = _BLOCK_OPEN.search(s)
            if not mo:
                break
            mc = _BLOCK_CLOSE.search(s, mo.end())
            if mc:
                s = s[:mo.start()] + " " + s[mc.end():]
            else:
                s = s[:mo.start()]
                in_block = True
                break
        s = _LINE_COMMENT.sub("", s)
        s = _STRING.sub('""', s)          # 로그 문구 안의 낱말은 식별자가 아니다
        if s.strip():
            out.append((i, s))
    return out


def scan(trees, extra=None):
    """{이름: [(파일, 줄, 원문)]}"""
    hits = {}
    words = {}
    for label, names in BUCKETS:
        for n in names:
            words[n] = label
    if extra:
        for n in extra:
            words[n] = "★ 자가시험"
    pat = re.compile(r"\b(" + "|".join(sorted(map(re.escape, words))) + r")\b")
    for tree in trees:
        for dp, dns, fns in os.walk(tree):
            dns[:] = [d for d in dns if d != ".git"]
            for fn in sorted(fns):
                if not fn.endswith((".h", ".cpp", ".hpp", ".c")):
                    continue
                p = os.path.join(dp, fn)
                for i, s in code_lines(p):
                    for m in pat.finditer(s):
                        hits.setdefault(m.group(1), []).append(
                            (os.path.relpath(p, ROOT), i, s.strip()[:74]))
    return hits, words


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    self_test = "--self-test" in sys.argv
    trees = [os.path.abspath(a) for a in argv] or [DEFAULT_TREE]

    print("훑는 트리 : %s" % " ".join(os.path.relpath(t, ROOT) for t in trees))
    print("찾는 이름 : 높음 %d · 중간 %d · 낮음 %d"
          % (len(HIGH), len(MID), len(LOW)))

    extra = None
    if self_test:
        # 🔴 **검사가 실패도 한다는 것을 보인다.** 이 트리에 반드시 있는 이름을
        #   목록에 끼운다 — 안 잡히면 훑기 자체가 깨진 것이다.
        extra = {"sock_t"}
        print("★ 자가시험 — 이 트리에 **반드시 있는** `sock_t` 를 목록에 끼웠다.")
        print("  아래에 그것이 안 나오면 **훑기가 깨진 것**이다(주석 제거가 코드까지 먹었다든가).")

    hits, words = scan(trees, extra)
    print("")
    total = 0
    for label, names in list(BUCKETS) + ([("★ 자가시험", extra or set())] if self_test else []):
        got = sorted(n for n in names if n in hits)
        if not got:
            continue
        print("─" * 56)
        print("%s — %d종" % (label, len(got)))
        for n in got:
            rows = hits[n]
            total += len(rows)
            print("  %-24s %d곳" % (n, len(rows)))
            for rel, i, s in rows[:4]:
                print("      %s:%d  %s" % (rel, i, s))
            if len(rows) > 4:
                print("      … 그리고 %d곳 더" % (len(rows) - 4))

    print("\n" + "=" * 56)
    if total == 0:
        # 🔑 **`0` 을 그냥 초록으로 내지 않는다.** 이 `0` 의 뜻을 적어 둔다 —
        #   §"`0` 은 셋이다: 미배포 · 미발생 · 도달 불가". 여기 `0` 은 **"이 목록에는 없다"** 다.
        print("✅ 걸린 이름 0 — **이 목록에 있는 이름**은 안 쓴다")
        print("   🔴 그 뜻은 *\"MSVC 가 통과한다\"* 가 **아니다.**")
        print("      `windows.h` 의 매크로는 수백 개다. 이 목록은 **자주 겹치는 것**만이다")
        print("   ✅ 확정하는 유일한 길 : **MSVC 의 첫 오류를 받는 것**")
        return 0
    print("⚠ 걸린 자리 %d곳 — **판정이 아니라 후보다.**" % total)
    print("   🔴 `windows.h` 가 그 이름을 매크로로 만들면 우리 선언이 **먹힌다.**")
    print("      증상은 *\"식별자가 정의되어 있지 않습니다\"* 로 뜬다 — 선언은 있는데도")
    print("   ✅ 확인 : 그 자리를 MSVC 오류 줄번호와 대 봐라")
    return 1 if any(n in hits for n in HIGH) else 0


if __name__ == "__main__":
    sys.exit(main())
