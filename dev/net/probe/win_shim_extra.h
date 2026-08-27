// net/probe/win_shim_extra.h — 🔴 **cpp 의 윈도우 파싱 shim 에 없는 것만** 채운다.
// ═══════════════════════════════════════════════════════════════════════════
// 🔑 **왜 shim 을 안 고치고 여기 두나**
//   `cpp/winparse/shim/**` 는 cpp-engineer 소유다. 내가 못 고친다.
//   그리고 고칠 일도 아니다 — 이건 **내 트리(`서머리/server`)가 새로 쓰기 시작한 것**이라
//   shim 이 몰랐던 것뿐이다. 필요한 쪽이 채우는 것이 맞다.
//
// ⚠ **이것은 "MSVC 에서 된다" 의 증거가 아니다.** 선언만 맞춰 파서를 통과시키는 것이고,
//   실제 시그니처·거동은 MSVC 문서와 대야 한다.
//
// 쓰기 :  c++ -std=c++20 -fsyntax-only -Wall -Wextra \
//           -I cpp/winparse/shim -I 서머리/server \
//           -Wno-macro-redefined -Wno-unknown-pragmas \
//           -include net/probe/win_shim_extra.h \
//           cpp/winparse/win_parse.cpp
//
// 🔴 **`-include` 는 `_WIN32` 가 켜지기 전에 들어간다.** 그래서 여기서는
//   `#ifdef _WIN32` 로 감싸도 소용없다 — 조건 없이 선언한다.
//   맥에서 이 이름들은 어차피 아무도 안 부르므로 해가 없다.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

// `<stdlib.h>` (MSVC) — 상대 경로를 절대 경로로. `realpath` 의 윈도우 짝이다.
//   🔑 인자 순서가 `realpath` 와 **반대**다 : `_fullpath(버퍼, 경로, 크기)`
extern "C" char* _fullpath(char* absPath, const char* relPath, __SIZE_TYPE__ maxLength);
