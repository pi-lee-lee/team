package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** 대기 관리. 시각을 인자로 넣으므로 실제 시간을 기다리지 않는다. */
class ShotCoordinatorTest {

    private fun coord(maxPending: Int = 64) = ShotCoordinator(maxPending)

    @Test
    fun `요청 뒤에 온 번호판으로 성공 응답을 만든다`() {
        val c = coord()
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.request(202608210001L, 1_000L))
        val replies = c.onPlate("12가3456", 1_200L)
        assertEquals(
            listOf(ShotCoordinator.Reply.Success(202608210001L, "12가3456")),
            replies,
        )
        // 답한 요청은 대기에서 빠진다 — 두 번 답하지 않는다.
        assertEquals(0, c.pendingCount)
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.onPlate("12가3456", 1_300L))
    }

    @Test
    fun `대기가 없으면 번호판이 와도 아무것도 안 만든다`() {
        // push 경로는 이것과 무관하게 계속 돈다. 여기서 답을 만들면 아무도 요청하지 않은
        // shot 응답이 나간다.
        val c = coord()
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.onPlate("12가3456", 1_000L))
    }

    // 🔴 **시한 시험 둘을 지웠다** (2026-08-27 · 완주 설계로 `sweepTimeouts` 가 사라졌다).
    //
    // 지운 것이 아쉬우면 §"시험 코드를 남기지 마라" 를 보라 — 죽은 갈래를 지키는 시험은
    // **그 갈래가 아직 있다고 읽히게** 만든다. 대기가 안 닫히는 걱정은 이제 다른 셋이 덮는다:
    // 인식 완료(`onPlate`/`failAllPending`) · `ShutterGate.captureWatchdog` · `dropAll`.

    @Test
    fun `같은 값이 여러 요청에 동시에 붙는다`() {
        val c = coord()
        c.request(1L, 0L)
        c.request(2L, 10L)
        val replies = c.onPlate("12가3456", 20L)
        assertEquals(2, replies.size)
        assertEquals(setOf(1L, 2L), replies.map { it.shotId }.toSet())
        assertTrue(replies.all { it is ShotCoordinator.Reply.Success })
    }

    @Test
    fun `빈 값은 답을 만들지 않는다`() {
        val c = coord()
        c.request(1L, 0L)
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.onPlate("", 10L))
        assertEquals(1, c.pendingCount)   // 계속 기다린다
    }

    @Test
    fun `표지 문자열은 성공으로 올리지 않고 계속 기다린다`() {
        val c = coord()
        c.request(1L, 0L)
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.onPlate(CameraShot.PLATE_MARKER, 10L))
        assertEquals(1, c.pendingCount)
        assertEquals(1, c.markerValueIgnored)
        // 그 뒤 정상값이 오면 그것으로 답한다.
        assertEquals(1, c.onPlate("12가3456", 20L).size)
    }

    @Test
    fun `대기 상한을 넘으면 오래된 것부터 too_many_pending 으로 답한다`() {
        val c = coord(maxPending = 2)
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.request(1L, 0L))
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.request(2L, 1L))

        val evicted = c.request(3L, 2L)
        assertEquals(
            listOf(ShotCoordinator.Reply.Failure(1L, CameraShot.REASON_TOO_MANY_PENDING)),
            evicted,
        )
        assertEquals(2, c.pendingCount)
        // 밀려난 1 은 이제 답할 대상이 아니다. 남은 둘만 값을 받는다.
        assertEquals(setOf(2L, 3L), c.onPlate("12가3456", 3L).map { it.shotId }.toSet())
    }

    @Test
    fun `🔴 실패 사유는 서로 다른 값이라야 원인이 갈린다`() {
        // 같은 사유로 접으면 "왜 못 했나" 의 원인이 서버에서 안 갈린다.
        // 🔑 그리고 **사유 수만큼 값이 달라야 한다** — 하나라도 겹치면 그 둘은 화면에서 같은 것이 된다.
        assertEquals(CameraShot.APP_REASONS.size, CameraShot.APP_REASONS.toSet().size)
        assertTrue(CameraShot.REASON_CAPTURE_FAILED != CameraShot.REASON_CAPTURE_STUCK)
    }

    @Test
    fun `링크가 끊기면 답하지 않고 버린다`() {
        val c = coord()
        c.request(1L, 0L)
        c.request(2L, 0L)
        assertEquals(listOf(1L, 2L), c.dropAll())
        assertEquals(0, c.pendingCount)
        // 버린 뒤에 값이 와도 답이 안 나간다 — 새 세션에 낡은 답을 흘리지 않는다.
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.onPlate("12가3456", 10L))
        assertEquals(emptyList<Long>(), c.dropAll())
    }

    @Test
    fun `대기 번호를 조회할 수 있다 - 촬영본 파일 이름에 쓴다`() {
        val c = coord()
        assertEquals(emptyList<Long>(), c.pendingIds())
        c.request(202608210001L, 0L)
        c.request(202608210002L, 1L)
        // 오래된 것부터. 답을 만들지 않는다 — 조회 전용이다.
        assertEquals(listOf(202608210001L, 202608210002L), c.pendingIds())
        assertEquals(2, c.pendingCount)
    }

    @Test
    fun `촬영이 최종 실패하면 대기 전부에 답한다`() {
        // 같은 셔터로 답하는 요청들이므로 결과도 같다.
        val c = coord()
        c.request(1L, 0L)
        c.request(2L, 0L)
        val replies = c.failAllPending(CameraShot.REASON_RECOGNIZE_FAILED)
        assertEquals(2, replies.size)
        assertTrue(replies.all { it is ShotCoordinator.Reply.Failure })
        assertEquals(setOf(1L, 2L), replies.map { it.shotId }.toSet())
        assertEquals(0, c.pendingCount)
        // 두 번 답하지 않는다
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.failAllPending("x"))
    }

    @Test
    fun `failAllPending 은 답하고 dropAll 은 버린다 - 다른 것이다`() {
        val a = coord()
        a.request(1L, 0L)
        assertEquals(1, a.failAllPending(CameraShot.REASON_CAPTURE_FAILED).size)

        val b = coord()
        b.request(1L, 0L)
        // dropAll 은 Reply 를 만들지 않는다 — 링크가 끊겨 보낼 곳이 없다
        assertEquals(listOf(1L), b.dropAll())
    }

    @Test
    fun `하나만 골라 실패로 닫을 수 있다`() {
        val c = coord()
        c.request(1L, 0L)
        c.request(2L, 0L)
        assertEquals(
            listOf(ShotCoordinator.Reply.Failure(1L, CameraShot.REASON_CAMERA_UNAVAILABLE)),
            c.failOne(1L, CameraShot.REASON_CAMERA_UNAVAILABLE),
        )
        assertEquals(1, c.pendingCount)
        // 이미 없는 번호는 두 번 답하지 않는다.
        assertEquals(emptyList<ShotCoordinator.Reply>(), c.failOne(1L, "x"))
    }
}
