// 공유 라이브러리 쪽 스파이크: 앱에 들어갈 libdigitcam.so 와 같은 형태(SHARED)에
// OpenCV core+imgproc 심볼이 정적으로 묻어 들어가는지 본다.
// jni.h 를 쓰지 않는다 — 링크 성질만 보는 코드다.

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <string>

extern "C" const char* digitcam_spike_version() {
  // 매크로(CV_VERSION)가 아니라 라이브러리 안의 함수를 부른다.
  // 매크로는 헤더에서 오므로 링크가 안 돼도 문자열이 나와 거짓 통과가 된다.
  static const std::string v = cv::getVersionString();
  return v.c_str();
}

extern "C" int digitcam_spike_threshold_smoke() {
  cv::Mat src(64, 64, CV_8UC1, cv::Scalar(0));
  cv::rectangle(src, cv::Rect(16, 16, 32, 32), cv::Scalar(255), cv::FILLED);

  cv::Mat blurred;
  cv::GaussianBlur(src, blurred, cv::Size(5, 5), 0);

  cv::Mat bin;
  cv::adaptiveThreshold(blurred, bin, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                        cv::THRESH_BINARY, 11, 2);

  cv::Mat labels, stats, centroids;
  return cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
}
