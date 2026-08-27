#pragma once
// TxState.h — 송신 상태 · 슬롯 상태   ⚙
// ⚠ 스케치(pN.ino)의 프레임워크 블록에서 include 된다 — 자리·순서 고정(hex 가 바뀐다) · 📖 docs/arduino/LEDGER.md
// ─────────────────────────────────────────────────────────────────────────
// 송신 상태
// ─────────────────────────────────────────────────────────────────────────
static uint16_t      seqNo = 0;                 // §2.4 uint16 순환. 재부팅하면 0
static unsigned long lastStatusAt = 0;          // §3.4 타이머는 하나
static unsigned long lastSendEndAt = 0;
static uint16_t      sentOcc = 0xFFFF, sentRes = 0xFFFF;  // 아직 아무것도 안 보냈다는 표시
static bool          changePending = false;
// REQ-0501 — 시리얼 에코 게이트. 이번 S 가 마지막 에코와 다를 때, 또는 사건 창(아래) 안일 때만
//   [TX] 와 [AT] 성공 왕복을 찍는다. 상행 S 자체는 그대로 1Hz 다 — 줄이는 것은 **시리얼 출력**뿐이다.
static bool          txEchoWanted  = true;      // 부팅 첫 프레임은 찍는다
static uint16_t      echoOcc = 0xFFFF, echoRes = 0xFFFF, echoTmask = 0xFFFE;   // 마지막으로 에코한 S 의 값
static uint16_t      cntTx = 0;                 // 지난 [CNT] 이후 실제로 나간 S 프레임 수 (분모)
static const uint16_t OKPREV_NA = 0xFFFF, OKPREV_TO = 0xFFFE;   // okprev 표지: NA=아직 한 번도 안 잼 · TO=직전 대기가 포기/복구로 끝남
static uint16_t      lastSendOkMs = OKPREV_NA;  // 직전 송신의 CIPSEND→SEND OK 지연(ms) 또는 표지 — [SLOT] okprev= 로 나간다
static const uint32_t ECHO_EVENT_MS = 60000UL;  // 사건(부팅·링크 전이·체크섬 실패) 뒤 이 시간은 정상 줄도 찍는다
static uint32_t      echoEventAt = 0;           // 마지막 사건 시각 — 부팅=0 이라 첫 60초는 자동으로 열린다
static inline bool   echoOpen(void) { return txEchoWanted || (uint32_t)(millis() - echoEventAt) < ECHO_EVENT_MS; }
static unsigned long changeAt = 0;

// tmask 도 변화 감지에 넣는다. 실제 tmask 는 10비트뿐이라 0xFFFF 를
// "필드 없음(=해제)" 표식으로 쓸 수 있다 — 어떤 실제 값과도 겹치지 않는다.
static const uint16_t TMASK_ABSENT = 0xFFFF;
static uint16_t       sentTmask = 0xFFFE;   // 실제 값·ABSENT 어느 쪽과도 다른 초기값

// ─────────────────────────────────────────────────────────────────────────
// 슬롯 상태
// ─────────────────────────────────────────────────────────────────────────
// 🔴 `slotStart` 는 **더해서** 전진시킨다(`+= SLOT_MS`). `= now` 로 하면 매 슬롯 오차가
//   누적돼 **주기가 스스로 늘어난다** — 1.000s 로 설계한 것이 실측 1.113s 가 된다.
static uint32_t slotStart   = 0;
static uint32_t slotNo      = 0;
static bool     slotSent    = false;   // 이번 슬롯에서 이미 보냈다(슬롯당 정확히 1거래)
static uint16_t slotOow     = 0;       // 수신 창 **밖**에 하행이 도착한 수 — 설계 위반 계수
static uint16_t slotMissed  = 0;       // 송신 창을 통째로 놓친 슬롯 수(보낼 기회를 못 씀)

#include "EspLink_state.h"   // ← EspLink 링크 계층. **위치를 옮기지 마라**
