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

ALPHA = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"

def make_pad(n: int) -> bytes:
    """길이 n 의 채움. 장치는 PAD 내용을 검사하지 않는다(체크섬만 본다)."""
    if n <= 0:
        return b""
    return (ALPHA * ((n // len(ALPHA)) + 1))[:n]

def make_line(seq: int, pad: bytes) -> bytes:
    body = b"M,%d,%s," % (seq, pad)
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
    # ── 4판 (REQ-0157) ───────────────────────────────────────────────────────
    # --pad    : 프레임 크기를 바꾼다. **건수 상한이 크기에 따라 변하는 것**을 보이기 위해서다.
    # --budget : 내 창(0.6s)에 쓸 **바이트 예산**. 기본 576 = 0.6s × 960B/s = 회선 상한.
    #   ⚠ 3판은 100ms 이벤트로 창당 약 12건만 보냈다 — 회선을 안 채웠다.
    #     그 상태의 `rxmax` 는 장치의 상한이 아니라 **내가 보낸 양**이다.
    #     상한을 재려면 **내 창을 내가 꽉 채워야 한다.**
    #   ⚠ 1판의 실수와 다르다: 1판은 **양방향이 동시에** 밀어 합계가 회선 상한을 넘었다.
    #     슬롯 구조는 한 번에 한 방향만 쓰므로, 자기 창을 채우는 것은 정의상 옳다.
    ap.add_argument("--pad", type=int, default=24)
    ap.add_argument("--budget", type=int, default=576)
    a = ap.parse_args()
    pad = make_pad(a.pad)
    frame_len = len(make_line(999, pad))     # 대표값(seq 3자리). 실제로는 자릿수에 따라 ±1

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
    in_window = False      # 지금 내 창 안인가 (창 진입 순간에 예산을 다시 채운다)
    budget = 0             # 이 창에 남은 바이트 예산
    windows = 0            # 내 창을 몇 번 채웠나
    txb = 0                # 실제로 쓴 총 바이트
    starved = 0            # 예산을 다 쓰고 창이 남은 횟수 (= 회선이 병목이 아니었다는 표지)

    while time.time() - t0 < a.secs:
        now = time.time()

        # ── 내 창일 때만 보낸다 (슬롯 +0.6 ~ +1.2) ────────────────────────
        # 🔴 4판 — **예산을 꽉 채운다.** 3판은 100ms 이벤트로 창당 약 12건만 보내서
        #   회선을 안 채웠다. 그러면 장치의 `rxmax` 는 장치의 상한이 아니라 **내가 보낸 양**이다.
        #   상한을 재려면 분모(회선을 채운 창)를 내가 만들어야 한다.
        # ⚠ 1판과 다르다: 1판은 **양방향이 동시에** 밀어 합계가 회선 상한을 넘겼다.
        #   슬롯은 한 번에 한 방향만 쓰므로 **자기 창을 채우는 것은 정의상 옳다.**
        mine = slot_start is not None and (TX_WIN_S <= (now - slot_start) < SLOT_MS)
        if mine:
            if not in_window:              # 창에 막 들어왔다 — 예산을 새로 채운다
                in_window = True
                budget = a.budget
                windows += 1
            line = make_line(seq, pad)
            if budget >= len(line):
                ser.write(line)
                budget -= len(line); txb += len(line); seq += 1; tx += 1
            else:
                starved += 1               # 예산 소진 · 창은 아직 남았다
                time.sleep(0.002)
        else:
            in_window = False
            time.sleep(0.002)              # 내 차례가 아니다 — 한 바이트도 쓰지 않는다

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
            perw = (txb / windows) if windows else 0.0
            print(f"[PEER] up={int(el):3d} tx={tx} rx={rx} gap={gaps} bad={bad} "
                  f"win={windows} txb/win={perw:.0f} starve={starved} "
                  f"txps={int(tx/el)} rxps={int(rx/el)}")
            if dev_report:
                print("       " + dev_report.decode("ascii", "replace"))

    ser.close()
    el = max(time.time() - t0, 1e-9)
    print("\n=== 상대(맥) 최종 ===")
    print(f"프레임 크기 {frame_len}B (pad={a.pad}) · 창당 예산 {a.budget}B")
    print(f"보냄 {tx} · 받음 {rx} · 유실(번호 건너뜀) {gaps} · 손상(체크섬) {bad}")
    print(f"내 창 {windows}회 · 창당 실제 송신 {(txb/windows if windows else 0):.0f}B "
          f"· 예산 소진 후 대기 {starved}")
    print(f"초당 보냄 {tx/el:.0f} · 초당 받음 {rx/el:.0f}")
    print("\n⚠ **장치의 `rxmaxb` 와 위 '창당 실제 송신'을 나란히 봐라.**")
    print("   둘이 가까우면 장치가 다 꺼낸 것이고, 장치 쪽이 작으면 그 차이가 창 경계로 샌 것이다.")
    print("   (내 창은 장치 프레임 도착으로 잡으므로 전파 지연만큼 장치 창보다 늦다 — 계통 오차다)")
    print("\n⚠ 판정은 **양쪽 보고를 같이** 봐야 한다 — 한쪽만 보면 어느 방향이 잃었는지 모른다.")
    print("⚠ 그리고 이 값을 A 창의 12,920건과 **나란히 놓지 마라** — 상대·부하·보율이 다르다.")
    print("   묻는 것은 손실률 비교가 아니라 **구조적으로 되는가/안 되는가** 하나다.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
