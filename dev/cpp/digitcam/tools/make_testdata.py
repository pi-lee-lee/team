#!/usr/bin/env python3
"""samples/plates/**.png + expected.json → 하네스가 읽을 PGM + manifest.tsv 로 변환한다.

왜 PGM 인가
  테스트 실행 파일이 imgcodecs(libpng/zlib)를 끌어오지 않게 하려는 것이다. 앱이 링크하는 것은
  core+imgproc 뿐이고, 하네스도 같은 조건이어야 "앱에서 되는데 하네스에선 안 된다"가 안 생긴다.
  P5(이진 PGM)는 15줄짜리 파서로 읽힌다.

정답표(expected.json)는 건드리지 않는다 — 틀렸다고 판단되면 고치지 말고 루트에 알린다(samples/README.md).
"""

import argparse
import json
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SAMPLES = os.path.join(REPO, "samples", "plates")
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "testdata"))


def to_gray_like_app(img):
    """앱과 **같은 식**으로 그레이를 만든다.

    PIL 의 convert("L") 은 반올림하는 ITU-R 601-2 식이라, 앱의
    `(r*299 + g*587 + b*114) / 1000` 정수 절삭과 화소값이 최대 1 어긋난다.
    실측에서 80~87% 화소가 1씩 달랐다. ±1 은 적응 이진화의 경계 판정을 뒤집을 수 있으므로
    "하네스와 앱이 같은 입력을 본다" 는 전제를 지키려면 식을 맞춰야 한다.
    """
    rgb = img.convert("RGB")
    w, h = rgb.size
    src = rgb.tobytes()
    out = bytearray(w * h)
    for i in range(w * h):
        r = src[i * 3]
        g = src[i * 3 + 1]
        b = src[i * 3 + 2]
        out[i] = (r * 299 + g * 587 + b * 114) // 1000
    return bytes(out), (w, h)


def write_pgm(path, data, size):
    w, h = size
    with open(path, "wb") as f:
        f.write(f"P5\n{w} {h}\n255\n".encode("ascii"))
        f.write(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    with open(os.path.join(SAMPLES, "expected.json"), encoding="utf-8") as f:
        expected = json.load(f)["plates"]

    pgm_dir = os.path.join(args.out, "pgm")
    os.makedirs(pgm_dir, exist_ok=True)

    rows = []
    for rel, meta in sorted(expected.items()):
        src = os.path.join(SAMPLES, rel)
        if not os.path.exists(src):
            print(f"⚠ 없는 파일: {rel}", file=sys.stderr)
            continue
        img = Image.open(src)
        gray, size = to_gray_like_app(img)
        flat = rel.replace("/", "__").replace(".png", ".pgm")
        write_pgm(os.path.join(pgm_dir, flat), gray, size)

        # 채점 규칙은 expected.json 의 rule 이 원본이다(개정 4).
        #   exact          — 값이 정확히 맞아야 한다
        #   exact_or_empty — 맞히거나 비우거나. **틀린 값이면 실패**
        #   empty          — 반드시 비어야 한다(모션블러·번호판 없음)
        rows.append("\t".join([
            flat,
            meta.get("value", ""),
            "1" if meta.get("band") else "0",
            meta.get("format", "new"),
            meta.get("rule", "exact"),
            f"{size[0]}x{size[1]}",
        ]))

    manifest = os.path.join(args.out, "manifest.tsv")
    with open(manifest, "w", encoding="utf-8") as f:
        f.write("# 파일\t기대값\t띠\t형식\t채점규칙\t원본크기\n")
        f.write("\n".join(rows) + "\n")

    print(f"PGM {len(rows)}장 → {pgm_dir}")
    print(f"목록 → {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
