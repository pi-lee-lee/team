// selftest_wire.h — 자리 비트열·자가 치유·G 전선 왕복·재등록 대조 ㉘~㉛
// 🔴 `selftest()` 의 몸통 조각. 단독 컴파일되지 않는다.
            // ═══════════════════════════════════════════════════════════════
            // 🔴 **단독 컴파일 불가.** `selftest()` **함수 몸통 조각**이고 그 자리에 include 된다.
            //   `s`(socketpair 가 꽂힌 Server) · `sv` · `bad` 를 바깥에서 물려받는다.
            // 🔑 옮긴 것이지 고친 것이 아니다 — **`.o` 바이트 동일**이어야 한다(REQ-0272).
            // ⚠ 시험을 새로 더할 때 **`bad++` 를 빠뜨리지 마라** — 빠지면 ✗ 를 찍고도 통과한다.
            // ═══════════════════════════════════════════════════════════════

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

