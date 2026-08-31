package com.example.digitcam

/**
 * 촬영 요청의 대기 목록. **누구에게 무엇을 답할지만 정하고, 전송도 인식도 하지 않는다.**
 *
 * Android 의존이 없다(시각을 인자로 받는다) — 그래서 중복·상한 거동이 단위 시험으로
 * 그대로 검증된다. 시계를 안에서 읽으면 그 시험이 실제 시간을 기다려야 한다.
 *
 * ## 요청이 온 **뒤에** 나온 값만 쓴다
 *
 * [onPlate] 는 새로 나온 인식 결과만 받는다. 화면이 들고 있는 마지막 값(캐시)을 여기에
 * 먹이지 마라 — 그러면 **요청 전에 이미 찍혀 있던 값**을 촬영 결과라고 올리게 된다.
 * "촬영했다" 가 참이려면 값이 요청보다 나중이어야 한다.
 *
 * ## 안정된 값만 온다
 *
 * `stable` 판정은 C++ 가 한다(계약 §1). 흔들리는 중간값을 올리면 서버가 그 번호판으로
 * **자리를 배정**하므로(카메라 pull 명세 §7) 부르는 쪽이 `stable` 을 걸러 넣는다.
 * 이 클래스는 "확정된 번호판" 만 받는다고 가정한다.
 *
 * 여러 스레드가 부른다(읽는 루프 · vision 스레드 · 워치독 코루틴) — 전부 잠금 아래다.
 */
class ShotCoordinator(
    private val maxPending: Int = DEFAULT_MAX_PENDING,
) {

    /** 한 요청에 대한 답. 전송은 부르는 쪽이 한다. */
    sealed interface Reply {
        val shotId: Long

        data class Success(override val shotId: Long, val plate: String) : Reply
        data class Failure(override val shotId: Long, val reason: String) : Reply
    }

    /** shotId → 요청을 받은 시각(ms). 삽입 순서를 유지해야 가장 오래된 것을 밀어낼 수 있다. */
    private val pending = LinkedHashMap<Long, Long>()

    /** 인식값이 [CameraShot.PLATE_MARKER] 여서 무시한 횟수. 0 이 아니면 이상한 일이 있는 것이다. */
    var markerValueIgnored = 0
        private set

    val pendingCount: Int
        @Synchronized get() = pending.size

    /**
     * 요청을 대기에 넣는다.
     *
     * @return 상한을 넘겨 밀려난 요청들. **비어 있지 않으면 그 답을 반드시 보내라** —
     *         안 보내면 서버 쪽에 영원한 `CAM_PENDING` 이 남는다.
     */
    @Synchronized
    fun request(shotId: Long, nowMs: Long): List<Reply> {
        // 같은 번호가 다시 오면 시각을 갱신하지 않는다. 접수 시각은 **처음 것**이 옳다.
        if (pending.containsKey(shotId)) return emptyList()
        pending[shotId] = nowMs
        if (pending.size <= maxPending) return emptyList()

        val evicted = ArrayList<Reply>(pending.size - maxPending)
        val it = pending.entries.iterator()
        while (pending.size > maxPending && it.hasNext()) {
            val oldest = it.next()
            it.remove()
            evicted.add(Reply.Failure(oldest.key, CameraShot.REASON_TOO_MANY_PENDING))
        }
        return evicted
    }

    /** 요청을 대기에서 빼고 이 답을 만들어 준다. 대기에 없으면 빈 목록(이미 답했다). */
    @Synchronized
    fun failOne(shotId: Long, reason: String): List<Reply> {
        pending.remove(shotId) ?: return emptyList()
        return listOf(Reply.Failure(shotId, reason))
    }

    /**
     * 대기 중인 요청 번호들. 오래된 것부터.
     *
     * 촬영본 **파일 이름**에 쓴다 — 그 사진이 어느 요청의 것인지 서버 대장과 눈으로 대조된다.
     * 🔑 답을 만들지 않는다. 조회 전용이다.
     */
    @Synchronized
    fun pendingIds(): List<Long> = pending.keys.toList()

    /**
     * 대기 전부를 이 사유로 닫는다. **촬영·인식이 최종 실패했을 때** 쓴다 —
     * 같은 셔터로 답하는 요청들이므로 결과도 같다.
     *
     * [dropAll] 과 다르다: 그쪽은 **답하지 않고 버리고**(링크가 끊겼다), 이쪽은 **답한다.**
     */
    @Synchronized
    fun failAllPending(reason: String): List<Reply> {
        if (pending.isEmpty()) return emptyList()
        val out = pending.keys.map { Reply.Failure(it, reason) }
        pending.clear()
        return out
    }

    /**
     * 확정된 번호판이 나왔다. 대기 중인 **모든** 요청에 이 값으로 답한다 — 짧은 사이에 두 번
     * 요청이 왔다면 둘 다 같은 차를 가리키고, 각자 `shot_id` 로 짝지어진다.
     *
     * 빈 값은 아무것도 하지 않는다. [CameraShot.PLATE_MARKER] 는 세고 무시한다.
     */
    @Synchronized
    fun onPlate(plate: String, nowMs: Long): List<Reply> {
        if (plate.isEmpty()) return emptyList()
        if (plate == CameraShot.PLATE_MARKER) {
            markerValueIgnored++
            return emptyList()
        }
        if (pending.isEmpty()) return emptyList()
        val out = pending.keys.map { Reply.Success(it, plate) }
        pending.clear()
        return out
    }

    // 🔴 **`sweepTimeouts` 를 지웠다** (2026-08-27 · 완주 설계).
    //
    // 지운 이유가 "안 쓰게 됐다" 가 아니다 — **그 함수가 결함의 방아쇠였다.**
    // 시한이 지나 여기서 대기를 비우면 `sendReplies` 가 `shutter.disarm()` 을 부르고,
    // 그러면 아직 도는 인식이 끝났을 때 `onRecognized` 가 `Wait` 을 돌려준다 →
    // **10초를 다 쓰고 나서 답만 사라진다.** socket 이 관측한 *늦게 도착한 번호 4건*이 그것이다.
    //
    // 🔑 대기가 영원히 안 닫히는 것이 걱정이라면 그 자리는 여기가 아니라 `ShutterGate` 다:
    //    · 인식이 끝나면 → `onPlate`/`failAllPending` 이 **전부** 답한다
    //    · 촬영 콜백이 안 오면 → `ShutterGate.captureWatchdog`
    //    · 링크가 끊기면 → `dropAll`
    //    ★ 세 갈래가 모든 경우를 덮는다. **시간으로 끊는 갈래만 없앴다.**

    /**
     * 링크가 끊겼다 — 대기 전부를 **답하지 않고 버린다.** 버린 번호를 돌려주므로 로그로 남긴다.
     *
     * 재접속 뒤에 늦은 답을 보내지 않는 이유: 그때 보낼 수 있는 것은 번호판 없는 실패 통지뿐이고,
     * 서버는 `cameraAge()` 로 같은 결론(오래된 pending)에 이미 도달할 수 있다. 명세 §8 이
     * **"시간 초과인가는 부르는 쪽이 판정한다"** 고 정했으므로 그 판정을 두 곳에 만들지 않는다.
     */
    @Synchronized
    fun dropAll(): List<Long> {
        if (pending.isEmpty()) return emptyList()
        val ids = pending.keys.toList()
        pending.clear()
        return ids
    }

    companion object {
        /**
         * 동시 대기 상한. 게이트가 하나라 이만큼 겹칠 구조가 아니다 — **추정값이고, 넘치는 것을
         * 조용히 버리지 않기 위한 선이지 성능 값이 아니다.** 넘으면 가장 오래된 것부터
         * [CameraShot.REASON_TOO_MANY_PENDING] 으로 답하고 밀어낸다.
         */
        const val DEFAULT_MAX_PENDING = 64
    }
}
