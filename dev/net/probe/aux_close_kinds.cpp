// net/probe/aux_close_kinds.cpp — **보조 노드를 실제로 붙였다 끊어서 `사유` 칸을 확인한다**
//
// 🔴 왜 필요한가 : `-AUX … 연결 종료` 의 `사유` 칸이 **실제로 갈리는지**는
//   소스를 읽어서 알 수 없다. 컴파일이 되는 것은 증거가 아니다.
//   이 도구는 **진짜 소켓으로 붙어 진짜로 끊는다** — FIN 과 RST 를 골라서.
//
// ★ 검사가 통과했다고 말하려면 **한 번은 빨간불을 내 봐야 한다.**
//   여기서 빨간불은 *"끊는 방식을 바꿨는데 사유가 안 바뀐다"* 이다.
//   그래서 이 도구는 **두 가지 방식**을 다 낼 수 있게 돼 있다. 하나만 내면 아무것도 증명 못 한다.
//
// 빌드 : c++ -std=c++11 -O2 net/probe/aux_close_kinds.cpp -o /tmp/aux_close
// 쓰기 : /tmp/aux_close --dev P9 --close fin
//        /tmp/aux_close --dev P9 --close rst
//        (그 뒤 서버 로그에서 `-AUX 노드 P9 연결 종료 … · 사유 …` 를 본다)
//
// ⚠ **주의 — 이 도구는 주차 노드를 뺏을 수 있다.** 서버는 first-S-wins 라
//   주 노드가 아직 없으면 **이 시험 devid 가 주차 노드가 된다**. 실기 보드가 붙어 있을 때만 써라.
//   (붙어 있으면 다른 devid 는 자동으로 **보조 노드**로 들어간다 — 그게 우리가 원하는 것이다.)
//
// ⚠ **막는 대기를 쓰지 않는다** — 규약대로 `select()` 타임아웃으로만 기다린다.
//
// ─────────────────────────────────────────────────────────────────────────────
// 🔴 **이 칸은 장부가 아니라 판별자다** — P3 조사에서 두 이야기를 가른다
//
//   | 사유                    | 뜻                          | 어느 이야기            |
//   |-------------------------|-----------------------------|------------------------|
//   | `상대가 닫음`(FIN)      | ESP 가 **살아서 스스로 닫았다** | DHCP T1 재결속 — 전원이 먼저가 아니다 |
//   | `keepalive 시간초과`    | 보드가 **조용히 사라졌다**    | 브라운아웃·전원 축      |
//   | `수신 오류`(RST)        | 급작스러운 절단              | 그 밖                  |
//
//   ★ `MCUSR`(칩 쪽 리셋 원인)과 **다른 층에서 같은 물음에 답한다.** 판별자가 하나에서 둘이 됐다.
//   ⚠ 그리고 **둘이 어긋나면 그것도 값이다** — 사유가 `FIN` 인데 `MCUSR` 이 `BORF` 면
//     "ESP 는 정상 종료했는데 우노는 브라운아웃됐다" = **두 사건이 겹친 것**이다.
//
// 🔴 **`keepalive 시간초과` 는 이 도구로 못 낸다.** 조용하기만 하면 커널이 probe 에 ACK 한다 —
//   **패킷이 사라져야** 난다(방화벽에 `sudo` 필요). 그러니 그 라벨은 배포 뒤에도 **`0/0`** 이고,
//   ★ **실기가 그 자극을 공짜로 준다**(보드가 전원·Wi-Fi 를 잃는 것이 곧 패킷 소실).
//   즉 그것이 뜨면 **시험 통과가 아니라 발견**이다.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static const int DEFAULT_PORT = 9991;   // config.h 의 PORT_ARDUINO

// 서버(server_device.h)와 **같은 규칙**이다: 체크섬은 **끝의 쉼표까지 포함**해서 XOR 한다.
// 🔴 그 쉼표를 빼면 서버가 조용히 버린다(`체크섬 불일치 — 버림`).
static std::string cksum(const std::string& prefix) {
    unsigned char x = 0;
    for (size_t i = 0; i < prefix.size(); i++) x ^= (unsigned char)prefix[i];
    char b[3]; snprintf(b, sizeof(b), "%02X", x);
    return std::string(b);
}
static std::string build_line(const std::string& prefix) { return prefix + cksum(prefix) + "\n"; }

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", dev = "P9", how = "fin";
    int port = DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--host"  && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = atoi(argv[++i]);
        else if (a == "--dev"  && i + 1 < argc) dev  = argv[++i];
        else if (a == "--close"&& i + 1 < argc) how  = argv[++i];
        else { fprintf(stderr, "쓰기: %s [--host H] [--port N] --dev P9 --close fin|rst\n", argv[0]); return 2; }
    }
    if (how != "fin" && how != "rst") { fprintf(stderr, "--close 는 fin 또는 rst\n"); return 2; }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) { fprintf(stderr, "주소 이상: %s\n", host.c_str()); return 2; }
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { perror("connect"); return 2; }
    printf("붙었다 — %s:%d · device_id=%s\n", host.c_str(), port, dev.c_str());

    // S,seq,occupied10,reserved10,uptime,devid,  + 체크섬
    // 🔑 **빈 자리로 보낸다**(occupied 전부 0) — 시험이 실제 주차 상태를 바꾸면 안 된다.
    const std::string prefix = "S,1,0000000000,0000000000,1000," + dev + ",";
    const std::string line = build_line(prefix);
    if (send(s, line.c_str(), line.size(), 0) < 0) { perror("send"); return 2; }
    printf("S 프레임 보냈다 : %s", line.c_str());

    // 서버가 승격을 처리할 틈을 준다. 🔴 sleep 이 아니라 **select 타임아웃**이다.
    fd_set rd; FD_ZERO(&rd); FD_SET(s, &rd);
    timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
    const int n = select(s + 1, &rd, 0, 0, &tv);
    if (n > 0) {
        char b[512]; const int r = (int)recv(s, b, sizeof(b) - 1, 0);
        if (r > 0) { b[r] = 0; printf("서버가 보낸 것 : %s", b); }
        else printf("서버가 %s\n", r == 0 ? "먼저 닫았다(FIN)" : "오류를 줬다");
    } else {
        printf("2초 동안 서버 응답 없음 — 정상이다(보조 노드는 **하행을 안 받는다**)\n");
    }

    if (how == "rst") {
        // 🔴 SO_LINGER(on, 0) → close 가 **FIN 이 아니라 RST** 를 보낸다.
        //   서버 쪽 recv 는 -1/ECONNRESET(macOS 54 · 리눅스 104) 로 돌아온다.
        linger lg; lg.l_onoff = 1; lg.l_linger = 0;
        setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        printf("★ **RST** 로 끊는다 → 서버 로그에 `사유 수신 오류(errno=…)` 가 떠야 한다\n");
    } else {
        printf("★ **FIN** 으로 끊는다 → 서버 로그에 `사유 상대가 닫음` 이 떠야 한다\n");
    }
    close(s);

    printf("\n확인 : 서버 로그에서 아래를 봐라\n");
    printf("  grep -a -- '-AUX 노드 %s 연결 종료' <로그>\n", dev.c_str());
    printf("🔴 **두 방식을 다 내고 사유가 서로 달라야** 이 칸이 일하는 것이다.\n");
    printf("   한쪽만 보고 '된다' 고 하지 마라 — 상수를 찍어도 그 하나는 맞는다.\n");
    return 0;
}
