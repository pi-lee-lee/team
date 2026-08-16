#!/usr/bin/env python3
"""A 창 감시기 — 새 사건 · **tap 사망** · **로그 정지** 중 하나가 나면 끝난다. 종료가 곧 신호다.

왜 이런 모양인가
  monitor 세션은 메시지를 받아야 움직인다. "A 창 8시간 동안 계속 세라"를 지키려면
  **일이 생겼을 때 나를 깨울 무언가**가 필요하다. 이 스크립트는 배경에서 돌다가 종료한다.

🔴 tap 은 **단일 실패점**이다 (루트 지시 2026-08-16 19:25 · 원장 6.10)
  서버 로그는 ESP 리셋과 `busy` 자해를 **원리적으로 구분하지 못한다**(uptime 이 Uno 의 millis 라
  ESP 가 죽어도 안 끊긴다). 즉 **tap 이 죽는 순간부터 A 는 갈래를 못 가르는 자료**가 된다.
  건수는 서버 로그로 복원되지만 **갈래는 영영 못 만든다.**

  가장 위험한 형태는 **조용한 손해**다 — tap 이 22:00 에 죽고 아무도 01:53 까지 모르면
  3시간 50분이 갈래 없는 자료가 되는데, 겉으로는 A 가 8시간 돈 것처럼 보인다.
  → 그래서 **사건만이 아니라 계측기 자체**를 감시한다.

  ⚠ **pid 생존만 보면 못 잡는다.** 프로세스가 살아 있어도 포트가 끊기면 로그가 안 자란다.
     그래서 **성장까지** 본다. 둘은 다른 고장이다.

⚠ **되살리는 것은 내 판단이 아니다.** 멈추면 알리고 **경계 시각만 즉시 기록**한다.
   되살리기는 개입이고 루트가 정한다(기록이 늦으면 복구해도 어디까지가 유효한지 못 가른다).

⚠ 이 감시기는 **A 판정 자료를 만들지 않는다.** 그래서 동결(`frozen-A/`)에 걸리지 않는다.

사용: python3 monitor/watch_events.py [로그] [종료ISO] [tap_pid]
"""
from __future__ import annotations

import os
import sys
import time
from datetime import datetime

LOG = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-newbase.log"
DEADLINE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 17, 2, 3, 0)
TAP_PID = int(sys.argv[3]) if len(sys.argv) > 3 else 79289

MARK = "전송 3회 연속 실패".encode()
POLL_S = 60
# 무성장 허용 한계. 실측 성장률 185 B/s 라 정상이면 60초에 ~11KB 붙는다.
# 180초로 잡은 이유: 오탐 한 번의 비용(감시 재기동)보다 놓침의 비용(갈래 없는 자료)이 크지만,
# 60초 하나로 잡으면 순간적인 버퍼 지연에도 깨진다. 3회 연속 무성장이면 실제 정지로 본다.
STALL_S = 180
# ⚠ 절대 경로로 고정한다. 상대 경로면 **cwd 에 따라 기록이 엉뚱한 데 떨어지거나 깨진다** —
#   시험에서 실제로 터졌다(/tmp 에서 실행하니 하트비트 기록이 예외). 감시기가 예외로 죽으면
#   감시가 없는 줄도 모르고 A 가 흘러간다. 정확히 이 도구가 막으려는 그 조용한 손해다.
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEARTBEAT = os.path.join(_ROOT, "monitor", "watch-heartbeat.tsv")
ALERT = os.path.join(_ROOT, "monitor", "ALERT-tap-stopped.md")


def count(path: str) -> int:
    """사건 수. **파일 전체**를 센다.

    🔴 tail 최적화를 넣지 마라. 한 번 넣었다가 뺐다 — 창이 EOF 에 붙어 앞으로 슬라이드하면
       새 사건이 늘어도 오래된 사건이 창 밖으로 빠져 총계가 안 늘고, **사건이 났는데 안 깨어난다.**
    """
    with open(path, "rb") as f:
        return f.read().count(MARK)


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)          # 신호 0 = 존재 확인만. 프로세스에 영향 없다
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True              # 남의 프로세스지만 살아 있다


def beat(ts: datetime, size: int, ev: int, alive: bool) -> None:
    """매 주기 상태를 파일에 남긴다.

    감시기 자체가 죽어도 **마지막으로 정상이었던 시각**이 남는다(LEDGER 2.4 의 하트비트 원리).
    이게 없으면 나중에 '언제부터 자료가 무효인가'를 못 가른다.
    """
    new = not os.path.exists(HEARTBEAT)
    with open(HEARTBEAT, "a") as f:
        if new:
            f.write("# A 창 감시 하트비트 — 60초 주기. 마지막 줄이 마지막으로 확인된 정상 시각이다.\n")
            f.write("iso\tlog_bytes\tevents\ttap_alive\n")
        f.write(f"{ts.isoformat(timespec='seconds')}\t{size}\t{ev}\t{int(alive)}\n")


def alert(reason: str, detail: str, last_ok: datetime | None) -> None:
    """🔴 경계 시각을 **즉시** 파일로 남긴다. 내가 깨어날 때까지 기다리지 않는다."""
    now = datetime.now()
    with open(ALERT, "w") as f:
        f.write(f"# 🔴 관측 중단 — {reason}\n\n")
        f.write(f"- 감지 시각: **{now:%Y-%m-%d %H:%M:%S}**\n")
        f.write(f"- 마지막 정상 확인: **{last_ok:%Y-%m-%d %H:%M:%S}**\n" if last_ok else "- 마지막 정상 확인: (없음)\n")
        f.write(f"- 상세: {detail}\n")
        f.write(f"- tap pid: {TAP_PID} · 로그: {os.path.abspath(LOG)}\n\n")
        f.write("## 이 시각 이후 자료의 성격\n\n")
        f.write("**건수는 서버 로그로 복원되지만 갈래(자해/리셋)는 복원 불가다.**\n")
        f.write("서버 로그는 두 기전을 원리적으로 구분하지 못한다(원장 6.10).\n")
        f.write("→ 이 경계 이후 구간은 **갈래 없는 자료**로 취급하고, A 판정에서 그렇게 라벨하라.\n\n")
        f.write("⚠ **되살리기는 루트가 정한다.** 되살리는 것 자체가 개입이다.\n")
        f.write("⚠ 다시 열면 **DTR 로 보드가 리셋**되므로 그 시각을 개입으로 기록해야 한다.\n")


def main() -> int:
    p = os.path.abspath(LOG)
    base = count(p)
    size = os.path.getsize(p)
    now = datetime.now()
    last_grow_t, last_size, last_ok = now, size, now
    print(f"# 감시 시작 {now:%Y-%m-%d %H:%M:%S}")
    print(f"# 원본 {p}  ({size:,} B)")
    print(f"# 기준 사건 수 {base} · tap pid {TAP_PID} · {POLL_S}초 주기 · 마감 {DEADLINE}")
    print(f"# 감시 항목: ①새 사건 ②tap 사망 ③로그 정지({STALL_S}초 무성장)")
    sys.stdout.flush()
    beat(now, size, base, pid_alive(TAP_PID))

    while True:
        if datetime.now() >= DEADLINE:
            print(f"\n## 마감 도달 {datetime.now():%H:%M:%S} — 최종 사건 {count(p)}건. 정상 종료")
            return 0
        time.sleep(POLL_S)
        now = datetime.now()

        alive = pid_alive(TAP_PID)
        try:
            size = os.path.getsize(p)
            ev = count(p)
        except FileNotFoundError:
            # LEDGER 2.4 계열 — 파일이 unlink 돼도 tap 은 계속 쓸 수 있다.
            # 이건 '사건 없음'이 아니라 **관측 불능**이다.
            alert("로그 파일 소실(unlink)", f"{p} 가 디렉터리에서 사라졌다", last_ok)
            print(f"\n## 🔴 로그 파일 소실 — {ALERT} 기록함. **관측 불능이지 사건 없음이 아니다.**")
            return 2

        if size > last_size:
            last_grow_t, last_size = now, size
        stalled = (now - last_grow_t).total_seconds() >= STALL_S

        if not alive:
            alert("tap 프로세스 사망", f"pid {TAP_PID} 없음. 마지막 크기 {size:,} B", last_ok)
            print(f"\n## 🔴 tap(pid {TAP_PID}) 사망 — {ALERT} 기록함. 되살리지 않는다(루트 판단).")
            return 3
        if stalled:
            gap = int((now - last_grow_t).total_seconds())
            alert("로그 정지(tap 은 생존)", f"{gap}초 무성장. 크기 {size:,} B 고정 — 포트가 끊겼을 수 있다", last_ok)
            print(f"\n## 🔴 로그 정지 {gap}초 — tap 은 살아 있다. {ALERT} 기록함.")
            return 4

        last_ok = now
        beat(now, size, ev, alive)

        if ev > base:
            print(f"\n## 🔔 새 사건 {ev - base}건 (총 {ev}) · {now:%H:%M:%S}")
            print("   → monitor/frozen-A/mech_split.py 로 갈래를 세고 루트에 알릴 것.")
            print("   → **순수 자해가 A 안에서 처음이면 즉시** 알린다(플래싱 판단이 걸려 있다).")
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
