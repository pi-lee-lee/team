// motion_test.cpp — 움직임 게이트(계약 §9.1)의 motion 값을 실측하고 임계를 찾는다 (REQ-0014)
//
// 목적은 "잘 도는지" 가 아니라 **세 무리가 값으로 갈리는가** 다:
//   still — 고정 카메라(센서 잡음만)          → 정지로 판정되어야 한다
//   hand  — 사람이 들고 멈춰 있음(1~3px 떨림)  → **정지로 판정되어야 한다**
//   move  — 실제 이동(8~20px)                 → 움직임으로 판정되어야 한다
// hand 와 move 사이에 빈 구간이 없으면 '평균 절대차분' 이 이 판단에 부적합하다는 뜻이다.
// 그 경우를 대비해 대안 지표 두 개(상위 백분위수, 큰 변화 화소 비율)도 같이 잰다.
//
// 사용법: motion_test <motion 데이터 디렉터리>

#include "digit_pipeline.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Sequence {
  std::string name;
  std::string kind;  // still | hand | move
  int frames = 0;
};

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
  f.get();
  cv::Mat img(h, w, CV_8UC1);
  f.read(reinterpret_cast<char*>(img.data), static_cast<std::streamsize>(w) * h);
  if (!f) {
    return false;
  }
  *out = img;
  return true;
}

// 파이프라인이 쓰는 것과 같은 축소본에서 대안 지표를 잰다(같은 조건에서 비교해야 의미가 있다).
struct AltMetrics {
  double mean_abs = 0.0;   // 지금 지표
  double p95 = 0.0;        // 차분의 95 백분위수
  double frac_gt8 = 0.0;   // 차분 8 초과 화소 비율(%)
  double coarse = 0.0;     // 더 작게(40x30) 줄여서 잰 평균 차분
  double px = 0.0;         // 시간차분 / 공간기울기 = 대략적인 이동량(화소)
  double phase_px = 0.0;   // 위상 상관으로 직접 잰 이동량(화소)
};

AltMetrics alt_metrics(const cv::Mat& prev, const cv::Mat& cur) {
  AltMetrics m;
  cv::Mat a, b;
  cv::resize(prev, a, cv::Size(160, 120), 0, 0, cv::INTER_AREA);
  cv::resize(cur, b, cv::Size(160, 120), 0, 0, cv::INTER_AREA);

  cv::Mat diff;
  cv::absdiff(a, b, diff);
  m.mean_abs = cv::mean(diff)[0];

  std::vector<unsigned char> v(diff.begin<unsigned char>(), diff.end<unsigned char>());
  std::sort(v.begin(), v.end());
  m.p95 = v.empty() ? 0.0 : v[static_cast<size_t>(v.size() * 0.95)];

  int big = 0;
  for (const unsigned char x : v) {
    if (x > 8) {
      ++big;
    }
  }
  m.frac_gt8 = v.empty() ? 0.0 : 100.0 * big / static_cast<double>(v.size());

  // 대안 1 — 더 작게 줄여서 잰 평균 차분.
  // 작은 평행이동은 축소하면 사라지고 큰 이동만 남는다. 계산은 지금과 같은 수준으로 싸다.
  {
    cv::Mat ca, cb, cd;
    cv::resize(prev, ca, cv::Size(40, 30), 0, 0, cv::INTER_AREA);
    cv::resize(cur, cb, cv::Size(40, 30), 0, 0, cv::INTER_AREA);
    cv::absdiff(ca, cb, cd);
    m.coarse = cv::mean(cd)[0];
  }

  // 대안 2 — 시간차분을 공간 기울기로 나눈다 = 대략적인 **이동량(화소)**.
  //
  // 평균 절대차분이 손떨림과 실제 이동을 못 가르는 이유는 그 값이 이동량뿐 아니라
  // **장면의 무늬 세기**에도 비례하기 때문이다(무늬가 강한 scene-03 의 손떨림 6.5 가
  // 무늬가 약한 scene-07 의 실제 이동 4.7 보다 크게 나온다). 기울기로 나누면 그 항이 지워지고
  // 남는 것이 이동량 자체다. 광류의 1차 근사(|I_t| / |∇I|)이고 DFT 없이 뺄셈·Sobel 로 끝난다.
  {
    cv::Mat gx, gy;
    cv::Sobel(a, gx, CV_32F, 1, 0, 3);
    cv::Sobel(a, gy, CV_32F, 0, 1, 3);
    const double grad = (cv::mean(cv::abs(gx))[0] + cv::mean(cv::abs(gy))[0]) * 0.5;
    // Sobel 3x3 은 기울기를 4배로 키운다(커널 합 기준) — 화소 단위로 되돌린다.
    m.px = grad > 1e-6 ? m.mean_abs / (grad / 4.0) : 0.0;
  }

  // 대안 3 — 위상 상관(cv::phaseCorrelate)으로 **이동량을 직접** 잰다.
  //
  // 대안 2 의 |I_t|/|∇I| 는 1차 근사라 이동이 1화소를 넘어가면 포화된다(실측에서 실제 이동이
  // 0.84~2.33 으로 눌려 손떨림과 겹쳤다). 위상 상관은 주파수 영역에서 정점을 찾으므로
  // 큰 이동도 그대로 나온다. 대가는 DFT 두 번이다.
  {
    cv::Mat fa, fb;
    a.convertTo(fa, CV_32F);
    b.convertTo(fb, CV_32F);
    cv::Mat win;
    cv::createHanningWindow(win, fa.size(), CV_32F);
    const cv::Point2d shift = cv::phaseCorrelate(fa, fb, win);
    m.phase_px = std::sqrt(shift.x * shift.x + shift.y * shift.y);
  }

  return m;
}

struct Stat {
  double min = 1e18;
  double max = -1e18;
  double sum = 0.0;
  int n = 0;
  void add(double v) {
    min = std::min(min, v);
    max = std::max(max, v);
    sum += v;
    ++n;
  }
  double avg() const { return n > 0 ? sum / n : 0.0; }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("사용법: motion_test <motion 데이터 디렉터리>\n");
    return 2;
  }
  const std::string root = argv[1];

  std::vector<Sequence> seqs;
  {
    std::ifstream f(root + "/sequences.tsv");
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }
      std::istringstream ss(line);
      Sequence s;
      std::string frames;
      std::getline(ss, s.name, '\t');
      std::getline(ss, s.kind, '\t');
      std::getline(ss, frames, '\t');
      s.frames = std::atoi(frames.c_str());
      seqs.push_back(s);
    }
  }
  if (seqs.empty()) {
    std::printf("sequences.tsv 를 읽지 못했다: %s\n", (root + "/sequences.tsv").c_str());
    return 2;
  }

  std::printf("OpenCV %s · 프레임 열 %zu개\n\n", digitcam::PlatePipeline::version().c_str(),
              seqs.size());
  std::printf("%-30s %-6s %7s %7s %7s | %6s %7s %8s\n", "열", "종류", "motion최소", "평균", "최대",
              "40x30", "이동px", "위상px");
  std::printf("--------------------------------------------------------------------------------\n");

  Stat by_kind[3];   // still, hand, move — 지금 지표(평균 절대차분)
  Stat by_kind_px[3];      // 대안 2(이동량 화소, 1차 근사)
  Stat by_kind_coarse[3];  // 대안 1(40x30 평균 차분)
  Stat by_kind_phase[3];   // 대안 3(위상 상관 이동량)
  // hand 로 시작하는 이름은 전부 '정지로 판정되어야 하는' 무리다(hand1 등 변형 포함).
  auto kind_index = [](const std::string& k) {
    if (k == "still") return 0;
    if (k.rfind("hand", 0) == 0) return 1;
    return 2;
  };

  // 열마다 프레임별 값을 모아 둔다(임계 후보 평가에 다시 쓴다).
  std::vector<std::pair<std::string, std::vector<double>>> per_seq;        // 지금 지표
  std::vector<std::pair<std::string, std::vector<double>>> per_seq_phase;  // 위상 상관

  // 두 방식의 프레임당 비용(계약 §9.1 은 1ms 수준을 요구한다)
  double cost_mean_ms = 0.0;
  double cost_phase_ms = 0.0;
  int cost_n = 0;

  for (const Sequence& s : seqs) {
    digitcam::MotionGate gate;
    gate.configure(1e9, 1);  // 임계는 여기서 쓰지 않는다 — 값만 뽑는다

    Stat st;
    AltMetrics alt_sum;
    int alt_n = 0;
    std::vector<double> motions;
    std::vector<double> phases;
    cv::Mat prev;

    for (int i = 0; i < s.frames; ++i) {
      char name[32];
      std::snprintf(name, sizeof(name), "/%02d.pgm", i);
      cv::Mat img;
      if (!read_pgm(root + "/" + s.name + name, &img)) {
        std::printf("✗ %s%s 읽기 실패\n", s.name.c_str(), name);
        break;
      }

      const digitcam::MotionGate::Verdict v = gate.update(img);
      if (i > 0) {  // 첫 프레임은 비교 대상이 없어 motion=0 이다(계약 §9.1)
        st.add(v.motion);
        motions.push_back(v.motion);
        by_kind[kind_index(s.kind)].add(v.motion);

        const AltMetrics m = alt_metrics(prev, img);
        alt_sum.p95 += m.p95;
        alt_sum.frac_gt8 += m.frac_gt8;
        alt_sum.coarse += m.coarse;
        alt_sum.px += m.px;
        alt_sum.phase_px += m.phase_px;
        by_kind_px[kind_index(s.kind)].add(m.px);
        by_kind_coarse[kind_index(s.kind)].add(m.coarse);
        by_kind_phase[kind_index(s.kind)].add(m.phase_px);
        phases.push_back(m.phase_px);
        ++alt_n;

        // 두 방식의 프레임당 비용을 같은 입력에서 잰다.
        {
          const double freq = cv::getTickFrequency();
          cv::Mat a, b;
          cv::resize(prev, a, cv::Size(160, 120), 0, 0, cv::INTER_AREA);
          cv::resize(img, b, cv::Size(160, 120), 0, 0, cv::INTER_AREA);

          int64 t = cv::getTickCount();
          cv::Mat d;
          cv::absdiff(a, b, d);
          volatile double sink = cv::mean(d)[0];
          (void)sink;
          cost_mean_ms += (cv::getTickCount() - t) / freq * 1000.0;

          cv::Mat fa, fb, win;
          a.convertTo(fa, CV_32F);
          b.convertTo(fb, CV_32F);
          cv::createHanningWindow(win, fa.size(), CV_32F);
          t = cv::getTickCount();
          const cv::Point2d sh = cv::phaseCorrelate(fa, fb, win);
          (void)sh;
          cost_phase_ms += (cv::getTickCount() - t) / freq * 1000.0;
          ++cost_n;
        }
      }
      prev = img;
    }

    if (st.n == 0) {
      continue;
    }
    std::printf("%-30s %-6s %7.3f %7.3f %7.3f | %6.2f %7.3f %8.3f\n", s.name.c_str(),
                s.kind.c_str(), st.min, st.avg(), st.max, alt_sum.coarse / alt_n,
                alt_sum.px / alt_n, alt_sum.phase_px / alt_n);
    per_seq.emplace_back(s.kind, motions);
    per_seq_phase.emplace_back(s.kind, phases);
  }

  std::printf("\n무리별 motion 범위\n");
  const char* names[3] = {"still", "hand ", "move "};
  for (int i = 0; i < 3; ++i) {
    if (by_kind[i].n > 0) {
      std::printf("  %s  최소 %7.3f  평균 %7.3f  최대 %7.3f  (표본 %d)\n", names[i], by_kind[i].min,
                  by_kind[i].avg(), by_kind[i].max, by_kind[i].n);
    }
  }

  // 지표 세 개를 같은 기준으로 비교한다: 정지로 봐야 하는 것의 최대 vs 움직임의 최소.
  auto verdict = [](const char* label, const Stat* k) {
    const double still_hand_max = std::max(k[0].max, k[1].max);
    const double move_min = k[2].min;
    std::printf("\n[%s] 정지로 봐야 하는 것 최대 = %.3f · 움직임 최소 = %.3f\n", label,
                still_hand_max, move_min);
    if (move_min > still_hand_max) {
      std::printf("  → 빈 구간 있음. 로그 중앙 %.2f (배수 %.1f배)\n",
                  std::sqrt(still_hand_max * move_min), move_min / still_hand_max);
    } else {
      std::printf("  → ⚠ 겹친다. 이 지표로는 두 무리를 가를 수 없다.\n");
    }
  };

  verdict("지금 지표: 평균 절대차분(160x120)", by_kind);
  verdict("대안1: 평균 절대차분(40x30)", by_kind_coarse);
  verdict("대안2: 이동량 1차근사(화소)", by_kind_px);
  verdict("대안3: 위상 상관 이동량(화소)", by_kind_phase);

  std::printf("\n무리별 위상 상관 이동량(화소, 160x120 기준)\n");
  for (int i = 0; i < 3; ++i) {
    if (by_kind_phase[i].n > 0) {
      std::printf("  %s  최소 %6.3f  평균 %6.3f  최대 %6.3f\n", names[i], by_kind_phase[i].min,
                  by_kind_phase[i].avg(), by_kind_phase[i].max);
    }
  }

  // 임계 후보를 실제로 적용해 무리별 오판을 센다.
  auto sweep = [](const char* label, const std::vector<std::pair<std::string, std::vector<double>>>& data,
                  const std::vector<double>& cands) {
    std::printf("\n%s — 임계 후보별 오판 (정지로 봐야 할 것을 움직임으로 / 그 반대)\n", label);
    for (const double th : cands) {
      int false_moving = 0, false_still = 0, still_total = 0, move_total = 0;
      for (const auto& p : data) {
        for (const double m : p.second) {
          if (p.first == "move") {
            ++move_total;
            if (m < th) {
              ++false_still;
            }
          } else {
            ++still_total;
            if (m >= th) {
              ++false_moving;
            }
          }
        }
      }
      std::printf("  임계 %5.2f : 정지→움직임 오판 %3d/%3d · 움직임→정지 오판 %3d/%3d %s\n", th,
                  false_moving, still_total, false_still, move_total,
                  (false_moving == 0 && false_still == 0) ? "← 둘 다 0" : "");
    }
  };

  sweep("지금 지표(평균 절대차분)", per_seq, {0.60, 1.00, 2.00, 3.00, 6.00, 13.00, 14.00});
  sweep("대안3(위상 상관 이동량 px)", per_seq_phase,
        {0.50, 1.00, 1.50, 1.80, 2.00, 2.10, 2.50, 3.00});

  if (cost_n > 0) {
    std::printf("\n프레임당 비용(160x120 축소는 두 방식 공통이라 뺐다)\n");
    std::printf("  평균 절대차분 : %.3f ms\n", cost_mean_ms / cost_n);
    std::printf("  위상 상관     : %.3f ms  (%.1f배)\n", cost_phase_ms / cost_n,
                cost_mean_ms > 0 ? cost_phase_ms / cost_mean_ms : 0.0);
  }

  return 0;
}
