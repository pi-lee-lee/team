#!/usr/bin/env python3
"""아두이노 시리얼 캡처 — 서버 로그와 **시각 대조**가 되도록 타임스탬프를 붙인다.

`arduino-cli monitor` 를 쓰지 않는 이유:
  - stdin 이 닫히면 즉시 죽는다(루트가 실제로 0바이트 로그를 만든 함정).
  - 타임스탬프가 없다. 서버 로그(`HH:MM:SS  ...`)와 맞춰 보려면 시각이 반드시 필요하다.
    이 REQ 의 핵심 가치가 "서버가 errno=54 를 적은 그 시각에 장치엔 뭐가 찍혔나" 이므로
    타임스탬프 없는 캡처는 목적을 절반 잃는다.

출력 두 벌:
  <out>       타임스탬프 텍스트 — 서버 로그와 같은 `HH:MM:SS  본문` 형식(대조용)
  <out>.raw   원시 바이트 그대로 — 디코딩으로 잃는 증거를 남긴다(부트 ROM 잡음 등)

포트를 여는 순간 DTR 이 올라가 Uno 가 **한 번** 리셋된다. 그 시각을 헤더에 박아
분석에서 '우리가 만든 리셋'으로 표시할 수 있게 한다.

사용: python3 monitor/serial_tap.py --port /dev/cu.usbmodem21201 --baud 115200 \
          --out monitor/serial-esplink.log
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from datetime import datetime

import serial


def stamp() -> str:
    return datetime.now().strftime("%H:%M:%S")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem21201")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="monitor/serial-esplink.log")
    args = ap.parse_args()

    raw_path = args.out + ".raw"
    txt = open(args.out, "ab", buffering=0)
    raw = open(raw_path, "ab", buffering=0)

    def note(msg: str) -> None:
        line = f"{stamp()}  «tap» {msg}\n".encode("utf-8")
        txt.write(line)

    note(f"캡처 시작 — port={args.port} baud={args.baud} pid={os.getpid()} "
         f"date={datetime.now().isoformat(sep=' ', timespec='seconds')}")
    note("⚠ 이 시각에 DTR 로 보드가 리셋된다 — 분석에서 '우리가 만든 리셋'으로 표시할 것")

    buf = bytearray()
    while True:
        try:
            with serial.Serial(args.port, args.baud, timeout=1) as ser:
                note(f"포트 열림 (dsrdtr={ser.dsrdtr})")
                while True:
                    chunk = ser.read(4096)
                    if chunk:
                        raw.write(chunk)
                        buf.extend(chunk)
                        while True:
                            i = buf.find(b"\n")
                            if i < 0:
                                break
                            line = bytes(buf[:i]).rstrip(b"\r")
                            del buf[: i + 1]
                            s = line.decode("utf-8", errors="replace")
                            txt.write(f"{stamp()}  {s}\n".encode("utf-8"))
                        # LF 없이 계속 쌓이면 잘라서 흘린다(장치가 개행을 안 줄 때)
                        if len(buf) > 4096:
                            s = bytes(buf).decode("utf-8", errors="replace")
                            txt.write(f"{stamp()}  «tap» LF없이 4096B 초과 — 잘라서 기록: {s}\n".encode("utf-8"))
                            buf.clear()
        except KeyboardInterrupt:
            note("중단(KeyboardInterrupt)")
            return 0
        except Exception as e:  # 포트가 사라지거나 장치가 재부팅해도 죽지 않는다
            note(f"포트 오류: {type(e).__name__}: {e} — 3초 후 재시도")
            time.sleep(3)


if __name__ == "__main__":
    raise SystemExit(main())
