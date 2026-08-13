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
     * 메인 스레드에서 부른다. 실제 적용은 executor 위에서 다음 프레임 직전에 일어난다.
     * "설정 다시 읽기" 가 이 경로를 쓴다.
     */
    fun updateConfig(config: AppConfig) {
        pendingConfig = config
    }

    /**
     * 초기화를 executor 에 예약한다. 네이티브가 없으면 [lastError] 만 채우고 조용히 넘어간다
     * (cpp-engineer 의 REQ-0003 이 끝나기 전에는 이 상태가 정상이다).
     */
    fun start(initialConfig: AppConfig, onReady: (String?) -> Unit) {
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
        try {
            executor.awaitTermination(1, TimeUnit.SECONDS)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    private companion object {
        const val TAG = "VisionEngine"
    }
}
