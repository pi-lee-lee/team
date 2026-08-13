#!/usr/bin/env python3
"""가짜 아두이노 — 하드웨어 없이 주차 관제 서버의 전 경로를 실증한다.

명세: docs/net/parking-protocol.md (§2 라인 문법, §4.2 멱등, §7.4 재부팅)

표준 라이브러리만 쓴다.

    python3 net/fake_arduino.py                       # 127.0.0.1:9991 에 접속, 1Hz 상태 전송
    python3 net/fake_arduino.py --host 192.168.0.5
    python3 net/fake_arduino.py --drop-rate 1.0       # ACK 를 전부 유실 → 서버 재전송 경로 발동
    python3 net/fake_arduino.py --reboot-after 12     # 12초 뒤 재부팅 흉내 → §7.4 재동기화 발동

## 이 스크립트가 흉내내는 것과 흉내내지 않는 것

흉내낸다: 라인 문법, 체크섬, rid 멱등 캐시, 1Hz 하트비트, 변화 시 즉시 전송,
          재부팅(seq/uptime/예약/캐시 초기화 + 재접속), ACK 유실.

흉내내지 **않는다**: SoftwareSerial 9600bps 의 지연과 송신 중 수신 유실(§6.3).
    여기는 루프백 TCP 라 훨씬 빠르고 안 잃는다. `--drop-rate` 는 그 유실을
    **결과만** 재현하는 장치다 — 타이밍까지 같지는 않다.
"""

import argparse
import random
import socket
import sys
import threading
import time

SLOTS = ["A1", "A2", "A3", "A4", "A5", "B1", "B2", "B3", "B4", "B5"]
IDEMPOTENT_CACHE_MAX = 8      # 명세 §4.2 — 최근 8건
MAX_LINE = 64                 # 명세 §2.1

_print_lock = threading.Lock()


def say(mark, text):
    with _print_lock:
        sys.stdout.write("%s  %s %s\n" % (time.strftime("%H:%M:%S"), mark, text))
        sys.stdout.flush()


def cksum(prefix):
    """명세 §2.2 — 첫 바이트부터 체크섬 앞 쉼표까지(그 쉼표 포함)의 XOR, 대문자 2자리 hex."""
    x = 0
    for b in prefix.encode("ascii"):
        x ^= b
    return "%02X" % x


def build(prefix):
    """'S,1,...,' 처럼 마지막 쉼표까지 만든 문자열에 체크섬과 LF 를 붙인다."""
    return prefix + cksum(prefix) + "\n"


def verify(line):
    """체크섬 검증. 통과하면 필드 리스트, 아니면 None."""
    cut = line.rfind(",")
    if cut < 0:
        return None
    if cksum(line[:cut + 1]) != line[cut + 1:]:
        return None
    return line[:cut].split(",")


class FakeArduino:
    def __init__(self, args):
        self.args = args
        self.reset_state(first=True)

    # --- 상태 -------------------------------------------------------------
    def reset_state(self, first=False):
        """재부팅. 명세 §7.4 가 감지해야 하는 것: seq 0 복귀, uptime 0 복귀, 예약 소실."""
        self.seq = 0
        self.boot_ts = time.time()
        if getattr(self.args, "start_empty", False):
            self.occupied = [0] * len(SLOTS)
        else:
            self.occupied = [random.randint(0, 1) for _ in SLOTS]
        self.reserved = [0] * len(SLOTS)      # 재부팅하면 예약이 사라진다 — 이게 §7.4 의 이유
        self.ack_cache = []                   # [(rid, slot, result)] 최근 8건
        self.sched = []                       # [(발동시각, 자리index, occupied값)] 입출차 예약
        if not first:
            say("!", "재부팅 — seq=0, uptime=0, 예약 전부 소실, 멱등 캐시 비움")

    def uptime(self):
        return int(time.time() - self.boot_ts)

    def bits(self, arr):
        return "".join(str(b) for b in arr)

    def status_line(self):
        p = "S,%d,%s,%s,%d,%s," % (
            self.seq, self.bits(self.occupied), self.bits(self.reserved),
            self.uptime(), self.args.devid)
        self.seq = (self.seq + 1) & 0xFFFF
        return build(p)

    # --- 예약 처리 --------------------------------------------------------
    def cached(self, rid):
        for r, slot, result in self.ack_cache:
            if r == rid:
                return (slot, result)
        return None

    def remember(self, rid, slot, result):
        self.ack_cache.append((rid, slot, result))
        if len(self.ack_cache) > IDEMPOTENT_CACHE_MAX:
            self.ack_cache.pop(0)

    def handle_request(self, kind, rid, slot):
        """R/C 처리. (slot, result) 반환. 명세 §2.4 의 result 값."""
        hit = self.cached(rid)
        if hit is not None:
            # 명세 §4.2 — 재적용하지 말고 같은 ACK 를 다시 보낸다
            say("=", "rid %s 재수신 → 멱등 캐시 적중, 재적용 없이 같은 ACK 재전송 %s" % (rid, hit))
            return hit

        if slot not in SLOTS:
            # 명세 §2.4(개정 2) — result=3 의 slot 은 항상 "??".
            # 받은 토큰을 되비추면 BNF 를 위반하는 줄이 나가고, 유효한 자리를 지어내면
            # 서버가 엉뚱한 자리를 갱신한다. 상관 키는 rid 이므로 정보 손실도 없다.
            self.remember(rid, "??", 3)
            return ("??", 3)
        else:
            i = SLOTS.index(slot)
            if kind == "R":
                if self.occupied[i]:
                    result = 1                    # 이미 점유
                elif self.reserved[i]:
                    result = 2                    # 이미 예약 (명세에서 내가 추가한 값)
                else:
                    self.reserved[i] = 1
                    result = 0
            else:                                  # C = 취소
                self.reserved[i] = 0
                result = 0
        # --arrive-sec 가 켜져 있으면 예약 성공 시 입차/출차를 예약해 둔다.
        # 서버의 예약 은퇴(명세 §7.5, occupied 1→0)를 실제로 발동시키기 위한 시험 장치다.
        if kind == "R" and result == 0 and self.args.arrive_sec > 0:
            t = time.time()
            self.sched.append((t + self.args.arrive_sec, i, 1))
            self.sched.append((t + self.args.arrive_sec + self.args.depart_sec, i, 0))
            say("*", "%s 입차 %.1fs 후, 출차 %.1fs 후로 예약(은퇴 시험용)"
                % (SLOTS[i], self.args.arrive_sec, self.args.arrive_sec + self.args.depart_sec))
        self.remember(rid, slot, result)
        return (slot, result)

    # --- 연결 -------------------------------------------------------------
    def run(self):
        while True:
            try:
                self.session()
            except (ConnectionRefusedError, OSError) as e:
                say("!", "접속 실패(%s) — 3초 후 재시도" % e)
                time.sleep(3)
            if not self.args.reboot_after:
                # 재부팅 옵션이 없으면 연결이 끊겼을 때만 재접속
                time.sleep(1)

    def session(self):
        s = socket.create_connection((self.args.host, self.args.port), timeout=5)
        s.settimeout(0.2)
        say("+", "서버 접속 %s:%d (uptime=%d, seq=%d)"
            % (self.args.host, self.args.port, self.uptime(), self.seq))

        buf = bytearray()
        last_beat = 0.0
        last_bits = None
        session_start = time.time()
        next_change = time.time() + random.uniform(3, 7)

        while True:
            now = time.time()

            # --- 재부팅 흉내 (§7.4 경로 발동)
            if self.args.reboot_after and now - session_start >= self.args.reboot_after:
                say("!", "--reboot-after %ds 도달 → 연결 끊고 재부팅" % self.args.reboot_after)
                s.close()
                self.reset_state()
                time.sleep(1.0)
                return

            # --- 예약해 둔 입차/출차 발동 (--arrive-sec)
            due = [x for x in self.sched if now >= x[0]]
            for t0, i, v in due:
                self.sched.remove((t0, i, v))
                if self.occupied[i] != v:
                    self.occupied[i] = v
                    say("~", "%s %s (occupied=%d)" % (SLOTS[i], "입차" if v else "출차", v))

            # --- 가상 센서: 가끔 점유 상태를 바꾼다 → 변화 시 즉시 전송
            if self.args.arrive_sec == 0 and now >= next_change:
                i = random.randrange(len(SLOTS))
                self.occupied[i] ^= 1
                say("~", "가상 센서 변화: %s occupied=%d" % (SLOTS[i], self.occupied[i]))
                next_change = now + random.uniform(3, 7)

            # --- 전송 규칙(§3.4): 변화 즉시 + 1Hz 하트비트, 타이머는 하나
            bits_now = (self.bits(self.occupied), self.bits(self.reserved))
            changed = last_bits is not None and bits_now != last_bits
            if changed or now - last_beat >= self.args.interval:
                line = self.status_line()
                try:
                    s.sendall(line.encode("ascii"))
                except OSError as e:
                    say("!", "전송 실패: %s" % e)
                    s.close()
                    return
                say("→", "%s%s" % (line.rstrip("\n"), "   (변화 감지 즉시)" if changed else ""))
                last_beat = now
                last_bits = bits_now
            if last_bits is None:
                last_bits = bits_now

            # --- 수신
            try:
                chunk = s.recv(4096)
                if not chunk:
                    say("-", "서버가 연결을 닫았다")
                    s.close()
                    return
                buf.extend(chunk)
            except socket.timeout:
                pass
            except OSError as e:
                say("!", "수신 오류: %s" % e)
                s.close()
                return

            # --- 줄 조립 (TCP 는 스트림이다 — 명세 §6.1)
            while True:
                i = buf.find(b"\n")
                if i < 0:
                    break
                raw = bytes(buf[:i])
                del buf[:i + 1]
                if raw.endswith(b"\r"):
                    raw = raw[:-1]
                if len(raw) + 1 > MAX_LINE:
                    say("!", "64바이트 초과 줄 — 버림")
                    continue
                self.on_line(s, raw.decode("ascii", "replace"))

            if len(buf) > MAX_LINE:
                say("!", "LF 없이 %d 바이트 — 버퍼 비움" % len(buf))
                buf.clear()

    def on_line(self, s, line):
        say("←", line)
        f = verify(line)
        if f is None:
            say("!", "체크섬 불일치 — 버림 (명세 §6.2)")
            return
        kind = f[0]
        if kind not in ("R", "C"):
            say("!", "모르는 타입 '%s' — 조용히 버림" % kind)
            return
        try:
            rid = f[1]
            slot = f[2]
        except IndexError:
            say("!", "필드 부족 — 버림")
            return

        slot, result = self.handle_request(kind, rid, slot)
        ack = build("A,%s,%s,%d," % (rid, slot, result))

        # --- ACK 유실 (§6.3 의 결과를 재현). 예약은 이미 반영된 뒤에 버린다 —
        #     그래야 서버 재전송 시 멱등 캐시가 적중하는 진짜 경로를 시험한다.
        if random.random() < self.args.drop_rate:
            say("✗", "ACK 유실시킴(--drop-rate) — 예약은 반영된 상태: %s" % ack.rstrip())
            return
        try:
            s.sendall(ack.encode("ascii"))
            say("→", ack.rstrip("\n"))
        except OSError as e:
            say("!", "ACK 전송 실패: %s" % e)


def main():
    ap = argparse.ArgumentParser(description="주차 관제 명세대로 동작하는 가짜 아두이노")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9991)
    ap.add_argument("--devid", default="P1", help="장치 ID (1~8자)")
    ap.add_argument("--interval", type=float, default=1.0, help="하트비트 주기 초 (기본 1.0 = 1Hz)")
    ap.add_argument("--drop-rate", type=float, default=0.0,
                    help="ACK 를 이 확률로 유실시킨다 (0.0~1.0). 서버 재전송 경로 시험용")
    ap.add_argument("--reboot-after", type=float, default=0,
                    help="N초 뒤 재부팅(연결 끊고 seq/uptime/예약 초기화 후 재접속). §7.4 시험용")
    ap.add_argument("--arrive-sec", type=float, default=0,
                    help="예약 성공 후 N초 뒤 입차시킨다(0=끔). 켜면 무작위 센서 변화를 멈춰 "
                         "은퇴 시험이 깨끗해진다. 명세 §7.5 시험용")
    ap.add_argument("--depart-sec", type=float, default=3.0,
                    help="입차 후 N초 뒤 출차시킨다. 이 출차(occupied 1→0)가 예약 은퇴를 발동시킨다")
    ap.add_argument("--start-empty", action="store_true",
                    help="모든 자리를 빈 상태로 시작(은퇴 시험 시 예약 가능한 자리 확보)")
    ap.add_argument("--seed", type=int, default=None, help="난수 시드(재현용)")
    a = ap.parse_args()
    if a.seed is not None:
        random.seed(a.seed)

    say("*", "가짜 아두이노 시작 — 명세 docs/net/parking-protocol.md")
    if a.drop_rate:
        say("*", "ACK 유실률 %.0f%% — 서버의 재전송을 기대한다" % (a.drop_rate * 100))
    if a.reboot_after:
        say("*", "%.0f초마다 재부팅한다" % a.reboot_after)
    try:
        FakeArduino(a).run()
    except KeyboardInterrupt:
        say("*", "종료")


if __name__ == "__main__":
    main()
