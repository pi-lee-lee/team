#!/usr/bin/env python3
"""창 O 감시기 — **사전 등록에 적은 정지·판정 조건을 실제로 본다.**

왜 있나 (2026-08-19 · PREREG-2026-08-19-winO §4)
  이 팀에서 **조건을 적어 놓고 그것을 보는 코드는 안 만드는** 일이 다섯 번 났다.
  적힌 조건은 지켜지는 것처럼 보인다 — 그게 이 결함의 핵심이다.

🔴 그리고 **결과를 볼 자리**까지가 한 벌이다(CLAUDE.md · socket 2026-08-19).
   그래서 이 도구는 조건마다 **분모를 같이 찍는다**. `0/0` 은 `0/0` 이라고 말해 준다 —
   분모 칸이 없으면 `0` 이 혼자 서서 건강처럼 보인다.

무엇을 보나
  ① 계측기 생존 : 탭 프로세스 + **로그 증가분(rx)**
     🔑 끊김과 rx=0 이 **다른 모양**이어야 계측기 정지와 대상 침묵이 갈린다(원장 §계측기 판본)
  ② 서버 epoch  : srv_id/INSTANCE 가 바뀌면 창을 닫아야 한다
  ③ 표본        : bigfill(>=64B 분모)이 판정선 300 에 얼마나 찼나
  ④ 링크 사건   : drop/esprst/stuck/okto/online 변화
  ⑤ WS접속 축   : 서버 요약의 `화면 N(최대 M)` — 🔴 **이름은 `화면` 이지만 세는 것은
                  `:9900` 의 WS 클라이언트 수다**(창 O §5 검정: 주입기 8개에 1→9→1).
                  **모듈 수도 끊김 표시 수도 아니다.** 내 출력에서는 `WS접속` 으로 찍는다 — 갈리면 구간을 갈라야 한다
  ⑥ 새 계수 셋  : `이름충돌`(누적) · `센서갈림 N자리`(🔴 **계기**) · `비자리예약`(누적)
     🔴 `센서갈림` 을 **누적으로 읽지 마라** — "지금 갈린 자리 수"다.
        socket 이 누적으로 만들었다가 실측 32(=state 방송 횟수) 대 실제 2 로 잡고 계기로 바꿨다.
        누적으로 읽으면 **"이중화 고장 32건"** 이 된다.
     ⚠ 없는 칸은 **`0` 이 아니라 "표지없음"** 으로 찍는다(§srv_field)

⚠ 이 도구는 **판정하지 않는다.** 눈금과 분모를 찍을 뿐이다.

사용: python3 monitor/winO_watch.py [시리얼로그] [주기초]
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import time

SER = sys.argv[1] if len(sys.argv) > 1 else "monitor/serial-winO.log"
PERIOD = int(sys.argv[2]) if len(sys.argv) > 2 else 120
SRV = os.path.expanduser("~/parking-logs/parking-server.log")
# 🔴 하드코딩이었다(2026-08-19 고침) — 창 P 를 돌렸더니 **창 O 파일에 섞여 들어갔다.**
#    창마다 기록이 갈려야 한다. 입력 로그 이름에서 유도한다.
OUT = SER.replace("serial-", "").replace(".log", "") + "-watch.log" \
      if SER.startswith("monitor/serial-") else "monitor/watch.log"
THRESH = 300          # 판정선: >=64B 분모(bigfill)
# socket 규약: 이 파일이 있으면 다음 라운드를 안 연다(정본).
# 🔴 창마다 이름이 다르다 — 창 O 것을 창 P 에서 쓰면 **엉뚱한 주입기를 멈추거나 아무것도 안 멈춘다**.
STOP_FILE = sys.argv[3] if len(sys.argv) > 3 else ".team/STOP-O"
# 🔴 hwrst 는 `N/M` 꼴이라 위 KV 정규식(숫자 하나)으로 안 잡힌다 — 따로 뽑는다
RE_HWRST = re.compile(r"hwrst=(\d+)/(\d+)")
FIELDS = ["up", "drop", "esprst", "stuck", "okto", "smiss", "ssovf",
          "cksumng", "penddrop", "pendfill", "bigfill", "bigdrop", "online"]


def say(msg: str) -> None:
    line = time.strftime("%H:%M:%S") + "  " + msg
    with open(OUT, "a") as f:
        f.write(line + "\n")
        f.flush()


def last_cnt(path: str) -> dict:
    """마지막 [CNT] 줄의 필드. ⚠ 이름으로 뽑는다 — 위치 기반 정규식은 그룹이 밀린다(실제로 밀렸다)."""
    best = {}
    try:
        with open(path, "rb") as f:
            for raw in f:
                if b"[CNT]" in raw:
                    ln = raw.decode("utf-8", "replace")
                    best = dict(re.findall(r"([a-z]+)=(\d+)", ln))
    except OSError:
        pass
    return best


def tap_alive() -> int:
    try:
        r = subprocess.run(["ps", "-Ao", "pid,command"], capture_output=True, text=True)
        return sum(1 for ln in r.stdout.splitlines()
                   if "serial_tap.py" in ln and "grep" not in ln)
    except Exception:
        return -1


def srv_epoch() -> str:
    try:
        r = subprocess.run(["grep", "-a", "INSTANCE logfmt", SRV], capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if l.strip()]
        if not lines:
            return "(없음)"
        m = re.search(r"pid=(\d+).*build=(\S+)", lines[-1])
        return "%s/%s" % (m.group(1), m.group(2)) if m else "(파싱실패)"
    except Exception:
        return "(못읽음)"


def srv_field(pat: str, label: str) -> str:
    """서버 소크 요약에서 라벨 한 칸을 뽑는다.

    🔴 **못 찾으면 `0` 이 아니라 "표지없음" 을 낸다.**
       없던 칸이 새로 생기면 파서가 조용히 못 찾고, 그 침묵이 `0` 으로 보이면
       "아직 안 일어났나 보다"로 읽힌다 — `mod_order_changed` 때 겪은 그것이다.
       **`0` 과 "못 찾음"은 다른 것이고 다른 모양으로 찍혀야 한다.**
    """
    try:
        r = subprocess.run(["grep", "-a", label, SRV], capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if label in l]
        if not lines:
            return "표지없음"
        m = re.search(pat, lines[-1])
        return m.group(1) if m else "파싱실패"
    except Exception:
        return "못읽음"


def screens() -> str:
    """`:9900` WS 클라이언트 수. **옛 이름 `화면` 과 새 이름 `WS접속` 을 둘 다 받는다.**

    🔴 세는 것은 `Conn::WS` 소켓 수이고 **탐침·하니스·주입기가 전부 포함된다.**
       사람이 보는 화면 수가 **아니다** — 창 O 실측(주입기 8개에 `1→9→1`)이 그것을 보였다.
    🔑 어느 이름을 잡았는지 같이 찍는다 — **그 라벨 자체가 배포 표지**다.
    ⚠ 사람이 화면 하나만 열어 둔 동안에는 두 뜻이 같은 값이라 안 갈린다.
    """
    for label in ("WS접속", "화면"):
        try:
            r = subprocess.run(["grep", "-a", label + " ", SRV], capture_output=True, text=True)
            lines = [l for l in r.stdout.splitlines() if label + " " in l]
            if not lines:
                continue
            m = re.search(label + r" (\d+)\(최대 (\d+)\)", lines[-1])
            if m:
                tag = "" if label == "WS접속" else "[옛이름]"
                return "%s(최대 %s)%s" % (m.group(1), m.group(2), tag)
        except Exception:
            return "못읽음"
    return "표지없음"


def _screens_old_unused() -> str:
    try:
        r = subprocess.run(["grep", "-a", "화면 ", SRV], capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if "화면 " in l]
        if not lines:
            return "표지없음"          # 🔴 "0" 이 아니다. 못 본 것과 0 을 가른다
        m = re.search(r"화면 (\d+)\(최대 (\d+)\)", lines[-1])
        return "%s(최대 %s)" % (m.group(1), m.group(2)) if m else "파싱실패"
    except Exception:
        return "못읽음"


hit = False


def main() -> int:
    say("«watch» 시작 — 로그=%s 주기=%ds 판정선 bigfill>=%d" % (SER, PERIOD, THRESH))
    base_epoch = srv_epoch()
    say("«watch» 기준 epoch=%s · **WS접속**=%s" % (base_epoch, screens()))
    prev_size = -1
    prev = {}
    while True:
        size = os.path.getsize(SER) if os.path.exists(SER) else -1
        # 🔴 첫 주기는 비교 대상이 없다. `0` 으로 찍으면 "대상 침묵"으로 오독된다(원장 1.1).
        rx = (size - prev_size) if prev_size >= 0 else None
        alive = tap_alive()
        c = last_cnt(SER)
        ep = srv_epoch()
        scr = screens()

        # ① 계측기 생존 — 끊김과 rx=0 을 다른 모양으로 남긴다
        if alive == 0:
            say("🔴 «watch» 탭 프로세스 0 — **계측기가 죽었다.** 정지 조건 충족")
        elif size < 0:
            say("🔴 «watch» 로그 파일 없음 — 정지 조건 충족")

        # ② epoch 변경
        if ep != base_epoch:
            say("🔴 «watch» 서버 epoch 변경 %s → %s — **창을 닫아야 한다**" % (base_epoch, ep))

        # ④ 링크 사건
        evt = []
        for k in ("drop", "esprst", "stuck", "okto", "smiss", "ssovf", "penddrop", "bigdrop"):
            a, b = prev.get(k), c.get(k)
            if a is not None and b is not None and a != b:
                evt.append("%s %s→%s" % (k, a, b))
        if prev.get("online") != c.get("online") and prev:
            evt.append("online %s→%s" % (prev.get("online"), c.get("online")))

        # ③ 표본 — 분모를 항상 같이 찍는다
        bf = int(c.get("bigfill", 0) or 0)
        mark = "판정가능" if bf >= THRESH else "**흔적**(판정선 미달)"
        rxs = "기준(첫주기·비교대상없음)" if rx is None else ("%dB" % rx)
        # 🔴 2026-08-19 배포로 새로 생기는 칸 셋. 없으면 "표지없음"으로 찍힌다(0 아님).
        nc  = srv_field(r"이름충돌 (\d+)", "이름충돌")
        # ⚠ **계기다(누적 아님).**
        # 🔴🔴 2026-08-19 16:1x **표지 취소** — 앞서 "기준선 3 · 정상" 과 "3→0 = 0270 도입"을
        #    찍게 해 뒀는데 **둘 다 틀렸다.** 굽지도 않았는데 16:10:53 에 0 이 됐다.
        #    원인: **점유 비트가 런타임에 움직인다**([TX] S,…,60A → 609). 슬롯이 토글되면 짝이 우연히 맞는다.
        # 🔑 **`센서갈림` 은 상수가 아니라 상태다.** 그러므로 **도입 표지로 쓸 수 없다.**
        #    좋은 표지의 조건: **그 사건 말고 다른 것으로는 만들어질 수 없어야 한다.**
        #    → 산출물 표지를 써라 : **등록 145B → 143B** · **`D,E1,OB`** (런타임에 안 변한다)
        # ⚠ 그리고 라벨에서 **"정상"을 뺐다** — "정상"이라는 말이 이 값을 검산 대상에서 빼고 있었다.
        sg  = srv_field(r"센서갈림 (\d+)자리", "센서갈림")
        if sg.isdigit():
            sg = "%s자리(⚠ **상태값 · 런타임에 움직인다. 표지로 쓰지 마라**)" % sg
        else:
            sg = sg + "자리"
        nrv = srv_field(r"비자리예약 (\d+)", "비자리예약")
        # 🔴 hwrst: N=실행 · M=성공. **분모를 같이 찍는다** — 0 이 혼자 서면 셋을 못 가른다
        hw = "표지없음"
        try:
            with open(SER, "rb") as _f:
                for _raw in _f:
                    _m = RE_HWRST.search(_raw.decode("utf-8", "replace"))
                    if _m:
                        n, m2 = int(_m.group(1)), int(_m.group(2))
                        if n == 0:
                            # 🔴 2026-08-19 16:2x 정정 — `ESP_RST_WIRED` 가 **1 → 0 으로 되돌았다**
                            #    (사용자: "리셋선은 없다"). `espRstAssert` 본문이 통째로 `#if` 밖이라
                            #    **호출돼도 안 센다. 항상 0/0 이다.**
                            # ⚠ 그러므로 `0/0` 을 **"4단이 아직 안 왔다"로 읽으면 틀린다 — "4단이 없다"** 가 맞다.
                            #    **판정 자료가 아니다.**
                            hw = "0/0 ⚠ **4단이 *없다*(ESP_RST_WIRED=0). 판정 자료 아님**"
                        elif m2 == 0:
                            hw = "🔴 %d/0 **실행했는데 안 들었다 → 전원 쪽**" % n
                        elif m2 == n:
                            hw = "✅ %d/%d **4단이 듣는다**" % (n, m2)
                        else:
                            hw = "⚠ %d/%d 일부만 들었다" % (n, m2)
        except OSError:
            pass
        say("«watch» 살아있음 탭=%d rx=%s · up=%s · bigfill=%d/%d %s · cksumng=%s · **WS접속**=%s"
            " · 이름충돌=%s · 센서갈림=%s(계기) · 비자리예약=%s · **hwrst=%s**%s"
            % (alive, rxs, c.get("up", "?"), bf, THRESH, mark,
               c.get("cksumng", "?"), scr, nc, sg, nrv, hw,
               (" · 🔴 사건: " + ", ".join(evt)) if evt else ""))

        # 🔴 정지선 도달을 **한 번만, 눈에 띄게** 찍는다.
        #    socket 은 이 계수를 볼 수 없다(장치 [CNT] 는 내 시리얼에만 있다) —
        #    그래서 "정지선을 코드가 본다"는 나에게만 성립한다. 도달하면 내가 알려야 한다.
        global hit
        if bf >= THRESH and not hit:
            hit = True
            # 🔴 **파일이 정본이다**(socket 규약). 메시지는 보조 —
            #    내가 다른 일 중이면 늦게 읽히고, 그 지연이 그대로 분모에 들어간다.
            #    그래서 사람이 아니라 **이 감시기가 직접** 만든다.
            try:
                os.makedirs(".team", exist_ok=True)
                with open(STOP_FILE, "w") as sf:
                    sf.write("bigfill=%d >= %d at %s\n" % (bf, THRESH, time.strftime("%H:%M:%S")))
                say("🔴🔴 «watch» **정지선 도달** bigfill=%d >= %d — %s 생성했다" % (bf, THRESH, STOP_FILE))
            except OSError as e:
                say("🔴🔴 «watch» **정지선 도달** bigfill=%d 인데 %s 생성 실패: %s — **손으로 알려라**"
                    % (bf, STOP_FILE, e))

        prev_size, prev = size, c
        time.sleep(PERIOD)


if __name__ == "__main__":
    raise SystemExit(main())
