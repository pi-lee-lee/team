#!/usr/bin/env python3
"""이 머신에서 한글을 실제로 그릴 수 있는 폰트가 무엇인지 확인한다.

계약 §9.5 의 폰트 목록에는 NotoSansGothic-Regular.ttf 가 들어 있는데, 이건 '고딕체'가 아니라
고트 문자(Gothic script, 고대 문자)용 폰트다 — 한글이 없다. 이런 걸 모르고 뱅크에 넣으면
빈 비트맵이 템플릿으로 들어가 분류가 조용히 망가진다. 그래서 렌더링 결과를 직접 확인한다.
"""

import sys

from PIL import Image, ImageDraw, ImageFont

CANDIDATES = [
    ("/System/Library/Fonts/AppleSDGothicNeo.ttc", list(range(0, 9))),
    ("/System/Library/Fonts/Supplemental/AppleGothic.ttf", [0]),
    ("/System/Library/Fonts/Supplemental/Arial Unicode.ttf", [0]),
    ("/System/Library/Fonts/Supplemental/NotoSansGothic-Regular.ttf", [0]),
]

PROBE = ["0", "8", "가", "허", "배"]


def ink_ratio(font, ch):
    """글자를 그린 뒤 실제로 잉크가 찍힌 픽셀 비율. 0 이면 글리프가 없다는 뜻."""
    img = Image.new("L", (128, 128), 0)
    ImageDraw.Draw(img).text((64, 64), ch, font=font, fill=255, anchor="mm")
    bbox = img.getbbox()
    if bbox is None:
        return 0.0
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    return (w * h) / (128.0 * 128.0)


def main():
    for path, indices in CANDIDATES:
        for index in indices:
            try:
                font = ImageFont.truetype(path, 72, index=index)
            except Exception as exc:  # 인덱스가 없으면 여기서 끝난다
                if index == 0:
                    print(f"{path}[{index}] 열기 실패: {exc}")
                break
            name = font.getname()
            ratios = [ink_ratio(font, ch) for ch in PROBE]
            ok = all(r > 0.001 for r in ratios)
            mark = "한글OK " if ok else "한글없음"
            print(f"{mark} {path}[{index}] {name} " +
                  " ".join(f"{ch}:{r:.3f}" for ch, r in zip(PROBE, ratios)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
