// cipsize — **이 ESP 가 `AT+CIPSEND` 를 몇 바이트까지 받아 주는가**를 재는 최소 프로브
//
// 왜 있나 (2026-08-17):
//   상행 배치(슬롯당 1거래에 여러 줄)를 설계하는데 **크기 상한을 아무도 모른다.**
//   지금 `sendLine` 은 한 줄 64바이트가 상한이고 **그 위로 올라가 본 적이 없다.**
//   "200~300B 로 간다"를 가정으로 깔면 그 위에 세운 설계 전체가 가정 위에 선다.
//
// 🔴 "깨진다"에는 셋이 있고 **셋째가 진짜다**:
//     ① `>` 가 안 온다                          → 여기서 보인다
//     ② `SEND OK` 가 안 온다                     → 여기서 보인다
//     ③ **둘 다 오는데 서버가 받은 바이트가 더 적다**  → 🔴 **여기서는 안 보인다**
//   ③ 은 일부 AT 빌드의 알려진 동작이고, **장치만 보면 조용한 잘림이 PASS 로 읽힌다.**
//   → 그래서 **모든 프레임에 길이와 체크섬을 실어 보낸다.** 판정은 **서버가 받은 바이트와 대조**해야
//     끝난다. 이 스케치 혼자서는 **①②만** 답한다. **③은 socket 이 같은 회차에서 대조한다.**
//
// ⚠ 최소로 만들었다 — 게이트·큐·상태머신 없음. 재는 것 하나에만 변수를 준다.

#include <SoftwareSerial.h>

static const uint8_t PIN_ESP_RX = 8;   // ESP TX → Uno
static const uint8_t PIN_ESP_TX = 7;   // Uno → ESP RX
SoftwareSerial wifi(PIN_ESP_RX, PIN_ESP_TX);

#define WIFI_SSID   "SK_WiFiGIGA50DC_2.4G"
#define WIFI_PASS   "2011050796"
#define SERVER_IP   "192.168.35.81"
#define SERVER_PORT "10500"                 // socket 시험 인스턴스

// 재 볼 크기. **작은 것부터 올라간다** — 큰 것부터 하면 어디서 깨졌는지 못 가른다.
//
// 🔴 대조군 둘을 일부러 넣었다. 없으면 판정이 안 선다:
//   · **맨 앞 40B** — 지금 확실히 되는 크기. 이것부터 실패하면 **크기 문제가 아니라 설정 문제**다
//   · **맨 뒤 40B** — 큰 것을 시도한 *뒤에* 다시 확인한다.
//     ⚠ **이게 없으면 "300 에서 실패"와 "그 전에 링크가 죽었다"를 구별할 수 없다.**
//     오늘 우리가 여러 번 밟은 형태다 — 분모를 확인하지 않고 분자만 읽는 것.
static const uint16_t SIZES[] = { 40, 64, 128, 200, 300, 40 };
static const uint8_t  NSIZES  = sizeof(SIZES) / sizeof(SIZES[0]);
static const uint8_t  REPEAT  = 3;          // 크기마다 3회 — 1회는 우연과 못 가른다
static uint16_t       probeId = 0;          // socket 과 짝지을 상관 id

static char line[320];

static bool waitFor(const char* needle, uint16_t ms) {
  const uint32_t t0 = millis();
  uint8_t m = 0;
  const uint8_t n = (uint8_t)strlen(needle);
  while ((uint32_t)(millis() - t0) < ms) {
    while (wifi.available()) {
      const char c = (char)wifi.read();
      Serial.write(c);                       // 원문을 그대로 흘린다 — 판독은 사람이 한다
      m = (c == needle[m]) ? (uint8_t)(m + 1) : (uint8_t)(c == needle[0] ? 1 : 0);
      if (m == n) return true;
    }
  }
  return false;
}

static void at(const char* cmd, const char* okNeedle, uint16_t ms) {
  Serial.print(F("\n>> ")); Serial.println(cmd);
  wifi.print(cmd); wifi.print(F("\r\n"));
  Serial.println(waitFor(okNeedle, ms) ? F("   [ok]") : F("   [timeout]"));
}

// 한 번 시도한다. **길이·순번·체크섬을 실어** 서버가 받은 것과 대조할 수 있게 한다.
static void probe(uint16_t size, uint8_t rep) {
  // 본문: "Z,<id>,<size>,<rep>,AAAA...." 를 정확히 size 바이트로 채운다(마지막은 LF 가 아니다)
  // 🔑 `id` 와 `size` 가 **줄 맨 앞**에 있는 것이 이 시험의 핵심이다 —
  //   ③(조용한 잘림)이면 뒤쪽이 사라지지만 **머리는 남는다.**
  //   그래서 서버는 잘린 줄에서도 **"몇 바이트라고 주장했는가"** 를 읽어 실제 `rx` 와 대조할 수 있다.
  //   ⚠ 체크섬은 꼬리라 잘리면 사라진다. **그건 무결성 확인용이지 ③ 판정용이 아니다.**
  const uint16_t id = ++probeId;
  int h = snprintf(line, sizeof(line), "Z,%u,%u,%u,", (unsigned)id, (unsigned)size, (unsigned)rep);
  if (h <= 0 || (uint16_t)h + 3 > size) return;
  uint8_t sum = 0;
  for (uint16_t i = (uint16_t)h; i < size - 3; i++) line[i] = (char)('A' + (i % 26));
  for (uint16_t i = 0; i < size - 3; i++) sum = (uint8_t)(sum ^ (uint8_t)line[i]);
  line[size - 3] = ',';
  line[size - 2] = "0123456789ABCDEF"[(sum >> 4) & 0x0F];
  line[size - 1] = "0123456789ABCDEF"[sum & 0x0F];
  line[size]     = 0;

  Serial.print(F("\n=== size=")); Serial.print(size);
  Serial.print(F(" rep="));       Serial.println(rep);

  wifi.print(F("AT+CIPSEND="));
  wifi.print((unsigned)(size + 1));          // +1 = LF
  wifi.print(F("\r\n"));

  const uint32_t tPrompt = millis();
  const bool gotPrompt = waitFor(">", 800);           // ① 프롬프트가 오나
  uint32_t promptMs = millis() - tPrompt;
  bool ok = false; uint32_t okMs = 0;

  if (gotPrompt) {
    wifi.write((const uint8_t*)line, size);
    wifi.write('\n');
    const uint32_t tOk = millis();
    ok = waitFor("SEND OK", 8000);                    // ② SEND OK 가 오나
    okMs = millis() - tOk;
  }

  // 🔑 **기계가 읽을 수 있는 한 줄** — socket 이 `id` 로 자기 `rx` 와 짝짓는다.
  //   ⚠ **둘 중 하나만으로는 ③ 을 판별할 수 없다.** `SEND OK=yes` 인데 `rx < sent` 인 조합만이 ③ 이다.
  Serial.print(F("\n[CIPSIZE] id="));  Serial.print(id);
  Serial.print(F(" size="));           Serial.print(size);
  Serial.print(F(" rep="));            Serial.print(rep);
  Serial.print(F(" sent="));           Serial.print(gotPrompt ? size + 1 : 0);   // LF 포함
  Serial.print(F(" prompt="));         Serial.print(gotPrompt ? F("y") : F("n"));
  Serial.print(F(" promptms="));       Serial.print(promptMs);
  Serial.print(F(" sendok="));         Serial.print(ok ? F("y") : F("n"));
  Serial.print(F(" okms="));           Serial.println(okMs);
  Serial.println(F("   ⚠ 이 줄만으로는 ③(조용한 잘림)을 못 본다 — 서버의 rx 와 같은 id 로 대조해야 끝난다"));
}

void setup() {
  Serial.begin(115200);
  wifi.begin(9600);
  Serial.println(F("\n\n=== cipsize — AT+CIPSEND 길이 상한 프로브 ==="));
  Serial.println(F("⚠ 판정은 서버 대조까지다. 이 출력만으로 PASS 라고 쓰지 마라."));

  at("AT+RST", "ready", 6000);
  delay(1500);
  at("AT+CWMODE=1", "OK", 3000);
  wifi.print(F("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n"));
  Serial.println(F("\n>> AT+CWJAP"));
  Serial.println(waitFor("OK", 30000) ? F("   [ok]") : F("   [timeout]"));
  at("AT+CIPMUX=0", "OK", 3000);
  at("AT+CIPSTART=\"TCP\",\"" SERVER_IP "\"," SERVER_PORT, "CONNECT", 8000);

  for (uint8_t s = 0; s < NSIZES; s++)
    for (uint8_t r = 1; r <= REPEAT; r++) { probe(SIZES[s], r); delay(1200); }

  Serial.println(F("\n=== 끝. socket 에 서버측 수신 바이트 대조를 요청하라 ==="));
}

void loop() {
  while (wifi.available()) Serial.write((char)wifi.read());   // 뒤늦게 오는 것도 흘린다
}
