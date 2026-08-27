// wsapi.h — 브라우저 → 서버 (WS 명령 수신 · §5.4). `struct Server` 의 몸통 조각 — server.cpp:187
// ⚠ 단독 컴파일 불가 · include 자리가 곧 문법이다 · 📖 server.cpp "목차"

    // ── 함수 ──────────────────────────────────────────────────────────────
    // 코드포인트를 UTF-8 로. digitcam 명세 §4.3 의 \uXXXX 복원에 쓴다.
    static void utf8_append(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }

    // ---------- 브라우저 → 서버 (§5.4)
    static std::string jget(const std::string& s, const char* key) {
        std::string pat = std::string("\"") + key + "\"";
        size_t i = s.find(pat);
        if (i == std::string::npos) return "";
        i = s.find(':', i + pat.size());
        if (i == std::string::npos) return "";
        i++;
        while (i < s.size() && (s[i]==' '||s[i]=='\t')) i++;
        if (i < s.size() && s[i] == '"') {
            i++; std::string o;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    char e = s[i+1];
                    // digitcam 명세 §4.3: 한글을 원문 UTF-8 로 보내든 \uXXXX 로 보내든
                    // **수신 측이 둘 다 복원**해야 한다. 이걸 안 하면 이스케이프로 보내는
                    // 송신기에서 번호판이 "123가4568" 로 저장돼 매칭이 전부 빗나간다.
                    if (e == 'u' && i + 5 < s.size()) {
                        unsigned cp = 0; bool ok = true;
                        for (int k = 0; k < 4; k++) {
                            char c = s[i+2+k]; int v;
                            if (c >= '0' && c <= '9') v = c - '0';
                            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                            else { ok = false; break; }
                            cp = (cp << 4) | (unsigned)v;
                        }
                        if (ok) { utf8_append(o, cp); i += 6; continue; }
                    }
                    if (e == 'n') { o += '\n'; i += 2; continue; }
                    if (e == 't') { o += '\t'; i += 2; continue; }
                    if (e == 'r') { o += '\r'; i += 2; continue; }
                    o += e; i += 2; continue;                 // \" \\ \/ 등
                }
                o += s[i++];
            }
            return o;
        }
        std::string o;
        while (i < s.size() && s[i] != ',' && s[i] != '}') o += s[i++];
        while (!o.empty() && (o[o.size()-1]==' ')) o.erase(o.size()-1);
        return o;
    }
    void on_ws_message(sock_t fd, const std::string& msg) {
        logf("←WS", msg.substr(0, 120));
        std::string type = jget(msg, "type");
        std::string rid  = jget(msg, "rid");
        std::string slot = jget(msg, "slot");
        std::string uid  = jget(msg, "user_id");
        if (uid == "null") uid.clear();

        // ---- REQ-0203 4b: `get_map` (설계 §6.8)
        // 🔑 **접속 시의 `map` 과 같은 봉투를 쓴다.** 다른 타입을 만들면 같은 것을 두 형식으로
        //   만들게 되고 한쪽만 고치는 날이 온다.
        // 🔴 **창을 기다리지 않고 즉답한다** — 이건 하행 전선이 아니라 화면 소켓이다.
        if (type == "get_map") {
            // ⚠ **연속 요청 상한.** 화면이 `epoch` 비교를 잘못 구현하면 **무한 재요청**이 된다.
            //   `get_map` 은 상태를 안 바꾸니 몇 번 와도 안전하고, **상한은 서버 보호용이지
            //   정합성용이 아니다** — 그래서 거절해도 화면의 정확성은 안 깨진다.
            const long long t = now_ms();
            // 🔑 **이 연결의 창**을 본다. 남의 화면이 물은 것은 이 화면의 몫을 안 먹는다.
            std::map<sock_t, Conn>::iterator ci = conns.find(fd);
            long long& win = (ci != conns.end()) ? ci->second.getmap_win_ms : getmap_win_ms;
            int&       cnt = (ci != conns.end()) ? ci->second.getmap_in_win : getmap_in_win;
            if (t - win >= 1000) { win = t; cnt = 0; }
            if (++cnt > GETMAP_MAX_PER_SEC) {
                getmap_rejects++;
                logf("!", "get_map 이 한 화면에서 1초에 " + std::to_string(cnt)
                          + "회 — 상한 초과로 거절(누적 " + std::to_string(getmap_rejects) + ")");
                send_err(fd, rid, "rate_limited", "요청이 너무 잦습니다");
                return;
            }
            if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, map_json());
            return;
        }

        // ---- 🔴 **수동 촬영 트리거** — 시험 전용 입구
        //   이 트리는 입차 흐름을 통째로 뺐다 → `cameraShoot()` 을 **부를 주체가 없다.**
        //   그래서 화면에서 직접 쏠 수 있게 타입 하나를 둔다.
        //   🔑 흐름을 만들지 않는다 — **촬영 왕복만** 낸다(배정·차단봉은 그대로 없다).
        //   ⚠ 2차 정본(`five/`)에는 **넣지 마라.** 거기엔 흐름이 있어 이 입구가 축을 늘린다.
        if (type == "shoot_now") {
            // 🔑 거절 사유는 **이미 쓰는 어휘**다(`already_pending`). 새 사유코드를 만들지 않는다.
            const long long busy = shot_recent_pending();
            if (busy > 0) {
                const std::string m = "이미 촬영이 진행 중입니다 — 요청번호 " + std::to_string(busy);
                send_err(fd, rid, "already_pending", m.c_str());
                return;
            }
            const long long id = shot_issue();
            if (id <= 0) {
                // 🔑 사유를 **기존 어휘**로 돌려준다 — 새 코드를 안 만든다.
                //   `shot_issue()` 가 0 이하를 주는 경우: 폰 미접속 · seq 소진 · 벽시계 실패.
                //   가장 흔한 것이 폰 미접속이라 그 어휘를 쓴다(로그에는 정확한 사유가 남는다).
                send_err(fd, rid, "device_offline",
                         "촬영 요청을 만들지 못했습니다 — 폰이 붙어 있는지 확인하세요");
                return;
            }
            logf("=", "화면이 촬영을 요청했다 — 요청번호 " + std::to_string(id));
            // 🔴 **요청번호를 같이 실어야 화면이 로그와 대조할 수 있다.**
            //   🔑 `ack` 에 **필드를 더하는 것**이라 모르는 화면은 그냥 무시한다 — 하위호환 공짜.
            //   ⚠ `ack` 는 *"요청을 만들었다"* 까지다. **결과는 `shot_result` 로 따로 간다.**
            {
                std::ostringstream o;
                o << "{\"type\":\"ack\",\"rid\":" << (rid.empty() ? std::string("null") : jstr(rid))
                  << ",\"slot\":null,\"result\":0"
                  << ",\"shot\":" << id
                  << ",\"message\":\"촬영을 요청했습니다\"}";
                if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());
            }
            return;
        }

        // ---- 🔴 수동 입력 폴백 — 화면이 사람이 친 번호를 올린다
        //   계약 정본 : docs/net/SPEC-manual-plate-2026-08-25.md §1
        // 🔑 **새 타입은 이것 하나뿐**이고 응답은 전부 기존 어휘다(`ack` · `error`).
        //   `send_cmd` 로는 못 싣는다 — 그쪽은 **모듈에 정수**를 보내는 통로다.
        if (type == "plate_manual") {
            const std::string plate = jget(msg, "plate");
            // 🔴 **길이 상한은 서버가 건다.** 화면이 막아 주기를 기대하지 않는다 —
            //   붙여넣기 사고 하나가 로그와 봉투를 통째로 밀어낸다.
            if (plate.size() > 64) {
                send_err(fd, rid, "bad_request", "번호가 너무 깁니다");
                return;
            }
            // 🔑 `owner_` 는 콜백에 넘길 공개 객체다 — **없으면 부를 수 없다.**
            if (!manual_cb_ || !owner_) {
                // 🔑 **조용히 버리지 않는다.** 그 기능이 없다는 것을 화면이 알아야
                //   사람에게 말해 줄 수 있다.
                send_err(fd, rid, "not_ready", "이 서버는 수동 입력을 받지 않습니다");
                return;
            }
            // 🔑 **화면이 올린 것은 언제나 사람이 친 것**이다 — 출처를 못 박는다.
            ParkingServer::ManualPlateResult mr = manual_cb_(*owner_, plate, "manual");
            if (!mr.code.empty()) {
                send_err(fd, rid, mr.code.c_str(),
                         mr.message.empty() ? "요청을 처리할 수 없습니다" : mr.message.c_str());
                return;
            }
            // 🔑 `ack` 는 **"받았다"까지**다. 자리 배정은 다음 `state` 가 말한다.
            send_ack(fd, rid, std::string(), 0, 'P');
            return;
        }

        // ---- REQ-0203 4d: 자리 조작 요청 (설계 §6.8 상행)
        // 🔑 **화면은 자리 하나만 지목한다**(`slot`). **모듈을 지목하지 않는다** —
        //   "어느 모듈이 그 조작을 맡는가"가 화면에도 생기면 서버 라우팅과 규칙이 두 곳이 되고,
        //   갈리면 **엉뚱한 모듈에 명령이 간다.** 자리 → 모듈 라우팅은 여기 있다.
        // 🔴🔴 **없앤 경로다**. 위 `wsjson.h` 의 긴 주석이 이유를 말한다.
        //   조용히 무시하지 않고 **무엇을 대신 하라는지** 답한다 — 옛 화면이 붙어 있을 수 있다.
        if (type == "open_gate" || type == "close_gate") {
            logf("!", "게이트 내장 조작(" + type + ") 요청 — **없앤 경로다.** "
                      "모듈 조작은 `lot.control(\"<devid>\",\"<모듈>\").choice(...)` 로 선언해라. "
                      "그래야 **뜻을 기여자가 정한다** (서버가 1/0 을 가정하던 것이 오늘 장치 거절을 냈다)");
            send_err(fd, rid, "not_supported",
                     "모듈 조작은 lot.control(...) 선언으로 합니다 (명령표를 기여자가 정합니다)");
            return;
        }

        // ---- 🔴 화면에서 모듈을 직접 조작한다
        //   명세: docs/net/SPEC-web-control.md
        //   🔑 **자리를 지목하지 않는다.** `open_gate` 는 자리→모듈 라우팅이 서버에 있어야 했지만,
        //     이건 화면이 `map` 에서 본 모듈을 그대로 지목한다. **라우팅할 것이 없다.**
        if (type == "send_cmd" || type == "send_batch") {
            const std::string dv = jget(msg, "devid");
            const Node* n = node_by_devid(dv);
            if (!n) { send_err(fd, rid, "module_absent", "그런 장치를 모릅니다"); return; }
            // 🔴 **장치 상태 검사는 요청 검증 *뒤* 로 옮겼다**
            //   ⚠ 여기서 `device_offline` 을 먼저 내지 마라. 그러면 **선언 오타나 범위 오류가
            //   "장치가 연결되어 있지 않습니다" 로 보고된다** — 기여자가 선을 들여다본다.
            //   🔑 **요청 자체의 결함은 장치 상태와 무관하다. 그것부터 답하는 것이 맞다.**
            //     §"빨강의 원인이 그 항목 안에 있다고 가정하지 마라" 의 사유 코드 판본이다.

            std::vector<std::pair<std::string, long> > items;
            if (type == "send_cmd") {
                items.push_back(std::make_pair(jget(msg, "module"), atol(jget(msg, "value").c_str())));
            } else {
                // `items` 배열을 훑는다. 🔑 **jget 은 첫 일치만 찾으므로** 객체 단위로 잘라 읽는다.
                const std::string key = "\"items\"";
                size_t i = msg.find(key);
                while (i != std::string::npos) {
                    i = msg.find('{', i);
                    if (i == std::string::npos) break;
                    size_t j = msg.find('}', i);
                    if (j == std::string::npos) break;
                    const std::string one = msg.substr(i, j - i + 1);
                    const std::string mn = jget(one, "module");
                    if (!mn.empty())
                        items.push_back(std::make_pair(mn, atol(jget(one, "value").c_str())));
                    i = j + 1;
                    if (msg.find('{', i) == std::string::npos || msg.find(']', i) < msg.find('{', i)) break;
                }
            }
            if (items.empty()) { send_err(fd, rid, "bad_request", "보낼 명령이 없습니다"); return; }

            // 🔴 **선언과 범위를 서버가 판정한다** — 화면이 먼저 막아도 좋지만 믿지 않는다.
            for (size_t k = 0; k < items.size(); k++) {
                const ControlDecl* c = control_of(dv, items[k].first);
                if (!c) {
                    logf("!", "화면 명령 거절 — 모듈 `" + items[k].first
                              + "` 에 `lot.control(...)` 선언이 없다. **선언해야 화면에 조작 UI 가 뜬다**");
                    send_err(fd, rid, "not_declared", "이 모듈은 조작 UI 가 선언되지 않았습니다");
                    return;
                }
                const char* why = c->reject_reason(items[k].second);
                if (why) { send_err(fd, rid, why, "이 모듈이 받을 수 있는 값이 아닙니다"); return; }
                bool found = false;
                for (size_t q = 0; q < n->mods.size(); q++)
                    if (n->mods[q].first == items[k].first) { found = true; break; }
                if (!found) { send_err(fd, rid, "module_absent", "장치에 그 모듈이 없습니다"); return; }
            }
            // ── 여기서부터 **장치 상태**를 본다. 요청은 이미 옳다.
            if (!n->reg_done) {
                // 🔑 **등록 전과 미접속을 가른다.** 사람이 할 일이 다르다 —
                //   전자는 기다리는 것이고 후자는 선을 보는 것이다.
                send_err(fd, rid, "node_unregistered", "장치가 아직 등록되지 않았습니다");
                return;
            }
            // 🔴 **그 명령이 갈 노드의 생사를 본다.** 주 노드만 보면 —
            //   P1 이 붙어 있으면 **P2 가 죽어 있어도 통과**하고, 그 명령은 갈 곳이 없어
            //   큐에서 조용히 죽는다. 화면은 그동안 "진행 중"이다.
            if (!node_online(*n)) { send_err(fd, rid, "device_offline", "장치가 연결되어 있지 않습니다"); return; }
            if ((int)items.size() > max_per_batch()) {
                // 🔴 **한 건도 안 보낸다. 자동 분할 안 한다** — 쪼개면 두 창에 걸쳐 나가고
                //   **묶은 뜻이 사라진다.** 거절이 정직하다.
                send_err(fd, rid, "batch_too_big", "한 번에 보낼 수 있는 개수를 넘었습니다");
                return;
            }
            int q = 0, rj = 0;
            if (items.size() == 1) {
                if (send_to_module(dv, items[0].first, items[0].second, 0, fd, rid)) q = 1; else rj = 1;
            } else {
                send_batch(dv, items, &q, &rj, fd, rid);
            }
            if (type == "send_batch") {
                std::ostringstream o;
                o << "{\"type\":\"batch_result\",\"rid\":" << jstr(rid)
                  << ",\"devid\":" << jstr(dv)
                  << ",\"queued\":" << q << ",\"rejected\":" << rj
                  << ",\"max\":" << max_per_batch() << "}";
                if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());
            } else if (rj) {
                // ⚠ 큐에 못 들어갔으면 **ACK 이 영영 안 온다** → `cmd_result` 도 안 온다.
                //   화면이 영원히 기다리지 않게 여기서 끝낸다.
                send_err(fd, rid, "bad_request", "명령을 큐에 넣지 못했습니다");
            }
            return;
        }

        // ---- 제어기 초기화 — **장치에 세션 상태를 두지 않는다**
        //   🔑 초기화란 *"장치의 기억을 지우는 것"* 이 아니라 **서버가 안전 명령을 다시 내는 것**이다.
        //     게이트 CLOSE · 안내등 OFF 를 서버가 발행하고, 장치는 그것을 수행할 뿐이다.
        //   ⚠ 그래서 장치가 재부팅해도 이 경로는 바뀌지 않는다.
        if (type == "reset_controller") {
            if (!owner_) {
                send_err(fd, rid, "not_ready", "서버 제어기가 준비되지 않았습니다");
                return;
            }
            onControllerReset(*owner_);
            if (fd != BAD_SOCK && conns.count(fd)) {
                std::ostringstream o;
                o << "{\"type\":\"controller_reset\",\"rid\":" << jstr(rid) << ",\"ok\":true}";
                ws_send(fd, o.str());
            }
            return;
        }

        // ---- §12B 시뮬레이터 한 걸음 (개정 5)
        // **무장 여부를 확인하지 않는다.** 테스트 모드와 별개라는 것이 이 기능의 요구다(§12B.3).
        if (type == "sim_step") {
            if (!device_online()) {
                send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
                return;
            }
            dispatch_sim(fd, rid);
            return;
        }

        // 🔑 **문장을 직접 싣는 `ack`.** `send_ack` 는 `kind` 로 정해진 문장을 **고르는** 것이라
        //   새 문장을 못 싣는다. 형식은 같다 — 화면이 갈라 볼 것이 없다.
        // (이 람다는 아래 두 갈래에서만 쓴다)

        // ═══ 8081 자리 선택 (SPEC-web8080-8081-2026-08-26) ═══════════════════
        //   🔴 **역할 선언.** *"8081 포트로 페이지를 받아 갔나"* 가 아니라
        //     *"지금 선택 화면이 살아 있나"* 를 센다 — HTTP GET 은 순간이라 답이 안 된다.
        //   ⚠ 안 보내고 끊어도 된다. 목록은 `conns` 를 정본으로 매번 맞춘다.
        if (type == "chooser") {
            const bool on = (jget(msg, "on") != "false" && jget(msg, "on") != "0");
            if (on) choosers_[fd] = true; else choosers_.erase(fd);
            logf("=", std::string("선택 화면이 ") + (on ? "붙었다" : "떨어졌다")
                      + " — 지금 " + std::to_string(chooser_count()) + "대");
            ack_msg(fd, rid, std::string(),
                    on ? "선택 화면으로 등록했습니다" : "선택 화면에서 해제했습니다");
            return;
        }
        // 🔴 **고른 자리를 흐름에 넘긴다.** 판정은 `lot.cpp` 가 한다 —
        //   여기서 판정하면 흐름을 아는 곳이 둘이 되고, 그 둘이 조용히 갈린다.
        if (type == "pick_slot") {
            if (!pick_cb_ || !owner_) {
                send_err(fd, rid, "not_ready", "이 서버는 자리 선택을 받지 않습니다");
                return;
            }
            const std::string want = jget(msg, "slot");
            if (want.empty()) { send_err(fd, rid, "bad_request", "slot 이 필요합니다"); return; }
            ParkingServer::ManualPlateResult pr = pick_cb_(*owner_, want);
            if (!pr.code.empty()) {
                send_err(fd, rid, pr.code.c_str(),
                         pr.message.empty() ? "자리를 배정할 수 없습니다" : pr.message.c_str());
                return;
            }
            ack_msg(fd, rid, want, "자리를 배정했습니다");
            return;
        }

        // ---- §12A 테스트 모드 (개정 3)
        if (type == "test_arm" || type == "test_disarm" ||
            type == "test_set" || type == "test_clear") {
            if (!device_online()) {
                send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
                return;
            }
            char op = (type == "test_arm") ? 'A' : (type == "test_disarm") ? 'D'
                    : (type == "test_set") ? 'S' : 'X';
            std::string tslot = "??", tval = "-";
            if (op == 'S' || op == 'X') {
                if (slot_index(slot) < 0) {
                    send_err(fd, rid, "bad_request", "그런 자리가 없습니다");
                    return;
                }
                tslot = slot;
                if (op == 'S') {
                    std::string occ = jget(msg, "occupied");
                    if (occ != "0" && occ != "1") {
                        send_err(fd, rid, "bad_request", "occupied 는 0 또는 1 이어야 합니다");
                        return;
                    }
                    tval = occ;
                }
            }
            dispatch_test(fd, rid, op, tslot, tval);
            return;
        }

        if (type != "reserve" && type != "cancel") {
            send_err(fd, rid, "bad_request", "알 수 없는 요청입니다");
            return;
        }
        if (slot_index(slot) < 0) {
            send_err(fd, rid, "bad_request", "그런 자리가 없습니다");
            return;
        }
        // 🔴 **`B1..B5` 는 이제 자리가 아니라 센서다** (사용자 확정 (A) · 명세 §9.3)
        //
        // `slot_index("B5")` 는 여전히 5 를 돌려준다 — `slots[10]` 이 모듈 값의 원천이라 남기 때문이다.
        // ⚠ **그대로 두면 예약이 *성공* 하고 화면 어디에도 안 보인다.** 자리가 5개인 화면에
        //   `B5` 예약이 조용히 성립하면 **그 자리는 예약된 채 아무 표시가 없다.**
        //   🔑 **없는 것이 보이는 것보다, 있는 것이 안 보이는 것이 나쁘다.**
        //
        // 사유 코드는 `not_supported` 와 **다르다**: 저건 "할 수 있는 일인데 수단이 없다"이고
        // 이건 **"애초에 그 대상이 아니다"** 다.
        {
            Zone* zt = zone_find(slot);
            if (!zt || zt->kind != "parking") {
                not_reservable_n++;
                logf("!", "예약 대상이 아닌 id — 거절 " + slot
                          + " (자리가 아니라 센서다. 명세 §9.3) 누적 "
                          + std::to_string(not_reservable_n));
                send_err(fd, rid, "not_reservable", "그 이름은 예약할 수 있는 자리가 아닙니다");
                return;
            }
        }
        // 브라우저 user_id 는 **번호판이 들어올 수 있으므로 UTF-8 을 허용한다.**
        // 전선으로 나갈 값은 wire_userid() 가 따로 좁힌다 — 두 이름공간이다.
        if (!valid_browser_user(uid)) {
            logf("!", "user_id 가 너무 길거나 제어문자 포함(" + std::to_string(uid.size()) + "B) — 거절");
            send_err(fd, rid, "bad_request", "user_id 가 너무 깁니다");
            return;
        }
        // 🔴🔴 **그 자리가 지금 살아 있나**(§7.1 +). 노드 하나의 생사가 아니라
        //   **선언된 센서 전부**가 값을 아는가다 — 하나만 죽어도 예약을 받지 않는다.
        //   🔑 **왜 막나**: 센서가 죽은 자리에 차를 보내면 들어왔는지 나갔는지 서버가 영영 모른다.
        //     그 자리는 **영구 점유로 굳고**, 사람이 손으로 풀어야 한다.
        //   ⚠ 자리는 살아 있는데 **조작만** 죽은 경우는 여기서 안 막는다(모듈별 규칙 · 기존 유지).
        if (!zone_usable(slot)) {
            send_err(fd, rid, "device_offline", "그 자리의 센서를 지금 신뢰할 수 없습니다");
            return;
        }
        // 🔴 같은 자리에 이미 진행 중인 하행이 있으면 **명시적으로 거절한다**(설계 §4-B).
        // 지금까지 이 검사는 `1721`·`1750`(S 프레임 판정)에서만 쓰였고 **브라우저 경로에는
        // 없었다** — 그래서 연타가 그대로 `dispatch` 를 여러 번 만들었다. 큐가 생긴 뒤에는
        // 그것이 곧 큐에 여러 건이 쌓이는 경로다.
        // ⚠ 이제 `pend` 는 **큐에서 기다리는 건까지 포함**하므로(dispatch 가 먼저 `pend` 를
        // 만든다) 이 검사가 큐 대기분도 덮는다.
        // ⚠ **`queue_full` 과 코드를 갈라 둔다** — 화면 문구가 완전히 다르다:
        // 하나는 시스템 사정이고 하나는 "이미 처리 중"이다. 지금까지는 둘 다 `error` 라 못 갈랐다.
        if (has_pending_for(slot)) {
            q_dup++;
            logf("!", "같은 자리에 진행 중인 하행이 있다 — 거절 " + slot);
            send_err(fd, rid, "already_pending", "그 자리는 이미 처리 중입니다");
            return;
        }
        // 서버가 아는 상태로 미리 거를 수도 있지만, 최종 판정은 아두이노 ACK 다(§7.2).
        dispatch(type == "reserve" ? 'R' : 'C', fd, rid, slot, uid);
    }

