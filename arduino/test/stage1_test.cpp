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
  // ★ 2단계 상태도 반드시 여기서 지운다.
  //   빠뜨렸더니 **[1] 의 성공이 [2]~[7] 로 새어** 이후 시험이 전부 "SEND OK 대기 중"으로
  //   건너뛰어졌고 9건이 무더기로 실패했다. 원장 §4.5 가 경고한 시험 간 격리 함정 그대로다.
  //   ⚠ 새 전역 상태를 추가하면 **여기도 같이 늘려라.** 안 그러면 다음 사람이 같은 곳에서 넘어진다.
  awaitingSendOk = false;
  sendOkWaitFrom = 0;
  sendSkips = sendOkTimeouts = sendFails = 0;
  espResets = 0;
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

  // ── [12] 안전망 — SEND OK 가 영영 안 와도 상한이 풀어 준다 ─────────────────
  // 이것이 없으면 2단계는 1단계보다 **더 나쁜 정지**를 만든다(설계 §4).
  printf("\n[12] 안전망 — SEND OK 가 안 와도 상한(3초)이 풀어 준다\n");
  arm(nullptr);
  sendLine(FRAME);                          // 대기 시작
  ok(awaitingSendOk,            "대기 중이다");
  sendOkWaitFrom = millis() - (SEND_OK_TIMEOUT_MS + 1);   // 상한을 넘긴 것으로 둔다
  bool r12 = sendLine(FRAME);
  ok(r12,                       "★★ 상한을 넘기면 대기를 풀고 실제로 보낸다 (영구 정지 방지)");
  ok(sendOkTimeouts == 1,       "상한 초과가 진단에 기록된다 — 잦으면 그 자체가 발견이다");

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
  netTick(millis());
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

  printf("\n=== 결과: %d PASS / %d FAIL ===\n\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
