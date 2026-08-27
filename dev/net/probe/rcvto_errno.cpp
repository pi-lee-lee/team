// net/probe/rcvto_errno.cpp — **SO_RCVTIMEO 가 만료되면 errno 가 무엇인가**
//
// 🔴 왜 재는가 : 서버는 "끊김" 과 "지금은 없다" 를 errno 로 가른다(metrics.h).
//   POSIX 갈래가 `ETIMEDOUT` 을 **"지금은 없다"(err_is_again)** 에 넣고 있는데,
//   같은 `ETIMEDOUT` 이 **keepalive 가 죽인 소켓**의 신호이기도 하다.
//   두 뜻이 겹치면 한쪽이 다른 쪽을 가린다 — **어느 쪽이 실제로 오는지**가 답을 정한다.
//
// ★ 추측으로 정하지 않는다. 실제 소켓을 만들어 **만료시키고 errno 를 찍는다.**
//
// 빌드 : c++ -std=c++11 -O2 net/probe/rcvto_errno.cpp -o /tmp/rcvto_errno
// 실행 : /tmp/rcvto_errno        (약 0.3초. 네트워크 밖으로 안 나간다 — 루프백뿐)

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static const char* ename(int e) {
    if (e == EAGAIN)      return "EAGAIN/EWOULDBLOCK";
    if (e == ETIMEDOUT)   return "ETIMEDOUT";
    if (e == ECONNRESET)  return "ECONNRESET";
    return "그 밖";
}

int main() {
    // 듣는 소켓 하나를 루프백에 세우고, 스스로 붙은 뒤 **아무것도 안 보낸다**.
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0) { perror("bind"); return 2; }
    socklen_t al = sizeof(a);
    getsockname(ls, (sockaddr*)&a, &al);
    listen(ls, 1);

    int c = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(c, (sockaddr*)&a, sizeof(a)) != 0) { perror("connect"); return 2; }
    int s = accept(ls, 0, 0);
    if (s < 0) { perror("accept"); return 2; }

    // 🔑 서버가 아두이노 소켓에 거는 것과 **같은 것**을 건다.
    timeval tv; tv.tv_sec = 0; tv.tv_usec = 200000;   // 0.2초
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char b[64];
    errno = 0;
    int r = (int)recv(s, b, sizeof(b), 0);
    const int e = errno;

    printf("SO_RCVTIMEO 만료 → recv=%d · errno=%d (%s)\n", r, e, ename(e));

    // ---- 판정 : 이 값이 서버의 갈래 둘 중 어느 쪽에 걸리나
    const bool as_again   = (e == ETIMEDOUT || e == EAGAIN || e == EWOULDBLOCK);  // err_is_again (POSIX)
    const bool as_timeout = (e == ETIMEDOUT);                                     // err_is_timeout (POSIX)
    printf("  err_is_again  → %s\n", as_again   ? "참" : "거짓");
    printf("  err_is_timeout→ %s\n", as_timeout ? "참" : "거짓");

    if (e == EAGAIN || e == EWOULDBLOCK) {
        printf("\n★ 결론 : 만료는 **EAGAIN** 이다. 그러므로 POSIX 의 `err_is_again` 에 있는\n");
        printf("   `ETIMEDOUT` 은 **만료 때문이 아니다** — 그것은 **keepalive 가 죽인 소켓**의 신호이고,\n");
        printf("   🔴 지금 그것이 \"끊김이 아니다\" 로 분류되어 **`err_is_timeout` 갈래가 도달 불가**다.\n");
    } else if (e == ETIMEDOUT) {
        printf("\n★ 결론 : 이 플랫폼은 만료를 **ETIMEDOUT** 으로 준다 →\n");
        printf("   `err_is_again` 이 그것을 삼키는 것이 **맞다**. 대신 keepalive 사망과 구별이 불가능하다.\n");
    }

    close(s); close(c); close(ls);
    return 0;
}
