import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.example.digitcam"
    compileSdk = 35

    // 🔵 **NDK 를 요구하지 않는다.** 네이티브를 여기서 빌드하지 않기 때문이다 —
    //    `app/src/main/jniLibs/` 의 **미리 빌드된 `.so`** 를 그대로 싣는다.
    //    ⚠ 그래서 이 폴더는 Android SDK 와 JDK 만 있으면 빌드된다. OpenCV SDK 도 필요 없다.

    defaultConfig {
        applicationId = "com.example.digitcam"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        ndk {
            // 🔴 **배포 대상만 싣는다** — 실기기는 arm64-v8a 다.
            //    x86_64 를 뺀 이유는 `libs/PROVENANCE.md` 에 값과 함께 적었다.
            abiFilters += listOf("arm64-v8a")
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

    testOptions {
        unitTests {
            // 🔴 이것을 켜지 않으면 android.jar 스텁이 `RuntimeException("Stub!")` 을 던진다.
            // 켜면 프레임워크 API 가 **조용히 기본값을 돌려준다** — 그래서 `TcpSender` 를
            // JVM 에서 돌릴 수 있다(그것이 쓰는 프레임워크 API 는 android.util.Log 뿐이다).
            //
            // ⚠ 대가: 다른 프레임워크 API 를 쓰는 코드가 들어오면 그 호출이 **조용히 0/null 이 된다.**
            //   `SystemClock.elapsedRealtime()` 은 0, `Build.MODEL` 은 null 이다.
            //   그래서 TcpSenderTest 는 그 둘을 타는 경로(push 전송)를 **일부러 밟지 않는다.**
            isReturnDefaultValues = true
        }
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

    // JVM 단위 시험. 카메라 pull 의 읽는 루프(ShotLink)·줄 조립(LineReader)·대기 관리
    // (ShotCoordinator)·전선 형식(CameraShot)이 Android API 를 쓰지 않으므로 여기서 돈다.
    // 🔑 org.json 을 시험 의존성으로 넣지 않는다 — 프레임워크의 org.json 은 단위 시험
    //    클래스패스에서 기본값만 돌려주는 껍데기가 되므로, 그 대신 위 넷을 순수 Kotlin 으로 뒀다.
    testImplementation("junit:junit:4.13.2")
}
