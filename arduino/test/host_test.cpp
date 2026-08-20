// client.ino 를 호스트에서 그대로 컴파일해 돌리는 회귀 테스트.
//
//   빌드/실행:  bash arduino/test/run.sh      (실행 비트를 세우지 않았으므로 bash 로 부른다)
//
// 왜: 이 프로젝트에는 아두이노 보드가 없다. 그런데 client.ino 에는 상태 기계,
// 줄 파서, 멱등 캐시, 3중 센서 소스가 들어 있어 "읽어서 확인"만으로는 회귀를 못 잡는다.
// (실제로 백오프 언더플로 버그를 읽기로만 잡았는데, 그건 운이 좋았던 것이다.)
//
// ⚠ 이 테스트가 검증하지 않는 것:
//   - 타이밍. millis() 는 호출마다 1ms 씩 흐르는 가짜 시계다(스핀 루프를 끝내기 위해).
//     "1Hz 하트비트"가 정확히 1000ms 인지는 여기서 확인되지 않는다.
//   - 실기 하드웨어. SoftwareSerial 의 송신 중 수신 유실(§6.3), ESP-01 의 실제 AT 응답
//     문구와 지연, 전기적 문제는 보드에서만 드러난다.
//   검증하는 것은 **무엇이 어떤 순서로 일어나는가**와 **전선에 나가는 바이트**다.

#include "Arduino.h"
#include "SoftwareSerial.h"

#include <string>
#include <vector>
#include <cstdio>

// ── 스텁 전역 실체 ──────────────────────────────────────────────────────
unsigned long g_millis = 0;
bool          g_clockAutoAdvance = true;
uint8_t       g_pinLevel[24];
uint8_t       g_pinMode[24];
HostSerial    Serial;

// ── AVR 전용 구문을 호스트에서 무해하게 만든다 (2026-08-16 추가) ────────────
// 스케치가 REQ-0092 로 **리셋 원인 캡처**를 얻으면서 AVR 전용 것들이 들어왔고,
// 그대로는 호스트에서 컴파일되지 않는다:
//   · `__attribute__((section(".noinit")))` / `((naked, used, section(".init3")))`
//     → mach-o 는 "세그먼트,섹션" 형식을 요구해서 거부한다
//   · `MCUSR`·`PORF`·`EXTRF`·`BORF`·`WDRF` → AVR 레지스터/비트라 호스트에 없다
//
// ⚠ 반드시 **표준 헤더를 다 포함한 뒤** 여기서만 정의한다.
//   `__attribute__` 를 먼저 지우면 시스템 헤더가 깨진다.
//
// ⚠ 이 스텁에서 `mcusrMirror` 는 그냥 0 인 전역이 된다 — 그리고 **그게 실기와 같다.**
//   부트로더가 MCUSR 을 지우고 넘어오므로 실기에서도 0 이고, 그래서 부팅 로그가
//   `리셋 원인: 불명` 을 찍는다. 즉 호스트에서도 같은 갈래를 탄다.
#define __attribute__(x)
static uint8_t MCUSR = 0;
#define PORF  0
#define EXTRF 1
#define BORF  2
#define WDRF  3
#ifndef _BV
#define _BV(bit) (1 << (bit))
#endif

// `ramProbe()` 가 쓰는 AVR 링커 심볼. 실기에서는 힙 시작 주소이고, 스택 포인터와의
// 거리가 곧 미사용 RAM 이다. 호스트에는 그런 배치가 없으므로 **더미 하나를 둔다.**
// ⚠ 그래서 호스트에서 나오는 `ramLow` 값은 **의미가 없다.** 이 테스트가 검증하는 것은
//   "ramProbe 가 불렸는가/안 불렸는가"의 흐름이지 그 수치가 아니다.
uint8_t __heap_start = 0;

// 스케치를 통째로 끌어온다. static 들까지 같은 TU 에 들어와 직접 들여다볼 수 있다.
//
// ⚠ 경로를 `SKETCH_PATH` 로 뺀 이유 (2026-08-16): **두 판본을 같은 테스트로 돌려 비교**하기
//   위해서다. 수정 뒤 실패가 나왔을 때 "내 수정 탓인가, 원래 그런가"를 가르려면
//   **고치기 전 판본을 같은 하네스로 돌려 기준선을 잡는 것**이 유일한 방법이다.
//   예: g++ -DSKETCH_PATH='"/tmp/base/client.ino"' ...
#ifndef SKETCH_PATH
#define SKETCH_PATH "../../조별과제샘플/ardu/client.ino"
#endif
#include SKETCH_PATH

// ── 테스트 유틸 ────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

static void ok(bool cond, const std::string& what) {
  if (cond) { g_pass++; printf("  PASS  %s\n", what.c_str()); }
  else      { g_fail++; printf("  FAIL  %s\n", what.c_str()); }
}

static void spin(int iterations) {
  for (int i = 0; i < iterations; i++) loop();
}

// 가짜 시계는 loop() 한 번에 10ms 남짓 흐른다(millis() 호출 횟수에 비례).
// 접속 상태 기계는 CWJAP 대기만 9초라 넉넉히 돌려야 한다 — 고정 횟수 대신 조건으로 기다린다.
static bool spinUntilOnline(int maxIter) {
  for (int i = 0; i < maxIter && !netOnline; i++) loop();
  return netOnline;
}

// 마지막으로 전선에 나간 S 프레임
static std::string lastStatus() {
  for (int i = (int)wifi.sentLines.size() - 1; i >= 0; i--)
    if (!wifi.sentLines[i].empty() && wifi.sentLines[i][0] == 'S') return wifi.sentLines[i];
  return "";
}
static std::string lastAck() {
  for (int i = (int)wifi.sentLines.size() - 1; i >= 0; i--)
    if (!wifi.sentLines[i].empty() && wifi.sentLines[i][0] == 'A') return wifi.sentLines[i];
  return "";
}

// S 프레임의 occupied 비트열(3번째 필드)을 꺼낸다
static std::string occField(const std::string& s) {
  size_t a = s.find(','); if (a == std::string::npos) return "";
  size_t b = s.find(',', a + 1); if (b == std::string::npos) return "";
  size_t c = s.find(',', b + 1); if (c == std::string::npos) return "";
  return s.substr(b + 1, c - b - 1);
}

// 명세 §2.2 체크섬을 테스트 쪽에서 **독립적으로** 다시 계산한다
static std::string xorCk(const std::string& prefix) {
  unsigned x = 0;
  for (unsigned char ch : prefix) x ^= ch;
  char b[4]; snprintf(b, sizeof b, "%02X", x & 0xFF);
  return b;
}
static bool checksumSelfConsistent(const std::string& line) {
  size_t cut = line.rfind(',');
  if (cut == std::string::npos) return false;
  return xorCk(line.substr(0, cut + 1)) == line.substr(cut + 1);
}

// 그 칸의 시뮬 값을 0 으로 고정한다 — simAdvance 가 다시 건드리지 못하게 마감을 멀리 민다.
// 이걸 안 하면 "오버라이드로 1 이 됐다"와 "시뮬이 마침 1 이었다"를 구별할 수 없다.
static void pinSimLow(uint8_t i) {
  // §12B.1 이후 시뮬은 자율 전진을 하지 않으므로 값만 내려 두면 그대로 유지된다.
  // (예전에는 simNextAt 마감도 멀리 밀어야 했다 — 그 배열 자체가 사라졌다.)
  node.simOcc &= (uint16_t)~((uint16_t)1 << i);
}

static std::vector<std::string> splitLine(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); i++) {
    if (i == s.size() || s[i] == ',') { out.push_back(s.substr(start, i - start)); start = i + 1; }
  }
  return out;
}

// S 프레임의 tmask. 필드가 없으면 "" 를 돌려준다(= 테스트 모드 해제).
//   있음: S,seq,occ,res,uptime,devid,tmask,cksum  (8필드)
//   없음: S,seq,occ,res,uptime,devid,cksum        (7필드)
static std::string tmaskField(const std::string& s) {
  std::vector<std::string> f = splitLine(s);
  return (f.size() == 8) ? f[6] : "";
}

// 모든 칸의 시뮬을 얼린다 — 시뮬이 제멋대로 바뀌면 "무엇이 전송을 촉발했는가"를 가릴 수 없다.
static void freezeAllSims() {
  for (uint8_t i = 0; i < SENSOR_N; i++) pinSimLow(i);
}

// 하트비트가 **막 나간 직후**로 정렬한다. lastStatusAt 은 전송할 때마다 리셋되므로,
// S 가 하나 나오는 순간까지 돌리면 다음 하트비트까지 꼬박 1000ms 가 남는다.
// 이게 없으면 관측 창을 열었을 때 하트비트까지 얼마 남았는지가 임의라,
// 하필 창 안에서 하트비트가 터지면 "즉시 전송 덕분에 나갔다"고 **잘못 통과**한다.
static bool alignToHeartbeat(int maxIter = 4000);

// AT 로그에서 그 명령이 처음 나온 위치. 없으면 SIZE_MAX.
static size_t firstAtIndex(const std::string& needle) {
  for (size_t i = 0; i < wifi.atLog.size(); i++)
    if (wifi.atLog[i].find(needle) != std::string::npos) return i;
  return SIZE_MAX;
}

static size_t countStatusLines() {
  size_t n = 0;
  for (const std::string& l : wifi.sentLines) if (!l.empty() && l[0] == 'S') n++;
  return n;
}

// 가상 시각 기준으로 ms 만큼 돌린다(루프 횟수가 아니라 시간으로 재야 하는 시험용).
static void spinMs(unsigned long ms) {
  unsigned long t0 = g_millis;
  while (g_millis - t0 < ms) loop();
}

static bool alignToHeartbeat(int maxIter) {
  size_t n0 = countStatusLines();
  for (int i = 0; i < maxIter; i++) {
    loop();
    if (countStatusLines() != n0) return true;
  }
  return false;
}

static void deliverIPD(const std::string& payload) {
  char hdr[32];
  snprintf(hdr, sizeof hdr, "+IPD,%u:", (unsigned)(payload.size() + 1));
  wifi.deliver(std::string(hdr) + payload + "\n");
}

// ── 시나리오 ────────────────────────────────────────────────────────────
int main() {
  for (int i = 0; i < 24; i++) { g_pinLevel[i] = HIGH; g_pinMode[i] = INPUT; }
  Serial.echoToStdout = false;          // 스케치의 디버그 출력은 삼킨다

  printf("\n=== client.ino 호스트 회귀 테스트 ===\n");

  setup();
  bool online = spinUntilOnline(20000);  // AT+RST~CWJAP~CIPSTART 가 다 지나야 한다
  spin(400);

  printf("\n[1] 접속과 상태 프레임\n");
  printf("        가상시각 %lums 에 접속 성립\n", g_millis);
  ok(online, "CONNECT 를 받아 netOnline 이 켜진다");
  ok(!lastStatus().empty(), "S 프레임이 전선에 나갔다");
  ok(checksumSelfConsistent(lastStatus()),
     "S 프레임 체크섬이 독립 계산과 일치: " + lastStatus());

  printf("\n[2] ★ 칸별 오버라이드가 다음 S 프레임에 반영되는가 (완료기준 핵심)\n");
  node.slotOverrideClearAll();
  // §12A.2 주입은 무장 중에만 적용된다. [2]~[9] 는 오버라이드 층 자체를 보는 시험이라
  // 무장 상태로 둔다(실사용에서는 T,A 가 켠다). 해제 상태의 거동은 [11] 에서 따로 본다.
  node.testArmed = true;
  // ⚠ 밑에 깔린 소스를 **0 으로 못 박고** 나서 1 을 강제해야 의미가 있다.
  //    가상 시계가 빨리 흘러 시뮬이 거의 모든 칸을 채워 두므로, 그냥 1 을 강제하면
  //    오버라이드가 no-op 이어도 시뮬 때문에 1 이 읽혀 테스트가 통과해 버린다.
  pinSimLow(2);
  spin(200);
  ok(occField(lastStatus())[2] == '0', "먼저: 오버라이드 없이 A3 는 0 이다 (밑바닥 확인)");
  node.slotOverrideSet(2, 1);                // A3 = 인덱스 2 를 강제 점유
  spin(400);
  std::string s1 = lastStatus();
  std::string occ1 = occField(s1);
  printf("        S = %s   (occupied=%s)\n", s1.c_str(), occ1.c_str());
  ok(occ1.size() == 10 && occ1[2] == '1', "A3(인덱스 2) 강제 점유 → occupied 비트 2 가 1");
  ok(checksumSelfConsistent(s1), "그 S 프레임의 체크섬도 맞다");

  node.slotOverrideSet(2, 0);                // 같은 칸을 강제 '비움' 으로
  spin(400);
  std::string occ2 = occField(lastStatus());
  printf("        S = %s   (occupied=%s)\n", lastStatus().c_str(), occ2.c_str());
  ok(occ2.size() == 10 && occ2[2] == '0', "A3 강제 해제(0) → occupied 비트 2 가 0");

  printf("\n[3] 오버라이드는 칸별이다 — 다른 칸은 영향받지 않는다\n");
  node.slotOverrideClearAll();
  pinSimLow(8);                         // B4 — 이웃. 오버라이드를 걸지 않는다
  pinSimLow(9);                         // B5 — 여기만 강제한다
  spin(200);
  std::string occPre = occField(lastStatus());
  ok(occPre[8] == '0' && occPre[9] == '0', "먼저: B4·B5 둘 다 0 이다 (밑바닥 확인)");
  node.slotOverrideSet(9, 1);                // B5 = 인덱스 9 만
  spin(400);
  std::string occ3 = occField(lastStatus());
  printf("        occupied=%s   (B4=%c, B5=%c)\n", occ3.c_str(), occ3[8], occ3[9]);
  ok(occ3.size() == 10 && occ3[9] == '1', "B5(인덱스 9) 가 1 로 강제된다");
  ok(occ3.size() == 10 && occ3[8] == '0', "옆칸 B4(인덱스 8) 는 0 그대로다 — 칸별이다");

  printf("\n[4] 소스 REAL — 핀을 LOW 로 당기면 점유로 읽힌다 (SENSOR_ACTIVE_LOW)\n");
  node.slotOverrideClearAll();
  node.slotSourceSet(0, 1);                              // A1 을 실물 소스로
  ok(g_pinMode[slotPin(0)] == INPUT_PULLUP, "A1 의 핀이 INPUT_PULLUP 으로 잡힌다");
  g_pinLevel[slotPin(0)] = LOW;                     // 차량 있음
  spin(400);
  std::string occ4 = occField(lastStatus());
  printf("        A1 핀=%u LOW → occupied=%s\n", (unsigned)slotPin(0), occ4.c_str());
  ok(occ4.size() == 10 && occ4[0] == '1', "실물 소스가 LOW 를 점유(1)로 읽는다");

  g_pinLevel[slotPin(0)] = HIGH;                    // 차량 없음
  spin(400);
  std::string occ5 = occField(lastStatus());
  ok(occ5.size() == 10 && occ5[0] == '0', "HIGH 는 비어 있음(0)으로 읽는다");

  printf("\n[5] 우선순위 — 오버라이드가 실물 소스를 이긴다\n");
  g_pinLevel[slotPin(0)] = HIGH;                    // 실물은 '비어 있음'
  node.slotOverrideSet(0, 1);                            // 그런데 강제로 점유
  spin(400);
  std::string occ6 = occField(lastStatus());
  printf("        실물=HIGH(0) 인데 오버라이드=1 → occupied=%s\n", occ6.c_str());
  ok(occ6.size() == 10 && occ6[0] == '1', "오버라이드가 실물보다 우선한다");

  node.slotOverrideClear(0);                             // 오버라이드만 풀면 실물로 복귀
  spin(400);
  std::string occ7 = occField(lastStatus());
  ok(occ7.size() == 10 && occ7[0] == '0', "오버라이드 해제 → 실물 값(0)으로 복귀");
  node.slotSourceSet(0, 0);

  printf("\n[6] 수신 경로 — +IPD → 예약 → ACK (명세 §6.2 / §8.1)\n");
  node.slotOverrideClearAll();
  // B3(인덱스 7) 를 **비어 있는 상태로 고정**한다. 시뮬이 마침 그 칸에 차를 넣어 두면
  // 명세 §2.4 대로 result=1(이미 점유) 이 나오는 것이 정답이라 예약 성공 경로를 못 본다.
  node.slotOverrideSet(7, 0);
  spin(200);
  size_t before = wifi.sentLines.size();
  deliverIPD("R,42,B3,u17,56");                     // 명세 §8.1 의 그 줄
  spin(200);
  std::string ack = lastAck();
  printf("        ACK = %s\n", ack.c_str());
  ok(ack == "A,42,B3,0,06", "R,42,B3,u17,56 → A,42,B3,0,06 (명세 §8.1 과 바이트 일치)");
  ok((node.resMask & (1u << 7)) != 0, "B3(인덱스 7) 의 reserved 비트가 켜졌다");
  (void)before;

  printf("\n[7] rid 멱등 — 같은 rid 재수신 시 재적용 없이 같은 ACK (§4.2)\n");
  uint16_t resBefore = node.resMask;
  deliverIPD("R,42,B3,u17,56");                     // 똑같은 줄을 한 번 더
  spin(200);
  ok(lastAck() == "A,42,B3,0,06", "같은 ACK 를 그대로 다시 낸다");
  ok(node.resMask == resBefore, "상태를 다시 적용하지 않는다");

  printf("\n[8] 체크섬이 틀린 줄은 버린다 (§2.2 / §6.2 4단계)\n");
  std::string ackBefore = lastAck();
  deliverIPD("R,77,A4,u1,00");                      // 00 은 틀린 체크섬
  spin(200);
  ok(lastAck() == ackBefore, "체크섬 불일치 줄은 ACK 를 만들지 않는다");

  // 문자열 안의 ??' 는 트라이그래프로 해석되므로 물음표 하나를 escape 한다
  printf("\n[9] 잘못된 자리 ID → slot='?\?', ACK 생략 없음 (REQ-0020 ②)\n");
  deliverIPD(std::string("C,55,ZZ,") + xorCk("C,55,ZZ,"));
  spin(200);
  printf("        ACK = %s\n", lastAck().c_str());
  ok(lastAck().find(",??,3,") != std::string::npos, "slot 이 ?? 이고 result=3 인 ACK 가 나간다");
  ok(checksumSelfConsistent(lastAck()), "그 ACK 의 체크섬도 맞다");

  printf("\n[10] 명세 §2.5 의 T/tmask 공표 예제를 내 체크섬 계산이 재현하는가\n");
  {
    const char* spec[][1] = {
      {"T,50,A,??,-,11"}, {"T,51,D,??,-,15"}, {"T,52,S,A3,1,6F"}, {"T,54,X,A3,-,7E"},
      {"A,50,??,0,74"},   {"A,52,A3,0,04"},   {"A,52,A3,4,00"},
      {"S,1236,0110100011,0000100000,3602,P1,-,32"},
      {"S,1234,0110100011,0000100000,3600,P1,0000000000,1F"},
      {"S,65535,1111111111,1111111111,4294967,DEVICE12,1111111111,67"},
    };
    int mism = 0;
    for (auto& row : spec) {
      std::string l = row[0];
      if (!checksumSelfConsistent(l)) { printf("        MISMATCH %s\n", l.c_str()); mism++; }
    }
    ok(mism == 0, "명세의 T/ACK/tmask 예제 10줄 전부 체크섬 일치");
    // 최장 S 프레임(devid 8자, tmask 포함)이 규격 상한 안에 있는지
    std::string longest = "S,65535,1111111111,1111111111,4294967,DEVICE12,1111111111,67";
    printf("        최장 S = %zu바이트(LF 포함) / 상한 64\n", longest.size() + 1);
    ok(longest.size() + 1 == 61, "최장 S 프레임은 61B — 송신 버퍼 char buf[64] 안에 들어간다");
  }

  printf("\n[11] 해제 상태 — 주입은 적용되지 않고 S/X 는 result=4 로 거절된다 (§12A.2)\n");
  // 실사용의 해제 상태를 재현한다: T,D 가 무장을 끄고 전 칸 오버라이드를 지운다.
  node.testArmed = false;
  node.slotOverrideClearAll();
  spin(200);
  ok(!node.testArmed, "해제 상태다 (부팅 직후도 이 상태 — §12A.3)");
  ok(tmaskField(lastStatus()).empty(), "해제 중 S 에는 tmask 필드가 없다");

  // ★ 안전 방향: 해제 중에는 오버라이드가 남아 있어도 occupied 에 실리면 안 된다.
  //   실리면 tmask 없이 가짜 값이 전선에 나가고, 화면은 그걸 실측으로 믿는다(§12A.6).
  pinSimLow(4);                                        // A5 밑바닥 0
  node.slotOverrideSet(4, 1);                               // 해제 중인데 강제로 1
  spin(300);
  printf("        해제 중 오버라이드 → occupied=%s  tmask=%s\n",
         occField(lastStatus()).c_str(), tmaskField(lastStatus()).c_str());
  ok(occField(lastStatus())[4] == '0', "해제 중 오버라이드는 occupied 에 실리지 않는다");
  ok(tmaskField(lastStatus()).empty(), "그리고 tmask 도 여전히 없다");
  node.slotOverrideClearAll();

  std::string t60 = std::string("T,60,S,A3,1,") + xorCk("T,60,S,A3,1,");
  deliverIPD(t60);
  spin(200);
  printf("        ACK = %s\n", lastAck().c_str());
  ok(lastAck() == std::string("A,60,A3,4,") + xorCk("A,60,A3,4,"),
     "무장 전 주입 → result=4 (" + lastAck() + ")");
  ok(lastAck()[lastAck().size() - 3 - 1] == '4', "result 자리가 실제로 4 다");
  ok(!node.testArmed, "거절됐으므로 여전히 해제 상태");

  printf("\n[12] T 무장 → tmask 가 S 에 실린다\n");
  deliverIPD("T,50,A,??,-,11");                       // 명세 §2.5 의 그 줄
  spin(300);
  printf("        ACK = %s\n", lastAck().c_str());
  ok(lastAck() == "A,50,??,0,74", "무장 ACK 가 명세의 A,50,??,0,74 와 바이트 일치");
  ok(node.testArmed, "무장 상태가 됐다");
  printf("        S = %s\n", lastStatus().c_str());
  ok(tmaskField(lastStatus()) == "0000000000", "무장 직후 tmask = 0000000000 (주입된 칸 없음)");

  printf("\n[13] T 주입 — occupied 와 tmask 가 함께 바뀐다\n");
  pinSimLow(2);                                        // A3 밑바닥을 0 으로 못 박는다
  spin(200);
  ok(occField(lastStatus())[2] == '0', "먼저: A3 는 0 이다 (밑바닥 확인)");
  deliverIPD("T,52,S,A3,1,6F");                        // 명세 §2.5 의 그 줄
  spin(300);
  printf("        ACK = %s\n", lastAck().c_str());
  printf("        S   = %s\n", lastStatus().c_str());
  ok(lastAck() == "A,52,A3,0,04", "주입 ACK 가 명세의 A,52,A3,0,04 와 바이트 일치");
  ok(occField(lastStatus())[2] == '1', "occupied 비트 2 가 1 (주입값이 이미 반영돼 있다)");
  ok(tmaskField(lastStatus())[2] == '1', "tmask 비트 2 가 1 (그 값이 주입된 것임을 알린다)");
  ok(tmaskField(lastStatus())[3] == '0', "건드리지 않은 A4 의 tmask 비트는 0");

  printf("\n[14] T 멱등 — 같은 rid 재수신 시 재적용 없이 같은 ACK (§4.2)\n");
  node.slotOverrideSet(3, 1);                               // 캐시 적중이면 이 값이 살아남아야 한다
  spin(200);
  uint16_t ovrBefore = node.ovrActive;
  deliverIPD("T,52,S,A3,1,6F");                        // 같은 rid 52 를 한 번 더
  spin(200);
  ok(lastAck() == "A,52,A3,0,04", "같은 ACK 를 그대로 다시 낸다");
  ok(node.ovrActive == ovrBefore, "상태를 다시 적용하지 않는다");

  printf("\n[15] T,X — 그 칸만 원래 소스로 (§12A.2)\n");
  deliverIPD("T,54,X,A3,-,7E");                        // 명세 §2.5 의 그 줄
  spin(300);
  printf("        ACK = %s   tmask=%s\n", lastAck().c_str(), tmaskField(lastStatus()).c_str());
  ok(lastAck() == "A,54,A3,0,02", "X 의 ACK 는 그 자리를 담는다");
  ok(tmaskField(lastStatus())[2] == '0', "A3 의 tmask 비트가 내려간다");
  ok(tmaskField(lastStatus())[3] == '1', "다른 칸(A4) 오버라이드는 살아 있다 — 칸별이다");

  printf("\n[16] T,D 해제 — 전 칸 오버라이드 소멸 + tmask 필드 자체가 사라진다\n");
  deliverIPD("T,51,D,??,-,15");                        // 명세 §2.5 의 그 줄
  spin(300);
  printf("        ACK = %s\n", lastAck().c_str());
  printf("        S   = %s\n", lastStatus().c_str());
  ok(lastAck() == "A,51,??,0,75", "해제 ACK 는 slot 이 ?? 다");
  ok(!node.testArmed, "해제 상태가 됐다");
  ok(node.ovrActive == 0, "전 칸 오버라이드가 한 번에 사라졌다 (§12A.2)");
  ok(tmaskField(lastStatus()).empty(), "해제되면 S 에서 tmask 필드가 통째로 빠진다(옛 형식)");

  printf("\n[17] ★ tmask 가 바뀌면 하트비트를 기다리지 않고 즉시 나간다 (REQ-0035 ②)\n");
  {
    // 시뮬을 전부 얼려 occupied 가 스스로 바뀌지 못하게 한다 —
    // 안 그러면 "시뮬이 바꿔서 나간 S" 를 "tmask 때문에 나간 S" 로 오독한다.
    freezeAllSims();
    node.testArmed = false;
    node.slotOverrideClearAll();
    spin(400);
    // ★ 하트비트 직후로 정렬한다. 임의 지점에서 창을 열면 하트비트가 창 안에 들어와
    //   즉시 전송이 없어도 통과해 버린다 — 그러면 이 시험은 아무것도 증명하지 않는다.
    ok(alignToHeartbeat(), "하트비트 직후로 정렬했다 (다음 하트비트까지 ~1000ms 남는다)");

    size_t sBefore  = countStatusLines();
    unsigned long t0 = g_millis;
    deliverIPD("T,70,A,??,-," + xorCk("T,70,A,??,-,"));   // 무장만. 주입은 없다
    spin(30);                                   // 하트비트(1000ms) 보다 훨씬 짧게만 돌린다
    unsigned long elapsed = g_millis - t0;
    size_t sAfter = countStatusLines();

    printf("        경과 %lums 동안 S 프레임 %zu개 추가, 마지막 tmask=\"%s\"\n",
           elapsed, sAfter - sBefore, tmaskField(lastStatus()).c_str());
    ok(elapsed < 1000, "관측 구간이 하트비트 주기보다 짧다 (즉시 전송이 아니면 못 나간다)");
    ok(sAfter > sBefore, "무장만 했는데도 그 구간 안에 S 가 나갔다");
    ok(tmaskField(lastStatus()) == "0000000000", "그 S 에 tmask 가 실려 있다");

    // 해제도 같은 경로여야 한다 — occupied 는 하나도 안 바뀌는데 tmask 필드가 사라져야 한다
    ok(alignToHeartbeat(), "해제 시험도 하트비트 직후로 정렬했다");
    sBefore = countStatusLines();
    t0 = g_millis;
    deliverIPD("T,71,D,??,-," + xorCk("T,71,D,??,-,"));
    spin(30);
    printf("        해제: 경과 %lums, S %zu개 추가, tmask=\"%s\"\n",
           g_millis - t0, countStatusLines() - sBefore, tmaskField(lastStatus()).c_str());
    ok(g_millis - t0 < 1000 && countStatusLines() > sBefore, "해제도 즉시 전송된다");
    ok(tmaskField(lastStatus()).empty(), "그 S 에서 tmask 필드가 빠져 있다");
  }

  printf("\n[18] ★ 새 TCP 연결에서 멱등 캐시가 비워진다 (REQ-0035 ① / REQ-0032 판정 a)\n");
  {
    // ⚠ rid 는 반드시 새 값을 써야 한다. 앞에서 쓴 rid(50 등)를 재사용하면 멱등 캐시가
    //    정상 동작해서 무장 명령 자체가 삼켜진다 — 실제로 처음에 그렇게 짜서 5건이 실패했다.
    deliverIPD("T,80,A,??,-," + xorCk("T,80,A,??,-,"));   // 다시 무장 (새 rid)
    spin(200);
    ok(node.testArmed, "rid 80 으로 무장됐다 (사전 조건)");
    // rid 90 으로 A3 주입 → 캐시에 (90, A3, 0) 이 남는다
    deliverIPD("T,90,S,A3,1," + xorCk("T,90,S,A3,1,"));
    spin(200);
    printf("        1) rid 90 최초    ACK = %s\n", lastAck().c_str());
    ok(lastAck() == "A,90,A3,0," + xorCk("A,90,A3,0,"), "rid 90 이 A3 로 처리됐다");

    // 같은 연결 안에서 같은 rid 로 **다른 명령**이 와도 캐시가 이긴다(= 재전송으로 본다)
    deliverIPD("T,90,S,B2,1," + xorCk("T,90,S,B2,1,"));
    spin(200);
    printf("        2) 같은 연결 재전송 ACK = %s\n", lastAck().c_str());
    ok(lastAck() == "A,90,A3,0," + xorCk("A,90,A3,0,"),
       "같은 연결에서는 여전히 멱등 — 옛 ACK(A3)가 그대로 나온다 (완료기준 2)");

    // 이제 새 TCP 연결이 맺어졌다고 알린다 (ESP-01 이 CONNECT 를 올린다)
    wifi.deliver("CONNECT\r\n");
    spin(50);
    deliverIPD("T,90,S,B2,1," + xorCk("T,90,S,B2,1,"));
    spin(200);
    printf("        3) 새 연결 후      ACK = %s\n", lastAck().c_str());
    ok(lastAck() == "A,90,B2,0," + xorCk("A,90,B2,0,"),
       "새 연결 뒤에는 같은 rid 가 새 명령으로 처리된다 — B2 ACK 가 나온다 (완료기준 1)");
    ok(tmaskField(lastStatus())[6] == '1', "실제로 B2(인덱스 6)에 주입이 적용됐다");

    // ALREADY CONNECTED 는 새 연결이 아니다 → 캐시를 비우면 안 된다
    deliverIPD("T,91,S,A4,1," + xorCk("T,91,S,A4,1,"));
    spin(200);
    wifi.deliver("ALREADY CONNECTED\r\n");
    spin(50);
    deliverIPD("T,91,S,B1,1," + xorCk("T,91,S,B1,1,"));
    spin(200);
    printf("        4) ALREADY CONNECTED 후 ACK = %s\n", lastAck().c_str());
    ok(lastAck() == "A,91,A4,0," + xorCk("A,91,A4,0,"),
       "ALREADY CONNECTED 로는 캐시가 비워지지 않는다 (기존 연결이 살아 있다)");
  }

  printf("\n[19] ★ CLOSED 만으로도 캐시가 비워진다 — CONNECT 문구에 기대지 않는다 (REQ-0036)\n");
  {
    // 사전 조건: 무장 + rid 를 하나 소비해 캐시에 넣는다
    deliverIPD("T,100,A,??,-," + xorCk("T,100,A,??,-,"));
    spin(200);
    ok(node.testArmed, "rid 100 으로 무장됐다 (사전 조건)");
    deliverIPD("T,101,S,A5,1," + xorCk("T,101,S,A5,1,"));
    spin(200);
    ok(lastAck() == "A,101,A5,0," + xorCk("A,101,A5,0,"), "rid 101 이 A5 로 처리됐다");

    // 같은 연결에서는 캐시가 이긴다 (회귀 — 완료기준 3 의 짝)
    deliverIPD("T,101,S,B4,1," + xorCk("T,101,S,B4,1,"));
    spin(200);
    ok(lastAck() == "A,101,A5,0," + xorCk("A,101,A5,0,"), "같은 연결에서는 여전히 멱등");

    // ★ CLOSED 만 준다. CONNECT 는 **일부러 주지 않는다.**
    //   가짜 ESP 는 CIPSTART 에 CONNECT 를 붙여 주므로, netTick 이 재시도하기 전에
    //   바로 같은 rid 를 밀어 넣어 "CLOSED 하나만으로 비워졌는가"를 본다.
    wifi.deliver("CLOSED\r\n");
    spin(3);
    ok(!netOnline, "CLOSED 로 오프라인이 됐다");
    ok(ackQ.find(101) < 0, "CLOSED 시점에 rid 101 이 캐시에서 사라졌다 — CONNECT 를 기다리지 않았다");
  }

  printf("\n[20] CLOSED→재접속 후 같은 rid 가 새 명령으로 처리된다\n");
  {
    // 가짜 ESP 가 CIPSTART 에 CONNECT 를 돌려주므로 재접속은 저절로 된다
    ok(spinUntilOnline(20000), "재접속됐다");
    // 재부팅이 아니라 재접속이므로 무장 상태는 스케치 안에서 유지된다(§12A.3 은 재부팅 이야기다)
    deliverIPD("T,101,S,B4,1," + xorCk("T,101,S,B4,1,"));
    spin(300);
    printf("        재접속 후 rid 101 ACK = %s\n", lastAck().c_str());
    ok(lastAck() == "A,101,B4,0," + xorCk("A,101,B4,0,"),
       "같은 rid 101 이 새 명령(B4)으로 처리된다 — 캐시가 비워졌다");
  }

  printf("\n[21] ALREADY CONNECTED 로는 여전히 안 비워진다 (회귀 — 살아 있는 연결의 멱등성)\n");
  {
    deliverIPD("T,110,S,A1,1," + xorCk("T,110,S,A1,1,"));
    spin(200);
    ok(lastAck() == "A,110,A1,0," + xorCk("A,110,A1,0,"), "rid 110 이 A1 로 처리됐다");
    wifi.deliver("ALREADY CONNECTED\r\n");
    spin(50);
    ok(ackQ.find(110) >= 0, "ALREADY CONNECTED 로는 rid 110 이 캐시에 남는다");
    deliverIPD("T,110,S,B1,1," + xorCk("T,110,S,B1,1,"));
    spin(200);
    printf("        ALREADY CONNECTED 후 ACK = %s\n", lastAck().c_str());
    ok(lastAck() == "A,110,A1,0," + xorCk("A,110,A1,0,"),
       "멱등이 유지된다 — netTick 의 5초 CIPSTART 재시도가 멱등성을 깨지 않는다");
  }

  printf("\n[22] ★ 접속 판정 변형 — 넓히되 WIFI CONNECTED 오인은 되살리지 않는다 (REQ-0042)\n");
  {
    // 오프라인으로 되돌린 뒤 각 변형을 하나씩 시험한다.
    struct Variant { const char* line; bool shouldConnect; const char* why; };
    const Variant vs[] = {
      {"CONNECT",           true,  "표준 AT 성공 응답"},
      {"connect",           true,  "대소문자 무시"},
      {"Linked",            true,  "구형 AT 펌웨어"},
      {"LINKED",            true,  "구형 + 대문자"},
      {"0,CONNECT",         true,  "CIPMUX 링크ID 접두"},
      {"  CONNECT  ",       true,  "앞뒤 공백"},
      {"ALREADY CONNECTED", true,  "이미 연결됨"},
      // ↓ 여기부터는 **켜지면 안 된다**
      {"WIFI CONNECTED",    false, "★ 와이파이 연결일 뿐 TCP 접속이 아니다 — 옛 버그"},
      {"WIFI GOT IP",       false, "와이파이 단계"},
      {"WIFI DISCONNECT",   false, "와이파이 끊김"},
      {"CONNECTED",         false, "길이가 다르다(9) — CONNECT(7) 가 아니다"},
      {"NO CONNECT",        false, "실패 응답인데 부분문자열로는 걸린다"},
      {"CONNECT FAIL",      false, "실패 응답"},
      {"OK",                false, "평범한 응답"},
      {"busy p...",         false, "모듈 바쁨"},
    };
    int bad = 0;
    for (const Variant& v : vs) {
      netOnline = false;
      wifi.deliver(std::string(v.line) + "\r\n");
      spin(3);
      bool got = netOnline;
      if (got != v.shouldConnect) {
        printf("        FAIL  \"%s\" → online=%d (기대 %d)  [%s]\n",
               v.line, (int)got, (int)v.shouldConnect, v.why);
        bad++;
      } else {
        printf("        ok    \"%-18s\" → online=%d   %s\n", v.line, (int)got, v.why);
      }
    }
    ok(bad == 0, "접속 판정 변형 15종이 전부 기대대로 동작한다");

    // 회귀 방지의 핵심을 따로 한 번 더 단언한다 — 이건 절대 통과하면 안 되는 줄이다
    netOnline = false;
    wifi.deliver("WIFI CONNECTED\r\n");
    spin(3);
    ok(!netOnline, "★ WIFI CONNECTED 는 여전히 netOnline 을 켜지 않는다 (되살리면 안 되는 버그)");

    // 진단 카운터가 실제로 늘고 있는지 — 실기에서 이 값으로 원인을 가른다
    ok(dbgRxBytes > 0 && dbgLineCnt > 0, "진단 카운터(rx/lines)가 집계되고 있다");

    ok(spinUntilOnline(20000), "시험 후 다시 온라인으로 복귀했다");
  }

  printf("\n[23] 진단 로그 형식 — 보이지 않는 문자가 실제로 보이는가 (REQ-0042 1순위)\n");
  {
    netOnline = false;
    Serial.out.clear();
    wifi.deliver("CONNECT \x01\r\n");        // 뒤에 공백 + 제어문자가 붙은 경우
    spin(3);
    printf("        로그 그대로: %s", Serial.out.c_str());
    ok(Serial.out.find("\\x01") != std::string::npos, "제어문자가 \\x01 로 펴져 보인다");
    ok(Serial.out.find("(9)") != std::string::npos, "길이(9)가 같이 찍힌다");
    ok(!netOnline,
       "그 줄은 CONNECT 로 인정되지 않는다 — 그래서 로그가 필요하다(눈으로는 CONNECT 로 보인다)");

    // 넘치는 줄도 버렸다는 사실이 남는가
    netOnline = false;
    Serial.out.clear();
    wifi.deliver(std::string(90, 'X') + "\r\n");   // RX_CAP(72) 초과
    spin(5);
    ok(Serial.out.find("[DROP-OVF]") != std::string::npos,
       "넘쳐서 버린 줄이 [DROP-OVF] 로 남는다 (조용히 사라지지 않는다)");

    // 오프라인 진단 한 줄이 나오는가.
    // ⚠ 가짜 ESP 는 CIPSTART 에 곧바로 CONNECT 를 돌려주므로 그냥 돌리면 오프라인 구간이
    //   생기지 않는다 — netTick 의 다음 시도를 멀리 밀어 두고 관측한다.
    netOnline = false;
    netStepAt = g_millis;
    netStepWait = 60000;
    dbgLastDiag = 0;
    Serial.out.clear();
    spin(20);
    size_t d = Serial.out.find("[DIAG]");
    ok(d != std::string::npos, "오프라인 동안 [DIAG] 가 주기적으로 나온다");
    if (d != std::string::npos) {
      size_t e = Serial.out.find('\n', d);
      if (e == std::string::npos) e = Serial.out.size();
      printf("        %s\n", Serial.out.substr(d, e - d).c_str());
    }

    netStepWait = 0;                       // 원상복구 — 다시 접속되게 한다
    ok(spinUntilOnline(20000), "시험 후 다시 온라인으로 복귀했다");
  }

  printf("\n[24] ★ 시뮬은 스스로 전진하지 않는다 — M 트리거만 민다 (§12B.1/.2, REQ-0047)\n");
  {
    node.testArmed = false;
    node.slotOverrideClearAll();
    for (uint8_t i = 0; i < SENSOR_N; i++) node.slotSourceSet(i, 0);
    node.resMask = 0;
    ok(spinUntilOnline(20000), "온라인 상태다 (사전 조건)");

    // (a) 자율 전진이 없다 — 이게 이번 변경의 핵심이다
    uint16_t before = node.simOcc;
    spinMs(120000);                                  // 가상시각 2분
    printf("        트리거 없이 2분: node.simOcc %04X → %04X\n", before, node.simOcc);
    ok(node.simOcc == before, "★ 트리거 없이는 시뮬이 한 비트도 바뀌지 않는다");

    // (b) 명세 §2.5 의 M/ACK 예제 체크섬을 내 계산이 재현하는가
    {
      const char* spec[] = {"M,60,4B", "M,65535,7D", "A,60,A3,0,05", "A,60,??,5,72"};
      int mism = 0;
      for (const char* l : spec) if (!checksumSelfConsistent(l)) { printf("        MISMATCH %s\n", l); mism++; }
      ok(mism == 0, "명세의 M/ACK 예제 4줄 체크섬 일치");
    }

    // (c) 트리거 한 번 = 한 칸만 바뀐다
    uint16_t b1 = node.simOcc;
    deliverIPD("M,60,4B");                           // 명세 §2.5 의 그 줄
    spin(300);
    int flipped = 0;
    for (uint8_t i = 0; i < SENSOR_N; i++) if (((node.simOcc >> i) & 1) != ((b1 >> i) & 1)) flipped++;
    printf("        M,60 → node.simOcc %04X → %04X, ACK=%s\n", b1, node.simOcc, lastAck().c_str());
    ok(flipped == 1, "★ 트리거 한 번에 정확히 한 칸만 바뀐다");
    ok(lastAck()[0] == 'A' && lastAck().find(",0,") != std::string::npos, "ACK result=0");
    // ACK 의 slot 이 실제로 바뀐 칸인가
    uint8_t changedIdx = 0xFF;
    for (uint8_t i = 0; i < SENSOR_N; i++) if (((node.simOcc >> i) & 1) != ((b1 >> i) & 1)) changedIdx = i;
    std::vector<std::string> af = splitLine(lastAck());
    ok(af.size() == 5 && af[2] == std::string(1, slotCol(changedIdx)) + std::string(1, slotRow(changedIdx)),
       "ACK 의 slot 이 실제로 바뀐 칸이다");

    // (d) 멱등 — 같은 rid 재전송이 두 걸음이 되면 안 된다 (§12B.4)
    uint16_t b2 = node.simOcc;
    deliverIPD("M,60,4B");
    spin(300);
    printf("        같은 rid 재전송 → node.simOcc %04X (변화 없어야 함), ACK=%s\n", node.simOcc, lastAck().c_str());
    ok(node.simOcc == b2, "★ 같은 rid 재전송은 두 걸음이 되지 않는다");

    // (e) 예약된 빈칸을 우선 채운다 → occupied=1,reserved=1 도달 (§12B.2)
    node.slotOverrideClearAll();
    node.resMask = 0;
    node.simOcc = 0;                                      // 전 칸 비움
    node.resMask |= (uint16_t)1 << 7;                     // B3 예약
    deliverIPD("M,61," + xorCk("M,61,"));
    spin(300);
    printf("        예약 B3 상태에서 M → occupied=%s reserved=%s ACK=%s\n",
           occField(lastStatus()).c_str(), lastStatus().empty() ? "" : splitLine(lastStatus())[3].c_str(),
           lastAck().c_str());
    ok(((node.simOcc >> 7) & 1) == 1, "★ 예약된 빈칸 B3 이 먼저 채워졌다");
    ok(occField(lastStatus())[7] == '1' && splitLine(lastStatus())[3][7] == '1',
       "★ occupied=1, reserved=1 조합이 실제로 전선에 나갔다 (§1.1 마지막 행)");
    ok(lastAck().find(",B3,0,") != std::string::npos, "ACK 이 B3 을 가리킨다");

    // (f) tmask 는 시뮬 변화를 포함하지 않는다 (§12B.3)
    ok(tmaskField(lastStatus()).empty(), "해제 상태라 tmask 필드 자체가 없다 — 시뮬 변화는 tmask 와 무관");

    // (g) 무장 중에도 트리거가 먹는다 ← REQ-0043 잔재 제거의 회귀 방지 핵심
    deliverIPD("T,140,A,??,-," + xorCk("T,140,A,??,-,"));
    spin(300);
    ok(node.testArmed, "무장됐다 (사전 조건)");
    uint16_t b3 = node.simOcc;
    deliverIPD("M,62," + xorCk("M,62,"));
    spin(300);
    printf("        무장 중 M → node.simOcc %04X → %04X, ACK=%s\n", b3, node.simOcc, lastAck().c_str());
    ok(node.simOcc != b3, "★ 무장 중에도 트리거가 먹는다 (테스트 모드와 별개 — §12B.3)");
    ok(tmaskField(lastStatus()) == "0000000000",
       "★ 시뮬로 바뀐 칸은 tmask 에 들어가지 않는다 (주입이 아니다)");

    // (h) 실물 칸은 트리거의 영향을 받지 않는다
    deliverIPD("T,141,D,??,-," + xorCk("T,141,D,??,-,"));
    spin(200);
    for (uint8_t i = 0; i < SENSOR_N; i++) node.slotSourceSet(i, 1);   // 전 칸 실물
    node.resMask = 0;
    uint16_t b4 = node.simOcc;
    deliverIPD("M,63," + xorCk("M,63,"));
    spin(300);
    printf("        전 칸 실물 상태에서 M → ACK=%s\n", lastAck().c_str());
    ok(node.simOcc == b4, "실물 칸은 트리거로 바뀌지 않는다");
    ok(lastAck().find(",??,5,") != std::string::npos, "★ 바꿀 시뮬 칸이 없으면 result=5, slot=??");
    for (uint8_t i = 0; i < SENSOR_N; i++) node.slotSourceSet(i, 0);   // 원복
  }

  printf("\n[25] ★ 연속 전송 실패가 오프라인 전환을 일으킨다 (REQ-0049 ①)\n");
  {
    node.testArmed = false;
    node.slotOverrideClearAll();
    ok(spinUntilOnline(20000), "온라인 상태다 (사전 조건)");
    sendFailStreak = 0;

    // 죽은 링크 흉내: CIPSEND 에 '>' 가 오지 않는다 (실기의 seq 고정 증상과 같은 원인)
    wifi.refusePrompt = true;
    size_t sBefore = countStatusLines();
    uint16_t seqBefore = seqNo;

    // 3회 연속 실패까지 돌린다. 오프라인이 되면 statusTick 이 더는 보내지 않는다.
    for (int i = 0; i < 4000 && netOnline; i++) loop();
    printf("        실패 누적 후: netOnline=%d netStep=%u seq=%u→%u (S 프레임 %zu개 추가)\n",
           (int)netOnline, netStep, seqBefore, seqNo, countStatusLines() - sBefore);
    ok(!netOnline, "연속 실패로 오프라인이 됐다 — CLOSED 통보 없이도 알아챈다");
    ok(seqNo == seqBefore, "못 나간 프레임은 seq 를 소비하지 않았다 (실기의 seq 고정과 같은 성질)");
    // REQ-0051 로 바뀐 부분: 예전엔 곧장 CIPSTART(4)로 갔는데, 그게 무한 루프의 원인이었다.
    // 이제 **CIPCLOSE(5) 부터** 간다 — 자세한 검증은 [30]~[32].
    ok(netStep == NET_CIPCLOSE, "복구가 CIPCLOSE 단계부터 시작한다 (CIPSTART 가 아니다)");

    // 링크가 살아나면 스스로 복귀한다
    wifi.refusePrompt = false;
    ok(spinUntilOnline(20000), "링크가 살아나면 스스로 재접속한다");
    size_t sAfter = countStatusLines();
    // ⚠ 루프 횟수가 아니라 **가상 시간**으로 기다려야 한다. 가상 시계는 millis() 호출 수에
    //   비례해 흐르는데, §12B.1 로 simAdvance() 가 사라지면서 루프당 호출이 크게 줄었다
    //   → 같은 spin(400) 이 예전보다 훨씬 짧은 시간이 됐다(하트비트 1000ms 에 못 미친다).
    spinMs(2500);
    ok(countStatusLines() > sAfter, "복귀 후 다시 S 프레임을 보낸다");
  }

  printf("\n[26] 한 번 성공하면 카운터가 초기화된다 — 한두 번 실패로 끊지 않는다\n");
  {
    ok(spinUntilOnline(20000), "온라인 (사전 조건)");
    sendFailStreak = 0;

    // 실패 2회 → 아직 온라인이어야 한다
    wifi.refusePrompt = true;
    while (sendFailStreak < 2 && netOnline) loop();
    printf("        실패 2회: streak=%u netOnline=%d\n", sendFailStreak, (int)netOnline);
    ok(netOnline, "2회 실패로는 끊지 않는다 (한계는 3)");

    // 한 번 성공시키면 0 으로 돌아간다
    wifi.refusePrompt = false;
    unsigned long t0 = g_millis;
    while (sendFailStreak != 0 && g_millis - t0 < 5000) loop();
    printf("        한 번 성공 후: streak=%u\n", sendFailStreak);
    ok(sendFailStreak == 0, "성공하면 카운터가 0 으로 초기화된다");
    ok(netOnline, "여전히 온라인이다");
  }

  printf("\n[27] link is not valid → 즉시 오프라인 / 그 밖 오류 문구는 로그만 (REQ-0049 ②)\n");
  {
    ok(spinUntilOnline(20000), "온라인 (사전 조건)");
    sendFailStreak = 0;

    // ERROR 는 세지 않는다 — 카운터를 건드리면 한 실패가 두 번 계수된다
    Serial.out.clear();
    wifi.deliver("ERROR\r\n");
    spin(3);
    ok(netOnline, "ERROR 하나로는 오프라인이 되지 않는다");
    ok(sendFailStreak == 0, "ERROR 는 카운터를 올리지 않는다 (이중 계수 방지)");
    ok(Serial.out.find("송신 오류 응답") != std::string::npos, "그래도 로그에는 남는다");

    // link is not valid 는 즉시 오프라인
    wifi.atLog.clear();
    wifi.deliver("link is not valid\r\n");
    spin(3);
    ok(!netOnline, "link is not valid → 즉시 오프라인 (모듈이 링크 무효를 명시했다)");

    // REQ-0051: 이 경로도 CIPSTART 로 바로 가면 무한 루프에 빠진다 → 같은 복구 사다리를 탄다.
    // ⚠ 순간 상태(netStep/staleSocket)로 단언하면 안 된다 — CIPCLOSE 가 즉시 나가고
    //   CLOSED 가 바로 와서 이미 다음 단계로 넘어가 있다. **AT 로그의 순서**로 봐야 한다.
    spinMs(300);
    printf("        AT 로그 선두: %s\n", wifi.atLog.empty() ? "(없음)" : wifi.atLog[0].c_str());
    ok(!wifi.atLog.empty() && wifi.atLog[0].find("AT+CIPCLOSE") != std::string::npos,
       "★ 복구의 첫 명령이 CIPCLOSE 다 (CIPSTART 앞에 나간다)");
    ok(spinUntilOnline(20000), "복귀한다");
  }

  printf("\n[28] CLOSED 경로 회귀 — 여전히 오프라인 + 캐시 비움\n");
  {
    ok(spinUntilOnline(20000), "온라인 (사전 조건)");
    deliverIPD("T,130,A,??,-," + xorCk("T,130,A,??,-,"));
    spin(200);
    ok(ackQ.find(130) >= 0, "rid 130 이 캐시에 있다 (사전 조건)");
    wifi.deliver("CLOSED\r\n");
    spin(3);
    ok(!netOnline, "CLOSED 로 오프라인이 된다");
    ok(ackQ.find(130) < 0, "CLOSED 는 여전히 캐시를 비운다");
    ok(spinUntilOnline(20000), "복귀한다");
  }

  printf("\n[30] ★ 복구가 CIPCLOSE 부터 간다 (REQ-0051 ①)\n");
  {
    ok(spinUntilOnline(20000), "온라인 (사전 조건)");
    sendFailStreak = 0;
    wifi.atLog.clear();

    wifi.refusePrompt = true;                       // 전송이 실패한다
    for (int i = 0; i < 4000 && netOnline; i++) loop();
    ok(!netOnline, "연속 실패로 오프라인이 됐다");
    // ★ 핵심: CIPSTART 만 나가면 안 된다. CIPCLOSE 가 먼저 나가야 한다.
    spinMs(300);
    printf("        AT 로그: CIPCLOSE %zu회, CIPSTART %zu회\n",
           wifi.countAt("AT+CIPCLOSE"), wifi.countAt("AT+CIPSTART"));
    ok(wifi.countAt("AT+CIPCLOSE") >= 1, "★ CIPCLOSE 가 나갔다 (CIPSTART 만이 아니다)");
    // 기준 1 의 실제 요구는 **순서**다: CIPCLOSE 가 CIPSTART 보다 먼저 나가야 한다.
    // (로그 선두는 실패한 AT+CIPSEND 들이라 atLog[0] 로 볼 수 없다.)
    printf("        첫 CIPCLOSE 위치=%zu, 첫 CIPSTART 위치=%zu\n",
           firstAtIndex("AT+CIPCLOSE"), firstAtIndex("AT+CIPSTART"));
    ok(firstAtIndex("AT+CIPCLOSE") < firstAtIndex("AT+CIPSTART"),
       "★ CIPCLOSE 가 CIPSTART 보다 먼저 나갔다 (기준 1)");

    // CIPCLOSE 가 먹으면 CLOSED 가 오고 정상 재접속으로 이어진다 (stickySocket=false 라 먹는다)
    wifi.refusePrompt = false;
    ok(spinUntilOnline(20000), "CIPCLOSE → CLOSED → CIPSTART → 재접속 성공");
    ok(!staleSocket, "재접속되면 낡은 소켓 의심이 해소된다");
  }

  printf("\n[31] ★ ALREADY CONNECTED 만 반복되는 상황에서 무한 루프에 빠지지 않는다 (REQ-0051 ②③)\n");
  {
    ok(spinUntilOnline(20000), "온라인 (사전 조건)");
    sendFailStreak = 0;
    wifi.atLog.clear();

    // 실기 증상 그대로: 전송 실패 + 모듈이 낡은 소켓을 붙들고 CIPCLOSE 도 안 먹는다
    wifi.refusePrompt = true;
    wifi.stickySocket = true;

    // 사다리를 끝까지 올라가는지 본다. 무한 루프면 AT+RST 가 영원히 안 나온다.
    unsigned long t0 = g_millis;
    for (int i = 0; i < 60000 && wifi.countAt("AT+RST") == 0; i++) loop();
    printf("        %lums 만에: CIPCLOSE %zu회, CIPSTART %zu회, AT+RST %zu회\n",
           g_millis - t0, wifi.countAt("AT+CIPCLOSE"), wifi.countAt("AT+CIPSTART"),
           wifi.countAt("AT+RST"));
    ok(wifi.countAt("AT+RST") >= 1, "★ 사다리가 AT+RST 까지 올라갔다 — 무한 루프에 빠지지 않는다");
    ok(wifi.countAt("AT+CIPCLOSE") >= CLOSE_ATTEMPT_LIMIT,
       "CIPCLOSE 를 한계(3회)까지 시도한 뒤 올라갔다");
    ok(!netOnline, "그 사이 ALREADY CONNECTED 를 온라인으로 받아들이지 않았다");

    // AT+RST 가 모듈을 풀었으므로, 링크만 살아나면 정상 복귀한다
    wifi.refusePrompt = false;
    ok(spinUntilOnline(30000), "RST 후 부팅 순서를 다시 타고 재접속한다");
    printf("        복귀 후 AT 로그: CWJAP %zu회 (RST 뒤 부팅 순서를 다시 탄 증거)\n",
           wifi.countAt("AT+CWJAP"));
    ok(wifi.countAt("AT+CWJAP") >= 1, "RST 뒤 CWJAP 부터 다시 진행했다");
  }

  printf("\n[32] 초기 접속 경합의 ALREADY CONNECTED 는 여전히 온라인 + 캐시 유지 (REQ-0035 [18]-4 회귀)\n");
  {
    ok(spinUntilOnline(20000), "온라인 (사전 조건)");
    // 캐시에 항목을 넣는다
    deliverIPD("T,150,A,??,-," + xorCk("T,150,A,??,-,"));
    spin(200);
    ok(ackQ.find(150) >= 0, "rid 150 이 캐시에 있다 (사전 조건)");

    // 낡은 소켓 의심이 **없는** 상태에서 ALREADY CONNECTED 가 오는 경우 = 초기 경합
    netOnline = false;
    staleSocket = false;
    wifi.deliver("ALREADY CONNECTED\r\n");
    spin(5);
    ok(netOnline, "★ 초기 경합의 ALREADY CONNECTED 는 여전히 온라인으로 받는다");
    ok(ackQ.find(150) >= 0, "★ 그리고 rid 150 을 캐시에서 지우지 않는다 (살아 있는 연결의 멱등성)");

    // 반대로 낡은 소켓 의심 중이면 온라인으로 올리지 않는다
    netOnline = false;
    staleSocket = true;
    wifi.deliver("ALREADY CONNECTED\r\n");
    spin(5);
    ok(!netOnline, "★ 낡은 소켓 의심 중의 ALREADY CONNECTED 는 믿지 않는다");
    staleSocket = false;
    ok(spinUntilOnline(20000), "복귀한다");
  }

  printf("\n[33] 모든 전선 라인이 문법·체크섬을 만족하는가 (누적 %zu줄)\n", wifi.sentLines.size());
  int bad = 0;
  for (const std::string& l : wifi.sentLines) {
    if (l.size() + 1 > 64) { bad++; continue; }                 // §2.1-6
    if (l[0] != 'S' && l[0] != 'A') { bad++; continue; }
    if (!checksumSelfConsistent(l)) bad++;
  }
  ok(bad == 0, "전 라인이 64바이트 이내 + 타입 S/A + 체크섬 일치");

  printf("\n=== 결과: %d PASS / %d FAIL ===\n\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
