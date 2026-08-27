// uplink.h — 아두이노 상행(장치 → 서버) 파서. `struct Server` 의 몸통 조각 — server.cpp:186
// ⚠ 단독 컴파일 불가 · include 자리가 곧 문법이다 · 📖 server.cpp "목차"
    // ═══════════════════════════════════════════════════════════════════
    // 🔑 **왜 이 형태인가**: 코드를 *옮긴 것*이지 *고친 것*이 아님을 증명하려고.
    //   전처리 결과가 원본과 같으므로 **`.o` 가 바이트 동일**해야 한다.
    // ═══════════════════════════════════════════════════════════════════

    // ═══════════════════════════════════════════════════════════════════════
    // 🔴🔴 **상행 로그 요약** (명세 `docs/net/SPEC-2026-08-27-ard-log-summary.md`)
    //
    //   `←ARD` 낱줄이 현행 로그의 **80.5%** 인데 **정보량은 대부분 0** 이다.
    //   ✅ **변화는 낱줄, 나머지는 요약.** 🔴 그리고 **사건이 나면 다시 낱줄을 켠다** —
    //     ★ 정상 구간이 부피의 99% 이고 정보의 0% 다. 사건 주변이 그 반대다.
    //
    //   ⚠ **여기가 유일한 판정 자리다.** 여러 곳에서 정하면 규칙이 갈리고,
    //     갈리면 **무엇이 안 찍혔는지 아무도 모른다.**
    // ═══════════════════════════════════════════════════════════════════════

    // `S,<seq>,<occ>,<rsv>,<up>,<devid>,<ck>` 에서 칸을 뽑는다. 🔑 **여기서 해석하지 않는다** —
    //   `occ`·`rsv` 는 **16진 문자열 그대로** 든다(2진으로 읽으면 P1 `38`·P2 `14` 를 놓친다).
    static bool ard_fields(const std::string& line, long long* seq, std::string* occ,
                           std::string* rsv, long long* up) {
        std::vector<std::string> t; std::string cur;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == ',') { t.push_back(cur); cur.clear(); }
            else cur += line[i];
        }
        t.push_back(cur);
        // 🔴 **필드가 모자라면 실패다.** 옛 형식 `S,0,0,P2,31` 이 그것이고,
        //   서버 본체도 `f.size() >= 6` 으로 안 받는다. **여기서도 같게 판정한다.**
        if (t.size() < 6) return false;
        *seq = atoll(t[1].c_str()); *occ = t[2]; *rsv = t[3]; *up = atoll(t[4].c_str());
        return true;
    }

    // 🔴 **이 낱줄을 찍을 것인가.** `true` 면 찍고, `false` 면 요약에만 담는다.
    //   ⚠ 부작용이 있다 — 요약 상태를 갱신하고 사건이면 낱줄 창을 연다. **한 번만 불러라.**
    bool ard_line_worthy(const std::string& devid, const std::string& line) {
        const long long now = now_ms();
        long long seq = 0, up = 0; std::string occ, rsv;
        if (!ard_fields(line, &seq, &occ, &rsv, &up)) {
            // 🔵 **세고 찍는다.** 못 잡는 것은 정상이지만 **조용히 버리면 안 된다** —
            //   이 수가 늘면 *"장치가 옛 형식으로 보낸다"* 는 뜻이다(socket·monitor 합의).
            ard_short_frames_++;
            return true;                       // 🔑 형식이 낯설면 **일단 남긴다**
        }
        ArdSum& a = ard_sum_[devid];
        bool event = false;      // **사건** — 낱줄 + 그 뒤 `ARD_BURST_MS` 창을 연다
        bool once  = false;      // **한 줄만** — 창을 안 연다
        if (a.win_start == 0) {
            // 🔵 그 보드의 **첫 프레임** — 창의 시작이 보여야 하니 **한 줄은 남긴다.**
            //   🔴 다만 **창을 열지는 않는다.** 이 시스템은 보드가 하루 **50~66회** 재접속한다
            //     (실측 2026-08-26) — 매번 60초씩 낱줄이면 **하루 한 시간이 낱줄**이 된다.
            //   ★ 그리고 재접속 자체는 **이미 `+AUX 노드 접속` 으로 찍힌다.** 여기서 또 열 이유가 없다.
            //   🔑 정말 이상하면 **다음 프레임들이 스스로 켠다**(seq·uptime·공백이 곧 걸린다).
            a.win_start = now; a.seq_first = seq; a.up_first = up; once = true;
        } else {
            if (occ != a.occ || rsv != a.rsv)          event = true;   // 점유·예약 변화
            if (seq != a.seq_last + 1)                 event = true;   // seq 불연속(결손·중복)
            if (up < a.up_last || up > a.up_last + 30) event = true;   // uptime 역행·도약
            const long long gap = a.last_ms ? (now - a.last_ms) : 0;
            if (gap > a.gapmax_ms) a.gapmax_ms = gap;
            if (gap >= OFFLINE_MS)                     event = true;   // 공백 초과
        }
        a.seq_last = seq; a.up_last = up; a.occ = occ; a.rsv = rsv; a.last_ms = now; a.rx++;
        // 🔴 **사건이면 그 뒤 `ARD_BURST_MS` 도 낱줄로 켠다** — 조사에 필요한 것은 사건 **주변**이다.
        if (event) a.burst_until = now + ARD_BURST_MS;
        return once || event || now < a.burst_until;
    }

    // 🔵 **요약 줄을 낼 때가 됐나** — 매 박자에 부른다(`serveOneTick`).
    void ard_sum_tick() {
        const long long now = now_ms();
        for (std::map<std::string, ArdSum>::iterator it = ard_sum_.begin();
             it != ard_sum_.end(); ++it) {
            ArdSum& a = it->second;
            if (a.win_start == 0 || now - a.win_start < ARD_SUM_WIN_MS) continue;
            const long long span = a.seq_last - a.seq_first;
            const long long miss = (span + 1) - a.rx;
            char b[256];
            // 🔑 **`key=value` 다**(monitor 권고) — 구분자 오판·UTF-8 오프셋·순서 결속을 다 피한다.
            //   ★ 둘째 토큰 `sum` 이 낱줄(`S,…`)과 **첫 글자에서** 갈린다.
            snprintf(b, sizeof(b),
                     "%s sum win=%lld seq=%lld..%lld rx=%ld miss=%lld gapmax=%.1f "
                     "occ=%s rsv=%s up=+%lld mode=summary",
                     it->first.c_str(), (now - a.win_start) / 1000,
                     a.seq_first, a.seq_last, a.rx, miss < 0 ? 0 : miss,
                     a.gapmax_ms / 1000.0, a.occ.c_str(), a.rsv.c_str(),
                     a.up_last - a.up_first);
            logf("←ARD", b);
            a.win_start = now; a.seq_first = a.seq_last; a.up_first = a.up_last;
            a.rx = 0; a.gapmax_ms = 0;
        }
        // 🔴 **보드가 0대여도 줄을 낸다.** 안 그러면 **부재와 요약기 고장이 같은 모양**이 된다.
        if (ard_sum_.empty()) {
            if (ard_sum_none_at_ == 0) ard_sum_none_at_ = now;
            else if (now - ard_sum_none_at_ >= ARD_SUM_WIN_MS) {
                char b[128];
                snprintf(b, sizeof(b), "(none) sum win=%lld boards=0 mode=summary",
                         (now - ard_sum_none_at_) / 1000);
                logf("←ARD", b);
                ard_sum_none_at_ = now;
            }
        } else {
            ard_sum_none_at_ = 0;
        }
    }

    // ── 함수 ──────────────────────────────────────────────────────────────
    // ---------- 아두이노 라인 처리
    // ⏳ **호환 껍데기 — 자가검증 전용.** 실기 호출부 둘은 이미 `park` 를 명시로 넘긴다.
    //   자가검증 50여 곳을 이 조각에서 같이 고치면 **거동 변화 0 의 증명 범위가 넓어진다.**
    //   🔴 ②-b(소켓별 버퍼 라우팅)에서 없앤다. **그때까지 새 호출부는 이걸 쓰지 마라.**
    void on_ard_line(const std::string& line) { on_ard_line(park, line); }

    // 🔴 ②-a — **노드를 인자로 받는다.**
    //   지금 호출부는 둘 다 `park` 를 넘기므로 **거동 변화 0 이어야 하고, 산출물 대조로 증명한다.**
    //   🔑 이것이 슬롯 경로 추출의 전제다 — `park` 32회 참조가 인자로 바뀌지 않으면
    //     "노드 안에서 닫힌 파서"가 안 되고, 그러면 떼어 내도 전역을 계속 만진다(설계 §8.13).
    void on_ard_line(Node& n, const std::string& line) {
        // 🔑 **받은 바이트 수를 같이 남긴다** — `AT+CIPSEND` 조용한 잘림(③)의 유일한 판별자다.
        // 장치는 `SEND OK` 를 받으면 **성공으로 세므로 잘린 것을 모른다.**
        // **서버가 "몇 바이트 받았나"를 적어야 "보낸 만큼 왔나"를 대조할 수 있다.**
        // ⚠ 64B 이하에서는 값이 뻔하지만, **긴 줄 시험에서 이 한 칸이 시험의 본체**가 된다.
        {
            char rb[24];
            // 🔴 **`rxnl` = "LF 제외" 를 이름에 박는다.** 계측기 이름이 그 규칙을 말해야
            //   합산할 때 자동으로 갈린다(이 축은 양쪽이 어긋나기 쉽다):
            //     ① `V` 길이 공식 — LF 포함/제외로 **1B 일관 차이**
            //     ② 등록 바이트 — 내 합 50B ↔ 전선 **54B**(LF 4개)
            //     🔑 ③ **마지막 줄 뒤에는 LF 가 없다** → 55 가 아니라 54 다
            //   > **"LF 를 세는가" 만으로는 부족하다. "*몇 개* 를 세는가" 까지가 답이다.**
            snprintf(rb, sizeof(rb), " (rxnl=%zuB)", line.size());
            // 🔴🔴 **어느 보드가 말했나 — 둘째 토큰에 박는다.** `←ARD <devid> <프레임> (rxnl=…)`
            //   보드가 여럿이면 **한 로그에 n대의 프레임이 섞여 들어온다.** 장치 칸이 없으면
            //   관측 도구가 그것을 **한 계열로 합쳐** 주기·간격·비율을 전부 뒤섞는다.
            //   ⚠ 그리고 그 숫자는 **그럴듯하게 나온다** — 틀린 줄 아무도 모른다.
            //   🔑 `A`·`E`·`D` 프레임은 **전선에 devid 를 안 싣는다**(`S` 만 싣는다) —
            //     그래서 **로그가 유일한 장치 축**이다. 여기서 안 적으면 어디에도 없다.
            // 🔑 승격 전(신원 미상)은 `?` 로 적는다. **칸을 비우지 않는다** — 위치로 파는 도구가
            //   칸이 사라지면 다음 칸을 장치로 읽는다.
            // ═══ 🔴🔴 **낱줄을 찍을 것인가** (명세 2026-08-27 · REQ-0499 ③) ═══════
            //   ★ **판정을 한 곳에 둔다.** 여러 곳에서 "이건 찍자" 를 하면 규칙이 갈리고,
            //     갈리면 **무엇이 안 찍혔는지 아무도 모른다**.
            //   🔑 `S` 가 아닌 프레임(`D`·`A`·`E`)은 **언제나 낱줄**이다 — 드물고 전부 사건이다.
            const bool isS = (line.size() > 1 && line[0] == 'S' && line[1] == ',');
            if (!isS || ard_line_worthy(n.devid, line)) {
                logf("←ARD", (n.devid.empty() ? std::string("?") : n.devid) + " " + line + rb);
            }
        }

        // 🔴 **전제 감시 — 조건을 적었으면 그것을 보는 코드를 같은 자리에 둔다**(CLAUDE.md).
        // `DOWN_BATCH_MAX_N` 은 배출 6 에서 유도됐고, 배출 6 은 `S_worst ≤ 60B` 를 전제한다.
        // ⚠ **개정에서 그 전제가 깨지는데 서버는 아무것도 안 하고 계속 4건을 보낸다** —
        //   그게 이 감시가 없을 때의 모습이다. **적어 두기만 하면 다음 사람이 안 본다.**
        // 🔑 새 배출률을 **여기서 계산하지 않는다**(arduino 의 상수를 복제하게 된다).
        //   서버는 **"전제가 깨졌다"까지만 말하고**, 값은 원본을 가진 쪽이 준다.
        // 🔑 **장치가 말했다 = 조건이 바뀌었다.** 창 포기를 다시 무장한다.
        //    `S` 만이 아니라 **어떤 줄이든** 무장한다 — 깨진 줄도 "링크가 살아 있다"는 증거다.
        //    🔑 **말한 그 노드만 무장한다** — 남이 말했다고 이 보드의 링크가 산 것은 아니다
        n.dmax_armed = true;

        if (!line.empty() && line[0] == 'S') {
            if ((int)line.size() > s_max_b) s_max_b = (int)line.size();
            if ((int)line.size() > DEV_S_WORST_ASSUMED_B && !s_worst_warned) {
                s_worst_warned = true;
                char wb[224];
                snprintf(wb, sizeof(wb),
                         "전제 붕괴 — S 프레임 %dB > 가정 %dB. "
                         "DEV_ACK_DRAIN_PER_SLOT=%d(→ 하행 %d건/창)의 근거가 깨졌다. "
                         "arduino 에 배출률 재확인 필요(자리 수 n 이 커졌을 가능성)",
                         (int)line.size(), DEV_S_WORST_ASSUMED_B,
                         DEV_ACK_DRAIN_PER_SLOT, DOWN_BATCH_MAX_N);
                logf("!!", wb);
            }
        }

        // ---------- 소크 관측 — **체크섬 검사보다 먼저 센다.**
        // 여기서 재는 것은 "쓸 만한 프레임"이 아니라 **수신 자체의 공백**이다.
        // 깨진 줄도 링크가 살아 있었다는 증거이므로 공백을 끊는다.
        {
            long long t = now_ms();
            if (sess_last_line_ms) {
                long long gap = t - sess_last_line_ms;
                if (gap > sess_max_gap_ms) sess_max_gap_ms = gap;
                if (gap > all_max_gap_ms) { all_max_gap_ms = gap; all_max_gap_at = epoch_ms(); }
            }
            sess_last_line_ms = t;
            sess_frames++; all_frames++;
        }

        std::vector<std::string> f;
        if (!verify_line(line, f)) { drop_cksum++; logf("!", "체크섬 불일치 — 버림"); return; }
        if (f.empty()) return;

        if (f[0] == "S" && f.size() >= 6) {
            // 🔴🔴 **이 소켓의 장치가 맞는가.** `S` 는 자기 `devid` 를 싣는데(f[5]), 그것이
            //   이 소켓으로 등록된 것과 다르면 **보드 둘이 한 신원을 쓰고 있다는 뜻**이다.
            //   ⚠ 받아 주면 그 비트열이 **남의 노드 상태를 덮고**, 두 보드가 서로를 계속 지운다.
            //     그러면 자리 점유가 1.2초마다 뒤집히는데 **어느 쪽도 오류를 안 낸다.**
            //   🔑 폐기가 옳은 방향이다 — 모르는 값으로 판정하느니 안 하는 것이 낫다.
            if (!n.devid.empty() && f[5] != n.devid) {
                drop_unknown++;
                logf("!", "S 의 device 가 소켓과 다르다 — 버림 (소켓 " + n.devid
                          + " · 프레임 " + f[5] + "). **DEVICE_ID 가 겹쳤는지 확인해라**");
                return;
            }
            // 🔴 **삼중 검산 ③ — `S` 의 자리 폭이 선언 `n` 과 맞는가**(설계 §5).
            //    ①선언 n · ②실제 `D` 줄 수 · ③hex 폭 이 **한 함수(`moduleCount()`)에서 나오므로
            //    갈릴 수 없다. 갈리면 그 자체가 결함 신호다.**
            // ⚠ 옛 펌웨어는 등록을 안 하므로 `reg_n` 을 모르고, 그때는 이 검사를 안 탄다.
            //    (옛 10진 형식은 폭이 `n` 이고 hex 는 `ceil(n/4)` 라 규칙이 다르다)
            //
            // 🔴🔴 **`reg_done` 을 조건에서 뺐다** (arduino 가 찾은 사각 · 회신):
            //   🔴 `reg_done && reg_n > 0` 으로 좁히지 마라 — **등록에 성공한 노드에서만** 폭을 검사하게 된다.
            //   ★ 그런데 **대비하려는 고장이 바로 등록 실패다** —
            //     두 고장이 겹치면 **진단이 같이 꺼진다.** 남는 증상은 *"`S` 는 오는데 자리가
            //     안 붙는다"* 하나뿐이고 **폭 로그는 안 뜬다.**
            //   🔑 `D,*` 머리만 받아도 `reg_n` 은 안다(선언값이다). **그 순간부터 검사할 수 있다.**
            //   ⚠ 모듈 줄이 다 안 왔어도 안전하다 — 이 검사는 `mods` 를 안 보고 `reg_n` 만 본다.
            if (!n.reg_first_ms) n.reg_first_ms = now_ms();   // `REG_TIMEOUT` 의 기준
            if (n.reg_n > 0) {
                const size_t want = (size_t)((n.reg_n + 3) / 4);
                if (f[2].size() != want) {
                    reg_widthbad++;
                    char wb[176];
                    snprintf(wb, sizeof(wb),
                             "🔴 자리 폭 불일치 — S 의 폭 %zu, 선언 n=%d 이면 %zu 여야 한다. "
                             "같은 함수에서 나온 값이 갈렸다 (누적 %lld)",
                             f[2].size(), n.reg_n, want, reg_widthbad);
                    logf("!!", wb);
                }
            }
            long seq = atol(f[1].c_str());
            long long up = atoll(f[4].c_str());
            // §7.4(개정 8) — 순환을 접은 uptime 전진량 하나로 본다.
            // **seq 는 판정에 쓰지 않는다**: 1Hz 에서 18.2시간마다 합법적으로 0 을 지나간다.
            // 🔴 **이 노드의 직전 uptime 과 댄다.** 전역과 대면 둘째 보드의 첫 `S` 가
            //   주 노드의 가동시간과 비교되어 **매 프레임 재부팅으로 읽힌다** —
            //   그러면 그 노드는 계속 예약을 재동기화하고, **그 소음이 진짜 재부팅을 가린다.**
            bool reboot = uptime_says_reboot(n.uptime, up);

            // ── 세션을 가로지르는 판정  — **보고 전용** ──────────────
            // 새 세션의 첫 프레임(`ard_uptime < 0`)에서만 본다. 지금까지 서버는 이 순간을
            // 무조건 "재부팅"이라 불렀지만, 직전 세션의 uptime 을 기억하면 갈린다:
            //   · 되감겼다  → 장치가 정말 재부팅했다
            //   · 이어진다  → 장치는 살아 있었고 **링크만 다시 선 것**이다
            // 08-16 기준선에서 새 연결 36건 중 35건이 후자였다. 지금까지 전부 전자로 셌다.
            //
            // ⚠ 위 `reboot` 변수에는 **손대지 않는다.** 그것은 §7.4 판정이고 여기는 장부다.
            // ⚠ **이 장부는 아직 주 노드 전용이다**(`xs_*`·`ard_seq`·`ard_uptime`·`ard_dev`).
            //   노드별로 옮기는 것은 다음 단계다 — 지금 같이 옮기면 이번 경계에 축이 하나 더 늘어
            //   무엇이 깨졌는지 못 가른다.
            // 🔴 **모든 노드에서 판정한다.** 전에는 `&n == &park` 이라 주 노드만 나왔고,
            //   그래서 보조 넷의 재접속이 **리셋인지 링크인지 아무도 몰랐다**(분모 0).
            //   ⚠ 판정 규칙은 한 글자도 안 바꾼다 — **누구에 대해 도는지만** 넓힌다.
            // 🔴 **관문도 노드 자신의 것이어야 한다.** `ard_uptime` 은 **주 노드 전용 스칼라**라
            //   (`nodes.h` 의 주 노드 접속 경로에서만 `-1` 로 리셋된다) 그것을 쓰면
            //   `&n == &park` 을 뺀 의미가 사라진다 — 보조 노드는 **관문이 닫힌 채**로 지나간다.
            //   ✅ `n.uptime` 은 `session_reset()` 이 노드마다 `-1` 로 만든다(주·보조 둘 다).
            //   🔑 park 에서는 두 조건이 **같은 사건**이라 거동이 안 바뀐다.
            //   ⚠ 아래 `n.uptime = up` 은 이 블록 **뒤**에 있다 — 그래서 여기선 아직 옛 값이다.
            if (n.uptime < 0) {
                // 🔑 키는 **장치가 스스로 밝힌 이름**(`f[5]`)으로 통일한다.
                //   `n.devid`(서버의 기록)와 갈리면 **로그의 `device=` 와 장부의 키가 어긋나고**,
                //   그 증상은 `미상` 이 영원히 나오는 것이다 — 갈렸다는 신호가 안 보인다.
                //   ⚠ `f[5]` 는 위 `f.size() >= 6` 검사가 보장한다.
                ReconMem& mem = recon_[f[5]];
                // 공백 G = 직전 프레임 도착 → 지금(초). **되감김만 보면 안 된다**:
                // 재부팅했는데 공백이 길면 새 uptime 이 옛 값을 넘어서 "안 죽었다"로 오분류된다.
                //
                // 안 죽었다면 지금 uptime = 옛 uptime + G 이므로 **반드시 G 이상**이다.
                // 죽었다면 부팅이 공백 안에서 일어났으므로 **G 미만**이다. 그래서 G 가 기준이다.
                // 허용오차 2초: 장치 시계는 초 단위 절삭이고 도착 시각도 틱에 걸린다.
                long long G = mem.last_ms ? (now_ms() - mem.last_ms) / 1000 : -1;
                if (mem.uptime < 0 || G < 0) {
                    xs_reconnect_unknown++;      // 기억 없음/다른 장치 — 모른다고 말한다
                // 🔴 **앞으로 튀는 것도 본다** — 되감김 규칙의 **거울상**이다.
                //   ⚠ 전에는 아래쪽(`up < prev`)만 봤다. 그래서 uptime 이 **+82,718초** 튄
                //     프레임이 *"장치는 안 죽었다"* 로 기록됐고, **다섯 대 동시 재부팅이 넷으로 보였다.**
                //   🔑 판별자: **장치가 살아 있었다면 uptime 증가분은 공백을 못 넘는다.**
                //     넘으면 그 값을 **못 믿는 것**이지 "재부팅했다" 가 아니다 → **`미상`**
                //
                //   🔴 **문턱 60초의 근거**(오늘 45건 실측):
                //     Δ가 공백을 넘은 것 중 2위가 **초과 30초**, 1위가 **초과 82,651초**.
                //     그 사이가 **비어 있다.** 30~60초대는 밀린 프레임 일괄 도착으로 설명된다 —
                //     ⚠ 그것까지 `미상` 으로 세면 **정상을 못 믿는 것으로 만든다**
                } else if (up > mem.uptime + G + 60) {
                    // 🔵 **"오염" 이라고 부르지 않는다.** 우리가 아는 것은 *"이 값으로는 못 가른다"* 뿐이다.
                    xs_reconnect_unknown++;
                    logf("!", "재연결 판정: **미상** — device=" + f[5]
                              + " · uptime " + std::to_string(mem.uptime) + " → " + std::to_string(up)
                              + " (+" + std::to_string(up - mem.uptime) + "초) · 공백 "
                              + std::to_string(G) + "초 — **가동시간이 공백보다 크게 앞선다. 이 값을 못 믿는다**");
                } else if (up < mem.uptime || up + 2 < G) {
                    xs_reconnect_reboot++;
                    logf("⟳", "재연결 판정: **재부팅** — device=" + f[5] + " · uptime " + std::to_string(mem.uptime)
                              + " → " + std::to_string(up) + " · 공백 " + std::to_string(G) + "초"
                              + (up < mem.uptime ? " (되감김)" : " (공백보다 짧은 가동시간)"));
                } else {
                    xs_reconnect_link++;
                    logf("=", "재연결 판정: **링크 재접속**(장치는 안 죽었다) — device=" + f[5]
                              + " · uptime " + std::to_string(mem.uptime) + " → " + std::to_string(up)
                              + " (+" + std::to_string(up - mem.uptime) + "초) · 공백 "
                              + std::to_string(G) + "초");
                }
            }
            // 🔴 **노드별 기억을 갱신한다** — 세션이 끊겨도 남아야 다음 재접속에서 비교가 된다.
            //   ⚠ 이것이 없으면 위 판정이 **영원히 `미상`** 이다(기억이 없다).
            {
                // 🔑 **위 `mem` 과 스코프가 안 겹친다** — 살아 있는 참조가 삽입을 가로지르지 않는다.
                //   ⚠ *"map 이라서 안전"* 이 아니다. 그렇게 읽으면 다음 사람이 `unordered_map` 으로
                //     바꿀 때 **rehash 가 참조를 무효화한다**는 것을 안 본다. **참인 이유는 스코프다.**
                ReconMem& mem2 = recon_[f[5]];
                mem2.uptime = up; mem2.last_ms = now_ms();
                // 🔵 **정본이 둘인 동안 서로를 검산한다** — park 에서는 두 장부가 같은 프레임에
                //   같은 값을 넣으므로 **반드시 같아야 한다.** 어긋나면 이관이 충실하지 않은 것이다.
                //   🔑 다음 경계에서 합칠 때 **이 계수가 0 이었다는 것이 그 증거**가 된다.
                if (&n == &park && xs_uptime >= 0 && xs_uptime != mem2.uptime) recon_split_++;
            }
            if (&n == &park) {
                // 🔑 아래 `xs_*` 는 **주 노드 요약(소크 줄)** 이 쓰는 것이라 그대로 둔다.
                xs_uptime = up; xs_dev = f[5]; xs_last_ms = now_ms();
                ard_seq = seq; ard_uptime = up; ard_dev = f[5];
            }
            // 🔑 **이 노드의 것은 이 노드에 적는다.** `ard_last_ms`·`ard_seen` 은 `park` 의
            //   필드를 가리키는 별칭이라, 여기서 그것을 쓰면 **둘째 보드의 `S` 가 주 노드의
            //   수신 시각을 갱신한다** — 주 노드가 죽어도 살아 있는 것처럼 보인다.
            n.seq = seq; n.uptime = up;
            n.last_ms = now_ms(); n.last_epoch_ms = epoch_ms(); n.seen = true;

            // 🔴🔴 **자리 비트열 해독** — 형식이 둘이고 **틀리면 오류 없이 값만 어긋난다**
            //   옛 펌웨어 : 10진 문자열 `0110000010` — **한 칸에 한 자리**
            //   새 펌웨어 : hex `182` — 전체를 `n` 비트 정수로 보고 **슬롯 i = 비트 (n−1−i)**
            // ⚠ **`n` 을 모르면 hex 를 못 푼다.** 그리고 **첫 `S` 는 `D` 보다 먼저 온다**(명세 §5) —
            //   그때 `n=10` 을 가정해 풀면 **폭 검사도 체크섬도 통과하고 모든 비트가 어긋난다.**
            //   🔑 **모르는 값으로 해독하느니 안 하는 것이 낫다**(arduino 권고 · 채택).
            // ⚠ 이 구멍은 **실기에서 실제로 열린다** — 장치가 hex 를 보내는데
            //   서버가 10진으로 읽어 **엉뚱한 자리를 점유로 표시했다.** 폭 검사를 넣어 둔 것이
            //   **"처리된 것처럼" 보이게 만들었다.**
            int mod_state[REG_MODS_MAX];
            // 🔑 해독 규칙은 `decode_slot_bits()` 한 곳에 있다(원장 §8.23-(66)).
            // 🔴 **이 노드의 등록 폭으로 푼다.** 전역으로 풀면 P2 의 비트열을 P1 의 폭으로 읽는다
            int  occ_bits  = decode_mod_bits(n, f[2], mod_state);
            bool occ_known = (occ_bits > 0);
            // ⚠ **여기서 자리 배열로 옮기지 않는다.** 이 비트는 **노드-지역 idx** 이고
            //   자리 번호가 아니다. 환산은 `sync_parking_slots_from_nodes()` 가 결속표로 한다.
            // 🔑 **자리 열 개를 넘는 비트를 보관한다** — 조작 완료 판정이 이것을 읽는다.
            //   ⚠ 못 읽었으면 `mod_bits_n = 0` 으로 남긴다. **모른다와 0 은 다르다.**
            n.mod_bits_n = occ_bits;
            if (occ_bits > 0) for (int i = 0; i < REG_MODS_MAX; i++) n.mod_bits[i] = mod_state[i];
            // 🔴 **전이 대조는 여기다 — `S` 를 적용한 *바로 뒤*.** (SPEC §15.2)
            //   `E` 가 `S` 보다 먼저 오므로 `E` 자리에서 대면 **한 슬롯 낡은 비트**와 댄다.
            // 🔑 **무엇을 결정하나** : `전이불일치 > 0` 이면 장치가 판정을 슬롯당 1회보다
            //   많이 돌리고 있다는 뜻이다(§15.10 미이행) — `occ` 와 `E` 가 다른 표본에서 나온다.
            //   ★ 굽기가 그 변경을 실었는지를 **이 값 하나로** 판정한다.
            //   ⚠ 분모(`etrans_cmp_`)를 같이 센다 — 분모 없는 `0` 은 못 셀 것과 구별이 안 된다.
            if (occ_bits > 0) {
                for (int i = 0; i < REG_MODS_MAX; i++) {
                    if (!n.trans_cmp_pending[i]) continue;
                    n.trans_cmp_pending[i] = false;
                    if (i >= occ_bits) continue;      // 🔑 비트 폭 밖 — **모른다**. 세지 않는다
                    etrans_cmp_++;
                    if ((n.mod_bits[i] != 0) != (n.trans_r[i] != 0)) etrans_split_++;
                    // 🔴 **액추에이터의 배타 검사도 여기다** — 상태 훅이 있으면
                    //   `occ` 비트가 **그 반환값에서 나와야** 한다(비트 = `값 != 0`).
                    //   🔑 센서(`I*`)에는 안 건다 — 그쪽 비트는 **문턱 비교**라 `값 != 0` 이 아니다.
                    // ⚠ **`V` 시절에는 이 검사가 `V` 처리 안에 있었다** — 즉 **한 슬롯 낡은 비트**와
                    //   대고 있었다. 🔴 아무도 못 본 이유는 `actstate_cmp_` 가 **늘 0** 이었기
                    //   때문이다(상태 훅 붙은 액추에이터가 하나도 없었다).
                    //   ★ §"시험 경로에서 발생할 수 없는 상태를 세라" — **안 도는 검사는 안 틀린다.**
                    if ((size_t)i < n.mods.size() && !n.mods[i].second.empty()
                        && n.mods[i].second[0] == 'O' && n.mod_val_has[i]) {
                        actstate_cmp_++;
                        if ((n.mod_bits[i] != 0) != (n.mod_val[i] != 0)) actstate_split++;
                    }
                }
            }
            if (!occ_known) {
                // 🔴 미등록 + hex 로 보이는 폭 → **해독하지 않는다.** 등록 뒤 다음 `S` 부터 읽는다
                if (!occ_undecoded_warned) {
                    occ_undecoded_warned = true;
                    logf("!", "자리 비트열을 해독하지 않는다 — 등록 전이라 n 을 모른다(폭 "
                              + std::to_string(f[2].size()) + "). **등록되면 읽는다**");
                }
            }
            occ_undecoded += occ_known ? 0 : 1;

            // §2.4 tmask — **선택 필드다. 없으면 해제로 본다**(§2.1 규칙 8, 옛 펌웨어 수용).
            // f 는 체크섬을 뺀 필드들이므로 7번째(index 6)가 있으면 그것이 tmask 다.
            // ⚠ **잠복 결함을 미리 닫는다.** `f[6].size() >= 10` 으로 두면
            //   **10진 폭을 전제**했다. 지금은 장치가 이 칸을 아예 안 보내서(필드 6개) 안 돌지만,
            //   **tmask 가 hex 로 오는 순간 `res` 와 똑같이 조용히 죽는다.**
            //   🔑 arduino 가 확인해 줬다 — *"세 필드를 같이 바꿨으므로 읽는 쪽도 셋이다."*
            //   **잠복은 "나중에 밟는다"는 뜻이고 그때는 또 아무도 안 본다. 지금 닫는다.**
            int ovr_bits_[REG_MODS_MAX];
            bool armed = false;
            int  ovr_n_ = 0;
            if (f.size() >= 7 && f[6] != "-" && !f[6].empty()) {
                // 🔴 **칸이 있다는 것 자체가 "무장"이다.** 해독 성공 여부와 분리한다 —
                //   못 읽는다고 `armed=false` 로 답하면 **장치가 시험 모드인데 서버가 정상이라 믿는다.**
                //   못 읽으면 무장은 알리되 덮어쓰기 목록은 비운다(안전한 방향).
                armed = true;
                ovr_n_ = decode_mod_bits(n, f[6], ovr_bits_);
            }
            // 🔑 **시험 비트도 노드의 것이다.** 전역 10칸에 넣으면 보드가 둘일 때 서로 덮는다.
            if (n.test_armed != armed)
                logf("*", std::string("테스트 모드 ") + (armed ? "무장" : "해제")
                          + " (출처: " + n.devid + " 의 S tmask)");
            n.test_armed  = armed;
            n.test_bits_n = (ovr_n_ > 0) ? ovr_n_ : 0;
            for (int i = 0; i < REG_MODS_MAX; i++)
                n.test_bits[i] = (ovr_n_ > 0) ? ovr_bits_[i] : 0;

            if (reboot) {
                reboot_by_uptime++;                  // 소크 관측(REQ-0065) — 2차 방어선이 실제로 몇 번 걸리는가
                resync_reservations("uptime 전진량이 한 바퀴에 가깝다");
            }

            // §7.5 — occupied 1→0 이면 예약 소진.
            // 재부팅했거나 기준선이 없으면 **판정하지 않고 기준선만 세운다**(§7.5-1).
            // 이걸 빼면 재부팅으로 occupied 가 초기화될 때 있지도 않은 1→0 전이가 잡혀
            // 방금 재하달한 예약을 그 자리에서 죽인다.
            // 🔴🔴 **판정도 자리 갱신도 `sync_parking_slots_from_nodes()` 한 곳이 한다.**
            //   🔴 `slots[i] = occ[i]` 로 **노드의 모듈 순서를 자리 번호에 직결하지 마라.**
            //   보드가 하나일 때만 맞는 식이라, 둘째 보드가 붙으면 **그 첫 모듈이 자리 `A1` 을 덮는다** —
            //   그리고 그건 오류를 안 낸다. 엉뚱한 자리가 그럴듯하게 점유로 보일 뿐이다.
            //   🔑 환산은 결속표가 한다. 여기서는 **언제 판정할지만** 정한다.
            //   ⚠ **은퇴를 여기서 또 하지 마라** — 두 곳에서 돌면 예약이 두 번 은퇴한다.
            if (!(base_valid && !reboot))
                logf("=", "은퇴 기준선 설정 — 이 프레임은 전이 판정을 건너뛴다");
            sync_parking_slots_from_nodes(base_valid && !reboot);
            base_valid = true;

            // reserved 는 서버가 durable owner 다(§7.4). 아두이노 값으로 덮지 않는다.
            // 다만 서버가 0 인데 아두이노가 1 이면 세계관이 갈라진 것이므로 C 를 다시 내려 맞춘다(§7.6).
            // 미결 요청이 있는 자리는 제외 — 아직 ACK 를 못 받았을 뿐인 정상 상태다.
            // 🔴 이 조건을 `f[3].size() >= 10` 으로 두지 마라.
            //   장치가 hex 로 바뀌자(`21:12:09`) 폭이 3 이 되어 **조건이 영영 거짓** —
            //   자가 치유가 **한 번도 안 돌았는데 로그에 아무 흔적이 없었다.**
            //   ⚠ 오독이 아니라 **건너뜀**이라 `0` 조차 안 남았다. 그래서 **분모를 센다**(§8.23-(66)).
            int res_bits_[REG_MODS_MAX];
            int res_n_ = (f.size() >= 4) ? decode_mod_bits(n, f[3], res_bits_) : 0;
            if (res_n_ <= 0) {
                res_undecoded++;
                if (f.size() >= 4 && !res_undecoded_warned) {
                    res_undecoded_warned = true;
                    logf("!", "예약 마스크를 해독하지 않는다 — 등록 전이라 n 을 모른다(폭 "
                              + std::to_string(f[3].size()) + "). **등록되면 읽는다**");
                }
            } else {
                heal_checks++;               // 🔑 **분모** — 이 검사가 실제로 돌았다
                // 🔑 `n.res_bits[]` 에 사본을 두던 줄이 여기 있었다. **읽는 곳이 0 이라 지웠다** —
                //   치유 판정은 바로 아래에서 지역 `res_bits_` 로 끝나고, 화면도 예약은
                //   **서버의 `slots[].reserved`**(durable owner · §7.4)를 본다. 장치 비트는 대조용일 뿐이다.
                //   ⚠ 쓰기만 하고 안 읽는 필드는 **"그 상태가 있다"로 읽힌다.**
                // 🔴 **`i` 는 이 노드의 모듈 순서지 자리 번호가 아니다.** 그 둘을 직결하면
                //   `res_bits_[i]` 를 `SLOT_ID[i]` 로 읽었다 — 보드가 둘이면 **P2 의 예약이
                //   P1 의 자리로 치유된다.** 결속표로 환산한다.
                for (int i = 0; i < res_n_ && i < (int)n.mods.size(); i++) {
                    if (!res_bits_[i]) continue;
                    const std::string zid = zone_of_bound_module(n.devid, n.mods[i].first);
                    if (zid.empty()) continue;              // 어느 자리에도 안 붙은 모듈
                    const int si = slot_index(zid);
                    if (si < 0) continue;
                    if (slots[si].reserved != 0) continue;
                    if (has_pending_for(zid)) continue;
                    heal_fires++;
                    logf("⚠", std::string("불일치: 아두이노 ") + n.devid + "." + n.mods[i].first
                              + " → 자리 " + zid + " reserved=1, 서버 0 → C 재하달");
                    dispatch('C', BAD_SOCK, "", zid, "");
                }
            }
            // ═══ 내 창이 열렸다. 큐 전부를 한 거래로 내보낸다 ═══════════════════════
            //
            // 🔴 **왜 S 분기의 맨 끝인가**: 이 분기 안에서 하행이 새로 생긴다 —
            // 불일치 감지(`dispatch('C')`)와 uptime 재동기화(`resync_reservations`)가 그것이다.
            // flush 를 `ard_last_ms` 갱신 직후(위쪽)에 두면 **그 치유 `C` 가 다음 창까지 밀려**
            // 원장 §8.21 이 실측한 "갈린 구간 ≈1초"가 조용히 ≈2.2초로 늘어난다.
            // **실측값을 낡게 만드는 배치를 고르지 않는다.**
            //
            // ⚠ 그리고 이 자리 때문에 **승격 직후 두 줄의 순서가 뒤집힌다**(원장 §7.2):
            //     전: `⟳ 재하달` → `→ARD R` → `←ARD S`
            //     후: `⟳ 재하달`(큐) → `←ARD S` → `→ARD R`   ← **같은 틱이다**
            // `adopt_as_parking()` 이 `resync_reservations()` 를 부른 직후 `drain_ard_buf()` 가
            // 첫 프레임을 같은 틱에서 처리하므로 지연은 사실상 0 이고, **순서만 바뀐다.**
            // 🔑 **이것은 회귀가 아니라 이 설계의 정의다** — 서버는 장치가 말한 뒤에 말한다.
            // 면제(승격 시 즉시 송신)를 두지 않은 이유: 그 순간의 위상은 `ard_last_ms` 가
            // 아직 옛 세션 값이라 **측정할 수 없고**, 측정 못 하는 순간에 쏘는 것이 §8.15 다.
            // 🔴 **방송보다 먼저 쏜다.** 아래 둘을 앞에 두지 마라:
            //     write_log_if_changed()  (파일 쓰기)
            //     push_snapshot()         (**붙어 있는 화면 수에 비례**하는 방송)
            //   창 M 에서 하행 송신 지연이 `≥2ms 9.4% → 40.9%` 로 늘었고, 후보 하나가 이것이었다.
            //   🔑 **하행은 슬롯 창에 묶여 있고 방송은 안 묶여 있다.** 묶인 쪽을 먼저 보낸다.
            //   ⚠ **원래 배치의 이유는 그대로 지킨다**: 이 분기 안에서 하행이 새로 생기는 자리는
            //     `resync_reservations()`(재부팅 감지)와 불일치 치유 `dispatch('C')` 인데
            //     **둘 다 이 줄보다 위에 있다.** 아래 둘은 하행을 만들지 않는다 —
            //     그래서 순서를 바꿔도 §8.21 이 실측한 "갈린 구간 ≈1초"가 안 늘어난다.
            //   🔑 **화면이 스냅샷을 몇 ms 늦게 받는 것은 아무것도 안 깨뜨린다. 장치는 다르다.**
            // 🔴 **flush 보다 먼저 부른다.** 콜백이 낸 명령이 **이 창에 같이 나가야** 한다 —
            //   뒤에 두면 다음 창(1.2초 뒤)으로 밀린다.
            //   ⚠ 이 자리는 이미 하행을 만드는 곳이다(`resync_reservations` · 치유 `dispatch('C')`
            //     가 위에 있다). 그래서 새 종류의 위험이 아니다.
            notify_occupancy_changes();
            // 🔴 **`K` 가 `G` 보다 먼저다** — 판정 수치가 없는 채로 명령을 받으면
            //   장치가 옛 기준으로 답한다. 🔑 남은 묶음은 창마다 하나씩 빠진다(§15.4 예산).
            // 🔴🔴 **그리고 같은 창에 `G` 를 같이 보내지 않는다.**
            //   두 경로가 각자 자기 상한(64B)까지 담으면 **합이 링을 두 배로 넘긴다** —
            //   상한이 둘인데 **아무도 합을 안 세는** 모양이고, 그것이 우리가 여러 번 밟은 형태다.
            //   🔑 대가는 없다시피 하다 : `K` 는 **등록 직후 한두 창**에만 있고
            //     그 시점에 떠 있는 `G` 는 거의 없다(자리 결속이 방금 생겼다).
            //   ⚠ 그리고 **밀린 `G` 는 버리지 않는다** — 다음 창에 그대로 나간다.
            // 🔑 **이 노드의** 판정 수치가 밀려 있나. 전역으로 보면 남의 `K` 때문에
            //   이 노드의 하행이 쉰다 — 창 하나가 통째로 놀고 아무도 이유를 모른다.
            const bool cfg_busy = cfg_pending_for(n.devid);
            flush_measure_config(n);
            if (cfg_busy) {
                if (!downq.empty())
                    logf("…", "판정 수치를 먼저 보낸다 — 이 창은 하행을 쉰다 (device=" + n.devid
                              + " · 대기 " + std::to_string(downq.size()) + "줄 · 다음 창에 나간다)");
            } else {
                // 🔑 **창은 노드마다 따로 열린다.** `S` 를 보낸 그 노드의 큐만 배출한다 —
                //   전역으로 배출하면 P1 의 창에 P2 로 갈 줄이 실려 나간다.
                flush_downq(n, "S 도착 — 창 시작", false);
            }
            write_log_if_changed();
            push_snapshot();
        }
        // ── §15 전이 프레임 `E,<idx>,<r>,<v>,<ck>` ─────────────────────────────
        // 계약 정본 : docs/net/SPEC-sensor-value.md §15.3
        //
        // 🔴 **`V` 를 대체한다.** `V` 는 매 슬롯 전 모듈의 값을 흘렸고 **빈 칸이 92%** 였다 —
        //   대역의 대부분이 *"값이 없다"* 를 말하는 데 쓰였다.
        //
        // 🔑 **위치가 아니라 번호를 싣는다.** `V` 는 항목 i = 모듈 idx i 라는 암묵 계약이었고,
        //   장치가 센서끼리 압축하는 순간 **값이 엉뚱한 모듈에 붙었다**.
        //   `idx` 를 명시로 실으면 그 부류가 **원리적으로** 안 난다. 대가는 2B 다.
        //
        // 🔴 **이것은 사건이지 상태가 아니다.** 잃어도 `occ`(=`S`)가 정본이라 상태가 안 틀어진다.
        //   그래서 ACK 도 rid 도 순서 보장도 없다. **점유 판정에 쓰지 마라** — `occ` 가 이긴다.
        //
        // ⚠ **`r` 과 `occ` 의 대조는 여기서 하지 않는다.** `E` 는 `S` 보다 **먼저** 오므로
        //   이 자리의 `mod_bits` 는 **한 슬롯 낡았다.** 표시만 세워 두고 `S` 처리 끝에서 댄다
        //   (SPEC §4 "배치 순서에 걸린 것" 목록의 넷째).
        else if (f[0] == "E") {
            Node& en = n;
            if (f.size() < 4) { ebad_++; return; }
            if (!all_digits(f[1]) || !all_digits(f[2])) { ebad_++; return; }
            const long idxl = atol(f[1].c_str());
            const long rl   = atol(f[2].c_str());
            if (idxl < 0 || idxl >= (long)REG_MODS_MAX || (size_t)idxl >= en.mods.size()
                || (rl != 0 && rl != 1)) { ebad_++; return; }
            const size_t idx = (size_t)idxl;
            eframes_++;
            en.trans_r[idx]  = (int)rl;
            en.trans_ms[idx] = now_ms();
            en.trans_cmp_pending[idx] = true;

            // 값. 🔴 **빈 칸은 "못 쟀다" 다. `0` 으로 접지 마라** — `0` 은 "0cm" 이다.
            //   센티널(`SENSOR_NO_READING`·최소거리 미만)은 **여기 값으로 오면 안 된다**(계약).
            if (f[3].empty()) {
                en.mod_val_has[idx] = false;
                emiss_++;
            } else {
                char* endp = 0;
                const long v = strtol(f[3].c_str(), &endp, 16);
                if (endp == f[3].c_str() || (endp && *endp)) { ebad_++; return; }
                en.mod_val[idx]     = v;
                en.mod_val_has[idx] = true;
                en.mod_val_ms[idx]  = now_ms();
                evalues_++;
            }
        }
        // ── §5 등록 프레임 `D` ─────────────────────────────────────────────────
        // 🔴 **관측이지 제어가 아니다**(Node::reg_* 주석). 하행 경로를 안 바꾼다.
        // ⚠ 이 분기가 없으면 `D` 는 `drop_unknown`("AT 잡음 유입 의심")으로 떨어져
        //   **세션마다 11씩 오르며 거짓 경보를 만든다.**
        else if (f[0] == "D" && f.size() >= 3) {
            if (f[1] == "*") {
                // `D,*,<drain>,<n>` — 묶음의 맨 앞. **자기 완결적이라 언제 와도 같은 뜻이다.**
                // 🔴 `*` 는 모듈 `name` 이 될 수 없다(설계 §5). 여기 걸리는 것이 그 방어다 —
                //    필드 수·숫자 형식 둘 다 안 맞으면 **모듈로도 배출률로도 안 읽힌다.**
                if (f.size() < 4 || !all_digits(f[2]) || !all_digits(f[3])) {
                    reg_bad++;
                    logf("!", "D,* 형식 위반 — 버림 (name 이 '*' 인 모듈이거나 숫자가 아니다)");
                    return;
                }
                // 🔴 **명세 §7.4 가 약속한 검사다 — 적어 놓고 안 만들면 지켜지는 것처럼 보인다.**
                //   약속: *"새 모듈은 목록 끝에만 붙인다. 중간에 끼우지 않는다."*
                //   깨지면 **`idx` 가 밀려 전선에 나가 있던 `G` 가 다른 모듈을 친다** — 조용히.
                //   ⚠ 이 검사는 *"약속을 어겼나"* 만 잡는다. *"왜 어겼나"* 는 arduino 만 안다.
                // 🔑 `D,*` 와 등록 완료는 **다른 프레임**이다 — 멤버로 잇는다.
                // 🔴 **노드별로 든다.** 전역 한 벌이면 두 보드가 거의 동시에 재등록할 때
                //   서로의 직전 구성을 덮어 **없는 "구성 변경"** 을 만든다.
                n.prev_mods_snapshot = n.mods;
                n.reg_reset();
                n.reg_drain = atoi(f[2].c_str());
                n.reg_n     = atoi(f[3].c_str());
                if (n.reg_n < 0 || n.reg_n > REG_MODS_MAX) {
                    reg_bad++;
                    logf("!", "D,* 의 n=" + f[3] + " 이 범위 밖(0~"
                              + std::to_string(REG_MODS_MAX) + ") — 등록 무효");
                    n.reg_n = -1; return;
                }
                char b[160];
                snprintf(b, sizeof(b), "등록 시작 — 선언 drain=%d · n=%d (device=%s)",
                         n.reg_drain, n.reg_n, n.devid.c_str());
                logf("=", b);
                return;
            }
            // `D,<name>,<kind>` — 모듈 한 줄. **순서가 곧 `idx` 다**(등록 순서가 비트 자리를 정한다).
            if (n.reg_n < 0) { reg_bad++; logf("!", "D,* 없이 모듈 줄이 왔다 — 버림"); return; }
            if ((int)n.mods.size() >= n.reg_n) {
                reg_bad++;
                logf("!", "선언 n=" + std::to_string(n.reg_n) + " 보다 모듈 줄이 많다 — 버림");
                return;
            }
            n.mods.push_back(std::make_pair(f[1], f[2]));
            if ((int)n.mods.size() == n.reg_n) {
                n.reg_done = true;
                reg_ok++;
                // 🔴 **`kind` 를 캐시에 박는다 — 여기가 유일한 기회다.**
                //   이 노드가 끊기면 `mods` 가 비어 *"그 모듈이 센서였나 조작이었나"* 를
                //   물어볼 곳이 사라진다. 그런데 **끊긴 뒤에 그 답이 필요하다**(자리 비활성 판정).
                //   🔑 재등록에서 `kind` 가 바뀌면 그때 덮인다 — 장치가 새로 말한 것이 이긴다.
                kind_cache_put(n);
                bind_modules(n);          // 🔑 등록이 지형을 바꾼다 → epoch 이 여기서 오른다
                // 🔴 **삼중 검산 ①②** — 선언 `n` 과 실제 줄 수. ③(hex 폭)은 `S` 에서 본다.
                // 🔴 **앞부분 대조** — 겹치는 구간이 그대로인가. 하나라도 다르면 `idx` 가 밀린 것이다.
                if (!n.prev_mods_snapshot.empty()) {
                    size_t n_ov = n.prev_mods_snapshot.size() < n.mods.size()
                                ? n.prev_mods_snapshot.size() : n.mods.size();
                    for (size_t i = 0; i < n_ov; i++)
                        if (n.prev_mods_snapshot[i] != n.mods[i]) {
                            mod_order_changed++;
                            logf("!", "🔴 재등록에서 모듈 순서가 바뀌었다 — idx " + std::to_string(i)
                                      + " 가 `" + n.prev_mods_snapshot[i].first + "` → `"
                                      + n.mods[i].first + "`. **끝에만 붙인다는 약속이 깨졌다** "
                                      "(명세 §7.4). 떠 있는 조작 명령을 버린다");
                            // ⚠ **떠 있는 `G` 를 버린다.** 그 `idx` 는 이제 다른 모듈을 가리킨다 —
                            //   보내 놓고 결과를 기다리는 것보다 **실패로 끝내는 것이 정직하다.**
                            for (std::map<uint16_t, Pending>::iterator it = pend.begin();
                                 it != pend.end(); ) {
                                if (it->second.kind == 'G') {
                                    if (it->second.ws_fd != BAD_SOCK)
                                        send_err(it->second.ws_fd, it->second.browser_rid,
                                                 "node_unregistered",
                                                 "장치 구성이 바뀌어 조작을 취소했습니다");
                                    uint16_t drid = it->first;
                                    pend.erase(it++);
                                    rid_release(drid);
                                } else ++it;
                            }
                            break;
                        }
                }
                n.prev_mods_snapshot.clear();
                char b[192];
                snprintf(b, sizeof(b),
                         "등록 완료 — n=%d · drain=%d · 명령가능 %d개 (device=%s)",
                         // 🔴 **이 노드를 센다.** 무인자 판은 `park` 를 세므로, 둘째 보드의
                         //   등록 로그에 **주 노드의 개수**가 찍힌다 — 값이 그럴듯해서 안 보인다.
                         n.reg_n, n.reg_drain, reg_cmdable(n), n.devid.c_str());
                logf("=", b);

                // 🔴 **판정 수치를 내려보낸다** (SPEC §15.4) — 등록 직후, `G` 보다 먼저.
                //   🔑 장치에 저장하지 않으므로 **매 등록마다** 다시 보낸다.
                //     그래서 낡은 값이 원리적으로 안 남는다 — 서버 소스가 정본이다.
                // ⚠ **큐만 채운다. 여기서 보내지 않는다.**
                //   🔴 지금 이 순간 장치는 등록 프레임을 막 보내고 **`SEND OK` 를 기다린다** —
                //     그 AT 응답이 장치의 수신 링에 들어 있다. 같은 순간에 `K` 를 밀어 넣으면
                //     **등록 응답 + `K` 가 한 링을 나눠 쓴다**(arduino 지적).
                //   🔑 그 합이 링을 넘기면 **세션이 끊긴다** — 📖 docs/net/LEDGER.md
                //   ✅ 다음 `S` 창까지 미루면 그 겹침이 **원리적으로** 없어진다 —
                //     그 창은 이미 `w_srv()` 가 지키는 자리다. 대가는 **1.2초**뿐이다.
                //   ⚠ 명세(§15.4.1)가 처음부터 *"다음 슬롯부터"* 라고 적혀 있었다.
                //     **구현이 명세를 안 지키고 있었고, arduino 가 물어서 드러났다.**
                queue_measure_config(n);

                // ── 노드 대장 (온보딩 2단계 · `docs/net/DESIGN-node-ledger.md` §0.4 ①) ──
                // 🔴 **여기서만 부른다.** `reg_done` 이 참인 자리이므로 목록이 완전하다.
                //   부분 목록으로 지문을 접으면 **거짓 "구성 변경"** 이 된다(명세 §0.3).
                // ⚠ 대장은 **판정에 관여하지 않는다.** 아래 어떤 값도 결속·하행을 안 바꾼다.
                if (!n.devid.empty()) {
                    std::vector<NodeLedger::Mod> lm;
                    for (size_t mi = 0; mi < n.mods.size(); mi++)
                        lm.push_back(NodeLedger::Mod(n.mods[mi].first, n.mods[mi].second));
                    NodeLedger::State st =
                        ledger_.onRegister(n.devid, peer_host(n.peer), lm, epoch_ms());
                    if (st == NodeLedger::NEW)               ledger_new_++;
                    else if (st == NodeLedger::NEEDS_REVIEW) ledger_review_++;

                    const NodeLedger::Entry* e = ledger_.find(n.devid);
                    char lb[320];
                    snprintf(lb, sizeof(lb),
                             "노드 대장 — %s = **%s** · 지문 %s · 접속 %lld회 · 상대 %s%s",
                             n.devid.c_str(), NodeLedger::stateName(st),
                             e ? e->fp.c_str() : "?",
                             e ? e->sessions : 0,
                             peer_host(n.peer).c_str(),
                             st == NodeLedger::NEEDS_REVIEW
                               ? " 🔴 **구성이 대장과 다르다 — 사람이 봐야 한다.**"
                                 " ⚠ 원인이 *다른 보드* 일 수 있다(devid 가 아직 고유하지 않다). 상대 IP 를 같이 봐라"
                               : "");
                    logf("=", lb);
                    ledger_save("등록완료");
                }
                // 🔴 **등록이 끝나야 이 노드의 자리를 안다.** 결속이 방금 생겼으므로 여기서
                //   예약을 다시 내려보낸다 — 안 그러면 이 보드가 붙기 전에 잡힌 예약이
                //   **장치에는 없고 서버에만 있는 상태**로 남는다(§7.6 이 고치는 갈림).
                //   ⚠ 재접속마다 도는 것이 맞다. 장치는 재부팅으로 예약을 잃는다.
                resync_reservations_for(&n, "등록 완료");
            }
            return;
        }
        else if (f[0] == "A" && f.size() >= 4) {
            uint16_t rid = (uint16_t)atoi(f[1].c_str());
            std::string slot = f[2];
            int result = atoi(f[3].c_str());
            std::map<uint16_t, Pending>::iterator it = pend.find(rid);
            if (it == pend.end()) {
                // 🔴 **계수한다.** 여기는 격리 가정(설계 §3.1)이 깨졌을 때 오르는 칸이다 —
                //   `RID_QUARANTINE_MS` 보다 늦게 오는 ACK 이 없다는 것을 **잰 적이 없다.**
                //   ⚠ 재전송 중복도 같은 칸에 온다. **둘을 여기서 못 가른다** — 값이 오르면
                //   그때 로그 시각으로 갈라야 한다. 그 한계를 요약 표에 적어 뒀다.
                ack_unknown_rid++;
                logf("!", "모르는 rid 의 ACK — 무시 (재전송 중복일 수 있다) rid=" + std::to_string(rid));
                return;
            }
            // 🔴🔴 **보낸 보드에서 온 ACK 인가.** `rid` 는 서버 전역 풀이라 **보드를 안 가른다** —
            //   P2 가 우연히 같은 번호로 답하면 P1 의 명령이 그것으로 닫힌다.
            //   ⚠ 그러면 P1 의 진짜 ACK 은 *"모르는 rid"* 가 되고, **둘 다 아무 오류를 안 낸다.**
            //   🔑 그래서 `(rid, devid)` 가 짝이다 — 유니크 키가 복합키인 것과 같은 이유다.
            if (!it->second.devid.empty() && it->second.devid != n.devid) {
                ack_unknown_rid++;
                logf("!", "다른 노드의 ACK — 무시 rid=" + std::to_string(rid)
                          + " (보낸 곳 " + it->second.devid + " · 답한 곳 " + n.devid + ")");
                return;
            }
            Pending p = it->second;
            pend.erase(it);
            rid_release(rid);

            // ── 왕복 실측 (설계 §1) — **`W_srv` 를 상수로 두지 않기 위한 유일한 입력**
            // 이 값은 `RTT + 장치 처리시간` 이라 **RTT 의 상한**이고, 창을 좁히는 방향이라
            // 보수적 = 안전하다. **재전송된 건은 표본에서 뺀다** — `sent_ms` 가 마지막 시도
            // 시각이라 값은 맞지만, 병리 구간이 섞이면 창이 그 최악에 영구히 묶인다.
            if (!p.queued && p.sent_ms > 0 && p.tries <= 1) {
                long long rtt = now_ms() - p.sent_ms;
                if (rtt >= 0 && rtt < DOWN_SLOT_MS * 4) {   // 그 이상은 표본이 아니라 사건이다
                    rtt_last_ms = rtt; rtt_n++;
                    if (rtt > rtt_max_ms) rtt_max_ms = rtt;
                }
            }

            // ── 🔴 에코 자리 대조 — **장치 멱등 캐시 재전송의 서명**을 여기서 본다
            //
            // arduino `docs/arduino/LEDGER.md` §25.3: 장치는 최근 서로 다른 `rid` **16개**를
            // 캐시에 들고 있고, 그 안에서 값이 재등장하면 **명령을 적용하지 않고 옛 ACK 을
            // 재전송한다.** 그러면 **에코된 자리가 옛 명령의 자리**다.
            // ⚠ **서버는 ACK 을 받으므로 `ack_timeout` 이 안 뜬다 — 실패가 성공처럼 보인다.**
            // 🔑 그래서 이 계수가 **`rid` 폭을 줄인 이 변경의 안전 지표**다(설계 §5).
            //
            // ⚠ **탐지만 한다. 거동은 안 바꾼다.** 바로 아래 줄이 "전선 값이 유효한 자리면
            //   그걸 쓴다"인데 주석은 "전선 값을 믿지 않는다"라고 적혀 있다 — **둘이 어긋나 있고
            //   그것을 고치는 것은 거동 변경**이라 이 배포에 안 넣는다(설계 §6.1 · 루트 결정 대기).
            if (slot != "??" && slot_index(slot) >= 0 && !p.slot.empty() && slot != p.slot) {
                ack_slot_mismatch++;
                logf("!", "🔴 ACK 에코 자리 불일치 rid=" + std::to_string(rid)
                          + " 보낸자리=" + p.slot + " 에코=" + slot
                          + " — 장치 멱등 캐시 재전송 의심(arduino §25.3)");
            }

            // result=3 이면 slot 은 "??" 다(§2.4). 전선의 자리 값을 믿지 않고
            // **매핑표의 원래 자리**를 쓴다 — 상관 키는 rid 이고 원본은 서버가 들고 있다.
            if (slot == "??" || slot_index(slot) < 0) slot = p.slot;
            int idx = slot_index(slot);
            // **예약 상태를 건드리는 것은 R/C 뿐이다.** T 를 여기에 섞으면
            // 테스트 주입 ACK 가 cancel 로 취급돼 **그 칸의 예약을 지워 버린다.**
            if (result == 0 && idx >= 0 && (p.kind == 'R' || p.kind == 'C')) {
                if (p.kind == 'R') {
                    slots[idx].reserved = 1;
                    // 화면·스냅샷에는 **원본**을 보여 준다(번호판이면 번호판 그대로).
                    // 전선에 나간 값(p.user_id)은 잘렸거나 비어 있을 수 있다.
                    slots[idx].user_id = p.plate;
                    slots[idx].reserved_at = epoch_ms();
                } else {
                    slots[idx].reserved = 0;
                    slots[idx].user_id.clear();
                    slots[idx].reserved_at = 0;
                }
            }
            // 테스트 결과는 여기서 상태에 반영하지 않는다 — 무장/오버라이드의 진실은
            // 다음 S 프레임의 tmask 다(§12A.4). ACK 는 "명령이 처리됐다"까지만 말한다.
            // 🔴 **`result=3` 은 `ack_timeout` 과 다른 칸에 센다.**
            //   `3` = *"장치가 못 한다"* → **재시도해도 같다.**
            //   `ack_timeout` = *"안 갔다"* → **재시도에 뜻이 있다.**
            //   ⚠ 합쳐 두면 `실패 N` 을 보고 재시도를 늘리는데, 절반은 늘려도 소용이 없다.
            //   (§8.16 의 `error` 한 칸 · §8.23-(38) 과 같은 형태다.)
            if (p.kind == 'G') gate_ans++;       // 🔑 **응답** — result 와 무관하게 답이 온 것
            // 🔴 **명령 결과 콜백** — `result` 로 갈래를 가른다.
            //   0 = 콜백이 true 를 냈다 · 3 = 장치가 거절했다.
            //   ⚠ 무응답은 여기 안 온다(ACK 이 없으니까) — 재전송 소진 자리에서 따로 부른다.
            if (p.kind == 'G')
                notify_cmd(p, (result == 0) ? CmdResult::OK : CmdResult::REJECTED, result);
            if (result == 3) {
                dev_reject++;
                logf("!", std::string("장치가 거절했다(result=3) — ") + p.kind + " " + slot
                          + " rid=" + std::to_string(rid) + ". **재시도는 뜻이 없다**");
            }
            if (p.ws_fd != BAD_SOCK) send_ack(p.ws_fd, p.browser_rid, slot, result, p.kind);
            // 이음매 1: 직접 호출 → 이벤트. **같은 틱의 drain 이 같은 일을 한다**(2481행).
            // 한 틱에 ACK 가 여러 건 겹치면 기록·화면이 건별 → 1회로 접힌다 — 이미 옮긴
            // 3종과 같은 성질이고, 브라우저가 보는 최종 상태는 같다.
            emit_dev_ack(park_dev, rid, (uint8_t)result,
                         std::string("ACK ") + p.kind + " " + slot
                         + " result=" + std::to_string(result));
        }
        else {
            drop_unknown++;
            // ⚠ 이 칸이 올라도 **AT 잡음 유입이 아니다.**
            // 등록(`D`)이 들어온 뒤로는 **그 해석이 더는 유일하지 않다** — `D` 는 위에서 처리되므로
            // 여기 안 오지만, **새 프레임 종류가 생기면 옛 서버에서 여기로 떨어진다.**
            // 🔑 **`모름` 이 오르면 "잡음"이 아니라 "내가 모르는 프레임"부터 의심해라.**
            logf("!", "모르는 타입 — 조용히 버림");
        }
    }
