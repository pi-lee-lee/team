package com.example.digitcam

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.View

/**
 * 번호판 사각형 오버레이(계약 §8). 프리뷰 위에 겹쳐 그린다.
 *
 * 네이티브가 주는 `quad` 는 **회전 보정된 프레임 좌표계**의 네 꼭짓점이라 기울어진 번호판도
 * 그대로 따라 그릴 수 있다. 축 정렬 사각형으로 바꾸면 기울기 정보가 사라지므로 Path 로 잇는다.
 *
 * 문자 박스(`chars[].box`)는 그리지 않는다 — 그건 정규화된 번호판 좌표계라 프레임 위에
 * 겹칠 수 없다(계약 §5·§8 이 명시).
 *
 * 좌표 변환은 PreviewView 의 기본 스케일 타입 FILL_CENTER 를 흉내낸다: 짧은 쪽을 채우도록
 * 확대(max 배율)하고 가운데 정렬해 넘치는 부분을 잘라낸다. 분석 해상도와 프리뷰의 종횡비가
 * 다르면 몇 픽셀 어긋날 수 있는데, 그건 표시 문제지 인식 실패가 아니다.
 */
class OverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : View(context, attrs, defStyleAttr) {

    private val quadPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 5f
        color = Color.parseColor("#00E676")
    }

    /** 값을 못 읽었을 때(번호판은 찾았지만 blurry/segment_fail 등)는 회색으로 그려 구분한다. */
    private val quadPaintWeak = Paint(quadPaint).apply {
        color = Color.parseColor("#90A4AE")
        strokeWidth = 3f
    }

    private val path = Path()

    private companion object {
        const val TAG = "OverlayView"
    }

    private var quad: List<DigitResult.Point> = emptyList()
    private var frameWidth = 0
    private var frameHeight = 0
    private var confident = false

    /**
     * true 면 프레임 전체가 보이도록 축소해 맞춘다(FIT_CENTER), false 면 화면을 채우고
     * 넘치는 부분을 자른다(FILL_CENTER).
     *
     * 카메라 프리뷰는 PreviewView 기본값이 FILL_CENTER 라 false 가 맞고, 샘플 모드는
     * ImageView 를 fitCenter 로 띄우므로 true 여야 사각형이 그림 위 제자리에 얹힌다.
     * 둘을 맞추지 않으면 오버레이가 배경과 어긋난다.
     */
    var useFitCenter = false
        set(value) {
            field = value
            invalidate()
        }

    fun update(result: DigitResult) {
        quad = result.quad
        frameWidth = result.frameWidth
        frameHeight = result.frameHeight
        confident = result.hasValue
        invalidate()
    }

    fun clear() {
        quad = emptyList()
        invalidate()
    }

    /**
     * 좌표 변환을 한 줄로 찍는다. **값이 바뀔 때만** — 매 프레임 찍으면 로그가 묻힌다.
     *
     * 왜 필요한가: *"박스가 엉뚱한 곳에 그려진다"* 를 로그 없이는 못 가른다.
     * 회전(축 바뀜) · 크롭(한 축만 늘어남) · 오프셋 중 무엇인지는 **이 수들이 답한다.**
     */
    private var lastTransformKey: String? = null

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (quad.size != 4 || frameWidth <= 0 || frameHeight <= 0) return

        // 🔑 계산은 [ViewTransform] 에 있다 — 순수 함수라 시험으로 고정된다.
        //    여기서 다시 계산하면 진실이 두 곳이 되고, 좌표 결함은 조용해서 그것을 못 잡는다.
        val f = if (useFitCenter) {
            ViewTransform.fit(width, height, frameWidth, frameHeight)
        } else {
            ViewTransform.fill(width, height, frameWidth, frameHeight)
        }

        val key = "$width x $height / $frameWidth x $frameHeight / $useFitCenter"
        if (key != lastTransformKey) {
            lastTransformKey = key
            // 🔑 뷰 종횡비와 프레임 종횡비가 다르면 크롭이 생기고, 프리뷰의 크롭과 다르면
            //    그 차이가 곧 어긋남이다. 두 비를 같이 찍어 눈으로 대조한다.
            android.util.Log.i(
                TAG,
                "좌표변환 뷰=${width}x$height(${"%.3f".format(ViewTransform.aspectRatio(width, height))}) " +
                    "프레임=${frameWidth}x$frameHeight(${"%.3f".format(ViewTransform.aspectRatio(frameWidth, frameHeight))}) " +
                    "scale=${"%.3f".format(f.scale)} dx=${"%.1f".format(f.dx)} dy=${"%.1f".format(f.dy)} " +
                    "fitCenter=$useFitCenter " +
                    "잘림 좌우=${"%.0f".format(f.cropX)}px 위아래=${"%.0f".format(f.cropY)}px",
            )
        }

        path.reset()
        quad.forEachIndexed { i, p ->
            val x = ViewTransform.mapX(p.x, f)
            val y = ViewTransform.mapY(p.y, f)
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        path.close()

        canvas.drawPath(path, if (confident) quadPaint else quadPaintWeak)
    }
}
