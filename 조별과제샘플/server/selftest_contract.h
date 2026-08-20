// selftest_contract.h — 봉투 키 계약·known 사유·B* 예약 금지·rid 폭과 격리 ㉟~㉜
// 🔴 `selftest()` 의 몸통 조각. 단독 컴파일되지 않는다.
            // ═══════════════════════════════════════════════════════════════
            // 🔴 **단독 컴파일 불가.** `selftest()` **함수 몸통 조각**이고 그 자리에 include 된다.
            //   `s`(socketpair 가 꽂힌 Server) · `sv` · `bad` 를 바깥에서 물려받는다.
            // 🔑 옮긴 것이지 고친 것이 아니다 — **`.o` 바이트 동일**이어야 한다(REQ-0272).
            // ⚠ 시험을 새로 더할 때 **`bad++` 를 빠뜨리지 마라** — 빠지면 ✗ 를 찍고도 통과한다.
            // ═══════════════════════════════════════════════════════════════

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
                    // 🔴 `"completion":` 이 여기 있었다. **2026-08-20 에 계약에서 뺐다** —
                    //   게이트 조작을 없앤 뒤로 `gate_want` 를 쓰는 곳이 없어져
                    //   `settled`·`mismatch` 가 **도달 불가**가 됐다. 답은 모듈의 `confirmed` 다.
                    //   🔑 **이 시험이 빨강이 된 것이 회귀가 아니라 *계약이 바뀌었다* 는 신호였다.**
                    "\"actions\":", "\"modules\":[",
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

            // ㊶ 🔴🔴 **화면 직접 조작 계약** (REQ-0281 · 2026-08-20)
            //
            //   여기 있는 것 둘은 **주석이 지키던 것을 시험으로 옮긴 것**이다:
            //     ① `send_to_module` 의 *"화면이 시킨 것이 아니다 — 답할 곳이 없다"*
            //        → 그 주석을 고치면서 사실이 같이 사라질 뻔했다.
            //     ② *"요청이 2 이상이면 `settled` 를 낼 수 없다"*
            //
            //   🔴 ②를 **모의(mock_node)로는 못 잰다** — 모의가 에코를 안 하기 때문이다.
            //     그리고 **일부러 안 넣었다**: 실기에 없는 것을 흉내 내면 없는 것을 시험하게 된다.
            //     그래서 이 갈래는 **여기가 유일한 계측 지점**이다.
            //
            //   ⚠ 명세 초안은 이 규칙을 *"숫자 모듈은 settled 를 안 낸다"* 로 적었는데 **부정확했다** —
            //     조건은 **모듈이 아니라 값**이다. `number` 위젯에 1 을 보내면 비트가 그것을 증명한다.
            //     **구현하면서 찾았다. 명세를 고쳤다.**
            {
                ParkingLot L;
                L.spot("A1").parking().module("P1", "A1").module("P1", "LD").module("P1", "L2");
                L.control("P1", "LD").toggle();
                L.label("P1", "LD", "등");
                L.control("P1", "L2").number(0, 9999999);
                Server t; t.lot_ = &L; t.build_default_zones(); t.init_srv_id();
                t.park.devid = "P1"; t.park.reg_done = true;
                t.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                t.park.mods.push_back(std::make_pair(std::string("LD"), std::string("OG")));
                t.park.mods.push_back(std::make_pair(std::string("L2"), std::string("OL")));
                t.bind_modules(t.park);
                t.park.mod_bits_n = 3;
                for (int i = 0; i < 3; i++) t.park.mod_bits[i] = 0;

                // ── 선언이 `map` 에 나간다. **이름을 손으로 박는다**(㉟ 와 같은 이유)
                const std::string m = t.map_json();
                bool c1 = (m.find("\"control\":{\"widget\":\"toggle\"") != std::string::npos);
                std::cout << (c1 ? "  ✓ " : "  ✗ ") << "map: control.widget=toggle 이 나간다\n";
                if (!c1) bad++;
                // 🔴 **이름은 `control` 이 아니라 모듈의 `label` 이 나른다** (2026-08-20)
                //   ⚠ 부정형 하나로 끝내지 않는다 — *"control 에 label 이 없다"* 는
                //     **control 자체가 없어도 참**이다. 있어야 할 것을 같이 단언한다.
                bool c1b = (m.find("\"label\":\"등\"") != std::string::npos
                            && m.find("\"control\":{\"widget\":\"toggle\",\"label\"") == std::string::npos);
                std::cout << (c1b ? "  ✓ " : "  ✗ ")
                          << "🔴 이름은 모듈의 label 로 나가고 control 에는 없다\n";
                if (!c1b) bad++;
                bool c2 = (m.find("\"widget\":\"number\"") != std::string::npos
                           && m.find("\"min\":0,\"max\":9999999") != std::string::npos);
                std::cout << (c2 ? "  ✓ " : "  ✗ ") << "map: number 는 min·max 를 같이 낸다\n";
                if (!c2) bad++;
                // 선언 안 한 모듈(A1)에는 **키가 아예 없어야 한다**(존재/부재 규칙).
                // 🔑 `control` 총 개수로 센다 — "있나"가 아니라 **"몇 개인가"** 를 물어야
                //   선언 안 한 것에 붙는 사고가 잡힌다.
                {
                    int nctl = 0; size_t p = 0;
                    while ((p = m.find("\"control\":", p)) != std::string::npos) { nctl++; p += 9; }
                    bool c3 = (nctl == 2);
                    std::cout << (c3 ? "  ✓ " : "  ✗ ")
                              << "map: 선언한 2개에만 control 이 붙는다 (실제 " << nctl << ")\n";
                    if (!c3) bad++;
                }

                // ── 🔴 `confirmed` 판정 넷. **분모를 먼저 세운다** — LD=idx1 · L2=idx2
                struct C { const char* mod; long req; int bit; const char* want; };
                static const C cases[] = {   // 🔑 위젯이 판별자다(LD=toggle · L2=number)
                    { "LD", 1,       1, "settled"  },   // 요청 1 · 비트 1 → 증명된다
                    { "LD", 1,       0, "mismatch" },   // 요청 1 · 비트 0 → 어긋났다
                    { "L2", 1234567, 1, "partial"  },   // 🔴 비트는 "0이 아니다"만 말한다
                    // 🔴 **비트가 0 이어도 `partial` 이다.** 처음엔 `mismatch` 로 적었는데 **틀렸다** —
                    //   에코 비트는 장치의 **상태**이고 우리가 보낸 **인자**가 아니다.
                    //   실측: `DR 2`(닫기) → 비트 **0**. **성공인데 값이 다르다.**
                    //   `mismatch` 로 두면 **정확히 성공한 명령을 실패라고 부른다.**
                    { "L2", 1234567, 0, "partial"  },
                    // 🔴 값이 1 이어도 **`number` 위젯이면 `partial`** 이다 — 판별자는 **값이 아니라 위젯**이다.
                    //   `toggle` 선언만이 *"값이 곧 상태다"* 를 뜻한다.
                    { "L2", 1,       1, "partial"  },
                };
                for (int i = 0; i < 5; i++) {
                    const int mi = (std::string(cases[i].mod) == "LD") ? 1 : 2;
                    t.mod_req.clear();
                    t.mod_req[std::string("P1\t") + cases[i].mod] = cases[i].req;
                    t.park.mod_bits[mi] = cases[i].bit;
                    const std::string st = t.state_json();
                    // 그 모듈 조각만 잘라 본다 — 다른 모듈의 값에 걸리면 시험이 거짓말한다
                    const std::string key = std::string("\"name\":\"") + cases[i].mod + "\"";
                    size_t p = st.find(key);
                    // 🔴 **`}` 로 자르면 안 된다** — 이 조각 안에 `"cmd":{...}` 가 중첩돼 있어서
                    //   첫 `}` 가 그 안쪽이다. **`confirmed` 는 그보다 뒤에 있어 창 밖으로 나간다.**
                    //   ⚠ 실제로 그렇게 짰다가 넷이 다 ✗ 로 나왔다 — **코드가 아니라 시험이 틀렸다.**
                    //   §"`없다`를 잘린 창에서 결론 내지 마라" 의 시험 판본이다.
                    size_t e = (p == std::string::npos) ? p : st.find("\"name\":\"", p + key.size());
                    if (e == std::string::npos) e = st.size();
                    const std::string one = (p == std::string::npos) ? std::string("")
                                          : st.substr(p, e - p);
                    const std::string want = std::string("\"confirmed\":\"") + cases[i].want + "\"";
                    bool ok = (one.find(want) != std::string::npos);
                    std::cout << (ok ? "  ✓ " : "  ✗ ") << "confirmed: " << cases[i].mod
                              << " req=" << cases[i].req << " bit=" << cases[i].bit
                              << " → " << cases[i].want << "\n";
                    if (!ok) bad++;
                }
                // 🔴 **요청이 2 이상이면 `settled` 가 어떤 비트에서도 안 나온다** (루트 지시)
                //   ⚠ 위 네 갈래와 겹쳐 보이지만 **다른 것을 묻는다** — 저건 값 하나씩이고
                //     이건 **"이 조건에서 그 값이 절대 안 나온다"** 는 전칭이다.
                {
                    bool never = true;
                    for (int b = 0; b <= 1; b++) {
                        t.mod_req.clear(); t.mod_req["P1\tL2"] = 7654321; t.park.mod_bits[2] = b;
                        const std::string st = t.state_json();
                        size_t p = st.find("\"name\":\"L2\"");
                        size_t e = st.find("\"name\":\"", p + 10);
                        if (e == std::string::npos) e = st.size();
                        if (st.substr(p, e - p).find("\"confirmed\":\"settled\"") != std::string::npos)
                            never = false;
                    }
                    std::cout << (never ? "  ✓ " : "  ✗ ")
                              << "🔴 요청 2 이상은 settled 를 **절대** 안 낸다\n";
                    if (!never) bad++;
                }

                // ── 🔴 **화면이 안 시킨 명령은 화면에 아무것도 안 보낸다**
                //   `send_to_module` 주석이 지키던 것이 이것이다. 주석은 고쳐도 이 시험은 남는다.
                // 🔴 **전선이 없으면 `enqueue_down` 이 거절해 `pend` 가 빈다** — 처음에 그렇게 짜서
                //   둘 다 ✗ 였다. **코드가 아니라 시험이 실기 경로를 안 밟은 것**이다(§시험≠실기).
                //   그래서 소켓쌍을 꽂는다. `t.ard` 가 있어야 큐에 든다.
                sock_t ap[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, ap) == 0) { t.ard = ap[0]; t.ard_seen = true; }
                t.pend.clear();
                bool sent = t.send_to_module("P1", "LD", 1);
                bool quiet = sent && !t.pend.empty();
                for (std::map<uint16_t, Pending>::const_iterator it = t.pend.begin();
                     it != t.pend.end(); ++it)
                    if (it->second.web_cmd || it->second.ws_fd != BAD_SOCK) quiet = false;
                std::cout << (quiet ? "  ✓ " : "  ✗ ")
                          << "기여자 API 로 낸 명령은 web_cmd=false · ws_fd=BAD_SOCK (답할 곳이 없다)\n";
                if (!quiet) bad++;
                // 반대 갈래도 밟는다 — 🔑 **한쪽만 재면 "언제나 false" 여도 통과한다**
                t.pend.clear();
                t.send_to_module("P1", "LD", 1, 0, (sock_t)999, "w9");
                bool web = !t.pend.empty();
                for (std::map<uint16_t, Pending>::const_iterator it = t.pend.begin();
                     it != t.pend.end(); ++it)
                    if (!it->second.web_cmd || it->second.browser_rid != "w9") web = false;
                std::cout << (web ? "  ✓ " : "  ✗ ") << "화면이 낸 명령은 web_cmd=true · rid 를 든다\n";
                if (!web) bad++;
                t.pend.clear();
                if (t.ard != BAD_SOCK) { closesock(ap[0]); closesock(ap[1]); t.ard = BAD_SOCK; }
            }
