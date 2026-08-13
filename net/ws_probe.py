#!/usr/bin/env python3
"""WebSocket 탐침 — 브라우저 없이 주차 관제 서버의 WS 경로를 검증한다.

명세: docs/net/parking-protocol.md §5

표준 라이브러리만. 검증에 필요한 만큼만 구현했다(텍스트 프레임, 마스킹, 16비트 길이).

    python3 net/ws_probe.py --listen 3                       # 3초간 받은 것만 출력
    python3 net/ws_probe.py --reserve B3 --user u17          # 예약 보내고 응답 관찰
    python3 net/ws_probe.py --cancel B3

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
    ap.add_argument("--port", type=int, default=9900)
    ap.add_argument("--listen", type=float, default=3.0, help="이 시간(초)만큼 수신하고 끝낸다")
    ap.add_argument("--reserve", metavar="SLOT")
    ap.add_argument("--cancel", metavar="SLOT")
    ap.add_argument("--user", default="u17")
    ap.add_argument("--rid", default="probe-1")
    ap.add_argument("--delay", type=float, default=0.4, help="접속 후 요청까지 대기")
    a = ap.parse_args()

    s = socket.create_connection((a.host, a.port), timeout=5)
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
        print("← [%s] %d바이트  길이필드: %s  마스킹=%s" % (t, len(body), desc, masked))
        if t == "snapshot":
            print("   device=%s" % json.dumps(o.get("device"), ensure_ascii=False))
            print("   slots =%s" % " ".join(
                "%s:%d%d" % (x["id"], x["occupied"], x["reserved"]) for x in o.get("slots", [])))
            keys_ok = all("user_id" in x and "reserved_at" in x for x in o.get("slots", []))
            print("   user_id/reserved_at 키 존재: %s · 자리 수 %d" % (keys_ok, len(o.get("slots", []))))
        else:
            print("   %s" % text[:200])

        if not sent and (a.reserve or a.cancel) and time.time() - (deadline - a.listen) >= a.delay:
            if a.reserve:
                send_text(s, {"type": "reserve", "slot": a.reserve, "user_id": a.user, "rid": a.rid})
            else:
                send_text(s, {"type": "cancel", "slot": a.cancel, "rid": a.rid})
            sent = True

    if not sent and (a.reserve or a.cancel):
        # 스냅샷이 안 와도 요청은 보내 본다
        if a.reserve:
            send_text(s, {"type": "reserve", "slot": a.reserve, "user_id": a.user, "rid": a.rid})
        else:
            send_text(s, {"type": "cancel", "slot": a.cancel, "rid": a.rid})
        for op, body, desc, _m in frames(s, buf, time.time() + 6):
            print("← %d바이트 길이필드: %s\n   %s" % (len(body), desc, body.decode("utf-8", "replace")[:300]))
    s.close()


if __name__ == "__main__":
    main()
