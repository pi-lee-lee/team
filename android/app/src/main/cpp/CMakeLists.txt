# jnidemo — 네이티브 라이브러리 빌드 정의
#
# 이 버전은 Gradle 의 externalNativeBuild { cmake { version "3.22.1" } } 와 맞춰 고정한다.
# 임의로 올리면 Gradle 쪽 설정과 어긋나므로 변경하지 말 것.
cmake_minimum_required(VERSION 3.22.1)

project(jnidemo)

# Kotlin 이 System.loadLibrary("jnidemo") 로 적재한다.
# 타겟명이 곧 libjnidemo.so 파일명이므로 이름을 바꾸면 UnsatisfiedLinkError 가 난다.
add_library(jnidemo SHARED native-lib.cpp)

target_compile_options(jnidemo PRIVATE -Wall -Wextra)

# Android 로그 라이브러리(__android_log_print). 지금은 쓰지 않지만
# 네이티브 디버깅 시 바로 쓸 수 있도록 연결해 둔다.
find_library(log-lib log)

target_link_libraries(jnidemo PRIVATE ${log-lib})
