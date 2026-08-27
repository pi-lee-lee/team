// plate_format.h — 번호판 **형식** 검증 (구조적 · 문턱 없음 · 의존 없음)
//
// 쓰는 곳 : 인식기 밖에서 "이 문자열이 번호판 모양인가"만 묻고 싶을 때.
//           예) 서버가 카메라 포트로 들어온 줄에서 **잡음을 가려낼 때**.
//
// 🔴 **정본은 여기가 아니다** — `android/app/src/main/cpp/plate_ocr.cpp` 의
//    `PlateOcr::plate_format_ok()` · `is_hangul()` · `is_digit()` 이 정본이다.
//    이 파일은 그 규칙을 **의존 없이 쓸 수 있게 떼어 온 사본**이다(서버는 OpenCV·JNI 를 안 링크한다).
//    ⚠ 사본이므로 **갈릴 수 있다.** 아래 `plate_format_selftest()` 의 표가 그 방어선이다 —
//      정본을 고치면 이 표도 같이 고쳐라. 표가 없으면 두 정본이 조용히 어긋난다.
//
// ── 규칙 ────────────────────────────────────────────────────────────────
//    숫자 2~3  +  한글 1  +  숫자 4        (그리고 **그 뒤에 아무것도 없어야 한다**)
//    예) 238다5927 · 12가3456 · 123바9898
//
// 🔴 **한글 범위는 `가-힣` 전체다**(U+AC00..U+D7A3, 11,172자). 번호판 용도문자
//    (가·나·다·…·하·허·호·배)로 **좁히지 않았다.** 그 선택의 값과 대가:
//      ✅ 정상을 안 막는다 — 낡은 판·희귀 용도문자·지역판이 와도 통과한다
//      🔴 잡음이 샐 수 있다 — `123뷁9898` 은 **통과한다**(실제 번호판엔 없는 음절)
//    ★ 좁히면 그 반대가 된다: 잡음은 더 잡고 **정상을 조용히 막는다.**
//      §"너무 엄한 문이 조용히 정상을 막는다" — 막힌 차는 로그에 한 줄로만 남고
//      사람은 *"왜 안 열리지"* 만 본다.
//
// 🔑 **잡음 배제 용도에는 넓은 쪽으로 충분하다** — 실측(2026-08-26): HTTP 헤더 네 줄
//    (`GET / HTTP/1.1` · `Host: …` · `User-Agent: …` · `Accept: */*`)은 **한글이 아예 없어서**
//    전부 구조에서 죽는다. 용도문자로 좁혀도 **더 잡히는 것이 없다.**
//    ★ 좁히는 것은 **"OCR 이 그럴듯한 오독을 냈을 때"** 만 값이 있고, 그건 인식기 쪽 축이다.
//
// ⚠ 이 검사가 **안 하는 것**: 그 번호판이 실재하는지 · 등록됐는지 · 값이 맞는지.
//    형식만 본다. 형식이 맞는 오답(`123바9599`)은 **이걸로 못 잡는다** —
//    그건 인식 신뢰도(conf) 축이고 서로 다른 것을 잡는다(cpp 원장 §4.4).

#ifndef PLATE_FORMAT_H_
#define PLATE_FORMAT_H_

#include <string>
#include <vector>

namespace plate_format {

// UTF-8 을 **문자 단위**로 쪼갠다. 바이트 단위로 세면 한글이 3으로 세어져 자릿수가 전부 틀린다.
inline std::vector<std::string> utf8_split(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = 1;
        if      ((c & 0x80u) == 0x00u) n = 1;
        else if ((c & 0xE0u) == 0xC0u) n = 2;
        else if ((c & 0xF0u) == 0xE0u) n = 3;
        else if ((c & 0xF8u) == 0xF0u) n = 4;
        else                           n = 1;   // 깨진 선행 바이트 — 한 바이트로 끊어 넘긴다
        if (i + n > s.size()) n = 1;            // 잘린 꼬리 — 넘겨 읽지 않는다
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

inline bool is_digit(const std::string& ch) {
    return ch.size() == 1 && ch[0] >= '0' && ch[0] <= '9';
}

// 한글 완성형 음절 U+AC00..U+D7A3 (UTF-8 3바이트).
inline bool is_hangul(const std::string& ch) {
    if (ch.size() != 3) return false;
    const unsigned int a = static_cast<unsigned char>(ch[0]);
    const unsigned int b = static_cast<unsigned char>(ch[1]);
    const unsigned int c = static_cast<unsigned char>(ch[2]);
    const unsigned int cp = ((a & 0x0Fu) << 12) | ((b & 0x3Fu) << 6) | (c & 0x3Fu);
    return cp >= 0xAC00u && cp <= 0xD7A3u;
}

// 숫자 2~3 + 한글 1 + 숫자 4, 그리고 **끝**.
inline bool plate_format_ok(const std::string& utf8) {
    const std::vector<std::string> ch = utf8_split(utf8);
    size_t i = 0, lead = 0;
    while (i < ch.size() && is_digit(ch[i])) { ++lead; ++i; }
    if (lead < 2 || lead > 3)                 return false;
    if (i >= ch.size() || !is_hangul(ch[i]))  return false;
    ++i;
    size_t tail = 0;
    while (i < ch.size() && is_digit(ch[i])) { ++tail; ++i; }
    return tail == 4 && i == ch.size();       // 🔑 `i == ch.size()` — 꼬리가 남으면 거절
}

}  // namespace plate_format

#endif  // PLATE_FORMAT_H_
