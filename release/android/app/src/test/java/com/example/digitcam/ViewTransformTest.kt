package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 좌표 변환. **REQ-0331 의 "수정 전 값" 이다.**
 *
 * 좌표 코드는 **조용히 깨진다** — 오류도 경고도 없고 화면만 이상하다. 그래서 알려진 크기에서
 * 기대값을 손으로 계산해 박는다. 기기가 없어도 이 시험이 변화를 잡는다.
 *
 * ⚠ **여기 적힌 값은 "옳은 값" 이 아니라 "지금 값" 이다.** 원인이 확정되고 고칠 때
 * 이 시험이 빨강이 되면 그것은 회귀가 아니라 **기대값이 낡았다는 신호**다.
 *
 * ## 🔑 수정 A(2026-08-21) 이후에도 **이 값들은 그대로다**
 *
 * A 는 `Preview` 와 `ImageAnalysis` 를 같은 4:3 으로 못박은 것이고 **이 순수 함수를 안 바꿨다.**
 * ```
 * 잘림   : 뷰와 소스의 종횡비가 다르면 생긴다 → 🔑 **A 뒤에도 남는다**(아래 값 그대로)
 * 어긋남 : 프리뷰와 오버레이가 **다른 분율** 을 자를 때 → 🔴 **A 가 이것을 0 으로 만든다**
 * ```
 * → 그래서 아래 잘림 값들은 **`(수정 전·후 동일)`** 이고, A 의 효과는
 * `수정 A` 로 시작하는 시험들이 본다.
 */
class ViewTransformTest {

    private fun assertClose(expected: Float, actual: Float, tag: String) {
        assertTrue("$tag: 기대 $expected · 실제 $actual", kotlin.math.abs(expected - actual) < 0.01f)
    }

    // ---- 종횡비가 같으면 잘림이 없다 ---------------------------------------

    @Test
    fun `종횡비가 같으면 잘리지 않는다`() {
        // 뷰 1600x1200 · 프레임 640x480 — 둘 다 4:3
        // sx = 2.5 · sy = 2.5 → scale 2.5 · dx 0 · dy 0
        val f = ViewTransform.fill(1600, 1200, 640, 480)
        assertClose(2.5f, f.scale, "scale")
        assertClose(0f, f.dx, "dx")
        assertClose(0f, f.dy, "dy")
        assertClose(0f, f.cropX, "cropX")
        assertClose(0f, f.cropY, "cropY")
    }

    // ---- 🔴 가로: 수직으로 잘린다 ------------------------------------------

    @Test
    fun `가로 16 대 9 뷰에 4 대 3 프레임 — 위아래가 잘린다`() {
        // 손 계산: sx = 1920/640 = 3.0 · sy = 1080/480 = 2.25 → FILL 은 큰 쪽 = 3.0
        //          dy = (1080 - 480*3.0)/2 = (1080-1440)/2 = -180
        val f = ViewTransform.fill(1920, 1080, 640, 480)
        assertClose(3.0f, f.scale, "scale")
        assertClose(0f, f.dx, "dx")
        assertClose(-180f, f.dy, "dy")
        assertClose(180f, f.cropY, "위아래 잘림")
        assertClose(0f, f.cropX, "좌우 잘림")
    }

    @Test
    fun `가로 20 대 9 뷰 — 잘림이 더 커진다`() {
        // sx = 2400/640 = 3.75 · sy = 2.25 → scale 3.75
        // dy = (1080 - 480*3.75)/2 = (1080-1800)/2 = -360
        val f = ViewTransform.fill(2400, 1080, 640, 480)
        assertClose(3.75f, f.scale, "scale")
        assertClose(-360f, f.dy, "dy")
        assertClose(360f, f.cropY, "위아래 잘림")
    }

    // ---- 🔴 세로: 수평으로 잘린다 (대칭이 아니다) --------------------------

    @Test
    fun `세로 뷰에 회전 후 3 대 4 프레임 — 좌우가 잘린다`() {
        // 세로에서는 인식기가 회전 후 480x640 을 준다(cpp 가 rotate_gray 뒤 w/h 를 채운다).
        // sx = 1080/480 = 2.25 · sy = 2400/640 = 3.75 → scale 3.75
        // dx = (1080 - 480*3.75)/2 = -360
        val f = ViewTransform.fill(1080, 2400, 480, 640)
        assertClose(3.75f, f.scale, "scale")
        assertClose(-360f, f.dx, "dx")
        assertClose(0f, f.dy, "dy")
        assertClose(360f, f.cropX, "좌우 잘림")
        assertClose(0f, f.cropY, "위아래 잘림")
    }

    /**
     * 🔴 **이것이 REQ-0331 의 핵심 관찰이다.**
     *
     * 잘리는 **축이 방향마다 다르다.** 사용자는 가로에서 알아챘지만 **세로에서도 잘린다** —
     * *"가로만 틀리다"* 를 전제로 고치면 세로가 틀린 채 남는다.
     */
    @Test
    fun `잘리는 축이 방향마다 다르다 — 가로는 수직 세로는 수평`() {
        val land = ViewTransform.fill(2400, 1080, 640, 480)   // 가로
        val port = ViewTransform.fill(1080, 2400, 480, 640)   // 세로(회전 후)

        assertTrue("가로는 위아래가 잘려야 한다", land.cropY > 0f && land.cropX == 0f)
        assertTrue("세로는 좌우가 잘려야 한다", port.cropX > 0f && port.cropY == 0f)
        // 🔑 같은 화면·같은 센서인데 잘리는 축이 바뀐다. 한쪽만 보고 고치면 다른 쪽이 남는다.
        assertClose(land.cropY, port.cropX, "잘림 양은 같다")
    }

    // ---- FIT_CENTER (샘플 모드) --------------------------------------------

    @Test
    fun `FIT_CENTER 는 잘리지 않고 여백이 생긴다`() {
        // scale = min(3.0, 2.25) = 2.25 · dx = (1920 - 640*2.25)/2 = (1920-1440)/2 = 240
        val f = ViewTransform.fit(1920, 1080, 640, 480)
        assertClose(2.25f, f.scale, "scale")
        assertClose(240f, f.dx, "dx(여백)")
        assertClose(0f, f.dy, "dy")
        assertClose(0f, f.cropX, "잘림 없음")
        assertClose(0f, f.cropY, "잘림 없음")
    }

    @Test
    fun `FILL 과 FIT 은 종횡비가 같을 때만 일치한다`() {
        val same4x3 = 1600 to 1200
        assertEquals(
            ViewTransform.fill(same4x3.first, same4x3.second, 640, 480),
            ViewTransform.fit(same4x3.first, same4x3.second, 640, 480),
        )
        // 다르면 갈린다 — 그 차이가 곧 "프리뷰와 오버레이가 다른 스케일 타입일 때" 의 어긋남이다
        assertTrue(
            ViewTransform.fill(1920, 1080, 640, 480) != ViewTransform.fit(1920, 1080, 640, 480),
        )
    }

    // ---- 좌표 매핑 ---------------------------------------------------------

    @Test
    fun `프레임 중앙은 뷰 중앙으로 간다`() {
        val f = ViewTransform.fill(1920, 1080, 640, 480)
        assertClose(960f, ViewTransform.mapX(320, f), "중앙 x")
        assertClose(540f, ViewTransform.mapY(240, f), "중앙 y")
    }

    @Test
    fun `잘리는 쪽에서는 프레임 가장자리가 뷰 밖으로 나간다`() {
        // 가로 16:9 · 4:3 프레임 → 위아래가 잘리므로 y=0 은 음수로 간다
        val f = ViewTransform.fill(1920, 1080, 640, 480)
        assertClose(-180f, ViewTransform.mapY(0, f), "위쪽 가장자리")
        assertClose(1260f, ViewTransform.mapY(480, f), "아래쪽 가장자리")
        // 🔑 그래서 프레임 위아래 끝에 있는 번호판은 화면에 안 보인다 — 검출은 됐는데도
    }

    // ---- 🔴 수정 A 의 검정 — 잘림과 어긋남은 다른 것이다 --------------------

    /**
     * 🔴 **수정 전 상태를 값으로 고정한다** — 프리뷰 16:9 · 분석 4:3.
     *
     * 같은 뷰인데 **자르는 분율이 다르다.** 그 차이가 곧 박스 어긋남이다.
     */
    @Test
    fun `수정 A 전 — 프리뷰 16 대 9 와 분석 4 대 3 은 자르는 분율이 다르다`() {
        // 뷰 2400x1080 (20:9)
        val overlay = ViewTransform.cropFraction(2400, 1080, 640, 480)    // 분석 4:3
        val preview = ViewTransform.cropFraction(2400, 1080, 1920, 1080)  // 프리뷰 16:9

        // 손 계산: 오버레이 scale 3.75 → 높이 1800 → 잘림 360 → 분율 360/1800 = 0.200
        //          프리뷰   scale 1.25 → 높이 1350 → 잘림 135 → 분율 135/1350 = 0.100
        assertClose(0.200f, overlay.second, "오버레이 위아래 분율")
        assertClose(0.100f, preview.second, "프리뷰 위아래 분율")
        assertTrue(
            "🔴 분율이 다르면 어긋난다",
            kotlin.math.abs(overlay.second - preview.second) > 0.01f,
        )
    }

    /**
     * ✅ **수정 A 이후** — 프리뷰도 4:3 이면 **해상도가 달라도** 자르는 분율이 같다.
     *
     * 🔑 그래서 어긋남이 **원리적으로 0** 이다. 그것이 A 의 보장이고,
     * "원인을 고치는" 것이 아니라 **"원인이 생길 수 없게"** 하는 수정인 이유다.
     */
    @Test
    fun `수정 A 이후 — 종횡비가 같으면 해상도가 달라도 분율이 같다`() {
        val overlay = ViewTransform.cropFraction(2400, 1080, 640, 480)    // 분석 4:3
        val preview = ViewTransform.cropFraction(2400, 1080, 1440, 1080)  // 프리뷰 4:3 (다른 해상도)

        assertClose(overlay.first, preview.first, "좌우 분율")
        assertClose(overlay.second, preview.second, "위아래 분율")
        // 🔑 잘림 자체는 **남아 있다** — 뷰가 20:9 이고 소스가 4:3 이므로.
        //    A 가 없애는 것은 잘림이 아니라 **둘의 차이** 다.
        assertTrue("잘림은 여전히 있다", overlay.second > 0.1f)
    }

    @Test
    fun `4 대 3 뷰라면 애초에 잘리지 않는다`() {
        // 참고값 — 뷰까지 4:3 이면 잘림 0. 지금 폰 화면은 그렇지 않다
        val f = ViewTransform.cropFraction(1440, 1080, 640, 480)
        assertClose(0f, f.first, "좌우")
        assertClose(0f, f.second, "위아래")
    }

    // ---- 방어 -------------------------------------------------------------

    @Test
    fun `프레임 크기가 0 이하면 거절한다`() {
        for (bad in listOf(0 to 480, 640 to 0, -1 to 480)) {
            try {
                ViewTransform.fill(1920, 1080, bad.first, bad.second)
                throw AssertionError("거절하지 않았다: $bad")
            } catch (_: IllegalArgumentException) {
                // 기대한 갈래 — 조용히 0으로 나누면 NaN 좌표가 되고 그건 화면에서 안 보인다
            }
        }
    }

    @Test
    fun `종횡비 계산`() {
        assertClose(1.3333f, ViewTransform.aspectRatio(640, 480), "4:3")
        assertClose(1.7778f, ViewTransform.aspectRatio(1920, 1080), "16:9")
        assertClose(0.75f, ViewTransform.aspectRatio(480, 640), "3:4")
    }
}
