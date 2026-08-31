package com.example.digitcam

/**
 * **지금 무엇을 하고 있는지**를 사람에게 보여 줄 문장을 만든다 (REQ-0500).
 *
 * ## 🔴 이건 꾸밈이 아니라 관측자다
 *
 * 인식 시한을 폐기했다 — 10~40초를 **끝까지 기다린다**. 그런데 그동안 화면이 아무 말도
 * 안 하면 사용자가 보는 것은 *"멈춘 폰"* 이다.
 * > ★ **기다린다는 것을 사람이 볼 수 있어야 그 설계가 산다.**
 *
 * 🔑 그래서 **경과 초가 핵심이다.** 마지막 문장만 남기면 *"도는 중"* 과 *"멈춤"* 이 같은
 * 모양이 되고, 그러면 이 화면은 **아무것도 관측하지 않는다.**
 *
 * ## 🔴 스레드 — 인식 스레드를 막지 않는 것이 설계 제약이다
 *
 * ```
 * 쓰기 : 소켓 읽는 스레드(요청 도착) · 메인(셔터) · vision 스레드(인식 끝)
 * 읽기 : UI 스레드만 (주기적으로 [snapshot] → [render])
 * ```
 * ⚠ **[render] 는 잠금 밖에서 돈다.** 문자열을 만드는 동안 잠금을 쥐면 그만큼 vision
 * 스레드가 기다리고, **시한을 없앤 이유를 스스로 깎는다.** 잠금 안에서 하는 일은
 * [snapshot] 의 목록 복사 하나뿐이다.
 *
 * Android 의존이 없다 — 그래서 단계 전이와 문구가 단위 시험으로 닫힌다.
 */
class ShotProgress(
    /**
     * 이력을 몇 건까지 남기나.
     *
     * 🔑 **하나만 남기면 안 된다.** 서버가 실패 뒤 재요청하므로 다음 시도가 앞 결과를 덮고,
     * **"왜 실패했는지" 가 사라진다** — 지금 우리가 쫓고 있는 것이 정확히 그것이다.
     */
    private val keep: Int = DEFAULT_KEEP,
) {

    enum class Stage {
        /** [1] 서버에서 요청이 왔다. */
        REQUESTED,

        /** [2] 셔터를 눌렀다. */
        CAPTURING,

        /** [3] 사진이 왔고 인식 중이다. **여기가 길다.** */
        RECOGNIZING,

        /** [4] 번호를 찾았다. */
        FOUND,

        /** [4'] 못 찾았다. [detail] 에 사유 코드. */
        FAILED,

        /** [5] 답을 전송 큐에 넣었다. */
        SENT,

        /** [5'] 답을 못 보냈다(연결 없음 또는 큐가 참). */
        SEND_FAILED,
    }

    /**
     * 요청 하나의 진행. **끝나도 지우지 않는다** — 이력이 남아야 재요청을 읽을 수 있다.
     *
     * @param shotId 서버 요청번호. `0` 이면 화면의 [촬영] 버튼(답할 상대가 없다)
     * @param endedMs `null` 이면 **아직 도는 중**이다. 경과 초를 올릴지 말지가 이 값으로 갈린다
     */
    data class Entry(
        val shotId: Long,
        val attempt: Int,
        val stage: Stage,
        val startedMs: Long,
        val stageSinceMs: Long,
        val endedMs: Long?,
        val detail: String,
        /**
         * 🔴 **결과를 따로 들고 다닌다.** [stage] 는 *지금* 단계라 [Stage.SENT] 가 되는 순간
         * 덮인다 — 그러면 화면에 *"전송했습니다"* 만 남고 **왜 실패했는지가 사라진다.**
         *
         * ★ 처음 구현이 정확히 그랬고 단위 시험이 잡았다. 이 REQ 가 막으려던 실패가
         * **구현 안에서 다시 났다** — 이력을 여러 줄 남기는 것만으로는 부족하고,
         * **한 줄 안에서도 결과가 진행에 덮이지 않아야** 한다.
         */
        val outcome: Stage? = null,
        val outcomeDetail: String = "",
    ) {
        val running: Boolean get() = endedMs == null
    }

    private val entries = ArrayList<Entry>(DEFAULT_KEEP + 1)

    /**
     * 🔴 화면에서 **밀려난** 건수. 안 세면 화면의 4건이 **전부처럼 읽힌다**.
     *
     * ⚠ 그리고 하필 **가장 보고 싶을 때 밀린다** — 서버가 실패 뒤 재요청하므로
     * 재요청이 잦을 때 정확히 앞 실패가 밀려난다.
     * 🔑 §"없다 는 창 크기에 종속된다" — **잘린 것을 말하지 않으면 자른 적이 없는 것처럼 보인다.**
     */
    private var dropped = 0

    /** 지금 도는 것이 있나. 화면을 계속 켜 둘지, 틱을 돌릴지 정하는 값이다. */
    @Synchronized
    fun isRunning(): Boolean = entries.any { it.running }

    /** [1] 새 요청. 같은 `shotId` 가 다시 오면 **새 줄**이다 — 재요청을 덮으면 안 된다. */
    @Synchronized
    fun requested(shotId: Long, nowMs: Long) {
        entries.add(
            Entry(
                shotId = shotId,
                attempt = 1,
                stage = Stage.REQUESTED,
                startedMs = nowMs,
                stageSinceMs = nowMs,
                endedMs = null,
                detail = "",
            ),
        )
        while (entries.size > keep) { entries.removeAt(0); dropped++ }
    }

    /**
     * 도는 줄의 단계를 바꾼다. **도는 줄이 없으면 아무것도 안 한다.**
     *
     * 🔑 화면 버튼으로 찍을 때는 요청이 없어서 도는 줄도 없다. 그때는 [requested] 를
     * `shotId = 0` 으로 먼저 부른다 — **없는 줄을 여기서 만들지 않는다.** 만들면
     * *"요청이 왔다"* 와 *"사람이 눌렀다"* 가 화면에서 섞인다.
     */
    @Synchronized
    fun advance(stage: Stage, nowMs: Long, detail: String = "", attempt: Int? = null) {
        val i = entries.indexOfLast { it.running }
        if (i < 0) return
        val e = entries[i]
        val isOutcome = stage == Stage.FOUND || stage == Stage.FAILED
        entries[i] = e.copy(
            stage = stage,
            stageSinceMs = nowMs,
            attempt = attempt ?: e.attempt,
            detail = detail,
            endedMs = if (stage == Stage.SENT || stage == Stage.SEND_FAILED) nowMs else null,
            // 결과는 **한 번 정해지면 안 덮인다.** 뒤따르는 전송 단계가 지우면 안 된다.
            outcome = if (isOutcome) stage else e.outcome,
            outcomeDetail = if (isOutcome) detail else e.outcomeDetail,
        )
    }

    /** 도는 줄을 그 자리에서 끝낸다(링크가 끊겨 답할 곳이 사라졌을 때 등). */
    @Synchronized
    fun abandon(nowMs: Long, detail: String) {
        val i = entries.indexOfLast { it.running }
        if (i < 0) return
        entries[i] = entries[i].copy(stage = Stage.SEND_FAILED, stageSinceMs = nowMs, endedMs = nowMs, detail = detail)
    }

    /** 화면에서 밀려난 건수. */
    @Synchronized
    fun droppedCount(): Int = dropped

    /** 잠금 안에서 하는 유일한 일. **문자열은 밖에서 만든다.** */
    @Synchronized
    fun snapshot(): List<Entry> = ArrayList(entries)

    companion object {
        const val DEFAULT_KEEP = 4

        /**
         * 인식이 이보다 오래 걸리면 화면에 경고를 띄운다. **끊지는 않는다.**
         *
         * 🔴 이것은 시한이 **아니다.** 시한은 폐기됐고 그 결정은 그대로다 —
         * 여기서 하는 일은 **끊는 것이 아니라 말하는 것**이다.
         */
        const val STALL_WARN_MS = 60_000L

        /**
         * 사유 코드 → 사람이 읽을 문장. **원문 코드를 괄호로 같이 남긴다.**
         *
         * 🔑 안 남기면 사용자가 **화면에서 본 것과 로그에 있는 것을 못 잇는다** —
         * *"흔들림"* 을 보고 `judgements.log` 에서 `moving` 을 찾을 수 없다.
         *
         * ⚠ **모르는 코드를 뭉개지 마라.** `모르는 사유 (xyz)` 로 **그대로 보여 준다** —
         * ★ 그 규율이 검사 역할까지 한다: 낯선 낱말이 화면에 뜨면 **누군가 어휘를 늘렸는데
         * 여기가 안 따라온 것**이고, "실패" 로 뭉개면 그 사실이 영영 안 보인다.
         */
        fun reasonText(code: String): String {
            val ko = KNOWN_REASONS[code] ?: return "모르는 사유 ($code)"
            return "$ko ($code)"
        }

        /**
         * 🔴 **짧게 쓴다 — 폰 화면은 좁다.**
         *
         * 처음에는 *"번호판은 찾았는데 글자를 나누지 못했습니다"* 처럼 문장으로 썼다.
         * 그런데 폭을 계산해 보니 그 줄이 **화면 폭의 2배**였다(380dp 에 752dp).
         * 넘치면 접히고, **접힌 줄은 다음 항목처럼 보인다** — 이력이 이력으로 안 읽힌다.
         *
         * 🔵 **긴 설명은 다른 데 있다** — 배포 문서(`DEPLOY-2026-08-28.md`)와 관제 화면(web).
         * 폰 화면이 할 일은 *"무엇이 왜 실패했나"* 를 **한 눈금**으로 보여 주는 것이다.
         * ⚠ 그래도 **원문 코드는 반드시 남긴다** — 그것이 로그와 잇는 유일한 끈이다.
         */
        private val KNOWN_REASONS: Map<String, String> = mapOf(
            // 네이티브가 주는 것 — 계약 §5.2 어휘
            "moving" to "움직임",
            "no_plate" to "판 못 찾음",
            "segment_fail" to "글자 분할 실패",
            "blurry" to "흔들림·초점",
            "low_conf" to "신뢰도 낮음",
            // 앱이 내는 것
            CameraShot.REASON_RECOGNIZE_FAILED to "못 읽음",
            CameraShot.REASON_CAPTURE_FAILED to "촬영 실패",
            CameraShot.REASON_CAPTURE_STUCK to "촬영 무응답",
            CameraShot.REASON_NO_PERMISSION to "권한 없음",
            CameraShot.REASON_CAMERA_UNAVAILABLE to "카메라 미준비",
            CameraShot.REASON_TOO_MANY_PENDING to "대기 초과",
        )

        /**
         * 화면에 그릴 여러 줄. **[nowMs] 를 받는 이유는 경과 초 때문이다** —
         * 도는 줄은 부를 때마다 값이 달라져야 한다. 안 그러면 정지 화면이다.
         */
        fun render(entries: List<Entry>, nowMs: Long, dropped: Int = 0): String {
            if (entries.isEmpty()) return ""
            val body = entries.joinToString("\n") { line(it, nowMs) }
            // 🔴 잘린 것을 말한다. 안 하면 화면의 몇 건이 **전부처럼** 읽힌다.
            return if (dropped > 0) "$body\n· (이전 ${dropped}건은 화면에서 밀렸습니다)" else body
        }

        /**
         * 결과 문장.
         *
         * ⚠ 결과가 없는 경우는 **"인식이 실패했다" 가 아니다** — `abandon()` 으로 끝난 줄은
         * **인식까지 가지도 않았다**(링크가 끊겨 그만뒀다). *"결과 없음"* 이라고만 쓰면
         * 읽는 사람이 **인식 실패로 읽는다.** 🔑 다른 것을 같은 글자로 말하지 않는다.
         */
        private fun outcomeText(e: Entry): String = when (e.outcome) {
            Stage.FOUND -> "찾았습니다: ${e.outcomeDetail}"
            Stage.FAILED -> "실패 — ${reasonText(e.outcomeDetail)}"
            else -> "결과 전에 중단됨"
        }

        /**
         * 🔴 **이 숫자가 무엇인지 정직하게 이름 붙인다.**
         *
         * ## 셋은 다른 사실이다 — 하나로 셋을 말하면 안 된다
         *
         * ```
         * 요청 뒤 12.4초  = **서버가 기다린 시간**. 서버의 대기 예산과 맞춰 볼 값
         * 인식 8.1초째    = **인식이 실제로 얼마나 걸리나**. 🔵 시한 폐기를 판정하는 값이 이것이다
         * (없음)          = **인식이 살아 있나**. 🔴 우리는 이것을 **못 잰다**(아래)
         * ```
         *
         * ## 🔴 왜 "인식이 살아 있나" 를 못 재나 — 하트비트가 안 되는 이유
         *
         * 인식의 대부분은 **네이티브 호출 하나**다(`nativeProcessGray`). 그 안에서 10~40초를
         * 쓴다. Kotlin 쪽에서 박동을 찍을 수 있는 자리는 **그 호출 앞뒤뿐**이라,
         * 정상 동작 중에도 박동이 **그 시간 내내 멎는다.**
         * > ★ **그런 하트비트는 살아 있는 것을 죽었다고 말한다. 없는 것보다 나쁘다.**
         * 🔑 진짜 박동은 **네이티브가 중간에 알려 줘야** 만들 수 있다 — 그건 cpp 의 자리다.
         *
         * ## ✅ 그래서 대신 하는 것 — **침묵하지 않는다**
         *
         * 시한을 없앤 것은 옳았다. 다만 그 시한이 **"멈춤을 감지하는 수단" 을 겸하고 있었다.**
         * 끊지는 않되 [STALL_WARN_MS] 를 넘으면 **경고를 띄운다** —
         * 완주는 그대로 두고, **화면이 건강하다고 거짓말하는 것만** 막는다.
         */
        private fun line(e: Entry, nowMs: Long): String {
            val mark = if (e.running) "▸" else "·"
            val who = if (e.shotId == 0L) "촬영 버튼" else "요청 ${e.shotId}"
            val tries = if (e.attempt > 1) " · ${e.attempt}번째" else ""
            // 🔴 **일부러 두 줄로 나눈다.** 한 줄에 다 넣으면 폭을 넘겨 **접히는데**,
            //    접힌 줄은 화면에서 **다음 항목처럼** 보인다 — 이력이 이력으로 안 읽힌다.
            //    ★ 접히는 것과 나누는 것은 다르다. 나누면 어디까지가 한 건인지 보인다.
            return "$mark $who$tries · ${timing(e, nowMs)}\n    ${body(e, nowMs)}"
        }

        /** 첫 줄 꼬리 — **숫자만.** 무엇을 잰 것인지 낱말에 드러나야 한다. */
        private fun timing(e: Entry, nowMs: Long): String {
            val total = fmt((e.endedMs ?: nowMs) - e.startedMs)
            return when {
                // 🔵 인식 중이면 **단계 경과가 먼저다** — 시한 폐기를 판정하는 값이 그것이다.
                e.running && e.stage == Stage.RECOGNIZING -> {
                    val stageMs = nowMs - e.stageSinceMs
                    "인식 ${fmt(stageMs)}째 · 총 $total"
                }
                e.running -> "총 $total"
                // ⚠ 끝난 줄에는 **"언제" 를 붙인다.** 소요만 있으면 30분 뒤에 봤을 때
                //    그것이 방금 것인지 모른다.
                else -> "$total · ${ago(nowMs - (e.endedMs ?: nowMs))}"
            }
        }

        /** 둘째 줄 — 사람이 읽는 말. */
        private fun body(e: Entry, nowMs: Long): String = when (e.stage) {
            Stage.REQUESTED -> "촬영 요청을 받았습니다"
            Stage.CAPTURING -> "촬영을 시작합니다"
            // 🔴 여기가 10~40초다. 점 세 개는 장식이 아니라 **아직 안 끝났다는 표시**다.
            // 🔵 경고는 **여기**에 붙인다 — 첫 줄(숫자)이 아니라 **하는 일** 옆이다.
            //    폭도 여기가 여유롭고, 읽는 사람도 "무엇이 오래 걸리나" 옆에서 본다.
            Stage.RECOGNIZING -> "번호를 추출합니다 …" + stallWarn(e, nowMs)
            Stage.FOUND, Stage.FAILED -> outcomeText(e)
            // ⚠ *"서버가 받았다"* 가 아니다. 우리가 아는 것은 **보냈다**까지다.
            // 🔴 결과를 앞에 붙여 **전송 단계가 사유를 덮지 않게** 한다.
            Stage.SENT -> outcomeText(e) + " → 전송함"
            Stage.SEND_FAILED -> outcomeText(e) + " → 전송 못 함(${e.detail})"
        }

        private fun fmt(ms: Long): String = "%.1f초".format(ms.coerceAtLeast(0L) / 1000.0)

        /** 끝난 지 얼마나 됐나. **분 단위면 충분하다** — 초까지 쓰면 화면이 시끄럽다. */
        private fun ago(ms: Long): String {
            val sec = (ms.coerceAtLeast(0L)) / 1000
            return when {
                sec < 60 -> "방금"
                sec < 3600 -> "${sec / 60}분 전"
                else -> "${sec / 3600}시간 전"
            }
        }

        /**
         * 오래 걸리면 경고. **끊지 않는다** — 완주 설계를 깎지 않으려는 것이다.
         *
         * ⚠ **[STALL_WARN_MS] 는 추정값이다.** 실기 관측이 10~40초라 그보다 넉넉히 위에 뒀다.
         * 🔑 **자주 울리는 경보는 꺼진다** — 정상 구간에 울리면 사람이 무시하기 시작한다.
         * ★ 배포 뒤 `judgements.log` 의 `ms=` 분포가 나오면 **그 값으로 다시 정해라.**
         */
        private fun stallWarn(e: Entry, nowMs: Long): String =
            if (e.running && nowMs - e.stageSinceMs >= STALL_WARN_MS) "  ⚠ 너무 깁니다" else ""
    }
}
