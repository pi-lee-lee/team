#!/usr/bin/env python3
# live_probe.py — **살아 있는 서버**에 골든 표본을 실제로 밀어 넣는다.
#
# ═══════════════════════════════════════════════════════════════════════════
# 🔴 이것은 코덱 하니스가 아니다. `golden_uplink.cpp` 는 **파서 함수**를 부르고,
#   이쪽은 **소켓으로 진짜 서버**에 보낸다 — 수신 조립·세션·등록 상태가 같이 돈다.
#
# 🔑 **통과만 보이지 않는다.** 같은 도구로 셋을 보인다:
#   ① 정상 등록 + 정상 S  → 받아들인다
#   ② 체크섬 한 글자 변조  → **거절한다**
#   ③ 모르는 타입          → **버린다**
#   ★ ②③이 없으면 ①의 통과가 "검사가 아무것도 안 봤다"와 구별되지 않는다.
#
# 표본 출처 : docs/arduino/GOLDEN-uplink-2026-08-25.txt (장치가 실제로 내보내는 바이트열)
# 판정      : 이 스크립트는 **서버 로그를 읽지 않는다.** 무엇을 보냈는지만 값으로 찍고,
#             판정은 서버 stdout 과 대조해서 사람이 한다(로그가 정본이다).
# ═══════════════════════════════════════════════════════════════════════════
import socket
import sys

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9991

# P2 등록 배치 — 🔑 모듈 줄 **앞**에 LF 가 붙고 **마지막 LF 는 없다**(그 1B 가 32와 33을 가른다)
REG_P2 = "D,*,7,2,6B\nD,A4,IP,04\nD,L4,OG,18"

# 🔴🔴 **순서가 계약의 일부다** — 실측으로 배웠다(2026-08-25 첫 시도).
#   D 를 먼저 보내면 **통째로 사라진다.** 서버는 연결 직후 `device_id 대기` 상태이고
#   `D` 프레임에는 **devid 칸이 없어서**(`D,*,7,2,6B`) 그것으로는 장치를 못 정한다.
#   ✅ 실제 흐름 : 장치가 **`S` 를 먼저** 보낸다 → 서버가 devid 를 잡고 **`Q`(등록 질의)** 를 낸다
#                  → 장치가 **`D` 배치**로 답한다
#   ★ 그래서 이 프로브는 **Q 를 기다렸다가** D 를 보낸다. 안 기다리면 등록 경로가 안 돈다.
CASES = [
    # (이름, 보낼 바이트, 기대)
    ("① 정상 S (devid 를 정한다)", "S,0,0,0,0,P2,31\n",  "주차 노드 지정 — device=P2"),
    ("② 체크섬 변조 S",           "S,0,0,0,0,P2,32\n",  "🔴 거절 — 체크섬 불일치"),
    ("② 필드 부족 S",             "S,0,0,P2,31\n",      "🔴 거절 — 필드 수 부족"),
    ("③ 모르는 타입",             "K,1,2,3,FF\n",       "🔴 버린다(타입 필터)"),
    ("③ 쓰레기",                  "ZZZZZZ\n",           "🔴 버린다"),
]


# 🔴🔴 **시험 주입이라는 표시를 로그 *자체* 에 남긴다.**
#   monitor 가 파일명과 README 에 인용 금지를 달아 격리해 줬는데, ★ **읽는 사람이 파일명을 못 볼 수 있다**
#   (한 줄만 인용해서 옮기거나, 로그를 합쳐서 볼 때).
#   🔑 그래서 프로브가 **표지 프레임**을 보낸다. 서버는 그것을 모르는 타입으로 버리면서
#     **받은 원문을 그대로 로그에 찍는다**(`←ARD … (rxnl=…B)`) → 로그 안에서 구간이 보인다.
#   ⚠ 표지는 **일부러 유효 프레임이 아니다** — 서버 상태를 바꾸면 그 자체가 오염이다.
# 🔴 **표지를 연결 직후에 보내면 로그에 안 남는다** (실측 2026-08-25).
#   서버는 devid 가 정해지기 전(`device_id 대기`)에 온 프레임을 **`←ARD` 로 찍지 않는다** —
#   `D` 배치를 먼저 보냈을 때 통째로 사라진 것과 **같은 자리**다.
#   ✅ 그래서 BEGIN 표지는 **첫 `S` 바로 뒤**에 보낸다. 문구에 그 사실을 적어 둔다.
MARK_BEGIN = "#PROBE-BEGIN-TESTTRAFFIC-NOT-A-DEVICE-FROM-SESSION-START\n"
MARK_END   = "#PROBE-END-TESTTRAFFIC-NOT-A-DEVICE\n"


def main():
    print("live_probe — %s:%d" % (HOST, PORT))
    print("  등록 배치 길이 = %d B  (골든 표본은 32 B)" % len(REG_P2))
    if len(REG_P2) != 32:
        print("  🔴 표본 길이가 32 가 아니다 — 표본을 잘못 옮겼다. 중단한다")
        return 1

    s = socket.create_connection((HOST, PORT), timeout=3)
    s.settimeout(1.0)
    try:
        # ── 🔴 첫 S 를 보내고 **서버의 `Q` 를 기다린다.** 그 뒤에야 등록이 먹는다
        s.sendall(CASES[0][1].encode("ascii"))
        print("  → %-24s %-22r  기대: %s"
              % (CASES[0][0], CASES[0][1].replace("\n", "\\n"), CASES[0][2]))
        # 🔑 devid 가 정해진 **뒤**라야 로그에 남는다 — 그래서 여기서 표지를 낸다
        s.sendall(MARK_BEGIN.encode("ascii"))
        print("  → 표지 %r  (서버가 버리면서 로그에 원문을 남긴다)" % MARK_BEGIN.strip())
        got_q = False
        for _ in range(8):                      # 🔑 최대 8회만 본다 — 안 오면 안 오는 것이다
            try:
                data = s.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            print("     ← %r" % data[:120])
            if b"Q," in data:
                got_q = True
                break
        if not got_q:
            print("  🔴 `Q`(등록 질의)가 안 왔다 — 등록 경로를 시험할 수 없다. 중단")
            return 1
        s.sendall((REG_P2 + "\n").encode("ascii"))
        print("  → %-24s %d B  기대: 등록 완료 — n=2 (device=P2)"
              % ("② 등록 배치 P2", len(REG_P2)))

        for name, payload, expect in CASES[1:]:
            s.sendall(payload.encode("ascii"))
            print("  → %-24s %-22r  기대: %s"
                  % (name, payload.replace("\n", "\\n"), expect))
            # 🔑 서버는 상행에 즉답하지 않는다(하행은 자기 창에 실린다).
            #   그래서 여기서는 **읽어서 판정하지 않는다** — 판정은 서버 로그다.
            try:
                data = s.recv(4096)
                if data:
                    print("     ← %r" % data[:120])
            except socket.timeout:
                pass
        s.sendall(MARK_END.encode("ascii"))
        print("  → 표지 %r" % MARK_END.strip())
    finally:
        s.close()
    print("보냈다. **서버 stdout 과 대조해라** — 거절 줄이 안 보이면 그 검사는 없는 것이다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
