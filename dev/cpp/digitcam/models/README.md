# 표준 경로 가중치 — 출처·라이선스·재현 방법

`plate_ocr.cpp`(검출망 + CTC 인식망)가 쓰는 파일 셋이다. **이 디렉터리가 유일한 원본**이고,
앱 `assets/` 로는 Gradle 이 복사한다 — 저장소에 같은 바이트를 두 벌 두지 않는다.

| 파일 | 크기 | 무엇인가 |
|---|---|---|
| `ppocr_det_v4.onnx` | 4,745,517 B (4.53 MiB) | PP-OCRv4 검출망(DB). 글자 영역을 찾는다. 언어 무관 |
| `korean_rec_static.onnx` | 3,216,845 B (3.07 MiB) | PP-OCR 한국어 인식망(CRNN + CTC). **분할 없이** 한 줄을 통째로 읽는다 |
| `korean_dict.txt` | 14,480 B | 글자 사전 3,688자. CTC 출력 인덱스와 **순서가 정확히 맞아야 한다** |
| 합계 | **7,976,842 B (7.61 MiB)** | ABI 와 무관한 자료 → assets 에 **한 벌만** |

## 라이선스 — Apache-2.0

```
모델 원본  PaddlePaddle/PaddleOCR              Apache-2.0
ONNX 변환  huggingface.co/SWHL/RapidOCR        Apache-2.0 (저장소 선언)
사전       PaddleOCR ppocr/utils/dict/korean_dict.txt   같은 저장소
```
⚠ **고지 의무가 있다.** 앱에 라이선스 고지를 넣어야 한다(android 몫).

## 어디서 받았나

```
det   https://huggingface.co/SWHL/RapidOCR/resolve/main/PP-OCRv4/ch_PP-OCRv4_det_infer.onnx
rec   https://huggingface.co/SWHL/RapidOCR/resolve/main/PP-OCRv1/korean_mobile_v2.0_rec_infer.onnx
사전  https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/release/2.7/ppocr/utils/dict/korean_dict.txt
```

## 🔴 `korean_rec_static.onnx` 는 받은 그대로가 아니다 — 입력 shape 를 고정했다

받은 rec 모델은 입력이 `[-1, 3, ?, ?]`(동적)이고, **그 상태로는 OpenCV dnn 이 못 읽는다**:

```
cv2.error: Node [Concat@ai.onnx]:(onnx_node!Concat_0) parse error
  (expected: 'total(targetShape) == total(shape(inputs[i]))')
```

입력을 `[1,3,32,320]` 으로 **고정**하고 접으면 적재·추론이 된다. 재현:

```bash
pip install onnx onnxsim
python3 - <<'EOF'
import onnx, onnxsim
m = onnx.load('korean_mobile_v2.0_rec_infer.onnx')
name = m.graph.input[0].name                     # 'x'
sm, ok = onnxsim.simplify(m, overwrite_input_shapes={name: [1, 3, 32, 320]})
assert ok
onnx.save(sm, 'korean_rec_static.onnx')
EOF
```

확인(출력이 `(1, 80, 3689)` = 80시점 x 3689클래스면 맞다):

```bash
python3 -c "
import cv2, numpy as np
n = cv2.dnn.readNetFromONNX('korean_rec_static.onnx')
n.setInput(np.zeros((1,3,32,320), np.float32)); print(n.forward().shape)"
```

🔑 **이 한 걸음이 APK 비용을 가른다.** 이게 없으면 ONNX Runtime AAR 이 필요하고
`libonnxruntime.so` 가 ABI 마다 실린다(arm64 +17.47 MiB · x86_64 +20.96 MiB).
지금은 OpenCV dnn 정적 링크만으로 되고 그 비용은 `libdigitcam.so` 증가분뿐이다
(실측: arm64 8.41 → 13.87 MiB · x86_64 40.15 → 47.00 MiB, 둘 다 stripped).

## 검증 — 값이 이렇게 나와야 한다

```bash
cmake --build cpp/digitcam/verify/build-host --target ocr_test
./cpp/digitcam/verify/build-host/ocr_test cpp/digitcam/models samples/plates/new-appdump \
    --expect 123바9898 --limit 960 --conf 0.85
# → 전체일치 11 · 틀림 0 · 값없음 4 / 15
```

⚠ 사전의 **순서가 곧 클래스 인덱스**다. 줄 하나가 밀리면 모든 글자가 다른 글자로 바뀐다.
파일을 편집하지 마라 — 바꿔야 하면 모델과 같이 바꿔라.
