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

