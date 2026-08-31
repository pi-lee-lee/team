package com.example.digitcam

import android.content.Context
import android.util.Log
import java.io.File
import java.io.IOException
import java.io.InputStreamReader
import java.util.Properties

/**
 * `server.properties` 를 **텍스트로** 다루는 계층 (계약 §6.1).
 *
 * 설정 화면이 이 파일을 편집하는데, 파일을 통째로 다시 쓰면 안 된다. 이 파일은 설정이자
 * 설명서다 — 포트 5500 을 쓰는 이유(macOS AirPlay 충돌)나 `min_sharpness` 눈금이 바뀐 경위 같은
 * 건 **주석에만** 적혀 있다. 저장 한 번에 그게 날아가면 다음 사람이 값의 근거를 잃는다.
 *
 * 그래서 [applyUpdates] 는 **값이 있는 줄만 제자리에서 바꾸고** 주석·빈 줄·모르는 키는
 * 손대지 않는다. `Properties.store()` 를 쓰지 않는 이유가 이것이다 — 그건 주석을 다 버리고
 * 키 순서까지 뒤집는다.
 */
object PropertiesFile {

    private const val TAG = "PropertiesFile"

    /**
     * `updates` 의 키만 제자리 교체한 새 텍스트를 돌려준다.
     *
     * - 주석(`#`, `!`)·빈 줄: 그대로
     * - `updates` 에 없는 키: 그대로 (**화면이 모르는 키도 살아남는다**)
     * - 파일에 없던 키: 맨 끝에 덧붙인다
     * - 구분자(`=` 또는 `:`)와 키 앞뒤 공백: 원본 유지
     *
     * 줄 끝의 인라인 주석은 보존하지 않는다. `java.util.Properties` 규격상 `=` 뒤는 전부 값이라
     * `port=5500 # 이유` 의 값은 `"5500 # 이유"` 이고, 그건 이미 파싱이 깨진 상태다.
     * 되살리면 오히려 잘못된 값을 복원하는 셈이라 그렇게 하지 않는다.
     */
    fun applyUpdates(original: String, updates: Map<String, String>): String {
        val seen = HashSet<String>()
        val lines = original.split("\n")

        val out = lines.map { line ->
            val trimmed = line.trimStart()
            if (trimmed.isEmpty() || trimmed.startsWith("#") || trimmed.startsWith("!")) return@map line

            val sep = line.indexOfFirst { it == '=' || it == ':' }
            if (sep < 0) return@map line

            val key = line.substring(0, sep).trim()
            val newValue = updates[key] ?: return@map line
            seen.add(key)
            // "key=" 부분(원본 공백 포함)을 그대로 두고 값만 바꾼다.
            line.substring(0, sep + 1) + newValue
        }.toMutableList()

        // 같은 키가 파일에 두 번 있으면 **전부** 바꾼다.
        // 첫 줄만 바꾸면 뒤에 남은 옛 줄이 이긴다 — Properties 는 나중 값이 이기기 때문이다.
        // 그러면 저장은 성공했는데 값은 안 바뀌는, 원인을 찾기 어려운 상태가 된다.
        // (사용자가 파일 끝에 한 줄 덧붙이는 것만으로 이 상황이 만들어진다.)

        val leftovers = updates.filterKeys { it !in seen }
        if (leftovers.isNotEmpty()) {
            // 파일에 없던 키. 어디서 온 값인지 알 수 있게 한 줄 설명을 달고 덧붙인다.
            if (out.isNotEmpty() && out.last().isNotBlank()) out.add("")
            out.add("# 설정 화면에서 추가됨")
            leftovers.forEach { (k, v) -> out.add("$k=$v") }
        }

        return out.joinToString("\n")
    }

    /**
     * 편집 대상 파일. 없으면 assets 기본값을 복사해 만든다.
     *
     * 편집은 항상 **외부 파일**을 향한다. assets 는 APK 안이라 쓸 수 없고, 계약 §6 의
     * 우선순위상 외부 파일이 assets 를 이기므로 여기만 고치면 그 값이 실제로 적용된다.
     */
    fun editableFile(context: Context): File? {
        val file = AppConfig.externalConfigFile(context) ?: return null
        if (!file.exists()) {
            try {
                file.parentFile?.mkdirs()
                context.assets.open(AppConfig.FILE_NAME).use { input ->
                    file.outputStream().use { output -> input.copyTo(output) }
                }
            } catch (e: IOException) {
                Log.w(TAG, "기본 설정 복사 실패: ${e.message}")
                return null
            }
        }
        return file
    }

    /** 편집 대상 파일의 원문. 읽지 못하면 null. */
    fun readText(context: Context): String? = try {
        editableFile(context)?.readText(Charsets.UTF_8)
    } catch (e: IOException) {
        Log.w(TAG, "설정 파일 읽기 실패: ${e.message}")
        null
    }

    /** 편집 결과를 파일에 쓴다. 성공하면 true. */
    fun writeText(context: Context, text: String): Boolean = try {
        val file = editableFile(context) ?: return false
        file.writeText(text, Charsets.UTF_8)
        true
    } catch (e: IOException) {
        Log.w(TAG, "설정 파일 쓰기 실패: ${e.message}")
        false
    }

    /** assets 기본값으로 덮어쓴다(계약 §6.1 의 "기본값으로 되돌리기"). */
    fun restoreDefaults(context: Context): Boolean = try {
        val file = AppConfig.externalConfigFile(context) ?: return false
        file.parentFile?.mkdirs()
        context.assets.open(AppConfig.FILE_NAME).use { input ->
            file.outputStream().use { output -> input.copyTo(output) }
        }
        true
    } catch (e: IOException) {
        Log.w(TAG, "기본값 복원 실패: ${e.message}")
        false
    }

    /**
     * 화면에 채울 값. **실제로 적용되는 것과 같은 순서로 병합한다**(assets → 외부파일).
     *
     * 계약 §6.1 이 "화면에 뜬 값과 네이티브에 들어간 값이 다르면 그 화면은 거짓말" 이라고
     * 못박았으므로, 표시용으로 따로 계산하지 않고 [AppConfig.load] 와 같은 순서를 쓴다.
     */
    fun readMerged(context: Context): Properties {
        val props = Properties()
        try {
            context.assets.open(AppConfig.FILE_NAME).use { stream ->
                InputStreamReader(stream, Charsets.UTF_8).use { props.load(it) }
            }
        } catch (e: IOException) {
            Log.w(TAG, "assets 기본값 읽기 실패: ${e.message}")
        }
        try {
            AppConfig.externalConfigFile(context)?.takeIf { it.isFile }?.inputStream()?.use { stream ->
                InputStreamReader(stream, Charsets.UTF_8).use { props.load(it) }
            }
        } catch (e: IOException) {
            Log.w(TAG, "외부 설정 읽기 실패: ${e.message}")
        }
        return props
    }
}
