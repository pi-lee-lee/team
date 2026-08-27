// ocr_test.cpp — 표준 경로(PlateOcr: 검출망 + CTC 인식망)를 데스크톱에서 채점하는 하니스.
//
// 앱과 **같은 plate_ocr.cpp 를 그대로 링크**한다. 여기서 나온 값이 앱의 값이다.
// PC 에서 파이썬으로 먼저 잰 값(REQ-0363 1단계)과 이 하니스의 값이 같아야 이식이 맞은 것이다.
//
// 사용법:
//   ocr_test <모델 디렉터리> <이미지 디렉터리> [--expect 값] [--limit 960] [--manifest 경로]
//            [--conf 0.0] [--debug]
//
//   모델 디렉터리에 다음 셋이 있어야 한다(이름 고정):
//     ppocr_det_v4.onnx · korean_rec_static.onnx · korean_dict.txt
//
// ⚠ 입력은 앱이 넘기는 것과 같은 **8비트 회색조**다. JPEG/PNG 는 EXIF 를 무시하고 읽는다
//   (앱의 BitmapFactory 와 같은 조건 — 도메인 원장 §3.4).

#include "plate_ocr.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<unsigned char> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return {};
  }
  return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
}

std::vector<std::string> list_images(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (d == nullptr) {
    return out;
  }
  while (const dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.size() < 5 || name[0] == '.') {
      continue;
    }
    std::string lower = name;
    for (char& c : lower) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const bool ok = lower.rfind(".jpg") == lower.size() - 4 ||
                    lower.rfind(".png") == lower.size() - 4 ||
                    lower.rfind(".pgm") == lower.size() - 4 ||
                    lower.rfind(".jpeg") == lower.size() - 5;
    if (ok) {
      out.push_back(name);
    }
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

// manifest.tsv: 파일<TAB>기대값<TAB>띠<TAB>형식<TAB>채점규칙<TAB>원본크기
std::map<std::string, std::pair<std::string, std::string>> read_manifest(const std::string& path) {
  std::map<std::string, std::pair<std::string, std::string>> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::vector<std::string> col;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, '\t')) {
      col.push_back(cell);
    }
    if (col.size() >= 5) {
      out[col[0]] = {col[1], col[4]};
    }
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("사용법: ocr_test <모델디렉터리> <이미지디렉터리> [--expect 값] [--limit 960]\n"
                "        [--manifest 경로] [--conf 0.0] [--debug]\n");
    return 2;
  }

  const std::string model_dir = argv[1];
  const std::string img_dir = argv[2];
  std::string expect = "123바9898";
  std::string manifest_path;
  int limit = 960;
  double conf_min = 0.0;
  bool debug = false;

  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
      expect = argv[++i];
    } else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
      limit = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
      manifest_path = argv[++i];
    } else if (std::strcmp(argv[i], "--conf") == 0 && i + 1 < argc) {
      conf_min = std::atof(argv[++i]);
    } else if (std::strcmp(argv[i], "--debug") == 0) {
      debug = true;
    }
  }

  const std::vector<unsigned char> det = read_file(model_dir + "/ppocr_det_v4.onnx");
  const std::vector<unsigned char> rec = read_file(model_dir + "/korean_rec_static.onnx");
  const std::vector<unsigned char> dict_bytes = read_file(model_dir + "/korean_dict.txt");
  if (det.empty() || rec.empty() || dict_bytes.empty()) {
    std::printf("🔴 모델 파일을 못 읽었다: det=%zuB rec=%zuB dict=%zuB (%s)\n", det.size(),
                rec.size(), dict_bytes.size(), model_dir.c_str());
    return 2;
  }

  digitcam::PlateOcr ocr;
  if (!ocr.load(det, rec, std::string(dict_bytes.begin(), dict_bytes.end()))) {
    std::printf("🔴 모델 적재 실패\n");
    return 2;
  }
  std::printf("모델 적재 OK · det %.2fMB · rec %.2fMB · dict %.2fMB · 긴변상한 %d · conf문턱 %.2f\n",
              det.size() / 1048576.0, rec.size() / 1048576.0, dict_bytes.size() / 1048576.0, limit,
              conf_min);

  const std::map<std::string, std::pair<std::string, std::string>> man =
      manifest_path.empty() ? std::map<std::string, std::pair<std::string, std::string>>()
                            : read_manifest(manifest_path);

  const std::vector<std::string> files = list_images(img_dir);
  std::printf("자료 %zu장 · %s\n\n", files.size(), img_dir.c_str());
  std::printf("파일\t결과\t등급\tconf\t표\tms\n");

  int exact = 0;
  int wrong = 0;
  int empty = 0;
  double tot_ms = 0.0;

  for (const std::string& file : files) {
    const cv::Mat src =
        cv::imread(img_dir + "/" + file, cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
    if (src.empty()) {
      std::printf("%s\t(읽기실패)\n", file.c_str());
      continue;
    }
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);

    std::string want = expect;
    std::string rule = "exact";
    const auto it = man.find(file);
    if (it != man.end()) {
      want = it->second.first;
      rule = it->second.second;
    }

    std::string dbg;
    digitcam::PlateOcr::Read r;
    const int64 t0 = cv::getTickCount();
    const bool got = ocr.read(gray, limit, &r, debug ? &dbg : nullptr);
    const double ms = (cv::getTickCount() - t0) / cv::getTickFrequency() * 1000.0;
    tot_ms += ms;

    std::string value = got ? r.value : std::string();
    if (!value.empty() && r.conf < conf_min) {
      value.clear();  // 문턱 미달은 **값없음** 으로 낸다(오답보다 낫다는 루트 판정)
    }

    const bool want_empty = rule == "empty";
    bool good;
    if (want_empty) {
      good = value.empty();
    } else if (rule == "exact_or_empty") {
      good = value == want || value.empty();
    } else {
      good = value == want;
    }
    if (good && !want_empty && value.empty()) {
      ++empty;  // 규칙상 통과지만 값은 못 냈다 — 정답 수에 세지 않는다
    } else if (value == want && !want_empty) {
      ++exact;
    } else if (value.empty()) {
      ++empty;
    } else {
      ++wrong;
    }

    std::printf("%s\t%s\t%s\t%.2f\t%d\t%.0f\n", file.c_str(),
                value.empty() ? "(없음)" : value.c_str(),
                value == want && !want_empty ? "전체일치"
                                             : (value.empty() ? "값없음" : "틀림"),
                r.conf, r.votes, ms);
    if (debug && !dbg.empty()) {
      std::printf("%s", dbg.c_str());
    }
  }

  std::printf("\n집계: 전체일치 %d · 틀림 %d · 값없음 %d / %zu · 평균 %.0f ms\n", exact, wrong,
              empty, files.size(), tot_ms / std::max<size_t>(1, files.size()));
  return 0;
}
