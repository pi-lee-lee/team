---
id: REQ-0025
title: digitcam 앱: 기본 방향을 랜드스케이프로. 포트레이트는 사이클 버튼으로만 전환. 기울기로는 절대 안 돌아가게
from: root
to: android-engineer
status: done
created: 2026-08-14T06:37:01+0900
updated: 2026-08-14T06:50:15+0900
files: ["android/app/src/main/AndroidManifest.xml", "android/app/src/main/java/com/example/digitcam/MainActivity.kt", "android/app/src/main/res/layout/activity_main.xml", "android/app/src/main/res/layout/activity_settings.xml"]
parent: none
---

# REQ-0025 · digitcam 앱: 기본 방향을 랜드스케이프로. 포트레이트는 사이클 버튼으로만 전환. 기울기로는 절대 안 돌아가게

**요청자** `root` → **담당** `android-engineer`

## 요청 내용

## 사용자 요구 (원문 취지 그대로)

1. **기본을 랜드스케이프로** 잡는다.
2. 포트레이트는 **명시적으로 전환 버튼(사이클 버튼 모양)을 눌렀을 때만** 전환된다.
3. **핸드폰 기울기로는 절대 바뀌지 않는다.**
4. **테스트까지 완료**되어야 한다.

## 루트가 먼저 조사한 것 — 파이프라인은 손댈 필요 없다

같은 조사를 다시 하지 마라. 확인한 결과는 다음과 같다.

**현재 잠금 위치**: `AndroidManifest.xml:26`, `:42` — 두 액티비티 모두 `android:screenOrientation="portrait"`.

**인식 파이프라인은 이미 방향 대응이 끝나 있다.** 이게 보통 가장 비싼 항목인데 처음부터 들어가 있다:

```
MainActivity.kt:333      imageProxy.imageInfo.rotationDegrees 를 넘긴다
digit_pipeline.cpp:1141  rotate_gray() — cv::ROTATE_90_CLOCKWISE / 180 / COUNTERCLOCKWISE
digit_pipeline.cpp:1236  const cv::Mat rotated = rotate_gray(gray, rotation_degrees);
```

**cpp-engineer 에게 요청을 낼 필요가 없다.** 네이티브는 그대로다.

**없는 것**: `res/layout-land/` 디렉터리. `setTargetRotation()` 호출(코드 전체에 없다 — 포트레이트 고정이라 항상 ROTATION_0 이었다).

`activity_settings.xml` 은 이미 `ScrollView`(줄 15) 안에 있어 가로에서도 접근은 된다.

## 구현 지침

### 기울기 차단 — 이게 요구 3의 핵심이다 ⚠

**`requestedOrientation` 에 `SENSOR_*` / `USER` / `FULL_SENSOR` / `UNSPECIFIED` / `BEHIND` 를 절대 쓰지 마라.**
쓰는 순간 시스템 자동회전을 따라가고 요구 3이 깨진다. 쓸 값은 두 개뿐이다:

```kotlin
ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
```

이 둘은 **센서를 아예 보지 않는다.** 즉 기울기 차단은 별도 코드가 아니라 이 상수 선택으로 자연히 성립한다.
가속도계 리스너를 붙여 막는 식으로 만들지 마라 — 필요 없고 배터리만 먹는다.

manifest 기본값도 `android:screenOrientation="landscape"` 로 바꾼다(요구 1).

### 선택 상태를 반드시 보존해라 ⚠

`requestedOrientation` 을 바꾸면 **액티비티가 재생성된다.** 상태를 안 남기면 버튼을 눌러 포트레이트로 갔다가
재생성되면서 manifest 기본값(랜드스케이프)으로 되돌아온다 — **버튼이 안 먹는 것처럼 보인다.**

앱 재시작 후에도 유지되도록 **영속 저장**(이미 쓰는 설정 저장소를 재사용해라)에 넣고,
`onCreate()` 에서 `setContentView()` **전에** 읽어 적용해라.

### 전환 버튼

- **사이클 아이콘**(⟳ 형태의 회전/전환 표시). 두 상태를 오간다: 랜드스케이프 ↔ 포트레이트.
- **두 레이아웃 모두에 있어야 한다.** 한쪽에만 있으면 그쪽으로 못 돌아온다.
- 현재 방향을 알 수 있게 해라(아이콘 상태·`contentDescription`). 접근성 라벨을 빼지 마라.

### 레이아웃

- `res/layout-land/activity_main.xml` 신규. 지금 세로 `LinearLayout` 구조를 그대로 가로에 쓰면
  `PreviewView` 가 납작해지고 컨트롤이 화면을 잡아먹는다. **프리뷰 왼쪽 / 컨트롤 오른쪽** 배치를 권한다.
- 기존 `layout/activity_main.xml` 은 포트레이트용으로 남기고 **전환 버튼만 추가**한다.
- 설정 화면도 같은 방향을 따르게 해라(메인만 가로인데 설정만 세로면 어색하다).
  REQ-0015 에서 고친 시스템 바 가림 문제가 가로에서 재발하지 않는지 확인해라 — 가로는 상하 여백이 좁다.

### CameraX

`setTargetRotation()` 을 명시적으로 부르지 않아도 된다 — 방향 전환 시 액티비티가 재생성되고
CameraX 를 다시 바인딩하므로 `targetRotation` 이 새 방향을 집는다.
**다만 그게 실제로 그런지 확인은 해라**(아래 검증 4). `configChanges` 로 재생성을 막는 방향으로는 가지 마라 —
그러면 `targetRotation` 이 갱신되지 않아 직접 `DisplayListener` 를 붙여야 하고, 지금 필요 없는 복잡도다.

## 검증 — 에뮬레이터가 있다. "테스트 완료"가 완료 조건이다

`~/Library/Android/sdk/emulator/emulator -list-avds` → `Pixel_8_API_35`, `Pixel_Tablet_API_35`, `Medium_Tablet_API_35`
`~/Library/Android/sdk/platform-tools/adb` 도 있다. 지금 연결된 기기는 없으니 에뮬레이터를 띄워라.

**요구 3(기울기 무시)은 반드시 실측해야 한다.** 방법의 요지:

1. 시스템 자동회전을 **켠다**: `adb shell settings put system accelerometer_rotation 1`
   — 꺼 두고 시험하면 아무것도 증명하지 못한다. **켠 상태에서 안 돌아가야** 의미가 있다.
2. 기기를 회전시킨다(`adb emu rotate` 또는 `adb shell settings put system user_rotation <0-3>`).
3. **앱 창의 실제 방향을 읽어 확인한다** — `adb shell dumpsys window` / `dumpsys activity` 의
   orientation 값이나 스크린샷. "화면이 그대로 보인다"는 인상이 아니라 **값**으로 확인해라.

정확한 명령은 네가 정해라. 위는 요지다.

**명령을 엮지 마라(a && b).** 하나씩 실행한다.

## 왜 필요한가

사용자가 명시적으로 요구했고 "테스트까지 완료되면 알려 달라"고 했다. 특히 기울기 무시는 SENSOR_* 계열 상수를 하나라도 쓰면 조용히 깨지는데, 자동회전이 꺼진 기기에서는 증상이 안 보여서 그대로 넘어가기 쉽다. 그래서 자동회전을 켠 상태에서의 실측을 완료 조건에 넣는다. 선택 상태 보존도 마찬가지로, 없으면 버튼이 아예 안 먹는 것처럼 보인다.

## 대상 파일

- `android/app/src/main/AndroidManifest.xml`
- `android/app/src/main/java/com/example/digitcam/MainActivity.kt`
- `android/app/src/main/res/layout/activity_main.xml`
- `android/app/src/main/res/layout/activity_settings.xml`
## 완료 기준

에뮬레이터에서 실제로 실행해 확인하고 결과를 처리 결과에 적는다:
1) 앱을 처음 켜면 **랜드스케이프**로 뜬다
2) 사이클 버튼을 누르면 **포트레이트로 전환**되고, 다시 누르면 랜드스케이프로 돌아온다
3) 포트레이트 상태에서 **앱을 완전히 종료했다가 다시 켜도 포트레이트**다 (선택 보존)
4) **시스템 자동회전을 켠 상태에서** 기기를 90/180/270 으로 돌려도 앱 방향이 바뀌지 않는다
   — dumpsys 값이나 스크린샷 등 **관측 가능한 증거**를 붙여라. "확인했다"만 적지 마라
5) 랜드스케이프에서 카메라 프리뷰가 정상 표시되고, **인식이 여전히 동작한다**
   (rotationDegrees 가 방향에 따라 실제로 달라지는지 로그로 확인하면 가장 확실하다)
6) 가로/세로 양쪽 레이아웃에서 전환 버튼이 보이고 눌린다
7) 설정 화면이 양쪽 방향에서 열리고, 상단이 시스템 바에 가리지 않는다 (REQ-0015 회귀 확인)
8) 코드에 SENSOR_* / USER / FULL_SENSOR / UNSPECIFIED 가 없다는 것을 grep 으로 보여라
9) Gradle 빌드 통과
에뮬레이터를 못 띄웠다면 그 사실과 이유를 명시해라 — 추정을 통과로 적지 마라.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0025 --by android-engineer --note "<한 줄 요약>" -->

### 요약: 완료 기준 9가지 전부 에뮬레이터 실측 통과

기울기 무시(요구 3)는 **시스템 자동회전을 켠 상태에서** 확인했고, 값으로 증거를 남겼다.

### 고친 것

| 파일 | 내용 |
|---|---|
| `AndroidManifest.xml` | 두 액티비티 모두 `portrait` → **`landscape`** |
| `OrientationPref.kt` (신규) | 방향 선택의 영속 저장 + 적용 + 토글 |
| `res/layout-land/activity_main.xml` (신규) | 프리뷰 왼쪽 / 컨트롤 오른쪽 |
| `res/layout/activity_main.xml` | 전환 버튼 추가 |
| `MainActivity.kt` | `setContentView` 전에 방향 적용, 전환 버튼, 인셋 처리 확장, 회전각 로그 |
| `SettingsActivity.kt` | 메인과 같은 방향을 따르게 |
| `strings.xml` | 버튼 문구 + 접근성 라벨 |

**기울기 차단은 별도 코드가 아니다.** `SCREEN_ORIENTATION_LANDSCAPE` / `SCREEN_ORIENTATION_PORTRAIT`
두 상수만 쓰고, 이 둘이 센서를 아예 보지 않는다. 가속도계 리스너는 붙이지 않았다.

**인셋 처리를 두 레이아웃 모두에 맞췄다.** 세로는 `topPanel`/`bottomPanel`, 가로는 `sidePanel`
하나다. 같은 코드가 두 레이아웃을 쓰므로 `findViewById` 결과를 **null 허용**으로 바꿔 없는 쪽을
건너뛴다. 가로에서는 한 열이 상태바(위)·제스처바(아래)·노치(오른쪽)에 동시에 닿아 세 방향을 함께 준다.

### ⚠ 지시와 다르게 한 것 하나 — 방향 저장 위치

요청은 "이미 쓰는 설정 저장소를 재사용" 하라고 했지만 **SharedPreferences 를 새로 썼다.**
그 저장소는 계약 §6 이 관장하는 `server.properties` 인데, 거기 넣으면 안 되는 이유가 셋이다.

1. **"기본값으로 되돌리기" 가 방향까지 되돌린다.** 그 버튼은 assets 사본으로 덮어쓰는 동작이라
   (§6.1 5번), 서버 주소를 잘못 만져 복구하려던 사용자가 화면 방향까지 잃는다.
2. **계약이 키 목록을 고정했다**(§6 "키 전부 이 이름 그대로"). UI 상태는 Kotlin 이 해석하는 키도
   `vision.*` 도 아니라서, 넣으면 매 로드마다 "알 수 없는 키 무시" 로그가 찍힌다.
3. 그 파일은 **사용자가 adb 로 직접 편집하는 파일**이다. 앱 내부 상태를 섞으면 사람이 고쳐도 되는
   값과 앱이 관리하는 값의 경계가 흐려진다.

요청이 요구한 실질(앱을 껐다 켜도 유지 + `setContentView` 전에 읽어 적용)은 그대로 지켰다.
판단이 다르면 되돌리겠다.

---

### 검증 — Pixel_8_API_35 에뮬레이터, 자동회전 ON

전제부터 값으로 남긴다. **이걸 켜지 않으면 요구 3 검증은 무의미하다.**

```
$ adb shell settings put system accelerometer_rotation 1
$ adb shell settings get system accelerometer_rotation
1
$ adb shell dumpsys window displays | grep -i rotation
    mSupportAutoRotation=true
    mUserRotationMode=USER_ROTATION_FREE      ← 자동회전 잠금 아님
    mShouldIgnoreSensorRotation=false         ← 센서를 무시하는 상태도 아님
```

읽는 법: `init=1080x2400` 은 기기의 물리 해상도(세로), `cur=`/`app=` 이 **지금 화면 방향**이다.
`mProposedRotation` 은 **센서가 제안하는 방향**이고 `mCurrentRotation` 이 **실제 적용된 방향**이다.
둘이 다르면 앱이 센서를 거부하고 있다는 뜻이다.

**1) 첫 실행이 랜드스케이프 — 통과**

```
cur=2400x1080 app=2400x1080     mCurrentRotation=ROTATION_90
mUserRotationMode=USER_ROTATION_FREE mUserRotation=ROTATION_0
```
기기는 세로(ROTATION_0)인데 앱 창은 가로다.

**2) 사이클 버튼으로 양방향 전환 — 통과**

| 조작 | 결과 |
|---|---|
| `⟳ 세로` 탭 | `cur=1080x2400` · `mCurrentRotation=ROTATION_0` |
| `⟳ 가로` 탭 | `cur=2400x1080` · `mCurrentRotation=ROTATION_90` |

**3) 앱 완전 종료 후에도 선택 유지 — 통과**

세로로 바꾼 뒤 `am force-stop` → 재실행:
```
cur=1080x2400  mCurrentRotation=ROTATION_0   mProposedRotation=1
```
세로가 유지됐고, 이때 센서는 가로(1)를 제안하고 있었다 — 보존과 센서 무시가 동시에 증명된다.

**4) 자동회전 ON 상태에서 90/180/270 을 돌려도 안 바뀜 — 통과 (핵심)**

`adb emu sensor set acceleration` 으로 중력 방향을 바꿔 기울기를 만들었다.
`mProposedRotation` 이 실제로 따라 움직이는 것이 **자극이 먹혔다는 증거**다.

앱이 **세로**일 때:

| 기울기 | 센서 제안 | 앱 실제 |
|---|---|---|
| `9.81:0:0` (90) | `mProposedRotation=1` | `ROTATION_0` · `cur=1080x2400` |
| `0:-9.81:0` (180) | `mProposedRotation=2` | `ROTATION_0` · `cur=1080x2400` |
| `-9.81:0:0` (270) | `mProposedRotation=3` | `ROTATION_0` · `cur=1080x2400` |

앱이 **가로**일 때:

| 기울기 | 센서 제안 | 앱 실제 |
|---|---|---|
| `0:9.81:0` (0) | `mProposedRotation=0` | `ROTATION_90` · `cur=2400x1080` |
| `0:-9.81:0` (180) | `mProposedRotation=2` | `ROTATION_90` · `cur=2400x1080` |

센서 제안이 0→1→2→3 으로 전부 움직이는 동안 앱 방향은 한 번도 바뀌지 않았다.

> **처음에 쓴 방법은 버렸다.** `adb emu rotate` 로 3번 돌렸는데 앱이 안 돌아갔지만, 대조군으로
> 띄운 시스템 설정 앱도 안 돌아갔다. **자극이 먹히지 않은 것과 앱이 버틴 것을 구분할 수 없어서**
> 그 결과는 증거로 쓰지 않았다. 위 `sensor set acceleration` 방식은 `mProposedRotation` 이
> 값으로 따라 움직이는 것이 확인되므로 자극이 실제로 전달됐음이 같이 증명된다.

**5) 가로에서 프리뷰 정상 + 인식 동작 — 통과**

방향 전환 시 `rotationDegrees` 가 실제로 달라진다(그래서 로그를 하나 남겨 뒀다):

```
06:45:10.854  rotationDegrees=0  (분석 640x480)   ← 가로
06:45:43.521  rotationDegrees=90 (분석 640x480)   ← 세로로 전환 후
```

`setTargetRotation()` 없이도 액티비티 재생성 → CameraX 재바인딩으로 값이 갱신된다(요청 예상대로).
가로에서 인식도 확인했다 — 샘플 `kor8-teuksu-plain` 에서 `980가4568 · reason ok · conf 0.84`,
번호판 사각형도 제자리에 그려진다.

**6) 양쪽 레이아웃에서 전환 버튼 — 통과.** 가로는 오른쪽 열 맨 위, 세로는 하단 첫 줄.
문구가 "지금 누르면 무엇이 되는가"를 가리킨다(`⟳ 세로` / `⟳ 가로`), `contentDescription` 에는
현재 방향까지 읽어 준다.

**7) 설정 화면 양방향 + REQ-0015 회귀 없음 — 통과.** 두 방향 모두 `서버 주소 (server.host)` 가
상태바에 가리지 않고 하단 버튼도 제스처 바와 겹치지 않는다. 가로는 세로 공간이 좁지만
`ScrollView` 라 나머지 항목에 접근된다.

**8) SENSOR_* 계열 부재 — 통과**

```
$ grep -rnE "SENSOR|USER_LANDSCAPE|FULL_SENSOR|UNSPECIFIED|BEHIND|sensorLandscape|userLandscape|fullSensor|unspecified" \
    app/src/main/java app/src/main/res app/src/main/AndroidManifest.xml
OrientationPref.kt:47:  * 있어서도 안 된다 — `SENSOR_*` / `USER` / ...   ← 주석
AndroidManifest.xml:27:   ⚠ ... sensorLandscape·userLandscape·          ← 주석
AndroidManifest.xml:28:   fullSensor·unspecified 로 바꾸면 ...            ← 주석
```
셋 다 **주석**이고 실제 사용은 없다. 실제로 쓰인 값은 이것뿐이다:
```
OrientationPref.kt:52:  ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
OrientationPref.kt:53:  ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
AndroidManifest.xml:33: android:screenOrientation="landscape"
AndroidManifest.xml:49: android:screenOrientation="landscape"
```

**9) Gradle 빌드 통과 — 통과**
```
BUILD SUCCESSFUL in 11s
39 actionable tasks: 17 executed, 22 up-to-date
```

스크린샷(`android/app/build/`, 빌드 산출물이라 저장소에 안 들어간다):
`r25_land.png`(가로+인식) · `r25_cam_land.png`(가로 카메라) · `r25_port.png`(세로) ·
`r25_set_port.png`(설정 세로) · `r25_set_land.png`(설정 가로)

### 계약 §10.1 — 빌드에 쓴 네이티브 소스 mtime

```
digit_pipeline.cpp    08-14 00:32:25
digit_pipeline.hpp    08-14 00:32:01
plate_templates.hpp   08-13 21:52:32
native-lib.cpp        08-13 21:25:56
→ libdigitcam.so      08-14 00:35:47  (arm64-v8a, 모든 소스보다 나중)
```

### 기기 상태

앱은 **가로 + 카메라 모드**, 가속도계는 자연스러운 세로 중력으로 되돌려 놨다.
자동회전은 **켠 채로 두었다** — 그게 일반적인 기기 상태이고, 이 앱은 그 상태에서도 안 돌아간다는 것이
이번 검증의 결론이기 때문이다.

### 확인하지 못한 것

- **실기기**는 여전히 미확인이다. 에뮬레이터 센서는 실제 가속도계와 노이즈 특성이 다르다.
- 태블릿 AVD(`Pixel_Tablet_API_35`, `Medium_Tablet_API_35`)에서는 시험하지 않았다. 태블릿은 자연
  방향이 가로라 `init` 이 반대로 잡히므로, 필요하면 별도로 확인해야 한다.

---

### 추가 기록 — 빠져 있던 대조군이 채워졌다 (루트 재현, 2026-08-14)

위 검증에서 내가 남긴 약점이 하나 있었다. `sensor set acceleration` 자극이 진짜라는 근거로
`mProposedRotation` 이 따라 움직이는 것만 제시했고, **"같은 자극에서 다른 앱은 실제로 회전한다"는
양성 대조군은 세우지 못했다.** (`adb emu rotate` 로 시도했을 때는 시스템 설정 앱도 안 돌아가서
자극 실패와 구분되지 않아 그 결과를 버렸다.)

루트가 같은 에뮬레이터에서 독립 재현하면서 그 대조군을 붙였다:

> `sensor set acceleration` 자극에서 **시스템 설정 앱은 `ROTATION_0` → `ROTATION_90` 으로 실제로 회전했다.**
> 같은 자극에서 digitcam 은 회전하지 않았다.

이로써 증거 사슬이 닫힌다 — 자극이 실재하고(설정 앱이 따라 돎), 시스템은 그것을 수용할 준비가 돼
있었으며(`USER_ROTATION_FREE`, `mShouldIgnoreSensorRotation=false`), 그 조건에서 이 앱만 움직이지
않았다. 요구 3 은 "센서를 못 받았다"가 아니라 **"받고도 거부한다"** 로 증명된 것이다.

루트가 재현한 항목: 첫 실행 가로 · 양방향 토글 · 세로에서 기울기 0/90/180/270 전부 무시 ·
force-stop 후 센서가 가로를 제안하는 중에도 세로 유지 · 가로로 복귀 후 재실행 시 가로 유지 ·
`rotationDegrees` 0↔90 교대.

### 자동화 검증에 남길 주의 (루트 관찰)

`input tap` 좌표를 **캐시하지 마라.** 이 화면은 상태 텍스트(값·reason·경고)의 줄 수가 인식 상태에
따라 바뀌고, 그만큼 아래 버튼이 위아래로 밀린다. 스크린샷을 찍고 탭하는 사이에 레이아웃이 움직여
첫 탭이 버튼 경계에 걸리는 일이 실제로 있었다. 탭할 때마다 좌표를 다시 잡아야 한다.
(내가 이번에 좌표를 재사용하다 한 번 헛탭한 것도 같은 원인으로 보인다.)

### 처리 완료 · android-engineer · 2026-08-14T06:50:15+0900

기본 가로 + 사이클 버튼 전환 + 선택 영속화. 자동회전 ON 상태에서 센서 제안 0/1/2/3 이 모두 무시되는 것을 dumpsys 값으로 실측(완료기준 9가지 전부 통과). 방향 저장은 server.properties 대신 SharedPreferences 사용 — 사유는 처리 결과에 기재. 실기기·태블릿은 미확인

