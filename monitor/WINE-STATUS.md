# winE 현재 상태 — 2026-08-18 11:25:02 자동 생성

> 🔴 **이것은 판정이 아니다.** 생존과 적재량을 보는 눈금이다.
> 판정은 창을 닫고 동결 파서로 한다 — `monitor/HANDOFF-winE.md` 참조.
> ⚠ 아래 숫자는 전부 **누적값**이다(구간값 아님 · 원장 6.8).

| 계측기 | 상태 |
|---|---|
| tap 프로세스 | 🟢 살아 있다 (pid 72812) |
| 시리얼 로그 성장 | 🟢 자라는 중 |
| 시리얼 마지막 줄 시각 | `11:25:01` |
| 시리얼 크기 | 744,269 B · 16,294 줄 |

## 시리얼 누계 — 🔴 **로그 줄 수다. 펌웨어 `[CNT]` 계수기가 아니다**

> `txdrop`·`txresync`·`txwait_skip`·`txwait_okto`·`cksumng_lines`·`slotoow` 는
`[CNT]` 의 같은/비슷한 이름
> 필드와 **다른 것을 센다.** 펌웨어 값은 아래 `[CNT]` 줄을 그대로 봐라.

| 표지(줄 수) | 누계 | 찾은 문자열 |
|---|---|---|
| `fail3` | 0  ⚠ 훑은 줄 16,294 | `전송 3회 연속 실패` |
| `banner` | 1 | `[PARKING NODE]` |
| `espbanner` | 1 — **우리 DTR 1 / 사건 0** | `System Ready` |
| `zeroip` | 0  ⚠ 훑은 줄 16,294 | `"0.0.0.0"` |
| `txresync` | 0  ⚠ 훑은 줄 16,294 | `[TX-RESYNC]` |
| `txdrop` | 0  ⚠ 훑은 줄 16,294 | `[TX-DROP]` |
| `txwait_skip` | 7 | `이번 주기는 건너뛴다` |
| `txwait_okto` | 2 | `T1 초과` |
| `busy` | 0  ⚠ 훑은 줄 16,294 | `"busy ` |
| `tx` | 2,985 | `[TX] ` |
| `sendok` | 2,985 | `"SEND OK"` |
| `ipd` | 301 | `+IPD,` |
| `connect` | 1 | `online (CONNECT)` |
| `ipfound` | 1 | `★ IP 확보` |
| `cipstart_err` | 0  ⚠ 훑은 줄 16,294 | `Unlink` |
| `stall` | 0  ⚠ 훑은 줄 16,294 | `★ 정지 감지` |

펌웨어 `[CNT]` 마지막 줄: `up=3540 drop=0 esprst=0 resync=0 sendfail=0 okto=2 stuck=0 ackq=0 ackdrop=0 slot=2950 oow=295 smiss=0 ssovf=0 cksumng=0 skip=7 online=1`

## 서버 누계 (파일 전체 — 창 구간이 아니다)

| 표지 | 누계 |
|---|---|
| `srv_accept` | 83 |
| `srv_sframe` | 97,690 |
| `srv_ack` | 2,718 |
| `srv_down` | 2,853 |
| `srv_offline` | 0 |

---

⚠ `0` 이 '건강'인지 '아직 안 남'인지 갈라 읽어라(원장 1.1).
특히 `ipd`(하행 수신)는 **주입이 없으면 0 이다 — 미실행이지 건강이 아니다.**
