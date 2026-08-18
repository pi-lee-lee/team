#!/usr/bin/env python3
"""모의 노드 — 시험 인스턴스에 `S`/`D` 를 보내는 최소 클라이언트.

🔴 **실물 보드를 대체하지 않는다.** 시리얼·보드는 배타적 자원이고 장치 인스턴스가 쓴다.
   이건 **서버·화면 계약을 실기 경로로 밟기 위한 것**이고, 그 범위 밖의 판정에 쓰면 안 된다.

⚠ **이 도구가 재는 것과 못 재는 것**
   ✅ 잰다   : 등록(`D`)이 자리에 붙는가 · `map`/`state` 가 모듈 키를 실어 오는가
              · `value`/`known`/`completion` 이 **실제로 무엇으로 오는가**
   🔴 못 잰다 : 타이밍·반이중 UART·`SEND OK` 지연 — **전부 실물에서만 난다**
              **여기서 "정상"이 나와도 장치가 정상이라는 뜻이 아니다.**

사용:
    python3 net/mock_node.py --port 10091 --devid P1 --seconds 120
"""
import argparse
import socket
import sys
import time

SLOTS = ["A1", "A2", "A3", "A4", "A5", "B1", "B2", "B3", "B4", "B5"]


def cksum(prefix):
    """전선 체크섬 — 서버 `cksum()` 과 **같은 규칙**이어야 한다.

    🔑 다시 구현하는 것이 아니라 **같은 정의를 옮긴 것**이고, 갈리면 서버가
    `체크섬 불량`으로 세므로 **조용히 틀리지는 않는다**(그 자리가 감시다).
    """
    x = 0
    for ch in prefix:
        x ^= ord(ch)
    return "%02X" % x


def line(body):
    return body + cksum(body) + "\n"


def bits_to_hex(bits, n):
    """자리 비트열 → hex. **슬롯 i 는 비트 (n−1−i)** · `ceil(n/4)` 고정폭 · 대문자.

    🔴 명세 §5 의 넷(대소문자·고정폭·비트순서·패딩 0)을 그대로 지킨다.
    비트 순서가 뒤집혀도 **길이도 체크섬도 통과하고 값만 틀린다** — 그래서 여기 적어 둔다.
    """
    v = 0
    for i in range(n):
        if bits[i]:
            v |= 1 << (n - 1 - i)
    return ("%0*X" % ((n + 3) // 4, v))


def main():
    ap = argparse.ArgumentParser(description="모의 노드 (시험 전용)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True, help="시험 인스턴스의 아두이노 포트")
    ap.add_argument("--devid", default="P1")
    ap.add_argument("--drain", type=int, default=7, help="선언할 슬롯당 ACK 배출 하한")
    ap.add_argument("--slot-ms", type=int, default=1200, help="S 주기(ms)")
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--occupied", default="", help="점유로 둘 자리 목록(쉼표) 예: A1,B2")
    a = ap.parse_args()

    occ_set = set(x.strip() for x in a.occupied.split(",") if x.strip())
    n = len(SLOTS)
    s = socket.create_connection((a.host, a.port), timeout=5)
    print("[mock] 접속 %s:%d · devid=%s · n=%d" % (a.host, a.port, a.devid, n), flush=True)

    seq = 0
    uptime = 1
    t_end = time.time() + a.seconds
    sent_reg = False
    try:
        while time.time() < t_end:
            occ = [1 if sl in occ_set else 0 for sl in SLOTS]
            res = [0] * n
            body = "S,%d,%s,%s,%d,%s," % (
                seq, bits_to_hex(occ, n), bits_to_hex(res, n), uptime, a.devid)
            s.sendall(line(body).encode())
            if seq == 0:
                print("[mock] S 보냄 — %s" % line(body).strip(), flush=True)

            # 🔑 **둘째 슬롯부터 `D`**(명세 §5). 첫 슬롯은 `S` 만 — 그것이 승격을 만든다.
            if not sent_reg and seq == 1:
                pkt = line("D,*,%d,%d," % (a.drain, n))
                for sl in SLOTS:
                    pkt += line("D,%s,IP," % sl)
                s.sendall(pkt.encode())
                sent_reg = True
                print("[mock] 등록 %d줄 · %dB 보냄" % (n + 1, len(pkt)), flush=True)

            # 서버가 보내는 것(하행·`Q`)을 비운다 — 안 비우면 버퍼가 차서 서버 송신이 막힌다
            s.setblocking(False)
            try:
                while True:
                    d = s.recv(4096)
                    if not d:
                        print("[mock] 서버가 닫았다", flush=True)
                        return 0
                    for ln in d.decode("utf-8", "replace").splitlines():
                        if ln.startswith("Q,"):
                            # 🔴 `Q` 는 "등록을 다시 보내라"다. 상한을 두지 않는다 —
                            #    비용이 서버 창에 있으므로 상한은 서버가 갖는다(명세 §5).
                            pkt = line("D,*,%d,%d," % (a.drain, n))
                            for sl in SLOTS:
                                pkt += line("D,%s,IP," % sl)
                            s.sendall(pkt.encode())
                            print("[mock] Q 받음 → 등록 재전송", flush=True)
            except (BlockingIOError, socket.error):
                pass
            s.setblocking(True)

            seq = (seq + 1) & 0xFFFF
            uptime += 1
            time.sleep(a.slot_ms / 1000.0)
    finally:
        s.close()
        print("[mock] 종료 · seq=%d" % seq, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
