#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Checksum.h — 체크섬(§2.2) — 순수 함수
//   `client.ino` 의 **319~357 행을 원문 그대로** 옮긴 것이다 (REQ-0275 A단계 · 2026-08-19).
//   ⚠ 행 번호는 **이 이동 이전 판(1,799줄) 기준**이다.
//   ⚙ 응용 성질
//     슬롯·프레임 로직. 잠금 대상이 아니다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라. 순서 보존이 `hex` 차이 0 의 조건이다.**
//   `client.ino` 의 **원래 그 자리에서** `#include` 된다. 자리를 바꾸거나 헤더 순서를 바꾸면
//   **그 순간 거동 변경이 되고 굽기 축이 하나 는다**(원장 §19·§31).

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

