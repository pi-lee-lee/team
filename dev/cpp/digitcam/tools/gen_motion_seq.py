#!/usr/bin/env python3
"""움직임 게이트(계약 §9.1) 시험용 **연속 프레임 열**을 합성한다.

왜 필요한가
  samples/plates 는 전부 정지 이미지 한 장이라 프레임 간 차분이 정확히 0.000 이다.
  그래서 26/26 을 통과해도 `motion_threshold` 는 한 번도 시험되지 않는다.
  이 스크립트는 실기기에서 실제로 생기는 세 가지 상태를 프레임 열로 만든다.

세 무리 (REQ-0014)
  still  — 카메라를 고정한 상태. 센서 잡음(가우시안 σ)만 프레임마다 다르다.
  hand   — 사람이 폰을 들고 '멈춰 있는' 상태. 1~3px 평행이동 + 미세 회전 + 잡음.
           **이건 정지로 판정되어야 한다.** 손으로 든 이상 이 정도는 늘 생긴다.
  move   — 실제 이동. 프레임마다 8~20px 평행이동. 이건 움직임이어야 한다.

그레이 변환은 앱과 같은 정수 절삭식을 쓴다(make_testdata.py 와 동일). 재현성을 위해
난수 시드를 파일명에 매어 고정한다 — 같은 명령을 두 번 돌리면 같은 프레임이 나온다.
"""

import argparse
import math
import os
import random
import sys
import zlib

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SCENES = os.path.join(REPO, "samples", "plates", "scenes")
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "testdata", "motion"))

FRAMES = 12  # 한 열의 프레임 수. still_frames 후보(최대 8)보다 넉넉해야 한다.

# (이름, 최대 평행이동 px, 최대 회전 도, 잡음 σ)
CLASSES = [
    ("still", 0.0, 0.0, 1.5),
    ("hand1", 1.0, 0.10, 1.5),  # 약한 손떨림 — 경계가 어디인지 보려고 같이 만든다
    ("hand", 3.0, 0.25, 1.5),
    ("move", 20.0, 0.5, 1.5),
]

MOVE_MIN_PX = 8.0  # move 무리의 프레임당 최소 이동량


def to_gray_bytes(img):
    """앱과 같은 정수 절삭식. PIL convert("L") 은 반올림이라 값이 1 어긋난다."""
    rgb = img.convert("RGB")
    w, h = rgb.size
    src = rgb.tobytes()
    out = bytearray(w * h)
    for i in range(w * h):
        out[i] = (src[i * 3] * 299 + src[i * 3 + 1] * 587 + src[i * 3 + 2] * 114) // 1000
    return bytes(out), (w, h)


def add_noise(data, sigma, rnd):
    """센서 잡음. 프레임마다 독립이라 '정지' 에서도 차분이 0 이 되지 않는다 — 실기기와 같다."""
    if sigma <= 0:
        return data
    out = bytearray(data)
    for i in range(len(out)):
        v = out[i] + rnd.gauss(0.0, sigma)
        out[i] = 0 if v < 0 else (255 if v > 255 else int(v))
    return bytes(out)


def write_pgm(path, data, size):
    w, h = size
    with open(path, "wb") as f:
        f.write(f"P5\n{w} {h}\n255\n".encode("ascii"))
        f.write(data)


def make_sequence(base_img, kind, max_shift, max_rot, sigma, out_dir, seed):
    rnd = random.Random(seed)
    os.makedirs(out_dir, exist_ok=True)

    # 누적 이동. move 는 한 방향으로 계속 흐르고, hand 는 한 자리에서 떨린다.
    cx = cy = 0.0
    names = []

    for i in range(FRAMES):
        if kind == "still":
            dx = dy = 0.0
            rot = 0.0
        elif kind == "hand":
            # 원점 주변에서 떨린다(누적되지 않는다). 사람이 한 자리를 겨누는 상태.
            dx = rnd.uniform(-max_shift, max_shift)
            dy = rnd.uniform(-max_shift, max_shift)
            rot = rnd.uniform(-max_rot, max_rot)
        else:
            # 프레임마다 8~20px 씩 같은 방향으로 흐른다.
            step = rnd.uniform(MOVE_MIN_PX, max_shift)
            ang = rnd.uniform(-0.3, 0.3)  # 대체로 가로 이동
            cx += step * math.cos(ang)
            cy += step * math.sin(ang)
            dx, dy = cx, cy
            rot = rnd.uniform(-max_rot, max_rot)

        frame = base_img
        if rot != 0.0:
            frame = frame.rotate(rot, resample=Image.BICUBIC, fillcolor=(0, 0, 0))
        if dx != 0.0 or dy != 0.0:
            frame = frame.transform(frame.size, Image.AFFINE, (1, 0, -dx, 0, 1, -dy),
                                    resample=Image.BICUBIC, fillcolor=(0, 0, 0))

        gray, size = to_gray_bytes(frame)
        gray = add_noise(gray, sigma, rnd)

        name = f"{i:02d}.pgm"
        write_pgm(os.path.join(out_dir, name), gray, size)
        names.append(name)

    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--scenes", nargs="*", default=None,
                    help="쓸 장면 파일 이름(기본: scenes/ 전체)")
    args = ap.parse_args()

    scene_files = args.scenes or sorted(
        f for f in os.listdir(SCENES) if f.endswith(".png"))
    if not scene_files:
        print("장면 이미지를 찾지 못했다.", file=sys.stderr)
        return 1

    os.makedirs(args.out, exist_ok=True)
    rows = []

    for scene in scene_files:
        base = Image.open(os.path.join(SCENES, scene))
        stem = scene.replace(".png", "")
        for kind, shift, rot, sigma in CLASSES:
            seq = f"{stem}__{kind}"
            # 파이썬의 str hash() 는 프로세스마다 달라진다(PYTHONHASHSEED). crc32 로 고정한다.
            names = make_sequence(base, kind, shift, rot, sigma,
                                  os.path.join(args.out, seq), seed=zlib.crc32(seq.encode()))
            rows.append("\t".join([seq, kind, str(len(names))]))
            print(f"  {seq}: {len(names)}프레임")

    manifest = os.path.join(args.out, "sequences.tsv")
    with open(manifest, "w", encoding="utf-8") as f:
        f.write("# 열이름\t종류(still|hand|move)\t프레임수\n")
        f.write("\n".join(rows) + "\n")

    print(f"\n열 {len(rows)}개 → {args.out}")
    print(f"목록 → {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
