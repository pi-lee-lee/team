#!/usr/bin/env bash
# REQ-0116 1단계 검증 — 프롬프트 4상태 판정과 살아있음 불변식을 보드 없이 돌린다.
#
#   bash arduino/test/run_stage1.sh                    (현재 소스)
#   bash arduino/test/run_stage1.sh /tmp/basefw/client.ino   (다른 판본과 비교)
#
# ⚠ `-w`(경고 끔)를 쓰는 이유: 스케치를 통째로 include 하므로 AVR 용 코드에서
#   호스트 컴파일러의 경고가 대량으로 나온다. 그건 이 시험의 관심사가 아니다.
#   **실기 빌드의 경고는 `arduino-cli compile --warnings all` 로 따로 본다.**
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/stage1_test.bin"

sketch="${1:-}"
if [ -n "$sketch" ]; then
  g++ -std=c++11 -O1 -w -I "$here" -DSKETCH_PATH="\"$sketch\"" -o "$out" "$here/stage1_test.cpp"
else
  g++ -std=c++11 -O1 -w -I "$here" -o "$out" "$here/stage1_test.cpp"
fi

"$out"
