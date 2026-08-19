#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""기동 로그 검사 — **자가검증이 원리적으로 못 보는 것**을 본다.

🔴 왜 이 도구가 필요한가 (원장 §"시험 경로 ≠ 실기 경로" ③)

  자가검증은 `openPorts()` 를 안 탄다(포트를 잡아야 하므로). 그래서 **기동 경로가
  무엇을 부르는지 못 본다.** 2026-08-18 에 `build_default_zones()` 가 정확히 그 구멍으로
  빠졌다 — **자가검증이 스스로 지형을 만들어 써서 전부 통과하고 실기만 비었다.**
  배포하고 로그에 `지형 판` 줄이 없는 것을 보고서야 알았다.

  > **③을 잡는 방법은 시험을 보는 것이 아니라 *실기 기동 로그* 를 보는 것이다.
  >  "기동했을 때 무엇이 찍혀야 하는가"를 미리 정해 두면 빈 것이 보인다.**

  이 도구가 그 "미리 정해 둔 것"이다. 2026-08-19 까지는 사람이 눈으로 했다 —
  **눈으로 한 것은 다음 사람이 안 한다.**

⚠ **이 도구는 시험 인스턴스만 띄운다.** 운영 포트를 절대 안 잡는다(`--port-offset` 필수).

사용:
    python3 net/startup_check.py --bin 조별과제샘플/server/server_test --offset 300
"""
import argparse
import os
import re
import subprocess
import sys
import time

# ─────────────────────────────────────────────────────────────────────────
# 🔴 **기동했을 때 찍혀야 하는 것.** 하나라도 없으면 그 경로가 안 불린 것이다.
#
# ⚠ **여기에 줄을 더할 때는 "그 줄이 없으면 무엇이 조용히 안 도는가"를 같이 적어라.**
#   이유가 없는 항목은 다음 사람이 못 지운다(지워도 되는지 판단할 근거가 없어서).
# ⚠ 패턴은 **느슨하게** 잡는다. 문구를 다듬을 때마다 이 도구가 깨지면 아무도 안 쓴다 —
#   🔑 **바뀌면 안 되는 최소한**만 건다.
REQUIRED = [
    (r"=== INSTANCE ",
     "인스턴스 경계 줄. 없으면 로그에 판·pid·cwd 가 안 남아 **어느 판이 돌았는지 사후에 못 센다**"),
    (r"rid 계약 —",
     "rid 공간·격리 계약. 없으면 `alloc_rid` 경로가 안 선 것이다"),
    (r"rid 커서 (이어받음|임의 지점)",
     "rid 커서 적재. **없으면 재기동마다 rid 가 겹쳐 장치 멱등 캐시가 옛 ACK 를 되돌려준다**"),
    (r"노드 대장 —",
     "노드 대장 적재(`ledger_load`). 없으면 **재기동할 때마다 누가 있었는지 다 잊는다**"),
    (r"조립 표 검사 —",
     "조립 표 검사. 없으면 **기여자의 선언 실수가 오늘도 조용하다**"),
    (r"지형 판 \d+",
     "지형 구성(`build_default_zones`). 🔴 2026-08-18 에 정확히 이 줄이 빠졌었다"),
    (r"정적 자원 —",
     "index.html·data_log.json 의 **절대경로**. 없으면 cwd 함정(§0.5.2)을 사후에 못 가른다"),
]

# ⚠ 이것들이 **있으면** 문제다. 있는 것을 세는 것과 없는 것을 세는 것은 다른 검사다.
FORBIDDEN = [
    (r"영속 안 함",
     "rid 커서나 대장이 메모리로만 돈다 — HOME 이 없거나 경로가 안 잡혔다"),
    (r"🔴 조립 표 —",
     "조립 표 선언 문제. `main.cpp` 를 봐라"),
]


def main():
    ap = argparse.ArgumentParser(description="기동 로그 검사 (시험 인스턴스 전용)")
    ap.add_argument("--bin", required=True, help="서버 실행파일 경로")
    ap.add_argument("--offset", type=int, required=True,
                    help="포트 오프셋. 🔴 **0 을 못 준다** — 운영 포트를 잡지 않기 위해서다")
    ap.add_argument("--wait", type=float, default=3.0, help="기동 뒤 로그를 읽기까지 기다릴 초")
    a = ap.parse_args()

    # 🔴 **구조로 막는다.** "운영에 쓰지 마라"라고 적어 두는 것보다 못 하게 만드는 쪽이 낫다.
    if a.offset <= 0:
        print("🔴 --offset 은 1 이상이어야 한다. 이 도구는 운영 포트를 잡지 않는다.")
        return 2

    # 🔴 **기여자 파일이 혼자서도 문법 검사를 통과하는가** (2026-08-19 신설)
    #
    #   `lot.cpp` 는 `server.cpp` 안에서 include 되지만, **편집기는 그것을 모른다.**
    #   그 파일만 열면 `ParkingLot` 미정의로 **전부 빨간 줄**이고,
    #   초급자가 `c++ lot.cpp` 를 치면 **오류 벽**이 첫 시도가 된다.
    #   ⚠ 주석에 "단독으로 컴파일하지 마라"라고 적어 뒀지만 **IDE 는 글을 안 읽는다.**
    #   🔑 §"조건을 확인하는 것보다 조건이 성립할 수밖에 없게 만드는 쪽이 낫다".
    #
    # ⚠ **문법 검사**이지 링크가 아니다 — `main()` 도 엔진도 거기 없다. 두 문장은 다르다.
    lotcpp = os.path.join(os.path.dirname(os.path.abspath(a.bin)), "lot.cpp")
    if os.path.exists(lotcpp):
        rc = subprocess.call(["c++", "-std=c++11", "-fsyntax-only", lotcpp],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if rc != 0:
            print("🔴 `lot.cpp` 단독 문법 검사 실패 — 기여자가 그 파일을 열면 빨간 줄이 뜬다.")
            print("   `#include \"parking.h\"` 가 빠졌는지 봐라.")
            return 1
        print("  ✓ lot.cpp 단독 문법 검사 (기여자가 열어도 빨간 줄이 없다)")

    home = os.path.expanduser("~")
    log = "%s/parking-logs/parking-server.test+%d.log" % (home, a.offset)
    before = os.path.getsize(log) if os.path.exists(log) else 0

    proc = subprocess.Popen([a.bin, "--port-offset=%d" % a.offset],
                            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        time.sleep(a.wait)
        if not os.path.exists(log):
            print("🔴 로그 파일이 안 생겼다: %s" % log)
            return 1
        # 🔑 **이번 기동분만 읽는다.** 옛 줄을 읽고 통과하는 것이 오늘 하루에 네 번 나온
        #   "헛통과" 의 첫째 형태다(원장 · web 2026-08-19).
        with open(log, "rb") as f:
            f.seek(before)
            text = f.read().decode("utf-8", "replace")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    if not text.strip():
        print("🔴 이번 기동분 로그가 비어 있다 — 서버가 뜨지 못했을 수 있다")
        return 1

    # ── 🔴 **필수 항목을 세기 *전* 에 "이 기동이 성립했는가"를 본다** ────────────
    #
    #   2026-08-19 에 이 도구를 처음 돌렸을 때 **6건이 빨강**이었다. 원인은 그 여섯 어디에도
    #   없었다 — `reason=port_bind_fail` 이었다(offset 400 → 폰 포트 **5900** 이
    #   macOS 화면공유와 충돌). **기동이 아예 안 됐으니 그 줄들이 없는 게 당연하다.**
    #
    #   > **§"빨강의 원인이 그 항목 안에 있다고 가정하지 마라" 의 실물이다.**
    #   > **한 결함이 다른 항목의 *측정* 을 먹는다.** 그대로 봤으면 `ledger_load` 를
    #   > 안 부른다고 판단하고 **멀쩡한 코드를 고쳤을 것이다.**
    #
    #   🔑 그래서 **선행 조건을 먼저 확인하고, 아니면 항목 판정을 아예 안 낸다.**
    #     빨강을 잘못 내는 것보다 **"못 쟀다"고 말하는 것이 낫다.**
    m = re.search(r"reason=(\w+)", text)
    if m and m.group(1) != "normal":
        print("🔴 **이 기동은 성립하지 않았다** — `reason=%s`" % m.group(1))
        if m.group(1) == "port_bind_fail":
            print("   → 그 오프셋의 포트 셋 중 하나가 이미 쓰이고 있다.")
            print("   ⚠ **offset 400 은 폰 포트가 5900 이 되어 macOS 화면공유와 충돌한다.**")
            print("      다른 오프셋을 써라(예: 300).")
        print("🔴 **필수 항목은 판정하지 않는다** — 기동이 안 됐으니 그 줄들이 없는 게 당연하다.")
        print("   (여기서 빨강을 내면 멀쩡한 코드를 고치게 된다)")
        return 2

    bad = 0
    print("기동 로그 검사 — %s (offset %d · 이번 기동분 %dB)" % (a.bin, a.offset, len(text)))
    for pat, why in REQUIRED:
        if re.search(pat, text):
            print("  ✓ %s" % pat)
        else:
            bad += 1
            print("  ✗ **없다**: %s\n      → %s" % (pat, why))
    for pat, why in FORBIDDEN:
        if re.search(pat, text):
            bad += 1
            print("  ✗ **있으면 안 되는 것이 있다**: %s\n      → %s" % (pat, why))

    print("기동 로그 검사 %s (필수 %d항목)" % ("통과" if bad == 0 else "실패 %d건" % bad,
                                              len(REQUIRED)))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
