#!/usr/bin/env python3
"""철회 도구 — **결론을 내릴 때, 그 결론을 인용한 곳을 같이 찾는다.**

왜 (루트 지적 2026-08-17 08:2x · 오늘 세 번 밟았다)
  결론은 바뀌는데 **그 결론이 심어진 문서들은 자동으로 안 따라온다.**
  · monitor: *"하행 주입 뒤 `★ 정지 감지` 0건이면 수정 확인"* 을 반박해 놓고
    **HANDOFF 에는 몇 시간 동안 그대로 남아 있었다.**
  · 루트: `drop=15` · A 창 시각 보정 — 철회한 뒤에도 문서·보고에 남아 있었다.

  **가장 조용한 형태다.** 틀린 문장이 어디에도 경고를 안 내고 다음 사람에게 그대로 간다.
  그리고 다음 사람이 읽는 것은 **내 메시지가 아니라 그 파일**이다.

  🔴 "조심하라"로는 또 놓친다. **철회를 절차로 만든다.**

쓰는 법
------
    python3 monitor/retract.py "정지 감지 0건"          # 어디에 남아 있나
    python3 monitor/retract.py --wide "5배"             # 프로젝트 문서까지 넓게

기본 탐색 대상 = **내 도메인 문서 전부**(`monitor/**.md` + `.py` 주석) 와
`requests/` · `docs/` 의 내가 쓴 것. ⚠ 남의 파일은 **고치지 말고 목록만** 낸다 —
그 사람에게 알리는 것이 내 일이다.

⚠ 이 도구는 **찾아만 준다. 무엇이 철회 대상인지는 사람이 정한다.**
   문자열이 겹칠 뿐 다른 맥락일 수 있다 — **반드시 열어 보고 고쳐라.**
"""
from __future__ import annotations

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

NARROW = ["monitor"]
WIDE = ["monitor", "requests", "docs"]
SKIP_DIRS = {"__pycache__", "out", ".git", "frozen-A"}
SKIP_NOTE = "frozen-A/ 는 동결이라 건드리지 않는다 — 목록에서도 뺀다"
EXTS = {".md", ".py", ".txt", ".tsv"}


def walk(dirs):
    for d in dirs:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, files in os.walk(base):
            dirnames[:] = [x for x in dirnames if x not in SKIP_DIRS]
            for fn in files:
                if os.path.splitext(fn)[1] in EXTS:
                    yield os.path.join(dirpath, fn)


def main() -> int:
    args = [a for a in sys.argv[1:]]
    wide = "--wide" in args
    args = [a for a in args if a != "--wide"]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    needle = args[0]
    pat = re.compile(re.escape(needle))

    hits = []
    scanned = 0
    for path in walk(WIDE if wide else NARROW):
        scanned += 1
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                for i, line in enumerate(f, 1):
                    if pat.search(line):
                        hits.append((os.path.relpath(path, ROOT), i, line.rstrip()))
        except OSError:
            continue

    print(f"# 철회 대상 찾기: {needle!r}")
    print(f"# 훑은 파일 {scanned}개 ({'넓게' if wide else 'monitor/ 만'}) · {SKIP_NOTE}")
    if not hits:
        print(f"\n0곳  ⚠ [훑은 파일 {scanned} · 찾은 것 {needle!r}]")
        print("   → **문자열이 달라서 못 찾았을 수 있다.** 더 짧은 조각으로 다시 찾아라.")
        print("      (원장 1.1 · 7.44 — `0` 은 '없다'가 아니라 '못 찾았다'일 수 있다)")
        return 0

    byfile = {}
    for p, i, ln in hits:
        byfile.setdefault(p, []).append((i, ln))
    print(f"\n**{len(hits)}곳 / 파일 {len(byfile)}개** — 전부 열어 보고 고쳐라\n")
    for p in sorted(byfile):
        mine = p.startswith("monitor/")
        tag = "" if mine else "   ⚠ **내 소유가 아니다 — 고치지 말고 담당에게 알려라**"
        print(f"## {p}{tag}")
        for i, ln in byfile[p]:
            print(f"   {i:>5}: {ln[:110]}")
        print()
    print("## 철회 절차")
    print("  1. 각 자리를 열어 **맥락이 정말 그 결론인지** 확인한다(문자열만 겹칠 수 있다)")
    print("  2. **지우지 말고 취소선 + 사유**를 남긴다(원장 규칙 2) — 지우면 다음 사람이 같은 길로 간다")
    print("  3. 남의 파일이면 **고치지 말고 담당에게 알린다**")
    print("  4. 철회 사실 자체를 **원장과 `req.sh notice`** 에 남긴다")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
