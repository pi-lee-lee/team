// st_downq.h — 하행 슬롯 큐 규율 ①~⑬ — 창·FIFO·상한·재전송·우선순위
// 🔴 `selftest()` 의 몸통 조각. 단독 컴파일되지 않는다.
            // ═══════════════════════════════════════════════════════════════
            // 🔴 **단독 컴파일 불가.** `selftest()` **함수 몸통 조각**이고 그 자리에 include 된다.
            //   `s`(socketpair 가 꽂힌 Server) · `sv` · `bad` 를 바깥에서 물려받는다.
            // 🔑 옮긴 것이지 고친 것이 아니다 — **`.o` 바이트 동일**이어야 한다(REQ-0272).
            // ⚠ 시험을 새로 더할 때 **`bad++` 를 빠뜨리지 마라** — 빠지면 ✗ 를 찍고도 통과한다.
            // ═══════════════════════════════════════════════════════════════

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

