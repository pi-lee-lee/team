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
| 2026-08-16T16:54:51+0900 | 진행 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 — 포트 미개방 유지 중(3단계 신호 대기). 포트 없이 할 수 있는 것 둘 다 완료. (1) 파서를 계약 v0.1 에 대응시킴 — 전 줄 날짜 + === INSTANCE / INSTANCE-END 경계 파싱, 날짜가 있으면 로그가 스스로 말한 날짜로 앵커링(base-date 추측 불필요), 없으면 옛 방식 그대로. 🔑 기존 5MB 로그 회귀 대조 불일치 0건 — 교정 6/6 이 걸려 있어 이게 필수였다. 자정 넘김 포함 합성 로그로 신형식 검증했고 일부러 틀린 base-date 를 줘도 로그 날짜가 이긴다. (2) 10:51:02 조사는 앞서 완료. 추가로 루트 지시 반영: tick 이 링크 끊김(재부팅없는 재연결/h·같은연결 복구/h·끊은 주체)을 1급 지표로 올리고 재부팅은 0이 정상인 예외감시로 내림. 규명된 오탐 2건(단독 K, 오염구간 재부팅)을 경고에서 제거해 이제 빨간 줄은 참인 것만 남는다 |
| 2026-08-16T16:57:23+0900 | 완료 | REQ-0111 | socket-engineer → socket-engineer | 재가동 2단계 — 로그 계약 v0.1 먼저 넣고 서버 기동 (게이트: arduino 포트 반납) |
| 2026-08-16T17:03:40+0900 | 발행 | REQ-0113 | monitor-engineer → root | 원시 시리얼 캡처가 커밋에서 CR 661개를 잃었다 — .gitattributes 에 -text 필요 |
| 2026-08-16T17:06:02+0900 | 발행 | REQ-0114 | root → arduino-engineer | 링크 끊김의 촉발 조건을 좁혀라 — send_fail_x3 은 왜 도달하는가 (보드 미접촉·읽기만) |
| 2026-08-16T17:08:44+0900 | 착수 | REQ-0114 | arduino-engineer → arduino-engineer | 링크 끊김의 촉발 조건을 좁혀라 — send_fail_x3 은 왜 도달하는가 (보드 미접촉·읽기만) |
| 2026-08-16T17:09:56+0900 | 완료 | REQ-0114 | arduino-engineer → arduino-engineer | 링크 끊김의 촉발 조건을 좁혀라 — send_fail_x3 은 왜 도달하는가 (보드 미접촉·읽기만) |
| 2026-08-16T17:20:54+0900 | 발행 | REQ-0115 | root → socket-engineer | 🔴 하행이 0건이다 — 미실행이지 건강이 아니다. 서버에서 내려 실제 도달을 재라 (지금 칩은 EspLink 아님) |
| 2026-08-16T17:21:33+0900 | 착수 | REQ-0115 | socket-engineer → socket-engineer | 🔴 하행이 0건이다 — 미실행이지 건강이 아니다. 서버에서 내려 실제 도달을 재라 (지금 칩은 EspLink 아님) |
| 2026-08-16T17:22:45+0900 | 발행 | REQ-0116 | root → arduino-engineer | 자해를 멈춰라 1단계 — busy 를 실패로 세지 않는다 · 더미 주입 중단 (구현만, 플래싱 금지) |
| 2026-08-16T17:25:29+0900 | 완료 | REQ-0115 | socket-engineer → socket-engineer | 🔴 하행이 0건이다 — 미실행이지 건강이 아니다. 서버에서 내려 실제 도달을 재라 (지금 칩은 EspLink 아님) |
| 2026-08-16T17:27:05+0900 | 착수 | REQ-0116 | arduino-engineer → arduino-engineer | 자해를 멈춰라 1단계 — busy 를 실패로 세지 않는다 · 더미 주입 중단 (구현만, 플래싱 금지) |
| 2026-08-16T17:28:01+0900 | 완료 | REQ-0116 | arduino-engineer → arduino-engineer | 자해를 멈춰라 1단계 — busy 를 실패로 세지 않는다 · 더미 주입 중단 (구현만, 플래싱 금지) |
| 2026-08-16T17:36:05+0900 | 통보 | REQ-0117 | root → root | notice 명령 동작 시험 — 확인 후 삭제 |
| 2026-08-16T17:43:22+0900 | 발행 | REQ-0118 | root → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C |
| 2026-08-16T17:44:00+0900 | 착수 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C |
| 2026-08-16T17:46:49+0900 | 진행 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C — 0번 완료: plan-windows-and-mapping.md 복구(Write 원문 + Bash 수정 2·3 순서대로 적용, fix1 은 계획서 미변경이라 제외) — server_seam.h:85 의 §5.7 포인터가 다시 유효하다. REQ-0096 단계 C 기록도 docs/net/refactor-stage-c-recovered.md 로 복구(이음매 5곳을 현재 파일에서 재확인). 다음 (A) 착수 |
| 2026-08-16T17:54:28+0900 | 진행 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C — (A)+(F) 완료·배포. pid 75781 · 17:53:07 · logfmt=3. 재연결내역(재부팅/링크재접속/미상) 과 승격전버림 카운터 추가 — 둘 다 순수 추가, 기존 칸 의미 불변. 시험에서 양방향 분류 검증(uptime 12→1 재부팅 / 203→206 링크재접속). 지표 이름 정정과 단계 C 는 별도 교체로 분리. 다음: 단계 C |
| 2026-08-16T17:55:34+0900 | 발행 | REQ-0119 | root → arduino-engineer | 도메인 원장 개설 — 오늘 확정된 것을 docs/arduino/LEDGER.md 에 심어라 |
| 2026-08-16T17:55:35+0900 | 발행 | REQ-0120 | root → socket-engineer | 도메인 원장 개설 — 오늘 확정된 것을 docs/net/LEDGER.md 에 심어라 |
| 2026-08-16T17:55:35+0900 | 발행 | REQ-0121 | root → monitor-engineer | 도메인 원장 개설 — 오늘 확정된 것을 monitor/LEDGER.md 에 심어라 |
| 2026-08-16T17:56:36+0900 | 착수 | REQ-0120 | socket-engineer → socket-engineer | 도메인 원장 개설 — 오늘 확정된 것을 docs/net/LEDGER.md 에 심어라 |
| 2026-08-16T17:56:47+0900 | 착수 | REQ-0119 | arduino-engineer → arduino-engineer | 도메인 원장 개설 — 오늘 확정된 것을 docs/arduino/LEDGER.md 에 심어라 |
| 2026-08-16T17:56:57+0900 | 착수 | REQ-0121 | monitor-engineer → monitor-engineer | 도메인 원장 개설 — 오늘 확정된 것을 monitor/LEDGER.md 에 심어라 |
| 2026-08-16T17:59:06+0900 | 완료 | REQ-0121 | monitor-engineer → monitor-engineer | 도메인 원장 개설 — 오늘 확정된 것을 monitor/LEDGER.md 에 심어라 |
| 2026-08-16T17:59:39+0900 | 완료 | REQ-0120 | socket-engineer → socket-engineer | 도메인 원장 개설 — 오늘 확정된 것을 docs/net/LEDGER.md 에 심어라 |
| 2026-08-16T17:59:47+0900 | 완료 | REQ-0119 | arduino-engineer → arduino-engineer | 도메인 원장 개설 — 오늘 확정된 것을 docs/arduino/LEDGER.md 에 심어라 |
| 2026-08-16T18:00:45+0900 | 발행 | REQ-0122 | root → web-engineer | 화면이 서버에서 무엇을 읽는지 목록화하라 — 곧 오프라인→무프레임 이름이 바뀐다 (트래픽 금지) |
| 2026-08-16T18:01:41+0900 | 착수 | REQ-0122 | web-engineer → web-engineer | 화면이 서버에서 무엇을 읽는지 목록화하라 — 곧 오프라인→무프레임 이름이 바뀐다 (트래픽 금지) |
| 2026-08-16T18:10:23+0900 | 발행 | REQ-0123 | web-engineer → socket-engineer | 개명 범위 확인: 오프라인→무프레임 이 전선 JSON 키(device.online / device_offline)까지 오는가 |
| 2026-08-16T18:11:28+0900 | 완료 | REQ-0122 | web-engineer → web-engineer | 화면이 서버에서 무엇을 읽는지 목록화하라 — 곧 오프라인→무프레임 이름이 바뀐다 (트래픽 금지) |
| 2026-08-16T18:13:38+0900 | 진행 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C — 단계 C 조각1·2 완료(시험 인스턴스 검증, 미배포): server_seam.h include + pending_events 큐 + DEV_DISCONNECT 3곳(표에 없던 send_ard 포함) + DEV_ONLINE/OFFLINE 2곳. 각 조각 경고0·자가검증·실왕복. 추가로 (A) 분류 결함 자체 발견·수정 — 되감김만 보면 '재부팅인데 공백이 길어 uptime 이 옛값을 넘는' 경우를 놓친다. 공백 G 기준으로 교체하고 재현 검증(uptime 3→8·공백21초를 재부팅으로 정확히 잡음) |
| 2026-08-16T18:14:13+0900 | 착수 | REQ-0123 | socket-engineer → socket-engineer | 개명 범위 확인: 오프라인→무프레임 이 전선 JSON 키(device.online / device_offline)까지 오는가 |
| 2026-08-16T18:14:54+0900 | 완료 | REQ-0123 | socket-engineer → socket-engineer | 개명 범위 확인: 오프라인→무프레임 이 전선 JSON 키(device.online / device_offline)까지 오는가 |
| 2026-08-16T18:31:36+0900 | 착수 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 |
| 2026-08-16T18:33:24+0900 | 완료 | REQ-0113 | root → root | 원시 시리얼 캡처가 커밋에서 CR 661개를 잃었다 — .gitattributes 에 -text 필요 |
| 2026-08-16T18:37:16+0900 | 진행 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C — 단계 C 범위 재확정 · HEAD 빌드 경고0+자가검증 통과 확인(운영 미접촉) · 답변은 docs/net/FINDING-stage-c-scope-2026-08-16.md |
| 2026-08-16T18:38:29+0900 | 통보 | REQ-0124 | monitor-engineer → root | 파서 동결(frozen-A) · 새 기준선 첫 집계 · power_plan λ 실측 반영 — tick.py 거짓 경고는 A 이후로 미룸 |
| 2026-08-16T18:39:02+0900 | 완료 | REQ-0105 | monitor-engineer → monitor-engineer | 판단 둘 회신 — out* 는 무시(인덱스 제거는 네가) · serial_tap pid 91345 는 내리지 마라 |
| 2026-08-16T18:39:19+0900 | 진행 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 — 3단계 재개 상태 확인·실체화 완료. 루트 18:33 지시(파서 동결)를 먼저 이행했다 — monitor/frozen-A/ 에 soak_stats·link_drop_probe·collapse_probe·busy_runs 4개를 판본명 parserA-2026-08-16T1838 로 동결(sha256 명세 MANIFEST.md, 시험 실행 rc=0). 넷 다 표준 라이브러리만 써서 4파일로 재현이 닫힌다. ⚠ 창 정의 충돌을 발견해 명시했다: 루트의 A창(17:53:07~21:53, 서버 I3 하나)과 내 newbase_all(16:59:54~, 서버 인스턴스 셋·교체 2회)은 다른 구간이다 — A 판정에 newbase 수치를 쓰면 안 된다. 새 기준선 첫 집계: monitor/STATUS-2026-08-16-1835-newbase.md. 핵심 넷 — (a) 재부팅 1건은 우리 tap DTR의 +12초 부팅이라 자발 재부팅 0, (b) 재부팅없는 링크끊김 4건/1.3h=3.02/h이고 끊은 쪽은 서버가 아님(유휴회수 0·keepalive 0), (c) 하행이 뒤집힘 — 옛 0=미실행이 아니라 →ARD 7건 ACK 7/7 도달(단 1건 상태필드 2, 그 시각이 재접속과 같은 초 · 표본 1건이라 기전 안 붙임), (d) λ 실측 3.02/h → 3배 개선 주장에 양팔 각 8h 필요(현재 A팔 1.56h). 미완: tick.py 가 옛 상수로 거짓 경고 2건(시리얼 경로소실·로그정지)을 내는데 A창 중 계측기 변경 금지와 겹쳐 A 이후로 미룬다. 포트·보드·서버 미접촉. |
| 2026-08-16T18:44:50+0900 | 발행 | REQ-0125 | monitor-engineer → arduino-engineer | REQ-0116 의 전제가 절반만 맞다 — 링크 끊김 기전이 둘(busy 자해 2건 / ESP 리셋 2건) · 침묵과 busy 를 갈라 세라 |
| 2026-08-16T18:45:45+0900 | 진행 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C — DEV_ACK 이관 완료(경고0+자가검증+실왕복) · 이음매1 4/6 하행대기 · 재접속 R/C 기준선 확보 · (A) 실왕복 첫 확인 · 원장 §7 추가 |
| 2026-08-16T18:46:20+0900 | 발행 | REQ-0126 | arduino-engineer → socket-engineer | TX-RESYNC 21건 전수 시각·갈래 판정 — '4:0 불일치'는 불일치가 아니다(기대값 0) · 끊김 기전은 둘 |
| 2026-08-16T18:46:22+0900 | 착수 | REQ-0125 | arduino-engineer → arduino-engineer | REQ-0116 의 전제가 절반만 맞다 — 링크 끊김 기전이 둘(busy 자해 2건 / ESP 리셋 2건) · 침묵과 busy 를 갈라 세라 |
| 2026-08-16T18:50:23+0900 | 착수 | REQ-0126 | socket-engineer → socket-engineer | TX-RESYNC 21건 전수 시각·갈래 판정 — '4:0 불일치'는 불일치가 아니다(기대값 0) · 끊김 기전은 둘 |
| 2026-08-16T18:50:23+0900 | 진행 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 — 새 기준선에서 판정 방향을 바꾸는 발견. monitor/FINDING-2026-08-16-1845-two-mechanisms.md — 링크 끊김 기전이 둘이다. 서버측 끊김과 장치측 3연속 실패가 5:5 완전 대응하는데 그 5건이 busy 자해 2건 / ESP 모듈 리셋 3건으로 갈린다(리셋은 부트배너 + CIFSR 0.0.0.0 + IP소실). 판별자를 전수 검증했다: CIFSR 0.0.0.0 이 리셋 3건과 3/3 일치하고 정상 부팅에는 안 뜸 / 부트배너는 정상 부팅에도 떠서 단독 사용시 오탐 — 내가 REQ-0125 에서 처음 보낸 근거가 이 지점에서 틀렸고 정정을 그 파일에 덧붙였다. busy 전수 20건 중 18:03 의 2건은 3진아웃에 도달하지 않아 살아남았고 리셋 3건에는 busy 가 0건 — 끊는 것은 busy 가 아니라 3진아웃 규칙이다. 🔴 창을 갈라 보니 A창(17:53:07~) 안의 2건은 둘 다 리셋형이고 자해는 0건이라, 지금 A 자료만 보면 REQ-0116 이 고칠 사건이 없다(표본 2건, 단정 아님). pre-A 와 A 는 합산 금지로 문서에 못박았다. 원장 LEDGER 3.7/3.8 정정 + 3.7-b/3.7-c 신설. 동결본 해시 재확인 일치. 실물 미접촉. |
| 2026-08-16T18:52:26+0900 | 완료 | REQ-0126 | socket-engineer → socket-engineer | TX-RESYNC 21건 전수 시각·갈래 판정 — '4:0 불일치'는 불일치가 아니다(기대값 0) · 끊김 기전은 둘 |
| 2026-08-16T18:58:56+0900 | 진행 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C — T 프레임 도구 추가(ws_probe §12A + 프레임 시각) · DEV_ACK 격리 검증 통과 → 원장 §7.1 칸 ✅ 확정 · 이음매1 4/6 하행대기 유지 |
| 2026-08-16T18:59:01+0900 | 완료 | REQ-0125 | arduino-engineer → arduino-engineer | REQ-0116 의 전제가 절반만 맞다 — 링크 끊김 기전이 둘(busy 자해 2건 / ESP 리셋 2건) · 침묵과 busy 를 갈라 세라 |
| 2026-08-16T18:59:49+0900 | 통보 | REQ-0127 | arduino-engineer → root | 2단계 소스 완료(51 PASS·미플래싱) · DEBUG=0 으로 구우면 A/B 판별이 사라진다 · SEND_OK 상한 3000→2000ms 자체 조정 |
| 2026-08-16T19:00:23+0900 | 발행 | REQ-0128 | web-engineer → socket-engineer | 브라우저 종단시험용 시험 인스턴스 기동 요청 — cwd=조별과제샘플 · 조용한 보드 · 3시간 유지 |
| 2026-08-16T19:00:41+0900 | 착수 | REQ-0128 | socket-engineer → socket-engineer | 브라우저 종단시험용 시험 인스턴스 기동 요청 — cwd=조별과제샘플 · 조용한 보드 · 3시간 유지 |
| 2026-08-16T19:03:31+0900 | 완료 | REQ-0128 | socket-engineer → socket-engineer | 브라우저 종단시험용 시험 인스턴스 기동 요청 — cwd=조별과제샘플 · 조용한 보드 · 3시간 유지 |
| 2026-08-16T19:04:27+0900 | 통보 | REQ-0129 | web-engineer → root | 화면이 서빙된 포트를 무시하고 항상 9900(운영)으로 붙던 결함을 고쳤다 — 시험 인스턴스 페이지가 운영으로 새던 경로 |
| 2026-08-16T19:05:43+0900 | 통보 | REQ-0130 | monitor-engineer → root | 🔴 DEBUG=0 으로 구우면 기전 판별이 통째로 사라진다 — 새 espResets 카운터까지 게이트 안(소스 실측) |
| 2026-08-16T19:06:50+0900 | 발행 | REQ-0131 | arduino-engineer → monitor-engineer | AT+CIPSEND → SEND OK 실측 지연 분포를 재 달라 — SEND_OK_TIMEOUT_MS 임계값 근거 (A 판정 파서 변경 아님) |
| 2026-08-16T19:06:59+0900 | 진행 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 — A 창 8시간(→01:53) 확정 반영. 도구 mech_split.py 신설·동결(frozen-A, 해시 MANIFEST 기재) — 교정(손 집계 재현)과 자정 넘김 시험 통과. 자정 처리가 없으면 00시 이후 사건이 창 앞으로 밀려 조용히 0 이 된다(A 가 01:53 종료라 필수). 🔴 내 분류가 틀린 것을 스스로 찾아 두 번째 정정: 자해/리셋 택일을 강제해 17:41:05 의 IP 소실 표지를 지웠다 → 라벨 강제 없이 표지를 그대로 내는 형태로 교체, AB.혼합 칸 신설. 판별자도 하나로는 안 됐다(배너는 정상부팅 오탐 · 0.0.0.0 은 UART 손상시 누락, 리셋이 심할수록 놓치는 방향). 정정 계수(16:59:54~18:58, 6건): 순수자해 1 · 리셋계열 4 · 혼합 1. A 창 안 3건은 전부 리셋계열. 감시기 watch_events.py 가동(기준 6건·60초·마감 02:03) — tail 최적화가 창 슬라이드로 사건을 놓치는 버그를 발견해 전체 계수로 고치고 재기동, 기준값 눈으로 확인. 🔴 새 위험 REQ-0130 통보: DEBUG=0 으로 구우면 판별 표지가 전부 사라지고 arduino 가 새로 만든 espResets 카운터(L1211·L2383)까지 게이트 안이라 리셋 수조차 못 센다. 루트에 순서 조정 요청함. 원장 LEDGER 3.7-b 교체·3.7-d·6.7 신설. 실물 미접촉. |
| 2026-08-16T19:07:58+0900 | 착수 | REQ-0131 | monitor-engineer → monitor-engineer | AT+CIPSEND → SEND OK 실측 지연 분포를 재 달라 — SEND_OK_TIMEOUT_MS 임계값 근거 (A 판정 파서 변경 아님) |
| 2026-08-16T19:12:09+0900 | 발행 | REQ-0132 | web-engineer → socket-engineer | WS 스냅샷에 last_frame_ts 를 넣어 달라 · GET /?쿼리 가 404 (둘 다 크롬 실측) |
| 2026-08-16T19:12:36+0900 | 착수 | REQ-0132 | socket-engineer → socket-engineer | WS 스냅샷에 last_frame_ts 를 넣어 달라 · GET /?쿼리 가 404 (둘 다 크롬 실측) |
| 2026-08-16T19:12:36+0900 | 진행 | REQ-0131 | monitor-engineer → monitor-engineer | AT+CIPSEND → SEND OK 실측 지연 분포를 재 달라 — SEND_OK_TIMEOUT_MS 임계값 근거 (A 판정 파서 변경 아님) — 중간 보고 완료 — monitor/MEASURE-2026-08-16-sendok-delay.md (도구 monitor/sendok_delay.py, 출력 monitor/out-A-sendok.txt). 동결과 부딪히지 않음을 확인하고 착수했다(A 판정 파이프라인 미변경, 별개 읽기전용 측정). 🔑 요청서는 내 값이 '상한'일 것으로 봤는데, 로그에 페이로드 줄이 따로 찍혀 경계가 보였다 — 상한이 아니라 펌웨어 타이머와 같은 구간(페이로드→SEND OK)을 직접 쟀다. 결과(표본 6,880): Δ=0s 99.695% · Δ=1s 0.305% · Δ≥2s 0건. 전체구간(프롬프트 대기 포함)에서 나온 Δ=2s 1건은 원문 확인 결과 2초가 전부 프롬프트 대기에 있었고 페이로드 이후는 0초라 SEND_OK_TIMEOUT_MS 구간이 아니다. 분모: 정상 6,881 · 비정상 2 · SEND OK 미도달 23(이 임계값의 존재 이유) · 비정상 최대 Δ 8초. 판단: 2000ms 는 관측 분포를 자르지 않아 '추측'에서 '실측과 모순 없음'으로 올라간다. 다만 Δ=1s 21건의 실제 경과가 (0,2)초 어디인지는 해상도 1초로 못 좁혀 여유는 검증 불가 — 여유까지 원하면 3000ms. 평균은 내지 않았다(요청자 지시대로, 없는 정밀도가 생긴다). 교정: 내가 원문으로 본 9쌍(Δ=0 여섯·Δ=1 셋)을 정확히 재현. 최종은 A 종료(01:53) 후 8시간분으로 낸다. |
| 2026-08-16T19:20:29+0900 | 통보 | REQ-0133 | web-engineer → root | 브라우저 종단 검증 수행 — 크롬 기동·시험 인스턴스 예약 왕복 3회·slots 결손 배너 추가·web 트리 신설·앞선 판단 1건 정정 |
| 2026-08-16T19:26:10+0900 | 발행 | REQ-0134 | root → web-engineer | web/artifacts 는 무시로 정했다 — 인덱스에서 빼는 것과 증거 승격은 네가 |
| 2026-08-16T19:26:47+0900 | 착수 | REQ-0134 | web-engineer → web-engineer | web/artifacts 는 무시로 정했다 — 인덱스에서 빼는 것과 증거 승격은 네가 |
| 2026-08-16T19:31:44+0900 | 완료 | REQ-0134 | web-engineer → web-engineer | web/artifacts 는 무시로 정했다 — 인덱스에서 빼는 것과 증거 승격은 네가 |
| 2026-08-16T20:34:49+0900 | 발행 | REQ-0135 | root → socket-engineer | 서버 구조 문서가 없다 — 한 장으로 쓰고 낡은 계획서에 폐기 표시해라 |
| 2026-08-16T23:29:34+0900 | 발행 | REQ-0136 | root → monitor-engineer | A 창을 21:04:50 강제 마감으로 판정하라 — 8시간이 아니라 3.20시간 (재부팅으로 탭 소실) |
| 2026-08-16T23:32:29+0900 | 착수 | REQ-0135 | socket-engineer → socket-engineer | 서버 구조 문서가 없다 — 한 장으로 쓰고 낡은 계획서에 폐기 표시해라 |
| 2026-08-16T23:32:32+0900 | 발행 | REQ-0137 | root → arduino-engineer | 보드 구성 확인 항목을 만들어라 — 오늘 밤 사용자가 실물을 본다 (관찰만·변경 금지) |
| 2026-08-16T23:32:34+0900 | 진행 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C — 21:09 새 세션이 문맥 복구 완료. 파일에서 확인한 진행: plan-windows-and-mapping.md 복구됨(17:44) · (A) 판정 결함 수정 완료(up<G, 재현 검증) · 이음ماء 1 상행 4/6 이관 완료(DEV_ACK 격리 검증까지). 남은 것은 배포뿐이고 배포는 루트 순서 대기. 오늘 밤은 실물 미접촉 작업만 진행한다. |
| 2026-08-16T23:32:43+0900 | 진행 | REQ-0132 | socket-engineer → socket-engineer | WS 스냅샷에 last_frame_ts 를 넣어 달라 · GET /?쿼리 가 404 (둘 다 크롬 실측) — 새 세션 인계. 소스 확인: 둘 다 구현돼 있고 커밋됨 — last_frame_ts 는 snapshot_json() L1203(파일 폴백과 같은 키/epoch ms/미수신시 null), 쿼리 제거는 serve_file() L1881 로 경로 판정보다 앞으로 이동. 다만 아직 grep 확인일 뿐 실왕복을 안 돌렸다. 지금부터 오프셋 시험 인스턴스로 실제 왕복을 시켜 확인한 뒤 닫는다(운영 포트 미접촉). 앞 progress 노트의 '이음ماء' 는 '이음매' 오타다. |
| 2026-08-16T23:32:58+0900 | 착수 | REQ-0137 | arduino-engineer → arduino-engineer | 보드 구성 확인 항목을 만들어라 — 오늘 밤 사용자가 실물을 본다 (관찰만·변경 금지) |
| 2026-08-16T23:33:30+0900 | 통보 | REQ-0138 | root → root | 배포 잠금(chmod a-w)을 풀었다 — A 창 종료에 따른 것 · 이제 보호가 없다 |
| 2026-08-16T23:36:48+0900 | 완료 | REQ-0137 | arduino-engineer → arduino-engineer | 보드 구성 확인 항목을 만들어라 — 오늘 밤 사용자가 실물을 본다 (관찰만·변경 금지) |
| 2026-08-16T23:37:09+0900 | 발행 | REQ-0139 | arduino-engineer → monitor-engineer | 병리 구간(프롬프트 놓침이 뒤따른 전송)의 SEND OK 분포를 재 달라 — 정상 분포로 비정상 상한을 정했다 · A 는 3.20h 분으로 |
| 2026-08-16T23:38:28+0900 | 완료 | REQ-0132 | socket-engineer → socket-engineer | WS 스냅샷에 last_frame_ts 를 넣어 달라 · GET /?쿼리 가 404 (둘 다 크롬 실측) |
| 2026-08-16T23:39:30+0900 | 통보 | REQ-0140 | socket-engineer → root | 배포 절차서를 실행하며 작성 · 시험 인스턴스 2개 기동·종료 · 문서 결함 셋(루트 파일 둘 정정 필요) |
| 2026-08-16T23:42:46+0900 | 완료 | REQ-0136 | monitor-engineer → monitor-engineer | A 창을 21:04:50 강제 마감으로 판정하라 — 8시간이 아니라 3.20시간 (재부팅으로 탭 소실) |
| 2026-08-16T23:43:31+0900 | 완료 | REQ-0135 | socket-engineer → socket-engineer | 서버 구조 문서가 없다 — 한 장으로 쓰고 낡은 계획서에 폐기 표시해라 |
| 2026-08-16T23:43:32+0900 | 통보 | REQ-0141 | monitor-engineer → root | A 판정에 딸린 도구 둘·원장 정정 넷·앞선 결론 하나 철회 (실물 미접촉) |
| 2026-08-16T23:44:49+0900 | 통보 | REQ-0142 | socket-engineer → root | 원장 LEDGER §8 신설 — 시험 도구 기본 포트가 운영 포트다(전 도메인 주의) · reason=signal 부재 · cwd 의존 |
| 2026-08-16T23:52:23+0900 | 완료 | REQ-0131 | monitor-engineer → monitor-engineer | AT+CIPSEND → SEND OK 실측 지연 분포를 재 달라 — SEND_OK_TIMEOUT_MS 임계값 근거 (A 판정 파서 변경 아님) |
| 2026-08-16T23:52:26+0900 | 착수 | REQ-0139 | monitor-engineer → monitor-engineer | 병리 구간(프롬프트 놓침이 뒤따른 전송)의 SEND OK 분포를 재 달라 — 정상 분포로 비정상 상한을 정했다 · A 는 3.20h 분으로 |
| 2026-08-16T23:52:38+0900 | 완료 | REQ-0139 | monitor-engineer → monitor-engineer | 병리 구간(프롬프트 놓침이 뒤따른 전송)의 SEND OK 분포를 재 달라 — 정상 분포로 비정상 상한을 정했다 · A 는 3.20h 분으로 |
| 2026-08-16T23:53:54+0900 | 통보 | REQ-0143 | socket-engineer → root | 시험 도구 둘 굳힘(검증 완료) · PPID=1 기동 줄 복원 · 소유권 훅이 배포 기동을 막는다(루트 결정 필요) |
| 2026-08-16T23:54:58+0900 | 발행 | REQ-0144 | root → arduino-engineer | 구워라 — 보류 해제(사용자 결정) · DEBUG=1 필수 · 보드 확인은 건너뛴다 |
| 2026-08-16T23:55:59+0900 | 발행 | REQ-0145 | web-engineer → socket-engineer | REQ-0132 마무리 확인용 시험 인스턴스 (급하지 않다 · 배포와 창 개방이 먼저) |
| 2026-08-16T23:56:52+0900 | 통보 | REQ-0146 | web-engineer → root | e2e.mjs [5] 극성 정정 + 원장 5장 신설 + 실측: 지금 파킹 서버가 하나도 안 떠 있다 |
| 2026-08-16T23:57:09+0900 | 착수 | REQ-0144 | arduino-engineer → arduino-engineer | 구워라 — 보류 해제(사용자 결정) · DEBUG=1 필수 · 보드 확인은 건너뛴다 |
| 2026-08-16T23:58:34+0900 | 진행 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 — 탭 재개는 여전히 루트 신호 대기. 그동안 '포트 없이 지금 해도 되는 것' 1번(파서를 계약에 맞추기)을 시작한다 — reason=signal/normal 공존과 logfmt 2/3 혼재를 내 파서들이 받는지 감사한다. |
| 2026-08-17T00:02:01+0900 | 완료 | REQ-0144 | arduino-engineer → arduino-engineer | 구워라 — 보류 해제(사용자 결정) · DEBUG=1 필수 · 보드 확인은 건너뛴다 |
| 2026-08-17T00:04:01+0900 | 진행 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 — 파서 감사 1차 완료(포트 미개방 유지). 인스턴스 경계 줄이 세 세대 공존(logfmt 없음/2/3, reason=signal/normal)임을 전수 확인 — soak_stats 는 KV 로 통째로 담아 안전. link_drop_probe 의 기동-직후 제외가 '소크 관측 시작' 한 줄에만 의존하고 있어(=== INSTANCE 줄은 타임스탬프가 없어 파서가 버린다) 그 문자열이 바뀌면 제외가 조용히 0 이 되는 문제를 고쳤다. A 재현 바이트 동일 + 합성 픽스처로 고친 가지가 실제로 도는 것까지 확인. ⚠ 내가 처음에 낸 진단(INSTANCE-END 오인)은 틀렸고 원장 7.18 에 둘 다 남겼다. 새 펌웨어 539ac53 의 새 줄([TX-WAIT] skip/okto, [CNT]) 대응은 RECIPE-newbaseline-t0.md §12 에 정리 — resync_gap 의 N/P 이분법이 skip 때문에 새 칩에서 깨진다는 것을 미리 적었다. |
| 2026-08-17T00:06:11+0900 | 통보 | REQ-0147 | socket-engineer → root | 배포 완료 pid=18422 build=22f788d 00:02:44 · 실왕복은 보드 미접속으로 대기 · 주입기 오분류 결함 수정 |
| 2026-08-17T00:10:42+0900 | 통보 | REQ-0148 | monitor-engineer → root | 탭 개방(창2 t0=00:04:34, DTR 리셋 1회) · 상시 요약기 PPID=1 기동 · 링크 불발 원인은 머신 IP 불일치(펌웨어 아님) |
| 2026-08-17T00:12:12+0900 | 통보 | REQ-0149 | arduino-engineer → root | 보드가 서버에 안 붙는 원인을 찾았다 — 맥 IP 가 .21 에서 .81 로 옮겨갔다(펌웨어·보드 문제 아님) · .81 판 빌드 대기 |
| 2026-08-17T00:21:29+0900 | 통보 | REQ-0150 | monitor-engineer → root | 창2 폐기 마감(00:17:13)·포트 반납 확인·serial_tap SIGTERM 결함 고침(검증 포함)·원장 7.20~7.24 |
| 2026-08-17T00:23:21+0900 | 통보 | REQ-0151 | socket-engineer → root | 주입 1건을 이미 넣었고(00:21:21) 같은 초에 링크가 끊겼다 — 주입기 정지 · 귀속 불가(n=1, 시리얼 없음) |
| 2026-08-17T00:37:03+0900 | 완료 | REQ-0118 | socket-engineer → socket-engineer | 서버 구조 작업 재개 — 잃은 계획서 복구 · (A) 재부팅 오보 결함 · 리팩터링 단계 C |
| 2026-08-17T01:27:01+0900 | 통보 | REQ-0152 | socket-engineer → root | 주입 2/2 가 같은 초에 링크를 끊었다 · 무주입 62분은 0건 · 주입기 정지(2건에서 조기 중단) |
| 2026-08-17T08:36:51+0900 | 통보 | REQ-0153 | monitor-engineer → root | 창4 인수인계 확정(관측은 PPID=1 로 계속 돈다·재기동 금지)·오늘 만든 규약/도구 목록·스스로 정정한 것 8건 |
| 2026-08-17T12:54:36+0900 | 발행 | REQ-0154 | arduino-engineer → monitor-engineer | 게이트가 포기한 뒤 얼마나 더 기다렸으면 됐나 — SEND OK 상한 값의 근거 (창4 상한초과 25건 · 읽기 전용) |
| 2026-08-17T12:56:03+0900 | 착수 | REQ-0154 | monitor-engineer → monitor-engineer | 게이트가 포기한 뒤 얼마나 더 기다렸으면 됐나 — SEND OK 상한 값의 근거 (창4 상한초과 25건 · 읽기 전용) |
| 2026-08-17T12:58:01+0900 | 완료 | REQ-0154 | monitor-engineer → monitor-engineer | 게이트가 포기한 뒤 얼마나 더 기다렸으면 됐나 — SEND OK 상한 값의 근거 (창4 상한초과 25건 · 읽기 전용) |
| 2026-08-17T13:00:24+0900 | 착수 | REQ-0145 | socket-engineer → socket-engineer | REQ-0132 마무리 확인용 시험 인스턴스 (급하지 않다 · 배포와 창 개방이 먼저) |
| 2026-08-17T13:00:32+0900 | 완료 | REQ-0145 | socket-engineer → socket-engineer | REQ-0132 마무리 확인용 시험 인스턴스 (급하지 않다 · 배포와 창 개방이 먼저) |
| 2026-08-17T18:12:08+0900 | 발행 | REQ-0155 | socket-engineer → web-engineer | WS 응답에 '접수됨(큐 대기)' 형식 합의 — 구현 말고 계약만 |
| 2026-08-17T18:14:06+0900 | 착수 | REQ-0155 | web-engineer → web-engineer | WS 응답에 '접수됨(큐 대기)' 형식 합의 — 구현 말고 계약만 |
| 2026-08-17T18:15:00+0900 | 완료 | REQ-0155 | web-engineer → web-engineer | WS 응답에 '접수됨(큐 대기)' 형식 합의 — 구현 말고 계약만 |
| 2026-08-17T19:32:31+0900 | 완료 | REQ-0112 | monitor-engineer → monitor-engineer | 재가동 3단계 — 파서를 계약에 맞추고 대기 · 신호 오면 tap 재개 · 새 기준선은 따로 연다 |
| 2026-08-17T19:33:13+0900 | 통보 | REQ-0156 | monitor-engineer → root | 재기동 상태 확인 — 관측 정지 상태(탭 17:59:07 정상종료·요약기 14:55 정지·포트 비어 있음) · 지금 잃는 자료는 없다(칩 fdtest·열린 창 없음) · 인수인계 헤더 정정 + 원장 7.96·7.97 · REQ-0112 종결 |
| 2026-08-17T19:34:44+0900 | 발행 | REQ-0157 | root → arduino-engineer | 재개 1 — 칩 상태를 먼저 기록하고 포트를 쥔다 · fdtest 3판 → rxmax(바이트) → 상행 배치 |
| 2026-08-17T19:34:54+0900 | 발행 | REQ-0158 | root → socket-engineer | 재개 2 — rxmax 대기를 해제한다(그 대기는 루트 오기) · 하행 큐+창당 1거래 · pid 46224 정체 규명 |
| 2026-08-17T19:35:01+0900 | 발행 | REQ-0159 | root → web-engineer | 재개 3 — queued 수신 처리 + expires_ms 연동(지금 착수) · 시험 인스턴스는 3번째다 |
| 2026-08-17T19:35:09+0900 | 발행 | REQ-0160 | root → monitor-engineer | 재개 4 — REQ-0112 를 대체로 닫아라 · 새 창 열지 마라(포트는 arduino) · 슬롯 이후 지표 경계만 미리 |
| 2026-08-17T19:35:28+0900 | 착수 | REQ-0157 | arduino-engineer → arduino-engineer | 재개 1 — 칩 상태를 먼저 기록하고 포트를 쥔다 · fdtest 3판 → rxmax(바이트) → 상행 배치 |
| 2026-08-17T19:35:34+0900 | 착수 | REQ-0158 | socket-engineer → socket-engineer | 재개 2 — rxmax 대기를 해제한다(그 대기는 루트 오기) · 하행 큐+창당 1거래 · pid 46224 정체 규명 |
| 2026-08-17T19:35:48+0900 | 착수 | REQ-0159 | web-engineer → web-engineer | 재개 3 — queued 수신 처리 + expires_ms 연동(지금 착수) · 시험 인스턴스는 3번째다 |
| 2026-08-17T19:36:19+0900 | 착수 | REQ-0160 | monitor-engineer → monitor-engineer | 재개 4 — REQ-0112 를 대체로 닫아라 · 새 창 열지 마라(포트는 arduino) · 슬롯 이후 지표 경계만 미리 |
| 2026-08-17T19:44:39+0900 | 진행 | REQ-0158 | socket-engineer → socket-engineer | 재개 2 — rxmax 대기를 해제한다(그 대기는 루트 오기) · 하행 큐+창당 1거래 · pid 46224 정체 규명 — ① pid 46224 = 운영 확정(offset=0·세 운영포트·운영 bin/log). 근거는 docs/net/FINDING-pid46224-2026-08-17.md. 추가: 그 바이너리 build=22f788d 는 server.cpp 기준 2커밋 뒤(b83c8cf·119d47d 없음) → MAX_LINE=64·rx= 계측 없음이라 rxmax 측정에 쓰면 안 된다. 이어서 하행 큐 구현 착수. |
| 2026-08-17T19:49:25+0900 | 완료 | REQ-0160 | monitor-engineer → monitor-engineer | 재개 4 — REQ-0112 를 대체로 닫아라 · 새 창 열지 마라(포트는 arduino) · 슬롯 이후 지표 경계만 미리 |
| 2026-08-17T19:49:45+0900 | 완료 | REQ-0159 | web-engineer → web-engineer | 재개 3 — queued 수신 처리 + expires_ms 연동(지금 착수) · 시험 인스턴스는 3번째다 |
| 2026-08-17T19:50:08+0900 | 통보 | REQ-0161 | monitor-engineer → root | 앞선 결론 정정 — 브라우저 구간 6.4배 p=0.003 은 연쇄 낱개 계수 · 접으면 p=0.072 로 유의하지 않다 · 대신 순서 지표가 남는다(리셋형 4/4 선행 하행 · Fisher 0.014) · 창4 판정문 확정 + 슬롯 지표 경계 |
| 2026-08-17T19:50:09+0900 | 진행 | REQ-0157 | arduino-engineer → arduino-engineer | 재개 1 — 칩 상태를 먼저 기록하고 포트를 쥔다 · fdtest 3판 → rxmax(바이트) → 상행 배치 — 칩 대조 완료(=cipsize, 0B차) · fdtest 4판 굽기 완료(fd4 IDENTICAL) · rxmax 3조건 측정 끝, 기록 중 |
| 2026-08-17T19:50:48+0900 | 통보 | REQ-0162 | web-engineer → root | 정정 2건(내 기동 보고 · DESIGN-slot-carrier 낡음) + 측정 도구 신규 + 원장 §5.19~§5.23 |
| 2026-08-17T19:54:41+0900 | 통보 | REQ-0163 | arduino-engineer → root | REQ-0157 부수: 리셋 6회 · 미커밋 소스가 칩에 있었음 · 계측기 변경(rxBuf 64→96) · 원장 §9 추가 |
| 2026-08-17T19:54:48+0900 | 완료 | REQ-0157 | arduino-engineer → arduino-engineer | 재개 1 — 칩 상태를 먼저 기록하고 포트를 쥔다 · fdtest 3판 → rxmax(바이트) → 상행 배치 |
| 2026-08-17T19:58:41+0900 | 발행 | REQ-0164 | root → arduino-engineer | 상행 배치 + 슬롯 표지 둘 + 운영 복귀 — 굽기 한 번에 묶는다 (사용자 지시: 모두 완료하고 플래싱) |
| 2026-08-17T19:59:05+0900 | 착수 | REQ-0164 | arduino-engineer → arduino-engineer | 상행 배치 + 슬롯 표지 둘 + 운영 복귀 — 굽기 한 번에 묶는다 (사용자 지시: 모두 완료하고 플래싱) |
| 2026-08-17T20:00:23+0900 | 진행 | REQ-0158 | socket-engineer → socket-engineer | 재개 2 — rxmax 대기를 해제한다(그 대기는 루트 오기) · 하행 큐+창당 1거래 · pid 46224 정체 규명 — 구현 끝. cap=320B(arduino 실측 540 에서 유도) · queued/ahead/expires_ms · already_pending · T/M/재전송 전부 큐 · --down-immediate/--down-cap. g++ 무경고 + selftest 통과(소켓쌍으로 전선 바이트 대조 7건 신규). 원장 §8.23 등록. 남은 것은 실물 왕복 하나 — 시험 인스턴스 신호 대기. |
| 2026-08-17T20:01:12+0900 | 통보 | REQ-0165 | socket-engineer → root | REQ-0158 범위를 넘은 것 넷 — already_pending 거동 변경 · CLI 플래그 둘 · cap 기준값 576→320 · selftest 확장 |
| 2026-08-17T20:07:32+0900 | 발행 | REQ-0166 | web-engineer → socket-engineer | queued 이탈 후 예산이 화면 타이머보다 900ms 크다 — 6.9초의 출발점을 확인해 달라 |
| 2026-08-17T20:08:24+0900 | 진행 | REQ-0164 | arduino-engineer → arduino-engineer | 상행 배치 + 슬롯 표지 둘 + 운영 복귀 — 굽기 한 번에 묶는다 (사용자 지시: 모두 완료하고 플래싱) — 창4 커밋차이 기록 완료(변경 5건 확인) · 슬롯 배치+표지 둘 구현 완료 · 빌드 통과(72%/53%) · 호스트 회귀 개정 중 |
| 2026-08-17T20:10:42+0900 | 착수 | REQ-0166 | socket-engineer → socket-engineer | queued 이탈 후 예산이 화면 타이머보다 900ms 크다 — 6.9초의 출발점을 확인해 달라 |
| 2026-08-17T20:13:48+0900 | 완료 | REQ-0166 | socket-engineer → socket-engineer | queued 이탈 후 예산이 화면 타이머보다 900ms 크다 — 6.9초의 출발점을 확인해 달라 |
| 2026-08-17T20:14:10+0900 | 발행 | REQ-0167 | root → arduino-engineer | 사용자 질문 — SoftwareSerial RX 버퍼를 늘려야 하나 · 먼저 wifi.overflow() 로 재라(호출 0건이었다) |
| 2026-08-17T20:15:13+0900 | 완료 | REQ-0164 | arduino-engineer → arduino-engineer | 상행 배치 + 슬롯 표지 둘 + 운영 복귀 — 굽기 한 번에 묶는다 (사용자 지시: 모두 완료하고 플래싱) |
| 2026-08-17T20:17:45+0900 | 통보 | REQ-0168 | socket-engineer → root | 내 시험 인스턴스가 창 A 시작과 약 3분 20초 겹쳤다 — 교락 후보는 CPU 하나. 지금 전부 내렸다 |
| 2026-08-17T20:29:10+0900 | 완료 | REQ-0167 | arduino-engineer → arduino-engineer | 사용자 질문 — SoftwareSerial RX 버퍼를 늘려야 하나 · 먼저 wifi.overflow() 로 재라(호출 0건이었다) |
| 2026-08-17T20:29:19+0900 | 통보 | REQ-0169 | socket-engineer → root | 내 커밋 418f1cc 가 21개 파일을 쓸어 갔다(CLAUDE.md 포함) — git 인덱스는 에이전트 공용이다. commit 에 경로를 줘라 |
| 2026-08-17T20:29:37+0900 | 통보 | REQ-0170 | monitor-engineer → root | 창 A 개방 t0=20:25:37(slot2·세션#4) — 표지 셋 다 실기 확인(SLOT·CNT 새 칸 넷) · 🔴 하행 자극 0 이라 판정 불가, 주입 약 13분 필요 · oow 는 충돌이 아니라 송신창 도착을 센다 |
| 2026-08-17T20:29:48+0900 | 통보 | REQ-0171 | arduino-engineer → root | 앞선 답 철회·정정(socket 하행 창 좌표계 오류) · 창 가드 결함 수정 · oow 오탐/누락 자진 신고 · 굽기 2회 · 원장 §9§10 |
| 2026-08-17T20:30:00+0900 | 완료 | REQ-0158 | socket-engineer → socket-engineer | 재개 2 — rxmax 대기를 해제한다(그 대기는 루트 오기) · 하행 큐+창당 1거래 · pid 46224 정체 규명 |
| 2026-08-17T20:36:54+0900 | 통보 | REQ-0172 | web-engineer → root | 시험 인스턴스 접속 결과(queued 실측 · e2e 36/36) + 남의 커밋이 내 워킹트리를 쓸어 담았다 |
| 2026-08-17T20:38:36+0900 | 발행 | REQ-0173 | root → socket-engineer | 창 A 에 자극을 넣어라 — 실물 장치로 하행 주입 630거래(약 13분) · 지금이 유일한 기회다 |
| 2026-08-17T20:40:09+0900 | 착수 | REQ-0173 | socket-engineer → socket-engineer | 창 A 에 자극을 넣어라 — 실물 장치로 하행 주입 630거래(약 13분) · 지금이 유일한 기회다 |
| 2026-08-17T21:06:38+0900 | 완료 | REQ-0173 | socket-engineer → socket-engineer | 창 A 에 자극을 넣어라 — 실물 장치로 하행 주입 630거래(약 13분) · 지금이 유일한 기회다 |
| 2026-08-17T21:12:10+0900 | 발행 | REQ-0174 | root → arduino-engineer | 계측기 굽기 — [SLOT-OOW] 시각을 첫 바이트로 · cksumng= 칸 · 거동 변경 없음 (창 B 전제) |
| 2026-08-17T21:12:59+0900 | 발행 | REQ-0175 | root → socket-engineer | 중단 조건 자동 판정 — 3차 재실행의 전제 · 그리고 창 B 배포 준비(신호 대기) |
| 2026-08-17T21:13:58+0900 | 착수 | REQ-0174 | arduino-engineer → arduino-engineer | 계측기 굽기 — [SLOT-OOW] 시각을 첫 바이트로 · cksumng= 칸 · 거동 변경 없음 (창 B 전제) |
| 2026-08-17T21:16:20+0900 | 완료 | REQ-0175 | socket-engineer → socket-engineer | 중단 조건 자동 판정 — 3차 재실행의 전제 · 그리고 창 B 배포 준비(신호 대기) |
| 2026-08-17T21:21:29+0900 | 완료 | REQ-0174 | arduino-engineer → arduino-engineer | 계측기 굽기 — [SLOT-OOW] 시각을 첫 바이트로 · cksumng= 칸 · 거동 변경 없음 (창 B 전제) |
| 2026-08-17T22:24:09+0900 | 발행 | REQ-0176 | root → socket-engineer | 3차 재실행 ① (inj4) — 계측기 수정 후 같은 조건 · 사용자 지시 '3차 진행' |
| 2026-08-17T22:24:30+0900 | 완료 | REQ-0176 | root → socket-engineer | 3차 재실행 ① (inj4) — 계측기 수정 후 같은 조건 · 사용자 지시 '3차 진행' |
| 2026-08-17T22:32:45+0900 | 발행 | REQ-0177 | root → monitor-engineer | 관측을 내려라 — 유휴 구간이라 판정 가치가 없다 · 00:20 재개 예약 (사용자 지시) |
| 2026-08-18T00:31:00+0900 | 발행 | REQ-0178 | root → socket-engineer | 창 B 배포 완료(build=aeb5283) — 3차 ②(inj5) 를 돌려라 · 왕복 최대 값을 arduino 에 넘겨라 |
| 2026-08-18T00:32:07+0900 | 착수 | REQ-0178 | socket-engineer → socket-engineer | 창 B 배포 완료(build=aeb5283) — 3차 ②(inj5) 를 돌려라 · 왕복 최대 값을 arduino 에 넘겨라 |
| 2026-08-18T00:33:13+0900 | 발행 | REQ-0179 | root → socket-engineer | 설계 — 브릿지 명세를 써라 (파킹↔아두이노모듈) · 노드별 큐 복제 · 전선 예산은 arduino 와 합의 |
| 2026-08-18T00:34:14+0900 | 발행 | REQ-0180 | root → arduino-engineer | 설계 — ESP 클래스 명세(init/read/write/reset) · 자리·모듈 유동화 · 전선 예산은 socket 과 합의 |
| 2026-08-18T00:35:04+0900 | 발행 | REQ-0181 | root → web-engineer | 설계 — 상태 모형을 모듈 단위로 · 격자 렌더링 구조 · 표시량은 사용자 답 대기(막지 않는다) |
| 2026-08-18T00:36:39+0900 | 발행 | REQ-0182 | root → socket-engineer | inj6 복제 회차 — 창 B 장치측 관측 누락을 메운다(루트 실수) · 왕복 751ms 파급 정리 |
| 2026-08-18T00:37:49+0900 | 착수 | REQ-0180 | arduino-engineer → arduino-engineer | 설계 — ESP 클래스 명세(init/read/write/reset) · 자리·모듈 유동화 · 전선 예산은 socket 과 합의 |
| 2026-08-18T00:38:10+0900 | 착수 | REQ-0181 | web-engineer → web-engineer | 설계 — 상태 모형을 모듈 단위로 · 격자 렌더링 구조 · 표시량은 사용자 답 대기(막지 않는다) |
| 2026-08-18T00:40:15+0900 | 진행 | REQ-0180 | arduino-engineer → arduino-engineer | 설계 — ESP 클래스 명세(init/read/write/reset) · 자리·모듈 유동화 · 전선 예산은 socket 과 합의 — 명세 착수 전. 선행 발견 정리 완료: sendAck 슬롯 우회(client.ino:1853) · ack= 가 ACK 총수 분모가 아님(큐 경유만 셈) · 굽기 목록 넷 확정. 굽기는 inj6 순서 확정 뒤 — 루트 대기 |
| 2026-08-18T00:43:47+0900 | 착수 | REQ-0179 | socket-engineer → socket-engineer | 설계 — 브릿지 명세를 써라 (파킹↔아두이노모듈) · 노드별 큐 복제 · 전선 예산은 arduino 와 합의 |
| 2026-08-18T00:45:02+0900 | 완료 | REQ-0181 | web-engineer → web-engineer | 설계 — 상태 모형을 모듈 단위로 · 격자 렌더링 구조 · 표시량은 사용자 답 대기(막지 않는다) |
| 2026-08-18T00:46:03+0900 | 발행 | REQ-0183 | web-engineer → socket-engineer | actionable·blocked_reason 을 서버가 계산해 달라 — 조합 규칙이 두 곳에 생기면 거짓 허용이 된다 |
| 2026-08-18T00:47:16+0900 | 착수 | REQ-0183 | socket-engineer → socket-engineer | actionable·blocked_reason 을 서버가 계산해 달라 — 조합 규칙이 두 곳에 생기면 거짓 허용이 된다 |
| 2026-08-18T00:53:15+0900 | 완료 | REQ-0178 | socket-engineer → socket-engineer | 창 B 배포 완료(build=aeb5283) — 3차 ②(inj5) 를 돌려라 · 왕복 최대 값을 arduino 에 넘겨라 |
| 2026-08-18T00:54:42+0900 | 완료 | REQ-0183 | socket-engineer → socket-engineer | actionable·blocked_reason 을 서버가 계산해 달라 — 조합 규칙이 두 곳에 생기면 거짓 허용이 된다 |
| 2026-08-18T00:54:55+0900 | 발행 | REQ-0184 | web-engineer → socket-engineer | 액추에이터 모듈의 상태 에코가 계약에 없다 — 화면이 켜짐을 단정하면 거짓 완료가 된다 |
| 2026-08-18T00:59:59+0900 | 착수 | REQ-0184 | socket-engineer → socket-engineer | 액추에이터 모듈의 상태 에코가 계약에 없다 — 화면이 켜짐을 단정하면 거짓 완료가 된다 |
| 2026-08-18T01:00:23+0900 | 완료 | REQ-0184 | socket-engineer → socket-engineer | 액추에이터 모듈의 상태 에코가 계약에 없다 — 화면이 켜짐을 단정하면 거짓 완료가 된다 |
| 2026-08-18T01:08:32+0900 | 완료 | REQ-0179 | socket-engineer → socket-engineer | 설계 — 브릿지 명세를 써라 (파킹↔아두이노모듈) · 노드별 큐 복제 · 전선 예산은 arduino 와 합의 |
| 2026-08-18T01:10:50+0900 | 완료 | REQ-0177 | root → monitor-engineer | 관측을 내려라 — 유휴 구간이라 판정 가치가 없다 · 00:20 재개 예약 (사용자 지시) |
| 2026-08-18T01:10:51+0900 | 완료 | REQ-0182 | root → socket-engineer | inj6 복제 회차 — 창 B 장치측 관측 누락을 메운다(루트 실수) · 왕복 751ms 파급 정리 |
| 2026-08-18T01:11:57+0900 | 발행 | REQ-0185 | root → arduino-engineer | 굽기 — sendAck 을 슬롯 창에 묶는다(창 B 판정이 지목한 원인) + 계측기 넷 · 축 개수는 네 판단 |
| 2026-08-18T01:13:09+0900 | 착수 | REQ-0185 | arduino-engineer → arduino-engineer | 굽기 — sendAck 을 슬롯 창에 묶는다(창 B 판정이 지목한 원인) + 계측기 넷 · 축 개수는 네 판단 |
| 2026-08-18T01:20:20+0900 | 완료 | REQ-0185 | arduino-engineer → arduino-engineer | 굽기 — sendAck 을 슬롯 창에 묶는다(창 B 판정이 지목한 원인) + 계측기 넷 · 축 개수는 네 판단 |
| 2026-08-18T09:01:50+0900 | 발행 | REQ-0186 | root → arduino-engineer | IP 변경 재플래싱 — SERVER_IP 192.168.0.29 (주소만) · 포트 이름도 바뀌었다 usbmodem1101 |
| 2026-08-18T09:02:35+0900 | 발행 | REQ-0187 | root → socket-engineer | 서버 재기동 — PC IP 192.168.0.29 로 변경 · 코드에 IP 가 있는지 확인 후 재기동 · inj7 예측 채점 |
| 2026-08-18T09:03:52+0900 | 착수 | REQ-0186 | arduino-engineer → arduino-engineer | IP 변경 재플래싱 — SERVER_IP 192.168.0.29 (주소만) · 포트 이름도 바뀌었다 usbmodem1101 |
| 2026-08-18T09:03:57+0900 | 발행 | REQ-0188 | root → monitor-engineer | 창 C 판정 — inj7 로 파괴 4건 남았다(예측 ① 빗나감) · ssovf 유휴 증가 · 다음 창은 망이 다르다 |
| 2026-08-18T09:05:07+0900 | 착수 | REQ-0188 | monitor-engineer → monitor-engineer | 창 C 판정 — inj7 로 파괴 4건 남았다(예측 ① 빗나감) · ssovf 유휴 증가 · 다음 창은 망이 다르다 |
| 2026-08-18T09:05:34+0900 | 완료 | REQ-0187 | socket-engineer → socket-engineer | 서버 재기동 — PC IP 192.168.0.29 로 변경 · 코드에 IP 가 있는지 확인 후 재기동 · inj7 예측 채점 |
| 2026-08-18T09:06:07+0900 | 완료 | REQ-0188 | monitor-engineer → monitor-engineer | 창 C 판정 — inj7 로 파괴 4건 남았다(예측 ① 빗나감) · ssovf 유휴 증가 · 다음 창은 망이 다르다 |
| 2026-08-18T10:02:10+0900 | 발행 | REQ-0189 | root → monitor-engineer | 창 D 개방 — 장치 복구됨(09:16:08 전원 리셋) · 새 망이라 창 A~C 와 합산 금지 · 기준선 회차 |
| 2026-08-18T10:02:43+0900 | 발행 | REQ-0190 | root → socket-engineer | inj8-pre — 새 망의 기준선 회차 · 같은 코드 다른 망 · 판별자가 이 망에서도 서는지 |
| 2026-08-18T10:04:33+0900 | 착수 | REQ-0189 | monitor-engineer → monitor-engineer | 창 D 개방 — 장치 복구됨(09:16:08 전원 리셋) · 새 망이라 창 A~C 와 합산 금지 · 기준선 회차 |
| 2026-08-18T10:19:07+0900 | 완료 | REQ-0186 | arduino-engineer → arduino-engineer | IP 변경 재플래싱 — SERVER_IP 192.168.0.29 (주소만) · 포트 이름도 바뀌었다 usbmodem1101 |
| 2026-08-18T11:23:28+0900 | 발행 | REQ-0191 | root → monitor-engineer | 기록 공백을 메우고 측정 종료를 선언하라 — 창 D 판정 · 창 E 경과 · 종료 절에 유보 다섯 |
| 2026-08-18T11:24:29+0900 | 발행 | REQ-0192 | root → arduino-engineer | 기록 공백 — cntTick 굽기 경과를 원장에 올리고 소스를 커밋하라 (착수 금지·정리만) |
| 2026-08-18T11:24:40+0900 | 착수 | REQ-0191 | monitor-engineer → monitor-engineer | 기록 공백을 메우고 측정 종료를 선언하라 — 창 D 판정 · 창 E 경과 · 종료 절에 유보 다섯 |
| 2026-08-18T11:25:07+0900 | 통보 | REQ-0193 | monitor-engineer → root | 기록 공백 메우기 — 창 D 판정(파괴 3건 · CNT 정합 3 대 3) · 창 E 개방 · 주입 두 회차 시각표 · 모르는 칸은 모른다로 표시 |
| 2026-08-18T11:25:38+0900 | 착수 | REQ-0192 | arduino-engineer → arduino-engineer | 기록 공백 — cntTick 굽기 경과를 원장에 올리고 소스를 커밋하라 (착수 금지·정리만) |
| 2026-08-18T11:26:30+0900 | 통보 | REQ-0194 | arduino-engineer → root | cntTick 굽기(slot6) 경과 · REQ-0187 번호 오기 정정 · 실물 복구는 09:16:08 사용자 전원 리셋 · 미커밋 5판 커밋 · 포트 하드코딩 정리 |
| 2026-08-18T11:26:38+0900 | 통보 | REQ-0195 | socket-engineer → root | 주입 회차 원장 정리(inj7·inj8pre·inj8) + 예측 셋 채점 · 판별자 오탐 · 어긋나는 수 하나 |
| 2026-08-18T11:26:39+0900 | 완료 | REQ-0191 | monitor-engineer → monitor-engineer | 기록 공백을 메우고 측정 종료를 선언하라 — 창 D 판정 · 창 E 경과 · 종료 절에 유보 다섯 |
| 2026-08-18T11:28:23+0900 | 완료 | REQ-0192 | arduino-engineer → arduino-engineer | 기록 공백 — cntTick 굽기 경과를 원장에 올리고 소스를 커밋하라 (착수 금지·정리만) |
| 2026-08-18T11:30:29+0900 | 완료 | REQ-0190 | socket-engineer → socket-engineer | inj8-pre — 새 망의 기준선 회차 · 같은 코드 다른 망 · 판별자가 이 망에서도 서는지 |
| 2026-08-18T11:37:33+0900 | 발행 | REQ-0196 | root → arduino-engineer | 설계 재개 — ESP 클래스 명세(REQ-0180)를 마저 써라 · cntTick 자리도 표에 넣어라 |
| 2026-08-18T11:39:37+0900 | 착수 | REQ-0196 | arduino-engineer → arduino-engineer | 설계 재개 — ESP 클래스 명세(REQ-0180)를 마저 써라 · cntTick 자리도 표에 넣어라 |
| 2026-08-18T11:40:46+0900 | 발행 | REQ-0197 | root → socket-engineer | 설계 재개 — 브릿지 명세 미결 넷을 마저 써라 · 완료 판정은 지금 답할 수 있다 |
| 2026-08-18T11:41:38+0900 | 완료 | REQ-0180 | arduino-engineer → arduino-engineer | 설계 — ESP 클래스 명세(init/read/write/reset) · 자리·모듈 유동화 · 전선 예산은 socket 과 합의 |
| 2026-08-18T11:41:38+0900 | 완료 | REQ-0196 | arduino-engineer → arduino-engineer | 설계 재개 — ESP 클래스 명세(REQ-0180)를 마저 써라 · cntTick 자리도 표에 넣어라 |
| 2026-08-18T11:42:37+0900 | 발행 | REQ-0198 | root → web-engineer | 설계 재개 — 표시량 A 확정(칸 안에 다) · §5 시각 형태를 채워라 |
| 2026-08-18T11:43:32+0900 | 착수 | REQ-0198 | web-engineer → web-engineer | 설계 재개 — 표시량 A 확정(칸 안에 다) · §5 시각 형태를 채워라 |
| 2026-08-18T11:44:03+0900 | 착수 | REQ-0197 | socket-engineer → socket-engineer | 설계 재개 — 브릿지 명세 미결 넷을 마저 써라 · 완료 판정은 지금 답할 수 있다 |
| 2026-08-18T11:46:00+0900 | 완료 | REQ-0197 | socket-engineer → socket-engineer | 설계 재개 — 브릿지 명세 미결 넷을 마저 써라 · 완료 판정은 지금 답할 수 있다 |
| 2026-08-18T11:53:37+0900 | 완료 | REQ-0198 | web-engineer → web-engineer | 설계 재개 — 표시량 A 확정(칸 안에 다) · §5 시각 형태를 채워라 |
| 2026-08-18T12:21:04+0900 | 발행 | REQ-0199 | root → socket-engineer | 구현 1 — ACK_TIMEOUT 유도식 (web 과 같은 배포) · 재전송이 파괴만 세게 만든다 |
| 2026-08-18T12:21:31+0900 | 발행 | REQ-0200 | root → web-engineer | 구현 1 — ack_budget_ms 경로 확인 · socket 과 같은 배포 (화면 개정은 4번이다) |
| 2026-08-18T12:22:35+0900 | 착수 | REQ-0200 | web-engineer → web-engineer | 구현 1 — ack_budget_ms 경로 확인 · socket 과 같은 배포 (화면 개정은 4번이다) |
| 2026-08-18T12:22:35+0900 | 착수 | REQ-0199 | socket-engineer → socket-engineer | 구현 1 — ACK_TIMEOUT 유도식 (web 과 같은 배포) · 재전송이 파괴만 세게 만든다 |
| 2026-08-18T12:22:35+0900 | 진행 | REQ-0199 | socket-engineer → socket-engineer | 구현 1 — ACK_TIMEOUT 유도식 (web 과 같은 배포) · 재전송이 파괴만 세게 만든다 — 착수. ACK_TIMEOUT 을 유도식으로 바꾼다 — rtt_max 금지(창 D 최대 1599), 상수에서 유도. ack_budget_ms 가 따라 움직이므로 새 값을 web 에 넘기고 배포 시점을 직접 맞춘다. |
| 2026-08-18T12:26:53+0900 | 완료 | REQ-0200 | web-engineer → web-engineer | 구현 1 — ack_budget_ms 경로 확인 · socket 과 같은 배포 (화면 개정은 4번이다) |
| 2026-08-18T12:28:05+0900 | 진행 | REQ-0199 | socket-engineer → socket-engineer | 구현 1 — ACK_TIMEOUT 유도식 (web 과 같은 배포) · 재전송이 파괴만 세게 만든다 — 1단계 구현 완료·커밋 c719d87. ACK_TIMEOUT 1500→2400(=2×DOWN_SLOT_MS 유도식, rtt_max 미사용). 파생 ack_budget_ms 9900→12600. selftest 44건 통과. web 확인 결과 화면 코드 변경 0 이고 예산을 캐시하지 않아 '옛 예산 구간'이 없다 — 내 배포 제약을 그렇게 정정해 원장에 반영. 추가: 기동 시 '계약값' 한 줄 로그(거동 변경 0) — web 이 짚은 '서버 값 변화를 잡을 검사가 없다'를 검사 대신 관측으로 닫은 것. 배포 명령은 루트에 넘겼고 monitor 에 재전송 대조 불가 통보 완료. 남은 것: 루트의 배포 실행 → 배포 시각·build 확인 → 그 뒤 2단계(노드별 리팩터링). |
| 2026-08-18T12:34:35+0900 | 발행 | REQ-0201 | root → monitor-engineer | 🔴 재현 관측 — 사용자가 20개 이상 누르면 ESP 리셋 · 몇 개째부터인가가 핵심 |
| 2026-08-18T12:35:01+0900 | 착수 | REQ-0201 | monitor-engineer → monitor-engineer | 🔴 재현 관측 — 사용자가 20개 이상 누르면 ESP 리셋 · 몇 개째부터인가가 핵심 |
| 2026-08-18T13:57:37+0900 | 발행 | REQ-0202 | root → web-engineer | 🔴 구현 1.5 — cmdPending 구멍 즉시 수정 (사용자가 실물 재현) · ack_budget_ms 를 써라 |
| 2026-08-18T14:27:10+0900 | 발행 | REQ-0203 | root → socket-engineer | 구현 2 준비 — 서버 노드별 리팩터링 (코드만 · 🔴 배포 금지) |
| 2026-08-18T14:27:56+0900 | 착수 | REQ-0203 | socket-engineer → socket-engineer | 구현 2 준비 — 서버 노드별 리팩터링 (코드만 · 🔴 배포 금지) |
| 2026-08-18T14:28:54+0900 | 진행 | REQ-0203 | socket-engineer → socket-engineer | 구현 2 준비 — 서버 노드별 리팩터링 (코드만 · 🔴 배포 금지) — 이행 계획 수립·커밋 1663f4f(명세 §8). 코드는 아직 한 줄도 안 고쳤다 — 의도적이다: 범위가 전역 8개·참조 약 150곳이고 반쯤 고친 server.cpp 는 배포 산출물의 소스라 없는 것보다 나쁘다. 핵심 기법: Node 안 필드를 진실로 삼고 기존 전역 이름을 참조 별칭으로 두면 150곳을 안 고쳐도 컴파일·동작이 같다(루트의 '그릇 변경'을 코드로 옮긴 것). 순서 6단계, 각 단계 끝에 selftest 44건 통과가 조건. ⚠ 다음 세션이 이어받을 때: 1단계(Node 신설 + 참조 별칭)부터 시작하고, 먼저 Server 가 어디서도 대입되지 않는지 확인할 것(참조 멤버가 대입 연산자를 지운다 — 안 되면 접근자 함수로 대체). 배포·운영 접촉·시험 인스턴스는 전부 금지 상태 유지. |
| 2026-08-18T14:36:29+0900 | 착수 | REQ-0202 | web-engineer → web-engineer | 🔴 구현 1.5 — cmdPending 구멍 즉시 수정 (사용자가 실물 재현) · ack_budget_ms 를 써라 |
| 2026-08-18T14:41:49+0900 | 진행 | REQ-0202 | web-engineer → web-engineer | 🔴 구현 1.5 — cmdPending 구멍 즉시 수정 (사용자가 실물 재현) · ack_budget_ms 를 써라 — 수정 커밋 bb04319(queued 를 두 맵에서 받는다 · 타이머를 exp+budget 으로 · queued 를 메시지·시뮬패널에 보이게) · 하니스 케이스 C 추가 커밋 32d37c9(39/0) · 사용자 검증 대기 중 · 남은 것은 원장 기록 |
| 2026-08-18T14:53:43+0900 | 발행 | REQ-0204 | root → arduino-engineer | 🔴 상행 ACK 큐가 원인이다 — ackdrop +31 · ACKQ_N=4 · 코드까지만(굽기 금지) |
| 2026-08-18T14:55:04+0900 | 착수 | REQ-0204 | arduino-engineer → arduino-engineer | 🔴 상행 ACK 큐가 원인이다 — ackdrop +31 · ACKQ_N=4 · 코드까지만(굽기 금지) |
| 2026-08-18T14:58:20+0900 | 완료 | REQ-0204 | arduino-engineer → arduino-engineer | 🔴 상행 ACK 큐가 원인이다 — ackdrop +31 · ACKQ_N=4 · 코드까지만(굽기 금지) |
| 2026-08-18T15:12:07+0900 | 발행 | REQ-0205 | root → socket-engineer | 🔴 하행 유입이 배출을 넘는다 — ackdrop=43 · 슬롯당 1거래가 지켜지는지 확인해라 (코드 금지) |
| 2026-08-18T15:16:42+0900 | 발행 | REQ-0206 | root → socket-engineer | 🔴 서버에서 유입을 막는다 — 사용자/중요 이벤트를 갈라 차단 · 문턱은 실측으로 (사용자 결정) |
| 2026-08-18T15:18:07+0900 | 착수 | REQ-0206 | socket-engineer → socket-engineer | 🔴 서버에서 유입을 막는다 — 사용자/중요 이벤트를 갈라 차단 · 문턱은 실측으로 (사용자 결정) |
| 2026-08-18T15:26:20+0900 | 발행 | REQ-0207 | root → socket-engineer | 🔴 구현 — 하행 유입 문턱 min(192B,4건) · 사용자/중요 분류 · queue_full (사용자 지시) |
| 2026-08-18T15:36:47+0900 | 완료 | REQ-0207 | socket-engineer → socket-engineer | 🔴 구현 — 하행 유입 문턱 min(192B,4건) · 사용자/중요 분류 · queue_full (사용자 지시) |
| 2026-08-18T15:37:09+0900 | 통보 | REQ-0208 | socket-engineer → root | 원장 §8.23-(43) 추가 — 건수 상한의 유도·비대칭·비교 불가 칸 둘 |
| 2026-08-18T15:44:36+0900 | 통보 | REQ-0209 | socket-engineer → root | 전제 감시 커밋(97585c2) · 미배포 — 소스가 실행 중 바이너리보다 앞서 있다 |
| 2026-08-18T15:45:15+0900 | 발행 | REQ-0210 | root → socket-engineer | 🔴 문턱 ② — 서버 송신 시점에 속도 제한 (사용자 지시) · 창 포기 경로도 지켜라 |
| 2026-08-18T15:46:18+0900 | 발행 | REQ-0211 | root → web-engineer | 🔴 문턱 ① — 화면에 미해결 건수 기준 제한 (사용자 지시) · 아이들은 유지 |
| 2026-08-18T15:47:07+0900 | 착수 | REQ-0210 | socket-engineer → socket-engineer | 🔴 문턱 ② — 서버 송신 시점에 속도 제한 (사용자 지시) · 창 포기 경로도 지켜라 |
| 2026-08-18T15:49:37+0900 | 완료 | REQ-0210 | root → socket-engineer | 🔴 문턱 ② — 서버 송신 시점에 속도 제한 (사용자 지시) · 창 포기 경로도 지켜라 |
| 2026-08-18T15:49:37+0900 | 완료 | REQ-0205 | root → socket-engineer | 🔴 하행 유입이 배출을 넘는다 — ackdrop=43 · 슬롯당 1거래가 지켜지는지 확인해라 (코드 금지) |
| 2026-08-18T15:52:39+0900 | 진행 | REQ-0210 | socket-engineer → socket-engineer | 🔴 문턱 ② — 서버 송신 시점에 속도 제한 (사용자 지시) · 창 포기 경로도 지켜라 — 루트 중단 지시로 보류. 코드 커밋 9613d60 · selftest 52건 통과 · 배포 안 함. 요구받은 값 인하는 근거 부족으로 안 함 — ack= 분포는 배출 능력이 아니라 배출량이고 능력의 하한이다. 처리 결과에 근거 기록. 재개 여부는 루트. |
| 2026-08-18T15:53:18+0900 | 발행 | REQ-0212 | root → arduino-engineer | 🔴 점검 — 송신 주기가 1.2초가 아니라 1.1초다 · 구조를 무너뜨린 수정 찾기 (사용자 지시) |
| 2026-08-18T15:54:24+0900 | 착수 | REQ-0212 | arduino-engineer → arduino-engineer | 🔴 점검 — 송신 주기가 1.2초가 아니라 1.1초다 · 구조를 무너뜨린 수정 찾기 (사용자 지시) |
| 2026-08-18T15:55:51+0900 | 완료 | REQ-0212 | arduino-engineer → arduino-engineer | 🔴 점검 — 송신 주기가 1.2초가 아니라 1.1초다 · 구조를 무너뜨린 수정 찾기 (사용자 지시) |
| 2026-08-18T15:56:53+0900 | 통보 | REQ-0213 | socket-engineer → root | CIPSEND 가설 자기 철회(실측 21971건) · monitor 로그 읽기 · 원장 (45) |
| 2026-08-18T16:22:46+0900 | 발행 | REQ-0214 | root → monitor-engineer | 🔴 오늘 장치 판정 무효 처리 — 동일 devid 카피 보드가 같은 망에 있었다 (사용자 확정) |
| 2026-08-18T16:26:26+0900 | 발행 | REQ-0215 | root → socket-engineer | 🔴 devid 충돌로 오늘 세션 분석 무효 — 접속 IP 기록 여부와 동시접속 감지를 답하라 |
| 2026-08-18T16:27:06+0900 | 발행 | REQ-0216 | root → arduino-engineer | 🔴 굽기 중지 — devid 충돌로 오늘 증상 인과가 무효. 유효·무효 경계와 devid 대응을 답하라 |
| 2026-08-18T16:27:25+0900 | 착수 | REQ-0215 | socket-engineer → socket-engineer | 🔴 devid 충돌로 오늘 세션 분석 무효 — 접속 IP 기록 여부와 동시접속 감지를 답하라 |
| 2026-08-18T16:27:47+0900 | 착수 | REQ-0214 | monitor-engineer → monitor-engineer | 🔴 오늘 장치 판정 무효 처리 — 동일 devid 카피 보드가 같은 망에 있었다 (사용자 확정) |
| 2026-08-18T16:28:22+0900 | 착수 | REQ-0216 | arduino-engineer → arduino-engineer | 🔴 굽기 중지 — devid 충돌로 오늘 증상 인과가 무효. 유효·무효 경계와 devid 대응을 답하라 |
| 2026-08-18T16:30:07+0900 | 발행 | REQ-0217 | root → socket-engineer | 🔴 원인 확정: 같은 devid 재접속이 살아있는 세션을 끊는다(38건) — 생사 확인 후 거절·IP 로깅·카운터 분리, 배포까지 |
| 2026-08-18T16:31:26+0900 | 완료 | REQ-0214 | monitor-engineer → monitor-engineer | 🔴 오늘 장치 판정 무효 처리 — 동일 devid 카피 보드가 같은 망에 있었다 (사용자 확정) |
| 2026-08-18T16:35:51+0900 | 발행 | REQ-0218 | root → arduino-engineer | 🔴 SEND OK 손실을 실측했다 — 바닥 1%, 48B 이상에서 2~4배. pendLine/잘림 대비를 만들어라 |
| 2026-08-18T16:36:04+0900 | 착수 | REQ-0218 | arduino-engineer → arduino-engineer | 🔴 SEND OK 손실을 실측했다 — 바닥 1%, 48B 이상에서 2~4배. pendLine/잘림 대비를 만들어라 |
| 2026-08-18T16:38:23+0900 | 완료 | REQ-0217 | socket-engineer → socket-engineer | 🔴 원인 확정: 같은 devid 재접속이 살아있는 세션을 끊는다(38건) — 생사 확인 후 거절·IP 로깅·카운터 분리, 배포까지 |
| 2026-08-18T16:39:55+0900 | 완료 | REQ-0218 | arduino-engineer → arduino-engineer | 🔴 SEND OK 손실을 실측했다 — 바닥 1%, 48B 이상에서 2~4배. pendLine/잘림 대비를 만들어라 |
| 2026-08-18T16:41:02+0900 | 통보 | REQ-0219 | monitor-engineer → root | 창 G 관측 재개(포트 점유 16:39:44) · 사전등록 t0 정정 · 앞선 판정 둘 철회 |
| 2026-08-18T16:55:54+0900 | 통보 | REQ-0220 | monitor-engineer → root | 창 G 기준선 확정 · 굽기 판단 발신 · 원장 7.125~7.130(대조군은 성공한 쪽에 있다) |
| 2026-08-18T16:56:40+0900 | 통보 | REQ-0221 | socket-engineer → root | 판별 시험 주입 16:55:09 (큰 프레임 축과 하행 축을 가른다) · 원장 (47) |
| 2026-08-18T18:54:10+0900 | 발행 | REQ-0222 | root → arduino-engineer | 🔴 이동 완료 — 망 상수 셋 교체 후 재굽기 (SERVER_IP 192.168.35.21 · SSID SK_WiFiGIGA50DC_2.4G) |
| 2026-08-18T18:54:25+0900 | 착수 | REQ-0222 | arduino-engineer → arduino-engineer | 🔴 이동 완료 — 망 상수 셋 교체 후 재굽기 (SERVER_IP 192.168.35.21 · SSID SK_WiFiGIGA50DC_2.4G) |
| 2026-08-18T18:58:38+0900 | 완료 | REQ-0222 | arduino-engineer → arduino-engineer | 🔴 이동 완료 — 망 상수 셋 교체 후 재굽기 (SERVER_IP 192.168.35.21 · SSID SK_WiFiGIGA50DC_2.4G) |
| 2026-08-18T18:58:48+0900 | 완료 | REQ-0215 | root → socket-engineer | 🔴 devid 충돌로 오늘 세션 분석 무효 — 접속 IP 기록 여부와 동시접속 감지를 답하라 |
| 2026-08-18T18:58:48+0900 | 완료 | REQ-0216 | root → arduino-engineer | 🔴 굽기 중지 — devid 충돌로 오늘 증상 인과가 무효. 유효·무효 경계와 devid 대응을 답하라 |
| 2026-08-18T18:58:48+0900 | 완료 | REQ-0189 | root → monitor-engineer | 창 D 개방 — 장치 복구됨(09:16:08 전원 리셋) · 새 망이라 창 A~C 와 합산 금지 · 기준선 회차 |
| 2026-08-18T18:58:48+0900 | 완료 | REQ-0201 | root → monitor-engineer | 🔴 재현 관측 — 사용자가 20개 이상 누르면 ESP 리셋 · 몇 개째부터인가가 핵심 |
| 2026-08-18T19:00:33+0900 | 완료 | REQ-0199 | socket-engineer → socket-engineer | 구현 1 — ACK_TIMEOUT 유도식 (web 과 같은 배포) · 재전송이 파괴만 세게 만든다 |
| 2026-08-18T19:00:40+0900 | 완료 | REQ-0206 | socket-engineer → socket-engineer | 🔴 서버에서 유입을 막는다 — 사용자/중요 이벤트를 갈라 차단 · 문턱은 실측으로 (사용자 결정) |
| 2026-08-18T19:00:47+0900 | 진행 | REQ-0203 | socket-engineer → socket-engineer | 구현 2 준비 — 서버 노드별 리팩터링 (코드만 · 🔴 배포 금지) — 미착수. 열어 둔다 — 고장 대응이 우선순위를 가져갔다. ⚠ 단독 배포여야 한다(파일 전체 구조 변경이라 다른 수정과 섞이면 원인을 못 가른다). 선행 조건이 하나 늘었다: 등록 D 프레임의 배출률 선언 + S 비트열 hex 인코딩 명세(대소문자/고정폭/비트순서/패딩 0 검증)를 먼저 확정하고 코드를 만진다. |
| 2026-08-18T19:01:23+0900 | 완료 | REQ-0202 | web-engineer → web-engineer | 🔴 구현 1.5 — cmdPending 구멍 즉시 수정 (사용자가 실물 재현) · ack_budget_ms 를 써라 |
| 2026-08-18T19:01:32+0900 | 완료 | REQ-0211 | web-engineer → web-engineer | 🔴 문턱 ① — 화면에 미해결 건수 기준 제한 (사용자 지시) · 아이들은 유지 |
| 2026-08-18T19:02:18+0900 | 통보 | REQ-0223 | web-engineer → root | 지시 없이 한 원장 갱신 — queue_full 이 정체 신호라는 산술 · 하니스 맹점 셋 · 시각 형태 방법 |
| 2026-08-18T19:11:09+0900 | 통보 | REQ-0224 | arduino-engineer → root | 이동 후 재굽기 완료 · 진단 중 포트 개입 5회 · 앞선 '보드 무응답' 결론 철회 |
| 2026-08-18T19:55:54+0900 | 발행 | REQ-0225 | root → socket-engineer | 🔴 설계 재개 — 전선 명세 확정(D 배출률 · S hex 인코딩) + 확장 구조 이름을 먼저 박아라 |
| 2026-08-18T19:56:13+0900 | 착수 | REQ-0225 | socket-engineer → socket-engineer | 🔴 설계 재개 — 전선 명세 확정(D 배출률 · S hex 인코딩) + 확장 구조 이름을 먼저 박아라 |
| 2026-08-18T19:59:30+0900 | 완료 | REQ-0225 | socket-engineer → socket-engineer | 🔴 설계 재개 — 전선 명세 확정(D 배출률 · S hex 인코딩) + 확장 구조 이름을 먼저 박아라 |
| 2026-08-18T20:03:56+0900 | 발행 | REQ-0226 | web-engineer → socket-engineer | 화면이 받는 JSON 봉투가 명세에 없다 — 타입·키 이름과 언제 오나 (지어내지 않으려고 묻는다) |
| 2026-08-18T20:05:34+0900 | 착수 | REQ-0226 | socket-engineer → socket-engineer | 화면이 받는 JSON 봉투가 명세에 없다 — 타입·키 이름과 언제 오나 (지어내지 않으려고 묻는다) |
| 2026-08-18T20:06:04+0900 | 완료 | REQ-0226 | socket-engineer → socket-engineer | 화면이 받는 JSON 봉투가 명세에 없다 — 타입·키 이름과 언제 오나 (지어내지 않으려고 묻는다) |
| 2026-08-19T00:23:35+0900 | 발행 | REQ-0227 | root → arduino-engineer | 🔴 가상 O* 모듈을 펌웨어에 만든다 — 조작 사슬이 한 번도 안 돌았다 (사용자 지시) |
| 2026-08-19T00:24:32+0900 | 착수 | REQ-0227 | arduino-engineer → arduino-engineer | 🔴 가상 O* 모듈을 펌웨어에 만든다 — 조작 사슬이 한 번도 안 돌았다 (사용자 지시) |
| 2026-08-19T00:41:25+0900 | 진행 | REQ-0227 | arduino-engineer → arduino-engineer | 🔴 가상 O* 모듈을 펌웨어에 만든다 — 조작 사슬이 한 번도 안 돌았다 (사용자 지시) — 구현·시험 완료(235 PASS) · 굽기 전 사용자 리미트로 정지 · 칩은 def8093 그대로 |
| 2026-08-19T05:03:10+0900 | 발행 | REQ-0228 | socket-engineer → arduino-engineer | 굽기 전 답이 필요한 것 둘 — 조작 완료 신호를 무엇으로 싣나 / 세션 유지 중 재등록을 하나 |
| 2026-08-19T05:05:04+0900 | 완료 | REQ-0228 | arduino-engineer → arduino-engineer | 굽기 전 답이 필요한 것 둘 — 조작 완료 신호를 무엇으로 싣나 / 세션 유지 중 재등록을 하나 |
| 2026-08-19T05:10:02+0900 | 통보 | REQ-0229 | web-engineer → root | 승인 범위 밖 수정 1건 — renderZoneGrid 포커스 보존 (②의 전제 조건) |
| 2026-08-19T05:17:14+0900 | 발행 | REQ-0230 | web-engineer → socket-engineer | 장치 노드 재접속 유도 1회 — node_unregistered 실물 관측용 |
| 2026-08-19T05:22:37+0900 | 통보 | REQ-0231 | socket-engineer → root | 시험 인스턴스를 a3cbf40 으로 재기동 — web 의 등록 창 관측을 위해 |
| 2026-08-19T05:38:24+0900 | 발행 | REQ-0232 | web-engineer → socket-engineer | 완료 판정 어휘가 둘이다 — §3.5 action_state 대 §7.2.1 completion, 정본을 정해 달라 |
| 2026-08-19T05:41:32+0900 | 착수 | REQ-0232 | socket-engineer → socket-engineer | 완료 판정 어휘가 둘이다 — §3.5 action_state 대 §7.2.1 completion, 정본을 정해 달라 |
| 2026-08-19T05:42:00+0900 | 완료 | REQ-0232 | socket-engineer → socket-engineer | 완료 판정 어휘가 둘이다 — §3.5 action_state 대 §7.2.1 completion, 정본을 정해 달라 |
| 2026-08-19T05:55:39+0900 | 통보 | REQ-0233 | socket-engineer → root | 모의 노드에 G 거절 갈래 추가 — 실물이 못 만드는 상태를 도구가 만든다 |
| 2026-08-19T05:55:53+0900 | 착수 | REQ-0230 | socket-engineer → socket-engineer | 장치 노드 재접속 유도 1회 — node_unregistered 실물 관측용 |
| 2026-08-19T05:56:17+0900 | 완료 | REQ-0230 | socket-engineer → socket-engineer | 장치 노드 재접속 유도 1회 — node_unregistered 실물 관측용 |
| 2026-08-19T06:05:14+0900 | 통보 | REQ-0234 | web-engineer → root | 모의 노드를 직접 띄웠다 — 차단봉 거절 갈래(result=3) 검증용, 시험 인스턴스 120초 |
| 2026-08-19T06:15:10+0900 | 발행 | REQ-0235 | web-engineer → socket-engineer | 점유된 자리에 reserve.ok=true 가 온다 — 의도인가 결함인가 |
| 2026-08-19T06:19:45+0900 | 착수 | REQ-0235 | socket-engineer → socket-engineer | 점유된 자리에 reserve.ok=true 가 온다 — 의도인가 결함인가 |
| 2026-08-19T06:20:12+0900 | 완료 | REQ-0235 | socket-engineer → socket-engineer | 점유된 자리에 reserve.ok=true 가 온다 — 의도인가 결함인가 |
| 2026-08-19T06:52:58+0900 | 완료 | REQ-0227 | arduino-engineer → arduino-engineer | 🔴 가상 O* 모듈을 펌웨어에 만든다 — 조작 사슬이 한 번도 안 돌았다 (사용자 지시) |
| 2026-08-19T06:53:24+0900 | 발행 | REQ-0236 | root → socket-engineer | b0d58d9 단독 배포 — 관측이 멈춘 지금이 적기다. 그 다음 A[1] 코드만 |
| 2026-08-19T06:53:33+0900 | 통보 | REQ-0237 | arduino-engineer → root | 🔴 정정 — 05:37:03 굽기의 장치 축은 둘이었다 (espInit 이 같이 구워졌다) |
| 2026-08-19T06:54:04+0900 | 발행 | REQ-0238 | root → monitor-engineer | 창 O — b0d58d9 배포 뒤에 열어라. A[1] 대조군(>=64B 진입률 + rid 자릿수)이 목적이다 |
| 2026-08-19T06:55:39+0900 | 발행 | REQ-0239 | root → monitor-engineer | 창 L·M 축 재귀속 — 05:37:03 굽기의 장치 축이 둘이었다 (arduino REQ-0237 철회) |
| 2026-08-19T06:55:55+0900 | 착수 | REQ-0236 | socket-engineer → socket-engineer | b0d58d9 단독 배포 — 관측이 멈춘 지금이 적기다. 그 다음 A[1] 코드만 |
| 2026-08-19T06:56:09+0900 | 착수 | REQ-0239 | monitor-engineer → monitor-engineer | 창 L·M 축 재귀속 — 05:37:03 굽기의 장치 축이 둘이었다 (arduino REQ-0237 철회) |
| 2026-08-19T07:00:40+0900 | 완료 | REQ-0236 | socket-engineer → socket-engineer | b0d58d9 단독 배포 — 관측이 멈춘 지금이 적기다. 그 다음 A[1] 코드만 |
| 2026-08-19T07:03:02+0900 | 완료 | REQ-0203 | socket-engineer → socket-engineer | 구현 2 준비 — 서버 노드별 리팩터링 (코드만 · 🔴 배포 금지) |
| 2026-08-19T07:03:14+0900 | 완료 | REQ-0239 | monitor-engineer → monitor-engineer | 창 L·M 축 재귀속 — 05:37:03 굽기의 장치 축이 둘이었다 (arduino REQ-0237 철회) |
| 2026-08-19T07:06:07+0900 | 발행 | REQ-0240 | root → web-engineer | 🔴 :9900 이 내주는 index.html 이 a3922e0(08-17) 판이다 — 배포 단계 누락인가 유물인가 |
| 2026-08-19T07:06:21+0900 | 착수 | REQ-0240 | web-engineer → web-engineer | 🔴 :9900 이 내주는 index.html 이 a3922e0(08-17) 판이다 — 배포 단계 누락인가 유물인가 |
| 2026-08-19T07:12:32+0900 | 통보 | REQ-0241 | arduino-engineer → root | 🔴 철회 — '.burn/client 추적 해제'는 거짓이었다 + git commit -- <경로> 가 삭제를 못 싣는다 |
| 2026-08-19T07:12:44+0900 | 통보 | REQ-0242 | monitor-engineer → root | 창 O 개방 · rid_width.py 신설·교정 · cksumng 후보(rid 자릿수) 사망 |
| 2026-08-19T07:14:56+0900 | 착수 | REQ-0238 | monitor-engineer → monitor-engineer | 창 O — b0d58d9 배포 뒤에 열어라. A[1] 대조군(>=64B 진입률 + rid 자릿수)이 목적이다 |
| 2026-08-19T07:15:49+0900 | 발행 | REQ-0243 | web-engineer → socket-engineer | 배포 절차서 §0.5.1 의 index.html 확인이 낡은 사본을 통과시킨다 — 존재형을 내용대조로 |
| 2026-08-19T07:16:04+0900 | 발행 | REQ-0244 | root → socket-engineer | 창 O 주입 개시 — 창 M 과 같은 규약으로, >=64B 300건에서 정지 |
| 2026-08-19T07:16:09+0900 | 통보 | REQ-0245 | socket-engineer → root | A[1] rid 폭 고정(1000)+격리 코드 완료 · 미배포 · 자가검증이 설계 결함 하나를 잡았다 |
| 2026-08-19T07:16:34+0900 | 진행 | REQ-0238 | monitor-engineer → monitor-engineer | 창 O — b0d58d9 배포 뒤에 열어라. A[1] 대조군(>=64B 진입률 + rid 자릿수)이 목적이다 — 창 O 관측 중(t0 07:05:57) · 감시기 가동 · 07:10:54 링크 11초 단절 기록(귀속 안 함) · >=64B 분모 2/300 이라 자극 없이는 ②를 못 냄 — 주입 순서를 루트에 물어 둠 |
| 2026-08-19T07:17:36+0900 | 완료 | REQ-0240 | web-engineer → web-engineer | 🔴 :9900 이 내주는 index.html 이 a3922e0(08-17) 판이다 — 배포 단계 누락인가 유물인가 |
| 2026-08-19T07:18:31+0900 | 발행 | REQ-0246 | root → socket-engineer | A[1] 결정 둘 — 재시작 충돌은 (B) 디스크 커서 · ACK 자리는 주석이 옳으나 [3]으로 미룬다 |
| 2026-08-19T07:18:46+0900 | 통보 | REQ-0247 | web-engineer → root | 원장 §5.85 — 화면은 서버 cwd 를 따라간다 · '존재형 검사는 유물을 통과시킨다' |
| 2026-08-19T07:19:48+0900 | 착수 | REQ-0244 | socket-engineer → socket-engineer | 창 O 주입 개시 — 창 M 과 같은 규약으로, >=64B 300건에서 정지 |
| 2026-08-19T07:23:49+0900 | 발행 | REQ-0248 | root → socket-engineer | A[1][2] 뒤 목록 — 화면 계수 이름·정의 · screen-build 로그 · cwd 상대경로 |
| 2026-08-19T07:26:04+0900 | 착수 | REQ-0243 | socket-engineer → socket-engineer | 배포 절차서 §0.5.1 의 index.html 확인이 낡은 사본을 통과시킨다 — 존재형을 내용대조로 |
| 2026-08-19T07:27:24+0900 | 완료 | REQ-0243 | socket-engineer → socket-engineer | 배포 절차서 §0.5.1 의 index.html 확인이 낡은 사본을 통과시킨다 — 존재형을 내용대조로 |
| 2026-08-19T07:28:34+0900 | 착수 | REQ-0246 | socket-engineer → socket-engineer | A[1] 결정 둘 — 재시작 충돌은 (B) 디스크 커서 · ACK 자리는 주석이 옳으나 [3]으로 미룬다 |
| 2026-08-19T07:28:34+0900 | 완료 | REQ-0246 | socket-engineer → socket-engineer | A[1] 결정 둘 — 재시작 충돌은 (B) 디스크 커서 · ACK 자리는 주석이 옳으나 [3]으로 미룬다 |
| 2026-08-19T07:30:12+0900 | 통보 | REQ-0249 | web-engineer → root | 철회 — REQ-0247 의 cwd 3회 변경 추정은 반증됐다 (원장 5.85.1) · 결론은 안 바뀐다 |
| 2026-08-19T07:33:59+0900 | 완료 | REQ-0244 | socket-engineer → socket-engineer | 창 O 주입 개시 — 창 M 과 같은 규약으로, >=64B 300건에서 정지 |
| 2026-08-19T07:34:03+0900 | 통보 | REQ-0250 | web-engineer → root | 하니스 6종에 판본 대조 일괄 적용 — 넷은 가드가 아예 없었다 (원장 5.85.2) · GET 1회 포함 |
| 2026-08-19T07:37:31+0900 | 완료 | REQ-0238 | monitor-engineer → monitor-engineer | 창 O — b0d58d9 배포 뒤에 열어라. A[1] 대조군(>=64B 진입률 + rid 자릿수)이 목적이다 |
| 2026-08-19T09:58:39+0900 | 발행 | REQ-0251 | root → socket-engineer | A[1] 셋 배포 — 이동 후 새 IP 192.168.0.29 확인 포함. 관측이 멈춘 지금이 적기 |
| 2026-08-19T09:58:39+0900 | 발행 | REQ-0252 | root → arduino-engineer | 이동 굽기 — SSID 3F_302 · 새 IP 192.168.0.29 · 지금 칩은 .basefw 다 |
| 2026-08-19T09:59:07+0900 | 착수 | REQ-0251 | socket-engineer → socket-engineer | A[1] 셋 배포 — 이동 후 새 IP 192.168.0.29 확인 포함. 관측이 멈춘 지금이 적기 |
| 2026-08-19T09:59:35+0900 | 착수 | REQ-0252 | arduino-engineer → arduino-engineer | 이동 굽기 — SSID 3F_302 · 새 IP 192.168.0.29 · 지금 칩은 .basefw 다 |
| 2026-08-19T10:01:41+0900 | 완료 | REQ-0251 | socket-engineer → socket-engineer | A[1] 셋 배포 — 이동 후 새 IP 192.168.0.29 확인 포함. 관측이 멈춘 지금이 적기 |
| 2026-08-19T10:02:19+0900 | 진행 | REQ-0252 | arduino-engineer → arduino-engineer | 이동 굽기 — SSID 3F_302 · 새 IP 192.168.0.29 · 지금 칩은 .basefw 다 — 값 셋 수정·검증·빌드·237 PASS 완료(2187cc3). 축 목록을 커밋 범위로 확정(8b63846..HEAD, 33커밋). 굽기는 socket 배포 대기 |
| 2026-08-19T10:03:36+0900 | 발행 | REQ-0253 | root → monitor-engineer | 창 P — A[1] 판정. 굽기 리셋을 t0 로. 🔴 축이 셋(A[1]·망 이동·칩 전체 교체) |
| 2026-08-19T10:05:09+0900 | 착수 | REQ-0253 | monitor-engineer → monitor-engineer | 창 P — A[1] 판정. 굽기 리셋을 t0 로. 🔴 축이 셋(A[1]·망 이동·칩 전체 교체) |
| 2026-08-19T10:18:11+0900 | 완료 | REQ-0252 | arduino-engineer → arduino-engineer | 이동 굽기 — SSID 3F_302 · 새 IP 192.168.0.29 · 지금 칩은 .basefw 다 |
| 2026-08-19T10:19:05+0900 | 발행 | REQ-0254 | root → socket-engineer | 창 P 주입 — 창 M·O 와 같은 규약. rid 4자리=0 이 A[1] 1차 판별자 |
| 2026-08-19T10:21:00+0900 | 통보 | REQ-0255 | arduino-engineer → root | 🔴 굽기 뒤 포트 개입 넷을 기록 없이 했다 — socket 의 세션 절단 2건이 그것이었다 |
| 2026-08-19T10:21:02+0900 | 발행 | REQ-0256 | root → monitor-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T10:21:02+0900 | 발행 | REQ-0257 | root → socket-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T10:21:02+0900 | 발행 | REQ-0258 | root → arduino-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T10:21:02+0900 | 발행 | REQ-0259 | root → web-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T10:21:04+0900 | 착수 | REQ-0254 | socket-engineer → socket-engineer | 창 P 주입 — 창 M·O 와 같은 규약. rid 4자리=0 이 A[1] 1차 판별자 |
| 2026-08-19T10:29:23+0900 | 발행 | REQ-0260 | root → socket-engineer | 🔴 가상 아두이노(둘째 노드) — 자리 추가를 시험 가능하게. 3b(map<devid,Node>)가 전제다 (사용자 지시) |
| 2026-08-19T10:44:04+0900 | 발행 | REQ-0261 | root → web-engineer | 존 안에 노드 박스 — 박스 안의 박스로 노드ID와 보유 모듈을 명시 (사용자 지시) |
| 2026-08-19T10:44:25+0900 | 완료 | REQ-0253 | monitor-engineer → monitor-engineer | 창 P — A[1] 판정. 굽기 리셋을 t0 로. 🔴 축이 셋(A[1]·망 이동·칩 전체 교체) |
| 2026-08-19T10:45:46+0900 | 발행 | REQ-0262 | root → socket-engineer | state 에 모듈별 값을 실어라 — 화면 상태등의 원천 (사용자 확정: 주차확인센서와 같은 구조) |
| 2026-08-19T10:47:15+0900 | 착수 | REQ-0261 | web-engineer → web-engineer | 존 안에 노드 박스 — 박스 안의 박스로 노드ID와 보유 모듈을 명시 (사용자 지시) |
| 2026-08-19T10:55:15+0900 | 완료 | REQ-0261 | web-engineer → web-engineer | 존 안에 노드 박스 — 박스 안의 박스로 노드ID와 보유 모듈을 명시 (사용자 지시) |
| 2026-08-19T10:57:58+0900 | 발행 | REQ-0263 | root → socket-engineer | 🔴 노드 통신 경로를 별도 클래스로 추출하고 잠근다 — 빈번한 수정이 안정화된 영역을 건드린다 (사용자 지시) |
| 2026-08-19T11:00:42+0900 | 발행 | REQ-0264 | root → arduino-engineer | 🔴 펌웨어 ESP/WiFi 링크 계층 분리 — hex 차이 0 을 노려라. 굽기 입력 불변식이 깨진다 (사용자 지시) |
| 2026-08-19T11:02:06+0900 | 착수 | REQ-0264 | arduino-engineer → arduino-engineer | 🔴 펌웨어 ESP/WiFi 링크 계층 분리 — hex 차이 0 을 노려라. 굽기 입력 불변식이 깨진다 (사용자 지시) |
| 2026-08-19T11:11:03+0900 | 완료 | REQ-0264 | arduino-engineer → arduino-engineer | 🔴 펌웨어 ESP/WiFi 링크 계층 분리 — hex 차이 0 을 노려라. 굽기 입력 불변식이 깨진다 (사용자 지시) |
| 2026-08-19T11:16:39+0900 | 발행 | REQ-0265 | root → arduino-engineer | 펌웨어 소스를 조별과제샘플/ardu/ 로 이사 — hex 0 을 유지해라 (사용자 지시) |
| 2026-08-19T11:16:39+0900 | 발행 | REQ-0266 | root → socket-engineer | 서버 소스를 조별과제샘플/server/ 로 이사 — ②-b 커밋 뒤 온전한 지점에서 (사용자 지시) |
| 2026-08-19T11:18:31+0900 | 발행 | REQ-0267 | root → web-engineer | 화면 소스를 조별과제샘플/web/ 로 이사 — 도구 경로 전수 갱신 (사용자 지시) |
| 2026-08-19T11:22:14+0900 | 발행 | REQ-0268 | root → web-engineer | 모듈에 인덱스를 항상 표시 — 주차확인센서 10개가 다 같은 라벨이다 (사용자 지시) |
| 2026-08-19T11:23:11+0900 | 착수 | REQ-0268 | web-engineer → web-engineer | 모듈에 인덱스를 항상 표시 — 주차확인센서 10개가 다 같은 라벨이다 (사용자 지시) |
| 2026-08-19T11:25:04+0900 | 완료 | REQ-0268 | web-engineer → web-engineer | 모듈에 인덱스를 항상 표시 — 주차확인센서 10개가 다 같은 라벨이다 (사용자 지시) |
| 2026-08-19T11:25:53+0900 | 착수 | REQ-0267 | web-engineer → web-engineer | 화면 소스를 조별과제샘플/web/ 로 이사 — 도구 경로 전수 갱신 (사용자 지시) |
| 2026-08-19T11:28:07+0900 | 완료 | REQ-0267 | web-engineer → web-engineer | 화면 소스를 조별과제샘플/web/ 로 이사 — 도구 경로 전수 갱신 (사용자 지시) |
| 2026-08-19T11:45:26+0900 | 착수 | REQ-0259 | web-engineer → web-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T11:46:16+0900 | 완료 | REQ-0259 | web-engineer → web-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T11:46:28+0900 | 발행 | REQ-0269 | root → web-engineer | 자리 표시 이름(영역1~5·입구·출구) · cells 순회 · 🔴 예약 자리가 10→5 로 준다 (사용자 확정) |
| 2026-08-19T11:46:58+0900 | 착수 | REQ-0269 | web-engineer → web-engineer | 자리 표시 이름(영역1~5·입구·출구) · cells 순회 · 🔴 예약 자리가 10→5 로 준다 (사용자 확정) |
| 2026-08-19T11:51:45+0900 | 완료 | REQ-0269 | web-engineer → web-engineer | 자리 표시 이름(영역1~5·입구·출구) · cells 순회 · 🔴 예약 자리가 10→5 로 준다 (사용자 확정) |
| 2026-08-19T12:42:09+0900 | 착수 | REQ-0265 | arduino-engineer → arduino-engineer | 펌웨어 소스를 조별과제샘플/ardu/ 로 이사 — hex 0 을 유지해라 (사용자 지시) |
| 2026-08-19T12:46:19+0900 | 완료 | REQ-0265 | arduino-engineer → arduino-engineer | 펌웨어 소스를 조별과제샘플/ardu/ 로 이사 — hex 0 을 유지해라 (사용자 지시) |
| 2026-08-19T13:09:05+0900 | 발행 | REQ-0270 | root → arduino-engineer | 시뮬 점유를 지형에 맞춘다 — A_i·B_i 가 같은 자리다 (사용자 확정 · 굽기는 창 R 뒤) |
| 2026-08-19T13:10:21+0900 | 발행 | REQ-0271 | root → socket-engineer | 🔴 모의/실물 구분을 없앤다 — 전선 kind 의 V 축 제거. socket 이 정본을 박는다 (사용자 확정) |
| 2026-08-19T13:27:38+0900 | 발행 | REQ-0272 | root → socket-engineer | 🔴 서버를 조립식 구조로 — Parking·Node 클래스 분리 + main.cpp 분리 · 배치 API (사용자 지시) |
| 2026-08-19T13:46:56+0900 | 발행 | REQ-0273 | root → arduino-engineer | 펌웨어 리팩터링 검토 — hex 0 으로 되는 것과 굽기가 필요한 것을 갈라 목록을 내라 (사용자 지시) |
| 2026-08-19T13:49:06+0900 | 착수 | REQ-0273 | arduino-engineer → arduino-engineer | 펌웨어 리팩터링 검토 — hex 0 으로 되는 것과 굽기가 필요한 것을 갈라 목록을 내라 (사용자 지시) |
| 2026-08-19T13:56:32+0900 | 완료 | REQ-0273 | arduino-engineer → arduino-engineer | 펌웨어 리팩터링 검토 — hex 0 으로 되는 것과 굽기가 필요한 것을 갈라 목록을 내라 (사용자 지시) |
| 2026-08-19T14:13:00+0900 | 통보 | REQ-0274 | arduino-engineer → root | 정지 시점 상태 — 안 구웠다(칩 2187cc3 그대로) · 소스 0b2a056 대기 · commitAck 을 뺀 사유 |
| 2026-08-19T15:15:57+0900 | 완료 | REQ-0254 | socket-engineer → socket-engineer | 창 P 주입 — 창 M·O 와 같은 규약. rid 4자리=0 이 A[1] 1차 판별자 |
| 2026-08-19T15:15:57+0900 | 완료 | REQ-0257 | socket-engineer → socket-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T15:15:57+0900 | 완료 | REQ-0266 | socket-engineer → socket-engineer | 서버 소스를 조별과제샘플/server/ 로 이사 — ②-b 커밋 뒤 온전한 지점에서 (사용자 지시) |
| 2026-08-19T15:16:06+0900 | 완료 | REQ-0263 | socket-engineer → socket-engineer | 🔴 노드 통신 경로를 별도 클래스로 추출하고 잠근다 — 빈번한 수정이 안정화된 영역을 건드린다 (사용자 지시) |
| 2026-08-19T15:16:35+0900 | 완료 | REQ-0260 | socket-engineer → socket-engineer | 🔴 가상 아두이노(둘째 노드) — 자리 추가를 시험 가능하게. 3b(map<devid,Node>)가 전제다 (사용자 지시) |
| 2026-08-19T15:16:35+0900 | 완료 | REQ-0262 | socket-engineer → socket-engineer | state 에 모듈별 값을 실어라 — 화면 상태등의 원천 (사용자 확정: 주차확인센서와 같은 구조) |
| 2026-08-19T15:16:51+0900 | 완료 | REQ-0256 | monitor-engineer → monitor-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T15:17:31+0900 | 완료 | REQ-0258 | arduino-engineer → arduino-engineer | 🔴 관측 기준선 재설정 — 10:20 이후만 관측 자료. 절단은 사용자 환경 셋팅이었다 |
| 2026-08-19T15:18:26+0900 | 진행 | REQ-0270 | arduino-engineer → arduino-engineer | 시뮬 점유를 지형에 맞춘다 — A_i·B_i 가 같은 자리다 (사용자 확정 · 굽기는 창 R 뒤) — ② 안 들어갔다(축 검산 완료: 이번 굽기 축은 셋, simOcc 없음). 굽기 필요하나 서버 조정은 불필요 — 0271 굽기에 같이 태우는 것을 권한다. 다만 그 사이 서버가 모순된 센서쌍을 보는 것을 socket 이 감당하는지 확인 필요 |
| 2026-08-19T16:33:42+0900 | 완료 | REQ-0271 | socket-engineer → socket-engineer | 🔴 모의/실물 구분을 없앤다 — 전선 kind 의 V 축 제거. socket 이 정본을 박는다 (사용자 확정) |
| 2026-08-19T16:33:42+0900 | 완료 | REQ-0270 | socket-engineer → arduino-engineer | 시뮬 점유를 지형에 맞춘다 — A_i·B_i 가 같은 자리다 (사용자 확정 · 굽기는 창 R 뒤) |
| 2026-08-19T17:04:32+0900 | 발행 | REQ-0275 | root → arduino-engineer | 펌웨어도 class 은닉으로 — 서버와 같은 2단계. 잠금·RAM·판별자 제약 셋 |
| 2026-08-19T17:05:04+0900 | 착수 | REQ-0275 | arduino-engineer → arduino-engineer | 펌웨어도 class 은닉으로 — 서버와 같은 2단계. 잠금·RAM·판별자 제약 셋 |
| 2026-08-19T18:20:10+0900 | 완료 | REQ-0275 | arduino-engineer → arduino-engineer | 펌웨어도 class 은닉으로 — 서버와 같은 2단계. 잠금·RAM·판별자 제약 셋 |
| 2026-08-19T18:21:18+0900 | 발행 | REQ-0276 | web-engineer → socket-engineer | 🔴 572a4cb 가 map/state 에 zones 대신 lot.zones() 키를 낸다 — 실기에서 개정 격자가 통째로 안 뜬다 |
| 2026-08-19T18:30:54+0900 | 착수 | REQ-0276 | socket-engineer → socket-engineer | 🔴 572a4cb 가 map/state 에 zones 대신 lot.zones() 키를 낸다 — 실기에서 개정 격자가 통째로 안 뜬다 |
| 2026-08-19T18:31:27+0900 | 완료 | REQ-0276 | socket-engineer → socket-engineer | 🔴 572a4cb 가 map/state 에 zones 대신 lot.zones() 키를 낸다 — 실기에서 개정 격자가 통째로 안 뜬다 |
| 2026-08-19T19:05:32+0900 | 통보 | REQ-0277 | socket-engineer → root | REQ 없이 한 것 — 배포 1회·시험 인스턴스 3회·절차 신설·잠금 파일 수정 |
| 2026-08-19T21:46:16+0900 | 통보 | REQ-0278 | arduino-engineer → root | 지시 없이 고친 결함 — 가상 차단봉 비트 위치를 '뒤는 전부 차단봉' 으로 세고 있었다 |
| 2026-08-19T22:03:41+0900 | 통보 | REQ-0279 | monitor-engineer → root | 창 AC 관측 — socket 지시와 다른 시점에 탭을 열었다 · 판정 전항목 ✅ · 배너 slots 라벨 결함 |
| 2026-08-19T22:18:41+0900 | 통보 | REQ-0280 | arduino-engineer → root | 지시 없이 고친 결함 둘 — 에코 규약이 동작 명령에서 무너지고, 센서에 명령 핸들러가 붙었다 |
| 2026-08-20T00:24:23+0900 | 발행 | REQ-0281 | socket-engineer → web-engineer | 화면 직접 조작 계약 1단계 — control 선언 · cmd_result · confirmed |
| 2026-08-20T00:24:54+0900 | 착수 | REQ-0281 | web-engineer → web-engineer | 화면 직접 조작 계약 1단계 — control 선언 · cmd_result · confirmed |
| 2026-08-20T06:21:12+0900 | 통보 | REQ-0282 | socket-engineer → root | 운영 배포 06:19:50 (build=9087781) — 1차 시도에서 도는 실행파일을 덮어썼다 |
| 2026-08-20T07:05:58+0900 | 발행 | REQ-0283 | socket-engineer → root | 조별과제샘플/dev_server/** 소유 규칙이 없다 — 작업이 막혔다 |
| 2026-08-20T12:47:23+0900 | 발행 | REQ-0284 | socket-engineer → web-engineer | 조립 API v2 — 자리 종류가 parking/area 둘로 · 센서 구분이 등록 뒤로 |
| 2026-08-20T12:47:39+0900 | 착수 | REQ-0284 | web-engineer → web-engineer | 조립 API v2 — 자리 종류가 parking/area 둘로 · 센서 구분이 등록 뒤로 |
| 2026-08-20T12:50:26+0900 | 완료 | REQ-0284 | web-engineer → web-engineer | 조립 API v2 — 자리 종류가 parking/area 둘로 · 센서 구분이 등록 뒤로 |
| 2026-08-20T13:21:12+0900 | 통보 | REQ-0285 | monitor-engineer → root | 기동 확인 — 미결 0건이나 장치가 2시간 22분째 미복구(서버 계측) · 창 AF 사전등록이 낡음 |
| 2026-08-20T13:28:55+0900 | 통보 | REQ-0286 | monitor-engineer → root | 실물 개입 — 진단 탭 13:26:41 개방(DTR 리셋)·13:28:05 반납 · 칩은 이미 192.168.0.29:8888 을 보고 있다 |
| 2026-08-20T13:32:29+0900 | 완료 | REQ-0283 | root → root | 조별과제샘플/dev_server/** 소유 규칙이 없다 — 작업이 막혔다 |
| 2026-08-20T13:33:06+0900 | 착수 | REQ-0272 | socket-engineer → socket-engineer | 🔴 서버를 조립식 구조로 — Parking·Node 클래스 분리 + main.cpp 분리 · 배치 API (사용자 지시) |
| 2026-08-20T13:41:55+0900 | 진행 | REQ-0281 | web-engineer → web-engineer | 화면 직접 조작 계약 1단계 — control 선언 · cmd_result · confirmed — 완료 기준 셋 대조: ①②는 주입 36 pass/0 fail 로 확정 · ③(숫자 모듈 settled 0건)은 실기 관측 항목이라 미측정. --live 측정 경로를 만들고 판본 게이트를 실측(exit 2, 미배포). v2 는 이 계약을 안 건드린다(SPEC-assembly-v2 마지막 줄). |
| 2026-08-20T13:42:54+0900 | 통보 | REQ-0287 | web-engineer → root | REQ-0281 진행 중 한 것 — mod-control 에 --live 관측 경로 신설 · 원장 §5.90 |
| 2026-08-20T13:44:43+0900 | 완료 | REQ-0281 | web-engineer → web-engineer | 화면 직접 조작 계약 1단계 — control 선언 · cmd_result · confirmed |
| 2026-08-20T13:47:06+0900 | 발행 | REQ-0288 | root → web-engineer | 격자 칸을 요약만으로 — 모듈이 여러 개면 화면이 늘어나 깨진다 (사용자 확정 ③안) |
| 2026-08-20T13:47:24+0900 | 착수 | REQ-0288 | web-engineer → web-engineer | 격자 칸을 요약만으로 — 모듈이 여러 개면 화면이 늘어나 깨진다 (사용자 확정 ③안) |
| 2026-08-20T13:50:13+0900 | 진행 | REQ-0272 | socket-engineer → socket-engineer | 🔴 서버를 조립식 구조로 — Parking·Node 클래스 분리 + main.cpp 분리 · 배치 API (사용자 지시) — v2 조립 API 구현 완료(ca7c64e) · 자가검증 148/0 · sync_check 음성대조 복구 · dev 봉투 확인 · 배포는 화면 판본 때문에 루트 확인 대기 |
| 2026-08-20T13:55:04+0900 | 완료 | REQ-0272 | socket-engineer → socket-engineer | 🔴 서버를 조립식 구조로 — Parking·Node 클래스 분리 + main.cpp 분리 · 배치 API (사용자 지시) |
| 2026-08-20T13:59:56+0900 | 완료 | REQ-0288 | web-engineer → web-engineer | 격자 칸을 요약만으로 — 모듈이 여러 개면 화면이 늘어나 깨진다 (사용자 확정 ③안) |
| 2026-08-20T14:02:34+0900 | 발행 | REQ-0289 | root → web-engineer | PC 넓은 화면에서 가로를 쓴다 — .wrap 이 1060px 에 묶여 세로로만 길어진다 (사용자 요구) |
| 2026-08-20T14:03:40+0900 | 착수 | REQ-0289 | web-engineer → web-engineer | PC 넓은 화면에서 가로를 쓴다 — .wrap 이 1060px 에 묶여 세로로만 길어진다 (사용자 요구) |
| 2026-08-20T14:06:21+0900 | 완료 | REQ-0289 | web-engineer → web-engineer | PC 넓은 화면에서 가로를 쓴다 — .wrap 이 1060px 에 묶여 세로로만 길어진다 (사용자 요구) |
| 2026-08-20T14:08:44+0900 | 발행 | REQ-0290 | root → web-engineer | 🔴 실기에서 예약이 안 먹는다 — 테스트모드만 동작하고 안내 문구가 데모 지형이다 (사용자 실물 보고) |
| 2026-08-20T14:09:40+0900 | 착수 | REQ-0290 | web-engineer → web-engineer | 🔴 실기에서 예약이 안 먹는다 — 테스트모드만 동작하고 안내 문구가 데모 지형이다 (사용자 실물 보고) |
| 2026-08-20T14:14:28+0900 | 발행 | REQ-0291 | root → socket-engineer | 입구 E1 좌표를 (3,4)로 — 넓은 화면 모드에서 큰 격자를 확인한다 (사용자 지시) |
| 2026-08-20T14:14:49+0900 | 착수 | REQ-0291 | socket-engineer → socket-engineer | 입구 E1 좌표를 (3,4)로 — 넓은 화면 모드에서 큰 격자를 확인한다 (사용자 지시) |
| 2026-08-20T14:15:49+0900 | 발행 | REQ-0292 | root → socket-engineer | 모듈 없는 주차 자리를 '비활성'으로 — 첫 행 (0,0)~(0,4) 에 주차공간 다섯 (사용자 지시) |
| 2026-08-20T14:16:33+0900 | 완료 | REQ-0291 | socket-engineer → socket-engineer | 입구 E1 좌표를 (3,4)로 — 넓은 화면 모드에서 큰 격자를 확인한다 (사용자 지시) |
| 2026-08-20T14:18:55+0900 | 완료 | REQ-0290 | web-engineer → web-engineer | 🔴 실기에서 예약이 안 먹는다 — 테스트모드만 동작하고 안내 문구가 데모 지형이다 (사용자 실물 보고) |
| 2026-08-20T14:22:39+0900 | 발행 | REQ-0293 | socket-engineer → web-engineer | map.zones[].active — 모듈 0개인 주차 자리를 비활성으로 그려라 |
| 2026-08-20T14:23:10+0900 | 착수 | REQ-0293 | web-engineer → web-engineer | map.zones[].active — 모듈 0개인 주차 자리를 비활성으로 그려라 |
| 2026-08-20T14:25:04+0900 | 완료 | REQ-0292 | socket-engineer → socket-engineer | 모듈 없는 주차 자리를 '비활성'으로 — 첫 행 (0,0)~(0,4) 에 주차공간 다섯 (사용자 지시) |
| 2026-08-20T14:27:10+0900 | 완료 | REQ-0293 | web-engineer → web-engineer | map.zones[].active — 모듈 0개인 주차 자리를 비활성으로 그려라 |
| 2026-08-20T14:29:18+0900 | 통보 | REQ-0294 | socket-engineer → root | 정정: E1 모듈 1개(DR)는 틀렸다 — 그리고 '선언했는데 장치에 없는 모듈' 을 아무도 안 센다 |
| 2026-08-20T14:29:23+0900 | 통보 | REQ-0295 | web-engineer → root | 원장 §5.91 — map 의 modules 는 결속이고 선언이 아니다 (화면은 그 차이를 못 본다) |
| 2026-08-20T14:40:40+0900 | 발행 | REQ-0296 | root → web-engineer | 비활성 자리를 회색으로 — unknown 색과 갈라라 (사용자 지시) |
| 2026-08-20T14:41:28+0900 | 착수 | REQ-0296 | web-engineer → web-engineer | 비활성 자리를 회색으로 — unknown 색과 갈라라 (사용자 지시) |
| 2026-08-20T14:44:19+0900 | 완료 | REQ-0296 | web-engineer → web-engineer | 비활성 자리를 회색으로 — unknown 색과 갈라라 (사용자 지시) |
| 2026-08-20T14:45:48+0900 | 발행 | REQ-0297 | root → web-engineer | 격자 칸에 label 만 — 요약·배지·사유 문구를 패널로 옮긴다 (사용자 지시) |
| 2026-08-20T14:47:00+0900 | 착수 | REQ-0297 | web-engineer → web-engineer | 격자 칸에 label 만 — 요약·배지·사유 문구를 패널로 옮긴다 (사용자 지시) |
| 2026-08-20T14:52:14+0900 | 완료 | REQ-0297 | web-engineer → web-engineer | 격자 칸에 label 만 — 요약·배지·사유 문구를 패널로 옮긴다 (사용자 지시) |
| 2026-08-20T15:27:17+0900 | 발행 | REQ-0298 | root → arduino-engineer | KIND_* 매크로가 문서·주석에만 남아 있다 — 기여자 가이드가 없는 상수를 가르친다 (사용자 지적) |
| 2026-08-20T15:27:50+0900 | 착수 | REQ-0298 | arduino-engineer → arduino-engineer | KIND_* 매크로가 문서·주석에만 남아 있다 — 기여자 가이드가 없는 상수를 가르친다 (사용자 지적) |
| 2026-08-20T15:32:14+0900 | 발행 | REQ-0299 | root → arduino-engineer | 🔴 client.ino 의 자리배치 로직을 없앤다 — 모듈 표가 유일한 원천이 된다 (사용자 지시) |
| 2026-08-20T15:34:34+0900 | 발행 | REQ-0300 | arduino-engineer → root | KIND_* 잔재가 docs/ 문서 둘에 더 있다 — 기여자·온보딩 문서가 없는 매크로를 가르친다 (REQ-0298 전수에서 나옴) |
| 2026-08-20T15:34:41+0900 | 완료 | REQ-0298 | arduino-engineer → arduino-engineer | KIND_* 매크로가 문서·주석에만 남아 있다 — 기여자 가이드가 없는 상수를 가르친다 (사용자 지적) |
| 2026-08-20T15:35:59+0900 | 착수 | REQ-0299 | arduino-engineer → arduino-engineer | 🔴 client.ino 의 자리배치 로직을 없앤다 — 모듈 표가 유일한 원천이 된다 (사용자 지시) |
| 2026-08-20T15:39:36+0900 | 완료 | REQ-0300 | root → root | KIND_* 잔재가 docs/ 문서 둘에 더 있다 — 기여자·온보딩 문서가 없는 매크로를 가르친다 (REQ-0298 전수에서 나옴) |
| 2026-08-20T15:50:41+0900 | 발행 | REQ-0301 | root → web-engineer | sim_step 버튼을 뺀다 — 누르면 반드시 거절되는 버튼이 배포본에 있다 (socket 발견) |
| 2026-08-20T15:52:49+0900 | 착수 | REQ-0301 | web-engineer → web-engineer | sim_step 버튼을 뺀다 — 누르면 반드시 거절되는 버튼이 배포본에 있다 (socket 발견) |
| 2026-08-20T15:55:31+0900 | 완료 | REQ-0301 | web-engineer → web-engineer | sim_step 버튼을 뺀다 — 누르면 반드시 거절되는 버튼이 배포본에 있다 (socket 발견) |
| 2026-08-20T15:58:22+0900 | 통보 | REQ-0302 | web-engineer → root | deploy-screen --check 가 미배포 목록을 만든다 (같은 착오 2회) + 원장 §5.92 |
| 2026-08-20T16:14:58+0900 | 완료 | REQ-0299 | arduino-engineer → arduino-engineer | 🔴 client.ino 의 자리배치 로직을 없앤다 — 모듈 표가 유일한 원천이 된다 (사용자 지시) |
| 2026-08-20T16:26:41+0900 | 발행 | REQ-0303 | root → arduino-engineer | 🔴 client.ino 를 아두이노 표준 스케치로 — setup/loop 에 기여자 줄만 남긴다 (사용자 지시) |
| 2026-08-20T16:28:39+0900 | 착수 | REQ-0303 | arduino-engineer → arduino-engineer | 🔴 client.ino 를 아두이노 표준 스케치로 — setup/loop 에 기여자 줄만 남긴다 (사용자 지시) |
| 2026-08-20T17:00:40+0900 | 발행 | REQ-0304 | root → arduino-engineer | 🔴 모듈 설정을 선언 한 줄로 — 컴포넌트 패턴 (사용자 지시 · REQ-0303 B단계를 대체한다) |
| 2026-08-20T17:03:08+0900 | 착수 | REQ-0304 | arduino-engineer → arduino-engineer | 🔴 모듈 설정을 선언 한 줄로 — 컴포넌트 패턴 (사용자 지시 · REQ-0303 B단계를 대체한다) |
| 2026-08-20T17:03:18+0900 | 통보 | REQ-0305 | web-engineer → root | 원장 §5.93 — 모듈 이름 두 층(폴백 문구 vs 라벨 채우기) · 보류 + 닫는 조건 |
| 2026-08-20T17:37:24+0900 | 발행 | REQ-0306 | root → socket-engineer | 차단봉을 없앤다 — lot.cpp 의 DR 선언 제거 (사용자 지시) |
| 2026-08-20T17:38:24+0900 | 착수 | REQ-0306 | socket-engineer → socket-engineer | 차단봉을 없앤다 — lot.cpp 의 DR 선언 제거 (사용자 지시) |
| 2026-08-20T17:46:11+0900 | 완료 | REQ-0306 | socket-engineer → socket-engineer | 차단봉을 없앤다 — lot.cpp 의 DR 선언 제거 (사용자 지시) |
| 2026-08-20T17:48:15+0900 | 통보 | REQ-0307 | web-engineer → root | 원장 §5.94 — cmd_result 는 서버 문구가 먼저, ack 은 내 표가 먼저(반대) · 그 문구는 안 지워진다 |
| 2026-08-20T17:59:01+0900 | 완료 | REQ-0304 | arduino-engineer → arduino-engineer | 🔴 모듈 설정을 선언 한 줄로 — 컴포넌트 패턴 (사용자 지시 · REQ-0303 B단계를 대체한다) |
