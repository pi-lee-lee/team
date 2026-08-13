---
id: REQ-0017
title: 주차 관제 데모: server.cpp 구현 (명세 REQ-0016 확정) + macOS 에서 실제 검증
from: root
to: socket-engineer
status: done
created: 2026-08-14T01:35:40+0900
updated: 2026-08-14T01:48:24+0900
files: ["조별과제샘플/server.cpp", "net/fake_arduino.py"]
parent: none
---

# REQ-0017 · 주차 관제 데모: server.cpp 구현 (명세 REQ-0016 확정) + macOS 에서 실제 검증

**요청자** `root` → **담당** `socket-engineer`

## 요청 내용

## 명세 확정 — 네가 올린 판단 3건 전부 승인한다

`docs/net/parking-protocol.md` v1 을 **얼린다.** 루트가 검산한 결과도 남긴다:

- 체크섬 예제 8건 전부 재계산 일치
- `Sec-WebSocket-Accept` 자가검증 벡터가 RFC 6455 예제와 일치
- 스냅샷 직렬화 845 B 확인 → 16비트 확장 길이 필수라는 지적이 맞다
- 9600bps 산식 재계산 일치 (1Hz 최대프레임 6.15%, 최악 14.90%)

승인 사유:
1. **ACK `result=2`(이미 예약됨)** — 승인. `occupied`/`reserved` 를 직교로 정한 이상
   "비었지만 남이 예약한 자리"는 실제로 발생한다. 내 3종 목록이 불완전했다.
2. **rid 이름공간 분리** — 승인. 브라우저 둘이 같은 rid 를 고르는 건 가정이 아니라 시간문제다.
3. **예약의 durable owner = 서버** — 승인. 센서 진실값은 아두이노, 사람의 약속은 서버.
   이 구분이 §7.4 재동기화의 근거로 정확하다.

이제 아두이노·웹에도 같은 명세로 요청을 발행한다. **명세는 이 시점부터 12절 절차로만 바뀐다.**

## 이 요청의 범위

`조별과제샘플/server.cpp` 를 명세대로 다시 쓴다. 현행 파일을 개선하는 것이지 새 트리로 옮기지 않는다.

### 반드시 지킬 것

**1) 크로스 플랫폼 — 이게 검증 가능성의 전제다**
현행은 Winsock 전용이라 **이 팀의 macOS 에서 빌드조차 안 된다.** 빌드도 못 하는 코드를
"구현 완료"라고 부를 수 없다. `#ifdef _WIN32` 로 Winsock/POSIX 양쪽을 지원해라.
- Windows: `ws2_32`, `MoveFileExA(MOVEFILE_REPLACE_EXISTING)`
- POSIX: `<sys/socket.h>` 계열, `rename()`
- 소켓 핸들·close·errno·초기화 차이를 얇은 어댑터로 감싸라

**2) 단일 프로세스 · 리스너 둘 · 다중 클라이언트**
현행의 블로킹 accept 이중 루프는 아두이노 하나만 받고 브라우저는 못 받는다.
`select()`(또는 `poll()`) 로 9991 TCP · 9900 HTTP/WS · 접속된 전 클라이언트를 한 루프에서 다룬다.
스레드보다 select 를 권한다 — 상태 공유가 단순해지고 데모 규모에 충분하다.

**3) 명세 §10 서버 체크리스트 전 항목.** 특히 자주 빠뜨리는 것:
- 연결마다 **개별** 바이트 버퍼(전역 버퍼 금지)
- WS **16비트 확장 길이** — ack 만 시험하면 통과하고 첫 스냅샷에서 깨진다
- 클→서 마스킹 해제 / 서→클 마스킹 금지
- ACK 타임아웃 1500ms · 재전송 2회 · **같은 wire_rid**
- 재부팅 감지 → 살아 있는 예약 전부 재하달
- `data_log.json` 최신 2건 + tmp→원자적 교체

**4) 정적 서빙**
9900 에서 `/` → `index.html`, `/data_log.json` → 그 파일. Content-Type 을 맞춰라.
경로 탈출(`../`) 차단은 넣어라 — 데모여도 디렉터리 서빙 코드에 이건 기본이다.

### 검증 도구도 같이 만든다 — `net/fake_arduino.py`

**하드웨어 없이 전 경로를 실증할 수 있어야 한다.** 아두이노 역할을 하는 파이썬 스크립트를
`net/` (네 소유)에 만들어라:
- 9991 로 접속해 명세대로 `S` 프레임을 1Hz 로 보낸다(가상 점유 상태를 무작위로 바꿈)
- `R`/`C` 를 받으면 체크섬 검증 후 반영하고 `A` 로 응답
- rid 멱등 캐시 8건
- 옵션: `--drop-rate` 로 ACK 를 일부러 유실시켜 **서버의 재전송 경로를 실제로 발동**시킨다
- 옵션: `--reboot-after N` 으로 재접속해 **§7.4 재동기화 경로를 발동**시킨다

## 이 요청은 "빌드했다"로 끝나지 않는다

macOS 에서 **실제로 띄우고 다음을 눈으로 확인**한 결과를 처리 결과에 적어라.
확인하지 않은 항목은 "미검증"이라고 명시해라 — 추정을 통과로 적지 마라.

## 왜 필요한가

서버가 없으면 아두이노와 웹은 각자 자기 절반만 만들고 통합에서 처음 만난다. 그리고 현행 Winsock 전용 코드는 이 팀의 macOS 에서 빌드가 안 되므로, 크로스 플랫폼으로 바꾸지 않으면 누구도 검증할 수 없는 코드가 된다. fake_arduino.py 가 있어야 하드웨어 없이 재전송·재부팅 같은 예외 경로까지 실증할 수 있다.

## 대상 파일

- `조별과제샘플/server.cpp`
- `net/fake_arduino.py`
## 완료 기준

다음을 macOS 에서 실제로 실행해 확인하고 결과를 처리 결과에 적는다:
1) 빌드 통과 — 사용한 컴파일 명령을 그대로 적는다 (macOS 용, 그리고 Windows 용 예상 명령)
2) fake_arduino.py 접속 → 서버가 S 프레임 수신 → data_log.json 이 **2건을 넘지 않고** 갱신됨 (실제 파일 내용을 붙여라)
3) 브라우저 없이도 WS 를 확인: 간단한 WS 클라이언트로 접속해 **845바이트 스냅샷이 온전히 수신**되는 것 확인 (16비트 길이 경로가 실제로 동작하는지가 핵심)
4) WS 로 예약 전송 → fake_arduino 가 R 수신 → A 응답 → ack 가 요청한 클라이언트에게만 도달
5) `--drop-rate` 로 ACK 유실 → **재전송이 같은 wire_rid 로 나가는 것** 확인 → 3회 실패 시 ack_timeout 오류
6) `--reboot-after` 로 재접속 → **살아 있는 예약이 재하달되는 것** 확인
7) 아두이노 미접속 상태에서 예약 → device_offline 오류
8) http://localhost:9900/ 가 index.html 을 서빙 (파일이 아직 옛 버전이어도 200 이면 된다)
확인 못 한 항목이 있으면 그 이유와 함께 "미검증"으로 남긴다.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0017 --by socket-engineer --note "<한 줄 요약>" -->

**담당** `socket-engineer` · 완료 — **완료 기준 8개 중 8개를 macOS 에서 실제로 실행해 확인.**

산출물: `조별과제샘플/server.cpp` (재작성) · `net/fake_arduino.py` · `net/ws_probe.py`(추가, 아래 설명)

### 1) 빌드 통과 ✅

```
$ c++ -std=c++11 -O2 -Wall -Wextra -o server server.cpp
(경고 0건, exit 0)
```

Windows 예상 명령(**미실행**): `cl /EHsc /std:c++14 server.cpp ws2_32.lib`

`#ifdef _WIN32` 로 소켓 타입·close·errno·초기화와 `MoveFileExA`/`rename` 를 갈랐다.
원본은 Winsock 전용이라 이 팀의 macOS 에서 빌드조차 안 됐다 — 그래서 아무도 검증할 수 없었다.

**추가로 넣은 것: `./server --selftest`**
하드코딩된 라이브러리 없이 SHA-1+base64 를 직접 구현했는데, 여기가 틀리면 브라우저가
핸드셰이크를 거부할 뿐 쓸 만한 에러가 안 나온다. 그래서 한 명령으로 자가진단하게 했다:

```
Sec-WebSocket-Accept : s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
기대값               : s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
  ✓ 일치
  ✓ (명세 §2.5 예제 라인 8건 전부 체크섬 통과)
자가검증 통과
```

### 2) data_log.json 최신 2건 유지 ✅

1.2초 간격으로 6회 읽어 **매번 유효한 JSON 이고 2건을 넘지 않음**을 확인했다.
(원자적 교체가 되고 있다는 증거다 — 반쯤 쓰인 파일을 한 번도 못 봤다.)

```
1회차: 2건 (2건 이하 True, 최신이 앞 True) 최신 ts=1786639507789 seq=23
...
5회차: 2건 (2건 이하 True, 최신이 앞 True) 최신 ts=1786639507789 seq=23   ← 변화 없으면 안 씀(§9.4)
6회차: 2건 (2건 이하 True, 최신이 앞 True) 최신 ts=1786639514668 seq=30
HTTP /data_log.json → 200 application/json; charset=utf-8 1003 바이트
```

실제 파일 내용:

```json
[
  {"ts":1786639498485,"device_id":"P1","uptime":10,"seq":13,"occupied":"1010001001","reserved":"0000000100","slots":[{"id":"A1","occupied":1,"reserved":0}, … {"id":"B5","occupied":1,"reserved":0}]},
  {"ts":1786639495246,"device_id":"P1","uptime":6,"seq":9,"occupied":"1010001000","reserved":"0000000100","slots":[ … ]}
]
```

### 3) WS 스냅샷 — 16비트 확장 길이 경로가 실제로 돌았다 ✅

`ws_probe.py` 가 **파싱한 길이 필드 바이트를 그대로 찍는다.** "845바이트가 왔다"만으로는
126 경로가 실제로 돌았는지 알 수 없기 때문이다.

```
← [snapshot] 829바이트  길이필드: 126 마커 + 16비트(03 3D) = 829  마스킹=False
← [snapshot] 839바이트  길이필드: 126 마커 + 16비트(03 47) = 839  마스킹=False
← [ack]       88바이트  길이필드: 7비트 즉시값 = 88          마스킹=False
   user_id/reserved_at 키 존재: True · 자리 수 10
```

- **`0x033D`=829, `0x0347`=839 — 16비트 필드가 실제로 쓰였다.** ack(88B)는 7비트 경로.
  ack 만 시험했으면 통과하고 스냅샷에서 깨졌을 것이다.
- **요청의 845바이트와 다른 이유**: 845 는 명세 §5.3 *예제*(`u17`/`reserved_at` 채워진 상태)의
  크기고, 실측은 내용에 따라 829~839 다. **핵심은 125 초과 → 16비트 경로이며 그건 실증됐다.**
- 서버→클라이언트 프레임 `마스킹=False` 확인(§5.2 요구).
- 핸드셰이크 Accept 를 파이썬 `hashlib` 로 독립 계산해 대조 — **매 접속 일치**.
  즉 손으로 쓴 SHA-1 이 표준 구현과 같다는 것이 실행 중에도 확인됐다.

### 4) 예약 왕복 + ack 는 요청자에게만 ✅

```
→ {"type":"reserve","slot":"B3","user_id":"u17","rid":"probe-ok"}
← [ack] {"type":"ack","rid":"probe-ok","slot":"B3","result":0,"message":"예약되었습니다"}
← [snapshot] … B3:01 …        ← reserved 반영
```

**WS 클라이언트 둘을 동시에 붙여** 타겟팅을 확인했다:

```
요청자 A → ack 1 · snapshot 5 · error 0
구경꾼 B → ack 0 · snapshot 6 · error 0
ack 는 요청자에게만: True      snapshot 은 양쪽 모두: True
```

### 5) ACK 유실 → 같은 wire_rid 재전송 → ack_timeout ✅

`fake_arduino.py --drop-rate 1.0` 은 **예약을 반영한 뒤 ACK 만 버린다.** (R 을 버리면
멱등 캐시가 채워지지 않아 더 약한 경로를 시험하게 된다 — §6.3 의 진짜 실패 모양이 아니다.)

서버 로그 — **세 번 모두 완전히 같은 줄**이다:

```
01:45:49  →ARD R,4,B5,u55,64
01:45:51  ↻ 재전송 2/3 (같은 wire_rid=4)
01:45:51  →ARD R,4,B5,u55,64
01:45:53  ↻ 재전송 3/3 (같은 wire_rid=4)
01:45:53  →ARD R,4,B5,u55,64
01:45:54  ! ACK 타임아웃 최종 실패 wire_rid=4
```

아두이노 쪽 로그 — **멱등 캐시가 실제로 적중**했다(§4.2 가 설계대로 작동):

```
01:45:49  ✗ ACK 유실시킴 — 예약은 반영된 상태: A,4,B5,0,32
01:45:51  = rid 4 재수신 → 멱등 캐시 적중, 재적용 없이 같은 ACK 재전송 ('B5', 0)
01:45:53  = rid 4 재수신 → 멱등 캐시 적중, 재적용 없이 같은 ACK 재전송 ('B5', 0)
```

브라우저가 받은 것: `{"type":"error","rid":"drop-test","code":"ack_timeout",…}` ✅

### 6) 재부팅 → 살아 있는 예약 재하달 ✅

`--reboot-after 10` 은 연결을 끊고 **seq/uptime/예약/멱등캐시를 전부 초기화**한 뒤 재접속한다.

서버: `01:46:25  ⟳ 재부팅 감지(새 연결) — 살아 있는 예약 2건 재하달`

아두이노:

```
01:46:24  ! 재부팅 — seq=0, uptime=0, 예약 전부 소실, 멱등 캐시 비움
01:46:25  ← R,7,A2,u-reboot,4F      → A,7,A2,0,35     ← 새 wire_rid, 정상 반영
01:46:25  ← R,8,B3,u17,68           → A,8,B3,0,38
```

**서버가 durable owner 라서 예약이 살아남았다**(§7.4). 새 wire_rid 로 내리는 것도 맞다 —
아두이노의 멱등 캐시도 재부팅으로 비었기 때문이다.

### 7) 아두이노 미접속 중 예약 → device_offline ✅

```
→ {"type":"reserve","slot":"B3","user_id":"u17","rid":"probe-offline"}
← {"type":"error","rid":"probe-offline","code":"device_offline","message":"센서가 연결되어 있지 않습니다"}
```

### 8) 정적 서빙 ✅

```
/                  → 200  text/html; charset=utf-8       9855바이트   (index.html 원본 그대로)
/data_log.json     → 200  application/json; charset=utf-8 1003바이트
/../server.cpp     → 403 Forbidden
/..%2fserver.cpp   → 403 Forbidden
```

### 구현 중 실제로 났던 버그 하나 (숨기지 않는다)

첫 실행에서 **SIGSEGV(exit 139)** 로 죽었다. `conns` 맵을 순회하면서 HTTP 응답 후
그 안에서 `erase` 해 **반복자가 무효화**된 것이다. 정적 파일 하나는 정상 서빙하고 그 직후
죽어서, 로그만 보면 "서빙 성공"으로 보였다.
→ 준비된 fd 를 먼저 벡터에 모으고 그 뒤에 처리하도록 고쳤고, 주석으로 이유를 남겼다.
**이 크래시가 criterion 8 을 "200 나왔으니 통과"로 적었으면 묻혔을 것이다.**

### `net/ws_probe.py` 를 추가한 이유 (요청 대상 파일 밖, 내 소유 트리 안)

브라우저 없이 §5 경로를 검증할 도구가 필요했다. 특히 **길이 필드 바이트를 눈으로 봐야**
16비트 경로가 돌았는지 알 수 있다. web-engineer 도 자기 구현을 이걸로 대조할 수 있다.
`net/` 은 내 소유라 요청 없이 만들었다 — 문제가 되면 지우거나 옮기겠다.

### 미검증 — 추정을 통과로 적지 않는다

- **Windows 빌드·실행을 하지 않았다.** `#ifdef _WIN32` 분기(Winsock 초기화,
  `MoveFileExA(MOVEFILE_REPLACE_EXISTING)`, `GetTickCount64`)는 **컴파일조차 해 보지 못했다.**
  macOS 에는 그 헤더가 없다. 윈도우에서 처음 빌드할 때 오타 수준의 오류가 나올 수 있다.
- **원자적 교체의 윈도우 경로가 실제로 원자적인지 확인 못 했다.** macOS `rename()` 경로만 실증했다.
- **실제 브라우저를 붙이지 않았다.** `index.html` 은 아직 옛 폴링 버전이고 web-engineer 소유라
  건드리지 않았다. WS 검증은 전부 `ws_probe.py` 로 했다.
- **진짜 아두이노 하드웨어가 없다.** `fake_arduino.py` 는 루프백 TCP 라 9600bps 지연도
  SoftwareSerial 의 송신 중 수신 유실도 재현하지 않는다. `--drop-rate` 는 그 **결과만** 흉내낸다.
- 부하·동시 접속 한계는 시험하지 않았다(데모 규모 가정).

### 루트가 판단할 것 (요청 발행은 하지 않았다)

빌드 산출물이 저장소에 남는다: `조별과제샘플/server`(바이너리), `data_log.json`,
`data_log.json.tmp`, `net/__pycache__/`. `.gitignore` 는 루트 소유라 손대지 않았다.
REQ-0010 에서 cpp-engineer 가 같은 성격의 요청을 한 것으로 보이니 함께 처리하면 될 것 같다.

### 처리 완료 · socket-engineer · 2026-08-14T01:48:24+0900

server.cpp 크로스플랫폼 재작성 + fake_arduino.py + ws_probe.py. macOS 실행 검증 8/8 통과(재전송 같은 rid·재부팅 재동기화·16비트 WS 길이 포함). Windows 분기는 컴파일도 못 해봄 = 미검증

