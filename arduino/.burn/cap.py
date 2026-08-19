#!/usr/bin/env python3
"""부팅 캡처 — 🔴 macOS 는 포트를 열 때 termios 를 초기화한다.
`stty -f` 로 미리 넣은 보율은 **열리는 순간 사라진다**(원장 §29).
그래서 **연 fd 에** tcsetattr 로 직접 박는다. 이게 이 파일이 존재하는 이유다."""
import os, sys, termios, time

port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = a[1] = a[3] = 0                       # iflag oflag lflag — 가공하지 않는다
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200                # ispeed / ospeed
termios.tcsetattr(fd, termios.TCSANOW, a)
t0 = time.time(); buf = bytearray()
while time.time() - t0 < secs:
    try:
        d = os.read(fd, 4096)
        if d: buf += d
    except BlockingIOError:
        time.sleep(0.01)
os.close(fd)
open(out, 'wb').write(buf)
print(f"{len(buf)} B 캡처 → {out}")
