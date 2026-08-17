#!/usr/bin/env python3
"""덤프한 칩 hex 를 저장소의 빌드 산출물들과 **바이트로** 대조한다 (원장 §4.4).

⚠ Intel HEX 텍스트를 눈으로 비교하지 마라 — 레코드 배치가 달라도 내용은 같을 수 있다.
   32768B raw 로 펼친 뒤 비교한다.

⚠ 응용영역만 본다. 덤프는 부트로더(0x7E00~)를 포함하지만 컴파일 산출물은 안 그렇다.
"""
import glob
import os
import sys

FLASH = 32768
BOOT_START = 0x7E00          # Uno(optiboot) 부트로더 시작


def parse_hex(path):
    """Intel HEX → bytearray(32768), 채워지지 않은 곳은 0xFF"""
    mem = bytearray(b"\xFF" * FLASH)
    base = 0
    seen = set()
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            n, addr, rtype = raw[0], (raw[1] << 8) | raw[2], raw[3]
            data = raw[4:4 + n]
            if rtype == 0x00:
                a = base + addr
                if a + n <= FLASH:
                    mem[a:a + n] = data
                    seen.update(range(a, a + n))
            elif rtype == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif rtype == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
            elif rtype == 0x01:
                break
    return mem, seen


def main():
    chip_path = sys.argv[1]
    chip, _ = parse_hex(chip_path)

    cands = sorted(glob.glob(os.path.join(os.path.dirname(chip_path), "*", "*.ino.hex")))
    if not cands:
        print("후보 없음")
        return 1

    print(f"칩 덤프: {chip_path}")
    print(f"{'후보':<34} {'응용B':>7} {'다른바이트':>10}  판정")
    print("-" * 72)

    best = None
    for c in cands:
        cand, seen = parse_hex(c)
        # 후보가 실제로 채운 영역 = 응용영역. 그 범위에서만 비교한다.
        idx = sorted(i for i in seen if i < BOOT_START)
        if not idx:
            continue
        lo, hi = idx[0], idx[-1] + 1
        diff = sum(1 for i in range(lo, hi) if chip[i] != cand[i])
        tag = "✅ IDENTICAL" if diff == 0 else ""
        label = os.path.relpath(c, os.path.dirname(chip_path))
        print(f"{label:<34} {hi - lo:>7} {diff:>10}  {tag}")
        if best is None or diff < best[1]:
            best = (label, diff)

    print("-" * 72)
    if best and best[1] == 0:
        print(f"→ 지금 칩 = {best[0]}")
    else:
        print(f"→ 🔴 일치하는 후보가 없다. 가장 가까운 것: {best[0]} (다름 {best[1]}B)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
