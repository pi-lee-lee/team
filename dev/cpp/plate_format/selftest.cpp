// selftest.cpp — plate_format.h 자가검증
//
// 🔴 **초록불만 내는 시험은 그 시험이 도는지도 증명하지 못한다.**
//    그래서 이 표는 **통과해야 하는 것과 거절해야 하는 것을 같이** 담는다.
//    ★ 마지막 절(§4)은 **알려진 한계**를 값으로 박아 둔다 — 나중에 "몰랐다" 가 안 되게.
//
// 빌드 : c++ -std=c++11 -Wall -Wextra -o /tmp/pf cpp/plate_format/selftest.cpp && /tmp/pf
// 종료 : 실패가 하나라도 있으면 exit=1

#include <cstdio>
#include <string>
#include "plate_format.h"

using plate_format::plate_format_ok;

static int fails = 0, total = 0;

static void expect(const char* what, const std::string& in, bool want) {
    ++total;
    const bool got = plate_format_ok(in);
    if (got != want) {
        ++fails;
        std::printf("  🔴 %s : \"%s\" → %s (기대 %s)\n",
                    what, in.c_str(), got ? "통과" : "거절", want ? "통과" : "거절");
    }
}

int main() {
    // ── §1 실기 표본 — **통과해야 한다** ────────────────────────────────
    // 출처: 2026-08-26 서버 로그 전수(socket 실측). ⚠ 분모가 5종뿐이다.
    std::printf("§1 실기 표본(통과해야 함)\n");
    expect("실기", "238다5927", true);
    expect("실기", "427나6153", true);
    expect("실기", "123가4568", true);
    expect("실기", "700가4568", true);
    expect("실기", "123바9898", true);
    expect("2자리 앞",  "12가3456", true);   // 옛 형식(앞 2자리)

    // ── §2 잡음 — **거절해야 한다** ─────────────────────────────────────
    // 출처: 2026-08-26 16:39:27, cpp 가 5500 에 curl 한 HTTP 헤더가 그대로 번호판이 됐다.
    std::printf("§2 실제로 들어왔던 잡음(거절해야 함)\n");
    expect("curl", "GET / HTTP/1.1",        false);
    expect("curl", "Host: 127.0.0.1:5500",  false);
    expect("curl", "User-Agent: curl/8.7.1", false);
    expect("curl", "Accept: */*",           false);

    // ── §3 경계 — 규칙의 각 조항이 **실제로 문다** ──────────────────────
    std::printf("§3 경계(각 조항이 무는지)\n");
    expect("빈 값",        "",             false);
    expect("앞 1자리",     "1가3456",      false);  // lead >= 2
    expect("앞 4자리",     "1234가3456",   false);  // lead <= 3
    expect("한글 없음",    "1234567",      false);
    expect("한글 2자",     "123가나4567",  false);
    expect("뒤 3자리",     "123가345",     false);  // tail == 4
    expect("뒤 5자리",     "123가34567",   false);
    expect("꼬리 붙음",    "123가4567X",   false);  // i == ch.size()
    expect("공백 붙음",    "123가4567 ",   false);
    expect("앞 공백",      " 123가4567",   false);
    expect("한글이 앞",    "가1234567",    false);
    expect("자모(ㄱ)",     "123\xE3\x84\xB1" "4567", false);  // U+3131 — 완성형 밖
    expect("깨진 UTF-8",   "123\xEA\xB0" "4567",     false);  // 잘린 3바이트

    // ── §4 🔴 **알려진 한계 — 이것도 값이다** ───────────────────────────
    // 한글 범위가 `가-힣` 전체라, 실제 번호판에 없는 음절도 **통과한다.**
    // ⚠ 이 줄이 `true` 인 것은 **결함이 아니라 선택**이다(plate_format.h 머리말 참조).
    //   좁히면 잡음을 더 잡지만 **정상을 조용히 막는다.**
    // ★ 이 줄이 언젠가 `false` 가 되면 그건 **누가 범위를 좁혔다는 뜻**이다 — 그때 이 주석을 읽어라.
    std::printf("§4 알려진 한계(통과가 정상)\n");
    expect("용도문자 아님", "123\xEB\xB7\x81" "9898", true);   // 123뷁9898

    std::printf("\n%s  %d/%d 통과 · 실패 %d\n",
                fails ? "🔴 실패" : "✅ 전부 통과", total - fails, total, fails);
    if (!fails) {
        std::printf("🔑 그리고 위 §2·§3 이 **거절**로 통과했다 — 이 검사는 빨간불도 낸다.\n");
    }
    return fails ? 1 : 0;
}
