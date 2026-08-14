// 호스트 테스트용 SoftwareSerial 스텁 + 가짜 ESP-01(AT).
// 실기 빌드에는 절대 들어가지 않는다 — arduino-cli 는 이 트리를 보지 않는다.
//
// 스케치가 내보낸 바이트를 AT 명령으로 해석해서 응답을 되돌려 준다:
//   AT+CIPSTART...  → "CONNECT"      (그래야 netOnline 이 켜진다)
//   AT+CIPSEND=<n>  → ">" 프롬프트    (안 주면 waitForPrompt 가 매번 실패해서
//                                      "전송이 굶은 상태"를 정상으로 착각하게 된다)
//   그 뒤 n 바이트   → 전선 라인으로 수집 + "SEND OK"
//   그 밖           → "OK"
#ifndef HOST_SOFTSERIAL_STUB_H
#define HOST_SOFTSERIAL_STUB_H

#include "Arduino.h"
#include <string>
#include <vector>
#include <deque>

class SoftwareSerial {
 public:
  SoftwareSerial(uint8_t, uint8_t) {}
  void begin(long) {}

  // 스케치 → ESP
  void print(const char* s) { if (s) for (const char* p = s; *p; ++p) txByte((uint8_t)*p); }
  void print(char c)          { txByte((uint8_t)c); }
  void print(int v)           { char b[24]; snprintf(b, sizeof b, "%d", v); print(b); }
  void print(unsigned int v)  { char b[24]; snprintf(b, sizeof b, "%u", v); print(b); }
  void print(unsigned long v) { char b[24]; snprintf(b, sizeof b, "%lu", v); print(b); }
  void write(uint8_t c)       { txByte(c); }
  void write(char c)          { txByte((uint8_t)c); }
  void write(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; i++) txByte(b[i]); }

  // ESP → 스케치
  int available() { return (int)rx.size(); }
  int read() { if (rx.empty()) return -1; char c = rx.front(); rx.pop_front(); return (unsigned char)c; }

  // 테스트가 서버인 척 밀어 넣는다 (+IPD 포장은 호출자가 한다)
  void deliver(const std::string& s) { for (char c : s) rx.push_back(c); }

  // 스케치가 실제로 전선에 내보낸 프로토콜 라인들 (LF 제거된 상태)
  std::vector<std::string> sentLines;
  bool traceAt = false;

 private:
  std::deque<char> rx;
  std::string atLine;          // 조립 중인 AT 명령
  size_t pendingPayload = 0;   // CIPSEND 뒤 받아야 할 바이트 수
  std::string payload;

  void reply(const std::string& s) { for (char c : s) rx.push_back(c); }

  void txByte(uint8_t b) {
    if (pendingPayload > 0) {
      payload += (char)b;
      if (--pendingPayload == 0) {
        std::string line = payload;
        if (!line.empty() && line[line.size() - 1] == '\n') line.erase(line.size() - 1);
        sentLines.push_back(line);
        payload.clear();
        reply("SEND OK\r\n");
      }
      return;
    }
    atLine += (char)b;
    if (atLine.size() >= 2 && atLine.substr(atLine.size() - 2) == "\r\n") {
      std::string cmd = atLine.substr(0, atLine.size() - 2);
      atLine.clear();
      if (traceAt) printf("        [esp<-] %s\n", cmd.c_str());
      if (cmd.compare(0, 11, "AT+CIPSEND=") == 0) {
        pendingPayload = (size_t)atol(cmd.c_str() + 11);
        reply("OK\r\n> ");
      } else if (cmd.compare(0, 12, "AT+CIPSTART=") == 0) {
        reply("OK\r\nCONNECT\r\n");
      } else {
        reply("OK\r\n");
      }
    }
  }
};

#endif  // HOST_SOFTSERIAL_STUB_H
