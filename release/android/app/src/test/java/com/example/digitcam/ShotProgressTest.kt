package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 진행 단계 표시(REQ-0500). **시각을 인자로 넣으므로 실제 시간을 안 기다린다.**
 *
 * 🔑 여기서 닫히는 것은 *"화면에 무엇이 뜨나"* 이지 *"뜨는 것을 사람이 보나"* 가 아니다.
 * 뒤엣것은 실기에서만 닫힌다 — 그 사실을 REQ 결과에 적는다.
 */
class ShotProgressTest {

    /** `progressText` 의 `maxWidth`. 레이아웃과 같이 움직여야 하는 수다. */
    private val MAX_WIDTH_DP = 380.0

    // ---- 순서 -------------------------------------------------------------

    @Test
    fun `요청 하나가 다섯 단계를 순서대로 지난다`() {
        val p = ShotProgress()
        p.requested(202608270001L, 0L)
        assertEquals(ShotProgress.Stage.REQUESTED, p.snapshot().last().stage)

        p.advance(ShotProgress.Stage.CAPTURING, 100L)
        assertEquals(ShotProgress.Stage.CAPTURING, p.snapshot().last().stage)

        p.advance(ShotProgress.Stage.RECOGNIZING, 500L)
        assertEquals(ShotProgress.Stage.RECOGNIZING, p.snapshot().last().stage)

        p.advance(ShotProgress.Stage.FOUND, 12_400L, detail = "12가3456")
        p.advance(ShotProgress.Stage.SENT, 12_500L)
        assertEquals(ShotProgress.Stage.SENT, p.snapshot().last().stage)
    }

    // ---- 🔴 경과 초 — 이게 없으면 이 화면은 아무것도 관측하지 않는다 --------

    @Test
    fun `🔴 도는 줄은 부를 때마다 경과가 올라간다`() {
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.RECOGNIZING, 100L)

        val at3 = ShotProgress.render(p.snapshot(), 3_000L)
        val at9 = ShotProgress.render(p.snapshot(), 9_000L)

        assertTrue("3초 시점에 3.0초가 보여야 한다: $at3", at3.contains("2.9초") || at3.contains("3.0초"))
        assertTrue("9초 시점에 9.0초가 보여야 한다: $at9", at9.contains("9.0초"))
        assertTrue(
            "같은 상태를 다른 시각에 그렸는데 글자가 같다 — 화면이 얼어 있다는 뜻이다",
            at3 != at9,
        )
    }

    @Test
    fun `🔴 끝난 줄의 소요 시간은 시간이 흘러도 안 변한다`() {
        // 끝난 이력의 **소요**가 계속 올라가면 끝난 것이 도는 것처럼 보인다.
        //
        // ⚠ 이 시험은 처음에 "줄 전체가 글자까지 같아야 한다" 로 적혀 있었다. 그러면
        //    "언제 끝났나"(방금/3분 전)를 넣는 순간 **멀쩡한 화면이 빨강**이 된다 —
        //    ★ 막으려는 실패가 아니라 **그때의 모양**을 못 박은 것이었다(web 이 오늘 짚은 함정).
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.SENT, 5_000L)

        val a = ShotProgress.render(p.snapshot(), 6_000L)
        val b = ShotProgress.render(p.snapshot(), 3_600_000L)
        assertTrue("끝난 시각까지의 소요여야 한다: $a", a.contains("5.0초"))
        assertTrue("한 시간 뒤에도 소요는 그대로여야 한다: $b", b.contains("5.0초"))
    }

    @Test
    fun `🔴 끝난 줄은 언제 끝났는지를 말한다`() {
        // (12.4초) 는 **얼마나 걸렸나**다. **언제 끝났나**가 없으면 30분 뒤에 화면을 봤을 때
        // 그 줄이 방금 것인지 30분 전 것인지 모른다.
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.SENT, 5_000L)

        assertTrue(ShotProgress.render(p.snapshot(), 6_000L).contains("방금"))
        assertTrue(ShotProgress.render(p.snapshot(), 600_000L).contains("9분 전"))
        assertTrue(ShotProgress.render(p.snapshot(), 7_205_000L).contains("2시간 전"))
    }

    @Test
    fun `도는 것이 없으면 화면을 켜 두지 않는다`() {
        val p = ShotProgress()
        assertFalse(p.isRunning())
        p.requested(1L, 0L)
        assertTrue(p.isRunning())
        p.advance(ShotProgress.Stage.SENT, 10L)
        assertFalse("답까지 나갔으면 도는 것이 아니다", p.isRunning())
    }

    // ---- 🔴 무엇을 잰 숫자인가 (REQ-0509) ----------------------------------

    @Test
    fun `🔴 인식 중에는 단계 경과와 누적 경과를 갈라 보인다`() {
        // 누적 하나로 셋을 말하면 안 된다:
        //   요청 뒤 N초 = 서버가 기다린 시간 · 인식 N초째 = 인식이 걸린 시간(판정 값)
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.RECOGNIZING, 4_000L)   // 요청 4초 뒤에 인식 시작

        val text = ShotProgress.render(p.snapshot(), 12_100L)
        assertTrue("인식 경과가 보여야 한다(8.1초): $text", text.contains("인식 8.1초째"))
        assertTrue("누적도 같이 보여야 한다(12.1초): $text", text.contains("총 12.1초"))
    }

    @Test
    fun `🔴 인식이 너무 길면 경고한다 - 다만 끊지는 않는다`() {
        // 시한 폐기는 옳았다. 다만 그 시한이 "멈춤을 감지하는 수단" 을 겸하고 있었다.
        // 🔑 끊는 대신 **말한다** — 화면이 건강하다고 거짓말하는 것만 막는다.
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.RECOGNIZING, 0L)

        val ok = ShotProgress.render(p.snapshot(), ShotProgress.STALL_WARN_MS - 1)
        assertFalse("문턱 전에는 조용해야 한다 — 자주 울리는 경보는 꺼진다: $ok", ok.contains("⚠"))

        val warn = ShotProgress.render(p.snapshot(), ShotProgress.STALL_WARN_MS)
        assertTrue("문턱을 넘으면 경고해야 한다: $warn", warn.contains("너무 깁니다"))

        assertTrue("경고해도 **끊지 않는다** — 여전히 도는 중이다", p.isRunning())
    }

    @Test
    fun `🔴 밀려난 건수를 말한다 - 잘린 것을 말하지 않으면 자른 적이 없어 보인다`() {
        val p = ShotProgress(keep = 2)
        for (i in 1..5) {
            p.requested(i.toLong(), i * 10L)
            p.advance(ShotProgress.Stage.SENT, i * 10L + 1)
        }
        assertEquals(3, p.droppedCount())
        val text = ShotProgress.render(p.snapshot(), 100L, p.droppedCount())
        assertTrue("밀린 건수가 보여야 한다: $text", text.contains("이전 3건은 화면에서 밀렸습니다"))
    }

    @Test
    fun `밀린 것이 없으면 그 줄을 붙이지 않는다`() {
        val p = ShotProgress()
        p.requested(1L, 0L)
        assertFalse(ShotProgress.render(p.snapshot(), 0L, p.droppedCount()).contains("밀렸습니다"))
    }

    @Test
    fun `🔴 인식까지 못 간 것을 "인식 실패" 로 말하지 않는다`() {
        // abandon 은 링크가 끊겨 그만둔 것이다 — 인식까지 **가지도 않았다**.
        // "결과 없음" 이라고만 쓰면 읽는 사람이 인식 실패로 읽는다.
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.RECOGNIZING, 10L)
        p.abandon(3_200L, "연결 끊김")

        val text = ShotProgress.render(p.snapshot(), 3_300L)
        assertTrue("중단이라는 사실이 드러나야 한다: $text", text.contains("결과 전에 중단됨"))
        assertFalse("인식 실패로 읽히면 안 된다: $text", text.contains("실패 —"))
    }

    // ---- 이력 -------------------------------------------------------------

    @Test
    fun `🔴 재요청이 앞 결과를 덮지 않는다`() {
        // 하나만 남기면 다음 시도가 앞 실패를 덮어 **왜 실패했는지가 사라진다.**
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.FAILED, 100L, detail = "no_plate")
        p.advance(ShotProgress.Stage.SENT, 110L)

        p.requested(2L, 200L)
        p.advance(ShotProgress.Stage.FOUND, 300L, detail = "12가3456")

        val text = ShotProgress.render(p.snapshot(), 400L)
        assertTrue("앞 실패가 남아 있어야 한다: $text", text.contains("no_plate"))
        assertTrue("뒤 성공도 보여야 한다: $text", text.contains("12가3456"))
    }

    @Test
    fun `🔴 회귀 - 전송 단계가 실패 사유를 덮지 않는다`() {
        // ★ 처음 구현이 정확히 이걸 어겼다. `stage` 하나만 들고 있어서 SENT 가 되는 순간
        //   화면에 "전송했습니다" 만 남고 **왜 실패했는지가 사라졌다.**
        //   🔑 이 REQ 가 막으려던 실패가 **구현 안에서 다시 난 것**이다 —
        //      이력을 여러 줄 남기는 것만으로는 부족했다.
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.FAILED, 100L, detail = "segment_fail")
        p.advance(ShotProgress.Stage.SENT, 110L)

        val text = ShotProgress.render(p.snapshot(), 200L)
        assertTrue("사유가 남아 있어야 한다: $text", text.contains("segment_fail"))
        assertTrue("전송 사실도 보여야 한다: $text", text.contains("전송함"))
    }

    @Test
    fun `🔴 회귀 - 전송 실패에서도 결과가 남는다`() {
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.FOUND, 100L, detail = "12가3456")
        p.advance(ShotProgress.Stage.SEND_FAILED, 110L, detail = "연결 없음")

        val text = ShotProgress.render(p.snapshot(), 200L)
        assertTrue("읽은 번호가 남아야 한다 — 못 보낸 것과 못 읽은 것은 다르다: $text", text.contains("12가3456"))
        assertTrue("$text", text.contains("전송 못 함"))
    }

    @Test
    fun `링크가 끊기면 도는 줄이 닫힌다 - 경과가 영원히 올라가면 안 된다`() {
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.RECOGNIZING, 10L)
        assertTrue(p.isRunning())

        p.abandon(5_000L, "연결 끊김")
        assertFalse("닫혀야 한다", p.isRunning())
        // 🔑 못 박는 것은 **소요가 멈췄다**이지 "줄이 글자까지 같다" 가 아니다.
        //    "언제 끝났나" 는 시간이 가면 당연히 바뀐다.
        assertTrue(ShotProgress.render(p.snapshot(), 9_000L).contains("5.0초"))
        assertTrue(ShotProgress.render(p.snapshot(), 99_000L).contains("5.0초"))
    }

    @Test
    fun `이력은 상한을 넘지 않는다`() {
        val p = ShotProgress(keep = 2)
        for (i in 1..5) {
            p.requested(i.toLong(), i * 10L)
            p.advance(ShotProgress.Stage.SENT, i * 10L + 1)
        }
        val snap = p.snapshot()
        assertEquals(2, snap.size)
        assertEquals("가장 최근 둘이 남아야 한다", listOf(4L, 5L), snap.map { it.shotId })
    }

    @Test
    fun `도는 줄이 없으면 단계를 밀어도 줄이 안 생긴다`() {
        // 없는 줄을 여기서 만들면 "요청이 왔다" 와 "사람이 눌렀다" 가 화면에서 섞인다.
        val p = ShotProgress()
        p.advance(ShotProgress.Stage.RECOGNIZING, 0L)
        assertEquals(emptyList<ShotProgress.Entry>(), p.snapshot())
    }

    // ---- 🔴 사유 어휘 -------------------------------------------------------

    @Test
    fun `🔴 아는 사유는 한국어와 원문 코드를 같이 보여 준다`() {
        // 코드가 없으면 사용자가 화면에서 본 것을 judgements.log 에서 찾을 수 없다.
        assertEquals("판 못 찾음 (no_plate)", ShotProgress.reasonText("no_plate"))
        assertEquals("움직임 (moving)", ShotProgress.reasonText("moving"))
    }

    @Test
    fun `🔴 모르는 사유는 뭉개지 않고 그대로 보여 준다`() {
        // ★ 이 규율이 검사 역할을 한다 — 낯선 낱말이 화면에 뜨면 누군가 어휘를 늘렸는데
        //   여기가 안 따라온 것이다. "실패" 로 뭉개면 그 사실이 영영 안 보인다.
        assertEquals("모르는 사유 (too_dark)", ShotProgress.reasonText("too_dark"))
        assertEquals("모르는 사유 ()", ShotProgress.reasonText(""))
    }

    @Test
    fun `🔴 앱이 보내는 사유는 전부 화면 문구를 갖고 있다`() {
        // 🔑 이것이 web 의 표와 같은 계약이다 — 앱 화면도 사유를 뭉개면 안 된다.
        //    APP_REASONS 에 사유를 늘렸는데 여기를 안 늘리면 이 시험이 잡는다.
        val missing = CameraShot.APP_REASONS.filter {
            ShotProgress.reasonText(it).startsWith("모르는 사유")
        }
        assertEquals(
            "이 사유들이 폰 화면에서 '알 수 없는 사유' 로 뜬다. ShotProgress.KNOWN_REASONS 에 넣어라",
            emptyList<String>(),
            missing,
        )
    }

    // ---- 🔴 화면 폭 예산 (REQ-0509 후속) ------------------------------------

    /**
     * 한 줄이 화면에서 차지하는 폭(dp). **레이아웃 값에서 계산한다.**
     *
     * `progressText` 는 `monospace`·`textSize=10sp`·`maxWidth=380dp` 다.
     * 등폭 글꼴에서 라틴은 약 `0.6em`, **한글은 전각이라 약 `1.0em`** 이다.
     */
    private fun widthDp(line: String): Double =
        line.sumOf { if (it.code > 0x2000) 10.0 else 6.0 }

    @Test
    fun `🔴 어떤 줄도 화면 폭을 넘지 않는다 - 넘치면 접히고 접힌 줄은 다음 항목처럼 보인다`() {
        // ★ 이 시험이 없어서 실제로 넘쳤다 — 최악이 752dp(폭의 2배)였다.
        //   web 이 "안 잰 축" 으로 짚어 줘서 알았다. 값 검사는 **재는 곳에서만** 참이다.
        val p = ShotProgress()
        p.requested(202608280001L, 0L)
        p.advance(ShotProgress.Stage.CAPTURING, 100L, attempt = 2)
        p.advance(ShotProgress.Stage.RECOGNIZING, 200L, attempt = 2)

        // 가장 긴 조합: 두 자리 시도 · 경고 · 가장 긴 사유 · 전송 실패 문구
        val worst = mutableListOf<String>()
        worst += ShotProgress.render(p.snapshot(), ShotProgress.STALL_WARN_MS + 200L).lines()

        p.advance(ShotProgress.Stage.FAILED, 99_000L, detail = "segment_fail")
        p.advance(ShotProgress.Stage.SEND_FAILED, 99_100L, detail = "연결 없음")
        worst += ShotProgress.render(p.snapshot(), 99_200L, dropped = 12).lines()

        val over = worst.filter { widthDp(it) > MAX_WIDTH_DP }
        assertEquals(
            "이 줄들이 화면 폭 ${MAX_WIDTH_DP}dp 를 넘는다 → 접힌다. 문구를 줄이거나 줄을 나눠라:\n" +
                over.joinToString("\n") { "  ${widthDp(it).toInt()}dp  $it" },
            emptyList<String>(),
            over,
        )
    }

    @Test
    fun `한 건은 두 줄이다 - 접힌 것이 아니라 나눈 것이다`() {
        // 🔑 접히면 어디까지가 한 건인지 안 보인다. 나누면 보인다 —
        //    둘째 줄은 들여쓰기로 시작하므로 눈으로 이어진다.
        val p = ShotProgress()
        p.requested(1L, 0L)
        val lines = ShotProgress.render(p.snapshot(), 0L).lines()
        assertEquals(2, lines.size)
        assertTrue("첫 줄은 표지로 시작한다: ${lines[0]}", lines[0].startsWith("▸ "))
        assertTrue("둘째 줄은 들여쓴다: '${lines[1]}'", lines[1].startsWith("    "))
    }

    // ---- 화면 문구 ---------------------------------------------------------

    @Test
    fun `요청번호와 시도 횟수가 보인다`() {
        val p = ShotProgress()
        p.requested(202608270007L, 0L)
        p.advance(ShotProgress.Stage.CAPTURING, 10L, attempt = 2)
        val text = ShotProgress.render(p.snapshot(), 100L)
        assertTrue("요청번호가 있어야 재요청을 구별한다: $text", text.contains("202608270007"))
        assertTrue("몇 번째 시도인지 보여야 한다: $text", text.contains("2번째"))
    }

    @Test
    fun `촬영 버튼은 서버 요청과 갈라 보인다`() {
        val p = ShotProgress()
        p.requested(0L, 0L)
        assertTrue(ShotProgress.render(p.snapshot(), 0L).contains("촬영 버튼"))
    }

    @Test
    fun `🔴 전송은 "서버가 받았다" 라고 말하지 않는다`() {
        // 우리가 아는 것은 큐에 넣었다까지다. half-open 이면 연결돼 보여도 안 갔을 수 있다.
        val p = ShotProgress()
        p.requested(1L, 0L)
        p.advance(ShotProgress.Stage.SENT, 10L)
        val text = ShotProgress.render(p.snapshot(), 10L)
        assertTrue("$text", text.contains("전송함"))
        assertFalse("도달을 단정하면 안 된다: $text", text.contains("받았"))
    }

    /**
     * 🔵 **배포 문서에 붙일 그림을 여기서 뽑는다.**
     *
     * ⚠ 손으로 옮겨 적으면 **코드가 바뀔 때 문서만 낡는다** — 오늘 그것으로 두 번 걸렸다
     * (배포 문서 §2 의 거짓 초록 · 명세 표). 실제 출력을 찍어서 붙이면 그 경로가 없다.
     */
    @Test
    fun `문서에 붙일 실제 출력을 찍는다`() {
        val p = ShotProgress()
        p.requested(202608280001L, 0L)
        println("=== 진행 ===")
        println(ShotProgress.render(p.snapshot(), 200L))
        p.advance(ShotProgress.Stage.CAPTURING, 400L)
        println(ShotProgress.render(p.snapshot(), 400L))
        p.advance(ShotProgress.Stage.RECOGNIZING, 500L)
        println(ShotProgress.render(p.snapshot(), 3_600L))
        println(ShotProgress.render(p.snapshot(), 12_200L))
        p.advance(ShotProgress.Stage.FOUND, 12_400L, detail = "12가3456")
        p.advance(ShotProgress.Stage.SENT, 12_400L)
        println(ShotProgress.render(p.snapshot(), 12_500L))

        println("=== 실패 · 경고 ===")
        val q = ShotProgress()
        q.requested(202608280002L, 0L)
        q.advance(ShotProgress.Stage.RECOGNIZING, 100L)
        println(ShotProgress.render(q.snapshot(), 62_000L))
        q.advance(ShotProgress.Stage.FAILED, 31_000L, detail = "segment_fail")
        q.advance(ShotProgress.Stage.SENT, 31_100L)
        println(ShotProgress.render(q.snapshot(), 211_000L, dropped = 3))
    }
}
