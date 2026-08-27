// entry.h — **엔진의 진입점.** `main()` 이 여기 있다.
// ═══════════════════════════════════════════════════════════════════════════
// 🔴 **기여자는 이 파일을 안 연다.** 여는 것은 `lot.cpp` 하나다.
//
//   `main()` 을 기여자 파일에 두면 **"각자 자기 `main()` 을 갖는다"** 로 배운다.
//   🔴 **그 오해는 로컬에서 한 번도 안 깨진다** — 혼자니까 잘 돈다.
//     합칠 때 깨지고, **그때는 이미 그 모델 위에 코드를 썼다.**
//
// 🔑 판별자는 우리가 이미 세운 것이다:
//     🔓 호출자가 **바꿀 수 있는** 순서 → 드러낸다
//     🔒 호출자가 **바꿀 수 없는** 순서 → 감춘다. 드러내 봐야 **지킬 의무만** 생긴다
//   `openPorts → 루프 → closeDown` 은 기여자가 못 바꾼다. 그래서 여기 있다.
//
// ⚠ 단독 컴파일되지 않는다. `server.cpp` 안 그 자리에 include 된다.
// ═══════════════════════════════════════════════════════════════════════════
// 🔑 **윈도우 전용 두 줄을 `#ifdef` 로 감싼다.** 윈도우 전처리 결과는 **글자 하나 안 바뀐다** —
//   `_WIN32` 가 정의된 곳에서는 아래 두 블록이 예전 그대로 펼쳐진다.
//   ⚠ `server.cpp` 는 이미 플랫폼 어댑터(`#ifdef _WIN32 / #else`)를 갖고 있었다.
//     **막고 있던 것은 이 파일의 무조건 include 하나뿐**이었다(맥 컴파일 오류가 정확히 1건).
#ifdef _WIN32
#include <windows.h>
#endif

// dev_server 판 — 🔴 **콘솔이 기본이다.** 파일 로그도 자가검증도 없다.
int main(int argc, char** argv) {
    int rc = 0;
    // 🔑 **첫 줄에서 잡는다** — `exe_sibling_dir()` 이 이 값으로 실행파일 위치를 푼다(REQ-0497).
    //   ⚠ 나중에 잡으면 안 된다. 누가 `chdir` 하면 상대 `argv[0]` 이 **다른 곳을 가리킨다**.
    if (argc > 0 && argv[0]) g_argv0 = argv[0];
#ifdef _WIN32
    // 윈도우 콘솔을 UTF-8 로. 🔑 POSIX 터미널은 이미 UTF-8 이라 대응물이 필요 없다.
    SetConsoleOutputCP(65001);
#endif
#include "cli.h"      // 손잡이 해석 — --log · --port-offset · --park-dev · 시험용 셋
    {
        // 🔴 **`open_log()` 를 안 부른다.** 출력은 stdout 하나다(사용자 확정).
        //   ⚠ 끄면 사라진다. 남겨야 하면 `./srv > dev.log` 로 직접 받아라.
        (void)log_path;

        // 🔑 **기여자의 조립을 부른다.** 이 한 줄이 `lot.cpp` 와 엔진의 유일한 접점이다.
        ParkingLot lot;
        buildLot(lot);

        // 🔑 **어디서 시작하는지를 값으로 말한다.** 문서로 설명하면 또 길어진다.
        {
            size_t mods = 0;
            for (size_t i = 0; i < lot.areas().size(); i++) mods += lot.areas()[i].modules.size();
            char b[160];
            snprintf(b, sizeof(b), "[기동] lot.cpp 의 buildLot() 을 부른다 → 자리 %zu개 · 모듈 %zu개",
                     lot.areas().size(), mods);
            logf("=", b);
        }

        ParkingServer srv(lot);
        srv.onCommandResult(onCmdResult);       // ③ — 기여자가 비워 뒀으면 아무 일도 안 한다
        srv.onOccupancyChanged(onOccupancy);    // ④ — 자리 점유가 바뀔 때
        srv.onManualPlate(onManualPlate);       // ⑥ — 수동 입력 폴백(상행 `plate_manual`)
        srv.onSlotPick(onSlotPick);             // ⑦ — 8081 자리 선택(상행 `pick_slot`)

        if (!srv.openPorts()) return 1;
        while (srv.serveOneTick()) {
            onTick(srv);                        // ② — 기여자가 비워 뒀으면 아무 일도 안 한다
        }
        srv.closeDown();
        rc = 0;
    }
    return rc;   // 🔑 `WSACleanup` 도 정적 소멸자가 한다
}
