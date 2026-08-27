// rxline_test.cpp — `ardu_multi/parking_p1/RxLine.h` 의 **줄 조립을 호스트에서 돌린다**.
//
// 왜 있나 — 그 코드는 링버퍼·타이머·계수기 전역에 얽혀 있어 순수 함수로 못 떼어 낸다.
// 그래서 **떼어 내지 않고**, 그것이 기대하는 전역을 여기서 채워 그대로 `#include` 한다.
// (`cpp/winparse/` 가 `_WIN32` 갈래에 쓴 것과 같은 요령이다 — 안 도는 코드를 돌게 만든다)
//
// 🔴 **원본을 한 줄도 안 고친다.** `ardu_multi/**` 는 arduino-engineer 소유다.
//
// 돌리는 법:
//   c++ -std=c++17 -Wall -Wextra -o /tmp/rxline_test cpp/ardulink/rxline_test.cpp && /tmp/rxline_test
//
// 🔴 **DEBUG 두 판을 다 돌린다.** `-DDEBUG=1` 로 한 번 더 빌드해라:
//   c++ -std=c++17 -Wall -Wextra -DDEBUG=1 -o /tmp/rxline_dbg cpp/ardulink/rxline_test.cpp
//   실기에서 굽는 판이 `DEBUG=1` 이면 **그 판을 안 재고 0 판만 재는 것은 다른 코드를 재는 것**이다
//   (`feedRxChar` 의 넘침 갈래 안에 `#if DEBUG` 블록이 통째로 들어 있다).
//
// 실측 2026-08-25 : DEBUG=0 → 12/0 · DEBUG=1 → 14/0 · **두 판의 조립 결과가 한 줄도 다르지 않다**
//   (DEBUG=1 에서만 [DROP-OVF] 출력 유무를 양성·음성으로 확인한다)
//
// ⚠ 이 하니스가 보는 것은 **줄 조립 한 계층뿐**이다. `+IPD` 헤더 해석·소프트시리얼 링·
//   타이머는 안 본다. 무엇을 못 보는지는 마지막 §"못 보는 축" 에 적었다.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

// ── RxLine.h 가 기대하는 환경 ────────────────────────────────────────────────
// 이름과 타입은 원본 선언 그대로다(RxBuf.h · Counters.h · Session.h 에서 읽었다).
// 하나라도 타입이 다르면 그 자리에서 컴파일이 깨지므로, **컴파일 성공 자체가 대조**다.
#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
// Arduino 대역. `F()` 는 PROGMEM 리터럴 매크로라 호스트에서는 항등이다.
#define F(x) (x)
static const char HEXD[] = "0123456789ABCDEF";
static unsigned long dbgRxBytes = 0;
static std::string g_serial;   // 시리얼로 나간 것. **버린 줄이 보이는가**를 값으로 본다
struct FakeSerial {
  void print(const char* s) { g_serial += s; }
  void print(char c) { g_serial += c; }
  void print(unsigned long v) { g_serial += std::to_string(v); }
  void print(unsigned int v) { g_serial += std::to_string(v); }
  void print(int v) { g_serial += std::to_string(v); }
  void print(uint8_t v) { g_serial += std::to_string((unsigned)v); }
  void println(const char* s) { g_serial += s; g_serial += '\n'; }
  void println(char c) { g_serial += c; g_serial += '\n'; }
};
static FakeSerial Serial;
#endif

static uint32_t g_millis = 0;
static uint32_t millis() { return g_millis; }

static const uint8_t RX_CAP = 96;
static char rxLine[RX_CAP];
static char workLine[RX_CAP];
static char pendLine[RX_CAP];
static uint8_t rxLen = 0;
static bool rxOverflow = false;
static bool pendReady = false;
static bool inSend = false;
static uint16_t rxLineOff = 0;
static uint16_t workLineOff = 0;
static uint16_t pendLineOff = 0;
static uint32_t slotStart = 0;
static uint8_t curTxLen = 0;
static const uint8_t RXBUF_THRESHOLD = 64;
static uint16_t pendFills = 0;
static uint16_t pendFillsBig = 0;
static uint16_t pendDrops = 0;
static uint16_t pendDropsBig = 0;
static uint16_t okLostByLine = 0;
static uint16_t okLostBig = 0;
static bool awaitingSendOk = false;
static bool sendOkT1Passed = false;
static uint8_t sendOkMatch = 0;
static uint16_t sendOkByStream = 0;
static uint16_t okStreamBig = 0;

// 조립이 끝난 줄이 여기로 온다. 원본은 `handleLine(workLine)` 을 부른다.
static std::vector<std::string> g_lines;
static void handleLine(char* s) { g_lines.push_back(std::string(s)); }

// SoftwareSerial 대역. **바이트 큐 하나**다 — `espRead()` 를 그대로 돌리기 위한 것이고,
// 이것이 있어야 `feedRxChar` 를 직접 부르지 않고 **실제 펌프 경로**로 시험할 수 있다.
// ⚠ `overflow()` 는 실물처럼 **읽으면 지워진다**(라이브러리 성질. 원본 주석이 그렇게 적었다).
//   하니스가 대상보다 관대하면 결함을 숨긴다 — 그래서 이 성질을 흉내낸다.
static uint16_t ssOverflows = 0;
struct FakeSoftSerial {
  std::string q;
  size_t pos = 0;
  bool ovf = false;
  int available() const { return static_cast<int>(q.size() - pos); }
  int read() { return pos < q.size() ? static_cast<unsigned char>(q[pos++]) : -1; }
  bool overflow() { const bool r = ovf; ovf = false; return r; }  // 읽으면 지워진다
  void push(const std::string& b) { q += b; }
  void clear() { q.clear(); pos = 0; ovf = false; }
};
static FakeSoftSerial wifi;

// ── 복구 사다리 대역 ────────────────────────────────────────────────────────
// `RxLine.h` 는 줄 조립만 있는 파일이 아니다 — 뒤쪽에 복구 사다리 조치부가 같이 있다.
// 그 부분은 **이 하니스의 시험 대상이 아니지만**, 같은 파일이라 컴파일은 돼야 한다.
// 값은 원본과 같게 맞춘다(틀린 값을 넣으면 나중에 그 부분을 시험할 때 조용히 거짓이 된다).
enum { NET_RST = 0, NET_CWMODE, NET_CWJAP, NET_CIFSR, NET_CIPMUX, NET_CIPSTART,
       NET_CIPCLOSE, NET_CWQAP, NET_GMR };
enum { RUNG_MEASURE = 0, RUNG_RESYNC, RUNG_CWQAP, RUNG_SOFTRST, RUNG_HWRST,
       RUNG_POWER, RUNG_MAX = RUNG_POWER };
static const uint16_t RUNG_BACKOFF_MS[] = { 0, 0, 5000, 15000, 30000, 60000 };
static uint8_t rung = 0;
static bool espRstHeld = false;
static unsigned long espRstReleaseAt = 0;
static bool netHasIp = false;
static uint8_t cifsrTries = 0;
static uint8_t g_netStep = 0;
static void netAdvance(uint8_t step, uint16_t) { g_netStep = step; }
static const uint8_t RUNG_LIMIT[] = { 1, 2, 2, 2, 2, 255 };
static bool ladderEverFailed = false;
static unsigned long lastLadderFailAt = 0;
static unsigned long assocAt = 0;
static uint8_t rungFails = 0;
static uint8_t cwjapFails = 0;
static bool lapDone = false;
#if DEBUG
static unsigned long cwjapSentAt = 0;   // DEBUG 출력에서만 쓴다
#endif

#include "../../조별과제샘플/ardu_multi/parking_p1/RxLine.h"

// ── 시험 뼈대 ───────────────────────────────────────────────────────────────
static int g_pass = 0;
static int g_fail = 0;

static void reset_rx() {
  rxLen = 0;
  rxOverflow = false;
  pendReady = false;
  inSend = false;
  sendOkMatch = 0;
  awaitingSendOk = false;
  g_lines.clear();
  wifi.clear();
}

static void check(bool ok, const char* what) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("  ✗ %s\n", what);
  }
}

// 바이트열을 chunk 크기로 쪼개 **큐에 넣고 `espRead()` 를 돌린다**.
// 🔑 `feedRxChar` 를 직접 부르지 않는 것이 요점이다 — 실기에서 도는 경로는 `espRead()` 이고,
//    조각 경계는 "큐에 얼마가 들어와 있을 때 espRead 가 불렸나"로 생긴다. 그것을 재현한다.
static void feed(const std::string& bytes, int chunk) {
  const size_t step = chunk <= 0 ? bytes.size() : static_cast<size_t>(chunk);
  for (size_t i = 0; i < bytes.size(); i += step) {
    wifi.push(bytes.substr(i, std::min(step, bytes.size() - i)));
    espRead();
  }
}

// 장치·서버가 쓰는 것과 같은 규칙: 첫 바이트부터 체크섬 앞 쉼표까지 XOR, 대문자 2자리.
// ⚠ 이 하니스는 **조립**을 보는 것이라 체크섬은 "잘린 줄이 걸리는가"를 보이는 데만 쓴다.
static std::string ck(const std::string& prefix) {
  unsigned char x = 0;
  for (size_t i = 0; i < prefix.size(); ++i) x ^= static_cast<unsigned char>(prefix[i]);
  char b[3];
  std::snprintf(b, sizeof(b), "%02X", x);
  return std::string(b);
}
static std::string line_of(const std::string& prefix) { return prefix + ck(prefix); }

int main() {
  std::printf("RxLine 줄 조립 하니스 — RX_CAP=%u\n\n", static_cast<unsigned>(RX_CAP));

  // ── ⓪ 하니스 자신이 실패할 수 있는가 (음성 대조의 대조) ────────────────────
  // ★ 이것부터 한다. 통과만 하는 시험은 통과만 한다 —
  //   틀린 기대를 넣었을 때 ✗ 가 나오는 것을 눈으로 확인하고 시작한다.
  {
    reset_rx();
    feed("A,1,B\n", 0);
    const bool wrong_on_purpose = (g_lines.size() == 2);  // 실제로는 1이다
    std::printf("⓪ 하니스 자가 대조 — 일부러 틀린 기대(줄 2개)를 넣는다: %s\n",
                wrong_on_purpose ? "🔴 참이 됐다(하니스가 고장났다)" : "✅ 거짓 — 하니스가 판별한다");
    check(!wrong_on_purpose, "하니스가 틀린 기대를 참으로 만든다");
  }

  // ── ① 조각 경계가 달라도 같은 줄이 나오나 ─────────────────────────────────
  // `+IPD` 는 소프트시리얼에서 임의 크기로 쪼개져 온다. 조립이 조각 경계에 의존하면
  // **실기에서만, 그것도 가끔** 깨진다 — 호스트에서 잡아야 하는 대표 축이다.
  {
    const std::string a = line_of("G,7,3,1,");
    const std::string b = line_of("R,12,A1,kim,");
    const std::string c = line_of("Q,");
    const std::string stream = a + "\n" + b + "\n" + c + "\n";

    std::vector<std::string> golden;
    reset_rx();
    feed(stream, 0);
    golden = g_lines;
    std::printf("① 통째로 먹인 결과: %zu줄", golden.size());
    for (size_t i = 0; i < golden.size(); ++i) std::printf(" [%s]", golden[i].c_str());
    std::printf("\n");
    check(golden.size() == 3 && golden[0] == a && golden[1] == b && golden[2] == c,
          "통째 입력에서 세 줄이 그대로 나오지 않는다");

    // 1바이트부터 전체 길이까지 **모든 조각 크기**로 같은 결과가 나와야 한다.
    int split_fail = 0;
    for (int chunk = 1; chunk <= static_cast<int>(stream.size()); ++chunk) {
      reset_rx();
      feed(stream, chunk);
      if (g_lines != golden) {
        ++split_fail;
        if (split_fail <= 3) std::printf("  ✗ chunk=%d 에서 결과가 다르다\n", chunk);
      }
    }
    std::printf("① 조각 크기 1~%zu 전수: 다른 결과 %d건\n", stream.size(), split_fail);
    check(split_fail == 0, "조각 경계에 따라 조립 결과가 달라진다");
  }

  // ── ② 한 조각에 줄이 둘이면 둘 다 나오나 (등록 배치가 그 모양이다) ─────────
  {
    const std::string a = line_of("C,13,A2,");
    const std::string b = line_of("M,15,");
    reset_rx();
    feed(a + "\n" + b + "\n", 0);   // 조각 하나에 두 줄
    std::printf("② 한 조각에 두 줄 → %zu줄 나왔다\n", g_lines.size());
    check(g_lines.size() == 2 && g_lines[0] == a && g_lines[1] == b,
          "한 조각 안의 둘째 줄이 사라진다");
  }

  // ── ③ RX_CAP 초과 — 버려지나 · 계수기가 오르나 · **다음 줄이 복구되나** ────
  {
    reset_rx();
    const uint16_t before_pend = pendDrops;
    std::string huge(RX_CAP + 40, 'X');       // 96 을 확실히 넘긴다
    const std::string next = line_of("Q,");
    feed(huge + "\n" + next + "\n", 0);
    std::printf("③ %zu바이트 줄 뒤에 정상 줄 → 나온 줄 %zu개", huge.size(), g_lines.size());
    for (size_t i = 0; i < g_lines.size(); ++i) std::printf(" [%s]", g_lines[i].c_str());
    std::printf("\n");
    check(g_lines.size() == 1 && g_lines[0] == next,
          "넘친 뒤 다음 줄이 복구되지 않는다(또는 넘친 줄이 통과했다)");

    // 🔴 여기가 이 하니스의 핵심 발견 자리다 — 계수기를 실제로 세어 본다.
    std::printf("③ 계수기 — pendDrops %u→%u · okLostByLine %u · sendOkByStream %u\n",
                static_cast<unsigned>(before_pend), static_cast<unsigned>(pendDrops),
                static_cast<unsigned>(okLostByLine), static_cast<unsigned>(sendOkByStream));
    std::printf("   🔴 줄 넘침(rxOverflow) 전용 계수기는 **소스에 없다** — DEBUG 출력뿐이다\n");
#if DEBUG
    // 🔴 **오늘 굽는 판이 DEBUG=1 이면 이 줄이 유일한 관측점이다.** 실제로 나오는지 값으로 본다.
    const bool drop_seen = g_serial.find("[DROP-OVF]") != std::string::npos;
    std::printf("③ DEBUG 판 — 시리얼에 [DROP-OVF] %s\n", drop_seen ? "✅ 나온다" : "🔴 안 나온다");
    check(drop_seen, "DEBUG=1 인데 [DROP-OVF] 가 안 찍힌다(그러면 넘침이 완전히 안 보인다)");
    // 음성 대조 — 넘치지 않은 정상 줄에서는 **안 나와야** 한다. 아니면 위 ✅ 는 무의미하다
    g_serial.clear();
    reset_rx();
    feed(line_of("Q,") + "\n", 0);
    const bool false_alarm = g_serial.find("[DROP-OVF]") != std::string::npos;
    std::printf("③ DEBUG 판 음성대조 — 정상 줄에서 [DROP-OVF] %s\n",
                false_alarm ? "🔴 나온다(오탐)" : "✅ 안 나온다");
    check(!false_alarm, "정상 줄에서도 [DROP-OVF] 가 찍힌다");
#endif
  }

  // ── ④ 음성 대조 — 안 이어졌다면 잘린 줄이 되어 체크섬에서 걸려야 한다 ──────
  {
    const std::string good = line_of("R,12,A1,kim,");
    // 조립이 조각을 안 이었다고 가정한 모양: 앞 조각만으로 줄이 끝난 경우
    const std::string truncated = good.substr(0, good.size() / 2);
    const size_t cut = truncated.rfind(',');
    const bool would_be_caught =
        (cut == std::string::npos) || (ck(truncated.substr(0, cut + 1)) != truncated.substr(cut + 1));
    std::printf("④ 잘린 줄 \"%s\" → 체크섬 검사에서 %s\n", truncated.c_str(),
                would_be_caught ? "✅ 걸린다" : "🔴 통과해 버린다");
    check(would_be_caught, "잘린 줄이 체크섬을 통과한다");

    // 그리고 **정상 줄은 통과해야** 한다 — 안 그러면 위 ✅ 가 "무조건 걸린다"일 뿐이다.
    const size_t gcut = good.rfind(',');
    const bool good_ok = ck(good.substr(0, gcut + 1)) == good.substr(gcut + 1);
    std::printf("④ 정상 줄 \"%s\" → %s\n", good.c_str(), good_ok ? "✅ 통과" : "🔴 거절");
    check(good_ok, "정상 줄까지 체크섬에서 거절된다(검사가 무조건 거절하고 있다)");
  }

  // ── ⑤ 부수 규칙 — CR 은 버리고, 빈 줄은 handleLine 을 안 부른다 ────────────
  {
    reset_rx();
    feed("Q,7D\r\n", 0);
    check(g_lines.size() == 1 && g_lines[0] == "Q,7D", "CR 이 줄에 섞여 들어간다");
    reset_rx();
    feed("\n\n\n", 0);
    std::printf("⑤ CR 제거 확인 · 빈 줄 %zu회 전달(0이어야 한다)\n", g_lines.size());
    check(g_lines.empty(), "빈 줄이 handleLine 까지 간다");
  }

  // ── ⑥ inSend 중에는 줄이 미뤄지고, **둘째 줄부터 버려진다** ────────────────
  // 원본 주석이 그렇게 적혀 있다(pendLine 깊이 1). 그 성질이 실제로 그런지 값으로 본다.
  {
    reset_rx();
    inSend = true;
    const std::string a = line_of("C,13,A2,");
    const std::string b = line_of("M,15,");
    const uint16_t d0 = pendDrops;
    const uint16_t f0 = pendFills;
    feed(a + "\n" + b + "\n", 0);
    std::printf("⑥ inSend 중 두 줄 → handleLine %zu회 · pendFills +%u · pendDrops +%u · pendLine=[%s]\n",
                g_lines.size(), static_cast<unsigned>(pendFills - f0),
                static_cast<unsigned>(pendDrops - d0), pendLine);
    check(g_lines.empty(), "inSend 중인데 handleLine 이 불렸다");
    check(pendFills - f0 == 1 && pendDrops - d0 == 1, "pendLine 깊이 1 성질이 안 보인다");
    check(std::string(pendLine) == a, "미뤄진 줄이 첫 줄이 아니다");
    inSend = false;
  }

  std::printf("\n집계: 통과 %d · 실패 %d\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
