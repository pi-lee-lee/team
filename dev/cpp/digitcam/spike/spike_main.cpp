// 에뮬레이터/기기에서 직접 돌려 링크를 증명하는 실행 파일.
// adb push 후 실행하면 OpenCV 버전과 imgproc 호출 결과를 찍는다.

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>

extern "C" const char* digitcam_spike_version();
extern "C" int digitcam_spike_threshold_smoke();

int main() {
  // 매크로가 아니라 라이브러리 함수 — 이 값이 찍히면 정말로 링크된 것이다.
  std::printf("getVersionString  = %s\n", cv::getVersionString().c_str());
  std::printf("CV_VERSION macro  = %s  (헤더에서 온 값. 링크 증거가 아니다)\n", CV_VERSION);

  cv::Mat src(64, 64, CV_8UC1, cv::Scalar(0));
  cv::rectangle(src, cv::Rect(16, 16, 32, 32), cv::Scalar(255), cv::FILLED);
  cv::Mat bin;
  cv::adaptiveThreshold(src, bin, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 11, 2);
  cv::Mat labels, stats, centroids;
  const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
  std::printf("connectedComponents(라벨수, 배경 포함) = %d\n", n);

  cv::Mat warped;
  const cv::Point2f srcq[4] = {{0, 0}, {63, 0}, {63, 63}, {0, 63}};
  const cv::Point2f dstq[4] = {{0, 0}, {40, 2}, {38, 30}, {2, 28}};
  cv::warpPerspective(src, warped, cv::getPerspectiveTransform(srcq, dstq), cv::Size(48, 32));
  std::printf("warpPerspective 결과 크기 = %dx%d\n", warped.cols, warped.rows);

  return 0;
}
