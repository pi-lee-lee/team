#!/usr/bin/env python3
# net/release_sync_check.py — **소스 사본 트리가 정본과 같은가**를 값으로 답한다.
#
# 🔴 왜 필요한가 (2026-08-27 실측)
#   `릴리즈/server` 가 정본보다 **셋 낡아 있었다**(`cli.h` · `lot.cpp` · `wsjson.h`).
#   그날 오후 정본만 고치고 릴리즈를 안 맞췄기 때문이다.
#   ★ 그리고 **아무도 몰랐다** — 릴리즈 폴더는 평소에 아무도 안 연다.
#
# > ### ★★ `릴리즈/` 라는 **이름이 "이게 최신이다" 를 뜻하는 것처럼 읽힌다.**
# > ### 그런데 갱신은 **사람이 기억해서 하는 것**이었다. 그래서 낡았다.
#
#   🔑 web 이 같은 말을 했다 — *"갱신 주체와 소유자를 갈라 놓으면 반드시 낡는다."*
#     그쪽은 화면(`.html`)을 `web/tools/release-sync.mjs` 가 지킨다.
#     🔴 **소스(`.h`/`.cpp`)를 지키는 것이 없었다.** 이 파일이 그 자리다.
#
# 쓰기 : python3 net/release_sync_check.py          검사만 (다르면 exit 1)
#        python3 net/release_sync_check.py --sync   정본 → 사본으로 맞춘다
#        python3 net/release_sync_check.py --self-test   🔴 **빨간불이 나는지** 보인다
#
# ⚠ **방향은 한쪽뿐이다** — 정본은 `서머리/server` 다.
#   🔑 사본은 **사진**이고, 사진을 고쳐서 원본을 바꾸지 않는다.

import os
import re
import shutil
import subprocess
import sys
import filecmp
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "서머리", "server")
# 🔴 사본을 **여기 다 적어라.** 하나 빠지면 그 트리가 조용히 낡는다 —
#   이 파일이 막으려는 실패가 정확히 그것이다.
COPIES = [
    os.path.join(ROOT, "릴리즈", "server"),
    os.path.join(ROOT, "릴리즈", "VS_server", "server"),
]
EXT = (".h", ".cpp")


def source_files():
    return sorted(f for f in os.listdir(SRC) if f.endswith(EXT))


def compare(dst, files):
    """(없는 것, 다른 것) 을 준다."""
    missing, diff = [], []
    for f in files:
        b = os.path.join(dst, f)
        if not os.path.exists(b):
            missing.append(f)
        elif not filecmp.cmp(os.path.join(SRC, f), b, shallow=False):
            diff.append(f)
    return missing, diff


# ═══════════════════════════════════════════════════════════════════════════
# 🔴🔴 **② 무엇을 컴파일하나** — 소스가 같은 것과 빌드가 같은 것은 **다른 물음**이다
#
#   실측 2026-08-27 : 맥은 `server.cpp lot.cpp` **둘**을 컴파일하는데
#   VS 의 `.vcxproj` 는 `EXAMPLES.cpp` 까지 **셋**이었다.
#   ★ 위 ① 검사는 **34개 전부 동일**이라고 초록을 냈다. 파일 내용은 정말 같았으니까.
#
# > ### ★★ **소유권은 파일을 갈랐고 ① 은 내용을 갈랐는데, *빌드 목록* 은 아무도 안 셌다.**
#
#   🔑 그리고 이건 §"계약에 무엇을 더하든 부르는 줄과 꽂는 줄을 둘 다 세라" 의 빌드판이다 —
#     파일이 **있는 것**과 **컴파일되는 것**은 다르고, 그 차이는 조용하다.
# ═══════════════════════════════════════════════════════════════════════════

# 🔴 **실제로 컴파일되는 것.** 여기를 늘릴 때는 아래 모든 자리가 같이 움직여야 한다.
BUILD_SOURCES = ["lot.cpp", "server.cpp"]

# 🔴 **컴파일 안 되는 `.cpp` 는 여기 적어라 — 이유와 함께.**
#   ⚠ 적지 않으면 **조용히 표류한다**: 어떤 트리는 컴파일하고 어떤 트리는 안 하는데
#     둘 다 초록이 난다. 그게 위에서 실제로 일어난 일이다.
DOC_ONLY = {
    "EXAMPLES.cpp":
        "문서 — `parking.h` 사용 예제. 아무도 이 함수를 부르지 않는다.\n"
        "        🔴 그래도 **컴파일은 돼야 한다** — 낡으면 기여자가 낡은 것을 베낀다.\n"
        "        그래서 아래 ③ 이 구문검사를 건다(빌드 대상은 아니지만 검사 대상이다)",
}

VCXPROJ = os.path.join(ROOT, "릴리즈", "VS_server", "server", "server.vcxproj")
FILTERS = VCXPROJ + ".filters"
MSNS = "{http://schemas.microsoft.com/developer/msbuild/2003}"

# 빌드 명령이 적힌 곳을 **훑어서** 찾는다 — 목록으로 두면 새 자리가 조용히 빠진다.
SCAN_DIRS = ["net", "서머리/server", "릴리즈", "docs/net"]
SCAN_EXT = (".sh", ".md", ".py")
_CC = re.compile(r"\b(?:c\+\+|clang\+\+|g\+\+)\b")
_CPP = re.compile(r"([A-Za-z_][\w.\-]*\.cpp)")


def vcxproj_sources(path):
    """`.vcxproj`/`.filters` 가 **컴파일하겠다고 적은** `.cpp` 들."""
    if not os.path.exists(path):
        return None, "파일이 없다"
    try:
        root = ET.parse(path).getroot()
    except Exception as e:          # 🔑 XML 이 깨지면 **조용히 빈 목록**이 아니라 빨강이어야 한다
        return None, "XML 이 깨졌다: %s" % e
    out = []
    for el in root.iter(MSNS + "ClCompile"):
        inc = el.get("Include")
        if not inc or not inc.lower().endswith(".cpp"):
            continue                # ItemDefinitionGroup 안의 `<ClCompile>` 은 Include 가 없다
        # ⚠ `ExcludedFromBuild` 는 **구성마다 따로** 붙는다. 하나라도 있으면 사람이 봐야 한다.
        excl = [c.tag.replace(MSNS, "") for c in el
                if c.tag.replace(MSNS, "") == "ExcludedFromBuild"]
        out.append((os.path.basename(inc.replace("\\", "/")), bool(excl)))
    return out, None


def build_recipe_lines():
    """맥 쪽 **빌드 한 줄**들을 훑는다 → [(파일, 줄번호, {이름들}, 원문)]"""
    hits = []
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for dp, dns, fns in os.walk(base):
            dns[:] = [x for x in dns if x not in (".git", "node_modules", "run")]
            for fn in sorted(fns):
                if not fn.endswith(SCAN_EXT):
                    continue
                p = os.path.join(dp, fn)
                try:
                    lines = open(p, encoding="utf-8").read().split("\n")
                except Exception:
                    continue
                for i, ln in enumerate(lines, 1):
                    if not _CC.search(ln):
                        continue
                    # 🔴 **구문검사 줄은 빌드가 아니다** — 한 파일만 태우고 링크를 안 한다
                    if "-fsyntax-only" in ln:
                        continue
                    names = set(os.path.basename(x) for x in _CPP.findall(ln))
                    # 🔑 `.cpp` 를 **둘 이상** 부르는 줄만 빌드로 본다.
                    #   하나짜리는 산문(`server.cpp:11 이 …`)이 대부분이라 잡으면 잡음이 된다.
                    #   ⚠ 이건 **회피다** — 진짜 한 파일 빌드가 생기면 이 검사는 그것을 못 본다
                    if len(names) < 2:
                        continue
                    hits.append((os.path.relpath(p, ROOT), i, names, ln.strip()))
    return hits


def check_build_targets(files, self_test=False):
    """0 이면 통과. 화면에 값을 찍는다."""
    bad = 0
    want = set(BUILD_SOURCES)
    print("\n" + "─" * 56)
    print("② 무엇을 컴파일하나 — 선언한 빌드 대상 : %s" % " ".join(sorted(want)))
    if self_test:
        # 🔴 **빨간불이 실제로 나는지 보인다.** 초록불은 "괜찮다"와 "안 봤다"가 같은 모양이다.
        want = want | {"__없는소스__.cpp"}
        print("★ 자가시험 — 없는 소스를 선언에 끼웠다. 아래가 전부 빨강이어야 한다")

    # ── VS 쪽 둘
    for path, label in ((VCXPROJ, "vcxproj"), (FILTERS, "filters")):
        got, err = vcxproj_sources(path)
        if err:
            print("🔴 %-8s %s" % (label, err))
            bad += 1
            continue
        names = set(n for n, _ in got)
        excl = [n for n, e in got if e]
        if names != want:
            print("🔴 %-8s %s   (더 있는 것 %s · 빠진 것 %s)"
                  % (label, " ".join(sorted(names)) or "(없음)",
                     " ".join(sorted(names - want)) or "-",
                     " ".join(sorted(want - names)) or "-"))
            bad += 1
        else:
            print("✅ %-8s %s" % (label, " ".join(sorted(names))))
        if excl:
            # ⚠ 구성이 넷이라 **한 구성만 제외**하면 Release 로 넘어갈 때 조용히 TU 가 는다
            print("   🔴 %s 에 `ExcludedFromBuild` 가 붙어 있다 — **구성마다 갈린다.** "
                  "빼려면 목록에서 빼라" % " ".join(excl))
            bad += 1

    # ── 맥 쪽 빌드 줄 전부
    hits = build_recipe_lines()
    if not hits:
        # 🔑 **0 은 통과가 아니다.** 훑어서 찾는 검사는 아무것도 못 찾아도 초록이 난다.
        print("🔴 빌드 줄을 **한 줄도 못 찾았다** — 훑기가 깨졌다(창이 어긋났나?)")
        bad += 1
    else:
        okn = 0
        line_bad = 0          # 🔑 위 XML 실패와 **섞지 마라** — 섞으면 분모가 틀린다
        other = []
        for rel, i, names, raw in hits:
            # 🔵 **이 트리의 빌드가 아닌 줄**은 실패가 아니다 — 없는 `.cpp` 를 부르는 줄은
            #   대개 **실현 안 된 계획서**다(실측: `plan-windows-and-mapping.md` 의
            #   `server_posix.cpp`/`server_win.cpp` — 2026-08-15 계획 · 착수 안 됨).
            #   ⚠ 그래도 **조용히 넘기지 않는다.** 세어서 보여 준다 —
            #     §"조용한 `0` 을 내지 마라". 다음 사람이 그 계획서를 빌드 문서로 읽을 수 있다.
            ghost = sorted(n for n in names if not os.path.exists(os.path.join(SRC, n)))
            if ghost:
                other.append((rel, i, ghost))
                continue
            if names != want:
                print("🔴 %s:%d  %s\n     %s"
                      % (rel, i, " ".join(sorted(names)), raw[:76]))
                line_bad += 1
            else:
                okn += 1
        bad += line_bad
        print("%s 맥 빌드 줄 %d개 중 %d개가 선언과 같다 (이 트리 기준)"
              % ("✅" if not line_bad else "🔴", okn + line_bad, okn))
        for rel, i, ghost in other:
            print("🔵 %s:%d — 이 트리의 빌드가 아니다 (정본에 없는 파일: %s)"
                  % (rel, i, " ".join(ghost)))

    # ── 트리에 있는데 어느 빌드에도 없는 `.cpp`
    stray = [f for f in files if f.endswith(".cpp") and f not in want and f not in DOC_ONLY]
    if stray:
        print("🔴 컴파일도 안 되고 선언도 없는 `.cpp` : %s" % " ".join(stray))
        print("   ✅ 빌드 대상이면 `BUILD_SOURCES` 에, 문서면 `DOC_ONLY` 에 **이유와 함께** 적어라")
        bad += 1
    for f, why in sorted(DOC_ONLY.items()):
        if f in files:
            print("🔵 %s — 빌드 대상 아님 : %s" % (f, why))

    # ── 사본에만 있는 `.cpp` (① 은 정본 목록만 돌아서 **여분을 못 본다**)
    for dst in COPIES:
        if not os.path.isdir(dst):
            continue
        extra = sorted(f for f in os.listdir(dst)
                       if f.endswith(".cpp") and f not in files)
        if extra:
            print("🔴 %s 에만 있는 `.cpp` : %s — VS 가 정본에 없는 것을 컴파일할 수 있다"
                  % (os.path.relpath(dst, ROOT), " ".join(extra)))
            bad += 1
    return bad


def check_doc_only_compiles(self_test=False):
    """③ 빌드에서 뺐다고 **검사까지 빼면 안 된다.**

    🔴 2026-08-27 : `EXAMPLES.cpp` 를 지키는 것은 `net/sync_check.sh` 인데
      그것은 **`조별과제샘플/` 두 트리만** 본다. 정본(`서머리/server`)은 **아무도 안 지켰다.**
      ★ 오늘까지 유일하게 정본 예제를 컴파일하던 것이 **VS 의 `ClCompile` 항목**이었고,
        빌드를 맞추면서 그것을 뺐다 — 즉 **증명이 같이 사라질 뻔했다.**
      🔑 §"없앨 때, 그것이 *겸하던* 일을 세라." 그 자리를 여기가 잇는다.
    """
    bad = 0
    print("\n" + "─" * 56)
    print("③ 빌드 대상이 아닌 `.cpp` 도 **컴파일은 되나** (정본 트리 기준)")
    for f in sorted(DOC_ONLY):
        p = os.path.join(SRC, f)
        if not os.path.exists(p):
            # ⚠ 대상이 없으면 **초록이 아니라 경보다.** 검사가 스스로 무장 해제된다.
            print("🔴 %s 가 없다 — **이 검사는 지금 아무것도 안 지킨다**" % f)
            bad += 1
            continue
        cmd = ["c++", "-std=c++11", "-fsyntax-only", "-Wall", p]
        if self_test:
            # 🔑 없는 매크로를 강제로 넣어 **빨강이 나는지** 본다
            cmd = ["c++", "-std=c++11", "-fsyntax-only",
                   "-include", "__없는헤더__.h", p]
            print("★ 자가시험 — 없는 헤더를 강제로 넣었다. 아래가 빨강이어야 한다")
        try:
            r = subprocess.run(cmd, capture_output=True, text=True)
        except FileNotFoundError:
            print("⚠ %s — 컴파일러가 없다. **못 쟀다**(초록이 아니다)" % f)
            bad += 1
            continue
        if r.returncode == 0:
            print("✅ %s — 정본 `parking.h` 로 컴파일된다 (경고 0)" % f)
        else:
            print("🔴 %s — **예제가 낡았다.** 앞 3줄:" % f)
            for ln in (r.stderr or "").split("\n")[:3]:
                print("     " + ln)
            bad += 1
    return bad


def main():
    do_sync = "--sync" in sys.argv
    self_test = "--self-test" in sys.argv

    files = source_files()
    print("정본 %s — 소스 %d개" % (os.path.relpath(SRC, ROOT), len(files)))

    if self_test:
        # 🔴 **검사가 실패도 한다는 것을 보인다.** 초록불은 "괜찮다" 와 "안 봤다" 가 같은 모양이다.
        files = files + ["__없는파일__.h"]
        print("★ 자가시험 — 없는 파일 하나를 목록에 끼웠다. 아래에서 빨간불이 나야 한다.\n")

    bad = 0
    for dst in COPIES:
        rel = os.path.relpath(dst, ROOT)
        if not os.path.isdir(dst):
            print("🔴 %-26s 폴더가 없다" % rel)
            bad += 1
            continue
        missing, diff = compare(dst, files)
        if do_sync and not self_test:
            for f in missing + diff:
                shutil.copy2(os.path.join(SRC, f), os.path.join(dst, f))
            missing, diff = compare(dst, files)
            print("   (--sync 로 %d개를 맞췄다)" % (len(files) - len(missing) - len(diff)) if False else "", end="")
        if missing or diff:
            bad += 1
            print("🔴 %-26s 없는 것 %d %s · 다른 것 %d %s"
                  % (rel, len(missing), " ".join(missing), len(diff), " ".join(diff)))
        else:
            print("✅ %-26s %d개 전부 동일" % (rel, len(files)))

    # 🔑 **사본끼리도 댄다.** 정본과 각각 같으면 서로도 같아야 하는데,
    #   그 등식이 깨지면 위 비교 자체가 틀린 것이다(대조군 노릇을 한다).
    if len(COPIES) >= 2 and not self_test:
        a, b = COPIES[0], COPIES[1]
        cross = [f for f in files
                 if os.path.exists(os.path.join(a, f)) and os.path.exists(os.path.join(b, f))
                 and not filecmp.cmp(os.path.join(a, f), os.path.join(b, f), shallow=False)]
        print("%s 사본끼리 : 다른 것 %d %s"
              % ("✅" if not cross else "🔴", len(cross), " ".join(cross) or ""))
        if cross:
            bad += 1

    copy_bad = bad
    # 🔴 ①(내용) 이 통과해도 ②(빌드 목록)·③(문서 예제) 는 따로 물어야 한다.
    #   **셋은 다른 물음이다** — 하나가 초록이라고 나머지를 안 보면 오늘 일이 또 난다.
    bad += check_build_targets(source_files(), self_test)
    bad += check_doc_only_compiles(self_test)

    print("\n" + "=" * 56)
    if bad:
        if copy_bad:
            print("🔴 실패 — 사본 %d개가 정본과 갈렸다." % copy_bad)
            print("   ✅ 맞추려면 : python3 net/release_sync_check.py --sync")
            print("   ⚠ 방향은 한쪽뿐이다 — 정본은 `서머리/server` 다. 사본을 고쳐서 원본을 바꾸지 마라")
        if bad > copy_bad:
            print("🔴 실패 — **빌드 대상 또는 예제**가 어긋났다(위 ②·③).")
            print("   ⚠ `--sync` 는 이걸 못 고친다. 고칠 곳은 `.vcxproj`·`.filters`·빌드 줄이다")
        return 1
    print("✅ 통과 — 사본 %d개가 정본과 같고, 빌드 대상도 같다" % len(COPIES))
    print("   ⚠ 이것은 **소스**만 본다. 화면(`.html`)은 `web/tools/release-sync.mjs` 가 본다")
    print("   🔴 그리고 **맥에서 잰 것이다.** MSVC 가 같은 답을 낸다는 뜻이 아니다")
    return 0


if __name__ == "__main__":
    sys.exit(main())
