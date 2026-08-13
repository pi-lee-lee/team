package com.example.jnidemo

import android.app.Activity
import android.os.Bundle
import android.widget.TextView

/**
 * JNI 연동 최소 예제 (REQ-0001).
 *
 * 이 화면은 Kotlin → C++ 단방향 호출 하나만 보여준다.
 * 네이티브 구현(`native-lib.cpp`)과 `CMakeLists.txt` 는 cpp-engineer 소유이며
 * REQ-0002 로 요청해 받았다. 두 계층을 잇는 계약은 아래 세 가지뿐이다.
 *
 *  1. 라이브러리 이름 `jnidemo`      → 산출물 `libjnidemo.so`
 *  2. 심볼 `Java_com_example_jnidemo_MainActivity_greetFromJNI`
 *  3. 시그니처 `(JNIEnv*, jobject, jstring) -> jstring`
 */
class MainActivity : Activity() {

    /**
     * 네이티브 구현과 이름으로 연결되는 인스턴스 메서드.
     *
     * 반드시 인스턴스 멤버여야 한다. `companion object` 안으로 옮기면 심볼이
     * `..._00024Companion_greetFromJNI` 로 바뀌어 링크가 끊긴다.
     * 이름을 바꾸려면 C++ 쪽도 같이 바뀌어야 하므로 cpp-engineer 에게 요청을 발행할 것.
     */
    external fun greetFromJNI(name: String): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // 표시 문자열은 onCreate 에서 매번 새로 계산한다. 화면 회전이나 프로세스 재생성으로
        // 액티비티가 다시 만들어져도 복원할 상태가 없다(입력값이 상수이므로 멱등).
        // 사용자 입력을 받도록 확장한다면 그 입력만 onSaveInstanceState 로 보존하면 된다.
        findViewById<TextView>(R.id.greetingText).text = greetingOrError(getString(R.string.default_name))
    }

    /**
     * 네이티브 호출을 감싸서, 라이브러리가 없거나 심볼이 어긋난 경우에도
     * 크래시 대신 화면에 원인을 표시한다.
     */
    private fun greetingOrError(name: String): String {
        val loadError = nativeLoadError
        if (loadError != null) {
            return getString(R.string.native_library_missing, loadError)
        }
        return try {
            greetFromJNI(name)
        } catch (e: UnsatisfiedLinkError) {
            // 라이브러리는 올라왔지만 심볼명이 맞지 않는 경우 (예: 시그니처 변경 후 재빌드 누락)
            getString(R.string.native_symbol_missing, e.message ?: e.toString())
        }
    }

    companion object {
        /** 적재 실패 사유. null 이면 정상 적재된 것이다. */
        private var nativeLoadError: String? = null

        init {
            // System.loadLibrary 를 그대로 두면 실패 시 클래스 초기화 단계에서
            // ExceptionInInitializerError 로 죽어 원인이 화면에 남지 않는다.
            nativeLoadError = try {
                System.loadLibrary("jnidemo")
                null
            } catch (e: UnsatisfiedLinkError) {
                e.message ?: e.toString()
            }
        }
    }
}
