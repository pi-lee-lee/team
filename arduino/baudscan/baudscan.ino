/*
 * baudscan.ino — ESP-01 의 UART 보율을 찾아내는 **일회성 진단 스케치**
 *
 * 왜 만들었나 (REQ-0064 중 발생):
 *   client.ino 가 멀쩡히 동작하다가 갑자기 ESP 응답을 한 줄도 못 읽게 됐다.
 *   증상: `rx` 는 계속 증가하는데 `lines=0`, `[AT]` 출력 0, `[DROP-OVF]` 도 0.
 *   → 971 바이트를 받는 동안 **개행(LF)이 단 하나도 없었다**는 뜻이다.
 *
 *   결정적 단서: **재부팅해도 바이트 수가 정확히 같았다**(3초 38B → 6초 113B → 9초 121B).
 *   잡음이면 매번 달라야 한다. 같다는 것은 **ESP 가 결정적으로 같은 바이트를 보내고 있고
 *   우리가 그것을 잘못 디코딩**하고 있다는 뜻이다 — 보율 불일치의 전형적인 모습이다.
 *
 *   의심 경위: ESP-01 은 UART 설정을 플래시에 저장한다. 이 모듈은 누군가 9600 으로 맞춰 둔
 *   물건이었다(공장 기본은 115200). CWJAP 가 자격증명을 플래시에 쓰는 도중 AT+RST 가
 *   반복해서 들어가면 설정 섹터가 깨질 수 있고, 그러면 공장값으로 되돌아간다.
 *
 * 이 스케치가 가르는 것 — **두 갈래를 한 번에 판정한다:**
 *   · 어떤 보율에서 읽을 수 있는 텍스트가 나온다  → **보율 문제.** 그 값을 client.ino 에 반영하면 된다
 *   · 어느 보율에서도 안 나온다                   → **배선 문제.** 사람이 하드웨어를 봐야 한다
 *
 * ⚠ 이것은 진단용이며 운용 스케치가 아니다. 확인이 끝나면 client.ino 를 다시 굽는다.
 *
 * 타깃 : Arduino Uno (FQBN arduino:avr:uno)
 * 배선 : client.ino 와 동일 — D8 = ESP TX → Uno / D7 = Uno → ESP RX
 * 출력 : USB 시리얼 115200
 */

#include <SoftwareSerial.h>

static const uint8_t PIN_ESP_RX = 8;   // ESP TX → Uno   (client.ino 와 반드시 같아야 한다)
static const uint8_t PIN_ESP_TX = 7;   // Uno → ESP RX

SoftwareSerial wifi(PIN_ESP_RX, PIN_ESP_TX);

// 시험할 보율. **115200 은 넣지 않았다** — 16MHz AVR 의 SoftwareSerial 로는 신뢰성 있게
// 수신할 수 없다(비트뱅잉 한계). 만약 ESP 가 공장값 115200 으로 돌아간 것이라면 이 스캔은
// 전부 실패할 것이고, 그 "전부 실패" 자체가 중요한 정보다 — 아래 판정문에 그렇게 적어 뒀다.
static const long BAUDS[] = { 9600, 19200, 38400, 57600, 4800, 2400 };
static const uint8_t BAUD_N = sizeof(BAUDS) / sizeof(BAUDS[0]);

static const char HEXD[] = "0123456789ABCDEF";

// 한 보율에서 1.5초 동안 받아 본다. 인쇄 가능 문자 수·개행 수·"OK" 포함 여부를 센다.
static void probe(long baud) {
  wifi.end();
  wifi.begin(baud);
  wifi.listen();

  // 버퍼에 남은 쓰레기를 비운다
  unsigned long t0 = millis();
  while (millis() - t0 < 150) { while (wifi.available()) wifi.read(); }

  wifi.print(F("AT\r\n"));

  char     buf[96];
  uint8_t  n = 0;
  uint16_t total = 0, printable = 0, newlines = 0;

  t0 = millis();
  while (millis() - t0 < 1500) {
    while (wifi.available()) {
      int c = wifi.read();
      if (c < 0) continue;
      total++;
      if (c == '\n') newlines++;
      if ((c >= 32 && c <= 126) || c == '\r' || c == '\n') printable++;
      if (n < sizeof(buf) - 1) buf[n++] = (char)c;
    }
  }
  buf[n] = '\0';

  Serial.print(F("[BAUD "));
  Serial.print(baud);
  Serial.print(F("] 바이트="));
  Serial.print(total);
  Serial.print(F(" 인쇄가능="));
  Serial.print(printable);
  Serial.print(F(" 개행="));
  Serial.print(newlines);

  // "OK" 가 보이면 그 보율이 정답이다
  bool ok = (strstr(buf, "OK") != NULL);
  Serial.print(ok ? F("  ★★ OK 발견 — 이 보율이 정답이다") : F(""));
  Serial.println();

  // 받은 것을 그대로 보여 준다. 눈으로 확인할 수 있어야 판정을 믿는다.
  Serial.print(F("        raw: \""));
  for (uint8_t i = 0; i < n; i++) {
    char c = buf[i];
    if (c >= 32 && c <= 126) {
      Serial.print(c);
    } else {
      Serial.print(F("\\x"));
      Serial.print(HEXD[((uint8_t)c) >> 4]);
      Serial.print(HEXD[((uint8_t)c) & 0x0F]);
    }
  }
  Serial.println(F("\""));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\n===== ESP-01 보율 스캔 (진단) ====="));
  Serial.println(F("각 보율에서 AT 를 한 번 쏘고 1.5초 동안 받는다."));
  Serial.println(F("판정: 'OK 발견' 이 뜨는 보율이 정답. 아무 데서도 안 뜨면 배선을 봐야 한다.\n"));
}

void loop() {
  for (uint8_t i = 0; i < BAUD_N; i++) probe(BAUDS[i]);

  Serial.println(F("\n----- 한 바퀴 끝. 판정 -----"));
  Serial.println(F("· 특정 보율에서만 인쇄가능 비율이 높고 OK 가 보였다 → 그 값으로 client.ino 를 고친다"));
  Serial.println(F("· 전부 깨져 보이고 개행이 0 이다 → 둘 중 하나다:"));
  Serial.println(F("    (a) ESP 가 공장값 115200 으로 돌아갔다 (SoftwareSerial 로는 못 읽는다)"));
  Serial.println(F("        → USB-TTL 로 115200 접속해 AT+UART_DEF=9600,8,1,0,0 을 넣어야 한다"));
  Serial.println(F("    (b) 배선/전압 문제 — D7·D8·GND·3.3V 를 봐야 한다"));
  Serial.println(F("· 바이트가 아예 0 이다 → ESP TX→D8 경로가 끊겼다\n"));

  delay(4000);
}
