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
if [ -n "$sketch" ]; then D=( -DSKETCH_PATH="\"$sketch\"" ); else D=(); fi

# 🔴 **정의 안 된 매크로를 `#if` 에 쓰는 것을 막는 전용 패스. 항상 돈다.**
#   왜 따로 있나: 본 빌드의 `-w` 가 `-Wundef` 를 **삼킨다.** 같은 줄에 붙이면 조용히 안 돈다.
#   ⚠ 그리고 처음엔 이것을 `if [ -n "$sketch" ]` **안** 에 넣었다 — 우리는 인자 없이 부르므로
#     **한 번도 안 돌았다.** 음성 대조로 두 번 잡았다. **검사는 실제로 도는 자리에 둬라.**
# 🔑 이 검사가 없으면: 매크로 정의를 지워도 `#if` 는 **0 으로 평가되어 코드가 조용히 빠지고**,
#   시험이 그 매크로를 스스로 정의하고 있으면 **시험만 통과하고 실기가 빈다.**
g++ -std=c++11 -fsyntax-only -Wundef -Werror=undef -I "$here" \
    ${D[@]+"${D[@]}"} "$here/stage1_test.cpp" || {
  echo "🔴 정의 안 된 매크로가 #if 에 있다 — 그 블록은 조용히 빠진다"; exit 1; }

g++ -std=c++11 -O1 -w -I "$here" ${D[@]+"${D[@]}"} -o "$out" "$here/stage1_test.cpp"

"$out"
