// `#ifdef _WIN32` 갈래를 **한 번이라도 파싱시키기 위한** 검증 전용 래퍼.
//
// 요령: 표준 라이브러리를 **_WIN32 없이 먼저** 파싱시킨 뒤에 _WIN32 를 켠다.
//       (libc++ 를 윈도우 모드로 밀어 넣으면 그쪽이 먼저 깨져 아무것도 못 본다)
//       그러면 include 가드 때문에 std 헤더는 다시 안 열리고, **프로젝트 코드만** 윈도우 갈래를 탄다.
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <csignal>

#define _WIN32 1
#include "server.cpp"
