#!/usr/bin/env python3
# net/probe/win_block_diff.py — 🔴 **두 트리의 `_WIN32` 갈래만 뽑아서 나란히 놓는다**
# ═══════════════════════════════════════════════════════════════════════════
# 🔑 **왜 전체 diff 가 아닌가**
#   두 트리는 판이 달라서 전체 diff 는 수천 줄이다. 그러면 **아무도 안 읽는다.**
#   지금 물음은 하나다 — *"윈도우에서만 도는 코드가 어디서 갈렸나."*
#
# ⚠ **이 도구는 `#ifdef _WIN32` 만 본다.** 조건 없이 쓰는 윈도우 API
#   (`#include <windows.h>` 를 그냥 적은 것 같은)는 **놓친다.**
#   🔴 실측 2026-08-27 : 샘플 `entry.h:16` 이 정확히 그 모양이었다 —
#     `_WIN32` 토큰으로만 세면 **0** 이 나오고 *"윈도우 코드가 없다"* 로 읽힌다.
#     ★ 그래서 `WIN_TOKENS` 로 **따로 한 번 더** 센다.
#
# 쓰기 : python3 net/probe/win_block_diff.py <샘플트리> <릴리즈트리> [파일…]
# ═══════════════════════════════════════════════════════════════════════════

import os
import re
import sys

# 조건문 밖에 있어도 **윈도우 전용인 것**들. `_WIN32` 만 세면 이것들을 놓친다.
WIN_TOKENS = re.compile(
    r"windows\.h|winsock|ws2tcpip|<direct\.h>|WSA\w+|SetConsole\w+|"
    r"GetTickCount\w*|FILETIME|GetSystemTimeAsFileTime|_fullpath|_mkdir|"
    r"INVALID_SOCKET|SOCKET\b|closesocket|_CRT_SECURE_NO_WARNINGS|NOMINMAX|FD_SETSIZE")

_IF = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def win_blocks(path):
    """`_WIN32` 조건 안의 줄만 [(시작줄, [줄…])] 로 준다."""
    out, cur, depth, win_at = [], None, 0, None
    try:
        lines = open(path, encoding="utf-8").read().split("\n")
    except Exception:
        return out
    for i, ln in enumerate(lines, 1):
        m = _IF.match(ln)
        if m:
            kind, rest = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                depth += 1
                if win_at is None and "_WIN32" in rest:
                    win_at = depth
                    cur = (i, [ln.rstrip()])
                    continue
            elif kind == "endif":
                if win_at is not None and depth == win_at:
                    cur[1].append(ln.rstrip())
                    out.append(cur)
                    cur, win_at = None, None
                depth -= 1
                continue
        if cur is not None:
            cur[1].append(ln.rstrip())
    return out


def loose_hits(path):
    """조건문 **밖**에 있는 윈도우 토큰 — 여기가 가장 잘 놓치는 자리다."""
    hits, depth, win_at = [], 0, None
    try:
        lines = open(path, encoding="utf-8").read().split("\n")
    except Exception:
        return hits
    for i, ln in enumerate(lines, 1):
        m = _IF.match(ln)
        if m:
            kind, rest = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                depth += 1
                if win_at is None and "_WIN32" in rest:
                    win_at = depth
            elif kind == "endif":
                if win_at is not None and depth == win_at:
                    win_at = None
                depth -= 1
            continue
        if win_at is not None:
            continue                       # 갈래 안이면 여기 관심사가 아니다
        s = re.sub(r"//.*$", "", ln)
        if WIN_TOKENS.search(s):
            hits.append((i, ln.strip()[:88]))
    return hits


def main():
    if len(sys.argv) < 3:
        print(__doc__ or "")
        print("쓰기 : python3 net/probe/win_block_diff.py <샘플트리> <릴리즈트리> [파일…]")
        return 2
    A, B = sys.argv[1], sys.argv[2]
    names = sys.argv[3:] or sorted(
        f for f in os.listdir(B) if f.endswith((".h", ".cpp"))
        and os.path.exists(os.path.join(A, f)))

    for fn in names:
        pa, pb = os.path.join(A, fn), os.path.join(B, fn)
        ba, bb = win_blocks(pa), win_blocks(pb)
        la, lb = loose_hits(pa), loose_hits(pb)
        if not (ba or bb or la or lb):
            continue
        print("\n" + "═" * 70)
        print("%s   —   _WIN32 블록 샘플 %d / 릴리즈 %d · 갈래밖 토큰 샘플 %d / 릴리즈 %d"
              % (fn, len(ba), len(bb), len(la), len(lb)))
        # 🔑 블록 **본문**을 정규화해서 댄다 — 줄번호가 달라도 같은 코드면 같다고 봐야 한다
        na = ["\n".join(x.strip() for x in b[1] if x.strip())for b in ba]
        nb = ["\n".join(x.strip() for x in b[1] if x.strip())for b in bb]
        only_b = [(ba_i, t) for ba_i, t in zip([b[0] for b in bb], nb) if t not in na]
        only_a = [(bb_i, t) for bb_i, t in zip([b[0] for b in ba], na) if t not in nb]
        if only_b:
            print("\n🔴 릴리즈에만 있는 `_WIN32` 블록 %d개" % len(only_b))
            for i, t in only_b:
                print("  ── %s:%d" % (fn, i))
                for ln in t.split("\n"):
                    print("     " + ln[:92])
        if only_a:
            print("\n🔵 샘플에만 있는 `_WIN32` 블록 %d개 (기능이 사라졌나 확인해라)" % len(only_a))
            for i, t in only_a:
                print("  ── %s:%d" % (fn, i))
                for ln in t.split("\n"):
                    print("     " + ln[:92])
        if la or lb:
            print("\n⚠ **갈래 밖** 윈도우 토큰 — 조건 없이 쓰는 것이다")
            for tag, hs in (("샘플  ", la), ("릴리즈", lb)):
                for i, s in hs:
                    print("   %s %s:%d  %s" % (tag, fn, i, s))
    return 0


if __name__ == "__main__":
    sys.exit(main())
