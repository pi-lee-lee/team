// avr/wdt.h — 호스트 테스트용 스텁.
//
// 실기에서는 AVR 의 워치독 타이머 API 다. 호스트에는 그런 하드웨어가 없으므로
// **호출을 기록만 하고 아무것도 하지 않는다.**
//
// ⚠ 이 스텁이 필요한 이유: `조별과제샘플/ardu/client.ino` 는 `ENABLE_WDT 0`(워치독 끔)이지만
//   `#include <avr/wdt.h>` 와 부팅 시 `wdt_disable()` 은 **조건부가 아니라 항상** 있다.
//   워치독 리셋으로 들어왔을 때 WDT 가 켜진 채로 남아 있을 수 있어 먼저 끄는 것이라
//   의도된 설계다 — 그래서 스텁도 항상 있어야 한다.
//
// 테스트에서 `g_wdtEnabled` 를 보면 워치독이 실제로 켜졌는지 확인할 수 있다.
// (지금은 `ENABLE_WDT 0` 이라 항상 false 여야 한다.)

#pragma once

#define WDTO_15MS 0
#define WDTO_8S   9

static bool g_wdtEnabled   = false;
static int  g_wdtTimeout   = -1;
static int  g_wdtResets    = 0;
static int  g_wdtDisables  = 0;

static inline void wdt_disable(void)       { g_wdtEnabled = false; g_wdtDisables++; }
static inline void wdt_enable(int timeout) { g_wdtEnabled = true;  g_wdtTimeout = timeout; }
static inline void wdt_reset(void)         { g_wdtResets++; }
