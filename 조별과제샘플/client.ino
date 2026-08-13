/*
 * client.ino — 주차 관제 노드 (2열 × 5행 = 10칸)
 *
 * 근거 문서: docs/net/parking-protocol.md v1 (얼림).  요청: REQ-0018
 * 이 파일과 명세가 어긋나면 **명세가 이긴다.** 명세를 바꿔야 하면 루트에게 요청을 발행한다(§12).
 *
 * 타깃 보드 : Arduino Uno   (FQBN arduino:avr:uno)
 * 무선 모듈 : ESP-01, AT 펌웨어, SoftwareSerial 9600bps
 * 배선      : D7 = ESP TX → Uno(RX) / D8 = Uno(TX) → ESP RX   (현행 배선 유지)
 *
 * 명세 대응표
 *   §1    자리 인덱스 0..9 = A1..A5,B1..B5. 비트열 왼쪽 끝이 인덱스 0
 *   §2.2  체크섬 = 첫 바이트 ~ 체크섬 앞 쉼표(포함) XOR, 대문자 2자리 hex
 *   §2.4  S(상태) 송신 / R,C 수신 / A(ACK) 송신
 *   §3.3  AT+UART_DEF 를 쓰지 않는다 — ESP-01 플래시에 영구 기록되기 때문
 *   §3.4  하트비트 1Hz + 변화 시 100ms 디바운스, 타이머는 하나·전송하면 리셋
 *   §4.2  rid 멱등 캐시 8건 — 같은 rid 재수신 시 재적용 없이 같은 ACK 재전송
 *   §6.2  수신 4단계: LF 분할 → +IPD,<n>: 뒤 → 타입 문자 → 체크섬
 *
 * String 을 쓰지 않는다(AVR 힙 단편화). 모든 프레임은 고정 char[] + snprintf 로 만든다.
 */

#include <SoftwareSerial.h>
#include <stdio.h>
#include <string.h>

#define DEBUG 1

// ─────────────────────────────────────────────────────────────────────────
// 배선 · 네트워크 상수
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t PIN_ESP_RX = 7;   // ESP TX → Uno
static const uint8_t PIN_ESP_TX = 8;   // Uno → ESP RX
SoftwareSerial wifi(PIN_ESP_RX, PIN_ESP_TX);

#define WIFI_SSID    "3F_302"
#define WIFI_PASS    "0424719222!!"
#define SERVER_IP    "192.168.0.29"     // §11 — 명세는 주소를 가정하지 않는다. 현장에서 바꾼다
#define SERVER_PORT  "9991"
#define DEVICE_ID    "P1"               // §2.3 devid ::= 1*8자. 옛 "ARD_NODE_01"(11자)은 BNF 위반이었다

// ─────────────────────────────────────────────────────────────────────────
// 타이밍 (§3.4, §6.3)
// ─────────────────────────────────────────────────────────────────────────
static const uint16_t HEARTBEAT_MS     = 1000;  // §3.4 1Hz
static const uint16_t DEBOUNCE_MS      = 100;   // §3.4 변화 디바운스
static const uint16_t PROMPT_TIMEOUT_MS= 300;   // '>' 프롬프트 대기 상한 ("수백 ms")
static const uint16_t SEND_FAIL_BACKOFF_MS = 500; // 전송 실패 후 재시도 최소 간격
static const uint8_t  SEND_GAP_MS      = 80;    // 연속 CIPSEND 사이 최소 간격 (busy p... 회피)

// ─────────────────────────────────────────────────────────────────────────
// 자리 (§1)
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t SLOT_N = 10;      // 인덱스 0..4 = A1..A5, 5..9 = B1..B5

static inline char slotCol(uint8_t i) { return (i < 5) ? 'A' : 'B'; }
static inline char slotRow(uint8_t i) { return (char)('1' + (i % 5)); }

// 자리 문자 2개 → 인덱스. 없으면 0xFF
static uint8_t slotIndexOf(char c0, char c1) {
  if (c1 < '1' || c1 > '5') return 0xFF;
  if (c0 == 'A') return (uint8_t)(c1 - '1');
  if (c0 == 'B') return (uint8_t)(c1 - '1' + 5);
  return 0xFF;
}

static uint16_t occMask = 0;   // 점유 비트 (센서가 주인 — §7.4)
static uint16_t resMask = 0;   // 예약 비트 (서버가 주인 — §7.4. R 로 켜고 C 로만 끈다)

// ─────────────────────────────────────────────────────────────────────────
// 가상 센서 — 실물 센서가 오면 readSlotSensor() 본문만 바꾼다
// ─────────────────────────────────────────────────────────────────────────
static uint16_t      simOcc = 0;
static unsigned long simNextAt[SLOT_N];
static unsigned long simLastChangeAt = 0;

static const uint16_t SIM_MIN_GAP_MS  = 1500;   // 동시에 여러 칸이 바뀌지 않게
static const uint16_t SIM_OCC_MIN_MS  = 20000;  // 주차 후 머무는 시간
static const uint16_t SIM_OCC_MAX_MS  = 60000;
static const uint16_t SIM_EMPTY_MIN_MS= 8000;   // 빈 채로 있는 시간
static const uint16_t SIM_EMPTY_MAX_MS= 25000;
static const uint16_t ARRIVE_MIN_MS   = 4000;   // ★ 예약자 도착까지 — occupied=1,reserved=1 경로(§1.1)
static const uint16_t ARRIVE_MAX_MS   = 12000;

// 가상 센서 전용. 실물 센서로 바꾸면 이 함수는 통째로 사라진다.
static void simAdvance(uint8_t i) {
  unsigned long now = millis();
  if ((long)(now - simNextAt[i]) < 0) return;                 // 아직 때가 아니다

  // 10칸이 한꺼번에 바뀌면 주차장처럼 안 보인다 — 변화는 한 번에 하나씩
  if (now - simLastChangeAt < SIM_MIN_GAP_MS) {
    simNextAt[i] = now + SIM_MIN_GAP_MS;
    return;
  }

  uint16_t bit = (uint16_t)1 << i;
  if (simOcc & bit) {                                          // 차가 나간다
    simOcc &= (uint16_t)~bit;
    simNextAt[i] = now + (unsigned long)random(SIM_EMPTY_MIN_MS, SIM_EMPTY_MAX_MS);
  } else {                                                     // 차가 들어온다
    simOcc |= bit;
    simNextAt[i] = now + (unsigned long)random(SIM_OCC_MIN_MS, SIM_OCC_MAX_MS);
  }
  simLastChangeAt = now;
}

// ★ 실제 센서로 교체할 유일한 지점 ★
//   예) return digitalRead(SLOT_PIN[i]) ? 1 : 0;
uint8_t readSlotSensor(uint8_t i) {
  simAdvance(i);
  return (uint8_t)((simOcc >> i) & 1);
}

// ─────────────────────────────────────────────────────────────────────────
// 수신 버퍼 — 셋은 반드시 서로 다른 버퍼여야 한다
//   rxLine   : 시리얼 누적 전용 (송신 중에도 계속 채워진다)
//   workLine : 파싱 전용 (그 자리에서 쉼표를 NUL 로 바꾼다)
//   pendLine : 송신 중 도착한 줄을 미뤄 두는 곳
// 하나로 합치면 ACK 송신 중 들어온 바이트가 파싱 중인 버퍼를 덮어써서 조용히 깨진다.
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t RX_CAP = 72;
static char    rxLine[RX_CAP];
static char    workLine[RX_CAP];
static char    pendLine[RX_CAP];
static uint8_t rxLen = 0;
static bool    rxOverflow = false;
static bool    pendReady = false;
static bool    inSend = false;         // 송신 중에는 줄을 처리하지 않고 미룬다(재진입 방지)

// ─────────────────────────────────────────────────────────────────────────
// rid 멱등 캐시 (§4.2) — 최근 8건, 결과까지 같이 들고 있는다
// ─────────────────────────────────────────────────────────────────────────
static const uint8_t CACHE_N = 8;
struct AckRec {
  uint16_t rid;
  char     slot[2];
  uint8_t  result;
};
static AckRec  cache[CACHE_N];
static uint8_t cacheHead = 0;   // 다음에 덮어쓸 자리
static uint8_t cacheCount = 0;

static int8_t cacheFind(uint16_t rid) {
  for (uint8_t k = 0; k < cacheCount; k++) if (cache[k].rid == rid) return (int8_t)k;
  return -1;
}
static void cachePut(uint16_t rid, char s0, char s1, uint8_t result) {
  cache[cacheHead].rid = rid;
  cache[cacheHead].slot[0] = s0;
  cache[cacheHead].slot[1] = s1;
  cache[cacheHead].result = result;
  cacheHead = (uint8_t)((cacheHead + 1) % CACHE_N);
  if (cacheCount < CACHE_N) cacheCount++;
}

// ─────────────────────────────────────────────────────────────────────────
// 송신 상태
// ─────────────────────────────────────────────────────────────────────────
static uint16_t      seqNo = 0;                 // §2.4 uint16 순환. 재부팅하면 0
static unsigned long lastStatusAt = 0;          // §3.4 타이머는 하나
static unsigned long lastSendEndAt = 0;
static uint16_t      sentOcc = 0xFFFF, sentRes = 0xFFFF;  // 아직 아무것도 안 보냈다는 표시
static bool          changePending = false;
static unsigned long changeAt = 0;

// ─────────────────────────────────────────────────────────────────────────
// 접속 상태 기계 (논블로킹 — loop() 를 막지 않는다)
// ─────────────────────────────────────────────────────────────────────────
static bool          netOnline = false;
static uint8_t       netStep = 0;
static unsigned long netStepAt = 0;
static uint16_t      netStepWait = 500;
static const uint8_t NET_STEP_N = 5;
static const uint16_t NET_WAIT[NET_STEP_N] = { 2500, 800, 9000, 800, 5000 };

static void netSendStep(uint8_t s) {
  switch (s) {
    case 0: wifi.print(F("AT+RST\r\n")); break;
    case 1: wifi.print(F("AT+CWMODE=1\r\n")); break;
    case 2: wifi.print(F("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n")); break;
    case 3: wifi.print(F("AT+CIPMUX=0\r\n")); break;
    case 4: wifi.print(F("AT+CIPSTART=\"TCP\",\"" SERVER_IP "\"," SERVER_PORT "\r\n")); break;
    default: break;
  }
}

// ─────────────────────────────────────────────────────────────────────────
// 체크섬 (§2.2)
// ─────────────────────────────────────────────────────────────────────────
static const char HEXD[] = "0123456789ABCDEF";

static uint8_t xorRange(const char* s, uint8_t n) {
  uint8_t x = 0;
  for (uint8_t i = 0; i < n; i++) x ^= (uint8_t)s[i];
  return x;
}

// 대문자 hex 만 받는다 (§2.2 "소문자를 쓰지 않는다")
static bool hexVal(char c, uint8_t* out) {
  if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0');      return true; }
  if (c >= 'A' && c <= 'F') { *out = (uint8_t)(c - 'A' + 10); return true; }
  return false;
}

// buf 에 담긴 len 바이트(체크섬 앞 쉼표까지)의 뒤에 대문자 2자리 체크섬을 붙인다
static uint8_t appendChecksum(char* buf, uint8_t len) {
  uint8_t ck = xorRange(buf, len);
  buf[len]     = HEXD[ck >> 4];
  buf[len + 1] = HEXD[ck & 0x0F];
  buf[len + 2] = '\0';
  return (uint8_t)(len + 2);
}

static bool checksumOk(const char* s, uint8_t len) {
  int lastComma = -1;
  for (uint8_t i = 0; i < len; i++) if (s[i] == ',') lastComma = (int)i;
  if (lastComma < 0) return false;
  if ((int)len - lastComma - 1 != 2) return false;            // 체크섬은 정확히 2자리
  uint8_t hi, lo;
  if (!hexVal(s[lastComma + 1], &hi)) return false;
  if (!hexVal(s[lastComma + 2], &lo)) return false;
  // 대상: 첫 바이트 ~ 체크섬 바로 앞 쉼표(포함) = 길이 lastComma+1
  return xorRange(s, (uint8_t)(lastComma + 1)) == (uint8_t)((hi << 4) | lo);
}

// ─────────────────────────────────────────────────────────────────────────
// 수신 — 줄 조립 (§6.2 1단계)
// ─────────────────────────────────────────────────────────────────────────
static void handleLine(char* s);

static void feedRxChar(char c) {
  if (c == '\n') {
    if (rxOverflow || rxLen == 0) { rxLen = 0; rxOverflow = false; return; }
    rxLine[rxLen] = '\0';
    uint8_t n = rxLen;
    rxLen = 0;                       // ★ 파싱 전에 먼저 비운다 — 아래에서 다시 채워질 수 있다
    if (inSend) {
      if (!pendReady) { memcpy(pendLine, rxLine, (size_t)n + 1); pendReady = true; }
      // 두 번째 줄은 버린다. 서버가 재전송하므로(§7.3) 잃지 않는다
    } else {
      memcpy(workLine, rxLine, (size_t)n + 1);
      handleLine(workLine);
    }
    return;
  }
  if (c == '\r') return;                                   // AT 응답의 CR. 명세는 CR 을 보내지 않는다
  if (rxLen >= RX_CAP - 1) { rxOverflow = true; return; }  // 넘치는 줄은 통째로 버린다
  rxLine[rxLen++] = c;
}

static void pumpSerialRaw(void) {
  while (wifi.available()) feedRxChar((char)wifi.read());
}

// ─────────────────────────────────────────────────────────────────────────
// 송신 — AT+CIPSEND 뒤 '>' 프롬프트를 실제로 기다린다
// ─────────────────────────────────────────────────────────────────────────
static bool waitForPrompt(void) {
  unsigned long t0 = millis();
  while (millis() - t0 < PROMPT_TIMEOUT_MS) {
    while (wifi.available()) {
      char c = (char)wifi.read();
      if (c == '>') return true;
      feedRxChar(c);                 // 대기 중 들어온 데이터도 버리지 않는다
    }
  }
  return false;                      // 포기는 정상 동작이다 — 다음 하트비트가 곧 온다
}

// line 은 LF 없는 문자열. LF 는 여기서 붙인다(전선 종단은 LF 하나 — §2.1)
static bool sendLine(const char* line) {
  if (!netOnline) return false;
  uint8_t len = (uint8_t)strlen(line);
  if (len == 0 || len > 63) return false;                  // §2.1 한 줄 최대 64바이트(LF 포함)

  inSend = true;
  while (millis() - lastSendEndAt < SEND_GAP_MS) pumpSerialRaw();   // 연속 CIPSEND 간격

  wifi.print(F("AT+CIPSEND="));
  wifi.print((unsigned int)(len + 1));                     // +1 = LF 도 전선에 나간다
  wifi.print(F("\r\n"));

  bool ok = waitForPrompt();
  if (ok) {
    wifi.write((const uint8_t*)line, (size_t)len);
    wifi.write('\n');
  }
  lastSendEndAt = millis();
  inSend = false;

#if DEBUG
  Serial.print(ok ? F("[TX] ") : F("[TX-DROP] "));
  Serial.println(line);
#endif
  return ok;
}

// ─────────────────────────────────────────────────────────────────────────
// S — 상태 프레임 (§2.4)
// ─────────────────────────────────────────────────────────────────────────
static void bitsToStr(uint16_t mask, char* out11) {
  for (uint8_t i = 0; i < SLOT_N; i++) out11[i] = ((mask >> i) & 1) ? '1' : '0';
  out11[SLOT_N] = '\0';
}

static bool sendStatus(void) {
  char buf[64];
  char occ[SLOT_N + 1], res[SLOT_N + 1];
  bitsToStr(occMask, occ);
  bitsToStr(resMask, res);

  int n = snprintf(buf, sizeof(buf), "S,%u,%s,%s,%lu,%s,",
                   (unsigned int)seqNo, occ, res,
                   (unsigned long)(millis() / 1000UL), DEVICE_ID);
  if (n <= 0 || (unsigned)n + 3 > sizeof(buf)) return false;
  appendChecksum(buf, (uint8_t)n);
  return sendLine(buf);
}

// ─────────────────────────────────────────────────────────────────────────
// A — ACK (§2.4). R 과 C 둘 다에 대한 응답이다
// ─────────────────────────────────────────────────────────────────────────
static bool sendAck(uint16_t rid, char s0, char s1, uint8_t result) {
  char buf[24];
  int n = snprintf(buf, sizeof(buf), "A,%u,%c%c,%u,",
                   (unsigned int)rid, s0, s1, (unsigned int)result);
  if (n <= 0 || (unsigned)n + 3 > sizeof(buf)) return false;
  appendChecksum(buf, (uint8_t)n);
  return sendLine(buf);
}

// ─────────────────────────────────────────────────────────────────────────
// R / C 처리
// ─────────────────────────────────────────────────────────────────────────
static bool parseU16(const char* s, uint16_t* out) {
  if (!s || !*s) return false;
  unsigned long v = 0;
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10UL + (unsigned long)(*p - '0');
    if (v > 65535UL) return false;
  }
  *out = (uint16_t)v;
  return true;
}

// workLine 을 그 자리에서 쪼갠다(쉼표 → NUL)
static uint8_t splitFields(char* s, char* out[], uint8_t maxF) {
  uint8_t n = 0;
  out[n++] = s;
  for (char* p = s; *p; p++) {
    if (*p == ',') {
      *p = '\0';
      if (n >= maxF) return 0xFF;
      out[n++] = p + 1;
    }
  }
  return n;
}

static void processCommand(char* cand) {
  char*   f[6];
  uint8_t nf = splitFields(cand, f, 6);
  if (nf == 0xFF) return;

  char type = f[0][0];
  //  R,rid,slot,userid,cksum  (5)   /   C,rid,slot,cksum  (4)
  uint8_t want = (type == 'R') ? 5 : 4;

  uint16_t rid;
  if (nf < 3 || !parseU16(f[1], &rid)) return;   // rid 를 모르면 ACK 를 만들 수 없다 → 버린다

  // §4.2 멱등: 이미 본 rid 면 상태를 다시 바꾸지 말고 같은 ACK 를 다시 보낸다
  int8_t hit = cacheFind(rid);
  if (hit >= 0) {
#if DEBUG
    Serial.print(F("[DUP rid] ")); Serial.println(rid);
#endif
    sendAck(cache[hit].rid, cache[hit].slot[0], cache[hit].slot[1], cache[hit].result);
    return;
  }

  const char* slotTok = f[2];
  uint8_t idx = 0xFF;
  if (nf == want && strlen(slotTok) == 2) idx = slotIndexOf(slotTok[0], slotTok[1]);

  uint8_t result;
  char s0, s1;

  if (idx == 0xFF) {
    // §2.4 result=3 — 잘못된 자리 ID / 해석 불가.
    // REQ-0020 ② 판정: slot ::= ("A"/"B")("1".."5") / "??". 고정 표식을 쓰고 ACK 를 생략하지 않는다.
    // 되비추지 않는 이유: 상관 키는 rid 이고, 서버가 §4.1 매핑표에 원래 자리를 이미 들고 있다.
    // 침묵하지 않는 이유: 재전송 3회·4.5초를 태울 뿐 아니라, 의도적 거절과 링크 장애를
    //                     서버가 구분할 수 없게 되어 "센서가 죽었다"고 오해한다.
    s0 = '?';
    s1 = '?';
    result = 3;
#if DEBUG
    Serial.print(F("[BAD SLOT] rid=")); Serial.println(rid);
#endif
  } else {
    s0 = slotCol(idx);
    s1 = slotRow(idx);
    uint16_t bit = (uint16_t)1 << idx;

    if (type == 'R') {
      if (occMask & bit)      result = 1;              // 이미 점유
      else if (resMask & bit) result = 2;              // 이미 예약
      else {
        resMask |= bit;
        result = 0;
        // ★ occupied=1,reserved=1 경로(§1.1 마지막 행): 예약이 잡히면 곧 그 차가 들어온다
        simNextAt[idx] = millis() + (unsigned long)random(ARRIVE_MIN_MS, ARRIVE_MAX_MS);
      }
    } else {                                            // 'C' — 취소
      resMask &= (uint16_t)~bit;                        // 예약을 끄는 유일한 경로 (§7.4)
      result = 0;
    }
  }

  cachePut(rid, s0, s1, result);
  sendAck(rid, s0, s1, result);
}

// ─────────────────────────────────────────────────────────────────────────
// 한 줄 처리 — §6.2 의 4단계
// ─────────────────────────────────────────────────────────────────────────
static void handleLine(char* s) {
  // (a) 접속 상태 키워드. "WIFI CONNECTED" 를 TCP CONNECT 로 오인하지 않는다
  if (strncmp(s, "WIFI", 4) == 0) {
#if DEBUG
    Serial.print(F("[AT] ")); Serial.println(s);
#endif
    return;
  }
  if (strstr(s, "ALREADY CONNECT")) { netOnline = true;  return; }
  if (strcmp(s, "CONNECT") == 0)    { netOnline = true;  return; }
  if (strstr(s, "CLOSED")) {
    netOnline = false;
    netStep = 4; netStepAt = millis(); netStepWait = 1000;   // CIPSTART 만 다시
    return;
  }

  // (b) §6.2 2단계 — +IPD,<n>: 이 있으면 그 뒤부터가 후보
  char* cand = s;
  char* ipd = strstr(s, "+IPD,");
  if (ipd) {
    char* colon = strchr(ipd, ':');
    if (!colon) return;
    cand = colon + 1;
  }

  uint8_t len = (uint8_t)strlen(cand);
  if (len == 0 || len > 63) return;                    // §2.1 한 줄 최대 64바이트(LF 포함)

  // (c) 3단계 — 타입 문자. 모르는 타입은 조용히 버린다(§2.1-7)
  if (cand[0] != 'R' && cand[0] != 'C') return;

  // (d) 4단계 — 체크섬. AT 잡음이 우연히 R 로 시작해도 여기서 걸린다
  if (!checksumOk(cand, len)) {
#if DEBUG
    Serial.print(F("[CKSUM NG] ")); Serial.println(cand);
#endif
    return;
  }

  processCommand(cand);
}

static void drainPending(void) {
  if (!pendReady) return;
  memcpy(workLine, pendLine, RX_CAP);
  pendReady = false;
  handleLine(workLine);
}

// ─────────────────────────────────────────────────────────────────────────
// 주기 처리
// ─────────────────────────────────────────────────────────────────────────
static void netTick(unsigned long now) {
  if (netOnline) return;
  if (now - netStepAt < netStepWait) return;

  if (netStep < NET_STEP_N) {
    netSendStep(netStep);
    netStepWait = NET_WAIT[netStep];
    netStepAt = now;
    netStep++;
    return;
  }
  netStep = 4;                                   // CIPSTART 부터 다시 시도
  netStepAt = now;
  netStepWait = 0;
}

static void sensorTick(void) {
  uint16_t m = 0;
  for (uint8_t i = 0; i < SLOT_N; i++) if (readSlotSensor(i)) m |= (uint16_t)1 << i;
  occMask = m;
}

static void statusTick(unsigned long now) {
  bool changed = (occMask != sentOcc) || (resMask != sentRes);
  if (changed && !changePending) { changePending = true; changeAt = now; }
  if (!changed) changePending = false;

  bool heartbeatDue = (now - lastStatusAt >= HEARTBEAT_MS);
  // changeAt 은 전송 실패 시 **미래 시각**으로 밀린다. unsigned 뺄셈으로 비교하면
  // 언더플로로 곧장 참이 되어 백오프가 통째로 무력화된다 → 부호 있는 비교로 본다.
  bool debounced    = changePending && ((long)(now - changeAt) >= (long)DEBOUNCE_MS);
  if (!netOnline || !(heartbeatDue || debounced)) return;

  uint16_t occSnap = occMask, resSnap = resMask;
  bool ok = sendStatus();

  lastStatusAt = millis();          // §3.4 어떤 이유로든 S 를 보내면 타이머 리셋 (타이머는 하나)
  if (ok) {
    seqNo++;                        // 나가지 못한 프레임은 번호를 소비하지 않는다
    sentOcc = occSnap;
    sentRes = resSnap;
    changePending = false;
  } else {
    changeAt = lastStatusAt + SEND_FAIL_BACKOFF_MS - DEBOUNCE_MS;   // 실패 후 재시도 간격 확보
  }
}

// ─────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  wifi.begin(9600);                 // §3.3 AT+UART_DEF 로 보율을 바꾸지 않는다

  randomSeed((unsigned long)analogRead(A0) ^ micros());

  // 시작 시 몇 칸은 차 있는 편이 주차장답다: A2, A3, B4
  simOcc = (uint16_t)((1U << 1) | (1U << 2) | (1U << 8));
  for (uint8_t i = 0; i < SLOT_N; i++) {
    simNextAt[i] = 3000UL + (unsigned long)i * 1700UL + (unsigned long)random(0, 1500);
  }
  simLastChangeAt = 0;

  netStep = 0;
  netStepAt = millis();
  netStepWait = 500;

#if DEBUG
  Serial.println(F("\n[PARKING NODE] proto v1 / 10 slots / dev=" DEVICE_ID));
#endif
}

void loop() {
  unsigned long now = millis();
  netTick(now);
  pumpSerialRaw();
  drainPending();
  sensorTick();
  statusTick(now);
}
