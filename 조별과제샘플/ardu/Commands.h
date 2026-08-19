#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Commands.h — R / C / T 프레임 처리
//   `client.ino` 의 **1333~1525 행을 원문 그대로** 옮긴 것이다 (REQ-0275 A단계 · 2026-08-19).
//   ⚠ 행 번호는 **이 이동 이전 판(1,799줄) 기준**이다.
//   ⚙ 응용 성질
//     슬롯·프레임 로직. 잠금 대상이 아니다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라. 순서 보존이 `hex` 차이 0 의 조건이다.**
//   `client.ino` 의 **원래 그 자리에서** `#include` 된다. 자리를 바꾸거나 헤더 순서를 바꾸면
//   **그 순간 거동 변경이 되고 굽기 축이 하나 는다**(원장 §19·§31).

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

// ─────────────────────────────────────────────────────────────────────────
// T — 테스트 모드 제어 (§2.4 / §12A, 개정 3). 필드: T,rid,top,slot,tval,cksum
//   top=A 무장 · D 해제(전 칸 소멸) · S 주입 · X 그 칸만 해제
// 결과를 (s0,s1,result) 로 돌려준다. 호출자가 ACK 를 만들고 멱등 캐시에 넣는다.
// ─────────────────────────────────────────────────────────────────────────
static void processTest(char* f[], char* s0, char* s1, uint8_t* result) {
  *s0 = '?';                        // A/D 의 ACK 는 slot 이 ?? 다. 오류일 때도 ?? 다
  *s1 = '?';

  char top = f[2][0];
  if (f[2][1] != '\0') { *result = 3; return; }          // top 은 한 글자

  if (top == 'A' || top == 'D') {
    if (top == 'A') {
      testArmed = true;
    } else {
      // §12A.2 "현실로 복귀"는 하나의 동작이어야 한다 — 칸마다 따로 풀게 만들지 않는다
      testArmed = false;
      slotOverrideClearAll();
    }
    // ⚠ 무장/해제는 시뮬레이터에 아무 영향이 없다(§12B.3). 시뮬은 자율 전진을 하지 않으므로
    //   멈출 것도 재개할 것도 없다. REQ-0043 의 "무장 중 시뮬 정지"는 여기서 사라졌다.
    *result = 0;
    return;
  }

  if (top != 'S' && top != 'X') { *result = 3; return; }  // 모르는 top

  // 여기부터 S / X — 자리 ID 가 필요하다
  const char* slotTok = f[3];
  const char* tval    = f[4];
  uint8_t idx = (strlen(slotTok) == 2) ? slotIndexOf(slotTok[0], slotTok[1]) : 0xFF;
  if (idx == 0xFF) { *result = 3; return; }               // slot 은 ?? 로 남는다

  // 값 검사를 무장 검사보다 **먼저** 한다. 깨진 프레임은 장치 상태와 무관하게 깨진 프레임이다.
  if (top == 'S' && ((tval[0] != '0' && tval[0] != '1') || tval[1] != '\0')) {
    *result = 3;                                          // slot 은 ?? 로 남는다 (§2.4 result=3 규칙)
    return;
  }

  // 프레임이 성립했으므로 이제 ACK 에 그 자리를 담는다
  *s0 = slotCol(idx);
  *s1 = slotRow(idx);

  // §12A.2 무장하지 않은 채 S/X 가 오면 조용히 무시하지 않고 result=4 로 거절한다
  if (!testArmed) { *result = 4; return; }

  if (top == 'S') slotOverrideSet(idx, (uint8_t)(tval[0] - '0'));
  else            slotOverrideClear(idx);
  *result = 0;
}

static void processCommand(char* cand) {
  ramProbe();                     // 수신 경로의 가장 깊은 지점 — 여기서 재는 것이 의미가 있다
  char*   f[7];
  uint8_t nf = splitFields(cand, f, 7);
  if (nf == 0xFF) return;

  char type = f[0][0];
  //  R,rid,slot,userid,cksum (5) / C,rid,slot,cksum (4)
  //  T,rid,top,slot,tval,cksum (6) / M,rid,cksum (3)
  uint8_t want;
  if      (type == 'R') want = 5;
  else if (type == 'T') want = 6;
  else if (type == 'M') want = 3;
  else                  want = 4;

  uint16_t rid;
  if (nf < 3 || !parseU16(f[1], &rid)) return;   // rid 를 모르면 ACK 를 만들 수 없다 → 버린다

  // §4.2 멱등: 이미 본 rid 면 상태를 다시 바꾸지 말고 같은 ACK 를 다시 보낸다
  int8_t hit = ackQ.find(rid);
  if (hit >= 0) {
#if DEBUG
    Serial.print(F("[DUP rid] ")); Serial.println(rid);
#endif
    sendAck(ackQ.at(hit).rid, ackQ.at(hit).slot[0], ackQ.at(hit).slot[1], ackQ.at(hit).result);
    return;
  }

  uint8_t result;
  char s0, s1;

  if (nf != want) {
    // 필드 개수가 안 맞으면 해석 불가 — 타입과 무관하게 result=3
    s0 = '?'; s1 = '?'; result = 3;
#if DEBUG
    Serial.print(F("[BAD FIELDS] rid=")); Serial.println(rid);
#endif
    commitAck(rid, s0, s1, result);
    return;
  }

  if (type == 'T') {
    processTest(f, &s0, &s1, &result);
    commitAck(rid, s0, s1, result);      // §4.2 멱등은 T 에도 그대로 적용된다
    return;
  }

  if (type == 'M') {
    // §12B.4 시뮬 한 걸음. **무장 여부로 막지 않는다** — 테스트 모드와 별개다(§12B.3).
    // 멱등이 특히 중요하다: 재전송이 새 걸음으로 처리되면 한 번 눌렀는데 두 칸이 바뀐다.
    // (위쪽 `ackQ.find()` 가 이미 걸러 준다 — M 도 R/C/T 와 같은 기계장치를 탄다.)
    uint8_t idx = simStep();
    if (idx == 0xFF) {
      s0 = '?'; s1 = '?'; result = 5;    // 바꿀 시뮬 칸이 없다
#if DEBUG
      Serial.println(F("[SIM] 바꿀 시뮬 칸이 없다 → result=5"));
#endif
    } else {
      s0 = slotCol(idx); s1 = slotRow(idx); result = 0;
#if DEBUG
      Serial.print(F("[SIM] 한 걸음: ")); Serial.print(s0); Serial.print(s1);
      Serial.print(F(" → occupied="));
      Serial.println((simOcc >> idx) & 1);
#endif
    }
    commitAck(rid, s0, s1, result);
    return;
  }

  // ── 여기부터 R / C ──
  const char* slotTok = f[2];
  uint8_t idx = (strlen(slotTok) == 2) ? slotIndexOf(slotTok[0], slotTok[1]) : 0xFF;

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
        // ★ occupied=1,reserved=1 경로(§1.1 마지막 행)는 이제 여기서 만들지 않는다.
        //   예전에는 예약이 잡히면 몇 초 뒤 시뮬이 그 칸에 차를 넣었다(ARRIVE_*).
        //   자율 전진이 없어졌으므로 §12B.2 의 **"예약된 빈칸 우선"** 규칙이 그 일을 한다 —
        //   다음 시뮬 트리거가 이 칸을 가장 먼저 채운다. simStep() 1순위가 그것이다.
      }
    } else {                                            // 'C' — 취소
      resMask &= (uint16_t)~bit;                        // 예약을 끄는 유일한 경로 (§7.4)
      result = 0;
    }
  }

  commitAck(rid, s0, s1, result);
}

#include "EspLink_at.h"   // ← AT 응답 어휘 해석 (REQ-0273). **위치를 옮기지 마라**

