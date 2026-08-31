package com.example.digitcam

import java.io.ByteArrayOutputStream

/**
 * LF 종단 줄 조립기. 바이트를 밀어 넣으면 완성된 줄만 돌려준다.
 *
 * ## 왜 `BufferedReader.readLine()` 을 안 쓰나
 *
 * 읽는 루프는 소켓에 `soTimeout` 을 걸고 돈다 — 그래야 코루틴 취소가 최대 그 시간 안에
 * 반영된다(블로킹 `read` 는 취소를 안 본다). 그런데 `readLine()` 이 줄 중간에서
 * `SocketTimeoutException` 을 맞으면 **그때까지 읽은 부분 문자를 통째로 잃는다** —
 * 모으던 `StringBuilder` 가 그 호출의 지역 변수이기 때문이다. 그러면 `SHOOT,2026...` 이
 * 조용히 반쪽으로 사라진다.
 *
 * 여기서는 부분 바이트가 [acc] 에 남으므로 타임아웃이 몇 번 끼어도 줄이 온전하다.
 *
 * ## 바이트로 모으고 LF 에서만 디코딩한다
 *
 * **번호판에 한글이 들어간다.** 문자로 먼저 디코딩하면 UTF-8 멀티바이트가 읽기 경계에서
 * 쪼개져 깨진다. LF(0x0A)는 UTF-8 멀티바이트의 이어지는 바이트로는 절대 나타나지 않으므로,
 * 바이트에서 LF 를 찾아 자른 뒤 디코딩하는 것이 안전하다.
 *
 * 스레드 안전하지 않다 — 읽는 루프 하나가 소유한다.
 */
class LineReader(private val maxLineBytes: Int = DEFAULT_MAX_LINE_BYTES) {

    private val acc = ByteArrayOutputStream()

    /** 상한을 넘긴 줄을 버리는 중인가. 다음 LF 까지 버린다. */
    private var skipping = false

    /** 상한 때문에 버린 줄 수. **조용히 버리지 않기 위해 세어 둔다** — 부르는 쪽이 로그한다. */
    var droppedLines = 0
        private set

    /** 아직 줄이 안 된 채로 들고 있는 바이트 수. */
    val pendingBytes: Int get() = acc.size()

    /**
     * [len] 바이트를 밀어 넣고 이번에 완성된 줄들을 순서대로 돌려준다.
     *
     * 돌려주는 줄에서 끝의 `\r` 은 떼어 낸다(`\r\n` 종단도 받는다). **빈 줄도 돌려준다** —
     * 버리면 그것이 무엇이었는지 부르는 쪽이 알 수 없다. 무시할지는 부르는 쪽이 정한다.
     */
    fun feed(bytes: ByteArray, len: Int): List<String> {
        if (len <= 0) return emptyList()
        var out: ArrayList<String>? = null
        for (i in 0 until len) {
            val b = bytes[i]
            if (b == LF) {
                if (skipping) {
                    skipping = false
                } else {
                    val line = decodeAndReset()
                    (out ?: ArrayList<String>(2).also { out = it }).add(line)
                }
                continue
            }
            if (skipping) continue
            if (acc.size() >= maxLineBytes) {
                // 한 줄이 상한을 넘었다. 남은 부분과 함께 버리고 다음 LF 를 기다린다.
                // 반쪽을 줄로 올리면 파서가 깨진 JSON 을 보게 되고, 안 버리면 메모리가 자란다.
                acc.reset()
                skipping = true
                droppedLines++
                continue
            }
            acc.write(b.toInt())
        }
        return out ?: emptyList()
    }

    private fun decodeAndReset(): String {
        val raw = acc.toByteArray()
        acc.reset()
        val end = if (raw.isNotEmpty() && raw[raw.size - 1] == CR) raw.size - 1 else raw.size
        return String(raw, 0, end, Charsets.UTF_8)
    }

    companion object {
        private const val LF: Byte = 0x0A
        private const val CR: Byte = 0x0D

        /**
         * 한 줄 상한. 하행은 `SHOOT,<12자리>` 라 20바이트 남짓이다. 8KB 는 넉넉하면서도
         * 상대가 LF 없이 계속 보낼 때 메모리가 무한정 자라지 않게 막는 선이다.
         */
        const val DEFAULT_MAX_LINE_BYTES = 8 * 1024
    }
}
