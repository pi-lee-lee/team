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
    /**
     * **촬영 콜백이 안 올 때만** 문을 여는 상한(`camera.capture_watchdog_ms`).
     *
     * ## 🔴 이 키는 옛 `camera.shot_timeout_ms` 가 아니다 — 뜻이 반대다
     *
     * ```
     * ❌ 옛 키 : "인식이 오래 걸리면 끊는다"  → **폐기**(2026-08-27 완주 설계)
     *           인식이 10초대라 8000ms 로도 거의 항상 졌고, 끊긴 뒤 도착한 번호가 버려졌다
     * ✅ 이 키 : "`takePicture` 콜백이 아예 안 온다" → 그때만 문을 연다
     * ```
     * 🔴 **이름을 바꾼 이유가 이것이다.** 같은 이름을 두면 다음 사람이 *"시한을 늘리면
     * 완주가 되겠지"* 라고 읽는다 — 뜻이 정반대인데. **키 이름이 뜻을 나른다.**
     *
     * ⚠ **추정값이다** — 콜백이 실제로 얼마나 늦을 수 있는지 안 쟀다. 크게 잡은 것은 의도다:
     * 작으면 **느린 촬영을 고장으로 오인**한다.
     */
    val captureWatchdogMs: Long,
    /**
     * 촬영본을 몇 장까지 남기나. 넘으면 오래된 것부터 지운다.
     *
     * ⚠ **추정값이다** — 촬영본 한 장의 크기를 실기에서 재지 않았다. 서버 대장(링 100)과
     * 다른 수인 것은 의도다: 이쪽 제약은 **폰 저장공간**이고 서버 링과 아무 관계가 없다.
     */
    val shotKeepFiles: Int,
    /**
     * 인식기에 넘길 그림의 **장축 상한**. 사용자가 설정에서 고를 수 있어야 한다는 요구(2026-08-25).
     *
     * ## 🔴 왜 `vision.` 이 아니라 `camera.` 인가 — 소유가 다르다
     *
     * ```
     * vision.*  → **네이티브가 해석한다.** Kotlin 은 글자 그대로 넘기기만 한다(계약 §4)
     * camera.*  → **앱이 해석한다.** capture_watchdog_ms · shot_keep_files 가 이미 그렇다
     * ```
     * 이 값은 **앱이 그림을 줄이는 크기**이고 네이티브에는 그런 키가 없다. `vision.` 을 붙이면
     * 읽는 사람이 *"네이티브 것"* 으로 읽고, 실제로는 Kotlin 이 해석하므로 **소유가 흐려진다.**
     *
     * ⚠ **짝이 되는 축은 `vision.ocr_det_limit`(네이티브 소유)다.** 둘은 **곱으로 작동**한다 —
     * 조합표는 `assets/server.properties` 에 값과 함께 적어 뒀다.
     */
    val maxEdge: Int,

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

        /**
         * 주차장 서버의 폰 포트(`PORT_PHONE`). 정본은 `docs/net/SPEC-camera-pull.md` §0 이다.
         *
         * 데스크톱 시험 수신기(`docs/digitcam-contract.md`)는 **5500** 을 쓴다 — 그쪽에 붙일
         * 때는 `server.port` 를 적어 준다. 두 대상이 다른 포트를 쓰므로 기본값은 하나만 될 수
         * 있고, 실제로 붙을 대상인 주차장 서버를 기본값으로 둔다.
         *
         * (5500 자체의 유래: 5000 은 macOS AirPlay 수신기가 점유한다.)
         */
        private const val DEF_PORT = 8911
        private const val DEF_MIN_INTERVAL = 1000L
        private const val DEF_RECONNECT = 3000L
        private const val DEF_CONNECT_TIMEOUT = 3000
        private const val DEF_CAPTURE_WATCHDOG = ShutterGate.DEFAULT_CAPTURE_WATCHDOG_MS
        private const val DEF_SHOT_KEEP = ShotStore.DEFAULT_KEEP

        /**
         * Kotlin 이 해석하는 키 전체. `vision.*` 는 여기 없다 — 접두어로 걸러 통과시킨다.
         * 이 목록에도 없고 vision 도 아닌 키는 무시하고 로그만 남긴다.
         */
        private val KNOWN_KEYS = setOf(
            "server.host", "server.port", "server.format",
            "send.mode", "send.min_interval_ms",
            "reconnect.interval_ms",
            "socket.connect_timeout_ms", "socket.keepalive",
            "camera.capture_watchdog_ms", "camera.shot_keep_files", "camera.max_edge",
        )

        /**
         * **폐기된 키 → 무엇을 하라는 안내.** 화면 경고로 나간다.
         *
         * ⚠ 여기 있는 키는 `KNOWN_KEYS` 처럼 *"안다"* 로 취급하되 **값은 안 읽는다.**
         * 🔑 목록에서 그냥 빼면 *"알 수 없는 키"* 로 logcat 에만 남고 **사용자는 못 본다.**
         */
        private val RETIRED_KEYS = mapOf(
            "camera.shot_timeout_ms" to
                "인식 시한은 폐기됐다(인식은 완주한다). 지워라. 촬영 콜백 상한은 camera.capture_watchdog_ms 다",
        )

        /**
         * 설정 파일을 읽기 **전** 에 쓰는 값. 화면이 잠깐 이 값을 보여 준다.
         *
         * 여기에 숫자를 다시 적지 않는다 — 적으면 기본값의 진실이 두 곳이 되고, 한쪽만 고치면
         * 화면과 실제가 어긋난다. `host` 가 비어 있으므로 이 상태로는 아무 데도 붙지 않는다.
         */
        fun fallback(): AppConfig = build(Properties(), ArrayList(), null)

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
            var externalKeys: Set<String> = emptySet()
            val externalFile = externalConfigFile(context)
            if (externalFile == null) {
                warnings.add("외부 저장소를 쓸 수 없어 assets 기본값만 적용했다")
            } else {
                copyDefaultsIfAbsent(context, externalFile, warnings)
                if (externalFile.isFile) {
                    try {
                        // 🔑 **어느 키가 외부에서 왔는지**를 따로 잡아 둔다. 그냥 덮어 읽으면
                        //    합쳐진 값만 남고 **출처가 사라진다** — 아래 traceSources 가 그것을 쓴다.
                        val ext = Properties()
                        externalFile.inputStream().use { stream ->
                            InputStreamReader(stream, Charsets.UTF_8).use { ext.load(it) }
                        }
                        externalKeys = ext.stringPropertyNames()
                        for (k in externalKeys) props.setProperty(k, ext.getProperty(k))
                        externalPath = externalFile.absolutePath
                    } catch (e: IOException) {
                        warnings.add("${externalFile.absolutePath} 를 읽지 못했다: ${e.message}")
                    }
                }
            }

            // 🔴 **폐기된 키는 조용히 무시하면 안 된다.**
            //
            // 외부 설정은 첫 실행 때 assets 를 통째로 복사해 만들어지므로, **옛 키가 폰에 그대로
            // 살아 있다.** 그런데 앱은 그것을 안 읽는다 → 사용자는 *"8000 으로 고쳐 뒀는데
            // 왜 그대로냐"* 를 영원히 못 푼다. 🔑 **아무 일도 안 일어나는 것이 가장 나쁜 신호다.**
            RETIRED_KEYS.forEach { (key, note) ->
                if (props.getProperty(key) != null) warnings.add("$key 는 폐기된 키다 — $note")
            }

            props.stringPropertyNames()
                .filterNot { it in KNOWN_KEYS || it in RETIRED_KEYS || it.startsWith(VISION_PREFIX) }
                .forEach { Log.i(TAG, "알 수 없는 키 무시: $it") }

            traceSources(props, externalKeys, externalPath)
            return build(props, warnings, externalPath)
        }

        /**
         * 🔴 **지금 실제로 쓰는 값과 그 출처를 남긴다.**
         *
         * ## 왜 필요한가 — `assets` 수정이 **조용히 무효**가 된다
         *
         * 이 클래스의 규칙 둘이 겹치면 함정이 된다:
         * ```
         * ① 외부 파일이 있으면 **그쪽이 이긴다**
         * ② 최초 실행 때 복사하고 **이미 있으면 손대지 않는다**
         * → 🔴 한 번 만들어진 외부 파일은 **APK 를 다시 깔아도 안 덮인다**
         *   (덮어설치는 `getExternalFilesDir` 를 안 지운다. **데이터 삭제**여야 지워진다)
         * → 그래서 `assets` 를 고치고 리빌드해도 **그 값이 영원히 안 먹는다**
         * ```
         * ⚠ 그리고 증상이 **"고쳤는데 안 바뀐다"** 라서 **코드를 의심하게 만든다.**
         * 실제로 2026-08-26 에 `camera.shot_timeout_ms` 를 5000→8000 으로 고쳤는데
         * 폰은 계속 **정확히 5.0초**에 시한을 냈다 — 외부 파일의 옛 5000 이 이기고 있었다.
         *
         * 🔑 **이 로그가 있으면 그 종류가 다시는 안 숨는다.** 값만 찍으면 부족하다 —
         * *"무엇을 쓰는가"* 와 *"어디서 왔는가"* 는 다른 물음이고, 함정은 **뒤쪽에 있다.**
         *
         * ★ 규약 §"적혀 있다를 있다로 읽는다" 의 이 도메인 판본이다.
         */
        private fun traceSources(props: Properties, externalKeys: Set<String>, externalPath: String?) {
            Log.i(TAG, "설정 적용 — 외부파일=${externalPath ?: "없음(assets 만)"}")
            for (key in props.stringPropertyNames().sorted()) {
                val src = if (key in externalKeys) "외부" else "assets"
                Log.i(TAG, "  $key=${props.getProperty(key)}  ($src)")
            }
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

            // 🔴 **범위를 검증한다** — 큰 값이 들어오면 RSS 가 튄다(실측: 원본 4032 에서 646 MiB).
            //
            // 하한 640 : 그 밑에서는 판이 너무 작아 검출이 성립하지 않는다
            //            (실측 960 에서 이미 11/15 — 더 줄이면 더 떨어진다)
            // 상한 4096 : 표본 원본이 4032 이고 그 조합이 최고 성적(13/15)이다. 여유 한 칸만 둔다.
            //            🔑 **상한이 없으면 오타 하나가 메모리를 통째로 날린다** — 40320 을 적으면
            //            비트맵 디코드에서 그대로 죽는다. 범위를 벗어나면 기본값으로 돌리고 경고한다
            val maxEdge = int("camera.max_edge", GrayConvert.DEFAULT_MAX_EDGE).let {
                if (it in 640..4096) it
                else GrayConvert.DEFAULT_MAX_EDGE.also { d ->
                    warnings.add("camera.max_edge=$it 는 범위(640~4096)를 벗어났다 → $d")
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
                // 🔴 하한 5초. 워치독은 **드물게 · 진짜로 죽었을 때만** 짖어야 한다 —
                //    짧게 잡으면 느린 촬영을 고장으로 오인하고, 자주 짖는 워치독은 결국 꺼진다.
                captureWatchdogMs = long("camera.capture_watchdog_ms", DEF_CAPTURE_WATCHDOG)
                    .coerceAtLeast(5_000L),
                // 하한 1 — 0 이면 방금 찍은 사진도 지워서 증거가 남지 않는다.
                shotKeepFiles = int("camera.shot_keep_files", DEF_SHOT_KEEP).coerceAtLeast(1),
                maxEdge = maxEdge,
                visionConfig = visionLines(props),
                warnings = warnings.toList(),
                externalFilePath = externalPath,
            )
        }
    }
}
