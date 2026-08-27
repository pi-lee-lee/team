// 검증 전용 shim — MSVC 가 없는 기기에서 `#ifdef _WIN32` 갈래를 **파싱시키기 위한** 것.
// 실제 Winsock 이 아니다. 목적은 하나: 그 블록의 이름·인자 수·타입을 컴파일러에 태우는 것.
#pragma once
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
// 🔴 SOCKET 의 **부호**를 바꿔 가며 볼 수 있게 뒀다.
//   실제 MSVC 는 `UINT_PTR` — x64 에서 **부호 없는 64비트**다. 기본(int)으로만 돌리면
//   부호 비교 축이 통째로 안 살아난다("음성 대조가 없는 통과" 와 같은 형태다).
//   -DWINPARSE_UNSIGNED_SOCKET 으로 MSVC 와 같은 성질을 켠다.
#ifdef WINPARSE_UNSIGNED_SOCKET
typedef unsigned long long SOCKET;                 // MSVC x64 의 UINT_PTR 과 같은 성질
#define INVALID_SOCKET ((SOCKET)(~(SOCKET)0))
#else
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif
#define SOCKET_ERROR   (-1)
inline int closesocket(SOCKET s) { return ::close((int)s); }
inline int WSAGetLastError() { return errno; }
#define WSAETIMEDOUT   ETIMEDOUT
#define WSAEWOULDBLOCK EAGAIN
#define WSAECONNRESET  ECONNRESET
typedef struct { unsigned short wVersion; char szDescription[257]; } WSADATA;
#define MAKEWORD(a,b) ((unsigned short)(((unsigned char)(a)) | ((unsigned short)((unsigned char)(b))) << 8))
inline int WSAStartup(unsigned short, WSADATA*) { return 0; }
inline int WSACleanup() { return 0; }
typedef unsigned long u_long;
inline int ioctlsocket(SOCKET, long, u_long*) { return 0; }
