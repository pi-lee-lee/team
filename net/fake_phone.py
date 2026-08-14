#!/usr/bin/env python3
"""가짜 폰 — digitcam 앱을 흉내내 주차 서버(5500)로 번호판을 보낸다.

명세: docs/net/digitcam-protocol.md (§4 json · §5 plain · §3 프레이밍)

표준 라이브러리만.

    python3 net/fake_phone.py 123가4568                    # json 한 건
    python3 net/fake_phone.py 123가4568 456나7890          # 여러 건 순서대로
    python3 net/fake_phone.py --format plain 123가4568
    python3 net/fake_phone.py --escape 123가4568           # 한글을 \\uXXXX 로 보낸다
    python3 net/fake_phone.py --split 123가4568            # 한글 한복판에서 갈라 보낸다
    python3 net/fake_phone.py --repeat 3 123가4568         # 같은 번호판을 3번 (중복 처리 시험)

**이 스크립트는 앱이 아니다.** 앱이 하는 인식·게이트 판정은 안 한다.
명세 §7 대로 "전송 조건을 통과한 값"만 이미 정해진 상태로 보낸다.
"""

import argparse
import json
import socket
import sys
import time


def frame_json(plate, seq, device, escape):
    """digitcam 명세 §4 의 json 프레임."""
    obj = {
        "ts": 1755080000000 + seq * 1000,
        "value": plate,
        "conf": 0.82,
        "format": "new" if len(plate) == 8 else "old",
        "seq": seq,
        "device": device,
    }
    # ensure_ascii=True 면 한글이 \uXXXX 로 나간다 — 명세 §4.3 은 둘 다 허용하고
    # **수신 측이 둘 다 복원**해야 한다고 못 박았다. 그 경로를 시험하기 위한 옵션이다.
    return json.dumps(obj, ensure_ascii=escape, separators=(",", ":")).encode("utf-8") + b"\n"


def main():
    ap = argparse.ArgumentParser(description="digitcam 앱을 흉내내는 번호판 송신기")
    ap.add_argument("plates", nargs="+", help="보낼 번호판 (예: 123가4568)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5500)
    ap.add_argument("--format", choices=["json", "plain"], default="json")
    ap.add_argument("--device", default="SM-S911N")
    ap.add_argument("--escape", action="store_true",
                    help="한글을 \\uXXXX 로 이스케이프해 보낸다 (명세 §4.3 의 다른 표기)")
    ap.add_argument("--split", action="store_true",
                    help="한글 바이트 한복판에서 갈라 두 번에 보낸다 (§8.3 조립 시험)")
    ap.add_argument("--repeat", type=int, default=1, help="각 번호판을 N번 보낸다")
    ap.add_argument("--gap", type=float, default=1.2, help="전송 간격 초")
    a = ap.parse_args()

    s = socket.create_connection((a.host, a.port), timeout=5)
    print("접속 %s:%d · 포맷 %s" % (a.host, a.port, a.format))
    seq = 1
    try:
        for plate in a.plates:
            for _ in range(a.repeat):
                if a.format == "plain":
                    data = plate.encode("utf-8") + b"\n"
                else:
                    data = frame_json(plate, seq, a.device, a.escape)
                seq += 1

                if a.split:
                    # 한글 선두 바이트 다음에서 자른다 = 문자 한복판
                    cut = None
                    for i, b in enumerate(data):
                        if b >= 0xE0:
                            cut = i + 1
                            break
                    if cut is None:
                        cut = len(data) // 2
                    s.sendall(data[:cut])
                    print("→ %s ...앞 %dB (한글 한복판에서 절단)" % (plate, cut))
                    time.sleep(0.3)
                    s.sendall(data[cut:])
                    print("→ %s ...나머지 %dB" % (plate, len(data) - cut))
                else:
                    s.sendall(data)
                    print("→ %s" % data.decode("utf-8").rstrip())
                time.sleep(a.gap)
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
        print("종료")


if __name__ == "__main__":
    main()
