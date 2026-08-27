// metrics.h — 소크 관측 · 복구 계측 · 지표 문장. `struct Server` 의 몸통 조각 — server.cpp:179
// ⚠ 단독 컴파일 불가 · include 자리가 곧 문법이다 · 📖 server.cpp "목차"


    // ── 함수 ──────────────────────────────────────────────────────────────
    // ⚠ `srv_id` 는 **기동마다 달라야 한다.** 벽시계 + 소스 식별자를 쓴다 —
    //   시각만 쓰면 같은 초에 두 번 뜨면 겹치고, 소스 식별자만 쓰면 재기동이 구분 안 된다.
    void init_srv_id() {
        char b[96];
        snprintf(b, sizeof(b), "%s-%lld", BUILD_ID, (long long)epoch_ms());
        srv_id = b;
    }

    // 🔑 `park` 의 필드들은 **`Node` 의 생성자가 초기화한다.** 여기 목록에 다시 적지 않는다 —
    //   적으면 참조에 임시값을 묶으려는 것이 되어 컴파일이 안 된다(그게 이 설계의 안전장치다).
    Server() : grid_rows(5), grid_cols(5),   // 선언 순서(-Wreorder)
               getmap_win_ms(0), getmap_in_win(0), getmap_rejects(0),
               lsn_ard(BAD_SOCK), lsn_http(BAD_SOCK), lsn_phone(BAD_SOCK),
               // 🔑 `phones` 무리와 **같은 자리**다 — 선언 순서를 따른다(`-Wreorder-ctor`)
               phone_epoch_(0), phone_max_(0), phone_hold_max_ms_(0), phone_hold_last_ms_(0),
               reg_ok(0), reg_bad(0), reg_qsent(0), reg_giveups(0), reg_widthbad(0),
               occ_undecoded(0), occ_undecoded_warned(false),
               mod_order_changed(0),
               gate_q(0), gate_sent(0), gate_ans(0), dev_reject(0),
               heal_checks(0), heal_fires(0), res_undecoded(0), res_undecoded_warned(false),
               dup_devid_reject(0), takeover_grace(0),  // 선언 순서와 일치시킨다(-Wreorder)
               admit_rejects(0),
               // 🔴 이 여섯(+다섯)은 **선언만 돼 있고 초기화 목록에 없었다.** 쓰기 시작하는 순간
               // 쓰레기값이 지표로 나간다 — 원장이 통째로 경고하는 그 형태다. 여기서 닫는다.
               downq_bytes(0), batch_count(0), batch_lines(0),
               q_rejected(0), q_full_n(0), s_max_b(0), s_worst_warned(false),
           q_nodev(0), q_dup(0),
               q_deferred(0), q_dropped_link(0),
               rtt_max_ms(0), rtt_last_ms(0), rtt_n(0), win_skips(0), dmax_flushes(0),
               ard_uptime(-1), ard_seq(-1),
               ard_dev("?"),
               xs_uptime(-1), xs_last_ms(0), xs_dev(""),
               xs_reconnect_reboot(0), xs_reconnect_link(0),
               xs_reconnect_unknown(0),
               rid_cursor(1), rid_reserved_to(0), rid_persist_on(false),
               // 🔑 선언 순서와 **같은 순서**여야 한다(state.h · `-Wreorder-ctor`)
               shot_day_(0), shot_seq_(0), shots_persist_on_(false),
               shot_issued_(0), shot_answered_(0), shot_failed_(0),
               shot_exhausted_(0), shot_evicted_(0), shot_orphan_(0), shot_sentinel_(0),
               shot_multiphone_(0), shot_nophone_(0), prev_eframes_(0), prev_evalues_(0),
               evalues_(0), emiss_(0),
               rid_rel_seq(0),
               rid_alloc_n(0), rid_skips(0), rid_forced(0), rid_exhausted(0),
               ack_unknown_rid(0), ack_slot_mismatch(0), mod_name_conflict(0), mod_dup_name(0), mod_unbound(0), mod_claimed_other_(0), rcvto_logged_(false), adopt_replace_n_(0), mod_seen_(0), asm_warn_(0), batch_seq_(0), batch_deferred(0), devid_example_(0), sensor_split_now(0), sensor_split_max_(0), not_reservable_n(0),
               base_valid(false), test_armed(false),
               cmd_cb_(0), cb_ok_(0), cb_rejected_(0), cb_noanswer_(0),
               // 🔑 **선언 순서와 같게 적는다.** 초기화는 선언 순서로 일어나므로
               //   여기 순서가 다르면 컴파일러가 경고한다 — 지금은 값이 전부 0 이라 무해하지만
               //   ⚠ 나중에 **다른 필드를 참조하는 초기화**가 들어오면 그때 조용히 틀린다.
               occ_cb_(0), manual_cb_(0), pick_cb_(0),
               // ⚠ **선언 순서와 같아야 한다**(`-Wreorder-ctor`). 여기 값은 전부 0 이라 결과는 같지만,
               //   순서가 어긋난 채로 두면 **나중에 서로를 참조하는 초기화가 들어올 때** 조용히 틀린다.
               plate_orphan_(0), shot_late_(0), shot_cancelled_(0), ard_sum_none_at_(0), ard_short_frames_(0), plate_repeat_(0), dup_last_ms_(0), dup_since_log_(0), phone_ping_at_(0), phone_ping_seq_(0), owner_(0), occ_change_n_(0),
               eframes_(0), etrans_cmp_(0), etrans_split_(0), ebad_(0),
               cfg_sent_(0), cfg_skipped_(0), cfg_gap_slots_(0),
               mod_missing_(0), mod_missing_max_(0), mod_declared_(0), act_nocontrol_(0), occ_fall_absorbed_(0), occ_fall_fast_(0), actstate_split(0), actstate_cmp_(0),
               ledger_new_(0), ledger_review_(0),
               resync_count(0), no_disk(false),
               soak_start_ms(0), ard_sessions(0), sess_start_ms(0), sess_frames(0),
               sess_last_line_ms(0), sess_max_gap_ms(0), all_frames(0), all_max_gap_ms(0),
               all_max_gap_at(0), link_down_ms(0), link_down_since(0),
               reboot_by_conn(0), reboot_by_uptime(0), offline_episodes(0),
               drop_cksum(0), drop_overlong(0), drop_unknown(0), drop_noise(0),
               drop_prepromo(0), drop_prepromo_buf(0),
               retx_count(0), ack_fail_count(0),                  // 선언 순서와 일치시킨다(-Wreorder)
               ard_online(false), last_report_ms(0),
               offline_since_ms(0), offline_at_session(0), recov_dropped(0),
               recov_same_conn(0), recov_reconn(0), recov_worst_ms(0),
               zombie_reaps(0), keepalive_reaps(0) {
        for (int i = 0; i < 10; i++) { test_ovr[i] = 0; }
    }

    // ---------- 소크 관측 보조
    static std::string hms(long long ms) {
        if (ms < 0) ms = 0;
        long long s = ms / 1000;
        char b[32];
        snprintf(b, sizeof(b), "%02lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
        return std::string(b);
    }
    static std::string secs(long long ms) {
        char b[32];
        snprintf(b, sizeof(b), "%.1f초", ms / 1000.0);
        return std::string(b);
    }
    // 요약 안에서 "언제"를 가리키는 값(최대공백@… 등)도 **줄 앞머리와 같은 완전 형식**으로 쓴다.
    // v0.1 에서 연도를 뺐다가 monitor 지적으로 되돌렸다: "경계 줄이 연도를 확정해 준다"는
    // **파일을 처음부터 순서대로 읽을 때만** 참인데, 관측자는 5MB 를 통째로 못 올려서
    // **항상 잘라 읽는다.** 잘린 조각에서 `@08-16 16:52:47` 은 다시 연도가 없다.
    // 이 계약의 요지는 **부분 문자열만 봐도 절대시각이 확정되는 것**이므로 예외를 두지 않는다.
    // 비용은 요약이 분당 1줄이라 줄 앞머리의 1/60 이다.
    static std::string clock_at(long long ep_ms) {
        if (ep_ms <= 0) return std::string("-");
        time_t tt = (time_t)(ep_ms / 1000);
        char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", localtime(&tt));
        return std::string(b);
    }

    // ⚠ **아직 닫히지 않은 침묵도 공백이다.** 공백을 "줄과 줄 사이"로만 세면
    // 장치가 조용히 멈춘 바로 그 사고가 최대공백에 **안 잡힌다** — 다음 줄이 영영 안 오기 때문이다.
    // (실제로 겪었다: 22분간 무프레임이었는데 세션 최대공백이 3.3초로 찍혔다.)
    long long live_gap_ms() const {
        if (!sess_start_ms || !sess_last_line_ms) return 0;
        return now_ms() - sess_last_line_ms;
    }
    void fold_live_gap() {                       // 진행 중이던 침묵을 확정한다
        long long g = live_gap_ms();
        if (g > sess_max_gap_ms) sess_max_gap_ms = g;
        if (g > all_max_gap_ms) { all_max_gap_ms = g; all_max_gap_at = epoch_ms(); }
    }

    // ---------- 복구 계측 보조
    // **평균이 아니라 중앙값을 쓴다.** 복구시간은 한쪽으로 길게 끌리는 분포다(대부분 몇 초,
    // 가끔 재연결로 수십 초). 평균은 그 꼬리 하나에 끌려가 "보통 얼마나 걸리나"를 못 말한다.
    // 최악값은 따로 낸다 — 중앙값과 최악을 **같이** 봐야 "대체로 빠른데 가끔 나쁘다"가 읽힌다.
    long long recov_median_ms() const {
        if (recov_ms.empty()) return -1;                  // -1 = 표본 없음(0 과 구별한다)
        std::vector<long long> v = recov_ms;              // 정렬이 원본을 흔들면 안 된다
        size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + mid, v.end());
        if (v.size() % 2) return v[mid];
        long long hi = v[mid];
        std::nth_element(v.begin(), v.begin() + (mid - 1), v.end());
        return (v[mid - 1] + hi) / 2;
    }

    // 아두이노 소켓의 유휴를 재는 기준 시각.
    // ⚠ **`ard_last_ms`(체크섬을 통과한 S 프레임)를 쓴다.** 이유는 ARD_IDLE_CLOSE_MS 주석에 있다 —
    // 모든 수신 줄(`sess_last_line_ms`)로 재면 잡음을 흘리는 장치가 영원히 회수되지 않는다.
    // `sess_start_ms` 와 큰 쪽을 쓰는 이유는 둘이다:
    //   · 붙기만 하고 **한 줄도 안 보내는** 연결도 회수 대상이다(그 자체로 누수 경로다).
    //   · `ard_last_ms` 는 세션 경계에서 리셋되지 않는다. 새 연결 직후 옛 값으로 재면
    //     **갓 붙은 정상 연결을 그 자리에서 끊는다.** sess_start_ms 가 그 사고를 막는다.
    long long ard_idle_base_ms() const {
        return (ard_last_ms > sess_start_ms) ? ard_last_ms : sess_start_ms;
    }

    // 아두이노 소켓에만 keepalive 를 건다. **모든 accept 에 일괄로 걸지 않는다** —
    // HTTP/WS/폰 연결의 수명 정책은 이 요청의 범위가 아니고, 조용히 바꿀 일도 아니다.
    //
    // 반환값은 **OS 가 실제로 받아들인 값을 되읽은 것**이다(요청한 값이 아니다).
    // setsockopt 는 조용히 무시되거나 값이 잘릴 수 있고, 그러면 "켰다고 믿는데 안 켜진"
    // 상태가 된다 — 로그에 요청값을 찍으면 그 거짓말을 그대로 기록하게 된다.
    // 되읽어 찍으면 켜졌는지 아닌지가 로그만 보고 판정된다.
    static std::string set_keepalive(sock_t s) {
        int on = 1;
        if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&on, sizeof(on)) != 0)
            return "keepalive 설정 실패(errno=" + std::to_string(sockerr()) + ")";
#ifdef _WIN32
        // ⚠ 윈도우는 SO_KEEPALIVE 만으로는 **기본 유휴가 2시간**이라 사실상 안 걸린다.
        // 세부 조정은 SIO_KEEPALIVE_VALS(mstcpip.h)가 필요한데 **이 기기에서 컴파일 검증을
        // 할 수 없어 넣지 않았다.** 그래서 윈도우에서는 OS 경로가 하중을 받지 못하고
        // 유휴 마감(ARD_IDLE_CLOSE_MS)이 **유일한** 회수 장치가 된다 — 명세에도 그렇게 적었다.
        // 안 되는 것을 된다고 적는 것보다, 못 한 것을 못 했다고 적는 편이 낫다.
        return "keepalive on(윈도우 기본 유휴 2시간 — 세부조정 없음, 유휴 마감이 주 장치)";
#else
        int idle = KEEPALIVE_IDLE_S, intvl = KEEPALIVE_INTVL_S, cnt = KEEPALIVE_CNT;
  #ifdef TCP_KEEPIDLE                        // 리눅스
        setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, (const char*)&idle, sizeof(idle));
        const int IDLE_OPT = TCP_KEEPIDLE;
  #elif defined(TCP_KEEPALIVE)               // macOS/BSD — 같은 뜻의 다른 이름이다
        setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE, (const char*)&idle, sizeof(idle));
        const int IDLE_OPT = TCP_KEEPALIVE;
  #endif
  #ifdef TCP_KEEPINTVL
        setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&intvl, sizeof(intvl));
  #endif
  #ifdef TCP_KEEPCNT
        setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, (const char*)&cnt, sizeof(cnt));
  #endif
        // ---- 되읽기. 여기서 찍는 숫자는 **커널이 들고 있는 값**이다.
        int ron = 0, ridle = -1, rintvl = -1, rcnt = -1;
        socklen_t sl = sizeof(int);
        getsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&ron, &sl);
  #if defined(TCP_KEEPIDLE) || defined(TCP_KEEPALIVE)
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, IDLE_OPT, (char*)&ridle, &sl);
  #endif
  #ifdef TCP_KEEPINTVL
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, (char*)&rintvl, &sl);
  #endif
  #ifdef TCP_KEEPCNT
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, (char*)&rcnt, &sl);
  #endif
        char b[160];
        snprintf(b, sizeof(b),
                 "keepalive %s · 유휴 %ds · 간격 %ds · 횟수 %d → 탐지 약 %ds (커널 되읽기)",
                 ron ? "on" : "**off**", ridle, rintvl, rcnt,
                 (ridle > 0 && rintvl > 0 && rcnt > 0) ? ridle + rintvl * rcnt : -1);
        return std::string(b);
#endif
    }

    // recv 가 -1 을 준 이유가 **keepalive 가 죽였기 때문인지**를 가른다.
    // 안 가르면 "수신 오류" 한 문자열에 전부 섞여 keepalive 가 한 번이라도 일했는지
    // 증명할 길이 없어진다 — 켜 놓고 안 켜진 것과 구별이 안 되는 계측은 계측이 아니다.
    // 🔴🔴 **읽기 타임아웃을 건다 — 그렇지 않으면 `recv` 가 이벤트 루프를 통째로 잡는다.**
    //
    //   실측 : 보드 하나가 붙은 뒤 조용해지면 `serveOneTick()` 안의 `recv` 에서
    //   **12분간 안 돌아왔다**(스택: `main → serveOneTick → __recvfrom`).
    //   그동안 HTTP 무응답 · 다른 보드 굶음 · 로그 정지 · Recv-Q 4041B 적체.
    //   ⚠ 그런데 **프로세스는 살아 있고 LISTEN 3포트도 그대로였다** — 겉보기엔 멀쩡했다.
    //
    // 🔑 우리 규약 §"막는 대기를 쓰지 마라" 가 **서버에도 걸린다.**
    //   `select()` 타임아웃은 허용이지만 **타임아웃 없는 `recv` 는 그 규약 위반**이다.
    //
    // ⚠ **타임아웃이 나면 `recv` 는 `-1` 을 준다** — 그것을 연결 끊김으로 읽으면 **정상 소켓을 끊는다.**
    //   그래서 호출부마다 `err_is_timeout()`(그리고 would-block)을 갈라야 한다. 값이 아니라 **사유**를 봐라.
    static std::string set_recv_timeout(sock_t s) {
#ifdef _WIN32
        DWORD ms = (DWORD)RECV_TIMEOUT_MS;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms)) != 0)
            return "recv 타임아웃 설정 실패(errno=" + std::to_string(sockerr()) + ")";
#else
        struct timeval tv;
        tv.tv_sec  = (long)(RECV_TIMEOUT_MS / 1000);
        tv.tv_usec = (long)((RECV_TIMEOUT_MS % 1000) * 1000);
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) != 0)
            return "recv 타임아웃 설정 실패(errno=" + std::to_string(sockerr()) + ")";
#endif
        // 🔴🔴 **되읽어서 값을 말한다** — `set_keepalive` 와 같은 규율이다.
        //   `setsockopt` 은 **조용히 무시되거나 값이 잘릴 수 있다.** 요청값을 로그에 찍으면
        //   *"걸었다고 믿는데 안 걸린"* 상태를 그대로 기록하게 된다.
        //   ★ 루트의 걱정이 정확히 그것이었다: *"윈도우에서 300ms 가 아니라 엉뚱한 값이 되면
        //     그건 조용하다."* → **되읽으면 조용하지 않다.**
        {
#ifdef _WIN32
            // 🔴 **`optlen` 타입이 플랫폼마다 다르다** — MSVC 는 `int*`, POSIX 는 `socklen_t*`.
            //   MSVC 의 `socklen_t` 는 `typedef int` 라(ws2tcpip.h) **크기가 같아 캐스팅이 안전하다.**
            //   ⚠ winparse 가 이 자리에서 **실제로 컴파일 오류를 냈다** — 검사가 일했다.
            DWORD got = 0; socklen_t sl = (socklen_t)sizeof(got);
            if (getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&got, &sl) == 0) {
                char b[96];
                snprintf(b, sizeof(b), "recv 타임아웃 %lums(요청 %dms)",
                         (unsigned long)got, RECV_TIMEOUT_MS);
                return std::string(b);      // 🔑 실패가 아니라 **값 보고**다(호출부가 로그로 찍는다)
            }
#else
            struct timeval got; socklen_t sl = sizeof(got);
            if (getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&got, &sl) == 0) {
                const long gotms = (long)got.tv_sec * 1000 + (long)got.tv_usec / 1000;
                char b[96];
                snprintf(b, sizeof(b), "recv 타임아웃 %ldms(요청 %dms)", gotms, RECV_TIMEOUT_MS);
                return std::string(b);
            }
#endif
        }
        return std::string();
    }

    // 🔴 **읽기가 "지금은 없다" 로 끝난 것인가** — 그러면 **끊긴 것이 아니다.**
    //   타임아웃(SO_RCVTIMEO)과 would-block 을 **같이** 본다: 플랫폼마다 어느 쪽을 주는지 다르다.
    //   ★ 이 함수가 없으면 위 타임아웃이 **정상 소켓을 끊는 장치**로 바뀐다.
    static bool err_is_again(int e) {
#ifdef _WIN32
        // 🔴 **윈도우는 여기서 `WSAETIMEDOUT` 을 빼면 안 된다.** 이 갈래는 `SO_RCVTIMEO`
        //   만료를 진짜로 `WSAETIMEDOUT` 으로 준다 — keepalive 사망과 **원리적으로 같은 값**이라
        //   구별이 불가능하다. 둘 중 하나를 골라야 하면 **정상 소켓을 안 끊는 쪽**을 고른다.
        return e == WSAETIMEDOUT || e == WSAEWOULDBLOCK;
#else
        // 🔴🔴 **POSIX 에는 `ETIMEDOUT` 이 없다** — 뺀 것이지 빠뜨린 것이 아니다.
        //   근거는 추측이 아니라 측정이다(`net/probe/rcvto_errno.cpp`):
        //     **`SO_RCVTIMEO` 만료는 `EAGAIN`(errno=35)** 이다. 즉 POSIX 에서 `ETIMEDOUT` 은
        //     만료가 아니라 **keepalive 가 죽인 소켓**의 신호다.
        //   ⚠ 여기 넣어 두면 그 사망이 "끊김이 아니다" 로 분류되어
        //     **`err_is_timeout()` 갈래가 도달 불가**가 되고, `keepalive_reaps` 가
        //     `0/N`(쟀는데 없다)이 아니라 **`0/0`(셀 자리에 못 갔다)** 이 된다.
        //   ★ 갈래를 만들었으면 **그 갈래가 한 번이라도 찍힌 적이 있나**를 세라. `0` 이면 도달 불가를 의심.
        return e == EAGAIN || e == EWOULDBLOCK;
#endif
    }

    static bool err_is_timeout(int e) {
#ifdef _WIN32
        return e == WSAETIMEDOUT;
#else
        return e == ETIMEDOUT;
#endif
    }

    // 🔴 **소켓이 죽은 이유를 한 곳에서 정한다.** 주 노드와 보조 노드가 **같은 낱말**을 쓰게.
    //   ⚠ 갈래를 두 곳에 복사해 두면 **한쪽만 고쳐진다** — 그리고 그 어긋남은
    //   로그를 세는 사람 쪽에서 "보드마다 다르게 죽는다" 로 보인다. 원인이 안 보이는 종류다.
    //
    // 🔴 **`err_is_again()` 의 POSIX 갈래에 `ETIMEDOUT` 을 다시 넣지 마라.**
    //   넣으면 호출부가 그것을 먼저 걸러서 이 함수의 keepalive 갈래가 **도달 불가**가 되고,
    //   `keepalive_reaps` 가 `0/N`(쟀는데 없다)이 아니라 **`0/0`(셀 자리에 못 갔다)** 이 된다.
    //   ★ 이 갈래가 **한 번도 안 찍히면** 그것부터 의심해라 — 값이 아니라 **순서**의 문제다.
    //
    // ⚠ **윈도우에서는 이 갈래를 믿지 마라.** 거기선 `SO_RCVTIMEO` 만료와 keepalive 사망이
    //   **같은 값**(`WSAETIMEDOUT`)이라 원리적으로 구별이 안 된다 — 그래서 만료 쪽으로 넘긴다.
    //   즉 이 사유는 **POSIX 에서만 뜻이 있다.**
    static std::string sock_close_why(int r, int e) {
        if (r == 0) return "상대가 닫음";                                        // FIN
        if (err_is_timeout(e)) return "keepalive 시간초과(errno=" + std::to_string(e) + ")";
        return "수신 오류(errno=" + std::to_string(e) + ")";                     // RST 등
    }

    // 한 줄로 소크 전체를 재구성할 수 있게 한다. 주기 보고와 종료 요약이 같은 문장을 쓴다.
    std::string soak_line() const {
        long long t = now_ms();
        long long gap_max = all_max_gap_ms;
        long long live = live_gap_ms();
        if (live > gap_max) gap_max = live;       // 진행 중인 침묵도 포함해서 보고한다
        long long down = link_down_ms + (link_down_since ? t - link_down_since : 0);
        std::string s = "소크 " + hms(t - soak_start_ms)
            + " · 세션 " + std::to_string(ard_sessions) + "회";
        if (sess_start_ms) s += "(현재 #" + std::to_string(ard_sessions)
                                + " 연결중 " + hms(t - sess_start_ms) + ")";
        else               s += "(현재 끊김)";
        s += " · 프레임 " + std::to_string(all_frames)
           // 🔴🔴 **프레임이 0 이면 `최대공백 0s` 는 거짓말이다** — *"공백이 한 번도 없었다"*,
           //   즉 **완벽하게 규칙적인 링크**처럼 읽힌다. 실제 뜻은 **한 줄도 안 왔다**.
           //   ★ `live_gap_ms()` 가 `!sess_last_line_ms` 일 때 **`0` 을 돌려주기 때문**이다.
           //   🔑 위 §"22분 무프레임인데 3.3초로 찍혔다" 주석이 **진행 중 침묵**은 고쳤는데,
           //     **한 번도 안 온 경우**는 안 고쳤다. 같은 병의 남은 절반이다.
           //   ⚠ 그리고 이것이 **못 잰 값에 좋은 얼굴을 주는** 가장 위험한 등급이다 —
           //     평범한 `0`(없음)은 의심을 받지만 `0s`·`0ms` 는 **축하를 받는다.**
           + (all_frames == 0
              ? std::string(" · 최대공백 **안 쟀다**(프레임 0 — 한 줄도 안 왔다)")
              : " · 최대공백 " + secs(gap_max)
                + (live > all_max_gap_ms ? "(진행중)" : "@" + clock_at(all_max_gap_at)))
           // 🔴 **이름을 `재부팅감지` → `새세션` 으로 바꿨다**
           //   옛 이름은 **새 TCP 연결을 전부 재부팅으로 셌다.** 08-16 에 과대집계가 밝혀졌고
           //   (36건 중 35건이 링크 재접속) 아래 `재연결내역` 이 그것을 이미 갈라 놨는데
           //   **머리 낱말만 옛 뜻으로 남아 있었다.**
           // 🔴 그 사이 실측이 그것을 다시 증명했다: `재부팅감지 5` 인데 장치 uptime 은
           //   **단조 증가**(60→…→360 · 되감김 0) — **재부팅은 0회였다.**
           // > **§"두 뜻을 겸한 이름" 이 아니다. *한 뜻인데 이름이 다른 것을 가리킨다* —**
           // > **값이 갈려도 안 보인다.** 괄호에 답이 있었는데 **머리 낱말이 그것을 덮었다.**
           // 🔑 **`uptime되감김` 을 따로 찍는다** — 그 `0` 이 *"장치는 안 죽었다"* 를 말한다.
           //   §"`0` 옆에 무엇을 둘지가 그 계수의 값을 정한다".
           // ⚠ **변수 이름(`reboot_by_*`)은 아직 옛 것이다** — 출력만 고쳤다.
           //   그것도 같은 결함이지만 **한 번에 하나만 바꾼다**(호출부가 여럿이다).
           + " · 새세션 " + std::to_string(reboot_by_conn)
           + "(uptime되감김 " + std::to_string(reboot_by_uptime) + ")"
           // 🔴 **이 칸의 분모가 배포 경계에서 바뀌었다.** 전에는 주 노드 한 대의 수였고
           //   지금은 다섯 대 합계다 — **같은 이름 · 다른 분모**다.
           //   ⚠ 라벨을 안 붙이면 배포 뒤 이 수가 뛰는 것을 **거동 변화로 오독한다.**
           + " · 재연결내역(다섯대합계 · 재부팅 " + std::to_string(xs_reconnect_reboot)
           + "/링크재접속 " + std::to_string(xs_reconnect_link)
           + "/미상 " + std::to_string(xs_reconnect_unknown) + ")"
           + " · 오프라인 " + std::to_string(offline_episodes) + "회"
           + " · 링크없음 " + hms(down)
           // 상행만 보면 하행이 통째로 죽어도 요약이 멀쩡하다. 네 숫자를 나란히 둔다.
           // 버린줄은 합과 원인을 함께 — 원인이 갈리면 진단이 갈린다(잡음 유입 vs 바이트 손상).
           + " · 버린줄 " + std::to_string(drop_cksum + drop_overlong + drop_unknown + drop_noise)
           + "(체크섬 " + std::to_string(drop_cksum)
           + "/과길이 " + std::to_string(drop_overlong)
           + "/모름 " + std::to_string(drop_unknown)
           + "/잡음 " + std::to_string(drop_noise) + ")"
           // 승격 전 소켓에서 버린 줄 — 위 4칸 어디에도 안 잡히던 것이다.
           // **0 이면 "안 왔다"가 확정된다.** ⚠ 분모가 없으면 0 이 "못 셌다"와 겹친다.
           + " · 승격전버림 " + std::to_string(drop_prepromo)
           + "(버퍼비움 " + std::to_string(drop_prepromo_buf) + ")"
           + " · 재전송 " + std::to_string(retx_count)
           + " · ACK실패 " + std::to_string(ack_fail_count)
           // ── 하행 슬롯 큐. **분모를 같이 찍는다** — `거절 0` 이 "건강"인지 "한 번도 안 돌았다"인지
           // 갈리려면 `배치` 가 옆에 있어야 한다(원장 §5.2). 배치 0 이면 하행이 아예 없었던 것이다.
           + " · 배치 " + std::to_string(batch_count)
           + "(줄 " + std::to_string(batch_lines)
           + "/미룬창 " + std::to_string(q_deferred)
           + "/창포기 " + std::to_string(dmax_flushes)
           + "/창놓침 " + std::to_string(win_skips) + ")"
           // 🔴 **네 칸을 갈라 둔다** — 합치면 `큐거절 3` 을 보고 cap 을 올리려 하는데
           // 실제로는 장치가 없었던 것일 수 있다. 원인이 갈리면 진단이 갈린다.
           + " · 큐넘침 " + std::to_string(q_rejected)
           + "(건수축 " + std::to_string(q_full_n)
           + "/장치없음 " + std::to_string(q_nodev)
           + "/중복 " + std::to_string(q_dup)
           + "/링크버림 " + std::to_string(q_dropped_link) + ")"
           // 🔑 **한 번 뜨는 경고는 놓친다.** 주기 요약에 계속 보이게 둔다 —
           // 이 칸이 가정을 넘으면 하행 건수 상한의 근거가 이미 깨진 것이다.
           // 🔴 **0 이 아니면 그 창의 장치 지표는 읽지 마라** — 두 보드가 섞였다는 뜻이다.
           // 계측 신뢰도 지표라서 다른 칸보다 앞에 둔다.
           // 🔑 **등록은 지금 관측 전용이다**(하행 경로를 안 바꾼다). 그래서 계수만 낸다.
           // `완료 0` 이 "옛 펌웨어라 등록을 안 한다"인지 "새 펌웨어인데 실패한다"인지는
           // **`질의`·`포기` 가 옆에 있어야 갈린다** — 분모를 같이 찍는 그 규율이다.
           + " · 등록 완료 " + std::to_string(reg_ok)
           + "(형식오류 " + std::to_string(reg_bad)
           + "/질의 " + std::to_string(reg_qsent)
           + "/포기 " + std::to_string(reg_giveups)
           + "/폭불일치 " + std::to_string(reg_widthbad)
           + "/미해독 " + std::to_string(occ_undecoded)
           + (reg_widthbad ? "🔴" : "") + ")"
           + " · devid거절 " + std::to_string(dup_devid_reject)
           + (dup_devid_reject ? "🔴" : "")
           + "/유예교체 " + std::to_string(takeover_grace)
           // 🔴 같은 등급이다 — `S최대 0B/가정 220B` 는 **"프레임이 아주 작다"**(=안전)로 읽힌다.
           //   실제 뜻은 **S 프레임을 한 번도 못 봤다**. 가정치와의 여유가 검증된 적이 없다는 뜻이다.
           + (s_max_b == 0
              ? std::string(" · S최대 **못 봤다**(가정 " + std::to_string(DEV_S_WORST_ASSUMED_B)
                            + "B 는 아직 검증 안 됐다)")
              : " · S최대 " + std::to_string(s_max_b) + "B/가정 "
                + std::to_string(DEV_S_WORST_ASSUMED_B) + "B"
                + (s_worst_warned ? "🔴" : ""))
           + " · 대기 " + std::to_string(downq.size())
           + "줄(" + std::to_string(downq_bytes) + "B/" + std::to_string(downq_max_b()) + "B)"
           // 🔴 `RTT` 는 **실측이고 표본 수를 같이 낸다.** `n=0` 이면 그 값은 "0ms"가 아니라
           // **"안 쟀다"** 이고, 그때 `W_srv` 는 상한(600−여유)에 그대로 앉아 있다.
           // 🔴🔴 **못 잰 값에 좋은 얼굴을 주지 마라 — `0ms` 라는 숫자를 아예 내지 않는다.**
           //   평범한 `0`(없음)은 의심이라도 받는다. **`0ms`·`100%`·`오류 0` 은 축하를 받는다.**
           //   ★ `왕복 최대 0ms` 는 **가장 빠른 링크처럼** 읽힌다 — 실제 뜻은 `n=0`, **안 쟀다**.
           //   🔑 처음엔 숫자를 두고 표지만 달았는데, **숫자를 안 보이게 하는 쪽이 더 세다**(루트).
           //     표지는 읽는 사람이 **끝까지 읽어야** 보이고, 숫자는 **먼저** 보인다.
           //   ⚠ 그리고 이 위 주석이 그 위험을 이미 적어 뒀었다 — *"`n=0` 이면 그 값은 `0ms` 가
           //     아니라 안 쟀다 이고, 그때 `W_srv` 는 상한에 그대로 앉아 있다."* **적고도 냈다.**
           + (rtt_n == 0
              ? std::string(" · 왕복 **안 쟀다**(n=0 — W_srv 는 실측이 아니라 상한값이다)")
              : " · 왕복 최대 " + std::to_string(rtt_max_ms) + "ms"
                + "(마지막 " + std::to_string(rtt_last_ms)
                + "/n=" + std::to_string(rtt_n) + ")")
           + " · W_srv " + std::to_string(w_srv()) + "ms"
           + " · " + recovery_phrase()
           + " · 회수 " + std::to_string(zombie_reaps + keepalive_reaps)
           + "(유휴 " + std::to_string(zombie_reaps)
           + "/keepalive " + std::to_string(keepalive_reaps) + ")";
        // 🔴 **분모를 찍는다** — `치유 0/0` 과 `치유 0/1200` 은 완전히 다른 문장이다.
        //   앞은 **검사가 안 돈 것**이고 뒤는 **건강한 것**이다.
        //   ⚠ 분자만 찍으면 둘이 똑같이 `0` 으로 보인다. 그래서 검사 수를 같이 낸다.
        // 🔑 **이름을 붙여 낸다.** `M − N` 은 성공이 아니다 — 무응답이 그 안에 섞인다.
        s += " · G 띄움 " + std::to_string(gate_q)
           + "·도달 " + std::to_string(gate_sent)
           + "·응답 " + std::to_string(gate_ans)
           + "·거절 " + std::to_string(dev_reject);
        // 🔴 **계수를 만들고 요약에 안 내보냈다**. monitor 에게 *"`순서변경` 을 봐라"* 라고
        //   알려 둔 상태였다 — **없는 칸을 보라고 한 것이다.** 그쪽 파서는 표지 기준이라 조용히 못 찾는다.
        //   🔑 §"조건을 적었으면 그것을 보는 감시를 같은 자리에 만들어라" 의 **한 걸음 뒤 판본**이다:
        //   감시는 만들었는데 **그 결과를 볼 자리를 안 만들었다.** 세는 것과 보이는 것은 다른 일이다.
        s += " · 순서변경 " + std::to_string(mod_order_changed);
        // 🔴 계수를 만들었으면 **볼 자리도 만든다**(원장 규칙 셋째). 분모는 `등록 완료` 다.
        s += " · 비자리예약 " + std::to_string(not_reservable_n)
           + (not_reservable_n > 0 ? " 🔴" : "");
        // 🔑 **계기 + 창최대.** 요약 틈(60초)에서 떴다 지면 지금값만으로는 안 남는다 —
        //   ⚠ 실제로 그렇게 사라진 값이 있다 — 📖 docs/net/LEDGER.md
        s += " · 센서갈림 " + std::to_string(sensor_split_now) + "자리"
           + "(창최대 " + std::to_string(sensor_split_max_) + ")"
           + (sensor_split_now > 0 || sensor_split_max_ > 0 ? " 🔴" : "");
        s += " · 이름중복 " + std::to_string(mod_dup_name)
           + (mod_dup_name > 0 ? " 🔴" : "");
        // 🔴 **`(누적)` 을 누적 쪽에 박는다.** 계기가 기본이고 누적이 예외로 읽히기 때문이다(루트).
        //   ⚠ 등록이 3회면 분모가 3배로 뛴다 — 표시가 없으면 사람이 **"칩이 바뀌었나"** 로 읽는다.
        //     ⚠ 관측자가 그 확인을 하러 온다. **그 왕복이 이 표기로 없어진다.**
        // 🔑 **0 이어야 정상이라 요약에 항상 싣는다** — 안 실으면 그 수를 아무도 안 본다
        s += " · 남의이름결속 " + std::to_string(mod_claimed_other_);
        // 🔑 **종류 수를 같이 낸다** — 이것이 없으면 겹침과 재접속을 못 가른다(monitor 합의)
        s += " · devid대체 " + std::to_string(adopt_replace_n_)
           + "(상대 " + std::to_string((long long)adopt_peers_.size()) + "종)";
        s += " · 미결속모듈 " + std::to_string(mod_unbound)
           + "/" + std::to_string(mod_seen_) + "(누적)"
           // 🔴 **사유를 붙인다.** 이 수는 *"장치가 선언했는데 조립 표에 없다"* 이고
           //   **대개 실물 없는 모듈을 표에서 뺀 결과**다 — 그러면 이것이 **정상 상태**다.
           //   ⚠ 사유가 없으면 사용자가 그냥 "에러" 로 읽는다.
           //   🔑 §"증상이 보이면 말하고 안 보이면 막아라" — 말하는 쪽이면 **왜인지도 말한다.**
           //   ⚠ 서버는 *"실물이 없어서"* 를 **모른다.** 그래서 단정하지 않고 **어디를 볼지**만 준다.
           + (mod_unbound > 0
              ? " 🔴(장치 선언에 있고 조립 표에 없다 — 위 `🔴 모듈 …` 줄이 이름을 지목한다)"
              // 🔴 **분모가 0 이면 분자의 `0` 은 건강이 아니다.** 선언을 한 번도 못 본 것이다.
              : (mod_seen_ == 0
                 ? " 🔴0/0(**모듈 선언을 한 번도 못 봤다** — 등록이 안 왔거나 `D` 가 안 온다)"
                 : ""));
        // 🔑 갈래를 갈라 센다 — 뭉치면 "장치가 이상한가 내 로직이 이상한가"를 못 가른다
        s += " · 명령결과 성공 " + std::to_string(cb_ok_)
           + "/거절 " + std::to_string(cb_rejected_)
           + "/무응답 " + std::to_string(cb_noanswer_)
           + (cb_noanswer_ > 0 ? " 🔴" : "");
        // 🔑 콜백을 등록 안 해도 센다 — 볼 자리를 선택적인 것에 묶지 않는다.
        //   ⚠ 이것은 **센서 비트 변화**만 센다. 명령 모듈의 에코는 여기 안 든다.
        s += " · 점유변화 " + std::to_string(occ_change_n_);
        // 🔴 **분모를 같이 낸다.** `0/0` = 못 쟀다(비교가 한 번도 성립 안 함) ·
        //   `0/N` = 정상 · `M/N` = 조사 대상. 분모가 없으면 앞의 둘이 **같아 보인다**.
        s += " · 상태갈림 " + std::to_string(actstate_split)
           + "/" + std::to_string(actstate_cmp_)
           + (actstate_split > 0 ? " 🔴"
              : (actstate_cmp_ == 0 ? " (미측정 — 상태 훅 붙은 액추에이터 0)" : ""));
        // ── 🔴 `E`(전이) 계수 — SPEC §15
        //   🔑 **`V` 의 "도착률" 을 그대로 옮기지 않았다.** `V` 는 매 슬롯 와서 *"몇 %가 왔나"* 가
        //     뜻이 있었는데, `E` 는 **전이 때만** 온다 — 안 오는 것이 정상이다.
        //     ★ 그 칸을 남겼으면 **정상이 영구 0% 로 보인다**(§"정상 상태가 경보처럼 보이면
        //     아무도 그 표시를 안 읽는다"). 그래서 **사건 수**로 바꾼다.
        {
            const long long de = eframes_ - prev_eframes_;
            const long long dv = evalues_ - prev_evalues_;
            prev_eframes_ = eframes_; prev_evalues_ = evalues_;
            s += " · **전이 " + std::to_string(de) + "건(최근 60초)**";
            //   🔑 값이 실린 비율은 **분모가 있을 때만** 낸다. `0/0` 이 스스로 말하게 둔다
            if (de > 0) s += " 값 " + std::to_string(dv) + "/" + std::to_string(de);
        }
        s += " · 전이누적 " + std::to_string(eframes_)
           + "(값 " + std::to_string(evalues_) + "/못쟀다 " + std::to_string(emiss_) + ")"
           + (ebad_ > 0 ? (" 🔴형식오류 " + std::to_string(ebad_)) : "");
        // 🔴🔴 **§15.10 의 유일한 관측자** — `E` 의 `r` 과 `occ` 비트가 갈린 수.
        //   ★ `> 0` 이면 장치가 판정을 **슬롯당 1회보다 많이** 돌리고 있다는 뜻이다
        //     (둘이 다른 표본에서 나온다). **굽기가 그 변경을 실었는지를 이 값으로 판정한다.**
        //   ⚠ 분모 없이는 `0` 이 *건강* 인지 *못 셈* 인지 안 갈린다 — 항상 `N/M` 으로 낸다.
        // 🔴 판정 수치 하행 — **분모를 같이 낸다.** `0보냄` 만 보이면
        //   *"선언이 없다"* 인지 *"보냈는데 0"* 인지 안 갈린다.
        //   ⚠ `건너뜀 > 0` 은 **기여자가 반쪽만 선언했다**는 뜻이다 — 로그가 어느 모듈인지 말한다.
        //   🔑 `대기` 가 계속 남으면 창 예산 산식이 틀린 것이다(§15.4).
        // 🔑 **대기는 전 노드 합이다** — 노드별로 쌓이므로 한 노드만 보면 남은 것을 못 센다
        const size_t cfg_wait = cfg_pending_total();
        if (cfg_sent_ > 0 || cfg_skipped_ > 0 || cfg_wait > 0)
            s += " · 판정수치 " + std::to_string(cfg_sent_) + "보냄"
               + (cfg_skipped_ > 0 ? ("/🔴건너뜀 " + std::to_string(cfg_skipped_)) : "")
               + (cfg_wait > 0 ? ("/대기 " + std::to_string((long long)cfg_wait)
                                     + "(" + std::to_string(cfg_gap_slots_) + "창)") : "");
        s += " · 전이불일치 " + std::to_string(etrans_split_)
           + "/" + std::to_string(etrans_cmp_)
           + (etrans_split_ > 0 ? " 🔴"
              : (etrans_cmp_ == 0 ? " (미측정 — 전이가 아직 없다)" : ""));
        // 🔴 카메라는 **분모를 항상 같이 낸다** — `실패 0` 만 보이면 건강인지 못 셈인지 모른다.
        //   ⚠ 발급이 0 이면 줄 자체를 안 낸다(아무도 안 쓰는 기능이 매 줄 자리를 먹지 않게).
        // 🔴 **폰 접속 칸.** 없어서 monitor 가 `WS접속`(화면 포트)을 골라 읽었다 —
        //   §"요약에서 값을 찾을 때 먼저 물어라: 이 칸이 내가 찾는 것을 세는가. **이름이 가장 잘 속인다**"
        //   🔑 셋을 같이 낸다 : **누적**(절단 빈도) · **현재**(SHOOT 가 몇 곳에 나가나) · **최대**(공존)
        //   ⚠ 유지 시간은 **최대·마지막**만 낸다. 중앙값은 표본을 들어야 해서 안 낸다 —
        //     **그러니 이 둘로는 분포의 모양을 모른다.** 그 사실을 값 옆에 적어 두는 대신 여기 적는다.
        if (phone_epoch_ > 0 || !phones.empty())
            s += " · 폰(" + std::to_string(g_port_cam) + ") " + std::to_string(phone_epoch_)
               + "회(현재 " + std::to_string(phones.size())
               + " · 최대 " + std::to_string(phone_max_)
               + " · 유지 최대 " + std::to_string(phone_hold_max_ms_)
               + "ms/마지막 " + std::to_string(phone_hold_last_ms_) + "ms)";
        if (shot_issued_ > 0 || shot_orphan_ > 0 || shot_nophone_ > 0)
            s += " · 촬영 " + std::to_string(shot_answered_) + "/" + std::to_string(shot_issued_)
               + (shot_failed_    > 0 ? ("·실패 "   + std::to_string(shot_failed_))    : "")
               + (shot_orphan_    > 0 ? ("·고아 "   + std::to_string(shot_orphan_))    : "")
               + (shot_sentinel_  > 0 ? (" 🔴표지값 " + std::to_string(shot_sentinel_)) : "")
               + (shot_exhausted_ > 0 ? (" 🔴소진 " + std::to_string(shot_exhausted_)) : "")
               + (shot_nophone_   > 0 ? ("·폰없음 " + std::to_string(shot_nophone_))   : "");
        // 🔴 **분모가 없는 계기라 0 을 안 찍는다** — 대신 0이 아니면 반드시 찍는다.
        //   ⚠ 이건 "몇 번" 이 아니라 "지금 몇 개" 이므로 누적 표기(`N/M`)를 쓰면 오독된다.
        // 🔑 계기는 **지금값(최대 M)** 으로 낸다 — 최대가 0 이면 창 내내 없었다는 뜻이다.
        // 🔑 **둘을 같이 낸다** — 갈라 봐야 뜻이 갈린다:
        //   `즉시` = 값이 있었다(멀다) · `흡수` = 빈 칸이었다(못 쟀다 · 디바운스가 삼켰다)
        if (occ_fall_absorbed_ > 0 || occ_fall_fast_ > 0)
            s += " · 하강 즉시 " + std::to_string(occ_fall_fast_)
               + "/흡수 " + std::to_string(occ_fall_absorbed_)
               + "(즉시=값이 있어 멀다고 판정 · 흡수=빈 칸이라 한 프레임 더 봤다)";
        if (act_nocontrol_ > 0)
            s += " · 조작칸없음 " + std::to_string(act_nocontrol_)
               + "(명령 모듈인데 화면에 누를 것이 없다 — 자동 제어만이면 정상)";
        // 🔴🔴 **여기 `if` 를 다시 만들지 마라.** 몸통 없는 `if` 하나가 아래 `묶음미룸` 을 삼켜서,
        //   **`batch_deferred > 0`(경보)인데도 `mod_missing_ == 0` 이면 줄이 통째로 숨었다.**
        //   ★ 경보가 **무관한 값에 묶여** 있었다. `-Wall -Wextra` 도 안 잡는다(같은 열이라
        //     `-Wmisleading-indentation` 조차 안 문다). `git log -S` → **태생 결함**이었다.
        //   🔑 그리고 그 위 몇 줄에 이 파일 자신의 규칙이 적혀 있다 —
        //     *"한 번 뜨는 경고는 놓친다. 주기 요약에 계속 보이게 둔다."* **규칙을 적은 자리가 그것을 깼다.**
        //
        // 🔑 계기 + 창최대 + **분모**로 낸다(`센서갈림` 과 같은 꼴).
        //   ⚠ 분모가 없으면 `등록안온모듈 0` 이 두 뜻을 겸한다 —
        //     `0/5`(다섯이 다 왔다) 와 `0/0`(**이 devid 로 선언된 것이 아예 없다**).
        //     ★ 뒤쪽은 건강이 아니라 **조립표·배선 오설정**이고, **굽기·자리 교체 직후에 가장 잘 난다.**
        // 🔴🔴 **`0/0` 은 값이 아니라 경보다.** 분모를 옆에 두는 것만으로는 부족했다 —
        //   2026-08-27 에 monitor 의 도구가 `제외 0건(표지 **0건** 기준)` 이라고 **정직하게 찍고 있었는데
        //   본인이 읽고 지나갔다.** 도구가 `0/0` 이라 말했는데 사람이 `0/N` 으로 읽었다.
        //   ★ 그러니 **읽는 사람을 멈춰 세우는 표지**를 그 칸에 붙인다.
        s += " · 등록안온모듈 " + std::to_string(mod_missing_)
           + "/" + std::to_string(mod_declared_)
           + "(창최대 " + std::to_string(mod_missing_max_) + ")"
           // 🔴🔴 **표지 문구 안에 ` · ` 를 쓰지 마라.** 이 줄의 **칸 구분자**다 —
           //   넣으면 소비자 파서에 **없는 칸이 하나 생긴다**(실측: `또는` 이라는 유령 칸이 생겼다).
           //   ★ 값 안에 구분자를 넣는 것은 프로토콜을 깨는 것이다. 문구는 **짧게, 구분자 없이.**
           + (mod_declared_ == 0 ? " 🔴0/0(선언된 모듈이 없다 — 조립표·배선 또는 등록 대기)"
              : (mod_missing_ > 0 || mod_missing_max_ > 0 ? " 🔴" : ""));
        // 🔴 **끈 상태가 요약에 보여야 한다**(사용자 지시 2026-08-27 · 루트 조건).
        //   ⚠ 안 보이면 다음 사람이 *"왜 8081 에서 고른 자리가 안 먹지"* 를 못 푼다 —
        //   화면은 멀쩡히 돌고 접속도 되기 때문에 **아무 증상이 없다.**
        //   🔑 §"증상이 보이면 말하고 안 보이면 막아라" — 이건 **안 보이는 쪽**이라 말해야 한다.
        //   ★ 켜져 있을 때는 안 찍는다(정상은 조용해야 한다). **끈 것이 예외다.**
        if (!g_chooser_on)
            s += " · ⏸ **선택서비스 중지**(--no-chooser · 8081 화면은 살아 있다)";
        s += " · 묶음미룸 " + std::to_string(batch_deferred)
           + (batch_deferred > 0 ? " 🔴" : "");
        s += " · 조립표문제 " + std::to_string(asm_warn_)
           + (asm_warn_ > 0 ? " 🔴" : "");
        s += " · 예시devid " + std::to_string(devid_example_)
           + (devid_example_ > 0 ? " 🔴" : "");
        s += " · 이름충돌 " + std::to_string(mod_name_conflict)
           + (mod_name_conflict > 0 ? " 🔴" : "");
        // 🔴 **화면 수를 찍는다**. `S` 처리 안에서 `push_snapshot`·`state` 방송이 돌고
        //   그 비용은 **붙어 있는 화면 수에 비례**한다. 창 M 에서 하행 송신 지연이
        //   `≥2ms 9.4% → 40.9%` 로 늘었는데 **그 축이 아무 데도 안 남아서 원인을 못 갈랐다.**
        //   ⚠ **없는 축은 사후에 복원할 수 없다.** 지금부터 남긴다.
        //   🔑 최대값을 같이 낸다 — 평균만 내면 **한 창에 몰린 접속이 안 보인다.**
        {
            int ws_n = 0;
            for (std::map<sock_t, Conn>::const_iterator it = conns.begin(); it != conns.end(); ++it)
                if (it->second.kind == Conn::WS) ws_n++;
            // 🔴 **이름을 `화면` 에서 `WS접속` 으로 바꿨다**
            //   루트가 이 값을 **"화면에 끊김이 표시된 수"** 로 읽어 없는 결함을 사용자에게
            //   보고할 뻔했다. monitor 가 창 O 실측(주입기 8개를 띄우자 `1→9`)으로 반증해 멈췄다.
            //   🔑 **`화면` 은 세는 대상이 이름에 없다** — 사람이 보는 화면인지, WS 소켓인지.
            //     실제로 세는 것은 **`Conn::WS` 소켓 수**이고 탐침·하니스·주입기도 전부 포함된다.
            //   ⚠ §"두 뜻을 겸한 이름은 값이 갈리기 전까지 안 보인다" 와 같은 자리다 —
            //     사람이 화면 하나만 열어 두는 동안에는 두 뜻이 같은 값이었다.
            // 🔴 **이름을 `화면접속` 으로 되돌리지 않는다** — 위 이유가 그대로 살아 있다.
            //   대신 **포트를 붙인다.** 포트는 세는 대상을 **물리적으로 지목**하므로
            //   *"이 칸이 내가 찾는 것을 세는가"* 에 이름보다 정확하게 답한다.
            //   ⚠ 관측자가 **폰 접속 수를 찾다 이 칸을 고른다** — 포트가 없으면 헷갈린다.
            s += " · WS접속(" + std::to_string(g_port_web) + ") " + std::to_string(ws_n)
               + "(최대 " + std::to_string(ws_peak) + ")";
        }
        s += " · 치유 " + std::to_string(heal_fires) + "/" + std::to_string(heal_checks)
           + (heal_checks == 0 ? " 🔴검사0" : "")
           + " · 예약미해독 " + std::to_string(res_undecoded);
        // 🔴 A[1] — **세는 것과 보이는 것은 다른 일이다.** `mod_order_changed` 를 만들어 놓고
        //   ⚠ 요약에 안 내보내면 **관측자에게 없는 칸을 보라고 하는 셈**이 된다.
        //   🔑 **분모를 같이 낸다** — `발행` 이 없으면 `건너뜀 0` 은 건강이 아니라 **표본 0** 이다.
        //   각 칸이 낮아지는 *다른* 이유는 `docs/net/DESIGN-rid-width-and-quarantine.md` §5 표에 있다.
        s += " · rid 발행 " + std::to_string(ridpool_.allocN())
           + "(다음 " + std::to_string(ridpool_.nextWire())
           + "/" + std::to_string((unsigned)RID_SPACE)
           + (ridpool_.persistOn() ? " 영속" : " 🔴영속꺼짐") + ")"
           + " 격리 " + std::to_string((long long)ridpool_.quarSize())
           + " 건너뜀 " + std::to_string(ridpool_.skips())
           + " 강제 " + std::to_string(ridpool_.forced())
           + (ridpool_.forced() > 0 ? " 🔴" : "")
           + (ridpool_.exhausted() > 0 ? (" 고갈 " + std::to_string(ridpool_.exhausted()) + " 🔴") : "")
           + " · ACK 미상rid " + std::to_string(ack_unknown_rid)
           + " 자리불일치 " + std::to_string(ack_slot_mismatch)
           + (ack_slot_mismatch > 0 ? " 🔴" : "");

        // ── 노드 대장 (온보딩 2단계) ────────────────────────────────────────
        // 🔴 **분모를 같이 둔다.** `0` 혼자 서 있으면 건강처럼 보인다 —
        //   `0/0` 은 최소한 "표본이 없다"고 말해 준다(monitor 가 세운 규칙).
        // 🔑 그리고 **영속 여부를 매번 찍는다.** 대장이 메모리로만 돌고 있는데
        //   요약이 정상으로 보이면, 재기동 뒤에야 "다 사라졌다"를 알게 된다.
        s += " · 대장 " + std::to_string(ledger_.size()) + "노드"
           + "(신규 " + std::to_string(ledger_new_)
           + "/재확인 " + std::to_string(ledger_review_)
           + (ledger_review_ > 0 ? " 🔴" : "") + ")"
           + " 할당 " + std::to_string(ledger_.assignCount())
           + " 저장 " + std::to_string(ledger_.saves())
           + (ledger_.saveFails() > 0
                ? ("/실패 " + std::to_string(ledger_.saveFails()) + " 🔴") : "")
           + (ledger_.persistOn() ? "" : " **영속꺼짐** 🔴")
           + (ledger_.linesBad() > 0
                ? (" 깨진줄 " + std::to_string(ledger_.linesBad()) + " 🔴") : "");
        return s;
    }

    // ---------- 복구 지표 문장
    // **"끊긴 뒤 몇 초 만에 스스로 돌아왔는가"** 를 한 구절로 말한다.
    //
    // ⚠ `미복구` 는 정의상 **0 아니면 1**이다. 다음 오프라인 에피소드가 열리려면 그 전에
    // 온라인으로 올라와야 하고(엣지 판정), 온라인으로 올라왔다는 것이 곧 복구이기 때문이다.
    // 그래서 "미복구 Z회" 를 여러 건으로 부풀리지 않고 **지금 몇 초째 못 돌아오고 있는지**를
    // 같이 찍는다 — 요약에서 눈에 띄어야 할 것은 횟수가 아니라 그 진행 시간이다.
    // (진짜로 여러 번 셀 수 있는 "못 돌아왔다" 지표는 위의 `회수`(유휴 마감) 쪽이다.)
    std::string recovery_phrase() const {
        std::string s = "복구 " + std::to_string(recov_same_conn + recov_reconn) + "회";
        if (recov_same_conn + recov_reconn) {
            s += "(같은연결 " + std::to_string(recov_same_conn)
               + "/재연결 " + std::to_string(recov_reconn) + ")";
            long long med = recov_median_ms();
            s += " 중앙 " + (med < 0 ? std::string("-") : secs(med))
               + " 최악 " + secs(recov_worst_ms);
            if (recov_dropped) s += "(표본상한 초과 " + std::to_string(recov_dropped) + "건 제외)";
        }
        // 진행 중인 미복구는 **가장 시끄럽게** 적는다. 요약만 보면 안 보이고
        // 프레임 수가 멈춘 것을 사람이 눈치채야 했다 — 그 실패를 되풀이하지 않으려는 칸이다.
        if (offline_since_ms)
            s += " · 🔴미복구 1회(진행 " + secs(now_ms() - offline_since_ms) + ")";
        else
            s += " · 미복구 0";
        return s;
    }

    // 세션이 끝나는 길은 두 갈래다(recv 실패, send 실패). 둘 다 이리로 모은다 —
    // 한쪽만 계측하면 "끊겼는데 세션이 계속 열려 있는" 장부가 만들어진다.
    // (why 를 const char* 에서 std::string 으로 넓혔다 — 끊긴 이유에 errno 를 실어야
    //  keepalive 가 죽인 것과 그냥 수신 오류를 로그에서 가를 수 있다. REQ-0072)
    void end_ard_session(const std::string& why) {
        if (sess_start_ms) {
            fold_live_gap();                     // 끝나지 않은 침묵을 공백으로 확정하고 닫는다
            logf("-ARD", "세션#" + std::to_string(ard_sessions) + " 종료(" + why + ") — 지속 "
                         + hms(now_ms() - sess_start_ms)
                         + " · 프레임 " + std::to_string(sess_frames)
                         + " · 최대공백 " + secs(sess_max_gap_ms)
                         + " · 상대 " + (ard_peer.empty() ? std::string("?") : ard_peer));
            sess_start_ms = 0;
            // 명세 §0.4 ② — 세션 종료에 `last_seen` 을 굳힌다.
            // ⚠ **SIGKILL 로 죽으면 이게 안 남는다.** 받아들인 대가다 —
            //   대장의 목적은 *누가 있었나*이지 *언제까지 있었나*가 아니다.
            if (!park.devid.empty()) {
                ledger_.onSessionEnd(park.devid, epoch_ms());
                ledger_save("세션종료");
            }
        }
        if (!link_down_since) link_down_since = now_ms();
        // 설계 §2 — **세션이 끝나면 하행 큐를 비운다.** 옛 큐를 새 세션에 쏘면 장치가 모르는
        // 상태에 명령이 떨어진다. 비운 항목은 `device_offline` 로 **반드시 답한다**(§4-B 보장).
        // 🔴 **장치가 사라지면 모듈 상태를 모른다.** 안 지우면 `state` 가 끊긴 뒤에도
        //   `known:true` 로 낡은 값을 **사실로 주장한다** — 차단봉이 열려 있다고 그려 놓고
        //   실제로는 볼 수 없는 상태다. **모르면 덜 주장한다**(같은 블록에 적어 둔 규칙이다).
        //   ⚠ `z.modules` 는 남는다(지형은 유지) — **없어지는 것은 값이지 구조가 아니다.**
        park.mod_bits_n = 0;
        clear_downq("세션 종료");
    }

    // 🔴 **생사 판정은 노드마다 같은 규칙이다.** 주 노드만 이 판정을 갖고 나머지가
    //   `online` 플래그를 썼는데, 그 둘은 **다른 것을 재고 있었다** —
    //   플래그는 *"접속 사건이 있었나"* 이고 이 함수는 *"지금 말하고 있나"* 다.
    //   ⚠ 그래서 끊긴 보드가 `online:true` 로 남아 **화면이 그 모듈을 살아 있다고 그렸다.**
    bool node_online(const Node& n) const {
        return n.fd != BAD_SOCK && n.seen && (now_ms() - n.last_ms) < OFFLINE_MS;
    }
    bool device_online() const { return node_online(park); }   // 옛 이름 — 주 노드를 묻는다
    // 지금 살아 있는 노드 수
    int online_node_count() const {
        int c = 0;
        std::vector<const Node*> ns = all_nodes();
        for (size_t i = 0; i < ns.size(); i++) if (node_online(*ns[i])) c++;
        return c;
    }

