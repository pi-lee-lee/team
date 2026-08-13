// native-lib.cpp — jnidemo 네이티브 계층
//
// Kotlin 측 선언 (com.example.jnidemo.MainActivity 의 인스턴스 멤버):
//     external fun greetFromJNI(name: String): String
// 이 선언에 대응하는 JNI 심볼명은 아래 함수명과 정확히 일치해야 한다.
// companion object 로 옮기면 심볼에 _00024Companion 이 붙어 링크가 깨진다.

#include <jni.h>

#include <string>

namespace {

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
    // 성공적으로 획득한 경우에만 해제한다. nullptr 을 Release 에 넘기지 않는다.
    if (chars_ != nullptr) {
      env_->ReleaseStringUTFChars(str_, chars_);
    }
  }

  ScopedUtfChars(const ScopedUtfChars&) = delete;
  ScopedUtfChars& operator=(const ScopedUtfChars&) = delete;

  // 획득 실패(널 인자 또는 OOM) 여부.
  bool valid() const { return chars_ != nullptr; }

  const char* c_str() const { return chars_; }

 private:
  JNIEnv* env_;
  jstring str_;
  const char* chars_;
};

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_jnidemo_MainActivity_greetFromJNI(JNIEnv* env, jobject /* thiz */,
                                                   jstring name) {
  // 방어 1: Kotlin 시그니처는 non-null 이지만 다른 호출 경로(리플렉션 등)에서
  //         null 이 들어올 수 있다. 크래시 대신 대체 문구를 돌려준다.
  if (name == nullptr) {
    return env->NewStringUTF("이름이 없습니다. C++ 네이티브 계층에서 인사합니다.");
  }

  ScopedUtfChars utf_name(env, name);

  // 방어 2: GetStringUTFChars 가 OOM 으로 실패하면 pending exception 이 있는
  //         상태다. 이때 다른 JNI 호출을 이어가지 않고 즉시 반환한다.
  if (!utf_name.valid()) {
    return nullptr;
  }

  // 인자로 받은 문자열이 반환 문자열에 그대로 포함된다.
  const std::string greeting =
      std::string(utf_name.c_str()) + "님 안녕하세요. C++ 네이티브 계층에서 인사합니다.";

  return env->NewStringUTF(greeting.c_str());
}
