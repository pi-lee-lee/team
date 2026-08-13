# digitcam 코어 검증 하네스 — 산출물이 아니라 검증 도구다.
#
# android/app/src/main/cpp/digit_pipeline.cpp 를 그대로 컴파일해(사본을 만들지 않는다)
# 계약 §5 의 stable/fresh 래치와 JSON 출력 형태를 실제로 돌려 본다.
# 호스트에 OpenCV 가 없으므로 안드로이드용으로 빌드해 에뮬레이터에서 실행한다.
cmake_minimum_required(VERSION 3.22)

project(digitcam_verify CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DIGITCAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../android/app/src/main/cpp")

if(NOT DEFINED OpenCV_DIR OR OpenCV_DIR STREQUAL "")
  set(OpenCV_DIR "$ENV{HOME}/opencv-android-sdk/OpenCV-android-sdk/sdk/native/jni")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc)

add_executable(core_test
  core_test.cpp
  "${DIGITCAM_CORE_DIR}/digit_pipeline.cpp"
)

target_include_directories(core_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(core_test PRIVATE -Wall -Wextra)
target_link_libraries(core_test PRIVATE ${OpenCV_LIBS})

# samples/plates 전체를 expected.json 과 대조해 점수를 내는 하네스(계약 §9.6)
add_executable(plate_test
  plate_test.cpp
  "${DIGITCAM_CORE_DIR}/digit_pipeline.cpp"
)

target_include_directories(plate_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(plate_test PRIVATE -Wall -Wextra)
target_link_libraries(plate_test PRIVATE ${OpenCV_LIBS})

# 움직임 게이트 임계 실측(REQ-0014). 정지 이미지로는 시험되지 않는 값이라 프레임 열이 필요하다.
add_executable(motion_test
  motion_test.cpp
  "${DIGITCAM_CORE_DIR}/digit_pipeline.cpp"
)

target_include_directories(motion_test PRIVATE "${DIGITCAM_CORE_DIR}")
target_compile_options(motion_test PRIVATE -Wall -Wextra)
target_link_libraries(motion_test PRIVATE ${OpenCV_LIBS})
