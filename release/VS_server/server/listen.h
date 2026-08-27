// listen.h — 소켓 준비 — 세 포트 바인드·리슨·기동 로그 (openPorts). `struct Server` 의 몸통 조각 — server.cpp:189
// ⚠ 단독 컴파일 불가 · include 자리가 곧 문법이다 · 📖 server.cpp "목차"

    // ── 함수 ──────────────────────────────────────────────────────────────
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

    // ── 🔴 `run()` 을 셋으로 갈랐다
    //   호출자가 **바꿀 수 있는 것**(언제 열지 · 어떻게 돌지)을 밖으로 낸다.
    //   한 박자 **안**의 순서는 못 바꾸므로 안에 남는다 — 우리 판별자 그대로다.
    //   ⚠ 기계적 분할이다. 루프 몸통에 최상위 `continue`/`break`/`return` 이 **하나도 없는 것**을
    //     먼저 확인하고 옮겼다 — 있으면 의미가 조용히 바뀐다.
    // 🔑 **`offset=` 을 대신한다** — monitor 가 보던 것은 *"이게 시험 인스턴스인가"* 였다.
    //   포트를 손으로 지정했는지로 그것을 답한다.
    static bool ports_are_default() {
        return g_port_ardu == PORT_ARDUINO && g_port_web == PORT_HTTP && g_port_cam == PORT_PHONE;
    }
    bool openPorts() {
        lsn_ard   = listen_on(g_port_ardu);
        lsn_http  = listen_on(g_port_web);
        lsn_phone = listen_on(g_port_cam);
        // 🔴 **이용자용 두 포트 — 못 열려도 죽지 않는다.**
        //   ★ 이것이 *"8080·8081 이 죽어도 주차장은 돈다"* 의 구현이다.
        //     위 셋과 달리 여기서 `return 1` 을 하지 않는다는 것이 그 뜻 전부다.
        lsn_find_   = listen_on(PORT_USER_FIND);
        lsn_choose_ = listen_on(PORT_USER_CHOOSE);
        if (lsn_find_ == BAD_SOCK)
            std::cout << "  ⚠ 이용자 포트 " << PORT_USER_FIND
                      << " 를 못 열었다 — 주차위치 확인 화면만 못 쓴다. 주차장은 그대로 돈다\n";
        if (lsn_choose_ == BAD_SOCK)
            std::cout << "  ⚠ 이용자 포트 " << PORT_USER_CHOOSE
                      << " 를 못 열었다 — 자리 선택 화면만 못 쓴다. 자동 배정으로 돈다\n";
        // 🔴 포트를 못 잡았으면 **그 사실을 로그에 남기고** 죽는다.
        // 08-16 에 `프레임 0` 짜리 짧은 인스턴스가 여럿 있었는데, 관측자가 그것이
        // "장치가 안 붙은 것"인지 "포트를 못 잡은 것"인지 **가를 수 없어서** 한참 헤맸다.
        // 0 이 "나쁨"인지 "해당 없음"인지 가르는 것 — 그게 관측의 절반이다.
        if (lsn_ard == BAD_SOCK || lsn_http == BAD_SOCK || lsn_phone == BAD_SOCK) {
            // 🔴 dev 는 기계용 경계 줄을 안 찍는다. **왜 못 떴는지만** 사람 말로 말한다.
            std::cout << "\n🔴 포트를 못 잡았다. 이미 누가 쓰고 있거나 권한이 없다.\n"
                      << "   웹 " << g_port_web << " · 아두이노 " << g_port_ardu
                      << " · 카메라 " << g_port_cam << "\n"
                      << "   확인 : lsof -nP -iTCP:" << g_port_web << " -sTCP:LISTEN\n"
                      << "   다른 포트로 : ./srv --port-web=<값> --port-ardu=<값> --port-cam=<값>\n\n";
            // 🔴 **`bool` 함수다. `1` 은 `true` — "떴다"로 읽힌다.**
            //   필수 포트를 못 잡은 것은 **기동 실패**여야 한다.
            return false;
        }

        // 🔴 **dev 는 기계용 경계 줄(`=== INSTANCE … logfmt=`)을 안 찍는다.**
        //   그건 monitor 가 한 파일에 쌓인 인스턴스를 자르려고 읽는 줄이다.
        //   개발자는 터미널 하나를 보고 있고 **자를 것이 없다.**
        if (!ports_are_default())
            std::cout << "  ⚠ 포트를 손으로 지정했다 (dev 기본: 웹 " << PORT_HTTP
                      << " · 아두이노 " << PORT_ARDUINO << " · 카메라 " << PORT_PHONE << ")\n";
        std::cout << "\n  개발용 주차 서버  (pid " << cur_pid() << ")\n"
                  << "  화면 http://127.0.0.1:" << g_port_web << "/\n"
                  << "  아두이노 TCP " << g_port_ardu << "  ·  카메라 " << g_port_cam << "\n"
                  << "  ─────────────────────────────────────────\n";
        std::cout.flush();
        // 🔴 **뿌리를 여기서 얼린다** — 이 줄 뒤로는 cwd 가 움직여도 실체가 안 바뀐다.
        //   ⚠ `ensure_log_exists()` **앞**이어야 한다(그것이 첫 소비자다).
        // 🔴🔴 **cwd 로 정하지 않는다**(REQ-0497 · 2026-08-27). 순서는 이렇다:
        //   ① `--web-root=` 를 줬으면 그것   ② 없으면 **실행파일 위치 기준 `../web`**
        //   ★ ②가 릴리즈 배치다 : `릴리즈/server/srv` · `릴리즈/web/*.html` → 폴더를 통째로
        //     옮겨도 **상대 관계가 유지된다**(web 이 짚은 값).
        //   🔴 **cwd 로 되돌아가는 기본값을 두지 마라** — 그러면 *"어디서 띄웠느냐"* 가 화면을 정한다.
        //     ⚠ 2026-08-27 새벽에 그 성질로 배포 도구가 **저장소 안 화면을 덮었다**(web 실측).
        //   ⚠ 개발 배치는 바이너리가 `/tmp` 에 있어 ②가 안 맞는다 →
        //     `net/run/start.sh` 가 **`--web-root` 를 명시로 넘긴다.** 그게 정상 경로다.
        if (g_docroot.empty()) g_docroot = exe_sibling_dir("web");
        if (g_dataroot.empty()) g_dataroot = g_docroot;   // 안 주면 종전대로 한 뿌리
        {
            // 🔑 **뿌리와 그 안의 화면 파일을 값으로 찍는다.**
            //   ⚠ `:9900` 이 낡은 화면을 내줘도 **로그에 흔적이 안 남는다** —
            //   `ls` 도 `curl 200` 도 통과했다. §"존재형은 유물을 통과시킨다".
            //   → 크기·시각은 약한 신원이지만 **없는 것보다 낫고**, 강한 신원은
            //     화면이 싣는 `screen-build` 가 따로 찍는다(serve_file).
            const std::string idx = doc_path("index.html");
            struct stat st;
            if (stat(idx.c_str(), &st) == 0) {
                char tb[32]; time_t mt = (time_t)st.st_mtime;
                strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", localtime(&mt));
                char b[512];
                snprintf(b, sizeof(b), "정적 뿌리 %s · index.html %lldB · 수정 %s",
                         g_docroot.c_str(), (long long)st.st_size, tb);
                logf("=", b);
            } else {
                logf("!", "🔴 정적 뿌리 " + g_docroot
                          + " 에 **index.html 이 없다** — 화면 요청이 전부 404 가 된다. "
                            "서버를 그 파일이 있는 디렉터리에서 띄워라");
            }
        }
        ensure_log_exists();
        // 🔴 **dev/VS 는 카메라 대장을 파일로 남기지 않는다** — `shots_load()` 를 안 부른다.
        //   근거 : 이 판은 `rid` 커서도 영속하지 않는다(위 `openPorts` 에 그 호출이 없다).
        //   🔑 **개발판이 운영 대장을 덮어쓰는 것**이 우리가 `rid` 에서 이미 데인 자리라,
        //     "혹시 몰라 켜 두는" 쪽을 안 고른다.
        //   ⚠ 그래서 **재기동하면 요청번호가 오늘치 1번부터 다시 나간다.** 조용하지 않게 말한다.
        std::cout << "  카메라 요청 대장 : 메모리에만 둔다(재기동하면 요청번호가 되풀이된다)\n";

        // 소크 관측 — 기동 시각과 "아직 링크 없음" 상태를 장부에 연다
        soak_start_ms = now_ms();
        last_report_ms = soak_start_ms;
        link_down_since = soak_start_ms;
        // 🔑 **서버가 자기 계약값을 기동 시 찍는다** (web 지적)
        // web: *"서버 값 변화를 잡을 수 있는 검사가 팀에 없다"* — 하니스가 값을 주입하고
        // 같은 값으로 단언하므로 **서버가 바뀌어도 빨간불이 안 된다.** 그렇다고 특정 값을 박으면
        // **고친 쪽이 벌을 받는 검사**가 된다.
        // → **검사로 막지 말고 관측 가능하게 만든다.** 그러면 "지금 서버가 보내는 값"이
        //   남의 원장 사본이 아니라 **로그에 남는 사실**이 된다.
        {
            // 🔴 운영판은 여기서 **계약값 덤프 두 줄(289자·188자)** 을 찍는다.
            //   arduino·monitor 가 계산에 쓰는 값이다. **개발자는 안 쓴다.**
            std::cout << "  묶음 상한 " << max_per_batch() << "건  ·  ACK 대기 "
                      << (ACK_TIMEOUT_MS / 1000.0) << "초 × " << ACK_MAX_TRIES << "회\n";
            init_srv_id();
            // 🔴 **여기서 부르지 않으면 자리가 비어 있고 `map` 이 빈 배열로 나간다.**
            //   첫 배포에서 실제로 그랬다 — **자가검증에서만 부르고 있었다.**
            //   ⚠ 자가검증이 스스로 지형을 만들어 쓰기 때문에 **시험은 전부 통과한다.**
            //   **"시험 경로 ≠ 실기 경로"의 가장 조용한 형태다.**
            build_default_zones();
            if (false) logf("=", "서버 인스턴스 id — " + srv_id
                      + " (🔑 `epoch` 는 **이 id 안에서만** 단조다. id 가 바뀌면 판을 비교하지 마라)");
        }
        // 🔴 소크 관측(60초 요약)은 **운영 관측**이다. 콘솔에서는 잡음이라 안 켠다.

        return true;
    }

    void closeDown() {

        // ---------- 소크 종료 요약 — 한 줄로 끝난다
        if (sess_start_ms) end_ard_session("서버 종료");
        // 🔴 소크 총평(1,329자)은 **운영 관측**이다. 콘솔에서 못 읽는다.
        // 🔴🔴 **굽기 실수 계수는 여기서 낸다** — 소크 요약(`soak_line`)은 **dev 판에서
        //   일부러 안 찍는다**(serve.h: "콘솔에서 그 줄은 못 읽고 프레임 로그를 밀어낸다").
        //   ⚠ 그래서 그쪽에만 넣으면 **영원히 안 보인다** — 계수를 만들고 볼 자리를 안 만든 것이 된다.
        //   🔑 **0 이면 안 찍는다.** 정상일 때 한 줄도 안 늘고, 뜨면 그것 자체가 신호다.
        if (mod_claimed_other_ > 0 || adopt_replace_n_ > 0) {
            std::cout << "\n  🔴 굽기 확인 — 남의이름결속 " << mod_claimed_other_
                      << " · devid대체 " << adopt_replace_n_
                      << "(상대 " << (long long)adopt_peers_.size() << "종)"
                      << "\n     🔑 상대가 **2종 이상**이면 같은 devid 를 **두 보드**에 구운 것이다."
                         " 1종이면 그 보드의 재접속이다"
                      << "\n     ⚠ 최종 판정은 로그 원문의 **왕복 패턴**이다 — 옛IP→새IP 한 번은 DHCP 갱신일 수 있다\n";
        }
        // 🔴 **`sess_frames` 를 여기 쓰지 마라. 그건 *현 세션* 수신 줄이다**(`nodes.h:41` 에서 리셋된다).
        //   아래 주석이 *"이 한 줄로 인스턴스 총계가 복원된다"* 고 말하는데, 세션 값을 실으면
        //   **세션 2회짜리 인스턴스에서 첫 세션 몫이 통째로 사라진다** — 읽는 쪽은 그것을 총계로 읽는다.
        //   ★ 그래서 **둘 다** 싣고 **어느 것이 무엇인지 이름으로 밝힌다.**
        // 🔵 **소크 종료 요약** — 옛 트리(`조별과제샘플/vs_win_server.cpp:3129`)에 있던 것이다.
        //   ⚠ 주기 요약은 60초마다라 **마지막 최대 60초분이 어디에도 안 남는다.** 이 줄이 그것을 닫는다.
        //   🔴 아래 `서버를 닫는다` 의 두 값만으로는 **인스턴스 총계가 복원되지 않는다** — 계수가 백 개다.
        //
        // 🔴🔴 **이 줄을 `서버를 닫는다` 뒤로 옮기지 마라. 종결 표지가 마지막이어야 한다.**
        //   ★ 처음에 뒤에 뒀다가 monitor 가 잡았다 — 그의 정상종료 판정이 **마지막 줄**을 보는데
        //     긴 요약이 뒤에 붙으면 종결 표지를 못 보고 **"말없이 끊겼다"** 로 뒤집힌다.
        //     🔑 그는 창을 20줄로 넓혀 자기 쪽을 고쳤지만, **사람이 `tail` 로 보는 경우**와
        //     아직 없는 도구들이 남는다. **긴 것이 먼저, 종결이 마지막**이 안전한 순서다.
        std::cout << "\n  ▣ 소크 종료 · " << soak_line() << "\n";
        std::cout << "  서버를 닫는다 — 장치 세션 " << ard_sessions
                  << "회 · 프레임 누적 " << all_frames
                  << "(현 세션 " << sess_frames << ")\n";

        // ---------- 인스턴스 종료 경계 (로그 계약 §2.3)
        // ⚠ **이 줄이 없다고 "아직 살아 있다"로 읽으면 안 된다.** SIGKILL·정전·패닉은
        // 이 줄을 남길 기회를 주지 않는다. 실제로 08-16 에 pid 36998 이 이 줄 없이 죽었다.
        // **생존 판정은 로그가 아니라 pid 로 해야 한다.** 이 줄은 "정상 종료였다"만 증명한다.
        // 누계를 이 줄에 싣는다 — **로그 뒤쪽이 잘려도 이 한 줄로 인스턴스 총계가 복원된다.**
        // ⚠ 그러려면 **누적값을 실어야 한다.** 위에서 `all_frames` 로 고친 이유다 —
        //   전에는 `sess_frames`(현 세션)를 싣고 "총계" 라고 적어 뒀다.
        //
        // 🔴 **`dev` 라는 갈래는 존재하지 않는다.** 전에 여기 *"dev 는 기계용 종료 줄을 안 찍는다"* 가
        //   적혀 있었는데 **그런 플래그가 이 트리에 없다**(`grep` 0건). 같은 문구가 `serve.h` 의
        //   주기 보고에도 있었고, 그쪽은 **아무것도 안 찍는 상태를 그 문구가 정당해 보이게** 만들고
        //   있었다(요약이 통째로 죽어 있었다). ★ **없는 조건을 설명하는 주석을 쓰지 마라** —
        //   읽는 사람이 *"다른 모드에서는 찍히는구나"* 로 읽고 **검산을 멈춘다.**
    }
