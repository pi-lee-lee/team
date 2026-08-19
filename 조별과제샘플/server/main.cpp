// main.cpp — **엔트리 포인트** (2026-08-19 · REQ-0272 1단계)
//
// 🔴 **이 파일은 지금 `server.cpp` 가 원래 자리에서 `#include` 한다.**
//   그래서 전처리 결과가 이전과 **한 글자도 다르지 않고**, 산출물(`.o`)이 **0 차이**여야 한다.
//   그 대조가 "떼어 냈지만 아직 아무것도 안 바꿨다"를 **값으로** 증명한다.
//   (arduino 가 펌웨어 846줄을 이 방법으로 빼고 `hex` 0 을 얻었다. 서버도 같은 방법을 쓴다.)
//
// ⏳ **다음 단계에서 뒤집는다** — `main.cpp` 가 번역 단위가 되고 서버 조각들을 include 하는 쪽으로.
//   🔴 **그때부터 축이 생긴다.** 지금은 축이 없다는 것을 먼저 증명해 두는 것이 목적이다.
//
// ⚠ 그러므로 **이 파일을 단독으로 컴파일하지 마라.** 빌드는 여전히 `c++ … server.cpp` 다.

int main(int argc, char** argv) {
    // 🔑 `WSAStartup`·`SIGPIPE`·`SIGINT/TERM` 은 **여기 없다.** 정적 초기화가 먼저 다 해 뒀다
    //   (`server.cpp` 의 `ProcessInit`). **Winsock 이 무엇인지 몰라도 주차장을 만들 수 있다.**
    int rc;
#include "cli.h"      // 손잡이 해석 — --log · --port-offset · --park-dev · 시험용 셋
    // --selftest 는 로그 파일을 열지 않는다. 자가검증 출력이 운영 로그에 섞이면
    // 인스턴스 경계 없이 사람이 만든 줄이 끼어드는 셈이라, 계약이 지키려는 것을 스스로 깬다.
    if (argc > 1 && std::string(argv[1]) == "--selftest") rc = selftest();
    else {
        open_log(log_path);

        // ── 🔴 여기가 **사용 코드**다 (REQ-0272) ─────────────────────────────
        //   사용자 요구: *"코드 작성 시점에 해당 코드의 **흐름이 보인다**"*
        //   위에서 아래로 읽으면 **무슨 일이 어떤 순서로** 일어나는지 보여야 한다.
        //
        //   ⚠ **자동 배선을 쓰지 않았다.** 자리마다 센서를 적는 것이 한 줄로 줄 수도 있지만
        //     그러면 **언제 무엇이 붙는지 안 보인다.** 배치는 호출자가 바꾸는 것이라 밖에 남긴다.
        //   🔑 판별자: **호출자가 바꿀 수 있으면 밖, 못 바꾸면 안.**

        // ① 주차장을 조립한다 — 자리 다섯, 각 자리에 센서 둘(이중화)
        ParkingLot lot;
        lot.spot("A1").sensor("A1").sensor("B1");
        lot.spot("A2").sensor("A2").sensor("B2");
        lot.spot("A3").sensor("A3").sensor("B3");
        lot.spot("A4").sensor("A4").sensor("B4");
        lot.spot("A5").sensor("A5").sensor("B5");
        lot.gate("E1", Gate::IN);      // 입구
        lot.gate("X1", Gate::OUT);     // 출구

        // ② 서버에 싣는다
        ParkingServer srv(lot);

        // ③ 포트를 연다 — 아두이노 · 화면 · 폰
        if (!srv.openPorts()) return 1;

        // ④ 한 박자씩 돈다 — 수신 → 자리 판정 → 하행 송신 → 화면 방송
        while (srv.serveOneTick()) { }

        // ⑤ 요약을 남기고 닫는다
        srv.closeDown();
        rc = 0;
    }
    return rc;   // 🔑 `WSACleanup` 도 정적 소멸자가 한다

}
