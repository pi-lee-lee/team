# win4 현재 상태 — 2026-08-17 14:55:39 자동 생성

> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.
> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-win4.md` 참조.
> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).

| 계측기 | 상태 |
|---|---|
| tap 프로세스 | 🟢 살아 있다 (pid 53235) |
| 시리얼 로그 성장 | 🟢 자라는 중 |
| 시리얼 마지막 줄 시각 | `14:55:35` |
| 시리얼 크기 | 4,257,862 B · 90,815 줄 |

## 시리얼 누계 — 🔴 **로그 줄 수다. 펌웨어 `[CNT]` 계수기가 아니다**

> `txdrop`·`txresync`·`txwait_skip`·`txwait_okto` 는 `[CNT]` 의 같은 이름
> 필드와 **다른 것을 센다.** 펌웨어 값은 아래 `[CNT]` 줄을 그대로 봐라.

| 표지(줄 수) | 누계 | 찾은 문자열 |
|---|---|---|
| `fail3` | 9 | `전송 3회 연속 실패` |
| `banner` | 1 | `[PARKING NODE]` |
| `espbanner` | 9 — **우리 DTR 1 / 사건 8** | `System Ready` |
| `zeroip` | 8 | `"0.0.0.0"` |
| `txresync` | 27 | `[TX-RESYNC]` |
| `txdrop` | 102 | `[TX-DROP]` |
| `txwait_skip` | 157 | `이번 주기는 건너뛴다` |
| `txwait_okto` | 32 | `상한 초과` |
| `busy` | 72 | `"busy ` |
| `tx` | 22,059 | `[TX] ` |
| `sendok` | 22,053 | `"SEND OK"` |
| `ipd` | 96 | `+IPD,` |
| `connect` | 13 | `online (CONNECT)` |
| `ipfound` | 54 | `★ IP 확보` |
| `cipstart_err` | 42 | `Unlink` |
| `stall` | 2 | `★ 정지 감지` |

펌웨어 `[CNT]` 마지막 줄: `up=24902 drop=11 esprst=9 resync=27 sendfail=0 okto=32 skip=157 online=0`

## 서버 누계 (파일 전체 — 창 구간이 아니다)

| 표지 | 누계 |
|---|---|
| `srv_accept` | 36 |
| `srv_sframe` | 59,292 |
| `srv_ack` | 89 |
| `srv_down` | 152 |
| `srv_offline` | 0 |

---

⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).
특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**
