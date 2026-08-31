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
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.core.resolutionselector.AspectRatioStrategy
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
import java.io.File

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

    /** 🔵 진행 단계(REQ-0500). **[startProgressTicker] 만 이 뷰를 만진다.** */
    private lateinit var progressText: TextView
    private lateinit var sampleButton: Button

    private val engine = VisionEngine()
    private lateinit var sender: TcpSender

    /** 설정 파일을 읽기 전의 값. 기본값을 여기 다시 적지 않는다(AppConfig.fallback 참고). */
    @Volatile
    private var config: AppConfig = AppConfig.fallback()

    private var cameraProvider: ProcessCameraProvider? = null

    /**
     * 촬영 요청 대기 목록(카메라 pull).
     *
     * 🔴 **시한 인자가 사라졌다**(2026-08-27 완주 설계). 요청은 시간으로 닫히지 않는다 —
     * 인식 완료 · 촬영 워치독 · 링크 끊김, 이 셋 중 하나로만 닫힌다.
     */
    private val shots = ShotCoordinator()

    /**
     * 언제 셔터를 누를지. 셔터는 기기에 하나라 이 상태도 하나다.
     *
     * 🔑 시계를 넣는 이유는 **워치독 하나 때문**이다 — 인식 시한이 아니다
     * ([ShutterGate.captureWatchdog]). 시계를 안 넣으면 워치독이 영영 안 짖는다.
     */
    private val shutter = ShutterGate(captureWatchdogMs = { config.captureWatchdogMs }, nowMs = ::nowMs)

    /**
     * 🔵 **진행 단계 표시**(REQ-0500). 인식 시한을 없앴으므로 *"기다리는 중"* 이 보여야 한다.
     * ⚠ 쓰기는 아무 스레드나, **그리기는 UI 틱 하나만**([startProgressTicker]).
     */
    private val progress = ShotProgress()

    /** 촬영본 보관. 외부 저장소를 못 쓰면 null — 그때는 파일 없이 메모리에서만 인식한다. */
    private var shotStore: ShotStore? = null

    /**
     * 셔터. 프리뷰·분석과 **같이** 바인딩된다.
     *
     * ⚠ use case 셋(Preview + ImageAnalysis + ImageCapture)을 동시에 쓰는 것은 기기의
     * 하드웨어 레벨에 따라 실패할 수 있다. 실패하면 `bindToLifecycle` 이 던지고
     * [analysisRunning] 이 false 로 남아 촬영 요청이 `camera_unavailable` 로 답해진다 —
     * **조용히 절반만 도는 상태가 되지 않는다.**
     */
    private var imageCapture: ImageCapture? = null

    /**
     * 분석이 실제로 돌고 있는가. **읽는 스레드가 본다** — 촬영 요청이 왔을 때 답할 수 있는
     * 상태인지 판단하는 값이라 `cameraProvider`(메인 스레드 전용)를 대신 쓸 수 없다.
     */
    @Volatile
    private var analysisRunning = false

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

    /** 마지막으로 "push 차단" 을 찍은 값. 같은 값을 33ms 마다 찍지 않기 위한 것이다. */
    private var lastBlockedPush: String? = null

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

    /**
     * 마지막 메모리 표본. **매 프레임 재지 않는다** — `Debug.getMemoryInfo` 가 수 ms 를 쓴다.
     * 부하가 서는 지점(촬영본 인식 직후 · 샘플 전환)에서만 갱신한다.
     */
    @Volatile
    private var memorySnapshot: MemoryWatch.Snapshot? = null

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
        progressText = findViewById(R.id.progressText)
        sampleButton = findViewById(R.id.sampleButton)

        sender = TcpSender(
            scope = lifecycleScope,
            onState = { state ->
                // 링크가 끊기면 대기 중인 촬영 요청을 버린다. UI 로 넘기기 전에 처리한다 —
                // 다음 연결이 열린 뒤에 답이 만들어지면 새 세션에 낡은 답이 나간다.
                if (state is TcpSender.State.Disconnected) {
                    val dropped = shots.dropAll()
                    if (dropped.isNotEmpty()) {
                        Log.w(TAG, "링크 끊김 — 답하지 못한 촬영 요청 ${dropped.size}건 버림: $dropped")
                    }
                    // 답할 곳이 없으므로 셔터도 닫는다. 안 닫으면 다음 검출에서 헛셔터가 나간다.
                    shutter.disarm()
                    // 🔴 화면의 도는 줄도 닫는다. 안 닫으면 **경과 초가 영원히 올라간다** —
                    //    그러면 화면이 "아직 하는 중" 이라고 거짓말을 한다.
                    progress.abandon(nowMs(), "연결 끊김")
                }
                // 콜백은 IO 코루틴에서 온다. UI 갱신은 메인으로 넘긴다.
                runOnUiThread {
                    serverState = state
                    renderStatus()
                }
            },
            onShoot = ::onShootRequest,
        )

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
        findViewById<Button>(R.id.shutterButton).apply {
            contentDescription = getString(R.string.shutter_desc)
            setOnClickListener { onShutterButton() }
        }
        sampleButton.setOnClickListener { toggleSampleMode() }
        findViewById<Button>(R.id.prevButton).setOnClickListener { stepSample(-1) }
        findViewById<Button>(R.id.nextButton).setOnClickListener { stepSample(+1) }

        reloadConfig(userInitiated = false)

        // 네이티브 준비는 vision 스레드에서. 실패해도 화면은 뜬다.
        // 모델은 **vision 스레드가 읽는다**(7 MiB 넘는다 — 메인에서 읽으면 첫 화면이 그만큼 늦는다).
        engine.start(config, models = { OcrModels.load(this) }) { version ->
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

        startCaptureWatchdog()
        startProgressTicker()
    }

    // ---------------------------------------------------------------- 카메라 pull (촬영 요청)

    /**
     * 하행 `SHOOT,<id>` 가 도착했다. **[TcpSender] 의 읽는 스레드에서 불린다** —
     * 여기서 무거운 일을 하면 다음 하행이 그만큼 늦는다.
     *
     * 답을 만드는 것은 나중이다(인식 결과가 나올 때 · 시한이 지날 때). 여기서 하는 일은
     * **지금 답할 수 있는 상태인가**를 가르고, 아니면 그 자리에서 실패로 답하는 것뿐이다.
     * 🔴 어느 갈래로도 답이 안 나가는 경우를 두지 않는다 — 침묵은 서버가 "아직 안 왔다" 와
     * 구별할 수 없다.
     */
    private fun onShootRequest(shotId: Long) {
        // 🔴 샘플 모드는 촬영 가능 조건에 넣지 않는다. 샘플 이미지의 인식값을 촬영 결과라고
        // 올리면 서버가 **있지도 않은 차의 번호판**을 받고 자리를 배정한다.
        // 모의가 실기보다 더 해 주면 안 된다 — 사슬 거동은 단위 시험(ShutterGate)이 닫는다.
        val running = analysisRunning && imageCapture != null
        if (!running) {
            // 왜 못 하는지를 가른다. 권한이 없는 것과 카메라가 아직 안 열린 것은 다른 일이고,
            // 앞은 사람이 허용해야 풀리고 뒤는 기다리면 풀린다.
            val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED
            val reason =
                if (granted) CameraShot.REASON_CAMERA_UNAVAILABLE else CameraShot.REASON_NO_PERMISSION
            Log.w(TAG, "촬영 요청 shot=$shotId 즉시 실패: $reason")
            sender.reply(CameraShot.encodeError(shotId, reason))
            return
        }

        progress.requested(shotId, nowMs())
        val evicted = shots.request(shotId, nowMs())
        // 🔴 **바로 찍는다.** 예전에는 프리뷰가 번호판을 잡을 때까지 기다렸고, 그 검출이
        // 안 서는 장면에서는 **셔터가 한 번도 안 눌린 채** 시한까지 갔다(사용자가 본 증상).
        // 🔴 그리고 이제 **시한 자체가 없다**(2026-08-27 완주 설계) — 인식은 끝까지 돌고
        //    반드시 성공이나 사유 있는 실패로 답한다.
        val decision = shutter.arm()
        Log.i(TAG, "촬영 요청 shot=$shotId 접수 — 대기 ${shots.pendingCount}건 · 완주(시한 없음) · $decision")
        sendReplies(evicted)
        applyDecision(decision)
    }

    /**
     * 화면의 **[촬영] 버튼**. 서버 없이도 *찍어서 읽는* 경로를 사람이 직접 밟을 수 있어야 한다.
     *
     * 🔑 요청 목록([shots])에 아무것도 넣지 않는다 — 답할 상대가 없다. 결과는 **화면에만**
     * 뜨고 판정 줄은 촬영본 옆에 남는다. 서버 경로와 **같은 [fireShutter]·[recognizeShot]**
     * 을 타는 것이 요점이다. 여기서 따로 인식하면 "버튼으로는 되는데 서버 요청으로는 안 된다"
     * 를 디버깅할 수 없다.
     */
    private fun onShutterButton() {
        if (sampleMode) {
            showNotice(getString(R.string.shutter_sample_mode))
            return
        }
        if (!analysisRunning || imageCapture == null) {
            showNotice(getString(R.string.shutter_camera_unavailable))
            return
        }
        // 🔑 요청번호가 없으므로 0 으로 연다 — 화면에서 *"서버 요청"* 과 *"사람이 눌렀다"* 가 갈린다.
        progress.requested(0L, nowMs())
        val decision = shutter.arm()
        Log.i(TAG, "촬영 버튼 — $decision")
        applyDecision(decision)
    }

    /**
     * 셔터를 누른다. **메인 스레드에서 부른다.**
     *
     * 콜백은 vision executor 로 받는다 — 인식이 그 스레드에서만 돌아야 하기 때문이다
     * ([VisionEngine] 규약). 그 사이 프리뷰 분석이 잠깐 밀리는데, 촬영 중에는 프리뷰 결과가
     * 답에 쓰이지 않으므로 수용한다.
     *
     * ⚠ **얼마나 밀리나** — [VisionEngine.processStill] 이 게이트가 열릴 때까지 같은 프레임을
     * 넣지만, **비싼 프레임은 1장뿐이다**(게이트가 열린 그 프레임에서 루프가 끝난다).
     * 그 1장의 값은 **모델이 실렸는지**로 갈린다 — 상태줄의 `인식모델` 이 그 답이다:
     * 모델 없으면 5~60ms, 표준 경로까지 가면 **~345ms**(에뮬 실측). 앞의 게이트 프레임들은 각 수 ms다.
     * 🔑 **`STILL_MAX_FRAMES` 를 곱하지 마라** — 근거가 틀린다([VisionEngine.STILL_MAX_FRAMES] 참조).
     * 프리뷰가 그동안 잠깐 멎는 것은 정상이다.
     */
    private fun fireShutter(attempt: Int) {
        val capture = imageCapture
        if (capture == null) {
            // 🔴 **네 번째 침묵 갈래였다**(2026-08-22 · 루트가 전수 확인을 지시해서 찾았다).
            //    카메라 바인딩이 실패하면 사진도 판정도 없어서 §17 ③(줄 < 사진 수)이 되고,
            //    **그때 원인 후보가 저장소·권한·용량·코드 넷으로 벌어진다.**
            shotStore?.appendJudgement(
                "(사진없음)  reason=app:no_capture  hasValue=N  value=''" +
                    "  note='카메라가 바인딩되지 않았다'  attempt=${attempt + 1}",
            )
            applyDecision(shutter.onCaptureFailed())
            return
        }
        Log.i(TAG, "셔터 (시도 ${attempt + 1}) — 대기 ${shots.pendingCount}건")
        noteProgress(ShotProgress.Stage.CAPTURING, attempt = attempt + 1)
        capture.takePicture(
            engine.executor(),
            object : ImageCapture.OnImageCapturedCallback() {
                override fun onCaptureSuccess(image: ImageProxy) {
                    try {
                        val rotation = image.imageInfo.rotationDegrees
                        val buf = image.planes[0].buffer
                        buf.rewind()
                        val bytes = ByteArray(buf.remaining())
                        buf.get(bytes)
                        shutter.onCaptured()
                        noteProgress(ShotProgress.Stage.RECOGNIZING, attempt = attempt + 1)
                        recognizeShot(bytes, attempt, rotation)
                    } catch (e: Exception) {
                        Log.w(TAG, "촬영본 처리 실패: ${e.message}")
                        shotStore?.appendJudgement(
                            "(사진없음)  reason=app:capture_failed  hasValue=N  value=''" +
                                "  err='${e.message?.take(60)}'  attempt=${attempt + 1}",
                        )
                        applyDecision(shutter.onCaptureFailed())
                    } finally {
                        image.close()
                    }
                }

                override fun onError(exception: ImageCaptureException) {
                    Log.w(TAG, "셔터 실패: ${exception.message}")
                    // 🔴 이 갈래는 **사진 자체가 없다.** 판정 줄도 안 남기면 측정할 때
                    //    *"왜 3장이 아니라 2장인가"* 를 답할 것이 아무것도 없다.
                    shotStore?.appendJudgement(
                        "(사진없음)  reason=app:capture_failed  hasValue=N  value=''" +
                            "  err='${exception.message?.take(60)}'  attempt=${attempt + 1}",
                    )
                    applyDecision(shutter.onCaptureFailed())
                }
            },
        )
    }

    /**
     * 촬영본에서 번호를 뽑는다. **vision executor 에서 실행된다.**
     *
     * 파일로 먼저 남기는 것이 요점이다 — 인식이 실패했을 때 *"인식기가 못 읽었다"* 와
     * *"이상한 사진을 찍었다"* 는 값만으로 구분되지 않는다. 그 사진이 있어야 갈린다.
     * 저장에 실패해도 인식은 계속한다(파일은 증거이고 인식의 입력이 아니다).
     */
    private fun recognizeShot(jpeg: ByteArray, attempt: Int, rotationDegrees: Int) {
        val shotIds = shots.pendingIds()
        val label = shotIds.firstOrNull()?.toString() ?: "push"

        val saved = shotStore?.save(shotIds.firstOrNull() ?: 0L, attempt, jpeg)
        if (saved != null) {
            Log.i(TAG, "촬영본 저장: ${saved.name} (${jpeg.size}B)")
        } else if (shotStore != null) {
            Log.w(TAG, "촬영본 저장 실패 — 인식은 계속한다")
        }

        val gray = PlateImage.fromJpeg(jpeg, label)
        if (gray == null) {
            // 🔴 **이 갈래도 판정이다.** 여기서 그냥 `return` 하면 사진은 남는데 판정 줄이 없어서
            //    **사진 N장 ↔ 판정 N−1줄** 이 되고, *그 사진이 왜 실패했는지* 셀 수 없다.
            // 🔑 §"로그 줄이 `return` 하나 뒤에 있으면 그 갈래는 침묵한다" — 그 침묵이
            //    **"안 불렸다" 와 같은 모양**이다.
            shotStore?.appendJudgement(
                "${saved?.name ?: "(미저장)"}  reason=app:decode_failed" +
                    "  hasValue=N  value=''  bytes=${jpeg.size}  rot=$rotationDegrees" +
                    "  attempt=${attempt + 1}",
            )
            applyDecision(shutter.onRecognized("", formatKnown = false))
            return
        }

        // 🔑 예외가 나도 밖 `catch` 가 잡는다 — **침묵은 아니다.** 다만 거기서는 사유가
        //    `app:capture_failed` 로 적힌다. **촬영은 성공했는데 그렇게 적히면 원인이 틀리게 귀속된다.**
        //    ★ 그래서 여기서 갈라 잡는다: 버퍼 읽기 실패(밖) ↔ 인식 예외(여기).
        val result = try {
            // 🔴 `process` 가 아니라 `processStill` 이다. 한 장만 넣으면 움직임 게이트가
            //    **직전 프리뷰 프레임과의 차분**을 보고 `reason=moving` 으로 잘라 버린다
            //    ([VisionEngine.processStill] 의 주석에 눈금과 근거가 있다).
            engine.processStill(gray.gray, gray.width, gray.height, gray.rowStride, rotationDegrees)
        } catch (e: Exception) {
            Log.w(TAG, "인식 예외 shot=$label: ${e.message}")
            shotStore?.appendJudgement(
                "${saved?.name ?: "(미저장)"}  reason=app:recognize_crashed  hasValue=N  value=''" +
                    "  err='${e.message?.take(60)}'  rot=$rotationDegrees  attempt=${attempt + 1}",
            )
            applyDecision(shutter.onRecognized("", formatKnown = false))
            return
        }
        Log.i(
            TAG,
            "촬영본 인식 shot=$label value='${result.value}' format=${result.plateFormat} " +
                "reason=${result.reason} conf=${"%.2f".format(result.conf)}",
        )
        // 🔴 **판정을 사진 옆에 남긴다** — 화면 값은 흘러가고 logcat 은 사용자가 못 본다.
        //
        // 왜 : `PREREG-plate-accuracy.md` §15 가 사진마다 `reason` 을 갈라 세라고 한다
        //      (검출 실패 ↔ 분할 실패는 **조정 방향이 반대다**). 이 줄이 없으면 27장을 찍어도
        //      **어느 것이 왜 실패했는지 셀 수 없다.**
        // 🔑 `rotationDegrees` 를 같이 적는 이유는 따로다 — §11 자료 인수 규약이 그것을 요구한다.
        //    `cv::imread` 는 EXIF 를 적용하고 `BitmapFactory` 는 무시하므로, 사진만 넘기면
        //    받는 쪽이 회전을 추측하고 **우리 실기와 갈린다.**
        shotStore?.appendJudgement(
            "${saved?.name ?: "(미저장)"}  reason=${result.reason.ifEmpty { "-" }}" +
                "  hasValue=${if (result.hasValue) "Y" else "N"}  value='${result.value}'" +
                "  engine=${result.engine}  conf=${"%.2f".format(result.conf)}" +
                "  plate=${if (result.plate) "Y" else "N"}" +
                "  sharp=${"%.1f".format(result.sharp)}  motion=${"%.2f".format(result.motion)}" +
                "  frame=${result.frameWidth}x${result.frameHeight}  rot=$rotationDegrees" +
                "  ms=${result.ms}  attempt=${attempt + 1}",
        )

        // 🔑 **촬영본 인식 직후가 피크가 서는 자리다**(검출망 blob 이 가장 크다).
        //    여기서 한 번 재 두면 실기기에서 죽어도 "죽기 직전 수" 가 로그에 남는다.
        memorySnapshot = MemoryWatch.sample(this, "촬영본 인식 직후")

        // 화면에도 촬영본 결과를 띄운다 — 사람이 무엇으로 답했는지 봐야 한다.
        runOnUiThread {
            lastResult = result
            overlayView.update(result)
            renderStatus()
        }

        // [4] 화면에 결과를 던진다. **여기는 vision 스레드다** — 그리지 않고 값만 넣는다.
        val good = result.hasValue && result.plateFormat != "unknown" && result.value != CameraShot.PLATE_MARKER
        if (good) {
            noteProgress(ShotProgress.Stage.FOUND, detail = result.value)
        } else {
            noteProgress(ShotProgress.Stage.FAILED, detail = CameraShot.reasonFromNative(result.reason))
        }

        // 🔴 **사유를 네이티브가 준 것으로 나른다**(계약 §5.2 어휘). 예전에는 무엇으로 실패했든
        //    `recognize_failed` 하나였는데, `no_plate`(검출 실패)와 `segment_fail`(분할 실패)은
        //    **고칠 곳이 반대**라 뭉치면 화면을 봐도 무엇을 할지 모른다.
        val decision = shutter.onRecognized(
            result.value,
            result.plateFormat != "unknown",
            CameraShot.reasonFromNative(result.reason),
        )
        applyDecision(decision)
    }

    /**
     * 🔴 **메모리 압력 신호를 받아 해상도를 강등한다** (REQ-0407 방어).
     *
     * ★ **죽는 것보다 낮은 해상도로 사는 것이 낫다** — 사용자 요구가 "인식률" 인데
     * 프로세스가 사라지면 인식률은 **0** 이다. 강등의 대가는 실촬영 정답 12 → 11 로 **한 장**이다(실측).
     *
     * 🔴 **`largeHeap` 을 쓰지 않는 이유**: 그건 Java heap 만 키운다. 우리 피크는 **네이티브**라
     * (RSS 342MB 중 네이티브 240MB · Java 41MB) 효과가 없고, 있어도 그건 증상을 미루는 것이지
     * 대책이 아니다(사용자 확정: *"없어야 한다는 감춰야 한다가 아니다"*).
     *
     * ⚠ 강등은 **조용히 일어나면 안 된다.** 그러면 다음 사람이 인식률 표를 보고 *"회귀"* 로 읽고
     * 원인을 못 찾는다 — 그래서 로그와 화면 양쪽에 드러낸다.
     */
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        // RUNNING_LOW 부터가 "지금 앱이 쓰는 것을 줄여라" 다. RUNNING_MODERATE 는 아직 여유가 있다.
        val pressure = level >= TRIM_MEMORY_RUNNING_LOW
        if (GrayConvert.setDegraded(pressure)) {
            Log.w(TAG, "메모리 압력(level=$level) → 인식 해상도 ${GrayConvert.activeMaxEdge}px 로 전환")
            runOnUiThread { renderStatus() }
        }
    }

    /**
     * 문의 결정을 실행한다. **[ShutterGate] 가 내는 결정은 여기 한 곳에서만 실행된다** —
     * 갈래마다 따로 처리하면 `Fire` 를 흘리는 자리가 생기고, 그러면 재시도가 조용히 사라진다.
     *
     * `Wait` 는 아무것도 하지 않는다(이미 촬영·인식이 도는 중이다).
     */
    private fun applyDecision(decision: ShutterGate.Decision) {
        when (decision) {
            // 셔터는 메인 스레드에서 누른다. 이 함수는 소켓 읽는 스레드·vision 스레드에서도 불린다.
            is ShutterGate.Decision.Fire -> runOnUiThread { fireShutter(decision.attempt) }
            is ShutterGate.Decision.Accept -> sendReplies(shots.onPlate(decision.plate, nowMs()))
            is ShutterGate.Decision.GiveUp -> sendReplies(shots.failAllPending(decision.reason))
            ShutterGate.Decision.Wait -> Unit
        }
    }

    /** 만들어진 답을 전선으로 올린다. [ShotCoordinator] 는 결정만 하고 전송은 하지 않는다. */
    private fun sendReplies(replies: List<ShotCoordinator.Reply>) {
        for (r in replies) {
            val queued = when (r) {
                is ShotCoordinator.Reply.Success -> {
                    Log.i(TAG, "촬영 응답 shot=${r.shotId} value='${r.plate}'")
                    sender.reply(CameraShot.encodeSuccess(r.shotId, r.plate, android.os.Build.MODEL))
                }
                is ShotCoordinator.Reply.Failure -> {
                    Log.w(TAG, "촬영 응답 shot=${r.shotId} error=${r.reason}")
                    sender.reply(CameraShot.encodeError(r.shotId, r.reason))
                }
            }
            // [5] 🔴 **"서버가 받았다" 라고 말하지 않는다.** 우리가 아는 것은 여기까지다 —
            //     큐에 들어갔나(queued), 그리고 지금 연결돼 있나(isConnected).
            //     ⚠ 연결돼 있어도 half-open 이면 안 갔을 수 있다. **아는 것까지만 적는다.**
            if (!queued) {
                noteProgress(ShotProgress.Stage.SEND_FAILED, detail = "큐가 참")
            } else if (!sender.isConnected) {
                noteProgress(ShotProgress.Stage.SEND_FAILED, detail = "연결 없음")
            } else {
                noteProgress(ShotProgress.Stage.SENT)
            }
        }
        // 대기가 비면 셔터 문을 닫는다. **모든 종료 경로가 여기를 지난다**
        // (응답 · 시한 초과 · 상한 초과) — 한 곳에서 닫아야 열린 채로 남지 않는다.
        if (shots.pendingCount == 0) shutter.disarm()
    }

    /**
     * **촬영 콜백 워치독**을 주기적으로 돌린다.
     *
     * ## 🔴 이것은 옛 `startShotTimeoutSweep` 이 아니다 — 성격이 반대다
     *
     * ```
     * ❌ 옛것 : 시한이 지나면 대기를 닫았다 → `disarm` → **도는 인식의 결과가 버려졌다**
     * ✅ 지금 : 인식은 **절대 안 끊는다.** 촬영 콜백이 안 온 경우만 문을 연다
     * ```
     * 🔑 그래서 [ShutterGate.captureWatchdog] 은 `RECOGNIZING` 을 아예 안 본다.
     * **여기서 인식을 끊을 수 있으면 완주 보장이 무너진다.**
     */
    private fun startCaptureWatchdog() {
        lifecycleScope.launch {
            while (isActive) {
                delay(CAPTURE_WATCHDOG_TICK_MS)
                val decision = shutter.captureWatchdog()
                if (decision is ShutterGate.Decision.GiveUp) {
                    Log.w(TAG, "🔴 촬영 콜백이 안 왔다 — 워치독이 문을 연다(사유 ${decision.reason})")
                    applyDecision(decision)
                }
            }
        }
    }

    /**
     * 진행 단계를 **던진다**. 그리지 않는다.
     *
     * ## 🔴 이 함수가 하는 일이 적은 것이 요점이다 (REQ-0500 함정 ①)
     *
     * vision 스레드·소켓 스레드·메인이 전부 부른다. 여기서 화면을 만지면
     * **인식이 그만큼 느려지고, 인식 시한을 없앤 이유를 스스로 깎는다.**
     * 하는 일은 잠금 하나와 목록 갱신뿐이고, 문자열은 [startProgressTicker] 가 UI 에서 만든다.
     *
     * 🔵 같은 문장을 logcat 에도 남긴다 — **폰을 못 볼 때 그것이 유일한 기록이다.**
     */
    private fun noteProgress(stage: ShotProgress.Stage, detail: String = "", attempt: Int? = null) {
        progress.advance(stage, nowMs(), detail, attempt)
        Log.i(TAG, "진행 $stage${if (detail.isEmpty()) "" else " · $detail"}")
    }

    /**
     * 화면에 진행을 그리는 **유일한 자리**. UI 스레드에서만 돈다.
     *
     * ## 왜 주기 갱신인가 — **경과 초가 올라가야 관측이 된다**
     *
     * 단계가 바뀔 때만 그리면 인식 구간(10~40초) 동안 **화면이 얼어 있다.**
     * 그러면 *"도는 중"* 과 *"멈춤"* 이 같은 모양이 되고, 이 화면은 아무것도 관측하지 않는다.
     *
     * 🔵 **도는 것이 없으면 다시 그리지 않는다** — 끝난 이력은 값이 안 변하므로 헛일이다.
     * ⚠ 화면 켜 두기도 여기서 켠다·끈다: **촬영~인식 구간에만.** 늘 켜 두면 배터리를 먹고,
     * 안 켜면 진행이 보일 때 화면이 꺼져 있다.
     */
    private fun startProgressTicker() {
        lifecycleScope.launch {
            var lastText = ""
            var screenHeld = false
            while (isActive) {
                // 🔵 도는 것이 없으면 **느리게 돈다.** 끝난 줄의 "N분 전" 도 갱신돼야 하므로
                //    아예 멈추지는 않는다 — 멈추면 "방금" 이 영원히 "방금" 이다.
                delay(if (progress.isRunning()) PROGRESS_TICK_MS else PROGRESS_IDLE_TICK_MS)
                val running = progress.isRunning()
                if (running != screenHeld) {
                    screenHeld = running
                    if (running) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                }
                // 잠금 안에서는 목록 복사만. 문자열은 여기(UI)에서 만든다.
                val text = ShotProgress.render(progress.snapshot(), nowMs(), progress.droppedCount())
                if (text == lastText) continue
                lastText = text
                progressText.text = text
                progressText.visibility = if (text.isEmpty()) View.GONE else View.VISIBLE
            }
        }
    }

    private fun nowMs(): Long = android.os.SystemClock.elapsedRealtime()

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

        // 🔑 **앱이 해석하는 축**은 여기서 적용한다. `vision.*` 은 네이티브가, `camera.max_edge` 는 앱이 갖는다.
        //    두 축은 곱으로 작동한다 — 조합표는 assets/server.properties 에 값과 함께 있다.
        if (GrayConvert.setConfigured(config.maxEdge)) {
            Log.i(TAG, "인식 입력 장축 상한 → ${config.maxEdge}px (camera.max_edge)")
        }
        sender.applyConfig(config)

        // 촬영본 보관. **설정을 읽은 뒤에 만든다** — onCreate 앞쪽에서 만들면 아직
        // fallback 값이라 camera.shot_keep_files 가 반영되지 않는다.
        // 외부 저장소를 못 쓰면 null 로 두고 **파일 없이 인식만** 한다: 사진은 증거이고
        // 인식의 입력이 아니므로 없어도 사슬은 돈다.
        shotStore = getExternalFilesDir(null)?.let {
            ShotStore(File(it, ShotStore.DIR_NAME), config.shotKeepFiles)
        }
        if (shotStore == null) Log.w(TAG, "외부 저장소를 못 써서 촬영본을 남기지 않는다")

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
        // 🔴 **프리뷰를 분석과 같은 4:3 으로 못박는다** (REQ-0331 · 수정 A).
        //
        // 왜 : OverlayView 는 **분석 프레임** 기준으로 FILL_CENTER 크롭을 계산하고,
        //      PreviewView 는 **프리뷰 영상** 을 FILL_CENTER 로 크롭한다.
        //      두 소스의 종횡비가 다르면 **크롭 양이 달라지고 그 차이가 곧 박스 어긋남**이다
        //      (`ViewTransformTest` 가 그 크기를 값으로 갖고 있다 — 가로 최대 360px).
        //
        // 🔑 이것은 "원인을 고치는" 수정이 아니라 **"원인이 생길 수 없게" 하는 수정**이다.
        //    종횡비가 같으면 크롭 어긋남이 **원리적으로 0** 이라, 오검출 같은 다른 후보가
        //    남아 있어도 이 변경은 아무것도 망치지 않는다.
        //
        // ⚠ **4:3 으로 통일한 이유**: 분석을 16:9 로 바꾸면 분석 해상도가 달라져
        //    **인식률 축이 하나 늘어난다.** 분석을 그대로 두고 프리뷰를 맞추는 쪽이 축을 안 늘린다.
        val ratio4x3 = ResolutionSelector.Builder()
            .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
            .build()

        val preview = Preview.Builder()
            .setResolutionSelector(ratio4x3)
            .build()
            .also { it.surfaceProvider = previewView.surfaceProvider }

        // 분석 해상도를 낮게 잡는다. 원본 해상도로 돌리면 프레임을 흘려서 프리뷰가 끊긴다.
        val analysis = ImageAnalysis.Builder()
            .setResolutionSelector(
                ResolutionSelector.Builder()
                    .setResolutionStrategy(
                        ResolutionStrategy(Size(640, 480), ResolutionStrategy.FALLBACK_RULE_CLOSEST_HIGHER_THEN_LOWER),
                    )
                    // 🔴 종횡비도 **명시** 한다. 640x480 요청만으로는 폴백이 다른 비율을 줄 수 있고,
                    //    그러면 프리뷰와 다시 갈린다(REQ-0331 수정 A 의 나머지 반쪽).
                    .setAspectRatioStrategy(AspectRatioStrategy.RATIO_4_3_FALLBACK_AUTO_STRATEGY)
                    .build(),
            )
            // 밀리면 최신 프레임만 남긴다. 큐잉하면 인식 결과가 과거를 가리킨다.
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
            .build()

        // analyzer 를 vision executor 에 붙인다 = 네이티브 호출이 그 스레드에 갇힌다.
        analysis.setAnalyzer(engine.executor(), ::analyze)

        // 셔터. 번호판이 잡혔을 때 한 장을 찍는다(사용자 요구 — "번호가 잡히면 셔터").
        // 지연 최소 모드: 차가 지나가는 순간을 잡아야 하므로 품질보다 시점이 중요하다.
        val capture = ImageCapture.Builder()
            .setCaptureMode(ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY)
            .build()

        try {
            provider.unbindAll()
            provider.bindToLifecycle(
                this, CameraSelector.DEFAULT_BACK_CAMERA, preview, analysis, capture,
            )
            imageCapture = capture
            analysisRunning = true

            // 🔴 좌표 어긋남 진단(REQ-0331). **프리뷰와 분석의 종횡비가 다르면 크롭이 달라진다.**
            // OverlayView 는 분석 프레임 기준으로 FILL_CENTER 를 흉내내는데, 프리뷰가 다른
            // 종횡비를 다른 배율로 채우면 그 차이가 곧 박스 어긋남이다.
            // 🔑 이 세 줄이 그것을 값으로 가른다 — 없으면 폰이 있어도 원인을 못 잰다.
            previewView.post {
                val pv = preview.resolutionInfo
                val an = analysis.resolutionInfo
                Log.i(
                    TAG,
                    "좌표진단 · 프리뷰뷰=${previewView.width}x${previewView.height} " +
                        "오버레이=${overlayView.width}x${overlayView.height} " +
                        "scaleType=${previewView.scaleType}",
                )
                Log.i(
                    TAG,
                    "좌표진단 · Preview해상도=${pv?.resolution} 회전=${pv?.rotationDegrees} " +
                        "· Analysis해상도=${an?.resolution} 회전=${an?.rotationDegrees}",
                )
                // 🔑 두 종횡비가 다르면 그 자체가 원인 후보다. 같으면 다른 데를 봐야 한다.
                val pr = pv?.resolution?.let { it.width.toFloat() / it.height }
                val ar = an?.resolution?.let { it.width.toFloat() / it.height }
                Log.i(
                    TAG,
                    "좌표진단 · 종횡비 프리뷰=${pr?.let { "%.3f".format(it) }} " +
                        "분석=${ar?.let { "%.3f".format(it) }} " +
                        if (pr != null && ar != null && kotlin.math.abs(pr - ar) > 0.01f)
                            "🔴 **다르다 — 크롭이 갈린다**" else "✅ 같다",
                )
            }
        } catch (e: Exception) {
            // use case 셋을 못 붙이는 기기일 수 있다. 절반만 도는 상태로 두지 않는다 —
            // analysisRunning 이 false 면 촬영 요청이 camera_unavailable 로 답해진다.
            imageCapture = null
            analysisRunning = false
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
                // ⚠ **배열 꼬리가 안 채워질 수 있고, 그 자리에 이전 프레임 바이트가 남는다.**
                //
                // 기기에 따라 Y 버퍼 용량이 `(height-1)*rowStride + width` 다(마지막 행에 패딩이
                // 없다). 그러면 `minOf(size, remaining())` 이 짧게 복사하고, `obtainBuffer` 는
                // 배열을 **재사용**하므로 남은 꼬리에 직전 프레임의 바이트가 그대로 있다.
                //
                // 🔑 **지금 무해한 이유는 "안 읽어서" 다** — 네이티브가 행마다 앞 `width` 바이트만
                //    읽고 그 패딩을 안 본다(cpp 확인, REQ-0399 §①). 값이 지워져서가 아니다.
                // 🔴 그러니 **네이티브가 패딩을 읽기 시작하면 이 쪽은 안 바뀌었는데 증상이 난다.**
                //    그때 의심할 자리가 여기다 — 프레임 경계에 이전 그림 조각이 섞이는 모양으로 나온다.
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

        // 🔴🔴 **샘플 모드의 인식값을 서버로 올리지 않는다.**
        //
        // 이 금지는 촬영 경로(`onShootRequest`)에 이미 있었는데 **push 통로에는 없었다.**
        // 그래서 샘플 모드로 표본을 훑는 동안 `kor8-*` 의 값이 실서버로 나갔고,
        // 서버는 **있지도 않은 차**의 번호판으로 자리를 배정하려다 실패했다
        // (monitor 실측 2026-08-25 09:49~09:50 — 10건 도착 · 배정 0/10).
        //
        // ★ §"금지의 범위는 그 금지가 막으려는 사고의 범위와 같아야 한다" —
        //   막으려던 사고(*"모의가 실기보다 더 해 주면 안 된다"*)는 두 통로에 **똑같이** 있었는데
        //   금지는 한쪽에만 걸려 있었다. 통로가 아니라 **사고**를 기준으로 막는다.
        //
        // ⚠ 상시 push 스트림 자체는 계약 §5·§7 이 정한 통로라 **없애지 않는다.**
        //   막는 것은 **샘플 모드일 때만**이고, 카메라 경로는 그대로 흐른다.
        if (sampleMode) {
            // 🔑 **막았다는 것을 값으로 남긴다.** 서버 로그의 `0건` 만으로는
            //    *"안 나갔다"* 와 *"통로가 통째로 죽었다"* 가 구분되지 않는다(§"분모 없는 0").
            //    이 줄이 있으면 0 이 **부재가 아니라 차단**이라는 증거가 된다.
            // ⚠ 같은 값이 계속 인식되므로 **값이 바뀔 때만** 찍는다(33ms 마다 찍으면 로그가 덮인다).
            if (result.hasValue && result.value != lastBlockedPush) {
                lastBlockedPush = result.value
                Log.i(TAG, "샘플 모드 — push 차단: '${result.value}' (서버로 안 보낸다)")
            }
        } else {
            sender.offer(result)
        }

        // 🔴 프리뷰 결과로는 **답하지 않고, 셔터도 열지 않는다.**
        // 사용자 확정(2026-08-24): *"사진 촬영하여 촬영된 이미지를 기반으로 추출"*.
        // 여기 남은 일은 화면(오버레이·상태)뿐이고, 답은 recognizeShot() 이 촬영본에서 만든다.
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
                // 🔴 `engine` 을 **conf 바로 앞**에 붙인다 — 두 눈금이 한 열에 섞여 있어서
                //    떨어뜨려 놓으면 사람이 다시 한 열로 센다([DigitResult.engine] 주석 참조).
                "· reason=${r.reason} engine=${r.engine} conf=${"%.2f".format(r.conf)} format=${r.plateFormat} " +
                // 🔑 **인식 시간이 없으면 인터벌을 못 잰다.** 판정만 남기면 "맞았나" 는 알아도
                //    "몇 초 걸리나" 는 못 답한다 — 두 물음은 다른 자료가 필요하다.
                "ms=${"%.1f".format(r.ms)} 입력=${r.frameWidth}x${r.frameHeight} edge=${GrayConvert.activeMaxEdge}",
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

            memorySnapshot = MemoryWatch.sample(this@MainActivity, "샘플 ${sampleIndex + 1} 투입")

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
                            // 🔴 회전은 **0 이다. EXIF 를 읽어 넘기지 마라 — 값으로 나빠진다.**
                            //
                            // 2026-08-24 실측(실촬영 15장 · `docs/android/LEDGER.md` 참조):
                            //   EXIF 미적용 → 정답 1/15   ·   EXIF 적용 → 정답 **0/15**
                            //   (유일하게 읽히던 006 이 ok 0.78 → low_conf 0.04 로 무너졌다)
                            // 🔑 이유: 이 사진들은 **책상에 평평히 놓인** 스티커를 위에서 찍은 것이라
                            //   EXIF 가 말하는 "폰의 위" 와 "판의 위" 가 무관하다. 15장 중 14장은
                            //   **회전을 안 준 원본 화소에서 이미 판이 가로**다. 회전을 주면 그 14장이 눕는다.
                            // ⚠ 차에 붙은 실제 번호판은 이야기가 다를 수 있다. 다만 **파일 입력은
                            //   디버그 표본 경로뿐**이고, 실제 카메라 경로는 `imageInfo.rotationDegrees`
                            //   를 쓰므로 이 결정의 사정거리 밖이다.
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
        ) + "\n" + getString(R.string.model_line, engine.modelState) +
            // 🔴 강등되면 **반드시 보이게 한다.** 조용히 낮은 해상도로 돌면 다음 사람이
            //    인식률 표를 보고 "회귀" 로 읽고 원인을 못 찾는다.
            (if (GrayConvert.degraded) getString(R.string.degraded_line, GrayConvert.activeMaxEdge) else "") +
            (memorySnapshot?.let { "\n" + MemoryWatch.line(it) } ?: "")

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
        analysisRunning = false
        cameraProvider?.unbindAll()
        sender.stop()
        engine.shutdown()
        super.onDestroy()
    }

    private companion object {
        const val TAG = "MainActivity"

        /** 샘플 프레임 투입 간격. 카메라(약 30fps)와 비슷하게 두어 게이트 동작을 같게 만든다. */
        const val SAMPLE_FRAME_INTERVAL_MS = 33L

        /**
         * 촬영 시한을 확인하는 주기. 시한 판정의 정밀도가 이만큼 거칠어진다
         * (기본 시한 5000ms 에 대해 ±500ms). 더 촘촘히 볼 이유가 없다 —
         * 서버는 이 답을 기다리는 동안 `cameraAge()` 로 자기 경과를 따로 본다.
         */
        /** 워치독 확인 주기. 30초 상한에 견줘 촘촘하지만 하는 일이 정수 비교 하나뿐이라 싸다. */
        const val CAPTURE_WATCHDOG_TICK_MS = 500L

        /**
         * 진행 화면 갱신 주기. 경과를 `0.1초` 자리까지 보이므로 그보다 촘촘하다.
         * 🔑 이 코루틴은 **UI 스레드**라 인식과 경쟁하지 않는다. 값이 안 바뀌면 그리지도 않는다.
         */
        const val PROGRESS_TICK_MS = 100L

        /**
         * 도는 것이 없을 때의 갱신 주기. 끝난 줄의 *"N분 전"* 만 바뀌므로 촘촘할 이유가 없다.
         * ⚠ **0 으로 두지 마라**(= 멈추지 마라) — 그러면 *"방금"* 이 영원히 *"방금"* 이다.
         */
        const val PROGRESS_IDLE_TICK_MS = 2_000L
    }
}
