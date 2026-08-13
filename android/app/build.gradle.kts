import org.jetbrains.kotlin.gradle.dsl.JvmTarget
// Kotlin DSL 에서는 `java` 가 Gradle 의 java 확장으로 먼저 해석되어 java.util.* 를
// 완전수식으로 쓸 수 없다. 그래서 import 로 가져온다.
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// ---------------------------------------------------------------------------
// OpenCV SDK 위치 — 계약 §3
//
// 경로를 소스에 하드코딩하지 않는다. local.properties 의 opencv.dir 를 우선 쓰고,
// 없으면 루트가 풀어 둔 기본 경로로 폴백한다. CMakeLists 는 cpp-engineer 소유라
// 그쪽에서 경로를 찾게 하지 않고 여기서 -DOpenCV_DIR 로 주입한다(REQ-0006 으로 합의).
// ---------------------------------------------------------------------------
val localProps = Properties().apply {
    val f = rootProject.file("local.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

val opencvSdkDir: String = localProps.getProperty("opencv.dir")
    ?: "${System.getProperty("user.home")}/opencv-android-sdk/OpenCV-android-sdk/sdk"

/**
 * cpp-engineer 가 OpenCV 를 **공유 라이브러리로** 링크했다면 libopencv_java4.so 를 APK 에
 * 넣어야 한다. 정적 링크(계약 §3 의 우선안)라면 넣으면 안 된다 — 24MB 를 헛되이 싣는다.
 *
 * 기본값은 정적 가정(false). REQ-0006 으로 cpp-engineer 에게 어느 쪽인지 회신을 요청해 뒀고,
 * 공유로 확정되면 local.properties 에 opencv.packageShared=true 를 넣는다.
 */
val packageSharedOpenCv: Boolean =
    (localProps.getProperty("opencv.packageShared") ?: "false").toBooleanStrict()

android {
    namespace = "com.example.digitcam"
    compileSdk = 35

    // 이 머신에 설치된 NDK. 버전을 고정하지 않으면 AGP 가 기본값을 내려받으려 한다.
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.example.digitcam"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        ndk {
            // 실기기(arm64) + 이 머신의 에뮬레이터.
            // 참고: emulator-5554 는 sdk_gphone64_arm64 라 x86_64 가 아니라 arm64-v8a 를 쓴다.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    // CMakeLists 안에서 find_package(OpenCV REQUIRED) 만 부르면 되도록 경로를 넘긴다.
                    "-DOpenCV_DIR=$opencvSdkDir/native/jni",
                    // 계약 §10 — CMake 4.1.2 는 cmake_minimum_required(VERSION 3.5 미만) 호환을 끊었다.
                    // OpenCVConfig.cmake 를 include 할 때 configure 가 깨지는 것을 막는다.
                    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
                )
            }
        }
    }

    // 네이티브(C++) 소스와 CMakeLists 는 cpp-engineer 소유다(REQ-0003/0006).
    // 여기서는 그 경로를 가리키기만 한다 — 경로를 바꾸면 소유권 경계가 깨진다.
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
    }

    if (packageSharedOpenCv) {
        sourceSets["main"].jniLibs.srcDir("$opencvSdkDir/native/libs")
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    // CameraX — 프리뷰 + 프레임 분석. 1.4.2 는 compileSdk 35 / AGP 8.6 조합에서 검증된 최신 안정판이다.
    val cameraX = "1.4.2"
    implementation("androidx.camera:camera-core:$cameraX")
    implementation("androidx.camera:camera-camera2:$cameraX")
    implementation("androidx.camera:camera-lifecycle:$cameraX")
    implementation("androidx.camera:camera-view:$cameraX")

    // ComponentActivity(권한 요청 계약) + lifecycleScope.
    // AppCompat 은 일부러 넣지 않는다 — 프레임워크 테마/위젯만 쓰므로 필요 없다.
    implementation("androidx.activity:activity-ktx:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.core:core-ktx:1.13.1")

    // 소켓 전송용. 카메라 스레드와 분리한다(계약 §7).
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")

    // 결과 JSON 파싱은 org.json (프레임워크 내장) — 추가 의존성 없음(계약 §5).
}
