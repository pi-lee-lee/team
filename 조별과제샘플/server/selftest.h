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

            // ① **이벤트가 전송을 만들지 않는다** — dispatch 3건 뒤에도 전선은 조용해야 한다
            s.dispatch('R', BAD_SOCK, "", "A1", "00000000");
            s.dispatch('R', BAD_SOCK, "", "A2", "00000000");
            s.dispatch('C', BAD_SOCK, "", "B3", "");
            int r0 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            bool ok1 = (s.downq.size() == 3 && s.batch_count == 0 && r0 < 0);
            std::cout << (ok1 ? "  ✓ " : "  ✗ ") << "dispatch 3건 → 큐 " << s.downq.size()
                      << "줄 · 전선 " << (r0 < 0 ? 0 : r0) << "B (기대: 큐 3 · 전선 0)\n";
            if (!ok1) bad++;

            // ② 창이 열리면 **한 거래**로 나간다 — recv 한 번에 세 줄이 다 들어온다
            s.flush_downq("selftest 창", false);
            int r1 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            std::string got(b, r1 > 0 ? r1 : 0);
            int lf = 0;
            for (size_t i = 0; i < got.size(); i++) if (got[i] == '\n') lf++;
            bool ok2 = (lf == 3 && s.batch_count == 1 && s.batch_lines == 3 && s.downq.empty());
            std::cout << (ok2 ? "  ✓ " : "  ✗ ") << "flush → 한 거래에 " << lf << "줄 · "
                      << (r1 > 0 ? r1 : 0) << "B · 배치 " << s.batch_count
                      << " (기대: 3줄 · 배치 1)\n";
            if (!ok2) bad++;

            // ③ FIFO — 예약/취소가 뒤집히면 안 된다
            size_t pA1 = got.find("R,1,A1"), pA2 = got.find("R,2,A2"), pB3 = got.find("C,3,B3");
            bool ok3 = (pA1 != std::string::npos && pA2 != std::string::npos &&
                        pB3 != std::string::npos && pA1 < pA2 && pA2 < pB3);
            std::cout << (ok3 ? "  ✓ " : "  ✗ ") << "FIFO 순서 R,A1 → R,A2 → C,B3\n";
            if (!ok3) bad++;

            // ④ 바이트 상한 — 넘치면 **버리지 않고 미룬다**
            int save_cap = DOWN_BATCH_CAP_B;
            DOWN_BATCH_CAP_B = 40;                    // 한 줄(약 20B)이 두 개면 넘는 값
            s.ard_last_ms = now_ms();
            s.dispatch('R', BAD_SOCK, "", "A3", "00000000");
            s.dispatch('R', BAD_SOCK, "", "A4", "00000000");
            s.dispatch('R', BAD_SOCK, "", "A5", "00000000");
            size_t before = s.downq.size();
            s.flush_downq("selftest cap", false);
            int r2 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            bool ok4 = (r2 > 0 && r2 <= 44 && !s.downq.empty() && s.downq.size() < before
                        && s.q_deferred == 1);
            std::cout << (ok4 ? "  ✓ " : "  ✗ ") << "cap 40B → 전선 " << (r2 > 0 ? r2 : 0)
                      << "B · 남은 큐 " << s.downq.size() << "줄 · 미룬창 " << s.q_deferred
                      << " (기대: 남은 큐 >0 · 미룬창 1 · 버림 0)\n";
            if (!ok4) bad++;
            DOWN_BATCH_CAP_B = save_cap;

            // ⑤ 창을 지나면 **다음 S 를 기다린다**(장치 송신 구간을 침범하지 않는다)
            s.ard_last_ms = now_ms() - (DOWN_WIN_MS + 10);
            long long skips0 = s.win_skips;
            s.flush_downq("selftest 창밖", false);
            int r3 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            bool ok5 = (s.win_skips == skips0 + 1 && r3 < 0);
            std::cout << (ok5 ? "  ✓ " : "  ✗ ") << "창 밖 flush → 창놓침 " << s.win_skips
                      << " · 전선 " << (r3 < 0 ? 0 : r3) << "B (기대: +1 · 0B)\n";
            if (!ok5) bad++;

            // ⑥ 🔴 **재전송이 `tries` 를 리셋하지 않는다** — 이 검사가 없으면 무한 재전송을
            //    그냥 내보냈을 것이다(구현 중에 실제로 그 코드를 썼고 여기서 막았다).
            s.ard_last_ms = now_ms();
            s.flush_downq("selftest 잔여", true);
            while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}   // 전선 비우기
            uint16_t rid = 0;
            for (std::map<uint16_t, Pending>::iterator it = s.pend.begin();
                 it != s.pend.end(); ++it)
                if (!it->second.queued) { rid = it->first; break; }
            bool ok6 = false;
            if (rid) {
                int t0 = s.pend[rid].tries;
                s.pend[rid].sent_ms = now_ms() - (ACK_TIMEOUT_MS + 50);
                s.tick();                                    // 재전송 → 큐로
                bool requeued = s.pend.count(rid) && s.pend[rid].queued;
                int t1 = s.pend.count(rid) ? s.pend[rid].tries : -1;
                s.ard_last_ms = now_ms();
                s.flush_downq("selftest 재전송", false);
                int t2 = s.pend.count(rid) ? s.pend[rid].tries : -1;
                ok6 = (requeued && t1 == t0 + 1 && t2 == t1);
                std::cout << (ok6 ? "  ✓ " : "  ✗ ") << "재전송: tries " << t0 << "→" << t1
                          << "→" << t2 << " · 큐 경유 " << (requeued ? "예" : "아니오")
                          << " (기대: 1→2→2 · 큐 경유)\n";
            } else {
                std::cout << "  ✗ 재전송 검사용 pend 를 못 찾았다\n";
            }
            if (!ok6) bad++;

            // ⑦ 세션이 끝나면 큐를 비우고 **답을 남긴다**(조용히 사라지지 않는다)
            s.ard_last_ms = now_ms();
            s.dispatch('R', BAD_SOCK, "", "B1", "00000000");
            size_t q0 = s.downq.size();
            long long dropped0 = s.q_dropped_link;
            s.clear_downq("selftest 세션종료");
            bool ok7 = (s.downq.empty() && s.downq_bytes == 0 &&
                        s.q_dropped_link == dropped0 + (long long)q0);
            std::cout << (ok7 ? "  ✓ " : "  ✗ ") << "세션 종료 → 큐 비움 · 링크버림 "
                      << s.q_dropped_link << " (기대: 큐 0 · 버린 수가 세어짐)\n";
            if (!ok7) bad++;

            // ⑧ 🔴 **창 포기가 슬롯당 1회로 묶인다** — `tick()` 은 200ms 마다 도는데
            //    조건이 "조용하다" 하나면 침묵 구간 내내 200ms 버스트가 된다.
            //    **장치가 가장 힘들어하는 구간에서 슬롯 규율이 통째로 꺼지는 것**이라
            //    이 검사가 없으면 다음 사람이 조건을 단순화하면서 되돌린다.
            s.ard = sv[0];
            s.ard_last_ms = now_ms() - (DOWN_DMAX_MS + 100);   // 오래 조용했다
            s.last_dmax_ms = 0;
            s.dispatch('R', BAD_SOCK, "", "B2", "00000000");
            s.dispatch('R', BAD_SOCK, "", "B4", "00000000");
            s.dispatch('R', BAD_SOCK, "", "B6", "00000000");
            long long dm0 = s.dmax_flushes;
            size_t q_before = s.downq.size();
            s.tick();                                          // 첫 포기 — 나가야 한다
            s.tick();                                          // 곧바로 또 — **나가면 안 된다**
            s.tick();
            // 🔴 **여기가 옛 규칙과 갈리는 자리다.** 슬롯이 지나도 장치가 조용하면
            //    **다시 쏘면 안 된다**(옛 규칙은 슬롯당 1회라 여기서 또 쐈다).
            s.last_dmax_ms = now_ms() - (DOWN_SLOT_MS + 100);
            s.tick();
            bool ok8 = (s.dmax_flushes == dm0 + 1);
            std::cout << (ok8 ? "  ✓ " : "  ✗ ") << "장치 침묵 중 4틱(슬롯 경과 포함) → 창포기 "
                      << (s.dmax_flushes - dm0) << "회 (기대 1 — **침묵당 1회**)\n";
            if (!ok8) bad++;

            // 🔴 탐침 크기 — 증거 없이 4건을 쏘지 않는다
            bool ok8b = (q_before >= 3 && s.downq.size() == q_before - DOWN_PROBE_N);
            std::cout << (ok8b ? "  ✓ " : "  ✗ ") << "탐침 크기: 큐 " << q_before
                      << " → " << s.downq.size() << " (기대 " << DOWN_PROBE_N << "건만 나감)\n";
            if (!ok8b) bad++;

            // 🔴 재무장 — 장치가 한 줄이라도 말하면 조건이 바뀐 것이다
            s.on_ard_line("X,noise");                          // 깨진 줄도 살아 있다는 증거다
            s.ard_last_ms = now_ms() - (DOWN_DMAX_MS + 100);   // 그 줄은 오래전이었다고 둔다
            s.last_dmax_ms = now_ms() - (DOWN_SLOT_MS + 100);
            s.tick();
            bool ok8c = (s.dmax_flushes == dm0 + 2);
            std::cout << (ok8c ? "  ✓ " : "  ✗ ") << "장치가 말한 뒤 → 창포기 "
                      << (s.dmax_flushes - dm0) << "회 (기대 2 — 재무장됨)\n";
            if (!ok8c) bad++;
            while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}

            // ⑨ 🔴 **링크가 끊긴 뒤 거짓 `ack_timeout` 이 안 나간다**
            //    `end_ard_session` 은 큐만 비우고 **전선에 나가 있던 pend 는 남는다.**
            //    그 건이 재전송으로 와서 거절되면 이미 `device_offline` 로 답이 갔는데,
            //    `tick()` 이 반환값을 무시하면 몇 틱 뒤 **`ack_timeout` 으로 한 번 더** 답한다.
            //    ⚠ 그 이름은 "전선에 나갔다. 3회 재전송까지 했다"를 뜻한다(§8.16) —
            //    서버가 스스로 거절한 건에 붙으면 **로그에 거짓 문장이 남는다.**
            {
                s.ard_last_ms = now_ms();
                s.dispatch('R', BAD_SOCK, "", "B5", "00000000");
                s.flush_downq("selftest ⑨ 전선에 올린다", false);
                uint16_t rid9 = 0;
                for (std::map<uint16_t, Pending>::iterator it = s.pend.begin();
                     it != s.pend.end(); ++it)
                    if (!it->second.queued) rid9 = it->first;
                long long fail0 = s.ack_fail_count;
                s.ard = BAD_SOCK;                       // 링크가 끊긴 상태
                if (rid9) s.pend[rid9].sent_ms = now_ms() - (ACK_TIMEOUT_MS + 50);
                s.tick(); s.tick(); s.tick(); s.tick();  // 3회 소진보다 많이 돌린다
                bool ok9 = (rid9 && s.pend.count(rid9) == 0
                            && s.ack_fail_count == fail0 && s.q_nodev > 0);
                std::cout << (ok9 ? "  ✓ " : "  ✗ ") << "링크 끊긴 뒤 재전송: pend "
                          << (s.pend.count(rid9) ? "남음" : "정리됨")
                          << " · ACK실패 " << (s.ack_fail_count - fail0)
                          << " · 장치없음 " << s.q_nodev
                          << " (기대: 정리됨 · ACK실패 0 · 장치없음 >0)\n";
                if (!ok9) bad++;
            }

            // ⑩ 🔴 **건수 상한 — 바이트가 남아도 `DOWN_BATCH_MAX_N` 에서 끊긴다**
            //    바이트만으로는 못 막는 자리다: 작은 명령 6건은 바이트 상한에 한참 못 미친다.
            {
                s.ard = sv[0];                       // ⑨ 가 끊어 놓았다 — 되살린다
                s.ard_last_ms = now_ms();
                s.downq.clear(); s.downq_bytes = 0;
                while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}
                for (int k = 0; k < 6; k++) s.dispatch_sim(BAD_SOCK, "");   // M = 9B 짜리 6건
                size_t before = s.downq.size();
                s.flush_downq("selftest 건수상한", false);
                bool ok10 = (before == 6 && (int)s.downq.size() == 6 - DOWN_BATCH_MAX_N);
                std::cout << (ok10 ? "  ✓ " : "  ✗ ") << "건수 상한: 담긴 " << before
                          << "건 중 " << (before - s.downq.size()) << "건 나감 · "
                          << s.downq.size() << "건 남음 (기대 6 담김 · "
                          << DOWN_BATCH_MAX_N << " 나감)\n";
                if (!ok10) bad++;
            }

            // ⑪ 🔴 **중요 계열이 먼저 나간다** — 치유 명령이 연타에 밀리면 치유가 죽는다
            {
                s.ard = sv[0];
                s.ard_last_ms = now_ms();
                while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}
                s.downq.clear(); s.downq_bytes = 0;
                for (int k = 0; k < 3; k++) s.dispatch_sim(sv[1], "user");  // 사용자(fd 있음)
                s.dispatch('C', BAD_SOCK, "", "B4", "");                    // 중요(내부 발생)
                s.flush_downq("selftest 우선순위", false);
                int r = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
                std::string got(b, r > 0 ? r : 0);
                bool ok11 = (got.compare(0, 2, "C,") == 0);   // 중요가 맨 앞이어야 한다
                std::cout << (ok11 ? "  ✓ " : "  ✗ ") << "중요 우선: 첫 줄이 "
                          << got.substr(0, 2) << " (기대 C,)\n";
                if (!ok11) bad++;
            }

            // ⑫ 🔴 **사용자는 거절되고 중요는 거절되지 않는다** — 이 비대칭이 이번 변경의 핵심이다.
            //    사용자 거절은 **화면에 뜬다**(다시 누르면 된다). 중요를 거절하면 **아무도 모르게**
            //    서버와 장치가 갈린 채 남는다. 대칭으로 "단순화"하면 그 침묵이 돌아온다.
            {
                s.ard = sv[0];
                s.ard_last_ms = now_ms();
                s.downq.clear(); s.downq_bytes = 0;
                while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}
                const int cap_n = DOWN_BATCH_MAX_N * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS);
                long long rej0 = s.q_full_n;
                for (int k = 0; k < cap_n + 3; k++) s.dispatch_sim(sv[1], "u");  // 사용자 연타
                size_t user_q = s.downq.size();
                long long rej = s.q_full_n - rej0;
                s.dispatch('C', BAD_SOCK, "", "B9", "");                         // 중요 — 넘쳐도 들어간다
                bool ok12 = ((int)user_q == cap_n && rej == 3 &&
                             (int)s.downq.size() == cap_n + 1 && s.downq.back().important);
                std::cout << (ok12 ? "  ✓ " : "  ✗ ") << "사용자 " << cap_n
                          << "건에서 막힘(거절 " << rej << ") · 중요는 통과 → 큐 "
                          << s.downq.size() << " (기대 사용자 " << cap_n
                          << " · 거절 3 · 큐 " << (cap_n + 1) << ")\n";
                if (!ok12) bad++;
                s.downq.clear(); s.downq_bytes = 0;
            }

            // ⑬ 🔴 **전제 감시가 실제로 울린다** — 감시는 "넣었다"가 아니라 "울린다"로 확인한다.
            //    울리지 않는 감시는 **검사되지 않는 조건이 검사되는 것처럼 보이게** 만든다(CLAUDE.md).
            {
                bool w0 = s.s_worst_warned;
                s.on_ard_line(std::string("S,1,") + std::string(DEV_S_WORST_ASSUMED_B, 'x'));
                bool ok13 = (!w0 && s.s_worst_warned &&
                             s.s_max_b > DEV_S_WORST_ASSUMED_B);
                std::cout << (ok13 ? "  ✓ " : "  ✗ ") << "전제 감시: S "
                          << s.s_max_b << "B > 가정 " << DEV_S_WORST_ASSUMED_B
                          << "B → 경고 " << (s.s_worst_warned ? "울림" : "안 울림")
                          << " (기대 울림)\n";
                if (!ok13) bad++;
                // ⚠ 가정 이하는 울리면 안 된다 — 늘 울리는 경고는 아무도 안 본다
                Server s2;
                s2.on_ard_line("S,1,short");
                bool ok13b = (!s2.s_worst_warned && s2.s_max_b == 9);
                std::cout << (ok13b ? "  ✓ " : "  ✗ ") << "가정 이하 S(9B) → 경고 "
                          << (s2.s_worst_warned ? "울림" : "안 울림") << " (기대 안 울림)\n";
                if (!ok13b) bad++;
            }

            // 🔑 **루프백 TCP 쌍** — `socketpair` 는 주소가 없어 IP 판별을 못 밟는다.
            //    거절 경로가 이 REQ 의 본체이므로 **진짜 주소가 붙은 소켓**으로 시험한다.
            struct LoopPair {
                sock_t a, b, lsn;
                LoopPair() : a(BAD_SOCK), b(BAD_SOCK), lsn(BAD_SOCK) {
                    lsn = socket(AF_INET, SOCK_STREAM, 0);
                    if (lsn == BAD_SOCK) return;
                    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
                    sa.sin_family = AF_INET; sa.sin_port = 0;
                    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    if (bind(lsn, (struct sockaddr*)&sa, sizeof(sa)) != 0) return;
                    socklen_t sl = sizeof(sa);
                    if (getsockname(lsn, (struct sockaddr*)&sa, &sl) != 0) return;
                    if (listen(lsn, 1) != 0) return;
                    a = socket(AF_INET, SOCK_STREAM, 0);
                    if (a == BAD_SOCK) return;
                    if (connect(a, (struct sockaddr*)&sa, sizeof(sa)) != 0) { a = BAD_SOCK; return; }
                    b = accept(lsn, NULL, NULL);
                }
                ~LoopPair() {
                    if (a != BAD_SOCK) closesock(a);
                    if (b != BAD_SOCK) closesock(b);
                    if (lsn != BAD_SOCK) closesock(lsn);
                }
            };

            // ⑬ 🔴 **자리 인수 판정** (REQ-0217 ①) — 세 갈래를 전부 밟는다.
            //    ⚠ `socketpair` 는 `getpeername` 이 이름 없는 주소를 주므로 `peer_str` 이 "?" 다.
            //      그래서 **같은 IP 경로**가 자연히 성립한다 — 그 경로부터 확인하고,
            //      다른 IP 는 `ard_peer` 를 손으로 바꿔 만든다(실제 판정 코드는 같은 것을 탄다).
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;

                // (가) 🔴 **주소를 못 얻는다 → 막지 않는다**(socketpair 가 그 상황을 그대로 만든다)
                t.ard_last_ms = now_ms();                    // 방금 프레임을 받았다
                long long r0 = t.dup_devid_reject;
                bool okA = t.promote_unknown(sv[1], "P1") && t.dup_devid_reject == r0;
                std::cout << (okA ? "  ✓ " : "  ✗ ") << "주소 판별 불가 · 공백 0ms → 교체 허용 "
                          << "(거절 쪽으로 넘어지면 우리 보드가 영영 막힌다)\n";
                if (!okA) bad++;
            }
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;
                t.ard_peer = "?:1";                          // 새 소켓과 같은 host("?")
                t.ard_last_ms = now_ms();
                bool okA2 = (t.promote_unknown(sv[1], "P1") && t.dup_devid_reject == 0);
                std::cout << (okA2 ? "  ✓ " : "  ✗ ") << "같은 IP · 공백 0ms → 교체 허용 "
                          << "(정상 재접속 85건 중 공백 0·1·2초가 실재한다)\n";
                if (!okA2) bad++;
                t.ard = BAD_SOCK;
            }
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;
                t.ard_peer = "10.0.0.99:1234";               // 🔴 다른 IP 로 만든다
                LoopPair lp;                                  // 새 소켓엔 진짜 주소가 붙는다

                // (나) 다른 IP + 기존이 최근에 말했다 → 거절
                t.ard_last_ms = now_ms();
                bool okB = (lp.b != BAD_SOCK && t.promote_unknown(lp.b, "P1") == false
                            && t.dup_devid_reject == 1 && t.takeover_grace == 0);
                lp.b = BAD_SOCK;                              // promote 가 닫았다
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "다른 IP · 기존이 말하는 중 → 거절 "
                          << "(거절 " << t.dup_devid_reject << " · 유예교체 "
                          << t.takeover_grace << " · 기대 1/0)\n";
                if (!okB) bad++;
            }
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;
                t.ard_peer = "10.0.0.99:1234";
                LoopPair lp;

                // (다) 다른 IP + 기존이 유예를 넘겨 조용하다 → 교체 허용(경고 남김)
                t.ard_last_ms = now_ms() - (TAKEOVER_GRACE_MS + 100);
                bool okC = (lp.b != BAD_SOCK && t.promote_unknown(lp.b, "P1") == true
                            && t.dup_devid_reject == 0 && t.takeover_grace == 1);
                lp.b = BAD_SOCK;
                std::cout << (okC ? "  ✓ " : "  ✗ ") << "다른 IP · 기존이 "
                          << TAKEOVER_GRACE_MS << "ms 조용 → 교체 허용 (거절 "
                          << t.dup_devid_reject << " · 유예교체 " << t.takeover_grace
                          << " · 기대 0/1)\n";
                if (!okC) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑭ 🔴 **주차 노드 잠금** (REQ-0217 ④) — 잠금 미지정 시 거동이 안 바뀌는 것까지 본다
            {
                Server t; t.ard = BAD_SOCK;
                g_park_dev_pin = "P1A";
                bool tookAux = t.promote_unknown(sv[1], "P1");   // 잠금과 다른 devid
                bool okD = (tookAux && t.park_dev != "P1");      // 주차 노드가 되면 안 된다
                std::cout << (okD ? "  ✓ " : "  ✗ ") << "잠금 P1A · device=P1 의 S → 주차 노드 '"
                          << t.park_dev << "' (기대: P1 이 아님 — 보조로 받는다)\n";
                if (!okD) bad++;
                t.ard = BAD_SOCK;
            }
            {
                Server t; t.ard = BAD_SOCK;
                g_park_dev_pin = "";                             // 잠금 없음 = 종전 동작
                bool okE = (t.promote_unknown(sv[1], "P1") && t.park_dev == "P1");
                std::cout << (okE ? "  ✓ " : "  ✗ ") << "잠금 없음 → first-S-wins 그대로 (주차 노드 '"
                          << t.park_dev << "' · 기대 P1)\n";
                if (!okE) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑮ 🔴 **등록 `D` — 정상 경로**(설계 §5). 슬롯1 `S` 승격 → 슬롯2 `D`
            {
                Server t; t.ard = sv[0]; t.park.devid = "P1"; t.ard_seen = true;
                t.on_ard_line(t_line("D,*,7,3,"));                  // drain=7 · n=3
                t.on_ard_line(t_line("D,A1,IP,"));
                t.on_ard_line(t_line("D,A2,OG,"));
                bool mid = (!t.park.reg_done && t.park.mods.size() == 2);
                t.on_ard_line(t_line("D,A3,OB,"));
                bool ok15 = (mid && t.park.reg_done && t.park.reg_n == 3
                             && t.park.reg_drain == 7 && t.reg_ok == 1 && t.reg_bad == 0
                             && t.reg_cmdable() == 2);   // OG·OB 만 명령 가능
                std::cout << (ok15 ? "  ✓ " : "  ✗ ") << "등록: n=3 다 받아야 완료 · drain "
                          << t.park.reg_drain << " · 명령가능 " << t.reg_cmdable()
                          << " (기대 완료 · 7 · 2)\n";
                if (!ok15) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑯ 🔴 **`*` 는 모듈 이름이 될 수 없다** — 안 막으면 배출률 선언으로 파싱된다
            {
                Server t; t.ard = sv[0]; t.ard_seen = true;
                t.on_ard_line(t_line("D,*,IP,"));            // name 이 `*` 인 모듈처럼 생긴 줄
                bool ok16 = (t.reg_bad == 1 && t.park.reg_n < 0 && t.park.reg_drain < 0);
                std::cout << (ok16 ? "  ✓ " : "  ✗ ") << "`*` 이름 거부: 형식오류 "
                          << t.reg_bad << " · drain " << t.park.reg_drain
                          << " (기대 1 · -1 — atoi 가 0 을 주지 않았다)\n";
                if (!ok16) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑰ 🔴 **삼중 검산 ③ — `S` 의 폭이 선언 `n` 과 갈리면 잡는다**
            //    ①②만으로는 못 잡는다. 같은 함수에서 나온 값이 갈렸다는 뜻이라 큰 신호다.
            {
                Server t; t.ard = sv[0]; t.ard_seen = true;
                t.on_ard_line(t_line("D,*,7,10,"));
                for (int i = 0; i < 10; i++) t.on_ard_line(t_line("D,X,IP,"));
                long long w0 = t.reg_widthbad;
                t.on_ard_line(t_line("S,1,18B,000,389,P1,"));        // 폭 3 = ceil(10/4) ✅
                bool okA = (t.reg_widthbad == w0);
                t.on_ard_line(t_line("S,2,18BC,000,389,P1,"));       // 🔴 폭 4 — 갈렸다
                bool ok17 = (okA && t.reg_widthbad == w0 + 1);
                std::cout << (ok17 ? "  ✓ " : "  ✗ ") << "폭 검산: 폭3 통과 · 폭4 잡힘 (불일치 "
                          << t.reg_widthbad << " · 기대 1)\n";
                if (!ok17) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑱ 🔴 **옛 펌웨어는 이 검사를 안 탄다** — 등록을 안 하므로 `reg_done` 이 false 다.
            //    이게 없으면 **지금 도는 장치가 매 프레임 폭불일치를 찍는다.**
            {
                Server t; t.ard = sv[0]; t.ard_seen = true;
                t.on_ard_line(t_line("S,1,0110000010,0000000000,389,P1,"));   // 옛 10진 폭 10
                bool ok18 = (t.reg_widthbad == 0 && t.reg_bad == 0);
                std::cout << (ok18 ? "  ✓ " : "  ✗ ") << "옛 펌웨어(등록 없음): 폭불일치 "
                          << t.reg_widthbad << " (기대 0 — 검사를 안 탄다)\n";
                if (!ok18) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑲ 🔴 **`Q` 상한과 포기** — 조건을 적었으면 그것을 보는 검사를 같은 자리에.
            //    창 A 에서 "정지 조건을 적어 놓고 그것을 보는 코드가 없어" 130건이 더 돌았다.
            {
                // 🔴 **자기 소켓쌍을 쓴다. 공용 `sv` 를 쓰면 안 된다** —
                //   앞 시험(⑬-가)이 `promote_unknown(sv[1], …)` 로 그 fd 를 `ard` 에 넣었고
                //   그 `Server` 의 **소멸자가 `sv[1]` 을 닫는다.** 그러면 여기 `send_raw` 가 실패하고
                //   `ard_send_failed()` 가 소켓을 닫아 **질의가 1회에서 멈춘다.**
                //   ⚠ 처음에 실제로 그렇게 나왔고, **시험이 재려던 것(상한)이 아니라 전송 실패를 쟀다.**
                LoopPair lq;
                Server t; t.ard = lq.a; t.ard_seen = true; t.park.devid = "P1";
                t.ard_last_ms = now_ms();
                t.park.reg_first_ms = now_ms() - (REG_TIMEOUT_MS + 100);   // 마감 지났다
                for (int i = 0; i < 6; i++) {          // 상한보다 많이 돌린다
                    t.park.last_q_ms = 0;              // 슬롯 문턱은 통과시킨다
                    t.tick();
                }
                bool ok19 = (t.reg_qsent == REG_Q_MAX && t.park.reg_giveup
                             && t.reg_giveups == 1);
                std::cout << (ok19 ? "  ✓ " : "  ✗ ") << "Q 상한: 6틱에 질의 " << t.reg_qsent
                          << "회 · 포기 " << t.reg_giveups << " (기대 " << REG_Q_MAX << " · 1)\n";
                if (!ok19) bad++;
                t.ard = BAD_SOCK;      // 소멸자가 lq.a 를 닫지 않게 — LoopPair 가 닫는다
            }

            // ⑳ 🔴 **등록이 끝났으면 `Q` 를 안 보낸다** — 이게 없으면 매 슬롯 질의가 나가고
            //    **그 노드의 하행이 통째로 굶는다**(질의가 창당 1거래를 독점한다).
            {
                LoopPair lr;
                Server t; t.ard = lr.a; t.ard_seen = true;
                t.ard_last_ms = now_ms();
                t.on_ard_line(t_line("D,*,7,1,"));
                t.on_ard_line(t_line("D,A1,IP,"));
                t.park.reg_first_ms = now_ms() - (REG_TIMEOUT_MS + 100);
                for (int i = 0; i < 4; i++) { t.park.last_q_ms = 0; t.tick(); }
                bool ok20 = (t.park.reg_done && t.reg_qsent == 0 && t.reg_giveups == 0);
                std::cout << (ok20 ? "  ✓ " : "  ✗ ") << "등록 완료 뒤 질의 " << t.reg_qsent
                          << "회 (기대 0)\n";
                if (!ok20) bad++;
                t.ard = BAD_SOCK;
            }

            // ㉑ 🔴 **`all_nodes()` 가 주차 + 보조를 다 훑는다** (REQ-0203 3a)
            //    ⚠ 색인을 저장하지 않는 선택의 값을 여기서 확인한다 — **`aux` 를 바꾼 뒤
            //      아무것도 갱신하지 않고 바로 물어봐도 맞아야 한다.**
            {
                Server t;
                bool e0 = t.all_nodes().empty();                 // 아무것도 없을 때
                t.park.devid = "P1"; t.park.fd = sv[0];
                bool one = (t.all_nodes().size() == 1);
                t.aux["P2"].fd = sv[1];                          // 갱신 호출 없이 바로
                t.aux["P3"].fd = sv[1];
                std::vector<Node*> v = t.all_nodes();
                bool three = (v.size() == 3 && v[0] == &t.park);  // 주차가 먼저다
                bool ok21 = (e0 && one && three);
                std::cout << (ok21 ? "  ✓ " : "  ✗ ") << "all_nodes(): 0 → 1 → "
                          << v.size() << " (기대 3 · 주차가 앞 · **갱신 호출 없이**)\n";
                if (!ok21) bad++;
                t.park.fd = BAD_SOCK; t.aux.clear();
            }

            // ㉒ 🔴 **지형 구성과 판(`epoch`)** (REQ-0203 4a)
            {
                Server t;
                t.build_default_zones();
                // 🔴 **7영역으로 바뀌었다**(사용자 확정 (A) · 명세 §9). 옛 기대는 12 였다 —
                //   주차 자리가 10 이 아니라 **5**(각 자리에 센서 둘)이고 + 입구 + 출구 = 7.
                //   ⚠ 이 시험이 옛 계약을 들고 있어서 지형을 바꾸자 곧바로 빨간불이 됐다. **그게 제 일이다.**
                Zone* a1 = t.zone_find("A1"); Zone* e1 = t.zone_find("E1");
                Zone* b1 = t.zone_find("B1");        // 🔴 이제 자리가 아니라 **센서 이름**이다
                bool ok22 = (t.lot.zones().size() == 7 && a1 && e1
                             && a1->kind == "parking" && e1->kind == "entrance"
                             && a1->cells.size() == 1 && t.lot.epoch() == 1
                             && b1 == 0);            // **B1 은 자리로 존재하지 않아야 한다**
                std::cout << (ok22 ? "  ✓ " : "  ✗ ") << "지형: 자리 " << t.lot.zones().size()
                          << " · A1 칸수 " << (a1 ? a1->cells.size() : 0)
                          << " · 판 " << t.lot.epoch()
                          << " · B1 자리없음 " << (b1 == 0 ? "예" : "🔴아니오")
                          << " (기대 7 · 1 · 1 · 예)\n";
                if (!ok22) bad++;

                // 🔴 **등록이 지형을 바꾸면 판이 오른다**
                t.park.devid = "P1";
                t.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                t.park.mods.push_back(std::make_pair(std::string("A2"), std::string("OG")));
                t.bind_modules(t.park);
                bool ok23 = (t.lot.epoch() == 2 && a1->modules.size() == 1
                             && a1->modules[0].first == "P1" && a1->modules[0].second == "A1");
                std::cout << (ok23 ? "  ✓ " : "  ✗ ") << "결속: A1 모듈 "
                          << a1->modules.size() << "개 (devid=" << (a1->modules.empty() ? "" : a1->modules[0].first)
                          << ") · 판 " << t.lot.epoch() << " (기대 1 · P1 · 2)\n";
                if (!ok23) bad++;

                // 🔴 **같은 등록을 다시 받아도 판이 안 오른다** — 안 그러면 재접속마다 화면이 맵을 다시 받는다
                t.bind_modules(t.park);
                bool ok24 = (t.lot.epoch() == 2 && a1->modules.size() == 1);
                std::cout << (ok24 ? "  ✓ " : "  ✗ ") << "같은 결속 반복 → 판 " << t.lot.epoch()
                          << " · 모듈 " << a1->modules.size() << " (기대 2 · 1 — 안 오른다)\n";
                if (!ok24) bad++;

                // 🔴 **상태만 바뀌는 것은 판을 안 올린다** — 올리면 봉투를 가른 이유가 사라진다
                long long e0 = t.lot.epoch();
                t.slots[0].occupied = 1; t.slots[0].reserved = 1;
                bool ok25 = (t.lot.epoch() == e0);
                std::cout << (ok25 ? "  ✓ " : "  ✗ ") << "점유·예약 변경 → 판 " << t.lot.epoch()
                          << " (기대 " << e0 << " — 상태는 판을 안 올린다)\n";
                if (!ok25) bad++;
            }

            // ㉓ 🔴 **`map` 봉투** (REQ-0203 4b) — web 이 이 형식에 맞춰 이미 구현했다
            {
                Server t;
                t.build_default_zones();
                t.park.devid = "P1";
                t.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                t.bind_modules(t.park);
                std::string j = t.map_json();
                bool ok26 = (j.find("\"type\":\"map\"") != std::string::npos
                             && j.find("\"epoch\":2") != std::string::npos
                             && j.find("\"rows\":5") != std::string::npos
                             && j.find("\"cells\":[[0,0]]") != std::string::npos
                             && j.find("\"devid\":\"P1\",\"name\":\"A1\",\"kind\":\"IP\",\"idx\":0")
                                != std::string::npos);
                // 🔴 **`srv_id` 가 봉투에 있어야 한다** — 없으면 재기동 뒤 화면이 새 맵을 무시한다
                t.init_srv_id();
                std::string j3 = t.map_json();
                bool ok26b = (j3.find("\"srv_id\":") != std::string::npos && !t.srv_id.empty());
                std::cout << (ok26b ? "  ✓ " : "  ✗ ") << "srv_id 가 map 봉투에 있다 ("
                          << t.srv_id.substr(0, 24) << "…)\n";
                if (!ok26b) bad++;

                std::cout << (ok26 ? "  ✓ " : "  ✗ ") << "map 봉투: type·epoch·grid·cells·모듈("
                          << "devid+name+kind+idx) 전부 포함\n";
                if (!ok26) bad++;

                // 🔴 **`kind`·`idx` 를 노드에서 찾는다 — 이름만으로 찾으면 안 된다**(복합 키)
                //    다른 노드가 같은 `name` 을 가져도 섞이면 안 된다.
                Node& other = t.aux["P9"]; other.devid = "P9";
                other.mods.push_back(std::make_pair(std::string("A1"), std::string("OB")));
                std::string j2 = t.map_json();
                bool ok27 = (j2.find("\"devid\":\"P1\",\"name\":\"A1\",\"kind\":\"IP\"")
                             != std::string::npos
                             && j2.find("\"kind\":\"OB\"") == std::string::npos);
                std::cout << (ok27 ? "  ✓ " : "  ✗ ") << "복합 키: 다른 노드의 같은 이름 모듈이 "
                          << "섞이지 않는다 (기대: P1 의 IP 만)\n";
                if (!ok27) bad++;
                t.aux.clear();
            }

            // ㉔ 🔴 **`get_map` 상한** — 화면이 비교를 잘못 구현하면 무한 재요청이 된다
            {
                Server t; t.build_default_zones();
                long long r0 = t.getmap_rejects;
                for (int i = 0; i < GETMAP_MAX_PER_SEC + 3; i++)
                    t.on_ws_message(BAD_SOCK, "{\"type\":\"get_map\",\"rid\":\"r\"}");
                bool ok28 = (t.getmap_rejects == r0 + 3);
                std::cout << (ok28 ? "  ✓ " : "  ✗ ") << "get_map 상한: " << (GETMAP_MAX_PER_SEC + 3)
                          << "회 중 거절 " << t.getmap_rejects << " (기대 3)\n";
                if (!ok28) bad++;
            }

            // ㉔-나 🔴 **상한은 화면마다다 — 남의 화면이 내 몫을 먹지 않는다** (2026-08-19)
            //   주석은 원래 *"화면 하나가 1초에"* 라고 말했는데 **구현은 서버 전역**이었다.
            //   그러면 **화면 여섯이 재접속하는 것만으로** 정상 요청이 거절되고,
            //   거절당한 화면은 **지형을 못 받아 빈 채로 남는다.**
            //   🔑 고친 것은 상한값이 아니라 **누구를 세는가**다. 그래서 시험도 "둘이 각각"이다.
            {
                sock_t p1[2], p2[2];
                bool okpair = (socketpair(AF_UNIX, SOCK_STREAM, 0, p1) == 0
                               && socketpair(AF_UNIX, SOCK_STREAM, 0, p2) == 0);
                if (okpair) {
                    Server t; t.build_default_zones(); t.init_srv_id();
                    t.conns[p1[0]].kind = Conn::WS;
                    t.conns[p2[0]].kind = Conn::WS;
                    long long r0 = t.getmap_rejects;
                    // 각 화면이 **상한만큼** 묻는다 → 합은 상한의 두 배지만 **거절은 0 이어야 한다**
                    for (int i = 0; i < GETMAP_MAX_PER_SEC; i++) {
                        t.on_ws_message(p1[0], "{\"type\":\"get_map\",\"rid\":\"a\"}");
                        t.on_ws_message(p2[0], "{\"type\":\"get_map\",\"rid\":\"b\"}");
                    }
                    bool ok28b = (t.getmap_rejects == r0);
                    std::cout << (ok28b ? "  ✓ " : "  ✗ ") << "화면 둘이 각각 "
                              << GETMAP_MAX_PER_SEC << "회 → 거절 " << (t.getmap_rejects - r0)
                              << " (기대 0 · 전역 상한이면 " << GETMAP_MAX_PER_SEC << ")\n";
                    if (!ok28b) bad++;

                    // 그리고 **한 화면이 넘기면 그 화면만** 거절된다
                    for (int i = 0; i < 3; i++)
                        t.on_ws_message(p1[0], "{\"type\":\"get_map\",\"rid\":\"a\"}");
                    bool ok28c = (t.getmap_rejects == r0 + 3);
                    std::cout << (ok28c ? "  ✓ " : "  ✗ ") << "넘긴 화면만 거절된다 (거절 "
                              << (t.getmap_rejects - r0) << " · 기대 3)\n";
                    if (!ok28c) bad++;
                    t.conns.clear();
                    closesock(p1[0]); closesock(p1[1]); closesock(p2[0]); closesock(p2[1]);
                }
            }

            // ㉔-다 🔴 **빈 지형은 `map` 으로 안 나간다** — 첫 배포가 정확히 그 상태였다
            //   그때 화면은 **"자리 0개인 주차장"** 을 정상으로 그렸다.
            //   🔑 **빈 지형은 답이 아니라 고장이다. 답인 척하면 아무도 안 본다.**
            {
                sock_t p3[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, p3) == 0) {
                    Server t; t.init_srv_id();          // 🔑 **`build_default_zones()` 를 일부러 안 부른다**
                    t.conns[p3[0]].kind = Conn::WS;
                    t.push_map();
                    char b2[256];
                    int r = (int)recv(p3[1], b2, sizeof(b2), MSG_DONTWAIT);
                    bool ok28d = (t.lot.zones().empty() && r <= 0);
                    std::cout << (ok28d ? "  ✓ " : "  ✗ ") << "빈 지형 → map 미송신 (전선 "
                              << (r > 0 ? r : 0) << "B · 기대 0)\n";
                    if (!ok28d) bad++;
                    t.conns.clear();
                    closesock(p3[0]); closesock(p3[1]);
                }
            }

            // ㉕ 🔴 **`state` 봉투와 `actions`** (REQ-0203 4c)
            {
                Server t; t.build_default_zones(); t.init_srv_id();
                std::string j = t.state_json();
                // 🔴 **2026-08-19 — 이 시험은 예전에 `module_absent` 를 기대했다. 바꿨다.**
                //   여기는 **노드가 아예 없는** 상태다(`all_nodes()` 가 빈다).
                //   `module_absent` 는 *"이 자리엔 장비가 없다"* = **영구**이고, 화면은 버튼을
                //   **안 그린다.** 그런데 진실은 *"아직 아무도 안 붙었다"* = **일시**다.
                //   🔑 **코드에 맞춰 시험을 고친 것이 아니다** — 어느 답이 화면에 옳은지로 골랐다.
                //   ⚠ 그리고 **셋을 다 밟는다.** 하나만 보면 나머지 둘이 서로 바뀌어도 통과한다.
                bool a1 = (j.find("\"reserve\":{\"ok\":false,\"reason\":\"node_offline\"}")
                           != std::string::npos)
                       && (j.find("\"cancel\"") == std::string::npos);
                std::cout << (a1 ? "  ✓ " : "  ✗ ") << "노드 없음 → reserve 막힘(node_offline) · "
                          << "예약 없으니 **cancel 키 자체가 없다**\n";
                if (!a1) bad++;

                // (나) — 노드가 붙었는데 **등록 중**이면 `node_unregistered`
                {
                    Server u; u.build_default_zones(); u.init_srv_id();
                    u.park.devid = "P1"; u.park.reg_done = false; u.park.reg_giveup = false;
                    bool b1 = (u.state_json().find("\"reason\":\"node_unregistered\"")
                               != std::string::npos);
                    std::cout << (b1 ? "  ✓ " : "  ✗ ") << "등록 중 → node_unregistered"
                              << "(**곧 붙는다**를 영구 부재로 답하지 않는다)\n";
                    if (!b1) bad++;

                    // (라) 🔴 **차가 있는 자리는 예약을 못 내놓는다** (REQ-0235)
                    //   ⚠ 이 시험이 없으면 "여는 근거와 막는 근거가 다른" 상태가 다시 생긴다 —
                    //     화면이 버튼을 그리고 장치가 `result=1` 로 거절하는 **눌렀는데 안 되는 버튼**.
                    {
                        // ⚠ **장치가 온라인이어야 이 갈래에 닿는다.** 안 세우면 `node_offline` 이
                        //   먼저 걸려 **막힌 이유가 달라진 채로 시험이 빨강**이 된다 —
                        //   처음에 그렇게 실패했고, 그게 "시험이 실기 상태를 안 세운" 것이다.
                        sock_t ws_[2];
                        if (socketpair(AF_UNIX, SOCK_STREAM, 0, ws_) != 0) { ws_[0] = ws_[1] = BAD_SOCK; }
                        Server w; w.build_default_zones(); w.init_srv_id();
                        w.ard = ws_[0]; w.ard_seen = true; w.ard_last_ms = now_ms();
                        w.park.devid = "P1"; w.park.reg_done = true;
                        for (int i = 0; i < 10; i++)
                            w.park.mods.push_back(std::make_pair(std::string(SLOT_ID[i]),
                                                                 std::string("IP")));
                        w.bind_modules(w.park);
                        w.slots[2].occupied = 1;              // A3 만 점유
                        std::string jw = w.state_json();
                        size_t pa = jw.find("\"id\":\"A3\"");
                        std::string a3 = (pa == std::string::npos) ? "" : jw.substr(pa, 400);
                        bool d1 = (a3.find("\"reserve\":{\"ok\":false,\"reason\":\"occupied\"}")
                                   != std::string::npos);
                        std::cout << (d1 ? "  ✓ " : "  ✗ ") << "점유된 A3 → reserve 가 "
                                  << "**ok:false/occupied** (키는 남긴다 — 차가 나가면 가능해진다)\n";
                        if (!d1) bad++;

                        // 🔑 **빈 자리는 그대로 열려 있어야 한다** — 안 그러면 전부 막아 놓고 통과한다
                        size_t pb = jw.find("\"id\":\"A1\"");
                        std::string a1 = (pb == std::string::npos) ? "" : jw.substr(pb, 400);
                        bool d2 = (a1.find("\"reserve\":{\"ok\":true") != std::string::npos);
                        std::cout << (d2 ? "  ✓ " : "  ✗ ") << "빈 A1 → reserve 는 열려 있다"
                                  << "(전부 막아 놓고 통과하는 것을 막는다)\n";
                        if (!d2) bad++;
                        w.ard = BAD_SOCK;
                        if (ws_[0] != BAD_SOCK) closesock(ws_[0]);
                        if (ws_[1] != BAD_SOCK) closesock(ws_[1]);
                    }

                    // (다) — 등록이 **끝났는데** 이 자리엔 안 붙었다 → 그때가 `module_absent` 다
                    u.park.reg_done = true;
                    bool c1 = (u.state_json().find("\"reason\":\"module_absent\"")
                               != std::string::npos);
                    std::cout << (c1 ? "  ✓ " : "  ✗ ") << "등록 끝 + 미결속 → module_absent"
                              << "(이때는 영구 부재가 정직하다)\n";
                    if (!c1) bad++;
                }

                // 예약되면 `reserve` 가 사라지고 `cancel` 이 나온다 — **존재/부재가 한 비트다**
                t.slots[0].reserved = 1;
                std::string j2 = t.state_json();
                bool a2 = (j2.find("\"cancel\":{") != std::string::npos);
                std::cout << (a2 ? "  ✓ " : "  ✗ ") << "예약 뒤 → cancel 키가 생긴다\n";
                if (!a2) bad++;

                // 입출구는 조작이 둘이다 — 🔑 **`bool` 하나였으면 못 갈렸을 자리**
                bool a3 = (j2.find("\"open_gate\":{") != std::string::npos
                           && j2.find("\"close_gate\":{") != std::string::npos);
                std::cout << (a3 ? "  ✓ " : "  ✗ ") << "입출구: open_gate·close_gate 둘 다 나온다\n";
                if (!a3) bad++;

                // 🔴 `state` 가 `srv_id`·`epoch` 를 싣는다 — 화면이 낡음을 스스로 안다
                bool a4 = (j2.find("\"srv_id\":") != std::string::npos
                           && j2.find("\"epoch\":1") != std::string::npos);
                std::cout << (a4 ? "  ✓ " : "  ✗ ") << "state 에 srv_id·epoch 가 실린다\n";
                if (!a4) bad++;
            }

            // ㉗ 🔴 **기동 경로가 지형을 만드는가** — 시험이 자기 지형을 만들면 이걸 못 잡는다
            //
            //   🔴🔴 **이 시험은 REQ-0276 사고를 못 잡았다. 못 잡은 게 아니라 *같이 틀렸다*.**
            //     정규식 치환이 **코드의 키와 이 시험의 기대값을 동시에** `"lot.zones()"` 로 바꿨다.
            //     → 양쪽이 같이 틀려서 **114개 전부 통과**했고 실기 화면만 죽었다.
            //   🔑 **기대값을 코드와 같은 도구로 만들면 이 부류가 구조적으로 못 잡힌다.**
            //     계약의 이름은 **손으로 박아라**(㉟ 가 그 방식이다).
            //    실제로 첫 배포에서 `lot.zones()` 가 빈 채 나갔고 **시험은 전부 통과했다.**
            {
                Server t;                                   // 🔑 **아무것도 안 부른 상태**
                bool empty0 = t.lot.zones().empty() && t.lot.epoch() == 0;
                std::string j = t.map_json();
                bool ok31 = (empty0 && j.find("\"zones\":[]") != std::string::npos);
                std::cout << (ok31 ? "  ✓ " : "  ✗ ") << "새 Server 는 지형이 비어 있다 — "
                          << "**기동 경로가 build_default_zones() 를 불러야 한다**"
                          << " (map 의 zones=" << (j.find("\"zones\":[]") != std::string::npos ? "빈 배열" : "채워짐")
                          << ")\n";
                if (!ok31) bad++;
            }

            // ㉖ 🔴 **조작 요청 라우팅** (REQ-0203 4d) — **거절 사유가 갈려야 한다**
            {
                LoopPair lw;                       // 화면 소켓 자리(진짜 fd 가 있어야 응답이 나간다)
                Server t; t.build_default_zones(); t.init_srv_id();
                t.conns[lw.a].kind = Conn::WS;     // 🔑 **WS 로 승격해야 응답이 나간다**

                // ① 없는 자리
                t.on_ws_message(lw.a, "{\"type\":\"open_gate\",\"slot\":\"ZZ\",\"rid\":\"1\"}");
                // ② 차단봉이 없는 주차 자리 — **조용히 무시하지 않는다**
                t.on_ws_message(lw.a, "{\"type\":\"open_gate\",\"slot\":\"A1\",\"rid\":\"2\"}");
                // ③ 입구인데 모듈이 안 붙었다 → module_absent
                t.on_ws_message(lw.a, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"3\"}");
                // ⚠ **여기도 재시도해야 한다.** 처음엔 한 번만 읽었고 디버그 빌드에서는 우연히
                //   통과했다 — 🔴 **`-O2` 빌드가 더 빨라서 그 우연이 깨졌다.**
                //   **배포용 빌드를 돌려 본 것이 이 결함을 드러냈다.**
                // 🔴 **한 번의 `recv` 에 단언하지 않는다** (2026-08-19 수리)
                //   전에는 첫 성공 `recv` 하나로 판정하고 **그 바이트 수를 찍었다.** 그 값이
                //   `93B` 와 `199B` 사이를 오갔다 — **응답 셋이 한 번에 오기도 하고 나뉘어 오기도 한다.**
                //   🔑 **부분 읽기는 정상이다.** TCP 는 스트림이지 메시지가 아니다(내 도메인 제1규칙).
                //   ⚠ 그런데 그 비결정 줄이 **출력 대조라는 판별 도구 자체를 흔들었다** —
                //     ②-a·②-c 대조에서 매번 잡음으로 나왔다. 판별자를 쓰기 직전이 고칠 마지막 기회다.
                //   ⚠ 그리고 더 나쁜 것: 첫 조각만 읽고 단언하면 **셋 중 하나만 보고 통과**할 수 있다.
                //     §"헛통과" 부류다 — 밟긴 밟았는데 다른 것을 밟았다.
                //   → **더 안 올 때까지 모으고, 찍는 값도 바이트가 아니라 *건수* 로 바꾼다.**
                char rb[4096]; std::string got; int idle = 0;
                for (int tries = 0; tries < 4000 && idle < 200; tries++) {
                    int n = (int)recv(lw.b, rb, sizeof(rb), MSG_DONTWAIT);
                    if (n > 0) { got.append(rb, n); idle = 0; } else idle++;
                }
                int msgs = 0;
                for (size_t q = got.find("\"rid\""); q != std::string::npos; q = got.find("\"rid\"", q + 1)) msgs++;
                bool ok29 = (got.find("module_absent") != std::string::npos && msgs == 3);
                std::cout << (ok29 ? "  ✓ " : "  ✗ ") << "조작 요청: 없는 자리·차단봉 없는 자리·"
                          << "미결속 입구 → 전부 사유가 간다(응답 " << msgs << "건 · 기대 3)\n";
                if (!ok29) bad++;

                // 🔴 ④ 모듈이 붙고 노드가 살아 있어도 **전선 명령이 없으면 not_supported**
                //    ⚠ 조용히 성공으로 답하면 화면이 "열렸다"로 그리고 아무 일도 안 일어난다
                Zone* e1 = t.zone_find("E1");
                e1->modules.push_back(std::make_pair(std::string("P1"), std::string("E1")));
                // 🔑 **자기 소켓쌍을 준다.** 앞 응답 셋이 남은 버퍼에서 읽으면
                //    **시험이 재려던 것이 아니라 앞 시험의 응답을 재게 된다** — 실제로 그랬다.
                LoopPair lx; t.conns[lx.a].kind = Conn::WS;
                t.park.devid = "P1"; t.park.fd = lx.a; t.park.seen = true;
                t.park.last_ms = now_ms(); t.park.reg_done = true;
                t.on_ws_message(lx.a, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"4\"}");
                // ⚠ **루프백 TCP 는 `socketpair` 와 달리 즉시 도착하지 않는다.**
                //   한 번만 읽고 `-1`(EWOULDBLOCK)을 "안 보냈다"로 읽으면 **없는 결함이 생긴다** —
                //   실제로 그렇게 나왔다. 짧게 재시도한다.
                // 🔑 여기도 **모아서 읽는다**(앞 시험과 같은 이유 · 부분 읽기는 정상이다)
                std::string g2; int idle2 = 0;
                for (int tries = 0; tries < 4000 && idle2 < 200; tries++) {
                    int n2 = (int)recv(lx.b, rb, sizeof(rb), MSG_DONTWAIT);
                    if (n2 > 0) { g2.append(rb, n2); idle2 = 0; } else idle2++;
                }
                bool ok30 = (g2.find("not_supported") != std::string::npos);
                if (!ok30) std::cout << "    [진단] lx.a=" << lx.a << " lx.b=" << lx.b
                                     << " conns=" << t.conns.count(lx.a) << " 받은B=" << g2.size() << "\n";
                std::cout << (ok30 ? "  ✓ " : "  ✗ ") << "준비된 자리인데 전선 명령이 없다 → "
                          << "**not_supported**(거짓 완료를 안 만든다)\n";
                if (!ok30) bad++;
                t.park.fd = BAD_SOCK; t.conns.clear();
            }

            // ㉘ 🔴🔴 **자리 비트열 해독** — 이 결함이 2026-08-19 에 **실기에서 살아 있었다**
            //    장치가 hex 를 보내는데 서버가 10진으로 읽어 **엉뚱한 자리를 점유로 표시했다.**
            //    폭 검사도 체크섬도 통과했다. **검사가 있어서 처리된 것처럼 보였다.**
            {
                Server t; t.ard = sv[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                // ① 등록 전 hex 폭 → **해독하지 않는다**(n 을 모른다)
                t.on_ard_line(t_line("S,1,182,000,5,P1,"));
                bool okA = (t.occ_undecoded == 1 && t.slots[0].occupied == 0);
                std::cout << (okA ? "  ✓ " : "  ✗ ") << "등록 전 hex → 해독 안 함(미해독 "
                          << t.occ_undecoded << " · A1 점유 " << t.slots[0].occupied << ")\n";
                if (!okA) bad++;

                // ② 등록 뒤 → hex 로 푼다. `182` = 0110000010 → **A2·A3·B4**
                t.on_ard_line(t_line("D,*,7,10,"));
                for (int i = 0; i < 10; i++) t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                t.on_ard_line(t_line("S,2,182,000,6,P1,"));
                bool okB = (t.slots[0].occupied == 0 && t.slots[1].occupied == 1
                            && t.slots[2].occupied == 1 && t.slots[8].occupied == 1
                            && t.slots[9].occupied == 0);
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "등록 뒤 hex `182` → A2·A3·B4 점유"
                          << " (A1=" << t.slots[0].occupied << " A2=" << t.slots[1].occupied
                          << " B4=" << t.slots[8].occupied << " · 기대 0·1·1)\n";
                if (!okB) bad++;
                t.ard = BAD_SOCK;
            }
            {
                // ③ 🔴 **옛 10진 펌웨어는 등록을 안 해도 그대로 읽힌다** — 회귀를 안 만든다
                Server t; t.ard = sv[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                t.on_ard_line(t_line("S,1,0110000010,0000000000,5,P1,"));
                bool okC = (t.slots[1].occupied == 1 && t.slots[2].occupied == 1
                            && t.slots[8].occupied == 1 && t.occ_undecoded == 0);
                std::cout << (okC ? "  ✓ " : "  ✗ ") << "옛 10진(미등록) → 그대로 읽는다 "
                          << "(미해독 " << t.occ_undecoded << " · 기대 0)\n";
                if (!okC) bad++;
                t.ard = BAD_SOCK;
            }

            // ㉙ 🔴🔴 **자가 치유(§7.6)가 실제로 돈다** — 2026-08-19 에 이 분기가 죽어 있었다
            //    hex 전환 뒤 조건이 `f[3].size() >= 10` 이라 **영영 거짓**이었다.
            //    ⚠ **오독이 아니라 건너뜀이라 로그에 `0` 조차 안 남았다.** 그래서 **분모를 검사한다** —
            //    "몇 번 고쳤나"가 아니라 **"몇 번 검사했나"** 가 이 결함을 잡는 유일한 값이다.
            {
                // 🔴 **자기 소켓을 쓴다. 공유 `sv` 를 안 쓴다.**
                //   앞선 시험이 `sv[0]` 을 닫아 두면 이 시험은 **하행이 안 나가는데 계수가 전부 0** 이라
                //   *"치유가 안 나간다"* 로 오독된다 — 실제로 그렇게 한 번 헤맸다.
                //   🔑 §8.23 의 "시험끼리 자원을 공유하면 실패 원인이 남의 시험에 있다" 그대로다.
                sock_t ms[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, ms) != 0) { ms[0] = ms[1] = BAD_SOCK; }
                Server t; t.ard = ms[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                t.on_ard_line(t_line("D,*,7,10,"));
                for (int i = 0; i < 10; i++) t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));

                // 장치는 A1 이 예약됐다고 말하는데 서버는 0 이다 → `C` 재하달로 맞춰야 한다.
                // `res` = hex `200` = 비트 9 = **슬롯 0(A1)**  (n=10 · 슬롯 i = 비트 n-1-i)
                long long q0 = t.heal_fires, c0 = t.heal_checks;
                t.on_ard_line(t_line("S,2,000,200,6,P1,"));
                bool okD = (t.heal_checks > c0);
                std::cout << (okD ? "  ✓ " : "  ✗ ") << "hex 예약 마스크로 **치유 검사가 돈다**"
                          << " (검사 " << (t.heal_checks - c0) << " · 기대 >0)\n";
                if (!okD) bad++;

                bool okE = (t.heal_fires == q0 + 1);
                std::cout << (okE ? "  ✓ " : "  ✗ ") << "불일치를 찾아 C 를 재하달한다 (재하달 "
                          << (t.heal_fires - q0) << " · 기대 1)\n";
                if (!okE) bad++;

                // 🔴 **비트 순서까지 확인한다.** `200` 이 A1 이어야 한다 — 뒤집혀도 건수는 1 이라
                //    위 두 검사만으로는 **자리가 어긋난 것을 못 잡는다**(§8.23-(58) 과 같은 함정).
                // 🔑 **내부 상태가 아니라 전선에 나간 바이트를 읽는다** — `pend` 는 ACK 나
                //    링크 실패로 비워질 수 있어서 **"안 보인다"가 "안 나갔다"를 뜻하지 않는다.**
                //    오늘 `49c07f6` 을 확인한 방법과 같다: **나가는 값을 본다.**
                // 🔑 **하행은 이 창에 안 나간다 — 다음 `S` 가 창을 연다**(설계 §3).
                //   ⚠ 처음에 이걸 빼먹어 전선이 비어 있었고, 하마터면 *"치유가 안 나간다"* 로
                //     읽을 뻔했다. **큐에 있는 것과 안 나간 것은 다르다.**
                //   두 번째 `S` 는 `res=000` 으로 보낸다 — 새 불일치를 안 만들어야 위 건수가 1 로 남는다.
                t.on_ard_line(t_line("S,3,000,000,7,P1,"));
                char wb[512]; std::string wire;
                for (;;) {
                    int r = (int)recv(ms[1], wb, sizeof(wb), MSG_DONTWAIT);
                    if (r <= 0) break;
                    wire.append(wb, (size_t)r);
                }
                bool okF = (wire.find(",A1,") != std::string::npos);
                std::cout << (okF ? "  ✓ " : "  ✗ ") << "전선에 나간 `C` 의 대상이 **A1** 이다"
                          << "(비트 순서가 뒤집혀도 건수는 1 이라 여기서만 갈린다)\n";
                if (!okF) { bad++; std::cout << "      전선(" << wire.size() << "B): " << wire
                                             << " · 큐 " << t.downq.size()
                                             << " · 배치 " << t.batch_count << "\n"; }
                t.ard = BAD_SOCK;
                if (ms[0] != BAD_SOCK) closesock(ms[0]);
                if (ms[1] != BAD_SOCK) closesock(ms[1]);
            }

            // ㉚ 🔴🔴 **자리 조작 `G` — 전선까지 간다** (REQ-0228 · arduino 파서에서 형식을 읽어 맞췄다)
            //    ⚠ **이 시험은 실기가 아직 못 만드는 상태를 만든다** — `명령가능 0개` 인 지금은
            //      `dispatch_gate` 가 실기에서 한 번도 안 돈다. **가상 모듈이 구워지면 그때 돈다.**
            //      🔑 그래서 **등록 순서를 실기와 똑같이** 세운다(자리 10 뒤에 차단봉 2) —
            //      순서를 다르게 세우면 `idx` 가 실기와 달라져 **시험만 통과하는 코드**가 된다.
            {
                sock_t gs[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, gs) == 0) {
                    Server t; t.build_default_zones(); t.init_srv_id();
                    t.ard = gs[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                    // n=12 : 자리 10(IP) + E1·X1 (**OB** = 차단봉)
                    // 🔴 옛 값은 `OBV` 였다 — 끝의 `V` 가 "가상"을 뜻했다(REQ-0271 로 없앤다).
                    //   🔑 **서버는 그 `V` 를 한 번도 읽지 않았다** — `kind_commandable()` 이
                    //     `k[0]=='O'` 만 본다. 그래서 픽스처를 `OB` 로 바꿔도 아무것도 안 깨진다.
                    //   ⚠ **이 시험이 그 사실의 증명이다.** 깨지면 내 판독이 틀린 것이다.
                    t.on_ard_line(t_line("D,*,7,12,"));
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OB,"));
                    t.on_ard_line(t_line("D,X1,OB,"));

                    bool okG0 = (t.park.reg_done && t.park.reg_n == 12 && t.reg_cmdable() == 2);
                    std::cout << (okG0 ? "  ✓ " : "  ✗ ") << "OB 2개가 명령가능으로 세어진다 ("
                              << t.reg_cmdable() << " · 기대 2 · `V` 접미는 종류를 안 바꾼다)\n";
                    if (!okG0) bad++;

                    // 창을 열고(첫 S) 큐를 비운 뒤 조작을 건다
                    t.on_ard_line(t_line("S,1,000,000,5,P1,"));
                    char gb[512];
                    while (recv(gs[1], gb, sizeof(gb), MSG_DONTWAIT) > 0) {}   // 전선 비우기

                    t.on_ws_message(BAD_SOCK, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"g1\"}");
                    t.on_ard_line(t_line("S,2,000,000,6,P1,"));                // 다음 창에 나간다
                    std::string gw;
                    for (;;) { int r = (int)recv(gs[1], gb, sizeof(gb), MSG_DONTWAIT);
                               if (r <= 0) break; gw.append(gb, (size_t)r); }
                    // 🔴 **`idx` 는 10 이어야 한다** — E1 은 자리 10개 **뒤**에 등록된 첫 모듈이다.
                    //    장치는 `idx >= SLOT_N` 만 받으므로 여기가 어긋나면 전부 `result=3` 이 된다.
                    bool okG1 = (gw.find("G,") != std::string::npos
                                 && gw.find(",10,1,") != std::string::npos);
                    std::cout << (okG1 ? "  ✓ " : "  ✗ ") << "open_gate(E1) → 전선에 `G,<rid>,10,1,`"
                              << (okG1 ? "" : std::string(" · 실제: ") + gw) << "\n";
                    if (!okG1) bad++;

                    // 닫기는 op=0
                    t.on_ws_message(BAD_SOCK, "{\"type\":\"close_gate\",\"slot\":\"X1\",\"rid\":\"g2\"}");
                    t.on_ard_line(t_line("S,3,000,000,7,P1,"));
                    gw.clear();
                    for (;;) { int r = (int)recv(gs[1], gb, sizeof(gb), MSG_DONTWAIT);
                               if (r <= 0) break; gw.append(gb, (size_t)r); }
                    bool okG2 = (gw.find(",11,0,") != std::string::npos);
                    std::cout << (okG2 ? "  ✓ " : "  ✗ ") << "close_gate(X1) → `,11,0,`"
                              << (okG2 ? "" : std::string(" · 실제: ") + gw) << "\n";
                    if (!okG2) bad++;

                    // 🔴 **완료는 ACK 이 아니라 다음 `S` 의 에코 비트다**
                    //    🔑 **자리 `i` 는 비트 `(n−1−i)`** 다(§5). `n=12` 이므로 **모듈 10 = 비트 1** →
                    //    hex `0x002` = `002`. ⚠ 처음에 `080`(비트 7)을 적었다가 시험이 잡았다 —
                    //    비트 7 은 모듈 4(A5)라 **자리 점유로 들어간다.** 값이 그럴듯해서 안 보인다.
                    t.on_ard_line(t_line("S,4,002,000,8,P1,"));
                    std::string js = t.state_json();
                    bool okG3 = (js.find("\"name\":\"E1\",\"idx\":10,\"value\":true,\"known\":true")
                                 != std::string::npos);
                    std::cout << (okG3 ? "  ✓ " : "  ✗ ") << "에코 비트 10 → E1 모듈 value=true·known=true"
                              << " (**ACK 이 아니라 `S` 가 답한다**)\n";
                    if (!okG3) bad++;

                    bool okG4 = (js.find("\"name\":\"X1\",\"idx\":11,\"value\":false,\"known\":true")
                                 != std::string::npos);
                    std::cout << (okG4 ? "  ✓ " : "  ✗ ") << "비트 11 이 0 → X1 value=false"
                              << "(비트 순서가 뒤집히면 여기서 갈린다)\n";
                    if (!okG4) bad++;

                    // 🔴 `result=3` 은 **`ack_timeout` 과 다른 칸**이다
                    long long d0 = t.dev_reject;
                    uint16_t rid3 = 0;
                    for (std::map<uint16_t, Pending>::iterator it = t.pend.begin();
                         it != t.pend.end(); ++it) if (it->second.kind == 'G') rid3 = it->first;
                    if (rid3) {
                        char ab[64];
                        snprintf(ab, sizeof(ab), "A,%u,G0,3,", rid3);
                        t.on_ard_line(t_line(ab));
                    }
                    bool okG5 = (t.dev_reject == d0 + 1);
                    std::cout << (okG5 ? "  ✓ " : "  ✗ ") << "result=3 → 장치거절 계수 "
                              << (t.dev_reject - d0) << " (기대 1 · ACK실패와 다른 칸)\n";
                    if (!okG5) bad++;

                    // ㉚-마 🔴 **화면 소켓을 실제로 물린다** — 위 검사들은 전부 `ws_fd = BAD_SOCK` 이라
                    //   **`send_ack` 자체를 건너뛰었다.** 그래서 `kind=='G'` 문구가 한 번도 안 돌았고
                    //   **차단봉을 열었는데 "예약되었습니다" 가 나가는 것을 아무도 못 봤다.**
                    //   ⚠ 그리고 `enqueue_down` 이 `ws_fd == BAD_SOCK` 을 **중요(거절 금지)** 로 분류하므로
                    //     실기(화면 조작 = 사용자 계열)와 **다른 갈래를 밟고 있었다.**
                    {
                        sock_t ws[2];
                        if (socketpair(AF_UNIX, SOCK_STREAM, 0, ws) == 0) {
                            t.conns[ws[0]].kind = Conn::WS;
                            // 🔴 **먼저 떠 있는 `G` 를 닫는다.** 안 그러면 `zone_block_reason` 이
                            //   `pending` 으로 새 요청을 거절한다 — **그게 옳은 거동이고**,
                            //   시험이 그것을 모르고 짜여 있었다(처음에 여기서 실패했다).
                            //   🔑 **같은 자리에 조작을 겹치지 않는 것**이 설계다. 시험이 설계를 따라야 한다.
                            for (std::map<uint16_t, Pending>::iterator it = t.pend.begin();
                                 it != t.pend.end(); ) {
                                if (it->second.kind == 'G') { char ab0[64];
                                    snprintf(ab0, sizeof(ab0), "A,%u,G0,0,", it->first);
                                    uint16_t rr = it->first; ++it; t.on_ard_line(t_line(ab0)); (void)rr; }
                                else ++it;
                            }
                            t.on_ws_message(ws[0], "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"g9\"}");
                            t.on_ard_line(t_line("S,5,002,000,9,P1,"));
                            uint16_t rg = 0;
                            for (std::map<uint16_t, Pending>::iterator it = t.pend.begin();
                                 it != t.pend.end(); ++it)
                                if (it->second.kind == 'G') rg = it->first;
                            char wsb[1024]; while (recv(ws[1], wsb, sizeof(wsb), MSG_DONTWAIT) > 0) {}
                            if (rg) { char ab[64]; snprintf(ab, sizeof(ab), "A,%u,G0,0,", rg);
                                      t.on_ard_line(t_line(ab)); }
                            std::string wsout;
                            for (;;) { int r = (int)recv(ws[1], wsb, sizeof(wsb), MSG_DONTWAIT);
                                       if (r <= 0) break; wsout.append(wsb, (size_t)r); }
                            bool okG6 = (wsout.find("차단봉을 열었습니다") != std::string::npos);
                            std::cout << (okG6 ? "  ✓ " : "  ✗ ") << "화면 ACK 문구가 `차단봉을 열었습니다`"
                                      << " (**예전엔 `예약되었습니다` 가 나갔다**)\n";
                            if (!okG6) bad++;
                            t.conns.clear();
                            closesock(ws[0]); closesock(ws[1]);
                        }
                    }

                    // ㉚-바 🔴 **완료 판정은 대조다** — "에코가 있다"가 아니라 "요청한 값과 같다"
                    //   E1 은 열라고 했고 비트 10 이 1 이다 → settled
                    //   X1 은 닫으라고 했고 비트 11 이 **1 이면** → 🔴 mismatch (장치가 안 했다)
                    t.on_ard_line(t_line("S,6,003,000,10,P1,"));   // 비트 1·0 = 모듈 10·11 둘 다 1
                    {
                        std::string js2 = t.state_json();
                        size_t px = js2.find("\"id\":\"X1\"");
                        std::string xseg = (px == std::string::npos) ? "" : js2.substr(px, 400);
                        bool okG7 = (xseg.find("\"completion\":\"mismatch\"") != std::string::npos);
                        std::cout << (okG7 ? "  ✓ " : "  ✗ ") << "닫으라 했는데 열려 있다 → "
                                  << "**completion=mismatch** (`settled` 로 답하면 그게 거짓 완료다)\n";
                        if (!okG7) bad++;

                        size_t pe = js2.find("\"id\":\"E1\"");
                        std::string eseg = (pe == std::string::npos) ? "" : js2.substr(pe, 400);
                        bool okG8 = (eseg.find("\"completion\":\"settled\"") != std::string::npos);
                        std::cout << (okG8 ? "  ✓ " : "  ✗ ") << "열라 했고 열려 있다 → completion=settled\n";
                        if (!okG8) bad++;
                    }

                    // ㉚-사 🔴 **세션이 끊기면 모듈 값을 모른다** — 낡은 값을 사실로 주장하지 않는다
                    t.end_ard_session("selftest 세션종료");
                    {
                        std::string js3 = t.state_json();
                        bool okG9 = (js3.find("\"name\":\"E1\",\"idx\":10,\"value\":null,\"known\":false")
                                     != std::string::npos);
                        std::cout << (okG9 ? "  ✓ " : "  ✗ ") << "세션 종료 뒤 모듈 값이 known=false"
                                  << " (**낡은 값을 known=true 로 주장하지 않는다**)\n";
                        if (!okG9) bad++;
                    }

                    // ㉚-아 🔴 **계수 넷의 불변식** — 하나가 안 오르면 여기서 걸린다
                    //   띄움 ≥ 도달 ≥ 응답 ≥ 거절.  각 화살표가 **다른 고장**이다:
                    //     띄움−도달 = 큐에서 죽음 · 도달−응답 = 나갔는데 무응답 · 응답−거절 = 성공
                    //   ⚠ **선언만 하고 안 올리는 계수**를 이 불변식이 잡는다 —
                    //     오늘 `mod_order_changed` 를 요약에 안 내보내 monitor 에게 없는 칸을 보라고 했고,
                    //     `gate_sent` 도 선언만 하고 안 올릴 뻔했다. **같은 결함을 두 번 하지 않으려고 넣는다.**
                    bool okC = (t.gate_q >= t.gate_sent && t.gate_sent >= t.gate_ans
                                && t.gate_ans >= t.dev_reject
                                && t.gate_q > 0 && t.gate_sent > 0 && t.gate_ans > 0);
                    std::cout << (okC ? "  ✓ " : "  ✗ ") << "G 계수 불변식 — 띄움 " << t.gate_q
                              << " ≥ 도달 " << t.gate_sent << " ≥ 응답 " << t.gate_ans
                              << " ≥ 거절 " << t.dev_reject << " (전부 >0)\n";
                    if (!okC) bad++;

                    t.ard = BAD_SOCK;
                    closesock(gs[0]); closesock(gs[1]);
                }
            }

            // ㉛ 🔴 **명세 §7.4 가 약속한 "재등록 앞부분 대조"가 실제로 도는가**
            //    ⚠ 적어 놓고 안 만들면 **지켜지는 것처럼 보인다.** 그래서 시험이 이 자리에 있다.
            {
                sock_t os_[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, os_) == 0) {
                    Server t; t.build_default_zones(); t.init_srv_id();
                    t.ard = os_[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                    t.on_ard_line(t_line("D,*,7,12,"));
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OB,"));
                    t.on_ard_line(t_line("D,X1,OB,"));
                    t.on_ard_line(t_line("S,1,000,000,5,P1,"));
                    t.on_ws_message(BAD_SOCK, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"z1\"}");
                    size_t pend0 = t.pend.size();

                    // 🔴 **중간에 끼워 넣은 재등록** — A1 앞에 새 모듈이 들어와 전부 밀린다
                    long long c0 = t.mod_order_changed;
                    t.on_ard_line(t_line("D,*,7,13,"));
                    t.on_ard_line(t_line("D,Z9,IP,"));                 // ← 끼어든 것
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OB,"));
                    t.on_ard_line(t_line("D,X1,OB,"));

                    bool okZ1 = (t.mod_order_changed == c0 + 1);
                    std::cout << (okZ1 ? "  ✓ " : "  ✗ ") << "중간 삽입 재등록을 잡는다 (순서변경 "
                              << (t.mod_order_changed - c0) << " · 기대 1)\n";
                    if (!okZ1) bad++;

                    bool okZ2 = (pend0 > 0 && t.pend.empty());
                    std::cout << (okZ2 ? "  ✓ " : "  ✗ ") << "떠 있던 `G` 를 버린다 (전 "
                              << pend0 << " → 후 " << t.pend.size()
                              << " · **그 idx 는 이제 다른 모듈이다**)\n";
                    if (!okZ2) bad++;

                    // 끝에만 붙이는 재등록은 **안 걸려야 한다** — 안 그러면 정상 확장을 막는다
                    long long c1 = t.mod_order_changed;
                    t.on_ard_line(t_line("D,*,7,13,"));
                    t.on_ard_line(t_line("D,Z9,IP,"));
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OB,"));
                    t.on_ard_line(t_line("D,X1,OB,"));
                    bool okZ3 = (t.mod_order_changed == c1);
                    std::cout << (okZ3 ? "  ✓ " : "  ✗ ") << "같은 순서 재등록은 안 걸린다 (거짓 경보 "
                              << (t.mod_order_changed - c1) << " · 기대 0)\n";
                    if (!okZ3) bad++;

                    t.ard = BAD_SOCK;
                    closesock(os_[0]); closesock(os_[1]);
                }
            }

            // ㉟ 🔴🔴 **전선 봉투의 키 이름을 단언한다** (REQ-0276 · 2026-08-19)
            //
            //   내가 `zones` → `lot.zones()` 정규식 치환을 하면서 **문자열 리터럴 안까지 바꿨다.**
            //   `map`·`state` 가 `"lot.zones()"` 라는 키를 냈고 **화면 격자가 10분 27초 동안 안 보였다.**
            //
            //   🔴 **자가검증 114개가 전부 통과했다. 못 잡은 게 아니라 *같이 틀렸다*** —
            //     ㉗ 의 기대값(`"zones":[]`)도 **같은 정규식이** 바꿨기 때문에 대조가 성립했다.
            //     > **기대값을 코드와 같은 도구로 만들면, 그 도구의 실수는 시험을 통과한다.**
            //     ⚠ web 이 같은 부류를 다른 경로로 밟았다 — **기대 표를 피검체에서 읽어** `[] == []` 공허 통과.
            //       **도구가 같아서 / 출처가 같아서. 둘 다 "기대값이 피검체와 독립인가"를 안 물었다.**
            //
            //   🔑 그래서 여기서는 **계약 이름을 손으로 박은 리터럴로** 둔다. 치환이 같이 못 바꾸도록.
            //
            //   ⚠ **이 시험의 분모는 "web 이 실제로 읽는 키"다**(2026-08-19 web 이 값으로 준 목록).
            //     **봉투 계약이 늘면 이 시험은 자동으로 안 는다.** 갱신 경로는 web 의 통보다 —
            //     화면이 읽는 키가 바뀌면 알려 주기로 했다. **안 오면 이 목록은 조용히 낡는다.**
            {
                Server t; t.build_default_zones(); t.init_srv_id();
                // 🔑 모듈이 결속돼야 `modules[]` 하위 키가 봉투에 나온다.
                //    빈 배열로 재면 **하위 키를 하나도 안 밟는다** — 통과 수만 늘고 분모는 0 이다.
                t.park.devid = "P1";
                t.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                t.bind_modules(t.park);
                const std::string m  = t.map_json();
                const std::string st = t.state_json();

                // ── web 이 실제로 파싱하는 키 (그쪽이 값으로 준 목록) ────────────────
                static const char* mapKeys[] = {
                    "\"type\":\"map\"", "\"srv_id\":", "\"epoch\":",
                    "\"grid\":", "\"rows\":", "\"cols\":",
                    "\"zones\":[", "\"id\":", "\"kind\":", "\"cells\":", "\"modules\":[",
                    "\"devid\":", "\"name\":", "\"idx\":"
                };
                static const char* stateKeys[] = {
                    "\"type\":\"state\"", "\"srv_id\":", "\"epoch\":", "\"ts_ms\":",
                    "\"zones\":[", "\"id\":", "\"occupied\":", "\"reserved\":",
                    "\"actions\":", "\"completion\":", "\"modules\":[",
                    "\"devid\":", "\"name\":", "\"idx\":", "\"value\":", "\"known\":"
                };
                bool ok35 = true; std::string missing;
                for (size_t i = 0; i < sizeof(mapKeys)/sizeof(mapKeys[0]); i++)
                    if (m.find(mapKeys[i]) == std::string::npos) { ok35 = false; missing += std::string(" map:") + mapKeys[i]; }
                for (size_t i = 0; i < sizeof(stateKeys)/sizeof(stateKeys[0]); i++)
                    if (st.find(stateKeys[i]) == std::string::npos) { ok35 = false; missing += std::string(" state:") + stateKeys[i]; }

                // 🔴 **서명 검사** — 키에 `.` 나 `(` 가 하나라도 있으면 실패.
                //    이번 사고의 서명이 정확히 그것이었다(C++ 표현식이 키 자리에 들어갔다).
                //    🔑 위 목록은 **아는 키만** 지키고, 이 검사는 **모르는 키까지** 잡는다.
                const std::string* both[2]; both[0] = &m; both[1] = &st;
                for (int w = 0; w < 2 && ok35; w++) {
                    const std::string& j = *both[w];
                    for (size_t q = j.find("\":"); q != std::string::npos; q = j.find("\":", q + 1)) {
                        size_t b = j.rfind('"', q - 1);
                        if (b == std::string::npos) continue;
                        std::string k = j.substr(b + 1, q - b - 1);
                        if (k.find('.') != std::string::npos || k.find('(') != std::string::npos) {
                            ok35 = false; missing += std::string(w ? " state이상한키:" : " map이상한키:") + k; break;
                        }
                    }
                }
                std::cout << (ok35 ? "  ✓ " : "  ✗ ") << "전선 봉투의 키가 계약대로다 (map "
                          << sizeof(mapKeys)/sizeof(mapKeys[0]) << "종 · state "
                          << sizeof(stateKeys)/sizeof(stateKeys[0]) << "종 + 서명검사)"
                          << (ok35 ? "" : (" — 🔴" + missing)) << "\n";
                if (!ok35) bad++;
            }


            // ㉞ 🔴 **`known:false` 의 사유가 갈린다** (명세 §8.10)
            //    ⚠ `ws_probe` 는 메시지를 잘라 찍어 `state` 전문을 못 읽는다 —
            //      그래서 **여기서 `state_json()` 문자열을 직접 본다.** 도구 한계를 시험으로 메운다.
            {
                Server t; t.build_default_zones(); t.init_srv_id();
                // (가) 주 노드가 등록도 안 된 상태 → 그 자리 모듈이 아예 없다(모듈 배열이 빈다)
                std::string j0 = t.state_json();
                bool okA = (j0.find("\"value_state\":\"unknown\"") != std::string::npos);

                // (나) 보조 노드가 등록만 됐다 → **값 경로가 없다** = bits_unavailable
                Node& aux2 = t.aux["P9"]; aux2.devid = "P9"; aux2.online = true;
                aux2.mods.push_back(std::make_pair(std::string("A2"), std::string("IP")));
                t.bind_modules(aux2);
                std::string j1 = t.state_json();
                bool okB = (j1.find("\"reason\":\"bits_unavailable\"") != std::string::npos);

                // (다) 그 보조 노드가 오프라인이면 사유가 바뀐다
                aux2.online = false;
                std::string j2 = t.state_json();
                bool okC = (j2.find("\"reason\":\"node_offline\"") != std::string::npos);

                bool ok34 = okA && okB && okC;
                std::cout << (ok34 ? "  ✓ " : "  ✗ ") << "known:false 사유가 갈린다 — "
                          << "미등록 unknown(" << (okA ? "예" : "🔴아니오") << ") · "
                          << "보조노드 bits_unavailable(" << (okB ? "예" : "🔴아니오") << ") · "
                          << "오프라인 node_offline(" << (okC ? "예" : "🔴아니오") << ")\n";
                if (!ok34) bad++;
                t.aux.clear();
            }

            // ㉝ 🔴 **`B*` 는 예약 대상이 아니다** (사용자 확정 (A) · 명세 §9.3)
            //    ⚠ `slot_index("B5")` 는 여전히 5 를 준다 — 그래서 **막지 않으면 예약이 성공한다.**
            //      성공했는데 화면에 안 보이는 것이 이 변경에서 가장 나쁜 결말이다.
            {
                LoopPair lb; Server t; t.build_default_zones(); t.init_srv_id();
                t.conns[lb.a].kind = Conn::WS;
                t.park.devid = "P1"; t.park.fd = lb.a; t.park.seen = true; t.park.last_ms = now_ms();
                t.on_ws_message(lb.a, "{\"type\":\"reserve\",\"slot\":\"B5\",\"rid\":\"b1\",\"user_id\":\"00000000\"}");
                t.on_ws_message(lb.a, "{\"type\":\"reserve\",\"slot\":\"A3\",\"rid\":\"b2\",\"user_id\":\"00000000\"}");
                char bb[4096]; std::string gb; int idleb = 0;
                for (int tries = 0; tries < 4000 && idleb < 200; tries++) {
                    int n = (int)recv(lb.b, bb, sizeof(bb), MSG_DONTWAIT);
                    if (n > 0) { gb.append(bb, n); idleb = 0; } else idleb++;
                }
                // 🔑 **B5 는 거절되고 A3 은 안 거절돼야 한다.** 둘 다 봐야 "전부 막혔다"와 갈린다.
                const bool okB = (gb.find("not_reservable") != std::string::npos)
                                 && (t.not_reservable_n == 1)
                                 && (gb.find("\"rid\":\"b2\",\"slot\":\"A3\"") != std::string::npos
                                     || gb.find("queued") != std::string::npos
                                     || t.pend.size() >= 1);
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "B5 예약은 not_reservable 로 거절 · A3 은 통과"
                          << " (비자리예약 " << t.not_reservable_n << " · 기대 1)\n";
                if (!okB) bad++;
                t.park.fd = BAD_SOCK; t.conns.clear();
            }

            // ㉜ 🔴 **A[1] `rid` 폭 고정과 격리** — 정본 `docs/net/DESIGN-rid-width-and-quarantine.md`
            //
            // 🔴 **분모를 먼저 적는다.** 아래 검사가 **밟지 못하는 것**:
            //   · 장치 멱등 캐시의 실제 삼킴 — 실기 장치가 있어야 한다. 여기서는 못 만든다
            //   · 늦은 ACK 의 실제 최대 지연 — 격리 값(§3.1)은 **가정이고 잰 적이 없다**
            //   · 재시작 충돌(§4) — 확률 1.6% 사건이라 시험으로 재현할 수 없다
            //   **그러므로 이 항목이 전부 ✓ 라도 "안전이 증명됐다"가 아니다.**
            struct NoneInUse { bool operator()(uint16_t) const { return false; } };
            struct AllButOne { uint16_t keep;
                               bool operator()(uint16_t r) const { return r != keep; } };
            // 🔴 **은닉 뒤로 옮기고 시험을 다시 썼다** (REQ-0272 3단계 · 2026-08-19)
            //   전에는 `t.rid_quar[321].until_ms = …` 처럼 **내부를 직접 만졌다.**
            //   `RidPool` 이 그것을 감추면서 그 시험이 못 쓰게 됐다 — **은닉의 대가다.**
            //   ✅ 대신 **시계를 인자로 받게 만든 덕에 시간을 통제해 계약으로 시험한다.**
            //   🔑 내부를 만지는 시험은 내부가 바뀌면 깨지고, **계약을 만지는 시험은 계약이 바뀔 때만 깨진다.**
            {
                // (a)(d) 폭 상한과 **재사용 간격** — 발행하고 곧바로 해제한다(같은 시각)
                RidPool pool; NoneInUse none;
                std::vector<int> last_at(RID_SPACE, -1);
                int minDist = 1 << 30; bool inRange = true; size_t maxDigits = 0;
                const long long T = 1000000;
                for (int i = 0; i < 2000; i++) {
                    uint16_t r = pool.alloc(none, T);
                    if (r == RID_NONE || r >= RID_SPACE) { inRange = false; break; }
                    size_t dg = std::to_string((unsigned)r).size();
                    if (dg > maxDigits) maxDigits = dg;
                    if (last_at[r] >= 0 && i - last_at[r] < minDist) minDist = i - last_at[r];
                    last_at[r] = i;
                    pool.release(r, T);
                }
                bool okA = inRange && maxDigits <= 3 && minDist >= DEV_RID_CACHE_N;
                std::cout << (okA ? "  ✓ " : "  ✗ ") << "rid 폭 ≤3자리(실측 " << maxDigits
                          << ") · 최소 재사용 간격 " << minDist
                          << " ≥ 장치 멱등창 " << DEV_RID_CACHE_N << "\n";
                if (!okA) bad++;
            }
            {
                // (b) 🔴 **하드 규칙** — 쓰이는 중인 rid 는 절대 발행하지 않는다.
                //     하나만 빼고 전부 "쓰는 중"이라고 답하면 **그 하나가 나와야 한다.**
                RidPool pool; AllButOne only; only.keep = 500;
                uint16_t got = pool.alloc(only, 1000000);
                bool okB = (got == 500 && pool.skips() > 0 && pool.forced() == 0);
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "쓰이는 중인 rid 를 피해 빈 칸을 고른다 (got "
                          << got << " · 기대 500 · 건너뜀 " << pool.skips() << ")\n";
                if (!okB) bad++;
            }
            {
                // (c) 격리는 **시간이 지나야** 풀린다 — 시계를 통제해 그것만 본다.
                RidPool pool; NoneInUse none;
                const long long T = 5000000;
                uint16_t first = pool.alloc(none, T);
                pool.release(first, T);
                bool blocked = true;                       // 같은 시각에 한 바퀴 — 그 값이 안 나와야 한다
                for (int i = 0; i < (int)RID_SPACE - 1; i++)
                    if (pool.alloc(none, T) == first) { blocked = false; break; }
                bool freed = false;                        // 격리가 지난 뒤에는 나와야 한다
                for (int i = 0; i < (int)RID_SPACE; i++)
                    if (pool.alloc(none, T + RID_QUARANTINE_MS + 1) == first) { freed = true; break; }
                bool okC = blocked && freed;
                std::cout << (okC ? "  ✓ " : "  ✗ ") << "격리 중에는 안 나오고(" << (blocked ? "예" : "🔴아니오")
                          << ") 지나면 나온다(" << (freed ? "예" : "🔴아니오") << ")\n";
                if (!okC) bad++;
            }
            {
                // (e) 전부 격리면 **가장 먼저 해제된 것**을 강제로 내준다(순번 FIFO).
                //     ⚠ **시각이 전부 같아도** 순번으로 갈리는지를 본다 — 그게 이 설계의 요점이다.
                RidPool pool; NoneInUse none;
                const long long T = 9000000;
                std::vector<uint16_t> order;
                for (int i = 0; i < (int)RID_SPACE; i++) {
                    uint16_t r = pool.alloc(none, T);
                    order.push_back(r);
                    pool.release(r, T);
                }
                uint16_t forced = pool.alloc(none, T);
                bool okE = (!order.empty() && forced == order[0] && pool.forced() == 1);
                std::cout << (okE ? "  ✓ " : "  ✗ ") << "전부 격리면 **가장 먼저 해제된 것**을 내준다 (got "
                          << forced << " · 기대 " << (order.empty() ? 0 : order[0])
                          << " · 강제 " << pool.forced() << ")\n";
                if (!okE) bad++;
            }
            {
                // (f) 🔴 **실기 발행 지점이 이 풀을 타는가.** (a)~(e)는 풀을 직접 부른다 —
                //     서버가 옛 경로로 남아 있어도 전부 통과한다. 그 구멍을 여기서 막는다.
                Server t; t.ard = BAD_SOCK;
                long long n0 = t.ridpool_.allocN();
                t.dispatch_sim(BAD_SOCK, "selftest-M");
                bool okF = (t.ridpool_.allocN() == n0 + 1);
                std::cout << (okF ? "  ✓ " : "  ✗ ") << "dispatch_sim 이 RidPool 을 탄다 (발행 "
                          << (t.ridpool_.allocN() - n0) << " · 기대 1)\n";
                if (!okF) bad++;
            }
            {
                // (g) 🔴 **커서 영속은 기본이 꺼져 있다** — 시험이 실기 커서를 덮어쓰지 못하게.
                //     그리고 **예약값은 커서보다 항상 앞서 있어야 한다** — 그게 영속의 전부다.
                Server t;
                bool offByDefault = (t.ridpool_.persistOn() == false);
                for (int i = 0; i < 700; i++) t.alloc_rid();
                bool ahead = (t.ridpool_.reservedTo() >= t.ridpool_.cursor());
                bool okG = offByDefault && ahead;
                std::cout << (okG ? "  ✓ " : "  ✗ ") << "커서 영속 기본 꺼짐(" << (offByDefault ? "예" : "🔴아니오")
                          << ") · 예약 " << t.ridpool_.reservedTo() << " ≥ 커서 " << t.ridpool_.cursor() << "\n";
                if (!okG) bad++;
            }

            s.ard = BAD_SOCK;              // 소멸자가 이 fd 를 건드리지 않게
            closesock(sv[0]); closesock(sv[1]);
        }
    }
#endif

    std::cout << (bad ? "자가검증 실패\n" : "자가검증 통과\n");
    return bad ? 1 : 0;
}

#endif  // SELFTEST_H
