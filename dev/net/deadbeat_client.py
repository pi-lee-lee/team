#!/usr/bin/env python3
"""안 빼가는 클라이언트 — 서버가 블로킹 send() 에서 서는지 시험한다.

접속만 하고 **절대 recv 하지 않는다.** 그러면 서버가 보낸 데이터가 이쪽 TCP 수신 창에
쌓이다가 창이 닫히고, 서버의 `send()` 는 더 밀어 넣을 수 없게 된다.

    python3 net/deadbeat_client.py --port 9991 --as arduino   # 아두이노인 척
    python3 net/deadbeat_client.py --port 9900 --as ws        # WS 클라이언트인 척
    python3 net/deadbeat_client.py --port 5500 --as phone     # 폰인 척

## 왜 이런 게 필요한가

서버가 **아무 로그 없이 행에 걸린 사고**가 실기에서 났다. 단일 스레드라 상대 하나가
데이터를 안 빼가면 `send()` 안에서 전체가 멈춘다. `SO_SNDTIMEO` 가 없으면 무한정이다.

이 스크립트는 그 상대를 재현한다. 서버에 `SO_SNDTIMEO` 가 걸려 있으면
**타임아웃 → 그 연결만 끊김 → 나머지는 계속 동작**이어야 한다.

**수신 창을 확실히 채우려면** 서버가 많이 보내게 만들어야 한다. `--as arduino` 는
접속 직후 상태 프레임을 한 번 보내 서버가 예약 재하달·스냅샷을 밀어내게 유도하고,
`--as ws` 는 핸드셰이크만 하고 스냅샷 세례를 받는다(읽지 않는다).
`--socket-buf` 로 수신 버퍼를 작게 잡아 창이 빨리 닫히게 한다.
"""

import argparse
import base64
import os
import socket
import sys
import time


def cksum(prefix):
    x = 0
    for b in prefix.encode("ascii"):
        x ^= b
    return "%02X" % x


def main():
    ap = argparse.ArgumentParser(description="접속 후 recv 를 전혀 하지 않는 클라이언트")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9991)
    ap.add_argument("--as", dest="role", choices=["arduino", "ws", "phone", "raw"],
                    default="raw")
    ap.add_argument("--hold", type=float, default=30.0, help="이 시간(초)만큼 잠자며 안 읽는다")
    ap.add_argument("--socket-buf", type=int, default=1024,
                    help="수신 버퍼 크기(작을수록 창이 빨리 닫힌다)")
    a = ap.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # 수신 버퍼를 작게 → 서버가 조금만 보내도 창이 닫힌다
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, a.socket_buf)
    except OSError:
        pass
    s.connect((a.host, a.port))
    print("접속 %s:%d (역할 %s) — 이제 절대 읽지 않는다" % (a.host, a.port, a.role))

    if a.role == "arduino":
        # 상태 프레임 하나를 보내 서버가 우리를 아두이노로 인식하게 한다.
        # 그러면 서버가 예약 재하달·시뮬/테스트 명령을 이쪽으로 밀어낸다.
        p = "S,0,0000000000,0000000000,0,DEAD,-,"
        s.sendall((p + cksum(p) + "\n").encode())
        print("→ 상태 프레임 1회 전송(아두이노인 척). 이후 무응답")
    elif a.role == "ws":
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET /ws HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n" % (a.host, a.port, key))
        s.sendall(req.encode())
        print("→ WS 핸드셰이크 요청. 응답도 스냅샷도 읽지 않는다")
    elif a.role == "phone":
        s.sendall("123가4568\n".encode("utf-8"))
        print("→ 번호판 1건 전송(폰인 척). 이후 무응답")

    print("%.0f초 동안 잠잔다 — 서버가 이 사이에 서면 다른 클라이언트도 멈춘다" % a.hold)
    try:
        time.sleep(a.hold)
    except KeyboardInterrupt:
        pass
    s.close()
    print("종료")


if __name__ == "__main__":
    main()
