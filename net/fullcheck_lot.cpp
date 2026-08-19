// net/fullcheck_lot.cpp — **전수검사 측정 전용 `lot.cpp`.** 커밋되는 샘플이 아니다.
// ═══════════════════════════════════════════════════════════════════════════
// 쓰는 법 : cp net/fullcheck_lot.cpp 조별과제샘플/server/lot.cpp
//           cd 조별과제샘플/server && c++ -std=c++11 -O2 -w -DBUILD_ID='"<해시>+full"' -o /tmp/srv_full server.cpp
//           cd - && git checkout -- 조별과제샘플/server/lot.cpp      🔴 즉시 되돌린다
//
// 🔴 **이 파일이 존재하는 이유**: 앞서 각본을 `lot.cpp` 안에서 직접 고쳤다가
//   되돌리면서 **통째로 잃었다.** 측정 판을 트리 안에서만 들고 있으면
//   "되돌리기"가 곧 "삭제"다. **측정 판은 자기 파일로 가져야 한다.**
//
// 🔑 재발사 : `touch /tmp/fullcheck_again` — **서버를 안 죽인다.**
//   사람 눈이 계측기인 항목(LED)은 다시 쏘는 비용이 싸야 한다.
//   재기동으로 재발사하면 monitor 의 창 t0 가 깨진다.
// ═══════════════════════════════════════════════════════════════════════════
#include "parking.h"
#include <cstdio>

// ── A6 측정용 판정 — 기본 OR 이 아니라 AND ──────────────────────────────────
// 🔴 **센서 둘이 갈린 상태**라야 두 판정이 다른 답을 낸다. 같은 값이면 밟아도 안 갈린다.
//   그래서 갈린 순간을 **값으로 찍는다** — 안 찍으면 "안 갈렸는데 통과"를 못 가른다.
struct AndSpot : SpotBehavior {
    virtual bool occupied(const std::vector<SensorReading>& s) const {
        int k = 0, v = 0;
        for (size_t i = 0; i < s.size(); i++)
            if (s[i].known) { k++; if (s[i].value) v++; }
        static int last_k = -1, last_v = -1;
        if (k != last_k || v != last_v) {
            last_k = k; last_v = v;
            std::cout << "[검사 A6] 센서 아는것=" << k << " 찬것=" << v
                      << "  → AND=" << (k > 0 && v == k ? 1 : 0)
                      << " · OR=" << (v > 0 ? 1 : 0)
                      << (k >= 2 && v > 0 && v < k ? "   ✅ **갈렸다** — 이 순간이 판정 근거다" : "")
                      << "\n";
        }
        return k > 0 && v == k;
    }
};
static AndSpot g_and;

void buildLot(ParkingLot& lot) {
    lot.spot("A1").sensor("P1", "A1").sensor("P1", "B1")
                  .actuator("P1", "LD").actuator("P1", "L2")
                  .behavior(g_and);                     // ⚠ 측정 전용(A6)
    lot.gate("E1", Gate::IN);
    lot.spot("E1").actuator("P1", "DR");
}

static bool      g_ready = false;
static long long g_t0    = 0;
static int       g_step  = 0;

void onTick(ParkingServer& srv) {
    // ── B6 : 등록 **전** deviceReady 는 거짓이어야 한다 ─────────────────────
    // 🔑 거짓 갈래는 **기동만 하면 밟힌다.** 억지로 만들 것이 없다.
    static bool b6 = false;
    if (!b6) {
        b6 = true;
        bool r = srv.deviceReady("P1");
        std::cout << "[검사 B6] 등록 전 deviceReady(\"P1\") = " << (r ? "참" : "거짓")
                  << (r ? "  🔴 장치가 이미 붙어 있었다 — 이 갈래는 **못 밟았다**"
                        : "  ✅") << "\n";
    }
    // ── 재발사 ────────────────────────────────────────────────────────────
    if (g_ready) {
        FILE* f = fopen("/tmp/fullcheck_again", "r");
        if (f) { fclose(f); remove("/tmp/fullcheck_again");
                 g_t0 = srv.nowMs(); g_step = 0;
                 std::cout << "\n=== 🔁 재발사 — 처음부터 다시 ===\n"; }
    }
    if (!g_ready) {
        if (!srv.deviceReady("P1")) return;
        g_ready = true; g_t0 = srv.nowMs(); g_step = 0;
        std::cout << "[검사 B5] 등록 후 deviceReady = 참 ✅ · maxPerBatch() = "
                  << srv.maxPerBatch() << "\n"
                  << "=== 각본 시작 — 9초 간격. 🔴 LED 는 +0초 켜짐 / +9초 꺼짐 ===\n";
        return;
    }
    if (g_step > 9) return;
    if (srv.nowMs() - g_t0 < (long long)g_step * 9000) return;
    int s = g_step++;

    if (s == 0) {
        std::cout << "\n[검사 LD1] LD 1 — 🔴 **보드 13번 LED 가 켜져야 한다**\n";
        std::cout << "  send() = " << (srv.send("P1", "LD", 1) ? "참(큐에 넣었다)" : "거짓") << "\n";
    } else if (s == 1) {
        std::cout << "\n[검사 LD2] LD 0 — 🔴 **LED 가 꺼져야 한다**\n";
        std::cout << "  send() = " << (srv.send("P1", "LD", 0) ? "참" : "거짓") << "\n";
    } else if (s == 2) {
        std::cout << "\n[검사 B7] 묶음 4건 — 한 창에 같이 나가야 한다\n";
        ParkingServer::Batch b = srv.batch("P1");
        b.add("LD", 1).add("LC", 1234567).add("DR", 1).add("L2", 7654321);
        ParkingServer::BatchResult r = b.send();
        std::cout << "  큐 " << r.queued << " · 거절 " << r.rejected
                  << (r.queued == 4 ? "  ✅" : "  🔴") << "\n";
    } else if (s == 3) {
        std::cout << "\n[검사 D3] 묶음 5건 — 상한 " << srv.maxPerBatch()
                  << " 초과 → **한 건도 안 나가야 한다**\n";
        ParkingServer::Batch b = srv.batch("P1");
        b.add("LD",0).add("LC",1).add("DR",2).add("L2",2).add("LD",1);
        ParkingServer::BatchResult r = b.send();
        std::cout << "  큐 " << r.queued << " · 거절 " << r.rejected
                  << (r.queued == 0 && r.rejected == 5 ? "  ✅ 전량 거절" : "  🔴") << "\n";
    } else if (s == 4) {
        std::cout << "\n[검사 D5] 묶음 4건 · 하나만 8자리(바이트 축 시험)\n";
        ParkingServer::Batch b = srv.batch("P1");
        b.add("LC", 87654321).add("L2", 1).add("LD", 0).add("DR", 2);
        ParkingServer::BatchResult r = b.send();
        std::cout << "  큐 " << r.queued << " · 거절 " << r.rejected << "\n";
    } else if (s == 5) {
        std::cout << "\n[검사 D6a] DR 1 (열기) — 다음 상태 프레임의 DR 에코가 **1** 이어야 한다\n";
        srv.send("P1", "DR", 1);
    } else if (s == 6) {
        std::cout << "\n[검사 D6b] DR 2 (닫기) — 에코가 **0** 이어야 한다\n"
                  << "  🔑 **닫기가 계약을 시험한다.** `arg!=0` 이었으면 여기서 켜진 채 남는다\n";
        srv.send("P1", "DR", 2);
    } else if (s == 7) {
        std::cout << "\n[검사 B4] 단건 회귀 — 묶음을 넣고도 단건이 그대로 도는가\n";
        std::cout << "  send() = " << (srv.send("P1", "LD", 1) ? "참" : "거짓")
                  << "   🔴 **LED 다시 켜짐**\n";
    } else if (s == 8) {
        std::cout << "\n[검사 ZZ] 선언 안 한 모듈 — **거짓을 돌려주고 전선에 안 나가야 한다**\n";
        std::cout << "  send(\"ZZ\") = " << (srv.send("P1", "ZZ", 1) ? "참  🔴" : "거짓  ✅") << "\n";
    } else if (s == 9) {
        srv.send("P1", "LD", 0);
        std::cout << "\n=== 각본 끝 — LED 끔. 🔁 다시 보려면 `touch /tmp/fullcheck_again` ===\n";
    }
}

void onCmdResult(const CmdResult& r) {
    std::cout << "[명령] " << r.module << " " << r.value << " → " << r.kindName()
              << " (rid=" << r.rid << " result=" << r.deviceResult << ")\n";
}
