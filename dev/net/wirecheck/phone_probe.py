#!/usr/bin/env python3
# phone_probe.py — **폰(digitcam) 자리에 붙어 하행을 그대로 받아 적는다.**
#
# 쓰는 곳 : 서버가 유휴 하트비트(`PING,<seq>`)를 실제로 보내는지 **값으로** 보는 것.
#   🔑 코드에 `send_raw` 가 있다는 것과 **전선에 바이트가 나온다**는 것은 다른 축이다.
#      §"부르는 줄과 꽂는 줄을 둘 다 세라" — 이 프로브가 '꽂는 줄' 쪽이다.
#
# ⚠ 아무것도 안 보낸다 — **유휴를 흉내내는 것이 목적**이다. 보내면 그 자체가 매핑을 갱신한다.
#
# 사용 : phone_probe.py <포트> <몇 초 동안>
import socket
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5500
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 40.0

s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
s.settimeout(1.0)
t0 = time.time()
buf = b""
lines = []
print("phone_probe — 포트 %d 에 붙었다. %.0f초 동안 **아무것도 안 보내고** 받기만 한다" % (PORT, SECS))
try:
    while time.time() - t0 < SECS:
        try:
            d = s.recv(4096)
        except socket.timeout:
            continue
        if not d:
            print("  🔴 서버가 닫았다 (%.1fs)" % (time.time() - t0))
            break
        buf += d
        while b"\n" in buf:
            i = buf.index(b"\n")
            ln = buf[:i].decode("ascii", "replace")
            buf = buf[i + 1:]
            lines.append((time.time() - t0, ln))
            print("  ← %6.1fs  %r" % (lines[-1][0], ln))
finally:
    s.close()

print("받은 줄 %d 개 / %.0f초" % (len(lines), SECS))
pings = [x for x in lines if x[1].startswith("PING")]
print("그 중 PING : %d 개" % len(pings))
if len(pings) >= 2:
    gap = pings[1][0] - pings[0][0]
    print("  🔑 첫 두 PING 간격 = %.1f초" % gap)
sys.exit(0 if pings else 1)
