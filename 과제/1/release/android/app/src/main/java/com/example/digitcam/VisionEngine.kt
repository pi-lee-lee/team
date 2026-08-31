package com.example.digitcam

import android.util.Log
import java.util.concurrent.Executors
import java.util.concurrent.ThreadFactory
import java.util.concurrent.TimeUnit

/**
 * 네이티브 핸들의 **유일한 소유자**. 계약 §4 가 `nativeConfigure` 를 "같은 스레드에서만
 * 부른다"고 전제하므로, 핸들의 생성·설정·처리·해제 전부를 스레드 하나에 가둔다.
 *
 * 그 스레드가 [executor] 다. CameraX `ImageAnalysis.setAnalyzer` 에 넘기는 것도 같은
 * executor 이고, 샘플 이미지 모드도 이 executor 로 들어온다. 그래서 어느 경로로 오든
 * `nativeProcessGray` 는 직렬화된다.
 *
 * 왜 이렇게까지 하냐면 — 카메라 프레임이 도는 중에 메인 스레드에서 "설정 다시 읽기" 가
 * `nativeConfigure` 를 부르거나 `onDestroy` 가 `nativeDestroy` 를 부르면, 네이티브
 * 파이프라인 객체가 처리 도중에 바뀌거나 사라진다. 전자는 값이 튀는 정도지만 후자는
 * use-after-free 라 프로세스가 죽는다.
 */
class VisionEngine {

    private val executor = Executors.newSingleThreadExecutor(
        ThreadFactory { r -> Thread(r, "digitcam-vision") },
    )

    /** 이 executor 위에서만 아래 필드들을 만진다. */
    private var handle = 0L
    private var created = false

    /**
     * 메인 스레드가 설정을 바꾸면 여기에 놓아 두고, 다음 프레임 처리 직전에
     * executor 스레드가 집어 간다. 메인에서 직접 nativeConfigure 를 부르지 않기 위한 우회로다.
     */
    @Volatile
    private var pendingConfig: AppConfig? = null

    /** CameraX analyzer 와 샘플 모드가 공유하는 실행 스레드. */
    fun executor(): java.util.concurrent.Executor = executor

    /** 마지막으로 확인한 적재/생성 실패 사유. 화면 표시용. */
    @Volatile
    var lastError: String? = null
        private set

    /** OpenCV 버전. 네이티브가 준비되기 전에는 null. */
    @Volatile
    var openCvVersion: String? = null
        private set

    /**
     * 인식 모델 적재 결과. 화면에 그대로 띄운다.
     *
     * 🔑 **"모델이 실렸는가" 는 인식률 표의 분모다.** 안 실린 채로 잰 표는 예전 경로의 표이고,
     * 그것을 새 경로의 표로 읽으면 결론이 통째로 틀린다. 그래서 값으로 보이게 둔다.
     */
    @Volatile
    var modelState: String = "미확인"
        private set

    /**
     * 메인 스레드에서 부른다. 실제 적용은 executor 위에서 다음 프레임 직전에 일어난다.
     * "설정 다시 읽기" 가 이 경로를 쓴다.
     */
    fun updateConfig(config: AppConfig) {
        pendingConfig = config
    }

    /**
     * 초기화를 executor 에 예약한다. 네이티브가 없으면 [lastError] 만 채우고 조용히 넘어간다.
     *
     * @param models 인식 모델을 읽어 오는 함수. **executor 스레드에서 불린다** —
     *   7 MiB 가 넘어서 메인에서 읽으면 첫 화면이 그만큼 늦는다. null 을 돌려주면
     *   모델 없이 진행한다(예전 템플릿 경로만 돈다). 그것도 정상 동작이다.
     */
    fun start(
        initialConfig: AppConfig,
        models: () -> OcrModels.Bundle? = { null },
        onReady: (String?) -> Unit,
    ) {
        pendingConfig = initialConfig
        executor.execute {
            if (!DigitVision.isAvailable) {
                lastError = "libdigitcam.so 적재 실패: ${DigitVision.loadError}"
                Log.w(TAG, lastError!!)
                onReady(null)
                return@execute
            }
            try {
                openCvVersion = DigitVision.nativeVersion()
                handle = DigitVision.nativeCreate()
                created = handle != 0L
                if (!created) lastError = "nativeCreate 가 0 을 돌려줬다(파이프라인 생성 실패)"
                loadModels(models)
                applyPendingConfig()
                onReady(openCvVersion)
            } catch (e: UnsatisfiedLinkError) {
                // 라이브러리는 올라왔지만 심볼이 어긋난 경우. 계약 §4 시그니처 불일치가 대표적이다.
                lastError = "네이티브 심볼 불일치: ${e.message}"
                Log.w(TAG, lastError!!)
                onReady(null)
            }
        }
    }

    /**
     * **executor 스레드에서만 부른다.** 한 프레임 처리.
     * 네이티브가 없으면 [DigitResult.EMPTY] 를 돌려준다.
     */
    fun process(gray: ByteArray, width: Int, height: Int, rowStride: Int, rotationDegrees: Int): DigitResult {
        if (!created) return DigitResult.EMPTY
        applyPendingConfig()
        return try {
            DigitResult.parse(
                DigitVision.nativeProcessGray(handle, gray, width, height, rowStride, rotationDegrees),
            )
        } catch (e: UnsatisfiedLinkError) {
            lastError = "nativeProcessGray 심볼 불일치: ${e.message}"
            created = false
            DigitResult.EMPTY
        }
    }

    /**
     * 모델 적재. **executor 스레드 전용** — `nativeCreate` 직후에 부른다.
     *
     * ⚠ **실패를 치명적으로 다루지 않는다**(cpp 규약). 못 실으면 네이티브가 예전 템플릿
     * 경로로만 돌고 인식 자체는 계속된다. 다만 [modelState] 에 남겨 **화면에서 보이게** 한다 —
     * 🔑 조용히 빠지면 *"모델이 있는 줄 알았는데 없는"* 상태로 인식률 표를 읽게 되고,
     * 그 표는 무엇을 재고 있는지 모르는 표가 된다.
     */
    private fun loadModels(provider: () -> OcrModels.Bundle?) {
        if (!created) return
        val t0 = System.nanoTime()
        val bundle = provider()
        if (bundle == null) {
            modelState = "모델 없음(템플릿 경로)"
            Log.w(TAG, "인식 모델이 없다 — 템플릿 경로로만 돈다")
            return
        }
        val readMs = (System.nanoTime() - t0) / 1_000_000.0
        val t1 = System.nanoTime()
        val ok = try {
            DigitVision.nativeLoadModels(handle, bundle.det, bundle.rec, bundle.dict)
        } catch (e: UnsatisfiedLinkError) {
            // 네이티브가 아직 이 심볼을 안 내보내는 빌드일 수 있다. 그것도 "모델 없음" 이다.
            modelState = "심볼 없음(템플릿 경로)"
            Log.w(TAG, "nativeLoadModels 심볼 불일치: ${e.message}")
            return
        }
        val loadMs = (System.nanoTime() - t1) / 1_000_000.0
        modelState =
            if (ok) "${"%.2f".format(bundle.totalBytes / 1048576.0)}MiB · 읽기 ${"%.0f".format(readMs)}ms · 적재 ${"%.0f".format(loadMs)}ms"
            else "적재 실패(템플릿 경로)"
        Log.i(TAG, "nativeLoadModels → $ok · $modelState")
    }

    /**
     * **정지 이미지 한 장**을 인식한다. **executor 스레드에서만 부른다.**
     *
     * ## 🔴 왜 같은 프레임을 반복해서 넣나 — 한 번 넣으면 반드시 잘린다
     *
     * 계약 §9.1 의 움직임 게이트는 **직전 프레임과의 차분**을 본다(`phaseCorrelate`).
     * 촬영본은 그 직전이 프리뷰 프레임이라 차분이 크게 잡히고, 게이트는 뒤 단계를
     * **아예 돌리지 않는다** — `reason=moving` 에 값은 빈 문자열이다.
     *
     * ```
     * 네이티브 MotionGate : v.moving = still_run < still_frames   (still_frames 기본 3)
     *   1번째 : 비교 대상 없음        → moving           (prev 를 채우는 프레임이다)
     *   2번째 : 차분 0, still_run=1  → moving
     *   3번째 : 차분 0, still_run=2  → moving
     *   4번째 : 차분 0, still_run=3  → **여기서 열린다**
     * ```
     * 🔑 그래서 **최소 4장**이 필요하다. 샘플 모드가 같은 그림을 33ms 간격으로 계속
     * 밀어 넣는 것도 같은 이유이고, 실제로 샘플마다 첫 판정이 `moving` 으로 찍힌다
     * (2026-08-24 실측 — 41장 전부 그랬다).
     *
     * ⚠ 이것은 게이트를 **끄는 것이 아니다.** 선명도(`blurry`)는 그대로 살아 있고,
     * 움직임 게이트도 정상적으로 판정한다 — 다만 *한 장짜리 입력에 대해서는 비교 대상이
     * 없다는 사실* 을 프레임을 채워서 해소할 뿐이다.
     *
     * @param maxFrames 상한. 게이트가 안 열려도 여기서 멈추고 **마지막 결과를 그대로** 돌려준다
     *   (조용히 실패하지 않는다 — `reason=moving` 이 판정 줄에 남아야 원인을 셀 수 있다).
     */
    fun processStill(
        gray: ByteArray,
        width: Int,
        height: Int,
        rowStride: Int,
        rotationDegrees: Int,
        maxFrames: Int = STILL_MAX_FRAMES,
    ): DigitResult {
        var last = DigitResult.EMPTY
        for (i in 0 until maxFrames) {
            last = process(gray, width, height, rowStride, rotationDegrees)
            if (!last.moving) {
                if (i > 0) Log.i(TAG, "정지 인식 — ${i + 1}번째 투입에서 움직임 게이트 통과")
                return last
            }
        }
        Log.w(TAG, "정지 인식 — ${maxFrames}장을 넣어도 움직임 게이트가 안 열렸다(motion=${last.motion})")
        return last
    }

    /** executor 스레드 전용. */
    private fun applyPendingConfig() {
        val cfg = pendingConfig ?: return
        pendingConfig = null
        if (!created) return
        try {
            // 임계값을 바꿨는데 동작이 안 변할 때, "네이티브가 무시했다" 와 "빈 문자열을 보냈다" 를
            // 가르는 유일한 증거다. gray 통계 로그와 같은 이유로 남겨 둔다.
            Log.i(TAG, "nativeConfigure ← ${cfg.visionConfig.length}자\n${cfg.visionConfig}")
            // 계약 개정 2 §4 — vision.* 줄 묶음을 문자열 그대로 넘긴다. 여기서 해석하지 않는다.
            DigitVision.nativeConfigure(handle, cfg.visionConfig)
        } catch (e: UnsatisfiedLinkError) {
            lastError = "nativeConfigure 심볼 불일치: ${e.message}"
            created = false
        }
    }

    /**
     * 해제. **호출 전에 CameraX 바인딩을 먼저 끊어야 한다** — analyze() 가 날아다니는 중에
     * 핸들을 없애면 use-after-free 다. 해제 자체도 executor 위에서 하므로, 이미 큐에 들어간
     * 프레임 처리가 모두 끝난 뒤에 실행된다.
     */
    fun shutdown() {
        executor.execute {
            if (created) {
                try {
                    DigitVision.nativeDestroy(handle)
                } catch (e: UnsatisfiedLinkError) {
                    Log.w(TAG, "nativeDestroy 실패: ${e.message}")
                }
            }
            created = false
            handle = 0L
        }
        executor.shutdown()
        // 해제가 끝나기를 잠깐 기다린다. 무한정 붙잡으면 onDestroy 가 ANR 로 이어진다.
        //
        // ⚠ **1초의 여유가 예전보다 좁다**(cpp 짝검증 REQ-0399). 표준 경로가 들어오면서
        //    프레임 한 장이 5~60ms → ~345ms 가 됐다. 진행 중 1장 + 큐에 1장이면 ~700ms 다.
        // ✅ **use-after-free 는 아니다** — 단일 스레드 executor 라 "진행 중 프레임 → destroy"
        //    순서가 보장되고, 이 대기가 시간을 넘겨도 destroy 는 **그 뒤에 반드시** 실행된다.
        //    넘길 때 일어나는 일은 onDestroy 가 먼저 반환하고 네이티브가 뒤에서 마저 도는 것뿐이다.
        // 🔑 그래서 안 고친다. 다만 **로그가 이상한 순서로 찍히면 이 자리다.**
        try {
            executor.awaitTermination(1, TimeUnit.SECONDS)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    companion object {
        private const val TAG = "VisionEngine"

        /**
         * [processStill] 이 같은 프레임을 최대 몇 번까지 넣나.
         *
         * 하한은 `vision.still_frames + 1` 이다(기본 3 → 4). 여유를 둔 것은 그 값이 설정으로
         * 바뀔 수 있기 때문이고, 상한이 있는 것 자체가 요점이다 — 게이트가 끝내 안 열리는
         * 입력에서 무한정 돌면 셔터가 안 끝난다.
         *
         * ## 🔴 비용은 **"이 값 × 프레임 단가" 가 아니다** — 그렇게 계산하지 마라
         *
         * ```
         * 1 ~ still_frames 번째 : 게이트가 moving 이라 process() 가 **검출 전에 반환**한다. 각 수 ms
         * 그 다음 1장           : 게이트 통과 → 인식 경로 전체가 돈다
         *                        🔑 그런데 `!moving` 이므로 **그 자리에서 루프가 끝난다**
         * 나머지                : **안 돈다**
         * → 비싼 프레임은 이 상한과 무관하게 **언제나 1장**이다
         * ```
         * 그 1장의 값은 **모델이 실렸는지**로 갈린다 — 화면의 [modelState] 가 그 답이다:
         * ```
         * 모델 없음(템플릿만)  5~60 ms
         * 모델 있음(표준 경로) **~345 ms**  (에뮬 arm64 · 1600×900 실측 2026-08-25)
         * ```
         * ⚠ **이 값을 올려도 최악이 그만큼 늘지 않는다.** 게이트가 끝내 안 열리는 입력에서만
         * 늘어나고, 그때는 **전부 싼 프레임**이다.
         *
         * ★ 예전 주석은 *"8장 × 5~60ms ≈ 0.5초"* 였다. 결과값은 지금도 얼추 맞지만
         * **근거가 틀렸다** — 그 계산으로 이 상한을 판단하면 다음에 조용히 어긋난다
         * (cpp 짝검증 REQ-0399 가 잡았다).
         */
        const val STILL_MAX_FRAMES = 8
    }
}
