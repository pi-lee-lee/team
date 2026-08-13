---
id: REQ-0001
title: JNI 연동 최소 예제 앱 뼈대
from: root
to: android-engineer
status: claimed
created: 2026-08-13T19:19:26+0900
updated: 2026-08-13T19:19:48+0900
files: ["android/app/src/main/java/com/example/jnidemo/MainActivity.kt", "android/app/build.gradle.kts", "android/app/src/main/AndroidManifest.xml"]
parent: none
---

# REQ-0001 · JNI 연동 최소 예제 앱 뼈대

**요청자** `root` → **담당** `android-engineer`

## 요청 내용

Kotlin 에서 네이티브 함수를 호출해 화면에 문자열을 표시하는 최소 예제 앱 뼈대를 만들어라. Kotlin 측 시그니처는 external fun greetFromJNI(name: String): String 로 하고, MainActivity 에서 그 결과를 TextView 에 표시한다. System.loadLibrary 로 네이티브 라이브러리를 적재한다. 네이티브(C++) 구현과 CMakeLists 는 네 소유가 아니다 — 담당 에이전트에게 md 요청을 발행해서 받아라. 절대 직접 만들지 마라.

## 왜 필요한가

팀 구조의 도메인 경계와 교차 요청 흐름을 실제 코드로 검증한다

## 대상 파일

- `android/app/src/main/java/com/example/jnidemo/MainActivity.kt`
- `android/app/build.gradle.kts`
- `android/app/src/main/AndroidManifest.xml`
## 완료 기준

Kotlin/Gradle/Manifest 가 작성되고, 네이티브 쪽은 별도 REQ 로 담당에게 넘어가 처리 완료됨. NDK 미설치이므로 실제 빌드는 시도하지 말고 검증 미실시임을 처리 결과에 명시할 것.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0001 --by android-engineer --note "<한 줄 요약>" -->

_(미처리)_
