---
id: REQ-0002
title: JNI: greetFromJNI 네이티브 구현 + CMakeLists
from: android-engineer
to: cpp-engineer
status: claimed
created: 2026-08-13T19:39:28+0900
updated: 2026-08-13T19:39:40+0900
files: ["android/app/src/main/cpp/native-lib.cpp", "android/app/src/main/cpp/CMakeLists.txt"]
parent: none
---

# REQ-0002 · JNI: greetFromJNI 네이티브 구현 + CMakeLists

**요청자** `android-engineer` → **담당** `cpp-engineer`

## 요청 내용

[정확히 지켜야 하는 3가지]

1) JNI 심볼명
Kotlin 측 선언은 com.example.jnidemo.MainActivity 의 "인스턴스 멤버" 이다.
(companion object 가 아니다 — companion 이면 심볼에 00024Companion 이 붙어 링크가 깨진다)
  package com.example.jnidemo
  class MainActivity : Activity() {
      external fun greetFromJNI(name: String): String
  }
따라서 C++ 쪽 심볼은 반드시 아래와 같아야 한다:
  extern "C" JNIEXPORT jstring JNICALL
  Java_com_example_jnidemo_MainActivity_greetFromJNI(JNIEnv* env, jobject thiz, jstring name)

2) 라이브러리 이름 = jnidemo
Kotlin 은 System.loadLibrary("jnidemo") 로 적재한다.
CMake 타겟도 반드시 add_library(jnidemo SHARED native-lib.cpp) 여야 한다. 이름이 다르면 UnsatisfiedLinkError 다.

3) 파일 경로 고정
  android/app/src/main/cpp/native-lib.cpp
  android/app/src/main/cpp/CMakeLists.txt
이 두 경로만 cpp-engineer 소유로 판정된다. 다른 위치(예: android/app/CMakeLists.txt)에 두면
소유권 훅이 너의 쓰기를 차단한다. Gradle 의 externalNativeBuild cmake path 를 위 CMakeLists 경로로
이미 지정해 두었으니 경로를 바꾸지 말아 달라.

[동작 요구]
- jstring 인자를 GetStringUTFChars 로 읽고, 사용 후 ReleaseStringUTFChars 로 반드시 해제한다.
- 반환값은 NewStringUTF 로 만들며, "인자로 받은 문자열" 이 반환 문자열에 그대로 포함돼야 한다.
- 문구 예시: "홍길동님 안녕하세요. C++ 네이티브 계층에서 인사합니다."
- name 이 null 일 때 크래시하지 않도록 방어 코드를 넣어 달라.

[CMakeLists 요구]
- cmake_minimum_required(VERSION 3.22.1)  ← Gradle 쪽 cmake version 을 3.22.1 로 잡아 두었다.
- project(jnidemo)
- add_library(jnidemo SHARED native-lib.cpp)
- 로그가 필요하면 find_library 로 log 를 찾아 target_link_libraries 로 연결해도 된다.

[환경 주의]
이 머신에는 NDK 가 설치돼 있지 않다. 실제 빌드는 아무도 돌릴 수 없으므로 시도하지 말고,
처리 결과에 "빌드 미검증" 임을 명시해 달라. 검증은 심볼명/타겟명/경로 정합성 눈검사로 갈음한다.

## 왜 필요한가

Kotlin MainActivity 가 external fun greetFromJNI(name: String): String 를 호출해 화면에 표시하는데, 네이티브 구현과 CMakeLists 가 없으면 앱이 링크되지 않는다. 해당 경로는 cpp-engineer 소유라 android-engineer 가 직접 만들 수 없다. (상위 요청 REQ-0001)

## 대상 파일

- `android/app/src/main/cpp/native-lib.cpp`
- `android/app/src/main/cpp/CMakeLists.txt`
## 완료 기준

native-lib.cpp 에 Java_com_example_jnidemo_MainActivity_greetFromJNI 심볼이 정확히 존재하고, CMakeLists.txt 의 add_library 타겟명이 jnidemo 이며, 두 파일이 android/app/src/main/cpp/ 아래에 있을 것. 인자 문자열이 반환값에 포함될 것. NDK 미설치이므로 실제 빌드는 미검증으로 남긴다.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0002 --by cpp-engineer --note "<한 줄 요약>" -->

_(미처리)_
