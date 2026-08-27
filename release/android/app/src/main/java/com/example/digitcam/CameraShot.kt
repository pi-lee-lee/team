package com.example.digitcam

/**
 * 카메라 pull 전선. **정본은 `docs/net/SPEC-camera-pull.md` §4 이고 여기서 형식을 바꾸지 않는다.**
 *
 * ```
 * 서버 → 폰 :  SHOOT,<shot_id>\n
 * 폰 → 서버 :  {"shot":<shot_id>,"value":"12가3456","device":"..."}   성공
 *              {"shot":<shot_id>,"error":"<사유>"}                     실패
 * ```
 *
 * `shot_id` 는 `yyyymmdd` × 10000 + seq 라 **12자리다. `Int` 로 담으면 넘친다** — `Long` 이다.
 *
 * `org.json` 을 쓰지 않는다. 스키마가 두 줄로 고정이고, 순수 Kotlin 이면 이 파일이
 * JVM 단위 시험에서 그대로 돌기 때문이다(Android 프레임워크의 `org.json` 은 단위 시험
 * 클래스패스에서 기본값만 돌려주는 껍데기로 바뀐다). 대신 [jsonEscape] 를 직접 지킨다.
 *
 * 상행 push(`{"value","conf",...}`)는 이 파일이 만들지 않는다 — `TcpSender` 가 그대로 쓴다.
 */
object CameraShot {

    /** 하행 줄의 머리. 이것으로 시작하지 않는 줄은 [Downlink.Unknown] 이다. */
    const val DOWNLINK_PREFIX = "SHOOT,"

    /**
     * 링크 유지용 하행. `PING,<seq>` (socket 확정 2026-08-25 · 서버 → 폰 · 30초 · **응답 없음**).
     *
     * ## 왜 있나 — **유휴가 연결을 끊는다**
     *
     * 이 앱은 차가 없으면 **아무것도 안 보낸다.** 그 유휴에서 연결이 끊기는 것을 실측했다
     * (103초 뒤 `ECONNABORTED`). 그러면 끊긴 사이의 촬영 요청이 **발급되지 않는다.**
     * 🔑 서버가 주기적으로 보내면 NAT 매핑이 갱신되고, **앱은 고칠 것이 없다** —
     * 모르는 하행을 무시하는 기존 규약([Downlink.Unknown])만으로 이미 안전했다.
     *
     * ⚠ 그런데 무시만 하면 30초마다 "모르는 줄" 로그가 쌓인다. 그래서 갈래를 따로 뒀다.
     */
    const val PING_PREFIX = "PING,"

    /**
     * 번호판 자리의 **표지**. 값이 아니다 — 서버는 이 문자를 번호판으로 받으면 거절하고
     * 실패로 기록한다(명세 §2). 그래서 인식값이 우연히 이것이면 성공으로 올리지 않는다.
     */
    const val PLATE_MARKER = "-1"

    // ---- 실패 사유 어휘 -----------------------------------------------------
    // 명세 §4 는 `error` 키만 못박고 사유 문자열은 열어 뒀다. 서버는 사유를 해석하지 않고
    // (§3 에서 CAM_FAILED 하나로 접힌다) 기록만 하므로, 여기서 정하고 문서에 적는다.
    // 🔴 새 사유를 추가하면 `docs/android/SPEC-camera-pull-app.md` 의 표도 같이 고친다.

    /** 카메라 권한이 없다. 사람이 허용해야 풀린다 — 다시 요청해도 같은 답이다. */
    const val REASON_NO_PERMISSION = "no_camera_permission"

    /** 카메라가 안 열렸거나 분석이 안 돌고 있다. 일시적일 수 있다. */
    const val REASON_CAMERA_UNAVAILABLE = "camera_unavailable"

    /**
     * 셔터나 파일 저장이 실패했다. **즉시 실패다** — 기다려도 안 된다.
     * 고칠 곳: 저장공간 · 카메라 하드웨어.
     */
    const val REASON_CAPTURE_FAILED = "capture_failed"

    /**
     * **촬영 콜백이 안 왔다.** [REASON_CAPTURE_FAILED] 와 갈라 둔다 — 저쪽은 *"실패했다고
     * 알려 왔다"* 이고 이쪽은 ***"아무 말도 없다"*** 다. **고칠 곳이 다르다**(앞은 저장공간·권한,
     * 뒤는 카메라 스택 자체가 죽은 것이라 앱 재시작이 필요할 수 있다).
     *
     * 🔑 이 사유가 나오면 **워치독이 짖은 것**이다. 드물어야 정상이고, 자주 나오면
     * `DEFAULT_CAPTURE_WATCHDOG_MS` 가 작은 것이지 카메라가 죽은 것이 아닐 수 있다.
     */
    const val REASON_CAPTURE_STUCK = "capture_stuck"

    /**
     * **찍었는데 못 읽었다.** 인식이 끝났고 결과가 비었거나 형식이 안 맞았다.
     * 고칠 곳: 초점 · 조명 · 각도 · 인식기.
     *
     * ⚠ **이건 이제 "그 밖의 실패" 다.** 네이티브가 사유를 알려 주면 [reasonFromNative] 가
     * 더 정확한 이름으로 바꾼다. 여기 남는 것은 **네이티브가 아무 말도 안 한 경우**뿐이다.
     */
    const val REASON_RECOGNIZE_FAILED = "recognize_failed"

    /**
     * 대기 상한을 넘겨 밀려났다. **앞선 요청이 아직 안 끝났는데 새 요청이 계속 들어온** 것이다.
     */
    const val REASON_TOO_MANY_PENDING = "too_many_pending"

    // 🔑 `PLATE_MARKER` 는 실패 사유가 아니다. 인식값이 그 문자면 **성공으로 올리지 않고**
    //    못 읽은 것으로 다룬다. 번호판 형식(숫자+한글+숫자)에서 "-1" 은 나올 수 없으므로
    //    이 갈래는 안전장치이고, 안 쓰일 사유를 어휘에 늘리지 않는다.

    /**
     * 네이티브가 준 `reason` 을 **그대로** 전선 사유로 쓴다. 계약 §5.2 의 어휘다.
     *
     * ## 🔴 왜 새 이름을 만들지 않았나
     *
     * 계약서가 이미 `moving`·`no_plate`·`blurry`·`segment_fail`·`low_conf` 를 정해 뒀고
     * **cpp 가 그 값을 실제로 낸다.** 여기서 `no_plate_found` 같은 새 이름으로 바꾸면
     * **같은 것에 이름이 둘이 되고**, 로그(네이티브 이름)와 화면(앱 이름)이 갈린다.
     *
     * > ★ **어휘를 늘리는 것보다 있는 어휘를 나르는 것이 낫다.**
     *
     * ⚠ 모르는 값이 오면 [REASON_RECOGNIZE_FAILED] 로 접는다 — **조용히 통과시키지 않는다.**
     * cpp 가 새 사유를 늘리면 여기 목록에 안 들어 있어 접히므로, **그때 이 집합을 같이 늘려야 한다.**
     * 🔑 그리고 web 이 사유마다 문구를 다므로 **늘리면 web 에 알려야 한다** — 모르는 코드는
     * 화면에서 "실패" 로 뭉개진다.
     */
    val NATIVE_REASONS: Set<String> = setOf("moving", "no_plate", "blurry", "segment_fail", "low_conf")

    /** 네이티브 사유 → 전선 사유. 아는 값만 통과시킨다. */
    fun reasonFromNative(native: String): String =
        if (native in NATIVE_REASONS) native else REASON_RECOGNIZE_FAILED

    /** 앱이 보내는 사유 전부. 새 사유를 추가하면 여기에도 넣는다 — 시험이 이 목록으로 검사한다. */
    val APP_REASONS: Set<String> = setOf(
        REASON_NO_PERMISSION,
        REASON_CAMERA_UNAVAILABLE,
        REASON_TOO_MANY_PENDING,
        REASON_CAPTURE_FAILED,
        REASON_CAPTURE_STUCK,
        REASON_RECOGNIZE_FAILED,
    ) + NATIVE_REASONS

    /**
     * 🔴 **서버가 스스로 만드는 사유. 앱은 이것을 보내지 않는다.**
     *
     * 동작은 같지만(둘 다 `CAM_FAILED`) **대장에서 출처가 섞인다** — 서버가 접은 것과 폰이
     * 보고한 것을 나중에 구별할 수 없다. 새 사유를 지을 때 이 낱말을 피한다.
     *
     * 정본은 `docs/net/SPEC-camera-pull.md` §4.1 이다.
     */
    val SERVER_RESERVED_REASONS: Set<String> = setOf(
        "empty_plate",     // 번호판 값이 비어 실패로 접었다
        "sentinel_plate",  // 번호판 자리에 PLATE_MARKER 가 와서 거절했다
    )

    /** 하행 한 줄의 해석 결과. */
    sealed interface Downlink {
        /** 촬영 요청. */
        data class Shoot(val shotId: Long) : Downlink

        /**
         * [DOWNLINK_PREFIX] 로 시작하는데 번호가 아니다. 응답할 대상(`shot_id`)이 없으므로
         * **답할 수 없다** — 세고 로그만 남긴다.
         */
        data class Malformed(val line: String) : Downlink

        /**
         * 링크 유지 신호. **답하지 않는다.**
         *
         * 🔑 [seq] 를 들고 다니는 이유는 **유실을 셀 수 있게** 하려는 것이다(socket 이 그러라고 넣었다).
         * 번호가 건너뛰면 그 사이 줄이 안 온 것이고, 그것이 **링크 품질의 유일한 지표**다 —
         * 이 앱은 평소에 아무것도 안 보내므로 다른 관측점이 없다.
         * ⚠ 번호는 **서버 기동 때 1 부터 다시 센다.** 줄어들면 유실이 아니라 **서버 재기동**이다.
         */
        data class Ping(val seq: Long) : Downlink

        /**
         * 우리가 모르는 줄. **무시한다 = 전방 호환.** 서버가 나중에 다른 하행을 늘려도
         * 앱이 깨지지 않는다.
         */
        data class Unknown(val line: String) : Downlink
    }

    /**
     * 하행 한 줄을 해석한다. 줄 끝 `\r` 과 앞뒤 공백은 여기서 턴다 — 서버가 `\n` 만
     * 보내지만 중간에 무엇이 끼어도 번호는 읽혀야 한다.
     */
    fun parseDownlink(rawLine: String): Downlink {
        val line = rawLine.trim()
        if (line.startsWith(PING_PREFIX)) {
            val seq = line.substring(PING_PREFIX.length).trim().toLongOrNull()
            // 번호가 깨졌어도 **PING 인 것은 맞다.** 링크 유지라는 목적은 그대로 이뤄졌으므로
            // 모르는 줄로 떨어뜨리지 않는다. 유실 계산만 못 할 뿐이라 -1 로 표시한다.
            return Downlink.Ping(seq ?: -1L)
        }
        if (!line.startsWith(DOWNLINK_PREFIX)) return Downlink.Unknown(line)
        val idText = line.substring(DOWNLINK_PREFIX.length).trim()
        val id = idText.toLongOrNull()
        // 0 이나 음수는 발급될 수 없다(yyyymmdd × 10000 + seq). 그런 값이 오면 깨진 줄이다.
        if (id == null || id <= 0L) return Downlink.Malformed(line)
        return Downlink.Shoot(id)
    }

    /** 성공 응답 한 줄(LF 포함). [plate] 는 비어 있지 않고 [PLATE_MARKER] 도 아니어야 한다. */
    fun encodeSuccess(shotId: Long, plate: String, device: String): String =
        """{"shot":$shotId,"value":"${jsonEscape(plate)}","device":"${jsonEscape(device)}"}""" + "\n"

    /** 실패 응답 한 줄(LF 포함). 사유는 위 `REASON_*` 중 하나다. */
    fun encodeError(shotId: Long, reason: String): String =
        """{"shot":$shotId,"error":"${jsonEscape(reason)}"}""" + "\n"

    /**
     * JSON 문자열 이스케이프. **번호판에는 한글이 들어간다**(`12가3456`) — 비 ASCII 는
     * `\uXXXX` 로 좁히지 않고 원문 UTF-8 로 내보낸다(명세가 둘 다 허용하고, 우리 전선은
     * 전부 UTF-8 이다). 반드시 처리해야 하는 것은 `"` · `\` · 제어문자다.
     *
     * 제어문자를 그대로 흘리면 **줄 안에 LF 가 들어가 한 줄이 두 줄로 쪼개진다** —
     * 받는 쪽 파서가 반쪽짜리 JSON 을 보게 되는 가장 조용한 실패다.
     */
    fun jsonEscape(s: String): String {
        val sb = StringBuilder(s.length + 8)
        for (ch in s) {
            when {
                ch == '"' -> sb.append("\\\"")
                ch == '\\' -> sb.append("\\\\")
                ch == '\n' -> sb.append("\\n")
                ch == '\r' -> sb.append("\\r")
                ch == '\t' -> sb.append("\\t")
                ch < ' ' || ch == '\u007f' -> sb.append(String.format("\\u%04x", ch.code))
                else -> sb.append(ch)
            }
        }
        return sb.toString()
    }
}
