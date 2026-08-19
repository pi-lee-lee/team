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
#define SKETCH_PATH "../../조별과제샘플/ardu/client.ino"
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
// ★ 하행 프레임을 **실제 체크섬 함수로** 만들어 먹인다.
//   손으로 `00` 을 적으면 `verify_line` 단계에서 걸려 `handleLine` 이 조용히 버린다 —
//   그러면 시험이 "ACK 가 안 나갔다"를 코드 결함으로 오독한다. (2026-08-17 실제로 밟았다)
static void feedDown(const char* bodyEndingWithComma) {
  char buf[40];
  int n = snprintf(buf, sizeof(buf), "%s", bodyEndingWithComma);
  appendChecksum(buf, (uint8_t)n);
  handleLine(buf);
}

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
  // ★ 2단계 상태도 반드시 여기서 지운다.
  //   빠뜨렸더니 **[1] 의 성공이 [2]~[7] 로 새어** 이후 시험이 전부 "SEND OK 대기 중"으로
  //   건너뛰어졌고 9건이 무더기로 실패했다. 원장 §4.5 가 경고한 시험 간 격리 함정 그대로다.
  //   ⚠ 새 전역 상태를 추가하면 **여기도 같이 늘려라.** 안 그러면 다음 사람이 같은 곳에서 넘어진다.
  awaitingSendOk = false;
  sendOkWaitFrom = 0;
  sendSkips = sendOkTimeouts = sendFails = 0;
  espResets = 0;
  // ★ 2026-08-17 신설 상태 — 위 주석이 시키는 대로 여기 같이 넣는다.
  sendOkT1Passed = false;    // 안 걸면 앞 시험의 걸쇠가 새어 `okto` 가 안 세진다
  sendOkGiveups  = 0;
  cifsrRefused   = false;
  ackQ.resetStats();         // ★ REQ-0204 — drops·stale 을 한 번에
  ackQ.clearQueue();
  ssOverflows    = 0;
  sendOkMatch    = 0;        // ★ REQ-0218
  sendOkByStream = 0;
  pendDrops      = 0;        // ★ REQ-0167 — 여기도 같이 늘린다(위 주석이 시키는 대로)
  slotSent       = false;
  cksumNg        = 0;        // ★ REQ-0174
  rxLen          = 0;
  pendReady      = false;
  rxLineOff = workLineOff = pendLineOff = 0;
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

  // ── [8b] 🔴 기준 시각이 **미래**여도 발동하면 안 된다 ─────────────────────
  // 2026-08-17 실기에서 터진 결함의 회귀 시험이다(원장 §8.7).
  //   `loop()` 은 `now = millis()` 를 맨 위에서 한 번만 뜨는데, 그 뒤 `drainPending()` 이
  //   하행 ACK 를 보내며 `lastTxOkAt` 을 **더 나중 시각**으로 갱신한다(실측 약 64ms).
  //   그러면 경과가 음수인데 unsigned 로 읽어 `2^32-64`(49.7일)가 되고 **멀쩡한 링크를 끊었다.**
  //   실제 로그: `[NET] ★ 정지 감지: 4294967232ms … → 링크를 다시 세운다`
  // ⚠ 이 시험은 **고치기 전 판본에서 반드시 실패해야 한다.** 통과하면 시험이 헛돈 것이다.
  printf("\n[8b] 미래 기준 시각 — 하행 ACK 뒤에 스스로 끊지 않는다 (언더플로 회귀)\n");
  arm(nullptr);
  lastStatusAt = millis();
  changePending = false;
  lastTxOkAt = (uint32_t)millis() + 64;     // ACK 송신에 걸린 만큼 now 보다 미래
  statusTick(millis());
  ok(netOnline,                 "★★ 기준 시각이 64ms 미래여도 정지 감지가 발동하지 않는다");
  ok(!staleSocket,              "★ 복구 사다리로도 들어가지 않는다");

  // ── [8c] 🔴 C-2 경로 — **하행 없이 `CONNECT` 만으로도** 같은 언더플로가 가능하다 ────
  // 위 [8b] 는 `lastTxOkAt` 을 직접 미래로 놓았지만, 실제 갱신 경로는 셋이다(원장 §8.7-5).
  //   :1534 sendLine ← drainPending(하행 ACK)  ·  :2148/:2160 espRead(CONNECT 처리)
  // 뒤 둘은 `statusTick` 보다 **앞에서** 돌므로 **하행이 전혀 없어도** 발동할 수 있다.
  // 여기서는 합성 대입이 아니라 **진짜 `CONNECT` 줄을 먹여** 그 경로를 태운다.
  printf("\n[8c] CONNECT 직후 — 하행 없이도 스스로 끊지 않는다 (C-2 경로)\n");
  arm(nullptr);
  netOnline    = false;                     // 아직 안 붙은 상태에서 시작
  lastStatusAt = millis();
  changePending = false;
  {
    unsigned long loopNow = millis();       // loop() 맨 위 스냅샷 (이 뒤로 시계가 흐른다)
    char cl[] = "CONNECT";
    handleLine(cl);                         // :2160 에서 lastTxOkAt = millis() → loopNow 보다 미래
    statusTick(loopNow);                    // 낡은 now 로 검사 — 여기서 끊으면 결함이다
  }
  ok(netOnline,                 "★★ CONNECT 처리가 시계를 앞세워도 즉시 끊지 않는다");

  // ── [9] 성공 송신이 불변식 시계를 되돌린다 ───────────────────────────────
  printf("\n[9] 성공하면 불변식 시계가 갱신된다\n");
  arm(nullptr);
  lastTxOkAt = millis() - 9500;             // 곧 발동할 뻔한 상태
  sendLine(FRAME);                          // 성공 송신
  unsigned long since = millis() - lastTxOkAt;
  ok(since < 1000,              "★ 성공하면 lastTxOkAt 이 지금으로 갱신된다 (정지 오판 방지)");

  // ══ 2단계 (REQ-0116 2단계 · `SEND OK` 를 실제로 기다린다) ═══════════════════
  // 여기부터는 **1단계가 아니라 2단계**를 잰다. 위 [1]~[9] 는 손대지 않았다 —
  // 그래야 2단계가 1단계를 깨지 않았다는 것이 같은 실행에서 증명된다.

  // ── [10] 성공 뒤에는 SEND OK 를 볼 때까지 다음 송신을 건너뛴다 ──────────────
  printf("\n[10] SEND OK 대기 — 앞 전송이 안 끝났으면 보내지 않는다\n");
  arm(nullptr);
  sendLine(FRAME);                          // 1) 성공 → 대기 시작
  ok(awaitingSendOk,            "★ 성공 송신 뒤에는 SEND OK 를 기다리는 상태가 된다");
  size_t sentAfterFirst = wifi.sentLines.size();
  bool r10 = sendLine(FRAME);               // 2) 아직 SEND OK 없음 → 건너뜀
  ok(!r10,                      "대기 중에는 sendLine 이 false 를 돌려준다");
  ok(wifi.sentLines.size() == sentAfterFirst,
                                "★ 건너뛴 주기에는 전선에 아무것도 안 나간다");
  ok(sendSkips == 1,            "건너뛴 횟수가 진단에 기록된다");
  // ★ 가장 중요한 것 — **건너뛰기는 실패가 아니다.**
  //   여기서 카운터가 오르면 정상 동작(ESP 가 아직 보내는 중)을 자해로 세게 된다.
  ok(sendFailStreak == 0,       "★★ 건너뛰기는 연속 실패로 세지 않는다 (자해 방지)");
  ok(promptResyncs == 0,        "★ 건너뛸 때 더미를 넣지 않는다 (CIPSEND 를 쏘지도 않았다)");

  // ── [11] SEND OK 가 오면 대기가 풀리고 다음 송신이 나간다 ──────────────────
  printf("\n[11] SEND OK 수신 → 대기 해제\n");
  {
    char sendok[] = "SEND OK";
    handleLine(sendok);
  }
  ok(!awaitingSendOk,           "★ SEND OK 를 받으면 대기가 풀린다");
  bool r11 = sendLine(FRAME);
  ok(r11,                       "★ 대기가 풀리면 다음 프레임이 정상적으로 나간다");

  // ── [12] 3단 게이트 — T1 을 넘겨도 **두들기지 않는다** ──────────────────────
  // ✏️ 2026-08-17 개정. 옛 시험은 *"상한을 넘기면 대기를 풀고 실제로 보낸다"* 를 검증했다.
  //   그 동작이 **`busy` 폭풍의 시작이었다**(창4 09:02·09:13, 설계서 §5-T):
  //   막힌 ESP 에 다시 쏘기 시작해 8초를 두들기고 10초에 불변식이 겨우 끊었다.
  //   → **T1~T2 는 계속 조용히 기다리고, T2 에서 복구로 간다.**
  //   ⚠ "영구 정지 방지"라는 원래 목적은 **[12b] 가 그대로 지킨다** — 없앤 것이 아니라 옮겼다.
  printf("\n[12] 3단 게이트 — T1 을 넘겨도 두들기지 않는다\n");
  arm(nullptr);
  sendOkTimeouts = 0;                       // ★ 앞 시험들의 누적을 지워 계수를 결정적으로 만든다
  sendLine(FRAME);                          // 대기 시작
  ok(awaitingSendOk,            "대기 중이다");
  sendOkWaitFrom = millis() - (SEND_OK_TIMEOUT_MS + 1);   // T1 을 넘긴 것으로 둔다
  bool r12 = sendLine(FRAME);
  ok(!r12,                      "★★ T1 을 넘겨도 보내지 않는다 (busy 폭풍의 원인을 제거)");
  ok(awaitingSendOk,            "★ 대기를 풀지 않는다 — 2단은 계속 기다리는 구간이다");
  ok(netOnline,                 "★ 그리고 아직 링크를 끊지도 않는다");
  // ⚠ 이 단언은 **[12] 안에 있어야 한다.** 예전엔 [12b] 뒤에 있었는데 `arm()` 이
  //   `sendOkTimeouts` 를 지우므로 거기서는 항상 0 이다 — 측정 지점이 틀렸던 것이다.
  ok(sendOkTimeouts == 1,       "★ T1 초과가 okto 에 **한 번만** 계상된다 (증상 수가 아니라 사건 수)");
  bool r12dup = sendLine(FRAME);                      // 같은 라운드에서 한 번 더 막힌다
  ok(!r12dup,                   "여전히 안 보낸다");
  ok(sendOkTimeouts == 1,       "★★ 2단을 여러 주기 지나도 okto 는 안 늘어난다 (걸쇠가 산다)");

  // ── [12b] 3단 — T2 를 넘기면 **두들기지 말고 링크를 다시 세운다** ───────────
  // 옛 시험의 "영구 정지 방지"가 여기로 옮겨 왔다. **빠져나오는 길은 그대로 있다.**
  printf("\n[12b] 3단 — T2 를 넘기면 복구로 간다 (영구 정지 방지 · 옛 [12] 의 목적)\n");
  arm(nullptr);
  sendLine(FRAME);
  sendOkWaitFrom = millis() - (SEND_OK_GIVEUP_MS + 1);    // T2 를 넘긴 것으로 둔다
  bool r12b = sendLine(FRAME);
  ok(!r12b,                     "T2 초과에서도 그 주기에 보내지는 않는다");
  ok(!netOnline,                "★★ T2 를 넘기면 링크를 다시 세운다 (영구 정지 방지)");
  ok(!awaitingSendOk,           "★ 대기가 풀린다 — 복구 뒤 첫 송신이 안 막힌다");
  ok(sendOkGiveups == 1,        "★ 3단 발동이 stuck 에 계상된다 (T2 의 자기교정 계기)");
  // ⚠ 옛 `sendOkTimeouts == 1` 단언은 여기 있었는데 **`arm()` 이 그 값을 지우므로
  //   여기서는 항상 0 이었다.** 측정 지점을 [12] 안으로 옮겼다 — 값이 아니라 자리가 틀렸다.

  // ══ ACK 보류 큐 (2026-08-17 · 설계서 §5-U) ════════════════════════════════
  // 결함: 게이트가 닫혀 있으면 `sendAck` 이 실패하는데 **호출자가 반환값을 버렸다.**
  //       → 장치는 하행을 받고 적용까지 하는데 **ACK 만 조용히 사라진다.**
  //       실측(사용자 재현 13:20:22): 서버 재전송 3건이 **전부 같은 닫힌 게이트**를 만났다.
  // ⚠ 이 시험들은 **수정 전 판본에서 반드시 실패해야 한다.**

  // ── [12c] 게이트가 닫혀 있으면 ACK 가 큐에 남는다 ──────────────────────────
  printf("\n[12c] ACK 보류 큐 — 게이트가 닫혀 있어도 잃지 않는다\n");
  arm(nullptr);
  ackQ.clearQueue();
  sendLine(FRAME);                          // 성공 송신 → 게이트가 닫힌다
  ok(awaitingSendOk,            "게이트가 닫힌 상태");
  feedDown("R,77,A1,u1,");                  // 하행 도착 (체크섬은 실제 함수로 붙인다)
  ok(ackQ.pending() == 1,            "★★ 못 보낸 ACK 가 큐에 남는다 (예전엔 사라졌다)");

  // ── [12d] 게이트가 열리면 보류된 ACK 가 나간다 ─────────────────────────────
  // ✏️ **2026-08-17 개정 (슬롯 구조 · REQ-0164)** — 원래 `ackqDrain()` 을 불렀다.
  //   그 함수는 **없어졌다**: 보류 ACK 를 한 건씩 따로 내보내면 **슬롯당 1거래 규칙이 깨지고
  //   수신 창을 침범한다.** 이제 `sendSlotBatch()` 가 S 프레임과 **함께** 실어 보낸다.
  //   ★ **시험의 목적은 그대로다** — "보류된 ACK 가 실제로 나간다". 부르는 함수만 바뀌었다.
  //   (원장 §8.2-15-2: 거동을 바꾸는 수정은 옛 시험이 실패하는 것이 정상이다.
  //    지우지 말고 개정 사유와 **원래 목적이 어디로 갔는지**를 남긴다.)
  printf("\n[12d] 게이트가 열리면 보류된 ACK 가 나간다 (슬롯 배치로 개정)\n");
  {
    char sendok[] = "SEND OK";
    handleLine(sendok);                     // 게이트가 열린다
  }
  ok(!awaitingSendOk,           "게이트가 열렸다");
  {
    // 🔴 2026-08-18 — **등록이 밀려 있으면 첫 슬롯은 `D` 만 나간다**(명세 §5).
    //   이 시험은 그 다음 슬롯을 보는 것이므로 등록을 먼저 끝낸다.
    //   ⚠ **이 두 줄이 없으면 시험이 깨진다** — 실제로 깨졌고, 그것이 배선이 살아 있다는 증거였다.
    //   🔑 등록 중에 ACK 가 큐에 그대로 남는다는 것은 아래 [12k] 가 따로 본다.
    if (regPending) { uint8_t a0 = 0; uint16_t b0 = 0; sendSlotBatch(&a0, &b0); }
    { char sendok2[] = "SEND OK"; handleLine(sendok2); }   // 등록 전송의 게이트를 연다

    uint8_t  acks = 0;
    uint16_t bytes = 0;
    const size_t before = wifi.sentLines.size();
    const bool sent = sendSlotBatch(&acks, &bytes);
    ok(sent,                    "배치가 나갔다");
    ok(acks == 1,               "★★ 보류된 ACK 1건이 그 배치에 실렸다");
    ok(ackQ.pending() == 0,          "★★ 성공했으므로 큐에서 소비됐다");
    ok(wifi.sentLines.size() == before + 1,
                                "★ 거래는 **한 번**이다 (ACK 를 따로 보내지 않는다)");
  }

  // ── [12j] 🔴 **게이트가 열려 있어도 ACK 은 즉시 나가지 않는다** (REQ-0185 ①) ──
  //   이번 수정의 핵심이다. 예전에는 `sendAck` 이 `sendLine` 을 직접 불러
  //   **슬롯 창을 안 보고 즉시 송신**했고, 그것이 수신 창의 하행과 겹쳐 프레임을 깼다(창 B 5건).
  //   ⚠ **이 시험은 수정 전 판본에서 반드시 실패한다** — 그때는 게이트가 열려 있으면 나갔다.
  //   ★ 나머지 회귀가 전부 통과해서 "거동이 안 바뀐 것처럼" 보였다. 그래서 이 시험이 필요하다.
  printf("\n[12j] 게이트가 열려 있어도 ACK 은 즉시 나가지 않는다 (슬롯 규율)\n");
  arm(nullptr);
  ackQ.clearQueue();
  ok(!awaitingSendOk,           "게이트가 열린 상태 (예전이면 즉시 나갔다)");
  {
    const size_t before = wifi.sentLines.size();
    feedDown("R,601,A1,u1,");
    ok(wifi.sentLines.size() == before,
       "★★ 전선에 아무것도 안 나갔다 — ACK 이 슬롯을 우회하지 않는다");
    ok(ackQ.pending() == 1,          "★★ 대신 큐에 담겼다 (배치가 자기 창에서 보낸다)");
  }

  // ── [12h] 🔴 배치에 S 와 ACK 가 **한 거래**로 같이 담긴다 ──────────────────
  //   이것이 슬롯 설계의 핵심이다. 확인하지 않으면 "묶었다"가 주장으로만 남는다.
  printf("\n[12h] 배치 — S 프레임과 ACK 들이 한 거래에 함께 담긴다\n");
  arm(nullptr);
  ackQ.clearQueue();
  sendLine(FRAME);                          // 게이트를 닫아 ACK 를 큐에 쌓는다
  feedDown("R,201,A1,u1,");
  feedDown("R,202,A2,u1,");
  ok(ackQ.pending() == 2,            "ACK 2건이 큐에 있다");
  // ⚠ **등록이 밀려 있으면 그 슬롯은 `D` 가 나간다**(명세: 첫 S → 둘째 D).
  //   이 시험은 정상 운항 중의 배치를 보는 것이므로 등록을 먼저 끝낸다.
  //   🔑 등록 자체는 [32] 가 따로 본다 — **여기서 섞으면 둘 다 흐려진다.**
  regPending = false; regAfterS = false;
  {
    char sendok[] = "SEND OK";
    handleLine(sendok);                     // 게이트를 연다
  }
  {
    uint8_t  acks = 0;
    uint16_t bytes = 0;
    const size_t before = wifi.sentLines.size();
    sendSlotBatch(&acks, &bytes);
    ok(acks == 2,               "★★ ACK 2건이 **한 배치**에 담겼다");
    ok(wifi.sentLines.size() == before + 1,
                                "★★ 거래는 한 번뿐이다 (3건을 3거래로 보내지 않는다)");
    const std::string& s = wifi.sentLines.back();
    ok(s.compare(0, 2, "S,") == 0,
                                "★ 첫 줄은 S 프레임이다 (반송파 · 슬롯 시작 통보)");
    ok(s.find("\nA,") != std::string::npos,
                                "★★ 같은 거래 안에 ACK 줄이 LF 로 이어 붙어 있다");
    ok(bytes == s.size(),       "★ 선언한 길이와 실제 쓴 바이트가 같다 (스트림 어긋남 방지)");
  }

  // ── [12i] 🔴 **실패하면 ACK 를 잃지 않는다** — 성공했을 때만 소비한다 ───────
  //   옛 `ackqDrain` 은 먼저 빼고 실패 시 되넣었다. 배치에서는 순서가 뒤집힐 수 있어
  //   그 방식을 못 쓴다. **손실 경로가 아예 없는지**를 시험이 직접 확인한다.
  printf("\n[12i] 배치 전송이 실패하면 ACK 가 큐에 남는다\n");
  arm(nullptr);
  ackQ.clearQueue();
  sendLine(FRAME);
  feedDown("R,211,A3,u1,");
  {
    char sendok[] = "SEND OK";
    handleLine(sendok);
  }
  ok(ackQ.pending() == 1,            "ACK 1건이 큐에 있다");
  wifi.refusePrompt = true;                 // 이번 거래는 실패시킨다
  wifi.refuseReply  = "busy s...\r\n";
  {
    uint8_t  acks = 0;
    uint16_t bytes = 0;
    const bool sent = sendSlotBatch(&acks, &bytes);
    ok(!sent,                   "배치가 실패했다");
    ok(ackQ.pending() == 1,          "★★ 실패했으므로 ACK 가 큐에 그대로 남는다 (잃지 않는다)");
  }

  // ── [12e] 큐가 넘치면 버리되 **세어서 드러낸다** ───────────────────────────
  // ⚠ 조용히 버리면 지금과 같아진다 — 폐기 경로가 안 보이면 그게 다음 사고가 된다.
  // ✏️ **2026-08-18 개정 (REQ-0204)** — `ACKQ_N` 이 4 → 12 로 늘었다.
  //   시험이 상수를 박고 있으면 값이 바뀔 때마다 깨진다 → **`ACKQ_N` 을 그대로 쓴다.**
  printf("\n[12e] 큐 넘침 — 버리되 ackdrop 으로 드러낸다\n");
  arm(nullptr);
  ackQ.clearQueue();
  ackQ.resetStats();
  sendLine(FRAME);                          // 게이트를 닫아 둔다
  for (int i = 0; i < ACKQ_N + 2; i++) {    // 깊이보다 2건 더 넣는다
    char body[24];
    snprintf(body, sizeof(body), "R,%d,A1,u1,", 100 + i);
    feedDown(body);
  }
  ok(ackQ.pending() == ACKQ_N,       "★ 깊이 상한(ACKQ_N)을 지킨다");
  ok(ackQ.drops() == 2,            "★★ 넘친 2건이 ackdrop 에 남는다 (조용히 안 버린다)");

  // ── [12f] 소켓이 끊기면 큐를 비운다 ────────────────────────────────────────
  // ⚠ 이 설계가 새로 만드는 **유일한 위험**이다 — 새 소켓에서 옛 rid 에 답하면 안 된다.
  printf("\n[12f] 소켓이 끊기면 보류 ACK 를 버린다 (새 소켓에 옛 rid 로 답하지 않는다)\n");
  ok(ackQ.pending() > 0,             "끊기 전에는 보류가 남아 있다");
  startSocketRecovery();
  ok(ackQ.pending() == 0,            "★★ 소켓 재수립 시 큐가 비워진다");

  // ══ 슬롯 구조 (2026-08-17 · REQ-0164) ═════════════════════════════════════
  // ⚠ 이 시험들은 **수정 전 판본에서 컴파일되지 않는다**(새 심볼을 쓴다).
  //   원장 §8.2-15-2: **컴파일 실패를 시험 실패로 쓰지 마라.** 그래서 여기서 보이는 것은
  //   "옛 판본이 깨진다"가 아니라 **"새 거동이 실제로 그렇게 도는가"** 다.

  // ── [15] 🔴 슬롯당 정확히 1거래 ───────────────────────────────────────────
  printf("\n[15] 슬롯당 정확히 1거래 — 같은 슬롯에서 두 번 보내지 않는다\n");
  arm(nullptr);
  g_clockAutoAdvance = false;
  slotStart = millis();
  slotNo = 0; slotSent = false; slotOow = 0; slotMissed = 0;
  {
    const size_t before = wifi.sentLines.size();
    statusTick(millis());                       // 슬롯 시작 직후 — 우리 차례다
    ok(wifi.sentLines.size() == before + 1, "송신 창에서 한 번 보낸다");
    ok(slotSent,                            "이번 슬롯의 기회를 썼다고 표시된다");
    statusTick(millis());                       // 같은 슬롯에서 또 부른다
    ok(wifi.sentLines.size() == before + 1, "★★ 같은 슬롯에서 두 번째는 나가지 않는다");
  }

  // ── [16] 🔴 수신 창(600~1200ms)에는 한 바이트도 쓰지 않는다 ────────────────
  //   **이것이 손실 0 의 직접 원인이다.** `SoftwareSerial` 은 송신 중 `cli()` 로 귀를 닫는다.
  printf("\n[16] 수신 창에는 쓰지 않는다\n");
  {
    g_millis += TX_WINDOW_MS + 10;              // 수신 창으로 넘어간다
    const size_t before = wifi.sentLines.size();
    statusTick(millis());
    ok(wifi.sentLines.size() == before,     "★★ 수신 창에서는 아무것도 안 나간다");
  }

  // ── [17] 🔴 다음 슬롯이 오면 다시 보낸다 + **위상이 드리프트하지 않는다** ──
  //   ⚠ `slotStart += SLOT_MS` 여야 한다. `= now` 로 하면 매 슬롯 오차가 쌓여
  //     주기가 스스로 늘어난다 — 원장 §3.4 의 1.113s 결함이 정확히 그것이었다.
  //
  // ⚠⚠ **게이트를 먼저 연다.** [15] 의 송신이 `awaitingSendOk` 를 세워 둔 상태라
  //   열지 않으면 이 슬롯이 통째로 건너뛰어진다(`[SLOT] r=0`).
  //   ★ 이것은 시험 편의가 아니라 **실기 사실을 그대로 반영한 것**이다:
  //     **`SEND OK` 를 못 받으면 다음 슬롯은 나가지 않는다.** 슬롯이 와도 게이트가 먼저다.
  //     정상 동작에서는 `SEND OK` 가 같은 초에 오므로(실측 99.7%) 이 충돌이 안 난다.
  printf("\n[17] 다음 슬롯 · 위상 드리프트 없음\n");
  {
    char sendok[] = "SEND OK";
    handleLine(sendok);
  }
  {
    const uint32_t firstStart = slotStart;
    g_millis += SLOT_MS - TX_WINDOW_MS;         // 다음 슬롯 초입으로
    const size_t before = wifi.sentLines.size();
    statusTick(millis());
    ok(wifi.sentLines.size() == before + 1, "★ 새 슬롯에서 다시 보낸다");
    ok(slotNo == 1,                         "슬롯 번호가 하나 올라갔다");
    ok(slotStart == firstStart + SLOT_MS,
       "★★ 슬롯 시작이 정확히 SLOT_MS 만큼 전진했다 (= now 가 아니라 += 다)");
  }

  // ── [18] 🔴 오래 막혀 여러 슬롯이 지나도 **위상이 유지된다** ───────────────
  printf("\n[18] 여러 슬롯을 건너뛴 뒤에도 위상이 유지된다\n");
  {
    const uint32_t base = slotStart;
    g_millis += SLOT_MS * 5 + 100;              // 5슬롯을 통째로 넘긴다
    statusTick(millis());
    ok(slotStart == base + SLOT_MS * 5,
       "★★ 밀린 슬롯을 전부 소진해 위상이 격자에 남는다");
    ok(slotMissed > 0,                      "★ 놓친 슬롯이 smiss 에 계상된다");
  }

  // ── [19] 🔴 `[SLOT-OOW]` — **계수기가 실제로 오르는 것**을 확인한다 ────────
  //   ⚠ 원장 §8.2-15-1: "안 오른다"만 시험하면 **영원히 0 인 계수기도 통과한다.**
  //     그래서 **오르는 시험**을 반드시 같이 둔다.
  // ⚠ 여기서는 **체크섬을 맞추지 않아도 된다** — `[SLOT-OOW]` 판정은 `+IPD` 를 보는 즉시
  //   내려지고 체크섬 검사는 그 뒤다. 그래서 로그에 `[CKSUM NG]` 가 떠도 이 시험과 무관하다.
  //   ★ 그래도 **왜 무관한지를 적어 둔다** — 원장 §8.2-15 에서 손으로 쓴 체크섬 때문에
  //     "ACK 가 안 나갔다"를 코드 결함으로 오독한 적이 있다. 무관함은 확인하고 쓰는 것이다.
  // ✏️ **2026-08-17 개정 (REQ-0174)** — 원래 `handleLine()` 을 직접 불렀다.
  //   그때는 `handleLine` 안에서 `millis()` 를 떠서 동작했지만, **실기 경로를 안 탔다.**
  //   이제 도착 오프셋은 `feedRxChar` 가 **첫 바이트에서** 잡으므로 그 경로로 먹여야 한다.
  //   ★ 수정이 이 사실을 드러냈다 — **시험이 실기와 다른 길로 들어가고 있었다.**
  printf("\n[19] 슬롯 위반 표지 — 송신 창에 하행이 오면 oow 가 오른다\n");
  arm(nullptr);
  g_clockAutoAdvance = false;
  slotStart = millis();                         // 지금이 슬롯 시작 = 우리 송신 창
  slotOow = 0;
  {
    const char* line = "+IPD,20:R,301,A1,u1,7B";
    for (const char* p = line; *p; ++p) feedRxChar(*p);
    feedRxChar('\n');
    ok(slotOow == 1,            "★★ 송신 창에 도착한 하행이 위반으로 계상된다 (오른다!)");
  }
  {
    g_millis += TX_WINDOW_MS + 50;              // 수신 창으로 이동
    const char* line2 = "+IPD,20:R,302,A1,u1,78";
    for (const char* p = line2; *p; ++p) feedRxChar(*p);
    feedRxChar('\n');
    ok(slotOow == 1,            "★★ 수신 창 도착은 위반이 아니다 (안 오른다)");
  }
  g_clockAutoAdvance = true;
  // ── [20] 🔴 송신이 **창 안에서 끝나야** 한다 — 시작만으로는 부족하다 ────────
  //   창 끝자락에 시작하면 배치가 흐르는 동안 수신 창을 침범한다.
  //   (socket 이 flush 위치를 묻다가 드러난 실제 구멍이다. fdtest 엔 있던 가드가 본체에 없었다.)
  printf("\n[20] 창 끝자락에서는 시작하지 않는다 (배치가 수신 창을 침범하지 않게)\n");
  arm(nullptr);
  g_clockAutoAdvance = false;
  slotStart = millis();
  slotSent = false;
  {
    g_millis += TX_WINDOW_MS - 50;              // 창 끝 50ms 전 — 배치(≈185ms)가 못 끝난다
    const size_t before = wifi.sentLines.size();
    statusTick(millis());
    ok(wifi.sentLines.size() == before,
       "★★ 남은 시간이 배치를 못 끝내면 아예 시작하지 않는다");
  }
  {
    slotStart = millis(); slotSent = false;     // 창 시작으로 되돌린다
    const size_t before = wifi.sentLines.size();
    statusTick(millis());
    ok(wifi.sentLines.size() == before + 1,
       "★ 창 시작에서는 정상적으로 보낸다 (가드가 과하지 않다)");
  }
  g_clockAutoAdvance = true;

  // ── [21] 🔴 SoftwareSerial 링버퍼 넘침 계수 (REQ-0167) ─────────────────────
  //   ⚠ 계측기가 이미 라이브러리에 있었는데 이 스케치가 **한 번도 안 읽었다.**
  //   ⚠ **읽으면 플래그가 지워진다** — 그래서 이 수는 사건 수가 아니라 **하한**이다.
  //     스텁이 그 성질까지 재현하므로 여기서 그것을 직접 확인한다.
  printf("\n[21] SoftwareSerial 링버퍼 넘침 — ssovf 가 오르고, 그 값은 하한이다\n");
  arm(nullptr);
  ssOverflows = 0;
  espRead();
  ok(ssOverflows == 0,          "넘침이 없으면 오르지 않는다");
  wifi.injectOverflow();
  espRead();
  ok(ssOverflows == 1,          "★★ 넘치면 계수가 오른다 (계측기가 실제로 동작한다)");
  espRead();
  ok(ssOverflows == 1,          "★★ 플래그는 읽으면 지워진다 → 이 수는 **하한**이다");
  wifi.injectOverflow();
  wifi.injectOverflow();                        // 확인 사이에 두 번 넘쳤다
  espRead();
  ok(ssOverflows == 2,          "★★ 두 번 넘쳐도 1 만 오른다 — 뭉친다(하한의 근거)");

  // ── [22] 🔴 `[SLOT-OOW]` 는 **도착** 시각을 재야 한다 — 처리 시각이 아니라 (REQ-0174)
  //   앞 판본은 `handleLine` 에서 `millis()` 를 떴다. 송신 중 도착한 줄은 `pendLine` 에
  //   갇혔다가 송신 후에 처리되므로 **위험 구간 도착이 바로 그 이유로 판정에서 빠졌다.**
  //   ⚠ 이 시험은 **수정 전 판본에서 반드시 실패한다** — 그래야 무언가를 재는 시험이다(§8.2-15).
  printf("\n[22] 송신 중 도착한 하행도 도착 시각으로 판정된다 (pendLine 우회로 보존)\n");
  arm(nullptr);
  g_clockAutoAdvance = false;
  slotStart = millis();
  slotOow = 0;
  {
    // 송신 창 한복판(=위험 구간)에 하행이 도착하되, **우리가 송신 중**이라 pendLine 으로 간다.
    inSend = true;
    g_millis += 100;                            // 슬롯 시작 +100ms 에 첫 바이트
    const char* line = "+IPD,20:R,401,A1,u1,7B";
    for (const char* p = line; *p; ++p) feedRxChar(*p);
    g_millis += 400;                            // 송신이 길어져 처리 시점은 +500ms
    feedRxChar('\n');                           // 줄 완성 — inSend 라 pendLine 에 갇힌다
    ok(pendReady,                 "송신 중이라 줄이 미뤄졌다");
    ok(slotOow == 0,              "아직 처리 전이라 계상되지 않았다");

    inSend = false;
    g_millis += 200;                            // 처리 시점은 +700ms = 수신 창(위험 구간 아님)
    drainPending();
    ok(slotOow == 1,
       "★★ 처리 시각(+700ms)이 아니라 **도착 시각(+100ms)** 으로 판정된다");
    //   ⚠ 수정 전에는 여기서 0 이었다 — 처리 시각 700ms 가 TX_WINDOW_MS(600) 밖이라
    //     "안전한 도착"으로 분류됐다. **위험 구간 도착을 정확히 놓치는 형태였다.**
  }
  g_clockAutoAdvance = true;

  // ── [23] `cksumng` 계수 (REQ-0174 ②) ──────────────────────────────────────
  printf("\n[23] 체크섬 불일치가 cksumng 에 계상된다\n");
  arm(nullptr);
  cksumNg = 0;
  {
    char bad[] = "R,501,A1,u1,00";              // 체크섬을 일부러 틀리게 둔다
    handleFrameLine(bad);
    ok(cksumNg == 1,             "★★ 파괴된 프레임이 계수된다 (서버 계수로는 안 보이는 손실)");
  }
  {
    char good[32];
    snprintf(good, sizeof(good), "R,502,A1,u1,");
    appendChecksum(good, (uint8_t)strlen(good));
    handleFrameLine(good);
    ok(cksumNg == 1,             "★ 정상 프레임은 올리지 않는다");
  }

  // ── [24] 🔴 `[CNT]` 도 슬롯 규율을 지킨다 (REQ-0187 ②) ─────────────────────
  //   이 줄이 **손실원이었다**: 142B 가 TX 링버퍼(64B)를 넘겨 블로킹하고,
  //   그동안 `espRead()` 가 안 돌아 도착 중인 하행이 사라졌다.
  //   창 B·C·D 에서 `[CKSUM NG]` 시각이 `[CNT]` 시각과 정합했다(monitor 전수 대조).
  //   ⚠ **이 시험은 수정 전 판본에서 반드시 실패한다** — 그때는 창과 무관하게 찍었다.
  printf("\n[24] [CNT] 가 수신 창에서는 안 나간다 (슬롯 규율을 진단 출력에도)\n");
  arm(nullptr);
  g_clockAutoAdvance = false;
  g_millis = 200000;                            // ⚠ 주기(60s)를 확실히 넘긴 시각대로 옮긴다
  slotStart = millis();
  cntLastAt = millis() - CNT_PERIOD_MS - 1;     // ★ 주기는 이미 넘긴 상태
  //   ⚠ 이 줄이 없으면 `cntTick` 이 **주기 검사에서 먼저 return** 해서
  //     창 검사에 도달조차 안 한다 — 그러면 "안 찍혔다"가 엉뚱한 이유로 통과한다.
  {
    // 수신 창 한복판 — 여기서 찍으면 하행과 겹친다
    g_millis += TX_WINDOW_MS + 100;
    const uint32_t before = cntLastAt;
    Serial.out.clear();
    cntTick(millis());
    ok(Serial.out.find("[CNT]") == std::string::npos,
       "★★ 수신 창에서는 [CNT] 를 찍지 않는다 (미룬다)");
    ok(cntLastAt == before,     "★ 주기 타이머도 안 건드린다 — 다음 창에서 찍힌다");
  }
  {
    // 다음 슬롯의 송신 창으로 — 여기서는 찍혀야 한다
    slotStart = millis();
    cntLastAt = millis() - CNT_PERIOD_MS - 1;   // 주기 조건을 다시 만족시킨다
    Serial.out.clear();
    cntTick(millis());
    ok(Serial.out.find("[CNT]") != std::string::npos,
       "★★ 송신 창에서는 정상적으로 찍는다 (진단이 사라지지 않는다)");
  }
  {
    // 오프라인이면 하행이 없으므로 창을 안 기다린다 — 진단이 통째로 멈추면 안 된다
    netOnline = false;
    slotStart = millis();
    cntLastAt = millis() - CNT_PERIOD_MS - 1;
    g_millis += TX_WINDOW_MS + 100;             // 수신 창이지만 오프라인이다
    Serial.out.clear();
    cntTick(millis());
    ok(Serial.out.find("[CNT]") != std::string::npos,
       "★★ 오프라인이면 창과 무관하게 찍는다 (링크가 죽어도 진단은 남는다)");
    netOnline = true;
  }
  g_clockAutoAdvance = true;

  // ── [25] 🔴 버림의 **두 원인이 갈려 세어진다** (REQ-0204) ────────────────
  //   예전에는 둘 다 `ackdrop` 이었다 — **대책이 다른데 한 칸이라 무엇을 고칠지 못 읽었다.**
  //     ① 큐가 가득 참   → 유입 초과 → 큐/배출을 키운다        → `ackdrop`
  //     ② 캐시에서 밀려남 → 캐시가 작다 → 캐시를 키운다          → `ackstale`
  //   ⚠ 이 시험은 수정 전 판본에서 **컴파일되지 않는다**(`ackQ.stale()` 이 없다) —
  //     §8.2-15-2 대로 컴파일 실패를 시험 실패로 쓰지 않는다. **가르는 동작 자체**를 확인한다.
  printf("\n[25] 큐 넘침(ackdrop)과 캐시 밀림(ackstale)이 갈려 세어진다\n");
  arm(nullptr);
  ackQ.clearQueue();
  ackQ.resetStats();
  {
    // ① 큐 넘침만 만든다 — 캐시에는 다 들어 있게 둔다
    sendLine(FRAME);                          // 게이트를 닫아 ACK 이 큐에 쌓이게
    for (int i = 0; i < ACKQ_N + 3; i++) {
      char body[24];
      snprintf(body, sizeof(body), "R,%d,A1,u1,", 700 + i);
      feedDown(body);
    }
    ok(ackQ.drops() == 3,          "★★ 넘친 3건이 ackdrop 에만 계상된다");
    ok(ackQ.stale() == 0,          "★★ 캐시 밀림은 0 이다 (두 원인이 안 섞인다)");
  }
  {
    // ② 캐시 밀림만 만든다 — 큐에 든 rid 를 캐시에서 밀어낸다
    ackQ.clearQueue();
    ackQ.resetStats();
    ackQ.push(9001);                           // 캐시에 없는 rid 를 직접 넣는다
    ok(ackQ.pending() == 1,          "큐에 1건");
    uint8_t  acks = 0; uint16_t bytes = 0;
    sendSlotBatch(&acks, &bytes);             // 배치가 만들려다 캐시에 없음을 발견한다
    ok(ackQ.stale() == 1,          "★★ 캐시 밀림이 ackstale 에 계상된다");
    ok(ackQ.drops() == 0,          "★★ 큐 넘침은 0 이다 (반대 방향도 안 섞인다)");
  }

  // ── [26] 🔴 `SEND OK` 를 **바이트 흐름**에서 잡는다 (REQ-0218 ②) ──────────
  //   줄 경로는 세 곳에서 놓친다: ① pendLine 이 두 번째 줄을 버린다 ② 접두 비교라
  //   앞에 뭐가 붙으면 실패 ③ RX_CAP 초과 줄은 통째로 버려진다.
  //   **셋 다 게이트가 안 풀려 T2 8초 → 링크 재수립으로 끝난다.**
  //   ⚠ 수정 전 판본에서는 ①②가 실제로 실패한다 — 그것이 이 시험의 존재 이유다.
  printf("\n[26] SEND OK 를 바이트 흐름에서 잡는다 (줄 경로가 놓치는 것들)\n");

  // ① 앞에 다른 바이트가 붙어도 잡는다 (접두 비교로는 실패하는 형태)
  arm(nullptr);
  sendLine(FRAME);
  ok(awaitingSendOk,            "게이트가 닫힌 상태");
  {
    const char* glued = "\xA6,12382,P1,39SEND OK";   // 페이로드 에코에 붙어 온 경우
    for (const char* q = glued; *q; ++q) feedRxChar(*q);
    ok(!awaitingSendOk,         "★★ 앞에 붙어 와도 게이트가 풀린다 (접두 비교로는 못 잡는다)");
    ok(sendOkByStream == 1,     "★ okstream 에 계상된다 — 줄 경로가 놓치던 양이 보인다");
  }

  // ② 송신 중(inSend)이라 pendLine 이 버리는 두 번째 줄이어도 잡는다
  arm(nullptr);
  sendLine(FRAME);
  ok(awaitingSendOk,            "게이트가 닫힌 상태");
  {
    inSend = true;                       // 송신 중 — 줄은 pendLine 으로 간다
    for (const char* q = "echo-line-1\n"; *q; ++q) feedRxChar(*q);   // 첫 줄이 pendLine 을 채운다
    ok(pendReady,               "첫 줄이 pendLine 을 차지했다");
    for (const char* q = "SEND OK\n"; *q; ++q) feedRxChar(*q);       // 두 번째 줄 → 버려진다
    ok(!awaitingSendOk,         "★★ 버려지는 줄이어도 바이트 흐름이 잡는다 (T2 8초를 막는다)");
    inSend = false;
  }

  // ③ 기다리지 않을 때는 안 찾는다 (오탐 창을 좁힌다)
  arm(nullptr);
  ok(!awaitingSendOk,           "게이트가 열린 상태");
  {
    const uint16_t before = sendOkByStream;
    for (const char* q = "SEND OK\n"; *q; ++q) feedRxChar(*q);
    ok(sendOkByStream == before, "★ 기다리지 않을 때는 매칭하지 않는다 (오탐 축소)");
  }

  // ── [13] SEND FAIL 은 실패로 센다 — **원래 있던 구멍** ─────────────────────
  // 프롬프트까지 받고 페이로드도 썼는데 전송이 실패한 것이라 `busy` 와 전혀 다르다.
  // 지금까지는 `>` 를 봤다는 이유로 성공으로 세고 카운터를 0 으로 되돌리고 있었다.
  printf("\n[13] SEND FAIL — 진짜 전송 실패는 실패로 센다\n");
  arm(nullptr);
  sendFailStreak = 1;                       // 앞선 실패 1회가 있었다고 두고
  sendLine(FRAME);                          // '>' 를 받았으므로 이 시점엔 0 으로 리셋된다
  ok(sendFailStreak == 0,       "성공으로 세어 카운터가 0 이 된 상태 (여기까지는 기존 동작)");
  {
    char sf[] = "SEND FAIL";
    handleLine(sf);
  }
  ok(sendFailStreak == 1,       "★★ SEND FAIL 을 받으면 실패로 센다 (성공 계상을 되돌린다)");
  ok(!awaitingSendOk,           "★ SEND FAIL 도 대기를 푼다 (여기서 안 풀면 3초를 헛기다린다)");
  ok(sendFails == 1,            "SEND FAIL 이 진단에 기록된다");

  // ── [14] 링크가 다시 서면 대기를 끌고 가지 않는다 ─────────────────────────
  // 소켓이 사라지는 마당에 앞 전송의 SEND OK 는 영영 오지 않는다. 안 풀면 복구 직후
  // 첫 송신이 통째로 건너뛰어진다 — **가장 급한 순간에 늦어진다.**
  printf("\n[14] 복구 시 대기 해제\n");
  arm(nullptr);
  sendLine(FRAME);
  ok(awaitingSendOk,            "대기 중이다");
  startSocketRecovery();
  ok(!awaitingSendOk,           "★★ 소켓 복구로 들어가면 대기를 푼다");

  // ── [15] ESP 리셋 지문 — CIFSR 이 0.0.0.0 을 답하면 센다 (진단 전용) ───────
  // ⚠ 부트 배너를 판별자로 쓰지 않는 이유: **정상 부팅에도 뜬다**(monitor 전수 검증, REQ-0125).
  printf("\n[15] ESP 리셋 지문 — CIFSR 0.0.0.0\n");
  arm(nullptr);
  netOnline   = false;                      // CIFSR 판정부는 오프라인 경로에 있다
  netLastSent = NET_CIFSR;
  {
    char zero[] = "0.0.0.0";
    handleLine(zero);
  }
  ok(espResets == 1,            "★ CIFSR 이 0.0.0.0 이면 ESP 가 상태를 잃은 것으로 센다");

  // ★★ 걸쇠 — 같은 사건의 증상이 여러 번 와도 **한 번만** 센다.
  //   이게 없으면 카운터가 "사건 수"가 아니라 "증상 수"가 되어 monitor 의 계수와 정의가 어긋난다.
  netLastSent = NET_CIFSR;
  { char z2[] = "0.0.0.0"; handleLine(z2); }
  netLastSent = NET_CIFSR;
  { char z3[] = "0.0.0.0"; handleLine(z3); }
  ok(espResets == 1,            "★★ 같은 사건의 0.0.0.0 이 반복돼도 한 번만 센다 (증상 아닌 사건)");

  // 실제로 IP 를 되찾으면 걸쇠가 풀리고, 다음 소실은 **새 사건**이다.
  netLastSent = NET_CIFSR;
  {
    char real[] = "192.168.35.79";
    handleLine(real);
  }
  ok(espResets == 1,            "★ 정상 IP 는 세지 않는다 (오탐 없음)");
  ok(!ipLossLatched,            "★ IP 를 되찾으면 걸쇠가 풀린다");
  netLastSent = NET_CIFSR;
  { char z4[] = "0.0.0.0"; handleLine(z4); }
  ok(espResets == 2,            "★★ 되찾은 뒤 다시 잃으면 새 사건으로 센다");

  // ── [17] 판별자 ② — 응답이 가비지여도 IP 소실을 잡는다 ────────────────────
  // 18:48:12 리셋에서 CIFSR 응답이 통째로 깨져 `0.0.0.0` 문자열이 안 나왔다(monitor 실측).
  // `0.0.0.0` 단독 판별자만 있으면 **가장 심한 리셋을 놓친다** — 그 구멍을 메우는 경로다.
  printf("\n[17] 판별자 ② — CIFSR 3회 소진(응답이 깨져도 성립)\n");
  arm(nullptr);
  netOnline = false;
  netHasIp  = true;
  ipLossLatched = false;
  espResets = 0;
  cifsrTries = 3;                           // 세 번 물어도 못 얻은 상태
  netStep = NET_CIFSR;
  netLastSent = NET_CIFSR;
  netStepAt = millis() - 5000;              // 대기시간이 지난 것으로 둔다
  espReset(millis());
  ok(espResets == 1,            "★★ 0.0.0.0 문자열이 하나도 없어도 IP 소실을 잡는다");

  // ── [16] 🔴 루트 지시(REQ-0125 3번) — **침묵 탐지를 잃지 않았다**를 증명한다 ──
  // 기전 B(ESP 모듈이 죽는다)를 탐지하는 유일한 경로가 3연속 실패다.
  // 2단계가 "건너뛰기"를 추가했으므로 **그 경로가 여전히 끝까지 도달하는지**를 재야 한다.
  // 주장으로 보고하지 않는다 — 실제로 ESP 죽음을 흉내 내어 오프라인까지 가는지 본다.
  printf("\n[16] ESP 죽음 시나리오 — 침묵 3연속이 여전히 오프라인으로 간다\n");
  arm(nullptr);
  sendLine(FRAME);                          // 마지막 성공 송신 → SEND OK 대기 시작
  ok(awaitingSendOk,            "성공 직후라 SEND OK 를 기다리는 상태");
  // 여기서 ESP 가 죽는다: SEND OK 도, '>' 도, 오류도 오지 않는다 = 완전 침묵
  wifi.refusePrompt = true;
  wifi.refuseReply  = "";                   // 아무 응답도 없음
  int guard = 0;
  while (netOnline && guard++ < 20) {
    g_millis += 1000;                       // 하트비트 1주기씩 전진
    sendLine(FRAME);
  }
  ok(!netOnline,                "★★ ESP 가 완전히 침묵해도 결국 오프라인으로 간다 (탐지 유지)");
  ok(staleSocket,               "★ 복구 사다리로 들어간다");
  // 늦어지기는 한다 — 그 비용을 숨기지 않고 수치로 못박는다.
  printf("      (참고: 오프라인까지 %d 주기 — 2단계 이전은 3주기였다)\n", guard);
  ok(guard <= 8,                "★ 늦어지더라도 상한+3주기 안에 잡힌다 (무한 정지 아님)");

  // ── [18] 🔴 운영 계수기가 `DEBUG` 와 무관하게 나간다 (루트 지시) ────────────
  // 요점은 "카운터가 있다"가 아니라 **"DEBUG=0 에서 읽을 수 있다"** 이다.
  // 셀 수만 있고 못 읽으면 안 뺀 것과 같다. 그래서 **출력 자체를 검사**한다.
  printf("\n[18] 운영 계수기 [CNT] — DEBUG 밖에서 읽을 수 있는가\n");
  arm(nullptr);
  linkDrops = 0; espResets = 0; promptResyncs = 0;
  cntLastAt = millis();
  startSocketRecovery();                    // 링크 끊김 1건
  ok(linkDrops == 1,            "★ 링크 재수립이 운영 계수기에 잡힌다");
  startSocketRecovery();
  ok(linkDrops == 2,            "★ 두 번째도 잡힌다");

  size_t mark = Serial.out.size();          // 여기서부터 새로 나온 것만 본다
  g_millis += 60001;                        // 출력 주기를 넘긴다
  cntTick(millis());
  std::string fresh = Serial.out.substr(mark);
  ok(fresh.find("[CNT]")   != std::string::npos,
                                "★★ [CNT] 줄이 실제로 나간다 (DEBUG 와 무관한 경로)");
  ok(fresh.find("drop=2")  != std::string::npos,
                                "★★ 끊김 횟수가 그 줄에 실려 읽을 수 있다");
  ok(fresh.find("esprst=") != std::string::npos,
                                "★ ESP 리셋 칸도 실려 나간다");

  // 주기 전에는 안 나간다 — 시리얼 대역을 낭비하지 않는다는 확인
  mark = Serial.out.size();
  cntTick(millis());
  ok(Serial.out.substr(mark).find("[CNT]") == std::string::npos,
                                "★ 주기 전에는 안 찍는다 (대역 낭비 없음)");

  // ── [27] 🔴 `penddrop` 의 **분모**와 **대역**이 실제로 오른다 (2026-08-18) ──
  //
  // 왜 있나: 창 I 에서 `>=64B` 1,067 거래에 `penddrop=0` 이 나왔는데 **그 0 이 두 가지로 읽혔다** —
  //   (A) 안 넘쳤다   (B) `inSend` 중에 줄이 아예 안 와서 셀 일이 없었다.
  // **분모(`pendfill`)가 없으면 못 가른다.** 원장 §1.1 의 그 형태다.
  //
  // ⚠ **계수기를 넣었다는 것과 그것이 오른다는 것은 다른 진술이다.** 여기서 실제로 밟는다.
  printf("\n[27] penddrop 의 분모(pendfill)와 대역(big*)이 실제로 오른다\n");
  {
    // ⚠ **`feedRxChar` 로 먹인다.** `handleLine` 을 직접 부르면 실기 경로를 건너뛴다 —
    //   옛 시험 [19] 가 그 실수를 했고, 내 수정이 그것을 깨서야 드러났다.
    auto feedLine = [](const char* t) {
      for (const char* q = t; *q; ++q) feedRxChar(*q);
    };
    pendFills = pendDrops = pendFillsBig = pendDropsBig = okStreamBig = 0;
    pendReady = false; inSend = true;

    // ① 작은 대역(<64) — 담김 1 · 버림 1
    curTxLen = 40;
    feedLine("+IPD,5:X,1,AA\n");            // 첫 줄 → pendLine 에 담긴다
    ok(pendFills == 1,     "★★ 담으면 분모(pendfill)가 오른다");
    ok(pendFillsBig == 0,  "★ 40B 는 작은 대역이라 bigfill 이 안 오른다");
    feedLine("+IPD,5:X,2,AB\n");            // 둘째 줄 → 버려진다
    ok(pendDrops == 1,     "★★ 버리면 penddrop 이 오른다");
    ok(pendDropsBig == 0,  "★ 작은 대역이라 bigdrop 은 안 오른다");

    // ② 경계 검사 — 63 은 작은 쪽, 64 는 큰 쪽. **기전 경계(_SS_MAX_RX_BUFF)다**
    pendReady = false; curTxLen = 63;
    feedLine("+IPD,5:X,3,AC\n");
    ok(pendFillsBig == 0,  "★★ 63B 는 아직 작은 대역이다 (경계 검사)");
    pendReady = false; curTxLen = 64;
    feedLine("+IPD,5:X,4,AD\n");
    ok(pendFillsBig == 1,  "★★ 64B 부터 큰 대역이다 — 기전 경계와 자리가 같다");
    feedLine("+IPD,5:X,5,AE\n");            // 둘째 줄 → 큰 대역에서 버림
    ok(pendDropsBig == 1,  "★★ 큰 대역의 버림이 따로 세어진다");

    // ③ 🔴 **불변식: big 은 전체의 부분집합이다.** 이게 깨지면 어느 계수기가 틀린 것이다.
    ok(pendFills >= pendFillsBig, "★★ 불변식 pendfill >= bigfill (big 은 부분집합)");
    ok(pendDrops >= pendDropsBig, "★★ 불변식 penddrop >= bigdrop");
    //   ⚠ 이 불변식이 깨진 출력을 실제로 봤다 — 시험이 big 을 리셋 안 해서였다.
    //     **읽는 쪽에서는 "펌웨어가 이상하다"로 보인다.** 그래서 검사로 박는다.

    // ④ 🔴 **분모가 0 이면 penddrop=0 이 아무 뜻도 없다** — 그 상태를 재현해 둔다
    pendFills = pendDrops = pendFillsBig = pendDropsBig = 0;
    pendReady = false; inSend = false;
    feedLine("+IPD,5:X,6,AF\n");            // inSend 가 아니면 pendLine 을 안 쓴다
    ok(pendFills == 0 && pendDrops == 0,
                           "★★ inSend 가 아니면 둘 다 안 오른다 = 분모 0 상태(=(B))");
    inSend = false;
  }

  // ── [28] [CNT] 에 새 칸이 실제로 실린다 ──────────────────────────────────
  printf("\n[28] [CNT] 에 pendfill/bigfill/bigdrop/bigokst 가 실린다\n");
  {
    size_t m2 = Serial.out.size();
    g_millis += 60001;
    cntTick(millis());
    std::string f2 = Serial.out.substr(m2);
    ok(f2.find("pendfill=") != std::string::npos, "★★ pendfill 칸이 나간다 (분모)");
    ok(f2.find("bigfill=")  != std::string::npos, "★ bigfill 칸이 나간다");
    ok(f2.find("bigdrop=")  != std::string::npos, "★ bigdrop 칸이 나간다");
    ok(f2.find("bigokst=")  != std::string::npos, "★ bigokst 칸이 나간다");

    // ⚠ 줄 길이를 **눈으로 본다.** 이 줄은 하드웨어 Serial 로 나가고, 64B TX 링버퍼를
    //   넘는 만큼 블로킹한다. 그동안 espRead 가 안 돌면 하행이 사라진다(§11.2 의 그 고장).
    size_t p1 = f2.find("[CNT]");
    size_t p2 = f2.find('\n', p1);
    size_t linelen = (p1 != std::string::npos && p2 != std::string::npos) ? (p2 - p1) : 0;
    printf("      [CNT] 줄 길이 = %zu B  (115200bps 에서 약 %.1f ms)\n",
           linelen, (double)linelen / 11.52);
    ok(linelen < 300,      "★★ [CNT] 줄이 300B 미만이다 (송신 창 여유 안)");
  }

  // ── [29] 🔴 출력 **도중** 계수기가 늘어도 불가능한 값이 안 나온다 ──────────
  //
  // `cntTick` 이 줄을 찍는 중간에 `espRead()` 가 돈다. 그때 `awaitingSendOk` 가
  // true 면(=CIPSEND 직후의 **정상** 상태) 바이트 매처가 `SEND OK` 를 잡아 계수기가 는다.
  // **먼저 찍힌 `okstream` 은 옛 값, 나중 찍힌 `bigokst` 는 새 값** → `bigokst > okstream`.
  // ⚠ **시험 [28] 은 이걸 못 잡는다** — 출력 중에 바이트를 안 먹이기 때문이다.
  printf("\n[29] 출력 도중 계수기가 늘어도 bigokst > okstream 이 안 나온다\n");
  {
    sendOkByStream = 5; okStreamBig = 5; okLostByLine = 0; okLostBig = 0;
    awaitingSendOk = true; sendOkMatch = 0;         // 정상 상태를 만든다
    curTxLen = 100;                                  // 큰 대역
    wifi.deliver("SEND OK\r\n");                    // ⚠ 출력 중 pump 가 이걸 읽는다

    size_t m3 = Serial.out.size();
    g_millis += 60001;
    cntTick(millis());
    std::string f3 = Serial.out.substr(m3);

    auto fieldOf = [&](const char* key) -> long {
      size_t k = f3.find(key);
      if (k == std::string::npos) return -1;
      return strtol(f3.c_str() + k + strlen(key), nullptr, 10);
    };
    long okst = fieldOf(" okstream=");
    long bigo = fieldOf(" bigokst=");
    ok(okst >= 0 && bigo >= 0 && bigo <= okst,
                            "★★ bigokst <= okstream — 한 시점의 스냅샷으로 찍힌다");

    // 🔴 **줄 무결성** — `[CNT]` 한가운데로 다른 출력이 끼면 monitor 의 파서가 깨진다.
    //   실제로 그렇게 나왔었다: `… pendfill=0[AT] "SEND OK" (7)⏎ bigfill=0 …`
    //   **계측기를 고치려다 계측기를 깬 꼴**이라 검사로 박는다.
    size_t c1 = f3.find("[CNT]");
    size_t c2 = f3.find('\n', c1);
    std::string cntline = (c1 != std::string::npos && c2 != std::string::npos)
                            ? f3.substr(c1, c2 - c1) : std::string();
    ok(!cntline.empty() && cntline.find("[AT]") == std::string::npos,
                            "★★ [CNT] 줄 한가운데에 다른 출력이 안 낀다");
    ok(cntline.find("online=") != std::string::npos,
                            "★★ 한 줄 안에 마지막 칸까지 다 있다 (중간에 안 잘린다)");
    ok(fieldOf(" oklost=") >= 0,
                            "★ oklost 칸이 나간다 (대책 ②의 진짜 분자)");
    awaitingSendOk = false;
  }

  // ── [30] 🔴 hex 인코딩 — **비대칭 패턴으로만 검증한다** (socket 명세 §5) ────
  //
  // ⚠ `0000…`·`1111…` 는 **뒤집혀도 같아서** 비트 순서를 검증하지 못한다.
  //   명세가 박은 왕복 예제 중 **비대칭인 것들**이 유일한 검증자다.
  printf("\n[30] hex 인코딩 왕복 — 비대칭 패턴이 비트 순서를 검증한다\n");
  {
    // 도우미: "0110001011" 같은 문자열 → mask (슬롯 i = 문자열 위치 i, mask 는 비트 i)
    auto strToMask = [](const char* bits) -> uint16_t {
      uint16_t m = 0;
      for (uint8_t i = 0; bits[i]; i++) if (bits[i] == '1') m |= (uint16_t)(1u << i);
      return m;
    };
    struct Case { const char* bits; uint8_t n; const char* hex; };
    // 🔑 명세의 왕복 예제 그대로다. **비대칭이 앞에 온다.**
    const Case cases[] = {
      { "0110001011",       10, "18B"  },   // ★ 순서 검증자
      { "011000101100110",  15, "3166" },   // ★ 순서 검증자
      { "10101",             5, "15"   },   // ★ 순서 검증자
      { "1000000000000001", 16, "8001" },   // ★ 양 끝 — 뒤집기에 특히 민감하다
      { "0000000000",       10, "000"  },   // ⚠ 이것만으로는 아무것도 증명 못 한다
      { "1111111111",       10, "3FF"  },   // ⚠ 위와 같다
    };
    for (const auto& c : cases) {
      char out[8];
      uint16_t m = strToMask(c.bits);
      bitsToHex(m, c.n, out);
      char msg[128];
      snprintf(msg, sizeof msg, "★★ n=%u %s -> %s", c.n, c.bits, c.hex);
      ok(strcmp(out, c.hex) == 0, msg);

      uint16_t back = 0xFFFF;
      bool okdec = hexToBits(c.hex, c.n, &back);
      snprintf(msg, sizeof msg, "★★ n=%u %s 를 되돌리면 같다 (왕복)", c.n, c.hex);
      ok(okdec && back == m, msg);
    }

    // ④ 패딩 검증 — **길이도 체크섬도 통과하는데 값만 틀리는 경로**
    uint16_t dummy = 0;
    ok(!hexToBits("58B", 10, &dummy),
                            "★★ n=10 에서 상위 패딩이 0 이 아니면 거부한다 (58B)");
    ok( hexToBits("18B", 10, &dummy),
                            "★ 같은 폭이라도 패딩이 0 이면 받는다 (18B)");
    // ① 소문자 거부 — 정본은 하나다
    ok(!hexToBits("18b", 10, &dummy), "★ 소문자는 거부한다 (정본은 대문자)");
    // ② 폭 고정
    ok(hexWidthFor(10) == 3 && hexWidthFor(15) == 4 && hexWidthFor(16) == 4 && hexWidthFor(5) == 2,
                            "★★ 폭 = ceil(n/4) — 길이로 n 을 검증할 수 있다");

    // 🔴 뒤집기가 실제로 일어나는지 **직접** 본다 — mask 와 hex 의 비트 순서가 반대다
    ok(strToMask("0110001011") == 0x346,
                            "★★ mask 안에서는 슬롯 i = 비트 i (0x346)");
    { char o[8]; bitsToHex(0x346, 10, o);
      ok(strcmp(o, "18B") == 0,
                            "★★ 전선에서는 슬롯 i = 비트 (n-1-i) (0x18B) — 반대다"); }

    // ⚠ 축 3 의 기대: **거동 변화 0**. moduleCount 가 지금은 SLOT_N 과 같아야 한다
    // ✏️ 2026-08-19 가상 모듈이 들어와 moduleCount() > SLOT_N 이 됐다
    ok(moduleCount() >= SLOT_N,
                            "★★ 표가 실물 자리를 전부 앞쪽에 포함한다");
  }

  // ── [31] 🔴 `S` 프레임이 **실제로** hex 로 나가고 짧아진다 ────────────────
  //
  // ⚠ 왜 따로 있나: `bitsToStr` → `bitsToHex` 로 바꿨는데 **시험이 하나도 안 깨졌다.**
  //   그건 **어느 시험도 `S` 의 자리 필드를 안 보고 있었다**는 뜻이다.
  //   🔑 통과 수는 그 경로를 봤다는 뜻이 아니다(원장 §16.4 ③).
  printf("\n[31] S 프레임이 실제로 hex 로 나간다 (형식 전환 확인)\n");
  {
    node.occMask = 0x346;            // 슬롯 {1,2,6,8,9} — ★ 비대칭이라 순서를 검증한다
    node.resMask = 0;
    node.testArmed = false;
    // 🔴 가상 차단봉을 **닫힌 상태로 고정**한다 — 안 하면 slotNo 에 따라 값이 흔들려
    //   이 시험이 비결정적이 된다(자율 토글이 slotNo 를 본다).
    gates.manual = true; gates.state = 0;
    char buf[64];
    uint8_t n = buildStatus(buf, sizeof buf);
    printf("      S = %s   (%u B)\n", buf, (unsigned)n);
    ok(n > 0,                                  "★ 프레임이 만들어진다");
    // 🔴 **n=10 → 12 로 바뀌어 값이 이동했다. 손으로 재계산한 값이다:**
    //   슬롯 i → 비트 (n−1−i) 이므로 {1,2,6,8,9} → 비트 {10,9,5,3,2} = 0x62C
    //   ⚠ **같은 점유인데 hex 가 다르다** — 이것이 `n` 이 바뀔 때의 위험 그 자체다.
    ok(strstr(buf, ",62C,") != NULL,
                            "★★ 자리 필드가 hex '62C' 다 (n=12 에서 재계산)");
    ok(strstr(buf, "0110001011") == NULL,
                            "★★ 옛 10진 표기가 남아 있지 않다");
    ok(strstr(buf, ",000,") != NULL,
                            "★ res 도 같이 hex 로 바뀌었다 (하나만 바뀌면 어긋난다)");

    // 폭이 실제로 줄었나 — 명세의 근거(D 6→7)가 이 감소에 서 있다
    ok(n < 45,              "★★ S 프레임이 45B 미만이다 (폭 3 유지 — n=12 가 공짜 상한)");

    // tmask 갈래도 같은 변환을 타는가 — **셋째 마스크를 빠뜨리기 쉬운 자리다**
    node.testArmed = true; node.ovrActive = 0x346;
    uint8_t n2 = buildStatus(buf, sizeof buf);
    printf("      S(tmask) = %s   (%u B)\n", buf, (unsigned)n2);
    ok(n2 > 0 && strstr(buf, "0110001011") == NULL,
                            "★★ tmask 갈래에도 10진이 안 남는다 (셋째 마스크)");
    {
      // '18B' 가 두 번 나와야 한다 — occ 와 tmask 둘 다
      const char* f = strstr(buf, "62C");
      ok(f != NULL && strstr(f + 1, "62C") != NULL,
                            "★★ occ 와 tmask 가 둘 다 hex 다");
    }
    node.testArmed = false; node.ovrActive = 0; node.occMask = 0;
  }

  // ── [32] 🔴 등록(`D`) — 첫 슬롯은 D 만, **ACK 는 잃지 않는다** (명세 §5) ────
  printf("\n[32] 등록 프레임 — 첫 슬롯은 D 만 · ACK 는 다음 슬롯으로 밀린다\n");
  {
    // 등록이 밀린 상태를 만든다 (온라인 전이가 하는 일)
    // ⚠ **앞 시험들이 남긴 상태를 명시적으로 씻는다.** [32] 는 맨 끝이라 전부 물려받는다 —
    //   특히 `refusePrompt` 가 켜진 채 남아 있으면 프롬프트가 안 와서 전송이 통째로 실패한다.
    //   🔑 실제로 그래서 처음에 8건이 깨졌다. **"내 코드가 틀렸나"가 아니라 전제가 더러웠다.**
    wifi.refusePrompt = false;
    wifi.stickySocket = false;
    markNeedsRegistration();
    ackQ.clearQueue();
    ackQ.clearCache();
    awaitingSendOk = false; sendOkT1Passed = false; inSend = false;
    netOnline = true; sendFailStreak = 0;
    lastSendEndAt = 0;

    // ACK 을 하나 큐에 넣어 둔다 — **재접속이면 이런 상태가 실제로 가능하다**
    ackQ.put(77, 'A', '1', 1);
    ackQ.push(77);
    ok(ackQ.pending() == 1,      "준비: ACK 1건이 큐에 있다");
    ok(!regPending && regAfterS,
                            "★★ 접속 직후엔 D 가 아니라 S 가 먼저다 (승격이 먼저다)");

    // ── 슬롯 1: 🔴 **S 가 나간다.** D 에는 devid 가 없어 승격을 못 만든다 ──
    uint8_t  a0 = 0; uint16_t b0 = 0;
    const size_t before = wifi.sentLines.size();
    ok(sendSlotBatch(&a0, &b0), "★ 첫 슬롯이 나갔다");
    ok(!wifi.sentLines.empty() && wifi.sentLines.back()[0] == 'S',
                            "★★ 첫 슬롯은 **S** 다 (D 가 아니다)");
    ok(regPending,          "★★ S 가 나간 뒤에야 D 가 예약된다");
    ok(a0 == 1,             "★★ 첫 S 에는 ACK 가 같이 실린다 (정상 배치다)");
    { char sk1[] = "SEND OK"; handleLine(sk1); }

    // 🔴 **이제 등록 슬롯에 ACK 이 도착한 상황을 만든다** — 이것이 밀림 시험의 본체다
    ackQ.put(78, 'B', '2', 1);
    ackQ.push(78);
    ok(ackQ.pending() == 1,      "준비: 등록 슬롯 직전에 ACK 가 하나 더 들어왔다");

    // ── 슬롯 2: D ────────────────────────────────────────────────────────
    uint8_t  a1 = 0; uint16_t b1 = 0;
    bool sent1 = sendSlotBatch(&a1, &b1);
    ok(sent1,               "★ 등록 배치가 나갔다");
    ok(a1 == 0,             "★★ 이 슬롯에 ACK 는 안 실린다 (D 전용)");
    // 🔴 **여기가 이 시험의 본체다.** 밀리는 것은 지연이고 버리는 것은 손실이다
    ok(ackQ.pending() == 1,      "★★ ACK 가 큐에 그대로 남는다 — 밀린 것이지 버린 게 아니다");
    ok(!regPending,         "★ 성공했으므로 등록 대기가 내려간다");

    const std::string& line = wifi.sentLines.back();
    ok(line.compare(0, 4, "D,*,") == 0,
                            "★★ 맨 앞이 배출률 선언이다 (D,*,...)");
    ok(line.find("D,A1,IP,") != std::string::npos,
                            "★★ 모듈 줄이 실린다 (name,kind)");
    ok(line.find("D,B5,IP,") != std::string::npos,
                            "★ 마지막 모듈까지 다 실린다");
    {
      // 줄 수 = 1(배출률) + moduleCount()
      size_t cnt = 0;
      for (size_t k = 0; k + 1 < line.size(); k++) if (line[k] == '\n') cnt++;
      ok(cnt + 1 == (size_t)moduleCount() + 1,
                            "★★ 줄 수 = 1 + moduleCount() — n 의 원천이 하나다");
    }
    printf("      등록 %uB · %u 줄\n", (unsigned)line.size(), (unsigned)moduleCount() + 1);
    ok(line.size() <= BATCH_CAP,
                            "★★ 등록이 상행 배치 상한 안에 든다");

    // 다음 슬롯에서 ACK 가 실제로 나간다 — **밀린 것이 도착하는지 끝까지 본다**
    { char sok[] = "SEND OK"; handleLine(sok); }
    uint8_t a2 = 0; uint16_t b2 = 0;
    bool sent2 = sendSlotBatch(&a2, &b2);
    ok(sent2 && a2 == 1,    "★★ 다음 슬롯에서 밀렸던 ACK 가 나간다");
    ok(wifi.sentLines.size() == before + 3,
                            "★ 거래는 슬롯당 하나씩 세 번이다 (S · D · S+ACK)");
    { char sok[] = "SEND OK"; handleLine(sok); }
  }

  // ── [33] 🔴 `Q` 를 받으면 등록을 다시 보낸다 (명세 §5 ③) ──────────────────
  //   ⚠ 없으면 등록이 한 번 실패했을 때 **복구 경로가 아예 없다** — 등록에 ACK 이 없어서
  //     장치는 자기가 실패한 줄 모르고, 서버는 그 노드를 영영 미등록으로 둔다.
  printf("\n[33] Q 를 받으면 다음 송신 창에 등록을 다시 보낸다\n");
  {
    wifi.refusePrompt = false;
    regPending = false;
    awaitingSendOk = false; inSend = false; netOnline = true;

    // 체크섬이 맞는 Q 를 만든다 — 다른 프레임과 같은 구조다
    char q[16] = "Q,";
    appendChecksum(q, 2);
    char qline[32];
    snprintf(qline, sizeof qline, "+IPD,%u:%s", (unsigned)strlen(q), q);
    for (const char* p2 = qline; *p2; ++p2) feedRxChar(*p2);
    feedRxChar('\n');
    ok(regPending,          "★★ Q 를 받으면 등록 대기가 다시 선다");

    // 그리고 다음 배치가 실제로 D 다
    const size_t before = wifi.sentLines.size();
    uint8_t a = 0; uint16_t b = 0;
    ok(sendSlotBatch(&a, &b), "★ 재등록 배치가 나갔다");
    ok(wifi.sentLines.size() == before + 1 &&
       wifi.sentLines.back().compare(0, 4, "D,*,") == 0,
                            "★★ 다음 슬롯에 D 가 나간다 (ACK 가 아니라)");
    ok(regSends >= 1,       "★ 전송 횟수가 세어진다 (진단)");

    // 🔴 체크섬이 틀린 Q 는 무시한다 — 잡음이 재등록을 유발하면 안 된다
    { char sok[] = "SEND OK"; handleLine(sok); }
    regPending = false;
    char bad[] = "Q,FF";
    for (const char* p2 = bad; *p2; ++p2) feedRxChar(*p2);
    feedRxChar('\n');
    ok(!regPending,         "★★ 체크섬이 틀린 Q 는 무시한다 (잡음 방어)");
  }

  // ── [34] 🔴 축 3(모듈 표) — **거동 변화 0 을 바이트로 확인한다** ──────────
  //
  // ⚠ **PASS 수는 리팩터링이 무해했다는 증거가 아니다.** 오늘 두 번 확인했다:
  //   형식을 바꿨는데 안 깨졌고([31] 이 필요했다), 배선을 넣었더니 깨졌다([12d] 가 그 증거였다).
  //   🔑 **기대가 "아무 일도 안 일어난다"인 축은 바이트로 못 박지 않으면 검증이 없다.**
  printf("\n[34] 모듈 표 도입 — 거동 변화 0 (바이트 대조)\n");
  {
    // ✏️ 2026-08-19 — 가상 모듈이 들어와 `moduleCount() > SLOT_N` 이 됐다
    ok(moduleCount() >= SLOT_N,
                            "★★ 표가 실물 자리를 전부 포함한다");
    ok(MODULE_N == 12,      "★ 표 길이가 12 다 (실물 10 + 가상 2)");

    // ① 🔴 **이름 열 개가 서버의 자리 id 와 같아야 한다** (socket 통보 2026-08-18)
    //   서버는 `D,<name>,<kind>` 의 **name 이 자리 id 와 같으면** 그 자리에 붙인다.
    //   🔴 **바꾸면 조용히 끊긴다** — 등록은 성공하고 자리에는 아무것도 안 붙는다.
    //   **화면에 모듈이 안 보이고 오류도 안 뜬다.** 장치 쪽에서는 볼 수 없는 고장이다.
    //   ⚠ **그래서 이 시험이 유일한 감시다.** 여기가 깨지면 socket 에 먼저 물어라.
    //   (겸해서 옛 계산식과의 동일성도 본다 — 표 도입이 무해했다는 증거)
    static const char* EXPECT[10] =
      {"A1","A2","A3","A4","A5","B1","B2","B3","B4","B5"};
    // ⚠ **실물 열 개만 본다** — 가상 모듈(E1·X1)은 아래에서 따로 검사한다
    bool allName = true;
    for (uint8_t i = 0; i < SLOT_N; i++) {
      char nm[4]; moduleNameOf(i, nm);
      if (strcmp(nm, EXPECT[i]) != 0) { allName = false;
        printf("      🔴 i=%u: 표 '%s' 대 기대 '%s'\n", i, nm, EXPECT[i]); }
    }
    ok(allName,             "★★ 열 개 이름이 옛 계산식과 전부 같다");

    // ② 핀이 SLOT_PIN 과 같은가 — **표와 핀 표가 갈리면 엉뚱한 칸을 읽는다**
    // ⚠ **실물 범위(SLOT_N)까지만 돈다** — 가상 모듈은 핀이 없고
    //   `SLOT_PIN[]` 은 크기가 `SLOT_N` 이라 그 밖은 배열 밖 읽기다.
    bool allPin = true;
    for (uint8_t i = 0; i < SLOT_N; i++)
      if (pgm_read_byte(&MODULE_TABLE[i].pin) != slotPin(i)) { allPin = false;
        printf("      🔴 i=%u: 표 핀 %u 대 SLOT_PIN %u\n", i,
               pgm_read_byte(&MODULE_TABLE[i].pin), slotPin(i)); }
    ok(allPin,              "★★ 표의 핀이 SLOT_PIN 과 전부 같다 (이행 중 두 표가 공존한다)");

    // ③ 🔴 **전선에 나가는 바이트가 그대로인가** — 이 축의 최종 판정이다
    wifi.refusePrompt = false;
    node.occMask = 0x346; node.resMask = 0; node.testArmed = false;
    seqNo = 3; g_millis = 389000;
    char sbuf[64];
    buildStatus(sbuf, sizeof sbuf);
    printf("      S = %s\n", sbuf);
    // ✏️ 2026-08-19 — 가상 모듈로 n 이 12 가 되어 이 리터럴이 바뀌었다. **의도한 변경이다.**
    //   🔑 이 시험의 원래 목적(표 도입이 무해했다)은 이미 달성됐고(커밋 021e16e),
    //     지금은 **"n 이 바뀌면 자리 필드가 이동한다"를 못 박는 자리**로 성격이 바뀌었다.
    ok(strstr(sbuf, ",62C,") != NULL,
                            "★★ n=12 에서 자리 필드가 62C 다 (18B 가 아니다)");

    char rbuf[BATCH_CAP + 1];
    uint16_t rn = buildRegistration(rbuf, sizeof rbuf);
    printf("      등록 %uB\n", (unsigned)rn);
    // ✏️ 118B → 121B · drain 6 → 7 : **둘 다 의도한 변경이다**
    //   +3B = `n`(모듈 수) 필드. drain 은 hex 전환으로 S_worst 가 줄어 재계산했다.
    //   🔑 **시험이 이 둘을 잡은 것이 맞다** — 값이 바뀌면 시험이 반응해야 한다.
    // ✏️ ~~145B~~ → **143B** (REQ-0271 · 2026-08-19). `OBV` → `OB` 로 **가상 줄이 1B씩 짧아졌다.**
    //   🔑 **시험이 이 변경을 잡았다** — 145 를 리터럴로 못 박아 뒀기 때문이다. 그게 이 줄의 목적이다.
    //   ⚠ 새 값도 리터럴로 박는다. 다음에 kind 가 또 바뀌면 여기가 다시 깨져야 한다.
    //   여유: BATCH_CAP 160 − 143 = **17B** (모듈 한 줄이 10~11B 이므로 여전히 더 못 넣는다)
    ok(rn == 143,           "★★ 등록이 143B — REQ-0271 로 OB(2글자). BATCH_CAP 160 까지 여유 17B");
    ok(strncmp(rbuf, "D,*,7,12,", 9) == 0,
                            "★★ 머리가 D,*,<drain>,<n>, 이다 (drain=7 · **n=12**)");
    // 🔴 **①선언 n · ②실제 D 줄 수 · ③hex 폭 — 셋이 서로를 정확히 못 박는다**
    {
      unsigned declaredN = 0;
      const char* p3 = strchr(rbuf, ',');            // D 뒤
      p3 = strchr(p3 + 1, ',');                      // * 뒤
      p3 = strchr(p3 + 1, ',');                      // drain 뒤
      declaredN = (unsigned)atoi(p3 + 1);
      size_t lines = 0;
      for (uint16_t k = 0; k + 1 < rn; k++) if (rbuf[k] == '\n') lines++;
      ok(declaredN == lines,
                            "★★ 선언한 n 과 실제 D 줄 수가 같다 (①==②)");
      ok(declaredN == moduleCount(),
                            "★★ 그리고 둘 다 moduleCount() 다 (원천이 하나다)");
      ok(hexWidthFor((uint8_t)declaredN) == 3,
                            "★ hex 폭도 그 n 에서 나온다 (③)");
    }
    node.occMask = 0;
  }

  // ── [35] 🔴 **등록 예약이 실제 전이 경로에서 선다** ────────────────────────
  //
  // ⚠ 왜 따로 있나: [32] 는 `markNeedsRegistration()` 을 **직접 부른다.**
  //   소스에는 그 호출이 **세 곳**(정상 접속 · 초기 경합 · 소켓 닫힘)에 있는데
  //   **시험이 하나도 안 탄다** → 🔴 **셋 중 하나가 빠져도 212 PASS 가 그대로 나온다.**
  //   🔑 옛 시험 [19] 가 `handleLine` 을 직접 불러 실기 경로를 건너뛴 것과 **같은 형태**다.
  //   증상: 재접속했는데 등록이 안 되고, 서버는 `Q` 3회 뒤 `node_unregistered` 로 굳힌다.
  printf("\n[35] 등록 예약이 **실제 연결 줄**로 선다 (세 경로 전부)\n");
  {
    struct Case { const char* line; const char* what; };
    // ⚠ `ALREAY CONNECT` 는 오타가 아니다 — 구형 ai-thinker 펌웨어의 실제 출력이다(원장 §8).
    const Case cases[] = {
      { "Linked",          "정상 접속(Linked)" },
      { "CONNECT",         "정상 접속(CONNECT)" },
      { "ALREAY CONNECT",  "초기 경합(ALREAY CONNECT · 펌웨어 오타)" },
      { "Unlink",          "소켓 닫힘(Unlink)" },
      { "CLOSED",          "소켓 닫힘(CLOSED)" },
    };
    for (const auto& c : cases) {
      regPending = true; regAfterS = false;        // 반대 상태로 만들어 둔다
      char buf2[32];
      snprintf(buf2, sizeof buf2, "%s", c.line);
      handleLine(buf2);
      char msg[128];
      snprintf(msg, sizeof msg, "★★ %s → 등록이 다시 예약된다", c.what);
      // 🔑 **`regAfterS` 여야 한다** — 새 소켓이면 `S` 가 먼저다(승격 전이므로)
      ok(!regPending && regAfterS, msg);
    }
    // 🔴 그리고 **`Q` 는 반대여야 한다** — 이미 승격된 뒤라 바로 `D` 다
    regPending = false; regAfterS = true;
    requestRegistrationNow();
    ok(regPending && !regAfterS,
                            "★★ Q 경로는 반대다 — regPending 이 서고 regAfterS 는 내려간다");
  }

  // ── [36] 🔴 가상 O* 모듈과 `G` 조작 명령 (REQ-0227) ───────────────────────
  //
  // ⚠ **새 기능을 밟는 시험이 하나도 없었다.** 기존 218 PASS 는 `n=12` 로 반응한 것뿐이고
  //   `G` 경로·자율 정지·에코는 **아무도 안 보고 있었다.** 오늘 세 번 밟은 그 형태다.
  printf("\n[36] 가상 차단봉과 G 조작 명령\n");
  {
    wifi.refusePrompt = false;
    gates.manual = false; gates.state = 0;
    ackQ.clearCache(); ackQ.clearQueue();
    node.occMask = 0; node.resMask = 0; node.testArmed = false;

    // ① 등록에 가상 모듈이 실린다 — 이름은 자리 id · kind 에 V 접미
    char rbuf[BATCH_CAP + 1];
    buildRegistration(rbuf, sizeof rbuf);
    // 🔴 REQ-0271 — `OBV` → `OB`. **모의/실물 구분을 전선에서 없앴다**(사용자 확정).
    //   ⚠ **`OBV` 가 안 나오는 것**도 같이 단언한다 — 긍정형만 두면 옛 값이 남아도 통과한다.
    ok(strstr(rbuf, "D,E1,OB,")  != NULL, "★★ E1 이 OB(차단봉)로 선언된다");
    ok(strstr(rbuf, "D,X1,OB,")  != NULL, "★★ X1 도 선언된다");
    ok(strstr(rbuf, "OBV")       == NULL, "★★ 전선에 OBV 가 하나도 없다 (V 접미 제거 확인)");
    ok(strstr(rbuf, "D,A1,IP,")  != NULL, "★ 실물 모듈은 그대로다 (V 없음)");

    // ② 자율 모드 — slotNo 로 결정적으로 토글한다. **무작위가 아니다**
    slotNo = 0;  bool e0 = gates.isOpen(0, slotNo), x0 = gates.isOpen(1, slotNo);
    slotNo = 10; bool e1 = gates.isOpen(0, slotNo);
    slotNo = 7;  bool x1 = gates.isOpen(1, slotNo);
    ok(e0 && !e1,           "★★ E1 이 주기 20 으로 토글한다 (0→열림 · 10→닫힘)");
    ok(x0 && !x1,           "★★ X1 은 주기 14 로 토글한다 — **서로 소라 조합이 다 나온다**");
    slotNo = 0;
    ok(gates.isOpen(0, slotNo) == e0,  "★ 같은 slotNo 면 같은 값이다 (결정적 · 재현 가능)");

    // ③ 🔴 `G` 명령 — idx 10 = E1 (SLOT_N=10 이므로)
    char g[24]; snprintf(g, sizeof g, "G,301,10,0,"); appendChecksum(g, (uint8_t)strlen(g));
    handleFrameLine(g);
    ok(gates.manual,         "★★ 첫 명령이 자율 토글을 **영구 정지**시킨다");
    ok(!gates.isOpen(0, slotNo),       "★★ op=0 이면 닫힌다");
    slotNo = 0;
    ok(!gates.isOpen(0, slotNo),       "★★ 자율 주기가 와도 안 열린다 — **명령이 되돌려지지 않는다**");

    snprintf(g, sizeof g, "G,302,10,1,"); appendChecksum(g, (uint8_t)strlen(g));
    handleFrameLine(g);
    ok(gates.isOpen(0, slotNo),        "★★ op=1 이면 열린다");

    // ④ 🔴 에코 — occ 비트 10 에 실려 나간다. **완료 판정이 이걸로 이뤄진다**
    char sbuf[64]; buildStatus(sbuf, sizeof sbuf);
    printf("      S(E1 열림) = %s\n", sbuf);
    {
      // ✏️ 기대가 "002" 였는데 실제는 "003" 이다. **코드가 맞고 시험이 틀렸다:**
      //   자율을 굳힌 시점이 `slotNo=0` 이라 **X1 도 열린 상태로 굳었다.**
      //   슬롯10 → 비트 1 · 슬롯11 → 비트 0 → 둘 다 열림 = 0b…011 = "003"
      ok(strstr(sbuf, ",003,") != NULL,
                            "★★ E1·X1 이 열린 것이 occ 비트로 나간다 (에코가 완료를 말한다)");
      // 🔴 X1 만 닫아서 **비트가 따로 움직이는지** 본다 — 하나로 뭉쳐 있으면 못 가른다
      char g2[24]; snprintf(g2, sizeof g2, "G,305,11,0,"); appendChecksum(g2, (uint8_t)strlen(g2));
      handleFrameLine(g2);
      char sb2[64]; buildStatus(sb2, sizeof sb2);
      printf("      S(E1 열림·X1 닫힘) = %s\n", sb2);
      ok(strstr(sb2, ",002,") != NULL,
                            "★★ X1 만 닫으면 002 — 두 비트가 **독립으로** 움직인다");
    }

    // ⑤ 🔴 모르는 idx — **조용히 안 버린다. 거절도 ACK 이 온다**
    ackQ.clearQueue();
    snprintf(g, sizeof g, "G,303,99,1,"); appendChecksum(g, (uint8_t)strlen(g));
    handleFrameLine(g);
    ok(ackQ.pending() == 1,      "★★ 모르는 idx 도 ACK 를 보낸다 (ack_timeout 을 안 만든다)");
    { int8_t h = ackQ.find(303);
      ok(h >= 0 && ackQ.at(h).result == 3,
                            "★★ result=3 (수행할 수 없다) — 새 코드를 안 만들었다"); }

    // ⑥ 실물 자리를 idx 로 조작하려 하면 거절 — 차단봉이 아니다
    snprintf(g, sizeof g, "G,304,3,1,"); appendChecksum(g, (uint8_t)strlen(g));
    handleFrameLine(g);
    { int8_t h = ackQ.find(304);
      ok(h >= 0 && ackQ.at(h).result == 3,
                            "★★ 실물 센서 자리(idx 3)는 조작 대상이 아니다"); }

    // ⑦ 멱등 — 같은 rid 를 다시 받으면 같은 답
    ackQ.clearQueue();
    snprintf(g, sizeof g, "G,302,10,0,"); appendChecksum(g, (uint8_t)strlen(g));
    handleFrameLine(g);
    ok(gates.isOpen(0, slotNo),        "★★ 같은 rid 는 상태를 다시 안 바꾼다 (열린 채 유지)");
    ok(ackQ.pending() == 1,      "★ 그래도 ACK 는 다시 보낸다");

    // 🔴 **G 의 ACK 가 전선에서 실제로 어떻게 보이나** — socket 이 명세에 적을 값이다.
    //   ⚠ "코드가 이렇게 생겼다"가 아니라 **나가는 바이트로** 확인한다.
    {
      // ⚠ **조건부 ok 를 쓰지 않는다.** 조건이 거짓이면 조용히 아무것도 안 하고
      //   PASS 수만 그대로여서 **검사된 것처럼 보인다.**
      wifi.refusePrompt = false;
      awaitingSendOk = false; sendOkT1Passed = false; inSend = false;
      netOnline = true; lastSendEndAt = 0;
      regPending = false; regAfterS = false;
      ackQ.clearQueue();
      ackQ.clearCache();
      ackQ.put(401, 'G', '1', 0);       // idx 11 → '1' (= 11 % 10)
      ackQ.push(401);
      size_t before2 = wifi.sentLines.size();
      uint8_t aa = 0; uint16_t bb = 0;
      const bool sent2 = sendSlotBatch(&aa, &bb);
      ok(sent2 && wifi.sentLines.size() == before2 + 1,
                            "★ G 의 ACK 가 실린 배치가 나갔다");
      const std::string& ln = wifi.sentLines.back();
      printf("      G 의 ACK 전선 = %s\n", ln.c_str());
      ok(ln.find("A,401,G1,0,") != std::string::npos,
                            "★★ 전선 형식이 A,<rid>,G<d>,<result>, 다 (d = idx %% 10)");
      { char sk[] = "SEND OK"; handleLine(sk); }
    }

    gates.manual = false; gates.state = 0; node.occMask = 0;
  }

  // ── [37] 시뮬 점유가 지형과 맞는다 — A_i 와 B_i 는 같은 자리다 (REQ-0270) ──────
  //   🔴 **한쪽만 움직이면 서버가 한 자리에서 모순된 두 값을 본다.** socket 의 `센서갈림` 이
  //     그 표지인데 **그건 서버 쪽 계측기**다. 장치 쪽에도 감시를 둔다 —
  //     §"조건을 적었으면 그것을 보는 감시를 **같은 자리에** 만들어라".
  {
    printf("\n[37] 시뮬 점유의 지형 정렬 (A_i · B_i 짝)\n");
    const uint8_t H = SLOT_N / 2;
    auto paired = [&](uint16_t m) {
      for (uint8_t i = 0; i < H; i++)
        if (((m >> i) & 1) != ((m >> (i + H)) & 1)) return false;
      return true;
    };

    node.simOcc = (uint16_t)((1U << 1) | (1U << 6)
                      | (1U << 2) | (1U << 7)
                      | (1U << 3) | (1U << 8));      // setup() 과 같은 초기값
    ok(paired(node.simOcc),      "★★ 부팅 초기값이 짝 단위다 (A_i == B_i 전부)");

    // 🔴 **무작위 토글을 여러 번 돌려도 짝이 유지되는가** — 한 번만 보면 우연히 통과한다
    node.resMask = 0; node.testArmed = false; node.ovrActive = 0;
    randomSeed(12345);                                // 결정적으로 돌린다
    bool held = true;
    for (int k = 0; k < 200; k++) { node.simStep(); if (!paired(node.simOcc)) { held = false; break; } }
    ok(held,                "★★ 무작위 토글 200회를 돌려도 짝이 유지된다");

    // 예약→점유 경로도 짝을 채우는가 (1순위 분기)
    node.simOcc = 0; node.resMask = (uint16_t)(1U << 0);        // 자리 1 을 예약
    node.simStep();
    ok(((node.simOcc >> 0) & 1) && ((node.simOcc >> H) & 1),
                            "★★ 예약→점유도 짝을 함께 채운다 (A1 과 B1 이 같이 선다)");
    node.simOcc = 0; node.resMask = 0;
  }

  printf("\n=== 결과: %d PASS / %d FAIL ===\n\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
