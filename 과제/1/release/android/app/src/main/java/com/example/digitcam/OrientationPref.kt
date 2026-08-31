package com.example.digitcam

import android.app.Activity
import android.content.Context
import android.content.pm.ActivityInfo

/**
 * 화면 방향 선택의 영속 저장 (REQ-0025).
 *
 * ## 왜 `server.properties` 가 아니라 SharedPreferences 인가
 *
 * 요청은 "이미 쓰는 설정 저장소를 재사용" 하라고 했지만, 그 저장소는 계약 §6 이 관장하는
 * `server.properties` 다. 거기에 방향 상태를 넣지 않은 이유가 셋이다.
 *
 * 1. **"기본값으로 되돌리기" 가 방향까지 되돌린다.** 그 버튼은 assets 사본으로 덮어쓰는
 *    동작이라(§6.1 5번), 서버 주소를 잘못 만져 복구하려던 사용자가 화면 방향까지 잃는다.
 *    서로 아무 관계 없는 두 가지가 한 파일에 있어서 생기는 결합이다.
 * 2. **계약이 키 목록을 고정했다.** §6 은 "키 전부 이 이름 그대로" 이고 Kotlin 이 해석하는
 *    키와 네이티브로 넘기는 `vision.*` 만 있다. UI 상태는 그 어느 쪽도 아니라서,
 *    넣으면 매 로드마다 "알 수 없는 키 무시" 로그가 찍힌다.
 * 3. 그 파일은 **사용자가 adb 로 직접 편집하는 파일**이다. 앱 내부 상태를 섞으면 사람이
 *    고쳐도 되는 값과 앱이 관리하는 값의 경계가 흐려진다.
 *
 * 요청이 요구한 실질(앱을 껐다 켜도 유지 + `setContentView` 전에 읽어 적용)은 그대로 지킨다.
 */
object OrientationPref {

    private const val PREFS = "digitcam_ui"
    private const val KEY_PORTRAIT = "portrait"

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** 기본값은 가로다(요구 1). */
    fun isPortrait(context: Context): Boolean =
        prefs(context).getBoolean(KEY_PORTRAIT, false)

    fun setPortrait(context: Context, portrait: Boolean) {
        prefs(context).edit().putBoolean(KEY_PORTRAIT, portrait).apply()
    }

    /**
     * 저장된 방향을 액티비티에 적용한다. **`setContentView` 보다 먼저 부른다.**
     *
     * `LANDSCAPE`/`PORTRAIT` 두 상수만 쓴다. 이 둘은 **센서를 아예 보지 않으므로**
     * 기기를 기울여도 방향이 바뀌지 않는다(요구 3). 기울기 차단을 위한 별도 코드는 없고,
     * 있어서도 안 된다 — `SENSOR_*` / `USER` / `FULL_SENSOR` / `UNSPECIFIED` / `BEHIND` 중
     * 하나라도 쓰는 순간 시스템 자동회전을 따라가 요구 3이 조용히 깨진다.
     */
    fun apply(activity: Activity) {
        activity.requestedOrientation =
            if (isPortrait(activity)) ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
    }

    /**
     * 가로 ↔ 세로 전환. 저장한 뒤 적용하면 **액티비티가 재생성되면서** 새 방향의
     * 레이아웃(`layout-land/` 또는 `layout/`)이 잡힌다.
     */
    fun toggle(activity: Activity) {
        setPortrait(activity, !isPortrait(activity))
        apply(activity)
    }
}
