package com.example.digitcam

/**
 * 이미지 → 네이티브에 넘길 그레이 버퍼로 바꾸는 **산술 부분만** 모았다.
 *
 * ## 왜 따로 있나
 *
 * 인식 입력이 두 경로로 들어온다 — **assets 샘플**과 **촬영본 파일**. 두 경로가 각자
 * 휘도 변환을 하면 *"샘플로는 되는데 촬영본으로는 안 된다"* 를 디버깅할 수 없다.
 * (`SampleImages` 의 주석이 카메라 경로에 대해 이미 그 규율을 적어 뒀다.)
 * 그래서 변환은 한 곳에만 둔다.
 *
 * ## Android 의존이 없다 — 그래서 시험된다
 *
 * `Bitmap` 을 만지는 부분은 [PlateImage] 에 있고 여기는 **`IntArray` → `ByteArray`** 뿐이다.
 * 휘도 계수가 틀어지면 인식이 통째로 망가지는데, 그 종류의 결함은 단위 시험으로만 잡힌다.
 */
object GrayConvert {

    /**
     * 긴 변 상한. 원본이 크면 프레임당 처리시간이 카메라 경로와 딴판이 되므로 줄인다.
     * (분석 프레임은 640×480 근처로 들어온다.)
     *
     * ## 🔴 이 값은 **인식률 손잡이다** — 960 → 1600 으로 올렸다 (2026-08-24 앱 실측)
     *
     * ```
     * MAX_EDGE  960 : 실촬영 15장 정답 11 · 오답 0 · 합성 26/26
     * MAX_EDGE 1600 : 실촬영 15장 정답 **12** · 오답 0 · 합성 26/26   ← 바뀐 것은 004 한 장뿐
     * 비용          : 실촬영 1장 처리 **344.8 ms**(에뮬 arm64 · 1600×900 · OCR 경로)
     * ```
     * 🔑 **비용이 프리뷰에 안 실린다.** 이 상한은 [SampleImages] 와 [PlateImage] 만 쓴다 —
     * 실시간 분석은 `ImageAnalysis` 프레임(640×480)을 **그대로** 받으므로 이 값과 무관하다.
     * 즉 대가는 *정지 이미지 한 장* 의 지연이고, 셔터 경로에서는 그만큼 기다려도 된다.
     *
     * ## ⚠ 이 결론은 **예전과 반대다** — 뒤집힌 이유를 적어 둔다
     *
     * ```
     * 옛 결론(2026-08-21) : "인식률 손잡이가 아니다. 960 → 4032 로 올려도 40칸 한 칸도 안 바뀜"
     *                        근거는 참이었다. 다만 **그때의 인식기가 템플릿 매칭**이었다
     * 왜 뒤집혔나          : 인식기가 **CTC(분할 없이 한 줄을 읽는 방식)** 로 바뀌었다.
     *                        템플릿은 정규화판(520×110)에서 셀을 잘라 대조하므로 원본 해상도가
     *                        묻혔고, CTC 는 **판 crop 의 실제 화소**를 그대로 읽어서 안 묻힌다
     * ```
     * ★ **근거가 틀린 것이 아니라 조건이 바뀌었다**(`docs/android/LEDGER.md` §13.2 와 같은 부류).
     * 인식기를 다시 갈아끼우면 **이 값을 다시 재라.** 결론을 옮겨 적지 마라.
     *
     * ⚠ 그리고 문자 셀 크기 이야기(정규화판 520×110 · 템플릿 24×32)는 **템플릿 경로에만** 해당한다.
     * 두 경로가 같이 도는 지금은(템플릿 먼저 → 실패하면 CTC) 그 설명이 절반만 맞다.
     */
    const val DEFAULT_MAX_EDGE = 1600

    /**
     * 🔴 **메모리 압력을 받으면 여기로 내려간다** (REQ-0407 방어).
     *
     * ★ **죽는 것보다 낮은 해상도로 사는 것이 낫다** — 사용자 요구가 "인식률" 인데
     * 앱이 죽으면 인식률은 **0** 이다. 이 값에서 실촬영 정답은 12 → 11 로 한 장 준다(실측).
     * 그 한 장이 프로세스가 사라지는 것보다 싸다.
     */
    const val LOW_MEMORY_MAX_EDGE = 960

    /**
     * **설정 파일이 정한 값**(`camera.max_edge`). 리빌드 없이 바꿀 수 있다.
     * 범위 검증은 [AppConfig] 가 한다(640~4096) — 여기서 또 검증하면 진실이 두 곳이 된다.
     */
    @Volatile
    var configuredMaxEdge: Int = DEFAULT_MAX_EDGE
        private set

    /** 메모리 압력을 받고 있나(`onTrimMemory`). */
    @Volatile
    private var underPressure = false

    /**
     * 지금 실제로 쓰는 상한. **설정값과 압력 강등의 합성이다.**
     *
     * 🔑 `minOf` 인 것이 요점이다 — 사용자가 이미 960 으로 낮춰 뒀는데 강등이 그것을
     * **올려 버리면** 방어가 아니라 역효과다.
     *
     * 🔴 **이 값이 바뀌면 인식률 표가 바뀐다.** 그래서 화면과 로그에 드러낸다 —
     * 조용히 강등되면 다음 사람이 그 표를 *"회귀"* 로 읽는다(그리고 원인을 못 찾는다).
     */
    val activeMaxEdge: Int
        get() = if (underPressure) minOf(LOW_MEMORY_MAX_EDGE, configuredMaxEdge) else configuredMaxEdge

    /** 압력 때문에 설정값보다 **낮게** 돌고 있나. 화면 표시용. */
    val degraded: Boolean get() = activeMaxEdge < configuredMaxEdge

    /** 설정을 적용한다. @return 값이 실제로 바뀌었으면 true. */
    fun setConfigured(value: Int): Boolean {
        if (configuredMaxEdge == value) return false
        configuredMaxEdge = value
        return true
    }

    /** @return 실제 사용값이 바뀌었으면 true(그때만 로그를 찍으라는 뜻). */
    fun setDegraded(on: Boolean): Boolean {
        if (underPressure == on) return false
        val before = activeMaxEdge
        underPressure = on
        return activeMaxEdge != before
    }

    /**
     * 축소 후 크기. 상한 이하면 그대로 돌려준다. 0 이 되지 않게 최소 1 을 보장한다.
     *
     * 🔴 **비율을 `Float` 으로 계산한다. `Double` 로 바꾸지 마라.**
     * 샘플 경로가 원래 `Float` 이었고, 정밀도가 달라지면 크기가 **1 어긋난다** —
     * 예: 1000×750 을 960 으로 줄일 때 `Float` 은 750×0.96f = 719.99998 → **719**,
     * `Double` 은 720.0000000000001 → **720**. 그러면 같은 사진이 두 경로에서 다른 크기가 되고
     * *"샘플로는 되는데 촬영본으로는 안 된다"* 가 여기서 생긴다.
     */
    fun scaledSize(width: Int, height: Int, maxEdge: Int = DEFAULT_MAX_EDGE): Size {
        require(width > 0 && height > 0) { "크기가 0 이하다: ${width}x$height" }
        val longest = maxOf(width, height)
        if (longest <= maxEdge) return Size(width, height)
        val ratio = maxEdge.toFloat() / longest
        return Size(
            (width * ratio).toInt().coerceAtLeast(1),
            (height * ratio).toInt().coerceAtLeast(1),
        )
    }

    data class Size(val width: Int, val height: Int)

    /**
     * ARGB 픽셀 → BT.601 휘도 바이트.
     *
     * **`Bitmap.Config.ALPHA_8` 로 뽑으면 안 된다** — 그건 알파 채널이고 휘도가 아니라서
     * 흰 배경/검은 글자 이미지가 통째로 한 색으로 나온다. 그래서 직접 계산한다.
     *
     * 절삭(`/1000`)을 쓴다. 데스크톱 하네스(PIL `convert("L")`)는 반올림이라 화소 대부분이
     * **1 차이**가 난다는 것이 이미 확인됐다 — 같은 코어인데 결과가 갈리면 그 차이를 먼저 본다.
     * 여기서 방식을 바꾸면 그 대조가 깨지므로 **바꾸지 마라.**
     *
     * @param out 재사용 버퍼. 크기가 맞지 않으면 새로 만든다(프레임마다 할당하면 쓰레기가 된다).
     */
    fun luminance(pixels: IntArray, out: ByteArray? = null): ByteArray {
        val dst = if (out != null && out.size == pixels.size) out else ByteArray(pixels.size)
        for (i in pixels.indices) {
            val p = pixels[i]
            val r = (p shr 16) and 0xFF
            val g = (p shr 8) and 0xFF
            val b = p and 0xFF
            dst[i] = ((r * 299 + g * 587 + b * 114) / 1000).toByte()
        }
        return dst
    }

    /**
     * 종횡비. **`ViewTransform.aspectRatio` 와 같은 식이지만 쓰는 곳이 다르다** —
     * 이쪽은 **이미지**(촬영본·샘플), 그쪽은 **뷰와 프레임**이다.
     * 🔑 한 곳으로 합치지 않은 이유: 합치면 이미지 코드가 뷰 계산에 의존하게 된다.
     */
    fun aspectOf(w: Int, h: Int): Float {
        require(h > 0) { "높이가 0 이하다: $h" }
        return w.toFloat() / h
    }

    /** 그레이 버퍼 통계. 값이 그럴듯한지 눈으로 보는 최소 증거다. */
    data class Stats(val mean: Int, val min: Int, val max: Int) {
        /**
         * 폭이 이만큼도 안 벌어지면 **디코딩·휘도 변환을 먼저 의심한다.** 번호판이 든 사진이면
         * 밝은 판과 검은 글자가 같이 있어 폭이 크게 벌어진다.
         *
         * ⚠ 이 값은 실측이 아니라 추정이다. 판정에 쓰지 말고 **의심의 근거로만** 쓴다 —
         * 그래서 실패 사유로 승격시키지 않았다.
         */
        val looksFlat: Boolean get() = max - min < 16
    }

    fun stats(gray: ByteArray): Stats {
        if (gray.isEmpty()) return Stats(0, 0, 0)
        var min = 255
        var max = 0
        var sum = 0L
        for (b in gray) {
            val v = b.toInt() and 0xFF
            if (v < min) min = v
            if (v > max) max = v
            sum += v
        }
        return Stats(mean = (sum / gray.size).toInt(), min = min, max = max)
    }
}
