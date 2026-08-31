package com.example.digitcam

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log

/**
 * **촬영본 파일 → 인식기 입력.** 프레임워크(`Bitmap`)에 닿는 부분만 여기 모았다.
 *
 * 산술은 [GrayConvert] 에 있다 — assets 샘플과 **같은 변환기**를 타야 *"샘플로는 되는데
 * 촬영본으로는 안 된다"* 를 디버깅할 수 있다.
 *
 * ## 회전을 여기서 하지 않는다
 *
 * `Bitmap` 을 돌리지 않고 **회전각을 그대로 인식기에 넘긴다**(`nativeProcessGray` 의
 * `rotationDegrees`). 프리뷰 경로가 `imageProxy.imageInfo.rotationDegrees` 를 그렇게
 * 넘기고 있으므로 두 경로가 같아진다. 🔑 여기서 비트맵을 돌리면 **회전이 두 번** 일어난다.
 *
 * ## 🔴 회전각은 **파일이 아니라 `imageInfo` 에서 온다** — 그것이 이 설계의 전제다
 *
 * ```
 * 촬영 : takePicture(OnImageCapturedCallback) → image.imageInfo.rotationDegrees
 *        🔑 **메모리에서 받으므로 EXIF 를 안 거친다**
 * `BitmapFactory` 는 **EXIF orientation 을 무시한다** — `cv::imread` 는 **적용한다**
 * ```
 * 🔴 **그래서 파일에서 읽는 경로를 만들면 회전이 통째로 빠진다** — `BitmapFactory` 가 무시하고
 * 파일에는 `imageInfo` 가 없다. **`fromFile` 을 그 이유로 지웠다**(호출부 0 · 2026-08-21).
 * 다시 필요해지면 **`ExifInterface` 로 orientation 을 읽어 각도를 인자로 받게** 만들어라.
 *
 * ⚠ **다만 EXIF 각도를 그대로 쓰는 것이 정답이라고 가정하지 마라.** 실촬영 15장에서
 * 그렇게 했더니 정답이 **1/15 → 0/15 로 떨어졌다**(2026-08-24 실측). EXIF 는 *폰의 위* 를
 * 말하고 우리가 필요한 것은 *번호판의 위* 인데, **판이 평면에 누워 있으면 그 둘이 무관하다.**
 * 🔑 각도를 인자로 받는 구조는 맞다. 그 인자에 **무엇을 넣을지는 자료로 정해라.**
 *
 * ⚠ **그리고 저장한 파일을 남에게 넘길 때 회전각을 같이 넘겨야 한다** —
 * 받는 쪽 도구가 EXIF 를 적용하면(`cv::imread`) **우리와 다른 픽셀을 본다.**
 *
 * ⚠ 이 파일은 `Bitmap`·`BitmapFactory` 에 의존하므로 **JVM 단위 시험에서 돌지 않는다**
 * (스텁이 null 을 돌려준다). 그래서 로직을 최소로 두고 판단은 전부 [GrayConvert] 와
 * [ShutterGate] 로 밀어냈다 — 그 둘은 시험된다.
 */
object PlateImage {

    private const val TAG = "PlateImage"

    /** 인식기에 넘길 한 장. `nativeProcessGray` 의 인자 모양 그대로다. */
    data class Gray(
        val gray: ByteArray,
        val width: Int,
        val height: Int,
        val rowStride: Int,
    ) {
        // ByteArray 를 담은 data class — 오용을 막기 위해 동일성 비교로 고정한다.
        override fun equals(other: Any?): Boolean = this === other
        override fun hashCode(): Int = System.identityHashCode(this)
    }

    /** JPEG 바이트를 바로 변환한다(파일로 쓰기 전에도 쓸 수 있다). */
    fun fromJpeg(bytes: ByteArray, label: String, maxEdge: Int = GrayConvert.activeMaxEdge): Gray? {
        val bitmap = try {
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        } catch (e: Exception) {
            Log.w(TAG, "JPEG 디코딩 실패($label): ${e.message}")
            null
        }
        return bitmap?.let { convert(it, label, maxEdge) }
    }

    private fun convert(bitmap: Bitmap, label: String, maxEdge: Int): Gray? {
        if (bitmap.width <= 0 || bitmap.height <= 0) {
            Log.w(TAG, "빈 비트맵($label) ${bitmap.width}x${bitmap.height}")
            bitmap.recycle()
            return null
        }
        // 🔴 **촬영본의 원본 크기와 종횡비**(REQ-0331 · 재기만 한다. 안 고친다).
        //
        // 왜 : `ImageCapture` 는 종횡비를 안 맞췄다 — A 의 목적(프리뷰↔오버레이) 밖이었다.
        //      그런데 **촬영본이 분석·프리뷰와 다른 비율이면 프리뷰에서 보이던 판이
        //      촬영본에서 잘린다.** 그러면 인식률 기준선이 그 잘림을 안고 측정된다.
        // 🔑 **재는 것은 고치는 것이 아니다.** 모르는 채로 재면 그 값을 나중에 못 쓴다 —
        //    같으면 그 축이 없고, 다르면 **기준선에 분모로 박고** 손잡이 판단에서 뺀다.
        Log.i(
            TAG,
            "촬영본 원본 '$label' ${bitmap.width}x${bitmap.height} " +
                "종횡비=${"%.3f".format(GrayConvert.aspectOf(bitmap.width, bitmap.height))} " +
                "(분석·프리뷰는 4:3=1.333)",
        )
        val target = GrayConvert.scaledSize(bitmap.width, bitmap.height, maxEdge)
        val scaled =
            if (target.width == bitmap.width && target.height == bitmap.height) bitmap
            else Bitmap.createScaledBitmap(bitmap, target.width, target.height, true)

        val w = scaled.width
        val h = scaled.height
        val pixels = IntArray(w * h)
        scaled.getPixels(pixels, 0, w, 0, 0, w, h)
        if (scaled !== bitmap) scaled.recycle()
        bitmap.recycle()

        val gray = GrayConvert.luminance(pixels)
        val st = GrayConvert.stats(gray)
        // 인식이 실패했을 때 "인식기가 못 읽었다" 와 "이상한 바이트를 먹였다" 를 가르는 최소 증거.
        Log.i(TAG, "촬영본 '$label' ${w}x$h · gray mean=${st.mean} min=${st.min} max=${st.max}" +
            if (st.looksFlat) "  ⚠ 폭이 좁다 — 디코딩·휘도 변환을 먼저 의심하라" else "")

        return Gray(gray = gray, width = w, height = h, rowStride = w)
    }
}
