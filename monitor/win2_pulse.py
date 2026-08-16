#!/usr/bin/env python3
"""창2 상시 요약기 — **끝나지 않는다. 아무도 안 깨어 있어도 자료가 쌓인다.**

왜 `watch_events.py` 를 안 쓰나
  그것은 **첫 사건에서 종료**한다("종료가 곧 신호"). monitor 세션을 깨우는 것이 목적이었다.
  루트 지시(2026-08-17 00:1x)는 반대다 — **세션이 전부 죽어도 밤새 계속 돌아야** 하고
  **파일만 보고 진행 상황을 알 수 있어야** 한다. 그래서 별도 도구다.

  ⚠ 그러니 이 도구는 **아무도 깨우지 않는다.** 깨우는 일은 `watch_events.py` 의 몫이고,
     이 도구의 일은 **기록이 끊기지 않게 하는 것**뿐이다. 둘을 같이 켜라.

무엇을 남기나 (둘 다 필요하다)
  <tsv>   5분마다 한 줄 append — 기계 판독용. **누적값**이다(구간값 아님, 원장 6.8)
  <md>    매 주기 덮어쓰기 — 사람이 읽는 현재 상태 한 장

🔴 이 도구는 **판정하지 않는다.** 갈래·λ·도달률을 만들지 않는다.
   판정은 창을 닫고 동결 파서로 한다(`RECIPE-A-judgment.md` 절차).
   여기 숫자는 **"살아 있나 · 얼마나 쌓였나"** 를 보는 눈금일 뿐이다.

⚠ `0` 은 셋이다(원장 1.1). 이 도구는 **"아직 한 번도 안 났다"** 를 `0` 으로 찍는다.
   건강의 증거로 읽지 마라. `up=` 과 `bytes` 가 자라는지를 같이 봐라.

사용: python3 monitor/win2_pulse.py [시리얼로그] [서버로그] [주기초]
"""
from __future__ import annotations

import os
import re
import sys
import time
from datetime import datetime

SER = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-win2.log"
SRV = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser("~/parking-logs/parking-server.log")
PERIOD = int(sys.argv[3]) if len(sys.argv) > 3 else 300

TSV = "monitor/win2-pulse.tsv"
MD = "monitor/WIN2-STATUS.md"

# 시리얼 표지 — 새 펌웨어 539ac53 의 새 줄 둘을 포함한다(arduino 통보 23:59).
# ⚠ 표지를 여기 적어 두는 이유: 파서에 없으면 조용히 0 이 된다(원장 1.1 · 5.1).
SER_MARKS = {
    "event": "전송 3회 연속 실패".encode(),      # 링크 끊김 사건
    "banner": b"[PARKING NODE]",                  # 부트 배너(우리 리셋 포함)
    "resync": b"[TX-RESYNC]",
    "drop": b"[TX-DROP]",
    "skip": "이번 주기는 건너뛴다".encode(),      # [TX-WAIT] skip  ← 새 칩
    "okto": "상한 초과".encode(),                  # [TX-WAIT] okto  ← 새 칩
    "busy": b'"busy ',
    "tx": b"[TX] ",
    "sendok": b'"SEND OK"',
    "ipd": b"+IPD,",                               # 🔑 하행 수신 — D1/D2 를 가르는 유일한 표지
    "ipfound": "★ IP 확보".encode(),
    "cipstart_err": b"Unlink",
}
SRV_MARKS = {
    "srv_accept": "연결 수락".encode(),
    "srv_sframe": "←ARD S,".encode(),
    "srv_ack": "←ARD A,".encode(),
    "srv_down": "→ARD ".encode(),
    "srv_offline": "무프레임 판정".encode(),
}
CNT_RE = re.compile(rb"\[CNT\]([^\n]*)")
TS_RE = re.compile(rb"^(\d\d:\d\d:\d\d)\s")


def count(path: str, marks: dict) -> dict:
    out = {k: 0 for k in marks}
    out["_bytes"] = 0
    out["_lines"] = 0
    out["_last_ts"] = ""
    out["_cnt"] = ""
    if not os.path.exists(path):
        return out
    out["_bytes"] = os.path.getsize(path)
    with open(path, "rb") as f:
        for raw in f:
            out["_lines"] += 1
            for k, m in marks.items():
                if m in raw:
                    out[k] += 1
            t = TS_RE.match(raw)
            if t:
                out["_last_ts"] = t.group(1).decode()
            c = CNT_RE.search(raw)
            if c:
                out["_cnt"] = c.group(1).decode("utf-8", "replace").strip()
    return out


def tap_pids() -> list:
    try:
        import subprocess
        r = subprocess.run(["ps", "-Ao", "pid,command"], capture_output=True, text=True)
        return [ln.split()[0] for ln in r.stdout.splitlines()
                if "serial_tap.py" in ln and "grep" not in ln]
    except Exception:
        return []


def main() -> int:
    if not os.path.exists(TSV):
        with open(TSV, "w", encoding="utf-8") as f:
            f.write("# 창2 상시 요약 — **누적값**이다(구간값 아님). 판정 자료가 아니라 생존 눈금이다.\n")
            f.write("iso\tser_bytes\tser_lines\tser_last\tevent\tbanner\tresync\tskip\tokto\t"
                    "busy\ttx\tsendok\tipd\tsrv_accept\tsrv_sframe\tsrv_ack\tsrv_down\ttap_pids\tgrew\n")
    prev = -1
    while True:
        now = datetime.now()
        s = count(SER, SER_MARKS)
        v = count(SRV, SRV_MARKS)
        pids = tap_pids()
        grew = "?" if prev < 0 else ("yes" if s["_bytes"] > prev else "NO")
        prev = s["_bytes"]

        with open(TSV, "a", encoding="utf-8") as f:
            f.write("\t".join(str(x) for x in [
                now.strftime("%Y-%m-%dT%H:%M:%S"), s["_bytes"], s["_lines"], s["_last_ts"],
                s["event"], s["banner"], s["resync"], s["skip"], s["okto"],
                s["busy"], s["tx"], s["sendok"], s["ipd"],
                v["srv_accept"], v["srv_sframe"], v["srv_ack"], v["srv_down"],
                ",".join(pids) or "-", grew]) + "\n")

        alive = "🟢 살아 있다" if pids else "🔴 **tap 프로세스가 없다**"
        growing = {"yes": "🟢 자라는 중", "NO": "🔴 **안 자란다**", "?": "⏳ 첫 주기"}[grew]
        with open(MD, "w", encoding="utf-8") as f:
            f.write(f"# 창2 현재 상태 — {now:%Y-%m-%d %H:%M:%S} 자동 생성\n\n")
            f.write("> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.\n")
            f.write("> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-win2.md` 참조.\n")
            f.write("> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).\n\n")
            f.write(f"| 계측기 | 상태 |\n|---|---|\n")
            f.write(f"| tap 프로세스 | {alive} (pid {','.join(pids) or '-'}) |\n")
            f.write(f"| 시리얼 로그 성장 | {growing} |\n")
            f.write(f"| 시리얼 마지막 줄 시각 | `{s['_last_ts'] or '-'}` |\n")
            f.write(f"| 시리얼 크기 | {s['_bytes']:,} B · {s['_lines']:,} 줄 |\n\n")
            f.write("## 시리얼 누계\n\n| 표지 | 누계 |\n|---|---|\n")
            for k in ("event", "banner", "resync", "drop", "skip", "okto",
                      "busy", "tx", "sendok", "ipd", "ipfound", "cipstart_err"):
                f.write(f"| `{k}` | {s[k]:,} |\n")
            f.write(f"\n펌웨어 `[CNT]` 마지막 줄: `{s['_cnt'] or '(아직 없음)'}`\n")
            f.write("\n## 서버 누계 (파일 전체 — 창 구간이 아니다)\n\n| 표지 | 누계 |\n|---|---|\n")
            for k in SRV_MARKS:
                f.write(f"| `{k}` | {v[k]:,} |\n")
            f.write("\n---\n\n⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).\n")
            f.write("특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**\n")
        time.sleep(PERIOD)


if __name__ == "__main__":
    raise SystemExit(main())
