/*
 * EspLink.h — ESP-01 WiFi 링크. **함수 셋만 알면 된다.**   (요청: REQ-0084)
 *
 *   #include "EspLink.h"            ← 스케치 폴더에 이 파일을 복사하고 이 한 줄
 *
 *   EspLink link(8, 7);             // (ESP TX→Uno 핀, Uno→ESP RX 핀)
 *   void setup() { link.begin("SSID","PASS","192.168.35.21", 9991); }
 *   void loop() {
 *     uint8_t buf[ESPLINK_PAYLOAD];
 *     int n = link.receive(buf, sizeof(buf));   // 없으면 0
 *     if (n > 0) { ... }
 *     link.send(payload, sizeof(payload));      // 못 보내면 false
 *   }
 *
 * 설치 과정이 없다. Arduino IDE 가 스케치 폴더의 파일을 같이 컴파일한다.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * ⚠ 호출이 얼마나 오래 붙잡는가 — **"논블로킹"이라고 쓰지 않는 이유**
 * ─────────────────────────────────────────────────────────────────────────
 * SoftwareSerial 의 `write()` 는 **비트뱅잉이고 인터럽트를 막고 돈다.** 보내는 동안은
 * 무조건 그 시간만큼 붙잡힌다 — 소프트웨어로 없앨 수 없다. 9600 8N1 에서 1바이트 = 1.04ms:
 *
 *     AT+CIPSEND=16\r\n  (16B) →  약 17ms   ← 피할 수 없음
 *     '>' 프롬프트 대기          →  최대 ESPLINK_PROMPT_MS
 *     페이로드 16B + LF  (17B) →  약 18ms   ← 피할 수 없음
 *
 * 그래서 이 라이브러리가 약속하는 것은 "안 막힌다"가 아니라 **"이만큼만 막힌다"** 다:
 *   · send()    : 정상 약 35ms · 최악 약 35ms + ESPLINK_PROMPT_MS
 *   · receive() : 약 1ms 미만(읽기만 한다). 단 아래 keepalive 가 걸리면 send() 와 같아진다
 *   · begin()   : **기다리지 않는다.** AT 한 줄만 쏘고 즉시 돌아온다(약 8ms)
 * **실측값은 README 에 적어 뒀다.** 모터 제어처럼 지연이 안전에 걸리는 노드는 그 숫자를 봐라.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * 이 파일이 대신 먹어 주는 함정들 (전부 우리가 실제로 물렸던 것이다)
 * ─────────────────────────────────────────────────────────────────────────
 *  · **문자열 공백** — 서버IP 앞 공백 한 칸이 `DNS Fail` 을 만들어 하루를 날렸다. begin() 이 지운다
 *  · **반이중 수신 손실** — 송신 직후 에코 첫 바이트가 99.7% 깨진다(0x53→0xA6). 고장이 아니라
 *    SoftwareSerial 의 특성이다. 송신 직후 드레인으로 삼킨다
 *  · **가변 길이 프레임** — 줄이 버퍼를 넘겨 통째로 버려지는 사고가 있었다. **고정 길이**로 바꿔
 *    그 고장 종류를 통째로 없앴다
 *  · AT 시퀀스 전부(RST·CWMODE·CWJAP·CIFSR·CIPMUX·CIPSTART·CIPSEND) 와 끊겼을 때의 재접속
 *
 * 안 되면: 이 include **위에** `#define ESPLINK_DEBUG 1` 을 넣고 시리얼(115200)을 보내라.
 */
#ifndef ESPLINK_H
#define ESPLINK_H

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────
// 전선 형식과 수신 구조체는 **socket-engineer 의 `server_wire.h` 가 유일한 정의처**다.
// 여기서 다시 정의하지 않는다 — **정의가 두 곳이면 반드시 어긋난다**(REQ-0084 루트 지시).
//
// 그 파일이 스케치 폴더에 있으면 자동으로 쓰고, 없으면 원시 바이트 모드로 컴파일된다.
// (Arduino IDE 는 스케치 폴더 밖 헤더를 자동 컴파일하지 않아 **복사가 불가피**하고,
//  복사하는 순간 갈라진다. 그래서 아래 배너에 `WIRE_VERSION` 을 찍어 **즉시 보이게** 한다.)
// ─────────────────────────────────────────────────────────────────────────
#if defined(__has_include)
#  if __has_include("server_wire.h")
#    include "server_wire.h"
#    define ESPLINK_HAVE_WIRE 1
#  endif
#endif
#ifndef ESPLINK_HAVE_WIRE
#  define ESPLINK_HAVE_WIRE 0
#endif

// ── 조절할 수 있는 것 (건드릴 일은 거의 없다) ────────────────────────────
#ifndef ESPLINK_DEBUG
#define ESPLINK_DEBUG 0        // 1 이면 내부 AT 대화가 전부 시리얼로 나온다
#endif
#ifndef ESPLINK_PAYLOAD
#define ESPLINK_PAYLOAD 64     // **송신 상한**(고정값 아님). §2.1 의 한 줄 64B 와 맞췄다
#endif
#ifndef ESPLINK_LINE
// **수신** 줄 상한. 하행 프레임은 ASCII 한 줄(`W,<rid>,<ch>,<8바이트>,<체크섬>` ≈ 20자)이라
// 32 면 넉넉하다. 넘치면 잘라서 넘기지 않고 그 줄을 버린다(잘린 줄을 파서에 주는 것이 더 나쁘다).
#define ESPLINK_LINE 32
#endif
#ifndef ESPLINK_PROMPT_MS
#define ESPLINK_PROMPT_MS 40   // '>' 대기 상한. ESP 로컬 응답이라 보통 수 ms 안에 온다
#endif
#ifndef ESPLINK_KEEPALIVE_MS
// ★★ **기본 꺼짐이다. 켜기 전에 이걸 읽어라.** 실기에서 물려서 끈 것이다.
//
// 원래 의도: 받기만 하는 노드가 아무것도 안 보내면 서버가 "죽었다"고 보고 소켓을 회수해
// 링크가 영원히 깜빡인다 — 조원 코드는 멀쩡한데 안 되는 최악의 형태다.
//
// **그런데 켜 놨더니 이런 일이 났다**(2026-08-16, nextgen client.ino 첫 판):
//     10:34:27  ←ARD K
//     10:34:27  ! 체크섬 불일치 — 버림
// 라이브러리가 만든 keepalive 바이트가 **응용 프로토콜에서는 쓰레기**였다. 서버가 정상적으로
// 거절했고, 그 결과 **버린줄 카운터만 1초에 한 번씩 올라갔다.**
//
// 교훈: **전송 계층은 응용이 무엇을 유효한 프레임으로 치는지 알 수 없다.** 그러니 내용을
// 지어내면 안 된다. 유휴를 채우는 것은 **응용의 몫**이다.
//   · 주기적으로 뭔가 보내는 노드(예: 1Hz 하트비트) → 그대로 두면 된다. 이 값은 0
//   · **받기만 하는 노드** → 응용이 자기 프로토콜의 유효한 프레임을 주기적으로 보내라.
//     그게 없으면 서버가 소켓을 회수한다는 것만 알고 있으면 된다.
#define ESPLINK_KEEPALIVE_MS 0
#endif
#ifndef ESPLINK_BUILD_ID
#define ESPLINK_BUILD_ID "?"   // 빌드 주입이 없으면 '?' — **모른다는 사실이 보이게** 한다
#endif

#if ESPLINK_DEBUG
  #define ESPLINK_LOG(x)   do { Serial.print(x); } while (0)
  #define ESPLINK_LOGLN(x) do { Serial.println(x); } while (0)
#else
  #define ESPLINK_LOG(x)   do {} while (0)
  #define ESPLINK_LOGLN(x) do {} while (0)
#endif

// ── 진단 출력 (REQ-0094) — `ESPLINK_DEBUG` 와 **다른 채널이다** ──────────────
// DEBUG 는 AT 대화를 **전부** 찍어서 소크에 못 쓴다(초당 1프레임 × 4시간 = 14,400줄).
// DIAG 는 **사건이 일어날 때만** 한 줄 찍는다. 그래서 **기본값이 켜짐**이다 —
// 관측 담당이 포트를 열었을 때 볼 것이 있어야 하고, 평소에는 조용해야 한다.
//
// 모든 줄에 `millis()` 를 접두로 붙인다. 캡처 쪽이 벽시계를 붙이므로 **둘을 맞추면
// 서버 로그와 3자 대조**가 된다(장치 millis ↔ 캡처 벽시계 ↔ 서버 벽시계).
#ifndef ESPLINK_DIAG
#define ESPLINK_DIAG 1
#endif
#if ESPLINK_DIAG
  #define ESPLINK_DIAG_AT()  do { Serial.print('['); Serial.print(millis()); Serial.print(F("] ")); } while (0)
  #define ESPLINK_DIAG1(a)         do { ESPLINK_DIAG_AT(); Serial.println(F(a)); } while (0)
  #define ESPLINK_DIAG2(a, v)      do { ESPLINK_DIAG_AT(); Serial.print(F(a)); Serial.println(v); } while (0)
#else
  #define ESPLINK_DIAG1(a)         do {} while (0)
  #define ESPLINK_DIAG2(a, v)      do {} while (0)
#endif

class EspLink {
public:
  EspLink(uint8_t espTxToUnoPin, uint8_t unoTxToEspPin)
    : _ser(espTxToUnoPin, unoTxToEspPin) {}

  // 기다리지 않는다. 접속은 send()/receive() 가 돌면서 뒤에서 진행된다.
  void begin(const char* ssid, const char* pass, const char* host, uint16_t port) {
    _ser.begin(9600);
    // ★ 공백을 여기서 한 번만 먹는다(쓸 때마다가 아니라). 그래야 아래 [CFG] 배너가
    //   **실제로 쓰일 값**을 찍게 되어 배너를 믿을 수 있다.
    trimCopy(_ssid, sizeof(_ssid), ssid, false);
    trimCopy(_pass, sizeof(_pass), pass, true);   // 비번은 지우면 알린다(뒤 공백이 유효할 수 있다)
    trimCopy(_host, sizeof(_host), host, false);  // IP·호스트명에 공백은 언제나 오류다
    _port = port;
    _state = ST_RST; _at = millis(); _wait = 300; _online = false;
#if ESPLINK_DEBUG
    Serial.print(F("[EspLink] build=[")); Serial.print(F(ESPLINK_BUILD_ID));
    Serial.print(F("] SSID=[")); Serial.print(_ssid);
    Serial.print(F("] SRV=[")); Serial.print(_host);
    Serial.print(F("]:")); Serial.println(_port);
    Serial.println(F("[EspLink] ↑ 대괄호 안에 공백이 보이면 그게 원인이다"));
    // ★ 전선 형식 판 번호. `server_wire.h` 를 스케치 폴더로 **복사**해야 하므로 원본과
    //   갈라질 수 있다 — 막을 수 없으니 **즉시 보이게** 한다(REQ-0084 루트 지시).
#if ESPLINK_HAVE_WIRE
    // ⚠ 이 값은 **찍기만 한다. 서버가 자동으로 대조하지 못한다** — 상행 프레임에 판 번호 필드가
    //   없어서다(socket-engineer 확인). 지금은 사람이 양쪽 로그를 눈으로 맞춰야 한다.
    //   "자동으로 잡힌다"고 믿지 마라. `D` 프레임이 들어오면 그때 공짜로 자동이 된다.
    Serial.print(F("[EspLink] wire=")); Serial.println(WIRE_VERSION);
#else
    Serial.println(F("[EspLink] ⚠ server_wire.h 없음 — 원시 줄 모드(파싱 없음)"));
#endif
#endif
  }

  // 준 만큼 그대로 보내고 LF 를 붙인다. 보냈으면 true.
  //
  // ⚠ **0 으로 패딩하지 않는다.** 처음엔 고정 길이로 패딩했는데, 그러면 텍스트 프레임
  //   (`S,12,0110000010,...,P1,33`)에 NUL 이 붙어 **서버 체크섬이 전부 깨진다.**
  //   고정 길이의 값어치는 **수신** 쪽 버퍼 초과를 없애는 것이고, 송신은 호출자가 만든
  //   줄을 그대로 내보내는 것이 맞다. `ESPLINK_PAYLOAD` 는 이제 **상한**이지 고정값이 아니다.
  bool send(const uint8_t* data, uint8_t len) {
    pump();
    if (!_online) return false;
    if (len == 0 || len > ESPLINK_PAYLOAD) return false;   // 상한을 넘으면 자르지 않고 거절한다
    return rawSend(data, len);
  }

  // 문자열 편의형 — `send((const uint8_t*)s, strlen(s))` 와 같다.
  bool send(const char* s) { return send((const uint8_t*)s, (uint8_t)strlen(s)); }

  // 도착한 프레임이 있으면 1, 없으면 0.
  //
  // ⚠ **파싱은 여기서 하지 않는다.** 줄 조립까지가 이 라이브러리 몫이고, 그 줄을 해석하는 것은
  //   `server_wire.h` 의 디코더다(REQ-0084 루트 지시: 정의가 두 곳이면 어긋난다).
  //   그 헤더가 스케치 폴더에 있으면 아래 구조체 형태가 켜지고, 없으면 원시 줄을 그대로 준다.
#if ESPLINK_HAVE_WIRE
  // ★ 권장 형태 — 받은 줄을 `server_wire.h` 의 디코더가 해석해 **의미가 드러나는 구조체**로 준다.
  //   반환 1 = 프레임이 도착했다. **해석에 성공했는지는 `msg.valid` 로 본다** — 둘은 다른 질문이다.
  //   (`valid==0` 이면 나머지 필드를 쓰면 안 된다. 디코더가 실패 시 out 을 memset 해 둔다.)
  //
  //   ⚠ 파싱을 여기서 짜지 않는다. 체크섬 범위·필드 순서를 양쪽이 각자 정하면 반드시 갈린다.
  //     정의처는 `server_wire.h` 하나다.
  int receive(WireMsg& msg) {
    char line[ESPLINK_LINE];
    int n = takeLine(line, sizeof(line));
    if (n <= 0) return 0;
    wire_decode(line, (uint16_t)n, &msg);
    return 1;
  }
#endif
  // 원시 줄이 필요할 때(디버깅·비표준 서버). 위와 같은 프레임을 문자열로 준다.
  int receive(char* line, uint8_t cap)    { return takeLine(line, cap); }
  int receive(uint8_t* buf, uint8_t cap)  { return takeLine((char*)buf, cap); }

private:
  enum { ST_RST, ST_CWMODE, ST_CWJAP, ST_CIFSR, ST_CIPMUX, ST_CIPSTART, ST_ONLINE };

  SoftwareSerial _ser;
  char     _ssid[33] = {0}, _pass[65] = {0}, _host[41] = {0};
  uint16_t _port = 0;
  uint8_t  _state = ST_RST;
  uint32_t _at = 0, _wait = 0, _lastSend = 0;
  bool     _online = false;
  uint8_t  _fail = 0;            // 연속 송신 실패 수 (3 이면 링크를 다시 세운다)
  uint32_t _reconnAt = 0;        // 재수립 시작 시각 (소요시간을 재려고)

  // 수신 프레임은 **ASCII 한 줄**이다(`W,<rid>,<ch>,<8바이트>,<체크섬>` — 형식은 server_wire.h).
  // 이진이 아니라 텍스트로 두는 것은 확정된 결정이다: 오늘 진단이 전부 시리얼 로그를
  // 눈으로 읽어서 나왔고, 이진이면 그게 불가능하다(REQ-0084 「이진 전송은 하지 않는다」).
  char     _rx[ESPLINK_LINE]; uint8_t _rxLen = 0; bool _rxReady = false;
  char     _line[48]; uint8_t _len = 0;      // AT 응답 줄
  int16_t  _need = -1; uint8_t _got = 0;     // +IPD 본문 바이트를 셀 때만 쓴다
#if ESPLINK_DEBUG
  uint16_t _badLen = 0;                      // 길이가 안 맞아 버린 프레임 수
#endif

  // 앞뒤 공백 제거. talk=true 면 지웠을 때 알린다(비번처럼 공백이 유효할 수 있는 값).
  static void trimCopy(char* dst, size_t cap, const char* src, bool talk) {
    if (!src) { dst[0] = 0; return; }
    const char* b = src;
    while (*b == ' ' || *b == '\t') b++;
    size_t n = strlen(b);
    while (n && (b[n-1] == ' ' || b[n-1] == '\t')) n--;
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, b, n); dst[n] = 0;
#if ESPLINK_DEBUG
    if (talk && strlen(src) != n) Serial.println(F("[EspLink] ⚠ 비밀번호 앞뒤 공백을 지웠다"));
#else
    (void)talk;
#endif
  }

  // 조립된 줄을 꺼내 준다. 없으면 0.
  int takeLine(char* out, uint8_t cap) {
    pump();
    if (!_rxReady) return 0;
    uint8_t n = _rxLen < (uint8_t)(cap - 1) ? _rxLen : (uint8_t)(cap - 1);
    memcpy(out, _rx, n); out[n] = 0;
    _rxReady = false; _rxLen = 0;
    return n;
  }

  void say(const __FlashStringHelper* s) { _ser.print(s); }

  void step(uint8_t st, uint32_t w) { _state = st; _at = millis(); _wait = w; }

  // 접속 상태기계 + 수신. send()/receive() 양쪽에서 불린다 —
  // 조원이 별도의 loop() 를 부를 필요가 없게 하려는 것이다(안 부르면 조용히 안 도는 함정 제거).
  void pump() {
    drain();
    if (_online) {
      // 받기만 하는 노드가 서버에게 죽은 것으로 보이지 않게 한다(위 KEEPALIVE 주석)
#if ESPLINK_KEEPALIVE_MS
      if (millis() - _lastSend >= (uint32_t)ESPLINK_KEEPALIVE_MS) {
        rawSend((const uint8_t*)"K", 1);   // 최소 keepalive 한 바이트
      }
#endif
      return;
    }
    if (millis() - _at < _wait) return;
    switch (_state) {
      case ST_RST:      say(F("AT+RST\r\n"));      step(ST_CWMODE,  2500); break;
      case ST_CWMODE:   say(F("AT+CWMODE=1\r\n")); step(ST_CWJAP,    800); break;
      case ST_CWJAP:
        _ser.print(F("AT+CWJAP=\"")); _ser.print(_ssid);
        _ser.print(F("\",\""));       _ser.print(_pass); _ser.print(F("\"\r\n"));
        step(ST_CIFSR, 30000);   // 이 펌웨어의 CWJAP 은 15초 넘게 걸린다(실측)
        break;
      case ST_CIFSR:    say(F("AT+CIFSR\r\n"));    step(ST_CIPMUX,  1500); break;
      case ST_CIPMUX:   say(F("AT+CIPMUX=0\r\n")); step(ST_CIPSTART, 800); break;
      case ST_CIPSTART:
        _ser.print(F("AT+CIPSTART=\"TCP\",\"")); _ser.print(_host);
        _ser.print(F("\","));                    _ser.print(_port); _ser.print(F("\r\n"));
        step(ST_RST, 8000);      // 답이 없으면 처음부터. 응답이 오면 아래 line() 이 온라인으로 올린다
        break;
      default: break;
    }
  }

  bool rawSend(const uint8_t* f, uint8_t len) {
    _ser.print(F("AT+CIPSEND="));
    _ser.print((unsigned)(len + 1));           // +1 = LF 도 전선에 나간다
    _ser.print(F("\r\n"));

    uint32_t t0 = millis(); bool ok = false;
    while (millis() - t0 < ESPLINK_PROMPT_MS) {
      while (_ser.available()) { if ((char)_ser.read() == '>') { ok = true; break; } }
      if (ok) break;
    }
    if (ok) {
      _ser.write(f, len);
      _ser.write('\n');
    } else {
      // 프롬프트를 놓쳤는데 ESP 는 약속한 길이를 기다리고 있다. 채워서 끝내지 않으면
      // 다음 명령이 페이로드로 먹혀 스트림이 통째로 어긋난다(실측으로 확인된 고장).
      for (uint8_t i = 0; i <= len; i++) _ser.write('#');
      ESPLINK_LOGLN(F("[EspLink] 프롬프트 놓침 → 더미로 스트림 복구"));
      ESPLINK_DIAG1("[AT] resync");   // 프롬프트를 놓쳐 스트림을 강제 복구했다
    }
    _lastSend = millis();
    // 송신 중에는 수신을 못 한다(반이중). 직후에 밀린 에코를 걷어낸다 —
    // 이때 첫 바이트가 깨져 있는 것이 정상이다. 여기서 버리므로 위로 새지 않는다.
    drain();

    // ★★ 연속 실패는 "링크가 죽었다"는 뜻이다 — 실측으로 물린 자리다.
    //   이게 없으면 소켓이 죽은 뒤 **영원히 프롬프트만 놓치며 스스로 복구하지 못한다**
    //   (초당 한 번씩 `프롬프트 놓침` 만 찍으며 무한 반복하는 것을 실기에서 봤다).
    //   ESP 가 `Unlink` 를 항상 내주지는 않으므로 **통보를 기다리면 안 된다.**
    //   3회로 정한 근거: 1~2회는 일시적 busy 로 흔하고, 3회면 약 3초라 서버의
    //   무프레임 판정(수 초)과 같은 시간대에 우리도 죽었다고 보게 된다.
    if (ok) { _fail = 0; }
    else if (++_fail >= 3) {
      _fail = 0;
      goOffline(F("[LINK] closed reason=send_fail_x3"));
    }
    return ok;
  }

  // 오프라인 확정 + 재수립 시작. **끊김을 판정하는 모든 경로가 여기를 지나야** 한다 —
  // 한 곳이라도 빠지면 그 경로의 끊김이 로그에 안 남고, 관측이 비는 것은 고장과 구별이 안 된다.
  void goOffline(const __FlashStringHelper* why) {
    _online = false;
    _reconnAt = millis();
    ESPLINK_DIAG_AT(); Serial.println(why);
    ESPLINK_DIAG1("[LINK] reconnect start");
    say(F("AT+CIPCLOSE\r\n"));        // 낡은 소켓을 먼저 닫는다. 안 닫으면 재접속이 거부된다
    step(ST_CIFSR, 500);              // 결합은 살아 있을 수 있으니 IP 확인부터
  }

  void drain() {
    while (_ser.available()) feed((char)_ser.read());
  }

  void feed(char c) {
    if (_need >= 0) {                       // +IPD 본문을 세는 중 — **줄로 모은다**
      // 종단 문자는 담지 않는다. 파서에게 넘길 때 깨끗한 문자열이라야 한다.
      if (c != '\r' && c != '\n' && _rxLen < ESPLINK_LINE - 1) _rx[_rxLen++] = c;
      if (++_got >= (uint8_t)_need) {
        _rx[_rxLen] = 0;
        if (_rxLen) _rxReady = true;
#if ESPLINK_DEBUG
        else { _badLen++; Serial.println(F("[EspLink] 빈 프레임 버림")); }
#endif
        _need = -1; _got = 0;
      }
      return;
    }
    if (c == '\r') return;
    if (c == '\n') { _line[_len] = 0; if (_len) line(_line); _len = 0; return; }
    if (_len < sizeof(_line) - 1) _line[_len++] = c;

    // `+IPD,<n>:` 과 `+IPD,<id>,<n>:` 둘 다 받는다. **첫 ':' 뒤가 본문**이라는 성질만 쓴다
    // (이 펌웨어가 어느 형식인지 확정된 적이 없다 — 둘 다 받는 것이 안전하다).
    if (c == ':' && strncmp(_line, "+IPD,", 5) == 0) {
      const char* p = _line + _len - 1;
      while (p > _line && p[-1] != ',') p--;
      _need = atoi(p); _got = 0; _len = 0;
      if (_need <= 0 || _need > 250) { _need = -1; }
    }
  }

  void line(const char* s) {
    ESPLINK_LOG(F("[AT] ")); ESPLINK_LOGLN(s);
    // ★ 펌웨어마다 접속 어휘가 다르다. **둘 다 받아야 한다** — 실측으로 물린 자리다:
    //   · 구형 ai-thinker : `Linked` / `Unlink`    ← 이 보드가 쓰는 말
    //   · ESP-AT 신형     : `CONNECT` / `CLOSED`
    //   처음에 `CONNECT` 만 봤더니 **접속은 되는데 영원히 오프라인**이었다.
    //   ⚠ `WIFI CONNECTED`(결합 알림)를 TCP 접속으로 오인하면 안 되므로 그것만 제외한다.
    if ((strstr(s, "Linked") || strstr(s, "CONNECT")) && !strstr(s, "WIFI")) {
      bool wasReconnect = !_online && _reconnAt;
      _online = true; _lastSend = millis();
      if (wasReconnect) { ESPLINK_DIAG2("[LINK] reconnect ok ", millis() - _reconnAt); _reconnAt = 0; }
      else              { ESPLINK_DIAG1("[LINK] online (first)"); }
      ESPLINK_LOGLN(F("[EspLink] online"));
    } else if (strstr(s, "Unlink") || strstr(s, "CLOSED") || strstr(s, "link is not valid")) {
      // ⚠ ESP 가 통보해 준 경우다. 통보가 항상 오지는 않으므로 위 send_fail_x3 경로도 필요하다.
      if (_online) goOffline(F("[LINK] closed reason=peer_closed"));
      else { _online = false; step(ST_CIFSR, 300); }
    } else if (strstr(s, "FAIL") && _state == ST_CIFSR) {
      _online = false; step(ST_RST, 2000);              // 결합 실패면 처음부터
      ESPLINK_DIAG1("[LINK] reconnect fail reason=cwjap_fail");
      _reconnAt = millis();                             // 다시 센다
    }
  }
};

#endif // ESPLINK_H
