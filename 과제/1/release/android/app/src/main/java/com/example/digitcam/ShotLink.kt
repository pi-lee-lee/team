package com.example.digitcam

import java.io.InputStream
import java.io.OutputStream
import java.net.SocketTimeoutException

/**
 * **읽는 루프.** 서버가 내려보내는 줄을 같은 연결에서 읽고, 응답을 같은 연결로 올린다.
 *
 * 이 클래스에 Android 의존이 하나도 없는 것이 의도다 — 그래서 **실기와 같은 코드**를
 * JVM 단위 시험에서 루프백 TCP 에 붙여 돌릴 수 있다(`ShotLinkTest`). 소켓을 만들고
 * 재접속을 관리하는 것은 `TcpSender` 의 몫이고, 여기는 스트림 두 개만 안다.
 *
 * ## 부르는 쪽이 지켜야 하는 것
 *
 * **소켓에 `soTimeout` 을 걸어라.** 블로킹 `read` 는 코루틴 취소를 보지 않으므로, 타임아웃이
 * 없으면 [readLoop] 이 영원히 깨어나지 않고 그 스레드가 연결이 끊길 때까지 매달린다.
 * 타임아웃이 걸려 있으면 그 주기마다 [shouldContinue] 를 다시 물어본다.
 *
 * ## 스레드
 *
 * [readLoop] 은 한 스레드가 소유한다. [writeLine] 은 여러 스레드에서 불러도 된다 —
 * 출력이 잠금으로 직렬화된다. **직렬화가 없으면 두 응답의 바이트가 섞여 두 줄이 다 깨진다.**
 */
class ShotLink(
    private val input: InputStream,
    private val output: OutputStream,
    readBufferSize: Int = DEFAULT_READ_BUFFER,
    maxLineBytes: Int = LineReader.DEFAULT_MAX_LINE_BYTES,
) {

    private val lineReader = LineReader(maxLineBytes)
    private val readBuffer = ByteArray(readBufferSize)
    private val writeLock = Any()

    @Volatile
    private var stopped = false

    /** 상한을 넘겨 버린 줄 수. 0 이 아니면 부르는 쪽이 로그로 남긴다. */
    val droppedLines: Int get() = lineReader.droppedLines

    /**
     * 스트림이 끝나거나 [stop] 이 불리거나 [shouldContinue] 가 false 가 될 때까지 읽는다.
     * **이 함수가 돌지 않으면 하행은 아무 데도 도착하지 않고, 오류도 나지 않는다.**
     *
     * @param shouldContinue 매 읽기 주기마다 물어본다. 코루틴 취소를 여기로 넣는다.
     * @param onLine 완성된 줄 하나. 읽는 스레드에서 불린다 — 오래 걸리는 일을 하지 마라.
     * @return `true` = 바깥 사정(취소·[stop])으로 멈췄다 · `false` = 상대가 스트림을 닫았다(EOF)
     */
    fun readLoop(shouldContinue: () -> Boolean, onLine: (String) -> Unit): Boolean {
        while (!stopped && shouldContinue()) {
            val n = try {
                input.read(readBuffer)
            } catch (_: SocketTimeoutException) {
                // 정상이다. 취소 여부만 다시 보고 계속 기다린다.
                // 🔑 이 예외 뒤에도 스트림은 유효하고, 모으던 부분 바이트는 LineReader 에 남는다.
                continue
            }
            if (n < 0) return false
            if (n == 0) continue
            for (line in lineReader.feed(readBuffer, n)) onLine(line)
        }
        return true
    }

    /**
     * 한 줄 올린다. LF 로 끝나지 않으면 붙인다 — **LF 누락은 받는 쪽에서 두 줄이 한 줄로
     * 붙어 버리는 조용한 실패라, 부르는 쪽의 주의에 맡기지 않는다.**
     */
    fun writeLine(text: String) {
        val payload = if (text.endsWith("\n")) text else text + "\n"
        val bytes = payload.toByteArray(Charsets.UTF_8)
        synchronized(writeLock) {
            output.write(bytes)
            output.flush()
        }
    }

    /** [readLoop] 을 멈춘다. 블로킹 `read` 중이면 `soTimeout` 주기 뒤에 반영된다. */
    fun stop() {
        stopped = true
    }

    companion object {
        private const val DEFAULT_READ_BUFFER = 4096
    }
}
