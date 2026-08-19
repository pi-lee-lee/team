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
//   void buildLot(ParkingLot& lot) {              // ← `lot.cpp` 에 이 함수만 채운다
//       lot.spot("A1").sensor("P1","A1").sensor("P1","B1");   // 자리 + 센서 둘(이중화)
//       lot.gate("E1", Gate::IN);                             // 입구
//       lot.spot("E1").actuator("P1","DR");                   // 그 입구의 차단봉
//   }
//
// 🔴 **`main()` 은 여기 없다.** 포트를 열고 도는 것은 엔진(`entry.h`)이 한다 —
//   그 순서는 호출자가 못 바꾸므로 드러내 봐야 **지킬 의무만** 생긴다.
//   srv.closeDown();                            // ③ 요약을 남기고 닫는다
//
// ⚠ 순서 요구가 **없다**: `spot` 과 `gate` 는 어느 쪽을 먼저 써도 같다.
//   `openPorts`→`serveOneTick`→`closeDown` 은 **순서가 아니라 흐름**이다 — 이름이 그것을 말한다.
#ifndef PARKING_H
#define PARKING_H

#include <string>
#include <vector>
#include <cstddef>
#include "spot.h"      // SpotBehavior — 자리의 동작 방식(기여자가 구현한다)
#include "cmdresult.h" // CmdResult — 명령 결과를 나중에 받는다

class ParkingLot;

class Gate {
public:
    enum Kind { IN, OUT };          // 입구 · 출구
};

// 자리 하나. **센서를 배치하는 것이 유일한 일**이다.
class Spot {
public:
    // 🔴 **1인자 형태 — "아무 장치나 그 이름을 가진 것"** (원래 있던 것. 지우지 않는다)
    //   지금 `main.cpp` 의 선언이 전부 이 형태이고, **그것이 안 깨지는 것이 이 개정의 조건**이다.
    Spot& sensor(const std::string& name);

    // 🔴🔴 **2인자 형태 — 어느 장치의 어느 모듈인지 못 박는다** (2026-08-19)
    //
    //   > **`devid` 를 받는 순간 "모듈 이름이 자리 id 와 같아야 한다"는 암묵 규칙이 사라진다.**
    //
    //   ```
    //   lot.spot("A3").sensor("P2", "왼쪽센서");   // 이름이 자리 id 와 달라도 붙는다
    //   ```
    //   `Modules.h` 주석이 경고하던 함정 — *"다른 이름을 쓰면 등록은 성공하고 자리에는
    //   아무것도 안 붙는다. 오류가 안 뜬다"* — 이 **구조적으로 없어진다.**
    //
    //   ⚠ 같은 이름을 여러 장치가 쓸 수 있게 되므로 **`devid` 가 그 구분자다.**
    //     1인자 형태는 그 구분을 안 하겠다는 선언이다(장치가 하나뿐일 때 편하다).
    Spot& sensor(const std::string& devid, const std::string& name);

    // 명령을 받는 모듈(차단봉·표시등 등). **점유 판정에 안 쓰인다.**
    // 🔑 센서와 갈라 두는 이유: `occupied()` 가 받는 것은 **센서뿐**이어야 한다.
    //   섞이면 차단봉 상태가 자리 점유로 새어 들어간다(명세 §8.1).
    Spot& actuator(const std::string& devid, const std::string& name);

    // 🔴🔴 **이 자리의 판정 방식을 갈아끼운다** (2026-08-19)
    //
    //   ```
    //   struct 내판정 : SpotBehavior {
    //       virtual bool occupied(const std::vector<SensorReading>& s) const {
    //           return s.size() >= 2 && s[0].known && s[1].known && s[0].value && s[1].value;  // AND
    //       }
    //   };
    //   static 내판정 g_내판정;                       // 🔴 **서버보다 오래 살아야 한다**
    //   lot.spot("A1").sensor("P1","A1").behavior(g_내판정);
    //   ```
    //   안 부르면 **기본(OR)** 이 쓰인다 — 지금 모든 자리가 그 경우다.
    //
    // ⚠ **참조를 든다. 사본이 아니다.** 지역 변수를 넘기면 그 함수가 끝나는 순간 죽는다 —
    //   `static` 이나 전역으로 둬라. **그래서 인자가 `&` 다**(포인터였으면 `new` 를 부르게 된다).
    Spot& behavior(SpotBehavior& b);
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
    // 자리에 붙은 모듈 하나의 **선언**. (실제로 붙었는지는 등록이 정한다)
    struct Attach {
        std::string devid;      // "" = **아무 장치나** (1인자 형태)
        std::string name;       // 전선 모듈 이름
        bool        actuator;   // true = 명령 받는 모듈. **점유 판정에서 빠진다**
        Attach() : actuator(false) {}
        Attach(const std::string& d, const std::string& n, bool a)
            : devid(d), name(n), actuator(a) {}
    };
    struct Area {
        std::string id;
        std::string kind;                     // "parking" | "entrance" | "exit"
        // ⚠ 이름이 `sensors` 였다. **센서만 담지 않게 됐으므로 바꿨다** —
        //   담는 것이 바뀌었는데 이름이 그대로면 다음 사람이 센서만 있다고 읽는다.
        std::vector<Attach> modules;
        // 🔑 `0` 이면 **서버의 기본 판정**을 쓴다. 자리마다 다른 것을 꽂을 수 있다.
        //   ⚠ 소유하지 않는다 — 호출자가 준 것이 살아 있어야 한다(위 `behavior()` 주석).
        SpotBehavior* behavior;
        Area() : behavior(0) {}
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

    // 🔴 **모듈에 값을 보낸다** (2026-08-19)
    //   `srv.send("P1", "LCD1", 1234567);`   ← 7자리 숫자
    //   `srv.send("P1", "LED1", 1);`         ← on/off
    //   `srv.send("P1", "DOOR", 2);`         ← 동작 — **뜻은 기여자가 정한 표**
    //
    // 🔑 **`value` 의 뜻을 서버는 모른다.** 장치의 콜백이 그것을 해석한다.
    //   **그 표를 양쪽 주석에 적어라** — 어긋나면 **조용히 다른 동작을 한다.**
    // ⚠ 반환 `true` 는 **"전선 큐에 넣었다"** 이지 "수행됐다"가 아니다.
    //   실제 수행은 장치 ACK 과 다음 상태 프레임이 답한다.
    bool send(const std::string& devid, const std::string& moduleName, long value);

    // ── 묶음 하행 ───────────────────────────────────────────────────────────
    //
    //   하행은 **슬롯당 한 창**(1.2초)에만 나간다. 낱개로 N건이면 **N × 1.2초**다.
    //   묶으면 **하행 1슬롯 + ACK 1슬롯 ≈ 2.4초** 로 끝난다.
    //
    //   ```
    //   ParkingServer::Batch b = srv.batch("P1");
    //   b.add("LD", 1).add("LC", 1234567).add("DR", 2);
    //   ParkingServer::BatchResult r = b.send();
    //   ```
    //
    // 🔴 **원자적이 아니다. 독립이다.** 각 명령이 자기 rid 와 자기 ACK 을 갖는다 —
    //   3건 중 2번이 거절돼도 1·3번은 그대로 수행된다.
    //   **되돌릴 수단이 없으므로 "전부 아니면 전무"를 약속하지 않는다.**
    //   🔑 못 지키는 보장을 API 가 약속하면 **그 약속을 믿고 쓴 코드가 조용히 틀린다.**
    struct BatchResult {
        int queued;      // 전선 큐에 들어간 건수. ⚠ **"보냈다"이지 "됐다"가 아니다**
        int rejected;    // 보내기 전에 거절된 건수(모듈 없음 · 이름 틀림 · 상한 초과 · rid 고갈)
        BatchResult() : queued(0), rejected(0) {}
    };
    class Batch {
    public:
        Batch& add(const std::string& moduleName, long value);
        BatchResult send();          // 🔑 부를 때까지 아무것도 안 나간다
    private:
        friend class ParkingServer;
        Batch(ParkingServer* s, const std::string& d) : srv_(s), devid_(d) {}
        ParkingServer* srv_;
        std::string devid_;
        std::vector<std::pair<std::string, long> > items_;
    };
    Batch batch(const std::string& devid) { return Batch(this, devid); }

    // ── 명령 결과를 나중에 받는다 (콜백) ───────────────────────────────────
    //
    //   `send()` 는 **큐에 넣었다**까지만 말한다. 실제 결과는 장치가 자기 주기에 실어 보내고,
    //   도착할 때 이 함수가 불린다.
    //
    //   ```
    //   static void onResult(const CmdResult& r) {
    //       // r.kindName() : "성공" | "거절" | "무응답"
    //   }
    //   srv.onCommandResult(onResult);
    //   ```
    //
    // 🔴 **갈래 셋을 꼭 갈라 읽어라 — 고치는 곳이 다르다:**
    //   `OK`        장치가 받았고 콜백이 `true` 를 냈다
    //               ⚠ **물리적으로 그렇게 됐다가 아니다.** 차단봉이 걸려도 `true` 가 온다
    //   `REJECTED`  장치가 **답했고** 거절했다 → 값이나 등록을 고쳐라. 재시도는 뜻이 없다
    //   `NO_ANSWER` 🔴 **답이 없다** → 장치·링크 문제다. **콜백 로직을 고쳐도 안 낫는다**
    //
    // ⚠ 이 함수 안에서 오래 걸리는 일을 하지 마라 — 서버의 한 박자 안에서 불린다.
    void onCommandResult(CmdResultFn fn);

    // 🔑 **장치가 붙어서 등록까지 마쳤나.** 명령을 내기 전에 이것부터 봐라 —
    //   안 붙었으면 `send()` 가 `false` 를 내고 로그에 *"노드 `P1` 를 모른다"* 가 찍힌다.
    //   ⚠ **접속만으로는 부족하다.** 등록(`D`)이 끝나야 모듈 이름을 풀 수 있다.
    bool deviceReady(const std::string& devid) const;

    // 한 번에 묶을 수 있는 최대 건수. 🔴 **상수가 아니다** — 손잡이(`--down-cap`)를 따라간다.
    int maxPerBatch() const;

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
