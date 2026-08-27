package com.example.digitcam

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.BlockingQueue
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlin.concurrent.thread

/**
 * **읽는 루프를 실제로 돌린다.** 루프백 TCP 두 끝을 만들어 한쪽(peer)이 서버처럼 하행을
 * 내려보내고, 다른 쪽에서 [ShotLink.readLoop] 을 돌린다.
 *
 * ## 이 시험이 밟는 것과 못 밟는 것
 *
 * ```
 * ✅ 밟는다   : 실기와 **같은** readLoop/writeLine 코드 · 진짜 소켓 · 진짜 soTimeout
 *              하행 도착 → 파싱 → 같은 연결로 응답이 나가는 왕복 전체
 * 🔴 못 밟는다 : TcpSender 가 이 클래스를 올바르게 배선하는가(소켓 생성·재접속·채널 소비)
 *              그것은 코드 판독으로만 확인된다 — 이 시험이 통과해도 그 배선은 검증되지 않는다
 * ```
 */
class ShotLinkTest {

    private lateinit var server: ServerSocket

    /** 앱 쪽 끝. [ShotLink] 가 이 스트림을 쓴다. */
    private lateinit var app: Socket

    /** 서버 흉내를 내는 끝. 여기서 `SHOOT,` 를 내려보내고 응답을 읽는다. */
    private lateinit var peer: Socket

    private lateinit var peerIn: BufferedReader

    @Before
    fun setUp() {
        server = ServerSocket(0, 1, InetAddress.getLoopbackAddress())
        val accepted = AtomicReference<Socket>()
        val t = thread(name = "accept") { accepted.set(server.accept()) }
        app = Socket().apply {
            connect(InetSocketAddress(server.inetAddress, server.localPort), CONNECT_TIMEOUT_MS)
            // 🔴 실기와 같은 조건. 이것이 없으면 readLoop 이 취소를 못 본다.
            soTimeout = READ_TIMEOUT_MS
        }
        t.join(CONNECT_TIMEOUT_MS.toLong())
        peer = requireNotNull(accepted.get()) { "accept 가 안 됐다" }
        peer.soTimeout = 3_000   // 시험이 영원히 매달리지 않게
        peerIn = BufferedReader(InputStreamReader(peer.getInputStream(), Charsets.UTF_8))
    }

    @After
    fun tearDown() {
        runCatching { app.close() }
        runCatching { peer.close() }
        runCatching { server.close() }
    }

    // ---- 도우미 -----------------------------------------------------------

    private fun newLink() = ShotLink(app.getInputStream(), app.getOutputStream())

    /** 하행 줄을 모으는 읽는 루프를 별도 스레드에서 돌린다. */
    private fun startReadLoop(
        link: ShotLink,
        lines: BlockingQueue<String>,
        onLine: (String) -> Unit = {},
    ): Pair<Thread, AtomicReference<Boolean>> {
        val stoppedByUs = AtomicReference<Boolean>()
        val t = thread(name = "readLoop") {
            val r = link.readLoop(shouldContinue = { true }) { line ->
                lines.put(line)
                onLine(line)
            }
            stoppedByUs.set(r)
        }
        return t to stoppedByUs
    }

    private fun peerSend(text: String) {
        peer.getOutputStream().write(text.toByteArray(Charsets.UTF_8))
        peer.getOutputStream().flush()
    }

    // ---- 시험 -------------------------------------------------------------

    /**
     * 🔴 이 REQ 의 받아들임 조건 ①. **하행을 받아 그 다음 줄이 실행된 증거**다 —
     * 응답이 같은 연결로 되돌아오지 않으면 이 시험은 통과할 수 없다.
     */
    @Test(timeout = 15_000)
    fun `SHOOT 를 받아 파싱하고 같은 연결로 응답을 올린다`() {
        val link = newLink()
        val lines = ArrayBlockingQueue<String>(8)
        val (t, _) = startReadLoop(link, lines) { line ->
            val d = CameraShot.parseDownlink(line)
            if (d is CameraShot.Downlink.Shoot) {
                link.writeLine(CameraShot.encodeSuccess(d.shotId, "12가3456", "TestPhone"))
            }
        }

        peerSend("SHOOT,202608210001\n")

        val received = lines.poll(5, TimeUnit.SECONDS)
        assertEquals("SHOOT,202608210001", received)

        val reply = peerIn.readLine()
        assertNotNull("응답이 안 왔다 — 읽는 루프가 돌았어도 다음 줄이 실행되지 않았다", reply)
        assertEquals("""{"shot":202608210001,"value":"12가3456","device":"TestPhone"}""", reply)

        link.stop()
        t.join(3_000)
    }

    @Test(timeout = 15_000)
    fun `읽기가 여러 번 타임아웃해도 줄이 온전하다`() {
        // soTimeout(READ_TIMEOUT_MS)보다 긴 공백을 줄 중간에 끼운다.
        // BufferedReader.readLine() 이었다면 여기서 앞부분을 잃는다.
        val link = newLink()
        val lines = ArrayBlockingQueue<String>(8)
        val (t, _) = startReadLoop(link, lines)

        peerSend("SHOOT,20260")
        Thread.sleep((READ_TIMEOUT_MS * 3).toLong())
        peerSend("8210001\n")

        assertEquals("SHOOT,202608210001", lines.poll(5, TimeUnit.SECONDS))

        link.stop()
        t.join(3_000)
    }

    @Test(timeout = 15_000)
    fun `상대가 닫으면 EOF 로 끝난다`() {
        val link = newLink()
        val lines = ArrayBlockingQueue<String>(8)
        val (t, stoppedByUs) = startReadLoop(link, lines)

        peer.close()
        t.join(5_000)
        assertFalse(t.isAlive)
        // false = 우리가 멈춘 것이 아니라 상대가 닫았다. 이 구분이 없으면 화면이
        // 계속 "연결됨" 으로 남는다.
        assertEquals(false, stoppedByUs.get())
    }

    @Test(timeout = 15_000)
    fun `stop 을 부르면 돌고 있던 루프가 빠져나온다`() {
        val link = newLink()
        val lines = ArrayBlockingQueue<String>(8)
        val (t, stoppedByUs) = startReadLoop(link, lines)

        // 🔑 먼저 루프가 **실제로 돌고 있다**는 것을 확인한다. 이 단계가 없으면 이 시험은
        //    루프가 아예 없어도 통과한다(음성 대조에서 그것이 드러났다).
        peerSend("PING\n")
        assertEquals("PING", lines.poll(5, TimeUnit.SECONDS))

        link.stop()
        // soTimeout 주기 안에 반응해야 한다. 넉넉히 기다린다.
        t.join(5_000)
        assertFalse("stop 뒤에도 읽는 루프가 살아 있다", t.isAlive)
        assertEquals(true, stoppedByUs.get())
    }

    @Test(timeout = 15_000)
    fun `모르는 하행은 무시되고 루프가 계속 돈다`() {
        val link = newLink()
        val lines = ArrayBlockingQueue<String>(8)
        val (t, _) = startReadLoop(link, lines)

        peerSend("PING\n")
        peerSend("SHOOT,202608210002\n")

        assertTrue(CameraShot.parseDownlink(lines.poll(5, TimeUnit.SECONDS)!!) is CameraShot.Downlink.Unknown)
        val second = lines.poll(5, TimeUnit.SECONDS)
        assertEquals(
            CameraShot.Downlink.Shoot(202608210002L),
            CameraShot.parseDownlink(second!!),
        )

        link.stop()
        t.join(3_000)
    }

    @Test(timeout = 20_000)
    fun `두 스레드가 동시에 써도 줄이 섞이지 않는다`() {
        // 실기에서 push 쓰기와 응답 쓰기가 다른 코루틴이다. 직렬화가 없으면 바이트가 섞여
        // 두 줄이 다 깨진다 — 그 결함은 로그에 "이상한 JSON" 으로만 보인다.
        val link = newLink()
        val perThread = 50
        val start = CountDownLatch(1)
        val writers = (0 until 2).map { w ->
            thread(name = "writer$w") {
                start.await()
                repeat(perThread) { i ->
                    link.writeLine(CameraShot.encodeSuccess(w * 1000L + i, "12가3456", "dev$w"))
                }
            }
        }
        start.countDown()
        writers.forEach { it.join(10_000) }

        var count = 0
        val ids = HashSet<Long>()
        repeat(perThread * 2) {
            val line = peerIn.readLine() ?: return@repeat
            count++
            // 온전한 줄인가 — 섞였으면 이 모양이 깨진다.
            assertTrue("깨진 줄: $line", line.startsWith("""{"shot":""") && line.endsWith("}"))
            val id = line.removePrefix("""{"shot":""").substringBefore(',').toLongOrNull()
            assertNotNull("shot 번호를 못 읽었다: $line", id)
            ids.add(id!!)
        }
        // 🔑 분모를 따로 단언한다. 0줄을 읽고 통과하면 위 검사가 아무것도 안 본 것이다.
        assertEquals(perThread * 2, count)
        assertEquals(perThread * 2, ids.size)
    }

    private companion object {
        const val CONNECT_TIMEOUT_MS = 3_000

        /** 실기(`TcpSender.READ_TIMEOUT_MS`)와 같은 자리의 값. 여기서는 시험을 빠르게 하려고 짧다. */
        const val READ_TIMEOUT_MS = 100
    }
}
