#!/usr/bin/env python3
# lcd_repro.py — 🔴 **P5 가 LCD 명령을 받고 죽는지 재현한다.**
#
# ═══════════════════════════════════════════════════════════════════════════
# 배경(2026-08-25 14:57 · `net/run/srv-2026-08-25-1456.log` 273~302행):
#   S,114 → **send_cmd C5=0** → S,115(정상) → **G 하행(S+0ms)** → 🔴 **그 뒤 침묵**
#   ✅ 확정 : 마지막 상행 뒤 0ms 만에 하행이 나갔고 그 뒤 조용해졌다
#   ❌ 미확정 : **n=1.** 우연히 그 순간 리셋됐을 수 있다
#
# 🔑 그래서 이 도구가 하는 일은 하나다 — **관측을 미리 세워 놓고 같은 명령을 한 번 보낸다.**
#   ⚠ 사용자 보드다. **루트 신호 없이 실행하지 마라.**
#
# 판정 (실행 뒤 서버 로그와 대조):
#   죽는다   → `←ARD P5 S,…` 가 하행 직후 끊긴다  → **재현 확정.** arduino 가 고칠 대상이 명확해진다
#   안 죽는다 → S 가 계속 온다                      → 그때 것은 다른 축이다(전원·리셋·우연)
#
# 사용 : lcd_repro.py [값] [포트]      기본 값 0(welcome) · 포트 9900
# ═══════════════════════════════════════════════════════════════════════════
import base64
import os
import socket
import sys
import time

VALUE = int(sys.argv[1]) if len(sys.argv) > 1 else 0
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9900
DEVID, MODULE = "P5", "C5"


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
        raise SystemExit("🔴 WS 업그레이드 실패: %r" % buf.split(b"\r\n", 1)[0])
    return s, buf.split(b"\r\n\r\n", 1)[1]


def ws_send_text(sock, text):
    # 🔑 **클라이언트 프레임은 마스킹이 필수다**(RFC 6455 §5.3). 서버가 안 받으면 여기가 원인이다.
    payload = text.encode()
    mask = os.urandom(4)
    masked = bytes(payload[i] ^ mask[i % 4] for i in range(len(payload)))
    n = len(payload)
    if n < 126:
        hdr = bytes([0x81, 0x80 | n])
    elif n < 65536:
        hdr = bytes([0x81, 0xFE]) + n.to_bytes(2, "big")
    else:
        hdr = bytes([0x81, 0xFF]) + n.to_bytes(8, "big")
    sock.sendall(hdr + mask + masked)


def main():
    s, _ = ws_connect(PORT)
    cmd = ('{"type":"send_cmd","rid":"repro-%d","devid":"%s","module":"%s","value":%d}'
           % (int(time.time()) % 100000, DEVID, MODULE, VALUE))
    # 🔴 **보내기 직전/직후 시각을 찍는다** — 로그와 붙여야 판정이 된다(루트 요구).
    print("보내기 직전 : %s" % time.strftime("%H:%M:%S"))
    ws_send_text(s, cmd)
    print("보냈다      : %s   %s" % (time.strftime("%H:%M:%S"), cmd))
    # 응답(ack/error/queued)을 잠깐 받아 본다 — 판정이 아니라 참고다
    t0 = time.time()
    try:
        while time.time() - t0 < 3:
            d = s.recv(65536)
            if not d:
                break
            txt = d.decode("utf-8", "replace")
            for k in ('"type":"ack"', '"type":"error"', '"type":"queued"'):
                if k in txt:
                    i = txt.find(k)
                    print("  ← %s" % txt[max(0, i - 20):i + 160].replace("\n", " "))
                    t0 = 0
                    break
    except socket.timeout:
        pass
    s.close()
    print("보낸 뒤     : %s" % time.strftime("%H:%M:%S"))
    print("🔑 이제 서버 로그에서 `←ARD %s S,` 가 **계속 오는지** 봐라 — 끊기면 재현 확정이다" % DEVID)


if __name__ == "__main__":
    main()
