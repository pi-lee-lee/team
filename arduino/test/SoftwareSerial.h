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

  // ★ 죽은 링크 흉내 (REQ-0049): true 면 CIPSEND 에 '>' 를 주지 않고 오류만 돌려준다.
  //   실기 증상(seq 고정 = waitForPrompt 매번 타임아웃)을 재현하는 스위치다.
  bool refusePrompt = false;
  std::string refuseReply = "ERROR\r\n";   // 실제 문구는 미확정 — 필요하면 바꿔 시험한다

  // ★ 낡은 소켓을 붙든 모듈 흉내 (REQ-0051): 실기의 무한 루프 증상을 그대로 만든다.
  //   true 면 CIPSTART 에 ALREADY CONNECTED 로 답하고, **CIPCLOSE 도 안 먹는다**(CLOSED 안 옴).
  //   AT+RST 를 받으면 모듈이 초기화되므로 이 상태가 풀린다 — 사다리 최상층이 실제로 듣는지 본다.
  bool stickySocket = false;

  // 보낸 AT 명령 기록 — 시험이 "CIPCLOSE 가 나갔는가"를 직접 확인할 수 있어야 한다
  std::vector<std::string> atLog;
  size_t countAt(const std::string& needle) const {
    size_t n = 0;
    for (const std::string& c : atLog) if (c.find(needle) != std::string::npos) n++;
    return n;
  }

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
      atLog.push_back(cmd);
      if (cmd.compare(0, 11, "AT+CIPSEND=") == 0) {
        if (refusePrompt) {
          reply(refuseReply);          // '>' 를 주지 않는다 → 스케치의 waitForPrompt 가 타임아웃
        } else {
          pendingPayload = (size_t)atol(cmd.c_str() + 11);
          reply("OK\r\n> ");
        }
      } else if (cmd.compare(0, 12, "AT+CIPSTART=") == 0) {
        // 낡은 소켓을 붙들고 있으면 새 연결을 만들지 않고 ALREADY CONNECTED 만 돌려준다
        reply(stickySocket ? "ALREADY CONNECTED\r\n" : "OK\r\nCONNECT\r\n");
      } else if (cmd == "AT+CIPCLOSE") {
        // 꼬인 모듈은 로컬 명령인 CIPCLOSE 조차 처리하지 못한다 → CLOSED 가 안 온다
        reply(stickySocket ? "ERROR\r\n" : "CLOSED\r\nOK\r\n");
      } else if (cmd == "AT+RST") {
        stickySocket = false;          // 모듈 초기화 = 낡은 소켓 상태가 풀린다
        reply("OK\r\nready\r\n");
      } else {
        reply("OK\r\n");
      }
    }
  }
};

#endif  // HOST_SOFTSERIAL_STUB_H
