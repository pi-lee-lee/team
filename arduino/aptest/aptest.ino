/*
 * aptest.ino — 2판: **MAC 만 바꿔 A/B** 로 차단 가설을 확정하거나 죽인다   (요청: REQ-0076)
 *
 * 1판 결과(REQ-0074): 141행 최소 스케치도 `CWJAP FAIL`.
 *   → **client.ino 로직은 결합 실패의 원인이 아니다.** 남은 후보는 AP 차단과 전원/RF.
 *
 * 2판이 가르는 것: **같은 보드·같은 전원·같은 코드에서 MAC 하나만 바꾼다.**
 *   라우터 재기동은 라우터 상태를 통째로 바꾸지만(블랙리스트·채널·DHCP 가 한꺼번에 움직인다),
 *   MAC 변경은 변수가 하나뿐이라 전원 가설과 차단 가설을 깨끗하게 가른다.
 *
 * 타깃 보드 : Arduino Uno (FQBN arduino:avr:uno) · ESP-01 구형 ai-thinker AT · SoftwareSerial 9600
 *
 * ─────────────────────────────────────────────────────────────────────────
 * ★ 판정표 — **결과를 보기 전에 미리 못 박는다**
 * ─────────────────────────────────────────────────────────────────────────
 *   A(원래 MAC) 성공            → 차단이 이미 풀렸거나 애초에 없었다. 지금은 붙는다
 *   A 실패 · B(새 MAC) 성공     → **AP 차단 확정.** 같은 전원으로 붙었으므로 **전원·HW 무죄**
 *   A 실패 · B 실패             → 차단이 아니다 → **전원/RF.** 다음은 전원 분리 실험
 *   (재부팅이 끼면 위 판정 보류)  → 아래 ⚠ 참조. 시행 중 리셋이 있었으면 그 시행은 무효다
 *
 * ─────────────────────────────────────────────────────────────────────────
 * ⚠⚠ 1판 로그에서 **판정표에 없던 것**이 나왔다 — 이번 판이 그걸 센다
 * ─────────────────────────────────────────────────────────────────────────
 *   (1) `/tmp/aptest1-trial1.log` : 스케치가 **멈춘 뒤**(AT 를 한 줄도 안 보내는 상태) ESP 가
 *       `[System Ready]` 를 **4번 더** 냈다. 아무 자극도 없는데 모듈이 스스로 재부팅한 것이다.
 *   (2) `/tmp/aptest2.log` : `[APTEST]` 배너가 두 번 찍혔다 = **MCU 가 시험 도중 재시작**했다.
 *   (3) 같은 로그에 `Vendor:www.ai-thinker.aom` — `com` 이 `aom` 으로 **한 바이트 깨졌다.**
 *
 *   **AP 는 칩을 재부팅시킬 수 없다.** 그래서 이 셋은 차단 가설이 아니라 전원/접점 쪽 신호다.
 *   다만 이번 A/B 를 오염시킬 수도 있다(재부팅 때문에 실패한 것을 "차단"으로 오독할 위험).
 *   → 그래서 **시행마다 ESP 재부팅·MCU 재부팅을 세서 판정에 같이 찍는다.**
 *
 * 여전히 **없는 것**: TCP(CIPSTART/CIPSEND) · 재시도 · 센서 · 프레임 · 사다리 · 시뮬.
 * ⚠ 핀 A2 는 INPUT(하이임피던스) 그대로 = ESP 를 리셋으로 잡지 않는다. 배선은 그대로 둬도 된다.
 */

#include <SoftwareSerial.h>
#include <avr/wdt.h>
#include <string.h>

// ── 부팅 원인 + 부팅 횟수 (REQ-0076 ⑤) ──────────────────────────────────
// MCUSR 은 부트로더가 지울 수 있으므로 `.init3`(스택은 섰고 .bss 초기화 전)에서 먼저 복사한다.
// 복사본은 `.noinit` 이라야 한다 — 일반 전역이면 뒤이은 .bss 초기화가 0 으로 덮는다.
// bootСount 도 같은 곳에 둬서 **MCU 가 몇 번째로 부팅했는지**를 로그 맨 위에 찍는다.
// 1판에서 배너가 두 번 찍힌 것을 사람이 눈으로 세야 했는데, 이제 `[BOOT] #2` 로 바로 보인다.
uint8_t  mcusrMirror  __attribute__((section(".noinit")));
uint16_t bootCount    __attribute__((section(".noinit")));
uint16_t bootMagic    __attribute__((section(".noinit")));
static const uint16_t BOOT_MAGIC = 0xB007;

void earlyInitCapture(void) __attribute__((naked, used, section(".init3")));
void earlyInitCapture(void) {
  mcusrMirror = MCUSR;
  MCUSR = 0;
  wdt_disable();          // 워치독으로 들어왔어도 즉시 해제 (부트 루프 방지)
}

// ── client.ino 와 같은 배선·자격증명 ────────────────────────────────────
// ⚠ client.ino:9 의 머리 주석("D7 = ESP TX")은 **낡았다.** 실제로 도는 것은 client.ino:80-82 의
//   `SoftwareSerial wifi(8, 7)` 이고 그게 지금 동작하는 배선이다. 여기서는 코드 쪽을 복사했다.
static const uint8_t PIN_ESP_RX = 8;   // ESP TX → Uno (Uno 가 받는 핀)
static const uint8_t PIN_ESP_TX = 7;   // Uno → ESP RX (Uno 가 보내는 핀)
SoftwareSerial wifi(PIN_ESP_RX, PIN_ESP_TX);

#define WIFI_SSID  "unknowns_network2.4"
#define WIFI_PASS  "Dmagbdgks_spxmdnjzm!01"

// B 시행에 쓸 가짜 MAC.
// ⚠ 첫 옥텟 0x1a: 최하위 비트 0 = **유니캐스트**(1 이면 멀티캐스트라 모듈이 거부한다),
//   그 위 비트 1 = **로컬 관리 주소**(제조사 대역과 충돌하지 않는다). 이 두 조건이 핵심이다.
#define FAKE_MAC   "1a:fe:34:11:22:33"

static const uint16_t PROBE_MS = 5000;

static char          lineBuf[72];
static uint8_t       lineLen = 0;
static char          ipBuf[20]  = "?";
static char          macBuf[24] = "(못 읽음)";
static bool          macCmdCur = false;     // _CUR 변종이 먹혔는가

static unsigned long assocAt = 0;
static unsigned long lastProbe = 0;
static bool          monitoring = false;    // 결합 유지 관측 중인가
static bool          stopped = false;
static bool          sawDisconnect = false;

static uint8_t       espBoots = 0;          // 이번 시행 중 ESP 재부팅 수
static uint8_t       espBootsTotal = 0;

// "이 줄에 쓸 만한 IPv4 가 있는가" — client.ino 의 hasUsableIp() 와 같은 판정(0.x.x.x 제외)
static bool hasIp(const char* s) {
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') continue;
    if (p != s && p[-1] >= '0' && p[-1] <= '9') continue;
    uint16_t oct = 0; uint8_t dots = 0, first = 0;
    const char* q = p;
    for (;;) {
      if (*q >= '0' && *q <= '9') { oct = (uint16_t)(oct * 10 + (*q - '0')); q++; continue; }
      if (dots == 0) first = (uint8_t)oct;
      if (*q == '.') { dots++; oct = 0; q++; continue; }
      break;
    }
    if (dots == 3 && first != 0) return true;
  }
  return false;
}

// 콜론이 5개면 MAC 꼴로 본다 (`+CIPSTAMAC:"18:fe:34:.."` 도 맨 MAC 도 둘 다 걸린다)
static bool looksLikeMac(const char* s) {
  uint8_t c = 0;
  for (const char* p = s; *p; p++) if (*p == ':') c++;
  return c >= 5;
}

// 모듈이 보내는 줄을 그대로 찍는다.
//   want   : 이 문구가 든 줄을 보면 즉시 끝낸다(NULL 이면 시간을 다 채운다)
//   반환   : 1 = want / -1 = FAIL·ERROR / 0 = 시간만 흘렀다
static int8_t readLines(const char* want, uint32_t ms, bool* gotIp, bool grabMac) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    while (wifi.available()) {
      char c = (char)wifi.read();
      if (c == '\r') continue;
      if (c != '\n') {
        if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
        continue;
      }
      if (lineLen == 0) continue;
      lineBuf[lineLen] = '\0';
      lineLen = 0;
      Serial.print(F("   [AT] "));
      Serial.println(lineBuf);

      // ★ ESP 자발 재부팅 계수. 1판에서 배너 문자가 깨진 적이 있어(`.aom`) 벤더명이 아니라
      //   **`Ready`** 로 찾는다 — 가장 덜 깨지고 가장 확실한 토막이다.
      if (strstr(lineBuf, "Ready")) {
        if (espBoots < 255) espBoots++;
        if (espBootsTotal < 255) espBootsTotal++;
      }
      if (strstr(lineBuf, "WIFI DISCONNECT")) sawDisconnect = true;
      if (grabMac && strncmp(lineBuf, "AT+", 3) != 0 && looksLikeMac(lineBuf)) {
        strncpy(macBuf, lineBuf, sizeof(macBuf) - 1);
        macBuf[sizeof(macBuf) - 1] = '\0';
      }
      if (gotIp && strncmp(lineBuf, "AT+", 3) != 0 && hasIp(lineBuf)) {
        *gotIp = true;
        strncpy(ipBuf, lineBuf, sizeof(ipBuf) - 1);
        ipBuf[sizeof(ipBuf) - 1] = '\0';
      }
      if (want && strstr(lineBuf, want))                       return 1;
      if (strstr(lineBuf, "FAIL") || strstr(lineBuf, "ERROR")) return -1;
    }
  }
  return 0;
}

// CWJAP 한 번. 붙고 IP 까지 받으면 true.
static bool tryJoin(const __FlashStringHelper* tag) {
  espBoots = 0;
  Serial.print(F("[APTEST] ── "));
  Serial.print(tag);
  Serial.println(F(" 시행: AT+CWJAP (최대 30초) ──"));

  unsigned long t0 = millis();
  wifi.print(F("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n"));
  int8_t r = readLines("OK", 30000, NULL, false);
  unsigned long took = (millis() - t0) / 1000UL;

  Serial.print(F("[APTEST] "));
  Serial.print(tag);
  Serial.print(F(" 결과: "));
  Serial.print(r == 1 ? F("OK") : (r == -1 ? F("FAIL/ERROR") : F("무응답(30초)")));
  Serial.print(F(" · 소요 "));
  Serial.print(took);
  Serial.print(F("s · 이 시행 중 ESP 재부팅 "));
  Serial.print(espBoots);
  Serial.println(F("회"));

  if (espBoots) {
    Serial.println(F("[APTEST]   ⚠ 이 시행은 재부팅이 끼었다 — 결과를 차단/비차단 근거로 쓰지 마라"));
  }
  if (r != 1) return false;

  bool gotIp = false;
  wifi.print(F("AT+CIFSR\r\n"));
  readLines("OK", 3000, &gotIp, false);
  if (!gotIp) {
    Serial.println(F("[APTEST]   CWJAP 는 OK 인데 **IP 가 없다**(0.0.0.0) — 결합 미성립으로 본다"));
    return false;
  }
  Serial.print(F("[APTEST]   ★ IP 확보 "));
  Serial.println(ipBuf);
  return true;
}

// 네 조합 각각의 결론을 **미리** 박아 둔다
static void verdict(bool aOk, bool bTried, bool bOk) {
  Serial.println(F("[APTEST] ═══════════════════════════════════════════════"));
  Serial.print(F("[APTEST] ★★ 판정: A(원래 MAC) "));
  Serial.print(aOk ? F("성공") : F("실패"));
  if (bTried) { Serial.print(F(" · B(새 MAC) ")); Serial.print(bOk ? F("성공") : F("실패")); }
  else        { Serial.print(F(" · B 미시행")); }
  Serial.println();

  if (aOk) {
    Serial.println(F("[APTEST]   → 지금은 붙는다. 차단이 이미 풀렸거나 애초에 없었다."));
    Serial.println(F("[APTEST]     차단이었다면 '언제 풀리는가'가 다음 질문이다."));
  } else if (bOk) {
    Serial.println(F("[APTEST]   → **AP 차단 확정.** 같은 보드·같은 전원으로 붙었다."));
    Serial.println(F("[APTEST]     즉 **전원·하드웨어는 무죄**다. 라우터의 차단/레이트제한을 봐라."));
  } else {
    Serial.println(F("[APTEST]   → **차단이 아니다.** MAC 을 바꿔도 못 붙었다."));
    Serial.println(F("[APTEST]     남은 것은 **전원/RF**다. 다음 순서는 전원 분리 실험."));
  }
  Serial.print(F("[APTEST]   (전 구간 ESP 재부팅 합계 "));
  Serial.print(espBootsTotal);
  Serial.print(F("회 · MCU 부팅 #"));
  Serial.print(bootCount);
  Serial.println(F(")"));
  if (espBootsTotal >= 2) {
    Serial.println(F("[APTEST]   ⚠ 아무 자극 없이 ESP 가 여러 번 재부팅했다 — AP 는 칩을 재부팅시킬 수"));
    Serial.println(F("[APTEST]     없다. 위 판정과 **별개로** 전원/접점 문제가 따로 있다는 뜻이다."));
  }
  Serial.println(F("[APTEST] ═══════════════════════════════════════════════"));
}

void setup() {
  Serial.begin(115200);
  wifi.begin(9600);
  delay(300);

  if (bootMagic != BOOT_MAGIC) { bootMagic = BOOT_MAGIC; bootCount = 0; }  // 콜드 부팅
  if (bootCount < 65535) bootCount++;

  Serial.println(F("\n[APTEST] 2판 — MAC A/B 로 차단 가설을 가른다 (REQ-0076)"));
  Serial.print(F("[BOOT] MCU 부팅 #"));
  Serial.print(bootCount);
  Serial.print(F(" · 리셋 원인: "));
  if (mcusrMirror == 0) {
    Serial.println(F("불명(부트로더가 MCUSR 을 지웠다)"));
  } else {
    if (mcusrMirror & _BV(PORF))  Serial.print(F("전원인가(POR) "));
    if (mcusrMirror & _BV(EXTRF)) Serial.print(F("외부리셋(버튼/DTR) "));
    if (mcusrMirror & _BV(BORF))  Serial.print(F("**브라운아웃(전원부족)** "));
    if (mcusrMirror & _BV(WDRF))  Serial.print(F("워치독 "));
    Serial.println();
  }
  if (bootCount > 1) {
    Serial.println(F("[BOOT] ⚠ #2 이상이면 **MCU 가 시험 도중 재시작한 것**이다(결합 소실이 아니다)"));
  }

  Serial.println(F("[APTEST] 1) AT+RST"));
  wifi.print(F("AT+RST\r\n"));
  readLines("Ready", 6000, NULL, false);
  espBoots = 0; espBootsTotal = 0;          // 우리가 시킨 리셋은 자발 재부팅이 아니다

  Serial.println(F("[APTEST] 2) AT+CWMODE=1"));
  wifi.print(F("AT+CWMODE=1\r\n"));
  readLines("OK", 3000, NULL, false);

  // ── 3) 원래 MAC 을 읽는다 (사용자가 라우터 차단목록에서 찾아볼 값이다) ──
  Serial.println(F("[APTEST] 3) 원래 MAC 조회"));
  wifi.print(F("AT+CIPSTAMAC?\r\n"));
  if (readLines("OK", 3000, NULL, true) != 1) {
    wifi.print(F("AT+CIPSTAMAC_CUR?\r\n"));    // 신형 변종으로 한 번 더
    if (readLines("OK", 3000, NULL, true) == 1) macCmdCur = true;
  }
  Serial.print(F("[APTEST] ★ 원래 MAC  "));
  Serial.println(macBuf);
  Serial.println(F("[APTEST]   ↑ 이 값을 라우터 차단목록/접속기기 목록에서 찾아봐라"));

  // ── 4) A 시행 ──
  bool aOk = tryJoin(F("A(원래 MAC)"));
  if (aOk) { verdict(true, false, false); assocAt = millis(); lastProbe = assocAt; monitoring = true; return; }

  // ── 5) B 시행 — MAC 을 바꾼다 ──
  Serial.println(F("[APTEST] ── B 준비: MAC 을 " FAKE_MAC " 로 바꾼다 ──"));
  bool macSet = false;
  wifi.print(F("AT+CIPSTAMAC_CUR=\"" FAKE_MAC "\"\r\n"));   // 휘발성 우선(플래시를 안 건드린다)
  if (readLines("OK", 3000, NULL, false) == 1) {
    macSet = true;
    Serial.println(F("[APTEST]   AT+CIPSTAMAC_CUR 이 먹었다 (휘발성 — 전원을 빼면 원래대로 돌아온다)"));
  } else {
    wifi.print(F("AT+CIPSTAMAC=\"" FAKE_MAC "\"\r\n"));
    if (readLines("OK", 3000, NULL, false) == 1) {
      macSet = true;
      Serial.println(F("[APTEST]   AT+CIPSTAMAC 이 먹었다"));
      Serial.println(F("[APTEST]   ⚠ 이 변종은 **플래시에 남을 수 있다.** 원래대로 돌리려면 위의"));
      Serial.println(F("[APTEST]     '원래 MAC' 값으로 AT+CIPSTAMAC=\"...\" 를 한 번 실행해라"));
    }
  }
  if (!macSet) {
    Serial.println(F("[APTEST] ✗ MAC 변경 명령이 둘 다 안 먹는다(구형 펌웨어 미지원)."));
    Serial.println(F("[APTEST]   → 이 펌웨어로는 MAC A/B 를 할 수 없다. 차단 가설은 다른 방법으로 갈라야 한다"));
    Serial.println(F("[APTEST]   (대안: 라우터에서 이 MAC 을 명시적으로 허용하거나, 다른 AP/폰 핫스팟에 붙여 본다)"));
    verdict(false, false, false);
    stopped = true;
    return;
  }

  // MAC 변경은 재부팅해야 먹는 펌웨어가 있다. 한 번 걸고 그 사실을 남긴다.
  Serial.println(F("[APTEST]   MAC 적용을 위해 AT+RST 를 한 번 더 건다"));
  wifi.print(F("AT+RST\r\n"));
  readLines("Ready", 6000, NULL, false);
  wifi.print(F("AT+CWMODE=1\r\n"));
  readLines("OK", 3000, NULL, false);
  espBootsTotal = 0;                         // 우리가 시킨 리셋은 빼고 다시 센다

  wifi.print(F("AT+CIPSTAMAC?\r\n"));        // 정말 바뀌었는지 확인하고 찍는다
  readLines("OK", 3000, NULL, true);
  Serial.print(F("[APTEST] 지금 MAC  "));
  Serial.println(macBuf);

  bool bOk = tryJoin(F("B(새 MAC)"));
  verdict(false, true, bOk);

  if (bOk) {
    // ★ 붙었으면 거기서 멈추지 않는다 — **12초를 넘겨 유지되는가**까지 한 판에서 본다
    Serial.println(F("[APTEST] B 성공 → 이제 결합이 유지되는지 5초마다 본다"));
    assocAt = millis(); lastProbe = assocAt; monitoring = true;
  } else {
    stopped = true;
  }
}

void loop() {
  if (stopped || !monitoring) {
    // 멈춘 뒤에도 **모듈이 스스로 말하는 것은 계속 받아 적는다.**
    // 1판에서 이 수동 관찰이 "아무 자극 없이 ESP 가 4번 재부팅" 을 잡아냈다. 자극이 아니라 관찰이다.
    readLines(NULL, 200, NULL, false);
    return;
  }

  if (millis() - lastProbe < PROBE_MS) {
    readLines(NULL, 50, NULL, false);
    if (sawDisconnect) {
      Serial.print(F("[T+"));
      Serial.print((millis() - assocAt) / 1000UL);
      Serial.println(F("s] ★ 결합 소실 (WIFI DISCONNECT 통보)"));
      monitoring = false; stopped = true;
    }
    return;
  }
  lastProbe = millis();

  bool gotIp = false;
  wifi.print(F("AT+CIFSR\r\n"));
  readLines("OK", 2500, &gotIp, false);

  unsigned long held = (millis() - assocAt) / 1000UL;
  Serial.print(F("[T+"));
  Serial.print(held);
  if (gotIp) {
    Serial.print(F("s] 결합유지 · IP "));
    Serial.print(ipBuf);
    if (espBootsTotal) { Serial.print(F(" · ESP재부팅 ")); Serial.print(espBootsTotal); Serial.print(F("회")); }
    Serial.println();
  } else {
    Serial.print(F("s] ★ 결합 소실 — 유지시간 "));
    Serial.print(held);
    Serial.println(F("s"));
    Serial.println(F("[APTEST]   (확인 주기 5초라 실제 소실은 -5초 이내. 12초 근처면 전원/AP, 몇 분 이상이면 다른 원인)"));
    monitoring = false; stopped = true;
  }
}
