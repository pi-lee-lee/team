#!/bin/sh
# golden_envelope.sh — **화면 봉투 골든 대조**
#
# 🔴 왜 이 도구가 있나
#   리팩터링이 "거동을 안 바꿨다"를 증명할 때 우리는 보통 `.o` 를 대조한다.
#   ⚠ **가상 함수·새 오버로드가 생기면 `.o` 는 반드시 바뀐다** — 그러면 그 판별자를 못 쓴다.
#   §"산출물 대조는 조건부 판별자다" 의 자리이고, 그때 **판별자를 다른 축으로 옮겨야 한다.**
#
#   > **화면이 받는 봉투가 한 바이트도 안 바뀌면, 화면에서 보이는 거동은 안 바뀐 것이다.**
#
#   ⚠ **봉투가 같다고 서버 전체가 같은 것은 아니다** — 로그·계수기·타이밍은 이것이 안 본다.
#     **이 판별자가 답하는 것은 "화면이 보는 것" 하나다.** 그 범위로만 인용해라.
#
# 🔑 **스크립트로 고정한 이유**: 손으로 두 번 하면 그 사이 차이가 판정에 섞인다.
#   같은 절차를 두 번 돌려야 대조가 성립한다.
#
# 사용:
#   sh net/golden_envelope.sh /tmp/before.txt     # 고치기 **전에** 먼저 뜬다
#   ... 코드를 고치고 다시 빌드 ...
#   sh net/golden_envelope.sh /tmp/after.txt
#   shasum -a 256 /tmp/before.txt /tmp/after.txt  # 두 해시가 같아야 통과
#
# ⚠ **고치기 전에 뜨는 것이 이 절차의 절반이다.** 고친 뒤에 "전" 을 만들 방법은 없다.
#
# ⚠ 이 스크립트는 **시험 인스턴스(+300)만** 띄운다. 운영 포트를 안 잡는다.
set -e
OUT="$1"
if [ -z "$OUT" ]; then echo "사용: sh net/golden_envelope.sh <출력파일>"; exit 2; fi
cd "$(dirname "$0")/.."

BIN=조별과제샘플/server/server_test
if [ ! -x "$BIN" ]; then echo "🔴 $BIN 이 없다. 먼저 빌드해라"; exit 2; fi

# 🔑 대장을 지우고 시작한다 — **앞 회차의 상태가 남으면 두 회차가 다른 조건이 된다.**
rm -f "$HOME/parking-logs/parking-nodes.test+300.txt"
pkill -f "port-web=10290" 2>/dev/null || true
sleep 1
"$BIN" --port-web=10290 --port-ardu=9188 --port-cam=9211 >/dev/null 2>&1 &
sleep 2

# 🔴 **갈린 센서를 일부러 만든다.** A1 자리는 센서 둘(A1·B1)인데 A1 만 점유로 준다 —
#   OR 판정이 실제로 갈라지는 입력이라야 그 경로가 밟힌다.
#   ⚠ 전부 비었거나 전부 찼으면 **어떤 판정을 써도 답이 같아서 대조가 아무것도 안 잡는다.**
python3 net/mock_node.py --port 10291 --devid P1 --occupied A1,B2 --seconds 14 >/dev/null 2>&1 &
sleep 6

# `srv_id` 와 `ts_ms` 만 정규화한다 — **회차마다 반드시 다르고 거동과 무관하다.**
# ⚠ 그 외에는 아무것도 지우지 마라. 지운 만큼 판별자가 약해진다.
python3 net/ws_probe.py --port 10200 --raw --listen 4 2>&1 \
  | grep -ao '{"type":"\(map\|state\)".*' \
  | sed -e 's/"srv_id":"[^"]*"/"srv_id":"X"/g' \
        -e 's/"ts_ms":[0-9]*/"ts_ms":0/g' \
  | sort -u > "$OUT"

sleep 1
pkill -f "port-web=10290" 2>/dev/null || true
pkill -f mock_node 2>/dev/null || true

LINES=$(wc -l < "$OUT" | tr -d ' ')
# 🔴 **분모가 0 이 아님을 따로 단언한다.** 빈 파일끼리는 항상 같다 —
#   §"기대값을 피검체에서 읽으면 둘 다 비었을 때 만난다" 의 자리다.
if [ "$LINES" -lt 2 ]; then
  echo "🔴 봉투를 못 떴다(줄 ${LINES}). 서버가 안 떴거나 mock 이 안 붙었다 — **대조하지 마라**"
  exit 1
fi
echo "골든 ${LINES}줄 · $OUT"   # ⚠ ${} 로 감싼다 — $LINES줄 은 변수명으로 먹힌다
shasum -a 256 "$OUT"
