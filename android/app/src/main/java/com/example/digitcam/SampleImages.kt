package com.example.digitcam

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * 계약 §9.6(개정 3)의 정지 이미지 디버그 경로.
 *
 * 목적은 **폰도 차도 없이 인식기를 반복 개선하는 것**이다. 그래서 카메라 프레임과
 * 똑같은 모양(그레이 바이트 배열 + width/height/rowStride/rotation)으로 만들어
 * **같은 `nativeProcessGray`** 에 넣는다. 여기서 경로가 갈라지면 "샘플로는 되는데
 * 카메라로는 안 된다" 를 디버깅할 수 없게 되므로 의미가 없다.
 *
 * 이미지는 **루트가 `samples/plates/` 에 만든 것을 그대로 복사해 왔다**(개정 3).
 * 셋(android/cpp/socket)이 같은 이미지로 돌려야 결과를 비교할 수 있어서, 여기서 따로
 * 만들지 않는다. `expected.json` 도 같이 들어 있어 기대값을 화면에 띄운다 —
 * 17장을 한 장씩 넘겨 보려면 "이 장의 정답이 무엇인지" 가 화면에 있어야 한다.
 */
object SampleImages {

    private const val TAG = "SampleImages"
    private const val DIR = "samples"
    private const val EXPECTED_FILE = "expected.json"

    /** 그레이 버퍼 덤프가 쌓이는 곳. `getExternalFilesDir(null)/sample-dumps/` */
    const val DUMP_DIR = "sample-dumps"

    /** 분석 해상도와 비슷한 크기로 줄인다. 원본이 크면 프레임당 처리시간이 카메라 경로와 딴판이 된다. */
    private const val MAX_EDGE = 960

    data class Frame(
        /** `samples/` 기준 상대 경로. `variants/...` 를 포함한다(expected.json 키와 같은 형식). */
        val name: String,
        val gray: ByteArray,
        val width: Int,
        val height: Int,
        val rowStride: Int,
    ) {
        // ByteArray 를 담은 data class 라 equals/hashCode 를 직접 맞춘다(경고 회피 겸 오용 방지).
        override fun equals(other: Any?): Boolean = this === other
        override fun hashCode(): Int = System.identityHashCode(this)

        /**
         * 화면에 띄울 비트맵을 **그레이 배열에서 되만든다.**
         *
         * 원본 파일을 다시 디코딩하지 않는 것이 요점이다. 여기서 나오는 그림은 네이티브가
         * 실제로 받은 바이트 그 자체라, 휘도 변환이나 축소가 잘못되면 화면에서 바로 보인다.
         */
        fun toDisplayBitmap(): Bitmap {
            val px = IntArray(width * height)
            for (y in 0 until height) {
                val row = y * rowStride
                val out = y * width
                for (x in 0 until width) {
                    val v = gray[row + x].toInt() and 0xFF
                    px[out + x] = (0xFF shl 24) or (v shl 16) or (v shl 8) or v
                }
            }
            return Bitmap.createBitmap(px, width, height, Bitmap.Config.ARGB_8888)
        }
    }

    /**
     * `samples/` 아래의 이미지 전부. `variants/` 하위까지 훑어 상대 경로로 돌려준다.
     * 정렬해서 순서를 고정한다 — 매 실행마다 순서가 바뀌면 "3번 샘플" 이라는 말이 안 통한다.
     */
    fun list(context: Context): List<String> = buildList {
        addAll(listDir(context, DIR, prefix = ""))
    }.sorted()

    private fun listDir(context: Context, path: String, prefix: String): List<String> = try {
        val entries = context.assets.list(path).orEmpty()
        buildList {
            for (e in entries) {
                if (!e.endsWith(".png", ignoreCase = true) && !e.endsWith(".jpg", ignoreCase = true)) {
                    // 디렉터리면 한 단계 더 내려간다. expected.json 같은 파일은 여기서 걸러진다.
                    val childEntries = context.assets.list("$path/$e").orEmpty()
                    if (childEntries.isNotEmpty()) addAll(listDir(context, "$path/$e", "$prefix$e/"))
                    continue
                }
                add("$prefix$e")
            }
        }
    } catch (e: IOException) {
        Log.w(TAG, "샘플 목록을 읽지 못했다($path): ${e.message}")
        emptyList()
    }

    /**
     * `expected.json` 한 항목. 기대값과 **판정 규칙**을 함께 들고 있다.
     *
     * 규칙을 파일명으로 추측하면 안 된다(개정 4). 예전에는 `blur3` 라는 이름을 보고 "거절이 정답"
     * 이라고 화면에 띄웠는데, 그 라벨 자체가 틀려서 화면이 정답표와 어긋났다. 판정 근거는
     * 언제나 정답표 안에 있어야 한다.
     */
    data class Expectation(val value: String, val rule: Rule) {
        enum class Rule {
            /** 정확히 이 값이 나와야 한다. */
            EXACT,

            /** 이 값이 나오거나, 못 읽고 거절해도 정답(경계 케이스 — 약한 블러·원거리 등). */
            EXACT_OR_EMPTY,

            /** 빈 값이 정답. 읽으면 오검출이다(번호판 없음·강한 블러). */
            EMPTY,

            /** 정답표에 없는 규칙 문자열. 화면에는 원문을 그대로 보여 준다. */
            UNKNOWN,
        }
    }

    /**
     * `expected.json` 의 정답표. 상대경로 → [Expectation].
     * 없거나 깨졌으면 빈 맵 — 기대값 표시만 사라지고 나머지는 정상 동작한다.
     */
    fun loadExpected(context: Context): Map<String, Expectation> = try {
        val text = context.assets.open("$DIR/$EXPECTED_FILE").use { it.readBytes().toString(Charsets.UTF_8) }
        val plates = JSONObject(text).optJSONObject("plates") ?: JSONObject()
        buildMap {
            for (key in plates.keys()) {
                val o = plates.optJSONObject(key) ?: continue
                val rule = when (o.optString("rule", "exact").lowercase()) {
                    "exact" -> Expectation.Rule.EXACT
                    "exact_or_empty" -> Expectation.Rule.EXACT_OR_EMPTY
                    "empty" -> Expectation.Rule.EMPTY
                    else -> Expectation.Rule.UNKNOWN
                }
                put(key, Expectation(o.optString("value").orEmpty(), rule))
            }
        }
    } catch (e: Exception) {
        Log.w(TAG, "expected.json 을 읽지 못했다: ${e.message}")
        emptyMap()
    }

    /**
     * 샘플 하나를 카메라 프레임과 같은 형태로 디코딩한다.
     *
     * rowStride 는 width 와 같게 준다. 카메라 경로에서는 rowStride 가 width 보다 큰 경우가
     * 흔한데(정렬 때문), 네이티브가 두 경우를 모두 다뤄야 하므로 여기서 굳이 패딩을 흉내내지는
     * 않는다 — 패딩 처리 검증은 실제 카메라 프레임이 맡는다.
     */
    fun load(context: Context, name: String): Frame? {
        val bitmap = try {
            context.assets.open("$DIR/$name").use { BitmapFactory.decodeStream(it) }
        } catch (e: IOException) {
            Log.w(TAG, "샘플 '$name' 디코딩 실패: ${e.message}")
            null
        } ?: return null

        val scaled = downscale(bitmap)
        val w = scaled.width
        val h = scaled.height
        val pixels = IntArray(w * h)
        scaled.getPixels(pixels, 0, w, 0, 0, w, h)
        if (scaled !== bitmap) scaled.recycle()
        bitmap.recycle()

        // ITU-R BT.601 휘도. Bitmap.Config.ALPHA_8 로 뽑으면 알파 채널이지 휘도가 아니라서
        // 흰 배경/검은 글자 이미지가 통째로 한 색으로 나온다 — 반드시 직접 계산한다.
        val gray = ByteArray(w * h)
        for (i in pixels.indices) {
            val p = pixels[i]
            val r = (p shr 16) and 0xFF
            val g = (p shr 8) and 0xFF
            val b = p and 0xFF
            gray[i] = ((r * 299 + g * 587 + b * 114) / 1000).toByte()
        }

        // 인식이 실패했을 때 "네이티브가 못 읽은 것" 과 "애초에 이상한 바이트를 보낸 것" 을
        // 가르는 최소 증거. 번호판 샘플이면 평균이 중간값 근처이고 min/max 가 벌어져 있어야 한다.
        // 전부 0 이거나 폭이 좁으면 디코딩·휘도 변환 쪽을 먼저 의심하라.
        logGrayStats(name, gray, w, h)

        return Frame(name = name, gray = gray, width = w, height = h, rowStride = w)
    }

    /**
     * 네이티브에 넘기는 **바로 그 그레이 버퍼**를 PGM(P5)으로 떨군다.
     *
     * 왜 필요한가 — 인식이 실패했을 때 "인식기가 못 읽었다" 와 "애초에 다른 바이트를 먹였다" 는
     * 통계만으로는 구분되지 않는다. 평균·최소·최대가 그럴듯해도 화소가 1씩 다를 수 있고,
     * 실제로 앱(절삭)과 데스크톱 하네스(PIL `convert("L")`, 반올림)의 BT.601 구현이 달라
     * 화소 대부분이 1 차이가 난다는 것이 확인됐다. 같은 코어인데 하네스는 통과하고 앱은
     * 실패한다면 둘 중 하나가 실제 조건을 못 담고 있다는 뜻이므로, **재변환 없이 이 버퍼
     * 그대로** 내보내야 비교가 성립한다.
     *
     * PGM 을 고른 이유는 헤더 세 줄 뒤에 원본 바이트가 그대로 이어져서, 받는 쪽이 어떤
     * 이미지 라이브러리를 쓰든 디코딩 과정에서 값이 변형될 여지가 없기 때문이다.
     *
     * @return 쓴 파일. 외부 저장소를 못 쓰거나 실패하면 null.
     */
    fun dumpPgm(context: Context, frame: Frame): File? {
        val dir = context.getExternalFilesDir(null)?.let { File(it, DUMP_DIR) } ?: return null
        return try {
            dir.mkdirs()
            // "variants/a.png" → "variants__a.pgm" (경로 구분자를 파일명에 녹인다)
            val flat = frame.name.replace('/', '_').substringBeforeLast('.') + ".pgm"
            val out = File(dir, flat)
            out.outputStream().buffered().use { s ->
                s.write("P5\n${frame.width} ${frame.height}\n255\n".toByteArray(Charsets.US_ASCII))
                if (frame.rowStride == frame.width) {
                    s.write(frame.gray, 0, frame.width * frame.height)
                } else {
                    // 카메라 프레임처럼 패딩이 있는 경우엔 행마다 폭만큼만 잘라 쓴다.
                    for (y in 0 until frame.height) {
                        s.write(frame.gray, y * frame.rowStride, frame.width)
                    }
                }
            }
            Log.i(TAG, "그레이 버퍼 덤프: ${out.absolutePath}")
            out
        } catch (e: IOException) {
            Log.w(TAG, "덤프 실패(${frame.name}): ${e.message}")
            null
        }
    }

    private fun logGrayStats(name: String, gray: ByteArray, w: Int, h: Int) {
        var min = 255
        var max = 0
        var sum = 0L
        for (b in gray) {
            val v = b.toInt() and 0xFF
            if (v < min) min = v
            if (v > max) max = v
            sum += v
        }
        val mean = if (gray.isNotEmpty()) sum / gray.size else 0
        Log.i(TAG, "샘플 '$name' ${w}x$h · gray mean=$mean min=$min max=$max")
    }

    private fun downscale(src: Bitmap): Bitmap {
        val longest = maxOf(src.width, src.height)
        if (longest <= MAX_EDGE) return src
        val ratio = MAX_EDGE.toFloat() / longest
        val w = (src.width * ratio).toInt().coerceAtLeast(1)
        val h = (src.height * ratio).toInt().coerceAtLeast(1)
        return Bitmap.createScaledBitmap(src, w, h, true)
    }
}
