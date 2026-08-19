#!/usr/bin/env bash
# 이동 뒤 망 설정만 바꿔 굽는다.  사용법:
#   bash arduino/.burn/reflash-net.sh '<SSID>' '<비밀번호>' '<서버IP>'
#
# 🔴 **왜 스크립트인가**: 이동 직후는 사람이 기다리고, 그때 명령을 조립하면 틀린다.
#   실제로 빌드 플래그 이스케이프를 한 번 틀려 `,"8888"` 이 나갈 뻔했다(AT 가 깨진다).
#
# 🔑 **검사가 실패하면 굽지 않는다.** 굽기는 되돌릴 수 없으므로 그 순서를 지킨다:
#     소스 수정 → 빌드 → **산출물 문자열 확인** → (통과해야) 굽기 → 칩 대조 → 캡처
set -euo pipefail
[ $# -eq 3 ] || { echo "사용법: $0 '<SSID>' '<비밀번호>' '<서버IP>'"; exit 2; }
SSID="$1"; PASS="$2"; IP="$3"
cd "$(dirname "$0")/../.."
SRC=조별과제샘플/ardu; B=arduino/.burn; ST=$(date +%H%M%S)

echo "── ① 소스 수정 (Config.h)"
python3 - "$SSID" "$PASS" "$IP" <<'PY'
import re, sys
ssid, pw, ip = sys.argv[1:4]
p = '조별과제샘플/ardu/Config.h'; s = open(p, encoding='utf-8').read()
for pat, val in ((r'(#define WIFI_SSID\s+")[^"]*(")', ssid),
                 (r'(#define WIFI_PASS\s+")[^"]*(")', pw),
                 (r'(#define SERVER_IP\s+")[^"]*(")', ip)):
    s, n = re.subn(pat, lambda m: m.group(1) + val + m.group(2), s, count=1)
    assert n == 1, f'🔴 못 바꿨다: {pat}'
open(p, 'w', encoding='utf-8').write(s)
PY
grep -n '^#define WIFI_SSID\|^#define WIFI_PASS\|^#define SERVER_IP' "$SRC/Config.h"

echo "── ② 빌드 (포트 8888 은 빌드 플래그로 덮는다)"
rm -rf "$B/client"; mkdir -p "$B/client"; cp "$SRC"/*.ino "$SRC"/*.h "$B/client/"
arduino-cli compile --fqbn arduino:avr:uno \
  --build-property 'compiler.cpp.extra_flags=-DSERVER_PORT="8888"' \
  --output-dir "$B/net$ST" "$B/client" | tail -2

echo "── ③ 🔴 산출물 확인 — 여기서 막히면 안 굽는다"
AT=$(strings "$B/net$ST/client.ino.elf" | grep -a 'AT+CIPSTART' || true)
echo "   $AT"
case "$AT" in
  *"\"TCP\",\"$IP\",8888"*) echo "   ✅ IP·포트가 맞다 (포트에 따옴표 없음)";;
  *) echo "   🔴 산출물이 기대와 다르다 — **굽지 않는다**"; exit 1;;
esac
strings "$B/net$ST/client.ino.elf" | grep -aq '9991' && { echo "   🔴 9991 이 남아 있다"; exit 1; } || true

echo "── ④ 굽기";  date '+   시각 %H:%M:%S'
PORT=$(python3 "$B/port.py")
arduino-cli upload -p "$PORT" --fqbn arduino:avr:uno --input-dir "$B/net$ST" "$B/client" | tail -1

echo "── ⑤ 칩 대조"
AV=$(ls ~/Library/Arduino15/packages/arduino/tools/avrdude/*/bin/avrdude | head -1)
CF=$(ls ~/Library/Arduino15/packages/arduino/tools/avrdude/*/etc/avrdude.conf | head -1)
"$AV" -C "$CF" -c arduino -p atmega328p -P "$PORT" -b 115200 \
      -U "flash:r:$B/chip-$ST.hex:i" 2>&1 | tail -1
python3 "$B/cmp.py" "$B/chip-$ST.hex" | grep -E 'IDENTICAL|→' | head -3

echo "── ⑥ 부팅 캡처 45초"
python3 "$B/cap.py" 45 "$B/boot-$ST.txt"
grep -a 'NET\] 대상\|PARKING NODE\|등록 전송\|online\|CIPSTART' "$B/boot-$ST.txt" | head -6
echo "── 끝. 포트 반납됨. 🔑 소스가 바뀌었으니 커밋해라."
