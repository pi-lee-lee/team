#!/usr/bin/env python3
"""DigitCam 테스트 TCP 서버 — 앱이 보낸 줄을 그대로 받아 찍는다.

명세: docs/net/digitcam-protocol.md (계약 원본은 docs/digitcam-contract.md §7)

표준 라이브러리만 쓴다. 설치할 것도, 가상환경도 없다.

    python3 net/test_server.py            # 0.0.0.0:5500
    python3 net/test_server.py 6000       # 포트 변경
    python3 net/test_server.py --host 127.0.0.1 5500

## 이 서버가 지켜야 하는 것 (여기가 틀리면 앱이 억울하게 의심받는다)

TCP 는 **바이트 스트림**이다. 보낸 쪽이 `write()` 를 한 번 했다고 받는 쪽이
`recv()` 를 한 번에 받는다는 보장이 없다. 실제로 이런 일이 늘 일어난다:

  * 한 recv 에 두 줄이 붙어서 온다      →  b'1234\\n5678\\n'
  * 한 줄이 두 recv 로 쪼개져 온다      →  b'12'  그리고  b'34\\n'
  * 경계가 줄 중간에서 갈린다            →  b'1234\\n56'  그리고  b'78\\n'

그래서 **연결마다 버퍼를 두고 LF 가 나올 때까지 모았다가** 잘라야 한다.
`recv().decode().split()` 로 끝내면 세 경우 전부 틀린다.
버퍼는 반드시 연결별(local)이다 — 전역 버퍼를 쓰면 클라이언트 둘이 섞인다.

또 하나: **부분 UTF-8**. 3바이트짜리 한글/이모지가 recv 경계에서 잘릴 수 있으므로
디코딩은 바이트를 줄 단위로 자른 **뒤에** 한다. recv 결과를 바로 decode 하면 깨진다.
"""

import argparse
import datetime
import json
import socket
import sys
import threading

LF = b"\n"
# 계약 §6 의 server.port 기본값과 같아야 한다. 5000 이 아닌 이유는 macOS 의 AirPlay
# 수신기(ControlCenter)가 5000 을 점유하기 때문이다 — 앱이 거기 붙으면 '연결됨'인데
# 값은 아무 데도 도착하지 않는다(REQ-0008). 계약이 바뀌면 이 값도 같이 바꾼다.
DEFAULT_PORT = 5500
DEFAULT_MAX_LINE = 65536  # LF 없이 이만큼 쌓이면 비정상 → 연결을 끊는다

_print_lock = threading.Lock()
_stats_lock = threading.Lock()
_live = 0          # 현재 접속 수
_accepted = 0      # 누적 접속 수
_total_lines = 0   # 누적 수신 줄 수


def stamp():
    """로컬 시각 ms 까지. 앱 로그와 눈으로 대조하기 위한 것이라 사람이 읽는 형식을 쓴다."""
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def say(mark, peer, text):
    """한 줄 출력. flush 하지 않으면 파이프/리다이렉션에서 '아무것도 안 온다'로 보인다."""
    with _print_lock:
        sys.stdout.write("%s  %s %-21s %s\n" % (stamp(), mark, peer, text))
        sys.stdout.flush()


# 제어문자를 그대로 찍으면 안 된다. CR 하나가 줄 앞으로 커서를 되돌려 로그를 덮어쓰고,
# ANSI 이스케이프는 터미널을 헝클어 놓는다 — 둘 다 "값이 안 왔다"로 오진하게 만든다.
# 그 오진을 막으려고 만든 서버가 그 오진을 만들면 안 된다.
_CTRL = {i: "\\x%02x" % i for i in range(0x20)}
_CTRL[0x7F] = "\\x7f"

SHOW_LIMIT = 400  # 로그 한 줄에 찍을 최대 문자 수


def show(data, limit=SHOW_LIMIT):
    """수신 바이트를 사람이 읽는 문자열로. 디코딩 실패도 제어문자도 숨기지 않는다."""
    try:
        text = data.decode("utf-8")
        bad = ""
    except UnicodeDecodeError:
        text = data.decode("utf-8", "replace")
        bad = "   ⚠ UTF-8 아님"
    text = text.translate(_CTRL)
    if len(text) > limit:
        text = "%s…(+%d자 생략)" % (text[:limit], len(text) - limit)
    return text + bad


def unescape_hint(text):
    """`\\uXXXX` 로 이스케이프된 JSON 줄이면 사람이 읽을 값을 덧붙여 준다.

    계약 §7: json 모드에서 한글을 원문 UTF-8 로 보내든 `\\uXXXX` 로 보내든
    **수신 측이 둘 다 복원**해야 한다. 이스케이프된 줄은 로그에 `123\\uac004568` 로
    찍히는데, 사람이 보면 "한글이 깨졌다"고 오해한다. 그래서 복원한 값을 덧붙인다.

    **원문 로그를 대체하지 않는다.** 이건 편의 표시이지 수신의 본체가 아니다.
    파싱이 어떻게 실패하든 줄 기록에는 영향이 없어야 한다 — 그래서 통째로 감싼다.
    """
    if "\\u" not in text:
        return ""
    try:
        obj = json.loads(text)
        if not isinstance(obj, dict):
            return ""
        bits = [
            "%s=%s" % (k, obj[k])
            for k in ("value", "format")
            if isinstance(obj.get(k), str)
        ]
        return "   → " + " ".join(bits) if bits else ""
    except Exception:
        return ""  # 편의 기능이 로그를 망치게 두지 않는다


def handle(conn, addr):
    global _live, _total_lines
    peer = "%s:%d" % addr
    buf = bytearray()
    n_line = 0
    n_byte = 0
    reason = "클라이언트가 닫음"

    try:
        while True:
            try:
                chunk = conn.recv(4096)
            except ConnectionResetError:
                reason = "RST(연결 리셋)"
                break
            except OSError as e:
                reason = "소켓 오류: %s" % e
                break

            if not chunk:
                # EOF. half-close 도 여기로 온다(클라이언트가 shutdown(SHUT_WR)).
                break

            n_byte += len(chunk)
            buf.extend(chunk)

            # LF 가 있는 만큼 전부 잘라낸다. 한 recv 에 여러 줄이 들어 있을 수 있다.
            while True:
                i = buf.find(LF)
                if i < 0:
                    break
                raw = bytes(buf[:i])
                del buf[:i + 1]
                if raw.endswith(b"\r"):     # CRLF 로 보내는 클라이언트(telnet 등) 관용 처리
                    raw = raw[:-1]
                n_line += 1
                with _stats_lock:
                    _total_lines += 1
                text = show(raw)
                say("←", peer, "#%d  %s%s" % (n_line, text, unescape_hint(text)))

            if len(buf) > MAX_LINE:
                # LF 를 영영 안 보내는 클라이언트가 메모리를 무한히 먹는 것을 막는다.
                say("!", peer, "줄 하나가 %d 바이트를 넘었다 — LF 종단이 없다. 끊는다." % MAX_LINE)
                reason = "줄 길이 초과"
                break

        # 남은 조각을 조용히 버리면 안 된다. '보냈는데 안 찍혔다'의 원인이 바로 이것이고,
        # 그때 의심받는 건 앱이다. 종단되지 않았음을 명시해서 찍는다.
        if buf:
            say("!", peer, "미종단 잔여 %d 바이트(LF 없음): %s"
                % (len(buf), show(bytes(buf[:200]), limit=200)))

    finally:
        try:
            conn.close()
        except OSError:
            pass
        with _stats_lock:
            _live -= 1
            live = _live
        say("-", peer, "해제 — %s · 줄 %d · %d 바이트 · 동시접속 %d" % (reason, n_line, n_byte, live))


def serve(host, port):
    global _live, _accepted

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # 껐다 켜는 시험을 반복하므로 필수다. 없으면 TIME_WAIT 때문에 재기동이 EADDRINUSE 로 막힌다.
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(16)

    with _print_lock:
        print("DigitCam 테스트 서버 — %s:%d 대기 중 (Ctrl-C 로 종료)" % (host, port))
        print("포맷: 시각  방향  주소  내용     ( ← 수신 / + 접속 / - 해제 / ! 이상 )")
        if host == "0.0.0.0":
            print("폰에서 접속할 주소는 노트북의 Wi-Fi IP 다 — net/README.md 참조.")
        else:
            print("⚠ %s 에만 바인드했다. 폰에서는 접속할 수 없다(같은 기기 전용)." % host)
        print("-" * 78)
        sys.stdout.flush()

    try:
        while True:
            conn, addr = srv.accept()
            with _stats_lock:
                _live += 1
                _accepted += 1
                live, seq = _live, _accepted
            say("+", "%s:%d" % addr, "접속 #%d · 동시접속 %d" % (seq, live))
            threading.Thread(target=handle, args=(conn, addr), daemon=True).start()
    except KeyboardInterrupt:
        with _print_lock:
            print()
            print("-" * 78)
            print("종료 — 누적 접속 %d · 누적 줄 %d" % (_accepted, _total_lines))
            sys.stdout.flush()
    finally:
        srv.close()


def main():
    global MAX_LINE
    ap = argparse.ArgumentParser(
        description="DigitCam 앱이 보내는 줄 단위 TCP 메시지를 받아 출력하는 테스트 서버")
    ap.add_argument("port", nargs="?", type=int, default=DEFAULT_PORT,
                    help="수신 포트 (기본 %d)" % DEFAULT_PORT)
    ap.add_argument("--host", default="0.0.0.0",
                    help="바인드 주소 (기본 0.0.0.0 = 모든 인터페이스. 폰에서 붙으려면 이대로 둔다)")
    ap.add_argument("--max-line", type=int, default=DEFAULT_MAX_LINE,
                    help="LF 없이 허용하는 최대 바이트 (기본 %d)" % DEFAULT_MAX_LINE)
    a = ap.parse_args()
    MAX_LINE = a.max_line
    try:
        serve(a.host, a.port)
    except OSError as e:
        print("바인드 실패 %s:%d — %s" % (a.host, a.port, e), file=sys.stderr)
        sys.exit(1)


MAX_LINE = DEFAULT_MAX_LINE

if __name__ == "__main__":
    main()
