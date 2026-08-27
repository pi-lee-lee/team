#!/usr/bin/env python3
# net/flowcheck/contract_check.py — **계약 문서와 코드가 같은 말을 하나**
#
# 🔴 왜 필요한가 (2026-08-27 에 두 번 데였다)
#   ① 형식을 `SPEC-…` 에만 적고 `LOG-GREP.md` 를 빠뜨려 **monitor 파서가 내 출력을 못 읽었다**
#   ② 하니스의 `logHas("…")` 는 **내가 쓴 로그를 내가 쓴 검사로** 본다 —
#      문구를 틀리게 써도 **초록**이다. 그런데 그 문구는 **monitor 와의 계약**이다.
#
# > ★ **생산자의 검사는 소비자를 대신 못 한다.**
#
# 그래서 이 검사는 **손으로 유지되는 두 파일이 서로를 검산**하게 한다.
# 자기가 넣은 값을 자기가 읽는 동어반복이 아니다 — 한쪽만 고치면 **빨간불이 난다.**
#
# 쓰기 : python3 net/flowcheck/contract_check.py
#        python3 net/flowcheck/contract_check.py --self-test   # 🔴 검사가 실패도 하는지 보인다
#
# 나가는 값 : 어긋난 것이 있으면 1, 없으면 0

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONTRACT = os.path.join(ROOT, "docs", "net", "LOG-GREP.md")
HARNESS = os.path.join(ROOT, "net", "flowcheck", "flow26.cpp")
SRCDIR = os.path.join(ROOT, "서머리", "server")

# 표의 "안전 조각" 칸에서만 뽑는다. 다른 칸의 백틱(정규식·상수)까지 뽑으면 잡음이 된다.
FRAG_CELL = 1          # | 사건 | 🔵 안전 조각 | 뜻 |
SKIP_CELL_MARK = "로그 없음"


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def contract_fragments(text):
    """계약 문서의 안전 조각 표에서 조각을 뽑는다. `/` 로 나뉜 칸은 둘 다 센다."""
    out = []
    in_table = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("|") and s.count("|") >= 3:
            cells = [c.strip() for c in s.strip("|").split("|")]
            # 머리줄·구분줄은 건너뛴다
            if not cells or set(cells[0]) <= set("-: "):
                in_table = True
                continue
            if cells[0] in ("사건", "모양", "뽑을 것"):
                in_table = True
                continue
            if not in_table or len(cells) <= FRAG_CELL:
                continue
            cell = cells[FRAG_CELL]
            if SKIP_CELL_MARK in cell:
                continue
            # 🔴 취소선은 **은퇴한 조각**이다 — 규약대로 지우지 않고 남겨 둔 것이라 검사 대상이 아니다.
            #   ⚠ 지우면 다음 사람이 옛 로그를 그 조각으로 긁다가 같은 곳에서 넘어진다.
            cell = re.sub(r"~~`[^`]+`~~", "", cell)
            for m in re.finditer(r"`([^`]+)`", cell):
                frag = m.group(1)
                # 정규식 칸(`노드 (P\d)` 같은 것)은 조각이 아니다
                if any(ch in frag for ch in "\\^$*+?[]()"):
                    continue
                out.append(frag)
        else:
            in_table = False
    # 순서를 지키면서 중복 제거
    seen, uniq = set(), []
    for f in out:
        if f not in seen:
            seen.add(f)
            uniq.append(f)
    return uniq


def harness_fragments(text):
    return sorted(set(re.findall(r'logHas\(\s*"([^"]+)"', text)))


def server_text():
    buf = []
    for name in sorted(os.listdir(SRCDIR)):
        if name.endswith((".h", ".cpp")):
            buf.append(read(os.path.join(SRCDIR, name)))
    return "\n".join(buf)


def logf_tags(src):
    """`logf("←ARD", …)` 의 **태그**를 모은다.

    🔴 이것이 없으면 검사기가 틀린 빨간불을 낸다. 로그 한 줄은 **태그와 본문이 따로**
    쓰여서(`logf(태그, 본문)`) `←ARD P` 같은 조각은 소스 어디에도 **한 덩어리로 없다.**
    ★ 첫 실행에서 이것 때문에 8개가 잘못 걸렸다 — **맞는 입력으로 먼저 돌려서** 잡았다.
    """
    return sorted(set(re.findall(r'logf\(\s*"([^"]{1,8})"', src)), key=len, reverse=True)


# 🔴 **소스에 있을 수 없는 조각.** 전선에서 온 것을 그대로 찍는 자리다.
#   ⚠ 목록으로 봐주는 것이라 **늘리기 전에 한 번 더 생각해라** — 여기 넣으면 그 조각은 영영 안 본다.
WIRE_FRAGS = {
    " S,": "보드가 보낸 프레임을 그대로 찍는다(`logf(\"←ARD\", b)`) — 상수가 아니다",
}


def found(frag, src, tags):
    """조각이 소스에서 만들어질 수 있나. 만들어질 수 있으면 (True, 어느 규칙으로).

    🔴 **런타임 값을 상수로 찾지 마라** — `errno=54` 의 54 는 소스에 없다. 있을 수도 없다.
    """
    if frag in WIRE_FRAGS:
        return True, "전선 값(%s)" % WIRE_FRAGS[frag]
    if frag in src:
        return True, "그대로"
    # 태그로 시작하면 태그와 본문을 갈라서 본다
    for t in tags:
        if frag.startswith(t):
            rest = frag[len(t):].lstrip()
            if not rest or rest in src:
                return True, "태그+본문(%s)" % t
    # 숫자는 런타임 값이다. 숫자를 뺀 뼈대가 있으면 만들어질 수 있다
    skel = re.sub(r"\d+", "", frag)
    if skel != frag and skel.strip() and skel in src:
        return True, "런타임 숫자 제외"
    return False, ""


def main():
    self_test = "--self-test" in sys.argv

    src = server_text()
    contract = contract_fragments(read(CONTRACT))
    harness = harness_fragments(read(HARNESS))

    if self_test:
        # 🔴 **검사가 실패도 한다는 것을 보인다.** 초록불은 "괜찮다" 와 "안 봤다" 가 같은 모양이다.
        contract = contract + ["이 문구는 서버 어디에도 없다"]
        print("★ 자가시험 — 없는 조각 하나를 계약에 끼워 넣었다. 아래에서 빨간불이 나야 한다.\n")

    print("계약 조각 %d개 · 하니스 검사 %d개 · 서버 소스 %d자\n" % (len(contract), len(harness), len(src)))

    # ── ① 계약이 약속한 문구를 **코드가 실제로 내나**
    tags = logf_tags(src)
    missing = [f for f in contract if not found(f, src, tags)[0]]
    print("① 계약 → 코드 : 약속한 문구가 소스에 있나")
    if missing:
        print("   🔴 소스에 없는 조각 %d개 — **계약이 코드보다 앞서 있거나 낡았다**" % len(missing))
        for f in missing:
            print("      · %r" % f)
    else:
        print("   ✅ %d개 전부 소스에 있다" % len(contract))

    # ── ② 하니스가 검사하는 문구를 **계약이 알고 있나**
    #    ⚠ 하니스는 계약보다 넓다(내부 문구도 본다). 그래서 이것은 **경고**지 실패가 아니다.
    unknown = [f for f in harness if not any(c in f or f in c for c in contract)]
    print("\n② 하니스 → 계약 : 검사하는 문구를 계약이 아나  ⚠ 참고용")
    print("   계약에 없는 문구 %d개 (하니스는 내부 문구도 본다 — 전부 실패는 아니다)" % len(unknown))
    for f in unknown:
        print("      · %r" % f)

    # ── ③ 계약에 있는데 **하니스가 한 번도 안 밟는** 것
    untested = [c for c in contract if not any(c in h or h in c for h in harness)]
    print("\n③ 계약 → 하니스 : 아무 검사도 안 밟는 조각  ⚠ 참고용")
    print("   %d개 — 문구가 바뀌어도 하니스는 조용하다" % len(untested))
    for f in untested:
        print("      · %r" % f)

    # ── ④ 🔴 **실기 로그에 한 번이라도 찍혔나** — ③ 보다 훨씬 강한 자다
    #    하니스가 안 밟는 것은 "안 밟기로 한 것" 일 수 있다. 그런데 **실기에서 한 번도 안 찍힌 것**은
    #    ★ **도달 불가 갈래**의 서명이다 — 2026-08-27 의 `keepalive_reaps` 가 정확히 그거였다.
    #    ⚠ 다만 `0` 은 두 가지다: **못 온다**(도달 불가) 와 **그 상황이 안 왔다**(자극 없음).
    #      값이 가르지 못한다. 그래서 **후보로만** 내고 사람이 판정한다.
    logdir = os.path.join(ROOT, "net", "run")
    logs = []
    if os.path.isdir(logdir):
        for n in sorted(os.listdir(logdir)):
            if n.endswith(".log"):
                try:
                    logs.append(read(os.path.join(logdir, n)))
                except (OSError, UnicodeDecodeError):
                    pass
    print("\n④ 계약 → 실기 로그 : 한 번이라도 찍힌 적이 있나  🔴 도달 불가 후보")
    if not logs:
        print("   ⚠ 로그를 못 읽었다 — **`0/0` 이다.** 이 항목은 판정에서 빼라")
    else:
        blob = "\n".join(logs)
        never = [f for f in contract if f not in WIRE_FRAGS and f not in blob]
        print("   로그 %d개 · 한 번도 안 찍힌 조각 **%d개**" % (len(logs), len(never)))
        for f in never:
            print("      🔴 %r" % f)
        print("   ⚠ `0` 은 **도달 불가**와 **자극 없음** 둘 다 뜻한다. 값이 안 가른다 — 사람이 판정해라")
        print("   ✅ 가르는 법 : **도는 산출물에 물어라** — 그 문구를 낼 수 있는 바이너리였나")
        print("      🔴 `grep -ac` 를 쓰지 마라. 바이너리에서 **다른 것을 센다**(실측 2026-08-27):")
        print("         `연결 종료` → grep -ac **2** · 파이썬 bytes **8**   (`-c` 는 *줄* 을 센다)")
        print("         ⚠ 로케일에 따라 **조용히 0** 이 되기도 한다 — 그러면 있는 것이 '미배포' 로 분류된다")
        print("      ✅ python3 -c \"print(open(BIN,'rb').read().count('<조각>'.encode()))\"")
        print("      🔑 소스 grep 으로는 못 가른다. 소스에는 **아직 안 나간 것도 있다**")
        print("      🔴 그리고 **조립되는 문자열은 이 방법 밖이다** — `logf(태그, 본문)` 으로")
        print("         만들어지는 줄은 **통짜로 바이너리에 없다.** 그 `0` 은 **`0/0`** 이지 '없다' 가 아니다")

    print("\n" + "═" * 60)
    if missing:
        print("🔴 실패 — ① 에서 %d개. **계약과 코드가 다른 말을 한다**" % len(missing))
        return 1
    print("✅ 통과 — 계약이 약속한 문구를 코드가 전부 낸다")
    print("   ⚠ 다만 ②③ 은 실패가 아니라 **덮이지 않은 자리**다. 0 이 목표가 아니다")
    return 0


if __name__ == "__main__":
    sys.exit(main())
