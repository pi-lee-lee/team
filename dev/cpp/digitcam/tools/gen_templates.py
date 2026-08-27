#!/usr/bin/env python3
"""번호판 문자 템플릿 뱅크를 생성해 plate_templates.hpp 로 박는다.

왜 오프라인 생성인가
  런타임에 모델/템플릿 파일을 두지 않는다(계약 §9.5) — 임베디드로 옮길 때 파일 배포가
  따라붙으면 안 된다. 그래서 여기서 만든 비트맵을 소스에 컴파일해 넣는다.
  cv::putText(Hershey) 는 쓰지 않는다 — 획 글꼴이라 획이 채워진 번호판 서체와 상관도가 낮다.

무엇을 만드는가
  숫자 10 + 번호판용 한글 40 = 50 클래스를, 이 머신의 고딕 계열 폰트 여러 벌로 렌더링해
  24x32 셀로 정규화한 뒤 **1비트로 패킹**해서 배열로 뽑는다(클래스당 96바이트).
  런타임(digit_pipeline.cpp)이 풀어서 블러 → NCC 로 비교한다.

정규화 규칙 — 런타임과 **똑같아야** 한다:
  잉크 bbox 로 타이트 크롭 → 24x32 셀에 **꽉 채워** 늘림(종횡비 무시).

  종횡비를 유지해 맞추던 초기 방식은 실패했다: 번호판의 '가' 는 폰트의 '가' 보다 세로로 길어
  질의는 높이 기준, 템플릿은 폭 기준으로 맞춰졌고, 같은 글자인데 크기가 달라져 NCC 가 0.44 까지
  떨어졌다(실측). 채움 정규화로 바꾸면 그 어긋남이 사라진다.
  대신 '1' 처럼 좁은 글자의 '좁음' 정보가 사라지므로 **종횡비를 따로 저장해** 런타임에서
  형태 점수와 별개의 벌점으로 쓴다.

사용법:
  python3 cpp/digitcam/tools/gen_templates.py            # 기본 경로에 생성
  python3 cpp/digitcam/tools/gen_templates.py --out <경로>
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

CELL_W = 24
CELL_H = 32
RENDER_SIZE = 160  # 크게 그려서 줄인다 — 계단현상을 줄이려고

DIGITS = [str(d) for d in range(10)]

# 계약 §9.5 의 닫힌 집합. 이 밖의 한글은 번호판에 나오지 않는다.
HANGUL = [
    "가", "나", "다", "라", "마",
    "거", "너", "더", "러", "머", "버", "서", "어", "저",
    "고", "노", "도", "로", "모", "보", "소", "오", "조",
    "구", "누", "두", "루", "무", "부", "수", "우", "주",
    "바", "사", "아", "자",
    "배",
    "하", "허", "호",
]

# (표시이름, 파일, ttc 인덱스, stroke_width)
# 번호판 서체는 굵은 고딕이다. 굵기가 다른 벌을 섞어 서체 차이를 흡수한다.
# NotoSansGothic-Regular.ttf 는 이름과 달리 '고트 문자' 폰트라 한글이 없다(probe_fonts.py 로 확인).
# 넣으면 .notdef 네모가 템플릿으로 들어가므로 제외한다.
FONTS = [
    ("SDGothicNeo-Bold", "/System/Library/Fonts/AppleSDGothicNeo.ttc", 6, 0),
    ("SDGothicNeo-Bold-thick", "/System/Library/Fonts/AppleSDGothicNeo.ttc", 6, 5),
    ("SDGothicNeo-Regular", "/System/Library/Fonts/AppleSDGothicNeo.ttc", 0, 0),
    ("AppleGothic", "/System/Library/Fonts/Supplemental/AppleGothic.ttf", 0, 0),
    ("AppleGothic-thick", "/System/Library/Fonts/Supplemental/AppleGothic.ttf", 0, 5),
    ("ArialUnicode", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 0, 0),
    ("ArialUnicode-thick", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 0, 5),
]

# 숫자 전용 폰트. 한글 글리프는 없지만 숫자는 있다.
# 번호판 숫자는 획이 곧고 각진 계열이라 고딕 본문용 폰트만으로는 '6' 처럼 꼬리가 곧은 글자가
# 잘 안 맞는다(실측: 번호판 '6' ↔ 폰트 '6' NCC 0.65, 오답 '8' 이 0.57 로 마진이 얇았다).
# DIN 계열·Impact 처럼 각진 폰트를 숫자에만 더해 그 간극을 메운다.
DIGIT_FONTS = [
    ("DINAlternate-Bold", "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf", 0, 0),
    ("DINAlternate-Bold-thick", "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf", 0, 5),
    ("DINCondensed-Bold", "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf", 0, 0),
    ("Impact", "/System/Library/Fonts/Supplemental/Impact.ttf", 0, 0),
    ("TahomaBold", "/System/Library/Fonts/Supplemental/Tahoma Bold.ttf", 0, 0),
    ("VerdanaBold", "/System/Library/Fonts/Supplemental/Verdana Bold.ttf", 0, 0),
    ("ArialBlack", "/System/Library/Fonts/Supplemental/Arial Black.ttf", 0, 0),
]

DEFAULT_OUT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "..", "android", "app", "src", "main", "cpp", "plate_templates.hpp",
)


def render_cell(font, ch, stroke):
    """글자 하나를 24x32 정규화 셀(0/1)과 종횡비로 만든다. 실패하면 (None, 0)."""
    canvas = RENDER_SIZE * 3
    img = Image.new("L", (canvas, canvas), 0)
    draw = ImageDraw.Draw(img)
    draw.text((canvas // 2, canvas // 2), ch, font=font, fill=255, anchor="mm",
              stroke_width=stroke, stroke_fill=255)

    bbox = img.getbbox()
    if bbox is None:
        return None, 0
    glyph = img.crop(bbox)
    if glyph.width < 2 or glyph.height < 2:
        return None, 0

    aspect100 = int(round(glyph.width / glyph.height * 100))

    # 셀을 꽉 채워 늘린다 — 런타임 normalize_cell 과 같은 규칙이다.
    glyph = glyph.resize((CELL_W, CELL_H), Image.LANCZOS)

    # 1비트로 떨어뜨린다. 런타임은 여기서 거리장을 만들어 비교하므로 계조는 필요 없다.
    return [1 if p >= 128 else 0 for p in glyph.getdata()], aspect100


def pack_bits(bits):
    """24*32=768 비트를 96바이트로 묶는다(MSB first)."""
    out = []
    for i in range(0, len(bits), 8):
        byte = 0
        for j in range(8):
            if bits[i + j]:
                byte |= 1 << (7 - j)
        out.append(byte)
    return out


def fingerprint(bits):
    """같은 비트맵인지 보는 지문. .notdef 네모가 여러 클래스에 들어가는 사고를 잡는다."""
    return "".join(str(b) for b in bits)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.normpath(DEFAULT_OUT))
    args = ap.parse_args()

    classes = [(c, False) for c in DIGITS] + [(c, True) for c in HANGUL]

    entries = []   # (label, is_hangul, font_name, packed bytes)
    skipped = []

    all_fonts = [(f, False) for f in FONTS] + [(f, True) for f in DIGIT_FONTS]

    for (font_name, path, index, stroke), digits_only in all_fonts:
        try:
            font = ImageFont.truetype(path, RENDER_SIZE, index=index)
        except Exception as exc:
            skipped.append(f"{font_name}: 폰트를 열 수 없음 ({exc})")
            continue

        seen = {}
        bad = 0
        for ch, is_hangul in (classes if not digits_only else [(c, False) for c in DIGITS]):
            bits, aspect100 = render_cell(font, ch, stroke)
            if bits is None:
                bad += 1
                continue
            fp = fingerprint(bits)
            if fp in seen:
                # 서로 다른 글자가 같은 비트맵 → 글리프가 없어 .notdef 를 그린 것이다.
                bad += 1
                continue
            seen[fp] = ch
            entries.append((ch, is_hangul, font_name, pack_bits(bits), aspect100))
        if bad:
            skipped.append(f"{font_name}: {bad}개 클래스 제외(글리프 없음/중복 비트맵)")

    if not entries:
        print("템플릿을 하나도 만들지 못했다. 폰트 경로를 확인하라.", file=sys.stderr)
        return 1

    labels = sorted({e[0] for e in entries})
    missing = [c for c, _ in classes if c not in labels]

    lines = []
    lines.append("// plate_templates.hpp — 자동 생성 파일. 손으로 고치지 마라.")
    lines.append("//")
    lines.append("// 생성기: cpp/digitcam/tools/gen_templates.py")
    lines.append("// 내용: 번호판 문자 템플릿 뱅크(24x32 셀, 1비트 패킹).")
    lines.append("// 이 헤더는 데이터 전용이다 — 로직도, 안드로이드 의존도 없다.")
    lines.append("// 임베디드 이식 시 digit_pipeline.{hpp,cpp} 와 함께 이 파일도 들고 간다.")
    lines.append("")
    lines.append("#ifndef DIGITCAM_PLATE_TEMPLATES_HPP_")
    lines.append("#define DIGITCAM_PLATE_TEMPLATES_HPP_")
    lines.append("")
    lines.append("namespace digitcam {")
    lines.append("namespace templates {")
    lines.append("")
    lines.append(f"constexpr int kCellW = {CELL_W};")
    lines.append(f"constexpr int kCellH = {CELL_H};")
    lines.append(f"constexpr int kPackedBytes = {CELL_W * CELL_H // 8};")
    lines.append("")
    lines.append("struct Entry {")
    lines.append("  const char* label;        // UTF-8 문자 하나")
    lines.append("  bool hangul;              // true 면 한글 자리 후보")
    lines.append("  const char* font;         // 어느 폰트에서 나왔는지(디버그용)")
    lines.append("  int aspect100;            // 원래 글리프의 폭/높이 x100 (셀은 꽉 채워 늘렸다)")
    lines.append("  unsigned char bits[kPackedBytes];  // MSB first, 행 우선")
    lines.append("};")
    lines.append("")
    lines.append(f"constexpr int kEntryCount = {len(entries)};")
    lines.append("")
    lines.append("inline const Entry* entries() {")
    lines.append("  static const Entry kEntries[kEntryCount] = {")
    for label, is_hangul, font_name, packed, aspect100 in entries:
        body = ",".join(str(b) for b in packed)
        lines.append(f'      {{"{label}", {"true" if is_hangul else "false"}, '
                     f'"{font_name}", {aspect100}, {{{body}}}}},')
    lines.append("  };")
    lines.append("  return kEntries;")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace templates")
    lines.append("}  // namespace digitcam")
    lines.append("")
    lines.append("#endif  // DIGITCAM_PLATE_TEMPLATES_HPP_")

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"생성: {args.out}")
    print(f"  템플릿 {len(entries)}개 · 클래스 {len(labels)}/{len(classes)}")
    for s in skipped:
        print(f"  주의: {s}")
    if missing:
        print(f"  ⚠ 빠진 클래스: {' '.join(missing)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
