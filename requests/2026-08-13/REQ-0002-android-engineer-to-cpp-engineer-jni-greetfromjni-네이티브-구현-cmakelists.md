---
id: REQ-0002
title: JNI: greetFromJNI 네이티브 구현 + CMakeLists
from: android-engineer
to: cpp-engineer
status: done
created: 2026-08-13T19:39:28+0900
updated: 2026-08-13T19:47:51+0900
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

처리자: `cpp-engineer` · 2026-08-13

### ⚠ 요청서의 환경 전제 정정 — NDK 는 실제로 설치돼 있다

요청서 [환경 주의] 의 "이 머신에는 NDK 가 설치돼 있지 않다" 는 **사실과 다르다.**

```
/Users/idong-u/Library/Android/sdk/ndk/29.0.14206865   ← 존재 확인
/Users/idong-u/Library/Android/sdk/cmake/4.1.2/bin/cmake
```

따라서 "빌드 미검증" 으로 남기지 않고 **실제로 빌드를 돌려 검증했다.**
다음 요청부터는 NDK 빌드를 전제해도 된다.

### 만든 파일 (2개, 요청된 경로 그대로)

- `android/app/src/main/cpp/native-lib.cpp`
- `android/app/src/main/cpp/CMakeLists.txt`

요청 시점에 `android/` 트리 자체가 아직 없었다. 위 두 파일이 그 트리의 첫 파일이므로
**android-engineer 가 이후 프로젝트 스캐폴딩을 생성할 때 이 두 파일을 덮어쓰지 않도록 주의 바란다.**

### 구현 내용

**native-lib.cpp**

- 심볼: `Java_com_example_jnidemo_MainActivity_greetFromJNI(JNIEnv*, jobject, jstring)`
  — 인스턴스 멤버 기준. `_00024Companion` 없음.
- `ScopedUtfChars` RAII 클래스로 `GetStringUTFChars` / `ReleaseStringUTFChars` 짝맞춤을
  **소멸자에 고정**했다. 조기 반환 경로에서도 누수가 생기지 않는다(주석이 아니라 타입으로 보장).
- 방어 1 — `name == nullptr`: `GetStringUTFChars` **호출 전에** 검사하고 대체 문구를 반환한다.
- 방어 2 — `GetStringUTFChars` 가 OOM 으로 `nullptr` 을 돌려준 경우: pending exception 이
  남은 상태이므로 추가 JNI 호출 없이 즉시 `nullptr` 반환. 이때 `Release` 를 부르지 않는다
  (실패한 획득에 대해 `Release` 를 호출하는 것 자체가 버그이므로 `valid()` 로 구분).
- 반환 문구: `<name>님 안녕하세요. C++ 네이티브 계층에서 인사합니다.`
  — 인자 문자열이 앞에 그대로 연결된다.

**CMakeLists.txt**

- `cmake_minimum_required(VERSION 3.22.1)` — Gradle 설정과 맞춰 고정(변경 금지 주석 명시).
- `project(jnidemo)` / `add_library(jnidemo SHARED native-lib.cpp)` → 산출물 `libjnidemo.so`.
- `target_compile_options(jnidemo PRIVATE -Wall -Wextra)`.
- `find_library(log-lib log)` + `target_link_libraries` 로 Android 로그 라이브러리 연결.

### 검증 (실제 실행함)

1) CMake 구성 — cmake 4.1.2 + NDK 29 툴체인, arm64-v8a / android-24

```
/Users/idong-u/Library/Android/sdk/cmake/4.1.2/bin/cmake \
  -S android/app/src/main/cpp -B /tmp/jnidemo-build \
  -DCMAKE_TOOLCHAIN_FILE=/Users/idong-u/Library/Android/sdk/ndk/29.0.14206865/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
→ Configuring done / Generating done   (cmake 4.1.2 에서 3.22.1 pin 그대로 통과)
```

2) 빌드 — **경고 0개** (`-Wall -Wextra` 켠 상태)

```
/Users/idong-u/Library/Android/sdk/cmake/4.1.2/bin/cmake --build /tmp/jnidemo-build
→ [ 50%] Building CXX object CMakeFiles/jnidemo.dir/native-lib.cpp.o
  [100%] Linking CXX shared library libjnidemo.so
  [100%] Built target jnidemo
```

3) 심볼 검사 — 완료 기준의 핵심. 컴파일 성공만으로는 심볼 문자열 정확성이 증명되지 않으므로 별도 확인.

```
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-nm --defined-only --extern-only \
  /tmp/jnidemo-build/libjnidemo.so
→ 0000000000022a90 T Java_com_example_jnidemo_MainActivity_greetFromJNI
```

- 심볼명 완전 일치, `T`(전역 정의). `Companion` 변형 없음 → `UnsatisfiedLinkError` 위험 없음.
- 산출물 파일명 `libjnidemo.so` → `System.loadLibrary("jnidemo")` 와 일치.

4) 한글 리터럴 인코딩 보존 — 빌드 성공·심볼 검사로는 잡히지 않는 실패 모드라 따로 확인했다.
   (여기서 깨지면 앱은 정상 링크·정상 실행되면서 화면에 모지바케만 뜬다)

```
grep -a -c 안녕하세요 /tmp/jnidemo-build/libjnidemo.so
→ 1
```

UTF-8 문자열이 `.so` 의 rodata 에 온전히 들어갔다.

5) x86_64 ABI 재검증 — 에뮬레이터는 보통 x86_64 이므로 arm64-v8a 단일 검증으로 끝내지 않았다.

```
... -B /tmp/jnidemo-build-x64 -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-24
→ Configuring done / Generating done / Built target jnidemo   (경고 0개)
llvm-nm ... /tmp/jnidemo-build-x64/libjnidemo.so
→ 0000000000021260 T Java_com_example_jnidemo_MainActivity_greetFromJNI
```

**arm64-v8a / x86_64 두 ABI 모두 빌드·심볼 검증 통과.** 실기기와 에뮬레이터 양쪽에 대응된다.

빌드 디렉터리는 `/tmp/jnidemo-build`, `/tmp/jnidemo-build-x64` 로 out-of-tree 로 뺐다.
소스 트리는 두 파일뿐이다.

### 검증하지 못한 것 (정직하게 남김)

- **런타임 동작은 실행 검증하지 않았다.** 반환 문자열에 인자가 포함되는지는 코드상
  단순 문자열 연결이므로 눈검사로 확인했을 뿐, 실제 호출로 확인하지 않았다.
  호스트용 JNIEnv 목(mock) 하네스를 `/tmp` 에 만들어 실행하려 했으나, 소유권 훅이
  **프로젝트 밖 경로를 root 소유로 판정**해 Write 를 차단했다. 우회하지 않고 중단했다.
  (구조상 도메인 에이전트가 저장소 밖 스크래치 공간에 검증 하네스를 만들 수 없다 —
   정책 판단이 필요하면 루트가 결정할 사안이라 별도 요청은 발행하지 않고 여기 남긴다.)
- 실기기/에뮬레이터 적재 및 Kotlin 측 실제 호출은 android-engineer 영역이라 하지 않았다.

### 처리 완료 · cpp-engineer · 2026-08-13T19:47:51+0900

native-lib.cpp + CMakeLists 작성, NDK29 실빌드 통과(경고0), nm 으로 심볼 정확성 확인 — 요청서의 'NDK 미설치' 전제는 오류

