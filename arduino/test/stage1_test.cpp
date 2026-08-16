// REQ-0116 1단계 검증 — 프롬프트 4상태 판정과 살아있음 불변식을 **보드 없이** 실행해 확인한다.
//
//   빌드/실행:  bash arduino/test/run_stage1.sh
//
// 왜 `host_test.cpp` 와 따로 두는가:
//   그쪽은 2026-08-14 판이라 **접속 사다리가 그 뒤로 크게 바뀌어(REQ-0051/0064/0092) 대부분
//   실패한다** — 고치기 전 판본으로 돌려도 62/138 만 통과한다(=하네스가 낡은 것이지 펌웨어
//   회귀가 아니다). 그 위에 검증을 얹으면 **깨진 계측기로 재는 셈**이라 여기서는
//   `sendLine()` 과 `statusTick()` 을 **직접 호출해** 순수 로직만 본다.
//
// 검증하지 않는 것: 타이밍의 실제 값(가짜 시계다), 실기 전기 특성, ESP 의 진짜 응답 지연.
// 검증하는 것: **어떤 응답에 어떤 판정을 내리는가**와 **그 결과 무엇이 바뀌는가**.

#include "Arduino.h"
#include "SoftwareSerial.h"

#include <string>
#include <vector>
#include <cstdio>

unsigned long g_millis = 0;
bool          g_clockAutoAdvance = true;
uint8_t       g_pinLevel[24];
uint8_t       g_pinMode[24];
HostSerial    Serial;

// AVR 전용 구문을 호스트에서 무해하게 만든다 (host_test.cpp 와 같은 이유)
#define __attribute__(x)
static uint8_t MCUSR = 0;
#define PORF  0
#define EXTRF 1
#define BORF  2
#define WDRF  3
#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif
uint8_t __heap_start = 0;

#ifndef SKETCH_PATH
#define SKETCH_PATH "../../조별과제샘플/client.ino"
#endif
#include SKETCH_PATH

// ── 테스트 유틸 ────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;
static void ok(bool cond, const char* what) {
  if (cond) { g_pass++; printf("  PASS  %s\n", what); }
  else      { g_fail++; printf("  FAIL  %s\n", what); }
}

// 각 시험을 같은 출발점에서 시작시킨다.
// `netOnline=true` 로 두는 이유: `sendLine()` 은 오프라인이면 즉시 false 로 빠진다.
static void arm(const char* refuse /* nullptr 이면 정상 프롬프트 */) {
  // ⚠ `reset()` 이 필수다. 앞 시험의 더미 `#` 들이 `\r\n` 없이 남아 다음 명령을 삼킨다
  //   — 스텁의 결함이 아니라 실기 스트림 엉킴의 정확한 재현이라 명시적으로 지운다.
  wifi.reset();
  wifi.refusePrompt = (refuse != nullptr);
  if (refuse) wifi.refuseReply = refuse;
  netOnline      = true;
  staleSocket    = false;
  sendFailStreak = 0;
  promptResyncs  = 0;
  stallBusy = stallTimeout = stallReject = 0;
  lastSendEndAt  = 0;
  lastTxOkAt     = millis();
  inSend         = false;
}

static const char* FRAME = "S,1,0000000000,0000000000,1,P1,";

int main() {
  printf("\n=== REQ-0116 1단계 — 프롬프트 4상태 & 살아있음 불변식 ===\n");

  // ── [1] 정상: '>' 를 받으면 성공하고 카운터가 0 으로 리셋된다 ──────────────
  printf("\n[1] 정상 프롬프트\n");
  arm(nullptr);
  sendFailStreak = 2;                       // 앞선 실패가 있었다고 두고
  bool r1 = sendLine(FRAME);
  ok(r1,                        "'>' 를 받으면 sendLine 이 true 를 돌려준다");
  ok(sendFailStreak == 0,       "성공하면 연속 실패 카운터가 0 으로 리셋된다");
  ok(wifi.sentLines.size() == 1,"프레임이 실제로 전선에 나갔다");
  ok(promptResyncs == 0,        "정상 경로에서는 더미를 넣지 않는다");

  // ── [2] busy: 세지 않는다 + 앞선 실패는 보존된다 + 더미 없음 ──────────────
  printf("\n[2] busy 응답 (ESP 가 '지금 바쁘다'고 명시적으로 답한 경우)\n");
  arm("busy s...\r\n");
  sendFailStreak = 2;                       // ★ 앞선 진짜 실패 2회가 있는 상태
  bool r2 = sendLine(FRAME);
  ok(!r2,                       "busy 면 sendLine 은 false 를 돌려준다(못 보냈다)");
  ok(sendFailStreak == 2,       "★ busy 는 카운터를 올리지 않는다");
  ok(sendFailStreak != 0,       "★ 그렇다고 0 으로 되돌리지도 않는다 — 앞선 실패가 보존된다");
  ok(promptResyncs == 0,        "★ busy 면 더미를 주입하지 않는다");
  ok(stallBusy == 1,            "진단 카운터에는 busy 로 기록된다");
  ok(netOnline,                 "busy 하나로 오프라인이 되지 않는다");

  // ── [3] busy 를 아무리 반복해도 연속 실패로 끊기지 않는다 ────────────────
  printf("\n[3] busy 반복 — 예전이라면 3회에 끊겼다\n");
  arm("busy s...\r\n");
  for (int i = 0; i < 10; i++) sendLine(FRAME);
  ok(sendFailStreak == 0,       "★ busy 10회로도 연속 실패 카운터가 오르지 않는다");
  ok(netOnline,                 "★ busy 10회로도 오프라인이 되지 않는다 (자해 중단)");

  // ── [4] ERROR: 여전히 센다 + 더미는 안 넣는다 ────────────────────────────
  printf("\n[4] ERROR 응답 (명령이 실제로 거부됨)\n");
  arm("ERROR\r\n");
  bool r4 = sendLine(FRAME);
  ok(!r4,                       "ERROR 면 false");
  ok(sendFailStreak == 1,       "★ ERROR 는 그대로 실패로 센다");
  ok(promptResyncs == 0,        "★ ERROR 면 더미를 주입하지 않는다");
  ok(stallReject == 1,          "진단 카운터에는 거부로 기록된다");

  // 대소문자 — 실측에서 `ERROR` 와 `Error` 가 둘 다 나왔다
  arm("Error\r\n");
  sendLine(FRAME);
  ok(sendFailStreak == 1,       "★ 소문자 섞인 'Error' 도 같은 판정을 받는다");

  // ── [5] 무응답: 더미를 넣고 실패로 센다 (기존 동작 유지) ──────────────────
  printf("\n[5] 무응답 (프롬프트도 오류도 안 옴) — 기존 동작이 유지되어야 한다\n");
  arm("");                                  // 아무 답도 주지 않는다
  bool r5 = sendLine(FRAME);
  ok(!r5,                       "무응답이면 false");
  ok(sendFailStreak == 1,       "무응답은 실패로 센다");
  ok(promptResyncs == 1,        "★ 무응답에서는 더미를 그대로 넣는다 (ESP 가 데이터 모드일 수 있다)");
  ok(stallTimeout == 1,         "진단 카운터에는 무응답으로 기록된다");

  // ── [6] 무응답 3회면 예전처럼 오프라인이 된다 (회복 경로 보존) ────────────
  printf("\n[6] 무응답 3회 — 연속 실패 판정은 살아 있어야 한다\n");
  arm("");
  for (int i = 0; i < 3; i++) sendLine(FRAME);
  ok(!netOnline,                "★ 무응답 3연속이면 오프라인으로 간다 (기존 회복 경로 유지)");
  ok(staleSocket,               "낡은 소켓 의심이 서고 CIPCLOSE 사다리로 간다");

  // ── [7] 살아있음 불변식 — 10초 전에는 발동하지 않는다 ────────────────────
  printf("\n[7] 살아있음 불변식 (TX_STALL_MS=10000)\n");
  arm(nullptr);
  lastStatusAt = millis();                  // 하트비트가 뜨지 않게 해서 송신을 격리한다
  changePending = false;
  lastTxOkAt = millis() - 9000;             // 9초 — 아직 아니다
  statusTick(millis());
  ok(netOnline,                 "★ 9초 무송신으로는 발동하지 않는다");

  // ── [8] 10초를 넘기면 이유를 묻지 않고 링크를 다시 세운다 ────────────────
  lastStatusAt = millis();
  changePending = false;
  stallBusy = 7;                            // busy 만 쌓인 상황을 흉내
  lastTxOkAt = millis() - 10001;            // 10초 초과
  statusTick(millis());
  ok(!netOnline,                "★ 10초 무송신이면 발동한다 — busy 만 쌓여도 빠져나온다");
  ok(staleSocket,               "★ 복구 사다리(CIPCLOSE)로 들어간다");
  ok(stallBusy == 0,            "발동하면서 진단 카운터를 비운다");

  // ── [9] 성공 송신이 불변식 시계를 되돌린다 ───────────────────────────────
  printf("\n[9] 성공하면 불변식 시계가 갱신된다\n");
  arm(nullptr);
  lastTxOkAt = millis() - 9500;             // 곧 발동할 뻔한 상태
  sendLine(FRAME);                          // 성공 송신
  unsigned long since = millis() - lastTxOkAt;
  ok(since < 1000,              "★ 성공하면 lastTxOkAt 이 지금으로 갱신된다 (정지 오판 방지)");

  printf("\n=== 결과: %d PASS / %d FAIL ===\n\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
