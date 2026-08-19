// parking.h — 🔴 **공개 조립 API.** 이 파일만 읽으면 주차장을 만들 수 있어야 한다
//
// 사용자 요구(REQ-0272): *"복잡한 구조는 은닉화하여 간단한 구조로 이용 가능해야 한다"*
//                        *"코드 작성 시점에 해당 코드의 **흐름이 보인다**"*
//
// 그래서 이 헤더의 판정은 둘이다:
//   ① `.cpp` 를 열지 않고 쓸 수 있는가
//   ② 사용 코드를 위에서 아래로 읽으면 **무슨 일이 어떤 순서로** 일어나는지 보이는가
//
// 🔑 무엇을 밖에 두고 무엇을 안에 넣는지의 판별자: **호출자가 그 순서를 바꿀 수 있는가.**
//   바꿀 수 있다(언제 포트를 열지 · 어떻게 루프를 돌지 · 무엇을 어디에 배치할지) → **밖**
//   바꿀 수 없다(한 박자 안의 고정 순서 · 등록 결속 · 재전송·격리) → **안**
//
// ── 쓰는 법 (이게 전부다) ────────────────────────────────────────────────
//
//   ParkingLot lot;
//   lot.spot("A1").sensor("A1").sensor("B1");   // 자리 하나에 센서 둘(이중화)
//   lot.spot("A2").sensor("A2").sensor("B2");
//   lot.gate("E1", Gate::IN);
//   lot.gate("X1", Gate::OUT);
//
//   ParkingServer srv(lot);
//   if (!srv.openPorts()) return 1;             // ① 포트를 연다
//   while (srv.serveOneTick()) { }              // ② 한 박자씩 돈다
//   srv.closeDown();                            // ③ 요약을 남기고 닫는다
//
// ⚠ 순서 요구가 **없다**: `spot` 과 `gate` 는 어느 쪽을 먼저 써도 같다.
//   `openPorts`→`serveOneTick`→`closeDown` 은 **순서가 아니라 흐름**이다 — 이름이 그것을 말한다.
#ifndef PARKING_H
#define PARKING_H

#include <string>
#include <vector>
#include <cstddef>

class ParkingLot;

class Gate {
public:
    enum Kind { IN, OUT };          // 입구 · 출구
};

// 자리 하나. **센서를 배치하는 것이 유일한 일**이다.
class Spot {
public:
    Spot& sensor(const std::string& name);   // 이 자리에 붙은 센서 이름(전선 이름)
private:
    friend class ParkingLot;
    Spot(ParkingLot* lot, std::size_t idx) : lot_(lot), idx_(idx) {}
    ParkingLot* lot_;
    std::size_t idx_;
};

// 주차장 한 곳. **지형을 선언하는 것이 유일한 일**이다.
class ParkingLot {
public:
    Spot spot(const std::string& id);                    // 주차 자리를 만든다
    void gate(const std::string& id, Gate::Kind kind);   // 입구/출구를 만든다

    // 서버가 읽는다. **호출자가 쓸 일은 없다** — 다만 감출 이유도 없다(상태는 드러낸다).
    struct Area {
        std::string id;
        std::string kind;                     // "parking" | "entrance" | "exit"
        std::vector<std::string> sensors;     // 전선 센서 이름들
    };
    const std::vector<Area>& areas() const { return areas_; }
    bool empty() const { return areas_.empty(); }

private:
    friend class Spot;
    std::vector<Area> areas_;
};

// 서버. 🔴 **복잡함은 전부 이 뒤에 있다** — 헤더에 자료구조가 하나도 안 나온다.
class ParkingServer {
public:
    explicit ParkingServer(const ParkingLot& lot);
    ~ParkingServer();

    bool openPorts();        // 포트를 연다. 실패하면 false (이유는 로그에 남는다)
    bool serveOneTick();     // 한 박자: 수신 → 자리 판정 → 하행 송신 → 화면 방송
    void closeDown();        // 요약을 남기고 닫는다

private:
    ParkingServer(const ParkingServer&);              // 복사 금지 — 소켓을 들고 있다
    ParkingServer& operator=(const ParkingServer&);
    struct Impl;
    Impl* p_;
};

#endif  // PARKING_H
