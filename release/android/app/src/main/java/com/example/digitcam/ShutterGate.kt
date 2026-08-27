package com.example.digitcam

/**
 * **언제 셔터를 누를지** 정하는 문. 사용자 요구가 *"사진 촬영하여 촬영된 이미지를 기반으로 추출"* 이다.
 *
 * ## 🔴 요청이 오면 **바로 찍는다** — 프리뷰는 셔터를 열지 않는다 (2026-08-24 전환)
 *
 * ```
 * 요청(서버 SHOOT · 촬영 버튼)  →  즉시 셔터  →  촬영본에서 인식  →  답
 * 프리뷰(ImageAnalysis)         →  화면 오버레이·상태 표시 **전용**
 * ```
 * 인식의 입력은 언제나 `takePicture()` 로 얻은 **정지 이미지**다. 프리뷰 프레임의 한 장이 아니다.
 *
 * ### 왜 바꿨나 — 프리뷰 검출을 셔터의 조건으로 두면 **셔터가 영영 안 눌린다**
 *
 * 🔴 **셔터의 조건에 프리뷰 검출을 넣지 마라.** 그러면 검출이 안 서는 장면에서 촬영도 인식도
 * **시작조차 안 하고** 요청이 닫힌다 — 화면에는 아무 일도 안 일어난 것처럼 보인다.
 * 🔑 **못 읽는 것과 시작도 안 하는 것은 화면에서 같은 모양이고, 고칠 곳이 다르다.**
 *
 * ⚠ 대가를 적어 둔다: **차가 없어도 찍는다.** 판이 없으면 사유 있는 실패로 답한다.
 *
 * ## `ShotCoordinator` 와 무엇이 다른가
 *
 * ```
 * ShotCoordinator : 요청 **목록** — 누구에게 무엇을 답할지 · 대기 상한
 * ShutterGate     : 셔터 **하나** — 지금 찍을지 · 이미 찍는 중인지 · 몇 번까지 다시 찍을지
 * ```
 * 셔터는 기기에 하나뿐이라 이 상태도 하나다. 여러 요청이 대기 중이어도 **한 번 찍어 전부에게
 * 같은 값을 답한다**(`ShotCoordinator.onPlate` 가 이미 그렇게 한다 — 같은 차를 보고 있으므로).
 *
 * Android 의존이 없다 — 그래서 사슬 거동이 단위 시험으로 닫힌다.
 * 여러 스레드가 부른다(소켓 읽는 스레드의 요청 · 메인의 버튼 · vision 의 셔터 콜백) → 전부 잠금 아래다.
 */
class ShutterGate(
    /** 형식이 안 맞거나 못 읽었을 때 다시 찍는 횟수를 포함한 **총 시도 수**. */
    private val maxAttempts: Int = DEFAULT_MAX_ATTEMPTS,
    /**
     * 촬영 콜백이 영영 안 올 때의 상한. **인식이 느린 것은 여기 안 걸린다**([captureWatchdog]).
     * 설정에서 바뀔 수 있으므로 값이 아니라 함수로 받는다.
     */
    private val captureWatchdogMs: () -> Long = { DEFAULT_CAPTURE_WATCHDOG_MS },
    /**
     * 되감기지 않는 시계. **기본값이 상수 0 이라 시계를 안 넣으면 워치독이 영영 안 짖는다** —
     * 그것이 의도다. 단위 시험은 시간을 직접 밀고, 실제 앱만 `SystemClock.elapsedRealtime` 을 넣는다.
     */
    private val nowMs: () -> Long = { 0L },
) {

    private enum class Phase {
        /** 요청이 없다. */
        IDLE,

        /** 셔터를 눌렀고 파일이 아직 안 왔다. */
        CAPTURING,

        /** 파일이 왔고 인식 중이다. */
        RECOGNIZING,
    }

    sealed interface Decision {
        /** 아무것도 하지 마라. */
        data object Wait : Decision

        /** 지금 셔터를 눌러라. [attempt] 는 0 부터 — 파일 이름에 들어간다. */
        data class Fire(val attempt: Int) : Decision

        /** 이 값으로 답해라. */
        data class Accept(val plate: String) : Decision

        /** 포기해라. 이 사유로 답한다. */
        data class GiveUp(val reason: String) : Decision
    }

    private var phase = Phase.IDLE
    private var attempt = 0

    /** [Phase.CAPTURING] 에 들어간 시각. 워치독이 보는 유일한 값이다. */
    private var capturingSinceMs = 0L

    /** 지금 촬영·인식이 진행 중인가. 화면 표시용. */
    val busy: Boolean
        @Synchronized get() = phase == Phase.CAPTURING || phase == Phase.RECOGNIZING

    /**
     * 요청이 들어왔다. **[Decision.Fire] 를 돌려주면 그 자리에서 셔터를 눌러야 한다.**
     *
     * 이미 진행 중이면 [Decision.Wait] 다 — 촬영·인식이 도는 중에 또 누르면 셔터가 연발된다.
     * 그때는 지금 도는 한 장의 결과를 **대기 중인 요청 전부에게** 같이 답한다
     * (`ShotCoordinator.onPlate` 의 규약. 같은 장면을 보고 있으므로).
     */
    @Synchronized
    fun arm(): Decision {
        if (phase != Phase.IDLE) return Decision.Wait
        phase = Phase.CAPTURING
        capturingSinceMs = nowMs()
        attempt = 0
        return Decision.Fire(attempt)
    }

    /**
     * **촬영 콜백이 영영 안 오는 경우만** 잡는다. 주기적으로 부른다.
     *
     * ## 🔴 이것은 인식 시한이 아니다 — 그 시한은 폐기됐다 (2026-08-27 설계)
     *
     * ```
     * ❌ 폐기 : "인식이 오래 걸리면 끊는다"  → 인식은 **완주**한다. 10초대여도 기다린다
     * ✅ 남김 : "촬영 콜백이 안 온다"        → CameraX 가 죽으면 성공·실패 콜백이 **둘 다** 안 온다
     * ```
     * 🔑 **둘은 다른 고장이고 한 상수로 다루면 둘 다 못 다룬다.**
     * 콜백이 없으면 [Phase.CAPTURING] 에 영원히 갇히고, 그러면 **이후 모든 요청이
     * [Decision.Wait]** 이 된다 — 서버에서 보면 카메라가 통째로 침묵한다.
     *
     * ⚠ **[Phase.RECOGNIZING] 은 절대 건드리지 않는다.** 그것이 완주 보장이다 —
     * 여기서 인식 중을 끊으면 *"10초를 다 쓰고 답만 버린다"* 는 옛 결함이 그대로 돌아온다.
     */
    @Synchronized
    fun captureWatchdog(): Decision {
        if (phase != Phase.CAPTURING) return Decision.Wait
        if (nowMs() - capturingSinceMs < captureWatchdogMs()) return Decision.Wait
        phase = Phase.IDLE
        attempt = 0
        return Decision.GiveUp(CameraShot.REASON_CAPTURE_STUCK)
    }

    /** 대기가 사라졌다(전부 답했거나 버렸다). */
    @Synchronized
    fun disarm() {
        phase = Phase.IDLE
        attempt = 0
    }

    /** 셔터·저장이 끝났다. 이제 인식을 돌린다. */
    @Synchronized
    fun onCaptured() {
        if (phase == Phase.CAPTURING) phase = Phase.RECOGNIZING
    }

    /**
     * 셔터나 저장이 실패했다. **재시도하지 않는다** — 저장공간·하드웨어 문제라 기다려도 안 된다.
     */
    @Synchronized
    fun onCaptureFailed(): Decision {
        phase = Phase.IDLE
        return Decision.GiveUp(CameraShot.REASON_CAPTURE_FAILED)
    }

    /**
     * 촬영본 인식 결과.
     *
     * @param plate 읽은 번호판. 못 읽었으면 빈 문자열
     * @param formatKnown 네이티브가 형식을 알아봤나(`plateFormat != "unknown"`)
     * @param failReason 포기할 때 실을 사유. **네이티브 `reason` 을 옮겨 온 값이다**
     *
     * 🔑 형식 판정을 **여기서 다시 하지 않는다.** 네이티브가 `format` 을 주므로 Kotlin 이
     * 또 판정하면 진실이 두 곳이 된다. 우리는 그 필드를 **조건으로 쓸 뿐**이다.
     *
     * ## 🔴 사유를 인자로 받는 이유 — 뭉치면 고칠 곳을 못 정한다
     *
     * 예전에는 실패가 전부 `recognize_failed` 하나였다. 그런데 **`no_plate`(검출 실패)와
     * `segment_fail`(분할 실패)은 조정 방향이 반대다** — 뭉쳐 놓으면 화면을 봐도 무엇을 할지 모른다.
     * 그래서 판단은 여기서 하되 **이름은 네이티브가 준 것을 그대로 나른다**
     * ([CameraShot.reasonFromNative]).
     */
    @Synchronized
    fun onRecognized(
        plate: String,
        formatKnown: Boolean,
        failReason: String = CameraShot.REASON_RECOGNIZE_FAILED,
    ): Decision {
        if (phase != Phase.RECOGNIZING) return Decision.Wait

        val good = plate.isNotEmpty() && plate != CameraShot.PLATE_MARKER && formatKnown
        if (good) {
            phase = Phase.IDLE
            return Decision.Accept(plate)
        }

        attempt++
        if (attempt >= maxAttempts) {
            phase = Phase.IDLE
            return Decision.GiveUp(failReason)
        }
        // 🔴 **바로 다시 찍는다.** 예전에는 여기서 `WAITING` 으로 돌아가 프리뷰 검출을 다시
        //    기다렸는데, 그 검출이 안 서는 장면에서는 재시도가 **한 번도 일어나지 않았다**.
        //    상한(`maxAttempts`)이 재시도를 끝내지, 프리뷰가 끝내는 것이 아니다.
        phase = Phase.CAPTURING
        capturingSinceMs = nowMs()
        return Decision.Fire(attempt)
    }

    companion object {
        /**
         * 총 시도 수. **1 = 한 번 찍고 결과를 그대로 답한다.**
         *
         * ## 🔴 2 → 1 (2026-08-27) — 성능 때문이 아니다
         *
         * ```
         * 새 설계에서 재요청의 방아쇠는 **서버**다(실패 응답을 받으면 다시 요청한다)
         * 그대로 두면 서버 3회 × 폰 2회 = **촬영 6회 · 최대 60초**로 곱해진다
         * ```
         * 🔑 그런데 더 센 이유가 따로 있다 — **폰은 "차가 아직 있나" 를 모른다.**
         * 모르는 채로 다시 찍으면 **앞차가 빠지고 뒤차가 들어온 뒤에 찍을 수 있고**,
         * 그 값이 앞 요청의 답으로 나간다. 설계 [C] 가 막으려는 사고를 **폰이 스스로 만든다.**
         *
         * > ★ **재시도 결정은 차의 유무를 아는 쪽이 해야 한다. 그건 서버뿐이다.**
         *
         * ⚠ 재시도 **기능 자체는 남겨 뒀다**(`maxAttempts` 를 올리면 산다). 지운 것은 기본값이다 —
         * 서버 전담이 값으로 안 좋으면 되돌릴 자리가 여기 하나로 남아 있어야 한다.
         */
        const val DEFAULT_MAX_ATTEMPTS = 1

        /**
         * 촬영 콜백 워치독. **인식 시한이 아니다**([captureWatchdog] 참조).
         *
         * ⚠ **추정값이다.** `takePicture` 콜백이 실제로 얼마나 늦을 수 있는지 안 쟀다.
         * 크게 잡은 것은 의도다 — 이 값이 작으면 **느린 촬영을 고장으로 오인**한다.
         * 🔑 워치독은 *"드물게, 진짜로 죽었을 때만"* 짖어야 한다. 자주 짖는 워치독은 꺼진다.
         */
        const val DEFAULT_CAPTURE_WATCHDOG_MS = 30_000L
    }
}
