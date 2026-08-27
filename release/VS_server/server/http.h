// http.h — HTTP 요청·WebSocket 업그레이드·정적 파일 서빙. `struct Server` 의 몸통 조각 — server.cpp:188
// ⚠ 단독 컴파일 불가 · include 자리가 곧 문법이다 · 📖 server.cpp "목차"

    // ── 함수 ──────────────────────────────────────────────────────────────
    // ---------- HTTP / WS 업그레이드
    static std::string header(const std::string& req, const char* name) {
        std::string low, ln = name;
        for (size_t i = 0; i < req.size(); i++) low += (char)tolower((unsigned char)req[i]);
        for (size_t i = 0; i < ln.size(); i++) ln[i] = (char)tolower((unsigned char)ln[i]);
        size_t i = low.find("\n" + ln + ":");
        if (i == std::string::npos) return "";
        i = req.find(':', i + 1) + 1;
        size_t e = req.find('\r', i);
        if (e == std::string::npos) e = req.find('\n', i);
        std::string v = req.substr(i, e - i);
        while (!v.empty() && (v[0]==' '||v[0]=='\t')) v.erase(0,1);
        while (!v.empty() && (v[v.size()-1]==' '||v[v.size()-1]=='\r')) v.erase(v.size()-1);
        return v;
    }
    void serve_file(sock_t fd, std::string path) {
        // 🔴 **쿼리를 가장 먼저 떼어낸다. 이 순서가 이 함수의 요점이다.**
        // ⚠ `"/"` 판정을 쿼리 제거보다 **앞에 두지 마라** — `GET /?demo=1` 이 404 가 된다:
        //   `/?demo=1` 은 `"/"` 와 다르므로 index.html 로 **안 바뀌고**, 그 뒤 `?` 앞을 자르면
        //   `fn` 이 **빈 문자열**이 되어 열기에 실패했다.
        //   (web-engineer 가 크롬으로 실측: `/index.html?x` 는 200 인데 `/?x` 만 404 다)
        // **쿼리는 경로가 아니다.** 경로에 관한 어떤 판정보다도 앞에서 떼는 것이 맞고,
        // 그래야 `/?x` · `/index.html?x` · `/data_log.json?t=…` 가 **한 규칙**으로 처리된다.
        size_t q = path.find('?');
        if (q != std::string::npos) path = path.substr(0, q);
        // 🔴 **`/` 가 주는 문서는 들어온 포트에 달렸다** (사용자 확정).
        //   9900 → index.html · 8080 → user8080.html · 8081 → user8081.html
        //   🔑 그 뒤 WS 업그레이드는 **완전히 같은 경로**를 탄다 — 이용자 기기가 포트 하나만 알면 된다.
        //   ⚠ 파일이 없으면 404 만 나고 **서버는 그대로 돈다**. web 이 아직 안 만들었어도 무해하다.
        if (path.empty() || path == "/") {
            std::map<sock_t, std::string>::const_iterator di = conn_doc_.find(fd);
            path = (di == conn_doc_.end()) ? std::string("/index.html") : di->second;
        }
        // 경로 탈출 차단 — 데모여도 디렉터리를 서빙하는 코드에 이건 기본이다
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
            const char* r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r), "HTTP 클라이언트");
            return;
        }
        std::string fn = path.substr(1);

        // 🔴 **뿌리 기준 절대경로로 연다**(`doc_path`) — cwd 가 움직여도 실체가 안 바뀐다
        const std::string abs = doc_path(fn);
        std::ifstream f(abs.c_str(), std::ios::binary);
        if (!f) {
            const char* r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r), "HTTP 클라이언트");
            return;
        }
        std::ostringstream ss; ss << f.rdbuf();
        std::string body = ss.str();

        // ── 🔴 **무엇을 내줬는지 로그에 남긴다**
        //
        //   ⚠ `:9900` 이 **이틀 낡은 화면을 계속 내주는** 일이 생긴다. 서버는 아무 잘못이 없고
        //   로그에도 흔적이 없었다 — **`serve_file()` 이 cwd 상대경로로 열기 때문**이다.
        //   ⚠ `ls` 도 `curl 200` 도 둘 다 통과했다. **존재형 검사는 유물을 통과시킨다.**
        //   → 화면이 자기 판본을 `<meta name="screen-build">` 로 싣기로 했으므로(web) **그것을 찍는다.**
        //   🔑 **한 번만 찍는다** — 요청마다 찍으면 로그가 그것으로 덮이고, 판본은 파일이 바뀔 때만 바뀐다.
        if (fn.size() > 5 && fn.substr(fn.size()-5) == ".html") {
            size_t mp = body.find("name=\"screen-build\"");
            std::string tag = "(표지 없음)";
            if (mp != std::string::npos) {
                size_t cs = body.find("content=\"", mp);
                if (cs != std::string::npos) {
                    cs += 9;
                    size_t ce = body.find('"', cs);
                    if (ce != std::string::npos) tag = body.substr(cs, ce - cs);
                }
            }
            if (tag != last_screen_build_) {
                last_screen_build_ = tag;
                logf("=", "화면 판본 — " + tag + " · " + std::to_string(body.size()) + "B · "
                          + abs_path(fn));
            }
        }

        const char* ct = "application/octet-stream";
        if (fn.size() > 5 && fn.substr(fn.size()-5) == ".html") ct = "text/html; charset=utf-8";
        else if (fn.size() > 5 && fn.substr(fn.size()-5) == ".json") ct = "application/json; charset=utf-8";
        else if (fn.size() > 3 && fn.substr(fn.size()-3) == ".js") ct = "application/javascript";
        else if (fn.size() > 4 && fn.substr(fn.size()-4) == ".css") ct = "text/css";

        std::ostringstream h;
        h << "HTTP/1.1 200 OK\r\nContent-Type: " << ct
          << "\r\nContent-Length: " << body.size()
          << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        std::string head = h.str();
        send_raw(fd, head.data(), head.size(), "HTTP 클라이언트");
        send_raw(fd, body.data(), body.size(), "HTTP 클라이언트");
    }
    // 반환 false = 이 연결을 닫아라. **여기서 직접 erase 하지 않는다** —
    // 호출부가 conns 를 순회 중이라 안에서 지우면 반복자가 무효화된다(실제로 SIGSEGV 를 냈다).
    bool on_http(sock_t fd, Conn& c) {
        size_t end = c.inbuf.find("\r\n\r\n");
        if (end == std::string::npos) return true;
        std::string req = c.inbuf.substr(0, end);
        c.inbuf.erase(0, end + 4);

        size_t sp1 = req.find(' ');
        size_t sp2 = req.find(' ', sp1 + 1);
        std::string path = (sp1 != std::string::npos && sp2 != std::string::npos)
                         ? req.substr(sp1 + 1, sp2 - sp1 - 1) : "/";

        std::string key = header(req, "Sec-WebSocket-Key");
        if (!key.empty() && path.substr(0, 3) == "/ws") {
            std::string acc = ws_accept(key);
            std::string r = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n";
            send_raw(fd, r.data(), r.size(), "WS 업그레이드");
            c.kind = Conn::WS;
            // 🔑 최대치는 **승격되는 자리**에서 갱신한다. 요약을 만드는 함수는 `const` 이고
            //   **읽는 함수가 상태를 바꾸면 안 된다**(그래서 `mutable` 을 안 썼다).
            {
                int ws_n = 0;
                for (std::map<sock_t, Conn>::const_iterator it2 = conns.begin();
                     it2 != conns.end(); ++it2)
                    if (it2->second.kind == Conn::WS) ws_n++;
                if (ws_n > ws_peak) ws_peak = ws_n;
            }
            logf("+WS", "업그레이드 완료");
            ws_send(fd, snapshot_json());          // 접속 즉시 현재 상태(옛 봉투)
            // 🔴 **접속 즉시 새 봉투도 보낸다**(4b/4c · web 실기 대조가 이 구멍을 찾았다).
            //   ⚠ `state` 를 `push_snapshot()` 안에서만 방송하면 **장치가 스냅샷을 밀 때까지
            //   새 클라이언트가 못 받았고, 장치가 없으면 영영 못 받았다.**
            //   ⚠ **화면에 우회가 없다** — `get_map` 은 있는데 `get_state` 는 없다.
            // 🔑 **`map` 을 먼저, `state` 를 뒤에 보낸다.** 지형이 먼저 서면 화면이 상태를 바로 그린다 —
            //   반대 순서면 화면이 `state` 를 들고 지형을 기다려야 하고, **그 대기 규칙이 화면에 생긴다.**
            //   **순서를 보내는 쪽이 지키면 받는 쪽에 규칙이 안 생긴다.**
            ws_send(fd, map_json());
            ws_send(fd, state_json());
            return true;
        }
        serve_file(fd, path);
        return false;                              // 정적 응답은 Connection: close
    }
    // WS 프레임 파싱 — 클라이언트 프레임은 **항상 마스킹**돼 있다(§5.2)
    bool ws_pump(sock_t fd, Conn& c) {
        while (true) {
            if (c.inbuf.size() < 2) return true;
            const unsigned char* p = (const unsigned char*)c.inbuf.data();
            int opcode = p[0] & 0x0F;
            bool masked = (p[1] & 0x80) != 0;
            uint64_t len = p[1] & 0x7F;
            size_t off = 2;
            if (len == 126) {
                if (c.inbuf.size() < 4) return true;
                len = (uint64_t(p[2]) << 8) | p[3]; off = 4;
            } else if (len == 127) {
                if (c.inbuf.size() < 10) return true;
                len = 0;
                for (int i = 0; i < 8; i++) len = (len << 8) | p[2+i];
                off = 10;
            }
            if (!masked) { logf("!", "마스킹 안 된 클라이언트 프레임 — 연결 종료"); return false; }
            // 선언된 길이를 그대로 믿으면 inbuf 가 무한히 커진다. 브라우저→서버 메시지는
            // 예약/취소 JSON 뿐이라 수백 바이트면 충분하다.
            if (len > WS_MAX_FRAME) {
                logf("!", "WS 프레임 길이 " + std::to_string((unsigned long long)len)
                          + " 바이트 — 상한 초과, 연결 종료");
                return false;
            }
            if (c.inbuf.size() < off + 4 + len) return true;
            unsigned char mask[4];
            for (int i = 0; i < 4; i++) mask[i] = p[off + i];
            std::string payload;
            payload.reserve((size_t)len);
            for (uint64_t i = 0; i < len; i++)
                payload += char(p[off + 4 + i] ^ mask[i % 4]);
            c.inbuf.erase(0, (size_t)(off + 4 + len));

            if (opcode == 0x8) return false;                       // close
            if (opcode == 0x9) {                                   // ping → pong
                // ping 페이로드는 125바이트 이하만 정상이다(제어 프레임 규칙). 넘으면 잘라 답한다.
                if (payload.size() > 125) payload.resize(125);
                std::string f; f += char(0x8A); f += char((unsigned char)payload.size()); f += payload;
                send_raw(fd, f.data(), f.size(), "WS pong");
                continue;
            }
            if (opcode == 0x1) on_ws_message(fd, payload);
        }
    }

    // ================= 폰(digitcam) 수신 · 번호판 매칭 =========================
    // 수신부는 net/test_server.py 에서 들어왔다 — 바이트 버퍼에 모으고 LF 로 자른 뒤
    // **그 다음에** 디코딩한다. 한글이 recv 경계에서 갈리기 때문이고, 그 파일이
    // 그 경우를 실측으로 검증해 뒀다(digitcam 명세 §8.3, §10.6).

    int find_slot_by_user(const std::string& plate) const {
        for (int i = 0; i < 10; i++)
            if (slots[i].reserved && slots[i].user_id == plate) return i;
        return -1;
    }
    // 빈자리 선택 규칙: **§1 의 전선 인덱스 순서(A1..A5,B1..B5)에서 가장 앞.**
    // 근거 — 결정적이라 같은 입력이면 항상 같은 결과가 나오고(재현 가능한 시험),
    // 명세가 이미 정한 유일한 자리 순서라 새 순서를 발명하지 않는다.
    // 입구에서 가까운 순 같은 물리 배치 기준은 **지금 아무도 갖고 있지 않다** —
    // 배치도가 생기면 이 함수 하나만 바꾸면 된다.
    // 🔴 **비활성 자리는 고르지 않는다**. 센서가 죽은 자리는 "비어 있다"고
    //   말할 근거가 없다 — `occupied` 가 `0` 인 것은 *"비었다"* 가 아니라 **낡은 값**일 수 있다.
    //   ⚠ 여기서 안 막으면 배정 직전 검사(`zone_usable`)가 막아 **매번 "배정 보류"만 찍힌다** —
    //     그러면 **만차인지 고장인지 로그로 안 갈린다.**
    int pick_free_slot() {
        for (int i = 0; i < 10; i++) {
            if (slots[i].occupied || slots[i].reserved) continue;
            if (!zone_usable(SLOT_ID[i])) continue;
            return i;
        }
        return -1;
    }

    void on_plate(const std::string& plate, const std::string& dev) {
        logf("←폰", "번호판 " + plate + " (device=" + dev + ")");

        // ═══ 🔴🔴 **(0) 흐름이 입구를 잡고 있으면 여기서 배정하지 않는다** ═══════════
        //   실측 2026-08-26 18:07:47 — 흐름은 `rejected`(번호 대기) 였는데 요청번호 없는
        //   옛 push 가 들어와 **이 함수가 혼자 A1 을 예약**하고 `R` 하나만 내보냈다.
        //   🔴 그 자리는 차단봉도 안 열리고 안내등도 안 켜진 채 **소리 없이 소모됐다.**
        //     운전자에게는 아무 변화가 없고, 자리는 줄었다. **가장 나쁜 종류의 조용한 실패다.**
        //
        //   ★ 규칙 : **배정자는 하나다.** 흐름이 번호를 기다리는 동안 도착한 번호는
        //     그 흐름의 답이지 별개의 입차가 아니다 — 수동 입력과 **같은 문**으로 넣는다.
        //   🔑 흐름이 안 도는 동안(`idle` 등)에는 종전대로 여기서 처리한다. 옛 폰이 안 깨진다.
        //   ⚠ 흐름이 거절하면 **그대로 돌아간다.** 아래로 흘려보내면 배정자가 다시 둘이 된다.
        //   🔴🔴 **`shooting` 을 뺐다**(2026-08-26 저녁 · 내가 낸 결함의 정정).
        //     `shooting` 은 **요청이 떠 있는 중**이다. 그 요청의 답이 곧 오는데
        //     요청번호 없는 번호가 그것을 **선점**하면 — 실측상 늦은 번호는 **9~39초** 뒤에 온다 —
        //     🔴 **앞차의 번호가 뒤차에게 붙는다.** 그리고 `data_log` 에 **영구 기록**된다.
        //     ★ 그건 ⑧(*"모른다"* 를 정직하게 남김)보다 **나쁘다.**
        //   ✅ `rejected` 는 **떠 있는 요청이 없다** — 경쟁자가 없으니 귀속이 모호하지 않다.
        //   🔑 그리고 **손실이 0 이다**: 실측 5건이 전부 `rejected` 에서 도착했다.
        //     폰이 시한을 **먼저 통보하고** 그 뒤에 번호를 보내므로 구조적으로 항상 그렇다.
        //   ⚠ 그래도 완전하지 않다 — 앞차가 `rejected` 인 채 뒤차가 와서 **뒤차도 실패**하면
        //     앞차 번호가 뒤차에 붙는다. **요청번호 없는 통로의 원리적 한계**다(봉투에 식별자가 없다).
        //     진짜 해결은 앱이 push 에도 식별자를 싣는 것이고, 그건 android 축이다.
        //   ★ 갈래가 **셋**이다. 둘로 하면 하나가 조용히 옛 경로로 샌다 —
        //     🔴 `shooting` 을 조건에서 빼기만 했더니 **아래 옛 배정으로 흘러갔다.**
        //       그건 §9.60(배정자가 둘) 그 자체다. **뺀 것이 아니라 옮긴 것이 된다.**
        const bool flowIdle = entry_.phase.empty() || entry_.phase == "idle";
        if (manual_cb_ && owner_ && entry_.phase == "rejected") {
            // ① 흐름이 **번호를 기다리는 중** — 이 번호가 그 답이다.
            ParkingServer::ManualPlateResult mr = manual_cb_(*owner_, plate, "camera");
            if (mr.code.empty()) {
                logf("=", "요청번호 없는 번호판을 **흐름으로 넘겼다** — " + plate
                          + " (흐름 " + entry_.phase + "). 여기서 배정하지 않는다");
            } else {
                logf("!", "요청번호 없는 번호판을 흐름이 거절 — " + plate
                          + " (" + mr.code + "). **배정하지 않는다**");
            }
            push_snapshot();
            return;
        }
        if (!flowIdle) {
            // ② 흐름이 **다른 단계에서 도는 중** — 넘기지도, 배정하지도 않는다.
            //   🔴 특히 `shooting` : 요청이 떠 있고 **그 답이 곧 온다.**
            //     요청번호 없는 번호가 그것을 선점하면 **앞차 번호가 뒤차에게 붙는다**
            //     (늦은 번호는 실측 **9~39초** 뒤에 온다 — 그 사이 뒤차가 올 수 있다).
            //   ★ 그리고 여기서 `return` 하지 않으면 아래 옛 경로가 **흐름 밖에서 배정**한다.
            //   🔑 **버리는 것이 아니라 세어서 말한다** — 조용히 버리면 다음 사람이 못 찾는다.
            // 🔴 **두 뜻을 한 칸에 섞지 않는다**(§9.73 — 오늘 축을 섞어 두 번 틀렸다).
            //   🔵 지금 처리 중인 **그 번호가 또 온 것** = 정상이다. 폰이 계속 보내는 것뿐이다
            //   🔴 **다른 번호**가 도는 중에 온 것 = **진짜 경보**다(앞차·뒤차가 뒤섞인다)
            //   ★ 안 가르면 초당 1건 송신에서 경보가 **소음에 묻혀 쓸모없어진다**
            if (!entry_.plate.empty() && plate == entry_.plate) {
                plate_repeat_++;
                return;                        // 조용히 센다 — 이건 사건이 아니다
            }
            plate_orphan_++;
            logf("!", "요청번호 없는 **다른** 번호판을 버린다 — " + plate + " (흐름 "
                      + entry_.phase + " · 처리 중인 번호 "
                      + (entry_.plate.empty() ? std::string("(없음)") : entry_.plate)
                      + " · 누적 " + std::to_string(plate_orphan_)
                      + "회). **떠 있는 요청의 답을 선점하지 않는다**");
            return;
        }
        // ③ 흐름이 **안 돈다**(idle) — 아래 옛 push 경로가 처리한다. 옛 폰이 안 깨진다.

        // (1) 같은 번호판이 또 왔다 — 이미 배정된 자리가 살아 있으면 새 입차가 아니다.
        //     fresh 는 상승 엣지지만 인식이 끊겼다 다시 잡히면 새 엣지가 뜬다.
        //     **시간 창을 두지 않고 상태로 판정한다** — 타이머는 근거 없는 숫자를 만든다.
        std::map<std::string, std::string>::iterator it = plate_slot.find(plate);
        if (it != plate_slot.end()) {
            int i = slot_index(it->second);
            if (i >= 0 && slots[i].user_id == plate && (slots[i].reserved || slots[i].occupied)) {
                logf("=", "중복 수신 — " + plate + " 는 이미 " + it->second
                          + " 에 배정돼 있다. 새로 배정하지 않는다");
                return;
            }
            plate_slot.erase(it);   // 그 사이 은퇴/취소됐다 → 다시 배정 가능
        }

        // (2) 이미 주차된 차의 번호판이 게이트에 나타났다.
        //     오인식일 수도, 실제로 나갔다 다시 온 것일 수도 있다 — 서버는 구분할 수 없다.
        //     **새 자리를 배정하지 않는다.** 배정하면 같은 차가 두 칸을 먹는다.
        //     사람이 볼 수 있게 로그로 올린다.
        //     🔴🔴 **`user_id` 만 보면 흐름이 세운 주차를 못 본다** (2026-08-26 실측):
        //       흐름의 ⑦ 은 `slotPlate()` 로 **`slots[i].plate`** 에 쓰고,
        //       이 가드는 **`slots[i].user_id`** 를 본다 — **서로 다른 칸이다.**
        //       `grep '\.plate =='` 가 **0건**이었다: 아무도 흐름이 쓴 칸을 안 봤다.
        //     🔴 그래서 폰이 같은 번호를 반복 송신하면(`send.mode=every_stable_frame`, 초당 1건)
        //       주차가 끝난 뒤 이 가드를 **그냥 통과해 초당 한 자리씩 배정한다** — 5초면 만차다.
        //     ✅ **두 칸을 다 본다.** 어느 경로가 세웠든 "이미 그 차가 있다" 는 같은 사실이다.
        for (int i = 0; i < 10; i++) {
            if (slots[i].occupied && (slots[i].user_id == plate || slots[i].plate == plate)) {
                // 🔑 **반복 억제** — 초당 1건이 오면 이 줄이 하루 86,400 줄이 된다.
                //   ★ 조용히 하지 않는다. **처음과 그 뒤 N초마다** 말하고, 그 사이는 센다.
                const long long t = now_ms();
                if (plate != dup_last_plate_ || t - dup_last_ms_ >= DUP_LOG_QUIET_MS) {
                    logf("!", "이미 주차된 번호판이 게이트에서 인식됨 — " + plate + " ("
                              + SLOT_ID[i] + "). 오인식이거나 재진입이다. 새 배정 없음"
                              + (dup_since_log_ ? " · 그 사이 같은 것 "
                                   + std::to_string(dup_since_log_) + "건 더" : ""));
                    dup_last_plate_ = plate; dup_last_ms_ = t; dup_since_log_ = 0;
                } else {
                    dup_since_log_++;
                }
                return;
            }
        }

        // (3) 예약이 있으면 그 자리로 확정한다. 키는 번호판이다.
        int i = find_slot_by_user(plate);
        if (i >= 0) {
            plate_slot[plate] = SLOT_ID[i];
            logf("✓", "예약 확인 — " + plate + " → " + SLOT_ID[i] + " (안내 대상 자리)");
            push_snapshot();
            return;
        }

        // (4) 예약이 없으면 빈자리를 배정한다(사용자 요구: "안 하면 빈자리").
        int f = pick_free_slot();
        if (f < 0) {
            // 조용히 지나가지 않는다. 브라우저로 올리려면 새 WS 메시지가 필요한데
            // 그건 명세 변경이라 하지 않았다(은 명세 무변경). 루트에 보고했다.
            logf("!", "빈자리 없음 — " + plate + " 배정 실패. 차단기를 열 자리가 없다");
            return;
        }
        // 🔴🔴 **그 자리가 지금 살아 있나**. 선언된 센서 하나만 죽어도 배정하지 않는다.
        //   🔑 센서가 죽은 자리에 차를 보내면 **들어왔는지 나갔는지 서버가 영영 모른다** —
        //     그 자리는 영구 점유로 굳는다. 배정을 미루는 편이 낫다.
        if (!zone_usable(SLOT_ID[f])) {
            logf("!", std::string("자리 ") + SLOT_ID[f] + " 의 센서를 신뢰할 수 없다 — "
                      + plate + " 배정 보류");
            return;
        }
        plate_slot[plate] = SLOT_ID[f];
        logf("✓", "예약 없음 → 빈자리 배정 " + plate + " → " + SLOT_ID[f]);
        dispatch('R', BAD_SOCK, "", SLOT_ID[f], plate);   // 예약의 주인은 서버다(§7.4)
    }

    void on_phone_line(const std::string& line) {
        // digitcam 명세 §5: plain 모드는 번호판 문자열 한 줄, json 모드는 오브젝트 하나.
        std::string plate, dev, shot_s, err;
        if (!line.empty() && line[0] == '{') {
            plate = jget(line, "value");
            dev = jget(line, "device");
            // 🔑 `shot` 이 **없으면 옛 push** 다 — 옛 폰이 붙어도 안 깨진다(명세 §4 하위 호환).
            shot_s = jget(line, "shot");
            err    = jget(line, "error");
        } else {
            plate = line;
        }
        // 🔴 요청에 대한 응답이면 **대장을 먼저 갱신한다.**
        //   ⚠ `on_plate()` 가 중복·이미주차 갈래에서 **일찍 return 한다** —
        //     뒤에 두면 그 갈래에서 대장이 영원히 CAM_PENDING 으로 남는다.
        //     §"그 줄이 어느 갈래에서만 찍히나" 를 여기서 미리 피한다.
        if (!shot_s.empty()) {
            const long long sid = atoll(shot_s.c_str());
            // 🔴 길이 상한은 대장에 넣기 **전**에 건다 — 넣고 나면 흐름·화면·로그로 그대로 나간다.
            if (plate.size() > MAX_PLATE_BYTES) {
                logf("!", "폰: 번호판이 너무 길다(" + std::to_string(plate.size()) + "B) — 실패로 기록");
                shot_answer(sid, std::string(), std::string("too_long"));
                return;
            }
            shot_answer(sid, plate, err);
            // 🔑 요청번호가 있는 응답은 **흐름(`lot.cpp`)이 요청번호로 회수한다.**
            //   ⚠ 옛 push 배정 경로(`on_plate` → `dispatch('R')`)를 타면 **배정자가 둘**이 되어
            //     한 차가 자리를 두 번 먹는다. 그래서 여기서 끝낸다.
            return;
        }
        if (plate.empty()) {
            // §5.2 — 송신 측은 빈 값을 보내지 않지만 수신 측은 견딘다.
            logf("!", "폰: 빈 값 — 무시");
            return;
        }
        if (plate.size() > MAX_PLATE_BYTES) {
            logf("!", "폰: 번호판이 너무 길다(" + std::to_string(plate.size()) + "B) — 무시");
            return;
        }
        // **신뢰도 임계값을 두지 않는다.** 폰이 이미 min_confidence 로 걸렀고,
        // conf 는 문자별 최소값이라 문자 수가 늘수록 낮아진다(digitcam 명세 §4.4 경고).
        // 서버가 절대값으로 자르면 **긴 번호판만 유독 거절된다.**
        on_plate(plate, dev.empty() ? std::string("?") : dev);
    }

    // 기동 시 data_log.json 이 없으면 **빈 배열로 만들어 둔다.**
    //
    // 왜: 서버는 §9.4 대로 상태가 바뀔 때만 파일을 쓴다. 아두이노가 안 붙으면 한 번도 안 쓴다.
    // 그런데 화면은 WS 가 붙기 전부터 폴백 폴링을 돌리므로 **아무 문제 없는 첫 실행에서
    // 404 를 받아 빨간 오류를 띄운다.** 이런 것이 쌓이면 진짜 오류를 무시하게 된다.
    //
    // `[]` 는 §9.1 형태에서 "아직 기록이 없다"를 정직하게 표현한다 —
    // 없는 파일보다 낫고, 지어낸 값을 넣는 것보다도 낫다.
    //
    // **이미 있으면 절대 덮지 않는다.** 덮으면 재시작할 때마다 직전 2건이 날아간다.
    void ensure_log_exists() {
        if (no_disk) return;                 // 시험용 인스턴스는 파일을 만들지도 않는다
        std::ifstream f(data_path("data_log.json").c_str(), std::ios::binary);
        if (f.good()) {
            logf("=", "data_log.json 이 이미 있다 — 그대로 둔다(직전 기록 보존)");
            return;
        }
        atomic_write_log("[]\n");     // 쓰기 경로는 원자적 교체 하나뿐이다
        logf("=", "data_log.json 이 없어 빈 배열로 만들었다");
    }

