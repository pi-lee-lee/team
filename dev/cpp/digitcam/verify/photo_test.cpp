// photo_test.cpp — 실사용 환경 사진(JPEG)으로 인식률과 해상도 스윕을 재는 하니스
//
// 앱의 촬영본 경로(PlateImage.fromJpeg → GrayConvert → nativeProcessGray)를 데스크톱에서
// 재현한다. 앱과 같은 digit_pipeline.cpp 를 그대로 링크하므로 여기서 나온 값이 앱의 값이다.
//
// ⚠ 재현이 완전하지 않은 지점 하나: 축소 보간이다.
//   앱      Bitmap.createScaledBitmap(filter=true)  → Skia bilinear
//   여기    cv::resize(..., INTER_LINEAR)           → OpenCV bilinear
//   같은 계열이지만 화소 단위로 동일하지는 않다. --interp area 로 다른 보간도 잴 수 있다.
//   그 밖의 단계(축소 크기 산술·휘도 계수·회전 처리·게이트 값)는 앱과 같다.
//
// ⚠ imgcodecs 는 **입력을 읽는 데만** 쓴다. 인식 경로(digit_pipeline)는 core+imgproc 만
//   쓰므로 앱의 링크 조건과 갈리지 않는다.
//
// 🔴 EXIF 회전을 **무시한다**(IMREAD_IGNORE_ORIENTATION). 앱의 BitmapFactory 도 무시하고
//   회전각을 인자로 넘기기 때문이다(PlateImage.kt "여기서 비트맵을 돌리면 회전이 두 번").
//   기본 imread 는 EXIF 를 적용해서 세로로 찍은 사진이 **가로/세로가 뒤바뀐 채** 들어온다
//   (실측: orientation=6 을 붙이면 4032x2268 이 프레임 540x960 으로 들어왔다).
//   회전이 필요하면 --rotate 로 앱과 같은 경로(회전각 인자)를 태워라.
//
// 사용법:
//   photo_test <이미지 디렉터리> [옵션]
//     --max-edge 960,1440,1920,2688,0   0 은 무축소(원본)
//     --expect 123바9898                정답. 전체/숫자/한자틀림 채점에 쓴다
//     --interp linear|area              축소 보간(기본 linear)
//     --ratio 0.25                      vision.min_plate_width_ratio (판장축/프레임폭 하한)
//     --rotate 0|90|180|270             인식기에 넘기는 회전각(앱의 rotationDegrees 와 같은 자리)
//     --only <파일명 일부>
//     --dump-dir <경로>                 검출 사각형을 그린 PNG 를 남긴다(오검출 확인용)
//     --models <디렉터리>               표준 경로(검출망+CTC) 가중치를 실어 같이 돌린다
//     --ocr-limit 960                   vision.ocr_det_limit — **--max-edge 와 다른 축이다**
//         --max-edge  : 앱이 네이티브에 넘기기 전 **프레임 자체**를 줄이는 상한(GrayConvert)
//                       → 인식망이 잘라 쓸 **원본 화소**가 여기서 정해진다
//         --ocr-limit : 그 프레임을 **검출망에 넣기 전에만** 줄이는 상한
//                       → 문자 크롭은 줄인 그림이 아니라 **넘겨받은 프레임에서** 잘라낸다
//         🔑 그래서 둘을 같이 재야 교환비가 보인다. 하나만 올리면 포화한다
//     --debug                           단계별 중간값

#include "digit_pipeline.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// GrayConvert.scaledSize 와 같은 산술.
// 🔴 비율을 float 으로 계산한다. double 로 바꾸면 크기가 1 어긋나 앱과 갈린다
//    (GrayConvert.kt 의 같은 경고와 짝이다).
cv::Size scaled_size(int w, int h, int max_edge) {
  const int longest = std::max(w, h);
  if (max_edge <= 0 || longest <= max_edge) {
    return cv::Size(w, h);
  }
  const float ratio = static_cast<float>(max_edge) / static_cast<float>(longest);
  int tw = static_cast<int>(static_cast<float>(w) * ratio);
  int th = static_cast<int>(static_cast<float>(h) * ratio);
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  return cv::Size(tw, th);
}

// GrayConvert.luminance 와 같은 산술: BT.601 정수 계수 + **절삭**.
// cv::cvtColor(BGR2GRAY) 는 반올림이라 화소 대부분이 1 차이 난다 — 그래서 직접 계산한다.
cv::Mat luminance_bt601(const cv::Mat& bgr) {
  cv::Mat out(bgr.rows, bgr.cols, CV_8UC1);
  for (int y = 0; y < bgr.rows; ++y) {
    const cv::Vec3b* src = bgr.ptr<cv::Vec3b>(y);
    unsigned char* dst = out.ptr<unsigned char>(y);
    for (int x = 0; x < bgr.cols; ++x) {
      const int b = src[x][0];
      const int g = src[x][1];
      const int r = src[x][2];
      dst[x] = static_cast<unsigned char>((r * 299 + g * 587 + b * 114) / 1000);
    }
  }
  return out;
}

// 목록에 든 것과 **건너뛴 것**을 같이 돌려준다.
// 🔴 조용히 빠지면 안 된다 — 새 자료가 .heic 로 오면 목록이 그냥 짧아지고, 그 짧은 분모로
//    "N장 중 M장" 을 세게 된다. imgcodecs 는 HEIC 를 읽지 못한다.
std::vector<std::string> list_images(const std::string& dir, std::vector<std::string>* skipped) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (d == nullptr) {
    return out;
  }
  while (const dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.size() < 4 || name[0] == '.') {
      continue;
    }
    std::string lower = name;
    for (char& c : lower) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // .pgm 을 받는 이유: 앱이 nativeProcessGray 에 **실제로 넘긴 바이트**(SampleImages.dumpPgm)를
    // 재변환 없이 그대로 먹이기 위해서다. PGM 은 8비트 1채널이라 IMREAD_COLOR 가 세 채널을
    // 같은 값으로 채우고, luminance_bt601 의 (299+587+114)/1000 이 정확히 1 이 되어 **항등**이다
    // — 즉 화소가 한 칸도 안 바뀐다. 축소도 장축이 상한 이하면 일어나지 않는다.
    const bool is_img = lower.rfind(".jpg") == lower.size() - 4 ||
                        lower.rfind(".png") == lower.size() - 4 ||
                        lower.rfind(".pgm") == lower.size() - 4 ||
                        lower.rfind(".jpeg") == lower.size() - 5;
    if (is_img) {
      out.push_back(name);
    } else if (skipped != nullptr) {
      skipped->push_back(name);
    }
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  if (skipped != nullptr) {
    std::sort(skipped->begin(), skipped->end());
  }
  return out;
}

std::vector<unsigned char> read_bytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return {};
  }
  return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
}

// UTF-8 문자 단위로 쪼갠다. 번호판 값에 한글이 한 자 들어가므로 바이트로 세면 안 된다.
std::vector<std::string> utf8_chars(const std::string& s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len = 1;
    if ((c & 0xF8) == 0xF0) {
      len = 4;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2;
    }
    if (i + len > s.size()) {
      len = s.size() - i;
    }
    out.push_back(s.substr(i, len));
    i += len;
  }
  return out;
}

std::string digits_only(const std::string& s) {
  std::string out;
  for (const char c : s) {
    if (c >= '0' && c <= '9') {
      out += c;
    }
  }
  return out;
}

// 성공을 셋으로 가른다 — 상한을 올리면 「한 자 틀림」이 「전체 일치」로 옮겨간다.
// 이진 지표는 그 이동을 못 본다.
enum class Grade { kExact, kDigits, kOneOff, kWrong, kEmpty };

const char* grade_name(Grade g) {
  switch (g) {
    case Grade::kExact: return "전체일치";
    case Grade::kDigits: return "숫자일치";
    case Grade::kOneOff: return "한자틀림";
    case Grade::kWrong: return "틀림";
    case Grade::kEmpty: return "값없음";
  }
  return "?";
}

Grade grade(const std::string& got, const std::string& want) {
  if (got.empty()) {
    return Grade::kEmpty;
  }
  if (got == want) {
    return Grade::kExact;
  }
  if (!want.empty() && digits_only(got) == digits_only(want)) {
    return Grade::kDigits;
  }
  const std::vector<std::string> a = utf8_chars(got);
  const std::vector<std::string> b = utf8_chars(want);
  if (a.size() == b.size()) {
    int diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i] != b[i]) {
        ++diff;
      }
    }
    if (diff == 1) {
      return Grade::kOneOff;
    }
  }
  return Grade::kWrong;
}

double quad_long_side(const std::vector<cv::Point>& q) {
  if (q.size() != 4) {
    return 0.0;
  }
  const double a = cv::norm(q[1] - q[0]);
  const double b = cv::norm(q[3] - q[0]);
  return std::max(a, b);
}

double quad_short_side(const std::vector<cv::Point>& q) {
  if (q.size() != 4) {
    return 0.0;
  }
  const double a = cv::norm(q[1] - q[0]);
  const double b = cv::norm(q[3] - q[0]);
  return std::min(a, b);
}

std::string quad_text(const std::vector<cv::Point>& q, int w, int h) {
  if (q.size() != 4 || w <= 0 || h <= 0) {
    return "(없음)";
  }
  char buf[224];
  std::snprintf(buf, sizeof(buf),
                "px[(%d,%d)(%d,%d)(%d,%d)(%d,%d)] norm[(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)]",
                q[0].x, q[0].y, q[1].x, q[1].y, q[2].x, q[2].y, q[3].x, q[3].y,
                q[0].x / static_cast<double>(w), q[0].y / static_cast<double>(h),
                q[1].x / static_cast<double>(w), q[1].y / static_cast<double>(h),
                q[2].x / static_cast<double>(w), q[2].y / static_cast<double>(h),
                q[3].x / static_cast<double>(w), q[3].y / static_cast<double>(h));
  return buf;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf(
        "사용법: photo_test <이미지 디렉터리> [--max-edge 960,1440,...] [--expect 값]\n"
        "        [--ratio 0.25] [--rotate 0|90|180|270] [--interp linear|area] [--only 이름]\n"
        "        [--dump-dir 경로] [--debug]\n");
    return 2;
  }

  const std::string dir = argv[1];
  std::vector<int> edges = {960, 1440, 1920, 2688, 0};
  std::string expect;
  std::string only;
  std::string dump_dir;
  std::string model_dir;
  bool debug = false;
  double ratio = 0.25;  // 앱 정본값. 손잡이 효과를 재려고 밖에서 바꿀 수 있게 뒀다
  int ocr_limit = 960;  // vision.ocr_det_limit 기본값
  int rotate = 0;       // 앱의 rotationDegrees 와 같은 자리. 비트맵을 돌리지 않는다
  int interp = cv::INTER_LINEAR;
  const char* interp_name = "linear";

  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--max-edge") == 0 && i + 1 < argc) {
      edges.clear();
      std::string spec = argv[++i];
      size_t pos = 0;
      while (pos <= spec.size()) {
        const size_t comma = spec.find(',', pos);
        const std::string tok = spec.substr(pos, comma == std::string::npos ? std::string::npos
                                                                           : comma - pos);
        if (!tok.empty()) {
          edges.push_back(std::atoi(tok.c_str()));
        }
        if (comma == std::string::npos) {
          break;
        }
        pos = comma + 1;
      }
    } else if (std::strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
      expect = argv[++i];
    } else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
      only = argv[++i];
    } else if (std::strcmp(argv[i], "--dump-dir") == 0 && i + 1 < argc) {
      dump_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--rotate") == 0 && i + 1 < argc) {
      rotate = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--ratio") == 0 && i + 1 < argc) {
      ratio = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--interp") == 0 && i + 1 < argc) {
      const std::string v = argv[++i];
      if (v == "area") {
        interp = cv::INTER_AREA;
        interp_name = "area";
      }
    } else if (std::strcmp(argv[i], "--models") == 0 && i + 1 < argc) {
      model_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--ocr-limit") == 0 && i + 1 < argc) {
      ocr_limit = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--debug") == 0) {
      debug = true;
    }
  }

  std::vector<unsigned char> det_bytes;
  std::vector<unsigned char> rec_bytes;
  std::string dict_text;
  if (!model_dir.empty()) {
    det_bytes = read_bytes(model_dir + "/ppocr_det_v4.onnx");
    rec_bytes = read_bytes(model_dir + "/korean_rec_static.onnx");
    const std::vector<unsigned char> d = read_bytes(model_dir + "/korean_dict.txt");
    dict_text.assign(d.begin(), d.end());
    if (det_bytes.empty() || rec_bytes.empty() || dict_text.empty()) {
      std::printf("🔴 모델을 못 읽었다 — 템플릿 전용으로 돈다\n");
      det_bytes.clear();
    } else {
      std::printf("표준 경로 켜짐 (det %.2fMB · rec %.2fMB · dict %.2fMB)\n",
                  det_bytes.size() / 1048576.0, rec_bytes.size() / 1048576.0,
                  dict_text.size() / 1048576.0);
    }
  }

  std::vector<std::string> skipped;
  const std::vector<std::string> files = list_images(dir, &skipped);
  if (files.empty()) {
    std::printf("이미지를 찾지 못했다: %s\n", dir.c_str());
    if (!skipped.empty()) {
      std::printf("🔴 읽을 수 없는 형식 %zu개를 건너뛰었다(첫 항목: %s). "
                  "HEIC 는 지원하지 않는다 — JPEG/PNG 로 바꿔라.\n",
                  skipped.size(), skipped[0].c_str());
    }
    return 2;
  }
  if (!skipped.empty()) {
    // 분모가 조용히 줄어드는 것을 막는다. 이 줄이 없으면 "8장 중" 이 "5장 중" 이 되어도 모른다.
    std::printf("🔴 건너뛴 파일 %zu개 (지원 형식이 아니다 — HEIC 는 못 읽는다):", skipped.size());
    for (const std::string& sk : skipped) {
      std::printf(" %s", sk.c_str());
    }
    std::printf("\n");
  }
  // 하위 디렉터리는 훑지 않는다. 자료가 폴더로 나뉘어 오면 폴더마다 따로 돌려라.

  // 앱 정본(assets/server.properties)의 vision.* 값 그대로.
  // still_frames·motion_threshold 도 정본을 쓰고, 같은 프레임을 6회 먹여 움직임 게이트를
  // **우회하지 않고 통과**시킨다(gate_bypass 를 켜면 선명도 게이트까지 꺼져 조건이 달라진다).
  char cfg_buf[512];
  std::snprintf(cfg_buf, sizeof(cfg_buf),
                "vision.min_confidence=0.70\n"
                "vision.stable_frames=3\n"
                "vision.motion_threshold=2.0\n"
                "vision.still_frames=3\n"
                "vision.min_sharpness=40\n"
                "vision.min_plate_width_ratio=%.4f\n"
                "vision.plate_format=auto\n"
                "vision.gate_bypass=false\n"
                "vision.ocr_det_limit=%d\n",
                ratio, ocr_limit);
  const char* kCfg = cfg_buf;

  std::printf("OpenCV %s · 사진 %zu장 · 상한 %zu가지 · 보간 %s\n",
              digitcam::PlatePipeline::version().c_str(), files.size(), edges.size(), interp_name);
  std::printf("설정: min_conf=0.70 min_sharp=40 min_plate_width_ratio=%.4f format=auto "
              "gate_bypass=false · 회전각=%d · EXIF무시 · ocr_det_limit=%d\n", ratio, rotate,
              ocr_limit);
  std::printf("정답: %s\n\n", expect.empty() ? "(주지 않음)" : expect.c_str());

  // 표 헤더 — TSV 로도 읽을 수 있게 탭으로 나눈다.
  std::printf("파일\t상한\t프레임\t결과\t등급\treason\t경로\tconf\tsharp\t판장축px\t판폭비\t"
              "판장축/520\t문자폭\t문자높이\t셀→24x32배율\tms\n");

  struct Row {
    std::string file;
    int edge;
    Grade g;
    double ms;
  };
  std::vector<Row> rows;

  for (const std::string& file : files) {
    if (!only.empty() && file.find(only) == std::string::npos) {
      continue;
    }

    // 🔴 IGNORE_ORIENTATION 이 핵심이다 — 앱과 같은 픽셀을 얻는 유일한 방법이다(머리 주석 참조).
    const cv::Mat src =
        cv::imread(dir + "/" + file, cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
    if (src.empty()) {
      std::printf("%s\t-\t-\t(읽기실패)\n", file.c_str());
      continue;
    }

    for (const int edge : edges) {
      const cv::Size target = scaled_size(src.cols, src.rows, edge);

      cv::Mat resized;
      if (target.width == src.cols && target.height == src.rows) {
        resized = src;
      } else {
        cv::resize(src, resized, target, 0, 0, interp);
      }
      const cv::Mat gray = luminance_bt601(resized);

      digitcam::PlatePipeline p;
      p.configure(kCfg);
      if (!det_bytes.empty()) {
        p.load_models(det_bytes, rec_bytes, dict_text);
      }
      p.set_debug(debug);

      digitcam::Result r;
      for (int i = 0; i < 6; ++i) {
        r = p.process(gray, rotate);
      }

      const double long_side = quad_long_side(r.quad);
      const double short_side = quad_short_side(r.quad);
      const double width_ratio = r.w > 0 ? long_side / r.w : 0.0;
      // 정규화판(신형 520x110)을 만들 때의 배율. 1 미만이면 warpPerspective 가 **확대 보간**이다
      // — 원본에 없는 화소를 만들어 문자를 읽는다는 뜻이다.
      const double warp_scale = long_side / 520.0;

      // 문자 셀 크기는 정규화판 좌표계다(chars[].box). 셀 → 24x32 리사이즈 배율이
      // 1 미만이면 그 단계에서 또 확대된다.
      int cw_min = 0;
      int ch_min = 0;
      if (!r.chars.empty()) {
        cw_min = r.chars[0].box.w;
        ch_min = r.chars[0].box.h;
        for (const digitcam::CharResult& c : r.chars) {
          cw_min = std::min(cw_min, c.box.w);
          ch_min = std::min(ch_min, c.box.h);
        }
      }
      const double cell_scale =
          cw_min > 0 ? std::min(cw_min / 24.0, ch_min / 32.0) : 0.0;

      const Grade g = grade(r.value, expect);
      rows.push_back({file, edge, g, r.ms});

      // 🔴 `경로` 를 conf 바로 앞에 둔다 — **두 경로의 conf 는 눈금이 다르다.**
      //    붙여 놓지 않으면 표를 읽는 사람이 두 자를 한 열로 센다.
      std::printf("%s\t%d\t%dx%d\t%s\t%s\t%s\t%s\t%.2f\t%.1f\t%.0f\t%.3f\t%.2f\t%d\t%d\t%.2f\t%.1f\n",
                  file.c_str(), edge, r.w, r.h, r.value.empty() ? "(없음)" : r.value.c_str(),
                  grade_name(g), digitcam::reason_name(r.reason), r.engine.c_str(), r.conf,
                  r.sharp, long_side,
                  width_ratio, warp_scale, cw_min, ch_min, cell_scale, r.ms);

      // 검출 사각형 좌표 — 오검출이면 이 좌표가 번호판이 아닌 곳을 가리킨다.
      std::printf("    판 %s 단축=%.0f 형식=%s 정규화후판=%dx%d\n",
                  quad_text(r.quad, r.w, r.h).c_str(), short_side, r.format.c_str(), r.plateW,
                  r.plateH);
      if (!r.chars.empty()) {
        std::printf("    문자:");
        for (const digitcam::CharResult& c : r.chars) {
          std::printf(" %s(%.2f,%dx%d)", c.c.c_str(), c.conf, c.box.w, c.box.h);
        }
        std::printf("\n");
      }
      if (debug) {
        std::printf("%s", p.debug_log().c_str());
      }

      if (!dump_dir.empty() && r.quad.size() == 4) {
        const std::string stem = file.substr(0, file.find_last_of('.'));
        char name[512];

        cv::Mat vis;
        cv::cvtColor(gray, vis, cv::COLOR_GRAY2BGR);
        std::vector<std::vector<cv::Point>> polys = {r.quad};
        cv::polylines(vis, polys, true, cv::Scalar(0, 0, 255), std::max(2, vis.cols / 400));
        cv::circle(vis, r.quad[0], std::max(4, vis.cols / 200), cv::Scalar(0, 255, 0), -1);
        std::snprintf(name, sizeof(name), "%s/%s-e%d-frame.png", dump_dir.c_str(), stem.c_str(),
                      edge);
        cv::imwrite(name, vis);

        // 채택된 quad 를 read_plate 와 같은 산술로 펴서 남긴다.
        // 🔑 이 그림 한 장이 "검출이 번호판을 잡았나" 에 답한다 — 좌표만으로는 안 갈린다.
        const int nw = r.format == "old" ? 335 : 520;
        const int nh = r.format == "old" ? 155 : 110;
        const cv::Point2f dst[4] = {{0.0f, 0.0f},
                                    {static_cast<float>(nw - 1), 0.0f},
                                    {static_cast<float>(nw - 1), static_cast<float>(nh - 1)},
                                    {0.0f, static_cast<float>(nh - 1)}};
        const cv::Point2f src4[4] = {
            cv::Point2f(static_cast<float>(r.quad[0].x), static_cast<float>(r.quad[0].y)),
            cv::Point2f(static_cast<float>(r.quad[1].x), static_cast<float>(r.quad[1].y)),
            cv::Point2f(static_cast<float>(r.quad[2].x), static_cast<float>(r.quad[2].y)),
            cv::Point2f(static_cast<float>(r.quad[3].x), static_cast<float>(r.quad[3].y))};
        cv::Mat norm_plate;
        cv::warpPerspective(gray, norm_plate, cv::getPerspectiveTransform(src4, dst),
                            cv::Size(nw, nh), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        std::snprintf(name, sizeof(name), "%s/%s-e%d-plate.png", dump_dir.c_str(), stem.c_str(),
                      edge);
        cv::imwrite(name, norm_plate);

        // char_mask 의 첫 단계와 같은 이진화. 문자 성분이 왜 안 잡히는지는 이 그림이 답한다.
        cv::Mat bin;
        cv::threshold(norm_plate, bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
        std::snprintf(name, sizeof(name), "%s/%s-e%d-mask.png", dump_dir.c_str(), stem.c_str(),
                      edge);
        cv::imwrite(name, bin);
      }
    }
    std::printf("\n");
  }

  // 상한별 집계 — 등급 이동이 보여야 손잡이의 효과를 읽을 수 있다.
  std::printf("\n상한별 집계 (분모 = 사진 %zu장)\n", files.size());
  std::printf("상한\t전체일치\t숫자일치\t한자틀림\t틀림\t값없음\t평균ms\t최대ms\n");
  for (const int edge : edges) {
    int ex = 0, dg = 0, oo = 0, wr = 0, em = 0, n = 0;
    double sum = 0.0;
    double mx = 0.0;
    for (const Row& row : rows) {
      if (row.edge != edge) {
        continue;
      }
      ++n;
      sum += row.ms;
      mx = std::max(mx, row.ms);
      switch (row.g) {
        case Grade::kExact: ++ex; break;
        case Grade::kDigits: ++dg; break;
        case Grade::kOneOff: ++oo; break;
        case Grade::kWrong: ++wr; break;
        case Grade::kEmpty: ++em; break;
      }
    }
    if (n == 0) {
      continue;
    }
    std::printf("%d\t%d\t%d\t%d\t%d\t%d\t%.0f\t%.0f\n", edge, ex, dg, oo, wr, em, sum / n, mx);
  }

  return 0;
}
