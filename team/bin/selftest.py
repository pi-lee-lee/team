#!/usr/bin/env python3
"""selftest.py — 소유권 훅이 실제로 막는지 검증한다.

에이전트를 띄우지 않고 훅에 직접 가짜 도구 호출을 먹여서 판정을 확인한다.
"6개 프로세스가 살아있다"는 검증이 아니라 **차단이 실제로 일어나는가**를 본다.
소유권 규칙을 고친 뒤에는 이걸 돌려서 회귀를 잡는다.

    python3 team/bin/selftest.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GUARD = os.path.join(ROOT, ".claude", "hooks", "ownership_guard.py")
NOSUB = os.path.join(ROOT, ".claude", "hooks", "no_subagent_guard.py")
REGISTRY = os.path.join(ROOT, ".claude", "team", "registry.json")

# 테스트용 가짜 세션 id → 역할
SESS = {
    "cpp-engineer": "test-sid-cpp",
    "android-engineer": "test-sid-android",
    "arduino-engineer": "test-sid-arduino",
    "web-engineer": "test-sid-web",
    "socket-engineer": "test-sid-socket",
}
ROOT_SID = "test-sid-unregistered"  # 미등록 = 루트 취급


def run(hook, role, tool, tool_input):
    payload = {
        "session_id": SESS.get(role, ROOT_SID),
        "cwd": ROOT,
        "hook_event_name": "PreToolUse",
        "tool_name": tool,
        "tool_input": tool_input,
    }
    p = subprocess.run(
        [sys.executable, hook], input=json.dumps(payload),
        capture_output=True, text=True,
    )
    return p.returncode, (p.stderr or "").strip()


def write(role, path):
    return run(GUARD, role, "Write", {"file_path": os.path.join(ROOT, path)})


def bash(role, cmd):
    return run(GUARD, role, "Bash", {"command": cmd})


# (설명, 실행함수, 기대: True=차단되어야 함)
CASES = [
    ("cpp 가 자기 C++ 을 수정",
     lambda: write("cpp-engineer", "cpp/main.cpp"), False),
    ("android 가 JNI .cpp 수정 시도  ← 핵심 차단",
     lambda: write("android-engineer", "android/app/src/main/cpp/native-lib.cpp"), True),
    ("cpp 가 JNI .cpp 수정 (소유자)",
     lambda: write("cpp-engineer", "android/app/src/main/cpp/native-lib.cpp"), False),
    ("android 가 자기 Kotlin 수정",
     lambda: write("android-engineer", "android/app/src/main/java/com/x/Main.kt"), False),
    ("cpp 가 Kotlin 수정 시도",
     lambda: write("cpp-engineer", "android/app/src/main/java/com/x/Main.kt"), True),
    ("arduino 가 스케치 안 .cpp 수정 (경로가 확장자를 이김)",
     lambda: write("arduino-engineer", "arduino/blink/helper.cpp"), False),
    ("cpp 가 아두이노 스케치 .cpp 수정 시도",
     lambda: write("cpp-engineer", "arduino/blink/helper.cpp"), True),
    ("web 이 web/net/ 수정 시도 (소켓 소유)",
     lambda: write("web-engineer", "web/net/client.js"), True),
    ("socket 이 net/ 수정",
     lambda: write("socket-engineer", "net/server.cpp"), False),
    ("web 이 자기 js 수정",
     lambda: write("web-engineer", "web/app.js"), False),
    ("루트가 C++ 수정 시도  ← 루트도 예외 아님",
     lambda: write("root", "cpp/main.cpp"), True),
    ("루트가 CLAUDE.md 수정",
     lambda: write("root", "CLAUDE.md"), False),
    ("cpp 가 .claude/settings.json 수정 시도 (루트 전용)",
     lambda: write("cpp-engineer", ".claude/settings.json"), True),
    ("cpp 가 요청 md 작성 (전원 공용)",
     lambda: write("cpp-engineer", "requests/2026-08-13/REQ-0001-a.md"), False),
    ("규칙 없는 새 경로 → 기본 거부",
     lambda: write("cpp-engineer", "sandbox/scratch.txt"), True),

    ("Bash: android 가 리다이렉션으로 JNI 변조 시도",
     lambda: bash("android-engineer", "echo x > android/app/src/main/cpp/a.cpp"), True),
    ("Bash: android 가 sed -i 로 JNI 변조 시도",
     lambda: bash("android-engineer", "sed -i '' s/a/b/ android/app/src/main/cpp/a.cpp"), True),
    ("Bash: cpp 가 남의 트리를 읽기만 (허용돼야 함)",
     lambda: bash("cpp-engineer", "grep -rn foo android/app/src/main/java"), False),
    ("Bash: 오탐 방지 — 'platform' 안의 'rm ' 로 막히면 안 됨",
     lambda: bash("arduino-engineer", "arduino-cli compile --fqbn esp32:esp32:platform arduino/blink"), False),
    ("Bash: 오탐 방지 — 'performance.cpp' 를 읽는 명령",
     lambda: bash("cpp-engineer", "cat cpp/performance.cpp"), False),
    ("Bash: cpp 가 자기 파일 리다이렉션 (허용)",
     lambda: bash("cpp-engineer", "echo x > cpp/gen.h"), False),
    ("Bash: 오탐 방지 — '2>&1' 은 fd 복제지 파일 쓰기가 아니다",
     lambda: bash("root", 'team/bin/req.sh new --to android-engineer '
                          '--files android/app/src/main/java/A.kt 2>&1'), False),
    ("Bash: 오탐 방지 — 남의 경로를 '인자로 언급'만 하는 명령",
     lambda: bash("root", 'team/bin/req.sh show REQ-0001 --files cpp/main.cpp'), False),
    ("Bash: 리다이렉션 대상이 남의 파일이면 여전히 차단",
     lambda: bash("root", "echo x 2> cpp/err.log"), True),
    ("Bash: 루트 소유 스크립트를 '실행'하는 건 수정이 아니다",
     lambda: bash("android-engineer", "team/bin/req.sh list --to android-engineer 2>&1"), False),
    ("Bash: 인자에 변조 낱말이 섞여도 실행은 허용 (--note \"cp 로 복사\")",
     lambda: bash("android-engineer",
                  'team/bin/req.sh done REQ-0001 --by android-engineer --note "cp 로 복사함"'), False),
    ("Bash: 루트 소유 스크립트를 '수정'하려 하면 차단",
     lambda: bash("android-engineer", "echo x > team/bin/req.sh"), True),

    ("하위 에이전트 생성 시도 (전문 에이전트)",
     lambda: run(NOSUB, "cpp-engineer", "Agent", {"subagent_type": "general-purpose"}), True),
    ("하위 에이전트 생성 시도 (루트도 금지)",
     lambda: run(NOSUB, "root", "Agent", {"subagent_type": "Explore"}), True),
    ("Workflow 도구도 차단",
     lambda: run(NOSUB, "cpp-engineer", "Workflow", {}), True),
]


def main():
    backup = None
    if os.path.exists(REGISTRY):
        backup = REGISTRY + ".selftest-backup"
        shutil.copy2(REGISTRY, backup)
    try:
        os.makedirs(os.path.dirname(REGISTRY), exist_ok=True)
        existing = {}
        if backup:
            with open(backup) as f:
                existing = json.load(f)
        merged = dict(existing)
        agents = dict(merged.get("agents") or {})
        for role, sid in SESS.items():
            agents[sid] = {"role": role, "short": "selftest"}
        merged["agents"] = agents
        with open(REGISTRY, "w") as f:
            json.dump(merged, f, ensure_ascii=False, indent=2)

        ok = fail = 0
        for desc, fn, should_block in CASES:
            code, err = fn()
            blocked = code == 2
            if blocked == should_block:
                ok += 1
                print("  ✓ %-52s %s" % (desc, "차단됨" if blocked else "허용됨"))
            else:
                fail += 1
                print("  ✗ %-52s 기대=%s 실제=%s" % (
                    desc, "차단" if should_block else "허용", "차단" if blocked else "허용"))
                if err:
                    print("      %s" % err.splitlines()[0])
        print("\n  통과 %d / 실패 %d" % (ok, fail))
        return 1 if fail else 0
    finally:
        if backup:
            shutil.move(backup, REGISTRY)
        elif os.path.exists(REGISTRY):
            os.remove(REGISTRY)


if __name__ == "__main__":
    sys.exit(main())
