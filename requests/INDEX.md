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
