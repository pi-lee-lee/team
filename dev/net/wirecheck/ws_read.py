#!/usr/bin/env python3
# ws_read.py — 🔴 **WS 봉투를 소비자와 같은 방식으로 읽는다.**
#
# ═══════════════════════════════════════════════════════════════════════════
# 왜 도구가 따로 있나 — 2026-08-26 에 같은 자리에서 **네 번** 틀렸다:
#
#   ① 바이트에서 중괄호를 세어 잘랐다 → 🔴 **WS 프레임 헤더가 섞여** 어긋난다
#   ② 한 프레임에 봉투가 **둘 이상** 온다 → `json.loads` 가 "Extra data" 로 죽는다
#   ③ `grep` 으로 키를 찾아 "있다" 고 읽었다 → 🔴 **글자는 있는데 JSON 객체 밖**이었다
#      (`..."modules":[]}]},"link":{…}` — 파서는 못 본다. 화면이 못 읽으면 **없는 것**이다)
#   ④ 한 장을 보고 값을 냈다 → 🔴 그 `0` 이 `0/N`(정상)인지 `0/0`(못 쟀다)인지 몰랐다
#
# 🔑 그래서 이 도구는 **분모를 먼저 말한다.** 몇 초 기다렸나가 아니라 **몇 장 받았나**가
#   그 판정이 무엇을 말할 수 있는지 정한다(web §5.124 · 원장 §9.58).
#
# ⚠ 그리고 알아 둘 것: `state` 는 **주기가 없다. 사건 기반이다**(원장 §9.57).
#   `http.h` 가 WS 업그레이드 직후 한 번 보내고, 그 뒤로는 `push_snapshot()` 이
#   **장치 프레임·화면 명령** 때만 돈다. 🔴 **장치가 0대면 한 장이 전부다** —
#   더 기다려도 두 번째 장은 **원리적으로 안 온다.**
#
# 사용 : ws_read.py [포트] [초]        기본 9900 · 6초
# ═══════════════════════════════════════════════════════════════════════════
import base64, json, os, socket, sys, time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9900
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0


def read_envelopes(port, secs):
    """WS 프레임을 풀어 **봉투 목록**을 돌려준다. 텍스트 프레임만 본다."""
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.settimeout(secs)
    k = base64.b64encode(os.urandom(16)).decode()
    s.sendall(("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n" % k).encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    buf = buf.split(b"\r\n\r\n", 1)[1]
    dec = json.JSONDecoder()
    envs, frames, t0 = [], 0, time.time()
    while time.time() - t0 < secs:
        while True:
            if len(buf) < 2:
                break
            b1, ln, off = buf[0], buf[1] & 0x7F, 2
            if ln == 126:
                if len(buf) < 4: break
                ln = int.from_bytes(buf[2:4], "big"); off = 4
            elif ln == 127:
                if len(buf) < 10: break
                ln = int.from_bytes(buf[2:10], "big"); off = 10
            if len(buf) < off + ln:
                break
            payload, buf = buf[off:off + ln], buf[off + ln:]
            if (b1 & 0x0F) != 1:          # 텍스트 프레임만
                continue
            frames += 1
            txt, i = payload.decode("utf-8", "replace"), 0
            while i < len(txt):           # 🔑 한 프레임에 봉투가 둘 이상 올 수 있다
                while i < len(txt) and txt[i] != "{":
                    i += 1
                if i >= len(txt):
                    break
                try:
                    obj, i = dec.raw_decode(txt, i)
                except ValueError:
                    i += 1; continue
                if isinstance(obj, dict):
                    envs.append(obj)
        try:
            d = s.recv(1 << 20)
        except socket.timeout:
            break
        if not d:
            break
        buf += d
    s.close()
    return envs, frames


def main():
    envs, frames = read_envelopes(PORT, SECS)
    kinds = {}
    for e in envs:
        kinds[e.get("type", "?")] = kinds.get(e.get("type", "?"), 0) + 1
    # 🔴 **분모를 먼저.** 이것 없이 낸 0 은 뜻이 없다.
    print("분모 : %.0f초 · 프레임 %d장 · 봉투 %d개  %s"
          % (SECS, frames, len(envs),
             " · ".join("%s %d" % (k, v) for k, v in sorted(kinds.items()))))
    n = kinds.get("state", 0)
    if n <= 1:
        print("  ⚠ `state` **%d장** — 순간 스냅샷만 말할 수 있다. **시간 축 판정은 못 한다.**" % n)
        print("    🔑 장치가 0대면 이것이 정상이다(사건 기반 · 주기 없음). 더 기다려도 안 온다")
    st = None
    for e in envs:
        if e.get("type") == "state":
            st = e
    if not st:
        print("🔴 `state` 봉투가 없다"); return 1
    print("state 키 : %s" % list(st.keys()))
    print("link     : %s" % json.dumps(st.get("link"), ensure_ascii=False))
    z = st.get("zones", [])
    ok = sum(1 for x in z if (x.get("usable") or {}).get("ok"))
    print("자리     : %d개 · usable ok %d개" % (len(z), ok))
    return 0


if __name__ == "__main__":
    sys.exit(main())
