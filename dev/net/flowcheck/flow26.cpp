// flow26.cpp — `서머리/server/lot.cpp` 의 **13단계 흐름을 실제로 굴린다.**
//
// ═══════════════════════════════════════════════════════════════════════════
// 🔴 **무엇이 진짜이고 무엇이 가짜인가** — 이것이 이 하니스의 판정 근거다
//
//   진짜 : `서머리/server/lot.cpp` 전체. 흐름 함수·상태기계·시한·번호판 환산 — **한 줄도 안 고쳤다**
//   가짜 : `ParkingServer` 의 **껍데기만**. 센서 값·시계·명령 발행을 시험이 쥔다
//
// 🔴🔴 **로그 문구로 검사할 때의 규율** (web 이 같은 함정을 세 번 밟고 정리해 줬다)
//   ★ **어휘가 갈리면 그 어휘에 묶인 검사도 같이 낡는다** — 그리고 **멀쩡한 코드를 빨갛게** 만든다.
//     오늘 이 파일에서 두 번 났다: `입구 앞 감지 → ` 가 갈라지면서 · 시나리오 P 의 거동 검사.
//
//   ✅ 순서 : ① **거동으로 잴 수 있으면 거동으로 재라**(`valueOf` · `plates` · `entry.*`)
//            ② 못 재는 것만 로그로 — **그때는 로그 자체가 산출물**이다(침묵 깨기 · 계수 · 사유)
//            ③ 로그로 잴 때는 **그 사건에만 있는 가장 짧은 구절**을 써라(낱말 말고)
//   🔴🔴 **그런데 이 하니스가 증명하지 못하는 것이 하나 있다**(monitor 가 오늘 실물로 보였다):
//     `logHas("…")` 검사는 **내가 쓴 로그를 내가 쓴 검사로** 본다 —
//     ★ 문구를 **틀리게 써도 초록**이다. 그런데 그 문구는 **monitor 파서와의 계약**이다.
//     🔑 그가 어제 *"자가검사 10/10"* 을 냈는데 그것이 **자기가 상상한 형식과의 10/10** 이었다.
//       그리고 내 산출물을 받자마자 **둘 다 못 읽었다.**
//   > ### ★ **생산자의 검사는 소비자를 대신할 수 없다.**
//   ✅ 그래서 로그 문구를 고칠 때는 **`docs/net/LOG-GREP.md` 를 같이 고쳐라** —
//     그것이 monitor 가 실제로 읽는 계약이다. 🔴 오늘 내가 명세만 고치고 그것을 빠뜨렸다.
//
//   🔑 판별자 : **"이 문구를 더 낫게 고치면 이 검사가 깨지나?"**
//     깨지면 그 검사는 **문구를 지키는 것**이지 **거동을 지키는 것**이 아니다.
//     ⚠ 다만 *"로그가 산출물"* 인 검사는 깨지는 것이 **맞다** — 그때는 문구가 곧 기능이다.
//
// 🔑 하니스가 대상보다 관대하면 시험이 결함을 숨긴다 — 그래서 **로직을 흉내내지 않았다.**
//   시간도 진짜로 흐르게 하지 않고 **값으로 밀어 넣는다**(`fake_now`), 그래서 시한 경계를
//   1ms 단위로 정확히 밟을 수 있다.
//
// 재현:
//   c++ -std=c++11 -I../../서머리/server flow26.cpp ../../서머리/server/lot.cpp -o /tmp/f26
//
// 🔴 **서버를 안 띄운다.** 포트도 프로세스도 안 쓴다 — 그래서 실기 판을 건드리지 않는다.
//   ★ 그리고 **8080·8081 이 없어도 흐름이 도는 것**이 이 하니스로 증명된다:
//     여기엔 그 포트가 **아예 없다.** 그런데 한 바퀴가 다 돈다(수용 조건).
// ═══════════════════════════════════════════════════════════════════════════
#include "parking.h"
// 🔑 시한 값을 **여기 다시 쓰지 않는다.** 시험이 자기 숫자를 들면
//   상수를 바꿨을 때 **시험만 통과하고 코드가 틀린다.**
#include "config.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

// ── 시험이 쥐는 세계 ──────────────────────────────────────────────────────
namespace H {
long long now = 1000;                              // 단조 시계. 시험이 민다
std::map<std::string, SensorReading> sensors;      // "F1\tA1" → 값
std::vector<std::string> sent;                     // 발행된 명령 "F5\tL1=1"
std::map<std::string, long> lastValue;             // 모듈 → 마지막 값
std::string lastPhase;
ParkingServer::EntryStatus entry;
std::map<std::string, std::string> plates;         // 자리 → 번호판
std::map<std::string, std::string> plateSrc;
int  camState = CAM_PENDING;
std::string camPlate;
long long shotSeq = 0;
// 🔴 **폰이 안 붙어 있으면 서버는 요청번호를 아예 발급하지 않는다**(`persist.h` shot_nophone_).
//   `cameraShoot()` 이 `-1` 을 주는 것은 "실패" 가 아니라 **"요청을 안 만들었다"** 다.
bool camCanIssue = true;
bool allReady = true;
std::vector<std::string> logs;

std::string key(const std::string& d, const std::string& m) { return d + "\t" + m; }
void setSensor(const std::string& d, const std::string& m, bool known, bool value) {
    sensors[key(d, m)] = SensorReading(known, value);
}
long valueOf(const std::string& m) {
    std::map<std::string, long>::const_iterator it = lastValue.find(m);
    return it == lastValue.end() ? -999 : it->second;
}
bool sentAny(const std::string& m) { return lastValue.count(m) != 0; }
// 🔑 값만 보면 *"열렸다가 닫혔다"* 를 **못 가린다.** 사건은 로그에 물어야 한다.
bool logHas(const std::string& frag) {
    for (size_t i = 0; i < logs.size(); i++)
        if (logs[i].find(frag) != std::string::npos) return true;
    return false;
}
void clearSent() { sent.clear(); }
}  // namespace H

// ── `ParkingServer` 껍데기 — 🔑 **lot.cpp 가 부르는 것만** 정의한다 ─────────
struct ParkingServer::Impl { int dummy; };
ParkingServer::ParkingServer(const ParkingLot&) : p_(0) {}
ParkingServer::~ParkingServer() {}
long long ParkingServer::nowMs() const { return H::now; }
void ParkingServer::log(const std::string& msg) const { H::logs.push_back(msg); }
bool ParkingServer::send(const std::string& devid, const std::string& moduleName, long value) {
    H::sent.push_back(devid + "\t" + moduleName + "=" + std::to_string(value));
    H::lastValue[moduleName] = value;
    return true;
}
bool ParkingServer::moduleReady(const std::string&, const std::string&) const { return H::allReady; }
// 🔑 **시험이 쥔다** — 8081 화면이 붙었나 안 붙었나로 갈래가 갈린다.
namespace H {
    int  choosers  = 0;
    // 🔵 선택 서비스 스위치 — **기본은 켜짐**이라 기존 시나리오 거동이 안 바뀐다.
    //   ⚠ `resetWorld()` 가 이것도 되돌려야 한다 — 안 그러면 **한 시나리오가 다음을 오염시킨다**.
    bool chooserOn = true;
}
int ParkingServer::chooserCount() const { return H::choosers; }
// 🔴 **하니스도 이 스위치를 안다** — `--no-chooser`(사용자 지시 2026-08-27).
//   ⚠ 기본은 **켜짐**이라 기존 시나리오 거동이 안 바뀐다.
//   ★ 끄는 시나리오는 `H::chooserOn = false` 로 만든다 — 그래야 **그 갈래가 검사된다**.
bool ParkingServer::chooserEnabled() const { return H::chooserOn; }
void ParkingServer::onSlotPick(SlotPickFn) {}
// 🔑 **시험이 쥔다.** 이게 고정 `true` 면 [가] 빈자리없음 갈래를 **영원히 못 밟는다**.
namespace H { std::map<std::string,bool> avail; }
bool ParkingServer::parkingSpotAvailable(const std::string& id) const {
    std::map<std::string,bool>::const_iterator it = H::avail.find(id);
    return it == H::avail.end() ? true : it->second;
}
SensorReading ParkingServer::sensorReading(const std::string& devid,
                                           const std::string& moduleName) const {
    std::map<std::string, SensorReading>::const_iterator it =
        H::sensors.find(H::key(devid, moduleName));
    return it == H::sensors.end() ? SensorReading() : it->second;
}
long long ParkingServer::cameraShoot() {
    if (!H::camCanIssue) return -1;
    return ++H::shotSeq;
}
int ParkingServer::cameraState(long long) const { return H::camState; }
// 🔴 **취소·사유는 시험이 쥔다** — 설계 2026-08-27 의 판정이 이 둘에 걸려 있다.
//   ★ `cameraCancel` 을 **기록**해야 *"stale 을 표시만 하고 안 닫았다"* 를 검사할 수 있다.
//     (원장 [ㄱ] : 안 닫으면 엔진이 앞 요청번호를 재사용해 **앞차 번호가 뒤차에 붙는다**)
namespace H { std::vector<std::string> cancels; std::string camReason; }
bool ParkingServer::cameraCancel(long long id, const std::string& why) {
    H::cancels.push_back(std::to_string(id) + ":" + why);
    H::camState = CAM_NONE;              // 닫혔으니 더는 그 요청의 상태가 아니다
    return true;
}
std::string ParkingServer::cameraReason(long long) const { return H::camReason; }
std::string ParkingServer::cameraPlate(long long) const { return H::camPlate; }
void ParkingServer::entryStatus(const EntryStatus& e) { H::entry = e; H::lastPhase = e.phase; }
void ParkingServer::slotPlate(const std::string& spotId, const std::string& plate,
                              const std::string& source) {
    H::plates[spotId] = plate;
    H::plateSrc[spotId] = source;
}
void ParkingServer::onManualPlate(ManualPlateFn) {}
void ParkingServer::onCommandResult(CmdResultFn) {}
void ParkingServer::onOccupancyChanged(OccupancyFn) {}

// ── `ParkingLot` 껍데기 — `buildLot()` 이 링크되게만 ───────────────────────
struct LotStore {
    std::vector<std::string> ids;
    std::vector<std::pair<std::string, std::string> > mods;   // (devid, name)
    // 🔴 **자리 라벨을 잡아 둔다**(REQ-0519) — 소비자 넷이 이것으로 자리 번호를 만든다.
    //   ⚠ 전에는 `Spot::label()` 이 인자를 **버렸다.** 그래서 `A1`→"1번" 을 "5번" 으로
    //     뒤집었는데 하니스가 **143/0 그대로**였다 — **대상이 바뀌어도 초록이 나왔다.**
    std::map<std::string, std::string> labels;                // 자리 id → 라벨
};
LotStore g_store;
Spot ParkingLot::spot(const std::string& id) {
    g_store.ids.push_back(id);
    return Spot(this, g_store.ids.size() - 1);
}
Spot& Spot::at(int, int) { return *this; }
Spot& Spot::parking() { return *this; }
// 🔴 **라벨을 버리지 않는다**(2026-08-27 · REQ-0519).
//   ⚠ 전에는 `{ return *this; }` 로 통째로 버려서 **자리 번호를 아무도 검사하지 않았다** —
//     `A1`→"1번" 을 `A1`→"5번" 으로 뒤집었는데 하니스가 **143/0 그대로**였다.
//   ★ 그것이 §"동어반복 검사" 의 사촌이다 — **대상이 바뀌어도 초록이 나온다.**
//   🔑 소비자 넷(8080·8081·관제·`data_log`)이 전부 이 라벨로 자리 번호를 만든다.
//     **버리면 그 계약을 검사할 방법이 없다.**
Spot& Spot::label(const std::string& text) {
    // 🔑 `idx_` 는 `spot()` 이 넣은 순서 그대로다(위 `g_store.ids`).
    if (idx_ < g_store.ids.size()) g_store.labels[g_store.ids[idx_]] = text;
    return *this;
}
Spot& Spot::module(const std::string& devid, const std::string& name) {
    g_store.mods.push_back(std::make_pair(devid, name));
    return *this;
}
Spot& Spot::module(const std::string& name) { return module(std::string(), name); }

// 🔑 조작 선언은 **화면이 그릴 것**을 정한다 — 흐름 판정에는 안 쓰인다.
//   그래도 링크가 되어야 `buildLot()` 이 산다. 껍데기로 둔다.
namespace H { std::vector<std::string> controls; }
Control ParkingLot::control(const std::string& devid, const std::string& name) {
    H::controls.push_back(devid + "\t" + name);
    return Control(this, H::controls.size() - 1);
}
// 🔑 라벨 인자는 **화면 몫**이라 흐름 판정에 안 쓰인다 — 껍데기가 서명만 맞추면 된다(REQ-0475).
Control& Control::toggle(const std::string&, const std::string&) { return *this; }
Control& Control::number(long, long) { return *this; }
Control& Control::choice() { return *this; }
Control& Control::option(long, const std::string&) { return *this; }

// ── 시험 뼈대 ─────────────────────────────────────────────────────────────
int g_pass = 0, g_fail = 0;
void check(bool ok, const char* what) {
    if (ok) { g_pass++; return; }
    g_fail++;
    std::printf("  FAIL  %s\n", what);
}
void checkEq(const std::string& got, const std::string& want, const char* what) {
    if (got == want) { g_pass++; return; }
    g_fail++;
    std::printf("  FAIL  %s  (got \"%s\" want \"%s\")\n", what, got.c_str(), want.c_str());
}


ParkingLot g_lot;
ParkingServer g_srv(g_lot);

void tick(long long advanceMs) { H::now += advanceMs; H::clearSent(); onTick(g_srv); }

// 🔑 **모든 센서를 "안다 · 안 막혔다" 로 놓는다.** `known=false`(못 받았다)와 `value=false`(비었다)를
//   시험이 섞으면 흐름이 조용히 보류되고, 그것이 통과로 보인다.
// 🔴 **촬영을 계속 실패시켜 `rejected` 까지 몬다**(설계 2026-08-27).
//   ★ 전에는 실패 **한 번**이 곧 `rejected` 였다. 이제는 `SHOT_FAST_TRIES` 번 실패해야
//     손 입력 안내(=`rejected`)가 뜬다 — **그 사이에는 서버가 조용히 재시도한다.**
//   🔑 그래서 시험이 *"실패 한 번 → rejected"* 를 못 박고 있으면 **고쳐야 할 거동을 지킨다**
//     (§9.63 · 오늘 세 번째다). 도우미로 빼서 **상수가 바뀌어도 시험이 안 깨지게** 한다.
void failUntilRejected() {
    for (int i = 0; i < SHOT_FAST_TRIES + 2; i++) {
        H::camState = CAM_FAILED; tick(100);
        if (H::lastPhase == "rejected") return;
        H::camState = CAM_PENDING; tick(100);      // 새 요청이 나가고 다시 대기 상태가 된다
    }
}

void resetWorld() {
    H::sensors.clear(); H::sent.clear(); H::lastValue.clear(); H::plates.clear();
    H::plateSrc.clear(); H::logs.clear(); H::avail.clear();
    H::camState = CAM_PENDING; H::camPlate.clear(); H::allReady = true; H::camCanIssue = true;
    H::cancels.clear(); H::camReason.clear();
    // 🔴🔴 **여기 빠져 있어서 시나리오가 서로 샜다.** J·I 가 `choosers = 1` 로 두고 끝나
    //   그 뒤에 끼운 시나리오가 **자동배정 대신 `choosing`** 으로 갔다 —
    //   실제로 M 을 넣다가 그 빨간불을 봤다(세 번째 재발이다).
    //   🔑 고칠 곳은 **시나리오마다 한 줄이 아니라 이 리셋 하나**다.
    //     시나리오에 적으면 다음에 끼우는 사람이 또 빠뜨린다.
    //   ★ 판별자 : `H::` 에 새 상태를 더하면 **이 함수에 그 줄이 있나**를 세라.
    H::choosers = 0;
    H::chooserOn = true;      // 🔵 기본은 **켜짐**. 끄는 시나리오만 스스로 끈다
    H::setSensor("P1","A1",true,false); H::setSensor("P1","A2",true,false);
    H::setSensor("P1","A3",true,false); H::setSensor("P2","A4",true,false);
    H::setSensor("P2","A5",true,false);
    H::setSensor("P1","EF",true,false); H::setSensor("P1","ER",true,false);
    H::setSensor("P2","XF",true,false); H::setSensor("P2","XR",true,false);
    onControllerReset(g_srv);
    tick(1);
    H::plates.clear(); H::plateSrc.clear(); H::logs.clear();
}

void say(const char* s) { std::printf("%s\n", s); }
void dumpLogs(const char* title) {
    std::printf("\n── %s ─────────────────────────────\n", title);
    for (size_t i = 0; i < H::logs.size(); i++) std::printf("   %s\n", H::logs[i].c_str());
}

// ═══════════════════════════════════════════════════════════════════════════
int main() {
    // ══ 시나리오 A — 🔴 **입차 한 바퀴** (수용 조건 [2]) ═══════════════════
    // ══ 시나리오 A0 — 🔴 **자리 배치가 확정된 그대로인가** (사용자 확정 · 2026-08-27) ══════
    //   사용자 원문 : *"**자리 배치는 확정되었다 수정하지마라.**"*
    //   ★ 이 검사는 **바꾸라는 것이 아니라 안 바뀌게 지키는 것**이다 — `A1`→"1번" … `A5`→"5번".
    //   ⚠ 한 번 뒤집었다가 사용자가 정정해서 되돌렸다(2026-08-27). **그 실수를 이 검사가 막는다.**
    //   🔵 화면의 **출력 순서**(5,4,3,2,1)는 **web 몫**이다 — 서버는 배치를 안 바꾼다.
    //   🔑 화면 넷이 이 라벨로 자리 번호를 만든다(`lotNumber()`) — **여기가 갈리면 넷이 같이 갈린다.**
    //   ★ 그리고 이 검사를 넣기 전에는 `Spot::label()` 이 인자를 **버려서**
    //     라벨을 뒤집어도 하니스가 **143/0 그대로**였다 — **대상이 바뀌어도 초록이 나왔다.**
    say("\n═══ A0 · 자리 배치 — 확정된 A1=\"1번\" … A5=\"5번\" 을 지킨다 ═══");
    {
        // 🔴 **하니스는 `buildLot()` 을 안 부른다** — 껍데기로 링크만 시켜 뒀다(위 주석).
        //   ★ 그래서 여기서 **직접 부른다.** 안 부르면 라벨이 빈 채로 통과해
        //     *"검사가 있는데 아무것도 안 본다"* 가 된다(실제로 처음에 그랬다 — 빈 문자열 5개).
        ParkingLot lot;
        buildLot(lot);
        const char* want[5] = { "1번 자리", "2번 자리", "3번 자리", "4번 자리", "5번 자리" };
        const char* ids [5] = { "A1", "A2", "A3", "A4", "A5" };
        const char* what[5] = { "🔴 A1 의 라벨", "🔴 A2 의 라벨", "🔴 A3 의 라벨",
                                "🔴 A4 의 라벨", "🔴 A5 의 라벨" };
        for (int i = 0; i < 5; i++)
            checkEq(g_store.labels[ids[i]], std::string(want[i]), what[i]);
        // 🔵 **배선은 안 바뀌었다** — 그것도 같이 못 박는다(라벨만 뒤집는 것이 이 변경의 전부다).
        check(g_store.labels.size() >= 5, "라벨이 다섯 자리 모두에 있다");
    }
    dumpLogs("A0 · 자리 번호");

    say("\n═══ A · 입차 한 바퀴 — EF→촬영→배정→안내등→LCD→게이트→ER→닫힘→주차 ═══");
    resetWorld();
    H::setSensor("P1","EF",true,true);  tick(100);          // ① 입구 앞 감지
    checkEq(H::entry.phase, "shooting", "① EF 감지 → shooting");
    H::camState = CAM_READY; H::camPlate = "238다5927"; tick(100);
    checkEq(H::entry.phase, "assigning", "번호 도착 → assigning");
    tick(100);                                              // ⑤⑥ 배정
    checkEq(H::entry.phase, "assigned", "배정 완료 → assigned");
    checkEq(H::entry.slot, "A1", "🔴 빈 자리 중 A1 부터");
    check(H::valueOf("R1") == 1, "⑤ 안내등 R1 ON");
    check(H::valueOf("C1") == 2385927, "⑥ LCD 에 번호(하위 7자리)");
    // 🔴 **순서가 계약이다** — 안내가 켜지기 전에 차단봉이 열리면 안 된다
    // 🔴 **`sentAny` 로 물으면 안 된다** — 제어기 초기화가 이미 `ED=0` 을 냈다.
    //   물어야 할 것은 *"명령을 낸 적 있나"* 가 아니라 **"지금 닫혀 있나"** 다.
    check(H::valueOf("ED") == 0, "🔴 이 시점에 차단봉은 아직 닫혀 있다 (③은 ⑤⑥ 뒤다)");
    tick(100);                                              // ③ 차단봉
    check(H::valueOf("ED") == 1, "③ 입구 차단봉 열림(전선 값 1)");
    checkEq(H::entry.phase, "gate_open", "→ gate_open");
    H::setSensor("P1","ER",true,true);  tick(100);          // 통과 시작
    checkEq(H::entry.phase, "passing", "ER 감지 → passing");
    H::setSensor("P1","ER",true,false); tick(100);          // ④ 사라짐
    tick(100);
    check(H::valueOf("ED") == 0, "④ 입구 차단봉 닫힘");
    checkEq(H::entry.phase, "wait_park", "→ wait_park");
    dumpLogs("A · 입차 흐름 로그 (실제 출력)");

    // ⑦⑨ 정상 주차 — 배정된 자리에 점유
    H::logs.clear();
    onOccupancy(g_srv, "A1", "A1", true, ModuleMeasure());
    check(H::valueOf("R1") == 0, "⑦ 주차 확인 → 안내등 OFF");
    check(H::valueOf("C1") == 0, "⑦ 주차 확인 → LCD welcome");
    checkEq(H::plates["A1"], "238다5927", "⑨ 번호판 저장(정상 주차만)");
    checkEq(H::plateSrc["A1"], "camera", "⑨ 출처 camera");
    checkEq(H::entry.phase, "wait_park", "(아직 publish 전)");
    tick(1);
    checkEq(H::entry.phase, "idle", "한 바퀴 끝 → idle");
    dumpLogs("A · 주차 판정 로그");

    // ══ 시나리오 B — 🔴 **빨간불: 빈자리 없음이면 차단봉이 안 열린다** [3] ══
    say("\n═══ B · 🔴 빈자리 없음(FULL) — 차단봉이 안 열려야 한다 ═══");
    resetWorld();
    const char* all[] = {"A1","A2","A3","A4","A5"};
    for (int i = 0; i < 5; i++) H::avail[all[i]] = false;    // 다 찼다
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "111가1111"; tick(100);
    tick(100);
    checkEq(H::entry.phase, "full", "🔴 빈자리 없음 → full");
    check(H::valueOf("ED") == 0, "🔴🔴 **차단봉이 닫힌 채다** — 이것이 이 시나리오의 전부다");
    // 🔵 **거동으로 잰다** — 문구가 바뀌어도 안 깨지고, 전선 값이 곧 사실이다.
    check(H::valueOf("ED") != 1, "🔴 입구 차단봉이 **안 열렸다**(전선 값으로 확인)");
    check(H::entry.slot.empty(), "배정된 자리가 없다");
    tick(5000);
    check(H::valueOf("ED") == 0, "🔴 5초를 더 돌려도 여전히 닫힌 채다");
    dumpLogs("B · FULL 로그");

    // ══ 시나리오 C — ⑧ 비정상 주차 [4][5] ══════════════════════════════════
    say("\n═══ C · ⑧ 배정 안 된 자리에 주차 — NonHuman · 번호판 저장 안 함 ═══");
    resetWorld();
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "777나7777"; tick(100);
    tick(100); tick(100);
    checkEq(H::entry.slot, "A1", "A1 에 배정됐다");
    H::plates.clear(); H::plateSrc.clear(); H::logs.clear();
    onOccupancy(g_srv, "A3", "A3", true, ModuleMeasure());   // 엉뚱한 자리
    check(H::valueOf("C1") == 1, "⑧ 그 자리 LCD ← 1(NonHuman)");
    check(H::plates.find("A3") == H::plates.end(), "🔴 ⑩ 번호판을 저장하지 않았다");
    check(H::valueOf("R1") == 1, "⚠ 배정된 자리의 안내등은 그대로 — 갈 곳을 계속 가리킨다");
    dumpLogs("C · 비정상 주차 로그");

    // ══ 시나리오 D — 출차 ⑪⑫⑬ ════════════════════════════════════════════
    say("\n═══ D · 출차 — ⑪ 자리 비움 · ⑫⑬ 출구 차단봉 ═══");
    resetWorld();
    H::logs.clear();
    onOccupancy(g_srv, "A2", "A2", false, ModuleMeasure());  // ⑪
    check(H::valueOf("R2") == 0, "⑪ 안내등 OFF");
    check(H::valueOf("C1") == 0, "⑪ LCD welcome");
    H::setSensor("P2","XF",true,true); tick(100);            // ⑫
    check(H::valueOf("XD") == 1, "⑫ 출구 차단봉 열림");
    H::setSensor("P2","XR",true,true); tick(100);
    H::setSensor("P2","XR",true,false); tick(100);           // ⑬
    check(H::valueOf("XD") == 0, "⑬ 출구 차단봉 닫힘");
    dumpLogs("D · 출차 로그");

    // ══ 시나리오 K — 🔴 **D3: 게이트가 열린 중에 배정 자리가 차면** ══════════
    say("\n═══ K · 🔴 차단봉이 열린 중에 A1 이 차면 — 남의 차다(⑧). ④ 를 건너뛰면 안 된다 ═══");
    resetWorld();
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "444아4444"; tick(100);
    tick(100); tick(100);
    checkEq(H::entry.phase, "gate_open", "(차단봉이 열렸다)");
    H::logs.clear();
    onOccupancy(g_srv, "A1", "A1", true, ModuleMeasure());     // 이 순간 A1 이 찬다
    // ═══ 🔴🔴 **이 검사가 결함을 정답으로 못 박고 있었다** (실기 2026-08-26 에 뒤집혔다) ═══
    //   ~~`check(logHas("배정 없이 점유"))`~~ · ~~`check(!logHas("정상 주차"))`~~
    //   ~~`checkEq(phase, "gate_open")`~~ — 셋 다 **옛 구현을 그대로 베낀 것**이다.
    //
    //   ★ K 가 정말 막으려던 것은 **"④ 를 건너뛰지 마라"** 였다(제목이 그렇게 말한다).
    //     그런데 그것을 **"⑧ 으로 보내라"** 라는 *기전*으로 적었다. 기전이 틀렸다 —
    //     A1 은 **이 차가 배정받은 자리**다. 남의 차가 아니다.
    //   🔴 실기 결과 : ⑦ 2건 vs ⑧ **4건.** 배정받은 차가 제 자리에 댔는데 NonHuman 이 뜨고
    //     **번호판이 안 남았다.** 이 검사가 그 결함을 지키고 있었다.
    //
    //   > 🔑 **검사가 "막으려는 실패"가 아니라 "그때의 구현"을 못 박으면, 그 검사가 결함을 지킨다.**
    //   ★ §"규약은 낱말이 아니라 막으려는 실패로 읽어라" 를 **하니스 자신에** 적용한 자리다.
    //
    //   ✅ 그래서 **K 의 관심사는 그대로 두고 기대만 바꾼다** — ④ 는 여전히 와야 한다.
    check(H::logHas("정상 주차"), "🔴 ⑦ 다 — A1 은 **이 차가 배정받은 자리**다");
    check(!H::logHas("배정 없이 점유"), "🔴 NonHuman 이 뜨면 안 된다");
    check(H::valueOf("ED") == 0,
          "🔴🔴 **④ 차단봉이 닫힌다** — K 의 원래 관심사. ⑦ 안에서 명시적으로 닫는다");
    tick(1);
    checkEq(H::entry.phase, "idle", "흐름이 끝났다");
    dumpLogs("K · 게이트 중 점유");

    // ══ 시나리오 L — 🔴 **D4: 통과가 없으면 닫고 배정을 푼다** ═══════════════
    say("\n═══ L · 🔴 30초 통과가 없으면 — 닫고 배정을 풀고 입구를 되돌린다 ═══");
    resetWorld();
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "666자6666"; tick(100);
    tick(100); tick(100);
    checkEq(H::entry.phase, "gate_open", "(차단봉이 열렸다)");
    H::logs.clear();
    tick(GATE_OPEN_MAX_MS + 1);                                 // ER 을 안 민다
    tick(1);
    check(H::valueOf("ED") == 0, "🔴 차단봉을 닫았다");
    check(H::valueOf("R1") == 0, "🔴 안내등을 껐다 — 배정을 풀었다");
    checkEq(H::entry.phase, "idle", "🔴🔴 **idle 로 돌아간다** — wait_park 로 가면 3분 봉쇄다");
    // ★ 입구가 실제로 살아났나 — 다음 차가 곧바로 되는지가 진짜 판정이다
    H::logs.clear();
    H::setSensor("P1","EF",true,false); tick(100);
    H::setSensor("P1","EF",true,true);  tick(100);
    // 🔵 거동으로 : **새 요청이 실제로 발급됐나**(시도 수가 곧 사실이다)
    check(H::entry.shot_tries >= 1, "🔴🔴 **다음 차의 촬영 요청이 즉시 나간다** — 입구가 안 막혔다");
    dumpLogs("L · 통과 없음");

    // ══ 시나리오 E — 🔴 **하니스가 실패도 한다는 증거**(대조군) ═════════════
    say("\n═══ E · 🔴 대조군 — 이 검사가 정말 도는가 ═══");
    {
        const int before = g_fail;
        check(H::valueOf("ED") == 12345, "(일부러 틀린 검사 — 위에 FAIL 한 줄이 떠야 정상)");
        const bool caught = (g_fail == before + 1);
        g_fail = before;                                     // 대조군은 점수에서 뺀다
        std::printf("   대조군이 %s — %s\n", caught ? "잡혔다" : "🔴 안 잡혔다",
                    caught ? "검사가 실제로 돈다" : "🔴 이 하니스는 아무것도 안 본다");
        check(caught, "대조군이 FAIL 을 냈다");
    }

    // ══ 시나리오 F — 🔴🔴 **불변식: 선택 화면이 없으면 기존 그대로** (루트 요구) ══
    say("\n═══ F · 🔴 8081 이 없어도(=붙은 기기 0) 흐름은 기존대로 A1 자동배정 ═══");
    resetWorld();
    H::choosers = 0;                              // 🔴 아무도 안 붙었다
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "555다5555"; tick(100);
    tick(100);
    checkEq(H::entry.phase, "assigned", "🔴 choosing 을 안 거친다");
    checkEq(H::entry.slot, "A1", "🔴🔴 **기존대로 A1 자동배정** — 이것이 이 기능의 불변식이다");
    check(H::valueOf("R1") == 1, "안내등도 그대로 켜진다");
    check(!H::logHas("사람이 자리를 골랐다"), "🔴 선택 경로를 안 탔다");
    dumpLogs("F · 선택 화면 0대");

    // ══ 시나리오 G-off — 🔴 **선택 서비스를 끄면 차단봉과 안내등이 같은 초에** ══════
    //   사용자 지시 2026-08-27 : *"8081 자리 선택 서비스를 **일시 중지**하여, 자리배정 및
    //   안내등이 **입구 차단봉 개폐와 연동되는 구조가 보이도록** 하라."*
    //   ★ 이 시나리오가 **그 요구를 그대로 검사한다** — 선택 화면이 붙어 있는데도
    //     `choosing` 으로 안 가고 **차단봉·안내등·LCD 가 한 박자**에 나야 한다.
    //   🔑 **대조군은 바로 아래 G** 다(같은 조건 · 스위치만 켜짐 → `choosing`).
    //     ★ 둘이 같이 있어야 *"스위치가 실제로 갈랐다"* 가 증명된다.
    say("\n═══ G-off · 선택 서비스 중지 — 차단봉과 안내등이 같은 박자 ═══");
    resetWorld();
    H::choosers  = 1;          // 🔵 **붙어 있다**(G 와 같다)
    H::chooserOn = false;      // 🔴 **그런데 껐다** — 이것만 다르다
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "999마9999"; tick(100);
    tick(100);
    checkEq(H::entry.phase, "assigned", "🔴 **choosing 이 아니라 assigned** — 선택을 건너뛰었다");
    check(!H::entry.slot.empty(), "🔴 그 자리에서 **자리가 정해졌다**");
    check(H::valueOf("R1") == 1, "🔴 **안내등이 먼저 켜졌다**");
    // 🔵 **차단봉은 그 다음 박자다. 설계가 그렇게 정했다** —
    //   `lot.cpp` 주석 : *"차단봉이 먼저 열리면 운전자가 **어디로 갈지 모르는 채로** 들어온다"*
    //   ★ 그러니 사용자가 보려는 것은 *"같은 순간"* 이 아니라 **"안내등 → 차단봉 이 붙어 있는 것"** 이다.
    //   ⚠ 선택 갈래는 그 사이가 **5초**였다(11:17:52 → 11:17:57 실측). 이제 **한 박자**다.
    check(H::valueOf("ED") == 0, "이 박자에는 차단봉이 아직 안 열렸다(안내가 먼저다)");
    tick(1);
    check(H::valueOf("ED") == 1, "🔴 **바로 다음 박자에 차단봉이 열린다** — 5초가 아니다");
    check(H::logHas("선택 서비스 **중지 중**"), "🔴 끈 것이 **로그에 보인다**");
    check(!H::logHas("사람이 자리를 고른다"), "선택 경로를 안 탔다");
    dumpLogs("G-off · 선택 중지");

    // ══ 시나리오 G — 8081 이 붙어 있으면 **순서가 갈린다** ══════════════════
    say("\n═══ G · 8081 이 붙어 있다 — 차단봉 먼저 열고 사람이 고른다 ═══");
    resetWorld();
    H::choosers = 1;
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "888라8888"; tick(100);
    tick(100);
    checkEq(H::entry.phase, "choosing", "🔴 assigned 가 아니라 choosing 이다");
    check(H::valueOf("ED") == 1, "🔴 차단봉이 **먼저** 열렸다 (선택 갈래)");
    check(H::entry.slot.empty(), "아직 배정된 자리가 없다");
    check(!H::sentAny("R1") || H::valueOf("R1") == 0, "안내등도 아직 안 켜졌다");
    // 사람이 A3 를 고른다
    ParkingServer::ManualPlateResult ok = onSlotPick(g_srv, "A3");
    checkEq(ok.code, "", "선택 수락");
    tick(1);
    checkEq(H::entry.slot, "A3", "🔴 **사람이 고른 자리**로 배정됐다");
    check(H::valueOf("R3") == 1, "⑤ A3 안내등 ON");
    check(H::valueOf("C1") == 8888888, "⑥ LCD 에 번호");
    checkEq(H::entry.phase, "gate_open", "차단봉은 이미 열려 있다 — 다시 안 연다");
    dumpLogs("G · 사람이 고른 판");

    // ══ 시나리오 H — 거절 셋 ═══════════════════════════════════════════════
    say("\n═══ H · 거절 어휘 — 기존 것만 쓴다 ═══");
    checkEq(onSlotPick(g_srv, "A2").code, "not_ready", "고를 때가 아니면 not_ready");
    resetWorld(); H::choosers = 1;
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "999마9999"; tick(100); tick(100);
    checkEq(H::entry.phase, "choosing", "(고르는 중)");
    checkEq(onSlotPick(g_srv, "Z9").code, "bad_request", "없는 자리면 bad_request");
    H::avail["A2"] = false;                        // 🔴 [다] 그 사이 찼다
    checkEq(onSlotPick(g_srv, "A2").code, "not_reservable", "🔴 그 사이 찼으면 not_reservable");
    checkEq(H::entry.phase, "choosing", "🔑 거절해도 **여전히 고르는 중** — 다시 고르면 된다");

    // ══ 시나리오 J — 🔴 차가 다 들어가면 **선택 창이 닫힌다**(명세 종료 조건) ══
    say("\n═══ J · EF 가 사라지면 선택 창을 닫는다 ═══");
    resetWorld(); H::choosers = 1;
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "222바2222"; tick(100); tick(100);
    checkEq(H::entry.phase, "choosing", "(고르는 중)");
    H::setSensor("P1","EF",true,false); tick(100);          // 차가 다 들어갔다
    tick(1);
    check(H::entry.phase != "choosing", "🔴 선택 창이 닫혔다");
    checkEq(H::entry.slot, "A1", "🔑 창은 닫히되 **자리는 준다** — 차가 이미 안에 있다");
    check(H::logHas("선택 창을 닫고"), "닫힌 이유가 로그에 남는다");
    dumpLogs("J · 선택 창 종료");

    // ══ 시나리오 I — [가] 안 고르면 기본 자동배정으로 폴백 ══════════════════
    say("\n═══ I · 15초 안에 안 고르면 자동배정으로 돌아간다 ═══");
    // 🔴 **자기 세계를 스스로 세운다.** 앞 시나리오가 남긴 상태에 기대면
    //   시나리오를 하나 끼우는 것만으로 깨진다 — 방금 J 를 넣다가 실제로 깨졌다.
    resetWorld(); H::choosers = 1;
    H::setSensor("P1","EF",true,true); tick(100);
    H::camState = CAM_READY; H::camPlate = "333사3333"; tick(100); tick(100);
    checkEq(H::entry.phase, "choosing", "(고르는 중)");
    tick(SELECT_WAIT_MS + 1);
    tick(1);
    checkEq(H::entry.phase, "assigned", "🔴 시한 뒤 자동배정");
    checkEq(H::entry.slot, "A1", "A1 부터");
    // 🔴 **무한루프가 아닌 것**을 확인한다 — 다시 choosing 으로 안 돌아간다
    tick(SELECT_WAIT_MS + 1);
    check(H::entry.phase != "choosing", "🔴🔴 다시 choosing 으로 안 돌아간다(한 번만 묻는다)");
    dumpLogs("I · 시한 폴백");

    // ══ 시나리오 M — 🔴 **번호가 들어오는 문**(원장 §9.60) ══════════════════
    //   여기까지 이 문은 **검사가 하나도 없었다** — 하니스가 `onManualPlate` 를
    //   한 번도 안 불렀다. 68 통과가 이 갈래에 대해서는 `0/0` 이었다.
    say("\n═══ M · 번호가 들어오는 문 — 배정자는 하나 · 출처를 지어내지 않는다 ═══");
    resetWorld();
    {
        // 🔴 흐름이 번호를 기다릴 때가 **아니면** 받지 않는다.
        //   아무 때나 받으면 배정이 흐름 밖에서 생긴다 — 그것이 §9.60 의 결함이었다.
        ParkingServer::ManualPlateResult r0 = onManualPlate(g_srv, "111가1111", "manual");
        checkEq(r0.code, "not_ready", "🔴 idle 에서는 거절한다");
    }
    H::setSensor("P1", "EF", true, true); tick(100);
    checkEq(H::entry.phase, "shooting", "① EF 감지 → shooting");
    failUntilRejected();
    checkEq(H::entry.phase, "rejected", "🔴 연속 실패 → rejected (화면 입력칸이 열리는 유일한 값)");
    {
        // 🔴 **빈 번호를 받지 않는다.** `slotPlate("")` 은 *"번호를 지운다"* 는 뜻이라
        //   그대로 흘리면 자리의 번호판이 조용히 사라진다.
        ParkingServer::ManualPlateResult re = onManualPlate(g_srv, "", "manual");
        checkEq(re.code, "bad_request", "🔴 빈 번호를 거절한다");
        checkEq(H::entry.phase, "rejected", "거절해도 흐름은 그 자리에 남는다(사람이 다시 친다)");
    }
    {
        // ✅ 요청번호 없는 폰 push 가 이 문으로 들어온다 — **출처는 camera** 다.
        ParkingServer::ManualPlateResult rc = onManualPlate(g_srv, "427나6153", "camera");
        checkEq(rc.code, "", "받아들인다");
    }
    tick(1);
    checkEq(H::entry.plate, "427나6153", "번호가 흐름에 들어갔다");
    checkEq(H::entry.plate_source, "camera",
            "🔴🔴 출처가 camera 다 — manual 로 적으면 data_log 가 거짓이 된다");
    tick(1);
    checkEq(H::entry.slot, "A1", "🔑 배정은 **흐름이** 한다 — 배정자는 하나다");
    check(H::logHas("폰이 보낸 번호"), "로그가 두 출처를 갈라 적는다");
    dumpLogs("M · 번호가 들어오는 문");

    // ══ 시나리오 M2 — 사람이 친 번호는 `manual` 로 남는다(대조군) ═══════════
    //   🔑 **대조군이 없으면 위 검사는 "무엇을 넣어도 camera" 와 구별이 안 된다.**
    say("\n═══ M2 · 대조군 — 사람이 친 번호는 manual 이다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    failUntilRejected();
    checkEq(H::entry.phase, "rejected", "(번호 대기)");
    onManualPlate(g_srv, "111가1111", "manual");
    tick(1); tick(1);
    checkEq(H::entry.plate_source, "manual", "🔴 이쪽은 manual 이다");
    check(H::logHas("손으로 입력한 번호"), "로그도 갈라 적는다");
    dumpLogs("M2 · 대조군");

    // ══ 시나리오 N — 🔴 **ER 이 안 풀렸는데 자리가 먼저 찬다**(실기 2026-08-26) ═══
    //   실기에서 ⑦ 2건 vs ⑧ 4건이 나왔다. 네 건 전부 **배정받은 차가 배정받은 자리**에
    //   댄 것인데 NonHuman 으로 판정돼 **번호판이 안 남았다.**
    //   🔑 이 하니스가 그때까지 그것을 못 잡은 이유: **모든 시나리오가 ER 을 얌전히
    //     올렸다 내렸다.** 실물은 그 순서를 지켜 주지 않는다.
    say("\n═══ N · 통과가 끝나기 전에 주차한다 — 그래도 정상 주차다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    H::camState = CAM_READY; H::camPlate = "238다5927"; tick(100); tick(100); tick(100);
    checkEq(H::entry.phase, "gate_open", "③ 차단봉 열림");
    H::setSensor("P1", "ER", true, true); tick(100);          // 차가 게이트에 들어섰다
    checkEq(H::entry.phase, "passing", "통과 중");
    // 🔴 **ER 을 안 내린다.** 차 뒤끝이 아직 게이트에 있는데 앞은 이미 자리에 닿았다
    //   🔑 하니스는 점유 콜백을 **자동으로 안 부른다**(껍데기가 no-op 이다) — K 처럼 직접 부른다.
    H::logs.clear();
    onOccupancy(g_srv, "A1", "A1", true, ModuleMeasure());
    tick(1);   // 🔑 `publish()` 는 다음 박자에 돈다 — 안 밀면 앞 phase 를 읽는다
    checkEq(H::entry.phase, "idle", "🔴 흐름이 끝났다 — ⑦ 로 갔다(예전엔 ⑧ 이었다)");
    checkEq(H::plateSrc["A1"], "camera", "🔴🔴 **번호판이 기록됐다** — ⑧ 갈래면 안 남는다");
    checkEq(H::plates["A1"], "238다5927", "기록된 번호가 맞다");
    check(H::plates["A1"] == "238다5927", "⑦ 로 판정했다 — **번호가 기록됐다**(거동으로 잰다)");
    check(!H::logHas("NonHuman"), "🔴 NonHuman 경고가 **안** 떴다");
    // 🔴 **그리고 차단봉이 열린 채 남으면 안 된다** — 옛 주석이 걱정한 바로 그것이다
    check(H::valueOf("ED") == 0, "🔴🔴 입구 차단봉이 **닫혔다** — ④ 를 안 건너뛴다");
    dumpLogs("N · 통과 전 주차");

    // ══ 시나리오 N2 — 대조군: **배정 안 된 자리**는 여전히 ⑧ 이다 ═══════════
    //   🔑 위 검사만 있으면 *"무엇이든 ⑦"* 인 코드와 구별이 안 된다.
    say("\n═══ N2 · 대조군 — 남의 자리에 대면 여전히 NonHuman 이다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    H::camState = CAM_READY; H::camPlate = "111가1111"; tick(100); tick(100); tick(100);
    checkEq(H::entry.slot, "A1", "A1 을 배정받았다");
    H::logs.clear();
    onOccupancy(g_srv, "A3", "A3", true, ModuleMeasure());   // 🔴 엉뚱한 자리에 댔다
    check(H::logHas("NonHuman"), "🔴 배정 안 된 자리는 여전히 ⑧ 이다");
    checkEq(H::plates["A3"], "", "🔴 그 자리에는 번호판을 저장하지 않는다");
    check(H::entry.slot == "A1", "🔑 배정은 그대로 둔다 — 원래 갈 곳을 계속 가리킨다");
    dumpLogs("N2 · 대조군");

    // ══ ~~시나리오 P~~ — 🔴 **지웠다. 그리고 그 이유가 이 하니스의 교훈이다** ═══════
    //   P 는 *"WAIT_PARK 중 EF 가 올라도 거동은 그대로다"* 를 검사했다:
    //       ~~check(!logHas("촬영 요청"), "거동은 그대로")~~
    //       ~~checkEq(entry.phase, "wait_park", "phase 도 안 바뀐다")~~
    //   🔴 그런데 **[D] 가 바로 그 거동을 바꿨다**(사용자 확정 "가" — 뒤차를 받는다).
    //     그래서 P 는 **고쳐야 할 거동을 정답으로 못 박고 있었다.**
    //
    //   > ★ **§9.63 이 하루에 두 번 났다.** 처음은 시나리오 K(내가 물려받은 것),
    //   >   이번은 **내가 몇 시간 전에 직접 쓴 P** 다.
    //   🔑 P 를 쓸 때는 그것이 옳았다 — *"거동을 안 바꾸는 변경"* 이었으니 거동을 못 박은 것이 맞다.
    //     **틀린 것은 그 검사가 아니라, 거동이 바뀔 때 그 검사를 같이 안 볼 위험**이다.
    //   ✅ 그래서 지우되 **여기 흔적을 남긴다.** 다음 사람이 *"P 는 어디 갔나"* 를 안 쫓게.
    //
    //   P 의 관심사는 둘로 갈려 살아 있다:
    //     · *"막을 때는 왜 막는지 말한다"*  →  **Q3**(gate_open 대조군)
    //     · *"idle 에서는 그 줄이 안 뜬다"*  →  **P2**(아래, 그대로 둔다)

    // ══ 시나리오 P2 — 대조군: **idle 에서는 그 경고가 안 뜬다** ═════════════
    //   🔑 없으면 *"무슨 일이 있어도 그 줄이 뜨는"* 코드와 구별이 안 된다.
    say("\n═══ P2 · 대조군 — idle 에서는 그 경고가 없다 ═══");
    resetWorld();
    H::logs.clear();
    H::setSensor("P1", "EF", true, true); tick(100);
    check(!H::logHas("새 입차를 시작하지 않는다"), "🔴 idle 에서는 **안** 뜬다");
    check(H::logHas("입구 앞 감지"), "정상 시작은 그대로 뜬다");
    dumpLogs("P2 · 대조군");

    // ══ 시나리오 Q — 🔴🔴 **[D] 뒤차가 오면 앞차 안내를 버린다** (사용자 확정 "가") ═══
    //   이것이 오늘 사용자가 **3분 막힌** 그 자리다(원장 §9.65).
    //   ★ 옛 코드에서는 이 시나리오가 *"EF 가 올라도 아무 일도 안 일어난다"* 로 통과했다 —
    //     검사가 없어서 통과한 것이지 동작해서가 아니었다.
    say("\n═══ Q · WAIT_PARK 중에 뒤차가 오면 앞차를 버리고 받는다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    H::camState = CAM_READY; H::camPlate = "111가1111"; tick(100); tick(100); tick(100);
    H::setSensor("P1", "ER", true, true);  tick(100);
    H::setSensor("P1", "ER", true, false); tick(100); tick(100);
    checkEq(H::entry.phase, "wait_park", "앞차: ④ 뒤 주차 대기");
    checkEq(H::entry.slot, "A1", "앞차는 A1 을 안내받았다");
    check(H::valueOf("R1") == 1, "앞차 안내등 켜져 있다");
    // 🔴 앞차가 자리에 안 간 채 **뒤차가 온다**
    H::setSensor("P1", "EF", true, false); tick(100);
    H::logs.clear();
    H::camState = CAM_PENDING; H::camPlate.clear();
    H::setSensor("P1", "EF", true, true);  tick(100);
    check(H::logHas("앞 입차 건을 버린다"), "🔴🔴 **버린다고 말한다**");
    check(H::logHas("111가1111"), "🔑 **버린 번호를 같이 찍는다** — 나중의 유일한 단서다");
    check(H::valueOf("R1") == 0, "🔴 앞차 안내등이 꺼졌다(정리가 완전하다)");
    check(H::valueOf("C1") == 0, "🔴 앞차 자리 LCD 가 welcome 으로 돌아갔다");
    // 🔴🔴 **그리고 같은 박자에 뒤차가 시작돼야 한다** — 미루면 다음 상승까지 또 기다린다
    check(H::logHas("입구 앞 감지"), "🔴🔴 **같은 박자에 뒤차 입차가 시작된다**");
    checkEq(H::entry.phase, "shooting", "뒤차: 촬영 중");
    dumpLogs("Q · 앞차를 버리고 뒤차를 받는다");

    // ══ 시나리오 Q2 — 🔴 **[가] 의 대가를 센다** ═════════════════════════════
    //   버린 앞차가 결국 그 자리에 대면 ⑧ NonHuman 이 된다. **그 수를 세는 것**이 루트 지시다.
    say("\n═══ Q2 · 버린 앞차가 결국 그 자리에 대면 — 대가를 센다 ═══");
    H::logs.clear();
    onOccupancy(g_srv, "A1", "A1", true, ModuleMeasure());
    check(H::logHas("버린 앞차가 결국 여기 주차했다"), "🔴 대가를 **말한다**");
    check(H::logHas("111가1111"), "🔑 번호를 찍는다 — 사람이 손으로 이어 붙일 수 있다");
    check(H::logHas("배정 없이 점유"), "거동은 ⑧ 그대로다(숨기지 않는다)");
    checkEq(H::plates["A1"], "", "🔴 그래서 **번호판이 기록되지 않는다** — 그게 대가다");
    dumpLogs("Q2 · 대가");

    // ══ 시나리오 Q3 — 대조군: **gate_open 의 EF 는 같은 차다. 버리면 안 된다** ══
    //   🔑 실기 18:53:40 에 정확히 이 줄이 찍혔다 — 여기서 버리면 **도는 입차를 깬다**
    say("\n═══ Q3 · 대조군 — gate_open 중의 EF 는 같은 차다. 안 버린다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    H::camState = CAM_READY; H::camPlate = "222나2222"; tick(100); tick(100); tick(100);
    checkEq(H::entry.phase, "gate_open", "(차단봉이 열렸다)");
    H::setSensor("P1", "EF", true, false); tick(100);
    H::logs.clear();
    H::setSensor("P1", "EF", true, true);  tick(100);
    check(!H::logHas("앞 입차 건을 버린다"), "🔴🔴 **안 버린다** — 같은 차다");
    check(H::logHas("새 입차를 시작하지 않는다"), "대신 왜 안 되는지 말한다");
    checkEq(H::entry.slot, "A1", "앞차 배정이 살아 있다");
    check(H::valueOf("R1") == 1, "안내등도 켜진 채다");
    dumpLogs("Q3 · 대조군");

    // ══ 시나리오 R — 🔴 **4초 규칙 : `unknown` 은 시계를 멈춘다** (설계 [A]) ═══════
    //   ★ 이 구현의 **가장 위험한 자리**다. 링크가 3.5초 끊기면 센서가 `known=false` 인데
    //     그것을 "차 없음" 으로 읽으면 **링크가 흔들릴 때마다 차가 사라진 것으로 판정**한다.
    say("\n═══ R · unknown 은 4초 시계를 멈춘다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    checkEq(H::entry.phase, "shooting", "촬영 시작");
    H::setSensor("P1", "EF", false, false);            // 🔴 링크가 끊겼다 — 모름
    tick(3000); tick(3000); tick(3000);                // 9초를 흘린다
    checkEq(H::entry.phase, "shooting", "🔴🔴 **모름이 9초 흘러도 접지 않는다**");
    check(!H::logHas("차가 갔다고 본다"), "4초 규칙이 **발동하지 않았다**");
    // 🔵 대조군 : **진짜로 비었다고 말하면** 4초에 접는다
    H::setSensor("P1", "EF", true, false);
    tick(100);                                   // 🔑 이 박자에 "비었다" 를 **처음 관측**한다
    tick(2000);
    checkEq(H::entry.phase, "shooting", "2초로는 아직 안 접는다");
    tick(2500);
    check(H::logHas("차가 갔다고 본다"), "🔵 **4초를 넘기면 접는다**(대조군)");
    checkEq(H::entry.phase, "idle", "흐름이 idle 로 돌아갔다");
    dumpLogs("R · unknown 은 시계를 멈춘다");

    // ══ 시나리오 S — 🔴 **[ㄱ] 접을 때 요청을 *닫는다*** (표시만 하면 안 된다) ══════
    //   ★ 안 닫으면 엔진이 `shot_recent_pending` 으로 **앞 요청번호를 재사용**해
    //     **앞차 번호가 뒤차에 붙는다**. 설계 [C] 가 막으려던 결함이 [C] 안에서 난다.
    say("\n═══ S · 접을 때 촬영 요청을 닫는다 — 표시만 하면 안 된다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    check(H::cancels.empty(), "아직 닫은 것이 없다");
    H::setSensor("P1", "EF", true, false);
    tick(100); tick(5000);                       // 🔑 관측 박자 + 4초 경과 박자
    check(!H::cancels.empty(), "🔴🔴 **요청을 닫았다**(표시만 한 것이 아니다)");
    check(H::cancels.size() && H::cancels[0].find("car_gone") != std::string::npos,
          "🔑 사유가 `car_gone` 이다 — 화면이 그대로 그린다");
    dumpLogs("S · 접을 때 닫는다");

    // ══ 시나리오 T — 🔵 **실패는 재시도한다. 3회 뒤에 손 입력 문이 열린다** ═════════
    say("\n═══ T · 실패 → 재시도 → 3회 뒤 rejected(손 입력 문) ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    H::camReason = "no_plate_found";
    H::camState = CAM_FAILED; tick(100);
    checkEq(H::entry.phase, "shooting", "🔵 **한 번 실패로는 안 접는다** — 바로 다시 요청한다");
    checkEq(H::entry.shot_last_error, "no_plate_found", "🔑 **폰 낱말을 그대로** 싣는다");
    check(H::entry.shot_tries >= 2, "🔵 **같은 박자에** 재요청이 나갔다(시도 수가 늘었다)");
    H::camState = CAM_PENDING; tick(100);
    failUntilRejected();
    checkEq(H::entry.phase, "rejected", "🔴 연속 실패 뒤 **손 입력 문이 열린다**");
    check(H::entry.shot_closed.empty(), "🔵 `shot_closed` 는 비어 있다 — **그만둔 것이 아니다**");
    dumpLogs("T · 재시도와 문");

    // ══ 시나리오 U — 🔴 **[E] 번호는 왔는데 차가 없으면 차단봉을 안 연다** ══════════
    say("\n═══ U · 번호가 와도 차가 없으면 진행하지 않는다 ═══");
    resetWorld();
    H::setSensor("P1", "EF", true, true); tick(100);
    H::setSensor("P1", "EF", false, false);            // 🔑 모름 — 4초 규칙이 안 접는다
    tick(100);
    H::camState = CAM_READY; H::camPlate = "777차7777"; tick(100);
    checkEq(H::entry.phase, "rejected", "🔴 배정으로 안 간다");
    checkEq(H::entry.slot, "", "🔴🔴 **자리를 안 준다**");
    check(H::valueOf("ED") != 1, "🔴🔴 **차단봉을 안 연다**");
    checkEq(H::entry.plate_discarded, "sensor_unknown",
            "🔑 **서버 판단**이 별도 칸에 실린다(폰 낱말과 안 섞인다)");
    check(H::entry.shot_last_error.empty(), "🔵 폰은 실패하지 않았다 — 그 칸은 비어 있다");
    check(H::logHas("비어 있던"), "로그가 **센서가 뭐라고 했는지**를 같이 남긴다");
    // ══ 🔴 U2 — **버린 뒤 다시 성공하면 그 자국이 남으면 안 된다** ═══════════════
    //   ★ web 이 *"둘 다 오면 둘 다 적는다"*(가정을 안 넣는다)로 짰다. 그건 옳다.
    //     🔑 **그래서 서버가 안 지우면 화면이 모순을 그대로 그린다** —
    //       *"자리를 배정했습니다"* + *"차량이 확인되지 않아 진행하지 않았습니다"* 를 동시에.
    //   ⚠ 소비자가 관대하면 **생산자의 실수가 안 드러나는 게 아니라 그대로 보인다.**
    H::setSensor("P1", "EF", true, true);              // 차가 돌아왔다(또는 센서가 회복했다)
    H::camState = CAM_PENDING; tick(100);              // 새 요청이 나간다
    checkEq(H::entry.plate_discarded, "",
            "🔴🔴 **새 시도가 시작되면 앞의 '버렸다' 자국이 지워진다**");
    H::camState = CAM_READY; H::camPlate = "777차7777"; tick(100); tick(100);
    checkEq(H::entry.plate_discarded, "", "성공 뒤에도 남아 있지 않다");
    checkEq(H::entry.slot, "A1", "🔵 이번엔 배정까지 간다");
    dumpLogs("U · 차가 없으면 안 연다");

    std::printf("\n════ 통과 %d · 실패 %d ════\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
