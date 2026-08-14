// 주차 관제 서버 — 명세: docs/net/parking-protocol.md (v1, 얼림)
//
// 한 프로세스가 리스너 둘을 연다:
//   TCP  9991 — 아두이노. LF 종단 압축 ASCII 라인 (명세 §2)
//   HTTP 9900 — index.html / data_log.json 정적 서빙 + GET /ws 를 WebSocket 으로 업그레이드 (§5)
//
// 외부 라이브러리를 쓰지 않는다. SHA-1 · base64 · WebSocket 프레이밍을 직접 구현했다.
//
// 빌드
//   macOS/Linux : c++ -std=c++11 -O2 -o server server.cpp
//   Windows     : cl /EHsc /std:c++14 /utf-8 server.cpp ws2_32.lib
//                 ^^^^^^^ **/utf-8 을 빼지 마라.**
//
//   이 파일은 BOM 없는 UTF-8 이고 안내 문구가 한글이다("예약되었습니다" 등).
//   BOM 이 없으면 MSVC 는 소스를 시스템 ANSI 코드페이지(한국어 윈도우면 CP949)로 읽어
//   그 문자열을 깨뜨린다. **컴파일은 그대로 통과한다** — 그래서 빌드 로그로는 절대 못 잡고
//   브라우저 화면에서 처음 보인다. BOM 을 넣어 해결하지 않는 이유는 POSIX 도구들이
//   BOM 을 성가셔하기 때문이다. 플래그로 해결하는 것이 맞다.
//
// 실행
//   ./server                  # 아두이노 TCP 9991 · HTTP/WS 9900
//   ./server --selftest       # SHA-1+base64 와 라인 체크섬 자가검증 (RFC 6455 §1.3 벡터)
//
// 하드웨어 없이 시험하려면 (명세 §10):
//   python3 ../net/fake_arduino.py                      # 가짜 아두이노
//   python3 ../net/ws_probe.py --listen 5               # 브라우저 대신 WS 관찰
//
// ⚠ 이 파일을 고칠 때 알아둘 것: now_ms() 와 epoch_ms() 는 **원점이 다르다**(각 함수 주석 참조).
//   POSIX 에서는 우연히 둘 다 Unix epoch 라 섞어 써도 티가 안 나지만, 윈도우에서는
//   now_ms() 가 부팅 후 경과 시간이라 섞는 순간 수십 년짜리 값이 나온다.
//
// 왜 크로스 플랫폼인가: 원본은 Winsock 전용이라 이 팀의 macOS 에서 빌드조차 되지 않았다.
// 빌드도 못 하는 코드는 검증할 수 없고, 검증할 수 없으면 "구현했다"고 말할 수 없다.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
// <ctime> 를 직접 넣는다. POSIX 에서는 아래 <sys/time.h> 가 우연히 끌어와서 통과했지만
// 그 include 는 #else 안이라 Windows 에는 없다 — 즉 macOS 빌드 성공은 우연이었다.
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

// ---------------------------------------------------------------- 플랫폼 어댑터
#ifdef _WIN32
  // 이 두 define 은 반드시 <winsock2.h> **앞**에 와야 한다. 뒤에 두면 아무 효과가 없다.
  //  · FD_SETSIZE: 윈도우 기본값이 64 다. FD_SET 은 넘치면 **조용히 무시**하므로
  //    브라우저가 여러 대 붙었을 때 원인 없는 무응답으로 보인다. 비용은 fd_set 크기뿐이다.
  //  · NOMINMAX: <windows.h> 의 min/max 매크로가 표준 함수와 충돌하는 것을 막는다.
  //    지금은 std::min/max 를 안 쓰지만 나중에 누가 쓰면 그때 깨진다. 비용 0 의 예방책이다.
  #define FD_SETSIZE 256
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define BAD_SOCK INVALID_SOCKET
  static void closesock(sock_t s) { closesocket(s); }
  static int  sockerr() { return WSAGetLastError(); }
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
  #include <signal.h>
  typedef int sock_t;
  #define BAD_SOCK (-1)
  static void closesock(sock_t s) { close(s); }
  static int  sockerr() { return errno; }
#endif

// ---------------------------------------------------------------- 상수 (명세대로)
static const int  PORT_ARDUINO   = 9991;
static const int  PORT_HTTP      = 9900;
static const int  PORT_PHONE     = 5500;    // digitcam 폰 수신 (docs/net/digitcam-protocol.md)
// 폰 프레임은 JSON 이라 아두이노 라인(64B)보다 길다. digitcam 명세 §9 가 권하는 방어적 상한.
static const size_t MAX_PHONE_LINE = 1024;
static const size_t MAX_PLATE_BYTES = 32;   // 번호판 저장 상한(신형 10B, 여유 포함)
static const int  MAX_LINE       = 64;      // §2.1
static const int  ACK_TIMEOUT_MS = 1500;    // §7.3
static const int  ACK_MAX_TRIES  = 3;       // 최초 + 재전송 2회
static const int  OFFLINE_MS     = 3500;    // §3.4
static const int  SELECT_TICK_MS = 200;     // 타이머를 돌리기 위한 select 최대 대기
static const int  LOG_KEEP       = 2;       // §9.1 최신 2건
static const uint64_t WS_MAX_FRAME = 64 * 1024;  // 클라이언트가 선언한 길이의 상한

static const char* SLOT_ID[10] = {"A1","A2","A3","A4","A5","B1","B2","B3","B4","B5"};
static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ---------------------------------------------------------------- SHA-1
// RFC 3174. 흔한 실수 셋: 길이는 **비트 수**를 빅엔디안 64비트로, 패딩은 0x80 뒤 0 을
// 채워 56 mod 64, 회전은 전부 32비트. --selftest 가 이 셋을 한 번에 잡는다.
struct SHA1 {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
    size_t   n;

    SHA1() { reset(); }
    void reset() {
        h[0]=0x67452301u; h[1]=0xEFCDAB89u; h[2]=0x98BADCFEu; h[3]=0x10325476u; h[4]=0xC3D2E1F0u;
        len = 0; n = 0;
    }
    static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16) |
                   (uint32_t(p[i*4+2]) << 8) | uint32_t(p[i*4+3]);
        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            uint32_t t = rol(a,5) + f + e + k + w[i];
            e = d; d = c; c = rol(b,30); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    void update(const uint8_t* p, size_t sz) {
        len += uint64_t(sz) * 8;                    // ← 비트 수
        while (sz--) {
            buf[n++] = *p++;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(uint8_t out[20]) {
        uint64_t bits = len;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t z = 0;
        while (n != 56) update(&z, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; i++) lb[i] = uint8_t((bits >> (56 - 8*i)) & 0xFF);  // 빅엔디안
        // update() 가 len 을 또 더하지 않도록 직접 넣는다
        for (int i = 0; i < 8; i++) { buf[n++] = lb[i]; if (n == 64) { block(buf); n = 0; } }
        for (int i = 0; i < 5; i++) {
            out[i*4]   = uint8_t((h[i] >> 24) & 0xFF);
            out[i*4+1] = uint8_t((h[i] >> 16) & 0xFF);
            out[i*4+2] = uint8_t((h[i] >> 8)  & 0xFF);
            out[i*4+3] = uint8_t( h[i]        & 0xFF);
        }
    }
};

static std::string base64(const uint8_t* p, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i+1 < n) v |= uint32_t(p[i+1]) << 8;
        if (i+2 < n) v |= uint32_t(p[i+2]);
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += (i+1 < n) ? T[(v >> 6) & 63] : '=';
        o += (i+2 < n) ? T[v & 63]        : '=';
    }
    return o;
}

static std::string ws_accept(const std::string& key) {
    std::string s = key + WS_GUID;
    SHA1 sh; sh.update((const uint8_t*)s.data(), s.size());
    uint8_t d[20]; sh.final(d);
    return base64(d, 20);
}

// ---------------------------------------------------------------- 시간
// ⚠ now_ms() 와 epoch_ms() 는 **원점이 다르다. 절대 섞어 쓰지 마라.**
//   now_ms()   : 단조 증가하는 상대 시각. 윈도우에서는 **부팅 후 경과 ms**.
//                타이머 계산(ACK 타임아웃·offline 판정)에만 쓴다.
//   epoch_ms() : Unix epoch 기준 절대 시각. 바깥으로 나가는 값(ts, reserved_at)에만 쓴다.
//   POSIX 에서는 둘 다 epoch 라 섞어도 티가 안 나지만, 윈도우에서는 그 순간
//   수십 년짜리 값이 나온다. **윈도우에서만 틀리는 버그**라 여기서 못 잡는다.

// 타이머 전용 — 상대 시각 (윈도우: 부팅 후 경과)
static long long now_ms() {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}
// 바깥으로 나가는 값 전용 — Unix epoch 절대 시각 (ts, reserved_at)
static long long epoch_ms() {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)((t - 116444736000000000ULL) / 10000ULL);
#else
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}
static void logf(const char* mark, const std::string& msg) {
    long long t = epoch_ms() / 1000;
    time_t tt = (time_t)t;
    char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&tt));
    std::cout << ts << "  " << mark << " " << msg << std::endl;
}

// ---------------------------------------------------------------- 라인 체크섬 (§2.2)
// 대상: 첫 바이트부터 체크섬 앞 쉼표까지(그 쉼표 포함). 대문자 2자리 hex.
static std::string cksum(const std::string& prefix) {
    unsigned char x = 0;
    for (size_t i = 0; i < prefix.size(); i++) x ^= (unsigned char)prefix[i];
    char b[3]; snprintf(b, sizeof(b), "%02X", x);
    return std::string(b);
}
static std::string build_line(const std::string& prefix) { return prefix + cksum(prefix) + "\n"; }

static bool verify_line(const std::string& line, std::vector<std::string>& out) {
    size_t cut = line.rfind(',');
    if (cut == std::string::npos) return false;
    if (cksum(line.substr(0, cut + 1)) != line.substr(cut + 1)) return false;
    out.clear();
    std::string body = line.substr(0, cut);
    std::string cur;
    for (size_t i = 0; i < body.size(); i++) {
        if (body[i] == ',') { out.push_back(cur); cur.clear(); }
        else cur += body[i];
    }
    out.push_back(cur);
    return true;
}

// ---------------------------------------------------------------- 상태
struct Slot {
    int occupied;            // 아두이노가 진실값을 준다
    int reserved;            // 서버가 durable owner (§7.4)
    std::string user_id;     // 빈 문자열 = null
    long long reserved_at;   // 0 = null
    Slot() : occupied(0), reserved(0), reserved_at(0) {}
};

struct Pending {             // 아두이노에 내려보내고 ACK 를 기다리는 요청
    uint16_t wire_rid;
    sock_t   ws_fd;          // 요청한 브라우저 (BAD_SOCK = 서버 자체 재동기화)
    std::string browser_rid;
    std::string slot, user_id;   // user_id = **전선에 실제로 나간 값**(ASCII 0*8 또는 빈 값)
    std::string plate;           // 서버가 보관할 원래 값(UTF-8 번호판 등). 전선에 안 나간다
    char kind;               // 'R' | 'C' | 'T'
    char top;                // kind=='T' 일 때의 op: 'A'|'D'|'S'|'X'
    long long sent_ms;
    int tries;
};

struct Conn {
    enum Kind { HTTP, WS } kind;
    std::string inbuf;
    Conn() : kind(HTTP) {}
};

struct Server {
    Slot slots[10];
    sock_t lsn_ard, lsn_http, lsn_phone;
    std::map<sock_t, std::string> phones;   // 폰 연결 → 수신 버퍼 (연결마다 따로!)
    sock_t ard;                        // 아두이노 연결 (하나만)
    std::string ard_buf;
    bool  ard_seen;
    long long ard_last_ms;
    long long ard_uptime;
    long  ard_seq;
    std::string ard_dev;

    std::map<sock_t, Conn> conns;      // HTTP/WS 클라이언트
    std::map<uint16_t, Pending> pend;
    uint16_t next_rid;
    std::string last_bits;             // data_log 중복 쓰기 방지

    // §7.5 예약 은퇴 — occupied 1→0 전이를 보려면 직전 프레임이 기준선으로 필요하다.
    // base_valid=false 면 기준선이 없다는 뜻이고, 그 프레임에서는 판정하지 않는다.
    int  base_occ[10];
    bool base_valid;

    // §12A 테스트 모드. **이 두 값의 출처는 S 프레임의 tmask 이지 서버의 기억이 아니다.**
    // T 의 ACK 를 받았다고 무장됐다고 단정하면, 해제 ACK 를 놓쳤을 때 서버는 "해제됨"이라
    // 믿고 화면은 가짜 값을 경고 없이 진짜로 표시한다 — §12A.6 이 막으려는 사고 자체다.
    // tmask 를 진실로 삼으면 다음 프레임에서 저절로 맞춰진다(§12A.4).
    bool test_armed;
    int  test_ovr[10];        // 1 = 그 칸의 occupied 는 주입된 값
    int  base_ovr[10];        // 직전 프레임의 오버라이드 비트 — 되돌림 판정용(§7.5-3)

    Server() : lsn_ard(BAD_SOCK), lsn_http(BAD_SOCK), lsn_phone(BAD_SOCK), ard(BAD_SOCK),
               ard_seen(false), ard_last_ms(0), ard_uptime(-1), ard_seq(-1),
               ard_dev("?"), next_rid(1), base_valid(false), test_armed(false) {
        for (int i = 0; i < 10; i++) { base_occ[i] = 0; test_ovr[i] = 0; base_ovr[i] = 0; }
    }

    bool device_online() const {
        return ard != BAD_SOCK && ard_seen && (now_ms() - ard_last_ms) < OFFLINE_MS;
    }
    static int slot_index(const std::string& s) {
        for (int i = 0; i < 10; i++) if (s == SLOT_ID[i]) return i;
        return -1;
    }
    // ── 두 이름공간 (rid 를 §4.1 에서 나눈 것과 같은 구조) ─────────────────────
    //   브라우저 ↔ 서버 · 서버 내부 저장 : **번호판 전체** (UTF-8, JSON 이라 자유)
    //   서버 → 아두이노 (전선 userid)    : ASCII 0*8 (§2.3) — 못 담으면 **빈 값**
    //
    // 전선에 번호판을 태우려고 valid_userid() 를 느슨하게 만들면 REQ-0023 이 막은 버그
    // (잘린 값이 체크섬과 함께 형식상 유효한 라인으로 나가는 것)가 되살아난다.
    // 아두이노는 userid 를 쓰지 않는다(§2.4 — 무시해도 되지만 필드는 있어야 한다).
    // 그래서 담을 수 없으면 그냥 비운다. 잃는 것이 없다.
    static std::string wire_userid(const std::string& browser_user) {
        return valid_userid(browser_user) ? browser_user : std::string();
    }
    // 브라우저 쪽 값은 UTF-8 이라 문자 집합을 강제하지 않는다. 다만 저장이 무한히 커지지
    // 않게 길이만 막고, 제어문자는 거른다(로그·JSON 오염 방지).
    static bool valid_browser_user(const std::string& u) {
        if (u.size() > MAX_PLATE_BYTES) return false;
        for (size_t i = 0; i < u.size(); i++)
            if ((unsigned char)u[i] < 0x20) return false;
        return true;
    }

    // 명세 §2.3 `userid ::= 0*8( ALPHA / DIGIT / "_" / "-" )`
    // 검증하지 않으면 snprintf 가 조용히 잘라서 **형식상 유효하지만 내용이 잘린 라인**이 나간다.
    // 메모리 안전 문제는 없지만(잘린다) 64B 상한(§2.1)을 넘거나 엉뚱한 user_id 가 기록되고,
    // 증상은 "가끔 예약이 안 된다"(재전송 3회 후 ack_timeout)로 보여 원인을 찾기 어렵다.
    static bool valid_userid(const std::string& u) {
        if (u.size() > 8) return false;
        for (size_t i = 0; i < u.size(); i++) {
            char c = u[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    // ---------- 송신 helper
    void send_raw(sock_t fd, const char* p, size_t n) {
        size_t off = 0;
        while (off < n) {                       // 부분 write 는 정상이다
            int w =
#ifdef _WIN32
                ::send(fd, p + off, (int)(n - off), 0);
#else
                (int)::send(fd, p + off, n - off, 0);
#endif
            if (w <= 0) return;
            off += (size_t)w;
        }
    }
    void send_ard(const std::string& line) {
        if (ard == BAD_SOCK) return;
        send_raw(ard, line.data(), line.size());
        logf("→ARD", line.substr(0, line.size() - 1));
    }

    // ---------- WebSocket 프레임 (§5.2)
    void ws_send(sock_t fd, const std::string& payload) {
        std::string f;
        f += char(0x81);                                  // FIN + text
        size_t n = payload.size();
        if (n < 126) {
            f += char((unsigned char)n);                  // 서버→클라이언트는 마스킹 금지 (n<126 확정)
        } else if (n <= 0xFFFF) {
            f += char(126);                               // 16비트 확장 길이 — 스냅샷이 여기 온다
            f += char((n >> 8) & 0xFF);
            f += char(n & 0xFF);
        } else {
            f += char(127);
            for (int i = 7; i >= 0; i--) f += char((n >> (8*i)) & 0xFF);
        }
        f += payload;
        send_raw(fd, f.data(), f.size());
    }
    void ws_broadcast(const std::string& payload) {
        for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it)
            if (it->second.kind == Conn::WS) ws_send(it->first, payload);
    }

    // ---------- JSON 만들기
    static std::string jstr(const std::string& s) {
        std::string o = "\"";
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b,sizeof(b),"\\u%04x",c); o += b; }
            else o += c;
        }
        return o + "\"";
    }
    std::string snapshot_json() {
        std::ostringstream o;
        o << "{\"type\":\"snapshot\",\"ts\":" << epoch_ms()
          << ",\"device\":{\"online\":" << (device_online() ? "true" : "false")
          << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq) << "}";
        // §5.3 test_mode — 출처는 S 의 tmask 다(§12A.4). 서버가 T 를 보냈다는 사실이 아니다.
        int novr = 0;
        for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) novr++;
        o << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
          << ",\"override_count\":" << novr << "}";
        o << ",\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) o << ",";
            o << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved << ",\"user_id\":";
            if (slots[i].user_id.empty()) o << "null"; else o << jstr(slots[i].user_id);
            o << ",\"reserved_at\":";
            if (slots[i].reserved_at == 0) o << "null"; else o << slots[i].reserved_at;
            o << ",\"overridden\":" << ((test_armed && test_ovr[i]) ? 1 : 0);
            o << "}";
        }
        o << "]}";
        return o.str();
    }
    void push_snapshot() { ws_broadcast(snapshot_json()); }

    void send_err(sock_t fd, const std::string& rid, const char* code, const char* msg) {
        std::ostringstream o;
        o << "{\"type\":\"error\",\"rid\":" << (rid.empty() ? std::string("null") : jstr(rid))
          << ",\"code\":\"" << code << "\",\"message\":" << jstr(msg) << "}";
        if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());
    }
    void send_ack(sock_t fd, const std::string& rid, const std::string& slot, int result,
                  char kind = 'R') {
        const char* m = "예약되었습니다";
        if (kind == 'T')      m = "테스트 값을 적용했습니다";
        else if (kind == 'C') m = "예약을 취소했습니다";
        else if (kind == 'M') m = "시뮬레이션 한 걸음 진행했습니다";
        if (result == 1) m = "이미 주차된 자리입니다";
        else if (result == 2) m = "이미 예약된 자리입니다";
        else if (result == 3) m = "잘못된 요청입니다";
        else if (result == 4) m = "테스트 모드가 꺼져 있습니다";
        // result=5 는 **성공이 아니다.** 버튼을 눌렀는데 아무 일도 안 난 것을
        // 화면이 성공으로 표시하면 안 되므로 값과 문구를 따로 둔다(§12B.4).
        else if (result == 5) m = "바꿀 시뮬 자리가 없습니다";
        std::ostringstream o;
        o << "{\"type\":\"ack\",\"rid\":" << jstr(rid) << ",\"slot\":";
        // 무장/해제처럼 자리가 없는 응답은 null 이다 — 전선의 "??" 를 그대로 흘리지 않는다(§5.4).
        if (slot.empty() || slot == "??") o << "null"; else o << "\"" << slot << "\"";
        o << ",\"result\":" << result << ",\"message\":" << jstr(m) << "}";
        if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());   // 요청자에게만
    }

    // ---------- data_log.json (§9)
    std::string bits(bool reserved_bits) {
        std::string s;
        for (int i = 0; i < 10; i++)
            s += char('0' + (reserved_bits ? slots[i].reserved : slots[i].occupied));
        return s;
    }
    std::vector<std::string> log_entries;
    void write_log_if_changed() {
        // 테스트 상태도 키에 넣는다 — 안 넣으면 무장/주입이 파일에 반영되지 않고
        // 폴백 화면이 낡은 "해제" 상태를 계속 보여 준다(개정 4 가 막으려는 바로 그 증상).
        std::string key = bits(false) + "|" + bits(true) + "|" + (test_armed ? "A" : "-");
        for (int i = 0; i < 10; i++) key += char('0' + ((test_armed && test_ovr[i]) ? 1 : 0));
        if (key == last_bits) return;            // §9.4 — 내용이 같으면 안 쓴다
        last_bits = key;

        std::ostringstream e;
        e << "{\"ts\":" << epoch_ms() << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq)
          << ",\"occupied\":\"" << bits(false) << "\",\"reserved\":\"" << bits(true) << "\"";
        // §9.1(개정 4) — 무장 여부와 칸별 주입 표시를 **파일에도** 넣는다.
        // WS 가 끊기면 브라우저는 이 파일로 폴백하는데, 이 두 필드가 없으면
        // **폴백 화면이 주입값을 실측처럼 그린다** — §12A.6 이 폴백 경로에서만 깨지고 있었다.
        {
            int n = 0;
            for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) n++;
            e << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
              << ",\"override_count\":" << n << "}";
        }
        e << ",\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) e << ",";
            e << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved
              << ",\"overridden\":" << ((test_armed && test_ovr[i]) ? 1 : 0) << "}";
        }
        e << "]}";

        log_entries.insert(log_entries.begin(), e.str());       // 최신이 앞
        while ((int)log_entries.size() > LOG_KEEP) log_entries.pop_back();

        std::string body = "[\n";
        for (size_t i = 0; i < log_entries.size(); i++) {
            body += "  " + log_entries[i];
            if (i + 1 < log_entries.size()) body += ",";
            body += "\n";
        }
        body += "]\n";

        atomic_write_log(body);
    }

    // tmp 에 다 쓰고 원자적으로 갈아끼운다 (§9.2). 잠금도 복사본도 필요 없다.
    // **쓰기 경로는 이것 하나다** — 기동 시 빈 배열을 만들 때도 이 함수를 쓴다.
    void atomic_write_log(const std::string& body) {
        const char* tmp = "data_log.json.tmp";
        const char* dst = "data_log.json";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) { logf("!", "data_log tmp 열기 실패"); return; }
            f << body;
            f.flush();
        }
        // 실패하면 **원인 코드를 반드시 남긴다.** 안 남기면 data_log.json 이 조용히 옛 내용을
        // 유지하는데 로그에는 아무것도 없다. 윈도우에서 백신·편집기가 파일을 잠깐 잡아
        // ERROR_SHARING_VIOLATION(32) 이 나는 것은 흔한 일이라 이 구분이 실제로 필요하다.
#ifdef _WIN32
        // 윈도우는 대상이 있으면 rename 이 실패한다 (§9.3).
        // "먼저 지우고 rename" 으로 흉내내면 그 틈에 읽은 클라이언트가 404 를 본다 — 원자성이 깨진다.
        if (!MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
            DWORD e = GetLastError();
            char b[128];
            snprintf(b, sizeof(b), "MoveFileEx 실패 GetLastError=%lu%s",
                     (unsigned long)e,
                     e == 32 ? " (ERROR_SHARING_VIOLATION — 다른 프로그램이 파일을 잡고 있다)" : "");
            logf("!", b);
        }
#else
        if (rename(tmp, dst) != 0) {
            char b[128];
            snprintf(b, sizeof(b), "rename 실패 errno=%d (%s)", errno, strerror(errno));
            logf("!", b);
        }
#endif
    }

    // ---------- 아두이노로 요청 내리기
    void dispatch(char kind, sock_t ws_fd, const std::string& brid,
                  const std::string& slot, const std::string& uid) {
        uint16_t rid = next_rid++;
        if (next_rid == 0) next_rid = 1;
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot;
        p.plate = uid;                       // 서버가 기억할 원본(번호판일 수 있다)
        p.user_id = wire_userid(uid);        // 전선에 나갈 값 — ASCII 0*8 아니면 빈 값
        p.kind = kind;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;

        char buf[64];
        // **p.user_id 를 쓴다. 인자 uid 를 쓰면 안 된다** — uid 는 번호판일 수 있고
        // 그러면 UTF-8 이 그대로 전선에 나가 §2.1(ASCII 전용)과 §2.3 을 위반한다.
        // 실제로 그 버그를 냈다: `R,1,B2,980가4568,F7` 이 나가 아두이노가 죽었다.
        if (kind == 'R') snprintf(buf, sizeof(buf), "R,%u,%s,%s,", rid, slot.c_str(), p.user_id.c_str());
        else             snprintf(buf, sizeof(buf), "C,%u,%s,", rid, slot.c_str());
        send_ard(build_line(buf));
    }

    // §2.4 `T` — 테스트 모드 제어. R/C 와 같은 pend 표·재전송·타임아웃을 그대로 탄다.
    // Pending.user_id 를 tval 보관에 재사용하고, slot 에 "??" 가 들어갈 수 있다.
    void dispatch_test(sock_t ws_fd, const std::string& brid,
                       char op, const std::string& slot, const std::string& tval) {
        uint16_t rid = next_rid++;
        if (next_rid == 0) next_rid = 1;
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot; p.user_id = tval; p.kind = 'T';
        p.top = op;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        send_ard(build_line(test_prefix(p)));
    }
    // §12B — 시뮬레이터 한 걸음. **무장 여부로 막지 않는다**(테스트 모드와 별개).
    void dispatch_sim(sock_t ws_fd, const std::string& brid) {
        uint16_t rid = next_rid++;
        if (next_rid == 0) next_rid = 1;
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.kind = 'M'; p.top = 0;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        send_ard(build_line(sim_prefix(p)));
    }
    static std::string sim_prefix(const Pending& p) {
        char buf[32];
        snprintf(buf, sizeof(buf), "M,%u,", p.wire_rid);
        return std::string(buf);
    }
    static std::string test_prefix(const Pending& p) {
        char buf[64];
        snprintf(buf, sizeof(buf), "T,%u,%c,%s,%s,",
                 p.wire_rid, p.top, p.slot.c_str(), p.user_id.c_str());
        return std::string(buf);
    }

    // ---------- 타이머 (§7.3 재전송 / §3.4 offline)
    void tick() {
        long long t = now_ms();
        std::vector<uint16_t> dead;
        for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it) {
            Pending& p = it->second;
            if (t - p.sent_ms < ACK_TIMEOUT_MS) continue;
            if (p.tries >= ACK_MAX_TRIES) {
                logf("!", "ACK 타임아웃 최종 실패 wire_rid=" + std::to_string(p.wire_rid));
                send_err(p.ws_fd, p.browser_rid, "ack_timeout", "센서가 응답하지 않습니다");
                dead.push_back(it->first);
                continue;
            }
            p.tries++;
            p.sent_ms = t;
            char buf[64];
            std::string line;
            if (p.kind == 'M') line = sim_prefix(p);       // 재전송이 두 걸음이 되면 안 된다(§12B.4)
            else if (p.kind == 'T') line = test_prefix(p); // 테스트도 같은 wire_rid 로 재전송
            else if (p.kind == 'R') {
                snprintf(buf, sizeof(buf), "R,%u,%s,%s,", p.wire_rid, p.slot.c_str(), p.user_id.c_str());
                line = buf;
            } else {
                snprintf(buf, sizeof(buf), "C,%u,%s,", p.wire_rid, p.slot.c_str());
                line = buf;
            }
            logf("↻", "재전송 " + std::to_string(p.tries) + "/" + std::to_string(ACK_MAX_TRIES)
                      + " (같은 wire_rid=" + std::to_string(p.wire_rid) + ")");
            send_ard(build_line(line));
        }
        for (size_t i = 0; i < dead.size(); i++) pend.erase(dead[i]);
    }

    // 그 자리에 아직 ACK 를 못 받은 요청이 있는가.
    // 있으면 세계관이 갈라진 게 아니라 "진행 중"일 뿐이므로 손대면 안 된다(§7.6).
    bool has_pending_for(const std::string& slot) const {
        for (std::map<uint16_t, Pending>::const_iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.slot == slot) return true;
        return false;
    }

    // ---------- §7.5 예약 은퇴
    // 예약된 자리에서 차가 빠지면(occupied 1→0) 그 예약은 소진된 것이다.
    // **ACK 를 기다리지 않고 서버 쪽에서 즉시 확정한다** — ACK 실패 시 살려 두면
    // 애초에 이걸 만들게 한 "죽은 자리" 버그가 은퇴 경로로 되돌아온다.
    void retire(int i) {
        logf("⏏", std::string("예약 소진(occupied 1→0) — ") + SLOT_ID[i]
                  + " 은퇴시킨다 (user=" + (slots[i].user_id.empty() ? "-" : slots[i].user_id) + ")");
        slots[i].reserved = 0;
        slots[i].user_id.clear();
        slots[i].reserved_at = 0;
        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");   // 아두이노에도 반영시킨다
    }

    // ---------- 아두이노 재부팅 → 재동기화 (§7.4)
    void resync_reservations(const char* why) {
        std::vector<int> live;
        for (int i = 0; i < 10; i++) if (slots[i].reserved) live.push_back(i);
        logf("⟳", std::string("재부팅 감지(") + why + ") — 살아 있는 예약 "
                  + std::to_string(live.size()) + "건 재하달");
        for (size_t k = 0; k < live.size(); k++)
            dispatch('R', BAD_SOCK, "", SLOT_ID[live[k]], slots[live[k]].user_id);
    }

    // ---------- 아두이노 라인 처리
    void on_ard_line(const std::string& line) {
        logf("←ARD", line);
        std::vector<std::string> f;
        if (!verify_line(line, f)) { logf("!", "체크섬 불일치 — 버림"); return; }
        if (f.empty()) return;

        if (f[0] == "S" && f.size() >= 6) {
            long seq = atol(f[1].c_str());
            long long up = atoll(f[4].c_str());
            bool reboot = (ard_uptime >= 0 && up < ard_uptime) || (ard_seq >= 0 && seq < ard_seq);
            ard_seq = seq; ard_uptime = up; ard_dev = f[5];
            ard_last_ms = now_ms(); ard_seen = true;

            int occ[10];
            for (int i = 0; i < 10; i++)
                occ[i] = (i < (int)f[2].size() && f[2][i] == '1') ? 1 : 0;

            // §2.4 tmask — **선택 필드다. 없으면 해제로 본다**(§2.1 규칙 8, 옛 펌웨어 수용).
            // f 는 체크섬을 뺀 필드들이므로 7번째(index 6)가 있으면 그것이 tmask 다.
            int ovr[10];
            bool armed = false;
            for (int i = 0; i < 10; i++) ovr[i] = 0;
            if (f.size() >= 7 && f[6] != "-" && f[6].size() >= 10) {
                armed = true;
                for (int i = 0; i < 10; i++) ovr[i] = (f[6][i] == '1') ? 1 : 0;
            }

            if (reboot) resync_reservations("uptime/seq 역행");

            // §7.5 — occupied 1→0 이면 예약 소진.
            // 재부팅했거나 기준선이 없으면 **판정하지 않고 기준선만 세운다**(§7.5-1).
            // 이걸 빼면 재부팅으로 occupied 가 초기화될 때 있지도 않은 1→0 전이가 잡혀
            // 방금 재하달한 예약을 그 자리에서 죽인다.
            if (base_valid && !reboot) {
                for (int i = 0; i < 10; i++) {
                    if (!(base_occ[i] == 1 && occ[i] == 0)) continue;      // 1→0 전이가 아니면 무관
                    if (slots[i].reserved != 1) continue;
                    if (has_pending_for(SLOT_ID[i])) continue;
                    // §7.5-3 **되돌림은 출차가 아니다.** 오버라이드 비트가 같이 1→0 이면
                    // 가짜 값을 걷어낸 것이지 차가 빠진 게 아니다. 이 구분이 없으면
                    // 해제(D) 한 번에 열 칸이 되돌아가면서 **예약이 통째로 은퇴한다.**
                    if (base_ovr[i] == 1 && ovr[i] == 0) {
                        logf("=", std::string("되돌림 감지 — ") + SLOT_ID[i]
                                  + " 은퇴시키지 않는다(테스트 오버라이드 해제)");
                        continue;
                    }
                    retire(i);
                }
            } else {
                logf("=", "은퇴 기준선 설정 — 이 프레임은 전이 판정을 건너뛴다");
            }
            for (int i = 0; i < 10; i++) {
                base_occ[i] = occ[i]; slots[i].occupied = occ[i];
                base_ovr[i] = ovr[i]; test_ovr[i] = ovr[i];
            }
            base_valid = true;
            if (test_armed != armed)
                logf("*", std::string("테스트 모드 ") + (armed ? "무장" : "해제")
                          + " (출처: S 의 tmask)");
            test_armed = armed;

            // reserved 는 서버가 durable owner 다(§7.4). 아두이노 값으로 덮지 않는다.
            // 다만 서버가 0 인데 아두이노가 1 이면 세계관이 갈라진 것이므로 C 를 다시 내려 맞춘다(§7.6).
            // 미결 요청이 있는 자리는 제외 — 아직 ACK 를 못 받았을 뿐인 정상 상태다.
            if (f.size() >= 4 && f[3].size() >= 10) {
                for (int i = 0; i < 10; i++)
                    if (f[3][i] == '1' && slots[i].reserved == 0 && !has_pending_for(SLOT_ID[i])) {
                        logf("⚠", std::string("불일치: 아두이노 ") + SLOT_ID[i]
                                  + " reserved=1, 서버 0 → C 재하달");
                        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");
                    }
            }
            write_log_if_changed();
            push_snapshot();
        }
        else if (f[0] == "A" && f.size() >= 4) {
            uint16_t rid = (uint16_t)atoi(f[1].c_str());
            std::string slot = f[2];
            int result = atoi(f[3].c_str());
            std::map<uint16_t, Pending>::iterator it = pend.find(rid);
            if (it == pend.end()) { logf("!", "모르는 rid 의 ACK — 무시 (재전송 중복일 수 있다)"); return; }
            Pending p = it->second;
            pend.erase(it);

            // result=3 이면 slot 은 "??" 다(§2.4). 전선의 자리 값을 믿지 않고
            // **매핑표의 원래 자리**를 쓴다 — 상관 키는 rid 이고 원본은 서버가 들고 있다.
            if (slot == "??" || slot_index(slot) < 0) slot = p.slot;
            int idx = slot_index(slot);
            // **예약 상태를 건드리는 것은 R/C 뿐이다.** T 를 여기에 섞으면
            // 테스트 주입 ACK 가 cancel 로 취급돼 **그 칸의 예약을 지워 버린다.**
            if (result == 0 && idx >= 0 && (p.kind == 'R' || p.kind == 'C')) {
                if (p.kind == 'R') {
                    slots[idx].reserved = 1;
                    // 화면·스냅샷에는 **원본**을 보여 준다(번호판이면 번호판 그대로).
                    // 전선에 나간 값(p.user_id)은 잘렸거나 비어 있을 수 있다.
                    slots[idx].user_id = p.plate;
                    slots[idx].reserved_at = epoch_ms();
                } else {
                    slots[idx].reserved = 0;
                    slots[idx].user_id.clear();
                    slots[idx].reserved_at = 0;
                }
            }
            // 테스트 결과는 여기서 상태에 반영하지 않는다 — 무장/오버라이드의 진실은
            // 다음 S 프레임의 tmask 다(§12A.4). ACK 는 "명령이 처리됐다"까지만 말한다.
            if (p.ws_fd != BAD_SOCK) send_ack(p.ws_fd, p.browser_rid, slot, result, p.kind);
            write_log_if_changed();
            push_snapshot();
        }
        else {
            logf("!", "모르는 타입 — 조용히 버림");
        }
    }

    // 코드포인트를 UTF-8 로. digitcam 명세 §4.3 의 \uXXXX 복원에 쓴다.
    static void utf8_append(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }

    // ---------- 브라우저 → 서버 (§5.4)
    static std::string jget(const std::string& s, const char* key) {
        std::string pat = std::string("\"") + key + "\"";
        size_t i = s.find(pat);
        if (i == std::string::npos) return "";
        i = s.find(':', i + pat.size());
        if (i == std::string::npos) return "";
        i++;
        while (i < s.size() && (s[i]==' '||s[i]=='\t')) i++;
        if (i < s.size() && s[i] == '"') {
            i++; std::string o;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    char e = s[i+1];
                    // digitcam 명세 §4.3: 한글을 원문 UTF-8 로 보내든 \uXXXX 로 보내든
                    // **수신 측이 둘 다 복원**해야 한다. 이걸 안 하면 이스케이프로 보내는
                    // 송신기에서 번호판이 "123가4568" 로 저장돼 매칭이 전부 빗나간다.
                    if (e == 'u' && i + 5 < s.size()) {
                        unsigned cp = 0; bool ok = true;
                        for (int k = 0; k < 4; k++) {
                            char c = s[i+2+k]; int v;
                            if (c >= '0' && c <= '9') v = c - '0';
                            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                            else { ok = false; break; }
                            cp = (cp << 4) | (unsigned)v;
                        }
                        if (ok) { utf8_append(o, cp); i += 6; continue; }
                    }
                    if (e == 'n') { o += '\n'; i += 2; continue; }
                    if (e == 't') { o += '\t'; i += 2; continue; }
                    if (e == 'r') { o += '\r'; i += 2; continue; }
                    o += e; i += 2; continue;                 // \" \\ \/ 등
                }
                o += s[i++];
            }
            return o;
        }
        std::string o;
        while (i < s.size() && s[i] != ',' && s[i] != '}') o += s[i++];
        while (!o.empty() && (o[o.size()-1]==' ')) o.erase(o.size()-1);
        return o;
    }
    void on_ws_message(sock_t fd, const std::string& msg) {
        logf("←WS", msg.substr(0, 120));
        std::string type = jget(msg, "type");
        std::string rid  = jget(msg, "rid");
        std::string slot = jget(msg, "slot");
        std::string uid  = jget(msg, "user_id");
        if (uid == "null") uid.clear();

        // ---- §12B 시뮬레이터 한 걸음 (개정 5)
        // **무장 여부를 확인하지 않는다.** 테스트 모드와 별개라는 것이 이 기능의 요구다(§12B.3).
        if (type == "sim_step") {
            if (!device_online()) {
                send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
                return;
            }
            dispatch_sim(fd, rid);
            return;
        }

        // ---- §12A 테스트 모드 (개정 3)
        if (type == "test_arm" || type == "test_disarm" ||
            type == "test_set" || type == "test_clear") {
            if (!device_online()) {
                send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
                return;
            }
            char op = (type == "test_arm") ? 'A' : (type == "test_disarm") ? 'D'
                    : (type == "test_set") ? 'S' : 'X';
            std::string tslot = "??", tval = "-";
            if (op == 'S' || op == 'X') {
                if (slot_index(slot) < 0) {
                    send_err(fd, rid, "bad_request", "그런 자리가 없습니다");
                    return;
                }
                tslot = slot;
                if (op == 'S') {
                    std::string occ = jget(msg, "occupied");
                    if (occ != "0" && occ != "1") {
                        send_err(fd, rid, "bad_request", "occupied 는 0 또는 1 이어야 합니다");
                        return;
                    }
                    tval = occ;
                }
            }
            dispatch_test(fd, rid, op, tslot, tval);
            return;
        }

        if (type != "reserve" && type != "cancel") {
            send_err(fd, rid, "bad_request", "알 수 없는 요청입니다");
            return;
        }
        if (slot_index(slot) < 0) {
            send_err(fd, rid, "bad_request", "그런 자리가 없습니다");
            return;
        }
        // 브라우저 user_id 는 **번호판이 들어올 수 있으므로 UTF-8 을 허용한다.**
        // 전선으로 나갈 값은 wire_userid() 가 따로 좁힌다 — 두 이름공간이다.
        if (!valid_browser_user(uid)) {
            logf("!", "user_id 가 너무 길거나 제어문자 포함(" + std::to_string(uid.size()) + "B) — 거절");
            send_err(fd, rid, "bad_request", "user_id 가 너무 깁니다");
            return;
        }
        if (!device_online()) {                                   // §7.1
            send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
            return;
        }
        // 서버가 아는 상태로 미리 거를 수도 있지만, 최종 판정은 아두이노 ACK 다(§7.2).
        dispatch(type == "reserve" ? 'R' : 'C', fd, rid, slot, uid);
    }

    // ---------- HTTP / WS 업그레이드
    static std::string header(const std::string& req, const char* name) {
        std::string low, ln = name;
        for (size_t i = 0; i < req.size(); i++) low += (char)tolower((unsigned char)req[i]);
        for (size_t i = 0; i < ln.size(); i++) ln[i] = (char)tolower((unsigned char)ln[i]);
        size_t i = low.find("\n" + ln + ":");
        if (i == std::string::npos) return "";
        i = req.find(':', i + 1) + 1;
        size_t e = req.find('\r', i);
        if (e == std::string::npos) e = req.find('\n', i);
        std::string v = req.substr(i, e - i);
        while (!v.empty() && (v[0]==' '||v[0]=='\t')) v.erase(0,1);
        while (!v.empty() && (v[v.size()-1]==' '||v[v.size()-1]=='\r')) v.erase(v.size()-1);
        return v;
    }
    void serve_file(sock_t fd, std::string path) {
        if (path == "/") path = "/index.html";
        // 경로 탈출 차단 — 데모여도 디렉터리를 서빙하는 코드에 이건 기본이다
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
            const char* r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r));
            return;
        }
        std::string fn = path.substr(1);
        size_t q = fn.find('?');
        if (q != std::string::npos) fn = fn.substr(0, q);   // ?t=... 캐시버스터

        std::ifstream f(fn.c_str(), std::ios::binary);
        if (!f) {
            const char* r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r));
            return;
        }
        std::ostringstream ss; ss << f.rdbuf();
        std::string body = ss.str();
        const char* ct = "application/octet-stream";
        if (fn.size() > 5 && fn.substr(fn.size()-5) == ".html") ct = "text/html; charset=utf-8";
        else if (fn.size() > 5 && fn.substr(fn.size()-5) == ".json") ct = "application/json; charset=utf-8";
        else if (fn.size() > 3 && fn.substr(fn.size()-3) == ".js") ct = "application/javascript";
        else if (fn.size() > 4 && fn.substr(fn.size()-4) == ".css") ct = "text/css";

        std::ostringstream h;
        h << "HTTP/1.1 200 OK\r\nContent-Type: " << ct
          << "\r\nContent-Length: " << body.size()
          << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        std::string head = h.str();
        send_raw(fd, head.data(), head.size());
        send_raw(fd, body.data(), body.size());
    }
    // 반환 false = 이 연결을 닫아라. **여기서 직접 erase 하지 않는다** —
    // 호출부가 conns 를 순회 중이라 안에서 지우면 반복자가 무효화된다(실제로 SIGSEGV 를 냈다).
    bool on_http(sock_t fd, Conn& c) {
        size_t end = c.inbuf.find("\r\n\r\n");
        if (end == std::string::npos) return true;
        std::string req = c.inbuf.substr(0, end);
        c.inbuf.erase(0, end + 4);

        size_t sp1 = req.find(' ');
        size_t sp2 = req.find(' ', sp1 + 1);
        std::string path = (sp1 != std::string::npos && sp2 != std::string::npos)
                         ? req.substr(sp1 + 1, sp2 - sp1 - 1) : "/";

        std::string key = header(req, "Sec-WebSocket-Key");
        if (!key.empty() && path.substr(0, 3) == "/ws") {
            std::string acc = ws_accept(key);
            std::string r = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n";
            send_raw(fd, r.data(), r.size());
            c.kind = Conn::WS;
            logf("+WS", "업그레이드 완료");
            ws_send(fd, snapshot_json());          // 접속 즉시 현재 상태
            return true;
        }
        serve_file(fd, path);
        return false;                              // 정적 응답은 Connection: close
    }
    // WS 프레임 파싱 — 클라이언트 프레임은 **항상 마스킹**돼 있다(§5.2)
    bool ws_pump(sock_t fd, Conn& c) {
        while (true) {
            if (c.inbuf.size() < 2) return true;
            const unsigned char* p = (const unsigned char*)c.inbuf.data();
            int opcode = p[0] & 0x0F;
            bool masked = (p[1] & 0x80) != 0;
            uint64_t len = p[1] & 0x7F;
            size_t off = 2;
            if (len == 126) {
                if (c.inbuf.size() < 4) return true;
                len = (uint64_t(p[2]) << 8) | p[3]; off = 4;
            } else if (len == 127) {
                if (c.inbuf.size() < 10) return true;
                len = 0;
                for (int i = 0; i < 8; i++) len = (len << 8) | p[2+i];
                off = 10;
            }
            if (!masked) { logf("!", "마스킹 안 된 클라이언트 프레임 — 연결 종료"); return false; }
            // 선언된 길이를 그대로 믿으면 inbuf 가 무한히 커진다. 브라우저→서버 메시지는
            // 예약/취소 JSON 뿐이라 수백 바이트면 충분하다.
            if (len > WS_MAX_FRAME) {
                logf("!", "WS 프레임 길이 " + std::to_string((unsigned long long)len)
                          + " 바이트 — 상한 초과, 연결 종료");
                return false;
            }
            if (c.inbuf.size() < off + 4 + len) return true;
            unsigned char mask[4];
            for (int i = 0; i < 4; i++) mask[i] = p[off + i];
            std::string payload;
            payload.reserve((size_t)len);
            for (uint64_t i = 0; i < len; i++)
                payload += char(p[off + 4 + i] ^ mask[i % 4]);
            c.inbuf.erase(0, (size_t)(off + 4 + len));

            if (opcode == 0x8) return false;                       // close
            if (opcode == 0x9) {                                   // ping → pong
                // ping 페이로드는 125바이트 이하만 정상이다(제어 프레임 규칙). 넘으면 잘라 답한다.
                if (payload.size() > 125) payload.resize(125);
                std::string f; f += char(0x8A); f += char((unsigned char)payload.size()); f += payload;
                send_raw(fd, f.data(), f.size());
                continue;
            }
            if (opcode == 0x1) on_ws_message(fd, payload);
        }
    }

    // ================= 폰(digitcam) 수신 · 번호판 매칭 =========================
    // 수신부는 net/test_server.py 에서 들어왔다 — 바이트 버퍼에 모으고 LF 로 자른 뒤
    // **그 다음에** 디코딩한다. 한글이 recv 경계에서 갈리기 때문이고, 그 파일이
    // 그 경우를 실측으로 검증해 뒀다(digitcam 명세 §8.3, §10.6).
    std::map<std::string, std::string> plate_slot;   // 번호판 → 배정된 자리

    int find_slot_by_user(const std::string& plate) const {
        for (int i = 0; i < 10; i++)
            if (slots[i].reserved && slots[i].user_id == plate) return i;
        return -1;
    }
    // 빈자리 선택 규칙: **§1 의 전선 인덱스 순서(A1..A5,B1..B5)에서 가장 앞.**
    // 근거 — 결정적이라 같은 입력이면 항상 같은 결과가 나오고(재현 가능한 시험),
    // 명세가 이미 정한 유일한 자리 순서라 새 순서를 발명하지 않는다.
    // 입구에서 가까운 순 같은 물리 배치 기준은 **지금 아무도 갖고 있지 않다** —
    // 배치도가 생기면 이 함수 하나만 바꾸면 된다.
    int pick_free_slot() const {
        for (int i = 0; i < 10; i++)
            if (!slots[i].occupied && !slots[i].reserved) return i;
        return -1;
    }

    void on_plate(const std::string& plate, const std::string& dev) {
        logf("←폰", "번호판 " + plate + " (device=" + dev + ")");

        // (1) 같은 번호판이 또 왔다 — 이미 배정된 자리가 살아 있으면 새 입차가 아니다.
        //     fresh 는 상승 엣지지만 인식이 끊겼다 다시 잡히면 새 엣지가 뜬다.
        //     **시간 창을 두지 않고 상태로 판정한다** — 타이머는 근거 없는 숫자를 만든다.
        std::map<std::string, std::string>::iterator it = plate_slot.find(plate);
        if (it != plate_slot.end()) {
            int i = slot_index(it->second);
            if (i >= 0 && slots[i].user_id == plate && (slots[i].reserved || slots[i].occupied)) {
                logf("=", "중복 수신 — " + plate + " 는 이미 " + it->second
                          + " 에 배정돼 있다. 새로 배정하지 않는다");
                return;
            }
            plate_slot.erase(it);   // 그 사이 은퇴/취소됐다 → 다시 배정 가능
        }

        // (2) 이미 주차된 차의 번호판이 게이트에 나타났다.
        //     오인식일 수도, 실제로 나갔다 다시 온 것일 수도 있다 — 서버는 구분할 수 없다.
        //     **새 자리를 배정하지 않는다.** 배정하면 같은 차가 두 칸을 먹는다.
        //     사람이 볼 수 있게 로그로 올린다.
        for (int i = 0; i < 10; i++) {
            if (slots[i].occupied && slots[i].user_id == plate) {
                logf("!", "이미 주차된 번호판이 게이트에서 인식됨 — " + plate + " (" + SLOT_ID[i]
                          + "). 오인식이거나 재진입이다. 새 배정 없음");
                return;
            }
        }

        // (3) 예약이 있으면 그 자리로 확정한다. 키는 번호판이다.
        int i = find_slot_by_user(plate);
        if (i >= 0) {
            plate_slot[plate] = SLOT_ID[i];
            logf("✓", "예약 확인 — " + plate + " → " + SLOT_ID[i] + " (안내 대상 자리)");
            push_snapshot();
            return;
        }

        // (4) 예약이 없으면 빈자리를 배정한다(사용자 요구: "안 하면 빈자리").
        int f = pick_free_slot();
        if (f < 0) {
            // 조용히 지나가지 않는다. 브라우저로 올리려면 새 WS 메시지가 필요한데
            // 그건 명세 변경이라 하지 않았다(REQ-0037 은 명세 무변경). 루트에 보고했다.
            logf("!", "빈자리 없음 — " + plate + " 배정 실패. 차단기를 열 자리가 없다");
            return;
        }
        if (!device_online()) {
            logf("!", "아두이노 미연결 — " + plate + " 배정 보류(예약을 내릴 수 없다)");
            return;
        }
        plate_slot[plate] = SLOT_ID[f];
        logf("✓", "예약 없음 → 빈자리 배정 " + plate + " → " + SLOT_ID[f]);
        dispatch('R', BAD_SOCK, "", SLOT_ID[f], plate);   // 예약의 주인은 서버다(§7.4)
    }

    void on_phone_line(const std::string& line) {
        // digitcam 명세 §5: plain 모드는 번호판 문자열 한 줄, json 모드는 오브젝트 하나.
        std::string plate, dev;
        if (!line.empty() && line[0] == '{') {
            plate = jget(line, "value");
            dev = jget(line, "device");
        } else {
            plate = line;
        }
        if (plate.empty()) {
            // §5.2 — 송신 측은 빈 값을 보내지 않지만 수신 측은 견딘다.
            logf("!", "폰: 빈 값 — 무시");
            return;
        }
        if (plate.size() > MAX_PLATE_BYTES) {
            logf("!", "폰: 번호판이 너무 길다(" + std::to_string(plate.size()) + "B) — 무시");
            return;
        }
        // **신뢰도 임계값을 두지 않는다.** 폰이 이미 min_confidence 로 걸렀고,
        // conf 는 문자별 최소값이라 문자 수가 늘수록 낮아진다(digitcam 명세 §4.4 경고).
        // 서버가 절대값으로 자르면 **긴 번호판만 유독 거절된다.**
        on_plate(plate, dev.empty() ? std::string("?") : dev);
    }

    // 기동 시 data_log.json 이 없으면 **빈 배열로 만들어 둔다.**
    //
    // 왜: 서버는 §9.4 대로 상태가 바뀔 때만 파일을 쓴다. 아두이노가 안 붙으면 한 번도 안 쓴다.
    // 그런데 화면은 WS 가 붙기 전부터 폴백 폴링을 돌리므로 **아무 문제 없는 첫 실행에서
    // 404 를 받아 빨간 오류를 띄운다.** 이런 것이 쌓이면 진짜 오류를 무시하게 된다.
    //
    // `[]` 는 §9.1 형태에서 "아직 기록이 없다"를 정직하게 표현한다 —
    // 없는 파일보다 낫고, 지어낸 값을 넣는 것보다도 낫다.
    //
    // **이미 있으면 절대 덮지 않는다.** 덮으면 재시작할 때마다 직전 2건이 날아간다.
    void ensure_log_exists() {
        std::ifstream f("data_log.json", std::ios::binary);
        if (f.good()) {
            logf("=", "data_log.json 이 이미 있다 — 그대로 둔다(직전 기록 보존)");
            return;
        }
        atomic_write_log("[]\n");     // 쓰기 경로는 원자적 교체 하나뿐이다
        logf("=", "data_log.json 이 없어 빈 배열로 만들었다");
    }

    // ---------- 소켓 준비
    sock_t listen_on(int port) {
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == BAD_SOCK) return BAD_SOCK;
        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons((u_short)port);
        if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
            std::cerr << "바인드 실패 포트 " << port << " (err " << sockerr() << ")\n";
            closesock(s); return BAD_SOCK;
        }
        if (listen(s, 8) != 0) { closesock(s); return BAD_SOCK; }
        return s;
    }

    int run() {
        lsn_ard   = listen_on(PORT_ARDUINO);
        lsn_http  = listen_on(PORT_HTTP);
        lsn_phone = listen_on(PORT_PHONE);
        if (lsn_ard == BAD_SOCK || lsn_http == BAD_SOCK || lsn_phone == BAD_SOCK) return 1;
        std::cout << "주차 관제 서버 — 아두이노 TCP " << PORT_ARDUINO
                  << " · HTTP/WS " << PORT_HTTP
                  << " · 폰(digitcam) " << PORT_PHONE << "\n"
                  << "명세: docs/net/parking-protocol.md\n"
                  << "-----------------------------------------------------------\n";
        std::cout.flush();
        ensure_log_exists();

        while (true) {
            fd_set rd; FD_ZERO(&rd);
            sock_t mx = 0;
            FD_SET(lsn_ard, &rd);  if (lsn_ard  > mx) mx = lsn_ard;
            FD_SET(lsn_http, &rd); if (lsn_http > mx) mx = lsn_http;
            FD_SET(lsn_phone, &rd); if (lsn_phone > mx) mx = lsn_phone;
            for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            if (ard != BAD_SOCK) { FD_SET(ard, &rd); if (ard > mx) mx = ard; }
            for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            // 타이머(재전송·offline)를 돌려야 하므로 NULL 을 주면 안 된다.
            // 소켓이 조용해도 이 주기로 깨어나 tick() 을 돈다.
            timeval tv; tv.tv_sec = 0; tv.tv_usec = SELECT_TICK_MS * 1000;
            int n = select((int)mx + 1, &rd, NULL, NULL, &tv);

            bool was_online = device_online();

            if (n > 0) {
                if (FD_ISSET(lsn_ard, &rd)) {
                    sock_t c = accept(lsn_ard, NULL, NULL);
                    if (c != BAD_SOCK) {
                        if (ard != BAD_SOCK) { closesock(ard); conns.erase(ard); }
                        ard = c; ard_buf.clear();
                        logf("+ARD", "아두이노 접속");
                        // 새 연결 = 재부팅했을 수 있다(§7.4)
                        ard_seq = -1; ard_uptime = -1;
                        // **은퇴 기준선도 반드시 버린다**(§7.5-1). 이 줄이 없으면 재부팅 전의
                        // occupied 를 기준선으로 들고 있다가, 재부팅으로 0 이 된 첫 프레임에서
                        // 있지도 않은 1→0 전이를 감지해 **방금 재하달한 예약을 그 자리에서 죽인다.**
                        // (uptime/seq 역행 경로와 달리 여기서는 ard_uptime 이 -1 로 초기화돼
                        //  reboot 판정이 false 가 되므로, base_valid 를 끄지 않으면 판정이 그대로 돈다.)
                        base_valid = false;
                        resync_reservations("새 연결");
                    }
                }
                if (FD_ISSET(lsn_http, &rd)) {
                    sock_t c = accept(lsn_http, NULL, NULL);
                    if (c != BAD_SOCK) conns[c] = Conn();
                }
                if (FD_ISSET(lsn_phone, &rd)) {
                    sock_t c = accept(lsn_phone, NULL, NULL);
                    if (c != BAD_SOCK) { phones[c] = std::string(); logf("+폰", "digitcam 접속"); }
                }
                // 폰 연결들 — 순회 중 map 을 건드리지 않도록 fd 를 먼저 모은다
                {
                    std::vector<sock_t> pr, pdrop;
                    for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it)
                        if (FD_ISSET(it->first, &rd)) pr.push_back(it->first);
                    for (size_t k = 0; k < pr.size(); k++) {
                        sock_t fd = pr[k];
                        char b[2048];
                        int r = (int)recv(fd, b, sizeof(b), 0);
                        if (r <= 0) { pdrop.push_back(fd); continue; }
                        std::string& pb = phones[fd];
                        pb.append(b, r);
                        size_t i;
                        while ((i = pb.find('\n')) != std::string::npos) {
                            std::string line = pb.substr(0, i);
                            pb.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (!line.empty()) on_phone_line(line);
                        }
                        if (pb.size() > MAX_PHONE_LINE) {
                            logf("!", "폰: LF 없이 상한 초과 — 버퍼 비움");
                            pb.clear();
                        }
                    }
                    for (size_t k = 0; k < pdrop.size(); k++) {
                        // LF 없이 남은 조각은 완성된 줄이 아니다 — 값으로 쓰지도, 조용히 버리지도 않는다
                        if (phones.count(pdrop[k]) && !phones[pdrop[k]].empty())
                            logf("!", "폰 해제 — 미종단 잔여 "
                                      + std::to_string(phones[pdrop[k]].size()) + "B 버림");
                        else logf("-폰", "digitcam 연결 종료");
                        closesock(pdrop[k]); phones.erase(pdrop[k]);
                    }
                }
                if (ard != BAD_SOCK && FD_ISSET(ard, &rd)) {
                    char b[2048];
                    int r = (int)recv(ard, b, sizeof(b), 0);
                    if (r <= 0) {
                        logf("-ARD", "아두이노 연결 종료");
                        closesock(ard); ard = BAD_SOCK; ard_buf.clear();
                        push_snapshot();
                    } else {
                        ard_buf.append(b, r);
                        size_t i;
                        while ((i = ard_buf.find('\n')) != std::string::npos) {
                            std::string line = ard_buf.substr(0, i);
                            ard_buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (line.size() + 1 > (size_t)MAX_LINE) { logf("!", "64B 초과 줄 — 버림"); continue; }
                            if (!line.empty()) on_ard_line(line);
                        }
                        if (ard_buf.size() > (size_t)MAX_LINE) {
                            logf("!", "LF 없이 64B 초과 — 버퍼 비움");
                            ard_buf.clear();
                        }
                    }
                }
                // 준비된 fd 를 먼저 모은 뒤 처리한다. 순회 중에 conns 를 건드리면
                // 반복자가 무효화된다 — 그대로 두면 SIGSEGV 다(실제로 겪었다).
                std::vector<sock_t> ready, drop;
                for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it)
                    if (FD_ISSET(it->first, &rd)) ready.push_back(it->first);

                for (size_t k = 0; k < ready.size(); k++) {
                    sock_t fd = ready[k];
                    if (!conns.count(fd)) continue;
                    Conn& c = conns[fd];               // map 의 참조는 erase 전까지 안정적이다
                    char b[4096];
                    int r = (int)recv(fd, b, sizeof(b), 0);
                    if (r <= 0) { drop.push_back(fd); continue; }
                    c.inbuf.append(b, r);
                    if (c.kind == Conn::HTTP) {
                        if (!on_http(fd, c)) { drop.push_back(fd); continue; }
                    }
                    if (c.kind == Conn::WS && !ws_pump(fd, c)) drop.push_back(fd);
                }
                for (size_t k = 0; k < drop.size(); k++) {
                    if (conns.count(drop[k])) { closesock(drop[k]); conns.erase(drop[k]); }
                }
            }

            tick();                                   // 소켓이 조용해도 매 주기 돈다
            if (was_online && !device_online()) {
                logf("!", "아두이노 오프라인 판정(3.5초 무프레임)");
                push_snapshot();
            }
        }
    }
};

// ---------------------------------------------------------------- 자가검증
static int selftest() {
    int bad = 0;
    // RFC 6455 §1.3 예제 벡터 — SHA-1 과 base64 를 한 번에 검증한다
    std::string acc = ws_accept("dGhlIHNhbXBsZSBub25jZQ==");
    std::cout << "Sec-WebSocket-Accept : " << acc << "\n"
              << "기대값               : s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\n";
    if (acc != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") { std::cout << "  ✗ SHA-1/base64 불일치\n"; bad++; }
    else std::cout << "  ✓ 일치\n";

    // 명세 §2.5 의 예제 라인 8건
    const char* L[] = {
        "S,1234,0110100011,0000100000,3600,P1,33",
        "S,0,0000000000,0000000000,0,P1,32",
        "S,65535,1111111111,1111111111,4294967,P1,31",
        "A,42,B3,0,06", "A,42,B3,2,04",
        "R,42,B3,u17,56", "R,7,A1,,15", "C,43,B3,19",
        // v1.2 (개정 3) — tmask 가 붙은 S, 그리고 T 4종
        "S,1236,0110100011,0000100000,3602,P1,-,32",
        "S,1234,0110100011,0000100000,3600,P1,0000000000,1F",
        "S,65535,1111111111,1111111111,4294967,DEVICE12,1111111111,67",
        "A,42,??,3,74", "A,50,??,0,74", "A,52,A3,4,00",
        "T,50,A,??,-,11", "T,51,D,??,-,15", "T,52,S,A3,1,6F", "T,54,X,A3,-,7E",
        // v1.4 (개정 5) — 시뮬 한 걸음
        "M,60,4B", "M,65535,7D", "A,60,A3,0,05", "A,60,??,5,72",
    };
    for (int i = 0; i < 22; i++) {
        std::vector<std::string> f;
        bool ok = verify_line(L[i], f);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << L[i] << "\n";
        if (!ok) bad++;
    }
    std::cout << (bad ? "자가검증 실패\n" : "자가검증 통과\n");
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2,2), &w) != 0) { std::cerr << "Winsock 초기화 실패\n"; return 1; }
#else
    signal(SIGPIPE, SIG_IGN);   // 끊긴 소켓에 write 해도 프로세스가 죽지 않게
#endif
    int rc;
    if (argc > 1 && std::string(argv[1]) == "--selftest") rc = selftest();
    else { Server s; rc = s.run(); }
#ifdef _WIN32
    WSACleanup();
#endif
    return rc;
}
