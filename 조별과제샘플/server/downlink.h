// downlink.h — 아두이노 하행(서버 → 장치) 발행·재전송·회수. `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **이 파일은 단독으로 컴파일되지 않는다.**
    //   `struct Server` 의 **몸통 조각**이고, `server.cpp` 안에서 그 자리에 include 된다.
    //   include 문을 클래스 밖으로 옮기면 컴파일이 깨진다 — 위치가 곧 문법이다.
    //
    // 🔑 **왜 이 형태인가**: 코드를 *옮긴 것*이지 *고친 것*이 아님을 증명하려고.
    //   전처리 결과가 원본과 같으므로 **`.o` 가 바이트 동일**해야 한다.
    //   그 대조가 0 이 아니면 이동이 아니라 재배치다 — 그때는 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 아두이노로 요청 내리기
    void dispatch(char kind, sock_t ws_fd, const std::string& brid,
                  const std::string& slot, const std::string& uid) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) {
            logf("!", "rid 공간 고갈 — 하행 발행 포기 (pend=" + std::to_string(pend.size()) + ")");
            return;
        }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot;
        p.plate = uid;                       // 서버가 기억할 원본(번호판일 수 있다)
        p.user_id = wire_userid(uid);        // 전선에 나갈 값 — ASCII 0*8 아니면 빈 값
        p.kind = kind;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;

        char buf[64];
        // **p.user_id 를 쓴다. 인자 uid 를 쓰면 안 된다** — uid 는 번호판일 수 있고
        // 그러면 UTF-8 이 그대로 전선에 나가 §2.1(ASCII 전용)과 §2.3 을 위반한다.
        // 실제로 그 버그를 냈다: `R,1,B2,980가4568,F7` 이 나가 아두이노가 죽었다.
        if (kind == 'R') snprintf(buf, sizeof(buf), "R,%u,%s,%s,", rid, slot.c_str(), p.user_id.c_str());
        else             snprintf(buf, sizeof(buf), "C,%u,%s,", rid, slot.c_str());
        // 🔑 **전송하지 않는다. 큐에 담는다.** 나가는 것은 다음 창(= 다음 `S` 도착)이다.
        if (!enqueue_down(pend[rid], build_line(buf), true, false)) { pend.erase(rid); rid_release(rid); }
    }

    // §2.4 `T` — 테스트 모드 제어. R/C 와 같은 pend 표·재전송·타임아웃을 그대로 탄다.
    // Pending.user_id 를 tval 보관에 재사용하고, slot 에 "??" 가 들어갈 수 있다.
    void dispatch_test(sock_t ws_fd, const std::string& brid,
                       char op, const std::string& slot, const std::string& tval) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — T 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot; p.user_id = tval; p.kind = 'T';
        p.top = op;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        // ⚠ **`T` 도 같은 큐를 탄다** — 하행이면 규율을 지킨다(설계 §7 은 "안 정했다"였고
        // 여기서 정한다). 대가가 있다: `T` 는 §8.7·§8.17 에서 **착지 타이밍을 재는 도구**였는데
        // 큐에 태우면 안전한 창으로 강제되어 **"착지 시점을 의도적으로 맞춘 시험"의 수단이 사라진다.**
        // → 그 시험이 필요하면 `--down-immediate` 로 옛 거동을 쓴다. **도구가 바뀐 사실은 원장 §8.23.**
        if (!enqueue_down(pend[rid], build_line(test_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
    }
    // §12B — 시뮬레이터 한 걸음. **무장 여부로 막지 않는다**(테스트 모드와 별개).
    void dispatch_sim(sock_t ws_fd, const std::string& brid) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — M 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.kind = 'M'; p.top = 0;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        if (!enqueue_down(pend[rid], build_line(sim_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
    }
    // ── 🔴 자리 조작 `G` — **전선 형식은 arduino 의 파서에서 읽어 맞췄다**(REQ-0228 · 말로 받지 않았다)
    //
    //   `G,<rid>,<idx>,<op>,<ck>`
    //     idx : **모듈 인덱스**(등록 `D` 순서). 장치는 `idx >= SLOT_N && idx < moduleCount()` 만 받는다
    //     op  : **0 이 아니면 열기, 0 이면 닫기**
    //   ACK  : `A,<rid>,G<d>,<result>,<ck>`  (d = idx % 10) · result 0=수행 · **3=수행 불가**
    //
    // ⚠ **ACK 의 둘째 칸이 자리 이름이 아니다**(`G0` 같은 값). 서버의 ACK 경로가 이미
    //   `slot_index(slot) < 0` 이면 `p.slot` 으로 되돌리므로 그대로 동작한다 — **상관 키는 `rid`** 다.
    // 🔴 **완료는 이 ACK 이 아니라 다음 `S` 의 비트 `idx` 로 판정한다.**
    //   ACK 은 *"명령이 도착해 적용됐다"* 까지이고, **실제로 열렸는지는 장치가 매 슬롯 에코한다.**
    static std::string gate_prefix(const Pending& p) {
        char buf[64];
        snprintf(buf, sizeof(buf), "G,%u,%d,%d,", p.wire_rid, p.mod_idx, (p.top == '1') ? 1 : 0);
        return std::string(buf);
    }
    // 자리에 붙은 **명령 가능한 모듈**의 인덱스. 없으면 -1.
    // 🔑 **자리 → 모듈 라우팅은 여기 한 곳에만 있다**(설계 §6.8) — 화면은 자리만 지목한다.
    // ── 🔴 ②-d — **그 모듈을 등록한 노드를 찾는다** (REQ-0262 · 2026-08-19)
    //   전에는 `state_json()` 이 **주 노드만** 보고 나머지는 `known:false` 로 냈다.
    //   그건 **버그가 아니라 옳은 답**이었다 — 값 경로가 없었으니까(설계 §8.9).
    //   ②-c 로 비트열이 노드별이 됐고 ②-b 로 보조 노드 등록이 들어오므로 **이제 채울 수 있다.**
    const Node* node_by_devid(const std::string& dev) const {
        // ⚠ **빈 `devid` 를 걸러내지 않는다.** 처음에 `if (dev.empty()) return 0;` 을 넣었다가
        //   **자가검증 셋이 깨졌다**(E1·X1 값, 세션 종료 뒤 known). 이유:
        //   승격 전 주차 노드는 `devid` 가 비어 있고 그 상태로 등록하면 지형에 `("", "E1")` 이 들어간다.
        //   옛 코드는 `z.modules[m].first == park.devid` 로 **빈 값끼리 맞춰** 동작했다.
        //   🔑 방어를 넣으면서 **거동을 같이 바꿨다** — 시험이 아니었으면 화면에서 값이 조용히 사라졌다.
        //   보조 노드의 빈 `devid` 는 `adopt_as_aux()` 에서 이미 고쳤으므로 여기서 또 막을 필요가 없다.
        if (dev == park.devid) return &park;
        std::map<std::string, AuxNode>::const_iterator it = aux.find(dev);
        return (it != aux.end()) ? &it->second : 0;
    }

    // ⚠ **이 함수는 주차 노드만 본다. 그대로 둔다** — 보조 노드는 **상행 전용**이라
    //   하행 경로가 없다. 없는 노드에 조작 색인을 만들어 주면 **누를 수 있는 버튼**이 생기고
    //   눌러도 아무 일이 안 난다. 🔑 §"조용히 성공으로 답하지 않는 것이 지금의 정답" 그대로다.
    int gate_index_of(const Zone& z) const {
        for (size_t m = 0; m < z.modules.size(); m++) {
            if (z.modules[m].first != park.devid) continue;      // 지금은 주차 노드만 명령을 받는다
            const std::string& nm = z.modules[m].second;
            for (size_t i = 0; i < park.mods.size(); i++)
                if (park.mods[i].first == nm && kind_commandable(park.mods[i].second))
                    return (int)i;
        }
        return -1;
    }
    void dispatch_gate(sock_t ws_fd, const std::string& brid,
                       const std::string& slot, int idx, bool open) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — G 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot; p.kind = 'G'; p.mod_idx = idx;
        p.top = open ? '1' : '0';
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        gate_want[idx] = open ? 1 : 0;      // 🔑 **대조할 값을 여기서 남긴다**(ACK 이 지우기 전에)
        // 🔑 **큐에 들어간 것만 센다.** 거절되면 전선에 안 나갔으므로 장치거절의 분모가 아니다 —
        //   분모에 넣으면 "장치가 멀쩡한데 거절률이 낮아 보이는" 착시가 생긴다.
        if (!enqueue_down(pend[rid], build_line(gate_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
        else gate_q++;
    }
    static std::string sim_prefix(const Pending& p) {
        char buf[32];
        snprintf(buf, sizeof(buf), "M,%u,", p.wire_rid);
        return std::string(buf);
    }
    static std::string test_prefix(const Pending& p) {
        char buf[64];
        snprintf(buf, sizeof(buf), "T,%u,%c,%s,%s,",
                 p.wire_rid, p.top, p.slot.c_str(), p.user_id.c_str());
        return std::string(buf);
    }

    // ---------- 타이머 (§7.3 재전송 / §3.4 offline)
    void tick() {
        long long t = now_ms();
        // ── 창을 포기하고 쏘는 경로 (설계 §3)
        // `S` 가 안 오면 창이 영영 안 열려 **하행이 조용히 사라진다.** 안 쏘는 것보다
        // 나가서 실패하는 것이 낫다 — 조용한 소실이 더 나쁘다.
        // ⚠ `DOWN_DMAX_MS`(2400) < `OFFLINE_MS`(3500) 이므로, 서버가 "장치 없음"으로
        // 판단하기 **전에** 한 번은 시도한다.
        // ⚠ `ard_seen` 이 false 면 `ard_last_ms` 는 의미 없는 0 이라 이 조건이 즉시 참이다 —
        // **그게 맞다.** 승격 직후 재하달은 창 개념이 없고, 보통은 같은 틱의 첫 `S` 처리가
        // 먼저 내보내므로 여기까지 오지 않는다.
        // ⚠ **슬롯당 1회로 묶는다**(위 `last_dmax_ms` 주석). 안 묶으면 200ms 버스트가 된다.
        // ── §5 등록 질의 `Q` ────────────────────────────────────────────────────
        // 🔴 **상한은 서버에 둔다 — 비용이 여기 있다.** `Q` 한 번이 **창당 1거래를 독점**하고
        //    그동안 그 노드의 하행이 못 나간다. 장치가 답하는 비용은 배치 하나이고 내 창을 안 먹는다.
        //    **비용 없는 쪽에 상한을 두면 복구 경로만 지운다**(설계 §5 · arduino 합의).
        // ⚠ **`Q` 를 보내는 것 자체가 이 소켓이 승격됐다는 뜻이다** — 승격 전이면 보낼 곳을 모른다.
        if (ard != BAD_SOCK && ard_seen && !park.reg_done && !park.reg_giveup
            && park.reg_first_ms && (t - park.reg_first_ms) > REG_TIMEOUT_MS
            && (t - park.last_q_ms) >= DOWN_SLOT_MS) {
            if (park.q_sent >= REG_Q_MAX) {
                park.reg_giveup = true;
                reg_giveups++;
                logf("!", "등록 포기 — `Q` " + std::to_string(REG_Q_MAX)
                          + "회에도 `D` 가 안 왔다. node_unregistered 로 굳힌다 (device="
                          + park.devid + ")");
                // 🔑 **끝없이 묻는 것보다 "모른다"를 확정하는 것이 낫다.** 안 그러면 이 노드가
                //    매 창을 질의로 먹는다. 재무장은 **새 세션**이다(설계 §5 의 열린 자리).
            } else {
                park.q_sent++; park.last_q_ms = t; reg_qsent++;
                // `Q,<ck>` — **인자 없음.** 주소는 소켓이 갖고 있고, 이 시점의 devid 는
                // 서버가 확신할 수 없는 값이다(미등록이라 묻는 것이다 · 설계 §5).
                std::string q = build_line("Q,");
                if (!send_raw(ard, q.data(), q.size(), "아두이노")) ard_send_failed();
                else logf("→ARD", "Q (등록 질의 " + std::to_string(park.q_sent) + "/"
                                  + std::to_string(REG_Q_MAX) + ")");
            }
        }

        // 🔴 `dmax_armed` 가 주 조건이다. 슬롯 문턱은 **그물로 남긴다** —
        //    재무장 논리에 결함이 생겨도(예: 줄이 폭주) 버스트로 돌아가지 않게.
        if (!downq.empty() && ard != BAD_SOCK && (t - ard_last_ms) > DOWN_DMAX_MS
            && dmax_armed && (t - last_dmax_ms) >= DOWN_SLOT_MS) {
            last_dmax_ms = t;
            dmax_armed = false;        // 장치가 한 줄이라도 말할 때까지 다시 안 쏜다
            dmax_flushes++;            // 그래서 이 수가 "몇 번 포기했나"로 읽힌다
            // ⚠ **탐침 크기로 쏜다**(4건 아님) — 증거가 없는 자리다.
            flush_downq("S 가 안 온다 — 창 포기(탐침)", true, DOWN_PROBE_N);
        }
        std::vector<uint16_t> dead;
        for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it) {
            Pending& p = it->second;
            // 🔴 **큐에서 기다리는 동안은 ACK 시계가 안 돈다.** 안 그러면 전선에 나가기도 전에
            // 재전송이 걸려 같은 rid 가 큐에 두 번 들어간다 — 큐가 없애려던 증폭이 큐 안에서 생긴다.
            if (p.queued) continue;
            if (t - p.sent_ms < ACK_TIMEOUT_MS) continue;
            if (p.tries >= ACK_MAX_TRIES) {
                ack_fail_count++;                         // 하행 건강 지표(소크 요약)
                logf("!", "ACK 타임아웃 최종 실패 wire_rid=" + std::to_string(p.wire_rid));
                send_err(p.ws_fd, p.browser_rid, "ack_timeout", "센서가 응답하지 않습니다");
                dead.push_back(it->first);
                continue;
            }
            p.tries++;
            retx_count++;                                 // 하행 건강 지표(소크 요약)
            p.sent_ms = t;
            char buf[64];
            std::string line;
            if (p.kind == 'G') line = gate_prefix(p);      // 🔑 같은 rid → 장치가 멱등 캐시로 같은 답을 준다
            else if (p.kind == 'M') line = sim_prefix(p);  // 재전송이 두 걸음이 되면 안 된다(§12B.4)
            else if (p.kind == 'T') line = test_prefix(p); // 테스트도 같은 wire_rid 로 재전송
            else if (p.kind == 'R') {
                snprintf(buf, sizeof(buf), "R,%u,%s,%s,", p.wire_rid, p.slot.c_str(), p.user_id.c_str());
                line = buf;
            } else {
                snprintf(buf, sizeof(buf), "C,%u,%s,", p.wire_rid, p.slot.c_str());
                line = buf;
            }
            logf("↻", "재전송 " + std::to_string(p.tries) + "/" + std::to_string(ACK_MAX_TRIES)
                      + " (같은 wire_rid=" + std::to_string(p.wire_rid) + ") — 큐에 다시 넣는다");
            // 🔴 **재전송도 하행이다 — 같은 큐를 탄다.** 창 밖에서 쏘면 규율이 무의미해진다.
            //   announce=false : `queued` 를 다시 보내면 같은 rid 의 `expires_ms` 가 **늘어나** 단조성이 깨진다
            //   force=true     : 이미 약속한 건이므로 깊이 상한으로 거절하지 않는다
            // ⚠ 대가: 재시도 간격이 창 주기에 종속된다. `ACK_TIMEOUT_MS`(1500) + 창 대기(≤1200)
            //   → 3회 소진이 최악 ≈6.9초(전에는 4.5초). web 타이머는 `expires_ms(≤4800) + 6000`
            //   = 10.8초라 **아직 안 겹친다.** `ACK_TIMEOUT_MS` 는 **일부러 안 건드렸다** —
            //   한 교체에 변경 둘을 넣으면 깨질 때 어느 것 때문인지 못 가른다(원장 §6).
            // 🔴 **반환값을 봐야 한다.** 링크가 끊긴 뒤에는 `end_ard_session` 이 큐만 비우고
            // **전선에 나가 있던 pend 는 남는다.** 그 건이 여기로 와서 거절되면(`ard==BAD_SOCK`)
            // 이미 `device_offline` 로 답이 갔는데, 반환값을 무시하면 `pend` 에 남아
            // 몇 틱 뒤 **`ack_timeout` 으로 한 번 더** 답한다.
            // ⚠ 그 이름은 **"전선에 나갔다. 3회 재전송까지 했다"** 를 뜻한다(§8.16) —
            // 서버가 스스로 거절한 건에 붙이면 **로그에 거짓 문장이 남고 `ack_fail_count` 도 오염된다.**
            // 08-17 07:54:40 줄이 정확히 그 형태였다. 같은 것을 새 코드에 만들지 않는다.
            if (!enqueue_down(p, build_line(line), false, true)) {
                dead.push_back(it->first);   // ⚠ 루프 안에서 erase 하면 반복자가 무효화된다
                continue;
            }
        }
        for (size_t i = 0; i < dead.size(); i++) { pend.erase(dead[i]); rid_release(dead[i]); }
    }

    // 그 자리에 아직 ACK 를 못 받은 요청이 있는가.
    // 있으면 세계관이 갈라진 게 아니라 "진행 중"일 뿐이므로 손대면 안 된다(§7.6).
    bool has_pending_for(const std::string& slot) const {
        for (std::map<uint16_t, Pending>::const_iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.slot == slot) return true;
        return false;
    }

    // ---------- §7.5 예약 은퇴
    // 예약된 자리에서 차가 빠지면(occupied 1→0) 그 예약은 소진된 것이다.
    // **ACK 를 기다리지 않고 서버 쪽에서 즉시 확정한다** — ACK 실패 시 살려 두면
    // 애초에 이걸 만들게 한 "죽은 자리" 버그가 은퇴 경로로 되돌아온다.
    void retire(int i) {
        logf("⏏", std::string("예약 소진(occupied 1→0) — ") + SLOT_ID[i]
                  + " 은퇴시킨다 (user=" + (slots[i].user_id.empty() ? "-" : slots[i].user_id) + ")");
        slots[i].reserved = 0;
        slots[i].user_id.clear();
        slots[i].reserved_at = 0;
        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");   // 아두이노에도 반영시킨다
    }

    // ---------- 아두이노 재부팅 → 재동기화 (§7.4)
    void resync_reservations(const char* why) {
        resync_count++;                      // --selftest 계측용 (§7.4 오탐 증명)
        std::vector<int> live;
        for (int i = 0; i < 10; i++) if (slots[i].reserved) live.push_back(i);
        logf("⟳", std::string("재부팅 감지(") + why + ") — 살아 있는 예약 "
                  + std::to_string(live.size()) + "건 재하달");
        for (size_t k = 0; k < live.size(); k++)
            dispatch('R', BAD_SOCK, "", SLOT_ID[live[k]], slots[live[k]].user_id);
    }
