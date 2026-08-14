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
