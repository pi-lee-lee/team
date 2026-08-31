package com.example.digitcam

import android.util.Log
import org.json.JSONObject

/**
 * 계약 §5(개정 2)의 인식 결과. C++ 가 JSON 으로 만들고 여기서 읽는다.
 *
 * 계약이 "키를 빼지 마라" 라고 못박았지만 파서는 그 약속을 신뢰하지 않는다. 계약 §5 자체가
 * **"모르는 키는 무시하고, 없는 키는 기본값"** 을 요구한다 — 개정 1→2 에서 이미 키가 통째로
 * 갈아엎였고(`digits`/`boxes` 삭제, 8개 추가) 또 늘어날 수 있기 때문이다.
 * 그래서 키마다 기본값을 두고, 원본 [raw] 도 들고 있는다.
 */
data class DigitResult(
    /** 번호판 문자열 `123가4568`. 못 읽으면 "". **한글이 들어간다.** */
    val value: String,
    /**
     * 🔴 **이 값을 읽은 경로.** `template` | `ocr` | `none` (cpp `0bb7efa`).
     *
     * ## 왜 필수인가 — **`conf` 한 열에 눈금이 둘 섞여 있다**
     *
     * ```
     * template : 문자별 NCC 신뢰도의 **최소값**   · 문턱 `vision.min_confidence`     0.70
     * ocr      : CTC 문자 확률의 **평균**         · 문턱 `vision.ocr_min_confidence` 0.85
     * ```
     * 🔴 경로를 안 적으면 표가 이렇게 읽힌다 — *"정답인데 conf 0.73 이 다섯 장이나 된다,
     * 문턱이 너무 높은 것 아닌가"*. **그 다섯은 전부 template 이고 그 눈금에서 0.73 은 낮지 않다.**
     * 그 오독으로 문턱을 내리면 012 의 **틀린 판독(0.82 · `123다9828`)** 이 나간다.
     *
     * ★ §"숫자 둘을 비교하기 전에 — 누가 만들었나 · 어느 좌표계인가" 가 **한 열 안에서** 일어난 자리다.
     * 🔑 그래서 기록에서 `conf` **바로 앞**에 붙인다. 떨어뜨려 놓으면 사람이 다시 한 열로 센다.
     */
    val engine: String,
    val conf: Double,
    val stable: Boolean,
    val fresh: Boolean,
    /** 움직임 게이트 판정. true 면 인식을 건너뛴 프레임이다. */
    val moving: Boolean,
    /** 프레임 간 평균 차분(0~255). 임계값 튜닝용으로 화면에 띄운다. */
    val motion: Double,
    /** 번호판 사각형을 찾았는가. */
    val plate: Boolean,
    /** 번호판 ROI 라플라시안 분산. 초점/모션블러 지표. */
    val sharp: Double,
    /** `new`(3+한글+4) · `old`(2+한글+4) · `unknown` */
    val plateFormat: String,
    /** `ok`·`moving`·`no_plate`·`blurry`·`segment_fail`·`low_conf` — 어디서 막혔는지. */
    val reason: String,
    /** 번호판 네 꼭짓점. **회전 보정된 프레임** 좌표계라 오버레이에 그대로 쓸 수 있다. */
    val quad: List<Point>,
    /** 회전 보정 후 프레임 크기. [quad] 의 기준. */
    val frameWidth: Int,
    val frameHeight: Int,
    val ms: Double,
    /**
     * 파싱 원본. 계약이 또 늘어도 파서를 고치지 않고 새 키를 꺼내 볼 수 있다.
     *
     * `chars` 를 여기에서만 꺼내 쓰는 이유: `chars[].box` 는 **정규화된 번호판 좌표계**라
     * 프레임 좌표계인 오버레이에 겹칠 수 없다(계약 §5·§8). 그려 봐야 엉뚱한 위치에 나오므로
     * 전용 필드로 승격시키지 않았다.
     */
    val raw: JSONObject?,
) {
    data class Point(val x: Int, val y: Int)

    /** 화면에 값을 띄울 수 있는 상태인가. */
    val hasValue: Boolean get() = value.isNotEmpty()

    companion object {
        private const val TAG = "DigitResult"

        val EMPTY = DigitResult(
            value = "", engine = "none", conf = 0.0, stable = false, fresh = false,
            moving = false, motion = 0.0, plate = false, sharp = 0.0,
            plateFormat = "unknown", reason = "", quad = emptyList(),
            frameWidth = 0, frameHeight = 0, ms = 0.0, raw = null,
        )

        /** 파싱 실패 시 [EMPTY] 를 돌려준다. 예외를 위로 던지지 않는다. */
        fun parse(json: String): DigitResult = try {
            val o = JSONObject(json)
            DigitResult(
                value = o.optString("value", ""),
                // 네이티브가 아직 안 주는 빌드면 "none" 이다 — 그것도 정보다(경로를 모른다는 뜻).
                engine = o.optString("engine", "none"),
                conf = o.optDouble("conf", 0.0),
                stable = o.optBoolean("stable", false),
                fresh = o.optBoolean("fresh", false),
                moving = o.optBoolean("moving", false),
                motion = o.optDouble("motion", 0.0),
                plate = o.optBoolean("plate", false),
                sharp = o.optDouble("sharp", 0.0),
                plateFormat = o.optString("format", "unknown"),
                reason = o.optString("reason", ""),
                quad = parseQuad(o),
                frameWidth = o.optInt("w", 0),
                frameHeight = o.optInt("h", 0),
                ms = o.optDouble("ms", 0.0),
                raw = o,
            )
        } catch (e: Exception) {
            Log.w(TAG, "결과 JSON 파싱 실패: ${e.message} · 원문=${json.take(200)}")
            EMPTY
        }

        /** `quad` 는 `[[x,y] × 4]`. 4점이 아니면 그리지 않는다(반쪽 사각형이 더 헷갈린다). */
        private fun parseQuad(o: JSONObject): List<Point> {
            val arr = o.optJSONArray("quad") ?: return emptyList()
            val out = ArrayList<Point>(arr.length())
            for (i in 0 until arr.length()) {
                val p = arr.optJSONArray(i) ?: continue
                if (p.length() < 2) continue
                out.add(Point(p.optInt(0), p.optInt(1)))
            }
            return if (out.size == 4) out else emptyList()
        }
    }
}
