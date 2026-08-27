// golden_uplink.cpp — **arduino 의 골든 표본을 내 서버 파서에 먹인다** (REQ-0402/0403)
//
// ★ 나누는 선(arduino 제안): **각자 *남의* 출력을 *자기* 파서에 먹인다.**
//   자기 출력을 자기 파서에 먹이면 아무것도 안 나온다 — 어제 서로에게서 하나씩 찾은 이유가 그것이다.
//
// 입력: docs/arduino/GOLDEN-uplink-2026-08-25.txt  (arduino 가 장치 송신부로 생성)
// 파서: 조별과제샘플/VS_server_multi/server_multi/server_device.h 의 `verify_line`(그대로 옮김)
//       + nodes.h `decode_mod_bits` 의 hex 갈래 · `all_digits`
//
// 🔴 **실패 사례가 반드시 거부되는지도 본다** — 통과만 있는 시험은 통과만 한다(arduino 규율).
//
//   c++ -std=c++11 -O0 -Wall -Wextra -o /tmp/golden_uplink golden_uplink.cpp && /tmp/golden_uplink
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── 서버 코덱 (server_device.h:120~141 그대로) ─────────────────────────────
static std::string cksum(const std::string& p) {
    unsigned char x = 0;
    for (size_t i = 0; i < p.size(); i++) x ^= (unsigned char)p[i];
    char b[3]; snprintf(b, sizeof(b), "%02X", x);
    return std::string(b);
}
static bool verify_line(const std::string& line, std::vector<std::string>& out) {
    size_t cut = line.rfind(',');
    if (cut == std::string::npos) return false;
    if (cksum(line.substr(0, cut + 1)) != line.substr(cut + 1)) return false;
    out.clear();
    std::string body = line.substr(0, cut), cur;
    for (size_t i = 0; i < body.size(); i++) {
        if (body[i] == ',') { out.push_back(cur); cur.clear(); } else cur += body[i];
    }
    out.push_back(cur);
    return true;
}
static bool all_digits(const std::string& x) {
    if (x.empty() || x.size() > 5) return false;
    for (size_t i = 0; i < x.size(); i++) if (x[i] < '0' || x[i] > '9') return false;
    return true;
}
static int decode_hex_bits(int reg_n, const std::string& fld, int* out) {
    for (int i = 0; i < 32; i++) out[i] = 0;
    if (reg_n <= 0) return 0;
    if (fld.size() != (size_t)((reg_n + 3) / 4)) return -1;   // 폭 불일치
    unsigned long long v = strtoull(fld.c_str(), NULL, 16);
    for (int i = 0; i < reg_n && i < 32; i++)
        out[i] = ((v >> (reg_n - 1 - i)) & 1ULL) ? 1 : 0;
    return reg_n;
}

static int g_pass = 0, g_fail = 0;
static void ok(bool c, const std::string& w) {
    if (c) { g_pass++; printf("  ✅ %s\n", w.c_str()); }
    else   { g_fail++; printf("  ❌ **FAIL** %s\n", w.c_str()); }
}

// 표본 한 줄이 **받아들여져야** 한다
static void accept(const std::string& line, int reg_n, const char* devid) {
    std::vector<std::string> f;
    if (!verify_line(line, f)) { ok(false, line + "  ← 체크섬 거부됨(받아들여야 하는데)"); return; }
    if (f[0] == "S") {
        bool okf = f.size() >= 6;
        if (okf && devid) okf = (f[5] == devid);
        int bits[32];
        int n = decode_hex_bits(reg_n, f[2], bits);
        char b[240];
        snprintf(b, sizeof(b), "%-34s S 필드%zu · occ폭%zu(기대 %d) · 해독 %s",
                 line.c_str(), f.size(), f[2].size(), (reg_n + 3) / 4,
                 n > 0 ? "OK" : (n == 0 ? "안함" : "폭불일치"));
        ok(okf && n > 0, b);
    } else if (f[0] == "D") {
        bool head = (f.size() >= 2 && f[1] == "*");
        char b[240];
        if (head) {
            snprintf(b, sizeof(b), "%-34s D머리 필드%zu · drain=%s n=%s",
                     line.c_str(), f.size(), f[2].c_str(), f[3].c_str());
            ok(f.size() >= 4 && all_digits(f[2]) && all_digits(f[3]), b);
        } else {
            snprintf(b, sizeof(b), "%-34s D모듈 필드%zu · %s/%s",
                     line.c_str(), f.size(), f[1].c_str(), f[2].c_str());
            ok(f.size() >= 3 && !f[2].empty(), b);
        }
    } else if (f[0] == "A") {
        char b[240];
        snprintf(b, sizeof(b), "%-34s A 필드%zu · rid=%s slot=%s result=%s",
                 line.c_str(), f.size(), f[1].c_str(), f[2].c_str(), f[3].c_str());
        ok(f.size() >= 4, b);
    } else ok(false, line + "  ← 모르는 타입");
}
// 표본 한 줄이 **거부돼야** 한다
static void reject(const std::string& line, int reg_n, const char* why) {
    std::vector<std::string> f;
    bool refused = !verify_line(line, f);
    std::string how = "체크섬";
    if (!refused) {                       // 체크섬은 통과 — 그 다음 관문에서 걸려야 한다
        if (f[0] == "S") {
            if (f.size() < 6) { refused = true; how = "필드수<6"; }
            else { int b[32]; if (decode_hex_bits(reg_n, f[2], b) < 0) { refused = true; how = "occ폭"; } }
        } else if (f[0] == "D" && f.size() >= 4 && f[1] == "*") {
            if (!all_digits(f[2]) || !all_digits(f[3])) { refused = true; how = "숫자아님"; }
        }
    }
    ok(refused, std::string(why) + " → " + (refused ? ("거부(" + how + ")") : "🔴 **통과됨**"));
}

int main() {
    printf("═══ 골든 표본 왕복 — arduino 장치 출력 → 내 서버 파서 ═══\n");
    printf("입력: docs/arduino/GOLDEN-uplink-2026-08-25.txt (arduino 생성)\n\n");

    printf("[P1] 모듈 10 · occ/res 폭 3\n");
    accept("S,0,000,000,0,P1,32", 10, "P1");
    accept("S,1,200,000,3,P1,32", 10, "P1");
    accept("S,42,2A8,100,3600,P1,4B", 10, "P1");
    accept("S,65535,3FF,3FF,86399,P1,3F", 10, "P1");
    accept("S,7,300,000,10,P1,200,19", 10, "P1");        // tmask 7필드

    printf("\n[P1] 등록 배치 — 머리 + 모듈 10줄\n");
    const char* p1reg[] = {"D,*,7,10,58","D,A1,IP,01","D,A2,IP,02","D,A3,IP,03","D,U1,IP,15",
                           "D,U2,IP,16","D,ED,OG,61","D,XD,OG,7C","D,L1,OG,1D","D,L2,OG,1E","D,L3,OG,1F"};
    size_t total = 0;
    for (size_t i = 0; i < sizeof(p1reg)/sizeof(p1reg[0]); i++) {
        accept(p1reg[i], 10, 0);
        total += strlen(p1reg[i]) + (i ? 1 : 0);         // 🔑 모듈 줄 **앞에** LF (마지막 LF 없음)
    }
    char lb[160];
    snprintf(lb, sizeof(lb), "🔑 등록 배치 길이 = **%zuB** (arduino 실측 121B)", total);
    ok(total == 121, lb);

    printf("\n[P2] 모듈 2 · occ/res 폭 1\n");
    accept("S,0,0,0,0,P2,31", 2, "P2");
    accept("S,1,2,0,3,P2,31", 2, "P2");
    accept("S,42,2,1,3600,P2,31", 2, "P2");
    accept("S,65535,3,3,86399,P2,3C", 2, "P2");
    accept("S,7,3,0,10,P2,2,1A", 2, "P2");
    const char* p2reg[] = {"D,*,7,2,6B","D,A4,IP,04","D,L4,OG,18"};
    size_t t2 = 0;
    for (size_t i = 0; i < 3; i++) { accept(p2reg[i], 2, 0); t2 += strlen(p2reg[i]) + (i ? 1 : 0); }
    snprintf(lb, sizeof(lb), "🔑 P2 등록 배치 = **%zuB** (arduino 실측 32B)", t2);
    ok(t2 == 32, lb);

    printf("\n[A] ACK\n");
    accept("A,7,A1,0,36", 0, 0);
    accept("A,12,L1,0,0F", 0, 0);
    accept("A,99,G3,0,05", 0, 0);      // slot 이 자리 이름이 아니다
    accept("A,999,G0,3,3C", 0, 0);     // result 3 = 수행 불가
    accept("A,0,??,3,42", 0, 0);       // slot "??"

    printf("\n🔴 [실패 사례] — 내 파서가 **거부해야** 한다\n");
    reject("S,1,200,000,5,P1,00", 10, "체크섬 틀림");
    // 🔴🔴 **arduino 표본의 이 줄은 라벨이 틀렸다** — 값으로 잡았다:
    //   `S,1,001,000,5,P1,` 의 올바른 체크섬이 **`37`** 이고 거기엔 **A~F 가 없다.**
    //   소문자로 바꿀 글자가 없어 **원본 그대로**가 됐고, 그래서 **유효한 프레임**이다.
    //   ★ 그쪽이 자기 하니스에서 찾은 함정(*"`72` 라 소문자가 안 바뀌는데 PASS"*)이
    //     **표본 파일에는 그대로 남아 있었다.**
    //   🔑 arduino 의 문장 그대로다: **"관대해서가 아니라 입력이 그 축을 안 건드린 것이다.
    //     통과했다가 쟀다가 아니다."**
    // ✅ **개정 2 반영** — arduino 가 표본을 고쳤고, 새 입력은 축이 살아 있다:
    //   `S,0,2A8,100,3600,P1,7D` 가 유효한 대문자 판이고 **`7d` 만 거부돼야 한다.**
    //   🔑 그쪽 생성기가 이제 `assert lo != up` 으로 **축이 안 살면 파일을 아예 안 만든다.**
    accept("S,0,2A8,100,3600,P1,7D", 10, "P1");                    // 대문자 판은 **유효**
    reject("S,0,2A8,100,3600,P1,7d", 10, "체크섬 소문자(개정2 · 7D→7d)");
    reject("S,1,001,000,5,7A", 10, "필드 5개(devid 없음)");
    reject("S,1,00001,000,5,P1,37", 10, "occ 폭 5(P1 은 3)");
    reject("D,*,7,X,01", 10, "n 이 숫자가 아니다");

    printf("\n═══ 결과: %d pass · %d fail ═══\n", g_pass, g_fail);
    if (g_fail) printf("🔴 **FAIL** — 서버 파서와 장치 출력이 갈렸다. 그 줄을 arduino 에게 보내라.\n");
    return g_fail ? 1 : 0;
}
