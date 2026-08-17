#!/usr/bin/env python3
"""WebSocket 탐침 — 브라우저 없이 주차 관제 서버의 WS 경로를 검증한다.

명세: docs/net/parking-protocol.md §5

표준 라이브러리만. 검증에 필요한 만큼만 구현했다(텍스트 프레임, 마스킹, 16비트 길이).

    python3 net/ws_probe.py --listen 3                       # 3초간 받은 것만 출력
    python3 net/ws_probe.py --reserve B3 --user u17          # 예약 보내고 응답 관찰
    python3 net/ws_probe.py --cancel B3
    python3 net/ws_probe.py --sim                            # 시뮬 한 걸음(§12B.5)

**길이 필드 바이트를 그대로 찍는다.** 스냅샷이 125바이트를 넘으면 서버가 126 마커 +
16비트 확장 길이를 써야 하는데(명세 §5.2), "845바이트가 왔다"만으로는 그 경로가 실제로
돌았는지 알 수 없다. 그래서 파싱한 길이 필드를 함께 보여 준다.
"""

import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def handshake(s, host, port, path="/ws"):
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, host, port, key)
    )
    s.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        d = s.recv(4096)
        if not d:
            raise RuntimeError("핸드셰이크 중 연결이 끊겼다")
        buf += d
    head, rest = buf.split(b"\r\n\r\n", 1)
    text = head.decode("latin1")
    if "101" not in text.split("\r\n")[0]:
        raise RuntimeError("101 이 아니다:\n" + text)
    want = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    got = ""
    for ln in text.split("\r\n"):
        if ln.lower().startswith("sec-websocket-accept:"):
            got = ln.split(":", 1)[1].strip()
    ok = got == want
    print("핸드셰이크 101 · Accept %s (기대 %s) → %s" % (got, want, "일치" if ok else "불일치!"))
    if not ok:
        raise RuntimeError("Sec-WebSocket-Accept 불일치")
    return rest


def send_text(s, obj):
    payload = json.dumps(obj, ensure_ascii=False).encode("utf-8")
    mask = os.urandom(4)
    n = len(payload)
    head = bytearray([0x81])
    if n < 126:
        head.append(0x80 | n)
    elif n <= 0xFFFF:
        head.append(0x80 | 126)
        head += struct.pack("!H", n)
    else:
        head.append(0x80 | 127)
        head += struct.pack("!Q", n)
    head += mask
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    s.sendall(bytes(head) + masked)
    print("→ %s" % json.dumps(obj, ensure_ascii=False))


def frames(s, buf, deadline):
    """수신 프레임을 (opcode, payload, 길이필드설명) 으로 내놓는다."""
    while True:
        while len(buf) < 2:
            s.settimeout(max(0.05, deadline - time.time()))
            try:
                d = s.recv(65536)
            except socket.timeout:
                return
            if not d:
                return
            buf += d
        b0, b1 = buf[0], buf[1]
        op = b0 & 0x0F
        masked = bool(b1 & 0x80)
        ln = b1 & 0x7F
        off = 2
        if ln == 126:
            while len(buf) < 4:
                buf += s.recv(65536)
            ln = struct.unpack("!H", buf[2:4])[0]
            desc = "126 마커 + 16비트(%02X %02X) = %d" % (buf[2], buf[3], ln)
            off = 4
        elif ln == 127:
            while len(buf) < 10:
                buf += s.recv(65536)
            ln = struct.unpack("!Q", buf[2:10])[0]
            desc = "127 마커 + 64비트 = %d" % ln
            off = 10
        else:
            desc = "7비트 즉시값 = %d" % ln
        need = off + (4 if masked else 0) + ln
        while len(buf) < need:
            s.settimeout(max(0.05, deadline - time.time()))
            try:
                d = s.recv(65536)
            except socket.timeout:
                return
            if not d:
                return
            buf += d
        body = buf[off + (4 if masked else 0): need]
        if masked:
            m = buf[off:off + 4]
            body = bytes(c ^ m[i % 4] for i, c in enumerate(body))
        del buf[:need]
        yield op, bytes(body), desc, masked


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    # 🔴 **기본값을 일부러 두지 않는다.** 전에는 9900(= 운영 HTTP/WS)이 기본값이었다.
    # 그래서 `--port` 를 빼먹은 사람은 **"시험한다"고 생각하면서 운영에 붙었다.**
    # 포트 분리·flock·로그 분리·chmod 잠금을 전부 통과하는 경로다 — 방어선이 몇 겹이든
    # **기본값이 운영을 가리키면 그 전부를 우회한다.** (원장 §8.2)
    # web 의 `web/tools/e2e.mjs` 가 같은 이유로 먼저 이렇게 해 뒀고, 그 방식이 옳다.
    ap.add_argument("--port", type=int, required=True,
                    help="시험 인스턴스의 HTTP/WS 포트. **기본값 없음** — "
                         "운영 포트를 실수로 집지 않게 하려는 것이다")
    ap.add_argument("--allow-production", action="store_true",
                    help="운영 포트(9900/9991/5500)에 붙는 것을 허용한다. **관측 전용** — "
                         "상태를 바꾸는 요청은 이것만으로 안 열린다(--allow-state-change 가 더 필요하다)")
    # 🔴 운영 상태 변경 — **둘째 열쇠**. 하나로 안 열리게 한 것이 요점이다.
    #
    # 처음에 나는 "상태 변경은 어떤 플래그로도 안 열린다"로 만들었다. 근거는
    # **예약 한 번이 관측 창을 깼기 때문**이다(web REQ-0129). 그 판단은 여전히 옳다.
    # 그런데 2026-08-17 에 **운영에서 예약이 실제로 되는지 재야 하는 일**이 생겼다 —
    # `5b9d967`(5/5 성공)이 `R`/`C` 로 잰 것이라 같은 자로 재야 비교가 성립한다.
    #
    # 전면 금지를 유지하면 그때 **사람이 압박 속에서 이 파일을 고친다.** 그게 더 나쁘다.
    # 그래서 **막는 것을 없애지 않고 열쇠를 하나 더 요구**한다:
    #   · 사고로는 절대 안 열린다(플래그 둘을 동시에 우연히 칠 수 없다)
    #   · 열 때는 **무엇을 하는지 알고 치게 된다**(이름이 그렇게 되어 있다)
    ap.add_argument("--allow-state-change", action="store_true",
                    help="운영에서 **상태를 바꾸는** 요청(--reserve/--cancel/--sim/--test-*)을 허용한다. "
                         "⚠ --allow-production 과 **함께** 줘야 한다. "
                         "관측 창이 도는 중이면 반드시 monitor 에 시각을 통보해라")
    ap.add_argument("--listen", type=float, default=3.0, help="이 시간(초)만큼 수신하고 끝낸다")
    ap.add_argument("--reserve", metavar="SLOT")
    ap.add_argument("--cancel", metavar="SLOT")
    ap.add_argument("--sim", action="store_true", help="시뮬 한 걸음(§12B.5 sim_step)")
    # ── §12A 테스트 모드 — 하행 `T` 프레임을 낸다.
    # **왜 넣었나(REQ-0118 이음매 1)**: `DEV_ACK` 이관이 실제로 도는지 격리 검증하려면
    # **장치 상태를 안 바꾸는 ACK** 가 필요하다. R/C 는 예약을 바꾸므로 장치가 즉시 S 프레임을
    # 보내고, 그 S 가 로그·스냅샷을 따로 일으켜 **ACK 가 만든 것과 구별이 안 된다**(원장 §7.6).
    # `--test-set` 을 **무장하지 않은 상태로** 쏘면 장치가 `result=4` 로 거절하고
    # **아무 상태도 안 바꾼다** → 뒤따르는 S 가 없다. 그래서 이것이 격리 조건이다.
    ap.add_argument("--test-arm", action="store_true", help="§12A 무장(T,A). 장치 상태를 바꾼다")
    ap.add_argument("--test-disarm", action="store_true", help="§12A 해제(T,D)")
    ap.add_argument("--test-set", metavar="SLOT",
                    help="§12A 오버라이드(T,S). **무장 전에 쓰면 result=4 로 거절되고 "
                         "장치 상태가 안 바뀐다** — DEV_ACK 격리 검증용")
    ap.add_argument("--test-clear", metavar="SLOT", help="§12A 오버라이드 해제(T,X)")
    ap.add_argument("--occupied", default="1", choices=["0", "1"],
                    help="--test-set 이 넣을 값")
    ap.add_argument("--user", default="u17")
    ap.add_argument("--rid", default="probe-1")
    ap.add_argument("--delay", type=float, default=0.4, help="접속 후 요청까지 대기")
    a = ap.parse_args()

    # 보낼 요청을 한 곳에서 만든다 — 세 군데에 흩어 두면 종류를 늘릴 때 하나를 빠뜨린다.
    def request():
        if a.reserve: return {"type": "reserve", "slot": a.reserve, "user_id": a.user, "rid": a.rid}
        if a.cancel:  return {"type": "cancel",  "slot": a.cancel,  "rid": a.rid}
        if a.sim:     return {"type": "sim_step", "rid": a.rid}
        if a.test_arm:    return {"type": "test_arm", "rid": a.rid}
        if a.test_disarm: return {"type": "test_disarm", "rid": a.rid}
        if a.test_set:    return {"type": "test_set", "slot": a.test_set,
                                  "occupied": a.occupied, "rid": a.rid}
        if a.test_clear:  return {"type": "test_clear", "slot": a.test_clear, "rid": a.rid}
        return None

    # ── 🔴 운영 포트 방어 (원장 §8.2)
    #
    # **막는 것은 "운영 접속"이 아니라 "운영에 상태 변경을 쏘는 것"이다.**
    # A 창을 깰 뻔한 것은 관측이 아니라 **예약 한 번**이었다(web REQ-0129). 그래서:
    #   · 운영 포트 + 상태 변경  → **어떤 플래그로도 안 열린다.** 여기서 죽는다
    #   · 운영 포트 + 관측만      → `--allow-production` 을 명시해야 열리고, 크게 찍는다
    #   · 그 외(시험 포트)        → 그냥 돈다
    #
    # 관측까지 전면 금지하지 않는 이유: 새벽에 운영을 들여다볼 정당한 이유가 실제로 생기는데,
    # 그때 도구가 무조건 거부하면 **사람이 압박 속에서 이 파일을 고치게 된다.** 그게 더 나쁘다.
    PRODUCTION_PORTS = (9900, 9991, 5500)
    if a.port in PRODUCTION_PORTS:
        if request() is not None and not (a.allow_production and a.allow_state_change):
            print("🔴 %d 는 운영 포트이고 이 요청은 **상태를 바꾼다**\n"
                  "   (--reserve/--cancel/--sim/--test-*).\n"
                  "   열려면 **둘 다** 필요하다: --allow-production --allow-state-change\n"
                  "   하나로 안 열리게 한 이유: 관측 창이 도는 동안 **예약 한 번이 기준선을 깬다**\n"
                  "   — 실제로 그럴 뻔했다(web REQ-0129).\n"
                  "   그냥 시험이면 시험 인스턴스를 띄워라(--port-offset)." % a.port,
                  file=sys.stderr)
            return 2
        if request() is not None:
            print("⚠⚠⚠ 운영 포트 %d 에 **상태를 바꾸는 요청**을 보낸다: %s\n"
                  "    관측 창이 돌고 있으면 **지금 시각을 monitor 에 통보해라.**\n"
                  "    이 개입은 기록되지 않으면 남이 원인 불명의 사건으로 쫓게 된다."
                  % (a.port, json.dumps(request(), ensure_ascii=False)), file=sys.stderr)
        if not a.allow_production:
            print("🔴 %d 는 운영 포트다. 관측만 할 것이라면 --allow-production 을 명시해라.\n"
                  "   (기본값을 없앤 이유: 전에는 이 포트가 기본값이라 --port 를 빼먹으면\n"
                  "    시험한다고 생각하면서 운영에 붙었다 — 원장 §8.2)" % a.port,
                  file=sys.stderr)
            return 2
        print("⚠⚠ 운영 포트 %d 에 **관측 전용**으로 붙는다. 상태 변경은 보내지 않는다.\n"
              "    개입 기록이 필요하면 req.sh notice 로 남겨라." % a.port, file=sys.stderr)

    s = socket.create_connection((a.host, a.port), timeout=5)
    t0 = time.time()
    buf = bytearray(handshake(s, a.host, a.port))

    deadline = time.time() + a.listen
    sent = False
    for op, body, desc, masked in frames(s, buf, deadline):
        if op == 0x8:
            print("← close")
            break
        text = body.decode("utf-8", "replace")
        try:
            o = json.loads(text)
            t = o.get("type")
        except Exception:
            t = "?"
        # ⚠ **시각을 반드시 찍는다.** 이게 없으면 "ack 다음에 snapshot 이 왔다"까지만 알 뿐
        # **그 스냅샷이 ACK 때문인지 1Hz S 프레임 때문인지 못 가른다**(원장 §7.6 이 그 함정이다).
        # t0 = 접속 직후이고, 아래 값은 그로부터의 경과 초다.
        print("← [%s] t=+%.3fs  %d바이트  길이필드: %s  마스킹=%s"
              % (t, time.time() - t0, len(body), desc, masked))
        if t == "snapshot":
            print("   device=%s" % json.dumps(o.get("device"), ensure_ascii=False))
            print("   slots =%s" % " ".join(
                "%s:%d%d" % (x["id"], x["occupied"], x["reserved"]) for x in o.get("slots", [])))
            keys_ok = all("user_id" in x and "reserved_at" in x for x in o.get("slots", []))
            print("   user_id/reserved_at 키 존재: %s · 자리 수 %d" % (keys_ok, len(o.get("slots", []))))
        else:
            print("   %s" % text[:200])

        if not sent and request() and time.time() - (deadline - a.listen) >= a.delay:
            send_text(s, request())
            sent = True

    if not sent and request():
        # 스냅샷이 안 와도 요청은 보내 본다
        send_text(s, request())
        for op, body, desc, _m in frames(s, buf, time.time() + 6):
            print("← %d바이트 길이필드: %s\n   %s" % (len(body), desc, body.decode("utf-8", "replace")[:300]))
    s.close()


if __name__ == "__main__":
    # ⚠ `main()` 만 부르면 **반환값이 버려져 종료 코드가 항상 0** 이 된다.
    # 그러면 위 운영 포트 방어가 "막았다"고 찍고도 **감싼 스크립트에는 성공으로 보인다.**
    # 거부는 종료 코드로도 말해야 한다.
    sys.exit(main() or 0)
