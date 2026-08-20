#!/bin/sh
# net/sync_check.sh — 🔴 **`lot.cpp` 가 두 트리 모두에서 컴파일되는가**
# ═══════════════════════════════════════════════════════════════════════════
#   개발자 흐름 : `dev_server` 에서 `lot.cpp` 를 완성 → **그 파일을 운영에 옮긴다**
#   → 🔑 **`lot.cpp` 가 유일한 이음매다.** 두 트리의 나머지는 갈려도 된다.
#     그래서 검사할 것은 **`parking.h` 의 공개 API 가 같은가** 하나뿐이다.
#
# 🔴 **왜 파일을 옮겨서 검사하나 — 안 옮기면 헛통과한다**
#   `#include "parking.h"` 는 **포함하는 파일의 디렉터리를 먼저** 뒤진다.
#   `server/lot.cpp` 를 제자리에서 컴파일하면 `-I dev_server` 를 줘도
#   **언제나 `server/parking.h` 를 집는다** → 두 검사가 같은 것을 재고 **늘 초록**이다.
#   ⚠ §"기대값을 피검체에서 읽으면 둘 다 비었을 때 만난다" 와 같은 부류다.
#   → **중립 디렉터리로 복사해서** `-I` 만이 유일한 경로가 되게 한다.
#
#   쓰기      : sh net/sync_check.sh
#   음성 대조 : sh net/sync_check.sh --negative   ← **빨강이 실제로 나는지 확인한다**
# ═══════════════════════════════════════════════════════════════════════════
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/조별과제샘플/server/lot.cpp"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$SRC" "$WORK/lot.cpp"

NEG=0
if [ "${1:-}" = "--negative" ]; then NEG=1; fi

rc=0
for T in "$ROOT/조별과제샘플/server" "$ROOT/조별과제샘플/dev_server"; do
  name=$(basename "$T")
  if [ ! -f "$T/parking.h" ]; then
    # 🔴 **선행 조건이 깨졌으면 판정을 내지 않는다.** 초록도 빨강도 아니다 —
    #   틀린 빨강은 사람을 엉뚱한 데로 보내고, 틀린 초록은 아무 말도 안 한다.
    echo "⚪ $name — 미측정 (parking.h 가 없다)"
    continue
  fi
  USE="$T"
  if [ "$NEG" = "1" ] && [ "$name" = "dev_server" ]; then
    # 음성 대조 : 공개 API 이름 하나를 일부러 바꿔서 **정말 빨강이 나는지** 본다.
    #   ⚠ 원본은 안 건드린다. 사본 트리를 만들어 거기서만 바꾼다.
    mkdir -p "$WORK/neg"
    cp "$T"/*.h "$WORK/neg/"
    sed 's/Spot& actuator/Spot\& actuatorX/' "$T/parking.h" > "$WORK/neg/parking.h"
    USE="$WORK/neg"
    echo "   (음성 대조: dev_server 의 actuator 를 actuatorX 로 바꿔서 잰다)"
  fi
  if c++ -std=c++11 -fsyntax-only -I "$USE" "$WORK/lot.cpp" 2>"$WORK/err"; then
    echo "✅ $name — lot.cpp 가 이 트리의 공개 API 로 컴파일된다"
    # 🔴 **dev_server 는 한 걸음 더 간다 — 링크까지 본다** (2026-08-20)
    #   `lot.cpp` 가 **진짜 번역 단위**가 됐으므로 *"공개 API 만으로 성립하나"* 를 넘어
    #   **"실제로 링크되나"** 를 잴 수 있다. 🔑 **검사의 뜻이 강해졌다.**
    #   ⚠ 운영 트리(`server/`)에는 안 한다 — 거기는 아직 `#include "lot.cpp"` 라
    #     따로 링크하면 **중복 정의가 나는 것이 정상**이다. **두 트리의 뜻이 다르다.**
    if [ "$name" = "dev_server" ] && [ "$NEG" = "0" ]; then
      if c++ -std=c++11 -w -DBUILD_ID='"sync"' -o "$WORK/link_probe" \
             "$T/server.cpp" "$T/lot.cpp" 2>"$WORK/lerr"; then
        echo "   ✅ 두 번역 단위 링크 성공 (server.cpp + lot.cpp)"
      else
        echo "   🔴 **링크가 안 된다** — Visual Studio 가 겪을 바로 그 오류다:"
        head -4 "$WORK/lerr" | sed 's/^/     /'
        rc=1
      fi
    fi
  else
    echo "🔴 $name — lot.cpp 가 **안 된다**. 공개 API 가 갈렸다:"
    head -4 "$WORK/err" | sed 's/^/     /'
    rc=1
  fi
done
exit $rc
