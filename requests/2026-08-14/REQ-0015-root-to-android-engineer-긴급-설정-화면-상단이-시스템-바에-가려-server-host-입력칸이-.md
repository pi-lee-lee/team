---
id: REQ-0015
title: 긴급: 설정 화면 상단이 시스템 바에 가려 server.host 입력칸이 안 보인다
from: root
to: android-engineer
status: done
created: 2026-08-14T00:28:16+0900
updated: 2026-08-14T00:39:46+0900
files: ["android/app/src/main/java/com/example/digitcam/SettingsActivity.kt", "android/app/src/main/java/com/example/digitcam/MainActivity.kt", "android/app/src/main/res/layout/activity_settings.xml", "android/app/src/main/res/layout/activity_main.xml", "android/app/src/main/AndroidManifest.xml"]
parent: REQ-0013
---

# REQ-0015 · 긴급: 설정 화면 상단이 시스템 바에 가려 server.host 입력칸이 안 보인다

**요청자** `root` → **담당** `android-engineer`

## 요청 내용

**사용자가 직접 겪은 문제다. 설정 화면 상단이 가려서 `server.host` 입력칸에 손이 닿지 않는다.** 이 화면을 만든 목적 자체가 서버 IP 를 폰에서 넣는 것이었으니, 지금 상태로는 REQ-0013 이 해결한 게 없다.

### 루트가 확인한 원인

**윈도우 인셋 처리가 앱 전체에 하나도 없다.** grep 으로 확인했다 — `ViewCompat.setOnApplyWindowInsetsListener`, `fitsSystemWindows`, `WindowInsetsCompat`, `setDecorFitsSystemWindows` 어느 것도 Kotlin 에도 레이아웃에도 없다.

그런데 `targetSdk = 35` 다. **Android 15 는 targetSdk 35 앱에 edge-to-edge 를 강제한다.** 시스템이 더 이상 알아서 인셋을 넣어 주지 않으므로, 앱이 직접 처리하지 않으면 컨텐츠가 상태바·내비게이션바 밑으로 들어간다. 지금이 정확히 그 상태다.

방증: 스크린샷에서 상태바 시계('12:26')가 **밝은 배경 위에 밝은 회색으로 찍혀 거의 안 보인다.** 앱이 자기 밝은 배경을 상태바 영역까지 그려 놓고 상태바 아이콘 색을 조정하지 않았다는 뜻이다.

### 고칠 것

1. **`SettingsActivity` 를 먼저 고쳐라** — 사용자가 지금 막혀 있는 화면이다. 루트 뷰에 systemBars 인셋을 패딩으로 적용해라. `androidx.core` 의 `ViewCompat.setOnApplyWindowInsetsListener` 로 `WindowInsetsCompat.Type.systemBars()` 를 받아 패딩으로 주는 방식을 권한다. 레이아웃 `fitsSystemWindows` 만으로 되는지 여부는 테마·API 조합에 따라 다르니 **실제로 화면을 보고 확인해라.**
2. **`MainActivity` 도 같은 문제일 가능성이 높다.** 이전 스크린샷에서 번호판 값이 화면 최상단에 붙어 있었다. 같이 봐라.
3. **IME(키보드)가 뜰 때 입력칸이 가리지 않는지도 확인해라.** 지금 화면에 키보드 툴바가 떠 있는 상태였다. `Type.ime()` 를 같이 처리하거나 `adjustResize` 를 확인해라.
4. **하단 버튼 줄(저장/기본값/취소)도 확인해라.** 스크린샷에서 제스처 내비게이션 바와 겹쳐 보인다.

### 검증 방법 — 이번엔 눈으로 봐야 한다

빌드 통과로는 아무것도 증명되지 않는 종류의 버그다.

- 설정 화면을 열고 **스크롤을 최상단으로 올린 상태**의 스크린샷을 뜬다
- `서버 주소 (server.host)` 라벨과 입력칸이 **온전히 보이고 탭이 되는지** 확인한다
- 실제로 IP 를 한 번 입력해서 저장까지 되는지 확인한다
- 그 스크린샷을 처리 결과에 첨부해라

기기는 동결 해제 상태다. 사용자가 같은 화면을 볼 수 있으니 검증 후에는 앱을 카메라 모드로 두고 손을 떼라.

## 왜 필요한가

설정 화면을 만든 유일한 이유가 폰에서 서버 IP 를 넣기 위해서인데, 그 입력칸이 안 보이면 화면이 없는 것과 같다. 게다가 targetSdk 35 에서 인셋을 처리하지 않은 것은 이 화면만의 문제가 아니라 앱 전체의 문제라, 실기기에 나가면 기종마다 다르게 깨진다.

## 대상 파일

- `android/app/src/main/java/com/example/digitcam/SettingsActivity.kt`
- `android/app/src/main/java/com/example/digitcam/MainActivity.kt`
- `android/app/src/main/res/layout/activity_settings.xml`
- `android/app/src/main/res/layout/activity_main.xml`
- `android/app/src/main/AndroidManifest.xml`
## 완료 기준

1) 설정 화면 최상단에서 server.host 라벨과 입력칸이 온전히 보이고 입력된다 — 스크린샷 첨부. 2) 하단 저장/취소 버튼이 내비게이션 바에 가리지 않는다. 3) 키보드가 떠도 편집 중인 칸이 가려지지 않는다. 4) MainActivity 도 상단이 상태바에 가리지 않는다. 5) 실제로 IP 를 입력해 저장하고 파일에 반영된 것까지 확인한다.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0015 --by android-engineer --note "<한 줄 요약>" -->

### 원인은 루트 진단이 맞다 — 인셋 처리가 앱 전체에 하나도 없었다

`targetSdk = 35` 라 Android 15 가 edge-to-edge 를 강제하는데, 나는 그에 맞는 인셋 처리를 어디에도
넣지 않았다. 그래서 앱이 상태바·제스처바 영역까지 자기 배경을 그렸고, 설정 화면 맨 위의
`server.host` 입력칸이 상태바 밑에 깔렸다. **그 칸에 손이 닿게 하는 것이 이 화면의 존재 이유였으므로
REQ-0013 은 사실상 아무것도 해결하지 못한 상태였다.** 루트가 지적한 상태바 시계가 안 보이던 것도
같은 원인의 다른 얼굴이다(밝은 배경 위에 밝은 아이콘).

내가 이걸 놓친 경위도 적어 둔다 — REQ-0013 을 "빌드 통과 + 파일이 올바르게 저장됨" 으로 검증했다.
그 둘은 사실이었지만 **화면을 눈으로 보지 않았다.** 로직이 맞아도 손이 닿지 않으면 기능은 없는 것이다.

### 고친 것

**1. `SettingsActivity` — 루트 뷰에 시스템 바 + 키보드 인셋을 여백으로 준다**

```kotlin
ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
    val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
    val ime  = insets.getInsets(WindowInsetsCompat.Type.ime())
    view.setPadding(bars.left, bars.top, bars.right, maxOf(bars.bottom, ime.bottom))
    insets
}
```

아래쪽을 `systemBars` 와 `ime` 중 **큰 쪽**으로 잡은 것이 요점이다. 루트 뷰가 세로 LinearLayout
(ScrollView weight=1 + 버튼줄 wrap_content)이라, 키보드가 올라오면 그 여백이 버튼줄을 키보드 위로
밀어 올리고 ScrollView 가 줄어든다 → 편집 중인 칸이 가려지지 않는다. Manifest 에
`windowSoftInputMode="adjustResize"` 도 같이 넣었다.

상태바 아이콘도 고쳤다 — 이 화면은 밝은 테마이므로 `isAppearanceLightStatusBars = true`.
스크린샷에서 시계가 검은 글씨로 또렷하다.

**2. `MainActivity` — 상·하단 패널에만 여백을 준다**

`topPanel` 은 위쪽 인셋만, `bottomPanel` 은 아래쪽 인셋만 원래 padding 에 **더한다**(덮어쓰지 않는다).

**프리뷰와 오버레이에는 일부러 여백을 주지 않았다.** 그 둘은 화면을 꽉 채워야 하고, 무엇보다
`OverlayView` 의 `quad` 좌표 변환이 프리뷰와 같은 크기를 전제한다 — 여기에 여백을 주면 번호판
사각형이 그만큼 어긋난다. 가려지면 안 되는 것은 글자와 버튼이지 영상이 아니다.

### 검증 (에뮬레이터 실측 · 스크린샷으로 확인)

요청대로 빌드 통과가 아니라 **화면과 파일로** 확인했다.

| 확인 항목 | 결과 |
|---|---|
| 설정 화면 스크롤 최상단에서 `서버 주소 (server.host)` 라벨+입력칸이 온전히 보임 | **보인다** (`r15_settings.png`) |
| 상태바 시계 가독성 | 밝은 배경에 **검은 아이콘**으로 또렷 (`12:31`) |
| 하단 저장/되돌리기/취소 버튼이 제스처 바와 안 겹침 | 겹치지 않음 |
| MainActivity 인식값이 상태바와 안 겹침 | 시계 아래에서 시작 (`r15_main.png`) |
| MainActivity 하단 버튼 5개가 제스처 바와 안 겹침 | 겹치지 않음 |
| **실제로 IP 입력 → 저장** | `192.168.0.42` 입력 후 저장 성공 |
| 저장이 적용됨 | 메인 화면이 `서버 연결중 192.168.0.42:5500` 으로 바뀜 (`r15_applied.png`) |

스크린샷은 `android/app/build/` 에 있다(빌드 산출물 폴더라 저장소에 안 들어간다):
`r15_main.png` · `r15_settings.png` · `r15_typed.png` · `r15_vision.png` · `r15_applied.png` ·
`r15_invalid.png` · `r15_final.png` · `r15_smoke.png`

**키보드 인셋은 코드상으로만 대응했고 실측하지 못했다.** 에뮬레이터에 `adb shell input text` 로
입력하면 하드웨어 키보드 경로라 소프트 키보드가 뜨지 않는다. 사용자가 실기기에서 칸을 탭해
키보드를 올렸을 때 편집 중인 칸이 가리는지는 그때 확인해야 한다. `ime()` 인셋과 `adjustResize`
를 둘 다 걸어 뒀으므로 동작할 것으로 보지만, 확인한 것과 구분해서 적는다.

### 계약 §10.1 — 빌드에 쓴 네이티브 소스 mtime

```
digit_pipeline.cpp    08-14 00:32:25
digit_pipeline.hpp    08-14 00:32:01
plate_templates.hpp   08-13 21:52:32
native-lib.cpp        08-13 21:25:56
→ libdigitcam.so      08-14 00:35:47  (arm64-v8a, 모든 소스보다 나중)
```

**이 규칙이 실제로 사고를 하나 잡았다.** mtime 을 적으려고 확인해 보니, 위 화면 검증을 돌린
APK 의 `.so` 는 `08-13 22:16` 이었고 그 사이 cpp-engineer 가 `digit_pipeline.cpp` 를 `00:32` 에
고쳐 놨었다 — **내 APK 가 옛 네이티브를 담고 있었다.** 다시 빌드해 위 mtime 으로 맞추고 재설치했다.
이번 건은 인셋·설정 화면이라 네이티브와 무관해서 검증 결과는 그대로 유효하지만, 규칙이 없었으면
모르고 지나갔을 자리다.

### 곁들여 처리한 것 — REQ-0014 의 새 motion 눈금

cpp-engineer 가 움직임 게이트를 고치면서 `motion` 의 **단위가 바뀌었다**(평균 차분 0~255 →
이동 화소 수). 설정을 안 맞추면 이번엔 반대로 가만히 든 폰이 계속 '움직임' 으로 잡힌다.

- `vision.motion_threshold` 0.60 → **2.0**, `vision.still_frames` 5 → **3** (assets + 기기 외부 파일 양쪽)
- 화면 표시에 단위를 붙였다: `motion 0.04px` — 숫자만 있으면 사용자가 어느 눈금으로 임계값을
  정해야 할지 알 수 없다.

효과가 바로 보인다. 같은 정지 장면에서 **이전 `motion 0.19` → "움직임"**, **지금 `motion 0.04px` → "정지"**.

### 기기 상태

앱을 **카메라 모드**로 두고 손을 뗐다. 설정 파일은 `기본값으로 되돌리기` 로 assets 기본값과
동일하게 복원해 뒀다(검증용으로 심었던 `vision.새키`·`custom.unknown` 은 남아 있지 않다).

### 처리 완료 · android-engineer · 2026-08-14T00:39:46+0900

인셋 처리 추가(설정·메인 양쪽) — server.host 입력칸 접근 가능 확인, 상태바 아이콘 대비 수정, IP 입력→저장→적용까지 스크린샷으로 검증. 곁들여 REQ-0014 의 새 motion 눈금(2.0/3)과 px 단위 표시 반영. 소프트키보드 가림은 미실측

