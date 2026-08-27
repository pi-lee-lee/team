package com.example.digitcam

import android.os.Build
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicInteger
import kotlin.coroutines.coroutineContext

/**
 * 계약 §7 의 전송부 + 카메라 pull 의 **읽는 루프**(`docs/net/SPEC-camera-pull.md` §4).
 *
 * TCP, 클라이언트가 접속, 줄 단위(LF 종단) UTF-8.
 *
 * ## 방향이 둘이다
 *
 * ```
 * 올림(push)  {"value","conf",...}      인식 결과를 스스로 올린다 — 옛 거동 그대로
 * 올림(응답)  {"shot":…}                촬영 요청에 대한 답
 * 내림        SHOOT,<shot_id>           🔴 **읽지 않으면 조용히 사라진다. 오류도 안 난다**
 * ```
 *
 * 하행이 생기기 전에는 이 클래스가 소켓에서 **읽지 않았다.** 읽는 루프는 [ShotLink] 에 있고
 * 여기서는 소켓을 만들어 그것에 붙이고 재접속을 관리한다.
 *
 * push 에서 두 가지를 일부러 **안 한다**:
 *  - 전송 실패분 큐잉·재전송. 계약이 금지한다 — 최신값이 중요한 용도라서 재접속 순간에
 *    낡은 값이 쏟아지면 오히려 해롭다. 끊긴 동안의 값은 버린다.
 *  - 전송 시점 판단. `fresh` 상승 엣지를 만드는 것은 네이티브 쪽이다(계약 §5).
 *    여기서는 그 신호를 받아 `min_interval_ms` 하한만 지킨다.
 *
 * **촬영 응답에는 그 하한을 적용하지 않는다.** 요청에 대한 답을 간격 때문에 버리면
 * 그것이 곧 침묵이고, 침묵은 서버가 "아직 안 왔다" 와 구별할 수 없다.
 *
 * 카메라/분석 스레드에서 부르지 않는다 — [offer] 와 [reply] 는 논블로킹이고, 실제 소켓 I/O 는
 * [Dispatchers.IO] 의 코루틴들이 전담한다. 프리뷰가 소켓 때문에 끊기면 안 된다.
 */
class TcpSender(
    private val scope: CoroutineScope,
    private val onState: (State) -> Unit,
    /**
     * 하행 `SHOOT,<id>` 가 도착했다. **읽는 스레드에서 불린다** — 오래 걸리는 일을 하지 마라.
     * 기본값은 아무것도 안 하는 것이 아니라 없다: 안 넘기면 하행이 도착해도 아무 일이 없다.
     */
    private val onShoot: (Long) -> Unit,
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

    /**
     * 촬영 응답 큐. **push 큐와 갈라 둔다** — 한 채널에 섞으면 `DROP_OLDEST` 가 응답까지
     * 버리고, 버려진 응답은 서버 쪽에 영원한 `CAM_PENDING` 으로 남는다.
     *
     * 차면 [reply] 가 실패를 로그로 남긴다(조용히 버리지 않는다). 넘칠 상황은
     * [ShotCoordinator.DEFAULT_MAX_PENDING] 이 이미 막고 있어서 여분이다.
     */
    private val replyQueue = Channel<String>(capacity = ShotCoordinator.DEFAULT_MAX_PENDING)

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
            isConnected = false
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

    /**
     * 촬영 응답 한 줄을 올린다(`{"shot":…}`). [CameraShot.encodeSuccess] · [CameraShot.encodeError]
     * 가 만든 것을 그대로 넘긴다. 논블로킹이다.
     *
     * 연결이 없는 동안 부르면 큐에 남고, 다음 연결이 열릴 때 **버려진다** — 늦은 답을 새 세션에
     * 흘리지 않기로 했다(`ShotCoordinator.dropAll` 의 근거와 같다).
     */
    /**
     * 응답을 큐에 넣는다. **돌려주는 값은 "큐에 들어갔다" 이지 "서버가 받았다" 가 아니다.**
     *
     * 🔑 그 구분을 값으로 남기는 이유: 큐가 차서 버려지는 갈래가 **지금까지 로그에만** 있었다.
     * 화면에 *"보냈습니다"* 를 띄우려면 **적어도 버려진 것은 알아야** 한다.
     * ⚠ 진짜 도달 여부는 이 함수가 알 수 없다 — 그건 [isConnected] 와 같이 읽어야 한다.
     */
    fun reply(line: String): Boolean {
        if (replyQueue.trySend(line).isSuccess) return true
        Log.w(TAG, "응답 큐가 찼다 — 버림: ${line.trim()}")
        return false
    }

    /**
     * 지금 연결돼 있나. **여러 스레드가 읽는다**(vision 스레드가 화면 문구를 정할 때).
     *
     * ⚠ 이것이 `true` 라도 **방금 끊겼을 수 있다** — half-open 이면 더 그렇다.
     * 그래서 화면 문구는 *"보냈습니다"* 가 아니라 **"전송했습니다(연결됨)"** 처럼
     * **우리가 아는 것까지만** 말해야 한다.
     */
    @Volatile
    var isConnected: Boolean = false
        private set

    fun stop() {
        job?.cancel()
        job = null
    }

    private suspend fun runConnectionLoop(cfg: AppConfig) {
        var attempt = 0
        while (scope.isActive) {
            attempt++
            isConnected = false
            onState(State.Connecting(cfg.host, cfg.port))
            var socket: Socket? = null
            try {
                socket = Socket().apply {
                    keepAlive = cfg.keepAlive
                    connect(InetSocketAddress(cfg.host, cfg.port), cfg.connectTimeoutMs)
                    // 🔴 읽는 루프가 코루틴 취소를 볼 수 있게 하는 유일한 장치다.
                    // 없으면 블로킹 read 가 연결이 끊길 때까지 안 깨어난다(ShotLink 문서 참고).
                    soTimeout = READ_TIMEOUT_MS
                }
                attempt = 0
                // 끊긴 동안 버퍼에 남아 있던 값을 버린다(계약 §7).
                // capacity=1 이라 "쏟아지지는" 않지만 한 건은 남고, 그 한 건이 몇 분 전 값일 수 있다.
                // 계약이 금지하는 것은 큐의 크기가 아니라 **재접속 순간에 낡은 값이 나가는 것**이다.
                while (queue.tryReceive().isSuccess) { /* 버린다 */ }
                // 앞 세션에서 못 나간 촬영 응답도 같이 버린다 — 새 세션에 늦은 답을 흘리지 않는다.
                var stale = 0
                while (replyQueue.tryReceive().isSuccess) stale++
                if (stale > 0) Log.i(TAG, "앞 세션의 미전송 촬영 응답 ${stale}건 버림")

                isConnected = true
                onState(State.Connected(cfg.host, cfg.port))
                val peerClosed = runLink(socket, cfg)
                // EOF 는 예외가 아니다. 여기서 상태를 내지 않으면 화면이 계속 "연결됨" 으로 남는다.
                if (scope.isActive) {
                    isConnected = false
                    onState(State.Disconnected(attempt, if (peerClosed) "서버가 연결을 닫았다" else "중단됨"))
                }
            } catch (e: IOException) {
                Log.i(TAG, "연결 실패/끊김: ${e.message}")
                isConnected = false
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

    /**
     * 한 연결의 수명. 읽기 하나 + 쓰기 둘이 같은 소켓을 쓴다.
     *
     * 읽기를 이 함수의 본문에 두고 쓰기를 자식 코루틴으로 뺐다 — 쓰기가 [IOException] 으로
     * 죽으면 그 예외가 [coroutineScope] 를 통해 올라가 재접속으로 이어지고, 읽기는
     * `soTimeout` 주기마다 [Job.isActive] 를 다시 봐서 스스로 빠져나온다.
     *
     * @return `true` = 상대가 닫았다(EOF) · `false` = 취소·쓰기 실패로 우리가 멈췄다
     */
    /**
     * 🔴 **링크가 죽었는데 안 죽은 척하고 있나**(half-open).
     *
     * ## 이 감시가 없으면 영원히 재접속을 안 한다
     *
     * `ShotLink.readLoop` 는 읽기 타임아웃을 **정상으로 보고 `continue`** 한다(맞는 동작이다 —
     * 조용한 링크는 정상이다). 그런데 서버가 죽었는데 `FIN`·`RST` 가 **안 닿으면**
     * (Wi-Fi 끊김 · 프로세스 강제 종료 · NAT 정리) 그 `continue` 가 **영원히** 돈다.
     * 그러면 `runLink` 가 안 끝나고 **바깥 재접속 루프가 한 바퀴도 못 돈다.**
     *
     * 🔴 그리고 이 앱은 **평소에 아무것도 안 보낸다**(차가 없으면 번호판이 없다).
     * 쓰기를 안 하니 **쓰기 실패로도 못 알아챈다** — 탈출구가 읽기 하나뿐이다.
     *
     * ## 켜는 조건이 이 설계의 값이다
     *
     * ```
     * [pingSeen] 이 true 일 때만 감시한다 = **서버가 PING 을 보내는 것을 확인한 뒤에만**
     * → PING 을 안 보내는 서버에서는 감시가 **아예 안 켜진다** → 멀쩡한 연결을 안 끊는다
     * ```
     * 🔑 *"조용한 것"* 과 *"죽은 것"* 은 PING 이 있어야 갈린다. 없으면 가를 수 없고,
     * 가를 수 없으면 **끊지 않는 쪽이 안전하다.**
     */
    private fun linkStalled(): Boolean {
        if (!pingSeen) return false
        val idle = android.os.SystemClock.elapsedRealtime() - lastDownlinkAt
        if (idle < PING_STALL_MS) return false
        Log.w(TAG, "🔴 PING 이 ${idle}ms 동안 없다 — 링크가 죽은 것으로 보고 닫는다(재접속한다)")
        return true
    }

    private suspend fun runLink(socket: Socket, cfg: AppConfig): Boolean {
        // 링크별 상태는 **여기서** 새로 잡는다. 앞 링크의 시각을 물려받으면 붙자마자 죽었다고 본다.
        lastDownlinkAt = android.os.SystemClock.elapsedRealtime()
        pingSeen = false
        val link = ShotLink(socket.getInputStream(), socket.getOutputStream())
        var peerClosed = false
        try {
            coroutineScope {
                val self = coroutineContext[Job]!!
                val pushWriter = launch { for (p in queue) link.writeLine(encode(p, cfg.format)) }
                val replyWriter = launch { for (line in replyQueue) link.writeLine(line) }

                // 🔴 이것이 읽는 루프다. 이 줄이 없으면 SHOOT 는 아무 데도 도착하지 않는다.
                peerClosed = !link.readLoop(
                    // 🔑 `shouldContinue` 는 읽기 타임아웃마다 평가된다 —
                    //    그래서 **ShotLink 를 고치지 않고** 여기에 감시를 끼울 수 있다.
                    shouldContinue = {
                        self.isActive && pushWriter.isActive && replyWriter.isActive && !linkStalled()
                    },
                    onLine = ::handleDownlink,
                )

                // 채널은 계속 살아 있으므로(재접속에서 다시 쓴다) 소비자만 접는다.
                pushWriter.cancel()
                replyWriter.cancel()
            }
        } finally {
            link.stop()
            if (link.droppedLines > 0) {
                Log.w(TAG, "상한을 넘긴 하행 ${link.droppedLines}줄을 버렸다")
            }
        }
        return peerClosed
    }

    /**
     * 하행 한 줄. **읽는 스레드에서 불린다.**
     *
     * 모르는 줄은 무시한다(전방 호환) — 서버가 하행을 늘려도 앱이 깨지지 않는다.
     */
    /**
     * 마지막으로 받은 `PING` 번호. **읽는 스레드 전용**이라 잠금이 없다
     * (`handleDownlink` 는 소켓 읽는 코루틴 하나에서만 불린다).
     *
     * ⚠ 재접속해도 **안 지운다** — 서버가 안 죽었으면 번호가 이어지고, 끊긴 구간의 유실이
     * 그 연속성으로 잡힌다. 지우면 그 관측을 잃는다.
     */
    private var lastPingSeq: Long? = null

    /** 이 링크에서 마지막으로 **무엇이든** 받은 시각. 접속할 때마다 새로 잡는다. */
    private var lastDownlinkAt = 0L

    /**
     * 이 링크에서 `PING` 을 한 번이라도 받았나. **감시를 켜는 조건이다.**
     *
     * 🔑 이것이 이 설계의 안전장치다 — `PING` 을 **안 보내는 서버**(1차 `server_multi`)에서는
     * 감시가 **아예 안 켜지므로** 멀쩡한 연결을 끊지 않는다. 켜는 근거를 서버가 준다.
     */
    private var pingSeen = false

    private fun handleDownlink(line: String) {
        lastDownlinkAt = android.os.SystemClock.elapsedRealtime()
        if (line.isEmpty()) return
        when (val d = CameraShot.parseDownlink(line)) {
            is CameraShot.Downlink.Shoot -> {
                Log.i(TAG, "하행 SHOOT 수신 shot=${d.shotId}")
                onShoot(d.shotId)
            }
            // 번호가 없으면 답할 대상이 없다. 세는 대신 눈에 보이게 남긴다.
            is CameraShot.Downlink.Malformed ->
                Log.w(TAG, "하행 SHOOT 번호를 못 읽었다 — 답할 대상이 없다: '${d.line}'")
            is CameraShot.Downlink.Ping -> onPing(d.seq)
            is CameraShot.Downlink.Unknown ->
                Log.i(TAG, "하행 무시(모르는 줄): '${d.line.take(80)}'")
        }
    }

    /**
     * 링크 유지 하행(`PING,<seq>`). **답하지 않는다** — 규약이 응답을 안 받는다.
     *
     * ## 🔴 조용히 넘기되, **번호가 건너뛰면 그때는 말한다**
     *
     * 30초마다 로그를 찍으면 하루 2,880줄이라 다른 것을 덮는다. 그래서 평소에는 안 찍는다.
     * 다만 **번호가 이 링크의 유일한 관측점**이다 — 이 앱은 차가 없으면 아무것도 안 보내므로
     * 상행에는 아무 신호가 없다. 그래서 **빠진 번호만** 값으로 남긴다.
     *
     * ```
     * seq == 직전 + 1  → 정상. 조용히 넘긴다
     * seq  > 직전 + 1  → 🔴 **그 사이 줄이 안 왔다.** 몇 개인지 센다
     * seq <= 직전      → 🔑 유실이 아니라 **서버 재기동**이다(번호가 1부터 다시 센다)
     * seq == -1        → 번호가 깨졌다. PING 인 것은 맞으므로 링크 유지는 됐다
     * ```
     * ⚠ **끊겼다 다시 붙어도 번호는 이어진다**(서버가 안 죽었으면). 그래서 재접속 구간의
     * 유실도 이 계산에 잡힌다 — 그것이 이 지표를 쓰는 이유다.
     */
    private fun onPing(seq: Long) {
        if (seq < 0) {
            Log.w(TAG, "PING 번호를 못 읽었다 — 링크 유지는 됐다(유실 계산만 못 한다)")
            return
        }
        if (!pingSeen) {
            pingSeen = true
            Log.i(TAG, "PING 감시 켜짐 — 이후 ${PING_STALL_MS / 1000}초 무수신이면 링크가 죽은 것으로 본다")
        }
        val prev = lastPingSeq
        lastPingSeq = seq
        when {
            prev == null -> Log.i(TAG, "PING 수신 시작 seq=$seq (30초 주기 · 답하지 않는다)")
            seq == prev + 1 -> Unit  // 정상 — 조용히
            seq <= prev -> Log.i(TAG, "PING 번호가 되감겼다 $prev → $seq — 서버가 재기동된 것으로 본다")
            else -> Log.w(TAG, "🔴 PING 유실 ${seq - prev - 1}건 ($prev → $seq)")
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
        /**
         * 이 시간 동안 하행이 하나도 없으면 링크가 죽은 것으로 본다(감시가 켜져 있을 때만).
         *
         * **90초 = socket 의 PING 주기 30초 × 3.** 2회 연속 유실까지 견딘다 —
         * 한 번 놓쳤다고 끊으면 잠깐의 혼잡에도 재접속이 반복된다.
         * ⚠ 주기가 바뀌면 이 값도 같이 봐야 한다. 배수가 근거이지 90 이라는 수가 근거가 아니다.
         */
        const val PING_STALL_MS = 90_000L

        const val TAG = "TcpSender"

        /**
         * 읽기 블로킹 상한. 이 주기마다 읽는 루프가 깨어나 취소 여부를 확인한다.
         * **하행 지연과는 무관하다** — 자료가 오면 즉시 깨어난다. 취소 반응 시간의 상한일 뿐이다.
         */
        const val READ_TIMEOUT_MS = 1000
    }
}
