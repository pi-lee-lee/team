#!/usr/bin/env python3
"""새 링크 끊김 사건이 나면 **끝난다**. 그게 전부다.

왜 이런 모양인가
  monitor 세션은 메시지를 받아야 움직인다. 그래서 "A 창 8시간 동안 갈래를 계속 세라"는
  지시를 지키려면 **사건이 났을 때 나를 깨울 무언가**가 필요하다.
  이 스크립트는 배경에서 돌다가 사건 수가 늘면 종료한다 — 종료가 곧 신호다.

⚠ 관측 도구가 관측 대상을 바꾸지 않게 (LEDGER 1.7)
  · 로그를 **읽기만** 한다. 포트도 보드도 건드리지 않는다.
  · 파일 끝 일부만 읽는다(대용량 로그를 통째로 올리지 않는다).
  · 반드시 **스스로 끝난다**(A 창 종료 + 여유). 잊힌 채 며칠 도는 프로세스를 남기지 않는다.

사용: python3 monitor/watch_events.py [로그] [종료ISO]
"""
from __future__ import annotations

import os
import sys
import time
from datetime import datetime

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-newbase.log"
# A 창(01:53) + 여유 10분. 이 시각이 지나면 사건이 없어도 끝낸다.
DEADLINE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 17, 2, 3, 0)

MARK = "전송 3회 연속 실패".encode()
POLL_S = 60
TAIL_BYTES = 4_000_000     # 사건은 뒤쪽에 쌓인다. 앞부분을 매번 다시 읽을 이유가 없다.


def count(path: str) -> int:
    """사건 수를 센다. 바이트로 읽는다 — 시리얼 로그에는 디코드 불가 바이트가 섞여 있다."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        if size > TAIL_BYTES:
            f.seek(size - TAIL_BYTES)
            f.readline()          # 잘린 첫 줄은 버린다
        return f.read().count(MARK)


def main() -> int:
    p = os.path.abspath(LOG)
    base = count(p)
    print(f"# 감시 시작 {datetime.now():%Y-%m-%d %H:%M:%S}")
    print(f"# 원본 {p}")
    print(f"# 기준 사건 수 {base} · {POLL_S}초마다 확인 · 마감 {DEADLINE}")
    sys.stdout.flush()

    while True:
        if datetime.now() >= DEADLINE:
            print(f"\n## 마감 도달 {datetime.now():%H:%M:%S} — 새 사건 없이 종료. 최종 {count(p)}건")
            return 0
        time.sleep(POLL_S)
        try:
            now = count(p)
        except FileNotFoundError:
            # LEDGER 2.4 계열의 함정 — 파일이 unlink 돼도 tap 은 계속 쓸 수 있다.
            # 이건 "사건 없음"이 아니라 **관측 불능**이다. 반드시 갈라서 알린다.
            print("\n🔴 로그 파일이 사라졌다(unlink?). 사건 없음이 아니라 **관측 불능**이다.")
            return 2
        if now > base:
            print(f"\n## 🔔 새 사건 {now - base}건 (총 {now}) · {datetime.now():%H:%M:%S}")
            print("   → monitor/frozen-A/mech_split.py 로 갈래를 다시 세고 루트에 알릴 것.")
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
