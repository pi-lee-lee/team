package com.example.digitcam

import android.content.Context
import android.util.Log

/**
 * 표준 인식 경로의 가중치를 **assets 에서 바이트로** 읽어 온다 (REQ-0374).
 *
 * ## 🔴 왜 바이트인가 — assets 는 파일이 아니다
 *
 * assets 는 APK 안에 들어가서 **파일시스템 경로가 없다.** 그래서 네이티브가
 * `cv::dnn::readNetFromONNX(const String& path)` 로 열 수 없다. 길이 둘이었고
 * 바이트를 골랐다(cpp 와 합의 · [DigitVision.nativeLoadModels] 주석에 근거):
 *
 * ```
 * A. getFilesDir() 로 복사하고 경로를 넘긴다  → 디스크에 한 벌 더 · 복사 실패 갈래 · 첫 실행 지연
 * B. 바이트로 넘긴다                          → 복사 없음 · 경로 없음 · 실패 갈래 하나(읽기)
 * ```
 *
 * ## 파일은 저장소에 **한 벌만** 있다
 *
 * 원본은 `cpp/digitcam/models/`(cpp 소유)에 있고, `build.gradle.kts` 의 `copyOcrModels` 가
 * 빌드 때 assets 로 복사한다. **여기에 원본을 두 벌 두지 않는다** — 두 벌이 갈리면
 * *"호스트 하니스는 되는데 앱은 안 된다"* 가 되고 그 원인을 값으로 못 가른다.
 *
 * ⚠ **없으면 null 이다. 그것은 오류가 아니다.** 모델이 없으면 네이티브가 예전 템플릿
 * 경로로만 돈다(cpp 규약). 실패를 치명적으로 다루지 마라 — 다만 **조용히 넘어가지도 마라.**
 * "모델이 있는 줄 알았는데 없는" 상태가 인식률 표를 통째로 오독하게 만든다.
 */
object OcrModels {

    private const val TAG = "OcrModels"
    private const val DIR = "models"

    private const val DET = "ppocr_det_v4.onnx"
    private const val REC = "korean_rec_static.onnx"
    private const val DICT = "korean_dict.txt"

    /** 세 파일을 한 묶음으로. 셋이 다 있어야 의미가 있으므로 부분 성공을 만들지 않는다. */
    class Bundle(val det: ByteArray, val rec: ByteArray, val dict: ByteArray) {
        val totalBytes: Int get() = det.size + rec.size + dict.size
    }

    /**
     * **IO 스레드에서 불러라.** 7 MiB 가 넘는다 — 메인에서 읽으면 첫 화면이 그만큼 늦는다.
     * 실제로는 vision executor 가 부른다([VisionEngine.start] 의 provider).
     */
    fun load(context: Context): Bundle? {
        val det = read(context, DET) ?: return null
        val rec = read(context, REC) ?: return null
        val dict = read(context, DICT) ?: return null
        Log.i(
            TAG,
            "모델 읽음 — det ${det.size}B · rec ${rec.size}B · dict ${dict.size}B " +
                "· 합계 ${"%.2f".format((det.size + rec.size + dict.size) / 1048576.0)} MiB",
        )
        return Bundle(det, rec, dict)
    }

    private fun read(context: Context, name: String): ByteArray? = try {
        context.assets.open("$DIR/$name").use { it.readBytes() }
    } catch (e: Exception) {
        // 🔑 어느 파일이 없는지 이름을 찍는다. "모델 없음" 만 찍으면 셋 중 무엇인지 못 가른다.
        Log.w(TAG, "모델 '$name' 을 못 읽었다: ${e.message} — 템플릿 경로로만 돈다")
        null
    }
}
