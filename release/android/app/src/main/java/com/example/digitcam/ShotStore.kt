package com.example.digitcam

import java.io.File
import java.io.IOException

/**
 * 촬영본 파일 보관. **요청번호로 이름을 붙이고 최근 N장만 남긴다.**
 *
 * ## 왜 파일로 남기나
 *
 * 인식이 실패했을 때 *"인식기가 못 읽었다"* 와 *"애초에 이상한 사진을 찍었다"* 는 값만으로는
 * 구분되지 않는다. 그 사진이 있어야 갈린다.
 *
 * ## 왜 지우나
 *
 * 안 지우면 저장공간이 찬다. 서버 대장이 링 100 인 것과 같은 형태로 **최근 N장**만 남긴다.
 * ⚠ N 의 기본값은 **추정이다** — 촬영본 크기를 아직 실기에서 재지 않았다.
 *
 * ## 이름이 요청번호를 담는다
 *
 * `shot-202608210001.jpg` — 서버 대장의 `shot_id` 와 **눈으로 대조된다.** 재시도는 `-r1` 이 붙는다.
 *
 * ## 🔴 정렬은 **이름이 아니라 파싱한 값**으로 한다
 *
 * ~~*"`shot_id` 가 단조 증가라 이름 정렬이 곧 시간 정렬이다"*~~ — **틀렸다. 시험이 잡았다.**
 * ```
 * shot-…001-r1.jpg  vs  shot-…001.jpg
 * '-'(0x2D) < '.'(0x2E)  →  🔴 **재시도본이 원본보다 먼저 온다**
 * → 이름 정렬로 지우면 **나중에 찍은 재시도본이 먼저 지워진다**
 * ```
 * 재시도본은 원본이 실패해서 다시 찍은 것이라 **인식에 성공한 쪽일 확률이 높다.**
 * 그것을 먼저 버리면 증거를 거꾸로 지운다.
 *
 * → `(shotId, attempt)` 로 정렬한다. **접미 형태를 바꿔도 안 깨진다** —
 * 순서 규칙이 이름 규칙에 딸려 있지 않다.
 *
 * `mtime` 을 안 쓰는 이유는 그대로다: 파일 복사·이동이 mtime 을 바꾼다.
 *
 * Android 의존이 없다(`java.io.File` 뿐) — 그래서 회전 거동이 단위 시험으로 닫힌다.
 */
class ShotStore(
    private val dir: File,
    private val keep: Int = DEFAULT_KEEP,
) {

    /**
     * 이 이름 규칙에 맞는 파일만 우리 것으로 센다. **규칙에 안 맞는 파일은 건드리지 않는다** —
     * 사람이 그 디렉터리에 무엇을 넣었을 수 있고, 남의 파일을 지우지 않는다.
     */
    private val ownPattern = Regex("""^shot-(\d+)(-r\d+)?\.jpg$""")

    /** 저장할 파일 경로. 디렉터리는 만들지 않는다(쓰는 시점에 [save] 가 만든다). */
    fun fileFor(shotId: Long, attempt: Int = 0): File =
        File(dir, if (attempt <= 0) "shot-$shotId.jpg" else "shot-$shotId-r$attempt.jpg")

    /**
     * 바이트를 파일로 쓴 뒤 회전한다.
     *
     * @return 쓴 파일. 실패하면 null — **예외를 위로 던지지 않는다.** 저장 실패는
     *         촬영 사슬의 한 갈래이고(`capture_failed`), 앱을 죽일 일이 아니다.
     */
    fun save(shotId: Long, attempt: Int, bytes: ByteArray): File? {
        val target = fileFor(shotId, attempt)
        return try {
            dir.mkdirs()
            target.outputStream().buffered().use { it.write(bytes) }
            prune()
            target
        } catch (e: IOException) {
            null
        }
    }

    /** `(shotId, attempt)` — 이름 규칙에 맞지 않으면 null(우리 파일이 아니다). */
    private fun order(name: String): Pair<Long, Int>? {
        val m = ownPattern.matchEntire(name) ?: return null
        val id = m.groupValues[1].toLongOrNull() ?: return null
        // 두 번째 그룹은 "-r1" 또는 빈 문자열. 빈 것은 첫 시도(0)다.
        val attempt = m.groupValues[2].removePrefix("-r").toIntOrNull() ?: 0
        return id to attempt
    }

    /** 우리 이름 규칙에 맞는 파일들. **오래된 것부터** — `(shotId, attempt)` 순이다. */
    fun list(): List<File> =
        dir.listFiles()
            ?.mapNotNull { f -> if (f.isFile) order(f.name)?.let { it to f } else null }
            ?.sortedWith(compareBy({ it.first.first }, { it.first.second }))
            ?.map { it.second }
            ?: emptyList()

    /**
     * [keep] 을 넘는 오래된 파일을 지운다.
     *
     * @return 지운 파일 수. 0 이 아니면 부르는 쪽이 로그로 남긴다 —
     *         **조용히 지우면 "내 사진이 어디 갔나" 를 아무도 답할 수 없다.**
     */
    fun prune(): Int {
        val files = list()
        if (files.size <= keep) return 0
        var removed = 0
        for (f in files.take(files.size - keep)) {
            if (f.delete()) removed++
        }
        return removed
    }

    /**
     * 🔴 **판정 한 줄을 사진 옆에 남긴다.** 사진 파일과 **같은 폴더, 한 파일**([LOG_NAME]).
     *
     * ## 왜 필요한가 — 없으면 측정 절차가 실행 불가능하다
     *
     * `PREREG-plate-accuracy.md` §15 는 사진마다 `reason` 을 갈라 세라고 한다
     * (검출 실패 ↔ 분할 실패는 **조정 방향이 반대다**). 그런데:
     * ```
     * 화면   : 값이 나오지만 **다음 프레임에 흘러간다**
     * logcat : 남지만 🔴 **사용자가 볼 수 없다**(폰만 있고 개발기가 없는 상황이 정상이다)
     * 사진    : 저장되는데 🔴 **판정이 안 붙어 있다**
     * ```
     * → 🔑 **27장을 찍어도 "어느 것이 왜 실패했는지" 를 셀 수 없다.** 이 파일이 그것을 닫는다.
     *
     * ## 한 파일인 이유
     *
     * 사진마다 `.txt` 를 만들면 파일이 두 배가 되고 **그중 하나만 빠지는 경우**가 생긴다.
     * 한 파일이면 `adb pull` 한 번에 순서까지 같이 온다.
     *
     * ⚠ **[prune] 이 이 파일을 지우지 않는다** — `ownPattern` 이 `.jpg` 만 잡는다.
     * 대신 줄 수 상한([LOG_MAX_LINES])을 넘으면 **앞을 버린다.** 사진이 [keep] 장만 남으므로
     * 그보다 훨씬 오래된 판정은 대조할 사진이 없다.
     *
     * @return 남겼으면 true. 🔑 **실패해도 인식은 계속해야 하므로 예외를 던지지 않는다.**
     */
    fun appendJudgement(line: String): Boolean {
        if (line.isBlank()) return false
        val f = File(dir, LOG_NAME)
        return try {
            if (!f.exists()) {
                dir.mkdirs()
                f.writeText(LOG_HEADER)
            }
            f.appendText(line.trim().replace('\n', ' ') + "\n")
            trimLog(f)
            true
        } catch (e: Exception) {
            // 조용히 실패하면 "왜 로그가 없나" 를 답할 수 없다.
            android.util.Log.w("ShotStore", "판정 로그 실패: ${e.message}")
            false
        }
    }

    /** 줄 수 상한을 넘으면 **앞을 버린다.** 헤더는 다시 붙인다. */
    private fun trimLog(f: File) {
        val lines = f.readLines()
        if (lines.size <= LOG_MAX_LINES) return
        // 🔴 헤더가 **여러 줄** 이다. `- 1` 로 두면 헤더만큼 상한을 넘는다(실측 2002).
        val headerLines = LOG_HEADER.count { it == '\n' }
        val kept = lines.takeLast((LOG_MAX_LINES - headerLines).coerceAtLeast(1))
        f.writeText(LOG_HEADER + kept.joinToString("\n") + "\n")
    }

    /** 판정 로그 파일(있으면). 시험과 진단용. */
    fun logFile(): File? = File(dir, LOG_NAME).takeIf { it.exists() }

    companion object {
        /** 앱 전용 외부 저장소 아래의 하위 폴더 이름. 권한이 필요 없고 `adb pull` 로 꺼낼 수 있다. */
        const val DIR_NAME = "shots"

        /** 판정 한 줄 로그. 🔑 사진과 **같은 폴더**라 `adb pull` 한 번에 같이 온다. */
        const val LOG_NAME = "judgements.log"

        /**
         * 줄 수 상한. 넘으면 앞을 버린다.
         *
         * 🔑 근거: 사진이 [DEFAULT_KEEP] 장만 남으므로 그보다 훨씬 오래된 판정은
         * **대조할 사진이 없다.** 넉넉히 두되 무한히 자라게는 두지 않는다.
         * ⚠ 촬영은 사용자 조작이 방아쇠라 주기 동작이 아니다 — 하루 수백 줄 규모다.
         */
        const val LOG_MAX_LINES = 2000

        /** 🔑 파일을 처음 여는 사람이 무엇인지 알아야 한다. */
        const val LOG_HEADER =
            "# 촬영본 판정 로그 — 사진 파일과 짝이다. reason 별로 세는 것이 측정 절차다\n" +
                "# 검출실패=no_plate · 분할실패=segment_fail · 흐림=blurry · 값있음=ok|low_conf\n" +
                "# 🔴 low_conf 는 값이 나오지만 틀릴 수 있다 — ok 와 따로 세라\n" +
                "# 🔑 reason=app:… 은 **앱 쪽 실패**다(인식기에 도달하지 못했다).\n" +
                "#    app:decode_failed=JPEG 디코딩 실패 · app:capture_failed=셔터·버퍼 실패\n" +
                "#    app:no_capture=카메라 미바인딩 · app:recognize_crashed=인식 중 예외\n" +
                "# ★ 줄 수와 사진 수를 대조하라 — 줄이 적으면 어느 갈래가 침묵한 것이다\n"

        /**
         * 남길 **장수**.
         *
         * ```
         * 실측(에뮬레이터 960x720 JPEG) : 한 장 **200.7 KB** → 50장 ≈ **10 MB**
         * 🔑 환산 : 한 요청이 최대 [ShutterGate.DEFAULT_MAX_ATTEMPTS] 장을 만든다
         *          → keep N = **N / 그 값 요청분**. 지금은 50/2 = **25 요청분**
         * ```
         * ⚠ **이 환산을 적어 두는 이유**: 재시도 상한이 바뀌면 "50장" 은 그대로인데
         * **요청분이 조용히 달라진다.** 장수만 적어 두면 그 낡음이 안 보인다.
         *
         * 서버 대장(링 100)과 다른 수인 것은 의도다: 이쪽 제약은 **폰 저장공간**이고
         * 서버 링과 아무 관계가 없다.
         *
         * ⚠ 200.7 KB 는 **에뮬레이터 장면 기준**이다. 실기 카메라는 장면이 복잡해 JPEG 이 더 클 수 있다.
         * 기본값을 올릴지는 **실제 게이트 통행량**을 봐야 안다 — `camera.shot_keep_files` 로 바꾼다.
         */
        const val DEFAULT_KEEP = 50
    }
}
