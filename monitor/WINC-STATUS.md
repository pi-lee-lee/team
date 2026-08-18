# winC 현재 상태 — 2026-08-18 09:05:40 자동 생성

> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.
> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-winC.md` 참조.
> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).

| 계측기 | 상태 |
|---|---|
| tap 프로세스 | 🔴 **tap 프로세스가 없다** (pid -) |
| 시리얼 로그 성장 | 🔴 **안 자란다** |
| 시리얼 마지막 줄 시각 | `07:22:37` |
| 시리얼 크기 | 4,079,880 B · 86,356 줄 |

## 시리얼 누계 — 🔴 **로그 줄 수다. 펌웨어 `[CNT]` 계수기가 아니다**

> `txdrop`·`txresync`·`txwait_skip`·`txwait_okto`·`cksumng_lines`·`slotoow` 는
`[CNT]` 의 같은/비슷한 이름
> 필드와 **다른 것을 센다.** 펌웨어 값은 아래 `[CNT]` 줄을 그대로 봐라.

| 표지(줄 수) | 누계 | 찾은 문자열 |
|---|---|---|
| `fail3` | 10 | `전송 3회 연속 실패` |
| `banner` | 1 | `[PARKING NODE]` |
| `espbanner` | 18 — **우리 DTR 1 / 사건 17** | `System Ready` |
| `zeroip` | 21 | `"0.0.0.0"` |
| `txresync` | 30 | `[TX-RESYNC]` |
| `txdrop` | 30 | `[TX-DROP]` |
| `txwait_skip` | 385 | `이번 주기는 건너뛴다` |
| `txwait_okto` | 65 | `T1 초과` |
| `busy` | 19 | `"busy ` |
| `tx` | 16,485 | `[TX] ` |
| `sendok` | 16,480 | `"SEND OK"` |
| `ipd` | 313 | `+IPD,` |
| `connect` | 20 | `online (CONNECT)` |
| `ipfound` | 23 | `★ IP 확보` |
| `cipstart_err` | 3 | `Unlink` |
| `stall` | 0  ⚠ 훑은 줄 86,356 | `★ 정지 감지` |

펌웨어 `[CNT]` 마지막 줄: `up=20760 drop=19 esprst=13 resync=30 sendfail=0 okto=64 stuck=9 ackq=0 ackdrop=0 slot=17300 oow=304 smiss=14 ssovf=14 cksumng=4 skip=383 online=1`

## 서버 누계 (파일 전체 — 창 구간이 아니다)

| 표지 | 누계 |
|---|---|
| `srv_accept` | 76 |
| `srv_sframe` | 91,370 |
| `srv_ack` | 2,112 |
| `srv_down` | 2,243 |
| `srv_offline` | 0 |

---

⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).
특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**
