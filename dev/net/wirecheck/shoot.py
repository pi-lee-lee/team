#!/usr/bin/env python3
# shoot.py — 🔴 **서버에게 촬영을 시킨다**(WS `shoot_now`). 그리고 왕복을 시각으로 잰다.
#
# 배경: `five_connect_test` 는 입차 흐름을 통째로 뺐다 → `cameraShoot()` 을 부를 주체가 없다.
#   그래서 서버에 시험 전용 입구(`shoot_now`)를 하나 뒀고, 이 도구가 그것을 누른다.
#   🔑 화면 버튼이 생기기 전까지 **사람이 쏠 수 있는 유일한 길**이다.
#
# 판정은 서버 로그와 대조한다(이 도구는 판정하지 않는다):
#   `→폰 촬영 요청 <id>`        나갔나
#   `←폰 촬영 <id> → <plate>`   🔴 **번호가 돌아왔나 — 이 시험의 전부다**
#   실패 갈래 넷은 원인이 다르다:
#     `촬영 요청 실패 — 폰이 안 붙어 있다`(발급조차 안 됨) / 폰 미접속 / `촬영 <id> 실패` / `"-1"`
#
# 사용 : shoot.py [포트]          기본 9900
import base64
import os
import socket
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9900


def ws_connect(port):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.settimeout(5)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n" % key).encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    if b"101" not in buf.split(b"\r\n", 1)[0]:
        raise SystemExit("🔴 WS 업그레이드 실패")
    return s


def ws_send(sock, text):
    # 🔑 클라이언트 프레임은 **마스킹이 필수**다(RFC 6455 §5.3).
    p = text.encode()
    mask = os.urandom(4)
    m = bytes(p[i] ^ mask[i % 4] for i in range(len(p)))
    n = len(p)
    hdr = bytes([0x81, 0x80 | n]) if n < 126 else bytes([0x81, 0xFE]) + n.to_bytes(2, "big")
    sock.sendall(hdr + mask + m)


def main():
    s = ws_connect(PORT)
    t0 = time.time()
    print("쏘기 직전 : %s" % time.strftime("%H:%M:%S"))
    ws_send(s, '{"type":"shoot_now","rid":"shoot-%d"}' % (int(t0) % 100000))
    # ack/error 만 잠깐 본다 — 🔑 **번호는 여기로 안 온다.** 서버 로그에 `←폰 촬영` 으로 뜬다
    try:
        end = time.time() + 4
        while time.time() < end:
            d = s.recv(65536)
            if not d:
                break
            t = d.decode("utf-8", "replace")
            for k in ('"type":"ack"', '"type":"error"'):
                if k in t:
                    i = t.find(k)
                    print("  ← %s   (+%.3f초)" % (t[max(0, i - 16):i + 140].replace("\n", " "),
                                                  time.time() - t0))
                    end = 0
                    break
    except socket.timeout:
        pass
    s.close()
    print("쏜 뒤     : %s" % time.strftime("%H:%M:%S"))
    print("🔑 서버 로그에서 `→폰 촬영 요청` 과 `←폰 촬영 … → <번호>` 를 대조해라 — 왕복은 그 두 줄의 차다")


if __name__ == "__main__":
    main()
