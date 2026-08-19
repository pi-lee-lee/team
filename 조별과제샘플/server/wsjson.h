// wsjson.h — 봉투 만들기 — map/state/snapshot JSON. `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- JSON 만들기
    static std::string jstr(const std::string& s) {
        std::string o = "\"";
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b,sizeof(b),"\\u%04x",c); o += b; }
            else o += c;
        }
        return o + "\"";
    }
    // ── REQ-0203 4b: `map` 봉투 (설계 §6.8) ─────────────────────────────────────
    // 🔴 **`slots[]`(옛 `snapshot`)를 대체하지 않는다. 더한다.**
    //   화면이 `map` 을 받은 적 없으면 옛 경로로 그리고, 받으면 격자로 바꾼다(web 의 이중 경로).
    //   **그래야 이 배포가 화면을 안 깨뜨리고, 화면 배포도 서버를 안 기다린다.**
    // ⚠ **한 번에 완결해서 보낸다. 조각내지 않는다** — 화면이 "모른다"로 그리는 창을 짧게 하려는 것이다.
    std::string map_json() {
        std::ostringstream o;
        o << "{\"type\":\"map\",\"srv_id\":" << jstr(srv_id) << ",\"epoch\":" << lot.epoch()
          << ",\"grid\":{\"rows\":" << grid_rows << ",\"cols\":" << grid_cols << "}"
          << ",\"zones\":[";
        for (size_t i = 0; i < lot.zones().size(); i++) {
            const Zone& z = lot.zones()[i];
            if (i) o << ",";
            o << "{\"id\":" << jstr(z.id) << ",\"kind\":" << jstr(z.kind) << ",\"cells\":[";
            for (size_t c = 0; c < z.cells.size(); c++) {
                if (c) o << ",";
                o << "[" << z.cells[c].first << "," << z.cells[c].second << "]";
            }
            o << "],\"modules\":[";
            for (size_t m = 0; m < z.modules.size(); m++) {
                if (m) o << ",";
                const std::string& dv = z.modules[m].first;
                const std::string& nm = z.modules[m].second;
                // `kind` 와 `idx` 는 **그 모듈을 등록한 노드**가 갖고 있다 — 이름만으로 찾으면 안 된다(복합 키).
                std::string kind; int idx = -1;
                std::vector<Node*> ns = all_nodes();
                for (size_t k = 0; k < ns.size(); k++) {
                    if (ns[k]->devid != dv) continue;
                    for (size_t j = 0; j < ns[k]->mods.size(); j++)
                        if (ns[k]->mods[j].first == nm) { kind = ns[k]->mods[j].second; idx = (int)j; }
                }
                o << "{\"devid\":" << jstr(dv) << ",\"name\":" << jstr(nm)
                  << ",\"kind\":" << jstr(kind) << ",\"idx\":" << idx << "}";
            }
            o << "]}";
        }
        o << "]}";
        return o.str();
    }
    // 🔴 **빈 지형을 `map` 으로 내보내지 않는다** (2026-08-19).
    //   첫 배포에서 `build_default_zones()` 가 기동 경로에 없어 `lot.zones()` 가 빈 채 나갔다.
    //   그때 화면은 **"자리가 0개인 주차장"** 을 정상 상태로 그렸다 —
    //   🔑 **빈 지형은 답이 아니라 고장이다. 답인 척하면 아무도 안 본다.**
    //   ⚠ 그래서 **안 보내고 시끄럽게 남긴다.** 화면은 지형이 없으면 옛 것을 유지하거나
    //     "아직 못 받았다"로 남는데, **둘 다 "0개"라고 그리는 것보다 낫다.**
    // 🔑 **선언 자리에서 초기화한다** — 생성자 목록에 넣으면 선언 순서에 묶여
    //   (`-Wreorder`) 다음 사람이 자리를 옮길 때 조용히 경고가 난다.
    // 🔑 **선언 자리에서 초기화한다**(C++11 NSDMI). 생성자 목록에 넣으면 선언 순서에
    //   묶여(`-Wreorder`) 다음 사람이 이 멤버를 옮길 때마다 경고가 난다.
    bool map_empty_warned = false;
    // 🔑 `mutable` 을 쓰지 않는다 — **요약을 만드는 함수가 상태를 바꾸면 안 된다.**
    //   갱신은 `ws_upgrade` 에서 하고 여기서는 읽기만 한다.
    int  ws_peak = 0;      // 이 인스턴스에서 동시에 붙었던 **WS 소켓 수**의 최대
                           //   ⚠ 사람이 보는 화면 수가 아니다 — 탐침·하니스·주입기도 센다
    void push_map() {
        if (lot.zones().empty()) {
            if (!map_empty_warned) {
                map_empty_warned = true;
                logf("!", "🔴 지형이 비어 있다 — `map` 을 보내지 않는다. "
                          "**기동 경로가 build_default_zones() 를 안 불렀을 수 있다**");
            }
            return;
        }
        ws_broadcast(map_json());
    }

    // ── REQ-0203 4c: `state` 봉투 + `actions` (설계 §6.5·§6.8·§6.9) ──────────────
    // 🔴 **`actions` 는 서버가 계산해 *값으로* 준다.** 화면이 "모듈 목록 + 노드 생사"로 조합하지 않는다 —
    //   조합 규칙이 화면에도 생기면 두 답이 갈리고 **관대한 쪽이 이기면 죽은 노드에 명령이 나간다.**
    // 🔑 **키의 존재/부재가 한 비트를 나른다**: 없으면 "그 조작은 지금 이 자리에 없다"(버튼을 안 그린다),
    //   있는데 `ok:false` 면 "뜻은 있는데 막혔다". **뜻 없는 것을 `ok:false` 로 보내면 화면이
    //   영영 안 풀리는 막힌 버튼을 그린다.**
    Node* node_of(const std::string& devid) {
        std::vector<Node*> ns = all_nodes();
        for (size_t i = 0; i < ns.size(); i++) if (ns[i]->devid == devid) return ns[i];
        return NULL;
    }
    // 자리 하나의 막힌 이유. 빈 문자열 = 안 막혔다. **코드 다섯 중에서만 고른다**(§6.5).
    std::string zone_block_reason(const Zone& z) {
        // 🔴 **2026-08-19 — "모듈이 없다"와 "아직 등록을 안 받았다"는 다른 상태다.**
        //   모듈은 등록(`D`)이 끝나야 자리에 붙는다(`bind_modules`). 그래서 **등록 전에는
        //   모든 자리가 `module_absent` 로 보인다** — 화면은 그것을 *"이 자리엔 장비가 없다"*(영구)
        //   로 읽는데 실제로는 **곧 붙는다**(일시). 🔑 **영구와 일시를 같은 코드로 답하면
        //   화면이 영영 안 그리는 자리를 만든다.**
        //   ⚠ `reg_giveup` 은 "형성 중"이 아니다 — 포기한 뒤에는 `module_absent` 가 정직하다.
        if (z.modules.empty()) {
            std::vector<Node*> ns = all_nodes();
            bool any_unreg = false;
            for (size_t i = 0; i < ns.size(); i++)
                if (!ns[i]->reg_done && !ns[i]->reg_giveup) any_unreg = true;
            if (ns.empty())  return "node_offline";        // 노드가 아예 없다
            if (any_unreg)   return "node_unregistered";   // 형성 중이다
            return "module_absent";                        // 등록이 끝났는데 이 자리엔 안 붙었다
        }
        for (size_t i = 0; i < z.modules.size(); i++) {
            Node* n = node_of(z.modules[i].first);
            if (!n) return "module_absent";
            // ⚠ **주차 노드만 생사 판정이 있다**(`device_online`). 보조 노드는 `online` 필드를 쓴다.
            bool up = (n == &park) ? device_online() : n->online;
            if (!up) return "node_offline";
            if (!n->reg_done) return "node_unregistered";
        }
        // 같은 자리에 이미 전선/큐에 나간 명령이 있으면 사용자 조작을 겹치지 않는다
        for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.slot == z.id) return "pending";
        return "";
    }
    void emit_action(std::ostringstream& o, bool& first,
                     const char* name, const std::string& reason) {
        if (!first) o << ",";
        first = false;
        o << jstr(name) << ":{\"ok\":" << (reason.empty() ? "true" : "false")
          << ",\"reason\":" << (reason.empty() ? std::string("null") : jstr(reason)) << "}";
    }
    std::string state_json() {
        std::ostringstream o;
        int split_now = 0;                     // 이번 판의 갈린 자리 수 — 끝에서 계기에 옮긴다
        o << "{\"type\":\"state\",\"srv_id\":" << jstr(srv_id)
          << ",\"epoch\":" << lot.epoch() << ",\"ts_ms\":" << epoch_ms() << ",\"zones\":[";
        for (size_t i = 0; i < lot.zones().size(); i++) {
            const Zone& z = lot.zones()[i];
            if (i) o << ",";
            const std::string blk = zone_block_reason(z);
            int si = slot_index(z.id);                 // 예약 상태는 여전히 slots[] 가 원본이다
            o << "{\"id\":" << jstr(z.id);
            // ── 🔴 `occupied` 를 **그 자리의 센서들에서 유도한다** (명세 §9.2)
            //   한 자리에 센서가 둘이므로 두 값에서 하나를 만들어야 한다. **서버가 계산한다** —
            //   화면이 조합하게 두면 규칙이 두 곳에 생긴다(§"파생 값은 원본을 가진 쪽이 계산한다").
            // 🔴 **서버는 값을 모으기만 하고, 무엇으로 읽을지는 자리가 정한다** (`spot.h` · 1단계)
            //   전에는 이 자리에서 바로 `occ = (v_ones > 0)` 을 계산했다.
            //   이제 **판단은 `SpotBehavior::occupied()`** 가 하고 여기서는 **입력만 만든다.**
            //   🔑 그래야 기여자가 자기 자리의 판정을 갈아끼울 수 있다(2단계).
            //   ⚠ 지금은 모든 자리가 기본 구현(=지금과 같은 OR)을 쓰므로 **거동 변화가 0 이다.**
            std::vector<SensorReading> readings;
            int v_known = 0, v_total = 0, v_ones = 0;
            for (size_t m = 0; m < z.modules.size(); m++) {
                const Node* mn = node_by_devid(z.modules[m].first);
                int mi = -1;
                if (mn)
                    for (size_t k = 0; k < mn->mods.size(); k++)
                        if (mn->mods[k].first == z.modules[m].second) { mi = (int)k; break; }
                if (mi < 0) continue;
                // 🔑 **점유 센서만 센다.** 차단봉(OB)은 자리 점유를 말하지 않는다(명세 §8.1).
                if (!mn || mn->mods[mi].second != "IP") continue;
                v_total++;
                const bool known = (mi < mn->mod_bits_n);
                const bool val   = known && mn->mod_bits[mi];
                readings.push_back(SensorReading(known, val));
                if (known) { v_known++; if (val) v_ones++; }
            }
            if (z.kind == "parking") {
                // 🔴 **OR 다.** 두 오류의 대가가 대칭이 아니다 —
                //   빈 자리를 "찼다"고 하면 손해는 자리 하나이고,
                //   찬 자리를 "비었다"고 하면 **운전자가 가서 못 댄다.**
                //   ⚠ AND 로 하면 센서 하나가 죽었을 때 그 자리가 영영 "비었다"로 보인다.
                // 🔴 **여기가 기여자가 갈아끼우는 자리다.** 지금은 자리 전부가 기본 구현을 쓴다.
                //   ⚠ 반환값 `false` 는 *"비었다"* 가 아닐 수 있다 — 전부 모르는 경우도 `false` 다.
                //     그 구분은 아래 `value_state` 가 나른다. **2단계에서 `Tri` 로 합친다.**
                const bool occ = default_spot_.occupied(readings);
                const char* vs = (v_total == 0 || v_known == 0) ? "unknown"
                               : (v_known == v_total ? "known" : "partial");
                // 🔴 `value_state` 는 **모든 자리에 항상 싣는다.** 선택적이면 화면이 안 본다.
                //   ⚠ `unknown` 일 때 `occupied:false` 는 **"비었다"가 아니라 "모른다"** 이다.
                o << ",\"occupied\":" << (occ ? "true" : "false")
                  << ",\"value_state\":" << jstr(vs)
                  << ",\"value_known\":" << v_known << ",\"value_total\":" << v_total;
                if (si >= 0) o << ",\"reserved\":" << (slots[si].reserved ? "true" : "false");
                // 🔴 **둘 다 아는데 값이 갈리면 센다.** 이중화의 목적이 고장 감지인데
                //   세지 않으면 이중화가 아무 일도 안 한다(명세 §9.2).
                if (v_known >= 2 && v_ones > 0 && v_ones < v_known) split_now++;
            } else if (si >= 0) {
                o << ",\"occupied\":" << (slots[si].occupied ? "true" : "false")
                  << ",\"reserved\":" << (slots[si].reserved ? "true" : "false");
            }
            o << ",\"actions\":{";
            bool first = true;
            if (z.kind == "parking" && si >= 0) {
                // 🔑 **뜻이 있는 것만 넣는다.** 예약이 없는 자리의 `cancel` 은 **키 자체를 안 보낸다**
                if (!slots[si].reserved) {
                    // 🔴 **차가 있는 자리는 예약할 수 없다** (REQ-0235 · web 이 찾음 · 2026-08-19).
                    //   장치가 그렇게 판정한다 — `client.ino`: `if (occMask & bit) result = 1;`
                    //   그리고 설계 의도가 그 위 주석에 있다: **`occupied=1 ∧ reserved=1` 은
                    //   "예약하고 나서 주차한" 성공 상태**다. **반대 순서는 없다.**
                    //
                    //   ⚠ **여는 근거와 막는 근거가 다르면 반드시 창이 생긴다.**
                    //     여기서 `ok:true` 를 주면 화면이 버튼을 그리고, 누르면 장치가 `result=1` 로
                    //     거절한다 — **사용자에게는 "눌렀는데 안 되는 버튼"이다.**
                    //   🔑 `actions` 가 있는 이유가 **실패할 것을 안 내놓는 것**이다.
                    //
                    //   ⚠ **키를 빼지 않고 `ok:false` 로 준다**: 차가 나가면 예약이 가능해지므로
                    //     *"뜻이 없다"* 가 아니라 *"뜻은 있는데 막혔다"* 다(§6.5 의 존재/부재 규칙).
                    const std::string rr = (blk.empty() && slots[si].occupied) ? "occupied" : blk;
                    emit_action(o, first, "reserve", rr);
                } else {
                    emit_action(o, first, "cancel",  blk);
                }
            } else if (z.kind == "entrance" || z.kind == "exit") {
                emit_action(o, first, "open_gate",  blk);
                emit_action(o, first, "close_gate", blk);
            }
            o << "}";
            // §3.5 완료 판정 — 🔴 **ACK 이 아니라 `S` 의 에코 비트로 판정한다.**
            //   ACK 은 *"명령이 도착해 적용됐다"* 까지다. **실제로 열렸는지는 장치가 매 슬롯 에코한다.**
            //   ⚠ ACK 으로 판정하면 장치가 받고 **못 움직였을 때** 화면이 "열렸다"로 그린다 — `거짓 완료`.
            //   🔑 에코가 성립하는 조건: **비교할 값이 매 슬롯 온다**(원장 §8.21 이 실측한 그 조건).
            //   값 셋만 쓴다: `pending`(내가 건 명령이 아직 있다) · `settled`(에코가 있다) · `unknown`(모른다).
            {
                bool any_pending = false;
                for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it)
                    if (it->second.kind == 'G' && it->second.slot == z.id) any_pending = true;
                const int gi = gate_index_of(z);
                // **닫힌 집합 넷**: `pending` · `settled` · `mismatch` · `unknown`
                //   pending  — 내가 건 명령이 아직 떠 있다
                //   settled  — 마지막으로 **요청한 값과 에코가 같다**
                //   🔴 mismatch — 요청과 에코가 **다르다.** 장치가 못 했거나 되돌아갔다.
                //                **이 값이 없으면 거짓 완료가 `settled` 로 보인다.**
                //   unknown  — 이 세션에 명령한 적이 없거나 비트를 못 읽었다
                const char* comp = "unknown";
                std::map<int,int>::const_iterator wi = gate_want.find(gi);
                if (any_pending)                                  comp = "pending";
                else if (gi >= 0 && gi < park.mod_bits_n && wi != gate_want.end())
                    comp = (park.mod_bits[gi] == wi->second) ? "settled" : "mismatch";
                o << ",\"completion\":\"" << comp << "\",\"modules\":[";
            }
            for (size_t m = 0; m < z.modules.size(); m++) {
                if (m) o << ",";
                // 🔴 **이제 값이 있다** — `S` 의 비트열에서 뽑는다(비트 `idx`).
                //   ⚠ **모르면 여전히 `known:false` 다.** 등록 전이거나 폭을 못 읽었으면 `mod_bits_n == 0` 이고,
                //     그때 `value:false` 를 내면 **화면이 "닫혀 있다"를 사실로 그린다.** 모름과 거짓은 다르다.
                // 🔴 ②-d — **그 모듈의 노드**에서 색인과 비트를 읽는다(주 노드 고정을 걷어낸다).
                const Node* mn = node_by_devid(z.modules[m].first);
                int mi = -1;
                if (mn)
                    for (size_t k = 0; k < mn->mods.size(); k++)
                        if (mn->mods[k].first == z.modules[m].second) { mi = (int)k; break; }
                const bool known = (mn && mi >= 0 && mi < mn->mod_bits_n);
                // 🔴 **`known:false` 에는 사유를 붙인다** (명세 §8.10 · 2026-08-19)
                //   전에는 원인 셋이 **같은 모양**으로 나왔다 — 원인이 다른데 표시가 같으면 아무도 못 고친다.
                //   ⚠ 어휘는 §6.5 의 기존 코드를 먼저 쓰고 없는 것만 새로 만들었다.
                //   ⚠ `reason` 은 **`known:false` 일 때만** 싣는다(존재/부재 규칙).
                const char* why = 0;
                if (!known) {
                    const bool off = mn && (mn == &park ? !device_online() : !mn->online);
                    if (!mn)                      why = "node_unregistered";
                    else if (mi < 0)              why = "module_absent";
                    else if (off)                 why = "node_offline";
                    else if (mn != &park && mn->mod_bits_n == 0)
                        // 🔑 보조 노드는 `S` 가 아직 파서에 안 들어간다(②-b 가 `D` 만 넣었다).
                        //   **구조적으로 값 경로가 없다** — "해독 실패"와 다른 사건이다.
                        why = "bits_unavailable";
                    else if (mn->mod_bits_n == 0) why = "bits_undecoded";
                    else                          why = "bits_out_of_range";
                }
                o << "{\"devid\":" << jstr(z.modules[m].first)
                  << ",\"name\":" << jstr(z.modules[m].second)
                  << ",\"idx\":" << mi
                  << ",\"value\":" << (known ? (mn->mod_bits[mi] ? "true" : "false") : "null")
                  << ",\"known\":" << (known ? "true" : "false");
                if (why) o << ",\"reason\":" << jstr(why);
                o << "}";
            }
            o << "]}";
        }
        o << "]}";
        sensor_split_now = split_now;      // 🔑 누적이 아니라 **지금 값**으로 덮는다
        return o.str();
    }
    void push_state() { ws_broadcast(state_json()); }

    std::string snapshot_json() {
        std::ostringstream o;
        o << "{\"type\":\"snapshot\",\"ts\":" << epoch_ms()
          << ",\"device\":{\"online\":" << (device_online() ? "true" : "false")
          << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq);
        // §9.1 — **파일 폴백(`data_log.json`)과 같은 키·같은 단위·같은 정의**로 낸다(REQ-0132).
        // 값은 `ard_last_epoch_ms` = **마지막 유효 S 프레임을 받은 서버 시각(epoch ms)** 이고,
        // "스냅샷을 만든 시각"(위 `ts`)과 **다른 것**이다. 둘이 갈리는 게 신선도 표시의 요점이다.
        //
        // ⚠ 전에는 이 키가 **WS 에만 없고 파일에만 있었다.** 그래서 신선도 표시가
        // **폴백일 때만 동작하고 정상 운영 중에는 영영 "알 수 없음"** 이었다 —
        // 기능이 가장 필요한 경로에서만 빠져 있던 것이다(web-engineer 크롬 실측).
        //
        // 한 번도 프레임을 못 받았으면 **`null`** 이다. `0` 을 내보내면 화면이 그것을
        // epoch 로 읽어 **1970년으로부터의 나이**를 그린다 — 누락보다 나쁘다.
        o << ",\"last_frame_ts\":";
        if (ard_last_epoch_ms > 0) o << ard_last_epoch_ms; else o << "null";
        o << "}";
        // §5.3 test_mode — 출처는 S 의 tmask 다(§12A.4). 서버가 T 를 보냈다는 사실이 아니다.
        int novr = 0;
        for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) novr++;
        o << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
          << ",\"override_count\":" << novr << "}";
        o << ",\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) o << ",";
            o << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved << ",\"user_id\":";
            if (slots[i].user_id.empty()) o << "null"; else o << jstr(slots[i].user_id);
            o << ",\"reserved_at\":";
            if (slots[i].reserved_at == 0) o << "null"; else o << slots[i].reserved_at;
            o << ",\"overridden\":" << ((test_armed && test_ovr[i]) ? 1 : 0);
            o << "}";
        }
        o << "]}";
        return o.str();
    }
    void push_snapshot() {
        ws_broadcast(snapshot_json());
        // 🔑 **옛 봉투와 같은 순간에 새 봉투를 낸다** — 같은 서버 상태에서 파생시키므로
        //   두 경로가 서로 다른 말을 할 수 없다(web §1.2 의 "한 화면이 두 진실" 방지).
        ws_broadcast(state_json());
    }

