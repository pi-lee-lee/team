#!/usr/bin/env python3
"""요청 프로토콜 집행 훅.

두 가지를 막는다:
  1. `tmux send-keys` 로 에이전트 창에 지시를 타이핑하는 것
  2. `SendMessage` 의 REQ 통지에 내용을 문장으로 옮겨 적는 것

왜 있는가 (2026-08-15~16 사고):
  루트가 급하다는 이유로 tmux 로 에이전트 창에 지시를 직접 타이핑했다. 그 지시는
  `requests/` 에 아무 기록도 남기지 않았고, **사용자가 무엇이 진행 중인지 볼 방법이 사라졌다.**
  상태창에는 "아무도 일하지 않는" 것으로 보였다.

  루트는 규칙을 몰라서 어긴 것이 아니다 — 같은 시각에 에이전트들에게 "반드시 req.sh 로
  발행하라"를 REQ 본문에 적어 넣고 있었다. **아는 것과 지키는 것은 다르고, 강제되지 않는
  규칙은 반복해서 깨진다.** tmux 를 막자 손이 SendMessage 로 옮겨 갔고, 그래서 둘 다 막는다.

  CLAUDE.md 의 설계 사상 그대로다:
    "규칙은 문서로만 있는 게 아니라 훅으로 기계 집행된다 —
     남의 영역을 고치려는 도구 호출은 실제로 실패한다."

⚠ 이 파일이 사라지면 Bash 호출 전체가 실패한다(2026-08-16 실제 발생).
   settings.json 의 PreToolUse 등록과 짝이므로 함께 다뤄라.
"""
import json
import re
import sys

# 창 지정자: learnteam:2, learnteam:arduino-engineer, :4, 2.0 등
_TARGET = re.compile(r"-t\s+(\S+)")
# 읽기 전용/무해한 tmux 하위명령
_SAFE_SUBCMDS = (
    "capture-pane", "list-windows", "list-panes", "list-sessions",
    "display-message", "has-session", "select-window", "show-", "list-keys",
)


def _window_of(target: str) -> str:
    """learnteam:2.0 → '2' / :4 → '4' / 2 → '2'"""
    t = target.strip("'\"")
    if ":" in t:
        t = t.split(":", 1)[1]
    return t.split(".", 1)[0]


def _check_sendmessage(payload) -> int:
    """SendMessage 는 **포인터만** 보내야 한다.

    왜: 요청 내용을 메시지에 옮겨 적으면 채널이 둘이 된다. 파일과 메시지가 어긋날 수 있고,
    무엇보다 **사용자가 보는 원장(requests/)에 그 지시가 안 남는다.**

    허용하는 형태 (한 줄):
        [REQ-0093] requests/open/REQ-0093.md 를 읽고 처리하라.
    """
    msg = (payload.get("tool_input") or {}).get("message", "") or ""
    body = msg.strip()
    if not body:
        return 0

    lines = [l for l in body.splitlines() if l.strip()]

    # REQ 포인터가 아닌 메시지: 짧은 조율(질문·확인·완료통지)은 통과시킨다.
    # 막으려는 것은 "요청 내용을 문장으로 옮기는 것"이지 대화 자체가 아니다.
    if not body.startswith("[REQ-"):
        return 0

    if len(lines) == 1 and len(body) <= 160:
        return 0

    sys.stderr.write(
        "[요청 프로토콜 차단] REQ 통지는 **포인터 한 줄**이어야 한다.\n"
        f"  보낸 길이: {len(body)}자 / {len(lines)}줄\n"
        "  이유: 내용을 메시지에 옮겨 적으면 채널이 둘이 되고, "
        "그 지시가 requests/ 원장에 남지 않아 사용자가 진행을 볼 수 없다.\n"
        "  허용 형태:\n"
        "    [REQ-0093] requests/open/REQ-0093.md 를 읽고 처리하라.\n"
        "  덧붙일 말이 있으면 **메시지가 아니라 REQ 파일 본문에** 적어라.\n"
        "  (REQ 포인터가 아닌 짧은 조율 메시지는 막지 않는다.)\n"
    )
    return 2


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        return 0  # 훅이 판단 못 하면 통과시킨다 — 주 방어선이 아니다

    if payload.get("tool_name") == "SendMessage":
        return _check_sendmessage(payload)

    if payload.get("tool_name") != "Bash":
        return 0
    cmd = (payload.get("tool_input") or {}).get("command", "") or ""

    if "tmux" not in cmd or "send-keys" not in cmd:
        return 0
    if any(s in cmd for s in _SAFE_SUBCMDS) and "send-keys" not in cmd:
        return 0

    for m in _TARGET.finditer(cmd):
        win = _window_of(m.group(1))
        if win in ("0", "root", ""):
            continue  # 루트 창은 사용자 자신의 창이다

        # 승인 프롬프트 응답 등 단일 키는 허용한다(Enter/Escape/숫자 한 글자).
        after = cmd[m.end():]
        keys = after.strip().split()
        literal = " -l " in cmd or " -X " in cmd
        if not literal and len(keys) <= 2 and all(len(k) <= 7 for k in keys):
            continue

        sys.stderr.write(
            "[요청 프로토콜 차단] 에이전트 창에 tmux 로 지시를 타이핑할 수 없다.\n"
            f"  대상 창: {m.group(1)}\n"
            "  이유: 그 지시는 requests/ 에 아무 기록도 남기지 않아 "
            "사용자가 무엇이 진행 중인지 볼 수 없게 된다.\n"
            "  대신 이렇게 하라 — 요청은 문장이 아니라 파일이다:\n"
            "    1) team/bin/req.sh new --from root --to <담당> --title \"...\" "
            "--files \"...\" --body \"...\" --why \"...\" --accept \"...\"\n"
            "    2) SendMessage(to=\"<담당>\", "
            "message=\"[REQ-XXXX] requests/open/REQ-XXXX.md 를 읽고 처리하라.\")\n"
            "  (창 상태를 보는 capture-pane 은 막지 않는다. "
            "승인 프롬프트에 한 글자 응답하는 것도 허용된다.)\n"
        )
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())
