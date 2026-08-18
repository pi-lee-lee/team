# winA2 현재 상태 — 2026-08-17 22:32:57 자동 생성

> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.
> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-winA2.md` 참조.
> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).

| 계측기 | 상태 |
|---|---|
| tap 프로세스 | 🟢 살아 있다 (pid 50028) |
| 시리얼 로그 성장 | 🟢 자라는 중 |
| 시리얼 마지막 줄 시각 | `22:32:57` |
| 시리얼 크기 | 847,179 B · 18,294 줄 |

## 시리얼 누계 — 🔴 **로그 줄 수다. 펌웨어 `[CNT]` 계수기가 아니다**

> `txdrop`·`txresync`·`txwait_skip`·`txwait_okto`·`cksumng_lines`·`slotoow` 는
`[CNT]` 의 같은/비슷한 이름
> 필드와 **다른 것을 센다.** 펌웨어 값은 아래 `[CNT]` 줄을 그대로 봐라.

| 표지(줄 수) | 누계 | 찾은 문자열 |
|---|---|---|
| `fail3` | 1 | `전송 3회 연속 실패` |
| `banner` | 1 | `[PARKING NODE]` |
| `espbanner` | 3 — **우리 DTR 1 / 사건 2** | `System Ready` |
| `zeroip` | 4 | `"0.0.0.0"` |
| `txresync` | 3 | `[TX-RESYNC]` |
| `txdrop` | 3 | `[TX-DROP]` |
| `txwait_skip` | 49 | `이번 주기는 건너뛴다` |
| `txwait_okto` | 3 | `T1 초과` |
| `busy` | 3 | `"busy ` |
| `tx` | 3,561 | `[TX] ` |
| `sendok` | 3,560 | `"SEND OK"` |
| `ipd` | 46 | `+IPD,` |
| `connect` | 4 | `online (CONNECT)` |
| `ipfound` | 4 | `★ IP 확보` |
| `cipstart_err` | 0  ⚠ 훑은 줄 18,294 | `Unlink` |
| `stall` | 0  ⚠ 훑은 줄 18,294 | `★ 정지 감지` |

펌웨어 `[CNT]` 마지막 줄: `up=4260 drop=3 esprst=2 resync=3 sendfail=0 okto=3 stuck=2 ackq=0 ackdrop=0 slot=3550 oow=35 smiss=1 ssovf=1 cksumng=0 skip=49 online=1`

## 서버 누계 (파일 전체 — 창 구간이 아니다)

| 표지 | 누계 |
|---|---|
| `srv_accept` | 49 |
| `srv_sframe` | 66,724 |
| `srv_ack` | 1,204 |
| `srv_down` | 1,320 |
| `srv_offline` | 0 |

---

⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).
특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**
