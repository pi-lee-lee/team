/*
 * client.ino (nextgen) — 주차 관제 노드 · **통신을 EspLink 에 맡긴 판**   (요청: REQ-0091)
 *
 * 옛 판(`조별과제샘플/client.ino`)과 하는 일은 같고 **전선 형식도 완전히 같다**(§2 그대로).
 * 다른 것은 하나뿐이다: **AT 명령·프롬프트 대기·재동기·복구 사다리가 이 파일에서 사라졌다.**
 * 전부 `EspLink.h` 안으로 들어갔다.
 *
 *   추출 성공 판정 기준(REQ-0091): **이 파일에 AT 명령 문자열이 한 번도 안 나온다.**
 *   하나라도 남아 있으면 추상화가 샌 것이다.
 *   ⚠ 판정 문구에 그 문자열 자체를 쓰면 grep 이 주석을 세어 **영원히 실패한다.**
 *     그래서 여기서는 풀어서 적었다. 검사: grep -c 'AT&#43;' 가 0 이면 통과.
 *
 * 옛 판에서 사라진 것 — 약 700행이 헤더로 갔다:
 *   netTick / netSendStep / netAdvance / waitForPrompt / sendLine / 프롬프트 재동기 /
 *   복구 사다리 0~6단 / CIPCLOSE 사다리 / hasUsableIp / DIAG / 부팅원인 / RAM 계측
 *
 * 남은 것 — **도메인 로직뿐이다**: 자리 10칸 · 센서/시뮬 · 프레임 조립과 체크섬 ·
 *   예약(R/C) · 테스트모드(T) · 시뮬 트리거(M) · ACK 멱등 캐시.
 *
 * 타깃 : Arduino Uno (FQBN arduino:avr:uno) · ESP-01 · SoftwareSerial 9600
 * 배선 : 핀 8 = ESP TX → Uno,  핀 7 = Uno → ESP RX   (옛 판 코드와 동일)
 */

// #define ESPLINK_DEBUG 1     // ← 안 될 때 주석을 푼다(반드시 include 위에). AT 대화를 전부 찍는다
#include "EspLink.h"
#include <EEPROM.h>

// ─────────────────────────────────────────────────────────────────────────
// 진단 출력 (REQ-0094) — **사건이 일어날 때만** 찍는다. 매 프레임 로그는 금지다
// (초당 1프레임 × 4시간 = 14,400줄이면 진짜 신호가 묻힌다).
// 접두의 `millis()` 를 캡처 쪽 벽시계와 맞추면 **서버 로그와 3자 대조**가 된다.
// ─────────────────────────────────────────────────────────────────────────
#define DIAG_AT()   do { Serial.print('['); Serial.print(millis()); Serial.print(F("] ")); } while (0)

// ── 부팅 카운터는 EEPROM 에 둔다 ──
// ⚠ `MCUSR` 을 쓰지 않는 이유: **이 보드 부트로더가 앱보다 먼저 지운다.** 플래시 덤프를
//   디스어셈블해 확인했다(REQ-0092): `in r24,0x34` 직후 `out 0x34,r1`, `mov r2,r24` 없음.
//   → 리셋 **원인**은 소프트웨어로 못 얻는다. **횟수**만이라도 EEPROM 으로 센다.
// ⚠ `.noinit` RAM 이 아니라 EEPROM 인 이유: 전원이 빠져도 남아야 서버가 못 본 구간의
//   재부팅까지 소급된다. 쓰기는 부팅당 1회뿐이라 수명(10만 회)에 문제없다.
static const uint16_t EE_MAGIC_ADDR = 0;
static const uint16_t EE_COUNT_ADDR = 2;
static const uint16_t EE_MAGIC      = 0xB007;
static uint16_t bootCount = 0;

static void bootCounterBump(void) {
  uint16_t magic = 0;
  EEPROM.get(EE_MAGIC_ADDR, magic);
  if (magic != EE_MAGIC) {                 // 처음 쓰는 보드 — 0 부터 시작한다
    magic = EE_MAGIC; bootCount = 0;
    EEPROM.put(EE_MAGIC_ADDR, magic);
  } else {
    EEPROM.get(EE_COUNT_ADDR, bootCount);
  }
  bootCount++;
  EEPROM.put(EE_COUNT_ADDR, bootCount);
}

// ── RAM 여유 ── 힙을 안 쓰므로(String·malloc 없음) __heap_start ~ 스택 포인터가 미사용 영역이다
extern uint8_t __heap_start;
static uint16_t ramMin = 0xFFFF;
static uint16_t dropRun = 0;      // 연속 송신 실패 수 (로그 속도 제한용)
static uint32_t lastDropLog = 0;
static uint16_t ramFree(void) {
  uint8_t here;
  return (uint16_t)(&here - (uint8_t*)&__heap_start);
}

EspLink link(8, 7);

#define WIFI_SSID   "SK_WiFiGIGA50DC_2.4G"
#define WIFI_PASS   "2011050796"
#define SERVER_IP   "192.168.35.21"
#define SERVER_PORT 9991
#define DEVICE_ID   "P1"          // §2.3 devid ::= 1*8자

// ─────────────────────────────────────────────────────────────────────────
// 자리 (§1) — 인덱스 0..4 = A1..A5, 5..9 = B1..B5
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t SLOT_N = 10;
static uint16_t occMask = 0;      // 점유 (센서가 주인)
static uint16_t resMask = 0;      // 예약 (서버가 주인 — R 로 켜고 C 로만 끈다)
static uint16_t simOcc  = 0;      // 시뮬 칸의 값
static uint16_t srcReal = 0;      // 비트 1 = 그 칸은 실물 센서
static uint16_t ovrActive = 0, ovrValue = 0;   // 테스트 주입
static bool     testArmed = false;

// 핀 배정 근거는 옛 판 주석 참조(D0/D1 USB · D7/D8 ESP · D13 LED · A4/A5 I2C 는 못 쓴다).
static const uint8_t SLOT_PIN[SLOT_N] PROGMEM = { 2,3,4,5,6, 9,10,11,12,A0 };
static inline uint8_t slotPin(uint8_t i) { return pgm_read_byte(&SLOT_PIN[i]); }
#define SENSOR_ACTIVE_LOW 1

static uint8_t slotIndexOf(char c0, char c1) {
  if (c1 < '1' || c1 > '5') return 0xFF;
  if (c0 == 'A') return (uint8_t)(c1 - '1');
  if (c0 == 'B') return (uint8_t)(c1 - '1' + 5);
  return 0xFF;
}
static inline char slotCol(uint8_t i) { return (i < 5) ? 'A' : 'B'; }
static inline char slotRow(uint8_t i) { return (char)('1' + (i % 5)); }

static uint8_t readSlot(uint8_t i) {
  uint16_t bit = (uint16_t)1 << i;
  if (testArmed && (ovrActive & bit)) return (ovrValue & bit) ? 1 : 0;   // 주입이 최우선
  if (srcReal & bit) {
    uint8_t raw = digitalRead(slotPin(i));
#if SENSOR_ACTIVE_LOW
    return (raw == LOW) ? 1 : 0;
#else
    return (raw == HIGH) ? 1 : 0;
#endif
  }
  return (simOcc & bit) ? 1 : 0;
}

// M 프레임 한 번 = 시뮬 칸 하나만 뒤집는다(§12B.2). 1순위는 "예약됐는데 빈 칸"을 채우는 것 —
// 그래야 occupied=1 ∧ reserved=1 이라는 성공 상태에 사람이 도달할 수 있다.
static bool simCandidate(uint8_t i) {
  uint16_t bit = (uint16_t)1 << i;
  if (srcReal & bit) return false;
  if (testArmed && (ovrActive & bit)) return false;
  return true;
}
static uint8_t simStep(void) {
  for (uint8_t i = 0; i < SLOT_N; i++) {
    uint16_t bit = (uint16_t)1 << i;
    if (simCandidate(i) && (resMask & bit) && !(simOcc & bit)) { simOcc |= bit; return i; }
  }
  uint8_t n = 0;
  for (uint8_t i = 0; i < SLOT_N; i++) if (simCandidate(i)) n++;
  if (!n) return 0xFF;
  uint8_t pick = (uint8_t)random(0, n);
  for (uint8_t i = 0; i < SLOT_N; i++) {
    if (!simCandidate(i)) continue;
    if (pick-- == 0) { simOcc ^= (uint16_t)1 << i; return i; }
  }
  return 0xFF;
}

// ─────────────────────────────────────────────────────────────────────────
// 체크섬 (§2.2) — 첫 바이트 ~ 체크섬 앞 쉼표(포함) XOR, 대문자 2자리 hex
// ⚠ 범위를 한 글자라도 다르게 잡으면 서버와 영원히 안 맞는다.
// ─────────────────────────────────────────────────────────────────────────
static const char HEXD[] = "0123456789ABCDEF";
static uint8_t xorRange(const char* s, uint8_t n) {
  uint8_t x = 0; for (uint8_t i = 0; i < n; i++) x ^= (uint8_t)s[i]; return x;
}
static uint8_t appendChecksum(char* buf, uint8_t len) {
  uint8_t x = xorRange(buf, len);
  buf[len]   = HEXD[x >> 4];
  buf[len+1] = HEXD[x & 0x0F];
  buf[len+2] = '\0';
  return (uint8_t)(len + 2);
}
static bool hexVal(char c, uint8_t* out) {
  if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0');      return true; }
  if (c >= 'A' && c <= 'F') { *out = (uint8_t)(c - 'A' + 10); return true; }
  if (c >= 'a' && c <= 'f') { *out = (uint8_t)(c - 'a' + 10); return true; }
  return false;
}
static bool checksumOk(const char* s, uint8_t len) {
  if (len < 4) return false;
  uint8_t hi, lo;
  if (!hexVal(s[len-2], &hi) || !hexVal(s[len-1], &lo)) return false;
  return xorRange(s, (uint8_t)(len - 2)) == (uint8_t)((hi << 4) | lo);
}

// ─────────────────────────────────────────────────────────────────────────
// rid 멱등 캐시 (§4.2) — 같은 rid 를 다시 받으면 **재적용 없이** 같은 ACK 를 되돌린다
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t CACHE_N = 8;
struct AckRec { uint16_t rid; char slot[2]; uint8_t result; };
static AckRec cache[CACHE_N];
static uint8_t cacheHead = 0, cacheCount = 0;
static int8_t cacheFind(uint16_t rid) {
  for (uint8_t k = 0; k < cacheCount; k++) if (cache[k].rid == rid) return (int8_t)k;
  return -1;
}
static void cachePut(uint16_t rid, char s0, char s1, uint8_t r) {
  cache[cacheHead].rid = rid; cache[cacheHead].slot[0] = s0;
  cache[cacheHead].slot[1] = s1; cache[cacheHead].result = r;
  cacheHead = (uint8_t)((cacheHead + 1) % CACHE_N);
  if (cacheCount < CACHE_N) cacheCount++;
}

// ─────────────────────────────────────────────────────────────────────────
// 송신
// ─────────────────────────────────────────────────────────────────────────
static uint16_t seqNo = 0;
static uint32_t lastStatusAt = 0, changeAt = 0;
static uint16_t sentOcc = 0xFFFF, sentRes = 0xFFFF, sentTmask = 0xFFFE;
static bool changePending = false;
static const uint16_t TMASK_ABSENT = 0xFFFF;

static void bitsToStr(uint16_t mask, char* out) {
  for (uint8_t i = 0; i < SLOT_N; i++) out[i] = ((mask >> i) & 1) ? '1' : '0';
  out[SLOT_N] = '\0';
}

static bool sendStatus(void) {
  char buf[64], occ[SLOT_N+1], res[SLOT_N+1];
  bitsToStr(occMask, occ); bitsToStr(resMask, res);
  int n;
  if (testArmed) {
    char tm[SLOT_N+1]; bitsToStr(ovrActive, tm);
    n = snprintf(buf, sizeof(buf), "S,%u,%s,%s,%lu,%s,%s,",
                 (unsigned)seqNo, occ, res, (unsigned long)(millis()/1000UL), DEVICE_ID, tm);
  } else {
    n = snprintf(buf, sizeof(buf), "S,%u,%s,%s,%lu,%s,",
                 (unsigned)seqNo, occ, res, (unsigned long)(millis()/1000UL), DEVICE_ID);
  }
  if (n <= 0 || (unsigned)n + 3 > sizeof(buf)) return false;   // 잘린 줄은 내보내지 않는다
  uint8_t total = appendChecksum(buf, (uint8_t)n);
  return link.,send((const uint8_t*)buf, total);                // ★ 전송은 이 한 줄이 전부다
}

static bool sendAck(uint16_t rid, char s0, char s1, uint8_t result) {
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "A,%u,%c%c,%u,", (unsigned)rid, s0, s1, (unsigned)result);
  if (n <= 0 || (unsigned)n + 3 > sizeof(buf)) return false;
  uint8_t total = appendChecksum(buf, (uint8_t)n);
  return link.send((const uint8_t*)buf, total);
}

// ─────────────────────────────────────────────────────────────────────────
// 수신 처리 — R(예약) / C(취소) / T(테스트) / M(시뮬 트리거)
// ─────────────────────────────────────────────────────────────────────────
static bool parseU16(const char* s, uint16_t* out) {
  if (!*s) return false;
  uint32_t v = 0;
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (uint32_t)(*p - '0');
    if (v > 65535UL) return false;
  }
  *out = (uint16_t)v; return true;
}
static uint8_t splitFields(char* s, char* out[], uint8_t maxF) {
  uint8_t n = 0; out[n++] = s;
  for (char* p = s; *p && n < maxF; p++) if (*p == ',') { *p = '\0'; out[n++] = p + 1; }
  return n;
}

static void processCommand(char* cand) {
  char* f[8];
  uint8_t nf = splitFields(cand, f, 8);
  if (nf < 3) return;

  uint16_t rid;
  if (!parseU16(f[1], &rid)) return;

  char s0 = '?', s1 = '?';
  uint8_t result = 4;                       // 4 = 알 수 없는 명령

  int8_t hit = cacheFind(rid);
  if (hit >= 0) {                           // §4.2 재수신 — **재적용하지 않고** 같은 답을 되돌린다
    sendAck(rid, cache[hit].slot[0], cache[hit].slot[1], cache[hit].result);
    return;
  }

  switch (f[0][0]) {
    case 'R': case 'C': {
      if (nf < 4 || strlen(f[2]) < 2) { result = 3; break; }
      s0 = f[2][0]; s1 = f[2][1];
      uint8_t i = slotIndexOf(s0, s1);
      if (i == 0xFF) { result = 3; break; }  // 3 = 그런 자리가 없다
      uint16_t bit = (uint16_t)1 << i;
      if (f[0][0] == 'R') {
        if (occMask & bit)      result = 1;  // 1 = 이미 점유
        else if (resMask & bit) result = 2;  // 2 = 이미 예약
        else { resMask |= bit;  result = 0; }
      } else {
        if (!(resMask & bit))   result = 2;
        else { resMask &= (uint16_t)~bit; result = 0; }
      }
      break;
    }
    case 'M': {                              // 시뮬 한 걸음
      uint8_t i = simStep();
      if (i == 0xFF) { result = 5; s0 = '?'; s1 = '?'; }
      else { result = 0; s0 = slotCol(i); s1 = slotRow(i); }
      break;
    }
    case 'T': {                              // 테스트 모드 (§12A)
      if (nf < 5) { result = 3; break; }
      char op = f[2][0];
      if (op == 'A')      { testArmed = true;  ovrActive = 0; ovrValue = 0; result = 0; }
      else if (op == 'D') { testArmed = false; ovrActive = 0; ovrValue = 0; result = 0; }
      else if (!testArmed) { result = 6; }    // 6 = 무장하지 않았다
      else if (op == 'S' || op == 'X') {
        if (strlen(f[3]) < 2) { result = 3; break; }
        s0 = f[3][0]; s1 = f[3][1];
        uint8_t i = slotIndexOf(s0, s1);
        if (i == 0xFF) { result = 3; break; }
        uint16_t bit = (uint16_t)1 << i;
        if (op == 'S') {
          ovrActive |= bit;
          if (f[4][0] == '1') ovrValue |= bit; else ovrValue &= (uint16_t)~bit;
        } else { ovrActive &= (uint16_t)~bit; ovrValue &= (uint16_t)~bit; }
        result = 0;
      } else result = 3;
      break;
    }
    default: return;                          // 모르는 타입은 조용히 버린다 (§2.1-7)
  }

  cachePut(rid, s0, s1, result);
  sendAck(rid, s0, s1, result);
}

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  randomSeed((unsigned long)analogRead(A1) ^ micros());
  for (uint8_t i = 0; i < SLOT_N; i++)
    if (srcReal & ((uint16_t)1 << i)) pinMode(slotPin(i), INPUT_PULLUP);

  testArmed = false; ovrActive = 0; ovrValue = 0;
  simOcc = (uint16_t)((1U << 1) | (1U << 2) | (1U << 8));   // A2·A3·B4 를 채운 채로 시작

  Serial.println(F("\n[PARKING NODE nextgen] proto v1 / 10 slots / dev=" DEVICE_ID));
  bootCounterBump();
  DIAG_AT(); Serial.print(F("[BOOT] n=")); Serial.println(bootCount);
  link.begin(WIFI_SSID, WIFI_PASS, SERVER_IP, SERVER_PORT);  // ★ 기다리지 않는다
}

void loop() {
  // ── 받기 ── (EspLink 가 줄 조립까지 해 준다. 우리는 체크섬만 보고 해석한다)
  char line[64];
  int n = link.receive(line, sizeof(line));
  if (n > 0) {
    if (checksumOk(line, (uint8_t)n)) processCommand(line);
#if ESPLINK_DEBUG
    else { Serial.print(F("[CKSUM NG] ")); Serial.println(line); }
#endif
  }

  // ── 센서 읽기 ──
  uint16_t m = 0;
  for (uint8_t i = 0; i < SLOT_N; i++) if (readSlot(i)) m |= (uint16_t)1 << i;
  occMask = m;

  // ── 보내기 ── §3.4 하트비트 1Hz + 변화 시 100ms 디바운스. 타이머는 하나다.
  uint32_t now = millis();
  uint16_t tmaskNow = testArmed ? ovrActive : TMASK_ABSENT;
  bool changed = (occMask != sentOcc) || (resMask != sentRes) || (tmaskNow != sentTmask);
  if (changed && !changePending) { changePending = true; changeAt = now; }
  if (!changed) changePending = false;

  bool due       = (now - lastStatusAt >= 1000UL);
  bool debounced = changePending && ((long)(now - changeAt) >= 100L);
  if (!(due || debounced)) return;

  uint16_t o = occMask, r = resMask, t = tmaskNow;
  bool ok = sendStatus();
  lastStatusAt = millis();
  if (ok) {
    seqNo++;                       // 나가지 못한 프레임은 번호를 소비하지 않는다
    sentOcc = o; sentRes = r; sentTmask = t; changePending = false;
    if (dropRun) {                 // 끊겼다가 돌아왔다 — 몇 개를 버렸는지 한 줄로 닫는다
      DIAG_AT(); Serial.print(F("[TX-DROP] end run=")); Serial.println(dropRun);
      dropRun = 0;
    }
  } else {
    // ★ 보내려다 버린 프레임. **서버 로그에는 절대 안 나타난다** — seqNo 가 성공 시에만
    //   증가해서 구멍을 안 남기기 때문이다(REQ-0075 에서 확인한 성질).
    //   즉 상행 소실을 잴 수 있는 곳은 여기뿐이다.
    //
    // ⚠⚠ **속도 제한이 반드시 있어야 한다. 없이 냈다가 장치를 못 붙게 만들었다(11:22 실측).**
    //   접속이 안 된 동안 매 시도마다 찍으면 **초당 2줄**이 나가는데, 그 하드웨어 UART 송신이
    //   SoftwareSerial 수신 타이밍을 흔들어 **`Linked` 를 놓쳤다.** 74초간 접속 실패였고,
    //   서버에는 "7초 안에 유효 프레임 없음"으로 소켓이 잘렸다.
    //   진단 출력이 관측 대상을 고장 낸 것이다 — 오늘 DTR 리셋과 같은 형태다.
    //   → **끊김이 시작될 때 한 줄, 그 뒤로는 10초에 한 줄(누적 수와 함께).**
    dropRun++;
    if (dropRun == 1 || now - lastDropLog >= 10000UL) {
      lastDropLog = now;
      DIAG_AT(); Serial.print(F("[TX-DROP] seq=")); Serial.print(seqNo);
      Serial.print(F(" run=")); Serial.println(dropRun);
    }
    changeAt = lastStatusAt + 500UL - 100UL;   // 실패 후 재시도 간격 확보
  }

  // ── 60초마다 RAM 한 줄 ── (최저치를 같이 낸다. 누수는 min 이 계속 내려가는 것으로 보인다)
  uint16_t f = ramFree();
  if (f < ramMin) ramMin = f;
  static uint32_t lastRam = 0;
  if (now - lastRam >= 60000UL) {
    lastRam = now;
    DIAG_AT(); Serial.print(F("[RAM] free=")); Serial.print(f);
    Serial.print(F(" min=")); Serial.println(ramMin);
  }
}
