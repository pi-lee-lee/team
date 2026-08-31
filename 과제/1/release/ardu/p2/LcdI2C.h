#pragma once
// LcdI2C.h — PCF8574 백팩을 단 HD44780 (16x2 등) I2C LCD. **Wire 를 쓰지 않는다.**
//
// 🔓 기여자가 바꾸는 것은 아래 `#ifndef` 블록뿐이다. 이 파일의 함수는 손대지 않아도 된다.
//
// 왜 `Wire` 를 안 쓰나 — 크기다. 이 프레임워크에서 잰 값:
//     Wire 1,126 B + LiquidCrystal_I2C 1,140 B = flash 2,266 B · RAM 정적 241 B
//     이 헤더                                   = flash 약 582 B · RAM 정적 **0 B**
//   RAM 쪽이 결정적이다. `Wire` 의 241 B 는 진단이 아니라 **버퍼**라서 어떤 스위치로도 안 준다.
//
// 🔴 표준 라이브러리와 **API 가 다르다.** 인터넷 예제를 그대로 못 쓴다:
//     lcd.print(x) → 없다 · lcdPrintU32(x) 뿐이다 (부호 없는 정수 전용)
//     lcd.backlight()/clear() → backlight 는 항상 켜짐 · 지우기는 lcdClear()
//     생성자 인자 → 없다. 주소·핀배치·행수는 아래 #define 이다
//
// ⚠ `lcdPrintU32` 는 **이전 내용을 안 지운다.** 7654321 뒤에 12 를 찍으면 화면에 1254321 이 남는다.
//    자릿수가 줄어드는 값을 찍는다면 `lcdPrintU32Pad(v, 폭)` 을 써라(안 쓰면 flash 를 안 먹는다).

#include <Arduino.h>
#include <stdlib.h>                  // ultoa

// ─────────────────────────────────────────────────────────────────────────
// 🔓 기여자 손잡이 — 전부 `#ifndef` 다. **스케치에서 `#define` 하면 그 값이 이긴다.**
//   🔑 기본값을 그대로 쓰면 flash 를 한 바이트도 더 안 쓴다.
// ─────────────────────────────────────────────────────────────────────────
#ifndef LCD_ADDR
#define LCD_ADDR      0x27           // 🔓 PCF8574=0x27 · PCF8574A=**0x3F** (둘 다 흔하다)
#endif
// PCF8574 핀 → HD44780 배치. 아래가 시중 백팩의 가장 흔한 배선이지만 **유일하지 않다.**
#ifndef LCD_BIT_RS
#define LCD_BIT_RS    0x01           // P0
#endif
#ifndef LCD_BIT_EN
#define LCD_BIT_EN    0x04           // P2
#endif
#ifndef LCD_BIT_BL
#define LCD_BIT_BL    0x08           // P3 백라이트
#endif
#ifndef LCD_COLS
#define LCD_COLS      16             // 🔓 20x4 면 20
#endif
#ifndef LCD_ROWS
#define LCD_ROWS      2              // 🔓 4행이면 4 — 3·4행 주소가 자동으로 따라온다
#endif
// 🔴 TWI 한 단계(START/주소/데이터)의 상한. 100kHz 에서 한 바이트가 약 90µs 다.
//   ⚠ **이 값을 0 으로 두지 마라** — 그러면 상한이 없던 옛 판으로 돌아간다.
#ifndef LCD_TWI_TIMEOUT_US
#define LCD_TWI_TIMEOUT_US 1000U
#endif
// 🔴 **두 번째 상한 — 시계와 무관한 회전수 백스톱.**
//   왜 둘인가: `micros()` 는 timer0 오버플로 **인터럽트**가 세는 값이다. 인터럽트가 꺼진
//   구간에서 불리면 그 값이 제대로 안 흐른다 → ★ **시간 상한만으로는 종료가 보장되지 않는다.**
//   ⚠ 실제 AVR 은 `TCNT0` 가 계속 돌아 대개 빠져나오지만, **"대개"는 보장이 아니다.**
//   🔑 정상 버스는 약 20 회전에서 끝난다(한 단계 ~90µs) → 2000 은 **100배 여유**다.
#ifndef LCD_TWI_TIMEOUT_SPINS
#define LCD_TWI_TIMEOUT_SPINS 2000U
#endif

// ── 실패 계수 — 🔴 **조용히 실패하지 않기 위한 것이다** ──────────────────
//   `lcdFail` 이 오르는데 화면이 안 나오면 **배선/주소**다. 안 오르는데 안 나오면 **대비/전원**이다.
//   🔑 그 둘을 가르는 것이 이 한 칸이다. 진단 줄에 같이 찍어라.
static uint16_t lcdFail = 0;

// ─────────────────────────────────────────────────────────────────────────
// TWI(하드웨어 I2C) — **상한이 있다**
//   🔴 왜 상한이 필요한가: 버스가 LOW 로 물려 있으면(풀업 없음·단락·클럭 스트레치)
//     `TWINT` 가 영영 안 선다. 상한이 없으면 `setup()` 에서 노드가 **영영 멈춘다** —
//     ★ 링크도 등록도 못 간다. **LCD 를 못 쓰는 것과 노드가 죽는 것은 급이 다르다.**
//   ⚠ 슬레이브가 없는 것만으로는 안 멈춘다 — 그때는 하드웨어가 NACK 로 완료하고 TWINT 를 세운다.
//   ⚠ `delay()` 로 재지 않는다. 그 자리가 블로킹이면 상한을 두는 뜻이 없다.
// ─────────────────────────────────────────────────────────────────────────
static bool twiWait(void) {
  const uint16_t t0 = (uint16_t)micros();
  uint16_t spins = 0;
  while (!(TWCR & _BV(TWINT))) {
    // 🔴 **회전수 상한이 먼저다** — 시계가 안 흐르는 구간에서도 반드시 빠져나온다
    if (++spins > (uint16_t)LCD_TWI_TIMEOUT_SPINS) return false;
    // 🔑 부호 없는 뺄셈이라 micros() 랩어라운드에 안전하다
    if ((uint16_t)((uint16_t)micros() - t0) > (uint16_t)LCD_TWI_TIMEOUT_US) return false;
  }
  return true;
}

static void twiInit(void) { TWSR = 0; TWBR = 72; }   // 100kHz @16MHz

// 한 바이트를 PCF8574 에 민다. 🔴 **실패하면 false 를 돌려주고 즉시 빠진다.**
static bool pcfWrite(uint8_t v) {
  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
  if (!twiWait()) goto fail;
  TWDR = (uint8_t)(LCD_ADDR << 1);
  TWCR = _BV(TWINT) | _BV(TWEN);
  if (!twiWait()) goto fail;
  // 🔴🔴 **ACK 을 봐야 한다.** `twiWait()` 은 *"전송이 끝났나"* 만 본다 — **끝났다 ≠ 받았다.**
  //   슬레이브가 없으면 하드웨어는 **NACK 로 정상 완료**하고 `TWINT` 을 세운다.
  //   ⚠ 그래서 이 줄이 없으면 **주소가 틀려도 `pcfWrite` 가 true 를 낸다** →
  //     `lcdInit()` 이 성공을 보고하고 `init=1 fail=0` 이 찍힌다. **화면만 죽는다.**
  //   ★ 이 검사가 없으면 **주소를 후보에서 아예 빼게 된다** — 진단줄이 초록이기 때문이다.
  //     📖 docs/arduino/LEDGER.md §304
  //   0x18 = SLA+W 에 ACK · 0x20 = NACK(그 주소에 아무도 없다) · 0x38 = 중재 실패
  if ((TWSR & 0xF8) != 0x18) goto fail;
  TWDR = v;
  TWCR = _BV(TWINT) | _BV(TWEN);
  if (!twiWait()) goto fail;
  // 0x28 = 데이터에 ACK · 0x30 = NACK(슬레이브가 더 못 받는다)
  if ((TWSR & 0xF8) != 0x28) goto fail;
  TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);        // STOP 은 기다리지 않는다
  return true;
fail:
  // 🔴 버스를 놓아준다 — 안 놓으면 다음 호출도 같은 자리에서 막힌다
  TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
  if (lcdFail < 65535) lcdFail++;
  return false;
}

// ── HD44780 4비트 ────────────────────────────────────────────────────────
// ⚠ 🔴 **첫 실패에서 통째로 그만둔다.** 안 그러면 버스가 죽었을 때 한 번 출력에
//   (자릿수 × 6 × 상한) 만큼 멈춘다 — `GUIDE.md §5` 의 66ms 예산을 통째로 깨뜨린다.
static bool lcdWrite4(uint8_t v) {          // v = 상위니블 + RS 비트
  const uint8_t b = (uint8_t)(v | LCD_BIT_BL);
  if (!pcfWrite(b)) return false;
  if (!pcfWrite((uint8_t)(b | LCD_BIT_EN))) return false;
  delayMicroseconds(1);                     // EN 펄스 폭(데이터시트 최소 450ns)
  if (!pcfWrite(b)) return false;
  delayMicroseconds(50);                    // 명령 처리 시간
  return true;
}

static bool lcdSend(uint8_t val, uint8_t rs) {
  if (!lcdWrite4((uint8_t)((val & 0xF0) | rs))) return false;
  return lcdWrite4((uint8_t)(((val << 4) & 0xF0) | rs));
}

static bool lcdCmd(uint8_t c) {
  const bool ok = lcdSend(c, 0);
  if (c < 4) delay(2);                      // clear/home 만 1.5ms 이상 걸린다
  return ok;
}
static bool lcdChar(uint8_t c) { return lcdSend(c, LCD_BIT_RS); }

// 🔴 `setup()` 에서 부른다. **실패해도 노드는 계속 돈다** — 그것이 상한을 둔 이유다.
static bool lcdInit(void) {
  twiInit();
  delay(50);                                // 전원 안정
  if (!lcdWrite4(0x30)) return false;  delay(5);
  if (!lcdWrite4(0x30)) return false;  delayMicroseconds(150);
  if (!lcdWrite4(0x30)) return false;
  if (!lcdWrite4(0x20)) return false;       // 4비트 모드로 전환
  if (!lcdCmd((uint8_t)(0x28 | ((LCD_ROWS > 1) ? 0x08 : 0x00)))) return false;  // 4bit/행수/5x8
  if (!lcdCmd(0x08)) return false;          // 표시 끔
  if (!lcdCmd(0x01)) return false;          // 지움
  if (!lcdCmd(0x06)) return false;          // 커서 오른쪽 이동
  return lcdCmd(0x0C);                      // 표시 켬 · 커서 없음
}

static bool lcdClear(void) { return lcdCmd(0x01); }   // ⚠ 약 2ms 블로킹

// 🔑 행 주소는 HD44780 규칙이다: 0행 0x00 · 1행 0x40 · 2행 0x00+cols · 3행 0x40+cols
static bool lcdSetCursor(uint8_t col, uint8_t row) {
#if LCD_ROWS > 2
  static const uint8_t off[4] = { 0x00, 0x40, (uint8_t)LCD_COLS, (uint8_t)(0x40 + LCD_COLS) };
  const uint8_t base = off[row & 0x03];
#else
  const uint8_t base = row ? 0x40 : 0x00;
#endif
  return lcdCmd((uint8_t)(0x80 | (uint8_t)(base + col)));
}

// 🔓 숫자 표시 — 이 드라이버의 유일한 출력 함수다.
//   ⚠ **이전 내용을 안 지운다.** 자릿수가 줄면 옛 숫자가 뒤에 남는다 → `lcdPrintU32Pad` 를 봐라.
static bool lcdPrintU32(uint32_t v) {
  char b[11];
  ultoa(v, b, 10);
  for (char* q = b; *q; q++) if (!lcdChar((uint8_t)*q)) return false;
  return true;
}

// 🔓 문자열 — **안 쓰면 링커가 통째로 뺀다(flash 0 B).**
//   ⚠ `F()`(PROGMEM) 는 안 받는다. 짧은 라벨용이다 — 긴 문자열은 RAM 을 먹는다.
static bool lcdPrintStr(const char* s) {
  for (; *s; s++) if (!lcdChar((uint8_t)*s)) return false;
  return true;
}

// 🔓 폭을 고정해 찍는다(앞을 공백으로 채운다) — **안 쓰면 링커가 통째로 뺀다(flash 0 B).**
//   🔑 값이 7654321 → 12 로 줄어도 잔상이 안 남는다.
static bool lcdPrintU32Pad(uint32_t v, uint8_t w) {
  char b[11];
  ultoa(v, b, 10);
  uint8_t n = (uint8_t)strlen(b);
  for (; n < w; n++) if (!lcdChar(' ')) return false;
  for (char* q = b; *q; q++) if (!lcdChar((uint8_t)*q)) return false;
  return true;
}
