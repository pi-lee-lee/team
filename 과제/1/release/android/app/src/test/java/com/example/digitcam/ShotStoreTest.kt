package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/**
 * 촬영본 보관. `java.io.File` 만 쓰므로 실제 파일로 시험한다 —
 * **회전이 틀리면 저장공간이 차거나 증거가 사라지고, 둘 다 조용하다.**
 */
class ShotStoreTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private fun store(keep: Int) = ShotStore(File(tmp.root, "shots"), keep)

    private fun bytes(n: Int) = ByteArray(n) { it.toByte() }

    @Test
    fun `이름에 요청번호가 들어간다 - 서버 대장과 눈으로 대조된다`() {
        val s = store(10)
        assertEquals("shot-202608210001.jpg", s.fileFor(202608210001L).name)
        assertEquals("shot-202608210001-r1.jpg", s.fileFor(202608210001L, 1).name)
        // attempt 0 과 음수는 접미가 없다
        assertEquals("shot-5.jpg", s.fileFor(5L, 0).name)
        assertEquals("shot-5.jpg", s.fileFor(5L, -1).name)
    }

    @Test
    fun `저장하면 디렉터리를 만들고 바이트를 쓴다`() {
        val s = store(10)
        val f = s.save(202608210001L, 0, bytes(64))
        assertNotNull(f)
        assertTrue(f!!.isFile)
        assertEquals(64, f.length().toInt())
    }

    @Test
    fun `최근 N장만 남기고 오래된 것부터 지운다`() {
        val s = store(3)
        for (i in 1..6) s.save(202608210000L + i, 0, bytes(8))

        val names = s.list().map { it.name }
        assertEquals(3, names.size)
        // 요청번호가 단조 증가라 이름 정렬이 곧 시간 정렬이다 → 큰 번호 셋이 남는다
        assertEquals(
            listOf("shot-202608210004.jpg", "shot-202608210005.jpg", "shot-202608210006.jpg"),
            names,
        )
    }

    @Test
    fun `재시도 파일도 한 장으로 센다`() {
        val s = store(2)
        s.save(202608210001L, 0, bytes(8))
        s.save(202608210001L, 1, bytes(8))
        s.save(202608210002L, 0, bytes(8))
        // 셋을 넣었고 상한이 2 → 가장 오래된 하나가 밀린다
        assertEquals(2, s.list().size)
        assertFalse("가장 오래된 것(첫 시도)이 밀려야 한다", s.fileFor(202608210001L, 0).exists())
        assertTrue("나중에 찍은 재시도본은 남아야 한다", s.fileFor(202608210001L, 1).exists())
    }

    /**
     * 🔴 이름 정렬이 시간 정렬이 아니라는 것을 박는다.
     *
     * `'-'(0x2D) < '.'(0x2E)` 이라 `shot-…001-r1.jpg` 가 `shot-…001.jpg` 보다 **앞에** 온다.
     * 이름으로 정렬해 지우면 **나중에 찍은 재시도본이 먼저 지워진다** — 재시도본은 원본이
     * 실패해서 다시 찍은 것이라 인식에 성공한 쪽일 확률이 높다. 증거를 거꾸로 버리는 것이다.
     */
    @Test
    fun `재시도본이 원본보다 먼저 지워지지 않는다`() {
        val s = store(1)
        s.save(202608210001L, 0, bytes(8))
        s.save(202608210001L, 1, bytes(8))

        val left = s.list()
        assertEquals(1, left.size)
        assertEquals("shot-202608210001-r1.jpg", left[0].name)
    }

    @Test
    fun `정렬은 요청번호와 시도 순이다`() {
        val s = store(100)
        // 일부러 뒤섞어 넣는다
        s.save(202608210002L, 0, bytes(4))
        s.save(202608210001L, 1, bytes(4))
        s.save(202608210001L, 0, bytes(4))
        s.save(202608210002L, 2, bytes(4))
        assertEquals(
            listOf(
                "shot-202608210001.jpg",
                "shot-202608210001-r1.jpg",
                "shot-202608210002.jpg",
                "shot-202608210002-r2.jpg",
            ),
            s.list().map { it.name },
        )
    }

    @Test
    fun `🔴 우리 이름 규칙이 아닌 파일은 건드리지 않는다`() {
        // 사람이 그 디렉터리에 무엇을 넣었을 수 있다. 남의 파일을 지우지 않는다.
        val dir = File(tmp.root, "shots")
        dir.mkdirs()
        val foreign = File(dir, "메모.txt").apply { writeText("건드리지 마라") }
        val alsoForeign = File(dir, "shot-abc.jpg").apply { writeText("번호가 아니다") }

        val s = store(1)
        for (i in 1..4) s.save(202608210000L + i, 0, bytes(8))

        assertTrue("남의 파일이 지워졌다", foreign.exists())
        assertTrue("이름 규칙이 아닌 파일이 지워졌다", alsoForeign.exists())
        // 우리 것은 상한대로 하나만 남는다
        assertEquals(1, s.list().size)
    }

    @Test
    fun `prune 은 지운 수를 돌려준다 - 조용히 지우지 않는다`() {
        val s = store(2)
        for (i in 1..5) s.save(202608210000L + i, 0, bytes(8))
        // save 가 이미 회전했으므로 더 지울 것이 없다
        assertEquals(0, s.prune())

        val s2 = store(1)
        // 같은 디렉터리를 더 좁은 상한으로 보면 이번엔 지운다
        assertEquals(1, s2.prune())
    }

    @Test
    fun `디렉터리가 없어도 list 가 죽지 않는다`() {
        assertEquals(emptyList<File>(), store(3).list())
        assertEquals(0, store(3).prune())
    }

    @Test
    fun `쓸 수 없으면 null 을 돌려준다 - 예외를 위로 던지지 않는다`() {
        // 디렉터리 자리에 **파일**을 놓아 mkdirs 를 실패시킨다.
        val blocked = File(tmp.root, "blocked").apply { writeText("x") }
        val s = ShotStore(blocked, 3)
        assertNull("저장 실패는 capture_failed 갈래이고 앱을 죽일 일이 아니다", s.save(1L, 0, bytes(4)))
    }
    // ---- 🔴 판정 로그 — 측정 절차(§15)가 이것에 걸려 있다 --------------------

    @Test
    fun `판정 로그에 헤더가 먼저 붙는다 - 파일을 처음 여는 사람이 무엇인지 알아야 한다`() {
        val s = store(10)
        assertTrue(s.appendJudgement("shot-001.jpg  reason=ok"))
        val text = s.logFile()!!.readText()
        assertTrue("헤더가 없다", text.startsWith("#"))
        // 🔑 low_conf 경고가 헤더에 있어야 한다 — 초록만 세면 섞인다(§13)
        assertTrue("low_conf 경고가 없다", text.contains("low_conf"))
        assertTrue(text.contains("shot-001.jpg  reason=ok"))
    }

    @Test
    fun `여러 줄이 순서대로 쌓인다`() {
        val s = store(10)
        for (n in 1..3) s.appendJudgement("shot-00$n.jpg  reason=r$n")
        val body = s.logFile()!!.readLines().filterNot { it.startsWith("#") }
        assertEquals(listOf("shot-001.jpg  reason=r1", "shot-002.jpg  reason=r2", "shot-003.jpg  reason=r3"), body)
    }

    @Test
    fun `빈 줄은 거절한다 - 형식이 깨지면 세는 도구가 조용히 틀린다`() {
        val s = store(10)
        assertFalse(s.appendJudgement(""))
        assertFalse(s.appendJudgement("   "))
        assertNull("로그 파일이 생기면 안 된다", s.logFile())
    }

    @Test
    fun `개행이 섞여도 한 줄로 눌러 쓴다`() {
        val s = store(10)
        s.appendJudgement("shot-001.jpg\nreason=ok")
        val body = s.logFile()!!.readLines().filterNot { it.startsWith("#") }
        assertEquals("한 줄이어야 한다", 1, body.size)
        assertTrue(body[0].contains("shot-001.jpg reason=ok"))
    }

    /**
     * 🔴 **이것이 이 기능의 핵심 불변식이다.**
     *
     * `prune()` 은 `^shot-\d+(-r\d+)?\.jpg$` 만 지운다. 판정 로그가 그 패턴에 걸리면
     * **사진을 정리할 때 증거가 같이 사라진다** — 그리고 조용하다.
     */
    @Test
    fun `prune 이 판정 로그를 지우지 않는다`() {
        val s = store(2)
        for (n in 1..5) s.save(n.toLong(), 0, bytes(4))
        s.appendJudgement("shot-000005.jpg  reason=ok")
        // ⚠ `save()` 가 **이미** `prune()` 을 부른다(기존 시험 `prune 은 지운 수를 돌려준다` 가
        //    그 사실을 적어 뒀다). 그래서 반환값이 아니라 **상태** 로 확인한다.
        assertEquals("사진은 keep 만 남아야 한다", 2, s.list().size)
        s.prune()
        assertNotNull("🔴 판정 로그가 사라졌다", s.logFile())
        assertTrue(s.logFile()!!.readText().contains("reason=ok"))
        // list() 에도 안 섞여야 한다 — 섞이면 정리 순서 계산이 틀어진다
        assertFalse("list() 에 로그가 섞였다", s.list().any { it.name == ShotStore.LOG_NAME })
    }

    @Test
    fun `줄 수 상한을 넘으면 앞을 버리고 헤더는 남는다`() {
        val s = store(10)
        val over = ShotStore.LOG_MAX_LINES + 50
        for (n in 1..over) s.appendJudgement("line-$n")
        val lines = s.logFile()!!.readLines()
        assertTrue("상한을 넘었다: ${lines.size}", lines.size <= ShotStore.LOG_MAX_LINES)
        assertTrue("헤더가 사라졌다", lines.first().startsWith("#"))
        // 🔑 **앞** 을 버려야 한다 — 최신이 남아야 대조할 사진이 있다
        assertTrue("최신 줄이 없다", lines.last().contains("line-$over"))
        assertFalse("가장 오래된 줄이 남아 있다", lines.any { it == "line-1" })
    }

}
