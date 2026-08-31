package com.example.digitcam

/**
 * 프레임 좌표 → 뷰 좌표 변환. **`OverlayView` 가 쓰던 계산을 그대로 뽑았다(거동 변화 0).**
 *
 * ## 왜 뽑았나
 *
 * 좌표 코드는 **조용히 깨진다** — 오류도 경고도 없고 화면만 이상하다.
 * 사용자가 *"가로모드에서 번호판 위치가 다른 곳에 형성된다"* 를 보고했고(REQ-0331),
 * 그 종류는 **눈으로만** 발견된다. 순수 함수로 빼면 알려진 크기에서 기대 좌표를
 * **단언할 수 있다** — 기기가 없어도.
 *
 * ⚠ **지금은 아무것도 고치지 않았다.** 이 파일의 시험은 **수정 전 거동을 고정**하는 것이고,
 * 원인이 확정된 뒤 고칠 때 그 변화를 잡아 주는 것이 목적이다.
 *
 * ## 🔴 이 변환이 전제하는 것
 *
 * `PreviewView` 의 기본 `ScaleType` 이 **`FILL_CENTER`** 라는 것, 그리고 **프리뷰가 보여 주는
 * 영상과 분석 프레임의 종횡비가 같다**는 것이다.
 * **둘째 전제가 깨지면 박스가 어긋난다** — 프리뷰와 오버레이가 다른 크롭을 하기 때문이다.
 *
 * ## ✅ 수정 A 로 그 전제를 되살렸다 — 🔴 **그런데 남는 것이 있다**
 *
 * A(프리뷰·분석을 같은 4:3 으로) 는 **둘이 자르는 분율을 같게** 만든다. 하지만:
 * ```
 * ✅ 없어진 것 : 프리뷰와 오버레이의 **어긋남**([cropFraction] 이 같아진다)
 * 🔴 남는 것   : **잘림 자체.** 뷰(예 20:9)와 소스(4:3)의 비가 다르므로 여전히 잘린다
 *              → 프레임 y=0 은 뷰 좌표 **-180** = 화면 밖
 *              → 🔑 **프레임 위아래 끝의 번호판은 검출됐는데도 박스가 안 보인다**
 * ```
 * ⚠ **A 는 그 셋째 경로를 고치지 않는다.** 고치려면 `FIT_CENTER`(검은 여백) 또는
 * 뷰 자체를 4:3 으로 두는 것이고 **둘 다 화면 생김새를 바꾼다** — 별개 결정이다.
 * 🔑 **예방적 수정을 적을 때 남는 것을 같이 적는다.** 안 적으면 다음 사람이 그 항목을 지운다.
 */
object ViewTransform {

    /** 배율과 중앙 정렬 오프셋. 오프셋이 음수면 그만큼 **잘린다**. */
    data class Fit(val scale: Float, val dx: Float, val dy: Float) {
        /** 좌우로 잘리는 픽셀(한쪽). 0 이면 안 잘린다. */
        val cropX: Float get() = if (dx < 0f) -dx else 0f

        /** 위아래로 잘리는 픽셀(한쪽). */
        val cropY: Float get() = if (dy < 0f) -dy else 0f
    }

    /**
     * 화면을 채우고 넘치는 부분을 자른다(`FILL_CENTER`). 카메라 프리뷰가 이것이다.
     * 배율은 **큰 쪽**을 쓴다 — 짧은 변을 채우면 긴 변이 넘친다.
     */
    fun fill(viewW: Int, viewH: Int, frameW: Int, frameH: Int): Fit =
        compute(viewW, viewH, frameW, frameH, fitInside = false)

    /**
     * 프레임 전체가 보이도록 축소해 맞춘다(`FIT_CENTER`). 샘플 모드의 `ImageView` 가 이것이다.
     * 배율은 **작은 쪽** — 잘리지 않고 여백이 생긴다.
     */
    fun fit(viewW: Int, viewH: Int, frameW: Int, frameH: Int): Fit =
        compute(viewW, viewH, frameW, frameH, fitInside = true)

    private fun compute(viewW: Int, viewH: Int, frameW: Int, frameH: Int, fitInside: Boolean): Fit {
        require(frameW > 0 && frameH > 0) { "프레임 크기가 0 이하다: ${frameW}x$frameH" }
        val sx = viewW.toFloat() / frameW
        val sy = viewH.toFloat() / frameH
        val scale = if (fitInside) minOf(sx, sy) else maxOf(sx, sy)
        return Fit(
            scale = scale,
            dx = (viewW - frameW * scale) / 2f,
            dy = (viewH - frameH * scale) / 2f,
        )
    }

    /** 프레임 좌표 하나를 뷰 좌표로. */
    fun mapX(x: Int, f: Fit): Float = x * f.scale + f.dx

    fun mapY(y: Int, f: Fit): Float = y * f.scale + f.dy

    /**
     * 뷰와 프레임의 **종횡비 차이**. 1.0 이면 같고, 멀어지면 그만큼 크롭이 생긴다.
     *
     * 🔑 **프리뷰의 종횡비와 이 값을 대조하는 것이 REQ-0331 의 판별자다** —
     * 프리뷰가 다른 비율을 다른 배율로 채우면 그 차이가 곧 박스 어긋남이다.
     */
    fun aspectRatio(w: Int, h: Int): Float {
        require(h > 0) { "높이가 0 이하다: $h" }
        return w.toFloat() / h
    }

    /**
     * 🔴 **소스의 몇 분율이 잘리나** (0.0 ~ 0.5). `FILL_CENTER` 기준.
     *
     * ## 잘림과 어긋남은 다른 것이다 — 이 함수가 그 구분이다
     *
     * ```
     * 잘림   : 뷰와 소스의 종횡비가 다르면 생긴다. 🔑 **수정 A 뒤에도 남는다**
     * 어긋남 : 프리뷰와 오버레이가 **다른 분율** 을 자를 때 생긴다 ← 🔴 이것이 결함이다
     * ```
     * 해상도가 달라도 **종횡비가 같으면 이 분율이 같다.** 그래서 박스가 맞는다.
     * `PreviewView` 와 `OverlayView` 의 이 값이 갈리면 그 차이가 곧 어긋남이다.
     *
     * @return `first` = 좌우 분율 · `second` = 위아래 분율
     */
    fun cropFraction(viewW: Int, viewH: Int, srcW: Int, srcH: Int): Pair<Float, Float> {
        val f = fill(viewW, viewH, srcW, srcH)
        val scaledW = srcW * f.scale
        val scaledH = srcH * f.scale
        return Pair(
            if (scaledW > 0f) f.cropX / scaledW else 0f,
            if (scaledH > 0f) f.cropY / scaledH else 0f,
        )
    }
}
