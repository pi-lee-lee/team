# digitcam — 네이티브 라이브러리 빌드 정의
#
# Kotlin 이 System.loadLibrary("digitcam") 으로 적재한다(계약 §2).
# 타겟명이 곧 libdigitcam.so 파일명이므로 바꾸면 UnsatisfiedLinkError 가 난다.
#
# OpenCV 경로는 android-engineer 가 build.gradle.kts 에서 주입한다(REQ-0006):
#     -DOpenCV_DIR=<opencv sdk>/native/jni
#     -DCMAKE_POLICY_VERSION_MINIMUM=3.5
# 그래서 여기에는 절대경로를 쓰지 않는다. 아래 폴백은 Gradle 없이(내 쪽 스파이크·검증
# 빌드에서) 이 CMakeLists 를 단독으로 configure 할 때만 쓰이며, 주입값이 있으면 항상 그쪽이 이긴다.
cmake_minimum_required(VERSION 3.22.1)

project(digitcam CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED OpenCV_DIR OR OpenCV_DIR STREQUAL "")
  set(OpenCV_DIR "$ENV{HOME}/opencv-android-sdk/OpenCV-android-sdk/sdk/native/jni")
  message(STATUS "[digitcam] OpenCV_DIR 주입값이 없어 폴백 경로를 쓴다: ${OpenCV_DIR}")
endif()

# core+imgproc 만 쓴다. SDK 의 abi-<ABI>/OpenCVConfig.cmake 가 OpenCV_SHARED=OFF 이므로
# 이 find_package 는 staticlibs/<ABI>/*.a 를 잡는다 = 정적 링크.
# 따라서 APK 에 libopencv_java4.so 를 넣을 필요가 없고 OpenCVLoader 초기화도 필요 없다(계약 §3).
find_package(OpenCV REQUIRED COMPONENTS core imgproc)

message(STATUS "[digitcam] OpenCV_VERSION = ${OpenCV_VERSION}")
message(STATUS "[digitcam] OpenCV_SHARED  = ${OpenCV_SHARED} (OFF = 정적 링크)")

add_library(digitcam SHARED
  native-lib.cpp
  digit_pipeline.cpp
)

target_compile_options(digitcam PRIVATE -Wall -Wextra)

# Android 로그 라이브러리(__android_log_print). 네이티브 디버깅 시 바로 쓸 수 있게 연결해 둔다.
# 로그는 native-lib.cpp 쪽에서만 쓴다 — digit_pipeline 은 안드로이드에 의존하지 않는다.
find_library(log-lib log)

target_link_libraries(digitcam PRIVATE ${OpenCV_LIBS} ${log-lib})
