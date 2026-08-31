package com.example.digitcam

import android.app.ActivityManager
import android.content.Context
import android.os.Debug
import android.util.Log

/**
 * **앱이 자기 메모리를 값으로 남긴다** (REQ-0407).
 *
 * ## 왜 필요한가 — 죽으면 아무것도 안 남는다
 *
 * 네이티브 할당으로 죽을 때는 **예외가 안 난다.** 커널의 LMK 가 프로세스를 조용히 죽이고,
 * 화면에는 *"앱이 그냥 사라졌다"* 만 남는다. 그때 `try/catch` 로 잡을 것도, 스택도 없다.
 * 🔑 **그래서 죽기 전에 남겨 두는 것 말고는 방법이 없다.** 이 파일이 그 일만 한다.
 *
 * ## 한계가 하나가 아니다 — 셋을 갈라서 잰다
 *
 * ```
 * ① Java heap   `dalvik.vm.heapgrowthlimit` (에뮬 실측 192MB). Runtime 으로 사용량을 본다
 *                🔴 **네이티브 할당은 여기 안 걸린다.** largeHeap 을 켜도 네이티브엔 무의미하다
 * ② 네이티브     OpenCV Mat · dnn blob. **앱 상한이 아니라 기기 전체 압력**으로 판정된다
 * ③ 총 RSS      위 둘 + 코드·그래픽. **LMK 가 보는 것이 이것**이다
 * ```
 * ⚠ 그래서 *"RSS 342MB"* 를 *"Java heap 상한 192MB"* 와 대면 **틀린 비교**다. 다른 축이다.
 *
 * ## 상주와 피크를 갈라 둔다
 *
 * 상주가 높으면 **항상** 위험하고, 피크만 높으면 그 순간만 넘기면 산다 — 대책이 다르다.
 * 그래서 [peakRssMb] 를 따로 들고 다닌다.
 */
object MemoryWatch {

    private const val TAG = "MemoryWatch"

    /** 이 프로세스가 지금까지 찍은 최대 RSS. **상주와 갈라 보려고 따로 센다.** */
    @Volatile
    var peakRssMb = 0
        private set

    /** 최대 RSS 를 찍은 순간의 설명(어느 단계였나). 대책을 정하는 것은 "언제" 다. */
    @Volatile
    var peakWhen = "-"
        private set

    data class Snapshot(
        /** ① Java heap 사용량 / 상한. 상한은 기기가 정한다. */
        val javaUsedMb: Int,
        val javaMaxMb: Int,
        /** ② 네이티브 힙. 대개 여기가 가장 크다. */
        val nativeMb: Int,
        /** ③ 총 RSS — **LMK 가 보는 수**. */
        val rssMb: Int,
        /** 기기에 남은 가용 메모리. **총 RAM 이 아니라 이것이 판단 근거다.** */
        val availMb: Int,
        /** 시스템이 "부족" 이라고 보기 시작하는 선. `availMem` 이 이 밑이면 LMK 가 움직인다. */
        val thresholdMb: Int,
        /** 시스템이 지금 저메모리 상태라고 보나. */
        val lowMemory: Boolean,
    ) {
        /** 가용이 문턱의 몇 배인가. **1.0 이하면 위험하다** — 값 하나로 읽는 판별자. */
        val headroom: Double get() = if (thresholdMb > 0) availMb.toDouble() / thresholdMb else Double.NaN

        override fun toString(): String =
            "java ${javaUsedMb}/${javaMaxMb}MB · native ${nativeMb}MB · RSS ${rssMb}MB · " +
                "가용 ${availMb}MB(문턱 ${thresholdMb}MB · 여유 ${"%.1f".format(headroom)}배)" +
                if (lowMemory) " 🔴저메모리" else ""
    }

    /**
     * 한 번 잰다. **비싸다** — `Debug.getMemoryInfo` 가 수 ms 를 쓴다.
     * 매 프레임 부르지 마라. 부하가 걸리는 지점에서만 부른다.
     *
     * @param label 지금이 어느 단계인가(피크를 찍었을 때 그 자리를 남기려고 받는다)
     */
    fun sample(context: Context, label: String): Snapshot {
        val dbg = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val sys = ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }
        val rt = Runtime.getRuntime()

        fun stat(key: String) = (dbg.getMemoryStat(key)?.toIntOrNull() ?: 0) / 1024

        val snap = Snapshot(
            javaUsedMb = ((rt.totalMemory() - rt.freeMemory()) / MB).toInt(),
            javaMaxMb = (rt.maxMemory() / MB).toInt(),
            nativeMb = stat("summary.native-heap"),
            // summary.total-rss 는 API 30+ 에서 채워진다. 없으면 PSS 로 대신하고 그 사실이 값에 드러난다.
            rssMb = stat("summary.total-rss").takeIf { it > 0 } ?: stat("summary.total-pss"),
            availMb = (sys.availMem / MB).toInt(),
            thresholdMb = (sys.threshold / MB).toInt(),
            lowMemory = sys.lowMemory,
        )

        if (snap.rssMb > peakRssMb) {
            peakRssMb = snap.rssMb
            peakWhen = label
            Log.i(TAG, "새 피크 RSS ${snap.rssMb}MB @ $label · $snap")
        }
        return snap
    }

    /** 화면 한 줄. **상주가 아니라 피크도 같이** 보여 준다 — 둘은 다른 이야기다. */
    fun line(snap: Snapshot): String =
        "메모리 RSS ${snap.rssMb}MB(피크 ${peakRssMb}MB@${peakWhen}) · 가용 ${snap.availMb}MB" +
            if (snap.lowMemory) " 🔴저메모리" else ""

    private const val MB = 1024L * 1024L
}
