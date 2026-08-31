package com.example.digitcam

/**
 * Kotlin ↔ C++ 경계. 선언은 계약서(docs/digitcam-contract.md) §4 그대로다.
 *
 * **이 파일의 external 선언을 임의로 바꾸면 cpp-engineer 쪽 심볼과 링크가 끊긴다.**
 * 시그니처를 바꿔야 한다면 계약을 먼저 고쳐야 하고, 계약 변경은 루트 소관이다.
 *
 * `object` 이므로 네이티브 함수의 두 번째 인자는 `jclass` 가 아니라 `jobject`(싱글턴
 * 인스턴스)다. C++ 쪽이 `jclass` 로 받으면 인자 위치가 밀리지는 않지만(둘 다 포인터),
 * 계약 §4 가 `jobject` 로 못박았으므로 그대로 간다.
 *
 * ## 스레드 규약 (중요)
 *
 * 계약 §4 는 `nativeConfigure` 를 "같은 스레드에서만 부른다"고 전제한다. 그래서
 * **핸들의 생성·설정·처리·해제 전부를 분석용 단일 스레드 하나에서만 호출한다.**
 * 이 규약을 지키는 주체는 [VisionEngine] 이다 — 이 object 를 직접 부르지 말고
 * 그쪽을 거쳐라.
 */
object DigitVision {

    /**
     * 적재 실패 사유. null 이면 정상 적재.
     *
     * `init { System.loadLibrary(...) }` 를 날것으로 두면 적재 실패 시 클래스 초기화
     * 단계에서 ExceptionInInitializerError 로 죽고, 그 뒤로는 이 클래스에 접근할
     * 때마다 NoClassDefFoundError 가 나서 원인이 화면에 남지 않는다. 지금은
     * cpp-engineer 의 REQ-0003 이 끝나기 전이라 libdigitcam.so 자체가 없는 상태이므로,
     * 실패를 값으로 들고 있다가 화면에 표시한다.
     */
    val loadError: String?

    init {
        loadError = try {
            System.loadLibrary("digitcam")
            null
        } catch (e: UnsatisfiedLinkError) {
            e.message ?: e.toString()
        }
    }

    val isAvailable: Boolean get() = loadError == null

    /** 파이프라인 인스턴스 생성. 반환값은 네이티브 핸들. 실패 시 0. */
    external fun nativeCreate(): Long

    /** 해제. 이후 해당 핸들 사용 금지. */
    external fun nativeDestroy(handle: Long)

    /**
     * 설정 주입. server.properties 의 `vision.` 으로 시작하는 줄을 **그대로** 넘긴다.
     * 형식: "vision.motion_threshold=0.6\nvision.stable_frames=3\n..."
     * 모르는 키는 C++ 가 무시한다. 값이 깨졌으면 그 키만 기본값을 유지한다.
     *
     * **Kotlin 은 파싱하지 않는다** — 임계값 해석은 전부 네이티브 몫이다(계약 개정 2 §4).
     * 덕분에 임계값이 늘어도 이 시그니처와 Kotlin 코드가 깨지지 않는다.
     */
    external fun nativeConfigure(handle: Long, config: String)

    /**
     * 한 프레임 처리. y 는 YUV_420_888 의 Y 평면(그레이스케일) 복사본.
     * rowStride 는 width 보다 클 수 있다. rotationDegrees 는 0/90/180/270.
     * 반환값은 계약 §5 의 JSON 문자열. 절대 null 을 반환하지 않는다.
     */
    external fun nativeProcessGray(
        handle: Long,
        y: ByteArray,
        width: Int,
        height: Int,
        rowStride: Int,
        rotationDegrees: Int,
    ): String

    /**
     * 표준 인식 경로(PaddleOCR 검출망 + CRNN/CTC 인식망)의 가중치를 **바이트로** 싣는다.
     * 시그니처는 cpp-engineer 가 정했다(REQ-0374).
     *
     * ## 🔴 왜 경로가 아니라 바이트인가
     *
     * **assets 는 파일이 아니다.** APK 안에 들어가서 파일시스템 경로가 없고, 그래서
     * `cv::dnn::readNetFromONNX(const String&)` 로는 **열 수 없다.** OpenCV 4.14 에
     * 바이트 오버로드가 이미 있어(`dnn.hpp:1113`) 복사 없이 그쪽으로 넘긴다 —
     * `getFilesDir()` 로 복사하는 길은 디스크에 한 벌을 더 만들고 실패 갈래를 늘린다.
     *
     * @param detOnnx `ppocr_det_v4.onnx` 검출망(글자 영역). 언어 무관
     * @param recOnnx `korean_rec_static.onnx` 인식망(CRNN+CTC). 분할 없이 한 줄을 읽는다
     * @param dictUtf8 `korean_dict.txt` 글자 사전 3,688자를 **UTF-8 바이트 그대로**
     * @return true 면 실렸다.
     *
     * ⚠ **false 를 치명적으로 다루지 마라.** 못 실으면 예전 템플릿 경로로만 돈다 —
     * 인식이 멈추지 않는다. 로그만 남기고 계속 간다(cpp 규약).
     * ⚠ null·빈 배열이면 false 다(예외를 던지지 않는다). 다시 불러도 되고, 이전 것을 덮는다.
     */
    external fun nativeLoadModels(
        handle: Long,
        detOnnx: ByteArray,
        recOnnx: ByteArray,
        dictUtf8: ByteArray,
    ): Boolean

    /** 링크된 OpenCV 버전 문자열. SDK 연결 확인용(화면에 표시한다). */
    external fun nativeVersion(): String
}
