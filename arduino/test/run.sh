#!/usr/bin/env bash
# client.ino 를 호스트에서 컴파일해 회귀 테스트를 돌린다.
# 보드가 없어도 로직을 실제로 실행해 볼 수 있는 유일한 수단이다.
#
#   bash arduino/test/run.sh        (실행 비트를 세우지 않았으므로 bash 로 부른다)
#
# 산출물 host_test.bin 은 이 디렉터리에 생긴다. .gitignore 는 루트 소유라 내가 못 고치므로
# 커밋 전에 지우거나 루트에게 무시 규칙 추가를 요청한다.
#
# 실기 빌드와는 완전히 별개다. arduino-cli 는 이 디렉터리를 보지 않는다.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/host_test.bin"

g++ -std=c++11 -O1 -Wall -Wextra -Wno-unused-function \
    -I "$here" \
    -o "$out" \
    "$here/host_test.cpp"

"$out"
