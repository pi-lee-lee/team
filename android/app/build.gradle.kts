import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.example.jnidemo"
    compileSdk = 35

    // 이 머신에 설치된 NDK. 버전을 고정하지 않으면 AGP 가 기본값을 내려받으려 한다.
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.example.jnidemo"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        ndk {
            // 로컬 검증용 두 ABI 만. 배포 시에는 armeabi-v7a 등을 추가한다.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    // 네이티브(C++) 소스와 CMakeLists 는 cpp-engineer 소유다(REQ-0002).
    // 여기서는 그 경로를 가리키기만 한다 — 경로를 바꾸면 소유권 경계가 깨진다.
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
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
    // 의존성 없음 — 순수 프레임워크 API 만 쓰는 최소 예제다.
}
