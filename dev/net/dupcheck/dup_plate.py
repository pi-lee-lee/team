#!/usr/bin/env python3
# dup_plate.py — 🔴 **같은 번호판을 반복해 보내면 자리가 계속 배정되나** (REQ 없음 · 2026-08-26)
#
# 왜 하니스가 아니라 이 도구인가:
#   판정이 `http.h::on_plate` 에 있는데 그건 **`struct Server` 의 몸통 조각**이다.
#   `net/flowcheck/flow26.cpp` 는 `lot.cpp` + 가짜 `ParkingServer` 만 링크해서 **도달할 수 없다.**
#   ★ 그래서 **실물 서버에 진짜 소켓으로 밀어 넣는다.** 이게 이 경로를 시험하는 유일한 자다.
#
# 🔴 빨간불 : 고치기 전 서버에 대고 돌리면 **자리가 계속 준다**(usable 이 줄어든다).
#            고친 뒤에는 **첫 건만** 배정되고 나머지는 안 는다.
# ⚠ 실기 서버에 쓰지 마라 — 자리를 실제로 먹는다. **시험 인스턴스에만.**
#
# 사용 : dup_plate.py <폰포트> <화면포트> [번호판] [반복수]
import json, socket, sys, time, base64, os

PHONE = int(sys.argv[1]) if len(sys.argv) > 1 else 5500
WEB   = int(sys.argv[2]) if len(sys.argv) > 2 else 9900
PLATE = sys.argv[3] if len(sys.argv) > 3 else "999하9999"
N     = int(sys.argv[4]) if len(sys.argv) > 4 else 6


def occupied_count():
    """화면 봉투에서 **점유된 자리 수**를 읽는다. 🔑 판정 입력은 이것 하나다."""
    s = socket.create_connection(("127.0.0.1", WEB), timeout=5); s.settimeout(6)
    k = base64.b64encode(os.urandom(16)).decode()
    s.sendall(("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % k).encode())
    buf = b""
    end = time.time() + 6
    got = None
    while time.time() < end:
        try:
            d = s.recv(65536)
        except socket.timeout:
            break
        if not d:
            break
        buf += d
        # 🔑 프레임을 제대로 안 푼다 — `reserved` 문자열만 찾는다. 이 도구엔 그것으로 충분하다.
        i = buf.find(b'"reserved":"')
        if i >= 0:
            got = buf[i + 12: i + 22].decode("ascii", "replace")
            break
    s.close()
    return got


def push(plate):
    s = socket.create_connection(("127.0.0.1", PHONE), timeout=5)
    s.sendall((json.dumps({"value": plate, "device": "DUPTEST"}, ensure_ascii=False) + "\n").encode())
    time.sleep(0.4)
    s.close()


def main():
    print("번호판 %s 를 %d회 반복 송신 (폰 %d · 화면 %d)" % (PLATE, N, PHONE, WEB))
    before = occupied_count()
    print("  전 : reserved=%s" % before)
    for i in range(N):
        push(PLATE)
        time.sleep(1.0)          # 🔑 `send.min_interval_ms` 하한과 같은 초당 1건
    time.sleep(1.0)
    after = occupied_count()
    print("  후 : reserved=%s" % after)
    print()
    print("🔑 판정은 **서버 로그**로 한다 — 이 도구는 값만 보여 준다:")
    print("   `✓ 예약 없음 → 빈자리 배정`  가 **몇 줄** 났나")
    print("     🔴 여러 줄  = 고쳐지지 않았다(반복마다 자리를 먹는다)")
    print("     ✅ 한 줄 이하 = 막혔다")
    print("   `! 이미 주차된 번호판이 게이트에서 인식됨` 이 **한 줄만** 났나(반복 억제)")


if __name__ == "__main__":
    main()
