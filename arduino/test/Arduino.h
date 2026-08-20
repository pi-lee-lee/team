// 호스트(PC)에서 client.ino 의 로직을 실행해 보기 위한 Arduino 코어 스텁.
// 실기(AVR)에서는 절대 쓰이지 않는다 — arduino-cli 빌드에는 포함되지 않는 별도 트리다.
//
// 왜 있는가: 이 프로젝트에는 아두이노 보드가 없다. 그런데 client.ino 는 상태 기계 ·
// 줄 파서 · 멱등 캐시 · 3중 센서 소스를 들고 있어서 "읽어서 맞는지 보는" 것만으로는
// 회귀를 못 잡는다(실제로 백오프 언더플로 버그를 그렇게 놓칠 뻔했다).
// 그래서 스케치를 그대로 호스트에서 컴파일해 돌린다.
//
// ⚠ 한계: 시간과 하드웨어는 흉내다. millis() 는 호출마다 1ms 씩 자동으로 흐른다
//    (그래야 waitForPrompt 같은 스핀 루프가 끝난다). 따라서 **타이밍은 재현하지 않는다.**
//    검증 대상은 "무엇이 언제 순서대로 일어나는가"이지 "몇 ms 에 일어나는가"가 아니다.
#ifndef HOST_ARDUINO_STUB_H
#define HOST_ARDUINO_STUB_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <deque>

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
// 🔴 함수 포인터를 표에서 읽는다(모듈 표가 핸들러를 갖는다).
//   호스트는 주소 공간이 하나라 그냥 역참조다 — AVR 은 `lpm` 을 쓴다.
#define pgm_read_ptr(p)  (*(p))
#define F(x) (x)

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// 아날로그 핀 번호 (Uno 와 같은 매핑: A0 == 14)
enum { A0 = 14, A1, A2, A3, A4, A5 };

// ── 가상 시계 ──────────────────────────────────────────────────────────
extern unsigned long g_millis;
extern bool          g_clockAutoAdvance;

inline unsigned long millis() {
  if (g_clockAutoAdvance) g_millis += 1;   // 스핀 루프가 반드시 끝나게 한다
  return g_millis;
}
inline unsigned long micros() { return g_millis * 1000UL; }

// ── 가상 핀 ────────────────────────────────────────────────────────────
// 보드 내장 LED. 실기 Uno 와 같은 값이어야 한다 — 다르면 시험이 다른 핀을 본다.
#define LED_BUILTIN 13

extern uint8_t g_pinLevel[24];    // 테스트가 직접 세팅한다 (기본 HIGH = 풀업, 차 없음)
extern uint8_t g_pinMode[24];

inline void pinMode(uint8_t p, uint8_t m) { if (p < 24) g_pinMode[p] = m; }
inline int  digitalRead(uint8_t p)        { return (p < 24) ? g_pinLevel[p] : HIGH; }

// 🔓 초음파 예시가 쓰는 것들. **없으면 샘플 코드가 시험에서 컴파일조차 안 된다** —
//   그러면 샘플이 조용히 썩는다(오타·타입 오류를 아무도 못 본다).
inline void delayMicroseconds(unsigned int us) { (void)us; }
// `pulseIn` 은 시험이 값을 **주입**한다. 0 = 반향 없음(범위 밖).
//   왕복 µs → cm 은 /58 이므로 예: 2900 ≈ 50cm · 5800 ≈ 100cm
extern unsigned long g_pulseIn;
inline unsigned long pulseIn(uint8_t p, uint8_t v, unsigned long timeout) {
  (void)p; (void)v;
  return (g_pulseIn > timeout) ? 0UL : g_pulseIn;   // 타임아웃을 넘으면 0 — 실물과 같은 규약
}
inline void digitalWrite(uint8_t p, uint8_t v) { if (p < 24) g_pinLevel[p] = v; }
inline int  analogRead(uint8_t)           { return 512; }

inline void  randomSeed(unsigned long s)  { srand((unsigned)s); }
inline long  random(long lo, long hi)     { return (hi <= lo) ? lo : lo + (rand() % (hi - lo)); }
inline long  random(long hi)              { return random(0, hi); }

// ── Print / Serial ─────────────────────────────────────────────────────
// 출력은 테스트가 검사할 수 있도록 문자열로도 모아 둔다.
class HostSerial {
 public:
  std::string out;            // 지금까지 찍힌 전부
  std::deque<char> inbox;     // 테스트가 밀어 넣는 입력(콘솔 명령)
  bool echoToStdout = true;

  void begin(long) {}
  int  available() { return (int)inbox.size(); }
  int  read() { if (inbox.empty()) return -1; char c = inbox.front(); inbox.pop_front(); return (unsigned char)c; }
  void feed(const std::string& s) { for (char c : s) inbox.push_back(c); }

  void emit(const std::string& s) { out += s; if (echoToStdout) { fputs(s.c_str(), stdout); } }

  void print(const char* s)     { emit(s ? s : ""); }
  void print(char c)            { emit(std::string(1, c)); }
  void print(int v)             { char b[24]; snprintf(b, sizeof b, "%d", v); emit(b); }
  void print(unsigned int v)    { char b[24]; snprintf(b, sizeof b, "%u", v); emit(b); }
  void print(long v)            { char b[24]; snprintf(b, sizeof b, "%ld", v); emit(b); }
  void print(unsigned long v)   { char b[24]; snprintf(b, sizeof b, "%lu", v); emit(b); }

  void println()                { emit("\n"); }
  template <typename T> void println(T v) { print(v); emit("\n"); }
};

extern HostSerial Serial;

#endif  // HOST_ARDUINO_STUB_H
