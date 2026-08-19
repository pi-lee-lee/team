// lot.cpp — **기여자가 여는 유일한 파일.** 자기 주차장은 여기만 고치면 된다.
//
//   빌드  cd 조별과제샘플/server
//         c++ -std=c++11 -O2 -w -DBUILD_ID='"내판본"' -o server_test server.cpp
//   실행  ./server_test --port-offset=300     🔴 자기 시험은 오프셋을 줘라
//         포트 셋 : 아두이노 9991+N · 화면 9900+N · 폰 5500+N
//         ⚠ `--port-offset=400` 은 피해라 — 폰 포트가 5900 이 되어 macOS 화면공유와 부딪힌다
//
// 🔑 **여기에 `main()` 은 없다.** 서버를 여닫고 도는 것은 엔진이 한다 —
//   그 순서는 기여자가 바꿀 수 없으므로 드러내 봐야 **지킬 의무만** 생긴다.
//
// 🔑 **설명은 여기 없다.** `docs/net/GUIDE-sample.md` 를 읽어라 —
//   이 파일은 **따라 치는 것**이고 그 문서는 **읽는 것**이다.
//   틀린 것은 서버가 말해 준다(문구와 뜻은 `docs/net/GUIDE-server-says.md`).
//
// ⚠ **단독으로 *링크* 하지 마라.** 빌드는 `c++ … server.cpp` 다 — 여기엔 `main()` 도 엔진도 없다.
//   다만 **문법 검사는 혼자서도 통과한다**(`c++ -fsyntax-only lot.cpp`) —
//   그래야 편집기가 타입을 알고 빨간 줄이 안 뜬다.

// 🔑 **이 한 줄이 조립 API 전부를 들인다** — `ParkingLot` · `ParkingServer` · `CmdResult` · `SpotBehavior`.
//   include 를 둘로 만들지 않는다. "이 파일만 읽으면 된다"가 깨진다.
#include "parking.h"

// ① 주차장을 조립한다 — 🔴 **이것만 채우면 돌아간다**       자세히: GUIDE-sample.md §조립
void buildLot(ParkingLot& lot) {
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
}

// ② 한 박자마다 불린다 — 명령을 여기서 낸다      🔑 **비워 둬도 돌아간다**
//   조립 시점에는 장치가 아직 안 붙어 있다. 그래서 명령은 여기서 낸다.
//   `srv.send(devid, 모듈이름, 값)` · 값의 뜻은 기여자가 정한다   자세히: §명령
//   🔴 값의 뜻을 서버도 프로토콜도 모른다. **표를 양쪽에 똑같이 적어라**
void onTick(ParkingServer& srv) {
    (void)srv;
    // if (srv.deviceReady("P1") && <내 조건>) {
    //     srv.send("P1", "LD", 1);                     // 단건
    //
    //     ParkingServer::Batch b = srv.batch("P1");     // 묶음 — 한 창에 같이 나간다
    //     b.add("LD",1).add("LC",1234567).add("DR",1).add("L2",7654321);
    //     ParkingServer::BatchResult r = b.send();      // 최대 srv.maxPerBatch() 건
    // }
    //   🔴 상한(지금 4)을 넘기면 **한 건도 안 보내고 거절한다**   자세히: §묶음
    //   ⚠ 창 원자성은 보장한다. **실행 원자성은 아니다** — 하나가 거절돼도 나머지는 수행된다
    //   ⚠ 주석 안의 코드는 컴파일러가 안 본다 — **켜면 한 번 빌드해 봐라**
}

// ③ 명령 결과가 도착하면 불린다 — 성공 / 거절 / 무응답   🔑 **비워 둬도 돌아간다**
//   🔴 셋은 **고치는 곳이 다르다.** 무응답은 장치·링크 문제다   자세히: §콜백
void onCmdResult(const CmdResult& r) {
    (void)r;
    // std::cout << r.module << " → " << r.kindName() << "\n";
}
