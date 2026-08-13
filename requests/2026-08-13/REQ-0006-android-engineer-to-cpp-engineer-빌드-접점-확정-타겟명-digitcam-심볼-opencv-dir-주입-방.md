---
id: REQ-0006
title: 빌드 접점 확정: 타겟명 digitcam · 심볼 · OpenCV_DIR 주입 방식 · 정적/공유 회신
from: android-engineer
to: cpp-engineer
status: done
created: 2026-08-13T21:13:43+0900
updated: 2026-08-13T21:28:45+0900
files: ["android/app/src/main/cpp/CMakeLists.txt", "android/app/src/main/cpp/native-lib.cpp"]
parent: REQ-0005
---

# REQ-0006 · 빌드 접점 확정: 타겟명 digitcam · 심볼 · OpenCV_DIR 주입 방식 · 정적/공유 회신

**요청자** `android-engineer` → **담당** `cpp-engineer`

## 요청 내용

REQ-0003(OpenCV 코어+JNI) 에 착수하기 전에, Gradle 쪽에서 이미 확정한 접점을 알린다. CMakeLists 에서 이 값들을 하드코딩하지 말고 그대로 받아라.

(1) 타겟명은 digitcam 이다. add_library(digitcam SHARED ...) 로 libdigitcam.so 를 만들어야 한다. Kotlin 이 System.loadLibrary("digitcam") 으로 적재한다. 지금 CMakeLists 는 add_library(jnidemo ...) 이고 native-lib.cpp 는 Java_com_example_jnidemo_MainActivity_greetFromJNI 를 내보낸다 — 둘 다 REQ-0003 에서 없어져야 한다. Kotlin 쪽 jnidemo 패키지와 greet 데모 화면은 내가 이미 지웠다.

(2) 내보낼 심볼 5개는 계약 docs/digitcam-contract.md 4절 표 그대로다. 두 번째 인자는 jclass 가 아니라 jobject 다 — Kotlin object 싱글턴이라서 그렇다. RegisterNatives 쓰지 말고 정적 등록(심볼명 매칭)으로 간다.

(3) OpenCV 경로는 내가 build.gradle.kts 의 defaultConfig.externalNativeBuild.cmake.arguments 로 주입한다. CMakeLists 안에 절대경로를 쓰지 마라. 내가 넘기는 인자 두 개는 이렇다.
      -DOpenCV_DIR=OPENCV_SDK/native/jni
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    여기서 OPENCV_SDK 는 android/local.properties 의 opencv.dir 키에서 읽고, 키가 없으면 HOME/opencv-android-sdk/OpenCV-android-sdk/sdk 로 폴백한다. local.properties 에는 내가 이미 적어 뒀다.
    따라서 CMakeLists 는 find_package(OpenCV REQUIRED) 만 부르면 된다. 경로 탐색 코드는 필요 없다.
    두 번째 인자는 계약 10절이 지목한 CMake 4.1.2 비호환 대비다. 이게 걸려 있으므로 cmake_minimum_required 를 3.22.1 로 그대로 둬도 configure 가 통과할 것으로 본다(내가 아직 확인하지는 않았다).

(4) 회신이 반드시 필요한 것 — 정적 링크인가 공유 라이브러리인가. 계약 3절은 정적 우선 검토다. staticlibs 의 각 ABI 폴더에 libopencv_core.a, libopencv_imgproc.a, libopencv_dnn.a 가 있는 것은 내가 확인했다. 네가 공유로 갔다면 내가 build.gradle.kts 에서 libopencv_java4.so 를 jniLibs 로 패키징해야 한다. 지금은 정적 가정으로 그 패키징을 꺼 뒀다(gradle 속성 opencv.packageShared, 기본 false). 공유를 택했으면 처리 결과에 반드시 적어라. 안 적으면 앱이 런타임에 libopencv_java4.so 를 못 찾고 죽는다.

(5) 이 머신 사실 정정 하나. emulator-5554 는 sdk_gphone64_arm64 다. 계약 9절의 'x86_64 에뮬레이터' 는 이 머신에서 틀렸다. 에뮬레이터 검증에 실제로 쓰이는 ABI 는 arm64-v8a 다. abiFilters 는 arm64-v8a 와 x86_64 둘 다 유지했으니 양쪽 모두 빌드되어야 한다. 정적 링크를 ABI 별로 시험한다면 arm64-v8a 를 먼저 보라.

## 왜 필요한가

CMakeLists 는 네 소유고 build.gradle.kts 는 내 소유라 빌드가 두 사람에 걸친다(계약 10절). 타겟명과 OpenCV 경로 주입 방식을 미리 맞추지 않으면, 네가 하드코딩한 경로와 내가 주입하는 값이 충돌하거나 정적/공유 선택을 내가 몰라서 패키징이 어긋난다. 통합 빌드 책임이 나에게 있으므로 네가 착수하기 전에 접점을 고정해 둔다.

## 대상 파일

- `android/app/src/main/cpp/CMakeLists.txt`
- `android/app/src/main/cpp/native-lib.cpp`
## 완료 기준

CMakeLists 가 add_library(digitcam SHARED ...) 이고, 절대경로 없이 find_package(OpenCV REQUIRED) 로 configure 가 통과한다. 내보낸 심볼 5개가 계약 4절 표와 일치한다(nm 으로 확인 가능). 처리 결과에 정적 링크인지 공유 라이브러리인지를 한 줄로 명시한다.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0006 --by cpp-engineer --note "<한 줄 요약>" -->

### 답: **정적 링크다.** libopencv_java4.so 를 패키징하지 마라 (opencv.packageShared=false 유지)

실측으로 확인했다. 추정이 아니다.

- SDK 의 `sdk/native/jni/abi-<ABI>/OpenCVConfig.cmake` 가 `set(OpenCV_SHARED OFF)` 이다.
  그래서 `find_package(OpenCV REQUIRED COMPONENTS core imgproc)` 는 `staticlibs/<ABI>/*.a` 를 잡는다.
  configure 로그에도 `[digitcam] OpenCV_SHARED = OFF (OFF = 정적 링크)` 로 찍히게 해 뒀다.
- 빌드된 `libdigitcam.so` 의 DT_NEEDED 에 **libopencv_java4.so 가 없다**(llvm-readelf -d 확인):
  `liblog.so, libc.so, libz.so, libdl.so, libm.so` 뿐이다. libc++_shared.so 도 없다(c++_static).
  → APK 에 OpenCV .so 를 넣을 필요가 없고 Kotlin 쪽 `OpenCVLoader` 초기화도 필요 없다.
- 링크가 진짜인지도 기기에서 확인했다. 매크로 `CV_VERSION` 은 헤더에서 오므로 증거가 못 된다 —
  `cv::getVersionString()` 을 호출하는 실행 파일을 arm64-v8a 로 빌드해 emulator-5554 에서 돌렸고
  `4.14.0` 이 찍혔다. `nativeVersion()` 도 같은 함수를 쓴다.

### (1) 타겟명 · (2) 심볼

`add_library(digitcam SHARED native-lib.cpp digit_pipeline.cpp)` 로 바꿨다. `jnidemo` 타겟과
`Java_com_example_jnidemo_MainActivity_greetFromJNI` 는 제거했다. 정적 등록만 쓰고
`RegisterNatives` 는 쓰지 않는다. 두 번째 인자는 `jobject` 다(object 싱글턴).

llvm-nm 으로 확인한 arm64-v8a 산출물의 내보낸 심볼 — 5개 정확히 일치:

```
T Java_com_example_digitcam_DigitVision_nativeCreate
T Java_com_example_digitcam_DigitVision_nativeDestroy
T Java_com_example_digitcam_DigitVision_nativeConfigure
T Java_com_example_digitcam_DigitVision_nativeProcessGray
T Java_com_example_digitcam_DigitVision_nativeVersion
```

⚠ **한 가지 알린다 — `nativeConfigure` 시그니처가 계약 개정 2 에서 바뀌었다.**
네 요청은 개정 1 기준이라 `(jlong, jdouble, jdouble, jint)` 였지만, 개정 2 §4 는
`(jlong, jstring)` 하나다(`vision.*` 줄 묶음을 C++ 가 파싱). 나는 **개정 2 를 따른다.**
Kotlin 쪽 `external fun nativeConfigure(handle: Long, config: String)` 로 맞춰라.
개정 1 시그니처로 선언하면 UnsatisfiedLinkError 가 난다.

### (3) OpenCV 경로 주입

CMakeLists 에 절대경로를 쓰지 않았다. 네가 주입하는 `-DOpenCV_DIR` 을 그대로 쓴다.
단 **주입값이 없을 때만** 동작하는 폴백 한 줄을 남겼다(`if(NOT DEFINED OpenCV_DIR ...)`) —
Gradle 없이 내가 CMakeLists 를 단독 configure 해 검증할 때 필요해서다. 네가 주입하면 항상 네 값이 이긴다.

`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` 는 **넣어도 되고 없어도 통과한다.** 둘 다 돌려 봤다:
CMake 4.1.2 + NDK 29.0.14206865 조합에서 이 SDK 의 `OpenCVConfig.cmake` 는 정책 에러를 내지 않았다.
계속 넘겨도 무해하니 네 쪽 설정은 그대로 둬도 된다.

### (5) 에뮬레이터 ABI 정정 반영

arm64-v8a 로 먼저 검증했다(에뮬레이터가 arm64 라는 네 정정이 맞다). 두 ABI 다 빌드된다:

| ABI | 결과 | libdigitcam.so (Release, strip 전) |
|---|---|---|
| arm64-v8a | 빌드 통과, 경고 0 (`-Wall -Wextra`) | 13.8 MB |
| x86_64 | 빌드 통과, 경고 0 | 15.9 MB |

정적 링크라 .so 자체는 크지만 OpenCV .so 를 따로 안 넣으므로 APK 총량은 공유 링크보다 작다.
Gradle 의 기본 strip 을 거치면 더 줄어든다.

### 재현 명령 (내가 돌린 그대로)

```
$CMAKE=~/Library/Android/sdk/cmake/4.1.2/bin/cmake
$NDK=~/Library/Android/sdk/ndk/29.0.14206865
$CMAKE -S android/app/src/main/cpp -B <빌드디렉터리> \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DOpenCV_DIR=<sdk>/native/jni
$CMAKE --build <빌드디렉터리> -j 8
```

### 지금 상태 (숨기지 않고 적는다)

빌드·링크·심볼·JSON 출력 형태는 위와 같이 검증했지만, **인식 알고리즘 자체는 아직 비어 있다**
(REQ-0003 진행 중, 계약 개정 2 반영해 구현 중). 지금 `nativeProcessGray` 는 전 키를 채운 JSON 을
정상 반환하되 `value` 가 항상 비어 있다. 그러니 지금 통합 빌드를 돌리면 **앱은 뜨고 버전은 표시되지만
번호판 값은 안 나온다** — 그건 네 쪽 버그가 아니다. 인식기가 들어가면 다시 알린다.

### 처리 완료 · cpp-engineer · 2026-08-13T21:28:45+0900

정적 링크 확정(libopencv_java4.so 패키징 불필요) · 타겟 digitcam · 심볼 5개 nm 확인 · nativeConfigure 는 개정2의 (jlong,jstring)

