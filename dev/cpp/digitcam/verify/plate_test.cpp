// plate_test.cpp — samples/plates 전체를 expected.json 정답과 대조하는 채점 하네스 (계약 §9.6)
//
// "잘 된다" 대신 **점수**를 내는 게 목적이다. 앱과 똑같은 digit_pipeline 을 그대로 링크하므로
// 여기서 맞으면 앱에서도 같은 값이 나와야 한다(경로가 갈라지면 의미가 없다).
//
// 입력은 make_testdata.py 가 만든 PGM + manifest.tsv 다. imgcodecs 를 안 쓰는 이유는
// 앱이 core+imgproc 만 링크하기 때문 — 하네스도 같은 조건이어야 한다.
//
// 사용법:  plate_test <testdata 디렉터리> [--debug] [--only <파일명 일부>] [--models <디렉터리>]
//
//   --models 를 주면 표준 경로(검출망 + CTC 인식망)의 가중치를 실어 **두 경로가 같이 도는**
//   조건으로 잰다. 안 주면 예전과 같은 템플릿 전용 조건이다 — 두 값을 나란히 봐야
//   어느 경로가 무엇을 벌었는지 갈린다.

#include "digit_pipeline.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Case {
  std::string file;
  std::string expect_value;
  bool band = false;
  std::string format;
  // 채점 규칙(계약 개정 4, expected.json 의 rule):
  //   exact          — 값이 정확히 맞아야 통과
  //   exact_or_empty — 맞히거나 비우거나. 틀린 값을 내면 실패
  //   empty          — 반드시 비워야 통과(모션블러·번호판 없음)
  std::string rule;
  std::string size;
};

std::vector<unsigned char> read_bytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return {};
  }
  return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
}

// P5(이진 PGM) 파서. 주석(#)과 공백을 건너뛴다.
bool read_pgm(const std::string& path, cv::Mat* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return false;
  }

  std::string magic;
  f >> magic;
  if (magic != "P5") {
    return false;
  }

  auto next_int = [&f]() -> int {
    int v = -1;
    while (f) {
      const int c = f.peek();
      if (c == '#') {
        std::string skip;
        std::getline(f, skip);
      } else if (std::isspace(c)) {
        f.get();
      } else {
        f >> v;
        break;
      }
    }
    return v;
  };

  const int w = next_int();
  const int h = next_int();
  const int maxval = next_int();
  if (w <= 0 || h <= 0 || maxval != 255) {
    return false;
  }
  f.get();  // 헤더 뒤 한 바이트(개행)

  cv::Mat img(h, w, CV_8UC1);
  f.read(reinterpret_cast<char*>(img.data), static_cast<std::streamsize>(w) * h);
  if (!f) {
    return false;
  }
  *out = img;
  return true;
}

std::vector<Case> read_manifest(const std::string& path) {
  std::vector<Case> cases;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream ss(line);
    Case c;
    std::string band;
    std::getline(ss, c.file, '\t');
    std::getline(ss, c.expect_value, '\t');
    std::getline(ss, band, '\t');
    std::getline(ss, c.format, '\t');
    std::getline(ss, c.rule, '\t');
    std::getline(ss, c.size, '\t');
    c.band = band == "1";
    cases.push_back(c);
  }
  return cases;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("사용법: plate_test <testdata 디렉터리> [--debug] [--only <이름 일부>]"
                " [--models <디렉터리>]\n");
    return 2;
  }

  const std::string root = argv[1];
  bool debug = false;
  std::string only;
  std::string model_dir;
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--debug") == 0) {
      debug = true;
    } else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
      only = argv[++i];
    } else if (std::strcmp(argv[i], "--models") == 0 && i + 1 < argc) {
      model_dir = argv[++i];
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
      std::printf("🔴 모델을 못 읽었다(det=%zuB rec=%zuB dict=%zuB) — 템플릿 전용으로 돈다\n",
                  det_bytes.size(), rec_bytes.size(), dict_text.size());
      det_bytes.clear();
    }
  }

  const std::vector<Case> cases = read_manifest(root + "/manifest.tsv");
  if (cases.empty()) {
    std::printf("manifest.tsv 를 읽지 못했다: %s\n", (root + "/manifest.tsv").c_str());
    return 2;
  }

  std::printf("OpenCV %s · 샘플 %zu장 · 표준경로 %s\n\n",
              digitcam::PlatePipeline::version().c_str(), cases.size(),
              det_bytes.empty() ? "꺼짐(템플릿 전용)" : "켜짐(표준 우선 · 템플릿 폴백)");

  int pass = 0;
  int total = 0;
  int stride_mismatch = 0;

  for (const Case& c : cases) {
    if (!only.empty() && c.file.find(only) == std::string::npos) {
      continue;
    }
    ++total;

    cv::Mat img;
    if (!read_pgm(root + "/pgm/" + c.file, &img)) {
      std::printf("✗ %-46s PGM 읽기 실패\n", c.file.c_str());
      continue;
    }

    // 샘플 이미지 모드와 같은 조건: 같은 프레임이 반복해서 들어온다.
    // 첫 프레임은 비교 대상이 없어 moving 이므로 still_frames+1 번 먹인다(계약 §9.1).
    digitcam::PlatePipeline p;
    p.configure("vision.still_frames=2\nvision.motion_threshold=0.6\n");
    if (!det_bytes.empty()) {
      p.load_models(det_bytes, rec_bytes, dict_text);
    }
    p.set_debug(debug);

    digitcam::Result r;
    for (int i = 0; i < 4; ++i) {
      r = p.process(img, 0);
    }

    // JNI 는 YUV_420_888 의 Y 평면을 넘기는데 그 rowStride 는 width 보다 큰 게 정상이다.
    // 하네스는 연속 버퍼만 쓰므로 그 경로가 시험되지 않는다 — 여기서 한 번 더 돌려 확인한다.
    // (앱에서만 나타나는 어긋남이 생기면 가장 늦게 발견되는 종류의 버그다)
    {
      const int stride = img.cols + 16;
      std::vector<unsigned char> padded(static_cast<size_t>(stride) * img.rows, 0);
      for (int y = 0; y < img.rows; ++y) {
        std::memcpy(padded.data() + static_cast<size_t>(y) * stride, img.ptr<unsigned char>(y),
                    static_cast<size_t>(img.cols));
      }
      const cv::Mat strided(img.rows, img.cols, CV_8UC1, padded.data(),
                            static_cast<size_t>(stride));

      digitcam::PlatePipeline ps;
      ps.configure("vision.still_frames=2\nvision.motion_threshold=0.6\n");
      if (!det_bytes.empty()) {
        ps.load_models(det_bytes, rec_bytes, dict_text);
      }
      digitcam::Result rs;
      for (int i = 0; i < 4; ++i) {
        rs = ps.process(strided, 0);
      }
      if (rs.value != r.value) {
        std::printf("  ⚠ rowStride>width 결과가 다르다: 연속=%s 패딩=%s\n", r.value.c_str(),
                    rs.value.c_str());
        ++stride_mismatch;
      }
    }

    const std::string reason = digitcam::reason_name(r.reason);
    bool ok = false;
    if (c.rule == "empty") {
      // 읽히면 안 되는 샘플(모션블러·번호판 없음). 무엇으로 걸렀는지는 묻지 않는다.
      ok = r.value.empty();
    } else if (c.rule == "exact_or_empty") {
      // 맞히거나 비우거나. **틀린 값을 내면 실패** — 틀린 값을 보내느니 안 보내는 게 낫다.
      ok = r.value.empty() || r.value == c.expect_value;
    } else {
      ok = r.value == c.expect_value;
    }
    if (ok) {
      ++pass;
    }

    std::printf("%s %-44s %-12s reason=%-13s conf=%.2f sharp=%7.1f [%s] 기대=%s\n", ok ? "✓" : "✗",
                c.file.c_str(), r.value.empty() ? "(없음)" : r.value.c_str(), reason.c_str(),
                r.conf, r.sharp, c.rule.c_str(),
                c.expect_value.empty() ? "(비어야 함)" : c.expect_value.c_str());

    if (debug) {
      std::printf("%s", p.debug_log().c_str());
      std::printf("  JSON: %s\n\n", digitcam::PlatePipeline::to_json(r).c_str());
    }
  }

  std::printf("\n점수: %d / %d\n", pass, total);
  std::printf("rowStride>width 경로: %s (어긋난 샘플 %d장)\n",
              stride_mismatch == 0 ? "연속 버퍼와 결과 동일" : "⚠ 결과가 다르다", stride_mismatch);
  return (pass == total && stride_mismatch == 0) ? 0 : 1;
}
