# winA 현재 상태 — 2026-08-17 21:13:39 자동 생성

> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.
> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-winA.md` 참조.
> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).

| 계측기 | 상태 |
|---|---|
| tap 프로세스 | 🟢 살아 있다 (pid 51860) |
| 시리얼 로그 성장 | ⏳ 첫 주기 |
| 시리얼 마지막 줄 시각 | `21:13:38` |
| 시리얼 크기 | 754,885 B · 17,709 줄 |

## 시리얼 누계 — 🔴 **로그 줄 수다. 펌웨어 `[CNT]` 계수기가 아니다**

> `txdrop`·`txresync`·`txwait_skip`·`txwait_okto` 는 `[CNT]` 의 같은 이름
> 필드와 **다른 것을 센다.** 펌웨어 값은 아래 `[CNT]` 줄을 그대로 봐라.

| 표지(줄 수) | 누계 | 찾은 문자열 |
|---|---|---|
| `fail3` | 0  ⚠ 훑은 줄 17,709 | `전송 3회 연속 실패` |
| `banner` | 1 | `[PARKING NODE]` |
| `espbanner` | 2 — **우리 DTR 1 / 사건 1** | `System Ready` |
| `zeroip` | 0  ⚠ 훑은 줄 17,709 | `"0.0.0.0"` |
| `txresync` | 0  ⚠ 훑은 줄 17,709 | `[TX-RESYNC]` |
| `txdrop` | 0  ⚠ 훑은 줄 17,709 | `[TX-DROP]` |
| `txwait_skip` | 239 | `이번 주기는 건너뛴다` |
| `txwait_okto` | 13 | `T1 초과` |
| `busy` | 1 | `"busy ` |
| `tx` | 3,202 | `[TX] ` |
| `sendok` | 3,201 | `"SEND OK"` |
| `ipd` | 1,060 | `+IPD,` |
| `connect` | 3 | `online (CONNECT)` |
| `ipfound` | 4 | `★ IP 확보` |
| `cipstart_err` | 1 | `Unlink` |
| `stall` | 0  ⚠ 훑은 줄 17,709 | `★ 정지 감지` |

펌웨어 `[CNT]` 마지막 줄: `up=2820 drop=2 esprst=0 resync=0 sendfail=0 okto=13 stuck=2 ackq=0 ackdrop=0 slot=2350 oow=693 smiss=0 ssovf=3 skip=239 online=1`

## 서버 누계 (파일 전체 — 창 구간이 아니다)

| 표지 | 누계 |
|---|---|
| `srv_accept` | 44 |
| `srv_sframe` | 62,864 |
| `srv_ack` | 1,158 |
| `srv_down` | 1,271 |
| `srv_offline` | 0 |

---

⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).
특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**
