# 주차 관제 데모 — 2열 5행 실시간 자리 현황 + 예약

아두이노(센서) → 서버 → 브라우저로 자리 상태가 실시간으로 흐르고,
브라우저에서 누른 예약이 서버를 거쳐 아두이노까지 내려가 반영된 뒤 되돌아온다.

```
브라우저 ──WebSocket ws://<host>:9900/ws── 서버 ──TCP :9991 압축 ASCII 라인── 아두이노
                                            └─> data_log.json (최신 2건, 원자적 교체)
                                            └─> HTTP :9900 로 index.html 도 직접 서빙
```

**프로토콜 원본은 `docs/net/parking-protocol.md` 다.** 세 파일이 공유하는 전선 계약이고,
바꾸려면 그 문서 §12 절차를 따른다. 한쪽만 조용히 바꾸면 나머지 둘이 깨진다.

| 파일 | 역할 | 담당 |
|---|---|---|
| `client.ino` | 아두이노 노드. 10칸 가상 센서, 예약 수신·반영·ACK | arduino-engineer |
| `server.cpp` | TCP + HTTP/WebSocket 서버, 예약 중계, data_log.json | socket-engineer |
| `index.html` | 2열 5행 격자, 예약 UI. 단일 파일, 외부 의존성 0 | web-engineer |

---

## 실행

### 1. 서버 빌드

```bash
# macOS / Linux
c++ -std=c++11 -O2 -o server server.cpp
```

```bat
REM Windows (MSVC)
cl /EHsc /std:c++14 /utf-8 server.cpp ws2_32.lib
```

> ⚠ **Windows 에서 `/utf-8` 을 빼지 마라.** 이 소스는 BOM 없는 UTF-8 이고, 그 플래그가 없으면
> MSVC 가 시스템 코드페이지(한국어 윈도우면 CP949)로 해석해서 **화면에 나가는 한글 안내 문구가
> 전부 깨진다.** 빌드는 그대로 통과하므로 시연장에서 처음 알게 된다.

### 2. 서버 실행

```bash
./server          # 이 디렉터리에서 실행한다 (index.html 과 data_log.json 을 여기서 찾는다)
```

- `9991` — 아두이노용 TCP
- `9900` — 브라우저용 HTTP + WebSocket

`./server --selftest` 로 WebSocket 핸드셰이크와 체크섬 구현을 한 번에 자가진단할 수 있다.

### 3. 아두이노

`client.ino` 를 Arduino IDE 에서 열어 업로드한다. 배선은 **D7 = ESP TX → Uno / D8 = Uno → ESP RX**.
스케치 안의 와이파이 SSID·비밀번호와 `SERVER_IP` 를 자기 환경에 맞게 고친다.

> `arduino-cli` 로 빌드할 때는 **`.ino` 파일명과 상위 디렉터리명이 같아야 한다.**
> `조별과제샘플/client.ino` 는 그 자리에서 컴파일되지 않으므로 `client/client.ino` 로 복사해서 빌드한다.

### 4. 브라우저

<http://localhost:9900/> 을 연다. **`python -m http.server` 를 따로 띄우지 않는다** —
서버가 직접 서빙하므로 그렇게 하면 예전 구조로 돌아가고 WebSocket 이 붙지 않는다.

---

## 하드웨어 없이 전 경로 시험하기

아두이노 보드가 없어도 `net/fake_arduino.py` 가 같은 프로토콜로 아두이노 역할을 한다.

```bash
python3 ../net/fake_arduino.py                      # 기본: 1Hz 하트비트 + 무작위 센서 변화
python3 ../net/fake_arduino.py --drop-rate 1.0      # ACK 유실 → 서버 재전송·타임아웃 경로
python3 ../net/fake_arduino.py --reboot-after 10    # 재부팅 → 예약 재하달 경로
python3 ../net/fake_arduino.py --start-empty --arrive-sec 3 --depart-sec 6   # 예약 은퇴 경로
```

`net/ws_probe.py` 는 브라우저 없이 WebSocket 쪽을 확인한다(프레임 길이 필드까지 찍어 준다).

---

## 동작 요약

- 자리 ID 는 `A1..A5`(1열) / `B1..B5`(2열). 전선 인덱스 순서도 같다.
- `occupied`(센서 진실값)와 `reserved`(예약)는 **직교**한다. 넷 다 화면에서 구분된다:
  빈 자리 / 예약됨·입차 전 / 예약 없이 주차됨 / **예약한 자리에 입차 완료(= 정상 완료)**.
- 예약된 자리에서 차가 빠지면(`occupied` 1→0) 서버가 그 예약을 **은퇴**시킨다.
  이게 없으면 자리가 하나씩 영구히 예약 불가가 된다.
- 아두이노가 재부팅해도 **서버가 예약을 다시 내려보낸다.** 센서값의 주인은 아두이노,
  사람의 약속의 주인은 서버다.
- `data_log.json` 은 최신 2건만 유지하고 **임시파일에 쓴 뒤 원자적으로 교체**한다.
  여러 클라이언트가 동시에 읽어도 반쯤 쓰인 파일을 볼 수 없다.

## 알려진 한계

- **예약자가 끝내 오지 않으면** 그 예약은 사용자가 취소하기 전까지 남는다(만료 없음).
  화면에서 자기 예약을 눌러 취소할 수 있다.
- 사용자 인증이 없다. `user_id` 는 브라우저가 만들어 `localStorage` 에 보관하는 임의 값이고,
  전선과 화면에 자리는 마련돼 있어 인증이 생기면 값만 채우면 된다.
- **Windows 빌드는 실기에서 확인되지 않았다.** 개발이 macOS 에서 이뤄졌고, Windows 분기는
  정적 검토만 거쳤다. 처음 빌드할 때 오류가 나면 `docs/net/parking-protocol.md` 와
  `requests/` 의 REQ-0021·REQ-0023 에 무엇을 봤는지 남아 있다.
- 실제 아두이노 보드에 올려 본 적이 없다. `>` 프롬프트 타이밍과 ESP-01 의 실제 AT 응답 문구는
  실기에서만 확인된다.
