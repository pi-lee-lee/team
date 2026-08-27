package com.example.digitcam

import android.os.Bundle
import android.text.InputType
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.activity.ComponentActivity

/**
 * 설정 입력 화면 (계약 §6.1).
 *
 * **이 화면은 설정 파일을 대체하지 않는다. 그 파일을 편집하는 도구다.**
 * 저장하면 `getExternalFilesDir()/server.properties` 에 써 넣고, 호출한 쪽이 기존
 * "설정 다시 읽기" 와 **같은 경로로** 재적용한다. adb 로 같은 파일을 고치는 방법도 그대로 살아 있다.
 *
 * 이 화면이 존재하는 이유는 Android 11+ 실기기에서 `Android/data/` 안을 폰만으로는 열 수 없기
 * 때문이다. 그 상태로는 서버 IP 하나 바꾸려고 노트북이 필요해서, "리빌드 없이 바꾼다" 는
 * 요구가 폰에서 절반만 달성된다. 에뮬레이터는 adb 가 늘 붙어 있어 이 문제가 보이지 않았다.
 */
class SettingsActivity : ComponentActivity() {

    private lateinit var hostInput: EditText
    private lateinit var portInput: EditText
    private lateinit var formatSpinner: Spinner
    private lateinit var sendModeSpinner: Spinner
    private lateinit var minIntervalInput: EditText
    private lateinit var reconnectInput: EditText
    private lateinit var connectTimeoutInput: EditText
    private lateinit var keepAliveCheck: CheckBox
    private lateinit var visionContainer: LinearLayout
    private lateinit var filePathText: TextView

    /**
     * `vision.` 키 → 입력칸. **키 목록은 파일에서 온다 — 여기에 상수로 적지 않는다.**
     * 그래야 cpp-engineer 가 임계값을 추가해도 이 화면이 자동으로 따라간다
     * (`nativeConfigure` 를 문자열 하나로 받기로 한 것과 같은 이유다).
     */
    private val visionInputs = LinkedHashMap<String, EditText>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // 메인과 같은 방향을 따른다 — 메인만 가로인데 설정만 세로면 열 때마다 화면이 돌아간다.
        // setContentView 보다 먼저 부른다(방향이 다르면 여기서 재생성된다).
        OrientationPref.apply(this)
        setContentView(R.layout.activity_settings)
        title = getString(R.string.cfg_title)
        applyWindowInsets()

        hostInput = findViewById(R.id.hostInput)
        portInput = findViewById(R.id.portInput)
        formatSpinner = findViewById(R.id.formatSpinner)
        sendModeSpinner = findViewById(R.id.sendModeSpinner)
        minIntervalInput = findViewById(R.id.minIntervalInput)
        reconnectInput = findViewById(R.id.reconnectInput)
        connectTimeoutInput = findViewById(R.id.connectTimeoutInput)
        keepAliveCheck = findViewById(R.id.keepAliveCheck)
        visionContainer = findViewById(R.id.visionContainer)
        filePathText = findViewById(R.id.filePathText)

        formatSpinner.adapter = spinnerAdapter(FORMATS)
        sendModeSpinner.adapter = spinnerAdapter(SEND_MODES)

        findViewById<Button>(R.id.saveButton).setOnClickListener { save() }
        findViewById<Button>(R.id.restoreButton).setOnClickListener { restoreDefaults() }
        findViewById<Button>(R.id.cancelButton).setOnClickListener { finish() }
        findViewById<Button>(R.id.noticesButton).setOnClickListener { showNotices() }

        populate()
    }

    /**
     * 오픈소스 고지를 띄운다. **Apache-2.0 §4(d) 의 의무다.**
     *
     * 모델(PaddleOCR·RapidOCR)·OpenCV·AndroidX 가 전부 Apache-2.0 이고, 배포물에는
     * 라이선스와 귀속 고지가 실려야 한다. 원문은 `res/raw/third_party_notices.txt` 이고
     * 여기서는 **읽어서 보여 주기만** 한다 — 문구를 코드에 복사하면 두 벌이 갈린다.
     *
     * ⚠ `korean_rec_static.onnx` 는 원본을 고친 것이라 **변경 고지**(§4(b))가 그 파일 안에 있다.
     * 모델을 바꾸면 그 문단도 같이 고쳐야 한다.
     */
    private fun showNotices() {
        val text = try {
            resources.openRawResource(R.raw.third_party_notices)
                .use { it.readBytes().toString(Charsets.UTF_8) }
        } catch (e: Exception) {
            getString(R.string.cfg_notices_failed) + ": ${e.message}"
        }
        val view = android.widget.TextView(this).apply {
            setText(text)
            textSize = 11f
            setTextIsSelectable(true)
            val pad = (16 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        android.app.AlertDialog.Builder(this)
            .setTitle(R.string.cfg_notices)
            .setView(android.widget.ScrollView(this).apply { addView(view) })
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }

    /**
     * 시스템 바·키보드 영역만큼 안쪽 여백을 준다.
     *
     * **`targetSdk = 35` 이면 Android 15 가 edge-to-edge 를 강제한다** — 시스템이 여백을 넣어
     * 주지 않으므로 앱이 상태바 아래까지 자기 배경을 그리고, 맨 위 입력칸(`server.host`)이
     * 상태바에 가려 손이 닿지 않는다. 이 화면을 만든 이유가 폰에서 서버 주소를 넣는 것이므로
     * 그게 가려지면 화면 전체가 무의미하다.
     *
     * 아래쪽은 `systemBars` 와 `ime` 중 **큰 쪽**을 쓴다. 그래야 키보드가 올라왔을 때
     * 저장·취소 버튼이 키보드 위로 밀려 올라가고, ScrollView 가 줄어들어 편집 중인 칸이
     * 가려지지 않는다.
     */
    private fun applyWindowInsets() {
        val root = findViewById<LinearLayout>(R.id.settingsRoot)
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
            val bars = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars())
            val ime = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.ime())
            view.setPadding(bars.left, bars.top, bars.right, maxOf(bars.bottom, ime.bottom))
            insets
        }

        // 밝은 배경 위에 흰 상태바 아이콘이 찍혀 시계가 안 보이는 문제. 이 화면은 밝은 테마다.
        androidx.core.view.WindowInsetsControllerCompat(window, root).isAppearanceLightStatusBars = true
    }

    private fun spinnerAdapter(values: List<String>) =
        ArrayAdapter(this, android.R.layout.simple_spinner_item, values).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }

    // ---------------------------------------------------------------- 채우기

    /**
     * 파일에서 다시 읽어 화면을 채운다(계약 §6.1 4번).
     *
     * 실제 적용과 **같은 병합 순서**(assets → 외부파일)를 쓴다. 표시용으로 따로 계산하면
     * 화면에 뜬 값과 네이티브에 들어간 값이 어긋날 수 있고, 그러면 이 화면은 거짓말을 하게 된다.
     */
    private fun populate() {
        val props = PropertiesFile.readMerged(this)

        hostInput.setText(props.getProperty("server.host").orEmpty().trim())
        portInput.setText(props.getProperty("server.port").orEmpty().trim())
        minIntervalInput.setText(props.getProperty("send.min_interval_ms").orEmpty().trim())
        reconnectInput.setText(props.getProperty("reconnect.interval_ms").orEmpty().trim())
        connectTimeoutInput.setText(props.getProperty("socket.connect_timeout_ms").orEmpty().trim())

        selectSpinner(formatSpinner, FORMATS, props.getProperty("server.format")?.trim(), "json")
        selectSpinner(sendModeSpinner, SEND_MODES, props.getProperty("send.mode")?.trim(), "on_fresh")

        keepAliveCheck.isChecked =
            props.getProperty("socket.keepalive")?.trim()?.lowercase() !in setOf("false", "0", "no", "off")

        buildVisionRows(props)

        filePathText.text = getString(
            R.string.cfg_file_path,
            AppConfig.externalConfigFile(this)?.absolutePath ?: getString(R.string.cfg_no_external),
        )
    }

    private fun selectSpinner(spinner: Spinner, values: List<String>, raw: String?, fallback: String) {
        val idx = values.indexOf(raw?.lowercase() ?: fallback).takeIf { it >= 0 }
            ?: values.indexOf(fallback)
        spinner.setSelection(idx.coerceAtLeast(0))
    }

    /**
     * `vision.` 으로 시작하는 키를 **파일에 있는 그대로** 라벨 + 자유 입력칸으로 그린다.
     * 값의 의미를 해석하지 않으므로 형식 검사도 하지 않는다 — 해석은 전부 네이티브 몫이다.
     */
    private fun buildVisionRows(props: java.util.Properties) {
        visionContainer.removeAllViews()
        visionInputs.clear()

        // 🔑 `vision.` 만이 아니라 **`camera.` 도 같은 방식으로** 그린다.
        //
        // 왜 : 사용자가 "설정에서 고를 수 있나" 고 물은 손잡이(`camera.max_edge`)가 여기 있는데,
        //      그 키 하나만 전용 행으로 넣으면 **같은 접두의 다른 키(shot_timeout_ms 등)는
        //      여전히 화면에 없다.** 규칙이 둘이 되고, 다음에 키가 늘면 또 갈린다.
        // ⚠ 값의 의미는 여전히 해석하지 않는다 — 파일에 있는 대로 보여 주고 그대로 돌려 쓴다.
        //    (`camera.*` 는 앱이, `vision.*` 은 네이티브가 해석한다. 이 화면은 **편집기**일 뿐이다)
        val keys = props.stringPropertyNames()
            .filter { it.startsWith(VISION_PREFIX) || it.startsWith(CAMERA_PREFIX) }
            .sorted()
        if (keys.isEmpty()) {
            visionContainer.addView(
                TextView(this).apply {
                    text = getString(R.string.cfg_vision_empty)
                    textSize = 12f
                },
            )
            return
        }

        for (key in keys) {
            val label = TextView(this).apply {
                text = key
                textSize = 13f
                setPadding(0, dp(10), 0, 0)
            }
            val input = EditText(this).apply {
                setText(props.getProperty(key).orEmpty().trim())
                inputType = InputType.TYPE_CLASS_TEXT
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                )
                setSingleLine()
                // 🔴 **빈 칸은 "꺼짐"(0/false)이 아니라 "네이티브 기본값을 따른다" 는 뜻이다.**
                //    비워 둔 키가 실제로 있고(server.properties 의 vision.ocr_*), 그것을 모르면
                //    조작자가 기능이 꺼져 있다고 읽는다. 그래서 빈 칸에 뜻을 적어 준다.
                // 🔑 **기본값 자체는 여기 안 적는다** — 적는 순간 진실이 두 곳이 되고, 네이티브가
                //    기본값을 바꾸면 이 화면이 조용히 거짓말을 한다. "비었다는 것의 뜻" 만 적는다.
                hint = getString(R.string.cfg_vision_empty_hint)
                // 자동완성 대상이 아니다(임의 키라 힌트를 줄 수 없다).
                importantForAutofill = android.view.View.IMPORTANT_FOR_AUTOFILL_NO
            }
            visionContainer.addView(label)
            visionContainer.addView(input)
            visionInputs[key] = input
        }
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    // ---------------------------------------------------------------- 저장

    /**
     * 계약 §6.1 3번 — **검증은 저장 시점에.** 깨진 값을 파일에 써 놓고 나중에 기본값으로
     * 폴백하는 것보다, 애초에 못 쓰게 막고 어디가 틀렸는지 말해 주는 편이 낫다.
     */
    private fun save() {
        val errors = ArrayList<String>()

        val port = requireInt(portInput, "server.port", 1, 65535, errors)
        val minInterval = requireInt(minIntervalInput, "send.min_interval_ms", 0, Int.MAX_VALUE, errors)
        val reconnect = requireInt(reconnectInput, "reconnect.interval_ms", 100, Int.MAX_VALUE, errors)
        val timeout = requireInt(connectTimeoutInput, "socket.connect_timeout_ms", 100, Int.MAX_VALUE, errors)

        if (errors.isNotEmpty()) {
            Toast.makeText(this, errors.joinToString("\n"), Toast.LENGTH_LONG).show()
            return
        }

        val updates = linkedMapOf(
            // host 는 비어 있어도 유효하다 — 그게 "전송 안 함" 이다(계약 §6).
            "server.host" to hostInput.text.toString().trim(),
            "server.port" to port.toString(),
            "server.format" to FORMATS[formatSpinner.selectedItemPosition],
            "send.mode" to SEND_MODES[sendModeSpinner.selectedItemPosition],
            "send.min_interval_ms" to minInterval.toString(),
            "reconnect.interval_ms" to reconnect.toString(),
            "socket.connect_timeout_ms" to timeout.toString(),
            "socket.keepalive" to keepAliveCheck.isChecked.toString(),
        )
        // vision.* 는 입력값을 그대로 싣는다. 해석도 검사도 하지 않는다.
        visionInputs.forEach { (key, input) -> updates[key] = input.text.toString().trim() }

        val original = PropertiesFile.readText(this)
        if (original == null) {
            Toast.makeText(this, getString(R.string.cfg_save_failed_read), Toast.LENGTH_LONG).show()
            return
        }

        // 값 있는 줄만 제자리 교체 — 주석과 모르는 키는 그대로 살아남는다(계약 §6.1 1번).
        val updated = PropertiesFile.applyUpdates(original, updates)
        if (!PropertiesFile.writeText(this, updated)) {
            Toast.makeText(this, getString(R.string.cfg_save_failed_write), Toast.LENGTH_LONG).show()
            return
        }

        Toast.makeText(this, getString(R.string.cfg_saved), Toast.LENGTH_SHORT).show()
        setResult(RESULT_OK)
        finish()
    }

    /** 숫자 칸 검증. 틀리면 그 칸에 표시하고 사유를 모아 돌려준다. */
    private fun requireInt(
        field: EditText,
        key: String,
        min: Int,
        max: Int,
        errors: MutableList<String>,
    ): Int {
        val raw = field.text.toString().trim()
        val value = raw.toIntOrNull()
        if (value == null) {
            val msg = getString(R.string.cfg_err_not_number, key, raw)
            field.error = msg
            errors.add(msg)
            return 0
        }
        if (value < min || value > max) {
            val msg = getString(R.string.cfg_err_range, key, value, min, max)
            field.error = msg
            errors.add(msg)
            return 0
        }
        field.error = null
        return value
    }

    /** 계약 §6.1 5번 — 값을 잘못 만져 못 쓰게 됐을 때의 탈출구. */
    private fun restoreDefaults() {
        if (!PropertiesFile.restoreDefaults(this)) {
            Toast.makeText(this, getString(R.string.cfg_restore_failed), Toast.LENGTH_LONG).show()
            return
        }
        populate()
        Toast.makeText(this, getString(R.string.cfg_restored), Toast.LENGTH_SHORT).show()
        // 파일이 이미 바뀌었으므로 호출한 쪽이 재적용해야 한다.
        setResult(RESULT_OK)
    }

    private companion object {
        const val VISION_PREFIX = "vision."
        private const val CAMERA_PREFIX = "camera."
        val FORMATS = listOf("json", "plain")
        val SEND_MODES = listOf("on_fresh", "every_stable_frame")
    }
}
