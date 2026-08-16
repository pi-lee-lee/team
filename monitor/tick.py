#!/usr/bin/env python3
"""1시간 단위 스냅샷 러너 — 매 갱신마다 같은 인자를 다시 치지 않는다.

구간 정의·팀 리셋 목록을 여기 한 곳에 두고, 매 tick 마다:
  1) 전체 재집계 (monitor/out-report.md/.json)
  2) 세션 타임라인 (monitor/out-timeline.txt)
  3) 체크섬 드롭 원문 덤프 (monitor/out-drops.txt)
  4) 시리얼 캡처 생존 확인
  5) **한 줄 이력 추가** (monitor/out-history.tsv) — 추세를 매번 재계산하지 않고 본다
  6) 문맥에 올릴 **짧은 요약만** 표준출력

경계가 바뀌면(재플래싱 등) 아래 상수만 고친다.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from datetime import date, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

LOG = "/tmp/parking-soak.log"
SERIAL = "monitor/serial-esplink.log"
HISTORY = "monitor/out-history.tsv"

# ── 🔴 오염 경계 — 절대 지우지 마라 ──────────────────────────────────────
# 2026-08-16 15:26 이후 관측은 **장비 판정에 쓸 수 없다.**
# 사용자가 복구 작업 중 아두이노를 물리적으로 뽑았다 끼웠고(테스트 목적),
# 시리얼 포트를 이 도구(serial_tap)에 뺏겨 서버·탭을 여러 번 재기동했다.
# 그 구간의 재부팅·세션단절·프레임공백은 전부 **사람이 만든 것**이다.
# 출처: 사용자 확인 (REQ-0108). 고장이 아니므로 고장으로 집계하면 없는 것을 쫓게 된다.
CONTAM_SINCE = "2026-08-16T15:26:00"

# ── 경계 정의 (재플래싱하면 여기만 고친다) ───────────────────────────────
BASE_DATE = "2026-08-16"
WINDOWS = [
    "old=2026-08-15T18:42:39..2026-08-16T10:34:15",
    "old_late=2026-08-16T09:00:00..2026-08-16T10:34:15",
    "old_adj=2026-08-16T10:25:30..2026-08-16T10:34:15",
    # ⚠ 아래 셋은 원래 열린 창이었다. 오염 경계에서 **닫았다** —
    #    열어 두면 사용자의 실물 작업이 펌웨어 지표로 계상된다.
    "esplink_all=2026-08-16T10:34:15..2026-08-16T15:26:00",
    "esplink_noboot=2026-08-16T10:34:55..2026-08-16T15:26:00",
    "esplink_tap=2026-08-16T11:13:20..2026-08-16T15:26:00",
    # 확정 판정창(닫힘). 오염 경계보다 앞선다.
    # 마지막 1분이 경계에 걸치지만 15:26 으로 잘라도 **사건 계수가 전부 동일**하다
    # (세션종료2·errno54 2·드롭0·자발재부팅0·오프라인6·복구11.8s·최대공백52.0s).
    # 프레임만 1분치 54개 줄어든다 → 오염이 판정창에 들어오지 않았음이 계수로 확인됨.
    "judge=2026-08-16T11:27:00..2026-08-16T15:27:00",
    "judge_clean=2026-08-16T11:27:00..2026-08-16T15:26:00",
    # 🔴 오염 구간. 사람이 만든 것이므로 **장비 판정에 인용 금지.**
    "CONTAMINATED_사람개입=2026-08-16T15:26:00..",
]
BASELINE = "old"
# 이름 → (시작ISO, 끝ISO). link_drop_probe 에 넘기려면 창을 파싱해 둬야 한다.
WINDOWS_BY_NAME = {}
for _w in WINDOWS:
    _n, _, _r = _w.partition("=")
    _s, _, _e = _r.partition("..")
    WINDOWS_BY_NAME[_n] = (_s, _e or "2100-01-01T00:00:00")
TEAM_RESETS = [
    "2026-08-15T19:46:14=client.ino 재플래싱",
    "2026-08-15T19:56:09=client.ino 재플래싱",
    "2026-08-15T20:32:16=client.ino 재플래싱",
    "2026-08-16T10:34:15=EspLink 플래싱(REQ-0091)",
    "2026-08-16T11:13:17=시리얼 캡처 DTR 리셋(monitor-engineer)",
]
MARKS = [
    "2026-08-16T10:34:15=EspLink 플래싱 (펌웨어 경계)",
    "2026-08-16T11:13:17=시리얼 캡처 개시 DTR 리셋 (관측 경계)",
]
TRACK = "judge"           # 4시간 판정 대상 창 (확정·닫힘)
TARGET_H = 4.0

# 깨끗한 4시간 창을 시작하려면 이 시각 이후로 (a) 자발 MCU 재부팅 0 (b) 재플래싱 0 이어야 한다.
# 매번 경계를 옮기면 아무것도 못 재므로, **조건이 충족된 뒤에** 창을 잡는다.
CLEAN_SINCE = "2026-08-16T11:27:00"
CLEAN_HOLD_MIN = 30.0


def run(cmd: list[str]) -> str:
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(f"!! 실패: {' '.join(cmd[:3])}…\n{p.stderr[:800]}", file=sys.stderr)
    return p.stdout


def main() -> int:
    now = datetime.now()
    now_iso = now.isoformat(timespec="seconds")

    cmd = [sys.executable, "monitor/soak_stats.py", "--log", LOG,
           "--base-date", BASE_DATE, "--baseline", BASELINE,
           "--now", now_iso,
           "--out-md", "monitor/out-report.md",
           "--out-json", "monitor/out-report.json"]
    for w in WINDOWS:
        cmd += ["--win", w]
    for t in TEAM_RESETS:
        cmd += ["--team-reset", t]
    run(cmd)

    tl = [sys.executable, "monitor/session_timeline.py", "--log", LOG,
          "--base-date", BASE_DATE, "--since", "2026-08-15T18:42:00"]
    for m in MARKS:
        tl += ["--marks", m]
    with open("monitor/out-timeline.txt", "w", encoding="utf-8") as f:
        f.write(run(tl))

    with open("monitor/out-drops.txt", "w", encoding="utf-8") as f:
        f.write(run([sys.executable, "monitor/drop_dump.py", LOG, BASE_DATE]))

    ev = [sys.executable, "monitor/esp_events.py", "--log", LOG, "--base-date", BASE_DATE]
    for t in TEAM_RESETS:
        ev += ["--team-reset", t]
    esp_out = run(ev)
    with open("monitor/out-esp-events.txt", "w", encoding="utf-8") as f:
        f.write(esp_out)

    # 창 시작 조건 판정: CLEAN_SINCE 이후 자발 MCU 재부팅이 있었나
    cutoff = datetime.fromisoformat(CLEAN_SINCE)
    reboots_after = []
    for line in esp_out.splitlines():
        if "MCU재부팅" not in line or line.lstrip().startswith("#"):
            continue
        try:
            ts = datetime.fromisoformat(line[:19])
        except ValueError:
            continue
        # 오염 구간(사용자가 보드를 뽑았다 끼운 구간)의 재부팅은 장비 사건이 아니다.
        # 이걸 세면 "전원 사건이 안 끝났다" 경고가 영구히 떠서 곧 무시당한다.
        if ts >= cutoff and ts < datetime.fromisoformat(CONTAM_SINCE):
            reboots_after.append(ts)

    d = json.load(open("monitor/out-report.json", encoding="utf-8"))
    wins = {w["name"]: w for w in d["windows"]}
    tgt = wins.get(TRACK)
    base = wins[BASELINE]
    exp = d["expected"].get(TRACK, {})

    # 시리얼 생존
    ser_bytes = os.path.getsize(SERIAL) if os.path.exists(SERIAL) else 0
    ser_raw = SERIAL + ".raw"
    ser_raw_bytes = os.path.getsize(ser_raw) if os.path.exists(ser_raw) else 0
    ser_lines = 0
    ser_last = "-"
    if ser_bytes:
        with open(SERIAL, "rb") as f:
            data = f.read()
        lines = [l for l in data.decode("utf-8", "replace").splitlines() if l.strip()]
        ser_lines = len(lines)
        ser_last = lines[-1][:90] if lines else "-"
    tap_alive = subprocess.run(["pgrep", "-f", "serial_tap.py"],
                               capture_output=True, text=True).stdout.strip()

    # 이력 한 줄
    newfile = not os.path.exists(HISTORY)
    with open(HISTORY, "a", encoding="utf-8") as f:
        if newfile:
            f.write("tick\ttrack_h\tframes\tfpm\tclose\tclose_ph\te54\te54_ph\t"
                    "realS\tdrops\tupreg_spont\toffline\tser_raw_B\tser_lines\n")
        f.write("\t".join(str(x) for x in [
            now.strftime("%m-%d %H:%M"),
            f"{tgt['duration_h']:.2f}", tgt["frames"], tgt["frames_per_min"],
            tgt["sess_close"], tgt["sess_close_per_h"],
            tgt["errno54"],
            round(tgt["errno54"] / tgt["duration_h"], 2) if tgt["duration_h"] else 0,
            tgt["drop_real_S"], tgt["drops_total"],
            tgt["uptime_regressions_spont"], tgt["offline_events"],
            ser_raw_bytes, ser_lines,
        ]) + "\n")

    # ── 문맥에 올릴 짧은 요약 ──────────────────────────────────
    print("🔴 오염 경계 " + CONTAM_SINCE + " — 그 이후 관측은 사람이 보드를 뽑았다 끼운 결과다.")
    print("   `CONTAMINATED_사람개입` 창의 수치를 장비 판정에 쓰지 마라 (출처: 사용자 확인 REQ-0108).")
    print(f"== tick {now.strftime('%H:%M:%S')} · 판정창 `{TRACK}`")
    pct = 100.0 * tgt["duration_h"] / TARGET_H
    print(f"  진행 {tgt['duration_h']:.2f}h / {TARGET_H}h ({pct:.0f}%)  "
          f"남은 {max(0.0, TARGET_H - tgt['duration_h']):.2f}h")
    print(f"  프레임 {tgt['frames']:,} ({tgt['frames_per_min']}/분)   "
          f"기준 옛 {base['frames_per_min']}/분")

    # ── 🔑 1급 지표: 링크 끊김 (REQ-0112 루트 지시) ─────────────────────
    # 이 장비에서 실제로 움직이는 양은 MCU 재부팅이 아니라 **ESP 링크의 끊김·재접속**이다.
    # 기준선 13.92h 에서 새 연결 36건 중 35건이 "재부팅 없는 재연결"이었고
    # Uno 는 한 번도 안 죽었다 (monitor/FINDING-2026-08-16-link-vs-mcu.md).
    # 그래서 재부팅을 세는 자리에 이것을 놓는다.
    ld = None
    try:
        w = WINDOWS_BY_NAME.get(TRACK)
        if w:
            run([sys.executable, "monitor/link_drop_probe.py", LOG, w[0], w[1]])
            ld = json.load(open("monitor/out/linkdrop-last.json", encoding="utf-8"))
    except Exception as e:
        print(f"  (링크 지표 산출 실패: {type(e).__name__})")
    if ld:
        print(f"  🔑 재부팅없는 재연결 {ld['reconnect_no_reboot']} ({ld['reconnect_no_reboot_per_h']}/h)"
              f"   같은연결 복구 {ld['same_conn_recover']} ({ld['same_conn_recover_per_h']}/h)")
        print(f"     끊은 주체: 서버 유휴회수 {ld['server_idle_reap']} · TCP리셋(errno54) {ld['errno54']}"
              f"   ← 유휴회수 0 이면 서버가 끊은 게 아니다")
        # 제외분은 0 이어도 찍는다. 소리 없이 빠지면 나중에 창이 빈 이유를 못 찾는다.
        _ex = ld.get("excluded_server_startup", 0)
        _mark = "🔴" if _ex else "  "
        print(f"   {_mark} 서버기동 직후 제외 {_ex}건 (기동표지 {ld.get('server_starts', 0)}건)"
              + ("  ← 이 안에 진짜 끊김이 있었어도 같이 빠진다. linkdrop-last.json 의 excluded_at 확인"
                 if _ex else "  ← 제외 없음"))

    print(f"  세션종료 {tgt['sess_close']} ({tgt['sess_close_per_h']}/h)  "
          f"기준 옛 {base['sess_close_per_h']}/h  | 기대(옛비율) {exp.get('sess_close')}")
    print(f"  errno=54 {tgt['errno54']}  기대(옛비율) {exp.get('errno54')}")
    print(f"  바이트손상(real_S) {tgt['drop_real_S']}  기대 {exp.get('drop_real_S')}  "
          f"| 드롭전체 {tgt['drops_total']}")
    # 재부팅은 **0 이 정상**이라는 전제로 예외 감시만 한다(REQ-0112).
    # 기준선 13.92h 에서도 0 이었다 — 고칠 재부팅 문제는 애초에 없었다.
    _rb = tgt['uptime_regressions_spont']
    if _rb:
        print(f"  🔴 예외 — 자발 재부팅 {_rb}건 발생. 0 이 정상인 지표다. 개입 이력부터 확인하라")
    else:
        print(f"  재부팅 0 (정상) · 기대 {exp.get('uptime_regressions_spont')}"
              f"  ← 기준선도 0 이라 이 지표엔 판별력이 없다")
    print(f"  오프라인 {tgt['offline_events']} · 복구중앙 {tgt['recover_median_s']}s "
          f"· 최대공백 {tgt['max_frame_gap_s']}s")
    # ⚠ 0 의 의미를 가른다 — "안 쌓였다"와 "읽을 수 없다"는 완전히 다른 사건이다.
    ser_exists = os.path.exists(SERIAL)
    if not ser_exists and tap_alive:
        print(f"  시리얼: 🔴 **경로 소실(unlink)** — tap(pid {tap_alive.split()[0]})은 살아서 쓰는 중이나 "
              f"파일이 디렉터리에서 지워져 아무도 못 읽는다.")
        print(f"          → 이 0 은 '데이터 없음'이 아니라 '접근 불가'다. "
              f"프로세스를 죽이면 그 내용은 영구 소실된다.")
    elif not ser_exists:
        print(f"  시리얼: 🔴 파일 없음 · tap 죽음 — 캡처가 돌지 않는다(0 은 무의미).")
    else:
        print(f"  시리얼: raw {ser_raw_bytes}B · {ser_lines}줄 · "
              f"tap {'살아있음' if tap_alive else '🔴죽음'}")
        print(f"          마지막: {ser_last}")

    held = (now - cutoff).total_seconds() / 60.0
    if reboots_after:
        last_rb = max(reboots_after)
        print(f"  🔴 창시작 조건 미충족 — {CLEAN_SINCE} 이후 자발 MCU 재부팅 {len(reboots_after)}건 "
              f"(마지막 {last_rb}). 전원 사건이 안 끝났다.")
    elif held < CLEAN_HOLD_MIN:
        print(f"  ⏳ 창시작 조건 감시 중 — 자발 재부팅 0건, 유지 {held:.0f}분 "
              f"/ {CLEAN_HOLD_MIN:.0f}분 필요")
    else:
        print(f"  ✅ 창시작 조건 충족 — {CLEAN_SINCE} 이후 자발 재부팅 0건, {held:.0f}분 유지. "
              f"이 시각을 4시간 창 시작으로 확정할 수 있다.")
    for n in d["notes"]:
        # 이미 원문까지 확인해 규명된 경고는 그렇게 표시한다.
        # 규명된 경고를 매번 그대로 띄우면 사람이 경고 전체를 무시하게 된다.
        if "단독 K" in n:
            print("  ✔(규명됨) 단독 K 33건 — 전부 10:34:16~10:34:54(38초)에 몰려 있다."
                  " 플래싱 경계 직후 옛 빌드의 잔재이고 판정창에서는 0 이다.")
            continue
        print("  " + n)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
