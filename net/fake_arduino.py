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
    """명세 §2.2 — 첫 바이트부터 체크섬 앞 쉼표까지(그 쉼표 포함)의 XOR, 대문자 2자리 hex.

    비 ASCII 가 섞이면 명세 §2.1(ASCII 전용) 위반이므로 None 을 돌려준다.
    **여기서 예외를 던지면 장치가 죽는다** — 실제로 서버가 UTF-8 번호판을 전선에 실어 보낸
    적이 있고 그때 이 스크립트가 죽었다. 진짜 아두이노라면 그냥 깨진 줄로 보고 버려야 한다.
    """
    try:
        data = prefix.encode("ascii")
    except UnicodeEncodeError:
        return None
    x = 0
    for b in data:
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
    want = cksum(line[:cut + 1])
    if want is None or want != line[cut + 1:]:
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
        # self.occupied 는 **센서(또는 시뮬)의 원래 값**이다.
        # 테스트 모드가 무장 중이면 self.override 가 칸별로 이 값을 가린다(§12A).
        if getattr(self.args, "start_empty", False):
            self.occupied = [0] * len(SLOTS)
        else:
            self.occupied = [random.randint(0, 1) for _ in SLOTS]
        # 테스트 모드는 재부팅하면 사라진다 — 예약(§7.4)과 정반대다(§12A.3).
        self.armed = False
        self.override = [None] * len(SLOTS)   # None = 주입 없음, 0/1 = 주입값
        # 실물 센서가 배정된 칸(§12B.2). 여기 든 칸은 시뮬 트리거가 건드리지 않는다.
        # 이 스크립트에 진짜 센서는 없지만, 그 규칙을 시험하려면 지정할 수 있어야 한다.
        self.real = set()
        for tok in (getattr(self.args, "real_slots", "") or "").split(","):
            tok = tok.strip().upper()
            if tok in SLOTS:
                self.real.add(SLOTS.index(tok))
        self.reserved = [0] * len(SLOTS)      # 재부팅하면 예약이 사라진다 — 이게 §7.4 의 이유
        self.ack_cache = []                   # [(rid, slot, result)] 최근 8건
        self.sched = []                       # [(발동시각, 자리index, occupied값)] 입출차 예약
        if not first:
            say("!", "재부팅 — seq=0, uptime=0, 예약 전부 소실, 멱등 캐시 비움")

    def uptime(self):
        return int(time.time() - self.boot_ts)

    def bits(self, arr):
        return "".join(str(b) for b in arr)

    def eff_occupied(self):
        """전선에 실어 보낼 occupied — 무장 중이면 주입값이 원래 값을 가린다(§12A)."""
        out = []
        for i in range(len(SLOTS)):
            if self.armed and self.override[i] is not None:
                out.append(self.override[i])
            else:
                out.append(self.occupied[i])
        return out

    def tmask(self):
        """§2.4 — 해제면 '-', 무장이면 칸별 '주입됨' 비트열."""
        if not self.armed:
            return "-"
        return "".join("1" if self.override[i] is not None else "0" for i in range(len(SLOTS)))

    def status_line(self):
        p = "S,%d,%s,%s,%d,%s" % (
            self.seq, self.bits(self.eff_occupied()), self.bits(self.reserved),
            self.uptime(), self.args.devid)
        # --legacy-frame 이면 tmask 를 빼고 옛 6필드 형태로 보낸다.
        # 수신 측이 §2.1 규칙 8(선택 필드 부재 허용)을 지키는지 시험하기 위한 것이다.
        if not self.args.legacy_frame:
            p += "," + self.tmask()
        p += ","
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

    def handle_test(self, rid, op, slot, tval):
        """§2.4 `T` 처리. (ack_slot, result) 반환.

        무장/해제는 slot 이 "??" 이고, 주입/해제는 자리 ID 다.
        멱등 캐시는 R/C 와 공유한다 — 재전송이 두 번 적용되면 안 되는 것은 여기도 같다.
        """
        hit = self.cached(rid)
        if hit is not None:
            say("=", "rid %s 재수신 → 멱등 캐시 적중, 재적용 없이 같은 ACK %s" % (rid, hit))
            return hit

        if op == "A":
            self.armed = True
            say("*", "테스트 모드 **무장**")
            res = ("??", 0)
        elif op == "D":
            n = sum(1 for v in self.override if v is not None)
            self.armed = False
            self.override = [None] * len(SLOTS)     # 해제하면 전 칸이 한 번에 원래 소스로(§12A.2)
            say("*", "테스트 모드 **해제** — 오버라이드 %d칸 소멸" % n)
            res = ("??", 0)
        elif op in ("S", "X"):
            if slot not in SLOTS:
                res = ("??", 3)
            elif not self.armed:
                # 무장하지 않았으면 조용히 무시하지 않고 명시적으로 거절한다(§12A.2)
                say("!", "무장 안 된 상태에서 %s — result=4 로 거절" % op)
                res = (slot, 4)
            else:
                i = SLOTS.index(slot)
                if op == "S":
                    if tval not in ("0", "1"):
                        res = (slot, 3)
                    else:
                        self.override[i] = int(tval)
                        say("*", "%s 주입 occupied=%s (원래값 %d)" % (slot, tval, self.occupied[i]))
                        res = (slot, 0)
                else:
                    self.override[i] = None
                    say("*", "%s 오버라이드 해제 → 원래값 %d 로 복귀" % (slot, self.occupied[i]))
                    res = (slot, 0)
        else:
            res = ("??", 3)

        self.remember(rid, res[0], res[1])
        return res

    def handle_simstep(self, rid):
        """§12B — `M` 한 걸음. (ack_slot, result) 반환.

        **칸 하나만** 바꾼다(§12B.2). 여러 칸을 바꾸면 무엇이 바뀌었는지 못 따라가고,
        그게 자율 전진을 없앤 이유 자체였다.
        """
        hit = self.cached(rid)
        if hit is not None:
            # 재전송이 **두 걸음**이 되면 안 된다(§12B.4). 한 번 눌렀는데 두 칸이 바뀐다.
            say("=", "rid %s 재수신 → 멱등 캐시 적중, 재적용 없이 같은 ACK %s" % (rid, hit))
            return hit

        # 후보에서 빼는 것 둘:
        #  · 실물 센서가 배정된 칸 — 그건 진실이고 사람이 흔들 것이 아니다.
        #  · **지금 오버라이드가 먹고 있는 칸**(§12B.2 개정 6) — 고르면 시뮬 값은 바뀌는데
        #    보고되는 occupied 는 주입값에 가려 안 바뀐다. ACK 는 "바뀌었다"인데 화면은 그대로 —
        #    "눌렀다 → 저 칸이 바뀌었다"는 1:1 대응이 깨진다.
        cand = [i for i in range(len(SLOTS))
                if i not in self.real
                and not (self.armed and self.override[i] is not None)]
        if not cand:
            say("!", "바꿀 시뮬 칸이 없다 — result=5")
            res = ("??", 5)
            self.remember(rid, res[0], res[1])
            return res

        # 1순위: **예약됐지만 비어 있는 칸을 채운다**(§12B.2).
        # 이게 없으면 occupied=1 ∧ reserved=1 (이 시스템의 성공 상태, §1.1)을
        # 보려고 평균 열 번쯤 눌러야 한다 — 사실상 안 보이는 것과 같다.
        waiting = [i for i in cand if self.reserved[i] == 1 and self.occupied[i] == 0]
        if waiting:
            i = random.choice(waiting)
            self.occupied[i] = 1
            say("*", "한 걸음: %s 예약된 빈칸에 입차 (occupied=1, reserved=1)" % SLOTS[i])
        else:
            i = random.choice(cand)
            self.occupied[i] ^= 1
            say("*", "한 걸음: %s occupied=%d" % (SLOTS[i], self.occupied[i]))

        res = (SLOTS[i], 0)
        self.remember(rid, res[0], res[1])
        return res

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
        # 명세 §4.2 — **새 연결에서 멱등 캐시를 비운다.**
        # wire_rid 는 서버가 발급하고 서버가 재시작하면 1부터 다시 시작한다. 캐시를 들고 있으면
        # 새 서버의 rid 1,2,3… 이 옛 세션의 것과 충돌해 **새 명령이 "재수신"으로 삼켜지고
        # result=0(성공)으로 응답된다** — 실패가 성공으로 보이는 종류라 로그 없이는 못 찾는다.
        # 비워도 잃는 보호가 없다: §7.3 재전송은 살아 있는 한 연결 안에서만 일어나고,
        # 끊기면 §7.4 가 새 rid 로 재하달한다.
        n = len(self.ack_cache)
        self.ack_cache = []
        say("+", "서버 접속 %s:%d (uptime=%d, seq=%d) · 멱등 캐시 비움(%d건)"
            % (self.args.host, self.args.port, self.uptime(), self.seq, n))

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

            # --- 명세 §12B.1: **시뮬레이터는 스스로 전진하지 않는다.**
            # 예전에는 여기서 타이머로 칸을 뒤집었다. 그 자율 전진을 없앴다 —
            # 화면 전환이 너무 빨라 무엇이 언제 바뀌었는지 따라갈 수 없어 시험이 불가능했다.
            # 이제 값은 서버가 보낸 `M`(§12B.4)을 받을 때만 한 걸음 움직인다.
            #
            # 아래 --arrive-sec 은 **명세 밖의 시험 보조 장치**이고 기본으로 꺼져 있다.
            # 예약 은퇴(§7.5) 경로를 시간으로 재현하려고 남겨 둔 것이지 시뮬레이터가 아니다.
            if self.args.arrive_sec > 0:
                due = [x for x in self.sched if now >= x[0]]
                for t0, i, v in due:
                    self.sched.remove((t0, i, v))
                    if self.occupied[i] != v:
                        self.occupied[i] = v
                        say("~", "%s %s (occupied=%d) [--arrive-sec 시험 보조]"
                            % (SLOTS[i], "입차" if v else "출차", v))

            # --- 전송 규칙(§3.4): 변화 즉시 + 1Hz 하트비트, 타이머는 하나
            # 전선에 나갈 값 기준으로 변화를 본다 — 주입/무장도 즉시 한 프레임을 유발해야 한다.
            bits_now = (self.bits(self.eff_occupied()), self.bits(self.reserved), self.tmask())
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
                try:
                    text = raw.decode("ascii")
                except UnicodeDecodeError:
                    # 명세 §2.1: 전선은 ASCII 전용이다. 비 ASCII 는 규약 위반이므로 버린다.
                    say("!", "비 ASCII 바이트가 섞인 줄 — 버림 (%r)" % raw[:40])
                    continue
                self.on_line(s, text)

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
        if kind not in ("R", "C", "T", "M"):
            say("!", "모르는 타입 '%s' — 조용히 버림" % kind)
            return
        try:
            rid = f[1]
            if kind == "T":
                op, slot, tval = f[2], f[3], f[4]
            elif kind == "M":
                slot = None                      # M 은 rid 뿐이다(§12B.4)
            else:
                slot = f[2]
        except IndexError:
            say("!", "필드 부족 — 버림")
            return

        if kind == "T":
            slot, result = self.handle_test(rid, op, slot, tval)
        elif kind == "M":
            slot, result = self.handle_simstep(rid)
        else:
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
    ap.add_argument("--real-slots", default="",
                    help="실물 센서가 배정된 칸(쉼표 구분, 예: A1,B5). "
                         "시뮬 트리거(M)가 이 칸들을 건드리지 않는지 시험할 때 쓴다 (§12B.2)")
    ap.add_argument("--legacy-frame", action="store_true",
                    help="tmask 없이 옛 6필드 S 프레임을 보낸다. 수신 측이 선택 필드 부재를 "
                         "견디는지(명세 §2.1 규칙 8) 시험용")
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
