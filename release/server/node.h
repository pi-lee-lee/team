// node.h — 통신 단위 — ESP 하나 = 소켓 하나. **주차 자리가 아니다**
//
// 🔴 **`server.cpp` 가 원래 자리에서 `#include` 한다.** 전처리 결과가 같아야
//   산출물(`.o`)이 **0 차이**이고, 그래야 "떼어 냈지만 아무것도 안 바꿨다"가 값으로 증명된다.
//   ⚠ **`#include` 위치를 옮기는 순간 그것은 분리가 아니라 변경이다.**
//
// ⚠ 헤더 가드를 안 넣었다 — **한 번만 include 되는 조각**이고, 가드를 넣으면
//   전처리 결과가 달라져 이 단계의 증명이 깨진다. 다음 단계(진짜 헤더화)에서 넣는다.

// ⚠ **낱말**: 여기의 `Node` 는 통신 단위(ESP 하나 = 소켓 하나 = 자기 1.2초 주기)다.
//   **주차 자리가 아니다.** 자리는 `Zone`(§0 낱말표).
struct Node {
    // ⚠ **필드 이름을 `AuxNode` 쪽에 맞췄다**. 반대로 하면 보조 노드
    //   호출부 20여 곳을 고쳐야 하는데, 이쪽은 **별칭 7줄만** 바꾸면 된다.
    //   🔑 **적게 고치는 쪽을 고른 것이 아니라, 고치는 자리가 내가 방금 만든 자리인 쪽**을 골랐다.
    std::string devid;          // 전선의 장치 id. "" = 아직 미정(first-S-wins)
    sock_t      fd;             // 이 노드의 연결
    std::string peer;           // "IP:포트" — `devid` 가 고유하지 않을 때 유일한 구분자(REQ-0215)
    std::string buf;            // 수신 조립 버퍼
    bool        seen;           // 유효 프레임을 한 번이라도 받았나
    long long   last_ms;        // 마지막 **유효** 프레임 수신 시각(단조 시계) — 유휴 마감의 기준
    long long   last_epoch_ms;  // 같은 사건의 벽시계 — **로그 대조용이지 계산용이 아니다**

    // ── `AuxNode` 에서 흡수한 것들 ─────────────────────────────
    // 🔑 **보조 노드는 처음부터 `Node` 의 부분집합이었다.** 두 구조체를 유지하면
    //    "노드마다 있는 상태"가 두 곳에 나뉘어 **다음 단계(`map<devid,Node>`)에서 합칠 수 없다.**
    long long   connected_ms;     // 접속 시각
    long long   frames;           // 누적 유효 프레임
    long long   drops;            // 버린 줄
    bool        online;           // §3.4 엣지 판정용 직전 상태
    int         offline_episodes;

    // ── 등록(§5 `D`/`Q`) ────────────────────────────────────────────────────────
    // 🔴 **이 단계에서 등록은 *관측*이지 제어가 아니다.** 하행 경로를 한 줄도 안 바꾼다 —
    //    지금 하행(`R`·`C`·`T`·`M`)은 **자리(슬롯 번호)** 로 주소를 정하므로 모듈 구성과 무관하다.
    //    **모듈 신원 기반 명령이 생길 때** 비로소 이 상태가 제어에 쓰인다(설계 §5).
    // ⚠ 그래서 옛 펌웨어(등록을 모르는 노드)도 **종전 그대로 동작한다.** 분기를 안 만든다.
    int         reg_n;          // 선언된 모듈 수. -1 = `D,*` 를 아직 못 받았다
    int         reg_drain;      // 선언된 슬롯당 ACK 배출 하한. -1 = 미선언
    bool        reg_done;       // `reg_n` 개를 다 받았다
    bool        reg_giveup;     // `Q` 3회에도 안 와서 굳혔다(node_unregistered)
    long long   reg_first_ms;   // 승격(첫 `S`) 시각 — `REG_TIMEOUT` 의 기준
    int         q_sent;         // 보낸 `Q` 수
    long long   last_q_ms;      // 마지막 `Q` — 슬롯당 1회로 묶는다
    std::vector<std::pair<std::string, std::string> > mods;   // (name, kind) · **순서가 곧 idx**
    // 재등록 전의 모듈 목록. 구성이 바뀌었는지 대는 데만 쓴다.
    std::vector<std::pair<std::string, std::string> > prev_mods_snapshot;

    // ── 🔴 ②-c — **자리 비트열은 노드의 것이다**
    //   ⚠ `Server` 에 한 벌만 두면 보조 노드의 모듈은 값 경로가 없어지고
    //   `state_json()` 이 그것을 정직하게 `known:false` 로 냈다(설계 §8.9).
    //   🔴 **이 필드가 `Node` 로 오기 전에 보조 노드의 줄을 파서에 넣으면**
    //     그 `S` 가 **주 노드의 비트열을 덮는다.** 그래서 ②-c 가 ②-b 보다 먼저다.
    int         mod_bits[REG_MODS_MAX];   // 자리 비트열(자리 10칸을 넘는 비트 포함)
    int         mod_bits_n;               // 해독한 비트 수. **0 = 안 읽었다**(모른다이지 0 이 아니다)
    // ── 🔴 `V` 프레임이 싣는 **센서가 잰 값**. 비트와 **다른 축**이다
    //   `mod_val_has` 가 false 면 *"못 쟀다"* 다 — 🔴 **`0` 으로 접지 마라.**
    //   ⚠ 그리고 `mod_bits` 의 `known`(= 폭이 닿았나)과도 **다른 것**이다.
    //     폭은 전송 계층이고 이것은 측정 계층이다. 🔴 **둘을 겸한 이름은 결함을 숨긴다.**
    long        mod_val[REG_MODS_MAX];
    bool        mod_val_has[REG_MODS_MAX];
    long long   mod_val_ms[REG_MODS_MAX];  // 받은 시각. 화면에 `age_ms` 로 나간다

    // ── 🔴 `E` 프레임(전이) — SPEC-sensor-value.md §15.3
    //   🔑 **`mod_val` 과 다른 축이다.** `mod_val` 은 *"마지막으로 받은 값"* 이고
    //     이것은 *"마지막 **전이** 의 결과와 그 시각"* 이다.
    //   ⚠ `E` 는 **사건**이라 잃어도 된다(§15.2). 그래서 **판정에 쓰지 않는다** —
    //     점유의 정본은 `mod_bits` 하나다. 갈리면 `mod_bits` 가 이긴다.
    int         trans_r[REG_MODS_MAX];      // 마지막 전이의 결과. **-1 = 전이를 아직 못 받았다**
    long long   trans_ms[REG_MODS_MAX];     // 그 전이를 받은 시각. 화면 `age_ms` 의 새 뜻
    // 🔴 `occ` 대조는 **`S` 를 적용한 뒤에** 해야 한다 — `E` 가 `S` 보다 먼저 오므로
    //   `E` 자리에서 비교하면 **한 슬롯 낡은 비트**와 대는 것이 되어 거짓 불일치가 난다.
    //   그래서 표시만 세워 두고 `S` 처리 끝에서 비교한다.
    bool        trans_cmp_pending[REG_MODS_MAX];

    // ── 🔴 **`S` 프레임과 송신 창은 노드마다 따로다** — 보드가 여럿이면 전역일 수 없다
    //   🔑 이 값들이 `Server` 에 한 벌만 있으면 **나중에 붙은 보드가 앞엣것을 덮는다.**
    //     덮인 쪽은 오류를 안 내고 *"방금 값을 받았다"* 로 보이므로 **원인이 안 보인다.**
    //   ⚠ `seq`/`uptime` 의 `-1` 은 **"아직 못 받았다"** 다. `0` 으로 접지 마라 —
    //     `0` 은 장치가 방금 재부팅했다는 **뜻이 있는 값**이라 둘을 겸하면 재부팅 판정이 깨진다.
    long        seq;                      // 마지막 `S` 의 시퀀스. -1 = 미수신
    long long   uptime;                   // 장치가 말한 가동시간. -1 = 미수신
    int         test_bits[REG_MODS_MAX];  // 시험 강제 비트열
    int         test_bits_n;
    bool        test_armed;               // 이 노드에 시험 강제가 걸려 있나
    long long   last_dmax_ms;             // 이 노드의 마지막 `dmax` 창 시각
    bool        dmax_armed;

    Node() : fd(BAD_SOCK), seen(false), last_ms(0), last_epoch_ms(0),
             connected_ms(0), frames(0), drops(0), online(false), offline_episodes(0),
             reg_n(-1), reg_drain(-1), reg_done(false), reg_giveup(false),
             reg_first_ms(0), q_sent(0), last_q_ms(0), mod_bits_n(0),
             seq(-1), uptime(-1), test_bits_n(0),
             test_armed(false), last_dmax_ms(0), dmax_armed(true) {
        // 🔑 값은 **없음**이 기본이다. `0` 이 아니라 "아직 못 받았다" 로 시작한다
        for (int i = 0; i < REG_MODS_MAX; i++) {
            mod_bits[i] = 0; test_bits[i] = 0;
            mod_val[i] = 0; mod_val_has[i] = false; mod_val_ms[i] = 0;
            trans_r[i] = -1; trans_ms[i] = 0; trans_cmp_pending[i] = false;
        }
    }

    void reg_reset() {          // 세션이 새로 서면 등록도 처음부터다
        reg_n = -1; reg_drain = -1; reg_done = false; reg_giveup = false;
        reg_first_ms = 0; q_sent = 0; last_q_ms = 0; mods.clear();
        // 🔴🔴 **idx 로 들고 있는 것을 같이 비운다.** 안 비우면 조용히 틀린다:
        //   재등록에서 **모듈 순서가 바뀌면** 옛 `idx` 의 값이 **다른 모듈에 붙는다.**
        //   ⚠ 자리 *결속* 은 이름 기반이라 안전한데(`lot.bind`), **값은 idx 기반**이다 —
        //     그래서 "결속은 멀쩡한데 값만 엉뚱한" 모양이 되고 **아무 오류도 안 난다.**
        //   🔑 비우면 `has=false`(= 못 쟀다)가 되고, 그건 **정직한 상태**다.
        //     다음 `V`·`S` 가 1.2초 안에 채운다 — 잃는 것은 한 슬롯이고 얻는 것은 정확성이다.
        //   (arduino 가 *"순서가 바뀌면 결속이 유지되나"* 라고 물어서 찾았다)
        mod_bits_n = 0;             // "모른다". `0` 개 비트는 "전부 비었다" 가 아니다
        for (int i = 0; i < REG_MODS_MAX; i++) {
            mod_bits[i] = 0; mod_val[i] = 0; mod_val_has[i] = false; mod_val_ms[i] = 0;
            trans_r[i] = -1; trans_ms[i] = 0; trans_cmp_pending[i] = false;
        }
    }

    // 🔴 **소켓이 새로 서면 이 노드가 말한 것은 전부 무효다.** 등록만 비우면 모자란다 —
    //   `seq`/`uptime` 이 남으면 **옛 세션의 시퀀스와 새 세션의 것을 잇는 판정**이 되고,
    //   `test_armed` 가 남으면 **끊긴 보드의 시험 강제가 자리를 계속 덮는다.**
    //   🔑 `reg_reset()` 은 *등록* 축만 비운다. 세션 축은 여기서 비운다 — **두 축이다.**
    void session_reset() {
        seen = false; online = false; seq = -1; uptime = -1;
        mod_bits_n = test_bits_n = 0;
        test_armed = false; last_dmax_ms = 0; dmax_armed = true;
        reg_reset();
    }
};

// 🔴 **`AuxNode` 는 이제 `Node` 다**. 이름만 남긴다 —
// 호출부 20여 곳이 `AuxNode` 로 적혀 있고 **그것을 지금 바꾸면 이 단계가 커진다.**
// ⚠ **다음 단계에서 이 이름도 없앤다.** 지금 남긴 이유는 **한 단계에 한 가지만 바꾸려는 것**이다.
// 🔑 별명이라 **보조 노드도 등록 상태를 갖게 된다** — 쓰지 않을 뿐 구조가 이미 준비된다.
typedef Node AuxNode;

struct UnknownSock {
    sock_t fd;
    std::string buf;
    long long since_ms;
    std::string peer;          // "IP:포트" — 승격 시 세션으로 넘어간다
    UnknownSock() : fd(BAD_SOCK), since_ms(0) {}
};
