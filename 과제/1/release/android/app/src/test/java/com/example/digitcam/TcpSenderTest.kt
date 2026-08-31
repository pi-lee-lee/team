package com.example.digitcam

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * **`TcpSender` 의 배선**을 실제 소켓으로 검증한다 — 소켓 생성 · 연결 · 하행 배달 · 응답 전송 ·
 * EOF 감지 · 재접속.
 *
 * ## 왜 이 시험이 따로 있나
 *
 * `ShotLinkTest` 는 읽는 루프 자체를 본다(스트림 두 개를 직접 준다). **그 루프가 옳아도
 * `TcpSender` 가 그것을 안 부르면 아무 일도 일어나지 않는다.** 그 사이가 여태 코드 판독뿐이었다.
 *
 * ## 밟는 것과 못 밟는 것
 *
 * ```
 * ✅ 밟는다   : applyConfig → 소켓 연결 → soTimeout → 읽는 루프 → onShoot 콜백
 *              reply() → 같은 연결로 나가는 바이트 · EOF → Disconnected · 재접속
 * 🔴 못 밟는다 : push 전송 경로(offer). `SystemClock.elapsedRealtime()` 과 `Build.MODEL` 을 타고,
 *              단위 시험에서 그 둘은 **0 과 null 이 된다**(build.gradle.kts 의 isReturnDefaultValues).
 *              0 이면 min_interval 게이트가 항상 열려 실기와 다르게 동작한다 → **일부러 안 밟는다**
 * 🔴 못 밟는다 : MainActivity 배선(권한 판정 · publish → onPlate · 시한 청소)
 * ```
 *
 * ⚠ **`TcpSender` 가 나중에 다른 프레임워크 API 를 쓰게 되면 그 호출은 조용히 기본값이 된다.**
 * 이 시험은 그것을 잡아 주지 않는다.
 */
class TcpSenderTest {

    private lateinit var server: ServerSocket
    private var scope: CoroutineScope? = null
    private var sender: TcpSender? = null

    private val states = LinkedBlockingQueue<TcpSender.State>()
    private val shots = LinkedBlockingQueue<Long>()

    @Before
    fun setUp() {
        server = ServerSocket(0, 4, InetAddress.getLoopbackAddress())
        server.soTimeout = ACCEPT_TIMEOUT_MS
    }

    @After
    fun tearDown() {
        sender?.stop()
        scope?.cancel()
        runCatching { server.close() }
    }

    // ---- 도우미 -----------------------------------------------------------

    /** 실기와 같은 경로로 기동한다 — `applyConfig` 가 워커를 띄운다. */
    private fun start(reconnectMs: Long = 300L): TcpSender {
        val cfg = AppConfig.fallback().copy(
            host = InetAddress.getLoopbackAddress().hostAddress ?: "127.0.0.1",
            port = server.localPort,
            reconnectIntervalMs = reconnectMs,
            connectTimeoutMs = 2_000,
        )
        val sc = CoroutineScope(Dispatchers.IO + SupervisorJob())
        scope = sc
        val s = TcpSender(sc, { states.put(it) }, { shots.put(it) })
        sender = s
        s.applyConfig(cfg)
        return s
    }

    /** 접속을 받고 읽기용 리더를 만든다. */
    private fun accept(): Pair<Socket, BufferedReader> {
        val peer = server.accept()
        peer.soTimeout = 5_000
        return peer to BufferedReader(InputStreamReader(peer.getInputStream(), Charsets.UTF_8))
    }

    private fun Socket.send(text: String) {
        getOutputStream().write(text.toByteArray(Charsets.UTF_8))
        getOutputStream().flush()
    }

    /** 상태 큐에서 원하는 종류가 나올 때까지 읽는다(Connecting 이 앞에 끼므로). */
    private inline fun <reified T : TcpSender.State> awaitState(seconds: Long = 6): T? {
        val until = System.nanoTime() + seconds * 1_000_000_000L
        while (System.nanoTime() < until) {
            val s = states.poll(seconds, TimeUnit.SECONDS) ?: return null
            if (s is T) return s
        }
        return null
    }

    // ---- 시험 -------------------------------------------------------------

    /**
     * 🔴 배선의 핵심. **`applyConfig` 만 부르고 나머지는 `TcpSender` 가 한다** —
     * 소켓을 만들고, `soTimeout` 을 걸고, 읽는 루프를 돌리고, 하행을 파싱해 콜백까지 올린다.
     */
    @Test(timeout = 30_000)
    fun `applyConfig 만으로 연결해서 SHOOT 를 받고 같은 연결로 응답을 올린다`() {
        val s = start()
        val (peer, peerIn) = accept()

        assertNotNull("연결 상태가 안 왔다", awaitState<TcpSender.State.Connected>())

        peer.send("SHOOT,202608210001\n")
        assertEquals(
            "하행이 onShoot 콜백까지 오지 않았다 — 읽는 루프가 배선되지 않았다",
            202608210001L,
            shots.poll(8, TimeUnit.SECONDS),
        )

        s.reply(CameraShot.encodeSuccess(202608210001L, "12가3456", "TestPhone"))
        assertEquals(
            """{"shot":202608210001,"value":"12가3456","device":"TestPhone"}""",
            peerIn.readLine(),
        )
    }

    @Test(timeout = 30_000)
    fun `깨진 하행과 모르는 하행은 콜백으로 오지 않는다`() {
        val s = start()
        val (peer, _) = accept()
        assertNotNull(awaitState<TcpSender.State.Connected>())

        peer.send("PING\n")
        peer.send("SHOOT,abc\n")
        // 그 뒤에 정상 요청을 보낸다 — **순서가 판별자다.** 정상 요청이 오는 것을 확인해야
        // "콜백이 안 왔다" 가 "아무것도 안 온다" 와 구별된다.
        peer.send("SHOOT,202608210007\n")

        assertEquals(202608210007L, shots.poll(8, TimeUnit.SECONDS))
        // 앞의 둘이 콜백으로 왔다면 큐에 남아 있을 것이다.
        assertNull("깨진/모르는 하행이 콜백으로 왔다", shots.poll(300, TimeUnit.MILLISECONDS))
        s.stop()
    }

    /**
     * EOF 는 예외가 아니다. 이 갈래가 없으면 화면이 계속 "연결됨" 으로 남는다 —
     * 하행이 생기기 전에는 이 경로가 아예 없었다(쓰기가 실패할 때만 알았다).
     */
    @Test(timeout = 30_000)
    fun `상대가 닫으면 Disconnected 가 오고 다시 붙는다`() {
        start()
        val (peer, _) = accept()
        assertNotNull(awaitState<TcpSender.State.Connected>())

        peer.close()
        val d = awaitState<TcpSender.State.Disconnected>()
        assertNotNull("EOF 뒤 Disconnected 가 안 왔다", d)

        // 재접속. reconnectIntervalMs = 300ms 로 뒀다.
        val (peer2, _) = accept()
        assertNotNull("재접속하지 않았다", awaitState<TcpSender.State.Connected>())
        assertTrue(peer2.isConnected)
    }

    /**
     * 끊긴 동안 만들어진 응답은 **새 세션에 흘리지 않는다**(`ShotCoordinator.dropAll` 과 같은 규율).
     */
    @Test(timeout = 30_000)
    fun `앞 세션에서 못 나간 응답은 재접속 때 버려진다`() {
        val s = start()
        val (peer, _) = accept()
        assertNotNull(awaitState<TcpSender.State.Connected>())

        peer.close()
        assertNotNull(awaitState<TcpSender.State.Disconnected>())

        // 연결이 없는 사이에 답이 만들어졌다고 가정한다.
        s.reply(CameraShot.encodeError(202608210009L, CameraShot.REASON_CAPTURE_STUCK))

        val (peer2, peer2In) = accept()
        assertNotNull(awaitState<TcpSender.State.Connected>())

        // 새 세션으로 낡은 답이 나오면 안 된다. 그 뒤 새 요청의 답은 정상적으로 나가야 한다 —
        // 🔑 이 확인이 없으면 "버려졌다" 가 "전송이 아예 죽었다" 와 구별되지 않는다.
        peer2.send("SHOOT,202608210010\n")
        assertEquals(202608210010L, shots.poll(8, TimeUnit.SECONDS))
        s.reply(CameraShot.encodeSuccess(202608210010L, "34나5678", "TestPhone"))

        val line = peer2In.readLine()
        assertEquals(
            "낡은 응답이 새 세션으로 나갔다",
            """{"shot":202608210010,"value":"34나5678","device":"TestPhone"}""",
            line,
        )
    }

    /** `stop()` 뒤에는 새 연결을 만들지 않는다. */
    @Test(timeout = 30_000)
    fun `stop 뒤에는 다시 붙지 않는다`() {
        val s = start(reconnectMs = 200L)
        val (peer, _) = accept()
        assertNotNull(awaitState<TcpSender.State.Connected>())

        s.stop()
        peer.close()

        server.soTimeout = 2_000
        val again = runCatching { server.accept() }.getOrNull()
        assertNull("stop 뒤에도 재접속했다", again)
    }

    private companion object {
        const val ACCEPT_TIMEOUT_MS = 15_000
    }
}
