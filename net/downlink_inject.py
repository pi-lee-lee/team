#!/usr/bin/env python3
"""하행 주입기 — 관측 창 동안 하행 명령을 **일정 주기로, 결정적으로, 기록을 남기며** 넣는다.

왜 존재하나
-----------
A 창(3.20h)에서 하행 명령이 **1건**뿐이었고 그것도 서버 기동 유예(90초) 안이었다.
→ **하행 도달률은 "건강"이 아니라 "측정 안 됨"(D3)이었다.**
하행은 사람이 트리거해야 생긴다 — 가만히 두면 8시간 뒤에도 0 이다(원장 §5.2).

🔑 왜 `T,S`(오버라이드 설정)를 쓰나 — **상태를 안 바꾸는 유일한 하행이라서**
------------------------------------------------------------------------
`R`/`C`(예약/취소)를 주기적으로 쏘면 **주차 상태가 바뀌고**, 장치가 즉시 S 프레임을
추가로 보내며, `slots[]`·`reserved` 마스크가 움직인다. 기준선을 재는 창에서
**측정 도구가 측정 대상을 흔드는 것**이다.

**무장하지 않은 상태의 `T,S` 는 장치가 `result=4` 로 거절하고 아무 상태도 안 바꾼다**
(원장 §7.6 — `DEV_ACK` 격리 검증에서 실측으로 확인된 성질이다).
그런데도 ①서버 송신 ②장치 수신 ③서버 수신(ACK) 세 관측점을 **전부** 만든다.
→ 하행 경로를 재면서 도메인 상태를 안 건드린다. 그래서 이것을 고른다.

⚠ **무장(`T,A`)을 절대 보내지 마라.** 무장하면 그 뒤 `T,S` 가 실제로 상태를 바꾼다.
   이 스크립트는 무장 명령을 **아예 만들지 않는다** — 옵션으로도 없다.

관측 계약 (monitor RECIPE-newbaseline-t0.md §8)
-----------------------------------------------
monitor 는 seq 로 셋을 짝짓는다:
  ① `→ARD T,<seq>,S,<slot>,1,<ck>`        parking-server.log
  ② `[AT] "+IPD,<n>:T,<seq>,S,…"`          serial 로그
  ③ `←ARD A,<seq>,<slot>,4,<ck>`           parking-server.log   ← result=4 가 정상이다
D0 도달 / D1 ②는 있고 ③ 없음 / D2 ①만 / D3 ①이 0건(미실행).
⚠ seq 는 **인스턴스마다 1 부터 다시 시작한다.** 인스턴스 경계를 넘어 짝짓지 마라.

사용
----
  python3 net/downlink_inject.py --port 10000 --interval 300 --slot A1
  운영에 넣을 때는 --allow-production 을 명시한다(그것이 이 도구의 목적이다).
"""
import argparse
import json
import os
import socket
import sys
import time

# ⚠ **핸드셰이크·프레이밍을 다시 구현하지 않는다.** 같은 것을 두 곳에 두면 갈라진다 —
#    이 저장소가 이미 그 함정을 여러 번 밟았다(원장 §5.5, drop 계수기·분류기).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ws_probe import handshake, frames, send_text          # noqa: E402

PRODUCTION_PORTS = (9900, 9991, 5500)


def stamp():
    """🔴 날짜를 반드시 붙인다 — 원장 §1.1.

    `HH:MM:SS` 만 찍으면 읽는 사람이 **반드시** 오독한다. 관측자는 로그를 잘라 읽으므로
    "부분 문자열만 봐도 절대시각이 확정되는 것"이 이 형식의 전부다.
    """
    return time.strftime("%Y-%m-%d %H:%M:%S")


class Injector:
    def __init__(self, a):
        self.a = a
        self.sock = None
        self.buf = bytearray()
        self.n_sent = 0
        self.n_ack = 0
        self.n_ack_other = 0
        self.n_not_injected = 0
        self.n_timeout = 0
        self.n_reconnect = 0
        self.n_error = 0
        # 큐 도입(REQ-0158) 뒤 생긴 **중간 상태**. 결말이 아니므로 다른 칸과 합치지 않는다 —
        # 합치면 "아직 전선에 안 나간 것"이 성공이나 실패로 세어진다.
        self.n_queued = 0
        self.streak_offline = 0     # `device_offline` 연속 — `check_stop` 만 갱신한다
        self.stopped_reason = None  # 중단 조건으로 멈췄으면 그 이유. None = 정상 종료

    def log(self, mark, msg):
        line = "%s  %s %s" % (stamp(), mark, msg)
        print(line, flush=True)
        if self.a.log:
            try:
                with open(self.a.log, "a", encoding="utf-8") as f:
                    f.write(line + "\n")
            except OSError as e:
                # 로그를 못 써도 주입은 계속한다. 다만 **조용히 넘어가지 않는다** —
                # "기록이 남는다"가 이 도구의 존재 이유의 절반이다.
                print("%s  ! 로그 기록 실패: %s" % (stamp(), e), flush=True)

    def connect(self):
        self.sock = socket.create_connection((self.a.host, self.a.port), timeout=10)
        self.buf = bytearray(handshake(self.sock, self.a.host, self.a.port))
        self.log("+", "WS 연결 — %s:%d" % (self.a.host, self.a.port))

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def inject(self, rid):
        """한 번 주입하고 응답을 기다린다.

        반환: 'ack'(result=4, 정상) | 'ack_other' | 'not_injected' | 'timeout'

        🔴 **`not_injected` 를 `timeout` 과 반드시 갈라야 한다.**
        장치가 오프라인이면 서버는 ACK 가 아니라 **`{"type":"error","code":"device_offline"}`**
        를 돌려주고 **전선에는 아무것도 안 내보낸다**(server.cpp:1799·1810·1851 → `send_err`).
        이것을 응답 없음으로 흘리면 30초 뒤 timeout 이 되고, monitor 는 그것을
        **D1/D2(장치가 못 받았다)로 센다** — 그러나 실제로는 **①이 아예 없었다.**
        → **없는 하행 고장을 만들어 내는 자리다.** 그래서 별도 칸으로 센다.
        """
        req = {"type": "test_set", "slot": self.a.slot, "occupied": "1", "rid": rid}
        send_text(self.sock, req)
        self.n_sent += 1
        self.log("→", "주입 %s slot=%s (무장 안 함 → result=4 기대)" % (rid, self.a.slot))

        # ⚠ 마감은 monitor 의 짝짓기 시한과 같은 30초로 둔다(RECIPE §8.3).
        #    여기서 더 짧게 잡으면 내 기록과 monitor 판정이 어긋난다.
        deadline = time.time() + self.a.ack_timeout
        for op, body, _desc, _m in frames(self.sock, self.buf, deadline):
            if op == 0x8:
                raise ConnectionError("서버가 close 를 보냈다")
            try:
                o = json.loads(body.decode("utf-8", "replace"))
            except ValueError:
                continue
            if o.get("rid") != rid:
                continue                      # 다른 요청의 응답 — 흘린다

            # ── 서버가 error 를 준 경우
            #
            # 🔴 **error 라고 다 "안 나갔다"가 아니다. 코드마다 정반대다.**
            #   · device_offline → 서버가 **전선에 안 내보냈다** → 미주입(분모에서 뺀다)
            #   · ack_timeout   → 서버가 **3회 재전송까지 하고** ACK 를 못 받았다
            #                     (server.cpp:1500, 재전송 경로 뒤) → **진짜 하행 실패**다.
            #                     **분모에 넣고 D1/D2 후보로 센다.**
            # 이 둘을 합치면 **가장 중요한 실패를 "시도 안 함"으로 지워 버린다.**
            if o.get("type") == "error":
                code = o.get("code", "?")
                if code == "device_offline":
                    self.n_not_injected += 1
                    self.log("=", "미주입 %s — 장치 오프라인(서버가 전선에 안 내보냈다). "
                                  "🔴 이것은 하행 실패가 **아니다**. D1/D2 로 세지 마라" % rid)
                    return "not_injected"
                if code == "ack_timeout":
                    self.n_timeout += 1
                    self.log("!", "🔴 하행 실패 %s — ACK 타임아웃. **전선에는 나갔다"
                                  "(서버가 3회 재전송했다)**. D1/D2 후보 — 분모에 넣어라. "
                                  "②(시리얼 +IPD)가 있어야 D1 과 D2 가 갈린다" % rid)
                    return "timeout"
                # 모르는 코드는 **어느 칸으로도 밀지 않는다** — 합치면 그 순간 증거가 사라진다
                self.n_error += 1
                self.log("!", "🔴 알 수 없는 서버 거절 %s code=%s — **분류하지 않았다.** "
                              "전선에 나갔는지 여부를 server.cpp 에서 확인하고 칸을 정해라"
                         % (rid, code))
                return "unknown"

            # ⚠ 큐가 들어온 뒤 생긴 중간 상태. **성공도 실패도 아니다** —
            # "받았고 아직 전선에 안 내보냈다"이므로 **어느 칸에도 세지 않고 계속 기다린다.**
            # 이것을 결말로 세면 **아직 나가지도 않은 하행을 판정해 버린다.**
            if o.get("type") == "queued":
                self.n_queued += 1
                self.log("=", "큐 대기 %s — ahead=%s · expires_ms=%s · ack_budget_ms=%s "
                              "(결말 아님. 계속 기다린다)"
                         % (rid, o.get("ahead"), o.get("expires_ms"), o.get("ack_budget_ms")))
                continue

            if o.get("type") == "ack":
                result = o.get("result")
                if result == 4:
                    self.n_ack += 1
                    self.log("←", "ACK %s result=4 (정상 — 무장 안 된 상태의 거절)" % rid)
                    return "ack"
                # 🔴 4 가 아니다. **무장돼 있으면 상태가 바뀐다.** 크게 남긴다.
                self.n_ack_other += 1
                self.log("!", "🔴 ACK %s result=%s — 4 가 아니다. **무장 상태를 의심하라.** "
                              "무장돼 있으면 이 주입이 실제로 상태를 바꿨다는 뜻이다. "
                              "판정에 반드시 적고, 계속 나오면 주입을 멈춰라" % (rid, result))
                return "ack_other"
        self.n_timeout += 1
        self.log("!", "ACK 없음 %s — %.0f초 안에 안 왔다 (monitor 의 D1/D2 후보)"
                 % (rid, self.a.ack_timeout))
        return "timeout"

    # ═══ 중단 조건 — **조건과 검사 코드가 같은 자리에 있다** ══════════════════════════
    #
    # 🔴 **왜 여기 모아 두는가**: 2026-08-17 창 A 에서 내가 중단 조건을 REQ 파일에 적어 두고
    # **그 조건이 걸린 칸을 한 번도 폴링하지 않았다.** 조건은 21:00:37 에 충족됐는데 주입은
    # 21:03:14 까지 약 130건 더 돌았다. 원인은 **"움직일 것으로 기대한 칸만 봤고
    # 멈추라고 정해 둔 칸은 안 봤다"** 였다.
    #
    # > **조건을 적는 것과 그 조건을 보는 감시를 만드는 것은 다르다.**
    #
    # 상수만 딴 데 두고 검사를 사람에게 맡기면 **같은 일이 또 난다.**
    # 그래서 **문턱·검사·문구가 이 함수 하나에 있다.** 고칠 때도 한 자리만 본다.
    #
    # ⚠ **내가 볼 수 없는 것은 조건으로 두지 않는다.** 예: "장치 세션 종료 N회"는
    # 서버 로그에만 있고 내 WS 에는 안 온다 — 그건 monitor 의 몫이고, 여기 적어 두면
    # **검사되지 않는 조건이 검사되는 것처럼 보인다.** 그게 이 결함의 원래 모양이다.
    #   → 다만 장치가 오프라인이 되면 서버가 내 주입을 `device_offline` 로 거절하므로
    #     아래 ①이 **그 사건의 대리 지표**가 된다.
    def check_stop(self, kind):
        """방금 주입의 결과(`kind`)를 반영하고, 멈춰야 하면 이유 문자열을 돌려준다."""
        # 연속 카운터는 여기서만 갱신한다 — 갱신과 검사가 갈라지면 또 어긋난다.
        if kind == "not_injected":
            self.streak_offline += 1
        else:
            self.streak_offline = 0

        a = self.a
        # ① 장치 오프라인 연속 — **장치가 없는데 넣으면 분모만 오염시킨다**
        if a.stop_offline_streak and self.streak_offline >= a.stop_offline_streak:
            return ("장치 오프라인 %d건 연속 (문턱 %d) — 서버가 전선에 안 내보내고 있다"
                    % (self.streak_offline, a.stop_offline_streak))
        # ② `result` 가 4 가 아니다 — **무장돼 있으면 이 주입이 상태를 바꾼다**
        if a.stop_bad_result and self.n_ack_other >= a.stop_bad_result:
            return ("result≠4 가 %d건 (문턱 %d) — 무장 상태를 의심해라. 상태를 바꾸고 있을 수 있다"
                    % (self.n_ack_other, a.stop_bad_result))
        # ③ 응답없음 누적 — **하행이 실제로 실패하고 있다**(3회 재전송까지 소진된 것)
        #    🔴 창 A 3차가 이 칸에서 3 을 찍었다. 이 규칙이 있었으면 그때 멈췄다.
        if a.stop_timeouts and self.n_timeout >= a.stop_timeouts:
            return ("응답없음 %d건 (문턱 %d) — 하행이 재전송까지 소진하고 실패하고 있다"
                    % (self.n_timeout, a.stop_timeouts))
        # ④ 알 수 없는 거절·예외·재접속 — **모르는 것이 반복되면 멈추고 사람이 본다**
        if a.stop_errors and (self.n_error + self.n_reconnect) >= a.stop_errors:
            return ("알 수 없는 거절/예외/재접속 합 %d건 (문턱 %d)"
                    % (self.n_error + self.n_reconnect, a.stop_errors))
        return None

    def _wait_with_heartbeat(self, total):
        """첫 주입까지 기다리면서 주기적으로 살아 있음을 남긴다."""
        if total <= 0:
            return
        beat = self.a.heartbeat
        waited = 0.0
        first_at = time.strftime("%Y-%m-%d %H:%M:%S",
                                 time.localtime(time.time() + total))
        self.log("*", "대기 %.0f초 — 첫 주입 예정 %s (그때까지 %.0f초마다 살아있음을 찍는다)"
                 % (total, first_at, beat))
        while waited < total:
            chunk = min(beat, total - waited)
            time.sleep(chunk)
            waited += chunk
            if waited < total:
                self.log("·", "대기 중 — 남은 %.0f초 (첫 주입 %s)" % (total - waited, first_at))

    def run(self):
        self.log("*", "하행 주입 시작 — 주기 %.0f초 · 첫 주입까지 %.0f초 대기 · slot=%s · 마감 %.0f초"
                 % (self.a.interval, self.a.start_delay, self.a.slot, self.a.ack_timeout))
        self.log("*", "기대 동작: 무장하지 않은 T,S → result=4 거절 → **장치 상태 변화 없음**")

        # 서버 기동 유예(SERVER_START_GRACE_S=90)를 피한다. 그 안의 하행은
        # monitor 집계에서 제외되므로 **넣어도 안 세어진다** — A 의 유일한 1건이 그랬다.
        #
        # 🔴 **대기 중에도 살아 있다는 것을 로그로 말한다.**
        # 무주입 기준선 국면 때문에 이 대기가 1시간이 될 수 있는데, 그동안 로그가 조용하면
        # **죽은 것과 기다리는 것이 구별되지 않는다.** 그러면 국면 2 가 조용히 시작 안 되고,
        # 아침에는 그것이 "하행 미실행(D3)"과 똑같이 보인다 — 원장 §5.2 의 그 자리다.
        self._wait_with_heartbeat(self.a.start_delay)

        i = 0
        next_at = time.time()
        while self.a.count == 0 or i < self.a.count:
            i += 1
            # 🔴 회차를 `rid` 자체로 가른다(원장 §8.13). 기본값 `inj` 로 두면 **실행마다 `inj-1`
            # 부터 다시 시작**하고, append-only 로그를 회차 구분 없이 `grep` 하면 **직전 실행분에
            # 걸린다** — 2026-08-17 에 내 감시기가 실제로 그렇게 걸렸다.
            # 시각으로 자르는 것도 되지만 **식별자가 유일하면 자르는 것을 잊어도 안전하다.**
            rid = "%s-%d" % (self.a.rid_prefix, i)
            try:
                if self.sock is None:
                    self.connect()
                kind = self.inject(rid)
                # 🔴 **주입이 끝나면 즉시 닫는다. 연결을 주기 내내 붙들지 않는다.**
                #
                # 서버는 **S 프레임마다 스냅샷을 WS 로 밀어 준다**(약 1KB · 1Hz).
                # 우리가 주기(기본 300초) 동안 안 읽으면 그 소켓의 송신 버퍼가 찬다.
                # 그러면 서버의 `send_raw` 가 **블로킹으로 최대 SEND_TIMEOUT_MS(1000ms)**
                # 를 기다렸다가 그 클라이언트를 끊는다(server.cpp:1093-1108).
                #
                # 🔴 문제는 서버가 **단일 스레드 select 루프**라는 것이다 —
                # 그 1초 동안 **9991(아두이노) 처리도 같이 멈춘다.**
                # 즉 내 주입기가 **측정 대상인 프레임 공백을 스스로 만들어 낸다.**
                # 8시간이면 약 96회다. 기준선을 재는 창에서 절대 하면 안 되는 짓이다.
                #
                # 닫으면 소켓 수명이 주기당 1초 남짓이라 버퍼가 찰 틈이 없다.
                # 부수 효과로 **주입마다 연결이 독립**된다 — 상한 소켓이 남긴 상태가
                # 다음 주입의 판정을 오염시키지 못한다(가짜 `미주입`이 안 생긴다).
                self.close()
                # 🔴 **매 주기 검사한다.** 사람이 보는 것에 맡기지 않는다 — 그게 창 A 의 결함이었다.
                reason = self.check_stop(kind)
                if reason:
                    self.stopped_reason = reason
                    # **조용히 멈추지 않는다.** 안 남기면 "끝났나 죽었나"를 못 가른다(§5.2).
                    self.log("🛑", "**중단 조건 충족 — 주입을 멈춘다** (i=%d/%s) · %s"
                             % (i, (str(self.a.count) if self.a.count else "무한"), reason))
                    break
            except (OSError, ConnectionError) as e:
                self.log("!", "연결 문제(%s) — 재접속한다" % e)
                self.close()
                self.n_reconnect += 1
                # 재접속을 즉시 반복하지 않는다. 서버가 죽어 있으면 초당 수십 번 두드리게 된다.
                time.sleep(min(self.a.interval, 10))
            except Exception as e:                      # noqa: BLE001
                # 🔴 **무엇이 터지든 이 루프는 안 죽는다.**
                # 8시간 무인으로 도는 물건이라, 예상 못 한 예외 하나로 프로세스가 사라지면
                # **그 시점부터 창 끝까지 하행 칸이 조용히 빈다.** 그런데 로그에는
                # "주입이 멈췄다"가 안 남아서, 아침에 보면 **D3(미실행)와 구별이 안 된다.**
                # 죽는 것보다 **시끄럽게 살아 있는 것**이 낫다.
                self.n_error += 1
                self.log("!", "🔴 예상 못 한 예외(%s: %s) — 기록하고 계속한다"
                         % (type(e).__name__, e))
                self.close()
                time.sleep(min(self.a.interval, 10))

            next_at += self.a.interval
            sleep_for = next_at - time.time()
            if sleep_for < 0:
                # 주기보다 오래 걸렸다 — 위상을 다시 맞춘다(누적 드리프트 방지).
                next_at = time.time()
                sleep_for = 0
            time.sleep(sleep_for)

        self.summary()

    def summary(self):
        # 🔴 **정상 종료와 중단을 요약에서 갈라 준다.** 안 갈라 두면 `시도 130` 이
        # "그만큼만 계획했다"인지 "멈췄다"인지 구별되지 않는다 — §5.2 의 그 자리다.
        if self.stopped_reason:
            self.log("🛑", "**중단으로 끝났다** — %s" % self.stopped_reason)
            self.log("🛑", "⚠ 이 회차의 표본은 계획보다 적다. **분모를 `시도` 로 쓰고 계획 수로 쓰지 마라**")
        else:
            self.log("▣", "계획대로 끝났다(중단 조건 미충족)")
        self.log("▣", "주입 종료 · 시도 %d · ACK(result=4) %d · ACK(다른 result) %d · "
                       "미주입(서버거절) %d · 응답없음 %d · 재접속 %d · 예외 %d · 큐대기통보 %d"
                 % (self.n_sent, self.n_ack, self.n_ack_other,
                    self.n_not_injected, self.n_timeout, self.n_reconnect, self.n_error,
                    self.n_queued))
        self.log("▣", "⚠ 이 숫자는 **서버 왕복**만 말한다. 장치가 실제로 받았는지(②)는 "
                      "시리얼 로그가 있어야 갈린다 — D1 과 D2 는 monitor 가 가른다")
        self.log("▣", "🔴 **분모는 '시도'가 아니라 '시도 − 미주입' 이다.** 미주입은 서버가 "
                      "전선에 안 내보낸 것이라 하행 경로를 시험한 적이 없다 — "
                      "D1/D2 로 세면 없는 고장이 생긴다")


def main():
    ap = argparse.ArgumentParser(description="관측 창용 하행 주입기")
    ap.add_argument("--host", default="127.0.0.1")
    # 원장 §8.2 — 기본값을 두지 않는다.
    ap.add_argument("--port", type=int, required=True, help="대상 HTTP/WS 포트. 기본값 없음")
    ap.add_argument("--allow-production", action="store_true",
                    help="운영 포트에 주입한다. **이 도구는 그것이 목적**이지만 "
                         "명시하게 해서 사고로 도는 것을 막는다")
    ap.add_argument("--slot", default="A1", help="대상 칸. 무장 안 된 상태라 어느 칸이든 거절된다")
    ap.add_argument("--interval", type=float, default=300.0, help="주입 주기(초). 기본 300 = 5분")
    ap.add_argument("--start-delay", type=float, default=150.0,
                    help="첫 주입까지 대기(초). 기본 150 — 서버 기동 유예 90초를 넘긴다")
    ap.add_argument("--ack-timeout", type=float, default=30.0,
                    help="ACK 마감(초). monitor 짝짓기 시한과 같은 30초")
    ap.add_argument("--count", type=int, default=0, help="주입 횟수(0=무한)")
    # ── 중단 조건 문턱. **검사 코드는 `check_stop()` 안에 있고 문구도 거기 있다.**
    #    🔴 문턱만 여기 두고 검사를 사람에게 맡기는 것이 창 A 의 결함이었다. 0 = 그 규칙 끔.
    ap.add_argument("--stop-offline-streak", type=int, default=5,
                    help="장치 오프라인 연속 N건이면 스스로 멈춘다 (0=끔)")
    ap.add_argument("--stop-bad-result", type=int, default=2,
                    help="result≠4 가 누적 N건이면 멈춘다 — 상태를 바꾸고 있다는 뜻 (0=끔)")
    ap.add_argument("--stop-timeouts", type=int, default=3,
                    help="응답없음 누적 N건이면 멈춘다. **창 A 3차가 이 칸에서 3 을 찍었다** (0=끔)")
    ap.add_argument("--stop-errors", type=int, default=5,
                    help="알 수 없는 거절+예외+재접속 합 N건이면 멈춘다 (0=끔)")
    ap.add_argument("--rid-prefix", default="inj",
                    help="rid 접두. **회차를 갈라야 할 때 바꿔라**(예: inj2). "
                         "기본값이면 실행마다 rid 가 1 부터 다시 시작해 "
                         "append-only 로그에서 직전 회차와 짝이 섞인다(원장 §8.13)")
    ap.add_argument("--heartbeat", type=float, default=300.0,
                    help="첫 주입 대기 중 살아있음을 찍는 주기(초). 기본 300")
    ap.add_argument("--log", default=None, help="주입 기록 파일 경로(append)")
    a = ap.parse_args()

    if a.port in PRODUCTION_PORTS and not a.allow_production:
        print("🔴 %d 는 운영 포트다. --allow-production 을 명시해라." % a.port, file=sys.stderr)
        return 2

    # 🔴 무장 명령은 이 도구에 없다. 그래도 한 번 더 못을 박는다 —
    #    누가 나중에 옵션을 늘릴 때 이 주석을 보게 하려는 것이다.
    #    무장하면 T,S 가 **실제로 상태를 바꾸고**, 그 순간 이 도구는 관측기가 아니라 교란원이 된다.

    try:
        Injector(a).run()
    except KeyboardInterrupt:
        print("\n%s  * 중단됨" % stamp(), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
