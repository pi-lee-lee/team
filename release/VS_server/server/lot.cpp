// lot.cpp — 🔴 **입·출차 흐름의 본체.** 지형 선언 + 13단계 상태기계.
//
// ═══════════════════════════════════════════════════════════════════════════
// 계약 정본 : docs/SPEC-flow-2026-08-26.md   ← 순서·시한·갈래의 근거는 전부 거기다
//
// 🔑 **이 파일이 흐름을 안다. 엔진은 모른다.**
//   엔진은 `entryStatus()` 로 받은 `phase` 를 **뜻도 모른 채 봉투에 싣기만** 한다.
//   그래서 흐름을 바꾸려면 여기만 고치면 되고, 두 곳이 갈릴 일이 없다.
//
// 🔴 **막는 대기가 없다.** 모든 시한은 `srv.nowMs()` 비교다 —
//   `sleep`/`delay` 는 이 시스템에서 링크를 죽인다(ESP01). 한 박자에 한 걸음만 나아간다.
// ═══════════════════════════════════════════════════════════════════════════
#include "parking.h"
// 🔴 **시한 상수를 여기 다시 쓰지 않는다.** `config.h` 한 곳이 정본이다(명세 §4[6]) —
//   복제하면 한쪽만 고쳐지고, 그때 **둘이 갈렸다는 것을 아무도 모른다.**
#include "config.h"

#include <iostream>
#include <string>

// ══════════════════════════════════════════════════════════════════════════
// 지형 — 오늘 실물로 확인된 조립표
//
// 🔴 **모듈은 (devid, 이름) 복합키로 고른다.** 이름만으로는 못 고른다 —
//   `C1` 이 다섯 보드에 **전부 같은 이름**이라 이름 하나로는 답이 안 나온다.
//   ★ 사용자가 처음 요구한 것이 정확히 이것이다:
//     *"아두이노ID와 모듈ID가 결합하여 유니크 값으로 동작해야"*
//
// 🔴 **자리는 보드가 아니라 모듈을 고른다**(사용자 확정) —
//   자리 A3 는 센서를 P1, 안내등을 P5, LCD 를 P3 에서 가져온다. 보드로 묶인 자리는 없다.
// ══════════════════════════════════════════════════════════════════════════

// 자리 하나가 쓰는 모듈 셋. 🔑 **흐름이 참조하는 유일한 표**다 — 여기가 정본이다.
struct SlotWiring {
    const char* slot;                    // 자리 id
    const char* irDev;  const char* ir;  // 점유 센서 (적외선)
    const char* ledDev; const char* led; // 안내등 릴레이
    const char* lcdDev; const char* lcd; // 표시기
};
static const SlotWiring SLOTS[] = {
    { "A1", "P1", "A1", "P5", "R1", "P1", "C1" },
    { "A2", "P1", "A2", "P5", "R2", "P2", "C1" },
    { "A3", "P1", "A3", "P5", "R3", "P3", "C1" },
    { "A4", "P2", "A4", "P5", "R4", "P4", "C1" },
    { "A5", "P2", "A5", "P5", "R5", "P5", "C1" },
};
static const int SLOT_N = (int)(sizeof(SLOTS) / sizeof(SLOTS[0]));

// 입·출구 — ✅ **EF·ER·XF·XR 은 실물이다**(설치 확인 · 사용자 확정).
//   🔴 **주입 입구를 다시 만들지 마라.** 주입이 하나라도 살아 있으면 그 모듈은
//     **실물 값을 영영 안 본다** — 판정이 조용히 가짜 위에 선다.
//   ★ 시험이 필요하면 **센서를 손으로 가려라.** 그것이 유일한 자다.
static const char* ENT_DEV = "P1";  static const char* ENT_FRONT = "EF";
static const char* ENT_REAR = "ER";
static const char* EXT_DEV = "P2";  static const char* EXT_FRONT = "XF";
static const char* EXT_REAR = "XR";
static const char* ENT_GATE_DEV = "P3"; static const char* ENT_GATE = "ED";
static const char* EXT_GATE_DEV = "P4"; static const char* EXT_GATE = "XD";

// 🔴 **전선 값이다. 각도가 아니다**(원장 §9.27) — 장치가 `arg > 1` 을 거절한다.
//   `90` 을 보내면 차단봉이 **영영 안 열리는데 서버 로그에는 명령이 나간 것으로 남는다.**
static const long GATE_OPEN = 1, GATE_CLOSE = 0;
static const long LED_ON = 1, LED_OFF = 0;

// 🔴 **LCD 코드는 장치에 둘뿐이다**(원장 §9.25 · 소스 실측).
//   `0`=welcome · `1`=NonHuman · **그 밖은 전부 번호판으로 그려진다.**
//   ⚠ 그래서 *"촬영 중"* 을 뜻하는 `2` 를 보내면 화면에 `000 0002` 가 뜬다. 안 보낸다.
static const long LCD_WELCOME = 0, LCD_NONHUMAN = 1;

// 번호판 텍스트 → LCD 가 받는 정수. 🔴 **음수면 "표시할 수 없다"** 는 뜻이다.
//   ⚠ `0`·`1` 은 welcome·NonHuman 예약값이라 **번호판으로 쓸 수 없다** —
//     그 값이 나오면 표시를 건너뛴다. 안 그러면 빈 화면이 "번호가 떴다"로 읽힌다.
static long plateToLcd(const std::string& plate) {
    std::string d;
    for (size_t i = 0; i < plate.size(); i++)
        if (plate[i] >= '0' && plate[i] <= '9') d += plate[i];
    if (d.empty()) return -1;
    if (d.size() > 7) d = d.substr(d.size() - 7);   // 장치가 7자리를 그린다
    long n = 0;
    for (size_t i = 0; i < d.size(); i++) n = n * 10 + (d[i] - '0');
    if (n <= LCD_NONHUMAN) return -1;
    return n;
}

void buildLot(ParkingLot& lot) {
    for (int i = 0; i < SLOT_N; i++) {
        const SlotWiring& w = SLOTS[i];
        // 🔴🔴 **이 라벨을 뒤집지 마라** — `A1`→"1번" … `A5`→"5번" 이 **확정이다**(사용자 2026-08-27).
        //   사용자 원문 : *"**자리 배치는 확정되었다 수정하지마라. 웹화면에서만 5,4,3,2,1순으로 출력**되게
        //   해달라 그게 요청이다."*
        //   ★ 한 번 뒤집었다가 **되돌렸다**(12:03 배포 → 12:0x 원복). **서버는 배치를 안 바꾼다.**
        //   🔑 사용자가 말한 것은 **화면의 출력 순서**이지 **번호 매핑**이 아니었다 —
        //     *"자리 번호를 5,4,3,2,1 로"* 를 **매핑 변경**으로 넓게 읽은 것이 잘못이었다.
        //     ⚠ §"규약은 낱말이 아니라 막으려는 실패로 읽어라" 의 **반대편 실패**다:
        //       좁게 읽어 놓치는 것만 위험한 게 아니라 **넓게 읽어 범위를 늘리는 것**도 위험하다.
        //       ★ **사용자가 확정한 것을 다시 여는 것**이 그것이다.
        //   🔵 출력 순서는 **화면(web) 몫**이다 — 서버는 `A1..A5` 를 그대로 주고 화면이 정렬한다.
        lot.spot(w.slot).at(0, i).parking().label(std::string(1, char('1' + i)) + "번 자리")
            .module(w.irDev, w.ir).module(w.ledDev, w.led).module(w.lcdDev, w.lcd);
    }
    // 입·출구는 **일반영역**이다(`parking()` 을 안 붙였다) —
    // 🔑 접근 감지가 **주차면을 먹거나 배정 대상이 되면 안 된다.**
    lot.spot("E1").at(4, 0).label("입구")
        .module(ENT_DEV, ENT_FRONT).module(ENT_DEV, ENT_REAR).module(ENT_GATE_DEV, ENT_GATE);
    lot.spot("X1").at(4, 4).label("출구")
        .module(EXT_DEV, EXT_FRONT).module(EXT_DEV, EXT_REAR).module(EXT_GATE_DEV, EXT_GATE);

    // 조작 선언 — **없으면 화면에 누를 것이 안 그려진다.**
    for (int i = 0; i < SLOT_N; i++) lot.control(SLOTS[i].lcdDev, SLOTS[i].lcd).number(0, 9999999);
    for (int i = 0; i < SLOT_N; i++) lot.control(SLOTS[i].ledDev, SLOTS[i].led).toggle();
    lot.control(ENT_GATE_DEV, ENT_GATE).toggle("열기", "닫기");
    lot.control(EXT_GATE_DEV, EXT_GATE).toggle("열기", "닫기");
}

// ══════════════════════════════════════════════════════════════════════════
// 상태기계 — 명세 §1
// ══════════════════════════════════════════════════════════════════════════
namespace {

// 🔑 **닫힌 집합이다.** 화면이 이 문자열로 문구 표를 그린다(web 과 합의).
//   ⚠ 늘릴 때는 화면에 알려라. 모르는 값이 가면 화면이 조용히 빈 칸을 그린다.
const char* PH_IDLE        = "idle";
const char* PH_SHOOTING    = "shooting";
const char* PH_ASSIGNING   = "assigning";
const char* PH_ASSIGNED    = "assigned";
const char* PH_GATE_OPEN   = "gate_open";
const char* PH_PASSING     = "passing";
const char* PH_GATE_CLOSE  = "gate_close";
const char* PH_WAIT_PARK   = "wait_park";
const char* PH_REJECTED    = "rejected";
const char* PH_FULL        = "full";
const char* PH_CHOOSING    = "choosing";   // 8081 이 붙어 있을 때만 나타난다

struct Entry {
    std::string phase;
    long long   since;          // 🔑 **phase 진입 시각.** phase 가 바뀌면 다시 잡는다
    long long   shot;           // 촬영 요청번호 (0 = 없음)
    std::string plate, source, slot;
    int         attempts;
    // 🔴 **이번 판에 선택 화면을 이미 물었나.** 한 번만 묻는다 —
    //   매번 물으면 시한 폴백이 다시 선택으로 돌아가 **무한히 맴돈다.**
    //   ★ 그리고 명세가 못 박은 것이기도 하다: *"차단봉이 들리는 그 순간에 한 번"*.
    bool        chooserAsked;
    // 🔴 촬영 재시도 — **방아쇠는 실패 응답**이다(설계 ③). 타이머가 아니다.
    int         shotTries;      // 이번 차에 촬영을 몇 번 요청했나 (봉투 `shot_tries`)
    long long   shotAt;         // 지금 요청을 낸 시각 (봉투 `shot_elapsed_ms` 의 기산점)
    long long   retryAt;        // 이 시각 뒤에 재요청한다 (0 = 즉시)
    std::string lastError;      // 마지막 실패 사유 — **폰 낱말 그대로** (봉투 `shot_last_error`)
    std::string discarded;      // 🔴 **서버가 버린 사유**(`no_car`) — 폰 어휘와 **다른 축**이다
    std::string closed;         // 🔴 **서버가 기다리기를 그만둔 사유**(`cap`·`phone_gone`)
    Entry() : phase(PH_IDLE), since(0), shot(0), attempts(0), chooserAsked(false),
              shotTries(0), shotAt(0), retryAt(0) {}
};
Entry g;

// 자리마다 흐름이 기억하는 것. 🔴 **엔진의 점유와 다른 축이다** —
//   엔진은 *"IR 이 무엇을 보나"*, 이쪽은 *"우리가 누구에게 줬나"* 를 기억한다.
struct SlotState {
    // 🔴 **안내다. 잠금이 아니다**(REQ-0483 · 사용자 확정).
    //   ~~예전엔 이 하나가 "안내"와 "잠금"을 겸했다~~ — `pickSlot` 이 이걸 보고 건너뛰어서
    //   **차가 안 들어와도 자리가 `PARK_GIVEUP_MS`(3분) 동안 잠겼다.** 실측 18:35:36.
    //   ★ 사용자 원문: *"자동 배정이면 예약은 없다. 안내등만 켠다."*
    bool assigned;      // 지금 이 자리를 **안내 중**인가 (안내등·LCD 가 켜져 있다)
    // 🔴 **사람이 직접 고른 자리만 잠근다.** 그것은 사람이 한 약속이다 —
    //   자동배정은 서버의 제안일 뿐이라 다음 차에게 다시 제안해도 된다.
    bool locked;
    // 🔴 **버려진 앞차의 번호**([D] · 사용자 확정 [가]). 뒤차가 와서 이 자리 안내를 걷었을 때 담는다.
    //   🔑 이 차가 **나중에 그 자리에 주차하면 ⑧ NonHuman 이 된다** — 내가 방금 고친 결함이
    //     다른 문으로 돌아오는 자리다. **그 대가를 세려고** 남긴다. 지우는 것은 ⑦·⑧·⑪ 다.
    std::string abandoned;
    bool occupied;      // 마지막으로 관측된 점유
    bool seen;          // 점유를 한 번이라도 읽었나 (🔑 `false` 면 아래는 뜻이 없다)
    // 🔴 **안내등을 못 켰다 — 다음 박자에 다시 낸다**(사용자 신고 2026-08-27).
    //   ⚠ 전에는 `cmd()` 가 실패해도 **버렸다.** 보드가 방금 재접속해 등록(`D`)이 아직
    //     안 온 순간에 배정이 겹치면 **영영 안 켜진다** — 실측 10:43:53 이 그 순서였다.
    //   🔑 자리별로 둔다. 노드별로 두면 **어느 자리를 못 켰는지**를 잃는다.
    bool guideRetry;
    SlotState() : assigned(false), locked(false), occupied(false), seen(false),
                  guideRetry(false) {}
    // 🔑 `abandoned` 는 std::string 이라 기본이 빈 값이다 — 초기화 목록에 안 적는다
};
SlotState st[5];

// 입·출구 센서의 직전 값 — **모서리(변화)를 봐야** ①④⑫⑬ 이 판정된다.
struct Edge {
    bool have, prev;
    Edge() : have(false), prev(false) {}
    // 값이 바뀌었나. `rose`=0→1(감지 시작) · `fell`=1→0(사라짐)
    void feed(bool known, bool val, bool* rose, bool* fell) {
        *rose = *fell = false;
        if (!known) return;   // 🔑 **모름은 값이 아니다.** 모서리도 안 만들고 **기준선도 안 버린다** —
                              //   ⚠ 버리면 링크가 3.5초 조용했다 돌아왔을 때 **첫 값에서 모서리가 안 나온다.**
                              //     EF 에 **서 있는 차를 통째로 못 본다** — 조용한 고장이다.
        if (have && val != prev) { *rose = val; *fell = !val; }
        have = true; prev = val;
    }
};
Edge eEF, eER, eXF, eXR;

// 🔴🔴 **차가 아직 있나 — 4초 규칙**(설계 2026-08-27 [A] · 사용자 확정)
//   ★ **`unknown` 은 시계를 멈춘다.** 링크가 3.5초 끊기면 센서가 `known=false` 인데
//     **그것은 "차가 없다" 가 아니다.** 안 그러면 링크가 흔들릴 때마다 차가 사라진 것으로 판정한다.
//     🔑 이 구현의 **가장 위험한 자리**라 별도 구조로 뺐다 — 조건문 안에 섞으면 다음 사람이 못 본다.
struct Vacancy {
    bool      empty;        // 마지막으로 **센서가 말한** 상태(비었다)
    long long since;        // 그 상태가 시작된 시각 (0 = 안 비었다)
    Vacancy() : empty(false), since(0) {}
    // 반환 : **차가 갔다고 판정되나**. `known=false` 면 **아무것도 바꾸지 않고 false**.
    bool feed(bool known, bool blocked, long long now, long long limit) {
        if (!known) return false;          // 🔴 **모름은 값이 아니다. 시계도 안 돌고 안 지운다**
        if (blocked) { empty = false; since = 0; return false; }
        if (!empty) { empty = true; since = now; return false; }
        return (now - since) >= limit;
    }
    void reset() { empty = false; since = 0; }
    long long heldMs(long long now) const { return since ? (now - since) : 0; }
};
Vacancy vEF;
long vacateFire = 0;            // 4초 규칙이 발동한 횟수 — 값 조정의 근거다
long plateNoCar = 0;            // 번호는 왔는데 차가 없어 버린 횟수([E])
long plateNoCarUnknown = 0;     // 그중 **센서를 못 믿는 상태**였던 것 — 둘을 갈라 센다
// 🔴 **잠금을 걷어 내면서 생긴 축을 센다**(REQ-0483 §3). 로그에만 있으면 세려면 다시 읽어야 한다.
//   🔑 우리가 오늘 배운 것 — **없앤 것이 정말 문제가 안 됐는지는 세어 봐야 안다.**
long dupGuide = 0;              // 같은 자리를 두 번 안내한 횟수
// 🔴 **[가] 의 대가를 센다**(루트 지시). 버린 앞차가 **그 자리에 나중에 주차한** 횟수.
//   ★ `0` 이면 대가가 없었다는 증거이고, 크면 **사용자에게 다시 물을 값**이 된다.
//   🔑 지금은 모른다 — 그래서 센다. §"없앤 것이 정말 문제가 안 됐는지는 세어 봐야 안다"
long abandonedThenParked = 0;
bool xGateOpen = false;         // 출구 차단봉을 우리가 열어 뒀나
long long xGateSince = 0;

int slotIndex(const std::string& id) {
    for (int i = 0; i < SLOT_N; i++) if (id == SLOTS[i].slot) return i;
    return -1;
}

void setPhase(ParkingServer& srv, const char* p) {
    if (g.phase == p) return;
    g.phase = p;
    g.since = srv.nowMs();
}

// 🔴 **상태를 바꿨으면 상태를 조회한다**(명세 §4[2] · 원장 §9.42).
//   `send()` 가 `true` 를 줘도 그것은 *"명령을 받아들였다"* 지 *"물리적으로 그렇게 됐다"* 가 아니다.
//   그래서 실패를 **조용히 넘기지 않고 로그로 낸다** — 다음 박자에 되읽어 확인한다.
bool cmd(ParkingServer& srv, const char* dev, const char* mod, long v, const char* why) {
    const bool ok = srv.send(dev, mod, v);
    if (!ok) srv.log(std::string("🔴 명령을 못 냈다 ") + dev + "/" + mod + " <- " +
                     std::to_string(v) + " (" + why + ")");
    return ok;
}

// 지금 비어 있는 자리를 **A1 부터** 고른다(명세 §1). 없으면 빈 문자열.
std::string pickSlot(ParkingServer& srv) {
    for (int i = 0; i < SLOT_N; i++) {
        // 🔴 **`assigned` 로 건너뛰지 않는다**(REQ-0483). 안내는 잠금이 아니다.
        //   🔑 점유의 정본은 **자리 IR**(`parkingSpotAvailable`)이다 — 아래 줄이 그것을 본다.
        if (st[i].locked) continue;                         // 사람이 직접 고른 자리만 비켜 준다
        if (!srv.parkingSpotAvailable(SLOTS[i].slot)) continue;
        return SLOTS[i].slot;
    }
    return std::string();
}

// ⑤⑥ **배정을 실행한다** — 안내등 ON · LCD 에 번호.
//   🔴 자동배정과 사람 선택이 **이 함수 하나를 같이 쓴다.** 갈래마다 따로 쓰면
//     한쪽만 고쳐지고, 그때 *"화면으로 고르면 안내등이 안 켜진다"* 같은 것이 생긴다.
// 🔑 `byHuman` : 화면에서 **사람이 고른 것**인가. 그때만 자리를 잠근다(REQ-0483).
void applyAssign(ParkingServer& srv, const std::string& slot, bool byHuman) {
    const int i = slotIndex(slot);
    if (i < 0) return;
    // 🔴 **같은 자리를 두 번 안내했나를 센다.** 잠금을 걷어 내면서 생긴 축이라
    //   *"안내가 겹쳤다"* 가 추측이 아니라 값이어야 한다 — `0` 이면 이 걱정이 없었다는 증거다.
    //   ★ 그리고 겹쳐도 **고장이 아니다**: 점유의 정본은 자리 IR 이고, 입구는 차단봉 때문에
    //     한 대씩 들어온다. 그래도 **조용히 두지는 않는다.**
    if (st[i].assigned && g.slot != slot) {
        dupGuide++;
        srv.log("! 같은 자리를 다시 안내한다 — " + slot + " (앞차가 아직 안 댔다 · 누적 "
                + std::to_string(dupGuide) + "회). 점유 정본은 자리 IR 이다");
    }
    g.slot = slot;
    st[i].assigned = true;
    st[i].locked   = byHuman;
    // 🔴🔴 **명령이 나갔는지 보고 나서 찍는다**(사용자 신고 2026-08-27 10:43).
    //   전에는 `cmd()` 의 반환을 버리고 **무조건** *"안내등 ON"* 이라고 썼다. 그래서 로그가:
    //     `🔴 명령을 못 냈다 P5/R1 <- 1 (배정 안내등)`
    //     `▸ ⑤⑥ 자리 A1 배정 — **안내등 ON**`      ← 🔴 **바로 다음 줄에서 거짓말**
    //   ★ 사용자가 *"안내등이 안 들어온다"* 고 신고했는데 **로그는 켰다고 말하고 있었다.**
    //   🔑 §"로그가 안 한 일을 했다고 말한다" — 오늘 이 병을 다섯 번 만났고 이것이 여섯째다.
    const bool ledOk = cmd(srv, SLOTS[i].ledDev, SLOTS[i].led, LED_ON, "배정 안내등");   // ⑤
    const long lcdv = plateToLcd(g.plate);
    bool lcdOk = false;
    if (lcdv < 0) srv.log("🔴 번호에서 표시할 숫자를 못 만들었다 — LCD 는 건너뛴다: " + g.plate);
    else          lcdOk = cmd(srv, SLOTS[i].lcdDev, SLOTS[i].lcd, lcdv, "배정 LCD");     // ⑥

    // 🔵 **못 냈으면 재시도 대상으로 남긴다.** 전에는 한 번 실패하고 **버렸다** —
    //   재접속 직후(등록 `D` 가 아직 안 온 순간)에 배정이 겹치면 **영영 안 켜진다.**
    //   ★ 실제 순서가 그랬다 : `+AUX 접속` → `모듈 없음(등록 0개)` → `발행 실패` → 배정.
    //   🔑 `⟳ 재동기화(등록 완료)` 가 **살아 있는 예약을 재하달**하는 자리다 — 거기가 복구 경로다.
    if (!ledOk) st[i].guideRetry = true;

    srv.log(std::string("⑤⑥ 자리 ") + slot + (byHuman ? " 배정(사람이 고름 · 잠금)"
                                                        : " 배정(자동 · 안내만)")
            + (ledOk ? " — 안내등 ON" : " — 🔴 **안내등 못 켰다**(등록 대기 · 재시도한다)")
            + (lcdv < 0 ? "" : (lcdOk ? " · LCD 에 번호" : " · 🔴 LCD 도 못 냈다")));
}

// 배정을 푼다 — 안내등 끄고 LCD 를 welcome 으로 되돌린다.
void releaseSlot(ParkingServer& srv, int i, const char* why) {
    if (i < 0 || i >= SLOT_N) return;
    st[i].assigned = false;
    st[i].locked   = false;
    st[i].guideRetry = false;   // 🔑 안내가 걷혔으면 **못 켠 것을 다시 낼 이유가 없다**
    cmd(srv, SLOTS[i].ledDev, SLOTS[i].led, LED_OFF, why);
    cmd(srv, SLOTS[i].lcdDev, SLOTS[i].lcd, LCD_WELCOME, why);
}

void toIdle(ParkingServer& srv) {
    g.shot = 0; g.plate.clear(); g.source.clear(); g.slot.clear(); g.attempts = 0;
    // 🔑 촬영 재시도 상태도 같이 지운다 — 안 지우면 **다음 차가 앞차의 시도 횟수를 물려받는다**.
    //   ⚠ `discarded` 는 **여기서 지운다**: 화면이 그것을 본 뒤 다음 차가 오면 사라져야 한다.
    g.shotTries = 0; g.shotAt = 0; g.retryAt = 0;
    g.lastError.clear(); g.discarded.clear(); g.closed.clear();
    vEF.reset();
    // ⚠ **이걸 빠뜨리면 다음 차부터 영영 선택을 못 한다** — 한 번 물은 채로 남는다.
    g.chooserAsked = false;
    setPhase(srv, PH_IDLE);
}

void publish(ParkingServer& srv) {
    ParkingServer::EntryStatus e;
    e.phase        = g.phase;
    e.elapsed_ms   = g.since ? (srv.nowMs() - g.since) : 0;
    // 🔑 **`limit_ms` 는 phase 마다 뜻이 다르다.** 한 칸에 여러 시한이 실린다 —
    //   화면은 phase 를 보고 읽어야 한다(web 과 합의).
    // 🔴 `shooting` 에는 **시한이 없다**(설계 2026-08-27). 기다리는 것은 상수가 아니라 **조건**이다 —
    //   ① 폰이 붙어 있나 ② 차가 있나. 그래서 여기서 `0`(시한 없음)을 낸다.
    //   ⚠ 화면은 `limit_ms == 0` 을 **"남은 시간을 그리지 않는다"** 로 읽는다(계약 §2-C).
    //     대신 `shot_wait_ms` 로 **경과**를 보여 준다 — *"얼마 남았나"* 가 아니라 *"얼마나 됐나"* 다.
    e.limit_ms     = (g.phase == PH_WAIT_PARK) ? PARK_CONFIRM_MS
                   : (g.phase == PH_GATE_OPEN) ? GATE_OPEN_MAX_MS
                   : (g.phase == PH_CHOOSING)  ? SELECT_WAIT_MS : 0;
    e.plate        = g.plate;
    e.plate_source = g.source;
    e.slot         = g.slot;
    e.attempts     = g.attempts;
    // ═══ 🔵 설계 [F] — **순수 추가 넷** (web 과 이름까지 합의했다) ═══════════
    //   🔑 어휘가 둘이라 **필드도 둘**이다:
    //     `shot_last_error` = **폰의 낱말**(해석하지 않는다)
    //     `plate_discarded` = **서버의 판단**(번호는 왔는데 차가 없었다 · 설계 [E])
    //   ★ 한 칸에 섞으면 화면이 *"폰이 실패했다"* 와 *"우리가 버렸다"* 를 **가를 근거가 없다**.
    //   ⚠ `shot_wait_ms` 는 `elapsed_ms` 와 **기산점이 다르다** — 이쪽은 **지금 요청을 낸 시각**이다.
    //     이름을 비슷하게 두면 다음 사람이 섞는다(web 이 그 이름을 골랐다).
    e.shot_tries      = g.shotTries;
    e.shot_wait_ms    = g.shotAt ? (srv.nowMs() - g.shotAt) : 0;
    e.shot_last_error = g.lastError;
    e.plate_discarded = g.discarded;
    e.shot_closed     = g.closed;
    srv.entryStatus(e);
}

// ── 입차 ───────────────────────────────────────────────────────────────────
void entryFlow(ParkingServer& srv) {
    const long long now = srv.nowMs();
    bool rose = false, fell = false;

    const SensorReading ef = srv.sensorReading(ENT_DEV, ENT_FRONT);
    const SensorReading er = srv.sensorReading(ENT_DEV, ENT_REAR);
    eEF.feed(ef.known, ef.value, &rose, &fell);
    const bool efRose = rose, efFell = fell;
    eER.feed(er.known, er.value, &rose, &fell);
    const bool erRose = rose, erFell = fell;

    // ═══ 🔴 **침묵을 깬다 — 거동은 한 줄도 안 바꾼다** (원장 §9.65) ══════════
    //   실측 2026-08-26 : WAIT_PARK 에 갇힌 3분 동안 **EF 가 세 번 올랐는데**
    //     `① 입구 앞 감지` 가 0회였고 **로그가 아예 없었다.**
    //   🔴 사용자에게는 *"센서를 가려도 아무 일도 안 일어난다"* 로만 보인다 —
    //     **고장과 구별이 안 된다.** 그것이 침묵의 값이다.
    //   ★ 무엇을 할지(앞차 안내를 버릴지)는 **설계 결정**이라 여기서 안 정한다.
    //     다만 *"왜 안 되는지"* 는 지금 말할 수 있고, 그건 공짜다.
    // 🔴🔴 **[D] — 뒤차가 오면 앞차 안내를 버린다** (사용자 확정 2026-08-26: *"가"*)
    //   근거(원장 §9.65) : `WAIT_PARK` 가 최대 `PARK_GIVEUP_MS`(3분) 입구를 통째로 막았다.
    //     실측으로 **EF 3회 상승 · 응답 0회 · 잠금 해제 9초 뒤 즉시 정상**이었다.
    //   ⚠ **`WAIT_PARK` 에서만 버린다.** `gate_open`·`passing` 의 EF 는 **같은 차**가
    //     게이트를 지나며 다시 가린 것이다 — 거기서 버리면 **도는 입차를 내가 깬다.**
    //     ★ 실기 18:53:40 에 정확히 그 줄(`gate_open`)이 찍혔다. 그래서 이 구분이 필요하다.
    if (efRose && g.phase == PH_WAIT_PARK) {
        const int ai = slotIndex(g.slot);
        // 🔑 **버린 번호를 같이 찍는다.** 그 차가 나중에 주차하면 이 줄이 유일한 단서다.
        srv.log("⚠ 앞 입차 건을 버린다 — 뒤차가 왔다(앞차 " + g.slot + " · 배정 후 "
                + std::to_string((now - g.since) / 1000) + "초 · 번호 "
                + (g.plate.empty() ? std::string("(없음)") : g.plate)
                + "). 안내등·LCD 를 끈다");
        if (ai >= 0) st[ai].abandoned = g.plate;      // 대가를 세려고 남긴다
        // 🔑 **시한이 하던 정리와 같은 경로**를 쓴다(루트 지시). 새로 만들면 두 갈래가 갈라진다.
        releaseSlot(srv, ai, "뒤차가 왔다 — 앞차 안내 해제");
        toIdle(srv);
        // ⚠ **여기서 return 하지 않는다.** 아래 시작 갈래로 흘러가 이 박자에 바로 시작한다 —
        //   `efRose` 는 이 박자에만 참이라, 미루면 **뒤차가 다음 상승까지 또 기다린다.**
    } else if (efRose && g.phase != PH_IDLE && g.phase != PH_REJECTED && g.phase != PH_FULL) {
        // 거동은 안 바꾼다. **왜 안 되는지만 말한다**(§9.65 · 침묵이 오늘의 결함이었다).
        srv.log("⚠ 입구 앞 감지 — 앞 입차 건이 진행 중이다(" + g.phase
                + "). **새 입차를 시작하지 않는다**");
    }

    // 🔴🔴 **`PH_REJECTED` 를 여기서 뺐다**(2026-08-27).
    //   ★ 전에는 시작 갈래가 `IDLE || REJECTED || FULL` 이었다. 그대로 두면
    //     `rejected` 가 **여기서 `!efRose` 로 먼저 return** 해서 **아래 재시도 갈래가 영영 안 돈다.**
    //   🔑 `rejected` 는 이제 *"손 입력을 받으면서 서버도 계속 시도한다"* 는 **진행 상태**다 —
    //     그 판정은 **아래 한 곳**이 한다. 여기서 가로채면 규칙이 두 곳이 된다.
    //   ⚠ 그러면 `rejected` 에서 새 입차는 어떻게 시작하나 : **4초 규칙이 `idle` 로 접은 뒤**다.
    //     차가 아직 있는데 새로 시작할 이유는 없다 — 그건 **같은 차**다.
    if (g.phase == PH_IDLE || g.phase == PH_FULL) {
        // ① 입구 앞 센서가 **새로 잡히면** 시작한다.
        //   🔑 모서리로 본다 — 값으로 보면 차가 서 있는 동안 매 박자 새로 시작한다.
        if (!efRose) return;
        srv.log("① 입구 앞 감지 — 촬영을 시작한다");
        vEF.reset();                        // 새 건이다. 앞 건의 "비었다" 기록을 물려받지 않는다
        setPhase(srv, PH_SHOOTING);
        // 🔴 **`return` 하지 않는다.** 아래 갈래로 흘러가 **이 박자에 바로 요청을 낸다** —
        //   미루면 다음 프레임(최악 1초)까지 기다린다. 사용자 요구는 *"감지 → **즉시** 요청"* 이다.
    }

    // ═══ 🔴🔴 **촬영 대기 — 시한이 아니라 조건이다** (설계 2026-08-27 · 사용자 확정) ═══
    //   `shooting`  = 시도 중이고 아직 손 입력 안내를 안 띄웠다
    //   `rejected`  = 🔵 **"지금 손 입력을 받는다"** — 그런데 **서버도 계속 시도한다**
    //   ★ 두 phase 를 **한 갈래로** 다룬다. 갈라 두면 재시도·판정 규칙이 두 곳이 되고 갈린다.
    if (g.phase == PH_SHOOTING || g.phase == PH_REJECTED) {
        // ── ① 🔴 **차가 아직 있나** (4초 규칙 · `unknown` 은 시계를 멈춘다)
        const bool carGone = vEF.feed(ef.known, ef.value, now, ENTRY_VACATE_MS);
        if (carGone) {
            vacateFire++;
            // 🔑 진행 중 요청을 **닫는다**(설계 [C]). 표시만 두면 ① 유령이 남고
            //   ② 화면이 *"촬영 중"* 에서 못 벗어나며 ③ 늦은 답이 되살아난다.
            if (g.shot > 0) srv.cameraCancel(g.shot, "car_gone");
            srv.log("⚠ 입구가 " + std::to_string(ENTRY_VACATE_MS / 1000)
                    + "초 이상 비었다 — 차가 갔다고 본다. 이번 건을 접는다"
                    + (g.shotTries ? " (촬영 " + std::to_string(g.shotTries) + "회 시도했다)"
                                   : std::string())
                    + " · 4초 규칙 누적 " + std::to_string(vacateFire) + "회");
            toIdle(srv);
            return;
        }

        // ── ② 답이 왔나 (성공/실패 어느 쪽이든 **먼저** 본다)
        const int cs = g.shot > 0 ? srv.cameraState(g.shot) : CAM_NONE;
        if (cs == CAM_READY) {
            // 🔴 **[E] — 번호는 왔는데 차가 없으면 차단봉을 안 연다**(사용자 확정).
            //   ⚠ 위 ①에서 이미 걸렀지만 **`unknown` 이면 ①이 판정하지 않는다** —
            //     그때 여기까지 온다. 그러니 **여기서 다시 묻는다.**
            //   ★ 그리고 *"센서를 못 믿는 상태"* 와 *"차가 확실히 없다"* 를 **갈라 센다** —
            //     안 가르면 나중에 *"차가 갔다"* 와 *"링크가 흔들렸다"* 를 구별할 수 없다([ㄹ]).
            const bool carHere = ef.known && ef.value;
            if (!carHere) {
                plateNoCar++;
                if (!ef.known) plateNoCarUnknown++;
                g.discarded = ef.known ? "no_car" : "sensor_unknown";
                srv.log("🔴 번호는 왔는데 **차가 없다** — 버린다(차단봉 안 연다) · 번호 "
                        + srv.cameraPlate(g.shot)
                        + " · 센서 " + (ef.known ? (ef.value ? "막힘" : "비었음")
                                                 : std::string("**모름(링크)**"))
                        + " · 비어 있던 " + std::to_string(vEF.heldMs(now)) + "ms"
                        + " · 누적 " + std::to_string(plateNoCar)
                        + "회(그중 센서 모름 " + std::to_string(plateNoCarUnknown) + ")");
                srv.cameraCancel(g.shot, g.discarded);
                g.shot = 0; g.shotAt = 0;
                // 🔑 **흐름을 접지 않는다** — 차가 다시 보이면(또는 사람이 넣으면) 이어서 간다.
                //   ⚠ 차가 정말 갔으면 위 ①이 4초 뒤에 접는다. **판정은 한 곳에서만** 한다
                setPhase(srv, PH_REJECTED);
                return;
            }
            g.plate = srv.cameraPlate(g.shot);
            g.source = "camera";
            srv.log("번호 도착 — " + g.plate + " (촬영 " + std::to_string(g.shotTries) + "회째 · "
                    + std::to_string(g.shotAt ? now - g.shotAt : 0) + "ms)");
            setPhase(srv, PH_ASSIGNING);
            return;
        }
        if (cs == CAM_FAILED) {
            // 🔵 **폰 낱말을 그대로 담는다.** 서버는 해석하지 않는다(어휘 정본은 앱이다).
            g.lastError = srv.cameraReason(g.shot);
            if (g.lastError.empty()) g.lastError = "unknown";
            g.shot = 0; g.shotAt = 0;
            // 🔴 **재요청의 방아쇠는 이 실패 응답이다.** 타이머가 아니다 —
            //   그래서 **겹침이 원리적으로 없다**(응답 하나에 요청 하나).
            const bool slow = (g.shotTries >= SHOT_FAST_TRIES);
            g.retryAt = slow ? now + SHOT_SLOW_GAP_MS : 0;
            srv.log(std::string(slow ? "⏳" : "↻") + " 촬영 실패 — 사유 " + g.lastError
                    + " (" + std::to_string(g.shotTries) + "회째)"
                    + (slow ? " · 간격을 " + std::to_string(SHOT_SLOW_GAP_MS / 1000)
                              + "초로 벌린다. **손으로 넣어도 된다**(plate_manual)"
                            : " · 바로 다시 요청한다"));
            // 🔑 상한을 넘기면 **`rejected` 로 간다** — 그것이 화면의 입력칸을 여는 유일한 값이다.
            //   ⚠ **멈추는 것이 아니다.** 차가 있는 한 계속 시도한다(사용자 확정)
            if (slow) setPhase(srv, PH_REJECTED);
            // 🔴 **`return` 하지 않는다.** 아래 ③으로 흘러 **이 박자에 바로 재요청**한다 —
            //   미루면 다음 프레임(최악 1초)까지 기다린다. 설계는 *"실패 응답이 곧 방아쇠"* 다.
            //   ⚠ 느린 구간이면 ③이 `retryAt` 를 보고 알아서 기다린다. **판정은 한 곳이다.**
        }

        // ── ③ 진행 중인 요청이 없으면 **새로 낸다**(첫 요청 또는 재요청)
        if (g.shot <= 0) {
            if (now < g.retryAt) return;             // 느린 구간 — 간격을 기다린다
            const long long id = srv.cameraShoot();
            if (id > 0) {
                g.shot = id; g.shotAt = now; g.shotTries++;
                // 🔴 **앞 시도의 판정 둘을 같이 지운다** — 새 시도가 그것을 대체한다.
                //   ⚠ `discarded` 를 안 지우면 **재시도가 성공했을 때도 남는다** →
                //     화면이 *"자리를 배정했습니다"* 와 *"차량이 확인되지 않아 진행하지 않았습니다"* 를
                //     **동시에** 그린다. 🔑 web 이 *"둘 다 오면 둘 다 적는다"* 로 짰기 때문에
                //     그 모순이 **조용히 안 사라지고 화면에 그대로 뜬다** — 그래서 여기서 지운다.
                //   ★ 판별자 : 이 둘은 **"직전 시도에 무슨 일이 있었나"** 다. 새 시도가 시작되면 과거다.
                g.closed.clear();
                g.discarded.clear();
                srv.log("② 촬영 요청 " + std::to_string(id)
                        + " (" + std::to_string(g.shotTries) + "회째)");
                if (g.phase == PH_SHOOTING) g.since = now;   // 경과 표시를 새 시도 기준으로
            } else {
                // 🔴 폰이 없다 — **발급 자체가 안 된다.** 사람이 넣을 수 있게 문을 연다.
                if (g.phase != PH_REJECTED) {
                    srv.log("🔴 촬영 요청을 못 만들었다 — 폰이 안 붙어 있다. "
                            "**손으로 넣을 수 있다**(plate_manual). 폰이 돌아오면 다시 시도한다");
                    setPhase(srv, PH_REJECTED);
                }
                g.closed = "phone_gone";              // 🔵 화면이 *"폰이 끊겼다"* 를 정확히 말한다
                g.retryAt = now + SHOT_SLOW_GAP_MS;   // 폰 없이 매 박자 두드리지 않는다
            }
            return;
        }

        // ── ④ 🔴 **최후 보루.** 조건 둘이 다 참인데도 이만큼 지났다
        //   ★ **이것이 뜨면 "우리가 모르는 경우가 있다" 는 신호다.** 그래서 문구를 따로 둔다.
        //   🔑 특히 **half-open** — 폰이 죽었는데 소켓이 안 끊긴 경우가 여기로 온다(android 확인).
        if (g.shotAt && now - g.shotAt >= SHOT_HARD_CAP_MS) {
            srv.cameraCancel(g.shot, "cap");
            // 🔴 **`lastError` 에 넣지 않는다** — 그 칸은 **폰의 낱말**뿐이다(web 이 잡았다).
            //   ★ `cap` 은 폰이 실패했다는 뜻이 아니라 **우리가 원인을 모른다는 신호**다.
            g.closed = "cap";
            g.shot = 0; g.shotAt = 0; g.retryAt = now + SHOT_SLOW_GAP_MS;
            srv.log("🔴🔴 **최후 보루 발동** — 폰도 붙어 있고 차도 있는데 "
                    + std::to_string(SHOT_HARD_CAP_MS / 1000)
                    + "초 무응답이다. 요청을 닫는다. **이 줄이 뜨면 우리가 모르는 경우가 있다는 뜻이다**");
            setPhase(srv, PH_REJECTED);
        }
        return;
    }

    if (g.phase == PH_ASSIGNING) {
        // ═══ 🔴 **갈래가 둘이다** (SPEC-web8080-8081-2026-08-26) ═══════════════
        //   차단봉이 들리는 **그 순간**에 선택 화면이 붙어 있나?
        //     없음 → 🔑 **기존 그대로.** 시스템이 빈 자리 중 A1 부터 자동 배정
        //     있음 → 🔴 **사람이 고른다.** 차단봉을 먼저 열고 고르게 한다
        //   ⚠ **판정은 여기서 한 번만** 한다. 그 뒤에 붙는 기기는 이번 판에 관여 안 한다 —
        //     매 박자 다시 물으면 판정이 흔들린다(명세가 못 박은 것).
        //   ★ **모순이 아니라 갈래 둘이다** — 누가 정하느냐가 다를 뿐이다.
        // 🔴🔴 **선택 서비스 일시 중지 스위치**(사용자 지시 2026-08-27 11:20)
        //   사용자 원문 : *"8081 사용자 자리 선택 서비스를 **일시 중지**하여, 자리배정 및
        //   안내등이 **입구 차단봉 개폐와 연동되는 구조가 보이도록** 하라."*
        //   ★ 8081 에 기여자가 붙어 있으면 매번 이 갈래로 가서 **차단봉이 열린 뒤 5초 동안
        //     안내등도 LCD 도 없다.** 그러면 *"차단봉↔안내등이 한 순간"* 이 안 보인다.
        //   🔵 **끄면** 아래 자동배정으로 흘러 `③ 차단봉 열기` 와 `⑤⑥ 안내등 ON` 이 **같은 초**에 난다.
        //   🔴 **지우지 마라. "일시 중지" 다** — `--no-chooser` 를 빼면 그대로 돌아온다.
        //   ⚠ 8081 **화면은 살아 있다**(접속·표시 그대로). 자리 배정에만 관여 안 한다.
        if (!srv.chooserEnabled()) {
            // 🔑 **끈 것이 보여야 한다** — 안 보이면 다음 사람이 *"왜 선택이 안 되지"* 를 못 푼다.
            //   ⚠ 매 박자 찍으면 로그를 덮으므로 **이번 입차 건에 한 번만** 찍는다.
            if (!g.chooserAsked && srv.chooserCount() > 0) {
                g.chooserAsked = true;            // 이 건에서 다시 안 묻는다(경고도 한 번만)
                srv.log("⏸ 선택 서비스 **중지 중**(`--no-chooser`) — 붙어 있는 선택 화면 "
                        + std::to_string(srv.chooserCount())
                        + "대를 **무시하고 자동 배정**한다. 화면은 살아 있다");
            }
        } else
        if (!g.chooserAsked && srv.chooserCount() > 0) {
            g.chooserAsked = true;
            if (pickSlot(srv).empty()) {          // 고를 것이 없으면 선택도 뜻이 없다
                srv.log("🔴 빈 자리가 없다 — 선택 화면이 있어도 차단봉을 열지 않는다");
                setPhase(srv, PH_FULL);
                return;
            }
            cmd(srv, ENT_GATE_DEV, ENT_GATE, GATE_OPEN, "입구 차단봉(선택 갈래)");
            srv.log("③ 차단봉 열기 → 선택 화면 " + std::to_string(srv.chooserCount()) +
                    "대가 붙어 있다. 사람이 자리를 고른다");
            setPhase(srv, PH_CHOOSING);
            return;
        }
        // ⑤ **차단봉을 열기 전에** 배정한다(명세 §1 — 순서를 바꾼 곳).
        //   🔑 근거: 차단봉이 먼저 열리면 운전자가 **어디로 갈지 모르는 채로** 들어온다.
        const std::string s = pickSlot(srv);
        if (s.empty()) {
            // [가] 빈자리 없음 — 🔴 **차단봉을 안 연다**(명세 기본값. 내가 정한 것이 아니다)
            srv.log("🔴 빈 자리가 없다 — 차단봉을 열지 않는다");
            setPhase(srv, PH_FULL);
            return;
        }
        applyAssign(srv, s, false);   // 자동배정 — 안내만
        setPhase(srv, PH_ASSIGNED);
        return;
    }

    if (g.phase == PH_CHOOSING) {
        // 🔴 **차가 다 들어가면 선택 창을 닫는다**(명세: *"EF 에서 차량이 진입 후 사라지면"*).
        //   🔑 창을 닫는 것이지 **배정을 없애는 것이 아니다** — 차는 이미 안에 있고
        //     자리가 있어야 한다. 그래서 시한 폴백과 **같은 곳으로** 간다.
        //   ⚠ `chooserAsked` 가 이미 참이라 이 `assigning` 은 자동배정으로 간다.
        if (efFell) {
            srv.log("차가 다 들어갔다 — 선택 창을 닫고 자동배정으로 간다");
            setPhase(srv, PH_ASSIGNING);
            return;
        }
        // 🔴 [가] **안 고르면 기본 프로세스로 폴백한다**(명세 기본값. 내가 바꾸지 않는다).
        //   ⚠ 사람이 안 누른다고 차가 서 있으면 안 된다 — 차단봉은 이미 열려 있다.
        if (now - g.since >= SELECT_WAIT_MS) {
            srv.log("🔴 " + std::to_string(SELECT_WAIT_MS) +
                    "ms 안에 안 골랐다 — 기본 자동배정으로 돌아간다");
            // 🔑 `chooserAsked` 가 이미 참이라 이 `assigning` 은 **자동배정으로 간다.**
            setPhase(srv, PH_ASSIGNING);
            return;
        }
        return;
    }

    if (g.phase == PH_ASSIGNED) {
        // ③ 안내가 켜진 **다음에** 차단봉을 연다.
        cmd(srv, ENT_GATE_DEV, ENT_GATE, GATE_OPEN, "입구 차단봉");
        srv.log("③ 입구 차단봉 열기");
        setPhase(srv, PH_GATE_OPEN);
        return;
    }

    if (g.phase == PH_GATE_OPEN) {
        if (erRose) { srv.log("입구 뒤 센서 감지 — 통과 중"); setPhase(srv, PH_PASSING); return; }
        // ⚠ 명세에 없는 상한이다. 근거: **열린 채 남는 것이 닫히는 것보다 위험하다.**
        //   아무도 안 지나가면(마음을 바꿔 후진) ER 이 영영 안 온다.
        if (now - g.since >= GATE_OPEN_MAX_MS) {
            // 🔴 **`WAIT_PARK` 로 보내면 안 된다** — 통과가 없었는데 자리·안내등이 3분 묶이고
            //   그동안 **입구가 통째로 봉쇄된다**(다음 차의 촬영 요청이 안 나간다).
            srv.log("🔴 차단봉을 연 지 " + std::to_string(GATE_OPEN_MAX_MS) +
                    "ms — 통과가 없다. 닫고 배정을 푼다 " + g.slot);
            cmd(srv, ENT_GATE_DEV, ENT_GATE, GATE_CLOSE, "통과 없음");
            releaseSlot(srv, slotIndex(g.slot), "통과 없음 — 배정 해제");
            toIdle(srv);
        }
        return;
    }

    if (g.phase == PH_PASSING) {
        // ④ 두 번째 센서에서 **사라지면** 다 지나간 것이다.
        if (erFell) { setPhase(srv, PH_GATE_CLOSE); }
        return;
    }

    if (g.phase == PH_GATE_CLOSE) {
        cmd(srv, ENT_GATE_DEV, ENT_GATE, GATE_CLOSE, "입구 차단봉");   // ④
        srv.log("④ 입구 차단봉 닫기 — 자리 " + g.slot + " 에 주차를 기다린다");
        setPhase(srv, PH_WAIT_PARK);
        return;
    }

    if (g.phase == PH_WAIT_PARK) {
        // 🔑 주차 판정 자체는 `onOccupancy` 가 한다 — **자리 IR 하나가 판정 입력**이다.
        //   여기서는 **포기 상한**만 본다. 없으면 안 들어온 차 하나가 자리를 영원히 죽인다.
        if (now - g.since >= PARK_GIVEUP_MS) {
            const int i = slotIndex(g.slot);
            srv.log("🔴 " + std::to_string(PARK_GIVEUP_MS) + "ms 안에 안 들어왔다 — 배정 해제 " + g.slot);
            releaseSlot(srv, i, "배정 포기");
            toIdle(srv);
        }
        return;
    }
}

// ── 출차 ───────────────────────────────────────────────────────────────────
// 🔑 **입차와 독립이다.** ⑪(자리 비움)과 ⑫(출구 접근)를 연결하지 않는다 —
//   연결하면 *"자리를 뜬 차가 아닌 다른 차가 출구에 오면"* 안 열린다. 그건 틀린 동작이다.
void exitFlow(ParkingServer& srv) {
    bool rose = false, fell = false;
    const SensorReading xf = srv.sensorReading(EXT_DEV, EXT_FRONT);
    const SensorReading xr = srv.sensorReading(EXT_DEV, EXT_REAR);
    eXF.feed(xf.known, xf.value, &rose, &fell);
    const bool xfRose = rose;
    eXR.feed(xr.known, xr.value, &rose, &fell);
    const bool xrFell = fell;

    if (!xGateOpen && xfRose) {                                   // ⑫
        cmd(srv, EXT_GATE_DEV, EXT_GATE, GATE_OPEN, "출구 차단봉");
        srv.log("⑫ 출구 앞 감지 → 차단봉 열기");
        xGateOpen = true; xGateSince = srv.nowMs();
        return;
    }
    if (xGateOpen && (xrFell || srv.nowMs() - xGateSince >= GATE_OPEN_MAX_MS)) {   // ⑬
        cmd(srv, EXT_GATE_DEV, EXT_GATE, GATE_CLOSE, "출구 차단봉");
        srv.log(xrFell ? "⑬ 출구 뒤 통과 완료 → 차단봉 닫기"
                       : "🔴 출구 차단봉 상한 초과 — 통과가 없다. 닫는다");
        xGateOpen = false;
    }
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════
// 기여자 진입점 넷
// ══════════════════════════════════════════════════════════════════════════

// ② 한 박자마다 — 🔑 **한 걸음만 나아간다.** 여기서 기다리지 않는다.
// 🔴🔴 **못 켠 안내등을 다시 낸다**(사용자 신고 2026-08-27 10:43).
//   전에는 `cmd()` 가 실패하면 **버렸다.** 그런데 실패는 대개 **일시적**이다 —
//   보드가 방금 재접속해 등록(`D`)이 아직 안 온 순간이면 몇 초 뒤에 된다.
//   ★ 실측 순서 : `+AUX 접속` → `모듈 없음(등록 0개)` → `발행 실패` → 그리고 **그대로 끝**.
//   🔑 **성공할 때까지만** 다시 낸다 — 자리 안내가 걷히면(`releaseSlot`) 같이 꺼진다.
//   ⚠ 매 박자 두드리는 것이 싸다 : 대상이 **자리 다섯**뿐이고 실패는 드물다.
//     그리고 **성공하면 그 박자에 표지가 꺼진다** — 무한히 두드리지 않는다.
static void retryGuides(ParkingServer& srv) {
    for (int i = 0; i < SLOT_N; i++) {
        if (!st[i].guideRetry) continue;
        if (!st[i].assigned) { st[i].guideRetry = false; continue; }   // 안내가 이미 걷혔다
        if (cmd(srv, SLOTS[i].ledDev, SLOTS[i].led, LED_ON, "배정 안내등(재시도)")) {
            st[i].guideRetry = false;
            srv.log(std::string("🔵 안내등 재시도 성공 — 자리 ") + SLOTS[i].slot
                    + " (등록이 늦어 처음에 못 냈던 것이다)");
        }
    }
}

void onTick(ParkingServer& srv) {
    retryGuides(srv);          // 🔑 흐름보다 **먼저** — 못 켠 것이 한 박자라도 빨리 켜지게
    entryFlow(srv);
    exitFlow(srv);
    publish(srv);
}

void onCmdResult(const CmdResult& r) {
    // ⚠ `result=0` 은 *"장치가 명령을 받아들였다"* 지 *"그렇게 됐다"* 가 아니다(§9.42).
    if (r.kind != CmdResult::OK) std::cout << "[명령] 🔴 " << r.module << " " << r.value
                           << " -> " << r.kindName() << "\n";
}

// ⑦⑧ 주차 판정 — 🔴 **판정 입력은 그 자리의 IR 하나뿐이다.**
void onOccupancy(ParkingServer& srv, const std::string& spot,
                 const std::string& module, bool occupied,
                 const ModuleMeasure& measure) {
    (void)measure; (void)module;
    const int i = slotIndex(spot);
    if (i < 0) return;                       // 입·출구는 주차 판정 대상이 아니다
    st[i].seen = true; st[i].occupied = occupied;

    if (occupied) {
        // 🔴🔴 **누구의 차인가는 `배정`이 답한다.** phase 는 그 판정의 입력이 아니다.
        //   ~~`phase == WAIT_PARK || GATE_CLOSE` 일 때만 ⑦~~ — **틀렸다.** 실측으로 뒤집혔다:
        //     2026-08-26 실기 · ⑦ **2건** vs ⑧ **4건**. 네 건 전부 **배정받은 그 차가
        //     배정받은 그 자리에 댄 것**인데 ER 이 아직 안 풀렸다는 이유로 NonHuman 이 됐다.
        //   🔴 그 갈래는 **번호판을 저장하지 않는다**(⑩) — 그래서 넷 다 `data_log` 에 안 남았다.
        //     그리고 정상 주차한 운전자의 LCD 에 **경고(NonHuman)** 가 떴다.
        //   ★ 왜 뒤집혔나 : ⑦ 이 되려면 **ER 이 올랐다 내려간 뒤에** 자리가 차야 하는데
        //     **그 순서를 보장하는 것이 아무것도 없다.** 주차장이 작을수록 뒤집힌다 —
        //     차가 게이트를 다 빠져나가기 전에 자리에 닿는다(성공 건은 ER 이 1초, 실패 건은 4초+).
        if (st[i].assigned && spot == g.slot && g.phase != PH_IDLE) {
            // 🔴 **차단봉이 아직 열려 있으면 여기서 닫는다.** 옛 주석이 걱정한 것이 이것이고,
            //   그 걱정은 옳았다 — 아래 `toIdle()` 로 빠지면 ④ 를 **영영 건너뛴다.**
            //   🔑 답은 *"⑦ 을 거절한다"* 가 아니라 *"⑦ 이 닫는다"* 였다.
            //   ★ 자리가 찼다는 것은 그 차가 **이미 지나갔다**는 뜻이다. 닫아도 안전하다.
            if (g.phase == PH_GATE_OPEN || g.phase == PH_PASSING || g.phase == PH_CHOOSING) {
                cmd(srv, ENT_GATE_DEV, ENT_GATE, GATE_CLOSE, "주차 완료 — 통과로 본다");
                srv.log("④ 입구 차단봉 닫기 — 자리 " + spot + " 가 찼다(통과 완료로 본다)");
            }
            // ⑦ 정상 주차 — 안내는 할 일을 다했다. 끄고 번호를 기록한다.
            cmd(srv, SLOTS[i].ledDev, SLOTS[i].led, LED_OFF, "주차 완료");
            cmd(srv, SLOTS[i].lcdDev, SLOTS[i].lcd, LCD_WELCOME, "주차 완료");
            srv.slotPlate(spot, g.plate, g.source.empty() ? "camera" : g.source);  // ⑨
            srv.log("⑦ 자리 " + spot + " 정상 주차 — 번호 " + g.plate + " 기록");
            st[i].assigned = false;
            st[i].locked   = false;
            st[i].abandoned.clear();   // 정상 주차로 닫혔다 — 대가가 아니다
            toIdle(srv);
        } else {
            // ⑧ 비정상 주차 — 🔴 **번호판을 저장하지 않는다.** 자리만 관리한다.
            //   ⚠ 배정된 자리의 안내등·LCD 는 **그대로 둔다** — 원래 가야 할 곳을 계속 가리킨다.
            cmd(srv, SLOTS[i].lcdDev, SLOTS[i].lcd, LCD_NONHUMAN, "비정상 주차");
            srv.log("⑧ 자리 " + spot + " 배정 없이 점유 — NonHuman · 번호판 저장 안 한다");
            // 🔴 **[가] 의 대가가 실제로 났나** — 버린 앞차가 결국 그 자리에 댔다.
            //   ★ 이 줄이 뜨면 *"우리가 안내를 걷었는데 그 차는 왔다"* 는 뜻이다.
            //     번호를 안다면 사람이 손으로 이어 붙일 수 있다 — **그래서 번호를 찍는다.**
            if (!st[i].abandoned.empty()) {
                abandonedThenParked++;
                srv.log("🔴 버린 앞차가 결국 여기 주차했다 — " + spot + " · 번호 "
                        + st[i].abandoned + " (누적 " + std::to_string(abandonedThenParked)
                        + "회). **번호판이 기록되지 않는다** — [가] 의 대가다");
                st[i].abandoned.clear();
            }
        }
        return;
    }

    // ⑪ 출차 — 자리가 비었다. 🔑 `data_log.json` 의 기록은 **지우지 않는다**(입차 이력이다).
    cmd(srv, SLOTS[i].ledDev, SLOTS[i].led, LED_OFF, "출차");
    cmd(srv, SLOTS[i].lcdDev, SLOTS[i].lcd, LCD_WELCOME, "출차");
    srv.slotPlate(spot, std::string(), std::string());
    st[i].abandoned.clear();   // 🔑 자리가 비었으면 그 표식은 뜻을 잃는다
    srv.log("⑪ 자리 " + spot + " 비었다 — 안내등·LCD 초기화, 번호판 해제");
}

void onControllerReset(ParkingServer& srv) {
    for (int i = 0; i < SLOT_N; i++) releaseSlot(srv, i, "제어기 초기화");
    cmd(srv, ENT_GATE_DEV, ENT_GATE, GATE_CLOSE, "제어기 초기화");
    cmd(srv, EXT_GATE_DEV, EXT_GATE, GATE_CLOSE, "제어기 초기화");
    xGateOpen = false;
    toIdle(srv);
    srv.log("[제어기] 안내등·LCD·차단봉을 모두 되돌렸다");
}

// ⑥ 수동 입력 폴백 — [나] 번호 인식 실패의 대안(명세 §1).
ParkingServer::ManualPlateResult onManualPlate(ParkingServer& srv, const std::string& plate,
                                               const std::string& source) {
    ParkingServer::ManualPlateResult r;
    g.attempts++;
    // 🔑 **아무 때나 받지 않는다.** 흐름이 번호를 기다리는 동안에만 뜻이 있다.
    if (g.phase != PH_SHOOTING && g.phase != PH_REJECTED) {
        r.code = "not_ready";
        r.message = "지금은 번호를 기다리는 중이 아닙니다";
        return r;
    }
    // 🔴 **빈 번호를 받지 않는다.** 빈 값은 `slotPlate()` 에서 *"번호를 지운다"* 는 뜻이라
    //   그대로 흘리면 자리의 번호판이 조용히 사라진다(§"한 칸에 두 뜻을 섞지 마라").
    if (plate.empty()) {
        r.code = "bad_request";
        r.message = "번호가 비어 있습니다";
        return r;
    }
    g.plate = plate;
    // 🔴 **출처를 지어내지 않는다.** 이 값이 `data_log.json` 에 그대로 간다 —
    //   폰이 읽은 것을 `manual` 로 적으면 나중에 인식률을 세는 사람이 틀린 분모를 쓴다.
    g.source = source.empty() ? std::string("manual") : source;
    srv.log(std::string(g.source == "camera" ? "폰이 보낸 번호(요청번호 없음) — "
                                             : "손으로 입력한 번호 — ")
            + plate + " (시도 " + std::to_string(g.attempts) + "회)");
    setPhase(srv, PH_ASSIGNING);
    return r;                                  // 빈 code = 수락
}

// ⑦ 🔴 **자리 선택**(상행 `pick_slot`) — 8081 화면이 고른 자리를 올린다.
//   🔑 **판정이 여기 있다.** 봉투를 받는 곳(`wsapi.h`)에 두면 흐름을 아는 곳이 둘이 된다.
ParkingServer::ManualPlateResult onSlotPick(ParkingServer& srv, const std::string& slot) {
    ParkingServer::ManualPlateResult r;
    // 🔴 **고를 수 있는 때가 아니면 거절한다.** 아무 때나 받으면 배정이 흐름 밖에서 바뀐다.
    if (g.phase != PH_CHOOSING) {
        r.code = "not_ready";
        r.message = "지금은 자리를 고를 때가 아닙니다";
        return r;
    }
    const int i = slotIndex(slot);
    if (i < 0) { r.code = "bad_request"; r.message = "그런 자리가 없습니다"; return r; }
    // 🔴 [다] **그 사이 찼으면 거절하고 다시 고르게 한다**(명세 기본값).
    //   ⚠ `phase` 를 안 바꾼다 — 사람은 여전히 고르는 중이다.
    // 🔑 **`locked` 로 본다**(REQ-0483) — 자동으로 안내 중인 자리는 사람이 골라도 된다.
    //   그 자리를 정말 쓸 수 없게 만드는 것은 **실제 점유**뿐이다.
    if (st[i].locked || !srv.parkingSpotAvailable(slot)) {
        r.code = "not_reservable";
        r.message = "그 자리는 방금 찼습니다 — 다른 자리를 골라 주세요";
        return r;
    }
    // 🔑 [나] **먼저 고른 사람이 이긴다** — 이 함수가 한 박자 안에서 순서대로 불리므로
    //   두 번째 사람은 위 `assigned` 검사에 걸린다. 따로 잠글 것이 없다.
    applyAssign(srv, slot, true);   // 🔑 사람이 골랐다 — 이때만 잠근다
    // ⚠ **차단봉은 이미 열려 있다**(선택 갈래는 열고 나서 고르게 한다) — 다시 열지 않는다.
    setPhase(srv, PH_GATE_OPEN);
    srv.log("사람이 자리를 골랐다 — " + slot);
    return r;                                   // 빈 code = 수락
}
