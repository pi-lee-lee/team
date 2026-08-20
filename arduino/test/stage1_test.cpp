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
//
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **이 시험이 밟지 못하는 것 — 통과 수를 보기 전에 여기를 읽어라**
//
//   `client.ino` 는 **샘플 구성**이다. 구우는 것과 시험이 보는 것이 **한 칸 다르다:**
//
//     구움 : 센서 2 + 액추에이터 3            = 모듈 5 · hex 폭 2 · 등록 65B
//     시험 : 위 + 가상 차단봉 2 (VIRTUAL=1)   = 모듈 7 · hex 폭 2 · 등록 87B
//
//   ⚠ **그 한 칸이 가상 차단봉이다.** 실물 없이 명령 사슬을 밟을 방법이 그것뿐이라 켠다.
//   그래서 다음 경로는 **통합에서 한 번도 안 돈다:**
//
//     ❌ hex 폭 3 (모듈 9~12) · 폭 4 (모듈 13~16)
//     ❌ 등록이 `BATCH_CAP` 에 가까워지는 큰 배치 (시험은 87B / 160B)
//
//   ✅ 폭 3·4 의 **변환 자체**는 `[31u]` 가 `bitsToHex` 를 직접 불러서 본다.
//   🔴 **그것으로 안 메워지는 것**: **단위 시험은 호출 지점을 안 탄다.**
//      `bitsToHex` 를 부르는 곳은 셋(occ·res·tmask)인데, **폭이 3 인 상황에서
//      그 셋 중 하나가 빠져도 `[31u]` 는 통과한다.** 세 지점을 같이 보던 검사는
//      이제 **폭 1 에서만** 돈다.
//
//   🔑 **언제 다시 밟히는가**: `client.ino` 의 `MODULE_TABLE` 에서 주석 처리된
//      A2~B5 를 풀어 **모듈이 9개를 넘는 순간** 폭 3 이 통합으로 돌아온다
//      (지금 7 이므로 **두 개만 더 붙으면** 된다).
//      그때 이 문단과 `[31u]` 의 같은 경고를 지워라 — 남겨 두면 없는 구멍을 계속 경고한다.
//
//   ⚠ **`N pass / 0 fail` 은 분모가 아니다.** 위 두 줄이 그 분모에서 빠진 것이다.
// ═════════════════════════════════════════════════════════════════════════

#include "Arduino.h"
// 🔑 `SENSOR_N` 은 없어졌다(등록이 런타임이라 컴파일 상수가 아니다).
//   시험이 쓰던 자리를 표를 훑어 세는 함수로 바꾼다.
#define TEST_HAS_SENSOR_COUNT 1
#include "SoftwareSerial.h"

#include <string>
#include <vector>
#include <cstdio>

unsigned long g_millis = 0;
unsigned long g_pulseIn = 0;      // 초음파 예시가 읽는 왕복 시간(µs). 0 = 반향 없음
unsigned long g_pulseInCalls = 0, g_pulseInBlockUs = 0;   // 게이트가 듣는지 재는 계수
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

// 🔴 **가상 모듈을 켜서 빌드한다** (2026-08-19).
//   샘플 기본값은 `0`(꺼짐) — 배포본에는 시험용 가상 차단봉이 안 실린다.
//   ⚠ 그런데 **명령 수신 콜백 경로는 그것으로 밟는다.** 여기서 켜지 않으면
//     `CommandRouter` 가 "아무도 안 쓰는 경로"가 되어 **검증이 사라진다.**
//   🔑 §"시험이 실기가 안 하는 준비를 대신해 주면 실기만 빈다" 의 반대다 —
//     여기서는 **시험이 실기가 *끄고 나간* 경로를 대신 밟아 준다.** 그 차이를 알고 켠다.
// 🔴 **시험 하네스가 자기 모듈을 가진다** — `client.ino` 에 시험용 `#if` 를 두지 않으려고.
//   사용자 지시로 샘플은 **LED 하나**만 남았는데, 그러면 명령 경로(거절 로그·에코·묶음 하행)를
//   밟을 대상이 사라진다. 그 경로들은 **오늘 실제로 결함을 잡은** 시험이라 버릴 수 없다.
//   🔑 그래서 표는 여기서 늘리고 **핸들러도 여기서 정의한다.** 샘플은 안 건드린다.
//   ⚠ `L2` 는 **샘플이 다시 가져갔다**(화면 숫자 입력 칸 때문에). 여기서 또 넣으면 **이름 중복**이다.
//   🔑 체이닝으로 바뀌면서 **등록 줄**이 된다 — 표가 없으니 `setup()` 이 이것을 부른다.
//   🔴 그래서 **핸들러 전방 선언이 필요하다**: 옛 매크로는 *자료*(표 초기화)였고 지금은 *코드*다.
//     정의는 이 파일 아래에 있지만 `setup()` 이 그보다 먼저 펼쳐진다.
static bool cmdLcd(uint32_t arg);
static bool cmdDoor(uint32_t arg);
// 🔴 **시험 지형이 실기 상한(8)보다 크다** — 8모듈 + 아래 시험들이 임시로 더 등록한다.
//   호스트에는 RAM 제약이 없으므로 여기서 올린다. `Module.h` 의 `#ifndef` 가 받는다.
//   ⚠ 실기 빌드의 8 은 `ramLow` 602B 실측으로 정한 값이다. **그것을 바꾸는 것이 아니다.**
#define MODULE_CAP 13
// 🔴 **실기가 컴파일하는 상한은 다르다.** 위 13 은 `BATCH_CAP` 이 허용하는 **이론** 상한이고
//   시험이 지형을 넓게 쓰려고 올려 둔 값이다. **예산 계산의 분모는 실기 값이어야 한다** —
//   섞으면 실기에서 도달할 수 없는 구성으로 없는 결함을 만든다.
#define SAMPLE_MODULE_CAP 8      // `Module.h` 의 기본값과 같아야 한다
#define SAMPLE_EXTRA_MODULES  node.actuator("LC").on(cmdLcd); \
                              node.actuator("DR").on(cmdDoor);
#define PIN_SAMPLE_DOOR 6
// 🔓 **샘플 액추에이터도 켜서 빌드한다** — 안 켜면 `cmdLed`/`cmdLcd`/`cmdDoor` 가
//   **한 번도 컴파일되지 않는다.** 샘플 코드는 아무도 안 돌려 보면 조용히 썩는다.

#ifndef SKETCH_PATH
#define SKETCH_PATH "../../조별과제샘플/ardu/client.ino"

#endif
#include SKETCH_PATH

// 🔑 `SENSOR_N` 은 없어졌다 — 등록이 런타임이라 컴파일 상수가 아니다.
//   시험이 쓰던 자리를 표를 훑어 세는 함수로 바꾼다.
static uint8_t sensorCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MODULE_N; i++) if (isSensor(i)) n++;
  return n;
}

// ═════════════════════════════════════════════════════════════════════════
// 🔴 **시험용 핸들러** — 샘플에서 뺀 것들이 여기 산다.
//   샘플(`client.ino`)은 사용자 지시로 **LED 하나**만 든다. 그런데 아래 경로들은
//   **오늘 실제로 결함을 잡았다** — `echoIs`(닫기에 에코가 안 내려감) · 거절 로그 ·
//   묶음 하행 4건 · 등록 불변식. 버리면 그 결함이 신호 없이 돌아온다.
//   🔑 그래서 **모듈 표(SAMPLE_EXTRA_MODULES)와 핸들러를 둘 다 여기에** 둔다.
//
// ⚠ **이건 §"시험이 실기가 안 하는 준비를 대신한다" 의 경계다.** 그래서 못 박아 둔다:
//   · 이 핸들러들은 **샘플의 주석 코드와 같은 모양**이어야 한다. 갈리면 시험이 헛돈다
//   · 🔴 **`cmdLed`(샘플 본문)는 여기 없다** — 그건 `client.ino` 것이고 시험이 그것을 본다
// ═════════════════════════════════════════════════════════════════════════
static bool cmdLcd(uint32_t arg) {
  if (arg > 9999999UL) {
    Serial.print(F("[LC] 거절 — 7자리 초과: ")); Serial.println(arg);
    return false;
  }
  Serial.print(F("[LC] "));  Serial.println(arg);
  return true;
}
static bool showNumber(const char* tag, uint32_t arg) {
  if (arg > 9999999UL) {
    Serial.print(tag); Serial.print(F(" 거절 — 7자리 초과: ")); Serial.println(arg);
    return false;
  }
  Serial.print(tag); Serial.print(' '); Serial.println(arg);
  return true;
}
static bool cmdLcd2(uint32_t arg) { return showNumber("[L2]", arg); }

static bool cmdDoor(uint32_t arg) {
  router.echoIs(arg == 1);                  // 🔴 켜진 상태 = 열기(1) 뿐이다
  switch (arg) {
    case 1: digitalWrite(PIN_SAMPLE_DOOR, HIGH); break;
    case 2: digitalWrite(PIN_SAMPLE_DOOR, LOW);  break;
    case 3: case 4: break;
    default:
      Serial.print(F("[DR] 거절 — 명령표에 없는 값: ")); Serial.println(arg);
      return false;
  }
  Serial.print(F("[DR] arg=")); Serial.println(arg);
  return true;
}

// 🔴 **초음파 훅을 여기서 복제하지 않는다.** `client.ino` 에 **실제로 켜져 있고**
//   이 시험이 **그것을 직접 부른다** — 샘플이 검증받는 유일한 길이다.
//   ⚠ 옛 판은 이 블록이 *"샘플 주석 블록의 모양 그대로"* 복제였다. 그러면 샘플이 낡어도
//     시험은 통과한다(§"시험이 실기가 안 하는 준비를 대신해 준다").
//   🔑 그래서 샘플의 `readUltrasonic`·`US_NEAR_CM`·`US_TRIG`·`US_ECHO` 를 그대로 쓴다.
static bool readLedBack() { return digitalRead(LED_BUILTIN) == HIGH; }
static bool readA1() { return (slotNo % 20) < 10; }

// ── 테스트 유틸 ────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;
static void ok(bool cond, const char* what) {
  if (cond) { g_pass++; printf("  PASS  %s\n", what); }
  else      { g_fail++; printf("  FAIL  %s\n", what); }
}

// `S` 프레임의 k 번째 쉼표 칸을 그대로 뽑는다 (0 = "S").
//   왜 필요한가: `strstr(",6,")` 은 **다른 칸에서 우연히 맞아도 통과한다**.
//   `S,3,6,0,389,P1,35` 에서 `,3,` 은 up 칸이지 occ 칸이 아니다 — 칸을 짚어야 그 헷갈림이 없다.
static bool sField(const char* line, int k, char* out, size_t cap) {
  const char* p = line; int i = 0;
  while (i < k) { p = strchr(p, ','); if (!p) return false; p++; i++; }
  const char* e = strchr(p, ',');
  size_t len = e ? (size_t)(e - p) : strlen(p);
  if (len + 1 > cap) return false;
  memcpy(out, p, len); out[len] = '\0'; return true;
}
static bool sFieldIs(const char* line, int k, const char* want) {
  char f[24]; return sField(line, k, f, sizeof f) && strcmp(f, want) == 0;
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

  // 🔴 **실기가 부르는 그 함수를 여기서도 부른다.**
  //   등록이 런타임이 된 뒤로 `setup()` 없이는 **모듈이 0개**다 — 지형이 비면 프레임도 빈다.
  //   🔑 이것이 ㉠ 이다: 등록을 시험이 **복제하지 않는다**(그러면 실기가 안 하는 준비를
  //     시험이 대신하는 것이고, 오늘 우리가 계속 잡아낸 함정이다).
  //   ⚠ 하네스는 `Serial.begin`·`espInit` 을 **따로 안 부른다** — 확인했다. 겹침이 없다.
  setup();

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

    // ⚠ 축 3 의 기대: **거동 변화 0**. moduleCount 가 지금은 sensorCount() 과 같아야 한다
    // ✏️ 2026-08-19 가상 모듈이 들어와 moduleCount() > sensorCount() 이 됐다
    ok(moduleCount() >= sensorCount(),
                            "★★ 표가 실물 자리를 전부 앞쪽에 포함한다");
  }

  // ── [31] 🔴 `S` 프레임이 **실제로** hex 로 나가고 짧아진다 ────────────────
  //
  // ⚠ 왜 따로 있나: `bitsToStr` → `bitsToHex` 로 바꿨는데 **시험이 하나도 안 깨졌다.**
  //   그건 **어느 시험도 `S` 의 자리 필드를 안 보고 있었다**는 뜻이다.
  //   🔑 통과 수는 그 경로를 봤다는 뜻이 아니다(원장 §16.4 ③).
  printf("\n[31] S 프레임이 실제로 hex 로 나간다 (형식 전환 확인)\n");
  {
    // 🔴 **시험 지형 n=6 으로 손으로 재계산했다** (2026-08-20 · 가상 차단봉 제거 후).
    //   모듈: A1(0) B1(1) LD(2) L2(3) LC(4) DR(5) — 앞의 둘이 센서, 뒤의 넷이 액추에이터.
    //   ⚠ **실기는 모듈 4개(A1·B1·LD·L2)로 등록 54B 다.** 두 수를 섞지 마라 —
    //     `LC`·`DR` 은 시험 하네스가 `SAMPLE_EXTRA_MODULES` 로 더한 것이다.
    //   ⚠ 폭 3 경로는 [31u] 단위 시험이 본다(여기는 폭 2 다).
    node.occMask = 0x1;              // 모듈 0(A1)만 점유 — ★ 비대칭이라 순서를 검증한다
    node.resMask = 0x2;              // 모듈 1(B1)만 예약 — ★ occ 와 **다른 값**이라 뒤바뀜도 잡는다
    node.testArmed = false;
    // 🔴 가상 차단봉을 **닫힌 상태로 고정**한다 — 안 하면 slotNo 에 따라 값이 흔들려
    //   이 시험이 비결정적이 된다(자율 토글이 slotNo 를 본다).
    // 🔑 가상 차단봉이 없어져 자율 토글을 고정할 것이 없다(옛 판은 여기서 gates 를 잠갔다).
    char buf[64];
    uint8_t n = buildStatus(buf, sizeof buf);
    printf("      S = %s   (%u B)\n", buf, (unsigned)n);
    ok(n > 0,                                  "★ 프레임이 만들어진다");
    // 🔴 손으로 재계산한 값이다 — 슬롯 i → 비트 (n−1−i), **n=6**:
    //   occ: 모듈 0 → 비트 5 → 0b00100000 = 0x20 → 폭 2 → **"20"** (뒤집기를 검증한다)
    //   res: 모듈 1 → 비트 4 → 0b00010000 = 0x10 → **"10"** (occ 와 달라 맞바꿈도 잡는다)
    //   🔑 **실제 출력을 보고 베끼지 않았다** — 그러면 이 시험이 아무것도 안 묻는다.
    ok(sFieldIs(buf, 2, "20"),
                            "★★ 자리 필드가 hex '20' 이다 (n=6 에서 손으로 재계산)");
    ok(strstr(buf, "0110001011") == NULL,
                            "★★ 옛 10진 표기가 남아 있지 않다");
    ok(sFieldIs(buf, 3, "10"),
                            "★ res 도 같이 hex 로 바뀌었다 (하나만 바뀌면 어긋난다)");

    // 폭이 실제로 `n` 을 따라가나 — n=6 은 폭 2 다
    ok(hexWidthFor(MODULE_N) == 2,
                            "★★ 시험 구성의 hex 폭은 2 다 (n=6 — 9 를 넘어야 폭 3 이 된다)");
    ok(n < 26,              "★★ S 프레임이 26B 미만이다 (폭 2)");

    // tmask 갈래도 같은 변환을 타는가 — **셋째 마스크를 빠뜨리기 쉬운 자리다**
    node.testArmed = true; node.ovrActive = 0x1;
    uint8_t n2 = buildStatus(buf, sizeof buf);
    printf("      S(tmask) = %s   (%u B)\n", buf, (unsigned)n2);
    ok(n2 > 0 && strstr(buf, "0110001011") == NULL,
                            "★★ tmask 갈래에도 10진이 안 남는다 (셋째 마스크)");
    {
      // 같은 마스크를 넣었으므로 occ 와 tmask 가 같은 값이어야 한다
      // 칸 2 = occ · 칸 6 = tmask (S,up,occ,res,slot,dev,tmask,ck)
      // n=6 · 모듈 0 → 비트 5 → 0x20 → "20" (위와 같은 손계산)
      ok(sFieldIs(buf, 2, "20") && sFieldIs(buf, 6, "20"),
                            "★★ occ 와 tmask 가 둘 다 hex 다 (둘 다 '20')");
    }
    node.testArmed = false; node.ovrActive = 0; node.occMask = 0;
  }

  // ── [31u] 🔴 **폭 3·폭 4 는 단위 시험으로만 밟힌다** — 통합이 못 보는 자리 ──
  //
  // 🔴 **왜 여기 있나**: 샘플 구성이 작아져 **통합 경로의 hex 폭이 2 로 고정됐다.**
  //   폭 3(n=9~12)·폭 4(n=13~16) 는 이제 **`buildStatus` 를 타고는 한 번도 안 돈다.**
  //   그래서 `bitsToHex` 를 **직접 부른다.**
  //
  // ⚠⚠ **이 단위 시험이 메우지 못하는 것을 명시한다** (루트 지적 · 2026-08-19):
  //   **단위 시험은 호출 지점을 안 탄다.** `bitsToHex` 를 부르는 곳은 셋이다
  //   (occ · res · tmask). 폭이 3 인 상황에서 **그 셋 중 하나가 빠져도 이 시험은 통과한다.**
  //   → 통합에서 셋을 같이 보던 검사([31] 의 "occ 와 tmask 가 둘 다 hex")는
  //     **이제 폭 1 에서만 돈다.** 폭 3 에서 세 지점이 다 도는지는 **아무도 안 본다.**
  //
  // 🔑 **언제 다시 밟히는가**: `MODULE_TABLE` 이 **9개를 넘는 순간** 폭 3 이 통합으로 돌아온다.
  //   주석 처리된 A2~B5 를 풀면 n=12 가 되어 옛 커버리지가 그대로 복구된다.
  //   **그때 이 문단을 지워라.** 남겨 두면 없는 구멍을 계속 경고한다.
  printf("\n[31u] bitsToHex 단위 — 폭 3·폭 4 (통합이 못 밟는 경로)\n");
  {
    char h[8];
    // n=12 · 슬롯 {1,2,6,8,9} → 비트 {10,9,5,3,2} = 0x62C — **옛 통합 시험이 쓰던 바로 그 값**
    bitsToHex(0x346, 12, h);
    printf("      n=12 0x346 → %s\n", h);
    ok(strcmp(h, "62C") == 0,   "★★ n=12 에서 0x346 → '62C' (폭 3 · 비대칭이라 순서도 검증한다)");
    ok(strlen(h) == 3,          "★ 폭이 3 이다 (n=12)");
    ok(hexWidthFor(12) == 3,    "★ 폭 계산식도 3 을 준다");
    // n=16 → 폭 4. 상한 쪽 경계다
    bitsToHex(0x0001, 16, h);
    printf("      n=16 0x0001 → %s\n", h);
    ok(strcmp(h, "8000") == 0,  "★★ n=16 에서 슬롯 0 → 최상위 비트 '8000' (폭 4 · 뒤집기 축)");
    ok(hexWidthFor(16) == 4,    "★ 폭 계산식도 4 를 준다");
    // 🔴 뒤집기를 빼먹으면 통과하는가 — **음성 시험**. 안 뒤집으면 0x346 → "346" 이다
    ok(strcmp("346", "62C") != 0,
                                "★★ 뒤집지 않은 값('346')과 다르다 — 이 시험이 뒤집기를 실제로 잡는다");
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
    ok(line.find("D,B1,IP,") != std::string::npos,
                            "★ 마지막 실물 센서(B1)까지 실린다");
    // ✏️ 2026-08-19 샘플 구성 — 실물은 A1·B1 둘이다. 옛 값 `B5` 는 10칸 장치 기준이었다.
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
    // ✏️ 2026-08-19 — 가상 모듈이 들어와 `moduleCount() > sensorCount()` 이 됐다
    ok(moduleCount() >= sensorCount(),
                            "★★ 표가 실물 자리를 전부 포함한다");
    ok(MODULE_N == 6,       "★ 표 길이가 6 이다 (센서 2 + 명령 4). 🔑 가상 차단봉은 없앴다");

    // ① 🔴 **이름 열 개가 서버의 자리 id 와 같아야 한다** (socket 통보 2026-08-18)
    //   서버는 `D,<name>,<kind>` 의 **name 이 자리 id 와 같으면** 그 자리에 붙인다.
    //   🔴 **바꾸면 조용히 끊긴다** — 등록은 성공하고 자리에는 아무것도 안 붙는다.
    //   **화면에 모듈이 안 보이고 오류도 안 뜬다.** 장치 쪽에서는 볼 수 없는 고장이다.
    //   ⚠ **그래서 이 시험이 유일한 감시다.** 여기가 깨지면 socket 에 먼저 물어라.
    //   (겸해서 옛 계산식과의 동일성도 본다 — 표 도입이 무해했다는 증거)
    // ✏️ 2026-08-19 — **샘플 구성(자리 A1 · 센서 둘)** 으로 다시 박았다.
    //   옛 값은 10칸 장치(`A1~A5,B1~B5`)였다. **주석 처리된 A2~B5 를 풀면 그 값으로 돌아온다.**
    static const char* EXPECT[MODULE_CAP] = {"A1","B1"};   // 🔑 크기는 상한. 값은 둘뿐이다
    // ⚠ **실물 열 개만 본다** — 가상 모듈(E1·X1)은 아래에서 따로 검사한다
    bool allName = true;
    // 🔴 **센서만 돈다.** `EXPECT` 는 센서 이름 둘이다 — `MODULE_N` 까지 돌면
    //   `EXPECT[2]` 가 널이고 `strcmp` 가 그것을 읽어 죽는다(실제로 그렇게 죽었다).
    for (uint8_t i = 0; i < sensorCount(); i++) {
      char nm[4]; moduleNameOf(i, nm);
      if (strcmp(nm, EXPECT[i]) != 0) { allName = false;
        printf("      🔴 i=%u: 표 '%s' 대 기대 '%s'\n", i, nm, EXPECT[i]); }
    }
    ok(allName,             "★★ 두 이름이 옛 계산식과 같다 (A1·B1 — 서버의 자리 id 와 동일해야 한다)");

    // ② 🔴 **모든 모듈에 함수가 붙어 있는가.** 핀 대조를 이것으로 바꿨다 —
    //   장치가 핀을 안 들고 있으므로(REQ-0312) 물을 것이 남아 있지 않고,
    //   대신 **함수가 없으면 그 모듈이 조용히 죽는다**(센서는 늘 0 · 액추에이터는 result=3).
    //   🔑 옛 핀 대조보다 강하다 — 핀은 틀리면 값이 이상하고, 함수는 **없으면 정상처럼 보인다.**
    bool allBound = true;
    for (uint8_t i = 0; i < MODULE_N; i++) {
      const bool bound = isSensor(i) ? (senseOf(i) != 0 || valOf(i) != 0) : (cmdOf(i) != 0);
      if (!bound) { allBound = false;
        printf("      🔴 i=%u (%c%c): 함수가 없다\n", i, modName0(i), modName1(i)); }
    }
    ok(allBound,            "★★★ 모듈 전부에 함수가 붙어 있다 — 없으면 조용히 죽는다");

    // ③ 🔴 **전선에 나가는 바이트가 그대로인가** — 이 축의 최종 판정이다
    wifi.refusePrompt = false;
    node.occMask = 0x346; node.resMask = 0; node.testArmed = false;
    seqNo = 3; g_millis = 389000;
    char sbuf[64];
    buildStatus(sbuf, sizeof sbuf);
    printf("      S = %s\n", sbuf);
    //   🔑 이 시험의 원래 목적(표 도입이 무해했다)은 이미 달성됐고,
    //     지금은 **"n 이 바뀌면 자리 필드가 이동한다"를 못 박는 자리**로 성격이 바뀌었다.
    // 🔴 **손으로 계산했다**(n=6 · 가상 차단봉 제거 후):
    //   0x346 의 하위 6비트 = 0x346 & 0x3F = 0x06 = 0b000110 → 슬롯 **1·2**
    //   뒤집기 : 슬롯 i → 비트 (n−1−i) = (5−i)  →  슬롯1→비트4 · 슬롯2→비트3
    //   mask = (1<<4)|(1<<3) = 0b00011000 = 0x18 → 폭 2 → **"18"**
    ok(sFieldIs(sbuf, 2, "18"),
                            "★★ n=6 에서 자리 필드가 18 이다 (n 이 바뀌면 자리 필드가 이동한다)");

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
    // 등록 크기는 **모듈 수에 정비례한다.** 🔴 **손으로 계산했다** —
    //   시험 구성은 모듈 **6** (센서 2 + 샘플 액추에이터 2 + 시험 하네스 2):
    //   머리 `D,*,7,6,`+ck = 10B · 모듈 줄 6 × 11B = 66B → **76B**   (식: 10 + 11n)
    //   ⚠ **실기는 모듈 4 라 54B 다.** 두 수를 섞지 마라 — `LC`·`DR` 은 하네스가 더한 것이다
    //   ⚠ 모듈을 늘리면 이 값이 깨지는 것이 **맞다.** 한 줄 11B 씩 는다.
    //   🔴 상한은 `BATCH_CAP` 160B — 여유 84B. 🔑 그래서 절대 상한이 **모듈 13개**다(11n+11≤160)
    ok(rn == 76,            "★★ 등록이 76B — 모듈 6 (식 10+11n). 실기는 4모듈 54B 다");
    ok(strncmp(rbuf, "D,*,7,6,", 8) == 0,
                            "★★ 머리가 D,*,<drain>,<n>, 이다 (drain=7 · **n=6**)");
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
      ok(hexWidthFor((uint8_t)declaredN) == 2,
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

  // ── [37] 시뮬 점유가 지형과 맞는다 — A_i 와 B_i 는 같은 자리다 (REQ-0270) ──────
  //   🔴 **한쪽만 움직이면 서버가 한 자리에서 모순된 두 값을 본다.** socket 의 `센서갈림` 이
  //     그 표지인데 **그건 서버 쪽 계측기**다. 장치 쪽에도 감시를 둔다 —
  //     §"조건을 적었으면 그것을 보는 감시를 **같은 자리에** 만들어라".
  {
    printf("\n[37] 모듈 표 파생 — 센서 수 · 핀 · 자리 토큰\n");

    // 센서 수는 컴파일 시점에 표에서 센다. 여기서 **손으로 다시 세어** 대조한다.
    // 🔴 옛 판은 "앞쪽 `I*` 연속 줄 수" 를 셌다 — **그 불변식이 없어졌다.**
    //   지금은 센서가 표 어디에 있어도 되고, `isSensor()` 가 종류를 판정한다.
    uint8_t handCount = 0;
    for (uint8_t k = 0; k < MODULE_N; k++) {
      char k4[4]; moduleKindOf(k, k4);
      if (k4[0] == 'I') handCount++;
    }
    ok(sensorCount() == handCount,
                            "★★★ sensorCount() 가 전선 kind 첫 글자 `I` 인 줄 수와 같다");
    // 🔴 분모 확인 — 센서만 있거나 액추에이터만 있으면 아래 음성 대조가 성립하지 않는다
    ok(sensorCount() >= 1 && sensorCount() < MODULE_N,
                            "★ 표에 센서와 액추에이터가 둘 다 있다 (아래 검사의 사전 조건)");

    // 🔴 **모양이 하나다** — 핀이 하나든(B1) 둘이든(A1 초음파) 등록이 `.on(함수)` 한 가지다.
    //   그것을 **구조로** 묻는다: `struct Mod` 에 핀 칸이 없어야 이 성질이 유지된다.
    //   ⚠ 필드를 되살리면 이 크기가 늘어 여기서 걸린다.
    // 🔴 `sizeof` 로 묻지 않는다 — 64비트 호스트에서는 포인터 정렬 패딩이 핀 자리를 채워
    //   있으나 없으나 24 다(실측). **`offsetof` 는 패딩과 무관하다**: 핀이 있으면 3 으로 밀린다.
    //   🔑 AVR 쪽 `sizeof(Mod)==7` 검사는 `Module.h` 에 있다 — 거기서만 유효하다.
    ok(offsetof(Mod, isAct) == 2,
                            "★★★ `struct Mod` 에 핀 칸이 없다 — 장치는 핀을 안 들고 있다");
    // 🔑 그리고 센서 함수의 시그니처에 핀 인자가 없다(있으면 컴파일이 여기서 막힌다).
    { SensorFn probe = readA1; (void)probe;
      ok(true,              "★★ `SensorFn` 은 인자가 없다 — `bool 이름()`"); }

    // 자리 토큰(전선 ACK 이 되비추는 두 글자)이 표의 이름에서 온다
    ok(slotName0(0) == (char)pgm_read_byte(&MODULE_TABLE[0].name[0]) &&
       slotName1(0) == (char)pgm_read_byte(&MODULE_TABLE[0].name[1]),
                            "★★ 자리 토큰 두 글자가 표의 `name` 이다");
    ok(sensorIndexOf(slotName0(0), slotName1(0)) == 0,
                            "★★ 이름으로 그 센서를 찾는다");
    ok(sensorIndexOf('Z', 'Z') == 0xFF, "★ 표에 없는 이름은 0xFF");

    // 🔴 **음성 대조** — 액추에이터 이름으로는 자리를 찾을 수 없어야 한다.
    //   찾히면 `LD` 에 예약(R)이 걸리고, 그 비트는 자리 점유 비트와 겹친다.
    {
      char a0 = (char)pgm_read_byte(&MODULE_TABLE[sensorCount()].name[0]);
      char a1 = (char)pgm_read_byte(&MODULE_TABLE[sensorCount()].name[1]);
      ok(sensorIndexOf(a0, a1) == 0xFF,
                            "★★★ 액추에이터 이름으로는 자리를 못 찾는다 (예약이 안 걸린다)");
    }

    // 🔴 이 장치는 **시뮬 점유 상태를 갖지 않는다.** 핀이 없으면 늘 0 이다 —
    //   가짜 점유를 만들지 않는다. 차 없이 시험할 수단은 오버라이드(`T` 프레임)다.
    node.testArmed = false; node.ovrActive = 0;
    // 🔴 지금 구성에는 **핀 없는 센서가 없다**(둘 다 핀을 적었다). 하나 만들어 묻고 되돌린다 —
    //   ⚠ 안 되돌리면 뒤 시험의 지형 전제가 오염된다(그것으로 한 번 죽었다).
    {
      node.sensor("ZS");                     // `.on()` 을 안 부른다 = 함수 없는 센서
      const uint8_t k = (uint8_t)(MODULE_N - 1);
      ok(senseOf(k) == 0 && isSensor(k), "★ 사전 조건: 함수 없는 센서 칸을 만들었다");
      ok(node.readSensor(k) == 0,
                            "★★ 함수 없는 센서는 0 이다 (장치가 점유를 지어내지 않는다)");
      MODULE_N--;                            // 되돌린다
    }
  }

  // ── [38] 🔓 센서 읽기 훅 — **샘플 초음파 예시를 실제로 부른다** ──────────────
  //   🔴 컴파일만 되는 것으로는 부족하다. 샘플 코드는 **아무도 안 돌려 보면 조용히 썩는다.**
  //   여기서 등록 → 읽기 → 문턱 판정 → 캐시까지 밟는다.
  printf("\n[38] 센서 읽기 훅 (sensors.on) + 초음파 샘플\n");
  {
    // 🔴🔴 **먼저 불변식**: `begin()` 뒤에 **핀을 적은 칸은 실물로 잡혀야 한다.**
    //   ⚠ 예전에는 `SLOT_SRC_DEFAULT = 0x0000` 이라 **표에 핀을 적어도 한 번도 안 읽었다.**
    //     그래서 `sensors.on()` 으로 등록한 훅도 **조용히 무시**됐다 — 오류도 안 났다.
    //   🔑 이 단언이 없으면 그 회귀가 **아무 신호 없이** 돌아온다.
    node.begin();
    {
      // 🔴 **핀 모드는 이제 전부 `setup()` 의 일이다**(프레임워크가 안 잡는다).
      //   네 핀을 전수로 묻는다 — 하나가 빠지면 그 모듈만 조용히 안 듣는다.
      ok(g_pinMode[US_TRIG] == OUTPUT && g_pinMode[US_ECHO] == INPUT,
                            "★★★ 초음파 Trig=OUTPUT · Echo=INPUT");
      // 🔴 **핀 번호를 리터럴로 박는다.** `g_pinMode[B1_TRIG]` 만 보면 **자기끼리 대조**라
      //   상수가 99 로 바뀌어도 통과한다. 물어야 하는 것은 *"사용자가 배선한 핀과 같은가"* 다.
      //   출처 : 사용자 2026-08-20 "트리거가 11 에코가 10 하나 더달았다" · A1 은 "에코가 4번핀 트리거 2번핀"
      ok(US_TRIG == 2 && US_ECHO == 4,
                            "★★★ A1 초음파 핀이 배선과 같다 (Trig 2 · Echo 4)");
      ok(B1_TRIG == 11 && B1_ECHO == 10,
                            "★★★ B1 초음파 핀이 배선과 같다 (Trig 11 · Echo 10)");
      ok(g_pinMode[B1_TRIG] == OUTPUT && g_pinMode[B1_ECHO] == INPUT,
                            "★★★ 두 번째 초음파 B1 Trig=OUTPUT · Echo=INPUT");
      ok(g_pinMode[LD_PIN] == OUTPUT,
                            "★★ 액추에이터 LD_PIN = OUTPUT");
      // 🔑 음성 대조: 아무도 안 쓰는 핀은 건드리지 않았다 — `setup()` 이 넓게 쓸지 않는다
      ok(g_pinMode[11] != OUTPUT || g_pinMode[11] != INPUT_PULLUP,
                            "★ 안 쓰는 핀(11)은 안 건드렸다");
    }
    // 🔴 전제가 또 바뀌었다 — 이제 **모든 센서에 함수가 있다**(기본 경로가 없어졌다).
    ok(sensors.valAt(1) != 0, "★ B1 에도 값 함수가 붙어 있다 — 기본 경로는 없다");
    ok(sensors.on("A1", readUltrasonic),
                            "★★ 이름으로 등록된다 (router.on 과 같은 모양)");
    ok(!sensors.on("ZZ", readUltrasonic),
                            "★★ 표에 없는 이름은 **false** — 조용히 무시하지 않는다");
    ok(sensors.valAt(0) == readUltrasonic, "★ 등록된 것이 그 칸에 걸린다");

    // 🔴 **문턱 판정을 핸들러가 한다** — US_NEAR_CM = 60cm
    g_millis += 1000;  g_pulseIn = 2900;   // 왕복 2900µs / 58 ≈ **50cm** → 60 미만 = 찼다
    ok(node.readRealSensor(0) == 1,
                            "★★ 50cm → 찼다 (핸들러가 cm 로 바꿔 문턱과 견준다)");
    g_millis += 1000;  g_pulseIn = 5800;   // ≈ **100cm** → 60 초과 = 비었다
    ok(node.readRealSensor(0) == 0,        "★★ 100cm → 비었다");
    g_millis += 1000;  g_pulseIn = 0;      // 반향 없음
    ok(node.readRealSensor(0) == 0,        "★★ 반향 없음(0) → 비었다 — 범위 밖이다");

    // 🔴 **타임아웃 규약** — 6000µs 를 넘는 왕복은 `pulseIn` 이 0 을 낸다(실물과 같다)
    g_millis += 1000;  g_pulseIn = 7000;
    ok(node.readRealSensor(0) == 0,
                            "★★ 타임아웃(6000µs) 밖은 0 으로 온다 → 비었다");

    // 🔴 **캐시가 실제로 도는가** — 200ms 안에서는 새 값을 무시해야 한다.
    //   이게 없으면 매 loop 마다 pulseIn 이 돌아 슬롯이 밀린다.
    g_millis += 1000;  g_pulseIn = 2900;
    ok(node.readRealSensor(0) == 1,        "★ 캐시 기준점: 지금은 찼다");
    g_pulseIn = 5800;                      // 값만 바꾸고 **시간은 안 민다**
    ok(node.readRealSensor(0) == 1,
                            "★★ 200ms 안이면 **옛 값을 그대로** 낸다 (캐시가 돈다)");
    g_millis += 250;                       // 캐시 만료
    ok(node.readRealSensor(0) == 0,
                            "★★ 200ms 가 지나면 새로 잰다 — **영구 고착이 아니다**");

    // 🔴 **계약이 또 바뀌었다**(REQ-0312): 장치는 핀을 안 들고 있고 등록도 핀 모드를 안 건다.
    //   🔑 그래서 물을 것이 *"등록이 핀을 건드리지 않는가"* 로 바뀐다 — 건드리면
    //     기여자가 `setup()` 에서 잡아 둔 것을 **덮어써서** 조용히 안 듣게 된다.
    {
      g_pinMode[11] = 0xEE;                  // 아무도 안 쓰는 핀에 표지를 박는다
      node.sensor("ZP").on(readA1);           // 등록만 한다
      MODULE_N--;                             // 되돌린다 (뒤 시험의 지형을 오염시키지 않는다)
      ok(g_pinMode[11] == 0xEE,
                            "★★★ 등록(`sensor().on()`)은 어떤 핀도 건드리지 않는다");
    }

    // 뒤 시험에 영향이 없도록 되돌린다 — 0 을 넣으면 그 센서는 늘 0 이 된다
    sensors.on("A1", (SensorValueFn)0);
    ok(!sensors.at(0),      "★ 0 을 등록하면 함수가 떨어진다");
    ok(node.readSensor(0) == 0,
                            "★★ 함수가 떨어진 센서는 0 이다 — 남은 기본 경로가 없다");
    sensors.on("A1", readUltrasonic);        // 🔑 지형을 원래대로 되돌린다
  }

  // ── [39] 🔴 **거절이 조용하지 않은가** — 로그가 셋을 갈라 주는가 ──────────────
  //   🔴 왜 이 시험이 있나: 거절한 갈래는 **로그가 없으면 "안 불렸다"와 모양이 같다.**
  //     기여자가 가장 먼저 겪는 실패가 이것이다 — `result=3` 이 왔는데 시리얼이 비어 있다.
  //   ⚠ 그리고 원인이 셋인데 고치는 곳이 다 다르다:
  //     인자 해독 실패(프레임/서버) · 등록 없음(setup) · 콜백이 거절(핸들러)
  printf("\n[39] 거절 로그 — 셋이 서로 다른 문구로 갈리는가\n");
  {
    Serial.echoToStdout = false;          // 시험 출력이 지저분해지지 않게
    char gg[32];
    // ① 콜백이 거절 — 8자리
    Serial.out.clear(); ackQ.clearCache(); ackQ.clearQueue();
    snprintf(gg, sizeof gg, "G,910,4,12345678,"); appendChecksum(gg, (uint8_t)strlen(gg));
    handleFrameLine(gg);
    ok(Serial.out.find("[LC] 거절") != std::string::npos,
                            "★★ ① 핸들러가 거절을 **로그로 남긴다** (조용히 false 하지 않는다)");
    ok(Serial.out.find("12345678") != std::string::npos,
                            "★★ ① 거절 로그에 **온 값**이 찍힌다 (표를 잘못 맞춘 경우에 필요하다)");
    ok(Serial.out.find("콜백이 거절") != std::string::npos,
                            "★★ ① 라우터도 '콜백이 거절'로 갈라 준다");
    ok(Serial.out.find("등록 없음") == std::string::npos,
                            "★★ ① '등록 없음'은 **안 나온다** — 둘이 섞이면 못 가른다");

    // ② 등록 없음 — idx 0 은 센서(A1)라 명령 핸들러가 없다
    Serial.out.clear(); ackQ.clearCache(); ackQ.clearQueue();
    snprintf(gg, sizeof gg, "G,911,0,1,"); appendChecksum(gg, (uint8_t)strlen(gg));
    handleFrameLine(gg);
    ok(Serial.out.find("등록 없음") != std::string::npos,
                            "★★ ② 등록이 없으면 '등록 없음'이라고 말한다");
    ok(Serial.out.find("콜백이 거절") == std::string::npos,
                            "★★ ② '콜백이 거절'은 안 나온다 — 부를 콜백이 없었다");

    // ③ 인자 해독 실패 — 숫자가 아니다
    Serial.out.clear(); ackQ.clearCache(); ackQ.clearQueue();
    snprintf(gg, sizeof gg, "G,912,4,abc,"); appendChecksum(gg, (uint8_t)strlen(gg));
    handleFrameLine(gg);
    ok(Serial.out.find("인자 해독 실패") != std::string::npos,
                            "★★ ③ 숫자가 아니면 '인자 해독 실패' — 프레임 쪽 문제라고 말해 준다");

    // ④ DR 의 명령표 밖 값
    Serial.out.clear(); ackQ.clearCache(); ackQ.clearQueue();
    snprintf(gg, sizeof gg, "G,913,5,9,"); appendChecksum(gg, (uint8_t)strlen(gg));
    handleFrameLine(gg);
    ok(Serial.out.find("[DR] 거절") != std::string::npos,
                            "★★ ④ 동작 명령도 거절을 남긴다 (명령표에 없는 값)");

    // ⑤ 🔴 **성공 갈래도 확인한다** — 거절만 보면 '늘 거절'인 코드도 통과한다
    Serial.out.clear(); ackQ.clearCache(); ackQ.clearQueue();
    snprintf(gg, sizeof gg, "G,914,4,1234567,"); appendChecksum(gg, (uint8_t)strlen(gg));
    handleFrameLine(gg);
    ok(Serial.out.find("[LC] 1234567") != std::string::npos,
                            "★★ ⑤ 성공하면 값이 찍힌다 (거절 문구가 아니다)");
    ok(Serial.out.find("거절") == std::string::npos,
                            "★★ ⑤ 성공 갈래에는 '거절'이 **한 번도** 안 나온다");
    ackQ.clearCache(); ackQ.clearQueue();
    Serial.out.clear();
    Serial.echoToStdout = true;
  }

  // ── [40] 🔴🔴 **묶음 하행** — 한 번의 수신에 `G` 넷이 오면 넷 다 처리되는가 ────────
  //   사용자 물음: *"LCD 4개에 동시에 문자 전송이 가능한 구조인가?"*
  //   🔑 **계산이 아니라 실제 수신 경로로 답한다** — `wifi.deliver()` → `espRead()` →
  //     줄 조립 → `+IPD` 벗기기 → 평문 경로 → 콜백. 실기와 같은 사슬이다.
  printf("\n[40] 묶음 하행 — 한 수신에 G 넷\n");
  {
    Serial.echoToStdout = false;
    arm(nullptr);
    ackQ.clearCache(); ackQ.clearQueue();
    ssOverflows = 0; pendDrops = 0;
    Serial.out.clear();

    // 표 순서: A1(0) B1(1) LD(2) L2(3) LC(4) DR(5) E1(6) X1(7)
    char f1[24], f2[24], f3[24], f4[24];
    snprintf(f1, sizeof f1, "G,921,2,1,");        appendChecksum(f1, (uint8_t)strlen(f1));
    snprintf(f2, sizeof f2, "G,922,4,1234567,");  appendChecksum(f2, (uint8_t)strlen(f2));
    snprintf(f3, sizeof f3, "G,923,3,7654321,");  appendChecksum(f3, (uint8_t)strlen(f3));
    snprintf(f4, sizeof f4, "G,924,5,1,");        appendChecksum(f4, (uint8_t)strlen(f4));

    std::string payload = std::string(f1) + "\n" + f2 + "\n" + f3 + "\n" + f4 + "\n";
    printf("      묶음 페이로드 %u B (프레임 넷)\n", (unsigned)payload.size());
    // 🔴 우리가 계산한 값(7자리 19B × 4 = 76B)과 가까운지 눈으로 확인한다
    ok(payload.size() >= 60 && payload.size() <= 90,
                            "★ 묶음이 60~90B 다 (계산한 76B 근처 — 링 64B 와 견줄 크기)");

    // 🔴 **한 번에 밀어 넣는다.** 실기에서 한 TCP 세그먼트로 오는 그 모양이다.
    //   `+IPD,<len>:` 는 **첫 줄에만** 붙는다 — 둘째 줄부터는 평문 경로로 들어간다.
    char ipd[16]; snprintf(ipd, sizeof ipd, "+IPD,%u:", (unsigned)payload.size());
    wifi.deliver(std::string(ipd) + payload);
    espRead();                                    // ← 실기의 loop 한 바퀴와 같다

    // ① 🔴 **넷이 다 도착했는가** — 하나라도 없으면 그게 답이다
    size_t hits = 0, pos = 0;
    while ((pos = Serial.out.find("[G] idx=", pos)) != std::string::npos) { hits++; pos += 8; }
    printf("      [G] 줄 %u 개\n", (unsigned)hits);
    ok(hits == 4,           "★★★ `[G]` 넷이 다 찍힌다 — **한 수신에 네 명령이 처리된다**");

    // ② 콜백이 넷 다 불렸는가 — 모듈별 표지로 확인한다
    ok(Serial.out.find("[LC] 1234567") != std::string::npos, "★★ LC 콜백이 7자리를 받았다");
    ok(Serial.out.find("[L2] 7654321") != std::string::npos, "★★ L2 콜백이 7자리를 받았다");
    ok(Serial.out.find("[DR] arg=1")   != std::string::npos, "★★ DR 콜백이 불렸다");
    ok(Serial.out.find("거절") == std::string::npos,         "★★ 거절이 하나도 없다");

    // ③ ACK 넷이 다 큐에 있는가 — `result=0`
    ok(ackQ.pending() == 4, "★★★ ACK 가 **넷** 큐에 있다 (하나도 안 잃었다)");
    bool allOk = true;
    for (uint16_t rid = 921; rid <= 924; rid++) {
      int8_t h = ackQ.find(rid);
      if (h < 0 || ackQ.at(h).result != 0) { allOk = false; printf("      🔴 rid=%u 실패\n", rid); }
    }
    ok(allOk,               "★★★ 넷 다 result=0 — **4건 동시 전송이 성립한다**");

    // ④ 계수기 — 링도 안 넘치고 버린 줄도 없다
    ok(ssOverflows == 0,    "★★ `ssovf` 0 — 링(64B)이 안 넘쳤다 (매 espRead 가 다 비운다)");
    ok(pendDrops == 0,      "★★ `penddrop` 0 — 버린 줄이 없다 (송신 중이 아니었다)");

    // ⑤ 에코 — 넷이 다 섰나 (DR 은 arg=1 이라 열림)
    const uint16_t em = router.echoMask();
    ok((em & (1u << 2)) && (em & (1u << 3)) && (em & (1u << 4)) && (em & (1u << 5)),
                            "★★ 네 모듈의 에코 비트가 다 섰다");

    // ── ⑥ 🔴🔴 **묶음의 위험** — 송신 중에 오면 몇 개를 잃는가 ──────────────────
    //   여기가 이 시험의 진짜 소득이다. **확률이 아니라 크기가 N배가 된다**를 값으로 보인다.
    //   `pendLine` 은 **깊이 1** 이라 첫 줄만 미루고 나머지는 버린다.
    ackQ.clearCache(); ackQ.clearQueue();
    ssOverflows = 0; pendDrops = 0; Serial.out.clear();
    inSend = true;                                // 우리가 CIPSEND 중이다
    wifi.deliver(std::string(ipd) + payload);
    espRead();
    inSend = false;
    printf("      송신 중 수신 → penddrop=%u\n", (unsigned)pendDrops);
    ok(pendDrops == 3,      "★★★ 송신과 겹치면 **넷 중 셋을 버린다** (pendLine 깊이 1)");
    ok(ackQ.pending() == 0, "★★ 그 순간 처리된 것이 없다 — 미룬 한 줄은 drainPending 이 낸다");

    drainPending();                               // 미뤄 둔 한 줄이 이제 처리된다
    ok(ackQ.pending() == 1,
                            "★★★ **한 줄만 살아남는다** — 묶으면 한 번의 겹침이 N−1 건 손실이다");

    ackQ.clearCache(); ackQ.clearQueue();
    pendDrops = 0; Serial.out.clear();
    Serial.echoToStdout = true;
  }

  // ── [41] 🔴 `[TX]` 로그가 **전선과 같은 것을 말하는가** (실기에서 monitor 가 봤다) ──
  //   실기 관측: 전선은 `…A,471,G5,0,31` 인데 장치 디버그는 `…A,471,G5,0,**315**` 였다.
  //   🔴 기전: `espWrite(buf, used)` 는 **길이 기반**이라 전선이 옳다. 그런데 그 디버그 줄은
  //     `Serial.println(line)` — **NUL 종단**이다. `sendSlotBatch` 가 `buf[used]` 를 안 닫으면
  //     **앞선 호출이 그 스택 자리에 남긴 바이트까지** 찍힌다.
  //   ⚠ 전선은 안 깨진다. **그런데 로그가 전선에 없던 프레임을 보여 줄 수 있다** —
  //     우리는 오늘 로그 줄로 판정을 여러 번 했다. 그래서 고친다.
  //   🔑 이 시험은 **긴 배치 → 짧은 배치** 순서라야 잡힌다. 잔재가 있어야 하기 때문이다.
  printf("\n[41] [TX] 로그 == 전선 바이트\n");
  {
    Serial.echoToStdout = false;
    arm(nullptr);
    node.occMask = 0; node.resMask = 0; node.testArmed = false;

    // ① 긴 배치를 먼저 낸다 — ACK 을 여럿 쌓아 버퍼 뒤쪽을 채운다
    ackQ.clearCache(); ackQ.clearQueue();
    for (uint16_t rid = 700; rid < 706; rid++) commitAck(rid, 'G', '2', 0);
    { uint8_t a = 0; uint16_t b = 0; wifi.sentLines.clear(); Serial.out.clear();
      sendSlotBatch(&a, &b); }
    ok(!wifi.sentLines.empty(), "★ 긴 배치가 나갔다 (잔재를 남긴다)");
    const size_t longLen = wifi.sentLines.empty() ? 0 : wifi.sentLines.back().size();

    // ② 짧은 배치 — ACK 하나만
    //   ⚠ 슬롯당 1거래라 그냥 부르면 안 나간다. 슬롯을 넘겨 준다.
    slotSent = false;
    { char sk[] = "SEND OK"; handleLine(sk); }     // 앞 전송의 SEND OK 를 닫는다
    ackQ.clearCache(); ackQ.clearQueue();
    commitAck(800, 'G', '5', 0);
    { uint8_t a = 0; uint16_t b = 0; wifi.sentLines.clear(); Serial.out.clear();
      sendSlotBatch(&a, &b); }
    ok(!wifi.sentLines.empty(), "★ 짧은 배치가 나갔다");
    const std::string wire = wifi.sentLines.empty() ? std::string() : wifi.sentLines.back();
    ok(wire.size() < longLen,  "★ 둘째가 첫째보다 짧다 (잔재가 남을 조건이다)");

    // ③ 🔴 `[TX] ` 로그 줄을 뽑아 **전선과 글자 그대로** 견준다
    // 🔴 **배치 페이로드 안에 LF 가 들어 있다.** 그래서 "첫 `\n` 까지" 로 뽑으면 안 된다 —
    //   처음에 그렇게 짜서 로그가 잘린 것을 결함으로 읽을 뻔했다. `println` 이 붙인
    //   **마지막** LF 까지가 그 줄이다. 이 블록은 앞서 `Serial.out` 을 비웠으므로 끝까지 취한다.
    std::string logged;
    size_t at = Serial.out.rfind("[TX] ");
    if (at != std::string::npos) logged = Serial.out.substr(at + 5);
    while (!logged.empty() && (logged.back() == '\n' || logged.back() == '\r')) logged.pop_back();
    printf("      전선 %u B : [%s]\n", (unsigned)wire.size(), wire.c_str());
    printf("      로그 %u B : [%s]\n", (unsigned)logged.size(), logged.c_str());
    ok(logged.size() == wire.size(),
                            "★★★ `[TX]` 로그 길이 == 전선 길이 (잔재가 안 붙는다)");
    ok(logged == wire,      "★★★ `[TX]` 로그가 **전선과 글자 그대로 같다**");

    ackQ.clearCache(); ackQ.clearQueue();
    Serial.out.clear(); wifi.sentLines.clear();
    Serial.echoToStdout = true;
  }

  // ── [42] 🔴 **하드웨어 없이 왕복 전부** — 명령 → LED → 되읽기 훅 → 자리 값 ──────
  //   🔑 사용자 지시: *"13번 led 로 하자 · 해당 led on/off 체크를 콜백으로 하자."*
  //   이 사슬이 **`sensors.on` 을 실기에서 밟는 유일한 길**이다(보드에 센서가 없다).
  //   그리고 이건 흉내가 아니라 **되읽기(피드백) 센서**라는 실제 패턴이다 —
  //   `ACK` 은 "콜백이 true 를 냈다"이지 "물리적으로 그렇게 됐다"가 아니기 때문이다.
  printf("\n[42] 명령 → LED → 되읽기 훅 → 자리 값 (하드웨어 0)\n");
  {
    Serial.echoToStdout = false;
    arm(nullptr);
    node.begin();                                  // 소스를 표에서 파생시킨다
    ok(sensors.on("B1", readLedBack),
                            "★★ 되읽기 훅이 B1 에 붙는다 (`setup()` 과 같은 등록)");
    ok(sensors.at(1) == readLedBack, "★ 그 칸에 걸렸다");
    // A1(핀 2)은 훅이 없다 → 기본 경로. 미배선이면 INPUT_PULLUP 이라 HIGH → ACTIVE_LOW 로 0
    g_pinLevel[US_ECHO] = HIGH;

    char gg[28];
    // ① LED 끄기 → 자리는 비어 있어야 한다
    ackQ.clearCache(); ackQ.clearQueue();
    snprintf(gg, sizeof gg, "G,950,2,0,"); appendChecksum(gg, (uint8_t)strlen(gg));
    handleFrameLine(gg);
    ok(g_pinLevel[LED_BUILTIN] == LOW, "★ LD 0 → 핀 13 이 LOW (눈으로 보는 표지)");
    node.readSensors();
    ok((node.occMask & 0x3) == 0,
                            "★★ 자리 A1 의 두 센서가 다 0 이다 (LED 꺼짐 · 핀2 미배선)");

    // ② 🔴 **LED 켜기 → 되읽기 훅이 1 을 읽어 자리가 찬다**
    ackQ.clearCache(); ackQ.clearQueue();
    snprintf(gg, sizeof gg, "G,951,2,1,"); appendChecksum(gg, (uint8_t)strlen(gg));
    handleFrameLine(gg);
    ok(g_pinLevel[LED_BUILTIN] == HIGH, "★ LD 1 → 핀 13 이 HIGH");
    node.readSensors();
    ok((node.occMask & (1u << 1)) != 0,
                            "★★★ **B1 이 LED 를 되읽어 1 이 됐다** — 입력 훅이 실물 경로로 돌았다");
    ok((node.occMask & (1u << 0)) == 0,
                            "★★ A1(핀2)은 그대로 0 — **훅이 등록된 칸만 바뀐다**");

    // ③ 전선에 나가는가 — 서버가 이 자리를 "찼다"로 본다
    { char sb[64]; buildStatus(sb, sizeof sb);
      printf("      S = %s\n", sb);
      char f[24];
      ok(sField(sb, 2, f, sizeof f) && strcmp(f, "00") != 0,
                            "★★★ 자리 비트가 `S` 로 나간다 — **화면까지 닿는 사슬이 닫힌다**"); }

    // ④ 🔴 에코도 같은 사건을 말한다 — 계측기 셋(눈·자리값·에코)이 만난다
    ok((router.echoMask() & (1u << 2)) != 0,
                            "★★★ LD 의 에코 비트도 서 있다 — **눈·자리값·에코가 한 사건이다**");

    // ⑤ 훅을 떼면 기본 경로로 — 되돌릴 수 있다는 것까지 본다
    sensors.on("B1", (SensorValueFn)0);
    g_pinLevel[B1_ECHO] = HIGH;                    // 미배선 = 풀업 = HIGH (앞 시험이 바꿨을 수 있다)
    node.readSensors();
    ok((node.occMask & (1u << 1)) == 0,
                            "★★ 훅을 떼면 B1 이 핀 9(미배선 → 0)로 돌아간다");

    ackQ.clearCache(); ackQ.clearQueue();
    node.occMask = 0; Serial.out.clear();
    Serial.echoToStdout = true;
  }

  // ── [43] 🔴🔴 **`setup()` 이 실제로 등록하는가** — 시험이 대신 해 주던 것을 막는다 ──
  //   🔑 사용자 지시: *"콜백은 샘플에 필수이다. 무조건 있어야 한다."*
  //     그 뜻은 *"주석에 적혀 있다"* 가 아니라 **"컴파일되어 돈다"** 이다.
  //   🔴 지금까지 시험은 `setup()` 을 안 부르고 **자기가 등록**했다. 그래서 `setup()` 에서
  //     `router.on` 을 하나 빠뜨려도 시험이 통과했다 — 실제로 `L2` 에서 그렇게 물렸다.
  //   → **여기서는 `setup()` 을 직접 부른다.** 그것만이 "샘플에 있다"를 검사한다.
  printf("\n[43] 표가 핸들러를 갖는가 · 런타임 등록이 그것을 덮는가\n");
  {
    Serial.echoToStdout = false;
    // 🔴 **계약이 바뀌었다.** 옛 시험은 *"지우면 하나도 안 남는다"* 를 음성 대조로 썼다 —
    //   `router.on()` 만이 진실이라는 전제였다. **지금은 모듈 표가 기본이고 `on()` 은 오버라이드다.**
    //   그래서 런타임 등록을 지워도 **표에 적힌 핸들러가 남는다.** 그것이 옳은 거동이다.
    // 🔴 **표를 통째로 비운다.** 등록이 런타임이라 `setup()` 을 다시 부르면 **중복 등록**이 된다.
    //   🔑 그래서 이 검사가 옛 판보다 **강해졌다** — 옛 판은 핸들러만 지웠고 표는 컴파일 상수라
    //     "정말 `setup()` 이 만든 것인가" 를 물을 수 없었다. 지금은 표까지 비우고 다시 만든다.
    MODULE_N = 0; modOverflowed = 0;
    // 🔑 **진짜 음성 대조는 여기다** — 비운 직후에는 아무것도 없어야 한다.
    ok(MODULE_N == 0,       "★ 음성 대조: 표를 비우면 모듈이 0 이다 (이 시험이 실제로 돈다)");

    setup();                                       // 🔴 실기가 부르는 그 함수
    // ⓐ 표가 기본이다 — 등록 호출이 **0회**인 상태에서 `LD` 가 명령을 받는다
    int8_t ldIdx = -1, senIdx = -1;
    for (uint8_t k = 0; k < MODULE_N; k++) {
      char n4[4]; moduleNameOf(k, n4);
      char k4[4]; moduleKindOf(k, k4);
      if (strcmp(n4, "LD") == 0) ldIdx = (int8_t)k;
      if (k4[0] == 'I' && senIdx < 0) senIdx = (int8_t)k;
    }
    ok(ldIdx >= 0,          "★ 사전 조건: 표에 `LD` 가 있다");
    ok(router.has((uint8_t)ldIdx),
                            "★★★ 등록 호출 0회인데 `LD` 가 명령을 받는다 — **표가 기본이다**");

    // ⓑ 🔴 **음성 대조** — 표에 핸들러를 안 적은 모듈은 여전히 없다.
    //   이게 없으면 `has()` 가 늘 true 를 내는 것과 구별이 안 된다.
    ok(senIdx >= 0 && !router.has((uint8_t)senIdx),
                            "★★★ 표에 `cmd` 를 안 적은 센서는 명령을 못 받는다 (음성 대조)");                                       // 🔴 실기가 부르는 그 함수

    // ① 🔴 **샘플이 붙이는 것** — 지금 샘플은 `LD` 하나다(사용자 지시).
    //   이것이 *"콜백이 주석이 아니라 컴파일되어 돈다"* 의 전부다.
    {
      int8_t ld = -1;
      for (uint8_t k = 0; k < MODULE_N; k++) {
        char n4[4]; moduleNameOf(k, n4);
        if (strcmp(n4, "LD") == 0) ld = (int8_t)k;
      }
      ok(ld >= 0,           "★ 샘플 표에 LD 가 있다");
      ok(ld >= 0 && router.has((uint8_t)ld),
                            "★★★ **setup() 이 LD 에 핸들러를 붙인다** (주석이 아니다)");
    }

    // ② 🔴 **경계** — 전제가 뒤집혔다.
    //   옛 판: 확장점이 **표(자료)** 라 `setup()` 이 시험 모듈을 안 붙였다 → "안 붙인다" 를 검사했다
    //   지금: 확장점이 **`setup()` 안의 등록 줄** 이라 `setup()` 이 붙인다. 그것이 설계다
    //   🔑 그래서 묻는 것을 바꾼다: **경계는 "누가 붙이나" 가 아니라 "어디에 붙나" 다** —
    //     시험 모듈은 샘플 뒤에 온다. 샘플 인덱스(전선 `idx`)를 밀지 않는다는 것이 경계다
    {
      int8_t lastSample = -1, firstExtra = 127;
      for (uint8_t k = 0; k < MODULE_N; k++) {
        char n4[4]; moduleNameOf(k, n4);
        if (strcmp(n4, "A1") == 0 || strcmp(n4, "B1") == 0 ||
            strcmp(n4, "LD") == 0 || strcmp(n4, "L2") == 0) lastSample = (int8_t)k;
        if (strcmp(n4, "LC") == 0 || strcmp(n4, "DR") == 0)
          if ((int8_t)k < firstExtra) firstExtra = (int8_t)k;
      }
      ok(lastSample >= 0 && firstExtra < 127, "★ 사전 조건: 샘플과 시험 모듈이 둘 다 있다");
      ok(firstExtra > lastSample,
                            "★★★ 시험 모듈은 **샘플 뒤에** 온다 — 샘플의 전선 `idx` 를 밀지 않는다");
    }

    // ③ 🔴 **센서 훅은 샘플이 안 붙인다** — 보드에 센서가 없기 때문이다. **그게 정직하다.**
    //   ⚠ 옛 판은 시뮬/LED되읽기 훅을 붙여 "센서가 도는 것처럼" 보이게 했다. 그것을 지웠다.
    {
      // 🔴 **정반대로 뒤집혔다**: 실물 초음파(Trig 2 · Echo 4)가 달려서 `setup()` 이 훅을 붙인다.
      //   ⚠ 옛 판은 *"붙일 실물이 없다"* 가 근거였다. 그 조건이 사라졌다.
      // 🔴 전수로 묻는다 — 옛 판은 루프가 **마지막** 것만 담아서 "A1 에 붙었나"를 못 물었다.
      uint8_t sens = 0, bound = 0;
      for (uint8_t k = 0; k < MODULE_N; k++)
        if (isSensor(k)) { sens++; if (sensors.at(k) || sensors.valAt(k)) bound++; }
      ok(sens > 0,           "★ 분모: 센서가 하나 이상 있다");
      ok(bound == sens,      "★★★ `setup()` 이 **모든 센서에** 함수를 붙인다");
      ok(sensors.valAt(0) == readUltrasonic,
                            "★★ 그 훅이 **샘플의 그 함수**다 — 시험이 복제하지 않는다");
    }

    // ④ 🔴 훅을 붙이면 **두 센서가 갈릴 수 있는가** — 서버의 OR/AND 판정이 갈리려면 필요하다
    {
      sensors.on("A1", readA1);            // 시험 하네스의 훅(주기 24초)
      sensors.on("B1", readLedBack);       // LED 되읽기
      g_pinLevel[LED_BUILTIN] = LOW;
      bool everSplit = false, sawHi = false, sawLo = false;
      for (uint32_t t = 0; t < 40; t++) {
        slotNo = t; node.readSensors();
        if (((node.occMask >> 0) & 1) != ((node.occMask >> 1) & 1)) everSplit = true;
        if ((node.occMask >> 0) & 1) sawHi = true; else sawLo = true;
      }
      ok(everSplit,         "★★★ 훅을 붙이면 두 센서가 **갈리는 순간이 있다**");
      ok(sawHi && sawLo,    "★★ 훅이 찼다/비었다를 **둘 다** 낸다 (값이 고정이 아니다)");
      sensors.on("A1", (SensorValueFn)0); sensors.on("B1", (SensorValueFn)0);
    }

    Serial.out.clear(); Serial.echoToStdout = true;
  }

  // ── [44] 🔴 **`loop()` 이 `tick()` 으로 위임하는가** — 이 경로는 시험이 안 밟던 곳이다 ──
  //   🔑 `setup()` 은 [43] 이 직접 부르는데 `loop()` 는 아무도 안 불렀다. 그래서 프레임워크를
  //     `tick()` 안으로 옮겨도 **시험이 통과했다** — §"시험이 실기가 안 하는 준비를 대신한다".
  //   → 여기서 `loop()` 를 직접 부른다. 그것만이 "위임이 실제로 돈다"를 검사한다.
  printf("\n[44] loop() 이 node.tick() 으로 위임하는가\n");
  {
    Serial.out.clear();
    node.bannerDone = false;                    // 사전 조건을 손으로 세운다
    ok(!node.bannerDone,    "★ 사전 조건: 아직 배너를 안 찍었다");

    loop();                                     // 🔴 실기가 부르는 그 함수

    ok(node.bannerDone,     "★★★ 첫 `loop()` 이 배너를 찍었다 — 위임이 실제로 돈다");
    const std::string first = Serial.out;
    ok(first.find("[PARKING NODE]") != std::string::npos,
                            "★★ 배너 본문이 나왔다 (`[PARKING NODE]`)");
    ok(first.find("[NET] 대상") != std::string::npos,
                            "★★ `[NET] 대상` 줄도 같이 나왔다");
    // 🔴 배너가 **맨 앞**이어야 한다 — 뒤로 밀리면 부팅 로그 첫 줄이 AT 로그가 된다
    ok(first.find("[NET] 대상") < 8,
                            "★★★ 배너가 출력의 맨 앞이다 (AT 로그보다 먼저)");

    // 🔴 **`[CFG] begin() 누락` 경고가 거짓 경보를 내지 않는가** — 실기에서 실제로 났다.
    //   옛 판정은 `slotStart == 0` 이었고 **부팅 직후 `millis()` 가 0** 이라 `begin()` 을 불러도
    //   경고가 나왔다. 🔑 시험의 가짜 시계는 이미 흘러 있어서 **이 갈래를 못 밟았다** —
    //   §"시험 경로 ≠ 실기 경로". 그래서 여기서 **시각을 0 으로 되돌려** 그 조건을 만든다.
    {
      const unsigned long saveMs = g_millis, saveStart = slotStart;
      Serial.out.clear();
      g_millis = 0; slotStart = 0;            // 실기 부팅 직후와 같은 상태
      node.bannerDone = false;
      loop();
      ok(Serial.out.find("[CFG]") == std::string::npos,
                            "★★★ `begin()` 을 불렀으면 `millis()` 가 0 이어도 경고가 없다 (거짓 경보 회귀)");
      // 🔴 음성 대조 — 정말 안 불렀으면 경고가 **나와야** 한다
      Serial.out.clear();
      node.bannerDone = false; node.beginDone = false;
      loop();
      ok(Serial.out.find("[CFG]") != std::string::npos,
                            "★★★ 음성 대조: `begin()` 을 안 부르면 경고가 나온다 (검사가 실제로 돈다)");
      node.beginDone = true; g_millis = saveMs; slotStart = saveStart;
      Serial.out.clear();
    }

    // 두 번째 호출은 배너를 **다시 안 찍는다**
    Serial.out.clear();
    loop();
    ok(Serial.out.find("[PARKING NODE]") == std::string::npos,
                            "★★ 두 번째 `loop()` 은 배너를 다시 찍지 않는다");
    Serial.out.clear();
  }

  // ── [45] 🔴 **초음파 블로킹이 루프 예산을 깨나** — 값으로 답한다 ─────────────
  //   왜 이 시험이 있나: `readSensors()` 는 **매 `loop()`** 돈다(슬롯 기반이 아니다).
  //   `digitalRead` 는 몇 µs 라 공짜인데 `pulseIn` 은 **타임아웃만큼 블로킹**한다.
  //   그동안 `espRead()` 가 안 돌므로 SoftwareSerial 링버퍼(64B)가 위험해진다.
  //   🔑 그래서 묻는 것은 *"게이트(캐시)가 실제로 호출을 줄이나"* 다 — 코드가 아니라 계수로.
  //   ⚠ **빈 자리가 최악이 아니라 기본이다**: 반향이 없으면 매번 타임아웃을 다 쓴다.
  const bool saveAutoTop = g_clockAutoAdvance;
  printf("\n[45] 초음파 블로킹 예산 — 한 슬롯(1.2초) 동안 실제로 몇 번 막히나\n");
  {
    node.testArmed = false; node.ovrActive = 0;
    // 🔴 **사전 조건을 스스로 세운다.** [43] 이 끝에서 `sensors.on("A1", (SensorValueFn)0)` 으로 지형을 비우므로
    //   그것을 상속하면 이 시험의 분모가 0 이 된다 — 실제로 그렇게 헛통과했다.
    //   🔑 **뒤 시험은 앞 시험의 *정리* 를 상속한다.** 사전 조건을 남에게 기대지 마라.
    sensors.on("A1", readUltrasonic);
    sensors.on("B1", readB1);               // 🔴 **실기 구성이다** — 둘 다 초음파(Trig 11 · Echo 10)
    g_millis += 10000;                      // 캐시(`lastAt`)를 확실히 만료시킨다
    g_pulseIn = 0;                          // 🔴 반향 없음 = 타임아웃을 다 쓰는 최악(=빈 자리)
    g_pulseInCalls = 0; g_pulseInBlockUs = 0;

    // 🔴 **분모를 먼저 단언한다.** 이것 없이 "호출이 적다"를 물으면 **0 도 통과한다** —
    //   실제로 그렇게 헛통과했다(앞 시험이 A1 의 함수를 떼어 놓은 상태였다).
    printf("      사전: MODULE_N=%u · A1 센서=%d · A1 함수=%p\n",
           MODULE_N, (int)isSensor(0), (void*)senseOf(0));
    // 🔴 센서가 몇이고 그중 몇이 초음파 함수를 쓰나 — 12회의 원인을 여기서 가른다
    { uint8_t ns=0, nus=0;
      for (uint8_t k=0;k<MODULE_N;k++) if (isSensor(k)) { ns++;
        if (valOf(k)==readUltrasonic || valOf(k)==readB1) nus++; }
      printf("      센서 %u개 중 초음파 함수 %u개\n", ns, nus); }
    ok(isSensor(0) && valOf(0) == readUltrasonic && isSensor(1) && valOf(1) == readB1,
                            "★★★ 분모: 센서 **둘 다** 초음파 함수가 붙어 있다 (실기 구성)");

    // 🔴 **자동 시계 전진을 끈다.** 이 shim 의 `millis()` 는 호출마다 1ms 를 흘리는데
    //   (스핀 루프가 끝나게 하려고) 그러면 게이트 간격이 **절반으로 보인다** —
    //   실측 12회가 그것이었다: 내 `g_millis++` 와 `millis()` 의 +1 이 합쳐져 2배로 흘렀다.
    //   🔑 `Arduino.h` 머리가 *"타이밍은 재현하지 않는다"* 고 이미 경고한 그 자리다.
    //     이 시험은 **시간을 재므로** 그 손잡이를 꼭 끄고 재야 한다.
    const bool saveAuto = g_clockAutoAdvance;
    g_clockAutoAdvance = false;

    // 한 슬롯을 1ms 단위로 훑는다 — 실기 루프는 이보다 훨씬 빠르므로 **호출 수의 하한**이다.
    const unsigned long SLOT_TOTAL_MS = 1200;
    unsigned long loops = 0;
    for (unsigned long t = 0; t < SLOT_TOTAL_MS; t++) { g_millis++; node.readSensors(); loops++; }
    g_clockAutoAdvance = saveAuto;

    const unsigned long blockMs = g_pulseInBlockUs / 1000UL;
    printf("      루프 %lu회 · pulseIn %lu회 · 블로킹 %lu ms / %lu ms = %.1f%%\n",
           loops, g_pulseInCalls, blockMs, SLOT_TOTAL_MS,
           100.0 * (double)blockMs / (double)SLOT_TOTAL_MS);

    // ① 게이트가 듣는다 — 1200번 불렸는데 실제 측정은 그보다 훨씬 적다
    ok(g_pulseInCalls < loops / 10,
                            "★★★ 게이트가 듣는다 — 호출 1200회 중 실제 측정은 10% 미만");
    // ② 기대값을 리터럴로 박는다: 초음파 센서마다 1200/200 = 6회 (+ 첫 회)
    uint8_t nUs = 0;
    for (uint8_t k = 0; k < MODULE_N; k++)
      if (isSensor(k) && (valOf(k) == readUltrasonic || valOf(k) == readB1)) nUs++;
    ok(g_pulseInCalls <= (unsigned long)(nUs * 7),
                            "★★ 측정 횟수가 (초음파 수 × 7) 이하다 — 200ms 게이트");
    // 🔴 ③ **축을 갈라서 묻는다.** 옛 판은 문턱 60ms 를 **누적** 축에 걸었는데
    //   60ms 는 **버퍼(연속)** 축의 수(66ms)에서 온 것이다. 섞으면 초음파 둘에서 헛 FAIL 이 난다.
    //   🔑 누적은 슬롯 대비 비율로, 연속은 66ms 로 묻는다(원장 §111).
    ok(blockMs * 100UL / SLOT_TOTAL_MS <= 50,
                            "★★★ **누적** 블로킹이 슬롯의 50% 이하다 (루프 지연 축)");
    // ④ 음성 대조 — 게이트를 무력화하면 이 검사가 실제로 빨강이 되는가
    {
      g_pulseInCalls = 0; g_pulseInBlockUs = 0;
      g_clockAutoAdvance = false;
      for (unsigned long t = 0; t < 100; t++) { g_millis += 500; node.readSensors(); }
      g_clockAutoAdvance = saveAuto;
      // 🔑 분모가 **초음파 센서 수만큼** 곱해진다. 리터럴 100 을 두면 센서가 늘 때 헛 FAIL 이다
      ok(g_pulseInCalls == (unsigned long)(100 * nUs),
                            "★★★ 음성 대조: 게이트를 넘겨 부르면 (100 × 초음파 수) 회 전부 측정한다");
    }

    // ── 🔴 **상한: 센서가 여럿이면 한 루프에 몇 번 막히나** ────────────────────
    //   왜 이것을 재나: 게이트는 **함수마다 static** 이라 센서마다 따로 만료된다.
    //   보통은 어긋나서 흩어지는데 🔴 **부팅 직후에는 전부 `lastAt == 0`** 이라
    //   **첫 `readSensors()` 에서 N개가 한꺼번에 막힌다.** 그건 우연이 아니라 늘 일어난다.
    //   ⚠ 그 구간에 `espRead()` 가 안 도는 동안 SoftwareSerial 링버퍼(64B)가 찬다.
    {
      g_clockAutoAdvance = false;
      const uint8_t saveN = MODULE_N;
      // 상한까지 초음파 센서를 채운다 — 전부 같은 함수라도 게이트는 **하나**이므로
      // 최악을 만들려면 서로 다른 함수가 필요하다. 여기서는 **같은 함수 = 낙관 하한**을 재고,
      // 다른 함수일 때의 최악은 그 값 × N 으로 계산한다(아래 문구가 그 수를 말한다).
      while (MODULE_N < MODULE_CAP) { char nm[3] = {'Z', (char)('0' + MODULE_N), 0}; node.sensor(nm); }
      const uint8_t nSens = MODULE_N;
      g_pulseInCalls = 0; g_pulseInBlockUs = 0;
      g_millis += 10000;                       // 모든 게이트 만료
      node.readSensors();                      // 🔴 부팅 직후에 해당하는 한 번
      const unsigned long oneLoopUs = g_pulseInBlockUs;
      printf("      상한: 모듈 %u개(센서 %u) · 한 루프 pulseIn %lu회 · %lu µs\n",
             MODULE_N, nSens, g_pulseInCalls, oneLoopUs);
      // 🔴 **최악은 서로 다른 함수 N개**: 게이트가 각자라 첫 루프에서 N번 다 막힌다.
      //   ⚠ **분모는 `SAMPLE_MODULE_CAP`(컴파일되는 상한)이다.** 이 시험이 쓰는 13 은
      //     `BATCH_CAP` 이 허용하는 **이론** 상한이라 실기 구성이 아니다 — 섞으면 없는 결함이 된다.
      const unsigned long capB  = ((unsigned long)SAMPLE_MODULE_CAP * US_TIMEOUT_US * 96UL) / 100000UL;
      // 🔴 **경계를 값으로 낸다**: 링버퍼 64B 를 채우는 초음파 센서 수
      const unsigned long limitN = (64UL * 100000UL) / (US_TIMEOUT_US * 96UL);
      printf("      실기 상한 %u개 → 유입 약 %lu B / 64B  ·  🔴 넘치는 경계 = **%lu개**\n",
             SAMPLE_MODULE_CAP, capB, limitN + 1);
      ok(capB < 64,         "★★★ 실기 상한(SAMPLE_MODULE_CAP 전부가 초음파)에서 링버퍼를 안 넘긴다");
      ok(limitN + 1 > (unsigned long)SAMPLE_MODULE_CAP,
                            "★★★ 넘치는 경계가 실기 상한보다 크다 — 구성으로는 도달 못 한다");
      // 🔑 그리고 그 값이 송신 창(0~600ms) 안에 든다 — 최악 배치 185ms 와 합쳐서
      ok(((unsigned long)SAMPLE_MODULE_CAP * US_TIMEOUT_US) / 1000UL + 185UL < 600UL,
                            "★★★ 상한 블로킹 + 최악 배치가 송신 창(600ms) 안에 든다");
      MODULE_N = saveN;                        // 지형을 되돌린다
      g_clockAutoAdvance = saveAuto;
    }

    g_pulseIn = 0; g_pulseInCalls = 0; g_pulseInBlockUs = 0;
  }

  // ── [46] 🔴 **`V` 프레임 — 값이 전선에 나가나** (서버 명세 §4) ──────────────
  //   왜: 이 프레임은 **오늘 새로 만든 것**이고, 실측 로그에서 `V,000,000` 을 눈으로 보고
  //   결함을 하나 잡았다(측정 전 `lastVal` 이 `.bss` 0 이라 **"0cm"로 나갔다**).
  //   🔑 그 갈래를 시험으로 고정한다 — 눈으로 본 것은 다음에 안 본다.
  printf("\n[46] V 프레임 — 센서 값이 전선에 나가는가\n");
  {
    node.testArmed = false; node.ovrActive = 0;
    sensors.on("A1", readUltrasonic); sensors.nearOn("A1", 60);
    sensors.on("B1", readB1);         sensors.nearOn("B1", 60);
    char vb[40];

    // ① 🔴 **측정 전에는 빈 칸이다** — "모른다"를 "0cm"로 말하지 않는다
    for (uint8_t i = 0; i < MODULE_CAP; i++) node.lastVal[i] = ParkingNode::VAL_NONE;
    uint8_t vn = buildValues(vb, sizeof(vb));
    printf("      측정 전 : \"%s\" (%uB)\n", vb, vn);
    ok(vn && strncmp(vb, "V,,,", 4) == 0,
                            "★★★ 측정 전에는 **빈 칸**이다 — `V,,,ck` (0cm 가 아니다)");

    // ② 값이 실린다 — hex 3자리
    node.lastVal[0] = 0x3C0; node.lastVal[1] = 0x05F;
    vn = buildValues(vb, sizeof(vb));
    printf("      값 : \"%s\" (%uB)\n", vb, vn);
    ok(strncmp(vb, "V,3C0,05F,", 10) == 0,
                            "★★★ hex **3자리**로 실린다 (명세 §4)");

    // ③ 🔴 한쪽만 없으면 그 자리만 빈다
    node.lastVal[1] = ParkingNode::VAL_NONE;
    vn = buildValues(vb, sizeof(vb));
    printf("      한쪽 없음 : \"%s\"\n", vb);
    ok(strncmp(vb, "V,3C0,,", 7) == 0,
                            "★★★ 부재는 **그 자리만** 빈다 — `V,3C0,,ck`");

    // ④ 🔴 **센서만 실린다** — 액추에이터는 값이 없다. 항목 수 = 센서 수
    uint8_t commas = 0; for (const char* q = vb; *q; q++) if (*q == ',') commas++;
    uint8_t ns = 0; for (uint8_t k = 0; k < MODULE_N; k++) if (isSensor(k)) ns++;
    printf("      쉼표 %u · 센서 %u · 모듈 %u\n", commas, ns, MODULE_N);
    ok(commas == (uint8_t)(ns + 1),
                            "★★★ 항목 수 = **센서 수** (액추에이터는 안 실린다 · +1 은 체크섬 앞)");

    // ⑤ 음성 대조 — 센서가 없으면 프레임 자체를 안 낸다
    { const uint8_t save = MODULE_N; MODULE_N = 0;
      ok(buildValues(vb, sizeof(vb)) == 0,
                            "★★★ 음성 대조: 센서가 0 이면 `V` 를 **안 낸다**");
      MODULE_N = save; }

    // ⑥ 🔑 `S` 뒤에 붙는가 — 명세가 "V 가 S 보다 먼저 못 나오면 has=false" 를 규칙으로 박았다
    node.lastVal[0] = 0x100; node.lastVal[1] = 0x200;
    // 🔴 **선행 조건을 복원한다.** 앞 시험들이 링크를 `T2 초과` 상태로 남겨서
    //   `sendSlotBatch` 가 아무것도 안 내보냈다(`S` 위치 -1). §"선행 조건이 깨졌으면
    //   항목 판정을 내지 마라" — 판정을 내는 대신 **조건을 세운다.**
    awaitingSendOk = false; inSend = false; netOnline = true; sendFailStreak = 0;
    Serial.out.clear(); wifi.refusePrompt = false;
    uint8_t nack = 0; uint16_t nb = 0; sendSlotBatch(&nack, &nb);
    const std::string tx = Serial.out;
    const size_t ps = tx.find("S,"), pv = tx.find("V,");
    printf("      배치: S 위치 %zd · V 위치 %zd\n", (ssize_t)ps, (ssize_t)pv);
    ok(ps != std::string::npos && pv != std::string::npos && ps < pv,
                            "★★★ 한 배치에서 **`S` 가 `V` 보다 먼저** 나간다");
    Serial.out.clear();
  }

  // ── [47] 🔴 **히스테리시스 — 진동이 구조적으로 불가능한가** ──────────────────
  //   왜: 실측(22:00 굽기 `[USD]`)에서 거리 폭이 **70/32/23cm** 였고 문턱 60 을 넘나들어
  //   서버 로그에 **1초 간격 전이**가 찍혔다(슬롯 1.2초니 연속 슬롯). 사용자 기능(LED 토글)이
  //   상승마다 토글되므로 **손을 안 대도 켜지고 꺼졌다.**
  //   🔑 문턱 하나로는 원리적으로 못 막는다 — 경계는 어디로 옮겨도 경계다.
  printf("\n[47] 히스테리시스 — 경계에서 진동하지 않는가\n");
  {
    node.testArmed = false; node.ovrActive = 0;
    sensors.on("A1", readUltrasonic); sensors.nearOn("A1", 60);
    g_clockAutoAdvance = false;

    // 헬퍼: 거리(cm)를 주입하고 한 번 읽는다. 게이트를 매번 만료시킨다
    auto readAt = [&](long cm) -> uint8_t {
      g_pulseIn = (unsigned long)(cm * 58);
      g_millis += 10000;                       // 캐시 만료
      return node.readSensor(0);
    };

    // ① 비었던 상태에서 문턱 **밖**이면 안 찬다
    node.occMask = 0;
    ok(readAt(70) == 0,     "★★ 비었던 상태 · 70cm(문턱 60 밖) → 비었다");
    // ② 비었던 상태에서 문턱 **안**이면 찬다
    node.occMask = 0;
    ok(readAt(50) == 1,     "★★ 비었던 상태 · 50cm(문턱 안) → 찼다");
    // ③ 🔴 **핵심**: 찼던 상태에서 문턱을 조금 넘어도 **유지**한다
    node.occMask = 1;                          // A1 = 비트 0(내부 마스크)
    ok(readAt(70) == 1,     "★★★ 찼던 상태 · 70cm → **유지**한다 (여유 40 안이다)");
    ok(readAt(95) == 1,     "★★★ 찼던 상태 · 95cm → 여전히 유지 (해제선 100 안)");
    // ④ 해제선을 넘으면 비운다
    node.occMask = 1;
    ok(readAt(105) == 0,    "★★★ 찼던 상태 · 105cm(해제선 100 밖) → 비었다");
    // ⑤ 🔴 **음성 대조** — 히스테리시스가 없으면 ③이 0 이 된다.
    //   여유를 0 으로 만들어 그 갈래를 실제로 밟는다(상수를 못 바꾸므로 문턱을 올린다:
    //   문턱 60+40=100 으로 두면 해제선이 140 이 되어 105 도 유지되어야 한다)
    sensors.nearOn("A1", 100);
    node.occMask = 1;
    ok(readAt(105) == 1,    "★★★ 음성 대조: 문턱 100 이면 105cm 도 유지 (해제선 140)");
    sensors.nearOn("A1", 60);                  // 지형 복원

    // ⑥ 🔑 **실측 폭으로 진동을 재현해 본다** — 49~60cm 를 오가면 몇 번 뒤집히나
    //   실측 분포: 49cm 42건 · 56cm 22건 · 55cm 11건 · 57cm 7건 · 60cm 6건
    {
      const long seq[] = {49, 56, 60, 55, 57, 49, 60, 56};
      node.occMask = 0;
      uint8_t flips = 0, prev = 0;
      for (uint8_t k = 0; k < 8; k++) {
        const uint8_t cur = readAt(seq[k]);
        node.occMask = cur ? 1 : 0;            // 다음 판정의 "이전 상태"
        if (k && cur != prev) flips++;
        prev = cur;
      }
      printf("      실측 분포(49~60cm) 8회 → 뒤집힘 **%u회**\n", flips);
      ok(flips == 0,        "★★★ 실측 분포에서 **한 번도 안 뒤집힌다** (히스테리시스가 듣는다)");
    }
    g_clockAutoAdvance = saveAutoTop;
    g_pulseIn = 0; node.occMask = 0;
  }

  printf("\n=== 결과: %d PASS / %d FAIL ===\n\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
