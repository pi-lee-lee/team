#!/usr/bin/env python3
"""생성된 템플릿 셀을 아스키로 찍어 본다. 하네스의 질의 셀 덤프와 나란히 놓고 비교하는 용도.

  python3 cpp/digitcam/tools/show_template.py 가 6
"""

import re
import sys

HPP = "/Users/idong-u/learn/android/app/src/main/cpp/plate_templates.hpp"
CELL_W = 24
CELL_H = 32

ENTRY = re.compile(r'\{"(.+?)", (true|false), "(.+?)", \{([0-9,]+)\}\}')


def main(argv):
    wanted = argv[1:] or ["가"]
    with open(HPP, encoding="utf-8") as f:
        text = f.read()

    for m in ENTRY.finditer(text):
        label, _hangul, font, body = m.groups()
        if label not in wanted:
            continue
        data = [int(x) for x in body.split(",")]
        bits = []
        for byte in data:
            for j in range(8):
                bits.append((byte >> (7 - j)) & 1)
        print(f"=== {label}  {font}")
        for y in range(CELL_H):
            row = bits[y * CELL_W:(y + 1) * CELL_W]
            print("    " + "".join("#" if b else "." for b in row))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
