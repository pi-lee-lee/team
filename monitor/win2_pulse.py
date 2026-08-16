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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zeroguard import z  # noqa: E402  — `0` 출력 규약(원장 7.44)

SER = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-win2.log"
SRV = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser("~/parking-logs/parking-server.log")
PERIOD = int(sys.argv[3]) if len(sys.argv) > 3 else 300

# 🔴 산출 경로는 **시리얼 로그 이름에서 파생**한다 — 상수로 두면 안 된다.
#   창2 를 닫고 창3 을 열 때 밟을 뻔했다: 경로가 고정이면 새 인스턴스가
#   **옛 창의 요약 파일을 덮어쓰거나 두 인스턴스가 같은 파일에 뒤섞여 쓴다.**
#   `serial-win3.log` → `win3-pulse.tsv` · `WIN3-STATUS.md`
_stem = os.path.basename(SER)
for _p in (".log",):
    if _stem.endswith(_p):
        _stem = _stem[: -len(_p)]
_stem = _stem.replace("serial-", "") or "win"
TSV = f"monitor/{_stem}-pulse.tsv"
MD = f"monitor/{_stem.upper()}-STATUS.md"

# 시리얼 표지 — 새 펌웨어 539ac53 의 새 줄 둘을 포함한다(arduino 통보 23:59).
# ⚠ 표지를 여기 적어 두는 이유: 파서에 없으면 조용히 0 이 된다(원장 1.1 · 5.1).
# 🔴🔴 2026-08-17 07:5x — **칸 이름을 전부 바꿨다. 내 결함이 사고를 냈다.**
#
#   옛 이름 `drop` 은 **`[TX-DROP]` 줄 수**였는데, 펌웨어 `[CNT] drop=`(=`linkDrops`)과
#   **이름이 같았다.** 루트가 `WIN3-STATUS.md` 표의 `drop 15` 를 펌웨어 계수기로 읽고
#   **그 위에 가설을 세웠다**("장치가 여전히 A 수준으로 끊겼다고 판단한다").
#   실제로는 `[CNT] drop=1` 로 밤새 고정이었고 **가설은 폐기됐다.**
#   (값이 15→21 로 자란 것 자체가 증거였다 — `linkDrops` 는 1 에 고정인데 그 칸은 계속 자랐다.)
#
#   🔑 **이 표는 전부 "로그 줄 수" 다. 펌웨어 `[CNT]` 계수기가 아니다.**
#      `resync`·`skip`·`okto`·`drop` 은 **넷 다** `[CNT]` 에 같은 이름 필드가 있어
#      **넷 다 같은 함정**이었다. 그래서 원문 표지를 그대로 쓰는 이름으로 바꾼다.
#      **이름은 "측정한 것"으로 짓는다**(원장 1.8 · 1.16).
SER_MARKS = {
    "fail3": "전송 3회 연속 실패".encode(),       # 링크 끊김 사건 (옛 이름 `event`)
    "banner": b"[PARKING NODE]",                  # ⚠ **Uno** 부트 배너(우리 DTR 리셋 포함)
    # 🔴 2026-08-17 08:2x 추가 — `0` 규약이 표에 표지를 찍게 하자 **곧바로 드러났다**:
    #   위 `banner` 는 **Uno** 것이고, **기전 B(ESP 모듈 리셋)의 배너는 이것**이다.
    #   둘을 헷갈려서 `garbage_lead.py` 가 배너를 0건으로 셌었다(원장 7.43).
    #   **이 표에는 ESP 배너 칸이 아예 없었다** — 즉 기전 B 를 요약기가 못 보고 있었다.
    "espbanner": b"System Ready",                 # ESP 모듈 리셋 배너 ← 기전 B 표지 ①
    "zeroip": b'"0.0.0.0"',                       # IP 소실           ← 기전 B 표지 ②
    "txresync": b"[TX-RESYNC]",                   # ⚠ [CNT] resync= 와 다르다
    "txdrop": b"[TX-DROP]",                       # ⚠ [CNT] drop= 와 다르다 ← 사고를 낸 칸
    "txwait_skip": "이번 주기는 건너뛴다".encode(),  # ⚠ [CNT] skip= 와 다르다
    "txwait_okto": "상한 초과".encode(),           # ⚠ [CNT] okto= 와 다르다
    "busy": b'"busy ',
    "tx": b"[TX] ",
    "sendok": b'"SEND OK"',
    "ipd": b"+IPD,",                               # 🔑 하행 수신 — D1/D2 를 가르는 유일한 표지
    "ipfound": "★ IP 확보".encode(),
    "cipstart_err": b"Unlink",
    # 🔴 기전 C(계측기 자해 · 언더플로) 표지. 루트 지시 2026-08-17 02:0x —
    #   전제는 "하행 ACK 가 있어야 발동한다" 이고, 무주입 구간에 이 줄이 뜨면 **전제가 깨진 것**이다.
    #   내 세션이 죽어도 잡혀야 하므로 요약기가 세고, 0 이 아니면 경보 파일을 쓴다.
    "stall": "★ 정지 감지".encode(),
    # 🔴 C-2 경로 — arduino 가 `lastTxOkAt` 대입을 `:2148 ALREADY CONNECTED` · `:2160 CONNECT`
    #   에서도 찾았다. **하행 없이 재접속만으로도 언더플로가 가능하다.**
    #   그래서 경보 판정식의 분모가 `+IPD` 만이 아니라 `+IPD + CONNECT` 다(루트 확정 07:5x).
    "connect": "online (CONNECT)".encode(),
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
    out["_first_ts"] = ""
    out["_cnt"] = ""
    # 🔴 우리 DTR 리셋분을 갈라 센다(루트 지적 2026-08-17 08:2x).
    #   탭을 열면 DTR 로 보드가 리셋되고 **ESP 배너가 반드시 하나 뜬다.**
    #   그걸 안 빼면 **모든 창에서 `espbanner` 가 1 부터 시작**해 기본값처럼 깔리고,
    #   경보를 걸면 창을 열자마자 오탐이 난다.
    #   기준: 로그 첫 타임스탬프(=t0)로부터 **120초 이내 = 우리 것**(t_start 규약과 같은 폭).
    out["espbanner_ours"] = 0
    out["espbanner_evt"] = 0
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
                cur = t.group(1).decode()
                out["_last_ts"] = cur
                if not out["_first_ts"]:
                    out["_first_ts"] = cur
                if marks.get("espbanner") and marks["espbanner"] in raw:
                    def _s(x):
                        h, m2, s2 = (int(v) for v in x.split(":"))
                        return h * 3600 + m2 * 60 + s2
                    d = _s(cur) - _s(out["_first_ts"])
                    if d < 0:
                        d += 86400
                    if d <= 120:
                        out["espbanner_ours"] += 1
                    else:
                        out["espbanner_evt"] += 1
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
            # ⚠ 창 이름을 하드코딩하지 마라 — `win3-pulse.tsv` 머리글이 "창2" 라고 적혀 있었다.
            #    파일명과 내용이 어긋나면 나중에 어느 창 자료인지 다투게 된다(원장 1.5).
            f.write(f"# {_stem} 상시 요약 — 원본 {SER} · **누적값**이다(구간값 아님).\n")
            f.write("# 판정 자료가 아니라 생존 눈금이다. 갈래·λ·도달률을 여기서 만들지 마라.\n")
            f.write("iso\tser_bytes\tser_lines\tser_last\tfail3\tbanner\ttxresync\ttxwait_skip\ttxwait_okto\t"
                    "busy\ttx\tsendok\tipd\tstall\tsrv_accept\tsrv_sframe\tsrv_ack\tsrv_down\t"
                    "tap_pids\tgrew\n")
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
                s["fail3"], s["banner"], s["txresync"], s["txwait_skip"], s["txwait_okto"],
                s["busy"], s["tx"], s["sendok"], s["ipd"], s["stall"],
                v["srv_accept"], v["srv_sframe"], v["srv_ack"], v["srv_down"],
                ",".join(pids) or "-", grew]) + "\n")

        # 🔴 기전 C 경보 — 무주입인데 정지 감지가 뜨면 전제가 깨진 것이다(루트 지시)
        if s["stall"]:
            with open("monitor/ALERT-win3-stall.md", "w", encoding="utf-8") as af:
                af.write(f"# 🔴 `★ 정지 감지` {s['stall']}건 — {now:%Y-%m-%d %H:%M:%S} 자동 기록\n\n")
                af.write("**기전 C(계측기 자해 · 부호 없는 뺄셈 언더플로)의 표지다.**\n")
                af.write("근거: `monitor/FINDING-2026-08-17-0125-underflow.md`\n\n")
                af.write(f"| | |\n|---|---|\n")
                af.write(f"| `★ 정지 감지` 누계 | **{s['stall']}** |\n")
                af.write(f"| `+IPD`(하행 수신) 누계 | **{s['ipd']}** |\n")
                af.write(f"| `CONNECT`(재접속) 누계 | **{s['connect']}** |\n")
                af.write(f"| **알려진 경로 합** | **{s['ipd'] + s['connect']}** |\n\n")
                # 🔴 판정 기준은 `정지 감지 > 하행` 이다 — 하행 1건당 거의 1건이 알려진 거동이므로,
                #    그 수를 **넘는** 순간이 "하행 없이도 발동한다"의 증거다. (ipd==0 은 그 특수한 경우)
                # ⚠ `espbanner` 는 경보식에 안 쓴다(우리 DTR 분이 섞인다). 참고로만 찍는다.
                known = s["ipd"] + s["connect"]
                if s["stall"] > known:
                    af.write("## 🔴🔴 **전제가 깨졌다 — 즉시 알려라**\n\n")
                    af.write(f"`정지 감지 {s['stall']}` > `하행 {s['ipd']} + 재접속 {s['connect']} = {known}`.\n")
                    af.write("루트·arduino 의 전제는 *\"하행 ACK 가 있어야 발동한다\"* 였다.\n")
                    af.write("`drainPending()` 말고 **다른 경로**가 `lastTxOkAt` 을 갱신한다는 뜻이다\n")
                    af.write("(arduino 가 `sensorTick()` 을 의심하고 확인 중이었다).\n\n")
                    af.write("**즉 지금 도는 무주입 λ 측정이 이 결함에 오염된다.**\n")
                    af.write("판정에서 이 사건들을 **기전 C 로 따로 빼야 한다.**\n")
                else:
                    af.write(f"✅ `정지 감지 {s['stall']}` <= `하행 {s['ipd']} + 재접속 {s['connect']}"
                             f" = {known}` — **알려진 경로(C-1 하행 · C-2 재접속)로 설명된다.**\n")
                    af.write("⚠ 이 수를 **넘는 순간** 전제가 깨진 것이다. 이 파일이 그때 바뀐다.\n")
                af.write("\n원문을 열어라:\n```\ngrep -a -n '정지 감지' monitor/serial-win3.log\n```\n")

        alive = "🟢 살아 있다" if pids else "🔴 **tap 프로세스가 없다**"
        growing = {"yes": "🟢 자라는 중", "NO": "🔴 **안 자란다**", "?": "⏳ 첫 주기"}[grew]
        with open(MD, "w", encoding="utf-8") as f:
            # ⚠ 창 이름 하드코딩 금지(원장 7.27) — MD 쪽에도 "창2" 가 박혀 있었다
            f.write(f"# {_stem} 현재 상태 — {now:%Y-%m-%d %H:%M:%S} 자동 생성\n\n")
            f.write("> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.\n")
            f.write(f"> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-{_stem}.md` 참조.\n")
            f.write("> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).\n\n")
            f.write(f"| 계측기 | 상태 |\n|---|---|\n")
            f.write(f"| tap 프로세스 | {alive} (pid {','.join(pids) or '-'}) |\n")
            f.write(f"| 시리얼 로그 성장 | {growing} |\n")
            f.write(f"| 시리얼 마지막 줄 시각 | `{s['_last_ts'] or '-'}` |\n")
            f.write(f"| 시리얼 크기 | {s['_bytes']:,} B · {s['_lines']:,} 줄 |\n\n")
            f.write("## 시리얼 누계 — 🔴 **로그 줄 수다. 펌웨어 `[CNT]` 계수기가 아니다**\n\n")
            f.write("> `txdrop`·`txresync`·`txwait_skip`·`txwait_okto` 는 `[CNT]` 의 같은 이름\n")
            f.write("> 필드와 **다른 것을 센다.** 펌웨어 값은 아래 `[CNT]` 줄을 그대로 봐라.\n\n")
            f.write("| 표지(줄 수) | 누계 | 찾은 문자열 |\n|---|---|---|\n")
            # 🔴 `0` 규약 — 0 이면 **찾은 문자열과 분모**를 같이 보여 준다(원장 7.44).
            #   루트가 이 표의 `drop 15` 를 펌웨어 계수기로 오독해 가설 하나가 죽었다.
            #   표지를 같이 찍으면 "무엇을 센 값인가" 를 표에서 바로 볼 수 있다.
            for k in ("fail3", "banner", "espbanner", "zeroip", "txresync", "txdrop",
                      "txwait_skip", "txwait_okto", "busy", "tx", "sendok", "ipd",
                      "connect", "ipfound", "cipstart_err", "stall"):
                mk = SER_MARKS[k].decode("utf-8", "replace")
                warn = "" if s[k] else f"  ⚠ 훑은 줄 {s['_lines']:,}"
                extra = ""
                if k == "espbanner":
                    extra = (f" — **우리 DTR {s['espbanner_ours']} / 사건 "
                             f"{s['espbanner_evt']}**")
                f.write(f"| `{k}` | {s[k]:,}{warn}{extra} | `{mk}` |\n")
            f.write(f"\n펌웨어 `[CNT]` 마지막 줄: `{s['_cnt'] or '(아직 없음)'}`\n")
            f.write("\n## 서버 누계 (파일 전체 — 창 구간이 아니다)\n\n| 표지 | 누계 |\n|---|---|\n")
            for k in SRV_MARKS:
                f.write(f"| `{k}` | {v[k]:,} |\n")
            f.write("\n---\n\n⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).\n")
            f.write("특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**\n")
        time.sleep(PERIOD)


if __name__ == "__main__":
    raise SystemExit(main())
