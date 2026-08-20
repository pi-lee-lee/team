#pragma once
// ═════════════════════════════════════════════════════════════════════════
// KSweep.h — 🔬 **센서 부하 측정 판** (측정용 임시 코드)
//   ⚙ 응용 성질 — 잠금 대상이 아니다
//
// 🔴 **평소 빌드에는 아무 영향이 없다.** `KSWEEP` 이 정의되지 않으면 이 파일은 통째로 빈다.
//     켜기 : `--build-property "compiler.cpp.extra_flags=-DKSWEEP=1 ..."`
//
// 🔑 **왜 별도 파일인가**: `client.ino` 는 **기여자가 여는 파일**이다.
//   훅 11개와 순환 코드가 거기 있으면 *"이것도 배워야 하나"* 로 읽힌다.
//   측정 코드는 측정 파일에 둔다.
//
// ── 무엇을 재나 ───────────────────────────────────────────────────────────
//   사용자 물음: *"리턴값까지 포함한 센서를 **최대 몇 개**까지 **지연 얼마**를 소모해서
//   설치할 수 있는가"*
//
//   🔑 **지연의 정본은 `up / slot`**(실제 슬롯 주기, 초)이다. `[CNT]` 에 이미 있다.
//     이상값 **1.200** — 두 판에서 검산했다(120/100 · 180/150).
//     ⚠ 벽시계로 안 잰다 — 로그 초 해상도(1초)가 슬롯 1.2초와 안 맞는다.
//       `up`·`slot` 은 **장치 내부 값**이라 그 문제가 원리적으로 없다.
//   보조 축 : `smiss`(송신 창 놓침) · `oow` · 🔴 `ssovf`(링버퍼 넘침 = 부하의 직접 증거) · `ramLow`
//
// ── 🔑 왜 핀을 안 물려도 최악을 재나 ─────────────────────────────────────
//   `pulseIn` 이 반사를 못 받으면 **타임아웃(6.96ms)을 전부 먹는다** = **최악 지연**이다.
//   물체가 있으면 그보다 **짧다**(가까울수록 짧다).
//   ⚠ §"모의는 실기보다 더 해 주면 안 된다" 에 **안 걸린다** — 더 해 주는 것이 아니라
//     **최악을 그대로 밟는다.** 그리고 지금 실물 B1 이 93.8% 를 못 재고 있어 **같은 상태**다.
// ═════════════════════════════════════════════════════════════════════════

#ifdef KSWEEP

// 🔑 **본체를 하나로 두고 얇은 래퍼만 만든다.** 매크로로 본문을 11번 펼치면
//   flash 가 **31,858B(98%)** 까지 갔다. 공유형은 **29,836B(92%)** 다 — **−2,022B**.
//   ⚠ 대가는 RAM 배열이다(아래). flash 가 훨씬 비쌌으므로 이 교환이 맞다.
// 🔴 `int16_t`·`uint16_t` 를 쓴다 — 값은 0~4095 이고 게이트 차분은 200ms 라 16비트로 충분하다.
//   `long`/`uint32_t` 로 두면 배열마다 2배가 되어 **64B 를 헛되게 쓴다**(RAM 이 벽이다).
#define KS_MAX 13
static uint16_t ksAt[KS_MAX];
static int16_t  ksCm[KS_MAX];

static long ksRead(uint8_t k, uint8_t trig, uint8_t echo) {
  const uint16_t now = (uint16_t)millis();
  if (ksAt[k] != 0 && (uint16_t)(now - ksAt[k]) < US_PERIOD_MS) return (long)ksCm[k];
  ksAt[k] = now ? now : 1;                 // 🔑 0 은 "아직 안 쟀다" 표지라 피한다
  digitalWrite(trig, LOW);   delayMicroseconds(2);
  digitalWrite(trig, HIGH);  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  const uint32_t us = pulseIn(echo, HIGH, US_TIMEOUT_US);
  ksCm[k] = (us == 0) ? (int16_t)SENSOR_NO_READING : (int16_t)(us / 58UL);
  return (long)ksCm[k];
}

// 래퍼 11개 — A1·B1 을 합쳐 **최대 13 센서**가 된다.
// ⚠ 핀은 남는 것을 돌려 쓴다(중복 무해 — 어차피 반사가 없고 **최악을 재는 것이 목적**이다).
static long ks03() { return ksRead( 2,  3,  5); }
static long ks04() { return ksRead( 3,  6, 12); }
static long ks05() { return ksRead( 4, A0, A1); }
static long ks06() { return ksRead( 5, A2, A3); }
static long ks07() { return ksRead( 6, A4, A5); }
static long ks08() { return ksRead( 7,  3,  5); }
static long ks09() { return ksRead( 8,  6, 12); }
static long ks10() { return ksRead( 9, A0, A1); }
static long ks11() { return ksRead(10, A2, A3); }
static long ks12() { return ksRead(11, A4, A5); }
static long ks13() { return ksRead(12,  3,  5); }

// ── 🔑 **k 순환** — 180초마다 다음 단계, 한 바퀴 뒤 처음으로 ────────────────
// 🔴 **왜 순환인가**(루트 지적): 링크가 불안정하다(소크에서 세션 9회·오프라인 9회).
//   180초 구간 하나가 **세션 절단으로 통째로 오염될 확률이 낮지 않다.**
//   단방향이면 **그 k 에 다시 못 온다** — 재부팅하면 처음부터다.
//   ✅ `modulo` 한 줄로 순환하면 오염된 구간을 **다음 바퀴에서 다시 얻고**,
//     같은 k 를 두 번 재서 **그 값이 이상치인지 갈린다.**
static const uint8_t  KS_STEPS[]  = { 2, 4, 6, 8, 10, 13 };
static const uint8_t  KS_NSTEP    = sizeof(KS_STEPS) / sizeof(KS_STEPS[0]);
static const uint16_t KS_PHASE_S  = 180;    // 구간 길이(초). `[CNT]` 60초 주기 × 3 → 차분 2개
static uint8_t ksActive  = 0;               // 지금 활성 센서 수
static uint8_t ksStepIdx = 0xFF;            // 마지막으로 찍은 단계(0xFF = 아직)

// 🔴 이 함수는 **활성 수만 바꾼다.** 등록은 `setup()` 에서 13개가 다 됐고,
//   `D` 프레임에도 13개가 실린다 — 그래서 `11n+11 <= 160` 벽도 같이 실측된다.
static void ksTick(uint32_t nowMs) {
  const uint8_t idx = (uint8_t)((nowMs / 1000UL / KS_PHASE_S) % KS_NSTEP);
  ksActive = KS_STEPS[idx];
  if (idx == ksStepIdx) return;
  ksStepIdx = idx;
#if DEBUG
  // 🔴🔴 **`up` 을 같이 찍는다** — 순환과 재부팅이 `k` 만 보면 **같은 모양**이다:
  //     순환   : k 13 → 2  (`up` 은 계속 증가)
  //     재부팅 : k 13 → 2  (🔴 `up` 이 0 으로 돌아간다)
  //   그리고 **RAM 이 무너지면 정확히 재부팅으로 나타난다.** `up` 이 없으면
  //   그것을 "13까지 잘 돌았다" 로 오독한다.
  //   🔑 monitor 가 이 줄로 구간을 자를 수 있게 되고, 경계를 사람이 추정하지 않는다.
  Serial.print(F("\n[KSWEEP] k="));   Serial.print(ksActive);
  Serial.print(F(" up="));            Serial.print(nowMs / 1000UL);
  Serial.print(F(" slot="));          Serial.print(slotNo);
  Serial.println();
#endif
}

#endif  // KSWEEP
