// serve.h — 한 박자 — 수신·자리 판정·하행 송신·화면 방송 (serveOneTick). `struct Server` 의 몸통 조각 — server.cpp:190
// ⚠ 단독 컴파일 불가 · include 자리가 곧 문법이다 · 📖 server.cpp "목차"

    // 한 박자 : 수신 → 자리 판정 → 하행 송신 → 화면 방송. **false 면 그만 돈다**
    //
    // 🔴 **여기에 코드를 더할 때 알아야 할 것**
    //   이 본문은 원래 `while (!g_stop) { … }` 의 몸통이었다. 기계적으로 옮겼고,
    //   옮기기 전에 **최상위 `continue`/`break`/`return` 이 하나도 없는 것**을 확인했다.
    //   그 확인이 이 분할의 안전망 전부였다.
    //
    //   ✅ **그런데 옮긴 뒤로 둘은 *구조적으로* 막혔다** — 실험으로 확인했다:
    //      최상위 `continue;` → `error: 'continue' statement not in loop statement` (컴파일 실패)
    //      `break;` 도 같다. **루프가 아니므로 컴파일러가 잡는다.**
    //
    //   🔴 **남는 위험은 `return` 하나다.** 최상위에 `return false;` 를 쓰면 **컴파일된다.**
    //      그리고 그 뜻은 "이 박자를 건너뛴다"가 아니라 **"서버를 멈춘다"** 이다.
    //      ⚠ 옛 코드에서 `continue` 였을 자리에 무심코 `return` 을 쓰면 **서버가 조용히 죽는다.**
    //      → 이 박자를 건너뛰려면 **`return true;`** 다. 멈추는 것은 `g_stop` 이 정한다.
    //
    //   🔴 **`false` 를 내는 경로는 지금 *둘* 이고 둘 다 `g_stop` 이 정한다**:
    //      ① 들머리 `if (g_stop) return false;`   ② 끝의 `return !g_stop;`
    //      **`return true` 는 0곳이다** — 아직 아무도 박자를 건너뛰지 않는다.
    //      🔑 **이 수가 늘면 신호다.** 셋째 `false` 는 `g_stop` 이 아닌 이유로 서버를 멈춘다는 뜻이고,
    //        그건 **호출자가 모르는 정지 조건**이 생겼다는 것이다. 늘릴 거면 명세 §11.6 에 이유를 적어라.

    // ── 함수 ──────────────────────────────────────────────────────────────
    // 🔴🔴 **폰 유휴 하트비트** — 앱을 안 고치고 유휴 끊김을 막는다
    //
    //   실측(android · 에뮬): 앱이 **103초 유휴** 뒤 `ECONNABORTED` 로 끊긴다.
    //   원인 추정은 **NAT 의 유휴 TCP 정리**다. 앱은 3초 만에 재접속하지만 그 창 동안
    //   `phones` 가 비어 `cameraShoot()` 이 `-1` 을 돌려준다 — 촬영이 아예 안 만들어진다.
    //
    // 🔴 **서버 keepalive 로는 못 막는다**: `set_keepalive()` 는 아두이노 소켓에만 걸고,
    //   윈도우 갈래는 `SO_KEEPALIVE` 만 켜서 **기본 유휴가 2시간**이다(metrics.h 주석).
    //   실기 서버가 윈도우이므로 그 경로는 하중을 못 받는다.
    //
    // 🔑 **서버가 보내는 쪽**을 골랐다(android 와 합의):
    //   ✅ 앱 변경 0 — 앱은 모르는 하행 줄을 **설계상 무시**한다(전방 호환 · CameraShot.kt:111)
    //   ✅ `plain`/`json` 함정이 없다 — 하행은 형식 설정과 무관하다
    //      ⚠ 상행으로 했으면 `plain` 에서 **줄 전체가 번호판**이라 `PING` 이 번호판이 된다
    //   ✅ 부수 이득 : 주기적으로 쓰므로 **폰이 죽은 것을 더 빨리 안다**
    //
    // ⚠ **응답을 안 받는다.** 목적은 NAT 매핑 유지이고, 응답을 받으려면 상행 형식이 필요해
    //   위의 `plain` 함정이 되돌아온다.
    // ⚠ 주기 30초는 **에뮬 NAT 값(103초)의 3배 여유**다. 실기(공유기)에서는 다시 재야 한다.
    void phone_ping_if_due() {
        if (phones.empty()) return;
        const long long t = now_ms();
        if (phone_ping_at_ != 0 && t - phone_ping_at_ < PHONE_PING_MS) return;
        phone_ping_at_ = t;
        char b[32];
        snprintf(b, sizeof(b), "PING,%lld\n", ++phone_ping_seq_);
        const std::string line(b);
        for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it)
            send_raw(it->first, line.data(), line.size(), "폰", SEND_TIMEOUT_WS_MS);
        // 🔑 **로그를 매번 찍지 않는다** — 30초마다 한 줄이면 하루 2,880줄이다.
        //   첫 세 번만 찍고 그 뒤는 계수로 남긴다(요약에 실린다).
        if (phone_ping_seq_ <= 3)
            logf("→폰", "유휴 하트비트 " + std::to_string(phone_ping_seq_)
                        + " (주기 " + std::to_string(PHONE_PING_MS / 1000) + "초 · 응답 안 받는다)");
    }

    bool serveOneTick() {
        if (g_stop) return false;
        phone_ping_if_due();
        // 🔵 상행 로그 요약 — 🔑 **매 박자에 부른다.** 안에서 시각을 보고 때가 됐을 때만 낸다
        //   ⚠ 여기서 조건을 걸지 마라 — **보드가 0대일 때도 줄이 나가야** 한다(그게 요점이다)
        ard_sum_tick();
            fd_set rd; FD_ZERO(&rd);
            sock_t mx = 0;
            FD_SET(lsn_ard, &rd);  if (lsn_ard  > mx) mx = lsn_ard;
            FD_SET(lsn_http, &rd); if (lsn_http > mx) mx = lsn_http;
            // 🔑 **못 연 포트는 감시하지 않는다** — `BAD_SOCK` 을 `FD_SET` 하면 select 가 깨진다.
            if (lsn_find_   != BAD_SOCK) { FD_SET(lsn_find_,   &rd); if (lsn_find_   > mx) mx = lsn_find_; }
            if (lsn_choose_ != BAD_SOCK) { FD_SET(lsn_choose_, &rd); if (lsn_choose_ > mx) mx = lsn_choose_; }
            FD_SET(lsn_phone, &rd); if (lsn_phone > mx) mx = lsn_phone;
            for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            if (ard != BAD_SOCK) { FD_SET(ard, &rd); if (ard > mx) mx = ard; }
            // ⚠ REQ-0083 — **여기에 하나라도 빠뜨리면 그 노드는 조용히 귀머거리가 된다.**
            // 오류도 로그도 없이 영영 readable 이 안 될 뿐이라, 방화벽 신원 사고와 같은 형태다.
            // 아래 둘(보조 노드·id 미상 소켓)은 select 대상에 **반드시** 들어가야 한다.
            for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
                if (it->second.fd == BAD_SOCK) continue;
                FD_SET(it->second.fd, &rd);
                if (it->second.fd > mx) mx = it->second.fd;
            }
            for (size_t k = 0; k < unknown.size(); k++) {
                if (unknown[k].fd == BAD_SOCK) continue;
                FD_SET(unknown[k].fd, &rd);
                if (unknown[k].fd > mx) mx = unknown[k].fd;
            }
            for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            // 타이머(재전송·offline)를 돌려야 하므로 NULL 을 주면 안 된다.
            // 소켓이 조용해도 이 주기로 깨어나 tick() 을 돈다.
            timeval tv; tv.tv_sec = 0; tv.tv_usec = SELECT_TICK_MS * 1000;
            int n = select((int)mx + 1, &rd, NULL, NULL, &tv);

            if (n > 0) {
                if (FD_ISSET(lsn_ard, &rd)) {
                    sock_t c = accept(lsn_ard, NULL, NULL);
                    // 🔴 **읽기 타임아웃을 건다** — 없으면 조용한 소켓 하나가 `recv` 에서
                    //   이벤트 루프를 통째로 잡는다.
                    // 🔑 **되읽은 값을 첫 소켓에서 한 번 찍는다** — 매번 찍으면 로그를 덮는다.
                    if (c != BAD_SOCK) { const std::string e = set_recv_timeout(c);
                                         if (!e.empty() && !rcvto_logged_) {
                                             rcvto_logged_ = true; logf("=", "아두이노 소켓 — " + e); } }
                    if (c != BAD_SOCK) {
                        // ⚠ REQ-0083 — **여기서 옛 소켓을 끊지 않는다. 그게 조원 배포의 차단 요인이었다.**
                        // 이 시점엔 device_id 를 모른다(첫 프레임을 받아야 안다). 그런데 옛 구조는
                        // 모르는 채로 기존 연결을 끊었고, 그래서 **노드 2대가 서로 밀어냈다.**
                        // 이제는 대기열에 넣고 **첫 유효 프레임에서 승격**한다(promote_unknown).
                        set_send_timeout(c);
                        std::string ka = set_keepalive(c);
                        // 상한 초과는 **거절**한다. 살아 있는 노드를 쫓아내지 않는 이유는
                        // MAX_ARD_NODES 주석에 있다(확실한 것을 버리고 불확실한 것을 얻지 않는다).
                        if (unknown.size() >= MAX_UNKNOWN_SOCKS) {
                            admit_rejects++;
                            logf("!", "id 미상 소켓이 상한(" + std::to_string(MAX_UNKNOWN_SOCKS)
                                      + ")에 찼다 — 새 연결 거절. 누적 " + std::to_string(admit_rejects) + "회");
                            closesock(c);
                        } else {
                            UnknownSock u; u.fd = c; u.since_ms = now_ms();
                            u.peer = peer_str(c);
                            unknown.push_back(u);
                            logf("+?", "연결 수락 — 상대 " + u.peer
                                       + " · device_id 대기 중(" + std::to_string(UNKNOWN_TIMEOUT_MS / 1000)
                                       + "초 안에 유효 프레임 없으면 끊는다) · " + ka);
                        }
                    }
                }
                if (FD_ISSET(lsn_http, &rd)) {
                    sock_t c = accept(lsn_http, NULL, NULL);
                    if (c != BAD_SOCK) (void)set_recv_timeout(c);
                    if (c != BAD_SOCK) { set_send_timeout(c, SEND_TIMEOUT_WS_MS); conns[c] = Conn();
                                         // 🔑 **fd 번호는 재사용된다.** 옛 주인의 문서 배정·선택 등록을
                                         //   안 지우면 **새 연결이 그것을 물려받는다.**
                                         conn_doc_.erase(c); choosers_.erase(c); }
                }
                // 🔴 **이용자 포트도 `conns` 에 같이 들어간다** — HTTP·WS 처리가 완전히 같다.
                //   갈리는 것은 `/` 에 줄 문서 하나뿐이다(`conn_doc_`).
                //   ⚠ 이 연결들도 `state` 방송을 받는다. 관제 자료가 이용자 기기에 그대로 간다 —
                //     지금은 그대로 둔다(DEFERRED). **동작을 먼저 세우고 양은 나중에 줄인다.**
                if (lsn_find_ != BAD_SOCK && FD_ISSET(lsn_find_, &rd)) {
                    sock_t c = accept(lsn_find_, NULL, NULL);
                    if (c != BAD_SOCK) {
                        (void)set_recv_timeout(c); set_send_timeout(c, SEND_TIMEOUT_WS_MS);
                        conns[c] = Conn(); conn_doc_[c] = "/user8080.html";
                        choosers_.erase(c);
                    }
                }
                if (lsn_choose_ != BAD_SOCK && FD_ISSET(lsn_choose_, &rd)) {
                    sock_t c = accept(lsn_choose_, NULL, NULL);
                    if (c != BAD_SOCK) {
                        (void)set_recv_timeout(c); set_send_timeout(c, SEND_TIMEOUT_WS_MS);
                        conns[c] = Conn(); conn_doc_[c] = "/user8081.html";
                        choosers_.erase(c);
                    }
                }
                if (FD_ISSET(lsn_phone, &rd)) {
                    sock_t c = accept(lsn_phone, NULL, NULL);
                    if (c != BAD_SOCK) (void)set_recv_timeout(c);
                    if (c != BAD_SOCK) {
                        set_send_timeout(c, SEND_TIMEOUT_WS_MS);
                        phones[c] = std::string();
                        phone_since_[c] = now_ms();      // 🔑 유지 시간을 재려면 시작이 있어야 한다
                        phone_epoch_++;                  // 🔑 호출자가 "방금 붙었나" 를 이것으로 안다
                        if (phones.size() > phone_max_) phone_max_ = phones.size();
                        // 🔑 상대 주소를 찍는다 — 여러 폰이 붙을 때 어느 것이 끊겼는지 갈린다
                        {
                            sockaddr_in pa; socklen_t pl = sizeof(pa);
                            char pip[64] = "?";
                            if (getpeername(c, (sockaddr*)&pa, &pl) == 0)
                                snprintf(pip, sizeof(pip), "%s:%d",
                                         inet_ntoa(pa.sin_addr), (int)ntohs(pa.sin_port));
                            logf("+폰", "digitcam 접속 — " + std::string(pip));
                        }
                    }
                }
                // 폰 연결들 — 순회 중 map 을 건드리지 않도록 fd 를 먼저 모은다
                {
                    std::vector<sock_t> pr, pdrop;
                    for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it)
                        if (FD_ISSET(it->first, &rd)) pr.push_back(it->first);
                    for (size_t k = 0; k < pr.size(); k++) {
                        sock_t fd = pr[k];
                        char b[2048];
                        int r = (int)recv(fd, b, sizeof(b), 0);
                        // 🔴 **타임아웃은 끊김이 아니다** — 사유를 보고 그냥 넘어간다(원장 §9.40)
                        if (r < 0 && err_is_again(sockerr())) continue;
                        // 🔴 **왜 끊겼는지를 남긴다.** 사유가 없으면 폰 쪽과 서버 쪽이
                        //   **영원히 서로를 의심한다** — 아두이노 경로는 이미 사유를 찍는다.
                        //   `r == 0` → 상대가 **정상 종료(FIN)** 했다. 서버가 닫은 것이 아니다
                        //   `r <  0` → 오류. `errno` 가 그것을 말한다(ECONNRESET 54 · ETIMEDOUT 60 …)
                        if (r <= 0) {
                            const int e = (r < 0) ? sockerr() : 0;
                            phone_close_why_[fd] = (r == 0)
                                ? std::string("상대가 닫음(FIN)")
                                : ("수신 오류 errno=" + std::to_string(e));
                            pdrop.push_back(fd); continue;
                        }
                        std::string& pb = phones[fd];
                        pb.append(b, r);
                        size_t i;
                        while ((i = pb.find('\n')) != std::string::npos) {
                            std::string line = pb.substr(0, i);
                            pb.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (!line.empty()) on_phone_line(line);
                        }
                        if (pb.size() > MAX_PHONE_LINE) {
                            logf("!", "폰: LF 없이 상한 초과 — 버퍼 비움");
                            pb.clear();
                        }
                    }
                    for (size_t k = 0; k < pdrop.size(); k++) {
                        // LF 없이 남은 조각은 완성된 줄이 아니다 — 값으로 쓰지도, 조용히 버리지도 않는다
                        // 🔑 사유를 **항상** 붙인다 — `-폰 digitcam 연결 종료` 만으로는
                        //   *"서버가 끊었나 폰이 끊었나"* 를 못 가른다.
                        std::string why = phone_close_why_.count(pdrop[k])
                                        ? phone_close_why_[pdrop[k]] : std::string("미상");
                        const long long held = phone_since_.count(pdrop[k])
                                             ? (now_ms() - phone_since_[pdrop[k]]) : -1;
                        if (held >= 0) why += " · 유지 " + std::to_string(held) + "ms";
                        if (held >= 0) {
                            phone_hold_last_ms_ = held;
                            if (held > phone_hold_max_ms_) phone_hold_max_ms_ = held;
                        }
                        // ⚠ **서버는 폰 소켓을 스스로 안 닫는다** — keepalive 도 유휴 회수도 없다.
                        //   그러므로 여기 오는 것은 **언제나 상대 또는 망**이다. 그 사실을 값으로 남긴다.
                        if (phones.count(pdrop[k]) && !phones[pdrop[k]].empty())
                            logf("!", "폰 해제 — " + why + " · 미종단 잔여 "
                                      + std::to_string(phones[pdrop[k]].size()) + "B 버림");
                        else logf("-폰", "digitcam 연결 종료 — " + why);
                        closesock(pdrop[k]); phones.erase(pdrop[k]);
                        phone_close_why_.erase(pdrop[k]); phone_since_.erase(pdrop[k]);
                    }
                }
                if (ard != BAD_SOCK && FD_ISSET(ard, &rd)) {
                    char b[2048];
                    int r = (int)recv(ard, b, sizeof(b), 0);
                    if (r < 0 && err_is_again(sockerr())) { /* 타임아웃 — 끊김이 아니다 */ } else
                    if (r <= 0) {
                        // ⚠ 이유를 **원인별로** 남긴다. keepalive 가 죽인 소켓은
                        // recv 가 ETIMEDOUT 으로 돌아오는데, 그걸 "수신 오류" 한 문자열에
                        // 섞으면 keepalive 가 한 번이라도 일했는지 증명할 수 없다 —
                        // 켠 것과 안 켠 것이 로그에서 똑같아 보이는 계측은 계측이 아니다.
                        const int e = (r == 0) ? 0 : sockerr();
                        if (r != 0 && err_is_timeout(e)) keepalive_reaps++;   // 🔴 세는 것은 **주 노드만**
                        const std::string why = sock_close_why(r, e);
                        end_ard_session(why);
                        closesock(ard); ard = BAD_SOCK; ard_buf.clear();
                        emit_dev(DEV_DISCONNECT, park_dev, why);   // 단계 C
                    } else {
                        ard_buf.append(b, r);
                        size_t i;
                        while ((i = ard_buf.find('\n')) != std::string::npos) {
                            std::string line = ard_buf.substr(0, i);
                            ard_buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            // 🔑 **버릴 때도 길이를 남긴다.** "64B 초과" 라고만 적고 버리면
            // **몇 바이트였는지**를 못 봤다 — `AT+CIPSEND` 잘림 시험에서 그것이 측정값이다.
            if (line.size() + 1 > (size_t)MAX_LINE) {
                drop_overlong++;
                // 🔴 이름이 `b` 였다 — **바깥의 수신 버퍼 `b` 를 가리고 있었다**(`-Wshadow`).
                //   여기서 누가 `recv(fd, b, sizeof(b))` 를 쓰면 **96바이트짜리로 읽는다.**
                //   ⚠ 컴파일은 통과한다. **가려진 이름은 틀렸을 때 조용하다.**
                char msg[96];
                snprintf(msg, sizeof(msg), "상한(%dB) 초과 줄 — 버림 · **rx=%zuB**",
                         MAX_LINE, line.size());
                logf("!", msg);
                continue;
            }
                            if (!line.empty()) on_ard_line(park, line);
                        }
                        if (ard_buf.size() > (size_t)MAX_LINE) {
                            drop_noise++;
                            logf("!", "LF 없이 64B 초과 — 버퍼 비움");
                            ard_buf.clear();
                        }
                    }
                }

                // ---------- id 미상 소켓 — 첫 유효 프레임에서 승격
                {
                    std::vector<size_t> gone;
                    for (size_t k = 0; k < unknown.size(); k++) {
                        sock_t fd = unknown[k].fd;
                        if (fd == BAD_SOCK || !FD_ISSET(fd, &rd)) continue;
                        char b[1024];
                        int r = (int)recv(fd, b, sizeof(b), 0);
                        if (r < 0 && err_is_again(sockerr())) continue;   // 타임아웃 — 끊김이 아니다
                        if (r <= 0) {
                            logf("-?", "id 미상 소켓이 승격 전에 끊겼다");
                            closesock(fd); unknown[k].fd = BAD_SOCK; gone.push_back(k);
                            continue;
                        }
                        unknown[k].buf.append(b, r);
                        std::string dev, rest;
                        bool found = false;
                        size_t i;
                        while (!found && (i = unknown[k].buf.find('\n')) != std::string::npos) {
                            std::string line = unknown[k].buf.substr(0, i);
                            unknown[k].buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (peek_devid(line, dev)) {
                                found = true;
                                // **승격해도 이 프레임을 잃지 않는다.** 되돌려 놓고 넘긴다 —
                                // 첫 프레임은 §7.4 기준선을 세우는 줄이라 버리면 안 된다.
                                rest = line + "\n" + unknown[k].buf;
                            } else if (!line.empty()) {
                                //  여기서 버려지는 줄을 **이제 센다.**
                                // ⚠ 아무 카운터도 안 올리고 사라지면, 이 소켓이 침묵했는지
                                // 말했는데 못 알아들었는지 구별할 방법이 없었다.
                                drop_prepromo++;
                            }
                        }
                        if (found) {
                            unknown[k].fd = BAD_SOCK; gone.push_back(k);
                            if (promote_unknown(fd, dev)) {
                                if (ard == fd)                      { ard_buf = rest; drain_ard_buf(); }
                                else if (aux.count(dev) && aux[dev].fd == fd) aux[dev].buf = rest;
                            }
                        } else if (unknown[k].buf.size() > (size_t)MAX_LINE * 4) {
                            // 잡음만 흘리는 소켓이 메모리를 먹지 않게 한다(§6.2)
                            //  통째로 버리는 것도 **말없이 하지 않는다.**
                            drop_prepromo_buf++;
                            logf("!", "승격 전 소켓 버퍼 상한 초과 — 통째로 비움("
                                      + std::to_string(unknown[k].buf.size()) + "B)");
                            unknown[k].buf.clear();
                        }
                    }
                    for (size_t j = gone.size(); j > 0; j--) unknown.erase(unknown.begin() + gone[j-1]);
                }

                // ---------- 보조 노드 — **상행 전용**
                {
                    std::vector<std::string> dead_devs;
                    std::vector<std::string> dead_why;   // dead_devs 와 **같은 첨자**로 짝을 이룬다
                    for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
                        AuxNode& a = it->second;
                        if (a.fd == BAD_SOCK || !FD_ISSET(a.fd, &rd)) continue;
                        char b[1024];
                        int r = (int)recv(a.fd, b, sizeof(b), 0);
                        if (r < 0 && err_is_again(sockerr())) continue;   // 타임아웃 — 끊김이 아니다
                        if (r <= 0) {
                            // 🔴 **주 노드와 같은 낱말로 가른다**(위 `if (r <= 0)` 과 짝).
                            //   뭉치면 FIN·RST·오류가 한 줄로 나와서 **"깨끗이 닫았나"** 를 못 묻는다.
                            //   보조 노드에도 수락 시점에 `set_keepalive` 가 걸려 있으므로
                            //   여기서 ETIMEDOUT 이 **실제로 난다** — 뭉치면 그것까지 가려진다.
                            // ⚠ `keepalive_reaps` 는 **주 노드만** 센다(그대로 둔다). 여기서 같이 세면
                            //   그 수의 분모가 배포 시점에 조용히 넓어져 **전후 비교가 깨진다** —
                            //   보조 노드 몫은 이 줄의 `사유` 칸을 세면 된다.
                            const int e = (r == 0) ? 0 : sockerr();
                            dead_devs.push_back(it->first);
                            dead_why.push_back(sock_close_why(r, e));
                            continue;
                        }
                        a.buf.append(b, r);
                        size_t i;
                        while ((i = a.buf.find('\n')) != std::string::npos) {
                            std::string line = a.buf.substr(0, i);
                            a.buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (line.empty()) continue;
                            std::vector<std::string> f;
                            if (!verify_line(line, f)) { a.drops++; continue; }
                            a.frames++; a.last_ms = now_ms(); a.last_epoch_ms = epoch_ms();
                            // ── 🔴 **모든 프레임을 파서에 넣는다** — 이 노드도 동등한 노드다
                            //
                            // ⚠ `D` 만 넣고 `S`·`A` 를 버리는 구성도 있다. 그 제약의 이유는:
                            // `S` 분기가 **`slots[]`·자리 은퇴·하행 flush** 같은 서버 공유 상태를
                            // 건드려서, 둘째 보드의 점유가 **주차 자리를 덮었다.**
                            //
                            // 🔑 **그 조건이 지금 사라졌다:**
                            //   · 자리 갱신은 `sync_parking_slots_from_nodes()` 가 **결속표로 환산**한다
                            //   · 하행 flush 는 `flush_downq(n, …)` 로 **노드별 창**이 됐다
                            //   · 비트·시험·`seq`·`uptime` 은 전부 `Node` 안에 있다
                            // → 이제 `S` 를 넣어도 **그 노드의 모듈이 붙은 자리에만** 반영된다.
                            //
                            // ⚠ `A`(ACK)도 같이 넣는다. 안 넣으면 이 보드로 보낸 명령의 ACK 이
                            //   **영영 안 와서** 재전송 3회를 다 쓰고 `ack_timeout` 으로 죽는다 —
                            //   명령은 실제로 적용됐는데 화면에는 실패로 뜬다.
                            on_ard_line(a, line);
                            continue;
                        }
                        if (a.buf.size() > (size_t)MAX_LINE * 2) { a.drops++; a.buf.clear(); }
                    }
                    for (size_t k = 0; k < dead_devs.size(); k++) {
                        {   // 🔑 칸만 더한다(순수 추가). ⚠ 이 줄은 **끊기고 안 돌아온** 경우다 —
                            //   위 `재접속으로 대체` 와 **성격이 정반대**라 갈라 세야 한다.
                            const AuxNode& an = aux[dead_devs[k]];
                            const long long held = an.connected_ms ? (now_ms() - an.connected_ms) : -1;
                            logf("-AUX", "노드 " + dead_devs[k] + " 연결 종료 — 프레임 "
                                         + std::to_string(an.frames)
                                         + " · 버린줄 " + std::to_string(an.drops)
                                         + " · 지속 " + (held >= 0 ? hms(held) : std::string("?"))
                                         + " · 상대 " + (an.peer.empty() ? std::string("?") : an.peer)
                                         // 🔴 **맨 뒤에 붙인다** — 앞 칸 자리를 밀면 이미 도는 파서가 깨진다.
                                         + " · 사유 " + dead_why[k]);
                        }
                        if (aux[dead_devs[k]].fd != BAD_SOCK) closesock(aux[dead_devs[k]].fd);
                        // 🔴 **큐와 결속을 같이 놓는다.** 이 노드도 하행을 받으므로 남기면
                        //   갈 곳 없는 줄이 큐에 쌓이고, 결속이 남으면 **끊긴 보드가 자리를 계속 점유**한다.
                        clear_downq_for(dead_devs[k], "노드 연결 종료");
                        if (lot.unbindDevice(dead_devs[k]))
                            bump_epoch("노드 연결 종료 — 결속 해제 " + dead_devs[k]);
                        // 🔴 **상수를 보내지 마라.** 주 노드는 여기에 진짜 사유를 싣는데
                        //   보조 노드만 `"연결 종료"` 라는 **고정 문자열**을 실었다 —
                        //   같은 병이 로그 줄에 이어 **이벤트 층에서 한 번 더** 있었던 것이다.
                        //   ★ 소비자가 사유별로 세려 해도 보조 노드는 **전부 한 값**이라 셀 수가 없다.
                        emit_dev(DEV_DISCONNECT, dead_devs[k], dead_why[k]);
                        aux.erase(dead_devs[k]);
                    }
                }
                // 준비된 fd 를 먼저 모은 뒤 처리한다. 순회 중에 conns 를 건드리면
                // 반복자가 무효화된다 — 그대로 두면 SIGSEGV 다(실제로 겪었다).
                std::vector<sock_t> ready, drop;
                for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it)
                    if (FD_ISSET(it->first, &rd)) ready.push_back(it->first);

                for (size_t k = 0; k < ready.size(); k++) {
                    sock_t fd = ready[k];
                    if (!conns.count(fd)) continue;
                    Conn& c = conns[fd];               // map 의 참조는 erase 전까지 안정적이다
                    char b[4096];
                    int r = (int)recv(fd, b, sizeof(b), 0);
                    if (r < 0 && err_is_again(sockerr())) continue;   // 타임아웃 — 끊김이 아니다
                    if (r <= 0) { drop.push_back(fd); continue; }
                    c.inbuf.append(b, r);
                    if (c.kind == Conn::HTTP) {
                        if (!on_http(fd, c)) { drop.push_back(fd); continue; }
                    }
                    if (c.kind == Conn::WS && !ws_pump(fd, c)) drop.push_back(fd);
                }
                for (size_t k = 0; k < drop.size(); k++) {
                    if (conns.count(drop[k])) { closesock(drop[k]); conns.erase(drop[k]);
                                                conn_doc_.erase(drop[k]); choosers_.erase(drop[k]); }
                }
            }

            reap_dead();                              // 전송 실패로 표시된 연결을 여기서 정리
            reap_nodes();                             // id 미상 마감 + 보조 노드 회수 (REQ-0083)
            tick();                                   // 소켓이 조용해도 매 주기 돈다

            // ---------- 좀비 아두이노 소켓 회수 — 유휴 마감
            // 장치가 FIN 없이 죽으면 이 fd 는 영원히 안 돌아온다. keepalive 는 상대 커널이
            // 살아 ACK 를 보내면 **원리적으로 못 잡는다**(ESP 가 행에 걸린 바로 그 경우).
            // 그래서 앱이 직접 마감한다. 기준 시계는 ard_idle_base_ms() — 주석에 이유가 있다.
            if (ard != BAD_SOCK && sess_start_ms) {
                long long idle = now_ms() - ard_idle_base_ms();
                if (idle >= ARD_IDLE_CLOSE_MS) {
                    zombie_reaps++;
                    logf("✂", "아두이노 소켓 회수 — " + secs(idle) + " 무프레임(유휴 마감 "
                              + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초) · 누적 "
                              + std::to_string(zombie_reaps) + "회");
                    // 닫는 순서는 recv 실패 경로(위)와 **똑같이** 간다. 새 정리 경로를 만들면
                    // 한쪽만 고쳐지는 장부가 생긴다.
                    end_ard_session("유휴 마감 " + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초");
                    closesock(ard); ard = BAD_SOCK; ard_buf.clear();
                    emit_dev(DEV_DISCONNECT, park_dev,             // 단계 C
                             "유휴 마감 " + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초");
                }
            }
            // ---------- 온·오프라인 **엣지** 판정 (§3.4)
            // ⚠ 옛 코드는 `was_online = device_online()` 을 루프 맨 위에서 재고 맨 아래에서
            // 다시 재 비교했다. 두 시점의 간격이 마이크로초라 **3.5초 경계는 언제나 두 반복
            // '사이'에서 넘어간다** — 즉 엣지가 잡히는 반복이 존재하지 않았다.
            // 그래서 "링크는 열려 있는데 프레임만 끊긴" 경우(ESP 가 조용히 멈춘 바로 그 경우)
            // 오프라인 판정이 **한 번도 뜨지 않았다.** 소켓이 닫힐 때만 떴고, 그때는
            // device_online() 이 ard==BAD_SOCK 때문에 뒤집힌 것이라 판정이 아니라 부작용이었다.
            // 피해는 로그만이 아니다 — 이 자리의 push_snapshot() 이 브라우저에 "센서 끊김"을
            // 알리는 유일한 경로인데, 장치가 조용하면 다른 이벤트도 없어서
            // **화면이 마지막 상태를 계속 진짜처럼 보여 준다.**
            // 상태를 멤버로 들고 비교하는 평범한 엣지 검출로 바꾼다. 복귀도 대칭으로 찍는다.
            bool now_online = device_online();
            if (now_online != ard_online) {
                ard_online = now_online;
                if (now_online) {
                    // ---------- 복구시간 확정
                    // 복구시간 = **오프라인이 시작된 순간 → 다음 프레임이 실제로 도착한 순간.**
                    // 끝점을 now_ms() 가 아니라 `ard_last_ms`(그 프레임의 수신 시각)로 잡는다 —
                    // 엣지는 select 주기(200ms) 뒤에 잡히므로 now 로 재면 매번 그만큼 부풀려진다.
                    std::string extra;
                    if (offline_since_ms) {
                        long long r = ard_last_ms - offline_since_ms;
                        if (r < 0) r = 0;
                        // 같은 연결에서 되살아났나, 재연결해서 되살아났나 — **이 구분이
                        // 장치 쪽 복구 사다리가 듣는지를 판정한다.** 합쳐 세면
                        // "링크가 잠깐 조용했다" 와 "소켓을 새로 세워야 했다" 가 같아 보인다.
                        bool same = (ard_sessions == offline_at_session);
                        if (same) recov_same_conn++; else recov_reconn++;
                        if (r > recov_worst_ms) recov_worst_ms = r;
                        if (recov_ms.size() < RECOV_SAMPLE_MAX) recov_ms.push_back(r);
                        else recov_dropped++;             // 조용히 버리지 않는다 — 요약에 적힌다
                        extra = " — 복구 " + secs(r) + (same ? "(같은 연결)" : "(재연결)");
                        offline_since_ms = 0;
                    }
                    logf("=", "아두이노 온라인 복귀" + extra);
                } else {
                    offline_episodes++;                   // 소크 관측(REQ-0065)
                    // 오프라인이 **시작된** 시각을 잡는다. 엣지가 잡힌 시각이 아니다.
                    //   · 3.5초 무프레임으로 뒤집힌 경우 → 마지막 프레임 + OFFLINE_MS 가 그 순간
                    //   · 소켓이 닫혀 뒤집힌 경우       → 지금이 그 순간(미래 값을 쓰면 안 되므로 min)
                    long long t = now_ms();
                    long long off_at = ard_last_ms ? (ard_last_ms + OFFLINE_MS) : t;
                    offline_since_ms = (off_at < t) ? off_at : t;
                    offline_at_session = ard_sessions;
                    logf("!", "아두이노 오프라인 판정(" + std::to_string(OFFLINE_MS)
                              + "ms 무프레임) — 누적 " + std::to_string(offline_episodes) + "회");
                }
                // ⚠ **파일 쓰기를 여기서 반드시 해야 한다**(§9.4 개정 9). 다른 두 호출 지점은
                // 둘 다 on_ard_line 안이라 **장치가 조용하면 아예 실행되지 않는다.**
                // 이 줄이 없으면 `device.online` 은 키에 넣어도 영영 false 로 기록되지 못한다 —
                // 필드가 **가장 필요한 순간에** 거짓말을 한다.
                // 단계 C: 직접 호출 → 이벤트. **소비자가 같은 틱에 같은 일을 한다**(아래 drain).
                emit_dev(now_online ? DEV_ONLINE : DEV_OFFLINE, park_dev,
                         now_online ? "프레임 복귀" : "3.5초 무프레임");
            }

            // ---------- 이음매 소비
            // **같은 틱 안에서** 소비한다 — 디바이스가 이번 반복에 낸 이벤트는 이번 반복에
            // 도메인이 처리한다. 다음 틱으로 미루면 최대 200ms 가 밀리고, 그건 옮기기 전과
            // 다른 동작이다. 단계 C 는 구조만 바꾸고 동작은 안 바꾼다.
            drain_dev_events();

            // 주기 보고 — **조용한 로그와 죽은 서버를 구별할 수 있게** 한다.
            // 이 줄이 없으면 "2시간 동안 아무 일 없었다"와 "1분 만에 멈췄다"가 같은 모양이다.
            if (now_ms() - last_report_ms >= SOAK_REPORT_MS) {
                last_report_ms = now_ms();
                // 🔴🔴 **이 줄을 지우지 마라.** 여기가 비어 있던 동안
                //   `soak_line()` 은 **호출자 0** 이었다 — 정의만 있고 아무도 안 불렀다.
                //   ★ 인라인 함수라 **코드 자체가 안 생겨서** 산출물에 그 문자열조차 없었고,
                //     로그 34개 전수에 `소크 ` 가 **0건**이었다. 옛 트리에는 있었다
                //     (`조별과제샘플/vs_win_server.cpp:3123`) — **이식하며 끊겼다.**
                //   🔑 그동안 요약의 계수 **백 개**가 전부 `0/0`(관측자 없음)이었고,
                //     그것이 *"그 상황이 아직 안 왔다"* 로 잘못 읽혔다.
                //
                // 🔴🔴 **값이 `0` 인 칸을 생략해서 줄을 줄이지 마라.**
                //   줄이 길어 보여서 반드시 그 생각이 든다. 그런데 **`0` 을 빼면
                //   `0`(정상)과 `없음`(못 잼)이 같은 모양이 된다** — 이 줄이 막으려는 바로 그것이다.
                //   ★ **이 줄이 긴 이유가 이 줄의 값이다.** 계수가 매번 다 나오는 것이 요점이다.
                //   ✅ 줄이고 싶으면 **주기를 늘려라**(`SOAK_REPORT_MS`). **칸을 빼지 마라.**
                //
                // 🔑 용량 산수(2026-08-27) : 소크 1,300자 × 60/시 ≈ **1.9MB/일**.
                //   같은 배포의 `←ARD` 요약이 26MB/일 → 약 1.3MB/일로 줄인다 → 합계 약 3.2MB/일.
                //   **줄일 것을 줄이고 볼 것을 켠 것**이지 용량을 늘린 것이 아니다.
                logf("⏱", soak_line());
            }
        return !g_stop;
    }

