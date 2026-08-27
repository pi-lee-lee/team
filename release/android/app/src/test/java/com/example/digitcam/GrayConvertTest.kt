package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 휘도 변환과 축소 크기. **인식 입력을 만드는 산술이라 틀리면 인식이 통째로 망가진다** —
 * 그리고 그 종류의 결함은 화면으로는 안 보인다(그림은 그럴듯하게 나온다).
 */
class GrayConvertTest {

    private fun argb(r: Int, g: Int, b: Int) = (0xFF shl 24) or (r shl 16) or (g shl 8) or b

    // ---- 휘도 -------------------------------------------------------------

    @Test
    fun `BT_601 계수로 계산한다`() {
        // 손으로 박은 기대값. 피검체 식을 다시 쓰지 않는다.
        //  흰색 : (255*299 + 255*587 + 255*114)/1000 = 255
        //  검정 : 0
        //  순적 : 255*299/1000 = 76
        //  순녹 : 255*587/1000 = 149
        //  순청 : 255*114/1000 = 29
        val px = intArrayOf(
            argb(255, 255, 255),
            argb(0, 0, 0),
            argb(255, 0, 0),
            argb(0, 255, 0),
            argb(0, 0, 255),
        )
        val g = GrayConvert.luminance(px)
        assertEquals(255, g[0].toInt() and 0xFF)
        assertEquals(0, g[1].toInt() and 0xFF)
        assertEquals(76, g[2].toInt() and 0xFF)
        assertEquals(149, g[3].toInt() and 0xFF)
        assertEquals(29, g[4].toInt() and 0xFF)
    }

    @Test
    fun `알파를 무시한다 - 알파 채널은 휘도가 아니다`() {
        // Bitmap.Config.ALPHA_8 로 뽑으면 흰 배경/검은 글자가 통째로 한 색이 된다.
        // 알파가 0 이어도 RGB 로 계산해야 한다.
        val opaque = GrayConvert.luminance(intArrayOf(argb(200, 200, 200)))
        val transparent = GrayConvert.luminance(intArrayOf((0 shl 24) or (200 shl 16) or (200 shl 8) or 200))
        assertEquals(opaque[0], transparent[0])
    }

    @Test
    fun `절삭이다 - 반올림으로 바꾸면 데스크톱 하네스 대조가 깨진다`() {
        // r=g=b=1 → (1*299 + 1*587 + 1*114)/1000 = 1000/1000 = 1
        assertEquals(1, GrayConvert.luminance(intArrayOf(argb(1, 1, 1)))[0].toInt() and 0xFF)
        // r=1,g=0,b=0 → 299/1000 = 0 (절삭). 반올림이면 0 이지만 아래가 갈린다
        assertEquals(0, GrayConvert.luminance(intArrayOf(argb(1, 0, 0)))[0].toInt() and 0xFF)
        // r=2,g=0,b=0 → 598/1000 = 0 (절삭) · 반올림이면 1 이다 → **여기서 갈린다**
        assertEquals(0, GrayConvert.luminance(intArrayOf(argb(2, 0, 0)))[0].toInt() and 0xFF)
    }

    @Test
    fun `버퍼를 재사용한다 - 프레임마다 할당하면 쓰레기가 된다`() {
        val px = intArrayOf(argb(10, 10, 10), argb(20, 20, 20))
        val buf = ByteArray(2)
        val out = GrayConvert.luminance(px, buf)
        assertTrue("같은 배열을 돌려줘야 한다", out === buf)
        // 크기가 안 맞으면 새로 만든다(잘못된 크기로 쓰면 IndexOutOfBounds 다)
        val wrong = ByteArray(1)
        assertFalse(GrayConvert.luminance(px, wrong) === wrong)
    }

    // ---- 축소 크기 ---------------------------------------------------------

    @Test
    fun `상한 이하면 그대로 둔다`() {
        assertEquals(GrayConvert.Size(640, 480), GrayConvert.scaledSize(640, 480, 960))
        assertEquals(GrayConvert.Size(960, 540), GrayConvert.scaledSize(960, 540, 960))
    }

    @Test
    fun `긴 변을 상한으로 맞춘다`() {
        assertEquals(GrayConvert.Size(960, 540), GrayConvert.scaledSize(1920, 1080, 960))
        // 세로가 긴 경우
        assertEquals(GrayConvert.Size(540, 960), GrayConvert.scaledSize(1080, 1920, 960))
    }

    /**
     * 🔴 비율이 `Float` 이라는 것을 값으로 박는다.
     *
     * `1066x533` → `Float` 은 **959x479**, `Double` 은 **960x480**. 양쪽 변이 다 갈린다.
     * 샘플 경로가 원래 `Float` 이었으므로 여기서 갈리면 같은 사진이 두 경로에서 다른 크기가
     * 되고, 그것이 *"샘플로는 되는데 촬영본으로는 안 된다"* 가 된다.
     *
     * ⚠ 처음에 `1000x750` 으로 적었는데 **그 입력은 안 갈린다**(둘 다 720). 갈리는 입력을
     * 전수로 찾아서(960·640 상한에 3283건) 실제로 갈리는 것으로 바꿨다 —
     * **"갈린다" 를 주장하려면 갈리는 값을 써야 한다.**
     */
    @Test
    fun `비율은 Float 이다 - Double 로 바꾸면 크기가 1 어긋난다`() {
        assertEquals(GrayConvert.Size(959, 479), GrayConvert.scaledSize(1066, 533, 960))
        // 한 변만 갈리는 경우도 있다
        assertEquals(GrayConvert.Size(959, 719), GrayConvert.scaledSize(1066, 799, 960))
    }

    @Test
    fun `0 이 되지 않는다`() {
        // 아주 납작한 이미지를 크게 줄이면 짧은 변이 0 으로 내려간다 → 최소 1
        val s = GrayConvert.scaledSize(10_000, 3, 100)
        assertEquals(100, s.width)
        assertTrue("짧은 변이 0 이면 안 된다: ${s.height}", s.height >= 1)
    }

    @Test
    fun `크기가 0 이하면 거절한다`() {
        // 조용히 빈 버퍼를 만들면 인식이 "못 읽었다" 로 끝나고 원인이 안 남는다.
        for (bad in listOf(0 to 10, 10 to 0, -1 to 5)) {
            try {
                GrayConvert.scaledSize(bad.first, bad.second)
                throw AssertionError("거절하지 않았다: $bad")
            } catch (_: IllegalArgumentException) {
                // 기대한 갈래
            }
        }
    }

    // ---- 통계 -------------------------------------------------------------

    @Test
    fun `통계는 평균 최소 최대를 준다`() {
        val g = byteArrayOf(0, 100.toByte(), 200.toByte())
        val st = GrayConvert.stats(g)
        assertEquals(0, st.min)
        assertEquals(200, st.max)
        assertEquals(100, st.mean)
    }

    @Test
    fun `폭이 좁으면 평평하다고 본다 - 판정이 아니라 의심의 근거다`() {
        assertTrue(GrayConvert.stats(byteArrayOf(100, 105.toByte(), 110.toByte())).looksFlat)
        assertFalse(GrayConvert.stats(byteArrayOf(10, 200.toByte())).looksFlat)
    }

    @Test
    fun `빈 배열에서 죽지 않는다`() {
        assertEquals(GrayConvert.Stats(0, 0, 0), GrayConvert.stats(ByteArray(0)))
    }
}
