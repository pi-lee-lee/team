import os, sys, time, termios
port, out, dur = sys.argv[1], sys.argv[2], float(sys.argv[3])
fd = os.open(port, os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
# 🔴 fd 를 연 **뒤** 설정한다. 먼저 stty 로 잡아도 open 이 되돌린다(macOS).
a = termios.tcgetattr(fd)
a[0] = 0                                  # iflag
a[1] = 0                                  # oflag
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[3] = 0                                  # lflag — raw
a[4] = a[5] = termios.B115200
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
end = time.time() + dur
with open(out, 'wb') as f:
    while time.time() < end:
        try:
            d = os.read(fd, 4096)
            if d: f.write(d); f.flush()
            else: time.sleep(0.02)
        except BlockingIOError:
            time.sleep(0.02)
os.close(fd)
print('캡처 종료 %s' % time.strftime('%H:%M:%S'))
