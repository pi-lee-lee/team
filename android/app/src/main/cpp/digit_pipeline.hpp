// digit_pipeline.hpp — DigitCam 번호판 인식 코어의 공개 인터페이스 (계약 개정 2)
//
// 이 파일과 digit_pipeline.cpp 는 **순수 OpenCV 코어**다.
//   - jni.h / android/*.h / __android_log_print 를 절대 include 하지 않는다.
//   - 안드로이드에 종속된 타입을 공개 API 에 노출하지 않는다.
// 임베디드 이식 시 들고 갈 파일: digit_pipeline.hpp / digit_pipeline.cpp /
// plate_templates.hpp(생성된 데이터 전용 헤더) 셋. 이 성질이 깨지면 이 프로젝트의 목적이
// 사라지므로 여기에 안드로이드 헤더를 넣지 마라.
//
// 계약 원본: docs/digitcam-contract.md (개정 2)

#ifndef DIGITCAM_DIGIT_PIPELINE_HPP_
#define DIGITCAM_DIGIT_PIPELINE_HPP_

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace digitcam {

// 계약 §6 의 vision.* 키에 1:1 대응. nativeConfigure 가 넘긴 문자열을 코어가 파싱한다
// (Kotlin 은 해석하지 않는다 — 임계값 해석은 임베디드에도 따라가야 하므로 여기 있다).
struct Config {
  double min_confidence = 0.70;         // 문자별 신뢰도 최소값의 하한
  int stable_frames = 3;                // 같은 value 연속 몇 프레임이면 stable
  // 프레임 이동량(화소, 160x120 축소본 기준)이 이 값 미만이면 '정지'.
  // 기본 2.0 은 실측으로 잡았다 — 손떨림 ≤2.08, 실제 이동 ≥1.97 (REQ-0014).
  // ⚠ 계약 개정 5까지의 '평균 절대차분' 과 단위가 다르다. 그 지표로는 두 무리가 겹쳐서
  //    쓸 수 있는 임계가 없었다(자세한 근거는 REQ-0014 처리 결과).
  double motion_threshold = 2.0;
  int still_frames = 3;                 // 정지로 인정하기까지 연속 프레임 수
  // 정규화 번호판의 방향별 평균 기울기를 대비로 나눈 값의 하한(단위 설명은 .cpp 참조).
  // 기본 40 은 샘플 26장 실측으로 잡았다 — 거절해야 하는 4장 ≤30.2, 읽어야 하는 22장 ≥52.4.
  double min_sharpness = 40.0;
  double min_plate_width_ratio = 0.25;  // 번호판 폭 / 프레임 폭 최소
  std::string plate_format = "auto";    // auto | new | old
  bool gate_bypass = false;             // true 면 움직임·선명도 게이트를 끈다(디버그)
};

// 계약 §5.2. 어느 단계에서 걸렸는지 항상 남긴다 — 이게 없으면 튜닝을 못 한다.
enum class Reason {
  kOk,
  kMoving,
  kNoPlate,
  kBlurry,
  kSegmentFail,
  kLowConf,
};

const char* reason_name(Reason r);

struct Box {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

// 문자 하나의 판독 결과. c 는 UTF-8 문자열(한글이 들어오므로 char 하나가 아니다).
struct CharResult {
  std::string c;
  double conf = 0.0;
  Box box;  // 정규화된 번호판 좌표계
};

// 한 프레임 처리 결과. 계약 §5 JSON 의 전 키에 1:1 대응한다.
//
// 불변식: quad 는 비었거나 정확히 4점.
// ⚠ value 와 chars 는 길이가 다를 수 있다. `low_conf` 로 버린 프레임은 value 를 비우되
//    chars 는 남긴다 — 어느 글자가 몇 점으로 걸렸는지 봐야 임계를 조정할 수 있기 때문이다
//    (계약 §5.2 의 "왜 값이 안 나왔는지" 를 화면에 띄우라는 요구와 같은 취지).
//    따라서 Kotlin 은 **value 가 비었는지로 판단**해야 하고 chars.size() 로 판단하면 안 된다.
struct Result {
  std::string value;             // 못 읽으면 ""
  double conf = 0.0;             // 문자별 신뢰도의 최소값
  bool stable = false;
  bool fresh = false;
  bool moving = true;            // 첫 프레임은 이전 프레임이 없으므로 움직임으로 본다
  double motion = 0.0;           // 프레임 간 평균 차분(0~255)
  bool plate = false;            // 번호판 사각형을 찾았는가
  double sharp = 0.0;            // 번호판 ROI 라플라시안 분산
  std::string format = "unknown";  // new | old | unknown
  Reason reason = Reason::kMoving;
  std::vector<cv::Point> quad;   // 회전 보정된 프레임 좌표계의 네 꼭짓점
  std::vector<CharResult> chars;
  int plateW = 0;
  int plateH = 0;
  int w = 0;
  int h = 0;
  double ms = 0.0;
};

// ---------------------------------------------------------------------------
// 움직임 게이트 (계약 §9.1) — "정지 상태에서만 인식" 요구의 본체.
// 프레임 차분만 쓴다. 가속도 센서는 임베디드로 안 따라가므로 쓰지 않는다.
// ---------------------------------------------------------------------------
class MotionGate {
 public:
  struct Verdict {
    bool moving = true;
    double motion = 0.0;  // 프레임이 움직인 양(화소, 160x120 기준)
  };

  void configure(double motion_threshold, int still_frames);

  // gray 는 CV_8UC1. 내부에서 작게 줄여 비교하므로 원본 크기는 상관없다.
  Verdict update(const cv::Mat& gray);

  void reset();

 private:
  double motion_threshold_ = 2.0;
  int still_frames_ = 3;
  cv::Mat prev_small_;  // 이전 프레임의 축소본(CV_32F)
  cv::Mat hann_;        // 위상 상관용 창. 크기가 고정이라 한 번만 만든다
  int still_run_ = 0;   // 임계값 미만이 연속으로 나온 횟수
};

// ---------------------------------------------------------------------------
// stable/fresh 래치 (계약 §5.1). 전송 트리거를 코어가 만든다.
// OpenCV 타입에 의존하지 않는다 — 단독으로 시험 가능하게 떼어 뒀다.
// ---------------------------------------------------------------------------
class StableLatch {
 public:
  struct Verdict {
    bool stable = false;
    bool fresh = false;
  };

  void configure(int stable_frames);

  // value: 이번 프레임의 판독값(실패면 빈 문자열).
  // plate_found: 번호판 사각형을 찾았는가 — 재무장 규칙 2 의 기준(개정 2 에서 바뀐 부분).
  Verdict update(const std::string& value, bool plate_found);

  void reset();

 private:
  int stable_frames_ = 3;
  std::string last_value_;
  int run_ = 0;
  int no_plate_run_ = 0;
  std::string latched_value_;
  bool latched_ = false;
};

// ---------------------------------------------------------------------------
// 파이프라인 본체
// ---------------------------------------------------------------------------
class PlatePipeline {
 public:
  PlatePipeline();

  // 계약 §4: "vision.min_confidence=0.7\nvision.stable_frames=3\n..." 형태를 그대로 받는다.
  // 모르는 키는 무시하고, 값이 깨진 키는 이전 값을 그대로 유지한다(기본값으로 되돌리지 않는다).
  void configure(const std::string& kv_lines);

  const Config& config() const { return cfg_; }

  // gray 는 CV_8UC1. rotation_degrees 는 0/90/180/270(그 외는 회전하지 않음).
  // 어떤 입력에도 예외를 던지지 않고 전 필드가 채워진 Result 를 돌려준다.
  Result process(const cv::Mat& gray, int rotation_degrees);

  // 계약 §5 의 전 키를 채운 JSON. 키를 빼지 않는다.
  static std::string to_json(const Result& r);

  // 링크된 OpenCV 버전. 매크로(CV_VERSION)가 아니라 라이브러리 함수에서 얻는다 —
  // 매크로는 헤더에서 오므로 링크가 안 돼도 값이 나와 거짓 통과가 된다.
  static std::string version();

  // 단계별 중간값(투영 프로파일·NCC 점수 등)을 문자열로 남긴다. 임계값 튜닝용이고
  // 기본은 꺼져 있다. 안드로이드 로그가 아니라 문자열이라 임베디드에서도 그대로 쓸 수 있다.
  void set_debug(bool on) { debug_enabled_ = on; }
  const std::string& debug_log() const { return debug_log_; }

 private:
  // 래치 갱신과 처리시간 기록. 어느 경로로 빠져나가든 반드시 여기를 거친다 —
  // 조기 반환이 여러 개라 각 자리에서 따로 하면 하나는 반드시 빠뜨린다.
  Result finish(Result& r, long long t0);

  Config cfg_;
  MotionGate motion_;
  StableLatch latch_;
  bool debug_enabled_ = false;
  std::string debug_log_;
};

// 0/90/180/270 회전. 그 외 각도는 원본을 그대로 돌려준다.
cv::Mat rotate_gray(const cv::Mat& src, int rotation_degrees);

}  // namespace digitcam

#endif  // DIGITCAM_DIGIT_PIPELINE_HPP_
