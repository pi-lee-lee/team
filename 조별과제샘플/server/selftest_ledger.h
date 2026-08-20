// selftest_ledger.h — 노드 대장 (온보딩 2단계 · `docs/net/DESIGN-node-ledger.md`)
// 🔴 `selftest()` 의 몸통 조각. 단독 컴파일되지 않는다.
            // ═══════════════════════════════════════════════════════════════
            // 🔑 **이 단계는 `.o` 대조로 증명할 수 없다.** 순수 이동이 아니라 진짜 변경이다.
            //   그래서 판별자가 시험뿐이고, **시험의 분모를 여기 적어 둔다:**
            //
            //   ✅ 밟는다 : 지문·상태 전이 · 파일 왕복 · 잘린 줄 · 탭 오염 · 영속 꺼짐
            //   🔴 **못 밟는 것** :
            //     ① **실기 기동 경로가 `ledger_load()` 를 부르는가** — 시험은 `openPorts()` 를 안 탄다
            //        → ㊶ 이 그 대리 지표다(새 Server 의 대장이 비어 있는 것을 단언한다).
            //          **그래도 "기동 로그에 `노드 대장 —` 줄이 찍히는가"를 눈으로 봐야 한다.**
            //          2026-08-18 에 `build_default_zones()` 가 정확히 이 형태로 빠졌다
            //          (시험이 스스로 준비를 해 줘서 전부 통과하고 실기만 비었다)
            //     ② **HOME 이 없는 환경** — 시험 기계에는 항상 있다. 그 갈래는 안 돈다
            //     ③ **파일시스템 실패**(디스크 참·권한) — `save_fails_` 경로가 안 밟힌다
            // ═══════════════════════════════════════════════════════════════
            {
                std::cout << "\n[대장] 노드 대장 — 지문·상태·왕복\n";

                // ㊱ 지문은 **순서를 포함한다** — `idx` 가 순서로 정해지므로 순서가 곧 신원이다
                {
                    std::vector<NodeLedger::Mod> a, b, c;
                    a.push_back(NodeLedger::Mod("A1", "IP"));
                    a.push_back(NodeLedger::Mod("B1", "IP"));
                    b.push_back(NodeLedger::Mod("B1", "IP"));   // 순서만 뒤집었다
                    b.push_back(NodeLedger::Mod("A1", "IP"));
                    c.push_back(NodeLedger::Mod("A1", "IP"));
                    c.push_back(NodeLedger::Mod("B1", "OG"));   // kind 만 다르다
                    const std::string fa = NodeLedger::fingerprint(a);
                    const std::string fb = NodeLedger::fingerprint(b);
                    const std::string fc = NodeLedger::fingerprint(c);
                    bool ok = (fa != fb) && (fa != fc) && (fa.size() == 8)
                              && (fa == NodeLedger::fingerprint(a));   // 같은 입력 → 같은 값
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "지문이 순서·종류를 다 반영한다 ("
                              << fa << " / 순서뒤집기 " << fb << " / kind변경 " << fc << ")\n";
                    if (!ok) bad++;
                }

                // ㊲ 상태 전이 — 신규 → 그대로 → **구성 변경은 재확인**
                //    🔴 그리고 **재확인이 자동으로 안 풀린다**. 풀리면 없는 모듈을 가리키는 자리가 산다
                {
                    NodeLedger L;
                    L.load("", "srv");                        // 영속 없이 메모리로만
                    std::vector<NodeLedger::Mod> m1, m2;
                    m1.push_back(NodeLedger::Mod("A1", "IP"));
                    m1.push_back(NodeLedger::Mod("B1", "IP"));
                    m2 = m1; m2.pop_back();                   // 모듈 하나가 빠졌다

                    NodeLedger::State s1 = L.onRegister("P1", "10.0.0.5", m1, 1000);
                    NodeLedger::State s2 = L.onRegister("P1", "10.0.0.5", m1, 2000);
                    NodeLedger::State s3 = L.onRegister("P1", "10.0.0.9", m2, 3000);
                    NodeLedger::State s4 = L.onRegister("P1", "10.0.0.9", m2, 4000);  // 같은 구성 재접속

                    const NodeLedger::Entry* e = L.find("P1");
                    bool ok = (s1 == NodeLedger::NEW)
                           && (s2 == NodeLedger::UNASSIGNED)
                           && (s3 == NodeLedger::NEEDS_REVIEW)
                           && (s4 == NodeLedger::NEEDS_REVIEW)      // 🔴 자동으로 안 풀린다
                           && e && e->sessions == 4
                           && e->first_seen == 1000 && e->last_seen == 4000
                           && e->last_peer == "10.0.0.9"            // 마지막 상대를 들고 있다
                           && e->mods.size() == 1
                           && L.size() == 1;
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "상태 전이 "
                              << NodeLedger::stateName(s1) << "→" << NodeLedger::stateName(s2)
                              << "→" << NodeLedger::stateName(s3) << "→" << NodeLedger::stateName(s4)
                              << " · 접속 " << (e ? e->sessions : -1)
                              << " · 상대 " << (e ? e->last_peer : "?")
                              << " (기대 new→unassigned→needs_review→needs_review · 4 · 10.0.0.9)\n";
                    if (!ok) bad++;
                }

                // ㊳ 🔴 **`devid` 가 다르면 다른 노드다** — 지금 전부 `P1` 이라 이 갈래는
                //    실기에서 안 돌지만, EEPROM 고유화가 오면 그날부터 이것이 주 경로다.
                //    ⚠ **그날 처음 돌아 보는 코드를 남기지 않으려고 지금 밟는다.**
                {
                    NodeLedger L; L.load("", "srv");
                    std::vector<NodeLedger::Mod> m;
                    m.push_back(NodeLedger::Mod("A1", "IP"));
                    L.onRegister("P1", "10.0.0.5", m, 1000);
                    NodeLedger::State s = L.onRegister("P2", "10.0.0.6", m, 1001);
                    bool ok = (s == NodeLedger::NEW) && L.size() == 2
                              && L.find("P1") && L.find("P2")
                              && L.find("P1")->last_peer == "10.0.0.5";
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "devid 가 다르면 줄이 따로 선다 (노드 "
                              << L.size() << " · 기대 2)\n";
                    if (!ok) bad++;
                }

                // ㊴ 파일 왕복 — **저장한 것이 그대로 돌아오는가**
                //    🔑 순서(=idx)까지 보존돼야 한다. 순서가 밀리면 할당이 딴 것을 가리킨다
                {
                    const std::string tp = "/tmp/parking-nodes-selftest.txt";
                    remove(tp.c_str());
                    {
                        NodeLedger L; L.load(tp, "srvA");
                        std::vector<NodeLedger::Mod> m;
                        m.push_back(NodeLedger::Mod("A1", "IP"));
                        m.push_back(NodeLedger::Mod("B1", "OG"));
                        m.push_back(NodeLedger::Mod("X1", "OG"));
                        L.onRegister("P1", "10.0.0.5", m, 7777);
                        L.save("srvA");
                    }
                    NodeLedger R; R.load(tp, "srvB");
                    const NodeLedger::Entry* e = R.find("P1");
                    bool ok = R.persistOn() && e
                           && e->mods.size() == 3
                           && e->mods[0].name == "A1" && e->mods[0].kind == "IP"
                           && e->mods[1].name == "B1" && e->mods[1].kind == "OG"
                           && e->mods[2].name == "X1"                       // 🔑 순서 보존
                           && e->first_seen == 7777 && e->sessions == 1
                           && e->last_peer == "10.0.0.5"
                           && e->fp == NodeLedger::fingerprint(e->mods)     // 지문도 그대로
                           && R.linesBad() == 0 && R.linesUnknown() == 0;
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "파일 왕복 — 모듈 "
                              << (e ? e->mods.size() : 0) << "개 · 순서 "
                              << (e && e->mods.size() == 3 ? (e->mods[0].name + "," + e->mods[2].name)
                                                           : std::string("?"))
                              << " · 깨진줄 " << R.linesBad() << " (기대 3 · A1,X1 · 0)\n";
                    if (!ok) bad++;
                    remove(tp.c_str());
                }

                // ㊵ 🔴 **잘린 파일에서 앞 줄이 살아남는가** — 줄 단위 형식을 고른 이유가 이것이다
                //    ⚠ 그리고 **탭이 든 값이 칸을 밀지 않는가.** 밀면 조용히 다른 필드가 된다
                {
                    const std::string tp = "/tmp/parking-nodes-trunc.txt";
                    {
                        NodeLedger L; L.load(tp, "s");
                        std::vector<NodeLedger::Mod> m;
                        m.push_back(NodeLedger::Mod("A1", "IP"));
                        L.onRegister("P1", "10.0.0.5", m, 1000);
                        L.onRegister("P2", "10.0.0.6", m, 1001);
                        L.save("s");
                    }
                    // 마지막 줄을 잘라 낸다(전원이 나간 모양)
                    std::string all;
                    { std::ifstream f(tp.c_str(), std::ios::binary);
                      std::ostringstream o; o << f.rdbuf(); all = o.str(); }
                    if (all.size() > 12) all.erase(all.size() - 12);
                    { std::ofstream f(tp.c_str(), std::ios::binary | std::ios::trunc); f << all; }

                    NodeLedger R; R.load(tp, "s");
                    bool survived = (R.find("P1") != 0);      // 앞 줄은 살아남아야 한다
                    // 탭 오염 — 값에 탭이 있어도 칸이 안 밀린다
                    const std::string tp2 = "/tmp/parking-nodes-tab.txt";
                    remove(tp2.c_str());
                    {
                        NodeLedger L; L.load(tp2, "s");
                        std::vector<NodeLedger::Mod> m;
                        m.push_back(NodeLedger::Mod("A\t1", "I\nP"));
                        L.onRegister("P\t9", "10.0.0.5", m, 1000);
                        L.save("s");
                    }
                    NodeLedger T; T.load(tp2, "s");
                    const NodeLedger::Entry* te = T.find("P 9");   // 탭이 공백으로 바뀌었다
                    bool tabOk = te && te->mods.size() == 1
                              && te->mods[0].name == "A 1" && te->mods[0].kind == "I P"
                              && T.linesBad() == 0;
                    bool ok = survived && tabOk;
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "잘린 줄은 버리고 앞 줄은 산다("
                              << (survived ? "산다" : "🔴죽었다") << ") · 탭 오염이 칸을 안 민다("
                              << (tabOk ? "안 민다" : "🔴민다") << ")\n";
                    if (!ok) bad++;
                    remove(tp.c_str()); remove(tp2.c_str());
                }

                // ㊶ 🔴🔴 **새 Server 의 대장은 비어 있고 영속도 꺼져 있다**
                //    = **기동 경로가 `ledger_load()` 를 불러야 한다**는 뜻이다.
                //    ⚠ 이 시험은 "불렀는가"를 **직접 못 본다.** 대리 지표다 —
                //      2026-08-18 에 `build_default_zones()` 가 시험만 통과하고 실기에서 비었다.
                //    🔑 **그래서 기동 로그에 `노드 대장 —` 줄이 찍히는지 눈으로 확인해야 한다.**
                {
                    Server t;
                    bool ok = (t.ledger_.size() == 0) && (t.ledger_.persistOn() == false)
                              && (t.ledger_.dirty() == false);
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "새 Server 의 대장은 비어 있다 — "
                              << "**기동 경로가 ledger_load() 를 불러야 한다** (노드 "
                              << t.ledger_.size() << " · 영속 "
                              << (t.ledger_.persistOn() ? "🔴켜짐" : "꺼짐") << ")\n";
                    if (!ok) bad++;
                }

                // ㊸ 🔴🔴 **한 장치 안 이름 중복을 잡는가** (2026-08-19)
                //   web REQ-0179 §① 이 `(devid,name)` 을 전역 신원으로 쓴다. 중복이 있으면
                //   **그 키가 오늘 이미 애매하고, 화면이 둘 중 하나를 임의로 집는다.**
                //   ⚠ 증상이 "가끔 엉뚱한 모듈이 보인다"라서 **결함으로 안 보인다.**
                //   🔑 **검사에 이빨이 있는지 여기서 본다** — 중복을 일부러 만들어 발화를 확인한다.
                {
                    Server t; t.build_default_zones();
                    t.park.devid = "P1";
                    t.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                    t.park.mods.push_back(std::make_pair(std::string("A2"), std::string("IP")));
                    t.bind_modules(t.park);
                    long long clean = t.mod_dup_name;          // 중복 없음 → 0 이어야 한다

                    Server d; d.build_default_zones();
                    d.park.devid = "P1";
                    d.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                    d.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                    d.bind_modules(d.park);
                    long long dirty = d.mod_dup_name;          // 중복 → 1 이어야 한다

                    bool ok = (clean == 0) && (dirty == 1);
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "장치 안 이름 중복 — 깨끗 "
                              << clean << " · 중복 " << dirty << " (기대 0 · 1)\n";
                    if (!ok) bad++;
                }

                // ㊹ 🔴🔴 **조립 표가 틀렸을 때 말하는가** — 사용 코드가 기여자의 면이다
                //   🔑 **일부러 틀린 표를 만들어 발화를 확인한다.** 안 하면 "검사가 있다"만 참이다
                {
                    // ① 같은 센서 이름이 두 자리에 · ② 센서 없는 자리
                    ParkingLot badlot;   // ⚠ `bad` 는 바깥의 실패 계수기다. 가리면 안 된다
                    badlot.spot("A1").parking().module("S1");
                    badlot.spot("A2").parking().module("S1");      // 🔴 같은 이름 — A1 이 이기고 A2 는 못 받는다
                    badlot.spot("A3").parking();         // 🔴 센서가 없는 **주차** 자리
                    Server t; t.lot_ = &badlot;
                    t.build_default_zones();
                    long long dirty = t.asm_warn_;

                    ParkingLot goodlot;
                    goodlot.spot("A1").parking().module("S1");
                    goodlot.spot("A2").parking().module("S2");
                    goodlot.spot("E1");                  // 🔑 일반영역(기본값) — 센서가 없어도 정상이다
                    Server u; u.lot_ = &goodlot;
                    u.build_default_zones();
                    long long clean = u.asm_warn_;

                    // 🔴 **v2 에서 기대값이 1 로 줄었다. 회귀가 아니다.**
                    //   `module()` 하나로 합치면서 **선언만 보고는 센서인지 알 수 없게 됐다** →
                    //   *"센서 없는 주차 자리"* 검사가 **등록(`bind_modules`) 시점으로 옮겨갔다.**
                    //   조립 시점에 남은 것은 **이름 중복** 하나다.
                    bool ok = (dirty == 1) && (clean == 0);
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "조립 표 검사(선언 시점) — 틀린 표 "
                              << dirty << "건 · 바른 표 " << clean
                              << "건 (기대 1 · 0 — 이름 중복만 선언으로 안다)\n";
                    if (!ok) bad++;

                    // 🔴 **옮겨간 검사를 그 자리에서 다시 잰다. 시험을 지우지 않는다.**
                    //   등록이 와야 `kind` 첫 글자로 센서를 가른다 — **알 수 있는 가장 이른 시점**이다.
                    {
                        long long before = t.asm_warn_;
                        t.park.devid = "P1";
                        t.park.mods.push_back(std::make_pair(std::string("S1"), std::string("IP")));
                        t.bind_modules(t.park);
                        // A3 는 주차 자리인데 이 노드의 모듈이 하나도 안 붙는다 →
                        // 🔑 **경고가 안 나야 한다**(다른 노드가 센서를 댈 수 있다).
                        long long afterA = t.asm_warn_;
                        // 이제 A3 에 **명령 모듈만** 붙여 본다 → 센서가 0 이므로 **말해야 한다**
                        badlot.spot("A3").module("P1", "LD");
                        t.build_default_zones();
                        t.park.mods.push_back(std::make_pair(std::string("LD"), std::string("OG")));
                        long long mid = t.asm_warn_;
                        t.bind_modules(t.park);
                        bool ok2 = (afterA == before) && (t.asm_warn_ > mid);
                        std::cout << (ok2 ? "  ✓ " : "  ✗ ")
                                  << "등록 시점 검사 — 남의 노드면 조용(" << (afterA - before)
                                  << ") · 명령 모듈만 붙으면 말한다(" << (t.asm_warn_ - mid) << ")\n";
                        if (!ok2) bad++;
                    }
                }

                // ㊷ 🔴 **자가검증은 대장 파일을 안 건드린다** — `no_disk` 가 그 약속이다
                //    ⚠ 안 지키면 자가검증을 한 번 돌릴 때마다 **운영 대장이 덮인다.**
                //      §"시험이 실기 자료를 오염시킨다"의 자리다
                {
                    Server t; t.no_disk = true;
                    t.ledger_load();
                    bool ok = (t.ledger_.persistOn() == false) && t.ledger_.path().empty();
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "no_disk 면 대장이 파일을 안 연다 (경로 "
                              << (t.ledger_.path().empty() ? "없음" : "🔴" + t.ledger_.path()) << ")\n";
                    if (!ok) bad++;
                }
            }

