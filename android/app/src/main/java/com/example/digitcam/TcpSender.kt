package com.example.digitcam

import android.os.Build
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.IOException
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicInteger

/**
 * 계약 §7 의 전송부. **확정 명세이므로 여기서 형식을 바꾸지 않는다.**
 *
 * TCP, 클라이언트가 접속, 줄 단위(LF 종단) UTF-8, fire-and-forget.
 *
 * 두 가지를 일부러 **안 한다**:
 *  - 전송 실패분 큐잉·재전송. 계약이 금지한다 — 최신값이 중요한 용도라서 재접속 순간에
 *    낡은 값이 쏟아지면 오히려 해롭다. 끊긴 동안의 값은 버린다.
 *  - 전송 시점 판단. `fresh` 상승 엣지를 만드는 것은 네이티브 쪽이다(계약 §5).
 *    여기서는 그 신호를 받아 `min_interval_ms` 하한만 지킨다.
 *
 * 카메라/분석 스레드에서 부르지 않는다 — [offer] 는 논블로킹이고, 실제 소켓 I/O 는
 * [Dispatchers.IO] 의 코루틴 하나가 전담한다. 프리뷰가 소켓 때문에 끊기면 안 된다.
 */
class TcpSender(
    private val scope: CoroutineScope,
    private val onState: (State) -> Unit,
) {

    sealed interface State {
        /** host 가 비어 있음 — 계약 §6 의 "서버 미설정". */
        data object NotConfigured : State
        data class Connecting(val host: String, val port: Int) : State
        data class Connected(val host: String, val port: Int) : State
        data class Disconnected(val attempt: Int, val reason: String) : State
    }

    /**
     * 용량 1 + DROP_OLDEST. 소켓이 느리면 최신값 하나만 남고 그 사이 값은 조용히 버려진다.
     *
     * 주의: 이 자료구조가 보장하는 것은 **깊이 1** 이지 **신선도**가 아니다. 연결이 끊긴 동안에도
     * 한 건은 버퍼에 남으므로, 재접속 직후 그 한 건(몇 분 전 값일 수 있다)이 나가지 않도록
     * [runConnectionLoop] 이 연결 성공 직후 비워 준다.
     */
    private val queue = Channel<Payload>(capacity = 1, onBufferOverflow = kotlinx.coroutines.channels.BufferOverflow.DROP_OLDEST)

    private val seq = AtomicInteger(0)
    private var job: Job? = null

    @Volatile
    private var config: AppConfig? = null

    /** 마지막 전송 시각(elapsed ms). min_interval_ms 하한 판정에만 쓴다. */
    @Volatile
    private var lastSentAt = 0L

    private data class Payload(
        val value: String,
        val conf: Double,
        /** 계약 §7 의 `format` — new/old. 결과 JSON 의 format 을 그대로 실어 보낸다. */
        val plateFormat: String,
        val seq: Int,
        val ts: Long,
    )

    /**
     * 설정 적용 + 워커 재기동. "설정 다시 읽기" 가 이 경로를 쓴다.
     * host/port 가 바뀌면 기존 연결을 끊고 새로 붙는다.
     */
    fun applyConfig(newConfig: AppConfig) {
        config = newConfig
        job?.cancel()
        if (!newConfig.isServerConfigured) {
            onState(State.NotConfigured)
            job = null
            return
        }
        job = scope.launch(Dispatchers.IO) { runConnectionLoop(newConfig) }
    }

    /**
     * 인식 결과를 전송 후보로 올린다. 분석 스레드에서 불러도 되는 논블로킹 경로다.
     * 전송할지 말지의 1차 판단(fresh/stable)은 [MainActivity] 가 계약 §7 대로 하고,
     * 하한 간격은 여기서 지킨다.
     */
    fun offer(result: DigitResult) {
        val cfg = config ?: return
        if (!cfg.isServerConfigured) return
        if (result.value.isEmpty()) return

        val shouldSend = when (cfg.sendMode) {
            AppConfig.SendMode.ON_FRESH -> result.fresh
            AppConfig.SendMode.EVERY_STABLE_FRAME -> result.stable
        }
        if (!shouldSend) return

        // 하한 간격은 "지연 전송" 이 아니라 "버림" 이다. 미뤄 두면 그게 곧 큐가 된다.
        val now = android.os.SystemClock.elapsedRealtime()
        if (now - lastSentAt < cfg.minIntervalMs) return
        lastSentAt = now

        queue.trySend(
            Payload(
                value = result.value,
                conf = result.conf,
                plateFormat = result.plateFormat,
                seq = seq.incrementAndGet(),
                ts = System.currentTimeMillis(),
            ),
        )
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private suspend fun runConnectionLoop(cfg: AppConfig) {
        var attempt = 0
        while (scope.isActive) {
            attempt++
            onState(State.Connecting(cfg.host, cfg.port))
            var socket: Socket? = null
            try {
                socket = Socket().apply {
                    keepAlive = cfg.keepAlive
                    connect(InetSocketAddress(cfg.host, cfg.port), cfg.connectTimeoutMs)
                }
                attempt = 0
                // 끊긴 동안 버퍼에 남아 있던 값을 버린다(계약 §7).
                // capacity=1 이라 "쏟아지지는" 않지만 한 건은 남고, 그 한 건이 몇 분 전 값일 수 있다.
                // 계약이 금지하는 것은 큐의 크기가 아니라 **재접속 순간에 낡은 값이 나가는 것**이다.
                while (queue.tryReceive().isSuccess) { /* 버린다 */ }
                onState(State.Connected(cfg.host, cfg.port))
                pump(socket.getOutputStream(), cfg)
            } catch (e: IOException) {
                Log.i(TAG, "연결 실패/끊김: ${e.message}")
                onState(State.Disconnected(attempt, e.message ?: e.javaClass.simpleName))
            } finally {
                try {
                    socket?.close()
                } catch (_: IOException) {
                }
            }
            if (!scope.isActive) break
            // 재접속 대기. 이 동안에도 카메라·인식은 계속 돈다(계약 §7).
            delay(cfg.reconnectIntervalMs)
        }
    }

    /** 연결이 끊길 때까지 큐를 비워 내보낸다. 서버 응답은 읽지 않는다(fire-and-forget). */
    private suspend fun pump(out: OutputStream, cfg: AppConfig) {
        for (payload in queue) {
            val line = encode(payload, cfg.format)
            withContext(Dispatchers.IO) {
                out.write(line.toByteArray(Charsets.UTF_8))
                out.flush()
            }
        }
    }

    /**
     * 계약 §7 의 줄 포맷. **`value` 에 한글이 들어간다**(`123가4568`) — 여기서 만든 문자열은
     * [pump] 에서 `Charsets.UTF_8` 로 인코딩되고, 그 사이 어디에서도 ASCII 로 좁히지 않는다.
     * org.json 은 비 ASCII 를 이스케이프하지 않고 원문으로 내보내는데, 계약이 원문 UTF-8 과
     * `\uXXXX` 를 모두 허용하므로 그대로 둔다.
     */
    private fun encode(p: Payload, format: AppConfig.Format): String = when (format) {
        AppConfig.Format.PLAIN -> "${p.value}\n"
        AppConfig.Format.JSON -> JSONObject()
            .put("ts", p.ts)
            .put("value", p.value)
            .put("conf", p.conf)
            .put("format", p.plateFormat)
            .put("seq", p.seq)
            .put("device", Build.MODEL)
            .toString() + "\n"
    }

    private companion object {
        const val TAG = "TcpSender"
    }
}
