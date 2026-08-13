// native-lib.cpp — DigitCam JNI 브리지 (계약 개정 2)
//
// Kotlin 쪽 선언 (com.example.digitcam.DigitVision — object 싱글턴):
//     external fun nativeCreate(): Long
//     external fun nativeDestroy(handle: Long)
//     external fun nativeConfigure(handle: Long, config: String)   // 개정 2에서 문자열 하나로 바뀜
//     external fun nativeProcessGray(handle: Long, y: ByteArray, width: Int, height: Int,
//                                    rowStride: Int, rotationDegrees: Int): String
//     external fun nativeVersion(): String
//
// object 선언이므로 두 번째 인자는 jclass 가 아니라 jobject(싱글턴 인스턴스)다.
// 정적 등록(심볼명 매칭)을 쓴다 — RegisterNatives 는 쓰지 않는다.
//
// 이 파일이 안드로이드 의존을 전부 흡수한다. 인식 로직은 digit_pipeline.{hpp,cpp} 에 있고
// 그쪽에는 jni.h 가 들어가지 않는다(계약 §1 — 임베디드 이식성).

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <string>
#include <vector>

#include "digit_pipeline.hpp"

namespace {

using digitcam::PlatePipeline;
using digitcam::Result;

// GetStringUTFChars / ReleaseStringUTFChars 의 짝맞춤을 타입으로 강제한다.
// 소유권이 주석이 아니라 소멸자에 있으므로 조기 반환·예외 경로에서도 누수가 없다.
class ScopedUtfChars {
 public:
  ScopedUtfChars(JNIEnv* env, jstring str) : env_(env), str_(str), chars_(nullptr) {
    if (str_ != nullptr) {
      // 실패 시 nullptr 을 돌려주며 OutOfMemoryError 가 pending 상태로 남는다.
      chars_ = env_->GetStringUTFChars(str_, nullptr);
    }
  }

  ~ScopedUtfChars() {
    if (chars_ != nullptr) {
      env_->ReleaseStringUTFChars(str_, chars_);
    }
  }

  ScopedUtfChars(const ScopedUtfChars&) = delete;
  ScopedUtfChars& operator=(const ScopedUtfChars&) = delete;

  bool valid() const { return chars_ != nullptr; }
  const char* c_str() const { return chars_; }

 private:
  JNIEnv* env_;
  jstring str_;
  const char* chars_;
};

PlatePipeline* from_handle(jlong handle) {
  return reinterpret_cast<PlatePipeline*>(static_cast<uintptr_t>(handle));
}

jlong to_handle(PlatePipeline* p) {
  return static_cast<jlong>(reinterpret_cast<uintptr_t>(p));
}

// 계약 §5: 절대 null 을 반환하지 않고, 실패한 프레임에서도 전 키를 채운다.
// reason 은 §5.2 의 여섯 값 중 하나여야 하므로 기본을 no_plate 로 둔다.
std::string fallback_json() {
  Result r;
  r.moving = false;
  r.reason = digitcam::Reason::kNoPlate;
  return PlatePipeline::to_json(r);
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_example_digitcam_DigitVision_nativeCreate(JNIEnv* /*env*/, jobject /*thiz*/) {
  // 계약 §4: 실패 시 0. C++ 예외를 JNI 경계 밖으로 흘리지 않는다.
  try {
    return to_handle(new PlatePipeline());
  } catch (...) {
    return 0;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_digitcam_DigitVision_nativeDestroy(JNIEnv* /*env*/, jobject /*thiz*/,
                                                    jlong handle) {
  // handle==0 은 정상적인 no-op 다(생성 실패 후 정리 경로).
  delete from_handle(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_digitcam_DigitVision_nativeConfigure(JNIEnv* env, jobject /*thiz*/, jlong handle,
                                                      jstring config) {
  PlatePipeline* pipeline = from_handle(handle);
  if (pipeline == nullptr || config == nullptr) {
    return;
  }

  ScopedUtfChars utf(env, config);
  if (!utf.valid()) {
    // GetStringUTFChars 실패 = 예외 pending. 다른 JNI 호출을 이어가지 않는다.
    return;
  }

  try {
    // 파싱은 코어가 한다 — 임계값 해석이 임베디드에도 따라가야 하므로(계약 §4).
    pipeline->configure(std::string(utf.c_str()));
  } catch (...) {
    // 설정 반영 실패는 프레임 처리를 막지 않는다. 이전 설정이 유지된다.
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_digitcam_DigitVision_nativeProcessGray(JNIEnv* env, jobject /*thiz*/,
                                                        jlong handle, jbyteArray y, jint width,
                                                        jint height, jint row_stride,
                                                        jint rotation_degrees) {
  PlatePipeline* pipeline = from_handle(handle);

  // 방어 1: 핸들·배열·치수. 어느 하나라도 어긋나면 전 키를 채운 JSON 을 돌려준다(널 금지).
  if (pipeline == nullptr || y == nullptr || width <= 0 || height <= 0) {
    return env->NewStringUTF(fallback_json().c_str());
  }

  const jint stride = row_stride < width ? width : row_stride;

  // 방어 2: rowStride 는 width 보다 클 수 있다. 그 stride 로 마지막 행까지 읽으려면
  //         배열이 (height-1)*stride + width 바이트 이상이어야 한다. 이 검사가 없으면
  //         짧은 배열에서 범위 밖 읽기가 나고, 그건 확정적으로 죽지도 않는다.
  const jsize len = env->GetArrayLength(y);
  const size_t need =
      static_cast<size_t>(height - 1) * static_cast<size_t>(stride) + static_cast<size_t>(width);
  if (len < 0 || static_cast<size_t>(len) < need) {
    return env->NewStringUTF(fallback_json().c_str());
  }

  std::string json;
  try {
    // 임계 영역(GetPrimitiveArrayCritical)을 파이프라인 전체에 걸면 그동안 GC 가 멈춘다.
    // 프레임 하나를 복사해 오는 편이 안전하다.
    std::vector<jbyte> buffer(static_cast<size_t>(len));
    env->GetByteArrayRegion(y, 0, len, buffer.data());

    // 방어 3: GetByteArrayRegion 실패 시 예외가 pending 이다. 이어서 JNI 를 호출하면 안 된다.
    if (env->ExceptionCheck() == JNI_TRUE) {
      return nullptr;
    }

    // 복사본을 감싸는 헤더 전용 Mat — 데이터를 다시 복사하지 않는다.
    const cv::Mat gray(static_cast<int>(height), static_cast<int>(width), CV_8UC1, buffer.data(),
                       static_cast<size_t>(stride));

    const Result r = pipeline->process(gray, static_cast<int>(rotation_degrees));
    json = PlatePipeline::to_json(r);
  } catch (...) {
    // cv::Exception / bad_alloc 포함. C++ 예외를 JNI 경계 밖으로 흘리지 않는다.
    json = fallback_json();
  }

  // NewStringUTF 는 OOM 시 null 을 반환할 수 있다(그때는 예외가 pending 이다).
  // value 는 UTF-8 이고 한글은 BMP 라 modified UTF-8 과 바이트열이 같다.
  return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_digitcam_DigitVision_nativeVersion(JNIEnv* env, jobject /*thiz*/) {
  try {
    return env->NewStringUTF(PlatePipeline::version().c_str());
  } catch (...) {
    return env->NewStringUTF("unknown");
  }
}
