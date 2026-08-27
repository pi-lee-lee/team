# 릴리즈/ardu — 다섯 보드 펌웨어 (2026-08-27 사진)

> **이 폴더만으로 다섯 벌이 빌드되고 구워진다.** 저장소 밖으로 복사해 검증했다(§검증).
> 정본 작업 트리는 `서머리/ardu/`(그쪽을 고친다). 이 폴더는 **그 시점의 사진**이다 — 다음 릴리즈 때 다시 뜬다.

## 무엇이 들어 있나

| 폴더 | 보드 | 지고 있는 것 | hex md5 (이 폴더에서 빌드) |
|---|---|---|---|
| `p1/` | 1번 | 자리 A1·A2·A3 · 입구 EF·ER · LCD C1 | `81ab4bf6` |
| `p2/` | 2번 | 자리 A4·A5 · 출구 XF·XR · LCD C1 | `4399b57e` |
| `p3/` | 3번 | 입구 차단봉 ED(서보 D3 · Timer2) · LCD C1 | `a96e354a` |
| `p4/` | 4번 | 출구 차단봉 XD(서보 D3 · Timer2 · **설치 반대라 펄스 상수 맞바꿈**) · LCD C1 | `40c27933` |
| `p5/` | 5번 | 안내등 릴레이 R1~R5(D2~D6 · active-LOW) · LCD C1 | `7fcdef19` |
| `preburn.py` | — | 굽기 전 자가검사(폴더↔보드 · 서버 표·포트·IP 대조) — 기대값은 **`../server`**(같은 릴리즈의 서버)에서 읽는다 | |
| `libraries/` | — | **비어 있다** — 외부 라이브러리 0. 이유는 `libraries/README.md` | |

- 들어간 수정(정본 커밋): 부팅 서보 구동을 링크 결속 뒤로(`07bd3fa` · p3·p4) · 송신 로그 게이트 REQ-0501(`6b5d5964` · 다섯) · 리셋 원인 MCUSR/r2(`332201f7` · 다섯) · 출구 펄스 맞바꿈 REQ-0474(`998e7fa` · p4) · **ESP 절전 끄기 `AT+SLEEP=0`(RST→CWMODE→**SLEEP**→CWJAP · 다섯 전부)**.
- 🔴 **다섯 폴더는 `pN.ino` 하나만 다르다.** 공통 파일 29개(`EspLink_state.h` 포함)는 다섯이 바이트 동일해야 한다 — `md5 -q p?/EspLink_state.h` 가 한 값이어야 한다. 한 폴더에만 다른 판이 있으면 그 보드만 다르게 돈다(2026-08-27 에 p4 만 SLEEP 이 있어 넷이 절전 링크로 돌았다).
- 서버 주소: 각 `pN/Config.h` 의 `SERVER_IP`(지금 `192.168.0.29`) · `SERVER_PORT` `9991` · `WIFI_SSID`. 바뀌면 다섯 Config.h 를 같이 고친다(`preburn.py` 가 en0 실측과 대조해 빨강을 낸다).

## 굽는 법 — 세 줄

```bash
arduino-cli board list                                                  # ① 포트
python3 릴리즈/ardu/preburn.py 릴리즈/ardu/p3 --device P3                  # ② 자가검사 — 통과해야 굽는다(폴더 잘못 고르면 막는다)
arduino-cli upload -p /dev/cu.usbmodem____ --fqbn arduino:avr:uno 릴리즈/ardu/p3   # ③ 굽기
```
- 컴파일만: `arduino-cli compile --fqbn arduino:avr:uno --libraries 릴리즈/ardu/libraries 릴리즈/ardu/p3`
- 코어가 없으면: `arduino-cli core update-index` → `arduino-cli core install arduino:avr`
- ⚠ 폴더 이름과 `.ino` 이름이 같아야 한다(`p3/p3.ino`). 바꾸면 둘 다 바꾼다.
- ⚠ 굽기는 보드를 리셋한다(DTR). 관측 중이면 먼저 알린다.
- 굽고 나서 부팅 배너: `[PARKING NODE] proto v1 / … dev=PN` · `[BOOT] 리셋 원인: …` — 리셋 버튼 → `외부리셋(EXTRF)`, 전원 재인가 → `전원인가(POR)` 이 **서로 다르게** 나와야 그 계측이 산 것이다("알 수 없음" 8/8 이면 부트로더가 원인을 안 넘기는 것).

## 검증 — 자립한다는 것을 값으로 (2026-08-27)

```
① 저장소 밖 복사(/Users/idong-u/rel-ardu-check/ardu)에서 --libraries <복사본/libraries> 로 다섯 컴파일
   ~~p1 ac77701e · p2 6ffe0741 · p3 d468724e · p4 c08411f3 · p5 2c7a79b9~~   ← 🔴 **무효**(SLEEP 통일 전 판 · 대조용으로만 남긴다 · 굽지 마라)
   SLEEP 통일 뒤(11:xx) 이 폴더에서 다시 빌드: p1 81ab4bf6 · p2 4399b57e · p3 a96e354a · p4 40c27933 · p5 7fcdef19 = `서머리/ardu` 빌드(`arduino/.burn/sleep-hex`)와 동일
② 🔴 빨간불: 복사본 p1 에서 SoftSerialBig.cpp 하나를 빼고 컴파일 → `undefined reference to SoftSerialBig::available()` 링크 실패
   = 스케치가 자기 폴더 안 파일에 의존하고, 그 밖 어디서도 그 심볼이 오지 않는다
③ --libraries 를 사용자 라이브러리 폴더(~/Documents/Arduino/libraries · Servo·LiquidCrystal_I2C 있음)로 바꿔도 md5 동일 → 코어 밖 의존 0
④ 커밋에 저장된 그대로(`git archive` · SoftSerialBig.* 는 CRLF→LF 정규화됨)를 꺼내 빌드해도 md5 동일(p1·p3·p4 확인) → 새로 clone 한 트리에서도 같은 hex 가 나온다
```
- 이 폴더의 hex 를 칩에서 되읽어 대조하려면 `arduino/test/chipcmp.py`·`chipdevid.py`(정본 저장소의 도구)를 쓴다 — 릴리즈에는 넣지 않았다(굽기 도구는 정본 트리 몫).
