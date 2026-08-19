// listen.h — 소켓 준비 — 세 포트 바인드·리슨·기동 로그 (openPorts). `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 소켓 준비
    sock_t listen_on(int port) {
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == BAD_SOCK) return BAD_SOCK;
        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons((u_short)port);
        if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
            std::cerr << "바인드 실패 포트 " << port << " (err " << sockerr() << ")\n";
            closesock(s); return BAD_SOCK;
        }
        if (listen(s, 8) != 0) { closesock(s); return BAD_SOCK; }
        return s;
    }

    // ── 🔴 `run()` 을 셋으로 갈랐다 (REQ-0272 1단계 · 2026-08-19)
    //   호출자가 **바꿀 수 있는 것**(언제 열지 · 어떻게 돌지)을 밖으로 낸다.
    //   한 박자 **안**의 순서는 못 바꾸므로 안에 남는다 — 우리 판별자 그대로다.
    //   ⚠ 기계적 분할이다. 루프 몸통에 최상위 `continue`/`break`/`return` 이 **하나도 없는 것**을
    //     먼저 확인하고 옮겼다 — 있으면 의미가 조용히 바뀐다.
    bool openPorts() {
        lsn_ard   = listen_on(PORT_ARDUINO + g_port_offset);
        lsn_http  = listen_on(PORT_HTTP    + g_port_offset);
        lsn_phone = listen_on(PORT_PHONE   + g_port_offset);
        // 🔴 포트를 못 잡았으면 **그 사실을 로그에 남기고** 죽는다.
        // 08-16 에 `프레임 0` 짜리 짧은 인스턴스가 여럿 있었는데, 관측자가 그것이
        // "장치가 안 붙은 것"인지 "포트를 못 잡은 것"인지 **가를 수 없어서** 한참 헤맸다.
        // 0 이 "나쁨"인지 "해당 없음"인지 가르는 것 — 그게 관측의 절반이다.
        if (lsn_ard == BAD_SOCK || lsn_http == BAD_SOCK || lsn_phone == BAD_SOCK) {
            std::cout << "=== INSTANCE"
                      << " logfmt=" << LOG_FORMAT_VERSION
                      << " pid=" << cur_pid()
                      << " start=" << iso8601(epoch_ms())
                      << " bin=" << exe_path()
                      << " build=" << BUILD_ID
                      << " ports=" << (PORT_ARDUINO + g_port_offset)
                      << ","      << (PORT_HTTP    + g_port_offset)
                      << ","      << (PORT_PHONE   + g_port_offset)
                      << " offset=" << g_port_offset
                      << " cwd=" << cur_cwd()
                      << " log=" << (g_log_path.empty() ? std::string("(화면만)") : g_log_path)
                      << " ===" << std::endl;
            std::cout << "=== INSTANCE-END"
                      << " logfmt=" << LOG_FORMAT_VERSION
                      << " pid=" << cur_pid()
                      << " stop=" << iso8601(epoch_ms())
                      << " reason=port_bind_fail"
                      << " frames=0 sessions=0"
                      << " ===" << std::endl;
            return 1;
        }

        // ---------- 인스턴스 경계 줄 (REQ-0111 로그 계약 §2.2) — 배너보다 **먼저** 나간다.
        // 기계가 읽는 줄이다. 한 파일에 인스턴스가 여러 개 쌓여도 이 줄로 자르면 섞이지 않는다.
        // 실제 리슨에 성공한 뒤에 찍으므로 ports= 는 **추측이 아니라 사실**이다.
        std::cout << "=== INSTANCE"
                  << " logfmt=" << LOG_FORMAT_VERSION
                  << " pid=" << cur_pid()
                  << " start=" << iso8601(epoch_ms())
                  << " bin=" << exe_path()
                  << " build=" << BUILD_ID
                  << " ports=" << (PORT_ARDUINO + g_port_offset)
                  << ","      << (PORT_HTTP    + g_port_offset)
                  << ","      << (PORT_PHONE   + g_port_offset)
                  << " offset=" << g_port_offset
                  << " cwd=" << cur_cwd()
                  << " log=" << (g_log_path.empty() ? std::string("(화면만)") : g_log_path)
                  << " ===" << std::endl;

        if (g_port_offset)
            std::cout << "*** 시험 인스턴스 — 포트 +" << g_port_offset
                      << " 이동됨. 운영이 아니다(REQ-0072 이음매) ***\n";
        std::cout << "주차 관제 서버 — 아두이노 TCP " << (PORT_ARDUINO + g_port_offset)
                  << " · HTTP/WS " << (PORT_HTTP + g_port_offset)
                  << " · 폰(digitcam) " << (PORT_PHONE + g_port_offset) << "\n"
                  << "명세: docs/net/parking-protocol.md\n"
                  << "-----------------------------------------------------------\n";
        std::cout.flush();
        ensure_log_exists();

        // 소크 관측(REQ-0065) — 기동 시각과 "아직 링크 없음" 상태를 장부에 연다
        soak_start_ms = now_ms();
        last_report_ms = soak_start_ms;
        link_down_since = soak_start_ms;
        // 🔑 **서버가 자기 계약값을 기동 시 찍는다** (web 지적 2026-08-18)
        // web: *"서버 값 변화를 잡을 수 있는 검사가 팀에 없다"* — 하니스가 값을 주입하고
        // 같은 값으로 단언하므로 **서버가 바뀌어도 빨간불이 안 된다.** 그렇다고 특정 값을 박으면
        // **고친 쪽이 벌을 받는 검사**가 된다.
        // → **검사로 막지 말고 관측 가능하게 만든다.** 그러면 "지금 서버가 보내는 값"이
        //   남의 원장 사본이 아니라 **로그에 남는 사실**이 된다.
        {
            char cb[288];
            snprintf(cb, sizeof(cb),
                     "계약값 — ACK_TIMEOUT %dms(=2x슬롯) · ACK_MAX_TRIES %d · "
                     "ack_budget_ms %lld · 큐대기마감 %dms · cap %dB/%d건 · "
                     "사용자큐상한 %d건 · 인수유예 %dms(=5x슬롯 · **IP 다를 때만**) · 노드잠금 %s · W_srv %lldms",
                     ACK_TIMEOUT_MS, ACK_MAX_TRIES, ack_budget_ms(),
                     DOWNQ_WAIT_CAP_MS, DOWN_BATCH_CAP_B, DOWN_BATCH_MAX_N,
                     DOWN_BATCH_MAX_N * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS),
                     TAKEOVER_GRACE_MS,
                     g_park_dev_pin.empty() ? "(none, first-S-wins)" : g_park_dev_pin.c_str(),
                     w_srv());
            logf("=", cb);
            // 🔴 A[1] — **폭 계약을 기동 로그에 값으로 낸다.** 남의 원장 사본이 아니라
            //   "지금 이 서버가 쓰는 값"이 로그에 남아야 arduino 가 `ACK_worst` 를 다시 계산할 수 있다.
            {
                char rb[224];
                snprintf(rb, sizeof(rb),
                         "rid 계약 — 공간 %u([0,%u] · 최대 %d자리) · 격리 %lldms(=ACK_TIMEOUT×ACK_MAX_TRIES) "
                         "· 장치 멱등창 %d (arduino §25.3)",
                         (unsigned)RID_SPACE, (unsigned)(RID_SPACE - 1),
                         (int)std::to_string((unsigned)(RID_SPACE - 1)).size(),
                         RID_QUARANTINE_MS, DEV_RID_CACHE_N);
                logf("=", rb);
            }
            // 🔴 **여기서 부르지 않으면 커서가 안 이어진다.** 생성자에 두지 않은 것은 일부러다 —
            //   자가검증이 만드는 `Server` 들이 실기 커서 파일을 덮어쓰지 못하게 하려는 것이다.
            //   ⚠ 이 한 줄이 빠지면 **아무 경고 없이** 매 기동이 1에서 시작한다(=옛 거동).
            //   그래서 기동 로그에 값을 찍는다 — 빠진 것이 로그의 *부재*로 보이게.
            // 🔴 **정적 자원·영속 상태의 해석된 절대경로를 찍는다** (REQ-0248 · 2026-08-19)
            //
            //   `serve_file()`(index.html)과 `data_log.json` 이 **cwd 상대**다.
            //   2026-08-19 에 그 때문에 `:9900` 이 이틀 된 화면을 내줬고 **로그에 흔적이 없어**
            //   "무엇을 열고 있나"를 사후에 못 물었다 — 반나절이 들었다.
            //   🔑 `cwd=` 는 경계 줄에 이미 찍는다. 그런데 **cwd 를 안다고 경로를 아는 것은 아니다** —
            //     읽는 사람이 머릿속에서 이어 붙여야 한다. **서버가 이어 붙여서 찍는다.**
            //   ⚠ 파일이 없어도 경로는 찍는다 — 그게 곧 "왜 404 인가"의 답이다.
            {
                char pb[512];
                snprintf(pb, sizeof(pb), "정적 자원 — index.html %s%s · data_log.json %s",
                         abs_path("index.html").c_str(),
                         std::ifstream("index.html").good() ? "" : " 🔴(없다 — GET / 는 404 다)",
                         abs_path("data_log.json").c_str());
                logf("=", pb);
            }
            rid_cursor_load();
            ledger_load();
            // 🔴 **조건을 적었으면 그것을 보는 감시를 같은 자리에 만든다.**
            //   재사용 주기가 장치 멱등창(16)에 가까워지면 명령이 **조용히 삼켜진다.**
            //   ⚠ 여유 4배는 임의로 고른 문턱이다 — **관측용이지 증명이 아니다.**
            if ((int)RID_SPACE <= DEV_RID_CACHE_N * 4) {
                logf("!", "🔴 rid 공간이 장치 멱등창의 4배 이하다 — 재사용이 캐시 안에서 일어날 수 있다."
                          " 명령이 조용히 삼켜진다(arduino §25.3). 폭을 줄인 사람이 이 줄을 읽어야 한다");
            }
            init_srv_id();
            // 🔴 **여기서 부르지 않으면 자리가 비어 있고 `map` 이 빈 배열로 나간다.**
            //   첫 배포에서 실제로 그랬다 — **자가검증에서만 부르고 있었다.**
            //   ⚠ 자가검증이 스스로 지형을 만들어 쓰기 때문에 **시험은 전부 통과한다.**
            //   **"시험 경로 ≠ 실기 경로"의 가장 조용한 형태다.**
            build_default_zones();
            logf("=", "서버 인스턴스 id — " + srv_id
                      + " (🔑 `epoch` 는 **이 id 안에서만** 단조다. id 가 바뀌면 판을 비교하지 마라)");
        }
        logf("⏱", "소크 관측 시작 — " + std::to_string(SOAK_REPORT_MS / 1000) + "초마다 요약, 종료(Ctrl-C) 시 한 줄 총평");

        return true;
    }

    void closeDown() {

        // ---------- 소크 종료 요약 (REQ-0065) — 한 줄로 끝난다
        if (sess_start_ms) end_ard_session("서버 종료");
        logf("▣", "소크 종료 · " + soak_line());

        // ---------- 인스턴스 종료 경계 (REQ-0111 로그 계약 §2.3)
        // ⚠ **이 줄이 없다고 "아직 살아 있다"로 읽으면 안 된다.** SIGKILL·정전·패닉은
        // 이 줄을 남길 기회를 주지 않는다. 실제로 08-16 에 pid 36998 이 이 줄 없이 죽었다.
        // **생존 판정은 로그가 아니라 pid 로 해야 한다.** 이 줄은 "정상 종료였다"만 증명한다.
        // 누계를 이 줄에 싣는다 — **로그 뒤쪽이 잘려도 이 한 줄로 인스턴스 총계가 복원된다.**
        std::cout << "=== INSTANCE-END"
                  << " logfmt=" << LOG_FORMAT_VERSION
                  << " pid=" << cur_pid()
                  << " stop=" << iso8601(epoch_ms())
                  << " reason=normal"
                  << " frames=" << all_frames
                  << " sessions=" << ard_sessions
                  << " ===" << std::endl;
    }
