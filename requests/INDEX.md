# 요청 원장 (append-only)

모든 요청 이벤트가 시간순으로 쌓인다. 현재 상태의 원천은 각 요청 md 의
front-matter 이고, 미결 목록은 `requests/open/` 이다. 이 파일은 고쳐 쓰지 않는다.

| 시각 | 이벤트 | ID | 행위자 → 담당 | 제목 |
|---|---|---|---|---|
| 2026-08-13T19:19:26+0900 | 발행 | REQ-0001 | root → android-engineer | JNI 연동 최소 예제 앱 뼈대 |
| 2026-08-13T19:19:48+0900 | 착수 | REQ-0001 | android-engineer → android-engineer | JNI 연동 최소 예제 앱 뼈대 |
| 2026-08-13T19:39:28+0900 | 발행 | REQ-0002 | android-engineer → cpp-engineer | JNI: greetFromJNI 네이티브 구현 + CMakeLists |
| 2026-08-13T19:39:40+0900 | 착수 | REQ-0002 | cpp-engineer → cpp-engineer | JNI: greetFromJNI 네이티브 구현 + CMakeLists |
| 2026-08-13T19:47:51+0900 | 완료 | REQ-0002 | cpp-engineer → cpp-engineer | JNI: greetFromJNI 네이티브 구현 + CMakeLists |
| 2026-08-13T19:57:58+0900 | 완료 | REQ-0001 | root → android-engineer | JNI 연동 최소 예제 앱 뼈대 |
| 2026-08-13T20:56:18+0900 | 발행 | REQ-0003 | root → cpp-engineer | OpenCV 숫자인식 코어 + JNI 브리지 (DigitCam) |
| 2026-08-13T20:56:38+0900 | 발행 | REQ-0004 | root → socket-engineer | 전송 프로토콜 명세 + 테스트 TCP 서버 (DigitCam) |
| 2026-08-13T21:07:45+0900 | 발행 | REQ-0005 | root → android-engineer | CameraX 상시 프리뷰 + 설정파일 + TCP 전송 앱 (DigitCam) |
| 2026-08-13T21:08:08+0900 | 착수 | REQ-0004 | socket-engineer → socket-engineer | 전송 프로토콜 명세 + 테스트 TCP 서버 (DigitCam) |
| 2026-08-13T21:08:11+0900 | 착수 | REQ-0005 | android-engineer → android-engineer | CameraX 상시 프리뷰 + 설정파일 + TCP 전송 앱 (DigitCam) |
| 2026-08-13T21:12:33+0900 | 착수 | REQ-0003 | cpp-engineer → cpp-engineer | OpenCV 숫자인식 코어 + JNI 브리지 (DigitCam) |
| 2026-08-13T21:13:43+0900 | 발행 | REQ-0006 | android-engineer → cpp-engineer | 빌드 접점 확정: 타겟명 digitcam · 심볼 · OpenCV_DIR 주입 방식 · 정적/공유 회신 |
| 2026-08-13T21:19:40+0900 | 완료 | REQ-0004 | socket-engineer → socket-engineer | 전송 프로토콜 명세 + 테스트 TCP 서버 (DigitCam) |
| 2026-08-13T21:22:20+0900 | 발행 | REQ-0007 | root → socket-engineer | 프로토콜 개정2 반영: value 에 한글 · format 필드 · 빈 값 불가 |
| 2026-08-13T21:22:35+0900 | 착수 | REQ-0007 | socket-engineer → socket-engineer | 프로토콜 개정2 반영: value 에 한글 · format 필드 · 빈 값 불가 |
| 2026-08-13T21:28:45+0900 | 완료 | REQ-0006 | cpp-engineer → cpp-engineer | 빌드 접점 확정: 타겟명 digitcam · 심볼 · OpenCV_DIR 주입 방식 · 정적/공유 회신 |
| 2026-08-13T21:34:23+0900 | 발행 | REQ-0008 | socket-engineer → root | 기본 포트 5000 이 macOS AirPlay 와 충돌 — 계약 §6 기본값 검토 요청 |
| 2026-08-13T21:35:00+0900 | 발행 | REQ-0009 | socket-engineer → root | 소유권 훅 오탐: Bash 안의 비교연산자를 출력 리다이렉션으로 오인 |
| 2026-08-13T21:36:34+0900 | 완료 | REQ-0007 | socket-engineer → socket-engineer | 프로토콜 개정2 반영: value 에 한글 · format 필드 · 빈 값 불가 |
| 2026-08-13T21:41:11+0900 | 완료 | REQ-0009 | root → root | 소유권 훅 오탐: Bash 안의 비교연산자를 출력 리다이렉션으로 오인 |
| 2026-08-13T21:41:12+0900 | 완료 | REQ-0008 | root → root | 기본 포트 5000 이 macOS AirPlay 와 충돌 — 계약 §6 기본값 검토 요청 |
| 2026-08-13T22:05:12+0900 | 발행 | REQ-0010 | cpp-engineer → root | cpp/digitcam 빌드 산출물·테스트데이터 .gitignore 추가 |
| 2026-08-13T22:06:30+0900 | 완료 | REQ-0010 | root → root | cpp/digitcam 빌드 산출물·테스트데이터 .gitignore 추가 |
| 2026-08-13T22:06:38+0900 | 완료 | REQ-0003 | cpp-engineer → cpp-engineer | OpenCV 숫자인식 코어 + JNI 브리지 (DigitCam) |
| 2026-08-13T22:09:44+0900 | 발행 | REQ-0011 | root → cpp-engineer | 선명도 게이트가 26장 중 한 번도 발동하지 않았다 — 사실상 미검증 |
| 2026-08-13T22:10:46+0900 | 착수 | REQ-0011 | cpp-engineer → cpp-engineer | 선명도 게이트가 26장 중 한 번도 발동하지 않았다 — 사실상 미검증 |
| 2026-08-13T22:10:52+0900 | 발행 | REQ-0012 | android-engineer → cpp-engineer | 정답표 대조 24/26 — scene-03-persp-60 과 scene-04-far-28 이 segment_fail |
| 2026-08-13T22:11:30+0900 | 완료 | REQ-0005 | android-engineer → android-engineer | CameraX 상시 프리뷰 + 설정파일 + TCP 전송 앱 (DigitCam) |
| 2026-08-13T22:20:25+0900 | 완료 | REQ-0011 | cpp-engineer → cpp-engineer | 선명도 게이트가 26장 중 한 번도 발동하지 않았다 — 사실상 미검증 |
| 2026-08-13T22:21:35+0900 | 착수 | REQ-0012 | cpp-engineer → cpp-engineer | 정답표 대조 24/26 — scene-03-persp-60 과 scene-04-far-28 이 segment_fail |
| 2026-08-13T22:24:26+0900 | 완료 | REQ-0012 | cpp-engineer → cpp-engineer | 정답표 대조 24/26 — scene-03-persp-60 과 scene-04-far-28 이 segment_fail |
| 2026-08-13T22:41:37+0900 | 발행 | REQ-0013 | root → android-engineer | 앱 안에 설정 입력 화면 (폰에서 Android/data 접근 불가 대응) |
| 2026-08-13T22:42:02+0900 | 착수 | REQ-0013 | android-engineer → android-engineer | 앱 안에 설정 입력 화면 (폰에서 Android/data 접근 불가 대응) |
| 2026-08-14T00:21:06+0900 | 발행 | REQ-0014 | root → cpp-engineer | 움직임 임계 0.60 도 근거가 없다 — 선명도와 같은 방식으로 실측 근거를 만들어라 |
| 2026-08-14T00:21:58+0900 | 착수 | REQ-0014 | cpp-engineer → cpp-engineer | 움직임 임계 0.60 도 근거가 없다 — 선명도와 같은 방식으로 실측 근거를 만들어라 |
| 2026-08-14T00:28:16+0900 | 발행 | REQ-0015 | root → android-engineer | 긴급: 설정 화면 상단이 시스템 바에 가려 server.host 입력칸이 안 보인다 |
| 2026-08-14T00:30:25+0900 | 착수 | REQ-0015 | android-engineer → android-engineer | 긴급: 설정 화면 상단이 시스템 바에 가려 server.host 입력칸이 안 보인다 |
| 2026-08-14T00:35:45+0900 | 완료 | REQ-0014 | cpp-engineer → cpp-engineer | 움직임 임계 0.60 도 근거가 없다 — 선명도와 같은 방식으로 실측 근거를 만들어라 |
| 2026-08-14T00:39:46+0900 | 완료 | REQ-0015 | android-engineer → android-engineer | 긴급: 설정 화면 상단이 시스템 바에 가려 server.host 입력칸이 안 보인다 |
| 2026-08-14T00:39:56+0900 | 완료 | REQ-0013 | android-engineer → android-engineer | 앱 안에 설정 입력 화면 (폰에서 Android/data 접근 불가 대응) |
| 2026-08-14T01:23:19+0900 | 발행 | REQ-0016 | root → socket-engineer | 주차 관제 데모: 전선 프로토콜 명세를 먼저 얼려라 (아두이노 TCP + 브라우저 WebSocket) |
| 2026-08-14T01:23:53+0900 | 착수 | REQ-0016 | socket-engineer → socket-engineer | 주차 관제 데모: 전선 프로토콜 명세를 먼저 얼려라 (아두이노 TCP + 브라우저 WebSocket) |
| 2026-08-14T01:33:58+0900 | 완료 | REQ-0016 | socket-engineer → socket-engineer | 주차 관제 데모: 전선 프로토콜 명세를 먼저 얼려라 (아두이노 TCP + 브라우저 WebSocket) |
| 2026-08-14T01:35:40+0900 | 발행 | REQ-0017 | root → socket-engineer | 주차 관제 데모: server.cpp 구현 (명세 REQ-0016 확정) + macOS 에서 실제 검증 |
| 2026-08-14T01:36:33+0900 | 발행 | REQ-0018 | root → arduino-engineer | 주차 관제 데모: client.ino 를 프로토콜 명세대로 다시 쓴다 (10칸 가상센서 + 예약 수신/ACK) |
| 2026-08-14T01:37:31+0900 | 발행 | REQ-0019 | root → web-engineer | 주차 관제 데모: index.html 을 2열 5행 주차 격자 + WebSocket 예약 화면으로 다시 쓴다 |
| 2026-08-14T01:37:55+0900 | 착수 | REQ-0018 | arduino-engineer → arduino-engineer | 주차 관제 데모: client.ino 를 프로토콜 명세대로 다시 쓴다 (10칸 가상센서 + 예약 수신/ACK) |
| 2026-08-14T01:37:59+0900 | 착수 | REQ-0019 | web-engineer → web-engineer | 주차 관제 데모: index.html 을 2열 5행 주차 격자 + WebSocket 예약 화면으로 다시 쓴다 |
| 2026-08-14T01:48:25+0900 | 완료 | REQ-0017 | socket-engineer → socket-engineer | 주차 관제 데모: server.cpp 구현 (명세 REQ-0016 확정) + macOS 에서 실제 검증 |
| 2026-08-14T01:51:13+0900 | 발행 | REQ-0020 | arduino-engineer → root | 주차 프로토콜 v1 공백 3건 — 예약 은퇴 규칙 / result=3 의 slot 필드 / 기준6 단독 재현 불가 |
| 2026-08-14T01:53:09+0900 | 발행 | REQ-0021 | root → cpp-engineer | 검토 전용: server.cpp 의 Windows 분기(#ifdef _WIN32)를 눈으로 검증하라 — 이 팀에서 컴파일이 불가능하다 |
| 2026-08-14T01:53:34+0900 | 착수 | REQ-0021 | cpp-engineer → cpp-engineer | 검토 전용: server.cpp 의 Windows 분기(#ifdef _WIN32)를 눈으로 검증하라 — 이 팀에서 컴파일이 불가능하다 |
| 2026-08-14T01:53:41+0900 | 완료 | REQ-0018 | arduino-engineer → arduino-engineer | 주차 관제 데모: client.ino 를 프로토콜 명세대로 다시 쓴다 (10칸 가상센서 + 예약 수신/ACK) |
| 2026-08-14T01:56:47+0900 | 완료 | REQ-0020 | root → root | 주차 프로토콜 v1 공백 3건 — 예약 은퇴 규칙 / result=3 의 slot 필드 / 기준6 단독 재현 불가 |
| 2026-08-14T01:57:42+0900 | 발행 | REQ-0022 | root → socket-engineer | 명세 개정 2 + 서버 구현: 예약 은퇴 규칙(occupied 1→0 소진) · result=3 의 slot 은 '??' |
| 2026-08-14T01:58:32+0900 | 착수 | REQ-0022 | socket-engineer → socket-engineer | 명세 개정 2 + 서버 구현: 예약 은퇴 규칙(occupied 1→0 소진) · result=3 의 slot 은 '??' |
| 2026-08-14T01:59:20+0900 | 완료 | REQ-0021 | cpp-engineer → cpp-engineer | 검토 전용: server.cpp 의 Windows 분기(#ifdef _WIN32)를 눈으로 검증하라 — 이 팀에서 컴파일이 불가능하다 |
| 2026-08-14T02:00:44+0900 | 완료 | REQ-0019 | web-engineer → web-engineer | 주차 관제 데모: index.html 을 2열 5행 주차 격자 + WebSocket 예약 화면으로 다시 쓴다 |
| 2026-08-14T02:01:28+0900 | 발행 | REQ-0023 | root → socket-engineer | server.cpp Windows 분기 결함 8건 반영 (cpp-engineer 검토 REQ-0021) — 한글 깨짐이 최우선 |
| 2026-08-14T02:01:28+0900 | 발행 | REQ-0024 | root → arduino-engineer | REQ-0020 ② 지금 반영하라 — result=3 의 slot 을 '??' 고정 + ACK 필수 |
| 2026-08-14T02:01:52+0900 | 착수 | REQ-0024 | arduino-engineer → arduino-engineer | REQ-0020 ② 지금 반영하라 — result=3 의 slot 을 '??' 고정 + ACK 필수 |
| 2026-08-14T02:01:56+0900 | 착수 | REQ-0023 | socket-engineer → socket-engineer | server.cpp Windows 분기 결함 8건 반영 (cpp-engineer 검토 REQ-0021) — 한글 깨짐이 최우선 |
| 2026-08-14T02:03:58+0900 | 완료 | REQ-0024 | arduino-engineer → arduino-engineer | REQ-0020 ② 지금 반영하라 — result=3 의 slot 을 '??' 고정 + ACK 필수 |
| 2026-08-14T02:11:40+0900 | 완료 | REQ-0022 | socket-engineer → socket-engineer | 명세 개정 2 + 서버 구현: 예약 은퇴 규칙(occupied 1→0 소진) · result=3 의 slot 은 '??' |
| 2026-08-14T02:11:45+0900 | 완료 | REQ-0023 | socket-engineer → socket-engineer | server.cpp Windows 분기 결함 8건 반영 (cpp-engineer 검토 REQ-0021) — 한글 깨짐이 최우선 |
| 2026-08-14T06:37:01+0900 | 발행 | REQ-0025 | root → android-engineer | digitcam 앱: 기본 방향을 랜드스케이프로. 포트레이트는 사이클 버튼으로만 전환. 기울기로는 절대 안 돌아가게 |
| 2026-08-14T06:38:03+0900 | 착수 | REQ-0025 | android-engineer → android-engineer | digitcam 앱: 기본 방향을 랜드스케이프로. 포트레이트는 사이클 버튼으로만 전환. 기울기로는 절대 안 돌아가게 |
| 2026-08-14T06:50:15+0900 | 완료 | REQ-0025 | android-engineer → android-engineer | digitcam 앱: 기본 방향을 랜드스케이프로. 포트레이트는 사이클 버튼으로만 전환. 기울기로는 절대 안 돌아가게 |
| 2026-08-14T07:00:37+0900 | 발행 | REQ-0026 | root → android-engineer | 가로 모드 UI 재구성: 프리뷰 전체화면 + 우상단 반투명 안내 + 하단 얇은 소형 버튼 바 |
| 2026-08-14T07:00:58+0900 | 착수 | REQ-0026 | android-engineer → android-engineer | 가로 모드 UI 재구성: 프리뷰 전체화면 + 우상단 반투명 안내 + 하단 얇은 소형 버튼 바 |
| 2026-08-14T07:11:26+0900 | 완료 | REQ-0026 | android-engineer → android-engineer | 가로 모드 UI 재구성: 프리뷰 전체화면 + 우상단 반투명 안내 + 하단 얇은 소형 버튼 바 |
| 2026-08-14T09:33:23+0900 | 발행 | REQ-0027 | root → arduino-engineer | 센서 소스를 3중화하라: 실물 / 시뮬 / 수동주입 — 전선 프로토콜은 그대로 두고 USB 시리얼에 테스트 콘솔 추가 |
| 2026-08-14T09:42:11+0900 | 발행 | REQ-0028 | root → socket-engineer | 명세 개정 3: 테스트 모드 — 브라우저에서 센서값을 주입하는 하행 경로 (구조 반전) |
| 2026-08-14T09:42:32+0900 | 착수 | REQ-0028 | socket-engineer → socket-engineer | 명세 개정 3: 테스트 모드 — 브라우저에서 센서값을 주입하는 하행 경로 (구조 반전) |
| 2026-08-14T09:47:41+0900 | 착수 | REQ-0027 | arduino-engineer → arduino-engineer | 센서 소스를 3중화하라: 실물 / 시뮬 / 수동주입 — 전선 프로토콜은 그대로 두고 USB 시리얼에 테스트 콘솔 추가 |
| 2026-08-14T09:52:11+0900 | 완료 | REQ-0028 | socket-engineer → socket-engineer | 명세 개정 3: 테스트 모드 — 브라우저에서 센서값을 주입하는 하행 경로 (구조 반전) |
| 2026-08-14T09:52:14+0900 | 완료 | REQ-0027 | arduino-engineer → arduino-engineer | 센서 소스를 3중화하라: 실물 / 시뮬 / 수동주입 — 전선 프로토콜은 그대로 두고 USB 시리얼에 테스트 콘솔 추가 |
| 2026-08-14T09:54:37+0900 | 발행 | REQ-0029 | root → socket-engineer | 테스트 모드 구현: server.cpp 의 T 중계 + tmask 처리 + fake_arduino.py |
| 2026-08-14T09:55:51+0900 | 발행 | REQ-0030 | root → arduino-engineer | 테스트 모드 수신부: T 프레임 처리 + tmask 송신 (그릇에 외부 진입점을 붙인다) |
| 2026-08-14T09:55:51+0900 | 발행 | REQ-0031 | root → web-engineer | 테스트 모드 UI: 토글 버튼 + 칸별 센서값 주입 + 무장 중 놓칠 수 없는 표시 |
| 2026-08-14T09:56:38+0900 | 착수 | REQ-0031 | web-engineer → web-engineer | 테스트 모드 UI: 토글 버튼 + 칸별 센서값 주입 + 무장 중 놓칠 수 없는 표시 |
| 2026-08-14T09:57:27+0900 | 착수 | REQ-0029 | socket-engineer → socket-engineer | 테스트 모드 구현: server.cpp 의 T 중계 + tmask 처리 + fake_arduino.py |
| 2026-08-14T10:03:12+0900 | 착수 | REQ-0030 | arduino-engineer → arduino-engineer | 테스트 모드 수신부: T 프레임 처리 + tmask 송신 (그릇에 외부 진입점을 붙인다) |
| 2026-08-14T10:04:54+0900 | 발행 | REQ-0032 | socket-engineer → root | 명세 결함: 서버 재시작 시 wire_rid 재사용으로 아두이노 멱등 캐시가 새 명령을 삼킨다 |
| 2026-08-14T10:04:54+0900 | 완료 | REQ-0030 | arduino-engineer → arduino-engineer | 테스트 모드 수신부: T 프레임 처리 + tmask 송신 (그릇에 외부 진입점을 붙인다) |
| 2026-08-14T10:06:27+0900 | 완료 | REQ-0029 | socket-engineer → socket-engineer | 테스트 모드 구현: server.cpp 의 T 중계 + tmask 처리 + fake_arduino.py |
| 2026-08-14T10:06:50+0900 | 발행 | REQ-0033 | web-engineer → root | data_log.json(§9.1)에 무장 여부가 없다 — 폴백에서 주입값이 실측처럼 보인다 |
| 2026-08-14T10:07:04+0900 | 완료 | REQ-0032 | root → root | 명세 결함: 서버 재시작 시 wire_rid 재사용으로 아두이노 멱등 캐시가 새 명령을 삼킨다 |
| 2026-08-14T10:07:04+0900 | 발행 | REQ-0034 | root → socket-engineer | REQ-0032 판정 반영: 명세 §4.2 에 '새 연결에서 멱등 캐시를 비운다' + fake_arduino.py |
| 2026-08-14T10:09:10+0900 | 완료 | REQ-0033 | root → root | data_log.json(§9.1)에 무장 여부가 없다 — 폴백에서 주입값이 실측처럼 보인다 |
| 2026-08-14T10:09:10+0900 | 발행 | REQ-0035 | root → arduino-engineer | 두 건: 새 연결에서 멱등 캐시 비우기(REQ-0032 판정) + tmask 변화 시 즉시 전송 |
| 2026-08-14T10:09:33+0900 | 착수 | REQ-0034 | socket-engineer → socket-engineer | REQ-0032 판정 반영: 명세 §4.2 에 '새 연결에서 멱등 캐시를 비운다' + fake_arduino.py |
| 2026-08-14T10:11:15+0900 | 완료 | REQ-0031 | web-engineer → web-engineer | 테스트 모드 UI: 토글 버튼 + 칸별 센서값 주입 + 무장 중 놓칠 수 없는 표시 |
| 2026-08-14T10:12:19+0900 | 착수 | REQ-0035 | arduino-engineer → arduino-engineer | 두 건: 새 연결에서 멱등 캐시 비우기(REQ-0032 판정) + tmask 변화 시 즉시 전송 |
| 2026-08-14T10:13:56+0900 | 완료 | REQ-0034 | socket-engineer → socket-engineer | REQ-0032 판정 반영: 명세 §4.2 에 '새 연결에서 멱등 캐시를 비운다' + fake_arduino.py |
| 2026-08-14T10:15:41+0900 | 완료 | REQ-0035 | arduino-engineer → arduino-engineer | 두 건: 새 연결에서 멱등 캐시 비우기(REQ-0032 판정) + tmask 변화 시 즉시 전송 |
| 2026-08-14T10:20:29+0900 | 발행 | REQ-0036 | root → arduino-engineer | 멱등 캐시 비우기를 CLOSED 분기에도 건다 — 미검증 전제 하나를 없앤다 |
| 2026-08-14T10:20:48+0900 | 착수 | REQ-0036 | arduino-engineer → arduino-engineer | 멱등 캐시 비우기를 CLOSED 분기에도 건다 — 미검증 전제 하나를 없앤다 |
| 2026-08-14T10:22:48+0900 | 완료 | REQ-0036 | arduino-engineer → arduino-engineer | 멱등 캐시 비우기를 CLOSED 분기에도 건다 — 미검증 전제 하나를 없앤다 |
| 2026-08-14T11:37:58+0900 | 발행 | REQ-0037 | root → socket-engineer | 번호판 수신·매칭: 폰(digitcam) → 주차 서버 연결. 전선 프로토콜은 안 건드린다 |
| 2026-08-14T11:38:28+0900 | 착수 | REQ-0037 | socket-engineer → socket-engineer | 번호판 수신·매칭: 폰(digitcam) → 주차 서버 연결. 전선 프로토콜은 안 건드린다 |
| 2026-08-14T11:46:18+0900 | 완료 | REQ-0037 | socket-engineer → socket-engineer | 번호판 수신·매칭: 폰(digitcam) → 주차 서버 연결. 전선 프로토콜은 안 건드린다 |
| 2026-08-14T14:22:25+0900 | 발행 | REQ-0038 | root → socket-engineer | 기동 직후 data_log.json 이 없어 404 가 난다 — 시작할 때 빈 배열로 만들어라 |
| 2026-08-14T14:23:03+0900 | 발행 | REQ-0039 | root → web-engineer | WS 가 살아 있는데 폴백 404 를 빨간 오류로 띄운다 — 첫 실행이 고장처럼 보인다 |
| 2026-08-14T14:23:41+0900 | 착수 | REQ-0038 | socket-engineer → socket-engineer | 기동 직후 data_log.json 이 없어 404 가 난다 — 시작할 때 빈 배열로 만들어라 |
| 2026-08-14T14:23:41+0900 | 착수 | REQ-0039 | web-engineer → web-engineer | WS 가 살아 있는데 폴백 404 를 빨간 오류로 띄운다 — 첫 실행이 고장처럼 보인다 |
| 2026-08-14T14:26:59+0900 | 완료 | REQ-0038 | socket-engineer → socket-engineer | 기동 직후 data_log.json 이 없어 404 가 난다 — 시작할 때 빈 배열로 만들어라 |
| 2026-08-14T14:34:11+0900 | 발행 | REQ-0040 | web-engineer → root | 빈 public/ 디렉터리 하나 정리 요청 (REQ-0039 검증 잔여물) |
| 2026-08-14T14:35:27+0900 | 완료 | REQ-0039 | web-engineer → web-engineer | WS 가 살아 있는데 폴백 404 를 빨간 오류로 띄운다 — 첫 실행이 고장처럼 보인다 |
| 2026-08-14T14:52:12+0900 | 발행 | REQ-0041 | arduino-engineer → arduino-engineer | x |
| 2026-08-14T14:52:12+0900 | 발행 | REQ-0042 | root → arduino-engineer | 긴급: 실기에서 TCP 는 붙는데 netOnline 이 안 켜진다 — AT 응답 전부를 찍게 만들어라 |
| 2026-08-14T14:54:12+0900 | 착수 | REQ-0042 | arduino-engineer → arduino-engineer | 긴급: 실기에서 TCP 는 붙는데 netOnline 이 안 켜진다 — AT 응답 전부를 찍게 만들어라 |
| 2026-08-14T14:55:47+0900 | 완료 | REQ-0040 | root → root | 빈 public/ 디렉터리 하나 정리 요청 (REQ-0039 검증 잔여물) |
| 2026-08-14T14:56:02+0900 | 반려 | REQ-0041 | root → arduino-engineer | x |
| 2026-08-14T14:59:42+0900 | 완료 | REQ-0042 | arduino-engineer → arduino-engineer | 긴급: 실기에서 TCP 는 붙는데 netOnline 이 안 켜진다 — AT 응답 전부를 찍게 만들어라 |
| 2026-08-14T15:11:28+0900 | 발행 | REQ-0043 | root → arduino-engineer | 무장 중에는 시뮬레이터를 멈춰라 — 주입해 놓아도 다른 칸이 제멋대로 바뀐다 |
| 2026-08-14T15:11:28+0900 | 발행 | REQ-0044 | root → socket-engineer | 명세 §12A 에 한 줄: 무장 중에는 시뮬레이터가 전진하지 않는다 |
| 2026-08-14T15:12:01+0900 | 착수 | REQ-0044 | socket-engineer → socket-engineer | 명세 §12A 에 한 줄: 무장 중에는 시뮬레이터가 전진하지 않는다 |
| 2026-08-14T15:12:24+0900 | 착수 | REQ-0043 | arduino-engineer → arduino-engineer | 무장 중에는 시뮬레이터를 멈춰라 — 주입해 놓아도 다른 칸이 제멋대로 바뀐다 |
| 2026-08-14T15:14:58+0900 | 반려 | REQ-0043 | root → arduino-engineer | 무장 중에는 시뮬레이터를 멈춰라 — 주입해 놓아도 다른 칸이 제멋대로 바뀐다 |
| 2026-08-14T15:14:58+0900 | 반려 | REQ-0044 | root → socket-engineer | 명세 §12A 에 한 줄: 무장 중에는 시뮬레이터가 전진하지 않는다 |
| 2026-08-14T15:15:45+0900 | 발행 | REQ-0045 | root → socket-engineer | 명세 개정 5: 시뮬레이터 자유 실행을 없앤다 — 웹 '시뮬레이션' 버튼으로 한 걸음씩 |
| 2026-08-14T15:16:59+0900 | 착수 | REQ-0045 | socket-engineer → socket-engineer | 명세 개정 5: 시뮬레이터 자유 실행을 없앤다 — 웹 '시뮬레이션' 버튼으로 한 걸음씩 |
| 2026-08-14T15:21:26+0900 | 완료 | REQ-0045 | socket-engineer → socket-engineer | 명세 개정 5: 시뮬레이터 자유 실행을 없앤다 — 웹 '시뮬레이션' 버튼으로 한 걸음씩 |
| 2026-08-14T15:23:09+0900 | 발행 | REQ-0046 | root → socket-engineer | 시뮬 트리거 구현: server.cpp 의 M 중계 + fake_arduino.py 자율 전진 제거 |
| 2026-08-14T15:24:15+0900 | 발행 | REQ-0047 | root → arduino-engineer | 시뮬 자율 전진 제거 + M 프레임 수신 — 명세 v1.4 §12B |
| 2026-08-14T15:24:15+0900 | 발행 | REQ-0048 | root → web-engineer | 웹에 '시뮬레이션' 버튼 — 누를 때마다 자리 현황이 한 칸씩 바뀐다 |
| 2026-08-14T15:24:45+0900 | 착수 | REQ-0046 | socket-engineer → socket-engineer | 시뮬 트리거 구현: server.cpp 의 M 중계 + fake_arduino.py 자율 전진 제거 |
| 2026-08-14T15:25:09+0900 | 착수 | REQ-0048 | web-engineer → web-engineer | 웹에 '시뮬레이션' 버튼 — 누를 때마다 자리 현황이 한 칸씩 바뀐다 |
| 2026-08-14T15:30:23+0900 | 완료 | REQ-0046 | socket-engineer → socket-engineer | 시뮬 트리거 구현: server.cpp 의 M 중계 + fake_arduino.py 자율 전진 제거 |
| 2026-08-14T15:34:56+0900 | 완료 | REQ-0048 | web-engineer → web-engineer | 웹에 '시뮬레이션' 버튼 — 누를 때마다 자리 현황이 한 칸씩 바뀐다 |
| 2026-08-14T16:12:52+0900 | 발행 | REQ-0049 | root → arduino-engineer | 긴급: 연결이 죽어도 아두이노가 모른다 — CLOSED 를 못 받으면 영원히 죽은 소켓에 쓴다 |
| 2026-08-14T16:16:58+0900 | 착수 | REQ-0049 | arduino-engineer → arduino-engineer | 긴급: 연결이 죽어도 아두이노가 모른다 — CLOSED 를 못 받으면 영원히 죽은 소켓에 쓴다 |
| 2026-08-14T16:18:04+0900 | 완료 | REQ-0049 | arduino-engineer → arduino-engineer | 긴급: 연결이 죽어도 아두이노가 모른다 — CLOSED 를 못 받으면 영원히 죽은 소켓에 쓴다 |
| 2026-08-14T16:25:40+0900 | 착수 | REQ-0047 | arduino-engineer → arduino-engineer | 시뮬 자율 전진 제거 + M 프레임 수신 — 명세 v1.4 §12B |
| 2026-08-14T16:26:47+0900 | 완료 | REQ-0047 | arduino-engineer → arduino-engineer | 시뮬 자율 전진 제거 + M 프레임 수신 — 명세 v1.4 §12B |
| 2026-08-14T16:27:57+0900 | 발행 | REQ-0050 | root → socket-engineer | 명세 §12B.2 에 한 줄: 오버라이드 중인 칸은 시뮬 후보에서 제외한다 |
| 2026-08-14T16:28:28+0900 | 착수 | REQ-0050 | socket-engineer → socket-engineer | 명세 §12B.2 에 한 줄: 오버라이드 중인 칸은 시뮬 후보에서 제외한다 |
| 2026-08-14T16:32:02+0900 | 완료 | REQ-0050 | socket-engineer → socket-engineer | 명세 §12B.2 에 한 줄: 오버라이드 중인 칸은 시뮬 후보에서 제외한다 |
| 2026-08-14T17:06:40+0900 | 발행 | REQ-0051 | root → arduino-engineer | 긴급: 복구가 무한 루프다 — 죽은 소켓을 CIPCLOSE 로 닫지 않아 ALREADY CONNECTED 만 반복된다 |
| 2026-08-14T17:08:06+0900 | 착수 | REQ-0051 | arduino-engineer → arduino-engineer | 긴급: 복구가 무한 루프다 — 죽은 소켓을 CIPCLOSE 로 닫지 않아 ALREADY CONNECTED 만 반복된다 |
| 2026-08-14T17:15:32+0900 | 완료 | REQ-0051 | arduino-engineer → arduino-engineer | 긴급: 복구가 무한 루프다 — 죽은 소켓을 CIPCLOSE 로 닫지 않아 ALREADY CONNECTED 만 반복된다 |
| 2026-08-14T17:33:55+0900 | 발행 | REQ-0052 | root → socket-engineer | 명세 개정 7: 테스트 주입 설정을 파일로 보존하고 재무장 시 복원한다 |
| 2026-08-14T17:34:35+0900 | 착수 | REQ-0052 | socket-engineer → socket-engineer | 명세 개정 7: 테스트 주입 설정을 파일로 보존하고 재무장 시 복원한다 |
| 2026-08-14T17:36:02+0900 | 반려 | REQ-0052 | root → socket-engineer | 명세 개정 7: 테스트 주입 설정을 파일로 보존하고 재무장 시 복원한다 |
| 2026-08-14T17:42:48+0900 | 발행 | REQ-0053 | root → socket-engineer | 최우선: 서버가 블로킹 send() 에서 행 — 상대가 안 빼가면 전체가 선다 |
| 2026-08-14T17:43:36+0900 | 착수 | REQ-0053 | socket-engineer → socket-engineer | 최우선: 서버가 블로킹 send() 에서 행 — 상대가 안 빼가면 전체가 선다 |
| 2026-08-14T17:53:48+0900 | 완료 | REQ-0053 | socket-engineer → socket-engineer | 최우선: 서버가 블로킹 send() 에서 행 — 상대가 안 빼가면 전체가 선다 |
| 2026-08-14T18:50:39+0900 | 발행 | REQ-0054 | root → socket-engineer | 검토: client.ino 의 명세 준수 — 전선 계약의 주인 눈으로 |
| 2026-08-14T18:50:56+0900 | 반려 | REQ-0054 | root → socket-engineer | 검토: client.ino 의 명세 준수 — 전선 계약의 주인 눈으로 |
| 2026-08-14T18:51:43+0900 | 발행 | REQ-0055 | root → cpp-engineer | 검토: server.cpp 전체 — 특히 select 루프와 이번에 바뀐 전송 경로 |
| 2026-08-14T18:52:21+0900 | 발행 | REQ-0056 | root → socket-engineer | 검토: client.ino 의 명세 준수 — 전선 계약의 주인 눈으로 |
| 2026-08-14T18:52:21+0900 | 발행 | REQ-0057 | root → arduino-engineer | 검토: index.html 과 서버의 상호작용 — 장치 쪽 눈으로 |
| 2026-08-14T18:52:47+0900 | 발행 | REQ-0058 | root → web-engineer | 검토: 얼린 명세 자체 — 화면을 만들며 발견한 명세의 공백 |
| 2026-08-14T18:53:17+0900 | 착수 | REQ-0055 | cpp-engineer → cpp-engineer | 검토: server.cpp 전체 — 특히 select 루프와 이번에 바뀐 전송 경로 |
| 2026-08-14T18:53:22+0900 | 착수 | REQ-0056 | socket-engineer → socket-engineer | 검토: client.ino 의 명세 준수 — 전선 계약의 주인 눈으로 |
| 2026-08-14T18:53:33+0900 | 착수 | REQ-0057 | arduino-engineer → arduino-engineer | 검토: index.html 과 서버의 상호작용 — 장치 쪽 눈으로 |
| 2026-08-14T18:53:41+0900 | 착수 | REQ-0058 | web-engineer → web-engineer | 검토: 얼린 명세 자체 — 화면을 만들며 발견한 명세의 공백 |
| 2026-08-14T18:57:33+0900 | 완료 | REQ-0056 | socket-engineer → socket-engineer | 검토: client.ino 의 명세 준수 — 전선 계약의 주인 눈으로 |
| 2026-08-14T18:59:02+0900 | 완료 | REQ-0057 | arduino-engineer → arduino-engineer | 검토: index.html 과 서버의 상호작용 — 장치 쪽 눈으로 |
| 2026-08-14T18:59:44+0900 | 발행 | REQ-0059 | root → socket-engineer | 명세 구멍: 멱등 캐시 8칸이 재전송 창을 못 덮는다 — M 이 두 번 적용된다 |
| 2026-08-14T18:59:59+0900 | 완료 | REQ-0055 | cpp-engineer → cpp-engineer | 검토: server.cpp 전체 — 특히 select 루프와 이번에 바뀐 전송 경로 |
| 2026-08-14T19:00:11+0900 | 완료 | REQ-0058 | web-engineer → web-engineer | 검토: 얼린 명세 자체 — 화면을 만들며 발견한 명세의 공백 |
| 2026-08-14T19:00:48+0900 | 착수 | REQ-0059 | socket-engineer → socket-engineer | 명세 구멍: 멱등 캐시 8칸이 재전송 창을 못 덮는다 — M 이 두 번 적용된다 |
| 2026-08-14T19:14:49+0900 | 완료 | REQ-0059 | socket-engineer → socket-engineer | 명세 구멍: 멱등 캐시 8칸이 재전송 창을 못 덮는다 — M 이 두 번 적용된다 |
| 2026-08-14T23:14:14+0900 | 발행 | REQ-0060 | root → arduino-engineer | 실기 플래시: 보드가 맥에 붙었다 — 최신 스케치를 올리고 시리얼을 확인하라 |
| 2026-08-14T23:14:50+0900 | 착수 | REQ-0060 | arduino-engineer → arduino-engineer | 실기 플래시: 보드가 맥에 붙었다 — 최신 스케치를 올리고 시리얼을 확인하라 |
| 2026-08-14T23:25:15+0900 | 완료 | REQ-0060 | arduino-engineer → arduino-engineer | 실기 플래시: 보드가 맥에 붙었다 — 최신 스케치를 올리고 시리얼을 확인하라 |
| 2026-08-14T23:32:34+0900 | 발행 | REQ-0061 | root → arduino-engineer | 재플래시: 사용자가 핀을 D7↔D8 로 바꿨다 |
| 2026-08-14T23:32:54+0900 | 착수 | REQ-0061 | arduino-engineer → arduino-engineer | 재플래시: 사용자가 핀을 D7↔D8 로 바꿨다 |
| 2026-08-15T01:27:01+0900 | 발행 | REQ-0062 | root → socket-engineer | 365일 상시가동 결함: seq 랩(18.2h)이 재부팅 오탐을 만든다 — 판정 규칙을 정하라 |
| 2026-08-15T01:27:46+0900 | 발행 | REQ-0063 | root → arduino-engineer | 365일 상시가동: 워치독 + ESP 하드웨어 리셋선 (와이어 포맷 불변) |
| 2026-08-15T01:28:02+0900 | 착수 | REQ-0062 | socket-engineer → socket-engineer | 365일 상시가동 결함: seq 랩(18.2h)이 재부팅 오탐을 만든다 — 판정 규칙을 정하라 |
| 2026-08-15T01:28:42+0900 | 착수 | REQ-0063 | arduino-engineer → arduino-engineer | 365일 상시가동: 워치독 + ESP 하드웨어 리셋선 (와이어 포맷 불변) |
| 2026-08-15T01:29:01+0900 | 완료 | REQ-0061 | arduino-engineer → arduino-engineer | 재플래시: 사용자가 핀을 D7↔D8 로 바꿨다 |
| 2026-08-15T01:42:16+0900 | 완료 | REQ-0062 | socket-engineer → socket-engineer | 365일 상시가동 결함: seq 랩(18.2h)이 재부팅 오탐을 만든다 — 판정 규칙을 정하라 |
| 2026-08-15T01:51:53+0900 | 발행 | REQ-0064 | root → arduino-engineer | 🔴 실기 최우선: no ip 무한반복 — WIFI 상태줄을 버려서 IP 없이 CIPSTART 를 쏜다 |
| 2026-08-15T01:53:13+0900 | 발행 | REQ-0065 | root → socket-engineer | 2시간 소크 관측: 서버가 연결·프레임·공백을 스스로 증언하게 하라 |
| 2026-08-15T01:54:26+0900 | 착수 | REQ-0065 | socket-engineer → socket-engineer | 2시간 소크 관측: 서버가 연결·프레임·공백을 스스로 증언하게 하라 |
| 2026-08-15T01:55:17+0900 | 착수 | REQ-0064 | arduino-engineer → arduino-engineer | 🔴 실기 최우선: no ip 무한반복 — WIFI 상태줄을 버려서 IP 없이 CIPSTART 를 쏜다 |
| 2026-08-15T02:02:01+0900 | 완료 | REQ-0065 | socket-engineer → socket-engineer | 2시간 소크 관측: 서버가 연결·프레임·공백을 스스로 증언하게 하라 |
| 2026-08-15T02:38:50+0900 | 발행 | REQ-0066 | arduino-engineer → socket-engineer | 실기 수신경로 검증: 서버에서 예약(R)을 한 번 내려 달라 — +IPD 가 0건이다 |
| 2026-08-15T02:40:38+0900 | 착수 | REQ-0066 | socket-engineer → socket-engineer | 실기 수신경로 검증: 서버에서 예약(R)을 한 번 내려 달라 — +IPD 가 0건이다 |
| 2026-08-15T02:42:04+0900 | 발행 | REQ-0067 | root → web-engineer | 화면이 죽은 데이터를 살아있는 것처럼 보여준다 — 신선도 표시가 필요하다 |
| 2026-08-15T02:42:30+0900 | 착수 | REQ-0067 | web-engineer → web-engineer | 화면이 죽은 데이터를 살아있는 것처럼 보여준다 — 신선도 표시가 필요하다 |
| 2026-08-15T02:54:21+0900 | 완료 | REQ-0067 | web-engineer → web-engineer | 화면이 죽은 데이터를 살아있는 것처럼 보여준다 — 신선도 표시가 필요하다 |
| 2026-08-15T03:00:51+0900 | 발행 | REQ-0068 | web-engineer → socket-engineer | 폴백에도 장치 생사를 실어라 — (마) 채택, 중첩 device{online,last_frame_ts} |
| 2026-08-15T03:01:44+0900 | 착수 | REQ-0068 | socket-engineer → socket-engineer | 폴백에도 장치 생사를 실어라 — (마) 채택, 중첩 device{online,last_frame_ts} |
| 2026-08-15T03:02:13+0900 | 발행 | REQ-0069 | root → web-engineer | 폴백 fetch 에 상한을 걸어라 — 매달린 응답은 실패 보고조차 못 한다 |
| 2026-08-15T03:02:49+0900 | 착수 | REQ-0069 | web-engineer → web-engineer | 폴백 fetch 에 상한을 걸어라 — 매달린 응답은 실패 보고조차 못 한다 |
| 2026-08-15T03:06:54+0900 | 완료 | REQ-0068 | socket-engineer → socket-engineer | 폴백에도 장치 생사를 실어라 — (마) 채택, 중첩 device{online,last_frame_ts} |
| 2026-08-15T03:07:18+0900 | 발행 | REQ-0070 | socket-engineer → web-engineer | 폴백 device{online,last_frame_ts} 준비 완료 — 샘플 레코드 첨부 |
| 2026-08-15T03:16:17+0900 | 완료 | REQ-0069 | web-engineer → web-engineer | 폴백 fetch 에 상한을 걸어라 — 매달린 응답은 실패 보고조차 못 한다 |
| 2026-08-15T03:16:54+0900 | 착수 | REQ-0070 | web-engineer → web-engineer | 폴백 device{online,last_frame_ts} 준비 완료 — 샘플 레코드 첨부 |
| 2026-08-15T09:07:23+0900 | 완료 | REQ-0066 | socket-engineer → socket-engineer | 실기 수신경로 검증: 서버에서 예약(R)을 한 번 내려 달라 — +IPD 가 0건이다 |
| 2026-08-15T12:36:34+0900 | 발행 | REQ-0071 | root → arduino-engineer | 🔴 불안상태에서 복구되지 않는다 — 복구 사다리 (REQ-0063 흡수). CWJAP FAIL 은 '못 붙음'이 아니라 '12초 뒤 결합 소실' 이다 |
| 2026-08-15T12:37:23+0900 | 발행 | REQ-0072 | root → socket-engineer | 좀비 아두이노 소켓 회수(유휴 마감+keepalive) · 그리고 '복구시간' 을 소크 지표에 넣어라 |
| 2026-08-15T12:38:00+0900 | 착수 | REQ-0072 | socket-engineer → socket-engineer | 좀비 아두이노 소켓 회수(유휴 마감+keepalive) · 그리고 '복구시간' 을 소크 지표에 넣어라 |
| 2026-08-15T12:46:59+0900 | 완료 | REQ-0070 | web-engineer → web-engineer | 폴백 device{online,last_frame_ts} 준비 완료 — 샘플 레코드 첨부 |
| 2026-08-15T12:52:55+0900 | 착수 | REQ-0071 | arduino-engineer → arduino-engineer | 🔴 불안상태에서 복구되지 않는다 — 복구 사다리 (REQ-0063 흡수). CWJAP FAIL 은 '못 붙음'이 아니라 '12초 뒤 결합 소실' 이다 |
| 2026-08-15T12:56:36+0900 | 완료 | REQ-0071 | arduino-engineer → arduino-engineer | 🔴 불안상태에서 복구되지 않는다 — 복구 사다리 (REQ-0063 흡수). CWJAP FAIL 은 '못 붙음'이 아니라 '12초 뒤 결합 소실' 이다 |
| 2026-08-15T12:56:41+0900 | 완료 | REQ-0064 | arduino-engineer → arduino-engineer | 🔴 실기 최우선: no ip 무한반복 — WIFI 상태줄을 버려서 IP 없이 CIPSTART 를 쏜다 |
| 2026-08-15T12:56:49+0900 | 완료 | REQ-0063 | arduino-engineer → arduino-engineer | 365일 상시가동: 워치독 + ESP 하드웨어 리셋선 (와이어 포맷 불변) |
| 2026-08-15T13:02:32+0900 | 완료 | REQ-0072 | socket-engineer → socket-engineer | 좀비 아두이노 소켓 회수(유휴 마감+keepalive) · 그리고 '복구시간' 을 소크 지표에 넣어라 |
| 2026-08-15T13:19:57+0900 | 발행 | REQ-0073 | root → arduino-engineer | 🔴 재시도 폭주가 라우터에 막힌다 — 지수 백오프(상한 5~10분) + 쿨다운 단 + '직전시도경과' 계측 |
| 2026-08-15T13:21:17+0900 | 발행 | REQ-0074 | root → arduino-engineer | 🔴 최우선: 최소 AP 결합 테스트 스케치 (arduino/aptest) — 로직인지 하드웨어인지 가른다. REQ-0073 보다 먼저 |
| 2026-08-15T13:21:52+0900 | 발행 | REQ-0075 | arduino-engineer → socket-engineer | 정상 재접속 66회의 소요시간 분포를 알려 달라 (REQ-0073 백오프 설계 근거) |
| 2026-08-15T13:23:13+0900 | 착수 | REQ-0074 | arduino-engineer → arduino-engineer | 🔴 최우선: 최소 AP 결합 테스트 스케치 (arduino/aptest) — 로직인지 하드웨어인지 가른다. REQ-0073 보다 먼저 |
| 2026-08-15T13:26:11+0900 | 착수 | REQ-0075 | socket-engineer → socket-engineer | 정상 재접속 66회의 소요시간 분포를 알려 달라 (REQ-0073 백오프 설계 근거) |
| 2026-08-15T13:26:45+0900 | 완료 | REQ-0074 | arduino-engineer → arduino-engineer | 🔴 최우선: 최소 AP 결합 테스트 스케치 (arduino/aptest) — 로직인지 하드웨어인지 가른다. REQ-0073 보다 먼저 |
| 2026-08-15T13:27:12+0900 | 완료 | REQ-0075 | socket-engineer → socket-engineer | 정상 재접속 66회의 소요시간 분포를 알려 달라 (REQ-0073 백오프 설계 근거) |
| 2026-08-15T13:42:17+0900 | 발행 | REQ-0076 | root → arduino-engineer | 🔴 aptest 2판: MAC 하나만 바꿔 A/B — 차단 가설을 확정하거나 죽인다 (단계1 결과: 최소 스케치도 CWJAP FAIL) |
| 2026-08-15T13:42:50+0900 | 착수 | REQ-0076 | arduino-engineer → arduino-engineer | 🔴 aptest 2판: MAC 하나만 바꿔 A/B — 차단 가설을 확정하거나 죽인다 (단계1 결과: 최소 스케치도 CWJAP FAIL) |
| 2026-08-15T13:47:07+0900 | 완료 | REQ-0076 | arduino-engineer → arduino-engineer | 🔴 aptest 2판: MAC 하나만 바꿔 A/B — 차단 가설을 확정하거나 죽인다 (단계1 결과: 최소 스케치도 CWJAP FAIL) |
| 2026-08-15T16:01:20+0900 | 반려 | REQ-0073 | root → arduino-engineer | 🔴 재시도 폭주가 라우터에 막힌다 — 지수 백오프(상한 5~10분) + 쿨다운 단 + '직전시도경과' 계측 |
| 2026-08-15T16:02:35+0900 | 발행 | REQ-0077 | root → socket-engineer | 서버 재기동 + 새 망 기준선 — 맥 IP 가 192.168.50.146 에서 192.168.35.21 로 바뀌었다 |
| 2026-08-15T16:02:55+0900 | 발행 | REQ-0078 | root → arduino-engineer | client.ino 플래싱까지 자율 수행 — ESP_RST_WIRED 0 복귀 · aptest 판정 버그 · 프레임 적재 확인 |
| 2026-08-15T16:03:21+0900 | 착수 | REQ-0077 | socket-engineer → socket-engineer | 서버 재기동 + 새 망 기준선 — 맥 IP 가 192.168.50.146 에서 192.168.35.21 로 바뀌었다 |
| 2026-08-15T16:03:22+0900 | 착수 | REQ-0078 | arduino-engineer → arduino-engineer | client.ino 플래싱까지 자율 수행 — ESP_RST_WIRED 0 복귀 · aptest 판정 버그 · 프레임 적재 확인 |
| 2026-08-15T17:25:09+0900 | 완료 | REQ-0077 | socket-engineer → socket-engineer | 서버 재기동 + 새 망 기준선 — 맥 IP 가 192.168.50.146 에서 192.168.35.21 로 바뀌었다 |
| 2026-08-15T17:26:23+0900 | 발행 | REQ-0079 | socket-engineer → arduino-engineer | 새 망 첫 소크 실측 — MCU 68.5분 연속 가동(옛 망 중앙값 약 6분의 11배), 재부팅 1회 |
| 2026-08-15T17:27:32+0900 | 완료 | REQ-0078 | arduino-engineer → arduino-engineer | client.ino 플래싱까지 자율 수행 — ESP_RST_WIRED 0 복귀 · aptest 판정 버그 · 프레임 적재 확인 |
| 2026-08-15T17:28:59+0900 | 완료 | REQ-0079 | arduino-engineer → arduino-engineer | 새 망 첫 소크 실측 — MCU 68.5분 연속 가동(옛 망 중앙값 약 6분의 11배), 재부팅 1회 |
| 2026-08-15T17:47:08+0900 | 발행 | REQ-0080 | root → socket-engineer | 2시간 소크 관측 — 새 환경 기준선 · 하행 재전송률 판정 |
| 2026-08-15T17:47:35+0900 | 발행 | REQ-0081 | root → arduino-engineer | 2시간 소크 — 장치 쪽 안정성 확인 · 저장소와 실제 칩의 불일치 재발 방지 |
| 2026-08-15T17:47:38+0900 | 착수 | REQ-0080 | socket-engineer → socket-engineer | 2시간 소크 관측 — 새 환경 기준선 · 하행 재전송률 판정 |
| 2026-08-15T17:48:21+0900 | 착수 | REQ-0081 | arduino-engineer → arduino-engineer | 2시간 소크 — 장치 쪽 안정성 확인 · 저장소와 실제 칩의 불일치 재발 방지 |
| 2026-08-15T18:21:23+0900 | 발행 | REQ-0082 | root → socket-engineer | 시연 시나리오 종단 검증 — 번호판 5500 경로 · 하행 예약 30회 · 통신은 통과했으나 기능은 미검증이다 |
| 2026-08-15T18:22:01+0900 | 착수 | REQ-0082 | socket-engineer → socket-engineer | 시연 시나리오 종단 검증 — 번호판 5500 경로 · 하행 예약 30회 · 통신은 통과했으나 기능은 미검증이다 |
| 2026-08-15T18:28:22+0900 | 완료 | REQ-0080 | socket-engineer → socket-engineer | 2시간 소크 관측 — 새 환경 기준선 · 하행 재전송률 판정 |
| 2026-08-15T18:28:30+0900 | 발행 | REQ-0083 | root → socket-engineer | 서버가 아두이노를 1대만 받는다 — device_id 키 다중 연결로 바꿔라 (조원 노드 여러 대가 서로 밀어낸다) |
| 2026-08-15T18:30:01+0900 | 발행 | REQ-0084 | root → arduino-engineer | EspLink 단일 헤더 추출 — 함수 3개(begin/send/receive)로 조원이 통신을 몰라도 쓰게 하라 |
| 2026-08-15T18:30:39+0900 | 완료 | REQ-0082 | socket-engineer → socket-engineer | 시연 시나리오 종단 검증 — 번호판 5500 경로 · 하행 예약 30회 · 통신은 통과했으나 기능은 미검증이다 |
| 2026-08-15T18:32:51+0900 | 발행 | REQ-0085 | root → socket-engineer | [계획 수립] 장치는 관측만·판단은 서버 — 센서-공간 매핑을 서버 설정으로. 층 확장이 설정 변경으로 끝나야 한다 |
| 2026-08-15T18:33:15+0900 | 착수 | REQ-0083 | socket-engineer → socket-engineer | 서버가 아두이노를 1대만 받는다 — device_id 키 다중 연결로 바꿔라 (조원 노드 여러 대가 서로 밀어낸다) |
| 2026-08-15T18:38:35+0900 | 발행 | REQ-0086 | root → socket-engineer | [계획] OS별 파일 분리(PAL) — ifdef 12곳을 없애고 파일 하나에 한 OS 코드만. 시연 PC 가 윈도우다 |
| 2026-08-15T18:43:58+0900 | 완료 | REQ-0083 | socket-engineer → socket-engineer | 서버가 아두이노를 1대만 받는다 — device_id 키 다중 연결로 바꿔라 (조원 노드 여러 대가 서로 밀어낸다) |
| 2026-08-15T18:48:31+0900 | 착수 | REQ-0085 | socket-engineer → socket-engineer | [계획 수립] 장치는 관측만·판단은 서버 — 센서-공간 매핑을 서버 설정으로. 층 확장이 설정 변경으로 끝나야 한다 |
| 2026-08-15T18:48:31+0900 | 착수 | REQ-0086 | socket-engineer → socket-engineer | [계획] OS별 파일 분리(PAL) — ifdef 12곳을 없애고 파일 하나에 한 OS 코드만. 시연 PC 가 윈도우다 |
| 2026-08-15T18:51:45+0900 | 완료 | REQ-0086 | socket-engineer → socket-engineer | [계획] OS별 파일 분리(PAL) — ifdef 12곳을 없애고 파일 하나에 한 OS 코드만. 시연 PC 가 윈도우다 |
| 2026-08-15T18:51:45+0900 | 완료 | REQ-0085 | socket-engineer → socket-engineer | [계획 수립] 장치는 관측만·판단은 서버 — 센서-공간 매핑을 서버 설정으로. 층 확장이 설정 변경으로 끝나야 한다 |
| 2026-08-15T19:32:11+0900 | 완료 | REQ-0081 | arduino-engineer → arduino-engineer | 2시간 소크 — 장치 쪽 안정성 확인 · 저장소와 실제 칩의 불일치 재발 방지 |
| 2026-08-15T19:32:55+0900 | 착수 | REQ-0084 | arduino-engineer → arduino-engineer | EspLink 단일 헤더 추출 — 함수 3개(begin/send/receive)로 조원이 통신을 몰라도 쓰게 하라 |
| 2026-08-15T19:48:39+0900 | 완료 | REQ-0084 | arduino-engineer → arduino-engineer | EspLink 단일 헤더 추출 — 함수 3개(begin/send/receive)로 조원이 통신을 몰라도 쓰게 하라 |
| 2026-08-15T19:49:06+0900 | 발행 | REQ-0087 | arduino-engineer → socket-engineer | 소크 오염 사과 + 고장주입 2·3번용 조용한 창 요청 (EspLink 검증 잔여분) |
| 2026-08-15T19:51:37+0900 | 완료 | REQ-0087 | socket-engineer → socket-engineer | 소크 오염 사과 + 고장주입 2·3번용 조용한 창 요청 (EspLink 검증 잔여분) |
| 2026-08-15T20:21:19+0900 | 발행 | REQ-0088 | root → socket-engineer | 일반 하행 프레임 W + 공유 헤더 server_wire.h — 기능마다 프레임 타입을 늘리지 않는다 (첫 사용자: LCD 번호판 7자리) |
| 2026-08-15T20:24:57+0900 | 발행 | REQ-0089 | arduino-engineer → socket-engineer | server_wire.h 인터페이스 4가지만 확정해 달라 — EspLink receive() 연결 대기 중 |
| 2026-08-15T20:26:37+0900 | 발행 | REQ-0090 | socket-engineer → arduino-engineer | server_wire.h 확정 — W 프레임 디코더/구조체 정의 (REQ-0089 가 물은 넷 전부) |
| 2026-08-15T20:27:17+0900 | 완료 | REQ-0088 | socket-engineer → socket-engineer | 일반 하행 프레임 W + 공유 헤더 server_wire.h — 기능마다 프레임 타입을 늘리지 않는다 (첫 사용자: LCD 번호판 7자리) |
| 2026-08-15T20:32:40+0900 | 착수 | REQ-0090 | arduino-engineer → arduino-engineer | server_wire.h 확정 — W 프레임 디코더/구조체 정의 (REQ-0089 가 물은 넷 전부) |
| 2026-08-15T20:33:23+0900 | 완료 | REQ-0090 | arduino-engineer → arduino-engineer | server_wire.h 확정 — W 프레임 디코더/구조체 정의 (REQ-0089 가 물은 넷 전부) |
| 2026-08-15T20:36:32+0900 | 완료 | REQ-0089 | socket-engineer → socket-engineer | server_wire.h 인터페이스 4가지만 확정해 달라 — EspLink receive() 연결 대기 중 |
| 2026-08-16T10:26:31+0900 | 발행 | REQ-0091 | root → arduino-engineer | EspLink 로 client.ino 재작성 + 직접 플래싱 · 조별과제샘플/nextgen 사본 만들기 |
| 2026-08-16T10:27:19+0900 | 착수 | REQ-0091 | arduino-engineer → arduino-engineer | EspLink 로 client.ino 재작성 + 직접 플래싱 · 조별과제샘플/nextgen 사본 만들기 |
| 2026-08-16T10:44:20+0900 | 완료 | REQ-0091 | arduino-engineer → arduino-engineer | EspLink 로 client.ino 재작성 + 직접 플래싱 · 조별과제샘플/nextgen 사본 만들기 |
| 2026-08-16T10:53:40+0900 | 발행 | REQ-0092 | root → socket-engineer | 쌓인 로그 사후 분석 — 옛 펌웨어 대 EspLink 펌웨어 회귀 판정 (라이브 관측 없이) |
| 2026-08-16T10:54:31+0900 | 착수 | REQ-0092 | socket-engineer → socket-engineer | 쌓인 로그 사후 분석 — 옛 펌웨어 대 EspLink 펌웨어 회귀 판정 (라이브 관측 없이) |
| 2026-08-16T10:57:19+0900 | 완료 | REQ-0092 | socket-engineer → socket-engineer | 쌓인 로그 사후 분석 — 옛 펌웨어 대 EspLink 펌웨어 회귀 판정 (라이브 관측 없이) |
| 2026-08-16T11:06:29+0900 | 발행 | REQ-0093 | root → monitor-engineer | EspLink 펌웨어 장기 관측 — 21분 표본을 4시간 이상으로 늘려 회귀 판정을 확정하라 |
| 2026-08-16T11:06:40+0900 | 착수 | REQ-0093 | monitor-engineer → monitor-engineer | EspLink 펌웨어 장기 관측 — 21분 표본을 4시간 이상으로 늘려 회귀 판정을 확정하라 |
| 2026-08-16T11:19:30+0900 | 발행 | REQ-0094 | monitor-engineer → arduino-engineer | EspLink 빌드에 진단 시리얼 출력 추가 — 지금 장치가 끊길 때 아무것도 안 찍는다 |
| 2026-08-16T11:29:11+0900 | 완료 | REQ-0094 | arduino-engineer → arduino-engineer | EspLink 빌드에 진단 시리얼 출력 추가 — 지금 장치가 끊길 때 아무것도 안 찍는다 |
| 2026-08-16T11:36:08+0900 | 발행 | REQ-0095 | monitor-engineer → arduino-engineer | INTERVENTIONS.md 에 사용자 개입 항목 추가 — 11:15~11:26 ESP 전원차단·캐패시터 교체 |
| 2026-08-16T11:36:36+0900 | 착수 | REQ-0095 | arduino-engineer → arduino-engineer | INTERVENTIONS.md 에 사용자 개입 항목 추가 — 11:15~11:26 ESP 전원차단·캐패시터 교체 |
| 2026-08-16T11:37:36+0900 | 완료 | REQ-0095 | arduino-engineer → arduino-engineer | INTERVENTIONS.md 에 사용자 개입 항목 추가 — 11:15~11:26 ESP 전원차단·캐패시터 교체 |
| 2026-08-16T11:58:29+0900 | 발행 | REQ-0096 | root → socket-engineer | 서버 리팩터링 착수 — 주차장 도메인 / 디바이스 계층 분리 (파일·클래스 단위, 프로세스 분리는 아님) |
| 2026-08-16T11:59:04+0900 | 착수 | REQ-0096 | socket-engineer → socket-engineer | 서버 리팩터링 착수 — 주차장 도메인 / 디바이스 계층 분리 (파일·클래스 단위, 프로세스 분리는 아님) |
| 2026-08-16T12:00:39+0900 | 발행 | REQ-0097 | socket-engineer → monitor-engineer | 서버 교체 창 조율 — REQ-0096 리팩터 완료 후 1회. 네 판정창을 안 깨는 시각을 달라 |
| 2026-08-16T12:01:40+0900 | 착수 | REQ-0097 | monitor-engineer → monitor-engineer | 서버 교체 창 조율 — REQ-0096 리팩터 완료 후 1회. 네 판정창을 안 깨는 시각을 달라 |
| 2026-08-16T12:02:37+0900 | 완료 | REQ-0097 | monitor-engineer → monitor-engineer | 서버 교체 창 조율 — REQ-0096 리팩터 완료 후 1회. 네 판정창을 안 깨는 시각을 달라 |
| 2026-08-16T13:37:38+0900 | 발행 | REQ-0098 | root → root | ⏳ 예약: 15:27 판정 이후 전 에이전트 재기동 (context7 안내 반영) — 이후 작업은 이 뒤로 줄 세운다 |
| 2026-08-16T14:08:55+0900 | 진행 | REQ-0096 | root → socket-engineer | 서버 리팩터링 착수 — 주차장 도메인 / 디바이스 계층 분리 (파일·클래스 단위, 프로세스 분리는 아님) — 루트가 progress 명령 동작을 시험함 |
| 2026-08-16T15:21:28+0900 | 진행 | REQ-0096 | socket-engineer → socket-engineer | 서버 리팩터링 착수 — 주차장 도메인 / 디바이스 계층 분리 (파일·클래스 단위, 프로세스 분리는 아님) — 단계 A·B 완료(server_device.h 175행 · server_seam.h) · server.cpp 2391→2244행 · 빌드 경고0 · 자가검증/실왕복 통과 · 단계 C 규모 실측 확정(이음매 넘는 호출 5곳) · 인수인계 절 기록됨 · 운영 pid 36998 미교체(15:27 이후 예정) |
| 2026-08-16T15:23:08+0900 | 발행 | REQ-0054 | socket-engineer → root | .gitignore 44행이 리팩터링 산출물을 통째로 무시한다 — server_wire.h 도 이미 누락 중 |
| 2026-08-16T15:26:28+0900 | 발행 | REQ-0055 | root → socket-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:26:28+0900 | 발행 | REQ-0056 | root → monitor-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:26:28+0900 | 발행 | REQ-0057 | root → arduino-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:29:42+0900 | 착수 | REQ-0056 | monitor-engineer → monitor-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:32:20+0900 | 발행 | REQ-0079 | root → root | 셸치환 차단 시험 — 이 REQ 는 확인 후 삭제 대상 |
| 2026-08-16T15:33:06+0900 | 진행 | REQ-0056 | monitor-engineer → monitor-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 — 정리 완료 · 인수인계 docs/monitor-engineer/handoff-2026-08-16.md (git add 로 인덱스에 올려 보호함). ⚠ docs/ 가 안전한 이유는 경로가 아니라 추적됨이다 — 새로 쓴 인수인계는 untracked 라 또 쓸려나간다. 다른 에이전트에게도 git add 를 전하라. 4시간 판정창 완주(11:27~15:27), 14:54 시점 수치 확보: 프레임 52.95/분(옛 49.99), 세션종료 1건(기대 12.2, P≈0.007%), 바이트손상 0 대 0, 무전송 0%. 단 옛 기준선은 캐패시터 2200µF·판정창은 100µF 라 변수가 둘이다. 시리얼 원문은 재현 불가 — 결론만 숫자로 남겼다 |
| 2026-08-16T15:51:16+0900 | 착수 | REQ-0055 | socket-engineer → socket-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:53:52+0900 | 착수 | REQ-0057 | arduino-engineer → arduino-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:53:56+0900 | 완료 | REQ-0057 | arduino-engineer → arduino-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:54:01+0900 | 진행 | REQ-0078 | arduino-engineer → arduino-engineer | client.ino 플래싱까지 자율 수행 — ESP_RST_WIRED 0 복귀 · aptest 판정 버그 · 프레임 적재 확인 — 재기동 후 세션 인계. ①ESP_RST_WIRED=0 디스크에 생존(uncommitted). ②aptest.ino 사고로 삭제-차단, git show dacddfd 로 복원가능. ③④ REQ-0057(하루 더 최신)이 새 작업 동결-플래싱 미실시. 보드 연결됨, 빌드 GREEN. 루트 순서 지정 대기 |
| 2026-08-16T15:54:50+0900 | 완료 | REQ-0056 | monitor-engineer → monitor-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T15:54:59+0900 | 진행 | REQ-0055 | socket-engineer → socket-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 — 정리 완료 · 인수인계 docs/net/handoff-socket-engineer-2026-08-16.md · 🔴 server.cpp 빌드 깨짐(server_device.h 삭제, git 복구 불가) · 서버 pid 36998 은 생존 중이며 재현 불가 유일본이라 재기동 금지 |
| 2026-08-16T15:58:32+0900 | 진행 | REQ-0055 | socket-engineer → socket-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 — 정정 2건 반영: .gitignore 는 루트가 이미 수정해 둬서 선행조건 없음(초안 권고 철회) · server_device.h 재추출 계획은 중복정의 없음까지 검증 완료 |
| 2026-08-16T16:00:37+0900 | 완료 | REQ-0054 | root → root | .gitignore 44행이 리팩터링 산출물을 통째로 무시한다 — server_wire.h 도 이미 누락 중 |
| 2026-08-16T16:00:46+0900 | 완료 | REQ-0055 | root → socket-engineer | 🔴 즉시 작업 정리 — untracked 파일 대량 삭제 사고. 재기동 전 문맥을 파일로 내려라 |
| 2026-08-16T16:02:25+0900 | 발행 | REQ-0099 | root → socket-engineer | server_device.h 복구 — 원문은 없지만 생성 스크립트가 트랜스크립트에 남아 있다 (서버 재기동 금지) |
| 2026-08-16T16:02:35+0900 | 발행 | REQ-0100 | root → arduino-engineer | nextgen·EspLink·aptest 복원 — 플래싱된 실물이 /private/tmp/ngbuild 에 남아 있다 (REQ-0078 충돌 해소) |
| 2026-08-16T16:02:45+0900 | 발행 | REQ-0101 | root → monitor-engineer | monitor/ 집계 도구 9개 복원 — 트랜스크립트 재생본 있음 · tick.py 만 불완전 · 이어서 판정 재산출 |
| 2026-08-16T16:03:09+0900 | 착수 | REQ-0099 | socket-engineer → socket-engineer | server_device.h 복구 — 원문은 없지만 생성 스크립트가 트랜스크립트에 남아 있다 (서버 재기동 금지) |
| 2026-08-16T16:03:20+0900 | 착수 | REQ-0100 | arduino-engineer → arduino-engineer | nextgen·EspLink·aptest 복원 — 플래싱된 실물이 /private/tmp/ngbuild 에 남아 있다 (REQ-0078 충돌 해소) |
| 2026-08-16T16:03:21+0900 | 착수 | REQ-0101 | monitor-engineer → monitor-engineer | monitor/ 집계 도구 9개 복원 — 트랜스크립트 재생본 있음 · tick.py 만 불완전 · 이어서 판정 재산출 |
| 2026-08-16T16:06:18+0900 | 진행 | REQ-0101 | monitor-engineer → monitor-engineer | monitor/ 집계 도구 9개 복원 — 트랜스크립트 재생본 있음 · tick.py 만 불완전 · 이어서 판정 재산출 — 9개 복원+git add 완료. 교정 통과: old 구간이 15.86h/47571프레임/드롭16/종료56/errno54 12/역행7 전부 정확히 재현 — 파서 무결. 판정창 11:27~15:27 4.0h 완주 재산출 완료. 현재 tick.py 빠진 Edit 복원 중 |
| 2026-08-16T16:06:24+0900 | 발행 | REQ-0102 | arduino-engineer → socket-engineer | 참고(비긴급) — 08-15 DNS Fail 의 원인은 서버가 아니라 펌웨어의 SERVER_IP 앞 공백이었다 |
| 2026-08-16T16:06:53+0900 | 완료 | REQ-0100 | arduino-engineer → arduino-engineer | nextgen·EspLink·aptest 복원 — 플래싱된 실물이 /private/tmp/ngbuild 에 남아 있다 (REQ-0078 충돌 해소) |
| 2026-08-16T16:09:50+0900 | 발행 | REQ-0103 | monitor-engineer → arduino-engineer | 장치가 5분을 못 넘기고 반복 재부팅한다 — 팀 개입 여부 확인 + INTERVENTIONS.md 복원 |
| 2026-08-16T16:10:00+0900 | 발행 | REQ-0104 | root → arduino-engineer | nextgen 스케치가 저장소 경로 그대로는 빌드되지 않는다 — EspLink.h 배치를 정리하라 (사본은 하나) |
| 2026-08-16T16:10:05+0900 | 완료 | REQ-0099 | socket-engineer → socket-engineer | server_device.h 복구 — 원문은 없지만 생성 스크립트가 트랜스크립트에 남아 있다 (서버 재기동 금지) |
| 2026-08-16T16:10:40+0900 | 착수 | REQ-0102 | socket-engineer → socket-engineer | 참고(비긴급) — 08-15 DNS Fail 의 원인은 서버가 아니라 펌웨어의 SERVER_IP 앞 공백이었다 |
| 2026-08-16T16:11:21+0900 | 착수 | REQ-0103 | arduino-engineer → arduino-engineer | 장치가 5분을 못 넘기고 반복 재부팅한다 — 팀 개입 여부 확인 + INTERVENTIONS.md 복원 |
| 2026-08-16T16:11:35+0900 | 완료 | REQ-0102 | socket-engineer → socket-engineer | 참고(비긴급) — 08-15 DNS Fail 의 원인은 서버가 아니라 펌웨어의 SERVER_IP 앞 공백이었다 |
| 2026-08-16T16:12:46+0900 | 완료 | REQ-0101 | monitor-engineer → monitor-engineer | monitor/ 집계 도구 9개 복원 — 트랜스크립트 재생본 있음 · tick.py 만 불완전 · 이어서 판정 재산출 |
| 2026-08-16T16:14:55+0900 | 완료 | REQ-0103 | arduino-engineer → arduino-engineer | 장치가 5분을 못 넘기고 반복 재부팅한다 — 팀 개입 여부 확인 + INTERVENTIONS.md 복원 |
| 2026-08-16T16:16:07+0900 | 발행 | REQ-0105 | root → monitor-engineer | 판단 둘 회신 — out* 는 무시(인덱스 제거는 네가) · serial_tap pid 91345 는 내리지 마라 |
| 2026-08-16T16:16:31+0900 | 착수 | REQ-0105 | monitor-engineer → monitor-engineer | 판단 둘 회신 — out* 는 무시(인덱스 제거는 네가) · serial_tap pid 91345 는 내리지 마라 |
| 2026-08-16T16:17:09+0900 | 발행 | REQ-0106 | arduino-engineer → socket-engineer | 공백 버그 경계 시각 = 08-16 11:26:36 (빌드 바이너리 실측) + 44초 증상이 Uno 재부팅과 맞물릴 가능성 |
| 2026-08-16T16:17:25+0900 | 착수 | REQ-0106 | socket-engineer → socket-engineer | 공백 버그 경계 시각 = 08-16 11:26:36 (빌드 바이너리 실측) + 44초 증상이 Uno 재부팅과 맞물릴 가능성 |
| 2026-08-16T16:21:42+0900 | 착수 | REQ-0104 | arduino-engineer → arduino-engineer | nextgen 스케치가 저장소 경로 그대로는 빌드되지 않는다 — EspLink.h 배치를 정리하라 (사본은 하나) |
| 2026-08-16T16:22:42+0900 | 완료 | REQ-0104 | arduino-engineer → arduino-engineer | nextgen 스케치가 저장소 경로 그대로는 빌드되지 않는다 — EspLink.h 배치를 정리하라 (사본은 하나) |
| 2026-08-16T16:26:40+0900 | 진행 | REQ-0105 | monitor-engineer → monitor-engineer | 판단 둘 회신 — out* 는 무시(인덱스 제거는 네가) · serial_tap pid 91345 는 내리지 마라 — 완료 기준 4개 중 2개 완료·1개 차단·1개 소멸. (1) 추적 유지 확인 완료 — JUDGMENT/STATUS/TOOLS-VERIFY/도구9개 전부 인덱스에 있음. (2) 15:27 이후 붕괴 관측 완료 → monitor/FINDING-2026-08-16-uno-reboot-cause.md · 전원 쪽을 가리킴(폭주선행 0/7, 정상흐름 중 예고없이 소실 4/7 seq점프0) + socket 의 44초 사이클 = Uno 재부팅으로 교차확인. (3) 인덱스 제거 명령이 소유권 훅에 차단됨 — 훅이 실삭제와 구분하지 못한다. 루트와 동일하게 막혔고 우회하지 않았다. 규칙 수정 필요. (4) pid 91345 가 16:16 무렵 사망 — 내가 죽이지 않았고 113KB 소실. 동시에 서버 pid 36998 도 사망, 로그가 16:15:37 에 정지 → monitor/ALERT-2026-08-16-1620-observation-blind.md |
| 2026-08-16T16:28:13+0900 | 완료 | REQ-0106 | socket-engineer → socket-engineer | 공백 버그 경계 시각 = 08-16 11:26:36 (빌드 바이너리 실측) + 44초 증상이 Uno 재부팅과 맞물릴 가능성 |
| 2026-08-16T16:31:35+0900 | 발행 | REQ-0107 | socket-engineer → monitor-engineer | 서버 로그 계약 초안 v0.1 — 형식 확정 요청(날짜 없는 타임스탬프가 오늘 두 번 오독을 만들었다) |
| 2026-08-16T16:32:20+0900 | 발행 | REQ-0108 | root → monitor-engineer | 🔴 관측 데이터 오염 확정 — 15:26 이후 전부 폐기 · FINDING 철회 · 포트는 사람이 우선 |
| 2026-08-16T16:32:32+0900 | 발행 | REQ-0109 | root → arduino-engineer | 🔴 칩 위 펌웨어가 바뀌었다 — 사용자가 조별과제샘플/client.ino 로 재플래싱 · 15:30 미스터리 종결 |
| 2026-08-16T16:32:42+0900 | 착수 | REQ-0107 | monitor-engineer → monitor-engineer | 서버 로그 계약 초안 v0.1 — 형식 확정 요청(날짜 없는 타임스탬프가 오늘 두 번 오독을 만들었다) |
| 2026-08-16T16:33:10+0900 | 착수 | REQ-0109 | arduino-engineer → arduino-engineer | 🔴 칩 위 펌웨어가 바뀌었다 — 사용자가 조별과제샘플/client.ino 로 재플래싱 · 15:30 미스터리 종결 |
| 2026-08-16T16:33:42+0900 | 완료 | REQ-0107 | monitor-engineer → monitor-engineer | 서버 로그 계약 초안 v0.1 — 형식 확정 요청(날짜 없는 타임스탬프가 오늘 두 번 오독을 만들었다) |
| 2026-08-16T16:33:55+0900 | 착수 | REQ-0108 | monitor-engineer → monitor-engineer | 🔴 관측 데이터 오염 확정 — 15:26 이후 전부 폐기 · FINDING 철회 · 포트는 사람이 우선 |
| 2026-08-16T16:35:46+0900 | 완료 | REQ-0109 | arduino-engineer → arduino-engineer | 🔴 칩 위 펌웨어가 바뀌었다 — 사용자가 조별과제샘플/client.ino 로 재플래싱 · 15:30 미스터리 종결 |
| 2026-08-16T16:39:10+0900 | 완료 | REQ-0108 | monitor-engineer → monitor-engineer | 🔴 관측 데이터 오염 확정 — 15:26 이후 전부 폐기 · FINDING 철회 · 포트는 사람이 우선 |
| 2026-08-16T16:47:48+0900 | 발행 | REQ-0110 | root → arduino-engineer | 재가동 1단계 — 보드 접촉 해제 · 플래시 덤프와 기동 창 측정 · 재플래싱은 하지 마라 |
| 2026-08-16T16:47:58+0900 | 발행 | REQ-0111 | root → socket-engineer | 재가동 2단계 — 로그 계약 v0.1 먼저 넣고 서버 기동 (게이트: arduino 포트 반납) |
| 2026-08-16T16:48:09+0900 | 발행 | REQ-0112 | root → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 |
| 2026-08-16T16:48:43+0900 | 착수 | REQ-0110 | arduino-engineer → arduino-engineer | 재가동 1단계 — 보드 접촉 해제 · 플래시 덤프와 기동 창 측정 · 재플래싱은 하지 마라 |
| 2026-08-16T16:49:00+0900 | 진행 | REQ-0110 | arduino-engineer → arduino-engineer | 재가동 1단계 — 보드 접촉 해제 · 플래시 덤프와 기동 창 측정 · 재플래싱은 하지 마라 — 16:49 플래시 덤프 시작 - 선언 완료. 포트 점유 예정, monitor 는 반납 신호까지 대기 |
| 2026-08-16T16:49:00+0900 | 착수 | REQ-0111 | socket-engineer → socket-engineer | 재가동 2단계 — 로그 계약 v0.1 먼저 넣고 서버 기동 (게이트: arduino 포트 반납) |
| 2026-08-16T16:53:24+0900 | 완료 | REQ-0110 | arduino-engineer → arduino-engineer | 재가동 1단계 — 보드 접촉 해제 · 플래시 덤프와 기동 창 측정 · 재플래싱은 하지 마라 |
| 2026-08-16T16:53:46+0900 | 진행 | REQ-0111 | socket-engineer → socket-engineer | 재가동 2단계 — 로그 계약 v0.1 먼저 넣고 서버 기동 (게이트: arduino 포트 반납) — 로그 계약 v0.1 구현 완료 — 전 줄 날짜 + INSTANCE/INSTANCE-END 경계 줄 + 고정 경로(HOME/parking-logs) + 화면과 파일 동일 기록. 경고 0 빌드·자가검증·실왕복·인스턴스 분리 검증 통과. 서버는 지시대로 아직 안 올렸다 — arduino 포트 반납 신호 대기 |
