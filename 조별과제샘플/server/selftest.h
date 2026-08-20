// selftest.h — 🔴 **자가검증 전부** (REQ-0272 · 2026-08-19 이동)
//
// `server.cpp` 에서 **통째로 옮겼다. 한 글자도 안 고쳤다.**
//   원래 자리에서 `#include` 하므로 전처리 결과가 같고, **`.o` 가 0 차이여야 한다.**
//   ⚠ `.o` 가 달라지면 그건 "이동이 아니었다"는 뜻이고 그 자리에서 멈춘다.
//
// 🔑 **이 덩어리를 첫째로 고른 이유 셋**:
//   ① 단일 최대(1,435줄 · `server.cpp` 의 23%) — 한 번에 가장 많이 줄어든다
//   ② **실기 경로가 아니다** — 방법이 틀려도 도는 서버가 안 다친다
//   ③ 그래서 **방법이 서버에서도 서는지 여기서 증명한다**
//
// ⚠ 파일 스코프다(`struct Server` 밖). 멤버 함수를 옮길 때는 **구조체 *안*에서** include 해야
//   전처리 결과가 같다 — 밖으로 옮기는 순간 이동이 아니라 재배치다.
#ifndef SELFTEST_H
#define SELFTEST_H

// ---------------------------------------------------------------- 자가검증
// §7.4 자가검증용 — `S` 한 줄을 만든다. on_ard_line 이 받는 형태(종단자 없음)다.
// 시험용 — 임의 프레임에 올바른 체크섬을 붙인다. **`cksum` 을 시험이 다시 구현하지 않는다** —
// 다시 구현하면 둘이 갈리고, 갈린 채로 통과하면 시험이 실기를 안 재는 것이 된다.
static std::string t_line(const std::string& prefix) { return prefix + cksum(prefix); }

static std::string s_line(long seq, const char* occ, const char* res, long long up) {
    char b[96];
    snprintf(b, sizeof(b), "S,%ld,%s,%s,%lld,P1,", seq, occ, res, up);
    std::string p(b);
    return p + cksum(p);
}

// 🔴 점유 변화 콜백 탐침 (㊶) — **파일 스코프여야 한다.** 시험 조각은 함수 몸통이라 여기 둔다.
//   ⚠ 콜백은 함수 포인터라 람다·멤버를 못 쓴다. 그래서 전역 셋으로 받는다.
static int         g_st_occ_n = 0;
static std::string g_st_occ_spot;
static bool        g_st_occ_val = false;
static std::string g_st_occ_mod;
static bool g_st_occ_has = false;
static long g_st_occ_meas = -1;
static void st_occ_probe(ParkingServer&, const std::string& spot,
                         const std::string& module, bool occupied,
                         const SensorMeasure& m) {
    g_st_occ_n++; g_st_occ_spot = spot; g_st_occ_mod = module; g_st_occ_val = occupied;
    g_st_occ_has = m.has; g_st_occ_meas = m.has ? m.value : -1;
}

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
    const int NL = (int)(sizeof(L) / sizeof(L[0]));   // 줄을 추가하고 개수를 못 고치는 사고를 막는다
    for (int i = 0; i < NL; i++) {
        std::vector<std::string> f;
        bool ok = verify_line(L[i], f);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << L[i] << "\n";
        if (!ok) bad++;
    }

    // ---------------- §7.4 (개정 8) 재부팅 판정 — 오탐을 없애면서 정탐을 잃지 않았는가
    // (A) 판정식 표 — 명세 §7.4 의 표를 그대로 옮겼다.
    std::cout << "\n[§7.4-A] 판정식 (fwd > " << UPTIME_MAX_FWD << " 이면 재부팅)\n";
    struct RbCase { long long prev, up; bool want; const char* name; };
    RbCase RB[] = {
        { 3600,    3601, false, "평시 1Hz" },
        { 3600,    4200, false, "링크 침묵 10분" },
        { 4294966,    1, false, "millis() 49.7일 랩" },
        { 4294000, 5000, false, "랩을 걸쳐 100분 침묵" },
        { -1,         5, false, "기준선 없음(첫 프레임)" },
        { 3600,       2, true,  "진짜 재부팅" },
        { 3,          1, true,  "부트 루프" },
        { 1000000,    2, true,  "11.6일 가동 뒤 재부팅" },
    };
    for (int i = 0; i < (int)(sizeof(RB) / sizeof(RB[0])); i++) {
        bool got = uptime_says_reboot(RB[i].prev, RB[i].up);
        bool ok  = (got == RB[i].want);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << RB[i].prev << " → " << RB[i].up
                  << " : " << (got ? "재부팅" : "정상")
                  << " (기대 " << (RB[i].want ? "재부팅" : "정상") << ")  " << RB[i].name << "\n";
        if (!ok) bad++;
    }

    // (B) **실제 호출 지점**까지 통과시킨다. 판정식이 맞아도 호출부가 틀리면 의미가 없으므로
    //     S 프레임을 만들어 on_ard_line 에 먹이고 resync_reservations 호출 횟수를 센다.
    std::cout << "\n[§7.4-B] on_ard_line → resync_reservations 실제 호출 횟수\n";
    const char* OCC = "0110100011";
    const char* RES = "0000100000";
    {   // (1) REQ-0062 의 재현 조건 — seq 를 65530 에서 시작시켜 랩을 넘긴다.
        //     옛 규칙(seq < ard_seq)이었다면 65535→0 프레임에서 반드시 터졌다.
        Server s; s.no_disk = true;
        long long up = 3600;
        long seqv = 65530;
        for (int k = 0; k < 8; k++) {
            s.on_ard_line(s_line(seqv, OCC, RES, up));
            seqv = (seqv + 1) & 0xFFFF;                  // uint16 순환
            up++;                                        // uptime 은 정상 전진
        }
        bool ok = (s.resync_count == 0);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "seq 65530→65535→0→1, uptime 정상 : resync="
                  << s.resync_count << " (기대 0)\n";
        if (!ok) bad++;
    }
    {   // (2) millis() 49.7일 랩도 재동기화를 부르면 안 된다
        Server s; s.no_disk = true;
        s.on_ard_line(s_line(100, OCC, RES, 4294966));   // 기준선
        s.on_ard_line(s_line(101, OCC, RES, 1));         // 랩
        bool ok = (s.resync_count == 0);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "uptime 4294966→1 : resync="
                  << s.resync_count << " (기대 0)\n";
        if (!ok) bad++;
    }
    {   // (3) 정탐 — 진짜 재부팅은 여전히 잡혀야 한다
        Server s; s.no_disk = true;
        s.on_ard_line(s_line(500, OCC, RES, 3600));      // 기준선
        s.on_ard_line(s_line(501, OCC, RES, 3601));      // 평시
        s.on_ard_line(s_line(9,   OCC, RES, 2));         // 재부팅(seq 도 작아졌다)
        bool ok = (s.resync_count == 1);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "uptime 3601→2 : resync="
                  << s.resync_count << " (기대 1)\n";
        if (!ok) bad++;
    }
    {   // (4) 정탐 — seq 가 **앞으로** 가는 재부팅. 옛 규칙은 이걸 놓쳤다.
        Server s; s.no_disk = true;
        s.on_ard_line(s_line(60000, OCC, RES, 5000));    // 기준선
        s.on_ard_line(s_line(60001, OCC, RES, 3));       // 재부팅인데 seq 는 전진
        bool ok = (s.resync_count == 1);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "uptime 5000→3 (seq 전진) : resync="
                  << s.resync_count << " (기대 1)\n";
        if (!ok) bad++;
    }

#ifndef _WIN32
    // ══ (5) 하행 슬롯 큐 — **코드 판독이 아니라 바이트를 받아 본다** ═══════════════════
    //
    // 🔑 `socketpair` 를 `ard` 자리에 꽂는다. `send_raw` 가 진짜 `send()` 를 하고 내가 반대편에서
    // 읽으므로 **"한 거래로 나갔는가"를 전선 바이트로 확인**할 수 있다.
    // ⚠ 이것은 **실물 왕복이 아니다** — 장치도 ESP 도 없다. 검증하는 것은 **내 큐 규율**이고,
    // 실기 도달은 시험 인스턴스(루트 순서)에서 따로 봐야 한다. 두 개를 같은 확신도로 말하지 않는다.
    {
        std::cout << "\n[슬롯 큐] socketpair 로 전선 바이트 대조\n";
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            std::cout << "  ! socketpair 실패 — 이 묶음을 건너뛴다\n";
        } else {
            Server s; s.no_disk = true;
            s.ard = sv[0]; s.park_dev = "P1";
            s.ard_seen = true; s.ard_last_ms = now_ms();
            char b[1024];

        // ─── 시험 목차 ─────────────────────────────────────────────────
        //  🔑 다섯 조각 다 **`selftest()` 의 몸통 조각**이다. `s`·`sv`·`bad` 를 물려받는다.
        //  🔴 **순서를 바꾸지 마라** — 뒤엣 시험이 앞엣 시험이 만든 상태를 쓰는 자리가 있다.
        //    (`s` 는 하나이고 조각들이 그것을 차례로 만진다. 독립이 아니다.)
        //  ⚠ 조각을 새로 더하면 **여기에 한 줄 적어라.** 안 적으면 목차가 낡는다.
        // ──────────────────────────────────────────────────────────────
#include "selftest_downq.h"      // ①~⑬  하행 슬롯 큐 — 창·FIFO·상한·재전송·우선순위
#include "selftest_nodes.h"      // ⑬~㉑  자리 인수 · 노드 잠금 · 등록 `D` · `Q` 상한
#include "selftest_zones.h"      // ㉒~㉖  지형과 판 · map/state 봉투 · 조작 라우팅
#include "selftest_wire.h"       // ㉘~㉛  자리 비트열 · 자가 치유 · `G` 전선 왕복
#include "selftest_contract.h"   // ㉟~㉜  봉투 키 계약 · known 사유 · rid 폭과 격리
#include "selftest_ledger.h"     // ㊱~㊷  노드 대장 — 지문·상태 전이·파일 왕복
#include "selftest_spot.h"       // ㊺~㊼  자리 동작 방식 — 기본 OR 계약·재정의

            s.ard = BAD_SOCK;              // 소멸자가 이 fd 를 건드리지 않게
            closesock(sv[0]); closesock(sv[1]);
        }
    }
#endif

    std::cout << (bad ? "자가검증 실패\n" : "자가검증 통과\n");
    return bad ? 1 : 0;
}

#endif  // SELFTEST_H
