// 검증 전용 shim. 실제 windows.h 가 아니다 — `_WIN32` 갈래를 파싱시키는 것이 목적이다.
#pragma once
#include <cstdint>
typedef unsigned long DWORD;
typedef struct { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
inline int SetConsoleOutputCP(unsigned) { return 1; }
inline unsigned long long GetTickCount64() { return 0; }
inline void GetSystemTimeAsFileTime(FILETIME* f) { f->dwLowDateTime = 0; f->dwHighDateTime = 0; }
inline DWORD GetCurrentProcessId() { return 0; }
inline DWORD GetLastError() { return 0; }
#define MOVEFILE_REPLACE_EXISTING 0x1
inline int MoveFileExA(const char*, const char*, DWORD) { return 1; }
inline void Sleep(DWORD) {}

// 🔴 여기부터가 이 shim 의 핵심이다 — **진짜 <windows.h> 가 정의하는 짧은 매크로들**.
// 이것이 없으면 "파싱은 됐다"가 거짓 안심이 된다. 실제 SDK(minwindef.h/windef.h/winnt.h/wingdi.h)
// 가 정의하는 것 중 **식별자와 충돌할 수 있는 것**만 옮긴다. NOMINMAX 가 막는 min/max 는 뺐다.
// 🔴 진짜 SDK 는 이 둘을 **빈 매크로**로 정의한다(16비트 호환 잔재, windef.h/minwindef.h).
//    이것이 이 shim 의 존재 이유다 — 2026-08-25 에 이 두 줄이 실제 결함 하나를 잡았다.
//    빼고 한 번 더 돌리면 "그 뒤에 또 있나" 를 볼 수 있다.
#define far
#define near
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define ERROR 0
#define DELETE (0x00010000L)
