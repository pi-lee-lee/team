// 루트 빌드 스크립트 — 플러그인 버전만 선언하고 적용은 각 모듈에서 한다.
plugins {
    id("com.android.application") version "8.6.0" apply false
    id("org.jetbrains.kotlin.android") version "2.2.10" apply false
}
