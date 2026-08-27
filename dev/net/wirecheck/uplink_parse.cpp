// uplink_parse.cpp — **장치가 보낸 바이트열을 서버 파서에 먹인다** (REQ-0401/0402)
//
// ═══════════════════════════════════════════════════════════════════════════
// 🔴 **왜 있나**: 서버와 장치가 소스 대조로만 맞춰져 있었다. 우리 둘 다 못 본 축에
//   *"전선 한 바이트도 안 봤다"* 를 적었다. 코덱은 **순수 함수**라 서버를 안 돌리고도
//   바이트열을 실제로 통과시켜 볼 수 있다.
//   ★ cpp 의 `winparse` 와 같은 계열 — **안 도는 코드를 돌게 만들어서** 본다.
//
// ⚠ **서버를 띄우지 않는다.** 소켓도 스레드도 없다. `verify_line()` 과 폭 해독만 부른다.
//
// 🔴🔴 **하니스 규율**(arduino REQ-0403): **하니스가 대상보다 관대하면 시험이 결함을 숨긴다.**
//   그래서 **실패 사례를 반드시 넣는다.** 통과만 있는 시험은 통과만 한다.
//   ⚠ 그쪽은 `TWCR`/`TWINT` 를 흉내 안 냈으면 **아홉 개가 전부 거짓 통과**할 뻔했다.
//
// 돌리는 법:
//   cd net/wirecheck
//   c++ -std=c++11 -O0 -Wall -Wextra -o /tmp/uplink_parse uplink_parse.cpp && /tmp/uplink_parse
// ═══════════════════════════════════════════════════════════════════════════
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

// ── 🔴 **서버의 진짜 코덱을 복사가 아니라 그대로 옮겨 온다** ──────────────────
//   출처: 조별과제샘플/VS_server_multi/server_multi/server_device.h:120~141
//   ⚠ 여기를 고치면 시험이 **대상과 달라진다.** 고칠 일이 있으면 서버 쪽을 고치고 같이 옮겨라.
static std::string cksum(const std::string& prefix) {
    unsigned char x = 0;
    for (size_t i = 0; i < prefix.size(); i++) x ^= (unsigned char)prefix[i];
    char b[3]; snprintf(b, sizeof(b), "%02X", x);
    return std::string(b);
}
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
// 출처: nodes.h `decode_mod_bits()` 의 hex 갈래 (등록된 노드용)
//   🔑 `reg_done` 전에는 서버가 **아예 안 푼다** — 그 거동도 아래에서 시험한다.
static int decode_hex_bits(int reg_n, const std::string& fld, int* out) {
    for (int i = 0; i < 32; i++) out[i] = 0;
    if (reg_n <= 0) return 0;                       // 등록 전 = 안 푼다
    const size_t want = (size_t)((reg_n + 3) / 4);
    if (fld.size() != want) return -1;              // 폭 불일치(서버는 로그만 찍고 계속 푼다)
    unsigned long long v = strtoull(fld.c_str(), NULL, 16);
    for (int i = 0; i < reg_n && i < 32; i++)
        out[i] = ((v >> (reg_n - 1 - i)) & 1ULL) ? 1 : 0;
    return reg_n;
}
// 출처: nodes.h `all_digits()`
static bool all_digits(const std::string& x) {
    if (x.empty() || x.size() > 5) return false;
    for (size_t i = 0; i < x.size(); i++) if (x[i] < '0' || x[i] > '9') return false;
    return true;
}

// ── 시험 골격 ────────────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;
static void ok(bool cond, const char* what) {
    if (cond) { g_pass++; printf("  ✅ %s\n", what); }
    else      { g_fail++; printf("  ❌ **FAIL** %s\n", what); }
}
// 체크섬을 붙여 완성된 줄을 만든다(장치가 보낼 모양)
static std::string wire(const std::string& prefix) { return prefix + cksum(prefix); }

int main() {
    printf("═══ 상행 코덱 왕복 — 장치 바이트열 → 서버 파서 ═══\n");
    printf("⚠ 이 표본은 **소스에서 조립한 것**이다. 실물 로그가 오면 그것으로 다시 돌려라.\n\n");
    std::vector<std::string> f;

    // ───────────────────────────────────────────────────────────── S 프레임
    printf("[S] 상태 프레임 — P1(모듈 10 · 폭 3) · P2(모듈 2 · 폭 1)\n");
    {
        // P1: 자리 A1 점유(모듈 idx 0) → 비트 (n-1-i) = 9 → 0x200 → 폭3 = "200"
        std::string s1 = wire("S,305,200,000,1234,P1,");
        ok(verify_line(s1, f), "P1 S 가 체크섬을 통과한다");
        ok(f.size() >= 6, "필드가 6개 이상이다(서버 조건)");
        ok(f[0] == "S" && f[5] == "P1", "f[0]=S · f[5]=devid");
        int bits[32];
        ok(decode_hex_bits(10, f[2], bits) == 10, "occ 폭 3 이 n=10 과 맞는다");
        ok(bits[0] == 1, "🔴 idx 0(A1) 비트가 선다 — **비트 순서가 서버와 같다**");
        ok(bits[1] == 0 && bits[9] == 0, "나머지 비트는 0");

        // P2: 모듈 2개 → 폭 1. idx 0(A4) 점유 → 비트 1 → 0x2 → "2"
        std::string s2 = wire("S,88,2,0,77,P2,");
        ok(verify_line(s2, f), "P2 S 가 체크섬을 통과한다");
        int b2[32];
        ok(decode_hex_bits(2, f[2], b2) == 2, "occ 폭 1 이 n=2 와 맞는다");
        ok(b2[0] == 1, "🔴 P2 의 idx 0(A4) 비트가 선다");
        // ★ P1 의 idx 0 과 P2 의 idx 0 이 **서로 다른 자리**로 가는 것이 이번 개조의 본체다.
        //   여기서는 비트 해독까지만 본다(자리 환산은 결속표가 하고 서버 안에 있다).

        // tmask 가 붙은 경우(선택 필드)
        std::string s3 = wire("S,306,200,000,1235,P1,004,");
        ok(verify_line(s3, f) && f.size() >= 7, "tmask 가 붙으면 필드가 7개다");
        ok(f[6] == "004", "f[6] 이 tmask 다");
    }

    // ───────────────────────────────────────────────── 🔴 S 실패 사례
    printf("\n[S·실패] **관대하면 결함을 숨긴다** — 여기가 이 하니스의 값이다\n");
    {
        std::string good = wire("S,305,200,000,1234,P1,");
        std::string bad = good; bad[bad.size()-1] ^= 1;          // 체크섬 한 글자 틀림
        ok(!verify_line(bad, f), "🔴 체크섬이 틀리면 **거부**한다");

        // 🔴🔴 **입력이 그 축을 건드리는지 먼저 확인한다** (arduino 가 자기 하니스에서 찾은 함정)
        //   그쪽 첫 판은 체크섬이 `72` 라 **A~F 가 없어 소문자로 바꿔도 안 바뀌는데 PASS 를 찍었다.**
        //   ★ **관대해서가 아니라 입력이 그 축을 안 건드린 것이다. "통과했다"가 "쟀다"가 아니다.**
        //   ⚠ 내 첫 판도 같았다 — `S,305,…` 의 체크섬은 `02` 라 바꿀 글자가 없었고,
        //     게다가 `!lower_differs ||` 로 **빠져나갈 구멍**까지 만들어 뒀다(무조건 통과).
        {
            const std::string pre = "S,42,2A8,100,3600,P1,";   // ← 이 체크섬은 `4B`. **B 가 있다**
            const std::string up  = cksum(pre);
            std::string lo = up;
            for (size_t i = 0; i < lo.size(); i++)
                if (lo[i] >= 'A' && lo[i] <= 'F') lo[i] = (char)(lo[i] - 'A' + 'a');
            // 🔑 **먼저 축이 살아 있는지 단언한다.** 이것이 실패하면 아래 시험은 아무것도 안 잰다
            ok(lo != up, "🔑 입력의 체크섬에 A~F 가 있다 — **소문자 축을 실제로 잰다**");
            ok(!verify_line(pre + lo, f), "🔴 체크섬 소문자는 **거부**된다(대문자 고정 계약)");
        }

        ok(!verify_line("S,305,200,000,1234,P1", f), "쉼표 없는 줄은 거부");
        std::string few = wire("S,1,2,3,");                       // 필드 4개
        ok(verify_line(few, f) && f.size() < 6, "🔴 필드가 모자라면 **서버 조건(>=6)에 안 걸린다**");

        int bits[32];
        std::string w = wire("S,1,20,0,5,P1,");                   // occ 폭 2 인데 n=10 이면 3이어야
        verify_line(w, f);
        ok(decode_hex_bits(10, f[2], bits) == -1, "🔴 occ 폭 불일치를 잡는다(폭 2 ≠ ceil(10/4)=3)");
        ok(decode_hex_bits(0, "200", bits) == 0,
           "🔴 **등록 전에는 안 푼다** — 폭을 몰라 짐작하면 모든 비트가 어긋난다");
    }

    // ───────────────────────────────────────────────────────────── D 등록
    printf("\n[D] 등록 — 머리(4필드) + 모듈 줄(3필드)\n");
    {
        std::string h = wire("D,*,3,10,");
        ok(verify_line(h, f), "D 머리가 체크섬을 통과한다");
        ok(f.size() >= 4 && f[1] == "*", "🔑 머리는 f[1]==\"*\" 로 갈린다");
        ok(all_digits(f[2]) && all_digits(f[3]), "drain·n 이 10진이다(all_digits 통과)");
        ok(atoi(f[3].c_str()) == 10, "n=10 (P1)");

        std::string m = wire("D,A1,IP,");
        ok(verify_line(m, f) && f.size() >= 3, "모듈 줄은 3필드");
        ok(f[1] == "A1" && f[2] == "IP", "이름·kind 가 자리에 온다");
        ok(f[2][0] == 'I', "🔑 kind 첫 글자 I = 센서(kind_commandable 이 false)");

        std::string a = wire("D,L1,OG,");
        verify_line(a, f);
        ok(f[2][0] == 'O', "🔑 kind 첫 글자 O = 조작(명령 가능)");

        // 🔴 P1 등록 전체 길이 — arduino 가 121B 라고 했다
        std::string reg = wire("D,*,3,10,") + "\n";
        const char* mods[10][2] = {{"A1","IP"},{"A2","IP"},{"A3","IP"},{"U1","IP"},{"U2","IP"},
                                   {"ED","OG"},{"XD","OG"},{"L1","OG"},{"L2","OG"},{"L3","OG"}};
        for (int i = 0; i < 10; i++)
            reg += wire(std::string("D,") + mods[i][0] + "," + mods[i][1] + ",") + "\n";
        printf("     P1 등록 전체 = **%zuB** (arduino 실측 121B 와 대조)\n", reg.size());
        ok(reg.size() > 100, "등록 묶음이 100B 를 넘는다 — 링 64B 라면 넘친다");

        std::string badname = wire("D,*,x,10,");
        verify_line(badname, f);
        ok(!all_digits(f[2]), "🔴 drain 이 숫자가 아니면 **거부**된다(D,* 형식 위반)");
    }

    // ───────────────────────────────────────────────────────────── A ACK
    printf("\n[A] ACK — rid 에코 · slot 이 2글자\n");
    {
        std::string a = wire("A,709,G3,0,");
        ok(verify_line(a, f) && f.size() >= 4, "A 는 4필드");
        ok(f[1] == "709", "🔑 rid 를 그대로 에코한다");
        ok(f[2] == "G3", "⚠ slot 이 **자리 이름이 아닐 수 있다** — 서버는 p.slot 으로 되돌린다");
        ok(atoi(f[3].c_str()) == 0, "result 0 = 수행");

        std::string r3 = wire("A,710,G3,3,");
        verify_line(r3, f);
        ok(atoi(f[3].c_str()) == 3, "🔑 result 3 = **수행 불가**(등록 없는 모듈 · 인자 파싱 실패)");
    }

    // ────────────────────────────────── 🔴 멀티에서 새로 생긴 실패
    printf("\n[멀티·새 실패] **rid 는 맞는데 보드가 다르다** — 오늘 처음 생긴 갈래\n");
    {
        // 서버는 (rid, devid) 로 짝짓는다. P1 으로 보낸 명령을 P2 가 같은 rid 로 답하면 무시해야 한다.
        std::string a = wire("A,709,G3,0,");
        ok(verify_line(a, f), "ACK 자체는 형식이 맞다");
        // ★ 여기서 프레임만 보면 **어느 보드가 답했는지 알 수 없다** —
        //   `A` 는 devid 를 안 싣는다. **소켓(노드)이 유일한 장치 축**이다.
        ok(f.size() == 4, "🔴 A 에는 **devid 칸이 없다** — 그래서 로그의 둘째 토큰이 유일한 장치 축이다");
        printf("     ⚠ 이 갈래는 **소켓 문맥이 있어야** 시험된다 — 이 하니스로는 못 본다(11시 판별자로)\n");
    }

    printf("\n═══ 결과: %d pass · %d fail ═══\n", g_pass, g_fail);
    if (g_fail) printf("🔴 **FAIL 이 있다.** 서버 파서와 장치 형식이 갈렸다는 뜻이다.\n");
    return g_fail ? 1 : 0;
}
