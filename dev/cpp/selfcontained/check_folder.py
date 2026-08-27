#!/usr/bin/env python3
"""폴더가 **자족적인가**를 기계로 검증한다 — `five` 요구사항용.

요구사항(사용자): *"해당 폴더 내에는 필요한 모든 파일을 복사하여 **그 폴더의 파일들만으로**
아두이노 플래싱과 테스트 서버 기동까지 가능해야 한다"*

★ 이건 사람이 눈으로 못 지킨다. `#include` 하나가 상위를 가리켜도 **개발 머신에서는 그 파일이
있어서 빌드가 된다.** 깨지는 곳은 파일을 옮긴 뒤 — 즉 **2차 실기 날 아침의 다른 PC** 다.
그래서 여기서 하는 일은 `winparse` 와 같은 계열이다: **없는 조건을 만들어서 본다.**

무엇을 보나
  A. 정적 — 폴더 안 소스에서 시작한 `#include "..."` **추이 폐포**가 폴더를 벗어나나
  B. 정적 — include 문자열 자체에 `../` 나 절대경로가 있나
  C. 격리 — 폴더만 **저장소 밖 임시 위치로 복사**해 실제로 빌드해 본다
  D. 음성 대조 — 파일 하나를 일부러 빼고 돌려서 **정말 실패하는지** 확인한다

  🔑 `#include <...>` 는 툴체인 몫이라 폴더 밖이어도 정상이다. 다만 **무엇을 요구하는지 목록으로
     낸다** — 대상 PC 에 그것이 있는지는 사람이 확인해야 하는 축이고, 조용히 넘기면 안 된다.

사용법
  python3 cpp/selfcontained/check_folder.py <폴더> [--build cpp|arduino|none] [--fqbn <보드>]

나가는 값
  0 = 자족적이다 · 1 = 아니다 · 2 = 검사 자체를 못 했다(입력·도구 문제)
  ⚠ **0 을 "빌드된다"로 읽지 마라.** `--build none` 이면 정적 검사만 한 것이다.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

INC_LOCAL = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
COND_OPEN = re.compile(r'^\s*#\s*(if|ifdef|ifndef)\b')
COND_CLOSE = re.compile(r'^\s*#\s*endif\b')
GUARD_IFNDEF = re.compile(r'^\s*#\s*ifndef\s+(\w+)')
GUARD_DEFINE = re.compile(r'^\s*#\s*define\s+(\w+)')


def local_includes_with_depth(text):
    """`#include "..."` 를 **전처리 조건 깊이**와 함께 뽑는다.

    🔴 왜 필요한가 — 벤더링한 라이브러리는 아키텍처마다 다른 헤더를 조건부로 들인다.
      예: Servo.h 가 `#elif defined(ARDUINO_ARCH_SAM)` 아래에서 `"sam/ServoTimers.h"` 를 부른다.
      AVR 대상에서는 **그 줄이 애초에 안 읽힌다.** 그것을 "없는 파일" 로 세면
      벤더링한 폴더가 전부 🔴 로 나오고, 그러면 아무도 이 도구를 안 믿는다.
      (툴체인 부재를 자족성 실패와 가른 것과 **같은 이유**다)

    ⚠ 헤더 가드(`#ifndef X_H` + `#define X_H`)는 파일 전체를 깊이 1 로 만든다.
      그러면 모든 include 가 조건부가 되어 검사가 통째로 무뎌진다 — 가드는 빼고 센다.
    """
    lines = text.splitlines()
    guard = None
    for i, ln in enumerate(lines[:40]):
        m = GUARD_IFNDEF.match(ln)
        if m:
            for nxt in lines[i + 1:i + 3]:
                d = GUARD_DEFINE.match(nxt)
                if d and d.group(1) == m.group(1):
                    guard = i
            break
        if ln.strip() and not ln.strip().startswith(("//", "/*", "*", "#pragma")):
            break

    out, depth = [], 0
    for i, ln in enumerate(lines):
        if COND_OPEN.match(ln):
            depth += 1
            if i == guard:
                depth -= 1          # 헤더 가드는 안 센다
                guard = -1
                continue
        elif COND_CLOSE.match(ln):
            depth = max(0, depth - 1)
        m = INC_LOCAL.match(ln)
        if m:
            out.append((m.group(1), depth))
    return out
INC_SYS = re.compile(r'^\s*#\s*include\s*<([^>]+)>', re.M)
SRC_EXT = (".c", ".cc", ".cpp", ".cxx", ".ino")
HDR_EXT = (".h", ".hpp", ".hxx", ".inl")


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def list_files(root, exts):
    out = []
    for base, _dirs, files in os.walk(root):
        for name in files:
            if name.lower().endswith(exts):
                out.append(os.path.join(base, name))
    return sorted(out)


def closure(root):
    """폴더 안 소스에서 시작한 `#include "..."` 추이 폐포를 따라간다.

    돌려주는 것: (방문한 파일, 폴더 밖으로 나간 것, 못 찾은 것, 요구한 시스템 헤더)
    """
    roots = list_files(root, SRC_EXT)
    seen, outside, missing, sysinc, conditional = set(), [], [], set(), []
    stack = list(roots)
    while stack:
        cur = stack.pop()
        real = os.path.realpath(cur)
        if real in seen:
            continue
        seen.add(real)
        try:
            text = read(cur)
        except OSError as e:
            missing.append((cur, "<읽기실패>", str(e)))
            continue
        for m in INC_SYS.finditer(text):
            sysinc.add(m.group(1))
        for inc, depth in local_includes_with_depth(text):
            # 컴파일러와 같은 순서: 먼저 그 파일이 있는 디렉터리, 그다음 폴더 루트
            cands = [os.path.join(os.path.dirname(cur), inc), os.path.join(root, inc)]
            hit = next((c for c in cands if os.path.isfile(c)), None)
            if hit is None:
                if depth > 0:
                    # 조건부다 — 이 대상에서는 안 읽힐 수 있다. **실패로 세지 않는다**
                    conditional.append((cur, inc))
                else:
                    missing.append((cur, inc, "폴더 안에서 못 찾았다"))
                continue
            if not os.path.realpath(hit).startswith(os.path.realpath(root) + os.sep):
                outside.append((cur, inc, os.path.realpath(hit)))
                continue
            stack.append(hit)
    return sorted(seen), outside, missing, sorted(sysinc), conditional


def suspicious_paths(root):
    """include 문자열 자체가 폴더 밖을 가리키는 모양인가 — 해석 전에 잡는다."""
    bad = []
    for f in list_files(root, SRC_EXT + HDR_EXT):
        for m in INC_LOCAL.finditer(read(f)):
            inc = m.group(1)
            if inc.startswith("..") or inc.startswith("/") or re.match(r"^[A-Za-z]:[\\/]", inc):
                bad.append((f, inc))
    return bad


# ── MSVC 프로젝트 일관성 ─────────────────────────────────────────────────────
# 🔴 이 축은 원래 이 도구가 **안 보던 것**이다. 2026-08-25 에 `five/` 를 손으로 훑다가
#   결함 둘을 찾았고(GUID 가 1차와 같음 · 폴더에 솔루션 없음), 손으로 찾은 것은 **다음에 또 안 된다.**
#   그래서 넣는다 — 요구가 *"그 폴더의 파일들만으로 기동"* 이면 프로젝트 파일도 그 요구에 걸린다.
PROJ_ITEM = re.compile(r'<Cl(?:Compile|Include)\s+Include="([^"]+)"')
PROJ_GUID = re.compile(r"<ProjectGuid>\{?([0-9A-Fa-f-]+)\}?</ProjectGuid>")
SLN_PROJ = re.compile(r'Path="([^"]+)"|"[^"]*\.vcxproj"')


def check_msvc_project(root):
    """`.vcxproj` 가 폴더와 맞나 · 솔루션이 폴더 안을 가리키나. (문제목록, 정보목록)"""
    problems, notes = [], []
    projs = [f for f in os.listdir(root) if f.lower().endswith(".vcxproj")]
    if not projs:
        return problems, notes

    for pj in projs:
        text = read(os.path.join(root, pj))
        listed = set(PROJ_ITEM.findall(text))
        actual = {f for f in os.listdir(root) if f.lower().endswith((".cpp", ".cc", ".cxx", ".c",
                                                                    ".h", ".hpp", ".hxx"))}
        gone = sorted(listed - actual)
        unlisted = sorted(actual - listed)
        if gone:
            problems.append("%s 에 적혀 있는데 폴더에 **없다**: %s" % (pj, ", ".join(gone)))
        if unlisted:
            problems.append("%s 폴더에 있는데 프로젝트에 **안 적혀 있다**: %s" % (pj, ", ".join(unlisted)))
        if not gone and not unlisted:
            notes.append("%s 의 파일 목록이 폴더와 **정확히 일치**(%d개)" % (pj, len(listed)))
        g = PROJ_GUID.search(text)
        if g:
            notes.append("%s ProjectGuid = {%s}" % (pj, g.group(1).upper()))
            notes.append("  ⚠ **다른 프로젝트와 겹치면 한 솔루션에 같이 못 넣는다.** 이 도구는"
                         " 폴더 안만 보므로 **겹침 여부는 저장소를 훑어야 안다**(사람 축)")

    slns = [f for f in os.listdir(root) if f.lower().endswith((".sln", ".slnx"))]
    if not slns:
        problems.append("폴더 안에 **솔루션 파일이 없다** — `.vcxproj` 를 직접 열어야 한다."
                        " 상위 솔루션을 같이 가져가면 **없는 폴더**를 가리킬 수 있다")
    else:
        for sn in slns:
            text = read(os.path.join(root, sn))
            refs = re.findall(r'Path="([^"]+)"', text) or re.findall(r'"([^"]*\.vcxproj)"', text)
            outside = [r for r in refs if r.startswith("..") or "/" in r.replace("\\", "/").rstrip("/")
                       and not os.path.isfile(os.path.join(root, r))]
            if outside:
                problems.append("%s 가 **폴더 밖 프로젝트**를 가리킨다: %s" % (sn, ", ".join(outside)))
            else:
                notes.append("%s → %s (폴더 안)" % (sn, ", ".join(refs) if refs else "(참조 못 읽음)"))
    return problems, notes


def isolated_copy(root):
    """저장소 **밖** 임시 위치로 폴더만 복사한다. 이것이 이 도구의 핵심이다."""
    tmp = tempfile.mkdtemp(prefix="selfcontained-")
    dst = os.path.join(tmp, os.path.basename(os.path.normpath(root)))
    shutil.copytree(root, dst)
    return tmp, dst


# 🔴 **자족성 실패**와 **툴체인 없음**을 갈라야 한다. 안 가르면 윈도우용 폴더가 맥에서
#   전부 "안 닫혔다" 로 나오고, 그러면 아무도 이 도구를 안 믿는다.
#   판별: 못 찾은 것이 `<...>` 인가(툴체인 몫) `"..."` 인가(폴더 몫).
SYS_MISS = re.compile(r"fatal error: '([^']+)' file not found")

# 아두이노 코어가 **같이 오는** 헤더들. 이 목록 밖의 `<...>` 는 **라이브러리 설치가 필요**하고,
# 그것은 "폴더만 복사하면 플래싱된다" 를 깨는 조건이다 — 조용히 넘기면 안 된다.
# 🔑 그래서 시스템 헤더를 한 덩어리로 뭉치지 않고 **코어/라이브러리로 가른다.**
ARDUINO_CORE = {
    "Arduino.h", "Stream.h", "Print.h", "Client.h", "Server.h", "Udp.h", "HardwareSerial.h",
    "SoftwareSerial.h", "Wire.h", "SPI.h", "EEPROM.h", "WString.h", "USBAPI.h", "binary.h",
    "new.h", "pins_arduino.h",
}
ARDUINO_CORE_PREFIX = ("avr/", "util/")
ARDUINO_C_STD = {
    "stdio.h", "stdlib.h", "string.h", "inttypes.h", "stdint.h", "math.h", "ctype.h",
    "limits.h", "stdarg.h", "stddef.h", "stdbool.h",
}


def unused_libraries(root, libs):
    """요구는 하는데 **폴더 안에서 쓰이지 않는** 라이브러리를 찾는다.

    🔑 왜 값이 있나 — 안 쓰는 `#include` 는 공짜가 아니다. arduino-cli 는 그 헤더를 보고
      **라이브러리를 빌드에 넣고**, 라이브러리의 **ISR·전역은 참조 여부와 무관하게 링크된다**
      (`--gc-sections` 도 ISR 은 못 뺀다 — 벡터 테이블이 잡고 있다).
      실측 2026-08-25(arduino): `<Servo.h>` 한 줄을 지우자 **보드마다 flash 200 B** 가 줄었다.
    ★ 그리고 그 한 줄이 "이 폴더는 라이브러리 설치가 선행 조건" 을 만든다 —
      **안 쓰는데 자족성을 깨는 것**이라 지우면 두 가지가 같이 닫힌다.

    ⚠ 판정을 실패로 만들지 않는다. 휴리스틱이다(헤더 이름과 식별자가 다른 라이브러리가 있다).
      **"지울 수 있어 보인다"** 까지만 말하고 판단은 사람에게 넘긴다.
    """
    text = "\n".join(
        "\n".join(ln for ln in read(f).splitlines() if not re.match(r"\s*#\s*include", ln))
        for f in list_files(root, SRC_EXT + HDR_EXT))
    out = []
    for h in libs:
        name = os.path.basename(h)
        for ext in (".h", ".hpp"):
            if name.endswith(ext):
                name = name[: -len(ext)]
        if not re.search(r"\b%s\b" % re.escape(name), text):
            out.append(h)
    return out


def split_arduino_includes(sysinc):
    """`<...>` 를 (코어·표준, **라이브러리 설치 필요**) 로 가른다."""
    core, libs = [], []
    for h in sysinc:
        if (h in ARDUINO_CORE or h in ARDUINO_C_STD
                or h.startswith(ARDUINO_CORE_PREFIX)):
            core.append(h)
        else:
            libs.append(h)
    return sorted(core), sorted(libs)


def build_cpp(folder, extra_include=None):
    """폴더 안의 .cpp 전부를 그 자리에서 **구문 검사**한다.

    ⚠ 링크가 아니라 구문 검사다 — 자족성은 "필요한 파일이 다 있나" 이지 "여기서 실행되나" 가 아니다.
      (실기 대상은 MSVC/윈도우다. 여기서 도는지는 별개 축이고 winparse 가 본다)
    """
    srcs = [p for p in list_files(folder, (".cpp", ".cc", ".cxx", ".c")) if os.path.isfile(p)]
    if not srcs:
        return None, "빌드할 .cpp 가 없다"
    cmd = ["c++", "-std=c++20", "-fsyntax-only", "-I", folder]
    if extra_include:
        cmd += ["-I", extra_include]
    cmd += srcs
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0, (r.stderr or r.stdout)[-4000:]


def build_arduino(folder, fqbn):
    ino = list_files(folder, (".ino",))
    if not ino:
        return None, ".ino 가 없다"
    if shutil.which("arduino-cli") is None:
        return None, "arduino-cli 가 이 기기에 없다 — **검사 못 했다**(0 으로 읽지 마라)"
    r = subprocess.run(["arduino-cli", "compile", "--fqbn", fqbn, folder],
                       capture_output=True, text=True)
    return r.returncode == 0, (r.stderr or r.stdout)[-4000:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--build", choices=["cpp", "arduino", "none"], default="none")
    ap.add_argument("--fqbn", default="arduino:avr:uno")
    ap.add_argument("--keep", action="store_true", help="격리 사본을 지우지 않는다(조사용)")
    ap.add_argument("--toolchain-shim", default=None,
                    help="이 기기에 없는 대상 툴체인 헤더를 대신할 디렉터리"
                         "(예: cpp/winparse/shim — 윈도우 대상 폴더를 맥에서 볼 때)")
    args = ap.parse_args()

    root = os.path.abspath(args.folder)
    if not os.path.isdir(root):
        print("🔴 폴더가 없다: %s" % root)
        return 2

    print("자족성 검사 — %s" % root)
    print("=" * 72)

    ok = True

    # ── A. include 추이 폐포 ────────────────────────────────────────────────
    visited, outside, missing, sysinc, conditional = closure(root)
    print("A. include 추이 폐포 — 따라간 파일 %d개" % len(visited))
    if outside:
        ok = False
        print("   🔴 **폴더 밖을 가리키는 include %d건**" % len(outside))
        for f, inc, hit in outside:
            print("      %s\n        #include \"%s\"  →  %s" % (os.path.relpath(f, root), inc, hit))
    if missing:
        ok = False
        print("   🔴 **못 찾은 include %d건**" % len(missing))
        for f, inc, why in missing:
            print("      %s : \"%s\" — %s" % (os.path.relpath(f, root), inc, why))
    if not outside and not missing:
        print("   ✅ 모든 **무조건** 지역 include 가 폴더 안에서 해결된다")
    if conditional:
        print("   ⚠ 조건부(`#if` 안) 미해결 %d건 — **이 대상에서는 안 읽힐 수 있다. 실패로 안 센다**"
              % len(conditional))
        for f, inc in conditional[:6]:
            print("      %s : \"%s\"" % (os.path.relpath(f, root), inc))
        if len(conditional) > 6:
            print("      … 외 %d건" % (len(conditional) - 6))
        print("      🔑 대개 벤더링한 라이브러리의 **다른 아키텍처 분기**다."
              " 대상 보드 분기가 폴더 안에 있는지만 확인하면 된다")

    # ── B. 의심스러운 경로 모양 ────────────────────────────────────────────
    bad = suspicious_paths(root)  # noqa: E501
    if bad:
        ok = False
        print("B. 🔴 **상위/절대 경로 include %d건**" % len(bad))
        for f, inc in bad:
            print("      %s : \"%s\"" % (os.path.relpath(f, root), inc))
    else:
        print("B. ✅ `../` · 절대경로 include 없음")

    # ── 시스템 헤더는 결함이 아니다. 다만 **목록으로 낸다** ─────────────────
    print("C. 툴체인에 요구하는 시스템 헤더 %d종 — 폴더 밖이지만 **정상**이다" % len(sysinc))
    print("   %s" % (", ".join(sysinc) if sysinc else "(없음)"))
    print("   ⚠ 대상 PC 에 이것들이 있는지는 **사람이 확인하는 축**이다. 이 도구는 실물을 안 본다")
    if list_files(root, (".ino",)):
        core, libs = split_arduino_includes(sysinc)
        print("   ├ 코어·표준(코어와 같이 온다) : %s" % (", ".join(core) if core else "(없음)"))
        if libs:
            print("   └ 🔴 **라이브러리 설치가 필요하다** : %s" % ", ".join(libs))
            unused = unused_libraries(root, libs)
            if unused:
                print("      ★ 그중 **폴더 안에서 안 쓰이는 것으로 보인다** : %s" % ", ".join(unused))
                print("         → include 를 지우면 자족성과 크기가 **같이** 닫힌다.")
                print("            안 쓰는 include 도 공짜가 아니다 — 라이브러리의 ISR·전역은")
                print("            참조 여부와 무관하게 링크된다(실측: Servo 한 줄 = flash 200 B)")
                print("         ⚠ 휴리스틱이다(헤더 이름과 식별자가 다를 수 있다). 판정에는 안 넣는다")
            print("      → \"폴더만 복사하면 플래싱된다\" 가 이 목록만큼 **안 참이다.**")
            print("         대상 PC 에서 `arduino-cli lib install <이름>` 이 먼저다.")
            print("         (또는 그 라이브러리를 폴더 안으로 벤더링한다 — 그러면 진짜로 닫힌다)")
        else:
            print("   └ ✅ 외부 라이브러리 요구 없음 — 코어만으로 플래싱된다")

    # ── C-2. MSVC 프로젝트 파일 ────────────────────────────────────────────
    mp, mn = check_msvc_project(root)
    if mp or mn:
        print("C-2. MSVC 프로젝트 파일")
        for n in mn:
            print("   ✅ %s" % n if not n.startswith("  ") else "   %s" % n)
        for x in mp:
            ok = False
            print("   🔴 %s" % x)

    # ── D. 격리 빌드 + 음성 대조 ──────────────────────────────────────────
    if args.build != "none":
        tmp, dst = isolated_copy(root)
        print("D. 격리 빌드 — 저장소 **밖** 으로 복사: %s" % dst)
        try:
            shim = os.path.abspath(args.toolchain_shim) if args.toolchain_shim else None
            if args.build == "cpp":
                runner = lambda p: build_cpp(p, shim)
            else:
                runner = lambda p: build_arduino(p, args.fqbn)
            res, log = runner(dst)
            if res is None:
                print("   ⚠ **검사 못 했다**: %s" % log)
            elif res:
                print("   ✅ 격리 위치에서 빌드 통과%s" % (" (툴체인 shim 사용)" if shim else ""))
            else:
                # 🔑 못 찾은 것이 시스템 헤더뿐이면 그건 **이 기기에 대상 툴체인이 없는 것**이다.
                #   자족성 실패가 아니다. 갈라서 말한다 — 안 가르면 이 도구를 아무도 안 믿는다.
                miss = set(SYS_MISS.findall(log))
                only_toolchain = bool(miss) and miss.issubset(set(sysinc))
                if only_toolchain:
                    print("   ⚠ 격리 빌드가 **대상 툴체인 부재로** 멈췄다 — 자족성 실패가 **아니다**")
                    print("      못 찾은 시스템 헤더: %s" % ", ".join(sorted(miss)))
                    print("      → `--toolchain-shim cpp/winparse/shim` 으로 다시 돌리면 이 축이 열린다")
                else:
                    ok = False
                    print("   🔴 격리 위치에서 **빌드 실패** — 폴더가 안 닫혀 있다")
                    print("   " + log.replace("\n", "\n   ")[:1500])

            # ★ 음성 대조 — 이게 없으면 위 ✅ 는 "검사가 돌았다"를 증명하지 못한다
            if res:
                hdrs = list_files(dst, HDR_EXT)
                if hdrs:
                    victim = max(hdrs, key=lambda p: os.path.getsize(p))
                    os.remove(victim)
                    res2, _ = runner(dst)
                    if res2 is False:
                        print("   ✅ 음성 대조 — `%s` 를 빼니 **실패했다**(검사에 이빨이 있다)"
                              % os.path.relpath(victim, dst))
                    else:
                        ok = False
                        print("   🔴 음성 대조 실패 — 헤더를 빼도 통과한다. **이 검사는 아무것도 안 보고 있다**")
                else:
                    print("   ⚠ 음성 대조 못 했다 — 뺄 헤더가 없다")
        finally:
            if not args.keep:
                shutil.rmtree(tmp, ignore_errors=True)
    else:
        print("D. ⚠ 격리 빌드를 **안 돌렸다**(--build none). 정적 검사만 한 것이다")

    print("=" * 72)
    print("판정: %s" % ("✅ 자족적이다" if ok else "🔴 자족적이지 않다"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
