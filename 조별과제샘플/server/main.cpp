// main.cpp — **개발자용 샘플.** 자기 주차장은 여기만 고쳐서 만든다.
//
//   빌드  cd 조별과제샘플/server
//         c++ -std=c++11 -O2 -w -DBUILD_ID='"내판본"' -o server_test server.cpp
//   실행  ./server_test --port-offset=300     🔴 자기 시험은 오프셋을 줘라
//         포트 셋 : 아두이노 9991+N · 화면 9900+N · 폰 5500+N
//         ⚠ `--port-offset=400` 은 피해라 — 폰 포트가 5900 이 되어 macOS 화면공유와 부딪힌다
//
// 🔑 **설명은 여기 없다.** `docs/net/GUIDE-sample.md` 를 읽어라 —
//   이 파일은 **따라 치는 것**이고 그 문서는 **읽는 것**이다.
//   틀린 것은 서버가 말해 준다(문구와 뜻은 `docs/net/GUIDE-server-says.md`).
//
// ⚠ 이 파일은 단독으로 컴파일하지 마라. 빌드는 `c++ … server.cpp` 다.

int main(int argc, char** argv) {
    int rc;
#include "cli.h"      // 손잡이 해석 — --log · --port-offset · --park-dev · 시험용 셋
    if (argc > 1 && std::string(argv[1]) == "--selftest") rc = selftest();
    else {
        open_log(log_path);

        // ① 주차장을 조립한다 — **여기만 고치면 된다**       자세히: GUIDE-sample.md §조립
        ParkingLot lot;
        lot.spot("A1").sensor("P1", "A1").sensor("P1", "B1")
                      .actuator("P1", "LD").actuator("P1", "LC")
                      .actuator("P1", "DR").actuator("P1", "L2");
        // 🔴 이름은 **정확히 2글자** — 장치 표(`client.ino`)와 글자 그대로 같아야 붙는다
        // 🔴 선언 안 한 모듈은 **꺼진 것**이다. 장치에 있어도 화면에 안 나온다
        // 🔑 `actuator` 는 **점유 판정에서 빠진다**(차단봉이 열려도 차가 있는 게 아니다)

        // 자리를 더 켜려면 — 주석을 지운다
        // lot.spot("A2").sensor("P1", "A2").sensor("P1", "B2");
        // 다른 사람 아두이노 : devid 만 바꾼다(1~8자)
        // lot.spot("A6").sensor("KIM01", "C1").sensor("KIM01", "C2");
        // 차단봉(지금 칩의 E1·X1 은 가상이라 꺼 뒀다)
        // lot.gate("E1", Gate::IN);  lot.spot("E1").actuator("P1", "E1");

        // ② 서버에 싣는다
        ParkingServer srv(lot);

        // ③ 명령 결과를 나중에 받는다 — 성공 / 거절 / 무응답  자세히: §콜백
        //   🔴 셋은 **고치는 곳이 다르다.** 무응답은 장치·링크 문제다
        // static void onResult(const CmdResult& r) { … }   ← main() 밖에 둔다
        // srv.onCommandResult(onResult);

        // ④ 포트를 연다 — 아두이노 · 화면 · 폰
        if (!srv.openPorts()) return 1;

        // ⑤ 한 박자씩 돈다 — 수신 → 자리 판정 → 하행 송신 → 화면 방송
        while (srv.serveOneTick()) {
            // 명령은 **여기서** 낸다 — 조립 시점(②)에는 장치가 아직 안 붙어 있다.
            //   `srv.send(devid, 모듈이름, 값)` · 값의 뜻은 기여자가 정한다  자세히: §명령
            //   🔴 값의 뜻을 서버도 프로토콜도 모른다. **표를 양쪽에 똑같이 적어라**
            //
            // if (srv.deviceReady("P1") && <내 조건>) {
            //     srv.send("P1", "LD", 1);                    // 단건
            //
            //     ParkingServer::Batch b = srv.batch("P1");    // 묶음 — 한 창에 같이 나간다
            //     b.add("LD",1).add("LC",1234567).add("DR",1).add("L2",7654321);
            //     ParkingServer::BatchResult r = b.send();     // 최대 srv.maxPerBatch() 건
            // }
            //   🔴 상한(지금 4)을 넘기면 **한 건도 안 보내고 거절한다**  자세히: §묶음
            //   ⚠ 창 원자성은 보장한다. **실행 원자성은 아니다** — 하나가 거절돼도 나머지는 수행된다
            //   ⚠ 주석 안의 코드는 컴파일러가 안 본다 — **켜면 한 번 빌드해 봐라**
        }

        // ⑥ 요약을 남기고 닫는다
        srv.closeDown();
        rc = 0;
    }
    return rc;
}
