#!/usr/bin/env python3
"""fdtest 상대 — 맥에서 돈다. 장치와 **동시에** 흘려보내고 **각자** 유실을 센다.

왜 있나 (2026-08-17):
  `SoftwareSerial` 은 송신 중 `cli()` 로 인터럽트를 끄고 수신은 그 인터럽트로만 받는다.
  → 송신 중 수신이 원리적으로 불가능하다. A 창에서 그 지문이 12,920건 찍혔다.
  이 시험은 **하드웨어 UART 가 그 제약에서 자유로운가**만 묻는다.

⚠ 이 스크립트는 **포트를 연다. 곧 보드를 리셋한다**(DTR).
  개입이므로 `arduino/INTERVENTIONS.md` 에 먼저 적고 monitor 에 알린 뒤에 돌려라.

쓰는 법:
    python3 arduino/fdtest/peer.py --port /dev/cu.usbmodem21201 --secs 60
"""
import argparse, sys, time

def xsum(b: bytes) -> int:
    c = 0
    for x in b:
        c ^= x
    return c

PAD = b"ABCDEFGHIJKLMNOPQRSTUVWX"          # 장치 쪽과 같은 24자

def make_line(seq: int) -> bytes:
    body = b"M,%d,%s," % (seq, PAD)
    return body + b"%02X" % xsum(body) + b"\n"

# ── 슬롯 ────────────────────────────────────────────────────────────────────
# 🔑 위상을 **추정하지 않는다.** 장치가 각 슬롯의 첫 프레임에 `1` 을 실어 알려 준다.
#    그것을 받은 시각이 슬롯 시작이고, 내 창은 +600ms ~ +1200ms 다.
#    → "1초 자로 113ms 를 겨냥할 수 없다"던 문제가 설계에서 사라진다.
SLOT_MS   = 1.200
TX_WIN_S  = 0.600      # 장치 차례
MY_WIN_S  = 0.600      # 내 차례 (0.6 ~ 1.2)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--secs", type=int, default=60)
    a = ap.parse_args()

    try:
        import serial                        # pyserial
    except ImportError:
        print("pyserial 이 없다: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    # ⚠ 포트를 여는 순간 DTR 로 보드가 리셋된다. 부팅을 기다린 뒤에 센다.
    ser = serial.Serial(a.port, a.baud, timeout=0)
    time.sleep(2.0)
    ser.reset_input_buffer()

    tx = rx = gaps = bad = 0
    nxt = None
    seq = 0
    buf = b""
    dev_report = b""
    t0 = time.time()
    last = t0

    slot_start = None      # 장치가 알려 준 슬롯 시작 시각
    deferred = 0           # 내 창이 아니어서 못 보낸 루프 수
    qmax = 0               # 큐 최고 수위
    pend = 0               # 지금 밀려 있는 수
    EVENT_S = 0.100        # 장치 쪽 EVENT_MS 와 같다 — 슬롯당 12건
    last_event = time.time()

    while time.time() - t0 < a.secs:
        now = time.time()

        # ── 내 창일 때만 보낸다 (슬롯 +0.6 ~ +1.2) ────────────────────────
        # ⚠ 1판은 쉬지 않고 밀어서 회선 상한을 넘겼다. 그건 "얼마나 빨리 넘치나"를 잰 것이다.
        #   2판은 **내 차례에만** 보낸다. 차례가 아니면 버리지 않고 담는다(pend).
        # ⚠ 3판 — 이벤트를 **시간 기준**으로 만든다. 2판은 루프 반복마다 `pend` 를 올려
        #   수백만 번 돌았고, 그 수는 아무 뜻이 없었다(`defer=1060` 은 루프 횟수였다).
        if now - last_event >= EVENT_S:
            last_event = now
            pend += 1
            qmax = max(qmax, pend)

        mine = slot_start is not None and (TX_WIN_S <= (now - slot_start) < SLOT_MS)
        if mine and pend:
            pend -= 1
            ser.write(make_line(seq)); seq += 1; tx += 1
        elif pend:
            deferred += 1          # 내 차례가 아니라 못 보내고 있다
        else:
            time.sleep(0.002)      # 보낼 것도 없다 — CPU 를 태우지 않는다(읽기는 아래에서 계속)

        # ── 받는다 (창과 무관하게 항상) ───────────────────────────────────
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
            while b"\n" in buf:
                one, buf = buf.split(b"\n", 1)
                one = one.strip(b"\r")
                if not one:
                    continue
                if one.startswith(b"["):          # 장치의 보고 줄 — 자료로 세지 않는다
                    dev_report = one
                    continue
                if not one.startswith(b"U,"):
                    bad += 1
                    continue
                try:
                    body, ck = one[:-2], one[-2:]
                    if xsum(body) != int(ck, 16):
                        bad += 1
                        continue
                    parts = one.split(b",")
                    n = int(parts[1])
                    # 🔑 슬롯 시작 표지 — **위상을 추정하지 않고 장치가 알려 준다**
                    if parts[2] == b"1":
                        slot_start = time.time()
                except Exception:
                    bad += 1
                    continue
                rx += 1
                if nxt is None:
                    nxt = n + 1
                else:
                    if n != nxt:
                        gaps += (n - nxt) if n > nxt else 1
                    nxt = n + 1

        now = time.time()
        if now - last >= 2.0:
            last = now
            el = max(now - t0, 1e-9)
            print(f"[PEER] up={int(el):3d} tx={tx} rx={rx} gap={gaps} bad={bad} "
                  f"defer={deferred} qmax={qmax} txps={int(tx/el)} rxps={int(rx/el)}")
            if dev_report:
                print("       " + dev_report.decode("ascii", "replace"))

    ser.close()
    el = max(time.time() - t0, 1e-9)
    print("\n=== 상대(맥) 최종 ===")
    print(f"보냄 {tx} · 받음 {rx} · 유실(번호 건너뜀) {gaps} · 손상(체크섬) {bad}")
    print(f"초당 보냄 {tx/el:.0f} · 초당 받음 {rx/el:.0f}")
    print("\n⚠ 판정은 **양쪽 보고를 같이** 봐야 한다 — 한쪽만 보면 어느 방향이 잃었는지 모른다.")
    print("⚠ 그리고 이 값을 A 창의 12,920건과 **나란히 놓지 마라** — 상대·부하·보율이 다르다.")
    print("   묻는 것은 손실률 비교가 아니라 **구조적으로 되는가/안 되는가** 하나다.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
