#!/usr/bin/env python3
"""부팅 캡처.

🔴 macOS 는 포트를 **열 때 termios 를 초기화한다.** `stty -f` 로 미리 넣은 보율은
   열리는 순간 사라진다. 그래서 **연 fd 에** `tcsetattr` 로 직접 박는다.

🔴 포트 이름을 손으로 넘기지 않는다 — 케이블을 다시 꽂으면 이름이 바뀐다(`port.py` 참조).
🔴 그리고 **반드시 닫는다**(`finally`). 안 닫힌 포트가 다음 열거에서 이름을 밀어 올린다.

쓰는 법 : python3 arduino/.burn/cap.py <초> <출력파일> [포트]
          포트를 안 주면 스스로 찾는다. 애매하면 **고르지 않고 멈춘다.**
"""
import os
import sys
import termios
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from port import find_port, PortError          # noqa: E402

if len(sys.argv) not in (3, 4):
    print(__doc__, file=sys.stderr)
    raise SystemExit(2)

secs, out = float(sys.argv[1]), sys.argv[2]
try:
    portname = sys.argv[3] if len(sys.argv) == 4 else find_port()
except PortError as e:
    print(e, file=sys.stderr)
    raise SystemExit(1)

print(f"포트 {portname} · {secs:g}초", file=sys.stderr)
fd = os.open(portname, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
buf = bytearray()
try:
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0                     # iflag oflag lflag — 가공하지 않는다
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200              # ispeed / ospeed
    termios.tcsetattr(fd, termios.TCSANOW, a)
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            d = os.read(fd, 4096)
            if d:
                buf += d
        except BlockingIOError:
            time.sleep(0.01)
finally:
    # 🔴 예외로 죽어도 닫는다. 안 닫으면 다음 사람이 "포트가 늘어난다" 를 겪는다.
    os.close(fd)
    open(out, 'wb').write(buf)
    print(f"{len(buf)} B → {out}", file=sys.stderr)
