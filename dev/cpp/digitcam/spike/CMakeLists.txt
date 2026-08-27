# digitcam 링크 스파이크 — OpenCV 4.14.0 정적 링크(core+imgproc)가 NDK 로 실제로 되는지만 본다.
#
# 여기는 산출물이 아니다. android/app/src/main/cpp/CMakeLists.txt 를 건드리기 전에
# (1) find_package 가 CMake 4.1.2 에서 통과하는지 (2) 정적 링크가 되는지 (3) 두 ABI 다 되는지
# 를 확인하는 용도다. 확인이 끝나면 같은 구성을 산출물 CMakeLists 로 옮긴다.
cmake_minimum_required(VERSION 3.22)

project(digitcam_spike CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 계약 §3: 경로를 하드코딩하지 않는다. -DOpenCV_DIR 로 주입하고 없으면 기본 경로로 폴백한다.
if(NOT DEFINED OpenCV_DIR OR OpenCV_DIR STREQUAL "")
  set(OpenCV_DIR "$ENV{HOME}/opencv-android-sdk/OpenCV-android-sdk/sdk/native/jni")
endif()
message(STATUS "[spike] OpenCV_DIR = ${OpenCV_DIR}")

find_package(OpenCV REQUIRED COMPONENTS core imgproc)

message(STATUS "[spike] OpenCV_VERSION = ${OpenCV_VERSION}")
message(STATUS "[spike] OpenCV_SHARED  = ${OpenCV_SHARED}")
message(STATUS "[spike] OpenCV_LIBS    = ${OpenCV_LIBS}")

# (a) 실기기에 올라갈 형태 — 공유 라이브러리
add_library(digitcam_spike SHARED spike_lib.cpp)
target_compile_options(digitcam_spike PRIVATE -Wall -Wextra)
target_link_libraries(digitcam_spike PRIVATE ${OpenCV_LIBS})

# (b) 에뮬레이터에서 직접 돌려 볼 실행 파일 — 인식기 검증에도 이 경로를 쓴다.
add_executable(spike_exe spike_main.cpp)
target_compile_options(spike_exe PRIVATE -Wall -Wextra)
target_link_libraries(spike_exe PRIVATE ${OpenCV_LIBS})
