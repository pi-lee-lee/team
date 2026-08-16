#!/usr/bin/env python3
"""소유권 훅 회귀 시험.

2026-08-16 에 거짓 차단 둘을 고치면서 만들었다.
  (1) `cd <프로젝트 루트>; <변조명령>` 이 대상과 무관하게 차단됐다
  (2) `sed -i '' s/a/b/ <내 파일>` 이 치환식을 경로로 읽어 차단됐다

**이 시험의 목적은 고친 것이 통과하는지가 아니라, 고치면서 집행이 약해지지
않았는지를 보는 것이다.** 그래서 '차단돼야 하는' 사례가 더 많다.

훅을 하위 프로세스로 띄우고 종료코드를 본다(0=통과, 2=차단).
시험 명령을 이 파일 안에 두는 이유: 셸에 그대로 쓰면 훅이 시험 자체를 막는다.
"""
import json
import subprocess
import sys

HOOK = "/Users/idong-u/learn/.claude/hooks/ownership_guard.py"
ARD = "da685332-6164-4a1e-9eec-f47b15af6249"        # arduino-engineer
WEB = "d27ae462-1ac2-4bb6-9364-739125a1c6ae"        # web-engineer
ROOT_SID = "00000000-0000-0000-0000-000000000000"   # 미등록 = 루트 취급

CD = "cd /Users/idong-u/learn; "
SED = "sed -i '' 's/a/b/' "

# (설명, session_id, 명령, 기대통과)
CASES = [
    # ── 고친 것 (1) cd 구획
    ("arduino · cd 뒤 자기 .ino 수정",          ARD, CD + SED + "arduino/x.ino",       True),
    ("arduino · cd 뒤 자기 트리 파일 삭제",      ARD, CD + "rm arduino/tmp.txt",        True),
    ("web · cd 뒤 자기 트리 복사",               WEB, CD + "cp web/a.png web/b.png",    True),

    # ── 고친 것 (2) sed 치환식
    ("arduino · 슬래시 치환식 + 자기 파일",      ARD, SED + "arduino/x.ino",            True),
    ("arduino · 따옴표 없는 치환식",             ARD, 'sed -i "" s/a/b/ arduino/x.ino', True),
    ("arduino · 파이프 구분자(원래 통과)",       ARD, "sed -i '' 's|a|b|' arduino/x.ino", True),

    # ── 🔴 집행 검사 — 여전히 막혀야 한다
    ("arduino · cd 뒤 남의 cpp 수정",            ARD, CD + SED + "cpp/y.cpp",           False),
    ("arduino · cd 없이 남의 cpp 수정",          ARD, SED + "cpp/y.cpp",                False),
    ("arduino · 치환식 뒤 남의 파일",            ARD, SED + "web/index.html",           False),
    ("arduino · cd + 치환식 + 남의 파일",        ARD, CD + SED + "web/index.html",      False),
    ("arduino · cd 뒤 남의 web 파일 삭제",       ARD, CD + "rm web/index.html",         False),
    ("arduino · cd 뒤 남의 파일에 리다이렉션",   ARD, CD + "echo x > cpp/y.cpp",         False),
    ("arduino · cd 두 번 뒤 남의 파일 수정",     ARD, CD + CD + SED + "cpp/y.cpp",      False),
    ("web · cd 뒤 남의 .ino 수정",               WEB, CD + SED + "arduino/x.ino",       False),
    ("루트 · cd 뒤 남의 web 파일 삭제",          ROOT_SID, CD + "rm web/a.png",         False),

    # ── 원래 통과해야 하는 것 (회귀)
    ("arduino · 루트 소유 스크립트 호출",        ARD, "team/bin/req.sh list",           True),
    ("arduino · cd 뒤 읽기 전용 명령",           ARD, CD + "cat cpp/y.cpp",             True),
    ("web · 자기 트리 파일 삭제",                WEB, "rm web/artifacts/x.png",         True),
]


def run(sid, cmd):
    payload = json.dumps({
        "tool_name": "Bash",
        "cwd": "/Users/idong-u/learn",
        "session_id": sid,
        "tool_input": {"command": cmd},
    })
    p = subprocess.run([sys.executable, HOOK], input=payload,
                       capture_output=True, text=True)
    return p.returncode == 0


fail = 0
for desc, sid, cmd, want_pass in CASES:
    got_pass = run(sid, cmd)
    ok = (got_pass == want_pass)
    if not ok:
        fail += 1
    print("%s| %-36s 기대=%s 실제=%s" % (
        "  ok " if ok else "FAIL ",
        desc,
        "통과" if want_pass else "차단",
        "통과" if got_pass else "차단",
    ))

print()
print("%d/%d 통과  (차단돼야 하는 사례 %d건)" % (
    len(CASES) - fail, len(CASES), sum(1 for c in CASES if not c[3])))
sys.exit(1 if fail else 0)
