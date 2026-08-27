#pragma once
// Counters.h — 운영 계수기 `[CNT]` 출력. `DEBUG` 와 무관하게 항상 나간다.   👁 관측
// ⚠ 스케치(pN.ino)의 프레임워크 블록에서 include 된다 — 자리·순서 고정(hex 가 바뀐다) · 📖 docs/arduino/LEDGER.md
//
// 🔴 **잠그지 마라.** 계수기를 더하는 것은 정상 작업이다.
// 🔴 **`#if DEBUG` 밖이다** — 셀 수만 있고 못 읽으면 안 뺀 것과 같다.
//   `DEBUG=0` 으로 구우면 관측이 줄어드는 게 아니라 **0칸**이 된다.
//   경계는 **"정수 계수는 밖 · 장문 진단은 안"** 이다. 이 경계를 흐리지 마라.
// ⚠ **형식을 바꾸면 monitor 파서가 깨진다.** 칸은 **뒤에 추가**하고 기존 이름·순서는 그대로 둔다.
static uint32_t      cntLastAt = 0;
static const uint32_t CNT_PERIOD_MS = 60000;
// REQ-0501 — [CNT] 가 심박이다(60s · 전부 0 이어도 찍는다). seq/tx/occ/rsv 를 더해 [TX] 없이도 상태가 보이게 한다.
//   occ/rsv 는 서버 S 프레임과 **같은 함수·같은 폭**(bitsToHex · 모듈 수로 폭 결정)으로 적는다 — 파서가 한 표기만 알면 되게.
static const uint8_t  HEX_W_MAX_CNT = 4;                          // bitsToHex 최대 폭(n≤16 → 4자리) · Modules.h 의 HEX_W_MAX 와 같은 값
static void bitsToHex(uint16_t mask, uint8_t n, char* out);      // Modules.h(뒤에 include) 의 것 — 앞선언

static void cntTick(uint32_t now) {
  if ((uint32_t)(now - cntLastAt) < CNT_PERIOD_MS) return;

  // 🔴 **이 줄도 송신 창을 지킨다.** `[CNT]` 는 약 142B 인데 하드웨어 UART TX 링은 **64B** 다 —
  //   넘치는 만큼 **블로킹**하고 그동안 `espRead()` 가 안 돌아 **도착 중인 하행 바이트가 사라진다.**
  //   ★ **UART 를 쓰는 모든 것이 슬롯 규율의 대상이다** — 프레임 송신만이 아니라 진단 출력도.
  //   ⚠ 60초 주기에서 최대 1.2초 지연은 무해하다. `cntLastAt` 을 **찍을 때** 갱신하므로 주기가 안 밀린다.
  //   ❌ **줄을 쪼개는 것은 대책이 아니다** — 총 바이트가 같고 오히려 여러 창에 흩어진다.
  //   📖 근거와 경위: docs/arduino/LEDGER.md
  if (!netOnline) {
    // 오프라인이면 하행이 없다 → 겹칠 대상이 없으므로 창을 기다리지 않는다.
    // (안 그러면 링크가 죽은 동안 [CNT] 가 통째로 멈춰 진단이 사라진다)
  } else if ((uint32_t)(now - slotStart) + SLOT_TX_RESERVE_CNT_MS > TX_WINDOW_MS) {
    return;                      // 우리 송신 창이 아니거나 남은 시간이 부족하다 → 다음 슬롯에
  }

  cntLastAt = now;
  // 전부 **부팅 이후 누적**이다. 구간값이 아니다 — 창을 잡으려면 두 줄을 빼서 써라.
  Serial.print(F("[CNT] up="));      Serial.print(now / 1000);
  Serial.print(F(" drop="));         Serial.print(linkDrops);
  Serial.print(F(" esprst="));       Serial.print(espResets);
  Serial.print(F(" hwrst="));        Serial.print(hwRstAsserts);
  Serial.print('/');                 Serial.print(hwRstOk);        // 4단 실행/성공 — 분모를 같이 둔다
  Serial.print(F(" resync="));       Serial.print(promptResyncs);
  Serial.print(F(" sendfail="));     Serial.print(sendFails);
  Serial.print(F(" okto="));         Serial.print(sendOkTimeouts);
  // ⚠ **옛 로그에는 아래 칸들이 없다. 굽기 전후를 같은 표에 넣지 마라** —
  //   그 칸의 `0` 은 "안 났다" 가 아니라 **"그 칩엔 없었다"** 다.
  Serial.print(F(" stuck="));        Serial.print(sendOkGiveups);   // T2 초과 → 링크 재수립
  Serial.print(F(" ackq="));         Serial.print(ackQ.pending());       // 지금 보류 중인 ACK
  Serial.print(F(" ackdrop="));      Serial.print(ackQ.drops());       // 큐가 넘쳐 버린 ACK(유입 초과)
  // 캐시에서 밀려 버린 ACK. **대책이 다르므로 칸을 가른다.**
  //   ⚠ 칸을 더해 이 줄이 길어지는 것은 이제 안전하다 — `cntTick` 이 송신 창 안에서만 나간다.
  Serial.print(F(" ackstale="));     Serial.print(ackQ.stale());
  // **정수 계수는 `DEBUG` 밖**이다.
  //   `[SLOT]` 줄은 매 슬롯 나가 대역을 먹으므로 `DEBUG` 안에 두지만, **누적은 여기 남긴다.**
  //   그래야 `DEBUG=0` 시연 빌드에서도 슬롯이 지켜졌는지 셀 수 있다.
  Serial.print(F(" slot="));         Serial.print(slotNo);          // 지난 슬롯 수 = **분모**
  Serial.print(F(" oow="));          Serial.print(slotOow);         // 수신 창 밖 하행 = 설계 위반
  Serial.print(F(" smiss="));        Serial.print(slotMissed);      // 송신 기회를 놓친 슬롯
  // SoftwareSerial 링버퍼(64B) 넘침. **하한이다**(읽으면 플래그가 지워진다).
  Serial.print(F(" ssovf="));        Serial.print(ssOverflows);
  // 체크섬 불일치(하행 프레임 파괴). 서버 계수로는 안 보이는 손실이다.
  Serial.print(F(" cksumng="));      Serial.print(cksumNg);
  // 바이트 흐름으로 잡은 `SEND OK` 수. **줄 경로가 놓치던 양**이다.
  // 🔴 **okstream 계열은 스냅샷으로 찍는다.**
  //   이 줄을 찍는 도중 `espRead()` 가 돈다. `cntTick` 중에 `inSend` 는 false 지만
  //   **`awaitingSendOk` 는 흔히 true 다**(CIPSEND 직후의 정상 상태).
  //   그 pump 가 `SEND OK` 를 매칭하면 `okstream`/`bigokst` 가 **출력 도중에 늘어나서**
  //   먼저 찍힌 `okstream` 은 옛 값, 나중 찍힌 `bigokst` 는 새 값이 된다.
  //   → **`bigokst > okstream` 이라는 불가능한 출력**이 나오고, 읽는 쪽은 펌웨어 결함으로 읽는다.
  //   ⚠ `pendfill`/`bigfill`/`bigdrop` 은 `inSend` 가 필요해서 이 문제가 없다.
  //     **이 계열만 해당한다** — 그래서 여기만 스냅샷한다.
  // 🔴 **출력 도중에는 `espRead()` 를 부르지 않는다.** 한 번 넣었다가 뺐다.
  //   이유: pump 가 `[AT] "..."` 를 찍어 **`[CNT]` 줄 한가운데로 끼어든다.**
  //   시험에서 실제로 그렇게 나왔다: `… pendfill=0[AT] "SEND OK" (7)\n bigfill=0 …`
  //   **monitor 의 파서가 그 줄을 못 읽는다.** 계측기를 고치려다 계측기를 깬 꼴이다.
  //   ⚠ 그러면 블로킹은 어떻게 되나 — **계산으로 안전하다:**
  //     `[CNT]` 약 220B @115200 ≈ 19ms · TX 링버퍼 64B 를 넘는 156B 만큼 ≈ 13.5ms 블로킹.
  //     그동안 들어올 하행은 9600bps 에서 **약 13B** 이고, SoftwareSerial 링버퍼 64B 에 여유가 있다.
  //   🔑 그리고 `cntTick` 은 **송신 창 안에서만** 나간다 — 하행이 오는 시간대가 아니다.
  //   ⚠ 줄이 이보다 크게 길어지면 이 계산을 다시 해라. **300B 시험이 그 경보다**(시험 [28]).
  //
  // 🔴 **여기 말고 블로킹하는 곳이 하나 더 있다 — 센서 함수다.**
  //   `pulseIn`(초음파 샘플)은 타임아웃만큼 막는다. 위 13B 는 **그것을 안 센 값이다.**
  //   비용: 타임아웃 6,960µs · 게이트 200ms → 슬롯당 **6회 · 41ms = 3.4%**
  //     그 6.96ms 동안 유입은 9600bps 에서 **약 6.7B** → 링버퍼 64B 에 여유가 남는다.
  //   ⚠ **타임아웃을 늘리면 이 값이 그대로 커진다.** 4m(25,000µs)로 두면 24B 가 되고
  //     위 13B 와 겹치면 37B 다 — 64B 의 절반을 넘는다. **문턱에서 유도하는 이유가 이것이다.**
  //   🔑 겹침 자체는 구조가 막는다: `readSensors()` 와 `cntTick()` 은 `tick()` 안에서
  //     **순차**이고 단일 스레드다. 두 블로킹이 동시에 일어날 수 없다.
  const uint16_t okStreamSnap = sendOkByStream;
  const uint16_t okLostSnap   = okLostByLine;
  const uint16_t okStreamBigSnap = okStreamBig;
  const uint16_t okLostBigSnap   = okLostBig;
  Serial.print(F(" okstream="));     Serial.print(okStreamSnap);
  // ★ monitor 요청 — `okstream` 과 **짝으로 읽는다**(위 feedRxChar 주석의 판정표).
  Serial.print(F(" penddrop="));     Serial.print(pendDrops);
  // 🔴 **`penddrop` 의 분모.** 이것이 0 이면 위 `penddrop=0` 은
  //   "안 넘쳤다"가 아니라 **"셀 일이 없었다"** 이고, 그 둘은 다른 결론으로 이어진다.
  Serial.print(F(" pendfill="));     Serial.print(pendFills);
  // ⚠ monitor 요청 — **대역(`>=64B`)별.** 누적 총계로는 창 G 의 갈림(`48~63` 0/43 · `>=64` 9/34)이 안 보인다.
  //   🔑 `64` 는 `_SS_MAX_RX_BUFF` = **기전 경계**다. monitor 의 `48` 은 표본 추출 기준이라 **다른 축이다.**
  Serial.print(F(" bigfill="));      Serial.print(pendFillsBig);
  Serial.print(F(" bigdrop="));      Serial.print(pendDropsBig);
  Serial.print(F(" bigokst="));      Serial.print(okStreamBigSnap);
  // ★ **대책 ②의 진짜 분자.** `okstream` 이 아니다 — 바이트 매처는 줄 완성 **전**에 발화해
  //   항상 줄 경로를 이기므로, `okstream` 은 그냥 탐지 총수다.
  //   **`oklost` 는 "줄 경로였으면 영영 잃었을 SEND OK"** 이고 그것이 곧 T2 였다.
  Serial.print(F(" oklost="));       Serial.print(okLostSnap);
  Serial.print(F(" bigoklost="));    Serial.print(okLostBigSnap);
  Serial.print(F(" skip="));         Serial.print(sendSkips);
  Serial.print(F(" online="));       Serial.print(netOnline ? 1 : 0);
  { char hx[HEX_W_MAX_CNT + 1];                                   // REQ-0501
    Serial.print(F(" seq="));        Serial.print(seqNo);
    Serial.print(F(" tx="));         Serial.print(cntTx);        cntTx = 0;
    bitsToHex(node.occMask, moduleCount(), hx); Serial.print(F(" occ=")); Serial.print(hx);
    bitsToHex(node.resMask, moduleCount(), hx); Serial.print(F(" rsv=")); Serial.println(hx); }
}
