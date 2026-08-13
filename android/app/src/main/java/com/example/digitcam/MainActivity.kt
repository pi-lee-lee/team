package com.example.digitcam

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.util.Size
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * DigitCam 메인 화면 (REQ-0005, 계약 개정 2).
 *
 * 역할 분담은 계약 §1 이 정한 대로다 — **인식·움직임 판정·안정화·전송 트리거가 전부 C++ 에
 * 있고**, 이 화면은 프레임을 떠서 넘기고, 결과를 그리고, 소켓으로 보내는 일만 한다.
 * 여기에 "움직이니까 건너뛰자" 같은 판단을 추가하면 임베디드 이식 때 그 로직이 남겨진다.
 *
 * 스레드 구조:
 *  - 메인      — UI, 권한, CameraX 바인딩
 *  - vision    — [VisionEngine] 이 소유한 단일 스레드. 네이티브 호출은 전부 여기서만.
 *  - IO 코루틴 — [TcpSender] 의 소켓. 프리뷰가 소켓 때문에 끊기지 않게 분리했다.
 */
class MainActivity : ComponentActivity() {

    private lateinit var previewView: PreviewView
    private lateinit var sampleImageView: android.widget.ImageView
    private lateinit var overlayView: OverlayView
    private lateinit var valueText: TextView
    private lateinit var stateText: TextView
    private lateinit var metricText: TextView
    private lateinit var serverText: TextView
    private lateinit var noticeText: TextView
    private lateinit var sampleButton: Button

    private val engine = VisionEngine()
    private lateinit var sender: TcpSender

    private var config: AppConfig = AppConfig(
        host = "", port = 5500, format = AppConfig.Format.JSON,
        sendMode = AppConfig.SendMode.ON_FRESH, minIntervalMs = 1000L,
        reconnectIntervalMs = 3000L, connectTimeoutMs = 3000, keepAlive = true,
        visionConfig = "", warnings = emptyList(), externalFilePath = null,
    )

    private var cameraProvider: ProcessCameraProvider? = null

    /**
     * 샘플 이미지 모드(계약 §9.6). 모드 on/off 는 [sampleMode], 어느 장을 보고 있는지는
     * [sampleIndex] 다. 17장을 한 장씩 확인해야 하므로 이전/다음을 따로 뒀다(개정 3).
     */
    private var sampleIndex = 0
    private var sampleNames: List<String> = emptyList()

    /** `expected.json` 의 정답표. 상대경로 → 기대값 + 판정 규칙. */
    private var sampleExpected: Map<String, SampleImages.Expectation> = emptyMap()
    private var sampleJob: Job? = null

    @Volatile
    private var sampleMode = false

    /** 분석 프레임 재사용 버퍼. 프레임마다 새로 할당하면 30fps 기준 초당 수 MB 의 쓰레기가 된다. */
    private var grayBuffer: ByteArray? = null

    /** 마지막으로 로그한 카메라 회전각. 값이 바뀔 때만 찍기 위한 것이다(vision 스레드 전용). */
    private var lastLoggedRotation = -1

    /** 같은 판정을 매 프레임 찍지 않도록 마지막으로 로그한 (샘플, 값, reason) 조합. */
    private var lastVerdictKey: String? = null

    /**
     * 샘플이 바뀔 때마다 증가한다. **샘플 전환 시 이전 샘플의 결과가 새 샘플 것으로 오인되는
     * 것을 막는다.**
     *
     * 전환은 메인 스레드에서 즉시 일어나지만, vision executor 에는 직전 샘플의 프레임이 이미
     * 몇 개 들어가 있다. 그 결과가 뒤늦게 돌아오면 `sampleIndex` 는 이미 새 값이라 화면과 로그가
     * 엉뚱한 샘플의 값을 보여 준다(실제로 26장 훑기에서 이 오류가 관측됐다 — 3번 샘플에
     * 2번 샘플의 값이 찍혔다). 제출 시점의 세대를 들고 다니다가 낡은 결과는 버린다.
     */
    @Volatile
    private var sampleEpoch = 0

    /**
     * 샘플 프레임이 vision 스레드에서 처리 중인지. **샘플 경로의 백프레셔다.**
     *
     * 카메라 경로는 CameraX 의 STRATEGY_KEEP_ONLY_LATEST 가 밀린 프레임을 알아서 버리지만,
     * 샘플 경로는 내가 직접 넣는 것이라 그런 보호가 없다. 고정 간격(33ms)으로 계속 제출하면
     * 네이티브가 프레임당 80ms 넘게 쓰는 장면 이미지에서 큐가 무한정 자란다. 그러면 샘플을
     * 넘겨도 한참 동안 이전 샘플의 밀린 프레임만 처리하느라 새 샘플 결과가 안 나온다
     * (26장 훑기에서 10장이 통째로 결과를 못 내는 것으로 실제 관측됐다).
     * 처리 중이면 제출을 건너뛴다 = 최신 프레임만 남기는 카메라 쪽과 같은 규율.
     */
    private val sampleFrameInFlight = java.util.concurrent.atomic.AtomicBoolean(false)

    private var openCvVersion: String? = null
    private var serverState: TcpSender.State = TcpSender.State.NotConfigured
    private var lastResult: DigitResult = DigitResult.EMPTY

    private val requestCameraPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startCamera() else showNotice(getString(R.string.camera_permission_denied))
        }

    /**
     * 설정 화면이 파일을 고치고 돌아오면 **기존 "설정 다시 읽기" 와 같은 경로로** 재적용한다.
     * 화면이 값을 따로 들고 있다가 넘겨주는 방식이 아니다 — 진실은 언제나 파일에 있고,
     * adb 로 고친 경우와 앱에서 고친 경우가 완전히 같은 흐름을 타야 한다(계약 §6.1).
     */
    private val openSettings =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == RESULT_OK) reloadConfig(userInitiated = true)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // setContentView 보다 먼저 부른다 — 저장된 방향과 지금 방향이 다르면 여기서
        // 액티비티가 재생성되고, 그때 layout-land / layout 중 맞는 쪽이 잡힌다.
        OrientationPref.apply(this)
        setContentView(R.layout.activity_main)
        // 계약 §8 — 카메라를 계속 보고 있어야 하므로 화면이 꺼지면 안 된다.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        applyWindowInsets()

        previewView = findViewById(R.id.previewView)
        sampleImageView = findViewById(R.id.sampleImageView)
        overlayView = findViewById(R.id.overlayView)
        valueText = findViewById(R.id.valueText)
        stateText = findViewById(R.id.stateText)
        metricText = findViewById(R.id.metricText)
        serverText = findViewById(R.id.serverText)
        noticeText = findViewById(R.id.noticeText)
        sampleButton = findViewById(R.id.sampleButton)

        sender = TcpSender(lifecycleScope) { state ->
            // 콜백은 IO 코루틴에서 온다. UI 갱신은 메인으로 넘긴다.
            runOnUiThread {
                serverState = state
                renderStatus()
            }
        }

        sampleNames = SampleImages.list(this)
        sampleExpected = SampleImages.loadExpected(this)

        findViewById<Button>(R.id.orientationButton).apply {
            // 접근성 라벨은 현재 방향까지 읽어 준다 — 아이콘만으로는 지금이 어느 쪽인지 모른다.
            contentDescription = getString(
                if (OrientationPref.isPortrait(this@MainActivity)) R.string.orientation_desc_now_portrait
                else R.string.orientation_desc_now_landscape,
            )
            setOnClickListener { OrientationPref.toggle(this@MainActivity) }
        }
        findViewById<Button>(R.id.settingsButton).setOnClickListener {
            openSettings.launch(android.content.Intent(this, SettingsActivity::class.java))
        }
        findViewById<Button>(R.id.reloadButton).setOnClickListener { reloadConfig(userInitiated = true) }
        sampleButton.setOnClickListener { toggleSampleMode() }
        findViewById<Button>(R.id.prevButton).setOnClickListener { stepSample(-1) }
        findViewById<Button>(R.id.nextButton).setOnClickListener { stepSample(+1) }

        reloadConfig(userInitiated = false)

        // 네이티브 준비는 vision 스레드에서. 실패해도 화면은 뜬다.
        engine.start(config) { version ->
            runOnUiThread {
                openCvVersion = version
                engine.lastError?.let { showNotice(it) }
                renderStatus()
            }
        }

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startCamera()
        } else {
            requestCameraPermission.launch(Manifest.permission.CAMERA)
        }
    }

    /**
     * 상·하단 패널만 시스템 바를 피하게 한다.
     *
     * `targetSdk = 35` 라 Android 15 가 edge-to-edge 를 강제하므로, 아무것도 안 하면 인식된
     * 번호판 값이 상태바 시계와 겹치고 하단 버튼이 제스처 바에 깔린다.
     *
     * **프리뷰와 오버레이에는 여백을 주지 않는다.** 그 둘은 화면을 꽉 채워야 하고, 오버레이의
     * `quad` 좌표 변환이 프리뷰와 같은 크기를 전제하기 때문이다. 여기에 여백을 주면 사각형이
     * 그만큼 어긋난다. 가려지면 안 되는 것은 글자와 버튼이지 영상이 아니다.
     */
    private fun applyWindowInsets() {
        // 세로 레이아웃은 상·하단 패널, 가로 레이아웃은 오른쪽 한 덩어리다.
        // 두 레이아웃이 같은 코드를 쓰므로 **없는 쪽은 건너뛴다** — findViewById 결과가 null 일 수 있다.
        val top: View? = findViewById(R.id.topPanel)
        val bottom: View? = findViewById(R.id.bottomPanel)

        top?.let { view ->
            val base = view.paddingTop
            androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(view) { v, insets ->
                val bars = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars())
                v.setPadding(v.paddingLeft, base + bars.top, v.paddingRight, v.paddingBottom)
                insets
            }
        }
        bottom?.let { view ->
            val base = view.paddingBottom
            androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(view) { v, insets ->
                val bars = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars())
                v.setPadding(v.paddingLeft, v.paddingTop, v.paddingRight, base + bars.bottom)
                insets
            }
        }
        // 가로 전체화면 구성(REQ-0026): 우상단 안내 패널과 하단 버튼 바.
        // 가로에서는 노치가 **옆면**에 오므로 좌우 인셋을 반드시 같이 넣어야 한다 —
        // 위/아래만 처리하면 우상단 패널이 노치에 물린다.
        val info: View? = findViewById(R.id.infoPanel)
        val bar: View? = findViewById(R.id.buttonBar)

        info?.let { view ->
            val padT = view.paddingTop
            val padR = view.paddingRight
            androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(view) { v, insets ->
                val bars = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars())
                val cut = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.displayCutout())
                v.setPadding(
                    v.paddingLeft,
                    padT + maxOf(bars.top, cut.top),
                    padR + maxOf(bars.right, cut.right),
                    v.paddingBottom,
                )
                insets
            }
        }
        bar?.let { view ->
            val padL = view.paddingLeft
            val padR = view.paddingRight
            val padB = view.paddingBottom
            androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(view) { v, insets ->
                val bars = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars())
                val cut = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.displayCutout())
                v.setPadding(
                    padL + maxOf(bars.left, cut.left),
                    v.paddingTop,
                    padR + maxOf(bars.right, cut.right),
                    padB + bars.bottom,
                )
                insets
            }
        }
    }

    // ---------------------------------------------------------------- 설정

    /**
     * 계약 §6 의 "설정 다시 읽기". 앱을 껐다 켜지 않고 IP·임계값을 바꿔 시험하기 위한 경로다.
     * 서버 주소만이 아니라 `vision.*` 와 전송 모드까지 전부 다시 밀어 넣는다 —
     * 일부만 갱신하면 화면에 보이는 설정과 실제 동작이 어긋난다.
     */
    private fun reloadConfig(userInitiated: Boolean) {
        config = AppConfig.load(this)

        // nativeConfigure 를 여기서 직접 부르면 안 된다(메인 스레드). 엔진이 vision 스레드에서 집어 간다.
        engine.updateConfig(config)
        sender.applyConfig(config)

        val messages = ArrayList<String>()
        if (userInitiated) {
            messages.add(
                getString(
                    R.string.config_reloaded,
                    config.externalFilePath ?: getString(R.string.config_from_assets),
                ),
            )
        }
        messages.addAll(config.warnings)
        engine.lastError?.let { messages.add(it) }
        showNotice(messages.joinToString("\n").ifEmpty { null })

        renderStatus()
    }

    // ---------------------------------------------------------------- 카메라

    private fun startCamera() {
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            val provider = try {
                future.get()
            } catch (e: Exception) {
                showNotice(getString(R.string.camera_open_failed, e.message ?: e.toString()))
                return@addListener
            }
            cameraProvider = provider
            bindUseCases(provider)
        }, ContextCompat.getMainExecutor(this))
    }

    private fun bindUseCases(provider: ProcessCameraProvider) {
        val preview = Preview.Builder().build().also {
            it.surfaceProvider = previewView.surfaceProvider
        }

        // 분석 해상도를 낮게 잡는다. 원본 해상도로 돌리면 프레임을 흘려서 프리뷰가 끊긴다.
        val analysis = ImageAnalysis.Builder()
            .setResolutionSelector(
                ResolutionSelector.Builder()
                    .setResolutionStrategy(
                        ResolutionStrategy(Size(640, 480), ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER),
                    )
                    .build(),
            )
            // 밀리면 최신 프레임만 남긴다. 큐잉하면 인식 결과가 과거를 가리킨다.
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
            .build()

        // analyzer 를 vision executor 에 붙인다 = 네이티브 호출이 그 스레드에 갇힌다.
        analysis.setAnalyzer(engine.executor(), ::analyze)

        try {
            provider.unbindAll()
            provider.bindToLifecycle(this, CameraSelector.DEFAULT_BACK_CAMERA, preview, analysis)
        } catch (e: Exception) {
            showNotice(getString(R.string.camera_bind_failed, e.message ?: e.toString()))
        }
    }

    /**
     * 프레임 하나. **vision 스레드에서 실행된다.**
     *
     * `imageProxy.close()` 를 빠뜨리면 STRATEGY_KEEP_ONLY_LATEST 특성상 두세 프레임 뒤에
     * 아무 에러 없이 분석이 영영 멈춘다. 그래서 finally 에 둔다.
     */
    private fun analyze(imageProxy: ImageProxy) {
        try {
            if (sampleMode) return  // 샘플 모드에서는 카메라 프레임을 버린다(같은 스레드를 샘플이 쓴다)

            val plane = imageProxy.planes[0]
            val buffer = plane.buffer
            buffer.rewind()  // position 이 0 이라는 보장이 없다

            val width = imageProxy.width
            val height = imageProxy.height
            val pixelStride = plane.pixelStride
            val rowStride = plane.rowStride

            val (gray, strideForNative) = if (pixelStride == 1) {
                val size = rowStride * height
                val buf = obtainBuffer(size)
                buffer.get(buf, 0, minOf(size, buffer.remaining()))
                buf to rowStride
            } else {
                // Y 평면의 pixelStride 는 보통 1 이지만 규격상 1 이 아닐 수 있다.
                // 그 경우 폭에 맞춰 압축 복사하고 stride 를 width 로 넘긴다.
                val buf = obtainBuffer(width * height)
                val row = ByteArray(rowStride)
                var out = 0
                for (y in 0 until height) {
                    val toRead = minOf(rowStride, buffer.remaining())
                    if (toRead <= 0) break
                    buffer.get(row, 0, toRead)
                    var x = 0
                    var i = 0
                    while (x < width && i < toRead) {
                        buf[out++] = row[i]
                        i += pixelStride
                        x++
                    }
                }
                buf to width
            }

            val rotation = imageProxy.imageInfo.rotationDegrees
            // 방향이 바뀌었을 때만 한 줄. 화면 방향과 카메라 회전 보정이 실제로 연동되는지
            // 확인하는 유일한 관측점이다 — 값이 안 바뀌면 인식이 90도 틀어진 영상을 보게 된다.
            if (rotation != lastLoggedRotation) {
                lastLoggedRotation = rotation
                Log.i(TAG, "rotationDegrees=$rotation (분석 ${width}x$height)")
            }

            val result = engine.process(gray, width, height, strideForNative, rotation)
            publish(result)
        } catch (e: Exception) {
            Log.w(TAG, "프레임 처리 실패: ${e.message}")
        } finally {
            imageProxy.close()
        }
    }

    private fun obtainBuffer(size: Int): ByteArray {
        val existing = grayBuffer
        if (existing != null && existing.size == size) return existing
        return ByteArray(size).also { grayBuffer = it }
    }

    /**
     * vision 스레드 → 전송 판단 + UI 갱신.
     *
     * [epoch] 은 샘플 모드에서만 의미가 있다. 제출 당시의 [sampleEpoch] 를 들고 오며, 그 사이
     * 샘플이 바뀌었으면 이 결과는 이전 샘플 것이므로 통째로 버린다. 카메라 경로는 세대가
     * 없으므로 -1 을 넘겨 항상 통과시킨다.
     */
    private fun publish(result: DigitResult, epoch: Int = -1) {
        if (epoch >= 0 && epoch != sampleEpoch) return
        sender.offer(result)
        runOnUiThread {
            if (epoch >= 0 && epoch != sampleEpoch) return@runOnUiThread
            lastResult = result
            overlayView.update(result)
            renderStatus()
            logSampleVerdict(result)
        }
    }

    /**
     * 샘플 모드에서 (값, reason) 이 바뀔 때마다 **정답표와 대조한 판정을 로그로** 남긴다.
     *
     * 26장을 한 장씩 눈으로 보고 expected.json 과 대조하는 것은 실수하기 쉽고 느리다.
     * 화면은 사람이 보라고 있는 것이고, 이 로그는 한 번에 훑어보라고 있는 것이다.
     * 각 샘플의 **마지막 줄**이 안정화된 최종 판정이다(전환 직후 몇 프레임은 아직 흔들린다).
     */
    private fun logSampleVerdict(r: DigitResult) {
        if (!sampleMode || sampleNames.isEmpty()) return
        val name = sampleNames[sampleIndex]
        val key = "$name|${r.value}|${r.reason}"
        if (key == lastVerdictKey) return
        lastVerdictKey = key

        val e = sampleExpected[name]
        val verdict = when (e?.rule) {
            null -> "정답표없음"
            SampleImages.Expectation.Rule.EXACT ->
                if (r.value == e.value) "PASS" else "FAIL"
            SampleImages.Expectation.Rule.EXACT_OR_EMPTY ->
                if (r.value == e.value || r.value.isEmpty()) "PASS" else "FAIL"
            SampleImages.Expectation.Rule.EMPTY ->
                if (r.value.isEmpty()) "PASS" else "FAIL(오검출)"
            SampleImages.Expectation.Rule.UNKNOWN -> "rule불명"
        }
        Log.i(
            TAG,
            "[샘플판정] $verdict ${sampleIndex + 1}/${sampleNames.size} $name " +
                "· rule=${e?.rule} 기대='${e?.value.orEmpty()}' 실제='${r.value}' " +
                "· reason=${r.reason} conf=${"%.2f".format(r.conf)} format=${r.plateFormat}",
        )
    }

    // ---------------------------------------------------------------- 샘플 이미지 모드

    /**
     * 계약 §9.6. 카메라 대신 assets 이미지를 **같은 [VisionEngine.process]** 로 보낸다.
     *
     * 버튼을 누를 때마다 다음 샘플로 넘어가고, 마지막 다음은 꺼짐이다.
     *
     * **한 샘플을 계속 반복해서 넣는 이유** — 계약 §9.1 의 움직임 게이트는 이전 프레임과의
     * 차분을 본다. 샘플을 매 프레임 바꿔 가며 넣으면 프레임마다 차분이 크게 잡혀 영원히
     * `moving:true` 가 되고 인식 단계에 도달조차 못 한다. 같은 이미지를 반복해 넣어야
     * 차분이 0 이 되어 `still_frames` 를 채우고 게이트를 통과한다.
     */
    private fun toggleSampleMode() {
        if (sampleNames.isEmpty()) {
            showNotice(getString(R.string.no_samples))
            return
        }
        sampleMode = !sampleMode
        sampleButton.text = getString(if (sampleMode) R.string.sample_mode_on else R.string.sample_mode_off)

        if (!sampleMode) {
            sampleJob?.cancel()
            sampleJob = null
            overlayView.clear()
            sampleImageView.visibility = View.GONE
            sampleImageView.setImageDrawable(null)
            overlayView.useFitCenter = false  // 카메라 프리뷰는 FILL_CENTER 다
            showNotice(null)
            return
        }
        startSampleFeed()
    }

    /** 이전/다음. 모드가 꺼져 있으면 켜면서 이동한다(누르면 바로 보이는 게 자연스럽다). */
    private fun stepSample(delta: Int) {
        if (sampleNames.isEmpty()) {
            showNotice(getString(R.string.no_samples))
            return
        }
        val size = sampleNames.size
        sampleIndex = ((sampleIndex + delta) % size + size) % size  // 음수 나머지 보정 = 순환
        if (!sampleMode) {
            sampleMode = true
            sampleButton.text = getString(R.string.sample_mode_on)
        }
        startSampleFeed()
    }

    /** 현재 [sampleIndex] 샘플을 반복 투입하는 코루틴을 새로 건다. */
    private fun startSampleFeed() {
        sampleJob?.cancel()
        val name = sampleNames[sampleIndex]
        // 이 세대 이후의 결과만 유효하다. 직전 샘플의 잔여 프레임 결과는 여기서 무효가 된다.
        val epoch = ++sampleEpoch

        sampleJob = lifecycleScope.launch {
            val frame = withContext(Dispatchers.IO) { SampleImages.load(this@MainActivity, name) }
            if (frame == null) {
                showNotice(getString(R.string.sample_decode_failed, name))
                return@launch
            }

            // 네이티브에 넘기는 버퍼를 그대로 파일로 남긴다(재변환 없음).
            // 데스크톱 하네스와 앱의 입력이 같은지 비교하려면 이 바이트가 있어야 한다.
            withContext<Unit>(Dispatchers.IO) { SampleImages.dumpPgm(this@MainActivity, frame) }

            // 실제로 투입하는 그레이 프레임을 화면에 띄운다. 배경이 라이브 카메라인 채로
            // quad 만 그리면 좌표계가 달라 엉뚱한 자리에 사각형이 나온다.
            sampleImageView.setImageBitmap(frame.toDisplayBitmap())
            sampleImageView.visibility = View.VISIBLE
            // ImageView 가 fitCenter 이므로 오버레이도 같은 계산을 써야 사각형이 맞는다.
            overlayView.useFitCenter = true

            showNotice(
                listOf(
                    getString(
                        R.string.sample_playing,
                        sampleIndex + 1, sampleNames.size, frame.name,
                        frame.width, frame.height,
                    ),
                    expectationText(sampleExpected[name]),
                ).joinToString("\n"),
            )

            // 같은 프레임을 반복 투입. 카메라 프레임률과 비슷하게 유지한다.
            while (isActive && sampleMode) {
                // 앞 프레임이 아직 처리 중이면 이번 차례는 버린다(큐를 쌓지 않는다).
                if (sampleFrameInFlight.compareAndSet(false, true)) {
                    engine.executor().execute {
                        try {
                            val result =
                                engine.process(frame.gray, frame.width, frame.height, frame.rowStride, 0)
                            publish(result, epoch)
                        } finally {
                            sampleFrameInFlight.set(false)
                        }
                    }
                }
                delay(SAMPLE_FRAME_INTERVAL_MS)
            }
        }
    }

    /**
     * 이 샘플에서 무엇이 정답인지 한 줄로. **판정 근거는 `expected.json` 의 `rule` 이다** —
     * 파일명으로 추측하면 정답표와 화면이 어긋난다(개정 4에서 실제로 겪은 문제다).
     */
    private fun expectationText(e: SampleImages.Expectation?): String = when (e?.rule) {
        null -> getString(R.string.sample_expected_unknown)
        SampleImages.Expectation.Rule.EXACT -> getString(R.string.sample_rule_exact, e.value)
        SampleImages.Expectation.Rule.EXACT_OR_EMPTY -> getString(R.string.sample_rule_exact_or_empty, e.value)
        SampleImages.Expectation.Rule.EMPTY -> getString(R.string.sample_rule_empty)
        SampleImages.Expectation.Rule.UNKNOWN -> getString(R.string.sample_rule_unknown, e.value)
    }

    // ---------------------------------------------------------------- 화면

    private fun renderStatus() {
        val r = lastResult

        valueText.text = r.value.ifEmpty { getString(R.string.no_value) }

        // 계약 §8 — 움직이는 동안 인식을 건너뛴다는 사실이 사용자에게 보여야 한다.
        val motionLabel =
            if (r.moving) getString(R.string.motion_moving) else getString(R.string.motion_still)

        stateText.text = getString(
            R.string.state_line,
            motionLabel,
            if (r.stable) getString(R.string.stable_yes) else getString(R.string.stable_no),
            r.conf,
            r.reason.ifEmpty { "-" },
            r.plateFormat,
        )

        // 임계값 튜닝은 사용자가 이 줄을 보고 한다 — motion/sharp 실측값이 없으면
        // vision.motion_threshold 를 얼마로 둬야 할지 알 방법이 없다.
        metricText.text = getString(
            R.string.metric_line,
            r.motion,
            r.sharp,
            if (r.plate) getString(R.string.plate_found) else getString(R.string.plate_none),
            r.ms,
            openCvVersion ?: getString(R.string.opencv_unavailable),
        )

        serverText.text = when (val s = serverState) {
            is TcpSender.State.NotConfigured -> getString(R.string.server_not_configured, config.port)
            is TcpSender.State.Connecting -> getString(R.string.server_connecting, s.host, s.port)
            is TcpSender.State.Connected -> getString(R.string.server_connected, s.host, s.port)
            is TcpSender.State.Disconnected -> getString(R.string.server_disconnected, s.attempt)
        }
    }

    private fun showNotice(message: String?) {
        noticeText.text = message.orEmpty()
        noticeText.visibility = if (message.isNullOrEmpty()) View.GONE else View.VISIBLE
    }

    // ---------------------------------------------------------------- 수명주기

    override fun onDestroy() {
        // 순서가 중요하다. 바인딩을 먼저 끊어 analyze() 가 더 들어오지 않게 한 다음에
        // 네이티브를 해제한다. 반대로 하면 처리 중인 프레임이 해제된 핸들을 만진다.
        sampleJob?.cancel()
        sampleMode = false
        cameraProvider?.unbindAll()
        sender.stop()
        engine.shutdown()
        super.onDestroy()
    }

    private companion object {
        const val TAG = "MainActivity"

        /** 샘플 프레임 투입 간격. 카메라(약 30fps)와 비슷하게 두어 게이트 동작을 같게 만든다. */
        const val SAMPLE_FRAME_INTERVAL_MS = 33L
    }
}
