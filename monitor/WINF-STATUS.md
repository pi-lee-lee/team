# winF 현재 상태 — 2026-08-18 15:40:04 자동 생성

> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.
> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-winF.md` 참조.
> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).

| 계측기 | 상태 |
|---|---|
| tap 프로세스 | 🟢 살아 있다 (pid 82574) |
| 시리얼 로그 성장 | 🟢 자라는 중 |
| 시리얼 마지막 줄 시각 | `15:40:04` |
| 시리얼 크기 | 431,549 B · 9,419 줄 |

## 시리얼 누계 — 🔴 **로그 줄 수다. 펌웨어 `[CNT]` 계수기가 아니다**

> `txdrop`·`txresync`·`txwait_skip`·`txwait_okto`·`cksumng_lines`·`slotoow` 는
`[CNT]` 의 같은/비슷한 이름
> 필드와 **다른 것을 센다.** 펌웨어 값은 아래 `[CNT]` 줄을 그대로 봐라.

| 표지(줄 수) | 누계 | 찾은 문자열 |
|---|---|---|
| `fail3` | 0  ⚠ 훑은 줄 9,419 | `전송 3회 연속 실패` |
| `banner` | 1 | `[PARKING NODE]` |
| `espbanner` | 1 — **우리 DTR 0 / 사건 1** | `System Ready` |
| `zeroip` | 6 | `"0.0.0.0"` |
| `txresync` | 0  ⚠ 훑은 줄 9,419 | `[TX-RESYNC]` |
| `txdrop` | 0  ⚠ 훑은 줄 9,419 | `[TX-DROP]` |
| `txwait_skip` | 24 | `이번 주기는 건너뛴다` |
| `txwait_okto` | 4 | `T1 초과` |
| `busy` | 0  ⚠ 훑은 줄 9,419 | `"busy ` |
| `tx` | 1,680 | `[TX] ` |
| `sendok` | 1,675 | `"SEND OK"` |
| `ipd` | 27 | `+IPD,` |
| `connect` | 7 | `online (CONNECT)` |
| `ipfound` | 9 | `★ IP 확보` |
| `cipstart_err` | 7 | `Unlink` |
| `stall` | 0  ⚠ 훑은 줄 9,419 | `★ 정지 감지` |

펌웨어 `[CNT]` 마지막 줄: `up=2100 drop=3 esprst=4 resync=0 sendfail=0 okto=4 stuck=3 ackq=3 ackdrop=86 ackstale=0 slot=1750 oow=21 smiss=5 ssovf=26 cksumng=7 skip=24 online=1`

## 서버 누계 (파일 전체 — 창 구간이 아니다)

| 표지 | 누계 |
|---|---|
| `srv_accept` | 95 |
| `srv_sframe` | 110,255 |
| `srv_ack` | 3,292 |
| `srv_down` | 3,972 |
| `srv_offline` | 0 |

---

⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).
특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**
