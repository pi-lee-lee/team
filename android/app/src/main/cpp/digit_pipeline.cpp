// digit_pipeline.cpp — DigitCam 번호판 인식 코어 (순수 OpenCV, 계약 개정 2)
//
// ⚠ 이 파일에는 jni.h / android/*.h 가 들어오지 않는다. 임베디드로 그대로 이식할 코어다.
//
// 단계 구성(계약 §9):
//   9.1 움직임 게이트   → 움직이면 뒤 단계를 아예 돌리지 않는다
//   9.2 번호판 검출     → minAreaRect/4점 근사 + 투시보정으로 정규화
//   9.3 파란 KOR 띠 제거 → 신형은 왼쪽 12% 를 버린다
//   9.4 문자 분할       → 세로 투영 프로파일(한글 획 분리 함정 회피)
//   9.5 분류            → 자리별 후보 제한 + 템플릿 NCC

#include "digit_pipeline.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "plate_templates.hpp"

namespace digitcam {
namespace {

// 정규화 크기. 실물 비율 520x110mm / 335x155mm 을 그대로 쓴다(계약 §9.2).
constexpr int kNewPlateW = 520;
constexpr int kNewPlateH = 110;
constexpr int kOldPlateW = 335;
constexpr int kOldPlateH = 155;

// 신형 = 숫자3 + 한글1 + 숫자4, 구형 = 숫자2 + 한글1 + 숫자4 (계약 §9.4)
constexpr int kNewChars = 8;
constexpr int kNewHangulIndex = 3;
constexpr int kOldChars = 7;
constexpr int kOldHangulIndex = 2;

// 파란 KOR 띠 판정(계약 §9.3): 왼쪽 12% 스트립 / 나머지의 밝은 화소 비율 비.
constexpr double kBandStripRatio = 0.12;
constexpr double kBandDecisionRatio = 0.8;

double sanitize(double v, double lo, double hi) {
  if (!std::isfinite(v)) {
    return lo;
  }
  return std::min(hi, std::max(lo, v));
}

std::string trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
    ++b;
  }
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
    --e;
  }
  return s.substr(b, e - b);
}

// 값이 온전한 수일 때만 true. atoi/atof 는 "abc" 를 조용히 0 으로 만들어
// "값이 깨졌으면 이전 값 유지"(계약 §4)를 지킬 수 없다.
bool parse_double(const std::string& s, double* out) {
  if (s.empty()) {
    return false;
  }
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) {
    return false;
  }
  *out = v;
  return true;
}

bool parse_int(const std::string& s, int* out) {
  if (s.empty()) {
    return false;
  }
  char* end = nullptr;
  const long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0' || v < std::numeric_limits<int>::min() ||
      v > std::numeric_limits<int>::max()) {
    return false;
  }
  *out = static_cast<int>(v);
  return true;
}

bool parse_bool(const std::string& s, bool* out) {
  std::string v;
  v.reserve(s.size());
  for (const char c : s) {
    v += static_cast<char>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
  }
  if (v == "true" || v == "1" || v == "yes" || v == "on") {
    *out = true;
    return true;
  }
  if (v == "false" || v == "0" || v == "no" || v == "off") {
    *out = false;
    return true;
  }
  return false;
}

// UTF-8 은 그대로 흘려보낸다(계약 §4: value 에 한글이 들어간다).
// 0x80~0xBF 는 unsigned 로 보면 0x20 이상이라 제어문자 이스케이프에 걸리지 않는다 —
// signed char 로 비교하면 음수가 되어 잘못 걸리므로 캐스팅을 빠뜨리지 마라.
void append_escaped(std::string& out, const std::string& s) {
  for (const char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (u < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", u);
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }
}

void append_double(std::string& out, double v, const char* fmt) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), fmt, v);
  out += buf;
}

void append_int(std::string& out, int v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d", v);
  out += buf;
}

// ---------------------------------------------------------------------------
// 템플릿 뱅크 (계약 §9.5)
// plate_templates.hpp 는 24x32 셀을 1비트로 눌러 담은 데이터 전용 헤더다.
// 처음 쓸 때 한 번 풀어서 블러 → 평균 제거 벡터로 만들어 둔다. 이후엔 내적만 한다.
// ---------------------------------------------------------------------------
struct TemplateVec {
  int label_id;                 // 같은 글자의 다른 폰트는 같은 id 를 갖는다
  bool hangul;
  int aspect100;                // 원래 글리프의 폭/높이 x100
  std::vector<float> centered;  // 평균을 뺀 값
  float norm;                   // sqrt(sum(centered^2))
};

struct Bank {
  std::vector<TemplateVec> vecs;
  std::vector<std::string> labels;  // label_id → UTF-8 글자
  std::vector<char> is_hangul;      // label_id → 한글인가
};

// 24x32 이진 셀 → **부호거리장(signed distance field)** → 평균 제거 벡터.
//
// 왜 원본 비트맵이 아니라 거리장인가:
//   번호판 서체는 이 머신에 없다. 고딕 폰트로 만든 템플릿과는 획 두께와 곡률이 다르고,
//   이진 비트맵끼리의 상관은 그 차이에 민감해서 맞는 글자도 점수가 뚝 떨어진다
//   (실측: 번호판 '6' ↔ 폰트 '6' 이 0.56, 오답 '8' 이 0.55 로 사실상 구분이 안 됐다).
//   거리장은 "획이 어디서 얼마나 떨어져 있는가"를 부드럽게 담아 두께 차이를 흡수하면서
//   전체 형태는 그대로 남긴다. 값은 ±kClamp 로 잘라 멀리 있는 배경이 점수를 지배하지 않게 한다.
void cell_to_vector(const cv::Mat& cell8u, std::vector<float>* out, float* norm) {
  constexpr float kClamp = 6.0f;

  cv::Mat ink;
  cv::threshold(cell8u, ink, 127, 255, cv::THRESH_BINARY);

  cv::Mat bg;
  cv::bitwise_not(ink, bg);

  // dist_out: 배경 화소에서 가장 가까운 획까지의 거리
  // dist_in : 획 화소에서 가장 가까운 배경까지의 거리
  cv::Mat dist_out, dist_in;
  cv::distanceTransform(bg, dist_out, cv::DIST_L2, 3);
  cv::distanceTransform(ink, dist_in, cv::DIST_L2, 3);

  const int n = cell8u.rows * cell8u.cols;
  out->resize(static_cast<size_t>(n));

  double sum = 0.0;
  for (int y = 0; y < cell8u.rows; ++y) {
    const float* row_out = dist_out.ptr<float>(y);
    const float* row_in = dist_in.ptr<float>(y);
    for (int x = 0; x < cell8u.cols; ++x) {
      float v = row_in[x] - row_out[x];  // 획 안쪽이 +, 바깥이 -
      v = std::min(kClamp, std::max(-kClamp, v));
      (*out)[static_cast<size_t>(y) * cell8u.cols + x] = v;
      sum += v;
    }
  }

  const float mean = static_cast<float>(sum / n);
  double sq = 0.0;
  for (float& v : *out) {
    v -= mean;
    sq += static_cast<double>(v) * v;
  }
  *norm = static_cast<float>(std::sqrt(sq));
}

const Bank& template_bank() {
  static const Bank bank = [] {
    Bank out;
    out.vecs.reserve(static_cast<size_t>(templates::kEntryCount));

    for (int i = 0; i < templates::kEntryCount; ++i) {
      const templates::Entry& e = templates::entries()[i];

      // 1비트 → 0/255 셀로 푼다(MSB first, 행 우선).
      cv::Mat cell(templates::kCellH, templates::kCellW, CV_8UC1, cv::Scalar(0));
      for (int p = 0; p < templates::kCellW * templates::kCellH; ++p) {
        const unsigned char byte = e.bits[p / 8];
        if ((byte >> (7 - (p % 8))) & 1) {
          cell.at<unsigned char>(p / templates::kCellW, p % templates::kCellW) = 255;
        }
      }

      // 같은 글자는 폰트가 달라도 하나의 클래스다.
      int label_id = -1;
      for (size_t k = 0; k < out.labels.size(); ++k) {
        if (out.labels[k] == e.label) {
          label_id = static_cast<int>(k);
          break;
        }
      }
      if (label_id < 0) {
        label_id = static_cast<int>(out.labels.size());
        out.labels.push_back(e.label);
        out.is_hangul.push_back(e.hangul ? 1 : 0);
      }

      TemplateVec tv;
      tv.label_id = label_id;
      tv.hangul = e.hangul;
      tv.aspect100 = e.aspect100;
      cell_to_vector(cell, &tv.centered, &tv.norm);
      if (tv.norm > 1e-6f) {
        out.vecs.push_back(std::move(tv));
      }
    }
    return out;
  }();
  return bank;
}

// 이진 마스크(문자=255) → 24x32 정규화 셀 + 원래 종횡비(x100).
// 규칙은 생성기(gen_templates.py)와 **똑같아야** 한다: 잉크 bbox 크롭 → 셀에 꽉 채워 늘림.
//
// 종횡비를 유지해 맞추면 같은 글자라도 질의는 높이 기준, 템플릿은 폭 기준으로 맞춰지는 일이
// 생겨(번호판 '가' 가 폰트 '가' 보다 세로로 길다) 크기가 어긋난 채 비교된다 — 실측에서 NCC 가
// 0.44 까지 떨어졌다. 채움 정규화로 그 어긋남을 없애고, 잃어버린 '좁고 넓음' 정보는
// aspect100 으로 따로 넘겨 분류기가 벌점으로 쓴다.
cv::Mat normalize_cell(const cv::Mat& mask, int* aspect100 = nullptr) {
  cv::Mat cell(templates::kCellH, templates::kCellW, CV_8UC1, cv::Scalar(0));
  if (aspect100 != nullptr) {
    *aspect100 = 100;
  }
  if (mask.empty()) {
    return cell;
  }

  const cv::Rect ink = cv::boundingRect(mask);
  if (ink.width < 1 || ink.height < 1) {
    return cell;
  }

  if (aspect100 != nullptr) {
    *aspect100 = std::max(1, static_cast<int>(std::lround(
                                 static_cast<double>(ink.width) / ink.height * 100.0)));
  }

  cv::resize(mask(ink), cell, cv::Size(templates::kCellW, templates::kCellH), 0, 0,
             cv::INTER_AREA);
  return cell;
}

struct Classification {
  std::string label;
  std::string runner_up;  // 어떤 글자와 헷갈렸는지(디버그·튜닝용)
  double conf = 0.0;
  double best = 0.0;      // 1등 NCC
  double second = 0.0;    // 다른 클래스 2등 NCC
};

// 정규화 셀을 눈으로 보기 위한 아스키 덤프. 템플릿과 질의의 모양이 어긋나면
// 숫자만 봐서는 원인을 못 찾는다 — 실제로 '6' 이 왜 안 맞는지 여기서 드러난다.
std::string cell_ascii(const cv::Mat& cell) {
  std::string s;
  for (int y = 0; y < cell.rows; ++y) {
    s += "    ";
    for (int x = 0; x < cell.cols; ++x) {
      s += cell.at<unsigned char>(y, x) >= 128 ? '#' : '.';
    }
    s += '\n';
  }
  return s;
}

// 자리 위치가 후보를 좁혀 준다(계약 §9.5) — 숫자 자리에서는 숫자끼리만 비교한다.
Classification classify_cell(const cv::Mat& mask, bool want_hangul) {
  Classification c;

  int q_aspect = 100;
  const cv::Mat cell = normalize_cell(mask, &q_aspect);
  std::vector<float> q;
  float qnorm = 0.0f;
  cell_to_vector(cell, &q, &qnorm);
  if (qnorm < 1e-6f) {
    return c;
  }

  const Bank& bank = template_bank();

  // 클래스마다 '가장 잘 맞은 폰트'의 점수만 남긴다. 같은 글자의 다른 폰트는
  // 경쟁자가 아니다 — 마진은 **다른 글자**와의 거리여야 의미가 있다.
  std::vector<double> per_label(bank.labels.size(), -2.0);

  for (const TemplateVec& t : bank.vecs) {
    if (t.hangul != want_hangul || t.centered.size() != q.size()) {
      continue;
    }
    double dot = 0.0;
    for (size_t i = 0; i < q.size(); ++i) {
      dot += static_cast<double>(q[i]) * t.centered[i];
    }
    double ncc = dot / (static_cast<double>(qnorm) * t.norm);

    // 셀을 꽉 채워 늘렸으므로 '좁다/넓다' 정보가 형태 점수에는 없다. 여기서 벌점으로 되돌린다 —
    // 이게 없으면 늘려 놓은 '1' 이 다른 숫자와 구분되지 않는다.
    const double ratio = static_cast<double>(q_aspect) / std::max(1, t.aspect100);
    ncc -= std::min(0.20, 0.20 * std::fabs(std::log(ratio)));

    if (ncc > per_label[static_cast<size_t>(t.label_id)]) {
      per_label[static_cast<size_t>(t.label_id)] = ncc;
    }
  }

  int best_id = -1;
  int second_id = -1;
  double best = -2.0;
  double second = -2.0;
  for (size_t i = 0; i < per_label.size(); ++i) {
    if (per_label[i] > best) {
      second = best;
      second_id = best_id;
      best = per_label[i];
      best_id = static_cast<int>(i);
    } else if (per_label[i] > second) {
      second = per_label[i];
      second_id = static_cast<int>(i);
    }
  }

  if (best_id < 0) {
    return c;
  }

  c.label = bank.labels[static_cast<size_t>(best_id)];
  c.runner_up = second_id >= 0 ? bank.labels[static_cast<size_t>(second_id)] : std::string();
  c.best = best;
  c.second = second;

  // 신뢰도 = 절대 점수와 '다른 글자와의 마진' 을 함께 본다.
  //
  // 눈금은 실측으로 잡았다. 번호판 서체가 이 머신에 없으므로 맞는 글자라도 절대 점수는
  // 0.65~0.95 에 흩어진다(폰트가 다르니 당연하다). 반대로 **틀릴 위험은 마진이 얇을 때** 온다
  // — 실제로 '6' 과 '8' 이 0.006 차이로 붙은 프레임이 있었다. 그래서 마진에 더 큰 무게를 준다.
  //   · 절대점수 0.40→0, 0.75→1
  //   · 마진     0.00→0, 0.12→1
  // 이 눈금에서 위 '6/8' 붙은 경우는 0.23, 제대로 갈린 경우는 0.7~0.99 가 나온다.
  const double score_part = std::min(1.0, std::max(0.0, (best - 0.40) / 0.35));
  const double margin_part = std::min(1.0, std::max(0.0, (best - second) / 0.12));
  c.conf = std::min(0.99, 0.45 * score_part + 0.55 * margin_part);
  return c;
}

// ---------------------------------------------------------------------------
// 9.2 번호판 검출
// ---------------------------------------------------------------------------

// minAreaRect 의 네 점을 TL,TR,BR,BL 순으로 정렬한다.
void order_quad(const cv::Point2f in[4], std::vector<cv::Point2f>* out) {
  int tl = 0, br = 0, tr = 0, bl = 0;
  double min_sum = 1e18, max_sum = -1e18, min_diff = 1e18, max_diff = -1e18;
  for (int i = 0; i < 4; ++i) {
    const double s = in[i].x + in[i].y;
    const double d = in[i].y - in[i].x;
    if (s < min_sum) { min_sum = s; tl = i; }
    if (s > max_sum) { max_sum = s; br = i; }
    if (d < min_diff) { min_diff = d; tr = i; }
    if (d > max_diff) { max_diff = d; bl = i; }
  }
  out->clear();
  out->push_back(in[tl]);
  out->push_back(in[tr]);
  out->push_back(in[br]);
  out->push_back(in[bl]);
}

// 번호판 '후보' 를 점수 순으로 여러 개 뽑는다(하나만 고르지 않는 이유는 아래).
//
// 밝은 판면을 하나의 연결성분으로 보고 minAreaRect 로 감싼다 — 기울어진 판을 받기 위해서다.
// 축 정렬 boundingRect 로 자르면 비스듬한 번호판에서 글자가 기울어 분류가 무너진다(계약 §9.2).
//
// **가장 큰 후보를 고르면 안 된다.** 실측: 640x480 장면에서 배경의 큰 밝은 영역(639x280,
// 종횡비 2.28 = 구형 번호판 비율과 비슷)이 진짜 번호판(253x46)보다 커서 그쪽이 뽑혔고,
// 뒤 단계가 통째로 헛돌았다. 그래서 여기서는 후보만 늘어놓고, 실제 채택은
// **문자 분할까지 해 보고 구조가 맞는 것**으로 한다(process 안의 루프).
constexpr size_t kMaxCandidates = 4;

double plate_likeness(double aspect, double long_side, double rect_area, double frame_w,
                      double frame_area) {
  // 장축(4.7)·단축(2.2) 중 가까운 쪽과의 로그 거리
  const double d_new = std::fabs(std::log(aspect / 4.7));
  const double d_old = std::fabs(std::log(aspect / 2.2));
  const double aspect_score = std::exp(-3.0 * std::min(d_new, d_old));

  // 번호판은 프레임의 일부다. 프레임 면적의 30% 를 넘는 덩어리는 배경일 가능성이 크다.
  const double area_penalty = rect_area > frame_area * 0.30 ? 0.3 : 1.0;

  // 너무 작은 것보다 큰 쪽을 선호하되 절반 폭에서 포화시킨다.
  const double size_score = std::min(1.0, long_side / (0.5 * frame_w));

  return aspect_score * area_penalty * (0.5 + 0.5 * size_score);
}

bool detect_plate(const cv::Mat& gray, const Config& cfg,
                  std::vector<std::vector<cv::Point2f>>* quads, std::string* dbg) {
  cv::Mat blurred;
  cv::GaussianBlur(gray, blurred, cv::Size(3, 3), 0);

  cv::Mat bright;
  cv::threshold(blurred, bright, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  cv::Mat labels, stats, centroids;
  const int n = cv::connectedComponentsWithStats(bright, labels, stats, centroids, 8, CV_32S);

  const double frame_w = gray.cols;
  const double frame_area = static_cast<double>(gray.cols) * gray.rows;

  struct Scored {
    double score;
    std::vector<cv::Point2f> quad;
    cv::Point2f center;
  };
  std::vector<Scored> scored;

  // 후보 원천 두 가지를 모아서 함께 채점한다.
  //  A) 밝은 연결성분 — 흰 판면 자체. 깨끗한 장면에서 가장 정확하다.
  //  B) 모폴로지 그래디언트 + 가로 close — '글자가 몰려 있는 덩어리'.
  //     A 는 판면이 밝은 배경과 붙어 버리면 통째로 놓친다(실측: scene-02 에서 번호판이
  //     배경과 한 성분이 되어 후보가 하나도 안 나왔다). B 는 배경 밝기와 무관하게 잡는다.
  std::vector<std::pair<cv::RotatedRect, double>> raw;  // (사각형, 실제 채워진 면적)

  for (int i = 1; i < n; ++i) {
    const int area = stats.at<int>(i, cv::CC_STAT_AREA);
    if (area < 200 || area < frame_area * 0.005) {
      continue;
    }

    const cv::Rect bb(stats.at<int>(i, cv::CC_STAT_LEFT), stats.at<int>(i, cv::CC_STAT_TOP),
                      stats.at<int>(i, cv::CC_STAT_WIDTH), stats.at<int>(i, cv::CC_STAT_HEIGHT));
    cv::Mat comp = (labels(bb) == i);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(comp, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
      continue;
    }
    size_t largest = 0;
    double largest_area = 0.0;
    for (size_t k = 0; k < contours.size(); ++k) {
      const double a = cv::contourArea(contours[k]);
      if (a > largest_area) {
        largest_area = a;
        largest = k;
      }
    }

    cv::RotatedRect rr = cv::minAreaRect(contours[largest]);
    rr.center.x += static_cast<float>(bb.x);
    rr.center.y += static_cast<float>(bb.y);
    raw.emplace_back(rr, static_cast<double>(area));
  }

  {
    cv::Mat grad;
    cv::morphologyEx(blurred, grad, cv::MORPH_GRADIENT,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
    cv::Mat gbin;
    cv::threshold(grad, gbin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::morphologyEx(gbin, gbin, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(17, 3)));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(gbin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const std::vector<cv::Point>& c : contours) {
      const double a = cv::contourArea(c);
      if (a < 200 || a < frame_area * 0.005) {
        continue;
      }
      cv::RotatedRect rr = cv::minAreaRect(c);
      // 글자 덩어리는 판면보다 조금 작다. 위아래 획이 잘리지 않게 살짝 넓힌다.
      rr.size.width *= 1.08f;
      rr.size.height *= 1.14f;
      raw.emplace_back(rr, a);
    }
  }

  for (const std::pair<cv::RotatedRect, double>& item : raw) {
    const cv::RotatedRect& rr = item.first;
    const double area = item.second;
    const double long_side = std::max(rr.size.width, rr.size.height);
    const double short_side = std::min(rr.size.width, rr.size.height);
    if (short_side < 8.0 || long_side < 24.0) {
      continue;
    }

    const double aspect = long_side / short_side;
    const double rect_area = long_side * short_side;
    const double rectangularity = area / rect_area;

    // 장축(≈4.7) 과 단축(≈2.2) 을 모두 받되, 그 밖의 모양은 번호판이 아니다.
    const bool aspect_ok = aspect >= 1.8 && aspect <= 7.0;
    const bool size_ok = long_side >= cfg.min_plate_width_ratio * frame_w;
    const bool solid_ok = rectangularity >= 0.55;

    const double score =
        plate_likeness(aspect, long_side, rect_area, frame_w, frame_area);

    if (dbg != nullptr) {
      char buf[256];
      std::snprintf(buf, sizeof(buf),
                    "  후보 area=%.0f long=%.0f short=%.0f aspect=%.2f 채움=%.2f 점수=%.2f %s%s%s\n",
                    area, long_side, short_side, aspect, rectangularity, score,
                    aspect_ok ? "" : "[종횡비X]", size_ok ? "" : "[너무작음]",
                    solid_ok ? "" : "[속빔]");
      *dbg += buf;
    }

    if (!aspect_ok || !size_ok || !solid_ok) {
      continue;
    }

    cv::Point2f pts[4];
    rr.points(pts);

    Scored s;
    s.score = score;
    s.center = rr.center;
    order_quad(pts, &s.quad);
    scored.push_back(std::move(s));
  }

  std::sort(scored.begin(), scored.end(),
            [](const Scored& a, const Scored& b) { return a.score > b.score; });

  quads->clear();
  for (const Scored& s : scored) {
    // 두 원천이 같은 번호판을 각각 잡는 일이 흔하다. 중심이 겹치면 점수가 높은 쪽만 남긴다.
    bool duplicate = false;
    for (const Scored& kept : scored) {
      if (&kept == &s) {
        break;
      }
      if (cv::norm(kept.center - s.center) < frame_w * 0.05) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    quads->push_back(s.quad);
    if (quads->size() >= kMaxCandidates) {
      break;
    }
  }

  if (quads->empty()) {
    // 계약 §9.2 폴백 — 후보가 없는데 프레임 자체가 번호판 비율이면 프레임 전체를 판으로 본다.
    // 크롭된 번호판 이미지(샘플 모드)와, 실사용에서 번호판에 바짝 붙인 경우가 여기에 해당한다.
    const double frame_aspect = static_cast<double>(gray.cols) / std::max(1, gray.rows);
    const bool near_new = frame_aspect >= 4.7 * 0.8 && frame_aspect <= 4.7 * 1.2;
    const bool near_old = frame_aspect >= 2.2 * 0.8 && frame_aspect <= 2.2 * 1.2;
    if (!near_new && !near_old) {
      return false;
    }
    if (dbg != nullptr) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "  후보 없음 → 프레임 전체를 판으로(폴백) aspect=%.2f\n",
                    frame_aspect);
      *dbg += buf;
    }
    std::vector<cv::Point2f> full;
    full.push_back(cv::Point2f(0.0f, 0.0f));
    full.push_back(cv::Point2f(static_cast<float>(gray.cols - 1), 0.0f));
    full.push_back(
        cv::Point2f(static_cast<float>(gray.cols - 1), static_cast<float>(gray.rows - 1)));
    full.push_back(cv::Point2f(0.0f, static_cast<float>(gray.rows - 1)));
    quads->push_back(std::move(full));
  }

  return !quads->empty();
}

// 계약 §9.3 — 띠가 있는지 검사한 뒤 **있는 만큼만** 자른다. 잘라낼 열 수를 돌려준다(0 = 안 자름).
//
// 루트가 준 판정식(왼쪽 12% 스트립 / 나머지의 밝은 화소 비 < 0.8 이면 띠)은 출발점으로 삼되
// 두 가지를 바꿨다. 둘 다 실측에서 걸린 문제다.
//
//  1) 밝음 기준을 140 으로 고정하면 저조도 샘플에서 흰 바탕까지 140 밑으로 내려가 두 비율이
//     모두 0 이 된다 → 그 번호판의 **Otsu 임계**를 밝음 기준으로 쓴다.
//  2) "띠가 있으면 왼쪽 12% 를 자른다" 는 고정 절단이 위험하다. 검출 단계가 이미 띠를 뺀
//     흰 판면만 잡아 오는 경우가 많은데(밝은 성분을 잡으니 당연하다), 그때 비가 0.79 처럼
//     애매하게 나오면 **첫 글자 '1' 이 통째로 잘려 나간다**(scene-01 에서 실제로 그랬다).
//     그래서 왼쪽부터 **어두운 열이 연속으로 이어지는 구간만** 자른다. 글자 앞에는 밝은 여백이
//     있으므로 첫 글자에서 멈춘다. 띠가 통째로 있으면 띠 끝까지, 띠 조각만 걸려 있으면 그만큼만.
int band_cut_columns(const cv::Mat& plate, std::string* dbg) {
  if (plate.cols < 20 || plate.rows < 4) {
    return 0;
  }

  cv::Mat bin;
  cv::threshold(plate, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  // 최대 절단 한계 — 이보다 많이 자르는 일은 없다.
  const int limit = std::max(1, static_cast<int>(std::lround(plate.cols * kBandStripRatio * 1.4)));

  const cv::Mat rest = bin(cv::Rect(limit, 0, bin.cols - limit, bin.rows));
  const double ref = cv::countNonZero(rest) / static_cast<double>(rest.total());
  if (ref < 1e-6) {
    return 0;
  }

  // 띠 안의 흰 KOR 글자 때문에 한두 열이 밝아질 수 있다 — 밝은 열이 몇 개 이어져야 끝으로 본다.
  const int bright_run_needed = std::max(2, plate.cols / 100);

  int cut = 0;
  int bright_run = 0;
  for (int x = 0; x < limit; ++x) {
    const double f = cv::countNonZero(bin.col(x)) / static_cast<double>(bin.rows);
    if (f < 0.70 * ref) {
      cut = x + 1;
      bright_run = 0;
    } else if (++bright_run >= bright_run_needed) {
      break;
    }
  }

  if (dbg != nullptr) {
    // 계약 §9.3 의 원래 판정식도 같이 찍어 둔다(수치표와 비교할 수 있게).
    const int strip_w = std::max(1, static_cast<int>(std::lround(plate.cols * kBandStripRatio)));
    const double strip_frac =
        cv::countNonZero(bin(cv::Rect(0, 0, strip_w, bin.rows))) / static_cast<double>(strip_w * bin.rows);
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "  띠판정 스트립=%.1f%% 나머지=%.1f%% 비=%.2f(계약식 임계 %.2f) → 실제 절단 %d열\n",
                  strip_frac * 100.0, ref * 100.0, ref > 1e-6 ? strip_frac / ref : 1.0,
                  kBandDecisionRatio, cut);
    *dbg += buf;
  }

  return cut;
}

// ---------------------------------------------------------------------------
// 9.4 문자 분할 — 세로 투영 프로파일
// ---------------------------------------------------------------------------

struct Run {
  int x0 = 0;
  int x1 = 0;  // 포함
  int width() const { return x1 - x0 + 1; }
};

// 문자 마스크(문자=255)를 만든다. 테두리 선과 너무 크거나 작은 성분은 버린다.
//
// 계약 §9.4 안전장치: 테두리에 닿은 성분을 지우는 건 **바깥 테두리 선**을 없애려는 것이지
// 글자를 없애려는 게 아니다. 번호판이 프레임을 꽉 채운 이미지에서는 글자가 좌우 끝에 닿아
// 함께 지워진다 — 그래서 지우고 난 결과가 기대 자릿수보다 적으면 그 제거를 되돌린다.
// (전부 지워 놓고 segment_fail 을 내는 게 이 단계의 가장 흔한 조용한 실패다)
cv::Mat char_mask(const cv::Mat& plate, int expected_chars, std::string* dbg) {
  cv::Mat bin;
  cv::threshold(plate, bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

  cv::Mat labels, stats, centroids;
  const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);

  const double plate_area = static_cast<double>(plate.cols) * plate.rows;

  cv::Mat strict(plate.size(), CV_8UC1, cv::Scalar(0));
  cv::Mat loose(plate.size(), CV_8UC1, cv::Scalar(0));
  int strict_count = 0;
  int loose_count = 0;

  for (int i = 1; i < n; ++i) {
    const int x = stats.at<int>(i, cv::CC_STAT_LEFT);
    const int y = stats.at<int>(i, cv::CC_STAT_TOP);
    const int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
    const int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
    const int area = stats.at<int>(i, cv::CC_STAT_AREA);

    // 테두리 선: 너무 넓거나 판 높이를 거의 다 차지한다. 이건 글자일 수 없다.
    if (w > plate.cols * 0.45 || h > plate.rows * 0.96) {
      continue;
    }
    // 한글 자모는 반 높이짜리가 나온다(고·구 처럼 위아래로 쌓인 글자) — 하한을 낮게 둔다.
    if (h < plate.rows * 0.12) {
      continue;
    }
    if (area < plate_area * 0.0004) {
      continue;
    }

    const cv::Rect bb(x, y, w, h);
    const cv::Mat comp = labels(bb) == i;
    loose(bb).setTo(255, comp);
    ++loose_count;

    if (x > 0 && x + w < plate.cols) {
      strict(bb).setTo(255, comp);
      ++strict_count;
    }
  }

  if (strict_count >= expected_chars) {
    return strict;
  }

  if (dbg != nullptr) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "  테두리 제거 되돌림(제거 후 %d개 < 기대 %d개, 되돌리면 %d개)\n", strict_count,
                  expected_chars, loose_count);
    *dbg += buf;
  }
  return loose;
}

// 세로 투영으로 글자 열을 나눈다. 한글은 획이 떨어져 있으므로(계약 §9.4)
// 간격이 좁고 합쳐도 한 글자 폭을 넘지 않는 이웃끼리는 병합한다.
std::vector<Run> segment_runs(const cv::Mat& mask, int expected_chars, std::string* dbg) {
  std::vector<Run> runs;
  const int min_col = std::max(1, static_cast<int>(mask.rows * 0.03));

  bool in_run = false;
  Run cur;
  for (int x = 0; x < mask.cols; ++x) {
    const int count = cv::countNonZero(mask.col(x));
    if (count >= min_col) {
      if (!in_run) {
        in_run = true;
        cur.x0 = x;
      }
      cur.x1 = x;
    } else if (in_run) {
      in_run = false;
      runs.push_back(cur);
    }
  }
  if (in_run) {
    runs.push_back(cur);
  }

  // 글자 하나가 차지할 평균 폭(간격 포함). 구조가 고정이라 이 값을 쓸 수 있다.
  const double pitch = static_cast<double>(mask.cols) / std::max(1, expected_chars);

  // **병합 가능한 쌍 중에서** 가장 좁은 틈을 고른다.
  // (전체 최소 틈을 고른 뒤 조건을 못 맞추면 거기서 멈추게 짜면 안 된다 — 실측에서
  //  숫자 사이의 좁은 틈 때문에 정작 'ㄱ'+'ㅏ' 병합이 시도조차 안 되어 9조각이 나왔다)
  bool merged = true;
  while (merged && runs.size() > 1) {
    merged = false;
    size_t best = 0;
    int best_gap = std::numeric_limits<int>::max();
    for (size_t i = 0; i + 1 < runs.size(); ++i) {
      const int gap = runs[i + 1].x0 - runs[i].x1 - 1;
      const int width = runs[i + 1].x1 - runs[i].x0 + 1;
      if (gap > pitch * 0.35 || width > pitch * 0.95) {
        continue;  // 이 쌍은 합칠 수 없다 — 다른 쌍을 계속 본다
      }
      if (gap < best_gap) {
        best_gap = gap;
        best = i;
        merged = true;
      }
    }
    if (merged) {
      runs[best].x1 = runs[best + 1].x1;
      runs.erase(runs.begin() + static_cast<long>(best) + 1);
    }
  }

  // 너무 좁은 조각(점·노이즈)은 버린다.
  std::vector<Run> kept;
  for (const Run& r : runs) {
    if (r.width() >= std::max(2, static_cast<int>(pitch * 0.12))) {
      kept.push_back(r);
    }
  }

  if (dbg != nullptr) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "  분할 pitch=%.1f 조각=%zu → ", pitch, kept.size());
    *dbg += buf;
    for (const Run& r : kept) {
      std::snprintf(buf, sizeof(buf), "[%d..%d w=%d] ", r.x0, r.x1, r.width());
      *dbg += buf;
    }
    *dbg += "\n";
  }

  return kept;
}

// 후보 하나를 끝까지 읽어 본다(투시보정 → 선명도 → 띠 → 분할 → 분류).
// r 에는 이 후보로 알아낸 것(quad·format·sharp·chars·value·reason)이 채워진다.
// 성공하면 true. 실패해도 r.reason 에 어디서 걸렸는지가 남는다.
bool read_plate(const cv::Mat& rotated, const std::vector<cv::Point2f>& quad, const Config& cfg,
                Result* r, std::string* dbg) {
  r->quad.clear();
  for (const cv::Point2f& p : quad) {
    r->quad.push_back(
        cv::Point(static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))));
  }

  // 장축/단축 비로 형식을 정한다(계약 §9.2). plate_format 이 auto 가 아니면 그 지정을 따른다.
  const double side_a = cv::norm(quad[1] - quad[0]);
  const double side_b = cv::norm(quad[3] - quad[0]);
  const double long_side = std::max(side_a, side_b);
  const double short_side = std::max(1.0, std::min(side_a, side_b));
  const double aspect = long_side / short_side;

  std::string format = cfg.plate_format;
  if (format == "auto") {
    format = aspect >= 3.5 ? "new" : "old";
  }
  r->format = format;

  const int norm_w = format == "old" ? kOldPlateW : kNewPlateW;
  const int norm_h = format == "old" ? kOldPlateH : kNewPlateH;
  const int expected_chars = format == "old" ? kOldChars : kNewChars;
  const int hangul_index = format == "old" ? kOldHangulIndex : kNewHangulIndex;

  // 투시보정 — 기울어진 번호판을 펴서 정규화 크기로 만든다.
  const cv::Point2f dst[4] = {{0.0f, 0.0f},
                              {static_cast<float>(norm_w - 1), 0.0f},
                              {static_cast<float>(norm_w - 1), static_cast<float>(norm_h - 1)},
                              {0.0f, static_cast<float>(norm_h - 1)}};
  const cv::Point2f src[4] = {quad[0], quad[1], quad[2], quad[3]};

  cv::Mat plate;
  cv::warpPerspective(rotated, plate, cv::getPerspectiveTransform(src, dst),
                      cv::Size(norm_w, norm_h), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

  // --- 선명도 (계약 §9.1 · REQ-0011) ----------------------------------------
  //
  // 계약이 적은 "번호판 ROI 라플라시안 분산" 은 이 판단에 **쓸 수 없다.** 26장 실측:
  //   읽어야 하는 저조도 샘플 278.8 < 거절해야 하는 모션블러 3787.0
  // 라플라시안 분산은 해상도와 대비에 같이 비례해서 커지므로, 작고 흐린 것과 크고 선명한 것을
  // 가르는 임계가 아예 존재하지 않는다. 정규화판에서 재도 마찬가지였다(288.9 vs 414.1).
  //
  // 대신 **정규화된 번호판에서 잰 방향별 평균 기울기를 그 판의 대비로 나눈 값**을 쓴다.
  //   sharp = min(mean|Sobel_x|, mean|Sobel_y|) / stddev(plate) x 100
  //   · 정규화판에서 재므로 번호판이 멀든 가깝든 같은 눈금이다(원본 ROI 는 해상도에 휘둘린다)
  //   · 대비로 나누므로 저조도에서 값이 같이 내려가지 않는다(45% 저조도 샘플이 여기서 살아난다)
  //   · x·y 중 **작은 쪽**을 쓴다 — 가로 모션블러는 세로 획을, 세로 블러는 가로 획을 먼저
  //     뭉갠다. 한 방향만 보면 반대 방향 블러를 통째로 놓친다.
  // 26장 실측 분리: 거절해야 하는 4장 ≤ 30.2, 읽어야 하는 22장 ≥ 52.4. 기본 임계 40 은 그 사이다.
  {
    cv::Mat sx, sy;
    cv::Sobel(plate, sx, CV_64F, 1, 0, 3);
    cv::Sobel(plate, sy, CV_64F, 0, 1, 3);
    cv::Scalar pmu, psg;
    cv::meanStdDev(plate, pmu, psg);
    const double gx = cv::mean(cv::abs(sx))[0];
    const double gy = cv::mean(cv::abs(sy))[0];
    r->sharp = psg[0] > 1e-6 ? sanitize(std::min(gx, gy) / psg[0] * 100.0, 0.0, 1.0e6) : 0.0;
  }

  if (dbg != nullptr) {
    char buf[224];
    std::snprintf(buf, sizeof(buf), "  판 aspect=%.2f 형식=%s 정규화=%dx%d 선명도=%.1f(임계 %.1f)\n",
                  aspect, format.c_str(), norm_w, norm_h, r->sharp, cfg.min_sharpness);
    *dbg += buf;
  }

  if (!cfg.gate_bypass && r->sharp < cfg.min_sharpness) {
    r->reason = Reason::kBlurry;
    return false;
  }

  // --- 9.3 파란 KOR 띠: 검사 후 조건부 절단 --------------------------------
  cv::Mat body = plate;
  {
    const int cut = band_cut_columns(plate, dbg);
    if (cut > 0 && cut < plate.cols - 10) {
      body = plate(cv::Rect(cut, 0, plate.cols - cut, plate.rows));
    }
  }
  r->plateW = body.cols;
  r->plateH = body.rows;

  // --- 9.4 문자 분할 --------------------------------------------------------
  const cv::Mat mask = char_mask(body, expected_chars, dbg);
  const std::vector<Run> runs = segment_runs(mask, expected_chars, dbg);

  if (static_cast<int>(runs.size()) != expected_chars) {
    // 구조가 안 맞으면 버린다. 억지로 맞추지 않는다(계약 §9.4).
    r->reason = Reason::kSegmentFail;
    return false;
  }

  // --- 9.5 분류 -------------------------------------------------------------
  std::string value;
  double min_conf = 1.0;
  r->chars.clear();

  for (size_t i = 0; i < runs.size(); ++i) {
    const Run& run = runs[i];
    const cv::Mat column = mask(cv::Rect(run.x0, 0, run.width(), mask.rows));
    const cv::Rect ink = cv::boundingRect(column);
    if (ink.width < 1 || ink.height < 1) {
      min_conf = 0.0;
      break;
    }

    const cv::Mat cell = column(ink);
    const bool want_hangul = static_cast<int>(i) == hangul_index;
    const Classification c = classify_cell(cell, want_hangul);

    if (dbg != nullptr) {
      char buf[192];
      std::snprintf(buf, sizeof(buf), "  [%zu] %s ncc1=%.3f (2등 %s %.3f) conf=%.3f %s\n", i,
                    c.label.empty() ? "?" : c.label.c_str(), c.best,
                    c.runner_up.empty() ? "-" : c.runner_up.c_str(), c.second, c.conf,
                    want_hangul ? "(한글자리)" : "");
      *dbg += buf;
      if (c.conf < 0.6) {
        *dbg += cell_ascii(normalize_cell(cell));
      }
    }

    if (c.label.empty()) {
      min_conf = 0.0;
      break;
    }

    CharResult cr;
    cr.c = c.label;
    cr.conf = c.conf;
    cr.box = {run.x0 + ink.x, ink.y, ink.width, ink.height};
    r->chars.push_back(cr);

    value += c.label;
    min_conf = std::min(min_conf, c.conf);
  }

  if (static_cast<int>(r->chars.size()) != expected_chars) {
    r->chars.clear();
    r->reason = Reason::kSegmentFail;
    return false;
  }

  r->conf = sanitize(min_conf, 0.0, 1.0);

  // 가장 약한 고리가 기준(계약 §5·§9.5). 하나라도 미달이면 판독 전체를 버린다.
  if (min_conf < cfg.min_confidence) {
    r->reason = Reason::kLowConf;
    return false;
  }

  r->value = value;
  r->reason = Reason::kOk;
  return true;
}

}  // namespace

const char* reason_name(Reason r) {
  switch (r) {
    case Reason::kOk: return "ok";
    case Reason::kMoving: return "moving";
    case Reason::kNoPlate: return "no_plate";
    case Reason::kBlurry: return "blurry";
    case Reason::kSegmentFail: return "segment_fail";
    case Reason::kLowConf: return "low_conf";
  }
  return "no_plate";
}

// ---------------------------------------------------------------------------
// MotionGate — 계약 §9.1
// ---------------------------------------------------------------------------

void MotionGate::configure(double motion_threshold, int still_frames) {
  motion_threshold_ = motion_threshold;
  still_frames_ = std::max(1, still_frames);
}

void MotionGate::reset() {
  prev_small_.release();
  still_run_ = 0;
}

MotionGate::Verdict MotionGate::update(const cv::Mat& gray) {
  Verdict v;

  if (gray.empty()) {
    // 볼 게 없으면 움직임으로 취급하고 카운트를 끊는다.
    prev_small_.release();
    still_run_ = 0;
    return v;
  }

  // 작게 줄여 비교한다 — 프레임당 1ms 수준이어야 하는 단계다(§9.1).
  cv::Mat small;
  cv::resize(gray, small, cv::Size(160, 120), 0, 0, cv::INTER_AREA);
  small.convertTo(small, CV_32F);

  if (prev_small_.empty() || prev_small_.size() != small.size()) {
    // 첫 프레임(또는 해상도 변경)은 비교 대상이 없다 → moving:true (§9.1).
    prev_small_ = small;
    still_run_ = 0;
    v.moving = true;
    v.motion = 0.0;
    return v;
  }

  // motion = **프레임이 몇 화소 움직였는가**(160x120 기준). 계약 개정 5까지의 '평균 절대차분'이
  // 아니다 — 그 지표로는 손떨림과 실제 이동을 가를 수 없다는 것이 실측으로 확인됐다(REQ-0014).
  //
  // 평균 절대차분은 이동량뿐 아니라 **장면의 무늬 세기**에도 비례한다. 그래서 무늬가 강한 장면의
  // 손떨림(12.9)이 무늬가 약한 장면의 실제 이동(3.2)보다 큰 값을 내고, 두 무리가 4배 폭으로
  // 겹친다. 어떤 임계를 넣어도 한쪽이 틀린다:
  //   임계 0.60 → 손에 든 폰의 프레임 154/231 을 '움직임' 으로 버린다(= 아무것도 못 읽는다)
  //   임계 13   → 손떨림은 통과하지만 실제 이동 68/77 도 같이 통과한다(= 게이트가 없는 것과 같다)
  //
  // 위상 상관은 주파수 영역에서 정점을 찾아 **이동량 자체**를 돌려주므로 무늬 세기가 지워진다.
  // 실측(28열 308프레임): 고정 0.001~0.040 · 손떨림 0.033~2.081 · 실제이동 1.971~5.194.
  // 비용은 프레임당 0.32ms(에뮬레이터 arm64) — 계약 §9.1 의 1ms 예산 안이다.
  if (hann_.empty() || hann_.size() != small.size()) {
    cv::createHanningWindow(hann_, small.size(), CV_32F);
  }
  const cv::Point2d shift = cv::phaseCorrelate(prev_small_, small, hann_);
  v.motion = std::sqrt(shift.x * shift.x + shift.y * shift.y);
  prev_small_ = small;

  if (v.motion < motion_threshold_) {
    if (still_run_ < std::numeric_limits<int>::max()) {
      ++still_run_;
    }
  } else {
    // 한 프레임이라도 임계를 넘으면 연속 카운트는 0 으로 끊는다(감소가 아니다).
    still_run_ = 0;
  }

  v.moving = still_run_ < still_frames_;
  return v;
}

// ---------------------------------------------------------------------------
// StableLatch — 계약 §5.1
// ---------------------------------------------------------------------------

void StableLatch::configure(int stable_frames) {
  stable_frames_ = std::max(1, stable_frames);
}

void StableLatch::reset() {
  last_value_.clear();
  run_ = 0;
  no_plate_run_ = 0;
  latched_value_.clear();
  latched_ = false;
}

StableLatch::Verdict StableLatch::update(const std::string& value, bool plate_found) {
  Verdict v;

  // 재무장 규칙 2(개정 2): 번호판을 못 찾은 프레임이 stable_frames 회 연속이면 래치를 푼다.
  // = 차를 보냈다가 같은 차를 다시 비추면 또 fresh 가 떠야 한다.
  if (!plate_found) {
    if (no_plate_run_ < std::numeric_limits<int>::max()) {
      ++no_plate_run_;
    }
    if (no_plate_run_ >= stable_frames_) {
      latched_ = false;
    }
  } else {
    no_plate_run_ = 0;
  }

  if (value.empty()) {
    run_ = 0;
    last_value_.clear();
    return v;
  }

  if (value == last_value_) {
    if (run_ < std::numeric_limits<int>::max()) {
      ++run_;
    }
  } else {
    last_value_ = value;
    run_ = 1;
  }

  // 재무장 규칙 1: 래치된 값과 다른 값이 보이면 래치를 푼다.
  if (latched_ && value != latched_value_) {
    latched_ = false;
  }

  v.stable = run_ >= stable_frames_;

  // fresh 는 stable 이 거짓→참으로 바뀌는 그 한 프레임에만.
  if (v.stable && !latched_) {
    v.fresh = true;
    latched_ = true;
    latched_value_ = value;
  }

  return v;
}

// ---------------------------------------------------------------------------
// 회전 보정
// ---------------------------------------------------------------------------

cv::Mat rotate_gray(const cv::Mat& src, int rotation_degrees) {
  int deg = rotation_degrees % 360;
  if (deg < 0) {
    deg += 360;
  }

  if (src.empty() || deg == 0 || (deg % 90) != 0) {
    return src;
  }

  cv::Mat dst;
  switch (deg) {
    case 90: cv::rotate(src, dst, cv::ROTATE_90_CLOCKWISE); break;
    case 180: cv::rotate(src, dst, cv::ROTATE_180); break;
    case 270: cv::rotate(src, dst, cv::ROTATE_90_COUNTERCLOCKWISE); break;
    default: return src;
  }
  return dst;
}

// ---------------------------------------------------------------------------
// PlatePipeline
// ---------------------------------------------------------------------------

PlatePipeline::PlatePipeline() {
  motion_.configure(cfg_.motion_threshold, cfg_.still_frames);
  latch_.configure(cfg_.stable_frames);
}

void PlatePipeline::configure(const std::string& kv_lines) {
  size_t pos = 0;
  while (pos <= kv_lines.size()) {
    const size_t nl = kv_lines.find('\n', pos);
    const std::string raw =
        kv_lines.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = (nl == std::string::npos) ? kv_lines.size() + 1 : nl + 1;

    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    std::string key = trim(line.substr(0, eq));
    const std::string val = trim(line.substr(eq + 1));

    // "vision." 접두어는 있어도 되고 없어도 된다.
    const std::string prefix = "vision.";
    if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0) {
      key = key.substr(prefix.size());
    }

    // 값이 깨진 키는 건드리지 않는다 — 이전 값이 그대로 유지된다(계약 §4).
    double d = 0.0;
    int i = 0;
    bool b = false;
    if (key == "min_confidence") {
      if (parse_double(val, &d)) cfg_.min_confidence = sanitize(d, 0.0, 1.0);
    } else if (key == "stable_frames") {
      if (parse_int(val, &i)) cfg_.stable_frames = std::max(1, i);
    } else if (key == "motion_threshold") {
      if (parse_double(val, &d)) cfg_.motion_threshold = sanitize(d, 0.0, 1.0e4);
    } else if (key == "still_frames") {
      if (parse_int(val, &i)) cfg_.still_frames = std::max(1, i);
    } else if (key == "min_sharpness") {
      if (parse_double(val, &d)) cfg_.min_sharpness = sanitize(d, 0.0, 1.0e9);
    } else if (key == "min_plate_width_ratio") {
      if (parse_double(val, &d)) cfg_.min_plate_width_ratio = sanitize(d, 0.0, 1.0);
    } else if (key == "plate_format") {
      if (val == "auto" || val == "new" || val == "old") cfg_.plate_format = val;
    } else if (key == "gate_bypass") {
      if (parse_bool(val, &b)) cfg_.gate_bypass = b;
    }
    // 모르는 키는 조용히 무시한다(계약 §6).
  }

  motion_.configure(cfg_.motion_threshold, cfg_.still_frames);
  latch_.configure(cfg_.stable_frames);
}

std::string PlatePipeline::version() {
  return cv::getVersionString();
}

Result PlatePipeline::process(const cv::Mat& gray, int rotation_degrees) {
  const int64 t0 = cv::getTickCount();

  debug_log_.clear();

  Result r;

  const cv::Mat rotated = rotate_gray(gray, rotation_degrees);
  r.w = rotated.cols;
  r.h = rotated.rows;

  const bool usable = !rotated.empty() && rotated.type() == CV_8UC1;

  // --- 9.1 움직임 게이트 ---------------------------------------------------
  const MotionGate::Verdict motion = motion_.update(usable ? rotated : cv::Mat());
  r.motion = sanitize(motion.motion, 0.0, 1.0e4);
  r.moving = cfg_.gate_bypass ? false : motion.moving;

  if (r.moving) {
    // 움직이는 동안에는 뒤 단계를 아예 돌리지 않는다(§9.1).
    r.reason = Reason::kMoving;
    return finish(r, t0);
  }

  // --- 9.2~9.5 후보를 순서대로 읽어 본다 -------------------------------------
  //
  // 한 후보만 골라 진행하면, 배경의 큰 밝은 사각형이 번호판보다 그럴듯해 보이는 순간
  // 프레임 전체를 버리게 된다(실측: 640x480 장면에서 실제로 그랬다).
  // 그래서 점수 순으로 몇 개를 받아 **구조가 맞는 후보가 나올 때까지** 시도한다.
  // 실패하면 가장 그럴듯했던 첫 후보의 실패 이유를 남긴다 — 그게 튜닝에 쓸모 있는 정보다.
  std::string* dbg = debug_enabled_ ? &debug_log_ : nullptr;

  std::vector<std::vector<cv::Point2f>> quads;
  if (!usable || !detect_plate(rotated, cfg_, &quads, dbg)) {
    r.plate = false;
    r.reason = Reason::kNoPlate;
    return finish(r, t0);
  }

  r.plate = true;

  Result first_attempt;
  for (size_t i = 0; i < quads.size(); ++i) {
    if (dbg != nullptr && quads.size() > 1) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "  --- 후보 %zu/%zu 시도\n", i + 1, quads.size());
      *dbg += buf;
    }

    Result attempt = r;
    attempt.reason = Reason::kNoPlate;
    if (read_plate(rotated, quads[i], cfg_, &attempt, dbg)) {
      return finish(attempt, t0);
    }
    if (i == 0) {
      first_attempt = attempt;
    }
  }

  first_attempt.w = r.w;
  first_attempt.h = r.h;
  first_attempt.moving = r.moving;
  first_attempt.motion = r.motion;
  first_attempt.plate = true;
  first_attempt.value.clear();
  return finish(first_attempt, t0);
}


// 래치 갱신과 시간 측정은 어느 경로로 빠져나가든 똑같이 거쳐야 한다.
Result PlatePipeline::finish(Result& r, long long t0) {
  // 래치에는 `r.plate`(사각형 후보를 찾았나)가 아니라 **문자 구조까지 확인됐나**를 넘긴다.
  //
  // r.plate 는 후보가 필터를 통과하기만 해도 true 다 — 책장 모서리·배경 사각형에서도 true 가 된다
  // (scene-07-negative 가 바로 그 경우다). 그걸 그대로 넘기면 어수선한 배경에서는 no_plate 연속
  // 카운트가 매 프레임 0으로 리셋되어 **재무장 규칙 2 가 영원히 발동하지 않고**, 차가 떠났다가
  // 같은 차가 다시 와도 fresh 가 안 뜬다(계약 §5.1 이 정확히 막으려던 상황이다).
  // 반면 JSON 의 plate 필드는 계약 §5 대로 "사각형을 찾았는가" 그대로 내보낸다.
  const bool confirmed = r.reason == Reason::kOk || r.reason == Reason::kLowConf;
  const StableLatch::Verdict verdict = latch_.update(r.value, confirmed);
  r.stable = verdict.stable;
  r.fresh = verdict.fresh;

  const double freq = cv::getTickFrequency();
  r.ms = freq > 0.0 ? (static_cast<double>(cv::getTickCount() - t0) / freq) * 1000.0 : 0.0;
  r.ms = sanitize(r.ms, 0.0, 1.0e9);
  return r;
}

std::string PlatePipeline::to_json(const Result& r) {
  std::string out;
  out.reserve(256 + r.chars.size() * 48);

  out += "{\"value\":\"";
  append_escaped(out, r.value);
  out += "\",\"conf\":";
  append_double(out, sanitize(r.conf, 0.0, 1.0), "%.3f");
  out += ",\"stable\":";
  out += r.stable ? "true" : "false";
  out += ",\"fresh\":";
  out += r.fresh ? "true" : "false";
  out += ",\"moving\":";
  out += r.moving ? "true" : "false";
  out += ",\"motion\":";
  append_double(out, sanitize(r.motion, 0.0, 1.0e4), "%.3f");
  out += ",\"plate\":";
  out += r.plate ? "true" : "false";
  out += ",\"sharp\":";
  append_double(out, sanitize(r.sharp, 0.0, 1.0e9), "%.1f");
  out += ",\"format\":\"";
  append_escaped(out, r.format);
  out += "\",\"reason\":\"";
  out += reason_name(r.reason);
  out += "\",\"quad\":[";
  for (size_t i = 0; i < r.quad.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    out += '[';
    append_int(out, r.quad[i].x);
    out += ',';
    append_int(out, r.quad[i].y);
    out += ']';
  }
  out += "],\"chars\":[";
  for (size_t i = 0; i < r.chars.size(); ++i) {
    if (i != 0) {
      out += ',';
    }
    const CharResult& c = r.chars[i];
    out += "{\"c\":\"";
    append_escaped(out, c.c);
    out += "\",\"conf\":";
    append_double(out, sanitize(c.conf, 0.0, 1.0), "%.3f");
    out += ",\"box\":[";
    append_int(out, c.box.x);
    out += ',';
    append_int(out, c.box.y);
    out += ',';
    append_int(out, c.box.w);
    out += ',';
    append_int(out, c.box.h);
    out += "]}";
  }
  out += "],\"plateW\":";
  append_int(out, r.plateW);
  out += ",\"plateH\":";
  append_int(out, r.plateH);
  out += ",\"w\":";
  append_int(out, r.w);
  out += ",\"h\":";
  append_int(out, r.h);
  out += ",\"ms\":";
  append_double(out, sanitize(r.ms, 0.0, 1.0e9), "%.2f");
  out += '}';

  return out;
}

}  // namespace digitcam
