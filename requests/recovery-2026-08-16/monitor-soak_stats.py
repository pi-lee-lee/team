#!/usr/bin/env python3
"""주차 소크 로그 구간 집계기 — monitor-engineer 전용 도구.

/tmp/parking-soak.log 는 HH:MM:SS 만 찍고 날짜가 없다. 파일이 자정을 여러 번 넘으므로
'시각이 뒤로 크게 점프하면 다음 날' 로 날짜를 복원한다. 마지막 줄을 기준일(--base-date)에
앵커링한다.

핵심 설계 의도 두 가지:

1) **체크섬 실패를 뭉뚱그리지 않는다.** 서버는 비프로토콜 텍스트도 `! 체크섬 불일치` 로
   집계한다(체크섬 검증이 타입 판별보다 먼저 돈다). 그래서 버려진 줄 **직전의 `←ARD` 원문**을
   붙잡아 세 갈래로 가른다:
     - lone_K   : 단독 'K' (EspLink keepalive 1바이트)
     - noise    : 비프로토콜 쓰레기(ESP 부트 ROM 등, 디코딩 실패 바이트 포함)
     - real_S   : 실제 S 프레임인데 체크섬이 틀림  ← 이것만이 '전선에서 바이트가 깨졌다'
   이 구분이 없으면 진단이 정반대인 두 사건이 같은 칸에 들어간다(REQ-0092 의 실제 사고).

2) **0 이 '건강'인지 '한 번도 안 돌았다'인지 갈라 적는다.**
   기준 구간(옛 펌웨어)의 발생률을 비교 구간 길이에 곱해 '옛 비율이면 몇 건이 나왔어야 하나'
   (기대값)를 같이 낸다. 기대값이 1 미만인 지표에서 0 은 아무것도 증명하지 않는다.

사용:
  python3 monitor/soak_stats.py \
      --win old=2026-08-15T18:42:39..2026-08-16T10:34:15 \
      --win new=2026-08-16T10:34:15.. \
      --win new_x=2026-08-16T10:34:55.. \
      --baseline old \
      --out-md monitor/out-report.md --out-json monitor/out-report.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, date, time as dtime, timedelta

TS_RE = re.compile(rb"^(\d{2}):(\d{2}):(\d{2})\s\s?")

RE_FRAME = re.compile(r"^←ARD (S,.*)$")
RE_ARD_IN = re.compile(r"^←ARD (.*)$")
RE_DROP = re.compile(r"^! 체크섬 불일치")
RE_RETX = re.compile(r"^↻ 재전송 (\d+)/(\d+)")
RE_ACKFAIL = re.compile(r"^! ACK 타임아웃 최종 실패")
RE_SESS_OPEN = re.compile(r"^\+ARD .*세션#(\d+)")
RE_SESS_CLOSE = re.compile(
    r"^-ARD 세션#(\d+) 종료\((.*)\) — 지속 (\d+):(\d+):(\d+) · 프레임 (\d+)"
)
RE_OFFLINE = re.compile(r"^! 아두이노 오프라인 판정\((\d+)ms")
RE_RECOVER = re.compile(r"^= 아두이노 온라인 복귀(?: — 복구 ([\d.]+)초\((.+)\))?")
RE_SRV_START = re.compile(r"^⏱ 소크 관측 시작")
RE_SRV_STOP = re.compile(r"^▣ 소크 종료")
RE_ORPHAN = re.compile(r"^✂ id 미상 소켓 마감")
RE_LFOVER = re.compile(r"^! LF 없이")
RE_TXOUT = re.compile(r"^→ARD ")

ERRNO_RE = re.compile(r"errno=(\d+)")


# ─────────────────────────────────────────────────────────── 파싱

class Event:
    __slots__ = ("ts", "kind", "data", "lineno", "raw")

    def __init__(self, ts, kind, data, lineno, raw=b""):
        self.ts = ts
        self.kind = kind
        self.data = data
        self.lineno = lineno
        self.raw = raw


def parse_log(path: str, base_date: date, rollback_threshold_s: int = 3600):
    """로그를 이벤트 열로 바꾼다. 날짜는 뒤에서 앞으로 앵커링한다."""
    events: list[Event] = []
    prev_secs = None
    day_idx = 0
    last_ard_raw = b""          # 직전 ←ARD 원문 바이트 (체크섬 드롭 귀속용)
    last_ard_txt = ""
    total_lines = 0
    untimed = 0

    with open(path, "rb") as f:
        for lineno, raw in enumerate(f, 1):
            total_lines += 1
            raw = raw.rstrip(b"\r\n")
            m = TS_RE.match(raw)
            if not m:
                untimed += 1
                continue
            hh, mm, ss = int(m.group(1)), int(m.group(2)), int(m.group(3))
            secs = hh * 3600 + mm * 60 + ss
            if prev_secs is not None and secs < prev_secs - rollback_threshold_s:
                day_idx += 1
            prev_secs = secs
            body_b = raw[m.end():]
            body = body_b.decode("utf-8", errors="replace")

            kind = None
            data = None

            fm = RE_FRAME.match(body)
            if fm:
                kind, data = "frame", parse_frame(fm.group(1))
                last_ard_raw, last_ard_txt = body_b, body
            elif RE_DROP.match(body):
                kind = "drop"
                data = {"prev_raw": last_ard_raw, "prev_txt": last_ard_txt}
            else:
                am = RE_ARD_IN.match(body)
                if am:
                    kind, data = "ard_in", {"payload": am.group(1)}
                    last_ard_raw, last_ard_txt = body_b, body
                elif RE_RETX.match(body):
                    kind, data = "retx", {}
                elif RE_ACKFAIL.match(body):
                    kind, data = "ackfail", {}
                elif RE_SESS_CLOSE.match(body):
                    cm = RE_SESS_CLOSE.match(body)
                    reason = cm.group(2)
                    dur = int(cm.group(3)) * 3600 + int(cm.group(4)) * 60 + int(cm.group(5))
                    en = ERRNO_RE.search(reason)
                    kind = "sess_close"
                    data = {
                        "n": int(cm.group(1)),
                        "reason": normalize_reason(reason),
                        "reason_raw": reason,
                        "errno": int(en.group(1)) if en else None,
                        "dur_s": dur,
                        "frames": int(cm.group(6)),
                    }
                elif RE_SESS_OPEN.match(body):
                    kind = "sess_open"
                    data = {"n": int(RE_SESS_OPEN.match(body).group(1))}
                elif RE_OFFLINE.match(body):
                    kind, data = "offline", {"ms": int(RE_OFFLINE.match(body).group(1))}
                elif RE_RECOVER.match(body):
                    rm = RE_RECOVER.match(body)
                    kind = "recover"
                    data = {
                        "secs": float(rm.group(1)) if rm.group(1) else None,
                        "how": rm.group(2),
                    }
                elif RE_SRV_START.match(body):
                    kind, data = "srv_start", {}
                elif RE_SRV_STOP.match(body):
                    kind, data = "srv_stop", {}
                elif RE_ORPHAN.match(body):
                    kind, data = "orphan_cut", {}
                elif RE_LFOVER.match(body):
                    kind, data = "lf_overflow", {}
                elif RE_TXOUT.match(body):
                    kind, data = "tx", {}

            if kind:
                events.append(Event((day_idx, secs), kind, data, lineno, body_b))

    # 날짜 앵커링: 마지막 날짜 인덱스를 base_date 로
    max_day = day_idx
    for ev in events:
        d, secs = ev.ts
        the_date = base_date - timedelta(days=(max_day - d))
        ev.ts = datetime.combine(the_date, dtime()) + timedelta(seconds=secs)

    return events, {"total_lines": total_lines, "untimed": untimed, "days": max_day + 1}


def parse_frame(payload: str):
    """S,seq,occ,res,uptime,dev,cksum"""
    parts = payload.split(",")
    out = {"seq": None, "uptime": None, "dev": None}
    try:
        out["seq"] = int(parts[1])
        out["uptime"] = int(parts[4])
        out["dev"] = parts[5]
    except (IndexError, ValueError):
        pass
    return out


def normalize_reason(reason: str) -> str:
    if "errno=54" in reason:
        return "수신오류 errno=54 (ECONNRESET)"
    if "errno=60" in reason:
        return "keepalive 시간초과 errno=60"
    if "재접속으로 대체" in reason:
        return "같은 device_id 재접속으로 대체"
    if "새 연결로 대체" in reason:
        return "새 연결로 대체"
    if "상대가 닫음" in reason:
        return "상대가 닫음"
    if "서버 종료" in reason:
        return "서버 종료"
    if "수신 오류" in reason:
        return "수신오류 (errno 미상)"
    return reason


# ─────────────────────────────────────────────────────────── 드롭 분류

def classify_drop(prev_raw: bytes, prev_txt: str) -> str:
    """버려진 줄 직전의 ←ARD 원문으로 드롭의 성격을 가른다."""
    if not prev_txt:
        return "unknown"
    payload = prev_txt[len("←ARD "):] if prev_txt.startswith("←ARD ") else prev_txt
    p = payload.strip()
    if p == "K":
        return "lone_K"
    if p.startswith("S,"):
        return "real_S"          # 진짜 S 프레임인데 체크섬 틀림 = 바이트 손상
    if "�" in payload or any(b < 0x20 or b > 0x7E for b in prev_raw[len("←ARD ".encode()):]):
        return "noise"           # 비ASCII/제어문자 = 부트 ROM 잡음 등
    return "other_text"


# ─────────────────────────────────────────────────────────── 구간 집계

class Window:
    def __init__(self, name, start, end, exclude=None):
        self.name = name
        self.start = start
        self.end = end
        self.exclude = exclude or []   # [(s,e)] 제외 구간

    def contains(self, ts):
        if ts < self.start:
            return False
        if self.end and ts >= self.end:
            return False
        for s, e in self.exclude:
            if s <= ts < e:
                return False
        return True

    def duration_s(self, last_ts):
        end = self.end or last_ts
        d = (end - self.start).total_seconds()
        for s, e in self.exclude:
            lo, hi = max(s, self.start), min(e, end)
            if hi > lo:
                d -= (hi - lo).total_seconds()
        return max(d, 0.0)


def aggregate(events, win: Window, last_ts):
    ev = [e for e in events if win.contains(e.ts)]
    dur_s = win.duration_s(last_ts)
    dur_h = dur_s / 3600.0

    frames = [e for e in ev if e.kind == "frame"]
    drops = [e for e in ev if e.kind == "drop"]
    drop_kinds: dict[str, int] = {}
    for d in drops:
        k = classify_drop(d.data["prev_raw"], d.data["prev_txt"])
        drop_kinds[k] = drop_kinds.get(k, 0) + 1

    closes = [e for e in ev if e.kind == "sess_close"]
    reasons: dict[str, int] = {}
    for c in closes:
        reasons[c.data["reason"]] = reasons.get(c.data["reason"], 0) + 1

    # uptime 역행 (= MCU 재부팅 후보; 우리가 만든 리셋과 구별 못 함)
    regressions = []
    prev_up = None
    prev_ts = None
    for f in frames:
        up = f.data["uptime"]
        if up is None:
            continue
        if prev_up is not None and up < prev_up:
            regressions.append({"at": f.ts.isoformat(sep=" "), "from": prev_up, "to": up})
        prev_up = up
        prev_ts = f.ts

    # 프레임 간 최대 공백 + **무전송 누적 시간**
    #   열린 창은 죽은 시간을 조용히 빨아들인다. 그러면 프레임/분이 서서히 떨어져
    #   회귀처럼 보이는데 실제로는 '창이 안 닫힌 인공물'이다(esplink_noboot 에서 실제로 겪었다).
    #   그래서 '벽시계 길이'와 '프레임이 실제로 흐른 길이'를 갈라 적는다.
    MUTE = 120.0
    max_gap = 0.0
    max_gap_at = None
    mute_s = 0.0
    mute_episodes = []
    pts = None
    for f in frames:
        if pts is not None:
            g = (f.ts - pts).total_seconds()
            if g > max_gap:
                max_gap, max_gap_at = g, f.ts.isoformat(sep=" ")
            if g > MUTE:
                mute_s += g
                mute_episodes.append({"from": pts.isoformat(sep=" "),
                                      "to": f.ts.isoformat(sep=" "), "sec": round(g, 1)})
        pts = f.ts
    # 창 끝까지 프레임이 없는 꼬리도 무전송이다
    if frames:
        tail = ((win.end or last_ts) - frames[-1].ts).total_seconds()
        if tail > MUTE:
            mute_s += tail
            mute_episodes.append({"from": frames[-1].ts.isoformat(sep=" "),
                                  "to": (win.end or last_ts).isoformat(sep=" "),
                                  "sec": round(tail, 1), "진행중": True})

    recovers = [e.data["secs"] for e in ev if e.kind == "recover" and e.data["secs"] is not None]
    recovers.sort()

    n_frames = len(frames)
    return {
        "name": win.name,
        "start": win.start.isoformat(sep=" "),
        "end": (win.end or last_ts).isoformat(sep=" "),
        "excluded": [[s.isoformat(sep=" "), e.isoformat(sep=" ")] for s, e in win.exclude],
        "duration_h": round(dur_h, 3),
        "frames": n_frames,
        "frames_per_min": round(n_frames / (dur_s / 60.0), 2) if dur_s > 0 else None,
        "drops_total": len(drops),
        "drops_by_kind": drop_kinds,
        "drop_real_S": drop_kinds.get("real_S", 0),
        "drop_real_S_pct": round(100.0 * drop_kinds.get("real_S", 0) / n_frames, 4) if n_frames else None,
        "drops_total_pct": round(100.0 * len(drops) / n_frames, 4) if n_frames else None,
        "retx": sum(1 for e in ev if e.kind == "retx"),
        "ack_fail": sum(1 for e in ev if e.kind == "ackfail"),
        "sess_open": sum(1 for e in ev if e.kind == "sess_open"),
        "sess_close": len(closes),
        "sess_close_per_h": round(len(closes) / dur_h, 3) if dur_h > 0 else None,
        "close_reasons": reasons,
        "errno54": reasons.get("수신오류 errno=54 (ECONNRESET)", 0),
        "sess_dur_median_s": median([c.data["dur_s"] for c in closes]),
        "uptime_regressions": len(regressions),
        "uptime_regressions_per_h": round(len(regressions) / dur_h, 3) if dur_h > 0 else None,
        "uptime_regression_times": [r["at"] for r in regressions],
        "uptime_regressions_team": 0,      # 아래 mark_team_resets() 가 채운다
        "uptime_regressions_spont": len(regressions),
        "uptime_regression_marks": [],
        "offline_events": sum(1 for e in ev if e.kind == "offline"),
        "recover_n": len(recovers),
        "recover_median_s": median(recovers),
        "recover_max_s": max(recovers) if recovers else None,
        "max_frame_gap_s": round(max_gap, 1),
        "max_frame_gap_at": max_gap_at,
        # 벽시계 길이 vs 프레임이 실제로 흐른 길이 — 둘을 갈라야 '조용해서 0' 을 안 놓친다
        "mute_s": round(mute_s, 1),
        "mute_h": round(mute_s / 3600.0, 3),
        "live_h": round(max(0.0, dur_s - mute_s) / 3600.0, 3),
        "mute_pct": round(100.0 * mute_s / dur_s, 2) if dur_s > 0 else None,
        "mute_episodes": mute_episodes,
        "orphan_cut": sum(1 for e in ev if e.kind == "orphan_cut"),
        "lf_overflow": sum(1 for e in ev if e.kind == "lf_overflow"),
        "srv_start_inside": sum(1 for e in ev if e.kind == "srv_start"),
        "srv_stop_inside": sum(1 for e in ev if e.kind == "srv_stop"),
        "lone_K_inside": drop_kinds.get("lone_K", 0),
        "last_frame_at": frames[-1].ts.isoformat(sep=" ") if frames else None,
    }


def mark_team_resets(stats, team_resets, tol_s=90):
    """`uptime 역행` 중 **우리가 만든 리셋**(플래싱·시리얼 DTR)을 표시해 자발 재부팅과 가른다.

    REQ-0092 §② 의 교훈: 이 지표는 '장치가 스스로 재시작했다'를 재지 못한다. 팀 활동 시각과
    대조한 뒤에만 인용할 수 있다. `7 → 0` 을 그냥 표에 넣으면 **오염된 수를 깨끗한 수와 비교**하는
    것이 되어 있지도 않은 7배 개선으로 읽힌다.
    """
    for s in stats:
        marks = []
        team = 0
        for t in s["uptime_regression_times"]:
            ts = datetime.fromisoformat(t)
            hit = None
            for rt, label in team_resets:
                if abs((ts - rt).total_seconds()) <= tol_s:
                    hit = label
                    break
            if hit:
                team += 1
                marks.append({"at": t, "cause": hit, "team": True})
            else:
                marks.append({"at": t, "cause": "불명(자발 후보)", "team": False})
        s["uptime_regressions_team"] = team
        s["uptime_regressions_spont"] = len(marks) - team
        s["uptime_regression_marks"] = marks


def median(xs):
    if not xs:
        return None
    s = sorted(xs)
    n = len(s)
    return round(s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0, 2)


# ─────────────────────────────────────────────────────────── 기대값(표본 해석용)

EXPECT_KEYS = [
    ("frames", "frames_per_min", 60.0),          # per hour = per_min*60
    ("sess_close", "sess_close_per_h", 1.0),
    ("uptime_regressions", "uptime_regressions_per_h", 1.0),
]


def expectations(baseline: dict, target: dict) -> dict:
    """옛 구간 발생률을 새 구간 길이에 곱한 기대값. 0 의 의미를 가르는 장치."""
    h = target["duration_h"]
    out = {}
    if baseline["frames_per_min"]:
        out["frames"] = round(baseline["frames_per_min"] * h * 60, 1)
    if baseline["sess_close_per_h"] is not None:
        out["sess_close"] = round(baseline["sess_close_per_h"] * h, 2)
    if baseline["duration_h"]:
        out["errno54"] = round(baseline["errno54"] / baseline["duration_h"] * h, 2)
        out["retx"] = round(baseline["retx"] / baseline["duration_h"] * h, 2)
        out["ack_fail"] = round(baseline["ack_fail"] / baseline["duration_h"] * h, 2)
        out["offline_events"] = round(baseline["offline_events"] / baseline["duration_h"] * h, 2)
    if baseline["uptime_regressions_per_h"] is not None:
        out["uptime_regressions"] = round(baseline["uptime_regressions_per_h"] * h, 2)
    if baseline["duration_h"]:
        # 팀이 만든 리셋을 걷어낸 자발 발생률로 본 기대값 — 이쪽이 정직한 비교다
        out["uptime_regressions_spont"] = round(
            baseline["uptime_regressions_spont"] / baseline["duration_h"] * h, 2)
    if baseline["frames"] and baseline["drop_real_S"] is not None and target["frames"]:
        rate = baseline["drop_real_S"] / baseline["frames"]
        out["drop_real_S"] = round(rate * target["frames"], 2)
    if baseline["frames"] and target["frames"]:
        rate_all = baseline["drops_total"] / baseline["frames"]
        out["drops_total"] = round(rate_all * target["frames"], 2)
    return out


# ─────────────────────────────────────────────────────────── 건전성 검사

def sanity(events, wins_stats, last_ts, now: datetime | None):
    notes = []
    new_like = [w for w in wins_stats if w["name"] != "old"]
    for w in new_like:
        if w["srv_start_inside"]:
            notes.append(f"🔴 {w['name']} 구간 안에 서버 재기동(⏱ 소크 관측 시작) {w['srv_start_inside']}회 — 경계가 움직였다. 구간 정의를 다시 잡아야 한다.")
        if w["srv_stop_inside"]:
            notes.append(f"🔴 {w['name']} 구간 안에 서버 종료(▣) {w['srv_stop_inside']}회.")
        if w["lone_K_inside"]:
            notes.append(f"🔴 {w['name']} 구간에 단독 K {w['lone_K_inside']}건 — keepalive 1바이트를 보내는 빌드가 붙어 있다는 뜻(현재 빌드에는 없어야 한다).")
    if now is not None:
        stale = (now - last_ts).total_seconds()
        if stale > 180:
            notes.append(f"🔴 로그 마지막 줄이 {stale:.0f}초 전 — 서버가 죽었거나 로그가 멈췄다.")
        else:
            notes.append(f"✔ 서버 로그가 살아 있다(마지막 줄 {stale:.0f}초 전).")

        # ⚠ 로그가 살아 있는 것과 **표본이 늘고 있는 것**은 다르다.
        #   서버는 프레임이 하나도 안 와도 60초마다 소크 요약을 계속 찍는다.
        #   그래서 '로그 최신성' 만 보면 장치가 죽어도 건강해 보인다 — 실제로 이 구멍에 빠졌다.
        frames = [e for e in events if e.kind == "frame"]
        if frames:
            fstale = (now - frames[-1].ts).total_seconds()
            if fstale > 120:
                notes.append(
                    f"🔴🔴 **마지막 S 프레임이 {fstale:.0f}초 전({frames[-1].ts})** — 장치가 붙어 있지 않다. "
                    f"이 구간의 통계는 '조용해서 0' 이지 '건강해서 0' 이 아니다.")
            else:
                notes.append(f"✔ 프레임이 들어오고 있다(마지막 {fstale:.0f}초 전) — 표본이 늘고 있다.")
    return notes


# ─────────────────────────────────────────────────────────── 출력

def fmt_md(stats, expect_map, meta, notes, baseline_name):
    L = []
    L.append("# 소크 로그 구간 집계 — monitor-engineer")
    L.append("")
    L.append(f"- 로그: `{meta['log']}` · 총 {meta['total_lines']:,}줄 · 날짜 복원 {meta['days']}일치")
    L.append(f"- 집계 시각: {meta['generated']}")
    L.append(f"- 기준선(baseline): `{baseline_name}`")
    L.append("")
    for n in notes:
        L.append(f"- {n}")
    L.append("")

    names = [s["name"] for s in stats]
    L.append("## 구간 정의")
    L.append("")
    L.append("| 구간 | 시작 | 끝 | 제외 | 길이(시간) |")
    L.append("|---|---|---|---|---|")
    for s in stats:
        exc = ", ".join(f"{a}~{b}" for a, b in s["excluded"]) or "-"
        L.append(f"| `{s['name']}` | {s['start']} | {s['end']} | {exc} | {s['duration_h']:.2f} |")
    L.append("")

    rows = [
        ("구간 길이(시간)", "duration_h", None),
        ("S 프레임", "frames", "frames"),
        ("프레임/분", "frames_per_min", None),
        ("**바이트손상 체크섬실패(real_S)**", "drop_real_S", "drop_real_S"),
        ("체크섬실패 전체(비프로토콜 포함)", "drops_total", "drops_total"),
        ("  └ 단독 K", "lone_K_inside", None),
        ("재전송", "retx", "retx"),
        ("ACK 최종실패", "ack_fail", "ack_fail"),
        ("세션 종료", "sess_close", "sess_close"),
        ("세션 종료/시간", "sess_close_per_h", None),
        ("  └ errno=54", "errno54", "errno54"),
        ("세션 지속 중앙(초)", "sess_dur_median_s", None),
        ("uptime 역행(원수)", "uptime_regressions", "uptime_regressions"),
        ("  └ 팀이 만든 리셋", "uptime_regressions_team", None),
        ("  └ **자발 후보**", "uptime_regressions_spont", "uptime_regressions_spont"),
        ("오프라인 판정", "offline_events", "offline_events"),
        ("복구 건수", "recover_n", None),
        ("복구 중앙(초)", "recover_median_s", None),
        ("복구 최악(초)", "recover_max_s", None),
        ("최대 프레임 공백(초)", "max_frame_gap_s", None),
    ]

    hdr = "| 지표 | " + " | ".join(f"`{n}`" for n in names) + " |"
    L.append("## 대조표")
    L.append("")
    L.append(hdr)
    L.append("|" + "---|" * (len(names) + 1))
    for label, key, _ in rows:
        cells = []
        for s in stats:
            v = s.get(key)
            cells.append("-" if v is None else (f"{v:,}" if isinstance(v, int) else f"{v}"))
        L.append(f"| {label} | " + " | ".join(cells) + " |")
    L.append("")

    L.append("## 0 의 해석 — 옛 펌웨어 발생률이면 이 구간에서 몇 건이 나왔어야 하나")
    L.append("")
    L.append("기대값이 1 미만인 지표에서 `0` 은 **아무것도 증명하지 않는다.**")
    L.append("")
    for name, exp in expect_map.items():
        st = next(s for s in stats if s["name"] == name)
        L.append(f"### `{name}` (길이 {st['duration_h']:.2f}시간)")
        L.append("")
        L.append("| 지표 | 실측 | 옛 비율 기대값 | 판정 가능? |")
        L.append("|---|---|---|---|")
        for label, key, ekey in rows:
            if not ekey or ekey not in exp:
                continue
            obs = st.get(key)
            e = exp[ekey]
            verdict = "판정 가능" if e >= 3 else ("약함" if e >= 1 else "**표본 부족 — 0 이어도 무의미**")
            L.append(f"| {label} | {obs} | {e} | {verdict} |")
        L.append("")

    L.append("## 세션 종료 사유 분포")
    L.append("")
    all_reasons = []
    for s in stats:
        for r in s["close_reasons"]:
            if r not in all_reasons:
                all_reasons.append(r)
    L.append("| 사유 | " + " | ".join(f"`{n}` (n={next(s for s in stats if s['name']==n)['sess_close']})" for n in names) + " |")
    L.append("|" + "---|" * (len(names) + 1))
    for r in all_reasons:
        cells = []
        for s in stats:
            c = s["close_reasons"].get(r, 0)
            tot = s["sess_close"]
            cells.append(f"{c} ({100.0*c/tot:.0f}%)" if tot else f"{c}")
        L.append(f"| {r} | " + " | ".join(cells) + " |")
    L.append("")

    L.append("## uptime 역행 — 팀이 만든 리셋을 걷어낸 뒤에만 인용할 수 있다")
    L.append("")
    L.append("> `uptime 역행` 은 **장치가 스스로 재시작했다**를 재는 지표가 아니다. 시리얼 포트를")
    L.append("> 여는 것만으로 Uno 가 DTR 리셋되므로 **우리가 만든 리셋과 장치가 죽은 것을 구별하지 못한다**(REQ-0092 §②).")
    L.append("> 그래서 원수(raw)를 그대로 대조하면 **오염된 수와 깨끗한 수를 비교**하게 된다.")
    L.append("")
    L.append("| 구간 | 원수 | 팀이 만든 것 | 자발 후보 | 자발/시간 |")
    L.append("|---|---|---|---|---|")
    for s in stats:
        sp = s["uptime_regressions_spont"]
        rate = f"{sp / s['duration_h']:.3f}" if s["duration_h"] else "-"
        L.append(f"| `{s['name']}` | {s['uptime_regressions']} | {s['uptime_regressions_team']} | {sp} | {rate} |")
    L.append("")
    for s in stats:
        L.append(f"**`{s['name']}` 상세**")
        if not s["uptime_regression_marks"]:
            L.append("- 없음")
        for m in s["uptime_regression_marks"]:
            tag = "🛠 팀" if m["team"] else "❓ 자발 후보"
            L.append(f"- {m['at']} — {tag} · {m['cause']}")
        L.append("")
    return "\n".join(L)


def parse_win_arg(a: str):
    name, rng = a.split("=", 1)
    s, _, e = rng.partition("..")
    start = datetime.fromisoformat(s)
    end = datetime.fromisoformat(e) if e else None
    return name, start, end


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default="/tmp/parking-soak.log")
    ap.add_argument("--base-date", default="2026-08-16")
    ap.add_argument("--win", action="append", required=True,
                    help="name=ISO..ISO (끝 생략 가능)")
    ap.add_argument("--exclude", action="append", default=[],
                    help="name=ISO..ISO — 해당 구간에서 뺄 창")
    ap.add_argument("--baseline", default="old")
    ap.add_argument("--team-reset", action="append", default=[],
                    help="ISO=설명 — 팀이 만든 리셋(플래싱·시리얼 DTR) 시각")
    ap.add_argument("--now", default=None, help="ISO. 로그 신선도 검사용")
    ap.add_argument("--out-md", default="monitor/out-report.md")
    ap.add_argument("--out-json", default="monitor/out-report.json")
    args = ap.parse_args()

    base = date.fromisoformat(args.base_date)
    events, meta = parse_log(args.log, base)
    if not events:
        print("이벤트 없음", file=sys.stderr)
        return 1
    last_ts = max(e.ts for e in events)

    excludes: dict[str, list] = {}
    for a in args.exclude:
        n, s, e = parse_win_arg(a)
        excludes.setdefault(n, []).append((s, e))

    wins = []
    for a in args.win:
        n, s, e = parse_win_arg(a)
        wins.append(Window(n, s, e, excludes.get(n)))

    stats = [aggregate(events, w, last_ts) for w in wins]

    team_resets = []
    for tr in args.team_reset:
        iso, _, label = tr.partition("=")
        team_resets.append((datetime.fromisoformat(iso), label or "팀 활동"))
    mark_team_resets(stats, team_resets)

    baseline = next((s for s in stats if s["name"] == args.baseline), None)
    expect_map = {}
    if baseline:
        for s in stats:
            if s["name"] != args.baseline:
                expect_map[s["name"]] = expectations(baseline, s)

    now = datetime.fromisoformat(args.now) if args.now else None
    notes = sanity(events, stats, last_ts, now)

    meta_out = {
        "log": args.log,
        "total_lines": meta["total_lines"],
        "untimed": meta["untimed"],
        "days": meta["days"],
        "last_log_ts": last_ts.isoformat(sep=" "),
        "generated": args.now or "(--now 미지정)",
    }

    md = fmt_md(stats, expect_map, meta_out, notes, args.baseline)
    with open(args.out_md, "w", encoding="utf-8") as f:
        f.write(md)
    with open(args.out_json, "w", encoding="utf-8") as f:
        json.dump({"meta": meta_out, "windows": stats, "expected": expect_map,
                   "notes": notes}, f, ensure_ascii=False, indent=2)

    print(f"→ {args.out_md}")
    print(f"→ {args.out_json}")
    for s in stats:
        print(f"  {s['name']:8s} {s['duration_h']:6.2f}h  frames={s['frames']:6d} "
              f"({s['frames_per_min']}/min)  realS_drop={s['drop_real_S']} "
              f"drops={s['drops_total']}  close={s['sess_close']} e54={s['errno54']} "
              f"upreg={s['uptime_regressions']}")
    for n in notes:
        print("  " + n)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
