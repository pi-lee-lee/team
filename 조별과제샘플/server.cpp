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
    std::string slot, user_id;
    char kind;               // 'R' | 'C'
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
    sock_t lsn_ard, lsn_http;
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

    Server() : lsn_ard(BAD_SOCK), lsn_http(BAD_SOCK), ard(BAD_SOCK),
               ard_seen(false), ard_last_ms(0), ard_uptime(-1), ard_seq(-1),
               ard_dev("?"), next_rid(1), base_valid(false) {
        for (int i = 0; i < 10; i++) base_occ[i] = 0;
    }

    bool device_online() const {
        return ard != BAD_SOCK && ard_seen && (now_ms() - ard_last_ms) < OFFLINE_MS;
    }
    static int slot_index(const std::string& s) {
        for (int i = 0; i < 10; i++) if (s == SLOT_ID[i]) return i;
        return -1;
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
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq) << "},\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) o << ",";
            o << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved << ",\"user_id\":";
            if (slots[i].user_id.empty()) o << "null"; else o << jstr(slots[i].user_id);
            o << ",\"reserved_at\":";
            if (slots[i].reserved_at == 0) o << "null"; else o << slots[i].reserved_at;
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
    void send_ack(sock_t fd, const std::string& rid, const std::string& slot, int result) {
        const char* m = "예약되었습니다";
        if (result == 1) m = "이미 주차된 자리입니다";
        else if (result == 2) m = "이미 예약된 자리입니다";
        else if (result == 3) m = "잘못된 요청입니다";
        std::ostringstream o;
        o << "{\"type\":\"ack\",\"rid\":" << jstr(rid) << ",\"slot\":\"" << slot
          << "\",\"result\":" << result << ",\"message\":" << jstr(m) << "}";
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
        std::string key = bits(false) + "|" + bits(true);
        if (key == last_bits) return;            // §9.4 — 내용이 같으면 안 쓴다
        last_bits = key;

        std::ostringstream e;
        e << "{\"ts\":" << epoch_ms() << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq)
          << ",\"occupied\":\"" << bits(false) << "\",\"reserved\":\"" << bits(true)
          << "\",\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) e << ",";
            e << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved << "}";
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

        // tmp 에 다 쓰고 원자적으로 갈아끼운다 (§9.2). 잠금도 복사본도 필요 없다.
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
        p.slot = slot; p.user_id = uid; p.kind = kind;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;

        char buf[64];
        if (kind == 'R') snprintf(buf, sizeof(buf), "R,%u,%s,%s,", rid, slot.c_str(), uid.c_str());
        else             snprintf(buf, sizeof(buf), "C,%u,%s,", rid, slot.c_str());
        send_ard(build_line(buf));
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
            if (p.kind == 'R') snprintf(buf, sizeof(buf), "R,%u,%s,%s,", p.wire_rid, p.slot.c_str(), p.user_id.c_str());
            else               snprintf(buf, sizeof(buf), "C,%u,%s,", p.wire_rid, p.slot.c_str());
            logf("↻", "재전송 " + std::to_string(p.tries) + "/" + std::to_string(ACK_MAX_TRIES)
                      + " (같은 wire_rid=" + std::to_string(p.wire_rid) + ")");
            send_ard(build_line(buf));
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

            if (reboot) resync_reservations("uptime/seq 역행");

            // §7.5 — occupied 1→0 이면 예약 소진.
            // 재부팅했거나 기준선이 없으면 **판정하지 않고 기준선만 세운다**(§7.5-1).
            // 이걸 빼면 재부팅으로 occupied 가 초기화될 때 있지도 않은 1→0 전이가 잡혀
            // 방금 재하달한 예약을 그 자리에서 죽인다.
            if (base_valid && !reboot) {
                for (int i = 0; i < 10; i++)
                    if (base_occ[i] == 1 && occ[i] == 0 && slots[i].reserved == 1
                        && !has_pending_for(SLOT_ID[i]))
                        retire(i);
            } else {
                logf("=", "은퇴 기준선 설정 — 이 프레임은 전이 판정을 건너뛴다");
            }
            for (int i = 0; i < 10; i++) { base_occ[i] = occ[i]; slots[i].occupied = occ[i]; }
            base_valid = true;

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
            if (result == 0 && idx >= 0) {
                if (p.kind == 'R') {
                    slots[idx].reserved = 1;
                    slots[idx].user_id = p.user_id;
                    slots[idx].reserved_at = epoch_ms();
                } else {
                    slots[idx].reserved = 0;
                    slots[idx].user_id.clear();
                    slots[idx].reserved_at = 0;
                }
            }
            if (p.ws_fd != BAD_SOCK) send_ack(p.ws_fd, p.browser_rid, slot, result);
            write_log_if_changed();
            push_snapshot();
        }
        else {
            logf("!", "모르는 타입 — 조용히 버림");
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
            while (i < s.size() && s[i] != '"') { if (s[i]=='\\' && i+1<s.size()) i++; o += s[i++]; }
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

        if (type != "reserve" && type != "cancel") {
            send_err(fd, rid, "bad_request", "알 수 없는 요청입니다");
            return;
        }
        if (slot_index(slot) < 0) {
            send_err(fd, rid, "bad_request", "그런 자리가 없습니다");
            return;
        }
        if (!valid_userid(uid)) {                                  // §2.3 userid 문법
            logf("!", "user_id 가 명세 범위를 벗어남(길이 " + std::to_string(uid.size()) + ") — 거절");
            send_err(fd, rid, "bad_request",
                     "user_id 는 영숫자·_·- 로 8자 이내여야 합니다");
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
        lsn_ard  = listen_on(PORT_ARDUINO);
        lsn_http = listen_on(PORT_HTTP);
        if (lsn_ard == BAD_SOCK || lsn_http == BAD_SOCK) return 1;
        std::cout << "주차 관제 서버 — 아두이노 TCP " << PORT_ARDUINO
                  << " · HTTP/WS " << PORT_HTTP << "\n"
                  << "명세: docs/net/parking-protocol.md\n"
                  << "-----------------------------------------------------------\n";
        std::cout.flush();

        while (true) {
            fd_set rd; FD_ZERO(&rd);
            sock_t mx = 0;
            FD_SET(lsn_ard, &rd);  if (lsn_ard  > mx) mx = lsn_ard;
            FD_SET(lsn_http, &rd); if (lsn_http > mx) mx = lsn_http;
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
    };
    for (int i = 0; i < 8; i++) {
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
