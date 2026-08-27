# digitcam 코어 검증 하네스 — 산출물이 아니라 검증 도구다.
#
# android/app/src/main/cpp/digit_pipeline.cpp 를 그대로 컴파일해(사본을 만들지 않는다)
# 계약 §5 의 stable/fresh 래치와 JSON 출력 형태를 실제로 돌려 본다.
# 호스트에도 OpenCV 를 쓸 수 있다 — brew 의 opencv@4 가 앱의 안드로이드 SDK 와 같은 4.14.0 이다:
#   PKG_CONFIG_PATH=/opt/homebrew/opt/opencv@4/lib/pkgconfig \
#   cmake -S . -B <빌드> -DOpenCV_DIR=/opt/homebrew/opt/opencv@4/lib/cmake/opencv4
# 안드로이드용으로 빌드해 에뮬레이터에서 돌릴 수도 있다(OpenCV_DIR 기본값이 그쪽이다).
cmake_minimum_required(VERSION 3.22)

project(digitcam_verify CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DIGITCAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../android/app/src/main/cpp")

if(NOT DEFINED OpenCV_DIR OR OpenCV_DIR STREQUAL "")
  set(OpenCV_DIR "$ENV{HOME}/opencv-android-sdk/OpenCV-android-sdk/sdk/native/jni")
endif()

# imgcodecs 는 photo_test 가 **입력 JPEG 를 읽는 데만** 쓴다. 인식 경로(digit_pipeline)는
# core+imgproc 만 쓰므로 앱의 링크 조건과 갈리지 않는다.
# dnn 은 표준 경로(plate_ocr)가 쓴다. 앱도 같은 컴포넌트를 링크한다.
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs dnn)

add_executable(core_test
  core_test.cpp
  "${DIGITCAM_CORE_DIR}/digit_pipeline.cpp"
  "${DIGITCAM_CORE_DIR}/plate_ocr.cpp"
)

target_include_directories(core_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(core_test PRIVATE -Wall -Wextra)
target_link_libraries(core_test PRIVATE ${OpenCV_LIBS})

# samples/plates 전체를 expected.json 과 대조해 점수를 내는 하네스(계약 §9.6)
add_executable(plate_test
  plate_test.cpp
  "${DIGITCAM_CORE_DIR}/digit_pipeline.cpp"
  "${DIGITCAM_CORE_DIR}/plate_ocr.cpp"
)

target_include_directories(plate_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(plate_test PRIVATE -Wall -Wextra)
target_link_libraries(plate_test PRIVATE ${OpenCV_LIBS})

# 움직임 게이트 임계 실측(REQ-0014). 정지 이미지로는 시험되지 않는 값이라 프레임 열이 필요하다.
add_executable(motion_test
  motion_test.cpp
  "${DIGITCAM_CORE_DIR}/digit_pipeline.cpp"
  "${DIGITCAM_CORE_DIR}/plate_ocr.cpp"
)

target_include_directories(motion_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(motion_test PRIVATE -Wall -Wextra)
target_link_libraries(motion_test PRIVATE ${OpenCV_LIBS})

# 실사용 환경 사진(JPEG)으로 인식률과 해상도 스윕을 재는 하네스.
# PGM 하네스(plate_test)와 갈라 둔 이유: 이쪽은 **앱의 촬영본 경로**(축소 산술·BT.601 절삭)를
# 재현해야 하고, 입력이 카메라가 찍은 원본 크기 JPEG 다.
add_executable(photo_test
  photo_test.cpp
  "${DIGITCAM_CORE_DIR}/digit_pipeline.cpp"
  "${DIGITCAM_CORE_DIR}/plate_ocr.cpp"
)

target_include_directories(photo_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(photo_test PRIVATE -Wall -Wextra)
target_link_libraries(photo_test PRIVATE ${OpenCV_LIBS})

# 표준 경로(검출망 + CTC 인식망) 채점 하니스. 앱과 같은 plate_ocr.cpp 를 그대로 링크한다.
add_executable(ocr_test
  ocr_test.cpp
  "${DIGITCAM_CORE_DIR}/plate_ocr.cpp"
)

target_include_directories(ocr_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(ocr_test PRIVATE -Wall -Wextra)
target_link_libraries(ocr_test PRIVATE ${OpenCV_LIBS})
