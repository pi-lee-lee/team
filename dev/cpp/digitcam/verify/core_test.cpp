// core_test.cpp — digit_pipeline 코어의 계약(개정 2) 준수 검증. 에뮬레이터/기기에서 실행한다.
//
// 검증 대상은 계약으로 못박힌 것들이다:
//   §9.1 움직임 게이트 — 첫 프레임 moving, 연속 정지 카운트, 한 번 튀면 0 으로 끊김
//   §5.1 stable/fresh 상승엣지 + 재무장 두 규칙(개정 2: plate=false 기준)
//   §5   실패 프레임에도 전 키가 있고 reason 이 여섯 값 중 하나
//   §4   설정 문자열 파싱 — 모르는 키 무시, 깨진 값은 이전 값 유지
//   §4   value 의 한글이 JSON 을 통과하며 깨지지 않는다
//
// 인식 알고리즘이 들어오면 실제 샘플 이미지 케이스를 여기에 덧붙인다.

#include "digit_pipeline.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool ok, const std::string& what) {
  if (ok) {
    ++g_passed;
    std::printf("  ok   %s\n", what.c_str());
  } else {
    ++g_failed;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

// 래치 궤적을 문자열로: F=fresh, S=stable, .=아무것도 아님
std::string latch_trace(int stable_frames,
                        const std::vector<std::pair<std::string, bool>>& frames) {
  digitcam::StableLatch latch;
  latch.configure(stable_frames);
  std::string trace;
  for (const auto& f : frames) {
    const digitcam::StableLatch::Verdict v = latch.update(f.first, f.second);
    trace += v.fresh ? 'F' : (v.stable ? 'S' : '.');
  }
  return trace;
}

// 무늬가 있는 시험용 프레임. 위상 상관은 특징이 없는 단색 화면에서는 의미가 없으므로
// (이전 판의 단색 채움 시험은 새 지표에 쓸 수 없다) 실제 장면처럼 무늬를 넣는다.
cv::Mat textured_frame(int w, int h) {
  cv::Mat img(h, w, CV_8UC1, cv::Scalar(40));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int v = 40 + ((x * 7 + y * 13) % 71) + ((x / 23 + y / 17) % 3) * 40;
      img.at<unsigned char>(y, x) = static_cast<unsigned char>(std::min(255, v));
    }
  }
  cv::rectangle(img, cv::Rect(w / 4, h / 4, w / 3, h / 5), cv::Scalar(240), cv::FILLED);
  cv::rectangle(img, cv::Rect(w / 2, h / 2, w / 5, h / 6), cv::Scalar(10), cv::FILLED);
  return img;
}

// 움직임 궤적: M=moving, s=still.
// shifts 는 프레임마다의 **누적 이동량(화소, 원본 해상도 기준)** 이다.
std::string motion_trace(double threshold, int still_frames, const std::vector<int>& shifts) {
  digitcam::MotionGate gate;
  gate.configure(threshold, still_frames);
  const cv::Mat base = textured_frame(640, 480);

  std::string trace;
  for (const int dx : shifts) {
    cv::Mat frame;
    const cv::Matx23d m(1, 0, dx, 0, 1, 0);
    cv::warpAffine(base, frame, m, base.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    const digitcam::MotionGate::Verdict v = gate.update(frame);
    trace += v.moving ? 'M' : 's';
  }
  return trace;
}

bool has_all_keys(const std::string& json) {
  const char* keys[] = {"\"value\"", "\"conf\"",  "\"stable\"", "\"fresh\"",  "\"moving\"",
                        "\"motion\"", "\"plate\"", "\"sharp\"",  "\"format\"", "\"reason\"",
                        "\"quad\"",  "\"chars\"", "\"plateW\"", "\"plateH\"", "\"w\"",
                        "\"h\"",     "\"ms\""};
  for (const char* k : keys) {
    if (json.find(k) == std::string::npos) {
      return false;
    }
  }
  return true;
}

bool reason_is_valid(const std::string& json) {
  const char* allowed[] = {"\"reason\":\"ok\"",           "\"reason\":\"moving\"",
                           "\"reason\":\"no_plate\"",     "\"reason\":\"blurry\"",
                           "\"reason\":\"segment_fail\"", "\"reason\":\"low_conf\""};
  for (const char* a : allowed) {
    if (json.find(a) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  std::printf("OpenCV = %s\n", digitcam::PlatePipeline::version().c_str());

  // --- §9.1 움직임 게이트 ------------------------------------------------------
  // motion 은 '프레임이 몇 화소 움직였는가'(160x120 기준)다. 원본 640x480 에서 4배 축소되므로
  // 원본 16px 이동 ≈ 4px, 4px 이동 ≈ 1px 로 잡힌다. 임계 2.0 은 그 사이다.
  std::printf("[움직임 게이트] threshold=2.0(화소) still_frames=3\n");
  {
    // 첫 프레임은 비교 대상이 없어 moving. 2·3·4 번째가 임계 미만 3연속이 되는 순간 정지다.
    const std::string t = motion_trace(2.0, 3, {0, 0, 0, 0, 0, 0});
    check(t == "MMMsss", "고정 화면: 첫 프레임 moving, 이후 3연속이면 정지  실제=" + t);
  }
  {
    // 손떨림 수준(원본 ±3px)은 **정지로 판정되어야 한다** — 손에 든 이상 늘 생기는 흔들림이다.
    const std::string t = motion_trace(2.0, 3, {0, 2, -1, 3, -2, 1, 2});
    check(t == "MMMssss", "손떨림(±3px)은 정지로 본다  실제=" + t);
  }
  {
    // 실제 이동(프레임마다 원본 16px)은 계속 움직임이어야 한다.
    const std::string t = motion_trace(2.0, 3, {0, 16, 32, 48, 64, 80});
    check(t == "MMMMMM", "연속 이동(16px/프레임)은 계속 움직임  실제=" + t);
  }
  {
    // 정지해 있다가 한 번 크게 움직이면 연속 카운트가 0 으로 끊긴다 — 감소가 아니다.
    const std::string t = motion_trace(2.0, 3, {0, 0, 0, 0, 40, 40, 40, 40});
    check(t == "MMMsMMMs", "한 프레임 크게 튀면 카운트가 0 으로 끊긴다  실제=" + t);
  }
  {
    // 임계를 크게 잡으면 그 이동량도 정지로 본다(설정이 실제로 먹는지 확인).
    const std::string t = motion_trace(100.0, 1, {0, 40, 80});
    check(t == "Mss", "임계가 크면 큰 이동도 정지로 본다  실제=" + t);
  }

  // --- §5.1 래치 --------------------------------------------------------------
  std::printf("[래치] stable_frames=3\n");
  {
    const std::string t = latch_trace(3, {{"123가4568", true},
                                          {"123가4568", true},
                                          {"123가4568", true},
                                          {"123가4568", true},
                                          {"123가4568", true}});
    check(t == "..FSS", "같은 값 5프레임 → fresh 는 3번째 한 프레임만  실제=" + t);
  }
  {
    const std::string t = latch_trace(3, {{"111가1111", true},
                                          {"111가1111", true},
                                          {"111가1111", true},
                                          {"222나2222", true},
                                          {"222나2222", true},
                                          {"222나2222", true}});
    check(t == "..F..F", "재무장 규칙 1: 값이 바뀌면 다시 fresh  실제=" + t);
  }
  {
    // 재무장 규칙 2(개정 2): plate=false 프레임이 stable_frames 회 연속.
    const std::string t = latch_trace(3, {{"111가1111", true},
                                          {"111가1111", true},
                                          {"111가1111", true},
                                          {"", false},
                                          {"", false},
                                          {"", false},
                                          {"111가1111", true},
                                          {"111가1111", true},
                                          {"111가1111", true}});
    check(t == "..F.....F", "재무장 규칙 2: 번호판 3프레임 실종 뒤 같은 값이면 다시 fresh  실제=" + t);
  }
  {
    const std::string t = latch_trace(3, {{"111가1111", true},
                                          {"111가1111", true},
                                          {"111가1111", true},
                                          {"", false},
                                          {"", false},
                                          {"111가1111", true},
                                          {"111가1111", true},
                                          {"111가1111", true}});
    check(t == "..F....S", "실종 2프레임으로는 재무장되지 않는다  실제=" + t);
  }
  {
    // 번호판은 보이는데(plate=true) 판독만 실패한 프레임은 재무장 대상이 아니다.
    const std::string t = latch_trace(3, {{"111가1111", true},
                                          {"111가1111", true},
                                          {"111가1111", true},
                                          {"", true},
                                          {"", true},
                                          {"", true},
                                          {"111가1111", true},
                                          {"111가1111", true},
                                          {"111가1111", true}});
    check(t == "..F.....S", "plate=true 인 실패 프레임은 재무장하지 않는다  실제=" + t);
  }

  {
    // 어수선한 배경에서 사각형 후보만 잡히는 프레임(plate=true, segment_fail)이 이어져도
    // 재무장이 되어야 한다. 파이프라인은 '사각형을 찾았나' 가 아니라 '문자 구조까지 확인됐나'를
    // 래치에 넘긴다 — 그러지 않으면 배경 잡동사니 때문에 같은 차가 다시 와도 fresh 가 안 뜬다.
    const std::string t = latch_trace(3, {{"700가1234", true},
                                          {"700가1234", true},
                                          {"700가1234", true},
                                          {"", false},   // 책장 모서리만 잡힌 프레임
                                          {"", false},
                                          {"", false},
                                          {"700가1234", true},
                                          {"700가1234", true},
                                          {"700가1234", true}});
    check(t == "..F.....F", "구조 미확인 프레임 3회 뒤 같은 차가 오면 다시 fresh  실제=" + t);
  }

  // --- §4 설정 파싱 ------------------------------------------------------------
  std::printf("[설정 파싱]\n");
  {
    digitcam::PlatePipeline p;
    p.configure(
        "vision.min_confidence=0.85\n"
        "vision.stable_frames=7\n"
        "vision.motion_threshold=1.25\n"
        "vision.still_frames=9\n"
        "vision.min_sharpness=120\n"
        "vision.min_plate_width_ratio=0.4\n"
        "vision.plate_format=old\n"
        "vision.gate_bypass=true\n");
    const digitcam::Config& c = p.config();
    check(c.min_confidence > 0.849 && c.min_confidence < 0.851, "min_confidence 반영");
    check(c.stable_frames == 7, "stable_frames 반영");
    check(c.motion_threshold > 1.24 && c.motion_threshold < 1.26, "motion_threshold 반영");
    check(c.still_frames == 9, "still_frames 반영");
    check(c.min_sharpness > 119.9 && c.min_sharpness < 120.1, "min_sharpness 반영");
    check(c.min_plate_width_ratio > 0.39 && c.min_plate_width_ratio < 0.41,
          "min_plate_width_ratio 반영");
    check(c.plate_format == "old", "plate_format 반영");
    check(c.gate_bypass, "gate_bypass 반영");
  }
  {
    digitcam::PlatePipeline p;
    p.configure("vision.stable_frames=abc\nvision.min_confidence=\nvision.plate_format=weird\n");
    const digitcam::Config& c = p.config();
    check(c.stable_frames == 3, "깨진 정수값은 이전 값 유지(0/1 로 바뀌지 않는다)");
    check(c.min_confidence > 0.69 && c.min_confidence < 0.71, "빈 값은 이전 값 유지");
    check(c.plate_format == "auto", "허용되지 않은 열거값은 무시");
  }
  {
    digitcam::PlatePipeline p;
    p.configure("# 주석\n\n  vision.stable_frames = 4  \nserver.host=1.2.3.4\nnonsense\nstable_frames=5\n");
    check(p.config().stable_frames == 5,
          "주석·빈 줄·공백·모르는 키를 넘기고 접두어 없는 키도 받는다");
  }

  // --- §5 JSON ----------------------------------------------------------------
  std::printf("[JSON]\n");
  {
    digitcam::PlatePipeline p;
    cv::Mat gray(640, 480, CV_8UC1, cv::Scalar(30));
    const digitcam::Result r = p.process(gray, 0);
    const std::string json = digitcam::PlatePipeline::to_json(r);
    std::printf("  %s\n", json.c_str());
    check(has_all_keys(json), "전 키가 있다");
    check(reason_is_valid(json), "reason 이 계약 §5.2 의 여섯 값 중 하나");
    check(json.find("\"moving\":true") != std::string::npos, "첫 프레임은 moving");
    check(json.find("\"reason\":\"moving\"") != std::string::npos, "첫 프레임 reason=moving");
    check(r.w == 480 && r.h == 640, "회전 0 → w/h 그대로");
  }
  {
    // 정지 상태로 진입한 뒤에는 게이트를 지나 다음 단계로 간다.
    digitcam::PlatePipeline p;
    p.configure("vision.still_frames=2\n");
    cv::Mat gray(640, 480, CV_8UC1, cv::Scalar(30));
    std::string json;
    for (int i = 0; i < 4; ++i) {
      json = digitcam::PlatePipeline::to_json(p.process(gray, 0));
    }
    std::printf("  %s\n", json.c_str());
    check(json.find("\"moving\":false") != std::string::npos, "정지 프레임이 쌓이면 moving=false");
    check(reason_is_valid(json), "정지 후에도 reason 이 유효값");
  }
  {
    digitcam::PlatePipeline p;
    const digitcam::Result r = p.process(cv::Mat(), 90);
    const std::string json = digitcam::PlatePipeline::to_json(r);
    check(has_all_keys(json), "빈 Mat 입력에도 전 키가 있다");
    check(r.w == 0 && r.h == 0, "빈 Mat 은 w=h=0");
  }
  {
    digitcam::PlatePipeline p;
    cv::Mat gray(640, 480, CV_8UC1, cv::Scalar(0));
    check(p.process(gray, 90).w == 640, "회전 90 → w/h 가 뒤바뀐다");
    check(p.process(gray, 45).w == 480, "90 의 배수가 아니면 회전하지 않는다");
  }

  // --- §4 한글이 JSON 을 통과해도 깨지지 않는가 (UTF-8) ------------------------
  std::printf("[UTF-8]\n");
  {
    digitcam::Result r;
    r.value = "123가4568";
    r.format = "new";
    r.reason = digitcam::Reason::kOk;
    r.conf = 0.82;
    r.plate = true;
    r.plateW = 520;
    r.plateH = 110;
    r.quad = {{120, 300}, {460, 296}, {462, 372}, {118, 376}};
    r.chars = {{"1", 0.93, {14, 18, 34, 62}}, {"가", 0.71, {130, 16, 40, 64}}};
    const std::string json = digitcam::PlatePipeline::to_json(r);
    std::printf("  %s\n", json.c_str());
    check(json.find("\"value\":\"123가4568\"") != std::string::npos,
          "value 의 한글이 원문 UTF-8 그대로 나온다");
    check(json.find("\"c\":\"가\"") != std::string::npos, "chars[].c 의 한글도 그대로");
    check(json.find("\\u") == std::string::npos, "한글을 제어문자로 오인해 이스케이프하지 않는다");
    // 실제 바이트열까지 확인한다 — '가' 는 UTF-8 로 EA B0 80.
    const std::string ga = "\xea\xb0\x80";
    check(json.find(ga) != std::string::npos, "'가' 의 바이트열 EA B0 80 이 그대로 들어 있다");
    check(json.find("\"quad\":[[120,300],[460,296],[462,372],[118,376]]") != std::string::npos,
          "quad 는 [x,y] 4점 배열");
    check(json.find("\"reason\":\"ok\"") != std::string::npos, "reason=ok 직렬화");
  }

  std::printf("\n통과 %d · 실패 %d\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
