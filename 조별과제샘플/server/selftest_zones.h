// selftest_zones.h — 지형과 판·map/state 봉투·get_map 상한·조작 라우팅 ㉒~㉖
// 🔴 `selftest()` 의 몸통 조각. 단독 컴파일되지 않는다.
            // ═══════════════════════════════════════════════════════════════
            // 🔴 **단독 컴파일 불가.** `selftest()` **함수 몸통 조각**이고 그 자리에 include 된다.
            //   `s`(socketpair 가 꽂힌 Server) · `sv` · `bad` 를 바깥에서 물려받는다.
            // 🔑 옮긴 것이지 고친 것이 아니다 — **`.o` 바이트 동일**이어야 한다(REQ-0272).
            // ⚠ 시험을 새로 더할 때 **`bad++` 를 빠뜨리지 마라** — 빠지면 ✗ 를 찍고도 통과한다.
            // ═══════════════════════════════════════════════════════════════

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

