// selftest_spot.h — 자리 동작 방식(`spot.h`) 계약
// 🔴 `selftest()` 의 몸통 조각. 단독 컴파일되지 않는다.
            // ═══════════════════════════════════════════════════════════════
            // 🔑 **이 시험의 분모**:
            //   ✅ 밟는다 : 기본 OR · 갈린 센서 · 전부 모름 · 모르는 값 무시 · 재정의가 듣는가
            //   🔴 **못 밟는 것** : *"화면 봉투가 안 바뀌었나"* — 그건 여기서 못 본다.
            //     **골든 봉투 대조**(`/tmp/golden.sh` 형태)로 따로 재야 하고, 1단계에서 그렇게 했다.
            //     ⚠ `.o` 대조는 **쓸 수 없다** — 가상 함수가 생기면 산출물이 반드시 바뀐다.
            // ═══════════════════════════════════════════════════════════════
            {
                std::cout << "\n[자리] 동작 방식 — 기본 OR 계약\n";
                SpotBehavior sb;

                // ㊺ 기본 OR — 아는 센서 중 하나라도 참이면 찼다
                {
                    std::vector<SensorReading> none;
                    std::vector<SensorReading> one, split, allNo, unknownOnly;
                    one.push_back(SensorReading(true, true));
                    split.push_back(SensorReading(true, true));
                    split.push_back(SensorReading(true, false));   // 🔑 갈림 — OR 는 참
                    allNo.push_back(SensorReading(true, false));
                    allNo.push_back(SensorReading(true, false));
                    unknownOnly.push_back(SensorReading(false, false));
                    unknownOnly.push_back(SensorReading(false, false));

                    bool ok = (sb.occupied(none)        == false)
                           && (sb.occupied(one)         == true)
                           && (sb.occupied(split)       == true)     // 🔴 이중화의 요점
                           && (sb.occupied(allNo)       == false)
                           && (sb.occupied(unknownOnly) == false);   // ⚠ "비었다"가 아니라 "모른다"
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "기본 OR — 빈입력 F · 하나참 T · "
                              << "**갈림 T** · 둘다거짓 F · 전부모름 F\n";
                    if (!ok) bad++;
                }

                // ㊻ 🔴 **`known=false` 인 센서의 `value` 는 안 본다**
                //    받은 적 없는 값이 참으로 새어 들어오면 **없는 차를 있다고 말한다.**
                //    ⚠ `SensorReading(false, true)` 는 원래 만들어질 수 없는 조합이지만,
                //      **방어가 아니라 계약이다** — 다음 사람이 이 구조체를 손으로 채울 수 있다.
                {
                    std::vector<SensorReading> bogus;
                    bogus.push_back(SensorReading(false, true));   // 모르는데 값이 참
                    bool ok = (sb.occupied(bogus) == false);
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "모르는 센서의 값은 안 본다 (known=false,"
                              << "value=true → " << (sb.occupied(bogus) ? "🔴참" : "거짓") << ")\n";
                    if (!ok) bad++;
                }

                // ㊽ 🔴🔴 **2인자 선언이 실제로 되는가** — 이름이 자리 id 와 달라도 붙는가
                //
                //   > **`devid` 를 받는 순간 "모듈 이름이 자리 id 와 같아야 한다"는 암묵 규칙이 사라진다.**
                //   `Modules.h` 주석이 경고하던 함정이 여기서 구조적으로 없어지는 것을 **값으로** 본다.
                //   ⚠ **쓰이지 않는 일반화는 검증되지 않은 코드다.** 그래서 더하자마자 밟는다.
                {
                    ParkingLot lt;
                    lt.spot("A1").sensor("P1", "왼쪽센서");   // 🔑 이름이 자리 id 와 **다르다**
                    Server t; t.lot_ = &lt;
                    t.build_default_zones();
                    t.park.devid = "P1";
                    t.park.mods.push_back(std::make_pair(std::string("왼쪽센서"), std::string("IP")));
                    t.bind_modules(t.park);
                    const Zone* z = t.lot.find("A1");
                    bool bound = z && z->modules.size() == 1
                                 && z->modules[0].first == "P1"
                                 && z->modules[0].second == "왼쪽센서";
                    bool quiet = (t.mod_unbound == 0);

                    // 음성 대조 — **다른 장치의 같은 이름은 안 붙어야 한다**
                    //   ⚠ 이게 없으면 "그냥 이름만 보고 붙은 것"과 구분이 안 된다
                    ParkingLot lt2;
                    lt2.spot("A1").sensor("P9", "왼쪽센서");   // 🔴 P9 를 지정했는데 P1 이 온다
                    Server u; u.lot_ = &lt2;
                    u.build_default_zones();
                    u.park.devid = "P1";
                    u.park.mods.push_back(std::make_pair(std::string("왼쪽센서"), std::string("IP")));
                    u.bind_modules(u.park);
                    const Zone* z2 = u.lot.find("A1");
                    bool notBound = z2 && z2->modules.empty() && u.mod_unbound == 1;

                    bool ok = bound && quiet && notBound;
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "2인자 선언 — 이름이 자리 id 와 달라도 붙는다("
                              << (bound ? "붙는다" : "🔴안 붙는다") << ", 미결속 " << t.mod_unbound
                              << ") · **다른 devid 는 안 붙는다**("
                              << (notBound ? "안 붙는다" : "🔴붙는다") << ")\n";
                    if (!ok) bad++;
                }

                // ㊿ 🔴🔴 **발행 API 가 전선 바이트까지 가는가** — 코드 판독이 아니라 바이트로
                //   ⚠ `socketpair` 로 실제 `send()` 를 태워 **나간 줄을 읽는다.**
                //   🔑 7자리가 그대로 실리는지가 이 시험의 요점이다 — 전에는 0/1 밖에 못 보냈다.
                {
                    int sv[2];
                    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
                        std::cout << "  ! socketpair 실패 — 건너뛴다\n";
                    } else {
                        Server t; t.no_disk = true;
                        t.ard = sv[0]; t.park_dev = "P1";
                        t.ard_seen = true; t.ard_last_ms = now_ms();
                        t.park.devid = "P1";
                        t.park.mods.push_back(std::make_pair(std::string("LCD1"), std::string("OG")));
                        t.park.reg_done = true;

                        bool okKnown   = t.send_to_module("P1", "LCD1", 1234567L);
                        bool okNoNode  = t.send_to_module("P9", "LCD1", 1L);      // 없는 노드
                        bool okNoMod   = t.send_to_module("P1", "없음", 1L);      // 없는 모듈
                        // 🔑 하행은 **슬롯 창이 열려야** 나간다. 시험은 창을 직접 연다.
                        //   ⚠ 처음엔 `tick()` 을 불렀는데 그건 장치의 `S` 를 기다리므로
                        //     이 시험에서는 아무것도 안 나갔다 — **빈 전선을 보고 "안 실렸다"가 나왔다.**
                        //     🔑 발행이 틀린 게 아니라 **시험이 조건을 안 만든 것**이었다.
                        t.flush_downq("selftest 발행", false);

                        char rb[512]; std::string got;
                        for (int i = 0; i < 4; i++) {
                            int n = (int)recv(sv[1], rb, sizeof(rb), MSG_DONTWAIT);
                            if (n <= 0) break;
                            got.append(rb, rb + n);
                        }
                        const bool wire7 = got.find("G,") != std::string::npos
                                        && got.find(",1234567,") != std::string::npos;
                        bool ok = okKnown && !okNoNode && !okNoMod && wire7;
                        std::cout << (ok ? "  ✓ " : "  ✗ ") << "발행 API — 7자리가 전선에 그대로("
                                  << (wire7 ? "실렸다" : "🔴안 실렸다")
                                  << ") · 없는 노드/모듈은 거절("
                                  << (!okNoNode && !okNoMod ? "거절" : "🔴통과") << ")\n";
                        if (!ok) bad++;
                        t.ard = BAD_SOCK;
                        closesock(sv[0]); closesock(sv[1]);
                    }
                }

                // ㊾ 🔴 **예시 `devid` 를 잡는가** — 그리고 **다른 값에는 안 뜨는가**
                //   ⚠ 음성 대조가 없으면 **"늘 뜨는 줄"** 이 되고, 늘 뜨는 경고는 아무 말도 안 하는 것과 같다.
                {
                    Server t; t.build_default_zones();
                    t.warn_example_devid("P1");
                    long long hit = t.devid_example_;

                    Server u; u.build_default_zones();
                    u.warn_example_devid("KIM7");      // 기여자가 자기 것으로 바꾼 경우
                    u.warn_example_devid("");          // 미승격(빈 값)도 안 걸려야 한다
                    u.warn_example_devid("P10");       // 🔑 **접두가 같아도 다른 값**이다
                    long long miss = u.devid_example_;

                    bool ok = (hit == 1) && (miss == 0);
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "예시 devid — `P1` " << hit
                              << "건 · `KIM7`/빈값/`P10` " << miss << "건 (기대 1 · 0)\n";
                    if (!ok) bad++;
                }

                // ㊼ 🔴 **재정의가 실제로 듣는가** — 이게 이 구조의 전부다.
                //    기여자가 상속해서 구현한 것이 안 불리면 **기본이 조용히 계속 쓰인다.**
                //    ⚠ 그 고장은 "내가 쓴 코드가 아무 일도 안 한다"이고, 오늘 우리가 없앤 부류다.
                {
                    struct AlwaysBusy : SpotBehavior {
                        virtual bool occupied(const std::vector<SensorReading>&) const { return true; }
                    };
                    AlwaysBusy ab;
                    const SpotBehavior& asBase = ab;      // 🔑 **기반 참조로** 불러야 뜻이 있다
                    std::vector<SensorReading> allNo;
                    allNo.push_back(SensorReading(true, false));
                    bool ok = (asBase.occupied(allNo) == true) && (sb.occupied(allNo) == false);
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "재정의가 듣는다 (같은 입력에 재정의 "
                              << (asBase.occupied(allNo) ? "T" : "F") << " · 기본 "
                              << (sb.occupied(allNo) ? "T" : "F") << " · 기대 T · F)\n";
                    if (!ok) bad++;
                }
            }

