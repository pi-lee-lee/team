package com.example.digitcam

import android.content.Context
import android.util.Log
import java.io.File
import java.io.IOException
import java.io.InputStreamReader
import java.util.Properties

/**
 * 계약 §6 의 `server.properties`.
 *
 * 서버 IP·포트가 미정이라 **리빌드 없이 바꿀 수 있어야 한다.** 그래서 값의 출처가 둘이다.
 *
 *  1. `assets/server.properties`                       — 앱에 번들되는 기본값
 *  2. `getExternalFilesDir(null)/server.properties`     — 있으면 이쪽이 이긴다
 *
 * 최초 실행 시 1을 2의 위치로 복사해 둔다(이미 있으면 건드리지 않는다). 그래야 사용자가
 * adb push 든 파일 관리자든으로 찾아서 고칠 수 있다.
 *
 * ## `vision.*` 는 해석하지 않는다 (개정 2)
 *
 * 임계값은 Kotlin 이 읽지 않고 [visionConfig] 문자열로 묶어 네이티브에 통째로 넘긴다.
 * 임계값이 늘어날 때마다 Kotlin·JNI 시그니처가 따라 깨지는 것을 막기 위해서다.
 * **여기에 vision 값 검증 코드를 추가하지 마라** — 그 순간 계약이 두 곳으로 갈라진다.
 */
data class AppConfig(
    val host: String,
    val port: Int,
    val format: Format,
    val sendMode: SendMode,
    val minIntervalMs: Long,
    val reconnectIntervalMs: Long,
    val connectTimeoutMs: Int,
    val keepAlive: Boolean,
    /** `vision.` 으로 시작하는 줄만 모은 원문. 그대로 `nativeConfigure` 로 간다. */
    val visionConfig: String,
    /** 값이 깨져서 기본값으로 폴백한 키들. 화면에 경고로 띄운다(계약 §6). */
    val warnings: List<String>,
    /** 외부 파일에서 덮어썼는지. 화면에 어느 파일이 먹었는지 보여주려고 들고 있다. */
    val externalFilePath: String?,
) {
    enum class Format { JSON, PLAIN }
    enum class SendMode { ON_FRESH, EVERY_STABLE_FRAME }

    /** host 가 비어 있으면 전송하지 않는다(계약 §6). */
    val isServerConfigured: Boolean get() = host.isNotBlank()

    companion object {
        private const val TAG = "AppConfig"
        const val FILE_NAME = "server.properties"

        private const val VISION_PREFIX = "vision."

        // 5000 은 macOS AirPlay 수신기가 점유한다 — 계약 개정으로 5500 이 기본값이다.
        private const val DEF_PORT = 5500
        private const val DEF_MIN_INTERVAL = 1000L
        private const val DEF_RECONNECT = 3000L
        private const val DEF_CONNECT_TIMEOUT = 3000

        /**
         * Kotlin 이 해석하는 키 전체. `vision.*` 는 여기 없다 — 접두어로 걸러 통과시킨다.
         * 이 목록에도 없고 vision 도 아닌 키는 무시하고 로그만 남긴다.
         */
        private val KNOWN_KEYS = setOf(
            "server.host", "server.port", "server.format",
            "send.mode", "send.min_interval_ms",
            "reconnect.interval_ms",
            "socket.connect_timeout_ms", "socket.keepalive",
        )

        /** 파일이 없거나 깨져도 예외를 던지지 않는다. 설정 때문에 앱이 죽으면 안 된다. */
        fun load(context: Context): AppConfig {
            val warnings = ArrayList<String>()
            val props = Properties()

            // Properties.load(InputStream) 은 ISO-8859-1 로 읽는다. 지금은 값이 전부 ASCII 지만
            // 나중에 한글이 들어가면 조용히 깨지므로 처음부터 UTF-8 Reader 로 읽는다.
            try {
                context.assets.open(FILE_NAME).use { stream ->
                    InputStreamReader(stream, Charsets.UTF_8).use { props.load(it) }
                }
            } catch (e: IOException) {
                warnings.add("assets/$FILE_NAME 을 읽지 못했다: ${e.message}")
            }

            // getExternalFilesDir 는 null 을 돌려줄 수 있다(외부 저장소 마운트 해제).
            // 그 경우 assets 값만으로 간다 — 크래시가 아니라 폴백이다.
            var externalPath: String? = null
            val externalFile = externalConfigFile(context)
            if (externalFile == null) {
                warnings.add("외부 저장소를 쓸 수 없어 assets 기본값만 적용했다")
            } else {
                copyDefaultsIfAbsent(context, externalFile, warnings)
                if (externalFile.isFile) {
                    try {
                        externalFile.inputStream().use { stream ->
                            InputStreamReader(stream, Charsets.UTF_8).use { props.load(it) }
                        }
                        externalPath = externalFile.absolutePath
                    } catch (e: IOException) {
                        warnings.add("${externalFile.absolutePath} 를 읽지 못했다: ${e.message}")
                    }
                }
            }

            props.stringPropertyNames()
                .filterNot { it in KNOWN_KEYS || it.startsWith(VISION_PREFIX) }
                .forEach { Log.i(TAG, "알 수 없는 키 무시: $it") }

            return build(props, warnings, externalPath)
        }

        /** 외부 설정 파일 경로. 외부 저장소를 못 쓰면 null. */
        fun externalConfigFile(context: Context): File? =
            context.getExternalFilesDir(null)?.let { File(it, FILE_NAME) }

        /** 최초 실행 시 assets 기본값을 외부 경로로 복사한다. 이미 있으면 손대지 않는다. */
        private fun copyDefaultsIfAbsent(context: Context, target: File, warnings: MutableList<String>) {
            if (target.exists()) return
            try {
                target.parentFile?.mkdirs()
                context.assets.open(FILE_NAME).use { input ->
                    target.outputStream().use { output -> input.copyTo(output) }
                }
                Log.i(TAG, "기본 설정을 ${target.absolutePath} 로 복사했다")
            } catch (e: IOException) {
                warnings.add("기본 설정 복사 실패: ${e.message}")
            }
        }

        /**
         * `vision.` 줄만 골라 원문 그대로 이어 붙인다.
         *
         * 병합된 Properties 에서 뽑으므로 외부 파일의 오버라이드가 반영되고, 값 자체는
         * 손대지 않는다. 키 순서는 정렬해 고정한다 — 네이티브는 순서에 의존하지 않지만
         * 로그로 비교할 때 사람이 읽기 쉽다.
         */
        private fun visionLines(props: Properties): String =
            props.stringPropertyNames()
                .filter { it.startsWith(VISION_PREFIX) }
                .sorted()
                .joinToString("\n") { "$it=${props.getProperty(it)?.trim().orEmpty()}" }

        private fun build(props: Properties, warnings: MutableList<String>, externalPath: String?): AppConfig {
            fun raw(key: String): String? = props.getProperty(key)?.trim()

            fun int(key: String, def: Int): Int {
                val v = raw(key) ?: return def
                if (v.isEmpty()) return def
                return v.toIntOrNull() ?: def.also { warnings.add("$key='$v' 는 정수가 아니다 → $def") }
            }

            fun long(key: String, def: Long): Long {
                val v = raw(key) ?: return def
                if (v.isEmpty()) return def
                return v.toLongOrNull() ?: def.also { warnings.add("$key='$v' 는 정수가 아니다 → $def") }
            }

            fun bool(key: String, def: Boolean): Boolean {
                val v = raw(key)?.lowercase() ?: return def
                return when (v) {
                    "true", "1", "yes", "on" -> true
                    "false", "0", "no", "off" -> false
                    "" -> def
                    else -> def.also { warnings.add("$key='$v' 는 참/거짓이 아니다 → $def") }
                }
            }

            val port = int("server.port", DEF_PORT).let {
                if (it in 1..65535) it
                else DEF_PORT.also { _ -> warnings.add("server.port=$it 는 범위를 벗어났다 → $DEF_PORT") }
            }

            val format = when (raw("server.format")?.lowercase()) {
                null, "", "json" -> Format.JSON
                "plain" -> Format.PLAIN
                else -> Format.JSON.also {
                    warnings.add("server.format='${raw("server.format")}' 은 json|plain 이 아니다 → json")
                }
            }

            val sendMode = when (raw("send.mode")?.lowercase()) {
                null, "", "on_fresh" -> SendMode.ON_FRESH
                "every_stable_frame" -> SendMode.EVERY_STABLE_FRAME
                else -> SendMode.ON_FRESH.also {
                    warnings.add("send.mode='${raw("send.mode")}' 을 알 수 없다 → on_fresh")
                }
            }

            return AppConfig(
                host = raw("server.host").orEmpty(),
                port = port,
                format = format,
                sendMode = sendMode,
                minIntervalMs = long("send.min_interval_ms", DEF_MIN_INTERVAL).coerceAtLeast(0L),
                reconnectIntervalMs = long("reconnect.interval_ms", DEF_RECONNECT).coerceAtLeast(100L),
                connectTimeoutMs = int("socket.connect_timeout_ms", DEF_CONNECT_TIMEOUT).coerceAtLeast(100),
                keepAlive = bool("socket.keepalive", true),
                visionConfig = visionLines(props),
                warnings = warnings.toList(),
                externalFilePath = externalPath,
            )
        }
    }
}
