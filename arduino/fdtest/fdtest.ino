// fdtest — **슬롯 구조에서 동시 송수신이 손실 없이 되는가**를 재는 최소 스케치
//
// ── 4판 (2026-08-17 · REQ-0157) ──────────────────────────────────────────────
// 3판은 `rxmax=16`(**건수**)을 냈다. 그 수를 하행 상한으로 쓸 수 없다:
//   **한 건의 크기가 바뀌면 건수 상한은 즉시 거짓이 된다.** 불변량은 바이트다(설계문서 §165).
//
// 4판이 바꾼 것 셋:
//   1. `rxmaxb` — 수신 창 안에 처리한 **바이트**의 최대. (건수 `rxmax` 도 남긴다 — 비교용)
//   2. `rxBuf` 64 → **96** (실기 `RX_CAP` 과 동일). 큰 프레임을 담기 위해서다.
//   3. 상대(`peer.py`)가 **프레임 크기 가변 + 창당 바이트 예산**으로 민다.
//      3판은 100ms 이벤트로 창당 약 12건을 보냈다 — **회선을 안 채운다.**
//      그 상태로는 `rxmaxb` 가 "장치가 꺼낼 수 있는 양"이 아니라 **"상대가 보낸 양"** 을 잰다.
//      (원장 §8.2-5 와 같은 형태: 분모를 안 만들어 놓고 분자를 읽는 것)
//
// 🔴 **미리 등록하는 예측** (자료 보기 전 · 2026-08-17 19:4x):
//   프레임 크기를 20/34/60B 로 바꿔도 **`rxmaxb` 는 540~576B 부근으로 수렴**하고,
//   **`rxmax`(건수)는 크게 갈린다**(≈27 / ≈16 / ≈9).
//   → 그러면 "바이트가 불변량"이 **관측으로** 뒷받침된다. 수렴하지 않으면 그 전제가 틀린 것이다.
//
// ── 이 판(2판)이 앞 판과 다른 점 ──────────────────────────────────────────
// 1판은 양쪽이 **쉬지 않고** 밀었다. 결과:
//     맥 gap=0 · 장치 gap=36 bad=32 **availhi=63**
//   → 전이중 자체는 됐다(양방향이 동시에 회선 상한 근처로 흘렀다).
//   → 그러나 장치가 약 5.5% 를 잃었고 원인은 **"못 들음"이 아니라 "받아 놓고 못 꺼냄"** 이었다.
//   → 그리고 양쪽 합계가 **회선 상한(960 B/s)을 넘겨 1100** 을 밀고 있었다.
//     즉 1판은 **"얼마나 빨리 넘치게 하나"** 를 재고 있었다. 그건 우리가 묻는 것이 아니다.
//
// 2판의 구조 (사용자 설계):
//     슬롯 1200ms  ·  0~600ms 장치 송신  ·  600~1200ms 수신 전용
//     슬롯 밖에서 생긴 송신은 **버리지 않고 담는다**
//
// 🔑 시계 동기가 필요 없다 — **슬롯 시작을 장치가 알려 준다.**
//   각 슬롯의 첫 프레임에 `1` 을 실어 보낸다. 맥은 그것을 받은 시각 + 600ms 부터 자기 창이다.
//   → 앞서 "1초 자로 113ms 를 겨냥할 수 없다"고 결론 낸 문제가 **설계에서 사라진다.**
//
// 🔴 이 시험의 진짜 산출은 **"0.6초 수신 창에 실제로 몇 건이 처리되나"** 다.
//   회선상으로는 576바이트(약 14건)가 들어가지만 **장치가 그만큼 꺼낼 수 있는지는 아무도 모른다.**
//   그 수가 곧 **서버가 지켜야 할 하행 상한**이다.
//
// ⚠ 미리 등록하는 예측(자료 보기 전):
//   **수신 창에는 우리가 송신하지 않는다 → 블로킹 쓰기가 없다 → 링버퍼가 안 마른다.**
//   그러므로 **회선 상한(약 14건)에 가깝게 처리될 것이다.** 크게 못 미치면 파싱이 병목이라는 뜻이고,
//   그건 지금 자료로 예상되지 않는다(체크섬 38바이트는 16MHz 에서 수십 µs 다).

static const unsigned long BAUD      = 9600;
static const uint16_t SLOT_MS        = 1200;   // 한 슬롯
static const uint16_t TX_WINDOW_MS   = 600;    // 0~600 우리 차례 · 600~1200 수신 전용
static const uint8_t  PAD_N          = 24;
static const char PAD[] = "ABCDEFGHIJKLMNOPQRSTUVWX";

static uint8_t xsum(const char* s, uint8_t n) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < n; i++) c = (uint8_t)(c ^ (uint8_t)s[i]);
  return c;
}

// ── 계수 ───────────────────────────────────────────────────────────────────
// ⚠ **유실·손상·창밖·넘침을 전부 따로 센다.** 합치면 기전을 못 가른다.
static uint32_t txLines = 0, rxLines = 0;
static uint32_t rxGaps = 0;        // 번호 건너뜀 = 유실
static uint32_t rxBad  = 0;        // 체크섬 불일치 = 손상
static uint32_t txDeferred = 0;    // 슬롯 경계를 넘길 것 같아 미룬 송신
static uint32_t qDrops = 0;        // 큐가 넘쳐 버린 것
static uint8_t  availHi = 0;       // RX 링버퍼 최고 수위 (63 이면 넘침 위험)
static uint16_t rxInSlot = 0, rxInSlotMax = 0;   // 한 수신 창에 처리한 **건수** · 그 최대
// 🔴 4판 산출 — **바이트**로 센다. 건수는 프레임 크기가 바뀌면 거짓이 된다(설계문서 §165).
//   전선 비용으로 센다: 줄 길이 + LF 1. (실기에는 여기에 `+IPD,<n>:` 접두 8~9B 가 더 붙는다)
static uint16_t rxInSlotB = 0, rxInSlotBMax = 0;
static uint32_t nextExpect = 0;
static bool     haveFirst = false;

// ── ② 장치 큐 — **의도를 담는다. 바이트를 담지 않는다** ────────────────────
// 실기 펌웨어의 최저 여유 RAM 은 실측 **782B** 다. 프레임(약 40B)을 깊이 4로 담으면 160B —
// 그 20% 를 한 기능이 먹는다. 그럴 필요가 없다:
//   · ACK 는 **멱등 캐시**가 내용을 갖고 있다 → `rid` 만 담으면 된다(8바이트)
//   · 상태 프레임은 **다시 만들면 된다** → 플래그 하나면 된다
// 🔴 그리고 더 중요한 이유: **미뤄 둔 텔레메트리는 재생(replay)이 아니라 재생성(regenerate)해야 한다.**
//   옛 내용을 나중에 보내면 **틀린 값을 보내는 것**이다. 마스크도 uptime 도 그 사이에 변한다.
static const uint8_t QN = 4;
static uint32_t qbuf[QN];          // 이 샘플에서는 "보내야 할 seq"를 담는다(실기에선 rid)
static uint8_t  qCount = 0, qHead = 0;

static void qPush(uint32_t v) {
  for (uint8_t i = 0; i < qCount; i++)
    if (qbuf[(uint8_t)((qHead + i) % QN)] == v) return;        // 중복은 안 넣는다
  if (qCount == QN) {                                           // 가장 오래된 것을 버리고 센다
    qHead = (uint8_t)((qHead + 1) % QN); qCount--; qDrops++;
  }
  qbuf[(uint8_t)((qHead + qCount) % QN)] = v; qCount++;
}

// 🔴 4판 — 64 → 96. **실기 `RX_CAP`(client.ino:368)과 같은 값으로 맞춘다.**
//   4판은 프레임 크기를 바꿔가며 재므로 64B 로는 큰 프레임을 담지 못한다.
//   ⚠ **계측기가 3판과 달라졌다는 뜻이다** — 측정 조건에 반드시 같이 적어라(원장 §5.5 "누가 만들었는가").
static char     rxBuf[96];
static uint8_t  rxLen = 0;
static uint32_t txSeq = 0;
static uint32_t slotStart = 0;
static bool     firstInSlot = true;
static bool     reportDue = false;
static uint32_t t0 = 0;
static uint32_t slots = 0;
static uint32_t lastEventAt = 0;
// 100ms 주기로 "보낼 것"이 생긴다 → 슬롯당 12건. 창(0.6s)에 14건이 들어가므로
// **큐가 쌓이지도 마르지도 않는 근처**다. 일부러 그 지점을 골랐다 — 넘치게 하면 `qdrop` 만 보고
// 정작 창 처리량을 못 잰다(1판이 회선 상한을 넘겨 "얼마나 빨리 넘치나"를 잰 것과 같은 실수다).
static const uint16_t EVENT_MS = 100;

static void onLine(char* s, uint8_t n) {
  if (n < 8 || s[0] != 'M') return;
  uint8_t want = 0;
  for (uint8_t i = (uint8_t)(n - 2); i < n; i++) {
    char ch = s[i];
    uint8_t v = (ch >= '0' && ch <= '9') ? (uint8_t)(ch - '0')
              : (ch >= 'A' && ch <= 'F') ? (uint8_t)(ch - 'A' + 10) : 0xFF;
    if (v == 0xFF) { rxBad++; return; }
    want = (uint8_t)((want << 4) | v);
  }
  if (xsum(s, (uint8_t)(n - 2)) != want) { rxBad++; return; }
  uint32_t seq = 0;
  for (uint8_t i = 2; i < n && s[i] != ','; i++) {
    if (s[i] < '0' || s[i] > '9') { rxBad++; return; }
    seq = seq * 10UL + (uint32_t)(s[i] - '0');
  }
  rxLines++;
  // 🔴 3판 정정 — **수신 창(0.6s) 안에서만 센다.**
  //   2판은 슬롯 경계에서만 리셋해서 **1.2초 전체**를 셌다 → `rxmax=30` 이 회선 상한(28.8)과
  //   같아 보였을 뿐, 우리가 알고 싶던 "창당 처리량"이 아니었다. **자리가 틀렸다.**
  // 🔴 4판 — 같은 자리에서 **바이트도** 센다. 창 판정 규칙은 3판과 같게 유지한다
  //   (줄의 마지막 바이트가 도착한 시각 기준 = 창 경계 걸침은 창 안으로 친다).
  //   규칙을 같이 두어야 3판의 `rxmax` 와 4판의 `rxmax` 를 나란히 볼 수 있다.
  if ((uint32_t)(millis() - slotStart) >= TX_WINDOW_MS) {
    rxInSlot++;
    rxInSlotB = (uint16_t)(rxInSlotB + n + 1);      // +1 = LF. 전선에 실제로 흐른 바이트
  }
  if (!haveFirst) { haveFirst = true; nextExpect = seq + 1; return; }
  if (seq != nextExpect) rxGaps += (seq > nextExpect) ? (seq - nextExpect) : 1;
  nextExpect = seq + 1;
}

static void pumpRx(void) {
  uint8_t a = (uint8_t)Serial.available();
  if (a > availHi) availHi = a;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n') { onLine(rxBuf, rxLen); rxLen = 0; }
    else if (ch != '\r' && rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = ch;
    else if (rxLen >= sizeof(rxBuf) - 1) { rxLen = 0; rxBad++; }
  }
}

// 한 줄을 실제로 내보낸다. **슬롯 경계를 넘길 것 같으면 안 보내고 큐에 담는다.**
// ⚠ 40바이트 ≈ 42ms(9600). 창이 600ms 라 여유는 크지만 **경계는 장치가 지킨다.**
// 🔴 3판 정정 — **보낼 수 있는지 먼저 묻는다.** 2판은 꺼낸 뒤에 물어서, 못 보내면 다시 담고
//   다음 반복에 또 꺼내는 **스핀**이 돌았다(`defer=47581`). 계수만 부풀고 CPU 를 태웠다.
static bool canSend(uint32_t now) { return (now - slotStart) + 60 <= TX_WINDOW_MS; }

static bool emitLine(uint32_t seq, uint32_t now) {
  (void)now;
  char buf[48];
  int n = snprintf(buf, sizeof(buf), "U,%lu,%d,%s,", (unsigned long)seq, firstInSlot ? 1 : 0, PAD);
  if (n <= 0 || (unsigned)n + 3 > (int)sizeof(buf)) return false;
  uint8_t c = xsum(buf, (uint8_t)n);
  buf[n]   = "0123456789ABCDEF"[(c >> 4) & 0x0F];
  buf[n+1] = "0123456789ABCDEF"[c & 0x0F];
  buf[n+2] = '\n';
  Serial.write((const uint8_t*)buf, (size_t)(n + 3));
  firstInSlot = false; txLines++;
  return true;
}

void setup() {
  Serial.begin(BAUD);
  t0 = slotStart = millis();
}

void loop() {
  pumpRx();
  const uint32_t now = millis();
  const uint32_t used = now - slotStart;

  // ── 이벤트 발생기 — **창과 무관하게** 일정 주기로 "보낼 것"이 생긴다 ────────
  // 실기에서 ACK 는 하행이 올 때 생긴다 — **우리 차례를 기다려 주지 않는다.**
  // 그래서 큐가 실제로 쓰이는 조건을 만들어야 계측이 뜻을 갖는다.
  // ⚠ 담는 것은 **의도(seq)** 이지 프레임이 아니다 — 미뤄 둔 것은 재생이 아니라 재생성한다.
  if ((uint32_t)(now - lastEventAt) >= EVENT_MS) {
    lastEventAt = now;
    if (!canSend(now) || used >= TX_WINDOW_MS) txDeferred++;   // 내 차례가 아니어서 미뤘다
    qPush(txSeq++);
  }

  if (used >= SLOT_MS) {                       // ── 슬롯 경계 ─────────────────
    slotStart += SLOT_MS;                      // ⚠ 누적 드리프트를 막으려고 **더한다**(§3.4 의 교훈)
    if (rxInSlot  > rxInSlotMax)  rxInSlotMax  = rxInSlot;
    if (rxInSlotB > rxInSlotBMax) rxInSlotBMax = rxInSlotB;
    rxInSlot = 0; rxInSlotB = 0;
    firstInSlot = true; slots++;
    reportDue = (slots % 5 == 0);              // 보고는 5슬롯(6초)마다 — 대역을 덜 먹는다
  }
  else if (used < TX_WINDOW_MS) {              // ── 0~600ms : 우리 차례 ───────
    // 🔴 3판 정정 — `seq` 를 **만들 때 올린다.**
    //   2판은 큐에서 꺼내 보낼 때 `txSeq` 를 안 올려서 **같은 seq 가 두 번 나갔다.**
    //   맥은 그것을 번호 건너뜀으로 읽었다 — **맥의 `gap=37` 은 맥 문제가 아니라 내 버그였다.**
    if (canSend(now) && qCount) {
      const uint32_t v = qbuf[qHead];
      qHead = (uint8_t)((qHead + 1) % QN); qCount--;
      if (!emitLine(v, now)) qPush(v);         // 이제 사실상 안 일어난다
    }
    // ⚠ 보고도 **우리 차례에만** 찍는다. 수신 창에서 찍으면 그 블로킹 쓰기가 링버퍼를 마르게 한다
    //   — 1판이 잃은 원인이 정확히 그것이었다(90바이트 ≈ 94ms · 링버퍼는 66ms 분).
    if (reportDue) {
      reportDue = false;
      const uint32_t sec = (now - t0) / 1000UL;
      Serial.print(F("[FD2] up="));    Serial.print(sec);
      Serial.print(F(" slots="));      Serial.print(slots);
      Serial.print(F(" tx="));         Serial.print(txLines);
      Serial.print(F(" rx="));         Serial.print(rxLines);
      Serial.print(F(" gap="));        Serial.print(rxGaps);
      Serial.print(F(" bad="));        Serial.print(rxBad);
      Serial.print(F(" defer="));      Serial.print(txDeferred);
      Serial.print(F(" qdrop="));      Serial.print(qDrops);
      Serial.print(F(" availhi="));    Serial.print(availHi);
      Serial.print(F(" rxmax="));      Serial.print(rxInSlotMax);     // 건수 — 3판과 비교용
      Serial.print(F(" rxmaxb="));     Serial.println(rxInSlotBMax);  // 🔴 4판의 산출 (바이트)
    }
  }
  // ── 600~1200ms : **수신 전용.** 아무것도 쓰지 않는다 ──────────────────────
  pumpRx();
}
