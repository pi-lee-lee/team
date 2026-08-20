#!/bin/sh
# net/dev_envelope_hash.sh — dev_server 의 **봉투 지문**을 뜬다
# ═══════════════════════════════════════════════════════════════════════════
# 🔴 **h/cpp 분리의 판별자다.** 산출물 바이트 대조는 못 쓴다 —
#   번역 단위가 갈리면 인라인 경계가 바뀌어 **아무것도 안 고쳐도 다르다**
#   (§"산출물 대조는 인터페이스가 그대로일 때만 판별자다").
#   대신 **밖으로 나가는 것**을 잰다: `map` · `state` · `snapshot` 과 기동 로그.
#
# ⚠ **정규화가 이 도구의 전부다.** 매 기동 달라지는 값을 안 지우면
#   **항상 다르게 나오고, 항상 다른 검사는 아무 말도 안 하는 검사다.**
#   지우는 것 : srv_id · epoch/ts_ms · pid · uptime · seq · rid · last_frame_ts · 시각
#
#   쓰기 : sh net/dev_envelope_hash.sh <서버바이너리>
#   ⚠ dev 기본 포트(9900·9991·5500)를 쓴다. **운영은 9990·8888·8911 이라 안 겹친다**
# ═══════════════════════════════════════════════════════════════════════════
set -u
BIN="${1:?사용법: sh net/dev_envelope_hash.sh <서버바이너리>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"
trap 'rm -rf "$W"; kill $SRV $MOCK 2>/dev/null' EXIT

"$BIN" > "$W/boot.txt" 2>&1 &
SRV=$!
sleep 2
python3 "$ROOT/net/mock_node.py" --port 9991 --devid P1 \
        --modules "A1:IP,B1:IP,LD:OG,LC:OL,DR:OB,L2:OL" --seconds 25 > "$W/mock.txt" 2>&1 &
MOCK=$!
sleep 6
python3 "$ROOT/net/ws_probe.py" --port 9900 --listen 4 --raw > "$W/ws.txt" 2>&1

# ── 봉투만 뽑아 정규화 ───────────────────────────────────────────────────
python3 - "$W" <<'PY'
import sys, re, io, hashlib
w = sys.argv[1]
txt = io.open(w + "/ws.txt", encoding="utf-8", errors="replace").read()
env = re.findall(r'\{"type":"(?:map|state)".*', txt)
# 🔴 **`snapshot` 은 원문이 안 잡힌다** — `ws_probe` 가 그것만 **가공해서** 찍는다
#   (`device={...}` · `slots =A1:00`). 그래서 그 *가공된 줄* 을 대신 넣는다.
#   ⚠ **원문보다 약한 대리 지표다.** 그 사실을 여기 적어 둔다 —
#     안 적으면 다음 사람이 "봉투 셋을 다 덮는다"로 읽는다.
env += re.findall(r'^\s*device=\{.*$', txt, re.M)
env += re.findall(r'^\s*slots\s*=.*$', txt, re.M)
# 🔑 **기동마다 달라지는 값을 지운다.** 안 지우면 늘 다르고, 늘 다른 검사는 무의미하다.
# ⚠ **공백을 허용해야 한다** — `ws_probe` 가 가공해 찍는 줄은 `"uptime": 5` 처럼 **콜론 뒤에 공백**이
#   있는 파이썬 `json.dumps` 형식이다. 공백 없는 패턴만 쓰면 **그 줄이 정규화를 통째로 빠져나가고**
#   매 기동 달라진다. 🔴 처음에 그렇게 짜서 **아무것도 안 바꾸고 두 번 돌렸는데 해시가 달랐다.**
subs = [
    (r'"srv_id":\s*"[^"]*"',        '"srv_id":"·"'),
    (r'"epoch":\s*\d+',             '"epoch":·'),
    (r'"ts_ms":\s*\d+',             '"ts_ms":·'),
    (r'"ts":\s*\d+',                '"ts":·'),
    (r'"uptime":\s*\d+',            '"uptime":·'),
    (r'"seq":\s*\d+',               '"seq":·'),
    (r'"last_frame_ts":\s*\d+',     '"last_frame_ts":·'),
    (r'"rid":\s*"[^"]*"',           '"rid":"·"'),
]
norm = []
for e in env:
    for a, b in subs:
        e = re.sub(a, b, e)
    norm.append(e)
# 🔴 **중복을 지우고 정렬한다** — 도착 순서·건수는 타이밍에 딸린 값이라 신호가 아니다.
uniq = sorted(set(norm))
body = "\n".join(uniq)
io.open(w + "/env.txt", "w", encoding="utf-8").write(body)
# 🔴 **분모가 0 이 아님을 따로 단언한다** — 비면 모든 대조가 공허하게 참이 된다
assert len(uniq) >= 3, "🔴 봉투가 %d종뿐이다 — 서버가 안 떴거나 장치가 안 붙었다. 이 값은 못 쓴다" % len(uniq)
print("봉투 %d종 (원본 %d건)" % (len(uniq), len(env)))
print("봉투 sha256 : " + hashlib.sha256(body.encode("utf-8")).hexdigest()[:16])

boot = io.open(w + "/boot.txt", encoding="utf-8", errors="replace").read()
# 시각과 pid 를 지운다
boot = re.sub(r'\d{4}-\d\d-\d\d \d\d:\d\d:\d\d', '·', boot)
boot = re.sub(r'pid \d+', 'pid ·', boot)
# 🔴 **임시 포트 번호**(`127.0.0.1:64888`)도 매 기동 다르다. 이것도 신호가 아니다.
boot = re.sub(r'127\.0\.0\.1:\d+', '127.0.0.1:·', boot)
io.open(w + "/boot_n.txt", "w", encoding="utf-8").write(boot)
print("기동로그 sha256 : " + hashlib.sha256(boot.encode("utf-8")).hexdigest()[:16])
print("기동로그 줄수 : %d" % len(boot.strip().split("\n")))
PY
cp "$W/env.txt" "$W/boot_n.txt" /tmp/ 2>/dev/null || true
echo "(정규화 원문은 /tmp/env.txt · /tmp/boot_n.txt 에 남겼다 — 다르면 diff 로 본다)"
