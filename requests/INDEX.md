# 요청 원장 (append-only)

모든 요청 이벤트가 시간순으로 쌓인다. 현재 상태의 원천은 각 요청 md 의
front-matter 이고, 미결 목록은 `requests/open/` 이다. 이 파일은 고쳐 쓰지 않는다.

| 시각 | 이벤트 | ID | 행위자 → 담당 | 제목 |
|---|---|---|---|---|
| 2026-08-13T19:19:26+0900 | 발행 | REQ-0001 | root → android-engineer | JNI 연동 최소 예제 앱 뼈대 |
| 2026-08-13T19:19:48+0900 | 착수 | REQ-0001 | android-engineer → android-engineer | JNI 연동 최소 예제 앱 뼈대 |
| 2026-08-13T19:39:28+0900 | 발행 | REQ-0002 | android-engineer → cpp-engineer | JNI: greetFromJNI 네이티브 구현 + CMakeLists |
| 2026-08-13T19:39:40+0900 | 착수 | REQ-0002 | cpp-engineer → cpp-engineer | JNI: greetFromJNI 네이티브 구현 + CMakeLists |
