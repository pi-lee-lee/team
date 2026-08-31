// persist.h — data_log.json 읽기·쓰기 (§9). `struct Server` 의 몸통 조각 — server.cpp:184
// ⚠ 단독 컴파일 불가 · include 자리가 곧 문법이다 · 📖 server.cpp "목차"

    // ── 함수 ──────────────────────────────────────────────────────────────
    // ---------- data_log.json (§9)
    std::string bits(bool reserved_bits) {
        std::string s;
        for (int i = 0; i < 10; i++)
            s += char('0' + (reserved_bits ? slots[i].reserved : slots[i].occupied));
        return s;
    }
    void write_log_if_changed() {
        if (no_disk) return;                 // no_disk 면 파일을 안 건드린다
        // 테스트 상태도 키에 넣는다 — 안 넣으면 무장/주입이 파일에 반영되지 않고
        // 폴백 화면이 낡은 "해제" 상태를 계속 보여 준다(개정 4 가 막으려는 바로 그 증상).
        // §9.4(개정 9) — **키에 online 을 넣는다.** 안 넣으면 장치가 죽어도 비트열이 그대로라
        // 파일이 안 써지고 `device.online` 이 영원히 true 로 남는다 — 필드가 거짓말을 한다.
        // 넣으면 오프라인/복귀 **전이가 그 자체로 상태 변화**가 되어 그 순간 한 건이 기록된다.
        // (`last_frame_ts` 는 키에 넣지 않는다. 넣으면 매 프레임이 변화가 되어 초당 한 번 쓴다.)
        bool online_now = device_online();
        std::string key = bits(false) + "|" + bits(true) + "|" + (test_armed ? "A" : "-");
        for (int i = 0; i < 10; i++) key += char('0' + ((test_armed && test_ovr[i]) ? 1 : 0));
        key += online_now ? "|O" : "|X";
        // 🔴 **번호판도 키에 넣는다.** 안 넣으면 자리 비트가 그대로인 채 번호만 바뀔 때
        //   파일이 **안 써진다** — 재기동하면 그 번호가 사라진다.
        for (int i = 0; i < 10; i++) key += "|" + slots[i].plate + "/" + slots[i].plate_source;
        // 🔴 **노드 생사도 키에 넣는다.** 안 넣으면 둘째 보드가 죽어도 비트열이 그대로라
        //   파일이 안 써지고, 폴백 화면은 그 보드를 **영원히 살아 있다고** 그린다 —
        //   주 노드에서 이미 겪은 그 구멍이 **노드 수만큼** 있는 셈이다.
        {
            std::vector<Node*> kns = all_nodes();
            for (size_t i = 0; i < kns.size(); i++) {
                if (kns[i]->devid.empty()) continue;
                key += "|" + kns[i]->devid + (node_online(*kns[i]) ? "O" : "X");
            }
        }
        if (key == last_bits) return;            // §9.4 — 내용이 같으면 안 쓴다
        last_bits = key;

        std::ostringstream e;
        e << "{\"ts\":" << epoch_ms() << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq)
          // §9.1(개정 9) — 장치 생사. 이게 없으면 폴백 화면이 **죽은 장치의 낡은 값을
          // 실측처럼 그린다**(개정 4의 test_mode 누락과 같은 종류의 구멍이었다).
          // last_frame_ts 는 epoch 시각이고 "파일을 쓴 시각"이 아니다 — 둘이 갈리는 게 요점이다.
          << ",\"device\":{\"online\":" << (online_now ? "true" : "false")
          // ⚠ 한 번도 프레임을 못 받았으면 `0` 이 아니라 **`null`** 이다.
          // `0` 은 epoch 로 읽히므로 화면이 **1970년으로부터의 나이**를 그린다.
          // WS 스냅샷(`snapshot_json()`)과 **같은 규칙**이어야 한다 — 두 경로가 갈리면
          // 폴백 여부에 따라 화면이 다른 말을 한다.
          << ",\"last_frame_ts\":" << (ard_last_epoch_ms > 0
                                       ? std::to_string(ard_last_epoch_ms) : std::string("null"))
          << "}"
          << ",\"occupied\":\"" << bits(false) << "\",\"reserved\":\"" << bits(true) << "\"";
        // §9.1(개정 4) — 무장 여부와 칸별 주입 표시를 **파일에도** 넣는다.
        // WS 가 끊기면 브라우저는 이 파일로 폴백하는데, 이 두 필드가 없으면
        // **폴백 화면이 주입값을 실측처럼 그린다** — §12A.6 이 폴백 경로에서만 깨지고 있었다.
        {
            int n = 0;
            for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) n++;
            e << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
              << ",\"override_count\":" << n << "}";
        }
        // 🔴 **붙어 있는 아두이노 전부** — 위 `device` 는 주 노드 하나만 말한다.
        //   🔑 폴백 화면도 *"어느 보드가 죽었나"* 를 알아야 한다. WS 스냅샷의 `devices[]` 와
        //     **같은 키·같은 규칙**이어야 한다 — 두 경로가 갈리면 폴백 여부에 따라 화면이 다른 말을 한다.
        {
            e << ",\"devices\":[";
            std::vector<Node*> dns = all_nodes();
            bool first = true;
            for (size_t i = 0; i < dns.size(); i++) {
                const Node* d = dns[i];
                if (d->devid.empty()) continue;
                if (!first) e << ",";
                first = false;
                // 🔴 **WS 스냅샷(`snapshot_json()`)과 키가 같아야 한다.** 하나라도 빠지면
                //   화면이 폴백일 때만 다른 것을 그리고, **그 차이는 폴백일 때만 드러난다** —
                //   즉 **가장 관측이 어려운 경로에서만 갈린다.**
                //   ⚠ 그리고 없는 키를 화면이 `0` 으로 메우면 *"방금 부팅했고 프레임 0장"* 이라는
                //     **없는 사실**이 된다. 그래서 `uptime`·`seq` 를 여기도 싣는다.
                e << "{\"device_id\":" << jstr(d->devid)
                  << ",\"online\":" << (node_online(*d) ? "true" : "false")
                  << ",\"primary\":" << ((d == &park) ? "true" : "false")
                  << ",\"registered\":" << (d->reg_done ? "true" : "false")
                  << ",\"module_count\":" << (long long)d->mods.size()
                  << ",\"uptime\":" << (d->uptime < 0 ? 0 : d->uptime)
                  << ",\"seq\":" << (d->seq < 0 ? 0 : d->seq)
                  << ",\"last_frame_ts\":" << (d->last_epoch_ms > 0
                        ? std::to_string(d->last_epoch_ms) : std::string("null"))
                  << "}";
            }
            e << "]";
            // 🔑 **계산 주체를 하나로 둔다.** 이 값이 없으면 화면이 배열에서 직접 세게 되고,
            //   같은 규칙이 두 곳에 생긴다 — 두 곳이 갈리면 아무도 모른다.
            e << ",\"devices_online\":" << online_node_count();
        }
        // 🔑 **실제 주차 자리만 낸다.** `SLOT_ID` 열 칸을 고정으로 돌면 지형에 없는 자리가
        //   폴백 화면에 **빈 자리로 그려진다** — 운전자가 없는 자리를 보고 간다.
        e << ",\"slots\":[";
        {
            bool first = true;
            for (int i = 0; i < 10; i++) {
                if (!lot.find(SLOT_ID[i])) continue;
                if (!first) e << ",";
                first = false;
                e << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
                  << ",\"reserved\":" << slots[i].reserved
                  // 🔴 **차량번호는 카메라·사람이 준 텍스트 그대로**다(명세 §5).
                  //   숫자만 뽑지 않는다 — LCD 로 보내는 7자리 숫자는 **다른 축**이다.
                  // 🔴 그리고 **출처를 같이 적는다.** 안 남기면 수동 입력이 인식 성공에
                  //   섞여 **인식률이 조용히 부풀려진다**(12/15 가 15/15 로 보인다).
                  << ",\"plate\":"
                  << (slots[i].plate.empty() ? std::string("null") : jstr(slots[i].plate))
                  << ",\"plate_source\":"
                  << (slots[i].plate_source.empty() ? std::string("null")
                                                    : jstr(slots[i].plate_source))
                  << ",\"overridden\":" << ((test_armed && test_ovr[i]) ? 1 : 0) << "}";
            }
        }
        e << "]}";

        log_entries.insert(log_entries.begin(), e.str());       // 최신이 앞
        while ((int)log_entries.size() > LOG_KEEP) log_entries.pop_back();

        std::string body = "[\n";
        for (size_t i = 0; i < log_entries.size(); i++) {
            body += "  " + log_entries[i];
            if (i + 1 < log_entries.size()) body += ",";
            body += "\n";
        }
        body += "]\n";

        atomic_write_log(body);
    }

    // tmp 에 다 쓰고 원자적으로 갈아끼운다 (§9.2). 잠금도 복사본도 필요 없다.
    // **쓰기 경로는 이것 하나다** — 기동 시 빈 배열을 만들 때도 이 함수를 쓴다.
    void atomic_write_log(const std::string& body) {
        const std::string tmps = data_path("data_log.json.tmp");
        const std::string dsts = data_path("data_log.json");
        const char* tmp = tmps.c_str();
        const char* dst = dsts.c_str();
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) { logf("!", "data_log tmp 열기 실패"); return; }
            f << body;
            f.flush();
        }
        // 실패하면 **원인 코드를 반드시 남긴다.** 안 남기면 data_log.json 이 조용히 옛 내용을
        // 유지하는데 로그에는 아무것도 없다. 윈도우에서 백신·편집기가 파일을 잠깐 잡아
        // ERROR_SHARING_VIOLATION(32) 이 나는 것은 흔한 일이라 이 구분이 실제로 필요하다.
#ifdef _WIN32
        // 윈도우는 대상이 있으면 rename 이 실패한다 (§9.3).
        // "먼저 지우고 rename" 으로 흉내내면 그 틈에 읽은 클라이언트가 404 를 본다 — 원자성이 깨진다.
        if (!MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
            DWORD e = GetLastError();
            char b[128];
            snprintf(b, sizeof(b), "MoveFileEx 실패 GetLastError=%lu%s",
                     (unsigned long)e,
                     e == 32 ? " (ERROR_SHARING_VIOLATION — 다른 프로그램이 파일을 잡고 있다)" : "");
            logf("!", b);
        }
#else
        if (rename(tmp, dst) != 0) {
            char b[128];
            snprintf(b, sizeof(b), "rename 실패 errno=%d (%s)", errno, strerror(errno));
            logf("!", b);
        }
#endif
    }

    // ── 🔴 A[1](B) 커서 영속 — **재시작을 건너 단조성을 유지한다**
    //
    // ⚠ **기본은 꺼져 있다**(`rid_persist_on = false`). 자가검증이 만드는 수십 개의 `Server` 가
    //   실기 커서 파일을 덮어쓰는 것을 **구조적으로** 막는다 — 시험이 운영 상태를 건드리면
    //   그건 시험이 아니라 사고다. **실기 기동 경로만 `rid_cursor_load()` 를 부른다.**
    // ── 🔴 `rid` 는 이제 `RidPool` 의 책임이다
    //
    //   🔑 상태 열 개와 함수 넷을 이 구조체 안에 흩어 두지 마라. **자료가 아니라 책임을 옮긴다** —
    //   커서·격리표·해제 순번·블록 예약·디스크 경로가 전부 `ridpool.h` 뒤로 갔다.
    //   🔑 **여기 남는 것은 "무엇이 못 쓰는 rid 인가" 하나다.** 그건 `pend` 를 가진 이쪽만 안다.
    //   ⚠ 계수는 그대로 요약에 낸다 — **감출 것은 복잡함이고 드러낼 것은 상태다.**
    struct PendHas {
        const std::map<uint16_t, Pending>* m;
        bool operator()(uint16_t r) const { return m->count(r) != 0; }
    };
    uint16_t alloc_rid() {
        PendHas in; in.m = &pend;
        return ridpool_.alloc(in, now_ms());
    }
    void rid_release(uint16_t rid) { ridpool_.release(rid, now_ms()); }
    // ── 노드 대장 (온보딩 2단계 · `docs/net/DESIGN-node-ledger.md`) ─────────
    // 🔑 **경로를 만드는 것은 서버의 일이다**(오프셋에 따라 갈린다). 대장은 경로를 받기만 한다.
    // ⚠ `no_disk`(자가검증)면 빈 경로를 준다 — 대장이 스스로 "영속 안 함"으로 돈다.
    void ledger_load() {
        ledger_.load(no_disk ? std::string() : node_ledger_path(), srv_id);
        if (!ledger_.persistOn()) {
            // 🔴 조용히 넘어가면 **"재기동했더니 등록이 사라졌다"의 원인을 못 찾는다.**
            logf("!", "노드 대장 — **영속 안 함**"
                      + std::string(no_disk ? " (자가검증)" : " (HOME 없음 — 메모리로만 돈다)"));
            return;
        }
        char b[320];
        snprintf(b, sizeof(b),
                 "노드 대장 — %lld 노드 이어받음 · 할당 %zu · 깨진줄 %lld · 모르는줄 %lld · %s",
                 ledger_.loaded(), ledger_.assignCount(),
                 ledger_.linesBad(), ledger_.linesUnknown(), ledger_.path().c_str());
        logf("=", b);
    }
    // 🔴 **사건이 있을 때만 쓴다**(명세 §0.4). 매 프레임·매 슬롯이 아니다.
    //   `save()` 는 `dirty` 가 아니면 아무것도 안 하므로 불러도 싸다.
    void ledger_save(const char* why) {
        if (!ledger_.dirty()) return;
        if (!ledger_.save(srv_id))
            logf("!", std::string("노드 대장 저장 실패(") + why + ") — 실패 누적 "
                      + std::to_string(ledger_.saveFails()));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // 카메라 요청 대장 — 발급 · 응답 · 조회 · 영속  (SPEC-camera-pull.md)
    // ═══════════════════════════════════════════════════════════════════════

    Shot* shot_find(long long id) {
        for (size_t i = 0; i < shots_.size(); i++) if (shots_[i].id == id) return &shots_[i];
        return 0;
    }
    const Shot* shot_find(long long id) const {
        for (size_t i = 0; i < shots_.size(); i++) if (shots_[i].id == id) return &shots_[i];
        return 0;
    }

    // 🔴 **판정은 여기(엔진)에 둔다.** 공개 래퍼(`ParkingServer::camera*`)는 한 줄 전달만 한다 —
    //   래퍼에 로직을 두면 **시험이 못 닿는 자리**가 생긴다(래퍼는 `Impl` 정의보다 앞에 있다).
    std::string shot_plate(long long id) const {
        const Shot* sh = shot_find(id);
        // 🔴 셋 다 `"-1"` 이다(없다 · 대기 · 실패). **거짓말은 아니다** — 셋 다 번호판이 없다.
        //   왜 없는지는 `shot_state()` 가 답한다.
        if (!sh || sh->state != CAM_READY) return std::string("-1");
        return sh->plate;
    }
    int shot_state(long long id) const {
        const Shot* sh = shot_find(id);
        return sh ? sh->state : (int)CAM_NONE;
    }
    long long shot_age(long long id) const {
        const Shot* sh = shot_find(id);
        if (!sh) return -1;
        const long long age = epoch_ms() - sh->req_ms;
        return age < 0 ? 0 : age;
    }

    // 🔴 폰이 하나라도 붙어 있나. **안 붙었으면 발급하지 않는다** —
    //   발급해 놓고 못 보내면 **영원히 CAM_PENDING 인 유령**이 남는다.
    bool phone_online() const { return !phones.empty(); }

    // 🔴 **최근 미결 촬영이 있나** — 있으면 그 요청번호, 없으면 0.
    //   ⚠ **시한이 반드시 필요하다**: `CAM_PENDING` 은 **스스로 안 풀린다**
    //     (폰이 답 없이 죽으면 이 파일이 위에서 말한 *"영원히 CAM_PENDING 인 유령"* 이 남는다).
    //     시한 없이 막으면 그 뒤로 **버튼이 영영 안 눌린다** — 고치려던 것보다 나쁘다.
    //   🔑 창 10초의 근거: 실측 왕복 **3초**를 덮고,
    //     화면 시한 15초보다 **짧다**. 즉 화면이 포기하기 전에 서버가 먼저 푼다.
    long long shot_recent_pending(long long window_ms = 10000) const {
        const long long now = epoch_ms();
        for (size_t i = shots_.size(); i-- > 0; )
            if (shots_[i].state == CAM_PENDING && now - shots_[i].req_ms < window_ms)
                return shots_[i].id;
        return 0;
    }

    // 🔴🔴 **진행 중인 요청을 닫는다**(2026-08-27 · 설계 [C]).
    //   ⚠ **정정(2026-08-27)** — 내가 처음에 근거를 틀리게 적었다. 사실은 이렇다:
    //     ~~`shot_recent_pending()` 이 앞 요청번호를 재사용해 앞차 번호가 뒤차에 붙는다~~
    //     🔵 **아니다.** `cameraShoot()`→`shot_issue()` 는 그 함수를 **안 부른다.**
    //       부르는 곳은 `wsapi.h` 의 시험 입구(`shoot_now`) 하나뿐이다.
    //   ✅ **그래도 닫아야 한다. 진짜 이유는 셋이다:**
    //     ① 안 닫으면 그 요청이 **영원히 `CAM_PENDING`** 이다 — 이 파일이 위에서 경고한 *"유령"* 이다
    //     ② 🔴 **화면이 끝을 못 본다** — `broadcast_shot_result` 가 안 나가서 *"촬영 중"* 이 영영 남는다
    //     ③ 늦은 답이 오면 `CAM_READY` 로 **되살아난다** — 그때 그 번호가 **어느 차 것인지 아무도 모른다**
    //   🔑 그리고 닫아야 `shot_late_` 가 그 늦은 답을 **세고 버린다.** 안 닫으면 조용히 쓰인다.
    //   🔑 `why` 는 **사유 코드**다(`stale` · `phone_gone` · `car_gone` · `cap`). 화면이 그대로 그린다.
    // 🔑 실패 사유를 그대로 돌려준다. **해석하지 않는다** — 어휘의 정본은 앱이다(SPEC §4.1).
    //   ⚠ 끝나지 않았거나 성공했으면 빈 문자열이다. **빈 값을 "사유 없음" 으로 그리지 마라.**
    std::string shot_reason(long long id) const {
        for (size_t i = shots_.size(); i-- > 0; )
            if (shots_[i].id == id) return shots_[i].reason;
        return std::string();
    }

    bool shot_cancel(long long id, const std::string& why) {
        Shot* sh = shot_find(id);
        if (!sh || sh->state != CAM_PENDING) return false;
        sh->state = CAM_FAILED; sh->reason = why; sh->ans_ms = epoch_ms();
        shot_cancelled_++;
        logf("=", "촬영 요청 " + std::to_string(id) + " 를 닫는다 — 사유 " + why
                  + " (누적 " + std::to_string(shot_cancelled_) + "회)");
        broadcast_shot_result(id, false, std::string(), why);
        shots_save();
        return true;
    }

    // 발급 + 송신. 🔴 **둘을 한 함수에 둔다** — 갈라 두면 "발급했는데 안 보냄" 이 생긴다.
    long long shot_issue() {
        if (!phone_online()) {
            // 🔴 **로그를 아낀다.** 폰이 없는 동안 호출자가 주기적으로 부르면(샘플은 10초)
            //   이 줄이 **하루 8,600줄**이 되어 로그를 덮는다.
            //   🔑 첫 세 번은 찍고 그 뒤는 100회마다 — **누적을 같이 내서 "몇 번 시도했나" 가 남는다.**
            //   ⚠ 계수 자체는 매번 올린다. **아끼는 것은 출력이지 관측이 아니다.**
            shot_nophone_++;
            if (shot_nophone_ <= 3 || shot_nophone_ % 100 == 0)
                logf("!", "촬영 요청 실패 — 폰(digitcam)이 안 붙어 있다. "
                          "요청번호를 발급하지 않는다 · 누적 " + std::to_string(shot_nophone_));
            return -1;
        }
        const long long today = shot_today_yyyymmdd();
        if (today <= 0) { logf("!", "촬영 요청 실패 — 벽시계를 못 읽었다"); return -1; }
        if (today != shot_day_) { shot_day_ = today; shot_seq_ = 0; }   // 날짜가 바뀌면 1부터
        if (shot_seq_ >= SHOT_SEQ_MAX) {
            // 🔴 **되감지 않는다.** wrap 하면 같은 요청번호가 그날 두 번 생기고
            //   `cameraPlate()` 가 **남의 번호판을 준다 — 조용히.**
            //   시끄럽게 거절하는 쪽을 고른다(§"틀렸을 때 조용한 쪽을 기본으로 두지 마라").
            shot_exhausted_++;
            if (shot_exhausted_ == 1)
                logf("!", "촬영 요청 소진 — 오늘 seq 9999 를 다 썼다. 되감지 않고 거절한다");
            return -1;
        }
        shot_seq_++;
        Shot sh;
        sh.id = shot_day_ * 10000 + shot_seq_;
        sh.state = CAM_PENDING;
        sh.req_ms = epoch_ms();
        // 🔑 링은 **넣기 전에** 비운다 — 넣고 나서 비우면 방금 넣은 것이 밀릴 수 있다(cap=1 극단)
        while (shots_.size() >= SHOT_RING_CAP) { shots_.pop_front(); shot_evicted_++; }
        shots_.push_back(sh);
        shot_issued_++;

        // 🔴 **폰이 둘 이상이면 한 요청이 여러 촬영을 만든다** — 서버는 어느 폰이 그 자리인지 모른다.
        //   대장은 **첫 응답만** 담지만(`shot_answer`), 번호판은 **전부** `on_plate()` 를 탄다(§7).
        //   ⚠ 게이트가 하나인 지금 지형에서는 **같은 차 = 같은 번호판**이라 중복 갈래가 걸러 준다.
        //     게이트가 둘이 되면 **한 요청이 두 자리를 배정**한다 — 그때 이 로그가 신호다.
        //   🔑 막지 않고 **보이게** 만든다: 폰이 여럿인 것 자체는 정상 운용일 수 있다
        //     (§"증상이 보이면 말하고 안 보이면 막아라" — 이 증상은 조용해서 말해 줘야 한다)
        if (phones.size() > 1) {
            shot_multiphone_++;
            if (shot_multiphone_ <= 3 || shot_multiphone_ % 100 == 0)
                logf("!", "폰이 " + std::to_string(phones.size())
                          + "대 붙어 있다 — 촬영 요청 " + std::to_string(sh.id)
                          + " 이 **전부에게** 나간다. 대장은 첫 응답만 담고, "
                            "나머지 번호판도 자리 배정 경로를 탄다. "
                            "게이트가 하나면 무해하지만 둘이면 한 요청이 두 자리를 배정한다 · 누적 "
                          + std::to_string(shot_multiphone_));
        }

        char b[64]; snprintf(b, sizeof(b), "SHOOT,%lld\n", sh.id);
        const std::string line(b);
        // 🔴 붙어 있는 폰 **전부** 에게 보낸다 — 어느 폰이 그 자리인지 서버는 모른다.
        //   ⚠ 여러 대면 여러 응답이 온다. **첫 응답이 이긴다**(아래 shot_answer).
        for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it)
            send_raw(it->first, line.data(), line.size(), "폰");
        logf("→폰", "촬영 요청 " + std::to_string(sh.id));
        shots_save();
        return sh.id;
    }

    // 응답. 🔑 **첫 응답이 이긴다** — 이미 끝난 건은 안 덮는다(늦게 온 두 번째를 무시).
    //   ⚠ 안 그러면 폰이 둘일 때 **나중 것이 앞 것을 지운다.**
    // 🔴🔴 **촬영 결과를 화면에 방송한다** — `ack` 는 *"요청을 만들었다"* 까지이고
    //   **결과는 폰이 답한 뒤에야 안다.** 그 둘을 같은 봉투에 담을 수 없다.
    //   🔑 **새 타입**이라 모르는 화면은 프레임째 무시한다(하위호환 공짜 · `queued` 와 같은 패턴).
    //   ★ 그리고 **세 갈래를 한 낱말로 뭉치지 않는다** — 사람이 할 일이 다르기 때문이다:
    //     `ok:true`                        번호가 왔다
    //     `reason:"recognize_failed"` 등   **찍었는데 못 읽었다** → 판을 다시 대라
    //     (요청 자체가 안 만들어진 경우는 `ack` 가 아니라 `error device_offline` 로 이미 갈렸다)
    void broadcast_shot_result(long long id, bool ok,
                               const std::string& plate, const std::string& reason) {
        std::ostringstream o;
        o << "{\"type\":\"shot_result\",\"shot\":" << id
          << ",\"ok\":" << (ok ? "true" : "false")
          << ",\"plate\":" << (plate.empty() ? std::string("null") : jstr(plate))
          << ",\"reason\":" << (reason.empty() ? std::string("null") : jstr(reason)) << "}";
        ws_broadcast(o.str());
    }

    void shot_answer(long long id, const std::string& plate, const std::string& err) {
        Shot* sh = shot_find(id);
        if (!sh) {
            // 🔑 재기동 뒤 늦게 온 응답이 정확히 이 모양이다. **세고 버리지 않는다** —
            //   번호판 자체는 호출자가 push 경로로 처리한다.
            shot_orphan_++;
            return;
        }
        if (sh->state != CAM_PENDING) {
            // 🔴 **조용히 버리지 않는다**(2026-08-27). 새 설계에서는 이 경우가 **더 자주** 난다:
            //   흐름이 `stale` 로 닫았거나(다른 차가 왔다), 폰이 끊겨 닫혔거나.
            //   ⚠ 옛 코드는 여기서 그냥 `return` 했다 — **계수도 로그도 없었다.**
            //     그래서 *"번호가 사라진다"* 를 다음 사람이 **찾을 방법이 없었다**(원장 §9.66).
            //   🔑 로그는 아낀다(재시도가 돌면 잦아질 수 있다). **계수는 매번 올린다.**
            shot_late_++;
            if (shot_late_ <= 3 || shot_late_ % 20 == 0)
                logf("!", "이미 닫힌 촬영에 답이 왔다 — 요청 " + std::to_string(id)
                          + " (그 요청은 " + (sh->state == CAM_FAILED ? sh->reason : std::string("종료"))
                          + " 로 닫혔다) · 누적 " + std::to_string(shot_late_)
                          + "회. **쓰지 않는다**");
            return;                                    // 첫 응답이 이긴다
        }
        sh->ans_ms = epoch_ms();
        if (!err.empty() || plate.empty()) {
            sh->state = CAM_FAILED; shot_failed_++;
            // 🔑 낱말을 **해석 없이 그대로** 담는다(어휘 정본은 앱이다 · SPEC §4.1)
            sh->reason = err.empty() ? std::string("empty_plate") : err;
            logf("←폰", "촬영 " + std::to_string(id) + " 실패 — "
                        + (err.empty() ? std::string("빈 번호판") : err));
            broadcast_shot_result(id, false, std::string(), sh->reason);
        } else if (plate == "-1") {
            sh->reason = "sentinel_plate";
            // 🔴 `"-1"` 은 **표지지 값이 아니다.** 저장하면 진짜 번호판과 구별이 사라진다.
            sh->state = CAM_FAILED; shot_failed_++; shot_sentinel_++;
            broadcast_shot_result(id, false, std::string(), sh->reason);
            logf("!", "촬영 " + std::to_string(id) + " — 번호판이 문자 그대로 \"-1\" 이다. "
                      "표지값과 겹치므로 거절하고 실패로 기록한다");
        } else {
            sh->state = CAM_READY; sh->plate = plate; shot_answered_++;
            broadcast_shot_result(id, true, plate, std::string());
            logf("←폰", "촬영 " + std::to_string(id) + " → " + plate);
        }
        shots_save();
    }

    // 🔴 대장 = **유일한 출처**. 여기서 오늘 최대 seq 를 되찾는다(별도 커서 파일 없음).
    void shots_load() {
        shots_file_ = shots_path();
        if (no_disk) { logf("=", "시험 인스턴스 — 카메라 대장을 파일로 안 쓴다"); return; }
        if (shots_file_.empty()) {
            logf("!", "🔴 HOME 을 못 읽었다 — 카메라 대장 영속 **꺼짐**. "
                      "재기동하면 요청번호가 되풀이될 수 있다");
            return;
        }
        shots_persist_on_ = true;
        std::ifstream f(shots_file_.c_str(), std::ios::binary);
        if (!f.good()) { logf("=", "카메라 대장 없음 — 새로 시작한다 (" + shots_file_ + ")"); return; }
        std::string ln;
        while (std::getline(f, ln)) {
            if (ln.empty()) continue;
            // `<id> <state> <req_ms> <ans_ms> <plate…>` — plate 가 마지막이라 공백을 품어도 된다
            std::istringstream is(ln);
            Shot sh; std::string rest;
            std::string rs;
            if (!(is >> sh.id >> sh.state >> sh.req_ms >> sh.ans_ms >> rs)) continue;
            if (rs != "-") sh.reason = rs;
            std::getline(is, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            sh.plate = rest;
            if (shots_.size() >= SHOT_RING_CAP) shots_.pop_front();
            shots_.push_back(sh);
        }
        const long long today = shot_today_yyyymmdd();
        shot_day_ = today; shot_seq_ = 0;
        for (size_t i = 0; i < shots_.size(); i++)
            if (shots_[i].id / 10000 == today) {
                const int sq = (int)(shots_[i].id % 10000);
                if (sq > shot_seq_) shot_seq_ = sq;
            }
        char b[192];
        snprintf(b, sizeof(b), "카메라 대장 %d건 이어받음 — 오늘(%lld) 다음 seq %d",
                 (int)shots_.size(), today, shot_seq_ + 1);
        logf("=", b);
    }

    void shots_save() const {
        if (!shots_persist_on_ || no_disk || shots_file_.empty()) return;
        ensure_parent_dir(shots_file_);
        const std::string tmp = shots_file_ + ".tmp";
        {
            std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
            if (!f.good()) return;
            for (size_t i = 0; i < shots_.size(); i++) {
                const Shot& sh = shots_[i];
                // 🔴 사유가 **plate 앞**이다 — 사유는 `[a-z_]+` 라 공백이 없고
                //   plate 는 공백을 품을 수 있어 **마지막이어야** 한다. 순서를 바꾸면 파싱이 깨진다.
                //   🔑 빈 사유는 `-` 로 적는다. 빈 칸으로 두면 필드가 하나 사라진다
                f << sh.id << " " << sh.state << " " << sh.req_ms << " " << sh.ans_ms
                  << " " << (sh.reason.empty() ? std::string("-") : sh.reason)
                  << " " << sh.plate << "\n";
            }
        }
        // 🔑 원자적 교체 — 쓰다 죽어도 반쯤 쓰인 대장이 남지 않는다
        // 🔴🔴 **윈도우는 대상이 있으면 `rename` 이 실패한다**(§9.3 · cpp-engineer).
        //   첫 저장은 성공하고 **둘째부터 항상 실패**한다 → 대장이 얼어붙고 `.tmp` 가 매번 남는다.
        // 🔴 **그리고 반환값을 안 보고 있었다** — 그래서 실패해도 **로그도 계수기도 없다.**
        //   ⚠ 바로 윗줄이 *"원자적 교체"* 라고 말하는데 윈도우에서는 **교체가 아예 안 됐다** —
        //     §"적혀 있다를 있다로 읽는다" 의 실물이다.
        //   🔑 **반환값을 보는 것은 플랫폼과 무관한 개선이다** — 가드보다 그게 먼저다.
#ifdef _WIN32
        if (!MoveFileExA(tmp.c_str(), shots_file_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            char b[128];
            snprintf(b, sizeof(b), "카메라 대장 교체 실패 GetLastError=%lu",
                     (unsigned long)GetLastError());
            logf("!", b);
        }
#else
        if (rename(tmp.c_str(), shots_file_.c_str()) != 0) {
            char b[128];
            snprintf(b, sizeof(b), "카메라 대장 교체 실패 errno=%d (%s)", errno, strerror(errno));
            logf("!", b);
        }
#endif
    }

    void rid_cursor_load() {
        // 🔑 **경로를 만드는 것은 서버의 일이다**(오프셋에 따라 갈린다). 풀은 경로를 받기만 한다.
        ridpool_.loadCursor(rid_cursor_path(), epoch_ms(), no_disk);
    }


