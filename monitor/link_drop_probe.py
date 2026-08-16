#!/usr/bin/env python3
"""**재부팅 없이 링크만 끊긴** 사례를 센다 — 두 번째 메커니즘이 있는지 확인한다.

물음(루트 REQ 지시):
  1. 10:51:02 같은 양상이 **몇 번** 있나? 1회뿐이면 조사 가치가 낮다.
     **표본 1건에 기전을 붙이지 않는다.**
  2. 끊은 주체가 장치가 아니라면 **서버 쪽 회수(유휴 마감·keepalive)**인가 **TCP 레벨**인가?

판별 원리 — `uptime` 은 Uno 의 millis()/1000 이다(arduino-engineer 확인).
  새 연결 직후 첫 S 프레임의 uptime 이
    작다(≲120s)  → 방금 부팅했다 = **재부팅**
    크다(>120s)  → 장치는 계속 살아 있었다 = **링크만 끊겼다**  ← 찾는 것

두 양상을 가른다:
  (A) 재연결형  — 연결이 끊겼다가 새로 붙었는데 장치는 안 죽었다
  (B) 같은연결형 — TCP 는 유지된 채 프레임만 멈췄다(서버가 `같은 연결` 복구로 기록)

⚠ 오염 구간(2026-08-16 15:26~)은 기본으로 보지 않는다. 사용자가 보드를 뽑았다 끼운 구간이다.

사용: python3 monitor/link_drop_probe.py [로그] [시작ISO] [끝ISO]
"""
from __future__ import annotations

import os
import re
import sys
from datetime import date, datetime, timedelta

LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/parking-soak.log"
SINCE = datetime.fromisoformat(sys.argv[2]) if len(sys.argv) > 2 else datetime(2026, 8, 15, 20, 32, 16)
UNTIL = datetime.fromisoformat(sys.argv[3]) if len(sys.argv) > 3 else datetime(2026, 8, 16, 15, 26, 0)
BASE = date(2026, 8, 16)
# 오염 **구간** — 열린 경계로 두지 마라.
# 열어 두면 앞으로 모든 관측창에서 경고가 뜨고, 늘 뜨는 경고는 곧 무시당한다.
# 그러면 진짜 오염이 왔을 때 아무도 안 본다.
CONTAM_FROM = datetime(2026, 8, 16, 15, 26, 0)   # 사용자 실물 작업 시작
CONTAM_TO = datetime(2026, 8, 16, 16, 15, 37)    # 서버 로그가 끊긴 시각 = 자료의 끝
# ⚠ 관측 재개 후는 **새 기준선**이라 오염이 아니다(사용자가 실물 구성을 바꿨을 수 있으므로
#   옛 구간과 같은 표에 넣지 않을 뿐이다 — REQ-0112).

# 재부팅으로 볼 uptime 상한. 이보다 크면 "장치는 살아 있었다".
# 근거: 실측 기동 창 **11.3초**(arduino-engineer, 커밋 337c926 — 옛 36초 값은 폐기됐다).
# 갓 부팅한 장치의 첫 프레임 uptime 은 11초대이므로 120 은 넉넉하고 안전한 문턱이다.
BOOT_UPTIME_MAX = 120

# 🔴 서버가 없을 때 장치는 **0.90초 주기로 백오프 없이** 재시도한다(arduino 실측).
# 이건 결함이 아니라 설계된 거동이므로 **링크 끊김으로 세면 안 된다.**
# 서버 인스턴스가 막 떴을 때 몰려드는 접속은 "끊겨서 다시 붙은 것"이 아니라
# "계속 두드리고 있던 것이 드디어 받아들여진 것"이다.
SERVER_START_GRACE_S = 90

TS = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\s+(.*)$")
SFRAME = re.compile(r"←ARD\s+S,(\d+),([01]+),([01]+),(\d+),(\w+),(\d+)")


TS_DATED = re.compile(r"^(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})\s+(.*)$")


def parse(path: str):
    """옛 형식(날짜 없음)과 계약 v0.1(날짜 있음)을 **한 파일 안에서 동시에** 다룬다.

    · 날짜 있는 줄  → 그 날짜를 그대로 쓴다(추측 없음).
    · 날짜 없는 줄  → 뒤에서 앞으로 앵커링. 앞에서 잡으면 하루씩 밀린다
      (2026-08-16 에 나와 socket 과 루트가 각각 이 함정에 빠졌다).
    """
    with open(path, "rb") as f:
        raw = f.read()
    rows = []          # (hms 또는 None, 절대날짜 또는 None, 본문)
    for line in raw.decode("utf-8", "replace").splitlines():
        dm = TS_DATED.match(line)
        if dm:
            rows.append(((int(dm[4]), int(dm[5]), int(dm[6])),
                         date(int(dm[1]), int(dm[2]), int(dm[3])), dm[7]))
            continue
        m = TS.match(line)
        if m:
            rows.append(((int(m[1]), int(m[2]), int(m[3])), None, m[4]))

    stamps = [None] * len(rows)
    # 날짜가 명시된 줄이 하나라도 있으면 **가장 뒤의 것**을 기준으로 삼는다.
    last_dated = next((i for i in range(len(rows) - 1, -1, -1) if rows[i][1]), None)
    if last_dated is not None:
        day = rows[last_dated][1]
        tail = last_dated
    else:
        day, tail = BASE, len(rows) - 1
    nxt = None
    for i in range(tail, -1, -1):
        hms, d, _ = rows[i]
        if d is not None:
            day = d
        elif nxt is not None and hms > nxt:
            day -= timedelta(days=1)
        nxt = hms
        stamps[i] = datetime.combine(day, datetime.min.time()).replace(
            hour=hms[0], minute=hms[1], second=hms[2])
    # 기준점 뒤쪽(날짜 명시 구간)은 그대로 앞으로 채운다
    for i in range(tail + 1, len(rows)):
        hms, d, _ = rows[i]
        if d is not None:
            day = d
        stamps[i] = datetime.combine(day, datetime.min.time()).replace(
            hour=hms[0], minute=hms[1], second=hms[2])
    return [(ts, r[2]) for r, ts in zip(rows, stamps) if ts is not None]


def provenance() -> None:
    """출처를 **출력 자체에** 찍는다.

    2026-08-16: 내가 만든 합성 픽스처의 출력을 실물 로그 출력처럼 붙여 보고했고,
    루트가 실물로 읽었다. 문제는 "잘못 읽은 것"이 아니라 **그렇게 읽히게 쓴 출력이
    판정문에 들어갔으면 아무도 못 걸렀을 것**이라는 점이다.
    → 사람이 붙일 때 기억하는 것에 의존하지 않는다. 출력이 스스로 밝힌다.
    """
    p = os.path.abspath(LOG)
    fake = any(k in os.path.basename(p).lower() for k in ("fake", "fixture", "sample", "synthetic"))
    if fake:
        print("#" * 72)
        print("# ⚠⚠ 합성 픽스처 출력이다 — 실물 관측이 아니다. 판정 근거로 인용하지 마라.")
        print(f"# 원본: {p}")
        print("#" * 72)
    else:
        print(f"# 원본 로그: {p}")


def main() -> int:
    if SINCE < CONTAM_TO and UNTIL > CONTAM_FROM:
        print(f"⚠ 요청 구간이 오염 구간({CONTAM_FROM} ~ {CONTAM_TO})과 겹친다.")
        print("   그 안의 재부팅·단절은 사용자가 보드를 뽑았다 끼운 결과다 — 장비 판정에 쓰지 마라.\n")

    provenance()
    ev = [(t, s) for t, s in parse(LOG) if SINCE <= t <= UNTIL]
    frames = [(t, int(SFRAME.search(s).group(4))) for t, s in ev if SFRAME.search(s)]
    print(f"# 구간 {SINCE} ~ {UNTIL}   ({(UNTIL - SINCE).total_seconds()/3600:.2f}h)")
    print(f"# S프레임 {len(frames):,}건")
    if len(frames) < 100:
        print("⚠ 프레임이 너무 적다 — 이 구간으로는 판별할 수 없다.")
        return 1

    # 서버 기동 시각들 — 그 직후 접속은 "설계된 재시도가 받아들여진 것"이라 제외한다.
    #
    # 🔴 2026-08-17 00:1x — REQ-0112 파서 감사. **처음에 내가 틀리게 진단했다. 둘 다 적는다.**
    #
    #   ❌ 내 첫 진단: "`startswith('=== INSTANCE')` 가 `=== INSTANCE-END` 도 통과하므로
    #      서버 종료를 기동으로 오인해 잘못 제외한다."
    #      → **틀렸다. 그 가지는 애초에 도달하지 않는다.** `parse()` 는 타임스탬프로 시작하는
    #      줄만 내보내는데 `=== INSTANCE…` 줄에는 타임스탬프 접두어가 없다. 그래서
    #      `ev` 에 들어오지 않는다. 합성 픽스처로 확인했다(`monitor/fixture-instance-end.log`):
    #      동결본과 고친 판이 **같은 결과**를 냈고 둘 다 "서버 기동 표지 0건" 이었다.
    #      → 오분류는 **일어난 적이 없다.** A 판정도 영향 없다.
    #
    #   ✅ 실제로 성립하는 문제는 다른 것이다: **그래서 이 제외는 지금 `⏱ 소크 관측 시작`
    #      한 줄에만 의존한다.** 실측(2026-08-16 로그 전수): `=== INSTANCE` 20건 ·
    #      `⏱ 소크 관측 시작` 20건 — 1:1 이라 지금은 맞는다. 그러나 그 한국어 문자열이
    #      한 번 바뀌면(이미 `오프라인 판정`→`무프레임 판정` 전례가 있다) **제외가 조용히 0**
    #      이 되고, 기동 직후 몰림이 전부 링크 끊김으로 세어진다(원장 1.1 의 '못 셈').
    #
    #   → 고침: 기계 판독용 `=== INSTANCE … start=<ISO>` 를 **원본에서 따로 긁어** 더한다.
    #     `start=` 를 키로 쓰므로 `INSTANCE-END`(`stop=`)는 구조적으로 안 들어온다.
    #   ⚠ `frozen-A/link_drop_probe.py` 는 **고치지 않는다**(고치면 A 를 다른 자로 재게 된다).
    #     이 파일과 동결본은 이 지점에서 **의도적으로 다르다.** A 재현은 확인했다(바이트 동일).
    starts = [t for t, s in ev if "소크 관측 시작" in s]
    with open(LOG, "rb") as _f:
        for _line in _f.read().decode("utf-8", "replace").splitlines():
            _t = _line.lstrip()
            if not _t.startswith("=== INSTANCE") or _t.startswith("=== INSTANCE-END"):
                continue
            _m = re.search(r"\bstart=(\S+)", _t)
            if not _m:
                continue
            try:
                _ts = datetime.fromisoformat(_m.group(1)).replace(tzinfo=None)
            except ValueError:
                continue
            if SINCE <= _ts <= UNTIL and _ts not in starts:
                starts.append(_ts)
    all_accepts = [t for t, s in ev if "+? 연결 수락" in s]

    def in_grace(t):
        return any(0 <= (t - st).total_seconds() <= SERVER_START_GRACE_S for st in starts)

    accepts = [t for t in all_accepts if not in_grace(t)]
    excluded = [t for t in all_accepts if in_grace(t)]
    excluded_startup = len(excluded)
    same_conn = [(t, s) for t, s in ev if "온라인 복귀" in s and "같은 연결" in s]
    reconn = [(t, s) for t, s in ev if "온라인 복귀" in s and "재연결" in s]

    # ── (A) 재연결형: ACCEPT 직후 첫 프레임의 uptime 이 크면 장치는 안 죽었다 ──
    print()
    print("## (A) 재연결형 — 붙었는데 장치는 이미 오래 살아 있던 경우")
    print(f"   ACCEPT {len(accepts)}건 검사 · uptime > {BOOT_UPTIME_MAX}s 이면 '재부팅 아님'")
    # ⚠ 제외는 **항상** 찍는다. 0건이어도 찍는다.
    #    "조용히 사라지는 것이 제일 나쁘다"(루트, REQ-0112) — 이 창 안에 진짜 끊김이 있었어도
    #    같이 지워지므로, 나중에 "이 창은 왜 비었지"를 물을 수 있어야 한다.
    print(f"   서버 기동 직후 {SERVER_START_GRACE_S}초 제외: **{excluded_startup}건**"
          f" (서버 기동 표지 {len(starts)}건 기준)")
    if excluded:
        print("   ↓ 제외된 접속 — 지표에서 뺐을 뿐 없던 일이 아니다. 직접 확인하라:")
        for t in excluded[:10]:
            nxt = next((f for f in frames if f[0] >= t), None)
            up = nxt[1] if nxt and (nxt[0] - t).total_seconds() <= 180 else None
            note = ("uptime %s → %s" % (up, "재부팅" if up is not None and up <= BOOT_UPTIME_MAX
                                        else "장치 생존")) if up is not None else "직후 프레임 없음"
            print(f"     {t.strftime('%m-%d %H:%M:%S')}  {note}")
        if len(excluded) > 10:
            print(f"     … 외 {len(excluded)-10}건")
    print()
    # ── 판정 규칙: **공백 보정**. 고정 문턱(uptime>120)은 틀린다 ──────────
    #
    # 순진한 규칙은 "다음 프레임 uptime 이 크면 안 죽었다"이다. 그런데 공백이 길면
    # **재부팅했어도 새 uptime 이 문턱을 넘어선다.** 관측값 사이에 낀 시간을 계산에
    # 안 넣은 것이 원인이고, socket-engineer 의 (A) 구현도 독립적으로 같은 실수를 했다.
    #
    #   G = 직전 프레임 도착 → 이번 프레임 (초)
    #   안 죽었다면  이번 uptime = 옛 uptime + G  →  **반드시 G 이상**
    #   죽었다면     부팅이 공백 안에서 일어났다  →  **G 미만**
    #   → 기준은 uptime < G (허용오차 2초: 장치 시계 절삭 + 도착 틱)
    #
    # 실증: uptime 3 → 8 인데도 공백이 21초면 재부팅이다. 8초 가동시간은 21초를 못 건넌다.
    UPTIME_TOL_S = 2
    alive, booted, nodata = [], 0, 0
    for t in accepts:
        idx = next((i for i, f in enumerate(frames) if f[0] >= t), None)
        if idx is None or (frames[idx][0] - t).total_seconds() > 180:
            nodata += 1
            continue
        nxt = frames[idx]
        prev = frames[idx - 1] if idx > 0 else None
        if prev is not None:
            gap = (nxt[0] - prev[0]).total_seconds()
            rebooted = nxt[1] < gap - UPTIME_TOL_S
        else:
            # 앞 프레임이 없으면 공백을 못 재므로 고정 문턱으로 물러선다(차선책)
            rebooted = nxt[1] <= BOOT_UPTIME_MAX
        if rebooted:
            booted += 1
        else:
            alive.append((t, nxt[0], nxt[1]))
    print(f"   재부팅(uptime 작음)        {booted}건")
    print(f"   🔑 재부팅 아님(uptime 큼)  {len(alive)}건")
    print(f"   판별불가(직후 프레임 없음) {nodata}건")
    if alive:
        print()
        print("   ACCEPT시각   첫프레임   uptime   직전 60초 서버 이벤트")
        for t, ft, up in alive[:20]:
            pre = [s for tt, s in ev if 0 < (t - tt).total_seconds() <= 60
                   and any(k in s for k in ("회수", "종료", "errno", "오프라인", "무프레임"))]
            tag = "; ".join(x[:46] for x in pre[-2:]) or "(없음)"
            print(f"   {t.strftime('%m-%d %H:%M:%S')}  +{(ft-t).total_seconds():>4.0f}s  {up:>6}   {tag}")

    # ── (B) 같은연결형: TCP 는 살아 있는데 프레임만 멈춘 경우 ──
    print()
    print("## (B) 같은연결형 — TCP 는 유지된 채 프레임만 멈춘 경우")
    print(f"   같은 연결 복구 {len(same_conn)}건 · 재연결 복구 {len(reconn)}건")

    # ── 끊은 주체 분류: 서버 회수인가 TCP 레벨인가 ──
    print()
    print("## 끊은 주체 — 서버가 회수했나, TCP 가 끊겼나")
    def count(*keys):
        return sum(1 for _, s in ev if any(k in s for k in keys))
    rows = [
        ("서버 회수(유휴 마감)", count("유휴 마감")),
        ("서버 회수(keepalive)", count("keepalive 시간초과", "회수 — keepalive")),
        ("TCP 리셋 errno=54", count("errno=54")),
        ("keepalive errno=60", count("errno=60")),
        ("id 미상 마감", count("id 미상 소켓 마감")),
        ("무프레임 판정(구 오프라인)", count("오프라인 판정", "무프레임 판정")),
    ]
    for name, n in rows:
        print(f"   {name:24} {n:>5}")

    # 기계 판독용 산출 — tick.py 가 이걸 읽어 1급 지표로 올린다(REQ-0112 루트 지시).
    #
    # ⚠ 분모는 **요청 구간**이 아니라 **자료가 실제로 있는 구간**이다.
    #    진행 중인 창을 `--until 23:59` 처럼 미래로 잡으면 아직 오지 않은 시간이
    #    분모에 들어가 시간당 값이 조용히 희석된다. "좋아 보이는" 쪽으로 틀리는
    #    종류라 특히 위험하다.
    data_end = max(t for t, _ in ev)
    eff_until = min(UNTIL, data_end)
    hours = (eff_until - SINCE).total_seconds() / 3600.0
    if eff_until < UNTIL:
        print()
        print(f"   ⓘ 분모 보정: 요청 끝 {UNTIL} → 자료 끝 {eff_until}"
              f" ({hours:.2f}h). 미래 구간을 분모에 넣지 않는다.")
    summary = {
        "since": SINCE.isoformat(), "until": UNTIL.isoformat(), "hours": round(hours, 3),
        "frames": len(frames), "accepts": len(accepts),
        "reconnect_no_reboot": len(alive), "reboots": booted, "undetermined": nodata,
        # 제외분을 산출물에도 남긴다 — 요약만 보는 사람에게도 보여야 한다.
        "excluded_server_startup": excluded_startup,
        "excluded_at": [t.isoformat() for t in excluded[:20]],
        "server_starts": len(starts),
        "same_conn_recover": len(same_conn), "reconn_recover": len(reconn),
        "reconnect_no_reboot_per_h": round(len(alive) / hours, 3) if hours else None,
        "same_conn_recover_per_h": round(len(same_conn) / hours, 3) if hours else None,
        "server_idle_reap": count("유휴 마감"), "errno54": count("errno=54"),
    }
    try:
        import json, os
        os.makedirs("monitor/out", exist_ok=True)
        with open("monitor/out/linkdrop-last.json", "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=1)
    except OSError:
        pass

    print()
    print("## 판정")
    n = len(alive)
    if n == 0:
        print("   재부팅 없는 재연결 0건 — 이 구간에는 두 번째 메커니즘의 증거가 없다.")
        print("   ⚠ 단 이 0 은 'ACCEPT 자체가 드물다'일 수도 있다. ACCEPT 총계를 함께 보라.")
    elif n == 1:
        print("   🟡 1건뿐이다. **기전을 붙이지 마라** — 표본 1로는 반복성을 말할 수 없다.")
    else:
        print(f"   🟢 {n}건 반복된다. 우연 1회가 아니라 **되풀이되는 양상**이다.")
        span = (alive[-1][0] - alive[0][0]).total_seconds() / 3600
        if span > 0:
            print(f"   {alive[0][0].strftime('%m-%d %H:%M')} ~ {alive[-1][0].strftime('%m-%d %H:%M')}"
                  f" ({span:.1f}h) 에 걸쳐 {n}건 = {n/span:.2f}건/h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
