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

사용: python3 monitor/serial_tap.py --port /dev/cu.usbmodem1101 --baud 115200 \
          --out monitor/serial-esplink.log
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from datetime import datetime

import serial


# 포트 복구 재시도 한도. 무한 재시도는 금지다 — 아래 예외 처리의 주석 참조.
MAX_RETRIES = 5
RETRY_WAIT = 5


def stamp() -> str:
    return datetime.now().strftime("%H:%M:%S")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default="monitor/serial-esplink.log")
    args = ap.parse_args()

    # 🔴 출력 디렉터리가 실제로 있는지 먼저 본다.
    #    2026-08-16 에 monitor/ 가 삭제된 채로 이 프로세스만 살아남아,
    #    아무도 열 수 없는 unlink 된 fd 에 113KB 를 쓰다가 통째로 잃었다.
    outdir = os.path.dirname(os.path.abspath(args.out))
    if not os.path.isdir(outdir):
        print(f"🔴 출력 디렉터리가 없다: {outdir} — 만들고 다시 실행해라.", file=sys.stderr)
        return 2

    raw_path = args.out + ".raw"
    txt = open(args.out, "ab", buffering=0)
    raw = open(raw_path, "ab", buffering=0)

    def note(msg: str) -> None:
        line = f"{stamp()}  «tap» {msg}\n".encode("utf-8")
        txt.write(line)

    # ── 사람이 보이도록 표식을 남긴다 ────────────────────────────────
    # 2026-08-16 오염의 절반은 "포트를 누가 잡고 있는지 사용자가 몰랐던 것"이다.
    # 사용자는 원인을 모른 채 서버·보드를 여러 번 재기동했다.
    marker = os.path.join(outdir, "PORT-IN-USE.txt")

    def write_marker() -> None:
        with open(marker, "w", encoding="utf-8") as f:
            f.write(
                f"이 파일이 있으면 monitor 의 시리얼 탭이 포트를 잡고 있다.\n\n"
                f"  포트   {args.port}\n"
                f"  pid    {os.getpid()}\n"
                f"  시작   {datetime.now().isoformat(sep=' ', timespec='seconds')}\n"
                f"  기록   {args.out}\n\n"
                f"보드를 직접 쓰려면 이것부터 내려라:\n"
                f"  kill {os.getpid()}\n\n"
                f"⚠ 내리면 포트가 즉시 풀린다. 탭은 포트를 다시 뺏지 않는다.\n"
                f"⚠ 다시 열 때 DTR 로 보드가 한 번 리셋된다 —\n"
                f"   arduino/INTERVENTIONS.md 에 그 시각을 적어야 자발 재부팅으로 오분류되지 않는다.\n"
            )

    def clear_marker() -> None:
        try:
            os.unlink(marker)
        except FileNotFoundError:
            pass

    write_marker()
    print(f"«tap» 포트 {args.port} 를 잡았다 · pid {os.getpid()} · 표식 {marker}", flush=True)

    note(f"캡처 시작 — port={args.port} baud={args.baud} pid={os.getpid()} "
         f"date={datetime.now().isoformat(sep=' ', timespec='seconds')}")
    note("⚠ 이 시각에 DTR 로 보드가 리셋된다 — 분석에서 '우리가 만든 리셋'으로 표시할 것")

    # 🔴 2026-08-17 00:2x 추가 — `kill` 로 내리면 정리 경로를 안 탔다 (실측으로 밟았다)
    #
    #   창2 를 닫으려고 `kill <pid>` 했더니 프로세스는 죽었는데 **`PORT-IN-USE.txt` 가 남았다.**
    #   그 표식은 *"탭이 포트를 잡고 있다"* 는 뜻이라, 남아 있으면 다음 사람이
    #   **포트가 묶여 있다고 오독한다.** 표식은 2026-08-16 오염("누가 포트를 잡았는지 몰랐다")
    #   때문에 만든 것인데, **거짓 양성을 내면 원래 목적을 정확히 반대로 배신한다.**
    #
    #   그리고 로그가 **자기 끝을 스스로 선언하지 않았다.** 원장 6.1 이 서버 로그에 대해
    #   적어 둔 것과 같은 문제다 — *경계를 사람의 선언에 기대면 안 된다.* 내 로그도 같다.
    #   끝 줄이 없으면 "여기서 끝난 것"과 "여기서 죽은 것"이 구분되지 않는다.
    #
    #   ⚠ `kill -9`(SIGKILL)는 잡을 수 없다. **그때는 표식이 남는 것이 정상이다** —
    #     그러니 표식을 봤으면 항상 `lsof <포트>` 로 다시 확인해라. 표식만 믿지 마라.
    import signal

    def _bye(signum, _frame):
        name = signal.Signals(signum).name
        note(f"캡처 종료 — {name} 수신. 이 줄이 이 로그의 마지막 경계다.")
        clear_marker()
        txt.close()
        raw.close()
        os._exit(0)

    for _sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(_sig, _bye)

    buf = bytearray()
    retries = 0
    while True:
        try:
            with serial.Serial(args.port, args.baud, timeout=1) as ser:
                note(f"포트 열림 (dsrdtr={ser.dsrdtr})")
                retries = 0
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
            clear_marker()
            return 0
        except Exception as e:
            msg = f"{type(e).__name__}: {e}"
            low = str(e).lower()

            # 🔴 포트는 사람이 우선이다 — 여기서 절대 싸우지 않는다.
            #
            # 2026-08-16 오염의 기계적 원인이 바로 이 자리였다. 옛 코드는 어떤 오류든
            # **3초마다 무한 재시도**했다. 그래서 사용자가 보드를 쓰려고 포트를 열 때마다
            # 탭이 3초 안에 도로 뺏어 갔고, 사용자는 원인을 모른 채 재기동을 반복했다.
            # 그 과정에서 보드가 여러 번 뽑혔다 끼워져 관측 4시간분이 오염됐다.
            #
            # 다른 프로세스가 포트를 쥐고 있다는 신호면 **양보하고 물러난다.**
            if any(k in low for k in ("resource busy", "permission denied", "in use", "ebusy")):
                note(f"포트를 다른 쪽이 쓰고 있다({msg}) — 사람이 우선이므로 탭을 내린다.")
                print(f"«tap» 포트를 양보하고 종료한다: {msg}", file=sys.stderr, flush=True)
                clear_marker()
                return 0

            # 장치가 사라진 경우(뽑힘·재부팅)는 잠깐 기다렸다 다시 본다.
            # 다만 **무한정 기다리지 않는다** — 사람이 보드를 가져간 것일 수 있다.
            retries += 1
            if retries > MAX_RETRIES:
                note(f"포트 복구 실패 {retries}회({msg}) — 매달리지 않고 종료한다.")
                print(f"«tap» {MAX_RETRIES}회 재시도 후 포기: {msg}", file=sys.stderr, flush=True)
                clear_marker()
                return 1
            note(f"포트 오류: {msg} — {RETRY_WAIT}초 후 재시도 ({retries}/{MAX_RETRIES})")
            time.sleep(RETRY_WAIT)


if __name__ == "__main__":
    raise SystemExit(main())
