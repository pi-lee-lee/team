// st_nodes.h — 자리 인수·노드 잠금·등록 D·Q 상한·다중 노드 ⑬~㉑
// 🔴 `selftest()` 의 몸통 조각. 단독 컴파일되지 않는다.
            // ═══════════════════════════════════════════════════════════════
            // 🔴 **단독 컴파일 불가.** `selftest()` **함수 몸통 조각**이고 그 자리에 include 된다.
            //   `s`(socketpair 가 꽂힌 Server) · `sv` · `bad` 를 바깥에서 물려받는다.
            // 🔑 옮긴 것이지 고친 것이 아니다 — **`.o` 바이트 동일**이어야 한다(REQ-0272).
            // ⚠ 시험을 새로 더할 때 **`bad++` 를 빠뜨리지 마라** — 빠지면 ✗ 를 찍고도 통과한다.
            // ═══════════════════════════════════════════════════════════════

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

