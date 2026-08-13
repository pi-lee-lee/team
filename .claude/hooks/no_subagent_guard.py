#!/usr/bin/env python3
"""PreToolUse 가드 — 하위 에이전트 생성 금지.

사용자 요구:
  · "각각의 에이전트들은 하위 에이전트를 생성할 수 없다. 자신에게 할당된 작업은 직접 수행한다."
  · "생성된 에이전트는 삭제되지 않는다. 생성된 모든 에이전트는 상시 기동 상태를 유지한다."

두 번째 문장이 첫 번째보다 강하다. 일회성 서브에이전트를 허용하면 레지스트리에
없는 에이전트가 생겼다 사라진다 — "모든 에이전트는 상시 기동"이 깨지고 루트의
관리감독 대상에서 벗어난다. 그래서 이 프로젝트에서는 **누구도**(루트 포함)
Agent/Task/Workflow 로 에이전트를 만들지 않는다.

에이전트를 늘리는 유일한 방법은 루트가 실행하는
    team/bin/agent-new.sh <이름> ...
이며, 이렇게 만든 에이전트는 등록되고 상시 기동되며 삭제되지 않는다.

ALLOW_ROOT_SUBAGENTS 를 True 로 바꾸면 루트만 예외가 된다. 기본값이 False 인
이유는 위와 같다 — 바꾸려면 그 대가(비등록 에이전트 발생)를 알고 바꿔라.
"""
import json
import os
import sys

PROJECT_ROOT = "/Users/idong-u/learn"
REGISTRY = os.path.join(PROJECT_ROOT, ".claude", "team", "registry.json")
BLOCKED_TOOLS = ("Agent", "Task", "Workflow")
ALLOW_ROOT_SUBAGENTS = False


def role_of_session(sid):
    try:
        with open(REGISTRY) as f:
            reg = json.load(f)
    except Exception:
        return "root"
    ent = (reg.get("agents") or {}).get(sid or "")
    return ent.get("role", "root") if isinstance(ent, dict) else "root"


def main():
    try:
        data = json.load(sys.stdin)
    except Exception:
        sys.exit(0)

    if data.get("tool_name", "") not in BLOCKED_TOOLS:
        sys.exit(0)

    role = role_of_session(data.get("session_id"))
    if role == "root" and ALLOW_ROOT_SUBAGENTS:
        sys.exit(0)

    if role == "root":
        msg = (
            "[차단] 이 팀에는 일회성 하위 에이전트가 존재하지 않는다.\n"
            "  규약: 생성된 에이전트는 삭제되지 않고 상시 기동을 유지한다 →"
            " 일회성 서브에이전트는 이 불변식을 깬다.\n"
            "  → 새 전문 영역이 필요하면 상시 에이전트를 만들어라:\n"
            '     team/bin/agent-new.sh <역할이름> --desc "<설명>" --paths "<소유 glob>,..."\n'
            "  → 기존 영역 작업이면 담당 에이전트에게 md 요청을 발행하라:\n"
            '     team/bin/req.sh new --from root --to <담당> --title "<제목>"'
        )
    else:
        msg = (
            "[차단] 너(%s)는 하위 에이전트를 만들 수 없다. 할당된 작업은 직접 수행하라.\n"
            "  → 네 영역 밖의 작업이면 담당 에이전트에게 md 요청을 발행하라:\n"
            '     team/bin/req.sh new --from %s --to <담당> --title "<제목>" --files <경로>\n'
            "     발행 후 SendMessage 로 상대에게 REQ 파일 경로만 알린다.\n"
            "  → 담당이 누구인지 모르겠으면 루트에게 보고하라(--to root)." % (role, role)
        )
    print(msg, file=sys.stderr)
    sys.exit(2)


if __name__ == "__main__":
    main()
