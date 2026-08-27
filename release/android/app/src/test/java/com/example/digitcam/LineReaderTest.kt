package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 줄 조립. 핵심은 **읽기 경계에서 아무것도 잃지 않는 것**이다 —
 * 그것이 `BufferedReader.readLine()` 대신 이것을 만든 이유다.
 */
class LineReaderTest {

    private fun LineReader.feedText(s: String): List<String> {
        val b = s.toByteArray(Charsets.UTF_8)
        return feed(b, b.size)
    }

    @Test
    fun `한 번에 들어온 여러 줄을 순서대로 돌려준다`() {
        val r = LineReader()
        assertEquals(listOf("a", "b", "c"), r.feedText("a\nb\nc\n"))
    }

    @Test
    fun `LF 가 아직 안 왔으면 줄을 내놓지 않는다`() {
        val r = LineReader()
        assertEquals(emptyList<String>(), r.feedText("SHOOT,2026"))
        assertTrue(r.pendingBytes > 0)
    }

    @Test
    fun `읽기가 줄 중간에서 끊겨도 조립된다 - 이 클래스의 존재 이유다`() {
        val r = LineReader()
        assertEquals(emptyList<String>(), r.feedText("SHO"))
        assertEquals(emptyList<String>(), r.feedText("OT,20260"))
        assertEquals(emptyList<String>(), r.feedText("8210001"))
        assertEquals(listOf("SHOOT,202608210001"), r.feedText("\n"))
        assertEquals(0, r.pendingBytes)
    }

    @Test
    fun `UTF-8 멀티바이트가 읽기 경계에서 쪼개져도 안 깨진다`() {
        // "가" = EA B0 80. 한 바이트씩 나눠 넣는다 — 문자로 먼저 디코딩하면 여기서 깨진다.
        val bytes = "12가3456".toByteArray(Charsets.UTF_8)
        val r = LineReader()
        for (b in bytes) {
            assertEquals(emptyList<String>(), r.feed(byteArrayOf(b), 1))
        }
        assertEquals(listOf("12가3456"), r.feedText("\n"))
    }

    @Test
    fun `CRLF 종단도 받는다`() {
        val r = LineReader()
        assertEquals(listOf("SHOOT,1"), r.feedText("SHOOT,1\r\n"))
    }

    @Test
    fun `빈 줄도 돌려준다 - 버리면 무엇이었는지 알 수 없다`() {
        val r = LineReader()
        assertEquals(listOf("a", "", "b"), r.feedText("a\n\nb\n"))
    }

    @Test
    fun `한 줄이 상한을 넘으면 그 줄만 버리고 다음 줄은 살린다`() {
        val r = LineReader(maxLineBytes = 8)
        // 상한을 넘긴 줄 + 정상 줄. 앞엣것은 사라지고 뒤엣것은 온전해야 한다.
        val lines = r.feedText("0123456789abcdef\nok\n")
        assertEquals(listOf("ok"), lines)
        // 🔑 조용히 버리지 않는다 — 부르는 쪽이 로그로 남길 수 있게 센다.
        assertEquals(1, r.droppedLines)
    }

    @Test
    fun `상한 초과가 메모리를 키우지 않는다`() {
        val r = LineReader(maxLineBytes = 8)
        repeat(100) { r.feedText("0123456789") }
        assertTrue("pending=${r.pendingBytes}", r.pendingBytes <= 8)
    }

    @Test
    fun `길이 0 을 넣으면 아무 일도 없다`() {
        val r = LineReader()
        assertEquals(emptyList<String>(), r.feed(ByteArray(4), 0))
        assertEquals(0, r.pendingBytes)
    }
}
