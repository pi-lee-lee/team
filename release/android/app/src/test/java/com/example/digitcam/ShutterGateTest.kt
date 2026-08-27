package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 요청 → 셔터 → 인식 사슬. **실물이 없어도 이 사슬의 거동은 전부 닫힌다** —
 * `Bitmap`·CameraX 에 닿지 않게 판단만 뽑아 뒀기 때문이다.
 *
 * 🔴 2026-08-24 에 규약이 바뀌었다: **프리뷰는 셔터를 열지 않는다.** 요청이 오면 바로 찍는다.
 * 그래서 `onPreview` 가 통째로 사라졌고, 이 시험도 그 갈래를 **지웠다**(남기면 죽은 갈래가 된다).
 */
class ShutterGateTest {

    private fun gate(maxAttempts: Int = 2) = ShutterGate(maxAttempts)

    // ---- 요청 → 즉시 셔터 ---------------------------------------------------

    @Test
    fun `요청이 오면 그 자리에서 찍는다`() {
        val g = gate()
        assertEquals(ShutterGate.Decision.Fire(0), g.arm())
        assertTrue("셔터 뒤에는 진행 중이어야 한다", g.busy)
    }

    @Test
    fun `🔴 요청 전에는 진행 중이 아니다`() {
        assertFalse(gate().busy)
    }

    @Test
    fun `🔴 한 요청에 셔터는 한 번만 - 요청이 겹쳐도 연발하지 않는다`() {
        val g = gate()
        assertEquals(ShutterGate.Decision.Fire(0), g.arm())
        // 촬영이 도는 중에 다음 요청이 들어온다. 여기서 또 Fire 를 내면 셔터가 연발된다.
        assertEquals(ShutterGate.Decision.Wait, g.arm())
        assertEquals(ShutterGate.Decision.Wait, g.arm())
        assertTrue(g.busy)
    }

    @Test
    fun `인식 중에 요청이 들어와도 새로 찍지 않는다`() {
        // 지금 도는 한 장의 결과를 대기 중인 요청 전부에게 같이 답한다(ShotCoordinator 규약).
        val g = gate()
        g.arm()
        g.onCaptured()
        assertEquals(ShutterGate.Decision.Wait, g.arm())
    }

    // ---- 인식 -------------------------------------------------------------

    @Test
    fun `읽었으면 그 값으로 답한다`() {
        val g = gate()
        g.arm()
        g.onCaptured()
        assertEquals(
            ShutterGate.Decision.Accept("12가3456"),
            g.onRecognized("12가3456", formatKnown = true),
        )
        assertFalse("답한 뒤에는 문이 닫힌다", g.busy)
    }

    @Test
    fun `🔴 못 읽으면 프리뷰를 기다리지 않고 바로 다시 찍는다`() {
        // 예전에는 여기서 WAITING 으로 돌아가 프리뷰 검출을 기다렸고, 그 검출이 안 서는
        // 장면에서는 **재시도가 한 번도 일어나지 않았다.** 재시도를 끝내는 것은 상한이다.
        val g = gate(maxAttempts = 2)
        assertEquals(ShutterGate.Decision.Fire(0), g.arm())
        g.onCaptured()
        assertEquals(ShutterGate.Decision.Fire(1), g.onRecognized("", formatKnown = false))
        assertTrue("재시도 중에도 진행 중이다", g.busy)
    }

    @Test
    fun `재시도 상한에 닿으면 recognize_failed 로 포기한다`() {
        val g = gate(maxAttempts = 2)
        g.arm()
        g.onCaptured()
        g.onRecognized("", formatKnown = false)   // 1회차 실패 → Fire(1)
        g.onCaptured()
        assertEquals(
            ShutterGate.Decision.GiveUp(CameraShot.REASON_RECOGNIZE_FAILED),
            g.onRecognized("", formatKnown = false),
        )
        assertFalse(g.busy)
    }

    @Test
    fun `형식을 못 알아보면 읽은 것으로 보지 않는다`() {
        // 값이 있어도 format=unknown 이면 오인식일 수 있다 → 다시 찍는다.
        // 🔑 형식 판정은 네이티브가 한다. 여기서 다시 판정하지 않고 그 필드를 조건으로만 쓴다.
        val g = gate(maxAttempts = 2)
        g.arm()
        g.onCaptured()
        assertEquals(ShutterGate.Decision.Fire(1), g.onRecognized("12X4567", formatKnown = false))
    }

    @Test
    fun `표지 문자열은 값으로 받지 않는다`() {
        // 서버는 번호판 자리의 "-1" 을 거절한다(명세 §2). 앱이 애초에 안 올린다.
        val g = gate(maxAttempts = 1)
        g.arm()
        g.onCaptured()
        assertEquals(
            ShutterGate.Decision.GiveUp(CameraShot.REASON_RECOGNIZE_FAILED),
            g.onRecognized(CameraShot.PLATE_MARKER, formatKnown = true),
        )
    }

    // ---- 실패 갈래 ---------------------------------------------------------

    @Test
    fun `셔터가 실패하면 재시도하지 않는다 - 기다려도 안 된다`() {
        // 저장공간·하드웨어 문제라 다시 찍어도 같다. capture_failed 로 즉시 답한다.
        val g = gate(maxAttempts = 3)
        g.arm()
        assertEquals(
            ShutterGate.Decision.GiveUp(CameraShot.REASON_CAPTURE_FAILED),
            g.onCaptureFailed(),
        )
        assertFalse(g.busy)
    }

    @Test
    fun `촬영 실패와 인식 실패는 다른 사유다`() {
        // "찍었는데 못 읽었다"(recognize_failed) 와 "셔터가 안 됐다"(capture_failed) 와
        // "콜백이 아예 안 왔다"(capture_stuck) 는 고칠 곳이 다르다.
        assertTrue(CameraShot.REASON_RECOGNIZE_FAILED != CameraShot.REASON_CAPTURE_FAILED)
        assertTrue(CameraShot.REASON_RECOGNIZE_FAILED != CameraShot.REASON_CAPTURE_STUCK)
        assertTrue(CameraShot.REASON_CAPTURE_FAILED != CameraShot.REASON_CAPTURE_STUCK)
    }

    // ---- 🔴 완주 보장 · 촬영 워치독 (2026-08-27) ----------------------------

    /** 시간을 손으로 미는 시계. 워치독을 재현 가능하게 만드는 유일한 방법이다. */
    private class FakeClock(var t: Long = 0L) : () -> Long {
        override fun invoke(): Long = t
    }

    private fun watched(limitMs: Long = 30_000L): Pair<ShutterGate, FakeClock> {
        val clock = FakeClock()
        return ShutterGate(maxAttempts = 1, captureWatchdogMs = { limitMs }, nowMs = clock) to clock
    }

    @Test
    fun `🔴 인식이 아무리 오래 걸려도 워치독이 끊지 않는다 - 완주 보장`() {
        val (g, clock) = watched(limitMs = 30_000L)
        g.arm()
        g.onCaptured()                       // 이제 RECOGNIZING 이다
        clock.t = 10 * 60 * 1000L            // 10분을 밀어도
        assertEquals(
            "인식 중은 워치독이 건드리면 안 된다 — 그게 완주 보장이다",
            ShutterGate.Decision.Wait,
            g.captureWatchdog(),
        )
        // 그리고 늦게 온 결과가 **살아 있다**(Wait 이 아니라 Accept 다).
        assertEquals(ShutterGate.Decision.Accept("12가3456"), g.onRecognized("12가3456", true))
    }

    @Test
    fun `🔴 촬영 콜백이 안 오면 워치독이 문을 연다`() {
        val (g, clock) = watched(limitMs = 30_000L)
        g.arm()                              // CAPTURING. 콜백은 영영 안 온다
        clock.t = 29_999L
        assertEquals("상한 직전에는 안 짖는다", ShutterGate.Decision.Wait, g.captureWatchdog())

        clock.t = 30_000L
        assertEquals(
            ShutterGate.Decision.GiveUp(CameraShot.REASON_CAPTURE_STUCK),
            g.captureWatchdog(),
        )
        // 🔑 문이 열려야 **다음 요청이 산다**. 안 열면 이후 전부 Wait 이 된다.
        assertEquals(ShutterGate.Decision.Fire(0), g.arm())
    }

    @Test
    fun `🔴 시계를 안 넣으면 워치독이 영영 안 짖는다`() {
        // 기본 시계는 상수 0 이다 — 실수로 시계 없이 만들면 **조용히 안 도는** 것이 아니라
        // 아예 안 짖는다. 그 사실을 값으로 못 박아 둔다.
        val g = ShutterGate(maxAttempts = 1)
        g.arm()
        assertEquals(ShutterGate.Decision.Wait, g.captureWatchdog())
    }

    @Test
    fun `🔴 실패 사유는 네이티브가 준 것을 그대로 나른다`() {
        val g = gate(maxAttempts = 1)
        g.arm()
        g.onCaptured()
        assertEquals(
            "no_plate 와 segment_fail 은 고칠 곳이 반대다 — 뭉치면 안 된다",
            ShutterGate.Decision.GiveUp("no_plate"),
            g.onRecognized("", formatKnown = false, failReason = "no_plate"),
        )
    }

    @Test
    fun `모르는 네이티브 사유는 조용히 통과시키지 않는다`() {
        assertEquals("no_plate", CameraShot.reasonFromNative("no_plate"))
        assertEquals(CameraShot.REASON_RECOGNIZE_FAILED, CameraShot.reasonFromNative("스스로_지어낸_사유"))
        assertEquals(CameraShot.REASON_RECOGNIZE_FAILED, CameraShot.reasonFromNative(""))
    }

    @Test
    fun `🔴 기본 시도 수는 1 이다 - 재시도는 서버가 정한다`() {
        // 폰은 "차가 아직 있나" 를 모른다. 모르는 채 다시 찍으면 뒤차를 앞 요청의 답으로 올린다.
        assertEquals(
            "재시도를 폰으로 되돌리려면 먼저 '폰이 차의 유무를 아는가' 에 답해라 — " +
                "모르는 채 다시 찍으면 뒤차를 앞 요청의 답으로 올린다",
            1,
            ShutterGate.DEFAULT_MAX_ATTEMPTS,
        )
        val g = ShutterGate()
        g.arm()
        g.onCaptured()
        assertTrue(
            "기본값에서는 재시도(Fire)가 아니라 포기여야 한다",
            g.onRecognized("", formatKnown = false) is ShutterGate.Decision.GiveUp,
        )
    }

    // ---- 문 닫기 -----------------------------------------------------------

    @Test
    fun `disarm 하면 진행 중이던 것도 멈춘다`() {
        // 링크가 끊기면 답할 곳이 없다. 안 닫으면 다음 요청에서 헛셔터가 나간다.
        val g = gate()
        g.arm()
        g.disarm()
        assertFalse(g.busy)
    }

    @Test
    fun `disarm 뒤 인식 결과가 늦게 와도 답하지 않는다`() {
        // 셔터 콜백은 비동기다 — 문을 닫은 뒤에 결과가 도착할 수 있다.
        val g = gate()
        g.arm()
        g.onCaptured()
        g.disarm()
        assertEquals(ShutterGate.Decision.Wait, g.onRecognized("12가3456", formatKnown = true))
    }

    @Test
    fun `다시 arm 하면 시도 횟수가 초기화된다`() {
        val g = gate(maxAttempts = 2)
        g.arm()
        g.onCaptured()
        g.onRecognized("", formatKnown = false)   // attempt 1
        g.disarm()
        assertEquals("새 요청은 0 부터 센다", ShutterGate.Decision.Fire(0), g.arm())
    }
}
