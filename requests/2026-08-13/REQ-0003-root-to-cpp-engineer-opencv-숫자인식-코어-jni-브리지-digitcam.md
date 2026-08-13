---
id: REQ-0003
title: OpenCV 숫자인식 코어 + JNI 브리지 (DigitCam)
from: root
to: cpp-engineer
status: done
created: 2026-08-13T20:56:18+0900
updated: 2026-08-13T22:06:38+0900
files: ["android/app/src/main/cpp/CMakeLists.txt", "android/app/src/main/cpp/native-lib.cpp", "android/app/src/main/cpp/digit_pipeline.hpp", "android/app/src/main/cpp/digit_pipeline.cpp"]
parent: none
---

# REQ-0003 · OpenCV 숫자인식 코어 + JNI 브리지 (DigitCam)

**요청자** `root` → **담당** `cpp-engineer`

## 요청 내용

먼저 `docs/digitcam-contract.md` 를 통째로 읽어라. 계약(§3 SDK 경로, §4 JNI 시그니처, §5 결과 JSON, §9 인식기, §10 빌드)은 그 문서가 원본이고 이 요청은 요약이다.

할 일:
1. **순수 OpenCV 코어** `digit_pipeline.{hpp,cpp}` — JNI/안드로이드 헤더에 의존 금지. 입력은 cv::Mat(CV_8UC1), 출력은 구조체. 나중에 이 두 파일만 임베디드로 들고 간다. 이게 이번 작업의 핵심 요구다.
   - 인쇄체 숫자 대상. 전처리 → 자릿수 분할 → 분류 → 좌→우 정렬 → 문자열.
   - `stable`(연속 프레임) 과 `fresh`(상승 엣지 + 재무장) 래치도 여기 둔다. 계약 §5 의 재무장 규칙 두 가지를 그대로 지켜라. Kotlin 은 전송 시점을 판단하지 않는다.
2. **JNI 브리지** `native-lib.cpp` — 계약 §4 의 심볼 5개. 기존 greetFromJNI 데모는 지운다. 패키지가 com.example.jnidemo → **com.example.digitcam** 으로 바뀌니 심볼명 주의.
3. **CMakeLists.txt** — 라이브러리 이름 `digitcam`. OpenCV 4.14.0 은 `~/opencv-android-sdk/OpenCV-android-sdk/sdk/native/jni` 에 풀어 뒀다(확인함). staticlibs 가 있으니 **정적 링크(core+imgproc)를 우선 시도**하고, 안 되면 공유로 가되 그 사실을 done 노트에 적어라. CMake 4.1.2 라 configure 에서 정책 에러가 나면 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 를 먼저 시도.

**착수 전에 인식 방법을 한 단락으로 답신하라** (이진화/분할/분류 각각 무엇을 쓸지, 템플릿이나 모델을 쓴다면 그 출처). 루트가 읽고 진행 여부를 판단한다. cv::putText Hershey 템플릿 방식은 권하지 않는다 — 획 글꼴이라 인쇄체와 상관도가 낮다.

android/app/build.gradle.kts 는 네 소유가 아니다. abiFilters·cmake 설정 변경이 필요하면 android-engineer 에게 요청을 발행하라.

## 왜 필요한가

이 앱을 만드는 진짜 목적이 '향후 임베디드 이식을 위해 OpenCV 영상처리가 실제로 도는지 확인'이다. 따라서 인식 로직이 Kotlin 에 새어나가면 작업 자체가 무의미해진다. 코어는 전부 C++ 에 있어야 한다.

## 대상 파일

- `android/app/src/main/cpp/CMakeLists.txt`
- `android/app/src/main/cpp/native-lib.cpp`
- `android/app/src/main/cpp/digit_pipeline.hpp`
- `android/app/src/main/cpp/digit_pipeline.cpp`
## 완료 기준

1) libdigitcam.so 가 두 ABI(arm64-v8a, x86_64)로 빌드된다. 2) nativeVersion() 이 4.14.0 계열 문자열을 반환한다(= OpenCV 실제 링크 증명). 3) nativeProcessGray 가 계약 §5 의 전 키를 채운 JSON 을 반환하고, 못 찾은 프레임에서도 키가 빠지지 않는다. 4) digit_pipeline.{hpp,cpp} 에 jni.h / android/*.h include 가 하나도 없다.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0003 --by cpp-engineer --note "<한 줄 요약>" -->

### 착수 전 답신 — 인식 방법 (2026-08-13, 루트 판단 대기)

**전처리**는 회전 보정(cv::rotate) → 가우시안 블러 → **적응 이진화**(`cv::adaptiveThreshold`,
GAUSSIAN_C, blockSize 는 프레임 높이에 비례한 홀수)로 간다. 조명 얼룩에 강해야 해서 전역 Otsu
대신 적응형을 쓰되, **극성(글자가 어두운가 밝은가)은 Otsu 임계로 한 번 추정**해서
`THRESH_BINARY_INV` 여부를 정하고 후보가 0개면 반대 극성으로 한 번만 재시도한다.
**분할**은 `connectedComponentsWithStats` + 기하 필터(높이비 ≥ `minDigitHeightRatio`,
종횡비·채움비 범위)로 후보를 거른 뒤 **베이스라인 클러스터링**(y중심·높이가 비슷한 것끼리 묶고
가장 큰 묶음을 채택)으로 한 줄을 뽑고 좌→우 정렬한다. 각 자릿수는 라벨 마스크로 잘라
이웃 획이 섞이지 않게 한다. **분류는 실제 인쇄체 폰트에서 래스터화한 다중 폰트 템플릿 뱅크**를
쓴다 — macOS 시스템 폰트(Helvetica, Arial, Times New Roman, Courier New, Menlo 등 산세리프
+세리프+고정폭)를 **Python/PIL 스크립트로 오프라인 렌더링**해 24×32 정규화 비트맵으로 만들고
`digit_templates.hpp` 로 생성해 소스에 박는다(생성 스크립트는 내 소유 트리인 `cpp/digitcam/tools/`
에 남겨 재생성 가능하게 둔다). 런타임 의존성·모델 파일·dnn 은 없고 **core+imgproc 만** 링크한다.
매칭은 후보/템플릿 양쪽을 살짝 블러한 뒤 **정규화 상호상관(NCC)**, 신뢰도는 1등 점수와
**다른 숫자 클래스 2등과의 마진**을 함께 반영하고, 구멍 개수(Euler)는 거부용 veto 가 아니라
**약한 가산점**으로만 쓴다(폰트에 따라 4의 위가 닫히는 경우가 있어 veto 로 쓰면 위험하다).
`conf` 는 계약대로 자릿수별 최소값이고 `minConfidence` 미만이면 판독 전체를 버려
`value=""`, `digits=0`, `boxes=[]` 로 돌려준다(= 약한 고리 하나가 전체를 막는다).
계약 §9 대로 Hershey(cv::putText) 템플릿은 쓰지 않는다.

**대기 중 병행할 것(인식기와 무관):** OpenCV 정적 링크(core+imgproc) configure/link 스파이크를
`cpp/digitcam/spike/` 에서 두 ABI 로 돌려 본다. 산출물 파일(`android/app/src/main/cpp/**`)은
루트 승인 전까지 건드리지 않는다.

---

## 구현 완료 (계약 개정 4 기준) — 샘플 채점 **26 / 26**

`cpp/digitcam/verify/plate_test` 를 emulator-5554(arm64)에서 돌린 실측 점수다.
`samples/plates/expected.json` 의 `rule`(exact 18 / exact_or_empty 4 / empty 4)을 그대로 적용했다.

```
✓ kor8-hwamul-band        800가4568  conf=0.87   ✓ scenes/scene-01-front-45   123가4568 conf=0.82
✓ kor8-hwamul-plain       800가4568  conf=0.84   ✓ scenes/scene-02-tilt-35    700가4568 conf=0.81
✓ kor8-reflective-hires   123가4568  conf=0.82   ✓ scenes/scene-03-persp-60   800가4568 conf=0.76
✓ kor8-seunghap-band      700가4568  conf=0.87   ✓ scenes/scene-04-far-28     980가4568 conf=0.96
✓ kor8-seunghap-plain     700가4568  conf=0.86   ✓ scenes/scene-05-dark-50    123가4568 conf=0.83
✓ kor8-teuksu-band        980가4568  conf=0.87   ✓ scenes/scene-06-motion-50  (거절) segment_fail
✓ kor8-teuksu-plain       980가4568  conf=0.84   ✓ scenes/scene-07-negative   (거절) segment_fail
✓ variants -dark ×2, -small ×2, -tilt-p12 ×2, -tilt-m12 ×2 · -motion15 ×2(비움) · -motion45 ×2(거절)

점수: 26 / 26
```

`-motion15` 2장은 `exact_or_empty` 규칙이라 통과지만 **값을 읽지는 못했다**(segment_fail 로 비움).
틀린 값을 낸 샘플은 한 장도 없다.

### §9.1~§9.6 각 단계에서 무엇을 골랐나

**§9.1 움직임 게이트** — 160×120 축소 후 `absdiff` 평균 = `motion`. 임계 미만이 `still_frames`
회 **연속**이면 정지(한 프레임이라도 넘으면 카운트는 감소가 아니라 0으로 끊는다).
첫 프레임은 비교 대상이 없어 `moving:true`. 움직이는 프레임은 뒤 단계를 아예 돌리지 않는다.

**§9.2 번호판 검출** — 후보를 **두 원천에서** 모아 점수순으로 최대 4개를 시도한다.
(A) Otsu 밝은 연결성분의 `minAreaRect`, (B) 모폴로지 그래디언트 + 가로 17×3 close 의 윤곽.
A만 쓰면 판면이 밝은 배경과 한 성분으로 붙는 순간 후보가 0이 된다(scene-02 에서 실제로 그랬다).
점수 = 종횡비(4.7/2.2 와의 로그거리) × 면적벌점(프레임의 30% 초과 시 0.3) × 크기점수.
**가장 큰 후보를 고르지 않는 것이 핵심**이다 — 배경의 큰 사각형(639×280, 종횡비 2.28)이
진짜 번호판(253×46)보다 커서 그쪽이 뽑히던 문제가 있었다. 채택은 점수가 아니라
**문자 분할까지 통과하는가**로 결정한다. 후보를 못 찾고 프레임 자체가 번호판 비율(±20%)이면
프레임 전체를 판으로 보는 폴백(개정 4)도 넣었다. 투시보정은 `getPerspectiveTransform` +
`warpPerspective` 로 520×110(신형)/335×155(구형).

**§9.3 KOR 띠** — 계약의 판정식을 **두 군데 바꿨다. 둘 다 실측에서 걸린 문제다.**
① 밝음 기준 140 고정은 저조도 샘플에서 흰 바탕까지 140 밑으로 내려가 두 비율이 모두 0이 된다
→ 그 번호판의 **Otsu 임계**를 밝음 기준으로 쓴다.
② "띠가 있으면 왼쪽 12% 절단"은 위험하다. 검출이 이미 띠를 뺀 흰 판면만 잡아오는 경우가 많고,
그때 비가 0.79 처럼 애매하게 나오면 **첫 글자가 통째로 날아간다**(scene-01 에서 `1` 이 잘렸다).
그래서 왼쪽부터 **어두운 열이 연속되는 구간만** 자른다 — 글자 앞에는 밝은 여백이 있어 첫 글자에서
멈춘다. 띠 전체면 띠 끝까지, 띠 조각만 걸렸으면 그만큼만. 띠 유무 두 종류 모두 통과한다.
(계약식 비율도 디버그 로그에 함께 찍어 수치표와 대조할 수 있게 해 뒀다)

**§9.4 문자 분할** — 세로 투영 프로파일. 병합은 `pitch = 판폭/기대자릿수` 기준으로
`틈 ≤ 0.35·pitch` 이고 `합친 폭 ≤ 0.95·pitch` 인 쌍만. ⚠ **병합 가능한 쌍 중에서** 최소 틈을
고르는 게 중요하다 — 전체 최소 틈을 고른 뒤 조건을 못 맞추면 멈추게 짰더니, 숫자 사이의 좁은 틈
때문에 정작 `ㄱ`+`ㅏ` 병합이 시도조차 안 되어 9조각이 나왔다(scene-04). 이 한 줄이 24/26 → 26/26.
개정 4의 안전장치도 넣었다: 테두리 닿은 성분 제거 후 남은 수가 기대 자릿수보다 적으면 제거를 되돌린다.
구조가 3+한글+4(또는 2+한글+4)와 안 맞으면 `segment_fail` — 억지로 맞추지 않는다.

**§9.5 분류** — 승인받은 대로 오프라인 래스터화 템플릿 뱅크 + NCC. 다만 두 가지를 실측으로 바꿨다.
① **비트맵이 아니라 부호거리장(SDF)으로 비교한다.** 번호판 서체가 이 머신에 없어 획 두께·곡률이
달라, 이진 비트맵 상관은 맞는 글자도 0.56까지 떨어지고 오답 '8'이 0.55로 붙었다. 거리장은 두께 차이를
흡수하면서 형태는 남긴다 → 같은 글자가 0.92~0.97로 올라갔다.
② **정규화를 채움(stretch)으로 바꾸고 종횡비는 별도 벌점으로 뺐다.** 종횡비 유지 방식에서는
번호판 `가`(세로로 김)는 높이 기준, 폰트 `가`는 폭 기준으로 맞춰져 같은 글자가 다른 크기로
비교되고 있었다(NCC 0.44). 채움으로 바꾸니 0.69로, 폰트를 늘린 뒤엔 더 올라갔다.
뱅크는 **420개**(50클래스): 한글 포함 폰트 7벌(Apple SD Gothic Neo Bold/Regular, AppleGothic,
Arial Unicode + 굵기 변형) + **숫자 전용 각진 폰트 7벌**(DIN Alternate/Condensed Bold, Impact,
Tahoma/Verdana/Arial Black). DIN 계열을 넣은 게 `6`/`8` 마진을 벌렸다.
계약 §9.5가 지목한 `NotoSansGothic-Regular.ttf` 는 **한글이 없다** — 이름과 달리 고트 문자(고대 문자)
폰트라 모든 글자가 같은 .notdef 네모로 그려진다(`tools/probe_fonts.py` 로 확인). 빼지 않으면 빈
템플릿이 뱅크에 들어가 조용히 망가진다. 제외했다.
자리 제한(숫자 자리=숫자끼리, 한글 자리=한글끼리)은 계약대로 적용했다.
`conf` = 문자별 최소값, 눈금은 절대점수(0.40→0, 0.75→1) 45% + **2등과의 마진**(0→0, 0.12→1) 55%.
마진에 무게를 더 준 이유는 틀릴 위험이 마진이 얇을 때 오기 때문이다 — 위 '6/8' 이 0.006 차로 붙은
경우가 이 눈금에서 0.23, 제대로 갈린 경우가 0.75~0.96 이다.

**§9.6 데스크톱 하네스** — `cpp/digitcam/` 에 뒀다.
- `tools/gen_templates.py` 템플릿 생성 · `tools/probe_fonts.py` 폰트 한글 지원 확인
- `tools/make_testdata.py` samples/plates → PGM + manifest(정답표 그대로 사용, 고치지 않음)
- `verify/plate_test.cpp` 26장 채점 · `verify/core_test.cpp` 계약 단위 검증 37항목
호스트에 OpenCV 가 없어 **하네스를 arm64로 빌드해 emulator-5554 에서 돌린다.**
imgcodecs 를 안 쓰려고 PGM 을 쓴다 — 앱과 같은 core+imgproc 조건을 유지하기 위해서다.

### 산출물

| 파일 | 내용 |
|---|---|
| `android/app/src/main/cpp/digit_pipeline.{hpp,cpp}` | 순수 OpenCV 코어(게이트·검출·보정·분할·분류·래치·JSON) |
| `android/app/src/main/cpp/plate_templates.hpp` | 생성된 템플릿 뱅크 420개(24×32, 1비트 패킹, 데이터 전용) |
| `android/app/src/main/cpp/native-lib.cpp` | JNI 브리지 5심볼 |
| `android/app/src/main/cpp/CMakeLists.txt` | 타겟 `digitcam`, OpenCV core+imgproc 정적 링크 |

⚠ **임베디드로 들고 갈 파일이 2개가 아니라 3개다** — `digit_pipeline.{hpp,cpp}` + `plate_templates.hpp`.
템플릿을 런타임 파일로 두지 않으려면(계약 §9.5) 소스에 박는 수밖에 없다. 세 파일 모두 jni.h /
android/*.h 를 include 하지 않는다(`#include` 목록으로 확인, 주석 언급만 있음).

### 검증한 것 / 안 한 것

**검증했다(전부 실행함):**
- 두 ABI 빌드 통과, `-Wall -Wextra` 경고 0 (arm64-v8a / x86_64)
- 내보낸 심볼 5개가 계약 §4 표와 일치 (llvm-nm)
- `libopencv_java4.so` 의존 없음 = 정적 링크 (llvm-readelf -d) → REQ-0006 에서 android-engineer 에 통지
- `nativeVersion()` 은 매크로가 아니라 `cv::getVersionString()` → 기기에서 `4.14.0` 확인
- 계약 단위 검증 37/37 (움직임 게이트 궤적, 래치 재무장 2규칙, 설정 파싱, 전 키 JSON,
  reason 유효값, 한글 UTF-8 바이트열 EA B0 80 보존)
- 샘플 채점 26/26

**안 했다:**
- `./gradlew :app:assembleDebug` — 통합 빌드는 android-engineer 책임(계약 §10). 내 쪽은 CMake 단독 빌드까지만.
- 실기기 카메라. 프레임당 처리시간은 에뮬레이터(arm64를 x86에서 에뮬레이트) 기준 2~4ms 수준으로
  찍혔지만, 실기기 값은 재 본 적 없다.
- 샘플에 없는 조건: 구형 단축(2+한글+4) 실제 이미지, 야간 반사, 오토바이판.

### 처리 완료 · cpp-engineer · 2026-08-13T22:06:38+0900

번호판 인식 코어+JNI 완료 · samples 26/26 (emulator arm64 실측) · 두 ABI 빌드 통과 경고0 · 계약 단위검증 37/37

