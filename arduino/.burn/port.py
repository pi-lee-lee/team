#!/usr/bin/env python3
"""아두이노 시리얼 포트를 **찾는다**. 이름을 손으로 들고 다니지 마라.

🔴 왜 있나: 케이블을 다시 꽂으면 macOS 가 **새 이름**을 준다(`…21201` → `…21301`).
   도구가 옛 이름을 하드코딩하고 있으면 **없는 포트를 열고**, 그 실패가
   *"장치가 안 붙는다"* 로 보인다. 대상이 움직이면 손으로 든 이름은 조용히 낡는다.

규칙 셋 — **셋 다 시끄럽게 실패한다:**
  · 하나면 그것을 쓴다
  · 🔴 **여럿이면 거부하고 목록을 보여 준다.** "가장 최근 것"으로 고르지 않는다 —
    틀리면 **엉뚱한 보드를 굽는다.** 되돌릴 수 없는 개입이다
  · 하나도 없으면 "케이블을 확인해라"

쓰는 법
  셸  : PORT=$(python3 arduino/.burn/port.py) || exit 1
  파이썬 : from port import find_port ;  p = find_port()
"""
import glob
import sys

PATTERN = "/dev/cu.usbmodem*"


class PortError(RuntimeError):
    pass


def list_ports(pattern=PATTERN):
    return sorted(glob.glob(pattern))


def find_port(pattern=PATTERN):
    """포트 하나를 돌려준다. 애매하면 **고르지 않고 던진다.**"""
    ports = list_ports(pattern)
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise PortError(
            f"🔴 시리얼 포트가 없다 ({pattern})\n"
            "   · USB 케이블이 꽂혀 있나\n"
            "   · 보드에 전원이 들어와 있나\n"
            "   ⚠ 이 상태에서 '장치가 안 붙는다' 로 읽지 마라. **포트가 없는 것이다**")
    raise PortError(
        "🔴 시리얼 포트가 여럿이다 — **자동으로 고르지 않는다**\n"
        + "".join(f"     {p}\n" for p in ports)
        + "   🔑 틀린 것을 고르면 **엉뚱한 보드를 굽는다.** 되돌릴 수 없다.\n"
          "   → 어느 것인지 확인하고 도구에 명시해서 넘겨라.\n"
          "   ⚠ 안 쓰는 것이 남아 있으면 그것부터 정리해라 —\n"
          "     `lsof <포트>` 로 누가 쥐고 있는지 본다. 우리 도구면 그것부터 닫아라.")


if __name__ == "__main__":
    try:
        print(find_port())
    except PortError as e:
        print(e, file=sys.stderr)
        raise SystemExit(1)
