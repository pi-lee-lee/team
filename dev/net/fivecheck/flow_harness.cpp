// flow_harness.cpp — `five` 의 **입·출차 흐름을 실제로 굴린다.**
//
// ═══════════════════════════════════════════════════════════════════════════
// 🔴 **무엇이 진짜이고 무엇이 가짜인가** — 이것이 이 하니스의 판정 근거다
//
//   진짜 : `five/lot.cpp` 전체. 흐름 함수·상태기계·시한·번호판 환산 — **한 줄도 안 고쳤다**
//   가짜 : `ParkingServer` 의 **껍데기만**. 센서 값·시계·명령 발행을 시험이 쥔다
//
// 🔑 하니스가 대상보다 관대하면 시험이 결함을 숨긴다 — 그래서 **로직을 흉내내지 않았다.**
//   시간도 진짜로 흐르게 하지 않고 **값으로 밀어 넣는다**(`fake_now`), 그래서 시한 경계를
//   1ms 단위로 정확히 밟을 수 있다.
//
// 재현:
//   c++ -std=c++11 -I../../조별과제샘플/VS_server_multi/five \
//       flow_harness.cpp ../../조별과제샘플/VS_server_multi/five/lot.cpp -o /tmp/fh && /tmp/fh
// ═══════════════════════════════════════════════════════════════════════════
#include "parking.h"

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
bool ParkingServer::parkingSpotAvailable(const std::string&) const { return true; }
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
};
LotStore g_store;
Spot ParkingLot::spot(const std::string& id) {
    g_store.ids.push_back(id);
    return Spot(this, g_store.ids.size() - 1);
}
Spot& Spot::at(int, int) { return *this; }
Spot& Spot::parking() { return *this; }
Spot& Spot::label(const std::string&) { return *this; }
Spot& Spot::module(const std::string& devid, const std::string& name) {
    g_store.mods.push_back(std::make_pair(devid, name));
    return *this;
}
Spot& Spot::module(const std::string& name) { return module(std::string(), name); }

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

void tick(long long advanceMs) {
    H::now += advanceMs;
    H::clearSent();
    onTick(g_srv);
}

// 🔑 **모든 센서를 "안다 · 안 막혔다" 로 놓는다.** 값을 못 받은 상태(`known=false`)와
//   비어 있는 상태를 시험이 섞으면 자리 배정이 조용히 보류된다.
void resetWorld() {
    H::sensors.clear(); H::sent.clear(); H::lastValue.clear(); H::plates.clear();
    H::plateSrc.clear(); H::logs.clear();
    H::camState = CAM_PENDING; H::camPlate.clear(); H::allReady = true;
    H::camCanIssue = true;
    const char* slots[] = {"A1", "A2", "A3", "A4", "A5"};
    for (int i = 0; i < 5; i++) H::setSensor("F1", slots[i], true, false);
    H::setSensor("F2", "E1", true, false);
    H::setSensor("F2", "E2", true, false);
    H::setSensor("F2", "X1", true, false);
    H::setSensor("F2", "X2", true, false);
    onControllerReset(g_srv);
    tick(1);
    tick(6000);      // 출차 확정(5초)이 한 번 돌아 자리 기록이 정리된 뒤에 시작한다
    tick(1);
    H::plates.clear(); H::plateSrc.clear();
}

// ═══ ① 정상 입차 — 카메라가 번호를 준다 ════════════════════════════════════
void t_entry_camera() {
    std::printf("① 정상 입차(카메라)\n");
    resetWorld();
    checkEq(H::entry.phase, "idle", "시작은 idle");

    H::setSensor("F2", "E1", true, true);           // 1번: 입구에 차가 섰다
    tick(10);
    checkEq(H::entry.phase, "shooting", "입구 감지 → shooting");
    // 🔑 장치 다섯 보드가 코드 2 를 `"shooting"` 으로 받는다(arduino 커밋 07ff9c8).
    //   ⚠ **굽지 않은 칩에는 없다** — 옛 펌웨어에서는 `000 0002` 가 뜬다.
    check(H::valueOf("C1") == 2, "촬영 중 코드 2 를 후보 자리 LCD 로 보낸다");

    H::camState = CAM_READY; H::camPlate = "123바9898";
    tick(10);
    checkEq(H::entry.phase, "assigned", "번호 수신 → assigned");
    checkEq(H::entry.slot, "A1", "빈 자리는 A1 부터");
    checkEq(H::entry.plate, "123바9898", "번호는 텍스트 그대로");
    checkEq(H::entry.plate_source, "camera", "출처 camera");
    // 🔴 전선 값은 **0/1** 이다. 각도(90)는 장치 안 상수다 —
    //   90 을 보내면 `cmdED` 가 `arg > 1` 로 **거절**한다(p3.ino 실측).
    check(H::valueOf("ED") == 1, "입구 차단봉 열림은 1 로 나간다(각도가 아니다)");
    check(H::valueOf("L1") == 1 && H::valueOf("L2") == 0, "A1 라인 안내등만 켠다");
    check(H::valueOf("C1") == 1239898, "LCD 에 7자리 숫자");

    H::setSensor("F2", "E2", true, true);           // 5-1번: 차가 지나갔다
    tick(10);
    check(H::valueOf("ED") == 0, "입구2 감지 → 차단봉이 닫힌다");

    H::setSensor("F2", "E1", true, false);
    H::setSensor("F2", "E2", true, false);
    H::setSensor("F1", "A1", true, true);           // 7번: 자리에 들어왔다
    tick(10);
    checkEq(H::entry.phase, "parking", "자리 감지 → parking");
    tick(9000);
    checkEq(H::entry.phase, "parking", "9초에는 아직 확정 안 한다");
    check(H::plates["A1"].empty(), "확정 전에는 기록하지 않는다");
    tick(1200);
    checkEq(H::entry.phase, "done", "10초 유지 → done");
    check(H::valueOf("C1") == 0, "welcome 코드 0");
    checkEq(H::plates["A1"], "123바9898", "data_log 에 번호가 텍스트 그대로");
    checkEq(H::plateSrc["A1"], "camera", "출처도 같이 기록");
    check(H::valueOf("L1") == 0, "안내등을 끈다");
    tick(3200);
    checkEq(H::entry.phase, "idle", "완료 표시 뒤 idle 로");
}

// ═══ ② 인식 실패 → 수동 입력 폴백 ══════════════════════════════════════════
void t_manual_fallback() {
    std::printf("② 인식 실패 → 수동 입력\n");
    resetWorld();
    H::setSensor("F2", "E1", true, true);
    tick(10);
    checkEq(H::entry.phase, "shooting", "shooting 진입");

    // 🔴 시한 전에는 수동 입력을 안 받는다
    ParkingServer::ManualPlateResult early = onManualPlate(g_srv, "123바9898");
    checkEq(early.code, "not_ready", "촬영 중에는 not_ready");

    tick(10100);                                     // 촬영 시한 10초 초과
    checkEq(H::entry.phase, "manual_wait", "시한 초과 → manual_wait");

    // 숫자가 없다
    ParkingServer::ManualPlateResult r1 = onManualPlate(g_srv, "가나다");
    checkEq(r1.code, "bad_request", "숫자가 없으면 거절");
    H::now += 1100;
    // 🔴 LCD 예약 코드와 겹친다 — 셋 다
    ParkingServer::ManualPlateResult r2 = onManualPlate(g_srv, "0000000");
    checkEq(r2.code, "bad_request", "0000000 은 welcome 과 겹친다");
    H::now += 1100;
    ParkingServer::ManualPlateResult r3 = onManualPlate(g_srv, "0000002");
    checkEq(r3.code, "bad_request", "0000002 는 촬영중과 겹친다");
    // 연타
    // 🔑 **연타는 수락 전에 본다** — 수락되면 phase 가 바뀌어 `not_ready` 가 되고,
    //   그러면 이 시험이 rate limit 을 **한 번도 안 밟는다**(공허한 통과).
    ParkingServer::ManualPlateResult rBurst = onManualPlate(g_srv, "가나다");
    checkEq(rBurst.code, "rate_limited", "1초 안의 연타는 거절");
    H::now += 1100;
    ParkingServer::ManualPlateResult r4 = onManualPlate(g_srv, "123바9898");
    checkEq(r4.code, "", "정상 번호는 수락");
    ParkingServer::ManualPlateResult r5 = onManualPlate(g_srv, "123바9899");
    checkEq(r5.code, "not_ready", "배정된 뒤에는 더 안 받는다");

    tick(10);
    checkEq(H::entry.phase, "assigned", "수동 입력으로 배정된다");
    checkEq(H::entry.plate_source, "manual", "출처 manual — 인식률에 안 섞인다");
    check(H::entry.attempts == 4, "시도 수를 센다(거절된 것도 센다)");
}

// ═══ ③ 차가 그냥 가버린다 ══════════════════════════════════════════════════
void t_vacate() {
    std::printf("③ 차가 가버리면 idle 로\n");
    resetWorld();
    H::setSensor("F2", "E1", true, true);
    tick(10);
    tick(10100);
    checkEq(H::entry.phase, "manual_wait", "manual_wait 진입");
    check(H::valueOf("C1") == 2, "촬영 중 표시가 떠 있다");

    H::setSensor("F2", "E1", true, false);           // 차가 갔다
    tick(1000);                                      // 여기서 처음 '비었다' 를 관측한다
    checkEq(H::entry.phase, "manual_wait", "3초 전에는 유지");
    tick(2900);
    checkEq(H::entry.phase, "manual_wait", "2.9초에도 유지 — 경계를 넘지 않았다");
    tick(200);
    checkEq(H::entry.phase, "idle", "3초 비면 취소");
    check(H::valueOf("C1") == 0, "촬영 중 표시를 지운다");
    ParkingServer::ManualPlateResult r = onManualPlate(g_srv, "123바9898");
    checkEq(r.code, "not_ready", "취소 뒤에는 입력을 안 받는다");
}

// ═══ ④ 오주차 — 배정 아닌 자리에 댄다 ══════════════════════════════════════
void t_wrong_parking() {
    std::printf("④ 오주차\n");
    resetWorld();
    H::setSensor("F2", "E1", true, true);
    tick(10);
    H::camState = CAM_READY; H::camPlate = "123바9898";
    tick(10);
    checkEq(H::entry.slot, "A1", "A1 을 배정했다");

    H::setSensor("F2", "E1", true, false);
    H::setSensor("F1", "A3", true, true);            // 엉뚱한 자리에 댔다
    tick(10);
    checkEq(H::entry.phase, "parking", "오주차도 같은 계수기로 확인한다");
    tick(10100);
    checkEq(H::entry.phase, "done", "10초 뒤 확정");
    check(H::valueOf("C3") == 1, "그 자리 LCD 에 NonHuman(코드 1)");
    check(H::plates["A3"].empty(), "🔴 번호를 기록하지 않는다");
    check(H::plates["A1"].empty(), "원래 배정 자리에도 안 남는다");
}

// ═══ ⑤ 출차 4단계 ═════════════════════════════════════════════════════════
void t_exit() {
    std::printf("⑤ 출차\n");
    resetWorld();
    H::setSensor("F1", "A2", true, true);
    tick(10);
    g_srv.slotPlate("A2", "123바9898", "camera");    // 이미 주차된 상태를 만든다
    checkEq(H::plates["A2"], "123바9898", "기록이 있다");

    H::setSensor("F1", "A2", true, false);           // 1번: 차가 빠진다
    tick(10);                                        // 여기서 처음 '비었다' 를 관측한다
    tick(4900);
    checkEq(H::plates["A2"], "123바9898", "4.9초에는 안 지운다");
    tick(200);
    check(H::plates["A2"].empty(), "5초 뒤 기록을 지운다");

    H::setSensor("F2", "X1", true, true);            // 2·3번
    tick(10);
    check(H::valueOf("XD") == 1, "출구1 감지 → 차단봉 열림(1)");
    H::setSensor("F2", "X2", true, true);            // 4번
    tick(10);
    check(H::valueOf("XD") == 0, "출구2 감지 → 차단봉 0도");
}

// ═══ ⑥ 🔴 음성 대조 — **모르면 배정하지 않는다** ═══════════════════════════
void t_unknown_blocks() {
    std::printf("⑥ 음성 대조 — known=false 를 '비었다'로 읽지 않는다\n");
    resetWorld();
    H::setSensor("F1", "A1", false, false);          // A1 값을 못 받았다
    H::setSensor("F2", "E1", true, true);
    tick(10);
    H::camState = CAM_READY; H::camPlate = "123바9898";
    tick(10);
    checkEq(H::entry.phase, "shooting", "자리 값을 모르면 배정을 보류한다");
    check(H::valueOf("ED") != 1, "차단봉을 열지 않는다(닫힘 유지)");

    H::setSensor("F1", "A1", true, false);           // 값이 왔다
    tick(10);
    checkEq(H::entry.phase, "assigned", "값이 오면 그때 배정한다");
}

// ═══ ⑦ 🔴 음성 대조 — 조립표가 장치를 정확히 가른다 ════════════════════════
void t_routing() {
    std::printf("⑦ 라우팅 — 감지 보드와 표시 보드가 다르다\n");
    resetWorld();
    H::setSensor("F2", "E1", true, true);
    tick(10);
    H::camState = CAM_READY; H::camPlate = "555가1234";
    // A1 을 채워 두면 A2 로 배정된다 → LCD 는 **F2** 로 가야 한다
    H::setSensor("F1", "A1", true, true);
    tick(10);
    checkEq(H::entry.slot, "A2", "A1 이 차 있으면 A2");
    bool lcdToF2 = false, ledToF5 = false, gateToF3 = false;
    for (size_t i = 0; i < H::sent.size(); i++) {
        if (H::sent[i].find("F2\tC2=") == 0) lcdToF2 = true;
        if (H::sent[i].find("F5\tL2=1") == 0) ledToF5 = true;
        if (H::sent[i].find("F3\tED=1") == 0) gateToF3 = true;
    }
    check(lcdToF2, "자리2 LCD 는 F2 로 간다");
    check(ledToF5, "안내등은 F5 로 간다");
    check(gateToF3, "입구 차단봉은 F3 으로 간다");
}

// ═══ ⑧ 번호판 → LCD 환산 ══════════════════════════════════════════════════
void t_plate_digits() {
    std::printf("⑧ 번호판 환산\n");
    resetWorld();
    H::setSensor("F2", "E1", true, true);
    tick(10);
    tick(10100);
    ParkingServer::ManualPlateResult r = onManualPlate(g_srv, "서울12가345678901");
    checkEq(r.code, "", "자릿수가 넘쳐도 막지 않는다");
    tick(10);
    check(H::valueOf("C1") == 5678901, "하위 7자리를 쓴다");
}

// ═══ ⑨ 🔴 폰이 안 붙어 있다 — **10초를 버리지 않는다** ═══════════════════
void t_no_phone() {
    std::printf("⑨ 폰 미접속 — 촬영 요청이 안 만들어진다\n");
    resetWorld();
    H::camCanIssue = false;                          // cameraShoot() → -1
    H::setSensor("F2", "E1", true, true);
    tick(10);
    // 🔴 **여기가 요점**: 시한(10초)을 기다리지 않고 **그 박자에** 수동 입력으로 간다.
    //   전에는 로그만 찍고 10초를 통째로 기다렸다 — 사람이 아무 일도 없는 화면을 본다.
    checkEq(H::entry.phase, "manual_wait", "요청번호가 없으면 곧바로 manual_wait");
    check(H::entry.elapsed_ms < 1000, "10초를 기다리지 않는다");

    // 그리고 그 상태에서 수동 입력이 정상으로 먹어야 한다
    ParkingServer::ManualPlateResult r = onManualPlate(g_srv, "123바9898");
    checkEq(r.code, "", "폰이 없어도 수동 입력으로 진행된다");
    tick(10);
    checkEq(H::entry.phase, "assigned", "배정까지 간다");
    checkEq(H::entry.plate_source, "manual", "출처 manual");
}

int main() {
    std::printf("=== five 흐름 하니스 — lot.cpp 는 진짜, ParkingServer 는 껍데기 ===\n");
    t_entry_camera();
    t_manual_fallback();
    t_vacate();
    t_wrong_parking();
    t_exit();
    t_unknown_blocks();
    t_routing();
    t_plate_digits();
    t_no_phone();
    std::printf("=== %d pass / %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
