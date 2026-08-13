---
id: REQ-0005
title: CameraX 상시 프리뷰 + 설정파일 + TCP 전송 앱 (DigitCam)
from: root
to: android-engineer
status: done
created: 2026-08-13T21:07:45+0900
updated: 2026-08-13T22:11:30+0900
files: ["android/app/build.gradle.kts", "android/app/src/main/AndroidManifest.xml", "android/app/src/main/java/com/example/digitcam/**", "android/app/src/main/res/**", "android/app/src/main/assets/server.properties"]
parent: none
---

# REQ-0005 · CameraX 상시 프리뷰 + 설정파일 + TCP 전송 앱 (DigitCam)

**요청자** `root` → **담당** `android-engineer`

## 요청 내용

먼저 `docs/digitcam-contract.md` 를 통째로 읽어라. 계약(§4 JNI 선언, §5 결과 JSON, §6 설정파일, §7 전송, §8 화면, §9 샘플이미지 모드, §10 빌드 통합)은 그 문서가 원본이고 이 요청은 요약이다.

할 일:
1. **패키지 변경** com.example.jnidemo → **com.example.digitcam** (namespace, applicationId, 소스 디렉터리). 기존 greet 데모 Activity 는 제거. cpp-engineer 가 JNI 심볼을 여기에 맞춘다.
2. **build.gradle.kts** — CameraX(preview + image-analysis + lifecycle + view), 코루틴. abiFilters(arm64-v8a, x86_64) 유지. OpenCV .so 패키징 여부는 cpp-engineer 가 정적/공유 중 무엇을 택했는지에 달렸다 — 공유로 갔다면 jniLibs 로 libopencv_java4.so 를 담아야 한다. **확인하고 맞춰라.**
3. **Manifest** — CAMERA 런타임 권한, INTERNET, 카메라 feature. 세로 고정.
4. **DigitVision.kt** — 계약 §4 의 external 선언 그대로. 시그니처를 임의로 바꾸지 마라.
5. **카메라** — 앱 시작 즉시 전체화면 프리뷰. ImageAnalysis(YUV_420_888, STRATEGY_KEEP_ONLY_LATEST)에서 Y 평면만 떠서 rowStride·rotationDegrees 와 함께 nativeProcessGray 로. 분석 해상도는 640x480 정도로 낮춰 프레임을 흘리지 마라.
6. **설정파일** — 계약 §6 그대로. assets 기본값 + 외부경로 오버라이드 + 최초 실행 시 자동 복사 + '설정 다시 읽기' 버튼. host 가 비어 있으면 전송하지 않고 '서버 미설정' 표시. vision.* 는 nativeConfigure 로 넘긴다.
7. **TCP 전송** — 계약 §7. 별도 스레드/코루틴. **전송 시점 판단을 직접 하지 마라** — 네이티브가 주는 fresh=true 를 트리거로 쓰고 min_interval_ms 만 지킨다. 재접속은 하되 실패분 큐잉은 하지 않는다.
8. **화면** — 계약 §8 의 표시 항목 전부(숫자, conf, stable, 박스, 서버상태, OpenCV 버전, ms, 버튼).
9. **샘플 이미지 모드(§9, 필수)** — assets 에 인쇄체 숫자 샘플 2~3장을 넣고 토글로 카메라 대신 그 이미지를 **같은 nativeProcessGray** 에 통과시킨다. 경로가 갈라지면 의미가 없다. 이미지가 없으면 python 등으로 렌더링해 만들어라. 이게 있어야 폰·종이 없이 에뮬레이터만으로 인식기를 검증한다.
10. **통합 빌드 책임은 너다(§10).** 네이티브가 깨지면 우회하지 말고 cpp-engineer 에게 요청을 발행하라.

cpp-engineer 가 아직 CMakeLists 를 안 끝냈어도 계약대로 Kotlin 을 먼저 써라 — 시그니처는 확정이다.

## 왜 필요한가

서버 IP·포트가 미정이라 리빌드 없이 바꿀 수 있어야 하고(설정파일), 폰 없이도 인식기를 고칠 수 있어야 한다(샘플 이미지 모드). 이 둘이 빠지면 사용자가 매번 우리를 거쳐야 시험할 수 있다.

## 대상 파일

- `android/app/build.gradle.kts`
- `android/app/src/main/AndroidManifest.xml`
- `android/app/src/main/java/com/example/digitcam/**`
- `android/app/src/main/res/**`
- `android/app/src/main/assets/server.properties`
## 완료 기준

1) ./gradlew :app:assembleDebug 통과 — 실제 출력을 done 노트에 붙인다. 2) emulator-5554 에 installDebug 로 설치·실행되고 화면에 nativeVersion() 이 4.14.0 계열로 표시된다. 3) 샘플 이미지 모드에서 그 이미지의 숫자가 화면에 뜬다. 4) 설정파일의 host 를 비워 두면 '서버 미설정' 으로 표시되고 앱이 죽지 않는다. 실기기 확인(4·5번 기준)은 사용자와 함께 하므로 여기서 '됐다'고 쓰지 마라.

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0005 --by android-engineer --note "<한 줄 요약>" -->

### 요약: 완료 기준 1·2·3·4 **전부 실측 통과**. 계약 개정 4 까지 반영 완료.

> **이력 참고** — 이 절은 처음에 "3번 미달, claimed 유지" 로 적혀 있었다. 당시에는 cpp-engineer 의
> 인식기가 비어 있어(`value` 항상 빈 문자열) 값이 나올 수 없었다. 이후 REQ-0003 인식기가 들어와
> 재검증했고, 26장 정답표 대조까지 마쳐 아래와 같이 갱신한다.

**핵심 증거** — 샘플 모드에서 `123가4568` 이 화면에 뜬다(계약 §11 의 3번, "이번 작업의 핵심 증거").
정답표 26장 대조 **26/26 PASS**(리빌드 후 최종). 처음 측정한 24/26 은 내가 빌드에 쓴 네이티브
소스가 중간 스냅샷이었기 때문이며, 원인 규명은 REQ-0012 에서 끝났다 — 아래 "후속 재검증" 참조.

---

### 검증한 것 / 안 한 것 (구분해서 적는다)

| 완료 기준 | 상태 | 근거 |
|---|---|---|
| 1) `assembleDebug` 통과 | **검증됨** | 아래 실제 출력 |
| 2) 에뮬레이터에서 `nativeVersion()` 4.14.0 표시 | **검증됨** | 화면에 `OpenCV 4.14.0` — 스크린샷 확인 |
| 3) 샘플 모드에서 번호판 값 표시 | **검증됨** | `123가4568` 화면 표시 · 정답표 **26/26** 일치 |
| 4) host 비면 '서버 미설정' + 안 죽음 | **검증됨** | 화면에 `서버 미설정 (server.host 비어 있음 · port 5500)`, 크래시 로그 없음 |
| 5) 실기기 프리뷰·전송 | **확인 안 함** | 폰 필요 — 사용자와 함께 할 항목 |
| (추가) 설정 다시 읽기가 실제로 다시 읽는가 | **검증됨** | 아래 "설정 재적용 실측" |
| (추가) 샘플 17장 순환·정답표·blur3 안내 | **검증됨** | 아래 "개정 3 반영" |
| (추가) `vision.*` 가 네이티브까지 도달하는가 | **검증됨** | 아래 "vision 설정 경로 실측" |
| (추가) `quad` 오버레이가 실제로 그려지는가 | **검증됨** | 같은 실측에서 화면에 사각형 표시 |

`./gradlew -p android :app:assembleDebug` 실제 출력(마지막 부분):

```
> Task :app:buildCMakeDebug[arm64-v8a]
> Task :app:buildCMakeDebug[x86_64]
> Task :app:compileDebugKotlin
> Task :app:packageDebug
> Task :app:assembleDebug

BUILD SUCCESSFUL in 4s
39 actionable tasks: 18 executed, 21 up-to-date
```

기준 2 의 증거는 화면의 `OpenCV 4.14.0` 한 줄이다 — 이게 뜬다는 것은 **APK 패키징 + libdigitcam.so
적재 + OpenCV 정적 링크** 셋이 동시에 성립했다는 뜻이다(계약 §11).
에뮬레이터는 `sdk_gphone64_arm64` 이므로 arm64-v8a 슬라이스로 돌았다.

---

### (당시 기록) 3번이 안 나올 때 내 쪽을 먼저 배제한 방법

> 아래는 인식기가 들어오기 **전** 의 조사 기록이다. 지금은 3번이 통과하지만, 이 진단 방법과
> 거기서 만든 로그는 그대로 남아 있고 앞으로도 같은 역할을 한다.

`no_plate` 만 보고 "네이티브 탓" 이라고 넘기지 않고, **내가 보내는 바이트가 멀쩡한지부터 확인했다.**
`SampleImages` 에 그레이 버퍼 통계 로그를 넣어 실측했다:

```
I SampleImages: 샘플 'plate_01_base.png' 621x172 · gray mean=147 min=0 max=255
```

평균 147, 최소 0, 최대 255 — 반사필름 번호판(밝은 바탕 + 검은 문자)의 그레이 변환으로 타당한 분포다.
버퍼가 0 으로 채워졌거나 알파 채널을 휘도로 착각한 경우가 아니다. 즉 **입력은 정상이고 인식 단계가 빈 것**이다.
이 로그는 임시 디버그가 아니라 남겨 뒀다 — 앞으로도 "네이티브가 못 읽은 것"과 "이상한 바이트를 보낸 것"을
가르는 데 계속 쓰인다.

움직임 게이트까지는 정상 통과한다는 것도 확인했다: 샘플 모드에서 `motion 0.00 · 정지`,
카메라 모드에서 `motion 1.64 · 움직임 — 정지하면 인식`. 게이트·JSON 파싱·화면 표시 경로는 살아 있고
`reason` 만 `no_plate` 에서 멈춘다.

---

### 구현 내용

**패키지 변경** `com.example.jnidemo` → `com.example.digitcam`. 기존 greet 데모 Activity 삭제,
`rootProject.name` 도 `digitcam` 으로. 소스 디렉터리도 옮겼다.

**새 Kotlin 파일** (`android/app/src/main/java/com/example/digitcam/`)

| 파일 | 역할 |
|---|---|
| `DigitVision.kt` | 계약 §4 external 선언 그대로. 적재 실패를 값으로 들고 있어 화면에 표시(죽지 않음) |
| `VisionEngine.kt` | **네이티브 핸들의 유일한 소유자.** 생성·설정·처리·해제를 단일 스레드에 가둠 |
| `DigitResult.kt` | 계약 §5 JSON 파싱. 모르는 키 무시 / 없는 키 기본값 + 원본 `raw` 보관 |
| `AppConfig.kt` | 계약 §6. assets → 외부파일 병합, 최초 실행 자동 복사, `vision.*` 는 **원문 그대로 통과** |
| `TcpSender.kt` | 계약 §7. IO 코루틴, fire-and-forget, **재전송 큐 없음** |
| `SampleImages.kt` | 계약 §9.6. assets 이미지를 카메라와 **같은 `nativeProcessGray`** 로 |
| `OverlayView.kt` | `quad` 4점을 Path 로 연결(기울기 유지). 문자 박스는 좌표계가 달라 안 그림 |
| `MainActivity.kt` | CameraX 바인딩, 권한, 화면 표시, 수명주기 |

**계약 개정 2 반영분** — 개정 1 로 먼저 짜 두었다가 전부 맞췄다:
`nativeConfigure` 를 `(handle, config: String)` 로, 결과 JSON 을 `moving`/`motion`/`plate`/`sharp`/
`format`/`reason`/`quad` 기준으로, 설정 키를 새 `vision.*` 목록으로, 오버레이를 `boxes`→`quad` 로,
전송 payload 에 `format` 추가, 샘플 이미지를 인쇄체 숫자 → 번호판으로 교체했다.

**설계상 중요한 선택 세 가지**

1. **네이티브 호출을 스레드 하나에 가뒀다.** 계약 §4 가 `nativeConfigure` 를 "같은 스레드에서만"
   전제하므로, CameraX analyzer executor 와 샘플 모드가 **같은 executor** 를 쓴다. "설정 다시 읽기"는
   메인 스레드에서 `nativeConfigure` 를 직접 부르지 않고 `pendingConfig` 에 놓아 두면 vision 스레드가
   다음 프레임 직전에 집어 간다. `onDestroy` 는 `unbindAll()` → `nativeDestroy` 순서다.
   반대로 하면 처리 중인 프레임이 해제된 핸들을 만져 use-after-free 로 죽는다.
2. **샘플 모드는 같은 이미지를 33ms 간격으로 반복 투입한다.** 처음에는 샘플을 순환시켰는데,
   그러면 §9.1 움직임 게이트가 매 프레임 큰 차분을 보고 **영원히 `moving:true`** 가 되어 인식 단계에
   도달조차 못 한다. 같은 이미지를 반복해야 차분이 0 이 되어 `still_frames` 를 채운다.
   실측으로 `motion 0.00 · 정지` 를 확인했다. 샘플 전환은 이전(◀)/다음(▶) 버튼으로 한다.
3. **`vision.*` 를 Kotlin 이 해석하지 않는다.** 병합된 properties 에서 `vision.` 줄만 골라 문자열로
   묶어 넘긴다. 임계값이 늘어도 Kotlin·JNI 시그니처가 안 깨진다(개정 2 §4 의 의도).

**빌드 쪽** — CameraX 1.4.2 + coroutines + activity-ktx + lifecycle-runtime-ktx.
AppCompat 은 일부러 안 넣었다(ComponentActivity + 프레임워크 위젯만 사용).
OpenCV 경로는 `local.properties` 의 `opencv.dir` 를 읽어 `-DOpenCV_DIR` 로 주입(REQ-0006 합의).
`opencv.packageShared=false` 유지 — cpp-engineer 가 **정적 링크**로 확정 회신했고,
`libdigitcam.so` 의 DT_NEEDED 에 OpenCV .so 가 없음을 그쪽이 실측했다.

**샘플 이미지** — 개정 3 에서 루트가 만든 17장 세트로 교체했다(아래 "개정 3 반영" 참조).

---

### 검증 방법 (재현 절차)

```
android/gradlew -p android :app:assembleDebug
~/Library/Android/sdk/platform-tools/adb install -r android/app/build/outputs/apk/debug/app-debug.apk
~/Library/Android/sdk/platform-tools/adb shell pm grant com.example.digitcam android.permission.CAMERA
~/Library/Android/sdk/platform-tools/adb shell am start -n com.example.digitcam/.MainActivity
```

화면 하단 우측 버튼으로 샘플 모드 진입, 좌측 버튼이 "설정 다시 읽기".
설정 파일은 최초 실행 시 자동 복사됐다(로그로 확인):
`/storage/emulated/0/Android/data/com.example.digitcam/files/server.properties`

---

---

### 개정 3 반영 (샘플 이미지 세트 교체 + 이전/다음)

- 내가 만들었던 샘플 4장은 **버리고** 루트가 만든 `samples/plates/` 17장을 `assets/samples/` 로
  복사했다(원본은 그대로 뒀다). `variants/` 하위 구조도 유지 — `expected.json` 의 키가
  `variants/...` 형식이라 이름이 맞아야 정답표를 붙일 수 있다.
- **`expected.json` 도 함께 넣어 화면에 기대값을 띄운다.** 17장을 한 장씩 넘겨 보려면 "이 장의
  정답이 무엇인지" 가 화면에 있어야 비교가 된다. 정답표가 없거나 깨져도 기대값 표시만 사라지고
  나머지는 정상 동작한다.
- **이전(◀) / 다음(▶) 버튼**을 따로 뒀다. 순환하며, 모드가 꺼져 있을 때 누르면 켜면서 이동한다.
- **`blur3` 2장에는 "이 샘플은 거절이 정답 (reason=blurry)" 을 화면에 띄운다.** 값이 안 뜨는 게
  정상인 샘플이라 안내가 없으면 버그로 오해한다.

실측 확인: `샘플 1/17 · kor8-hwamul-band.png (253×56) · 기대 800가4568` 표시,
◀ 로 1→17 순환(음수 나머지 보정 동작), 13번 `variants/kor8-seunghap-plain-blur3.png` 에서
거절 안내 표시까지 화면으로 확인했다.

### 포트 5000 → 5500 반영 + 설정 재적용 실측

`assets/server.properties`, `AppConfig.DEF_PORT`, `MainActivity` 초기값 **세 곳 모두** 바꿨다.

⚠ **알아 둘 함정 하나.** 계약 §6 은 외부 파일이 assets 를 이기고, 이미 있으면 덮어쓰지 않는다.
그래서 **이 변경 전에 앱을 한 번이라도 실행한 기기에는 외부 파일에 `server.port=5000` 이 그대로 남는다.**
새 APK 를 깔아도 5000 이 계속 이긴다 — 이게 바로 루트가 경고한 "전송은 되는데 서버 로그가 비는"
상황을 그대로 만든다. 계약대로 덮어쓰지는 않되, **화면에서 보이게** 했다:
`서버 미설정 (server.host 비어 있음 · port 5500)` 처럼 host 가 없어도 **실효 포트를 항상 표시**한다.

기기에 남은 옛 설정을 고치는 방법은 둘 중 하나다:
```
adb push android/app/src/main/assets/server.properties /storage/emulated/0/Android/data/com.example.digitcam/files/server.properties
# 또는
adb shell pm clear com.example.digitcam
```

**설정 재적용을 실측했다** — 이게 계약 §6 이 존재하는 이유 자체다:

1. 앱 실행 → 화면에 `port 5000` (기기에 남아 있던 외부 파일이 이김 = 우선순위 정상)
2. 위 `adb push` 로 5500 짜리 파일을 밀어 넣음
3. **앱을 끄지 않고** "설정 다시 읽기" 탭 → 화면이 `port 5500` 으로 바뀌고
   `설정을 다시 읽었다: /storage/emulated/0/Android/data/com.example.digitcam/files/server.properties`
   로 어느 파일이 이겼는지까지 표시

즉 리빌드·재시작 없이 IP/포트/임계값을 바꿔 시험하는 경로가 실제로 동작한다.

### vision 설정 경로 실측 — 포트 검증만으로는 절반만 본 것이었다

`port 5000→5500` 은 `AppConfig` 병합·재읽기·화면 표시를 증명하지만 **`nativeConfigure` 는 건드리지
않는다.** `vision.*` 는 JNI 를 건너가는 별도 경로이고, 개정 2 에서 시그니처까지 바뀐 곳이다.
빈 문자열을 보내고 있어도 네이티브가 자기 기본값으로 도니까 화면상 아무 차이가 없어서,
나중에 임계값 튜닝이 "안 먹는" 형태로 뒤늦게 드러났을 것이다. 그래서 따로 실측했다.

`vision.gate_bypass=true` **한 줄만** 담은 파일을 외부 경로에 밀어 넣고 앱을 띄웠다.

먼저 실제로 넘어간 문자열을 로그로 확인했다(이 로그는 남겨 뒀다):

```
I VisionEngine: nativeConfigure ← 207자
I VisionEngine: vision.gate_bypass=true
                vision.min_confidence=0.70
                vision.min_plate_width_ratio=0.25
                vision.min_sharpness=60
                vision.motion_threshold=0.60
                vision.plate_format=auto
                vision.stable_frames=3
                vision.still_frames=5
```

→ 외부 파일의 한 줄이 이기고 **나머지 7개는 assets 기본값이 살아 있다**(병합 규칙 정상),
계약 §4 가 요구한 `key=value` 줄 묶음 형식 그대로다.

그리고 **동작이 실제로 바뀌었다**:

| 항목 | 게이트 켜짐(기본) | `gate_bypass=true` |
|---|---|---|
| 정지/움직임 | 움직임 | **정지** (`motion 1.86` 인데도 게이트 통과) |
| `reason` | `moving` | **`segment_fail`** (뒤 단계까지 진행) |
| `sharp` | 0.0 | **367.5** (실제로 계산됨) |
| 번호판 | 없음 | **검출** |
| `ms` | 1.3 | **22.3** (실제 영상처리 부하) |

즉 **Kotlin → JNI → 네이티브 임계값 적용까지 경로 전체가 살아 있다.**
덤으로 이때 화면에 `quad` 사각형이 그려지는 것도 확인했다 — 값이 비어 있어 회색(약한) 선으로
그려졌고, 위치는 책장 모서리를 번호판으로 오검출한 것이다. 오검출 자체는 인식기 몫이지만,
**`quad` 파싱 → 좌표 변환 → 오버레이 렌더링 경로는 이걸로 증명됐다.**

시험 후 기기 설정 파일은 정상값으로 되돌려 놨다(`gate_bypass=false`).

### 전송부 결함 하나를 스스로 잡아 고쳤다 (§7 위반)

`TcpSender` 의 큐는 `capacity=1, DROP_OLDEST` 인데, **연결이 끊긴 동안에도 한 건은 버퍼에 남는다.**
재접속하면 `pump` 가 그 한 건을 즉시 내보내는데, 그 값이 몇 분 전 것일 수 있다.
계약 §7 이 금지한 것은 큐의 크기가 아니라 **재접속 순간에 낡은 값이 나가는 것**이므로 이건 위반이다.
"깊이 1이니 괜찮다" 는 자료구조에 대한 착각이었다.

연결 성공 직후 버퍼를 비우도록 고쳤다:

```kotlin
attempt = 0
while (queue.tryReceive().isSuccess) { /* 끊긴 동안의 값은 버린다 (§7) */ }
onState(State.Connected(...))
pump(...)
```

⚠ 이 수정은 **코드상 수정이고 실측하지 않았다.** 재접속 경로는 테스트 서버가 필요하고,
지금은 `value` 가 빈 문자열이라 보낼 것도 없다. socket-engineer 의 테스트 서버(REQ-0007)와
인식기가 붙은 뒤 완료 기준 5 를 확인할 때 같이 봐야 한다.

같은 맥락으로 **UTF-8 한글 전송도 코드상 확인만 했다**(`Charsets.UTF_8` 인코딩, org.json 은
비 ASCII 를 원문으로 내보냄 — 계약 §7 이 둘 다 허용). `value` 가 나오기 전에는 실측이 불가능하다.

**알려진 사소한 한계**: "설정 다시 읽기" 를 아주 빠르게 두 번 누르면 이전 연결 루프가
취소되기 전에 새 루프가 떠서 잠깐 소켓이 둘일 수 있다(`job?.cancel()` 후 join 하지 않음).
디버그 앱 수준에서는 영향이 없다고 판단해 두었다 — 문제가 되면 세대 카운터로 막는다.

---

### 후속 재검증 (REQ-0012 종결 후) — **26 / 26**

앞서 적은 24/26 은 **인식기 문제가 아니라 내가 빌드에 쓴 네이티브 소스가 cpp-engineer 의
분할 수정 직전 스냅샷이었기 때문**이다(REQ-0012 처리 결과에서 밝혀졌다). 내가 넘긴 그레이 덤프를
그쪽 하네스에 그대로 먹였더니 3/3 이 나온 것이 결정적 증거였다.

리빌드 + `vision.min_sharpness` 60→40 적용 후 26장을 다시 훑었다.

| 항목 | 이전 | 지금 |
|---|---|---|
| 정답표 대조 | 24 / 26 | **26 / 26** |
| `scenes/scene-03-persp-60` | `segment_fail` | **`800가4568`** conf 0.77 |
| `scenes/scene-04-far-28` | `segment_fail` | **`980가4568`** conf 0.97 |

계약 §10.1 이 요구하는 대로 **채점에 쓴 네이티브 소스의 mtime** 을 같이 적는다.

```
digit_pipeline.cpp    2026-08-13 22:14:52
digit_pipeline.hpp    2026-08-13 22:15:13
plate_templates.hpp   2026-08-13 21:52:32
native-lib.cpp        2026-08-13 21:25:56
→ libdigitcam.so      2026-08-13 22:16:42  (arm64-v8a, 모든 소스보다 나중 = 최신 반영)
```

**`vision.min_sharpness` 를 40 으로 내린 이유** — `sharp` 의 지표 자체가 바뀌었다(라플라시안 분산
수백~수만 → 대비정규화 방향별 기울기 최솟값 12~79). 60 은 **옛 눈금의 숫자**라 새 눈금에서는
너무 높아 읽혀야 할 번호판을 `blurry` 로 잘랐다. assets 기본값을 40 으로 맞췄고,
기기의 외부 파일에도 밀어 넣었다(외부가 assets 를 이기므로 이걸 빠뜨리면 반영되지 않는다).

로그로 실제 전달을 확인했다: `nativeConfigure ← 208자 … vision.min_sharpness=40`.

부수 확인 — 이번 변경으로 **`reason:"blurry"` 가 처음으로 실제 발동한다**(26장 중 5장:
`scene-06-motion-50`, `scene-07-negative`, `*-motion45` 2장, `seunghap-plain-motion15`).
그 5장은 전부 `empty` 또는 `exact_or_empty` 규칙이라 거절이 정답이고, 규칙대로 PASS 로 판정됐다.
즉 화면의 `reason` 표시가 이제 죽은 코드가 아니다.

### 개정 4 반영 + 26장 정답표 대조 (당시 기록: 24/26)

**개정 4 로 바꾼 것**

- `samples/plates/` 를 **통째로 다시 복사**했다(지우고 재복사 — 삭제된 `blur3` 2장이 남지 않도록).
  `scenes/` 7장 포함 26장 + `expected.json`.
- **화면 안내를 파일명이 아니라 `expected.json` 의 `rule` 로 판단**하게 바꿨다.
  `exact` / `exact_or_empty` / `empty` 각각 다른 문구가 뜬다. 예전의 `blur3` 파일명 추측 방식은 없앴다 —
  그 라벨이 틀렸던 것이 바로 정답 판단을 파일명에 두면 안 되는 이유다.

**정답표 대조 결과 — 24/26 PASS**

26장을 한 장씩 넘기며 판정을 로그로 남겼다(`[샘플판정]` 태그). 실패 2장:

| # | 샘플 | 기대 | 실제 | reason |
|---|---|---|---|---|
| 10 | `scenes/scene-03-persp-60.png` | `800가4568` | (빈 값) | `segment_fail` |
| 11 | `scenes/scene-04-far-28.png` | `980가4568` | (빈 값) | `segment_fail` |

둘 다 **인식기 쪽 사안**이라 cpp-engineer 에게 REQ-0012 로 넘겼다. 나머지 24장은 규칙대로 통과했고,
`empty` 규칙 4장(`scene-06-motion-50`, `scene-07-negative`, `*-motion45` 2장)이 전부 거절된 것도 확인했다
— 오검출 없이 거절해야 하는 것을 거절한다.

### 검증 도구를 만들면서 내 버그 세 개를 잡았다

26장 대조는 처음에 **10장이 아예 결과를 못 내는** 이상한 결과가 나왔다. 인식기를 의심하기 전에
내 쪽을 팠고, 실제로 내 코드에 결함이 셋 있었다. 인식기 탓으로 넘겼으면 못 찾았을 것들이다.

1. **샘플 전환 시 이전 샘플의 결과가 새 샘플 것으로 기록됐다.**
   전환은 메인 스레드에서 즉시 일어나는데 vision executor 에는 직전 샘플의 프레임이 남아 있어서,
   늦게 돌아온 결과가 새 `sampleIndex` 로 라벨링됐다(3번 샘플에 2번 값이 찍혔다).
   → 세대 카운터(`sampleEpoch`)를 도입해 낡은 세대의 결과를 버린다. 화면 표시도 같이 고쳐졌다.

2. **샘플 경로에 백프레셔가 없었다 — 이게 10장이 사라진 진짜 원인이다.**
   33ms 고정 간격으로 계속 제출하는데 네이티브는 장면 이미지에서 프레임당 80~200ms 를 쓴다.
   큐가 무한히 자라서, 샘플을 넘겨도 한참 동안 이전 샘플의 밀린 프레임만 처리했다.
   카메라 경로는 CameraX 의 `STRATEGY_KEEP_ONLY_LATEST` 가 이걸 막아 주지만 **내가 직접 넣는
   샘플 경로에는 그 보호가 없었다.** → 처리 중이면 제출을 건너뛴다(최신 프레임만 남긴다).

3. **샘플 모드 오버레이가 엉뚱한 배경 위에 그려졌다.**
   `quad` 는 샘플 좌표계인데 배경은 라이브 카메라라, 사각형이 화면을 가로지르는 선으로 나왔다.
   → 샘플 모드에서 **네이티브에 실제로 넣는 그레이 프레임을 화면에 띄우고**(원본 재디코딩이 아니라
   `gray` 배열에서 되만든다 — 휘도 변환이 틀리면 화면에서 바로 보인다), ImageView 를 `fitCenter`,
   OverlayView 도 같은 fit 계산으로 맞췄다. 이제 번호판 위에 사각형이 제자리에 얹힌다.
   (`kor8-hwamul-band` 에서 사각형이 파란 KOR 띠를 빼고 잡히는 것까지 눈으로 확인된다.)

`ms` 값이 카메라 1~2ms 대에서 장면 이미지 96~222ms 로 크게 뛰는 것도 이 과정에서 드러났다.
실기기 성능 판단에 쓸 수 있는 수치다(에뮬레이터 기준이라는 점은 감안해야 한다).

### cpp-engineer 에게 남은 것 (REQ-0003)

내 쪽은 인식기가 들어오는 즉시 값이 뜨도록 준비돼 있다. `quad` 렌더링·`reason` 표시·`sharp`/`motion`
계기판·설정 주입까지 전부 실측으로 살아 있는 것을 확인했으므로, 네이티브가 `value` 를 채우기만 하면
화면·오버레이·전송이 그대로 동작한다. **추가로 요청할 것은 없다.**

참고가 될 만한 관측 두 가지(요청이 아니라 정보다):

1. `gate_bypass=true` 로 게이트를 끄고 에뮬레이터의 가상 거실 장면을 비추면 `plate=true` 가 뜨고
   `reason=segment_fail`, `format=old` 가 나온다. 책장 모서리를 번호판 후보로 잡는 오검출이다.
   `min_plate_width_ratio` 가 후보를 거르지 못하는 각도가 있다는 뜻일 수 있다.
2. 샘플 이미지는 번호판이 프레임을 꽉 채운다(예: 253×56 전체가 번호판). 실제 카메라 프레임처럼
   여백이 없으므로, 테두리에 닿은 성분을 제거하는 §9.4 규칙이 번호판 자체를 지울 가능성이 있다.
   루트가 만든 세트라 내가 바꾸지 않았다 — 필요하면 루트에게 여백 있는 변형을 요청하라.

인식기가 들어왔고 재검증까지 끝냈다. 남은 2장은 REQ-0012 로 넘겼다.

### 아직 확인하지 못한 것 (닫으면서 명확히 남긴다)

- **실기기 프리뷰·흔들림 게이트** (계약 §11 의 4번) — 폰이 있어야 한다. 사용자와 함께 확인할 항목.
- **TCP 실제 전송 + 한글 UTF-8 수신** (계약 §11 의 5번) — socket-engineer 의 테스트 서버가 필요하다.
  전송부는 코드상으로만 확인했고, 특히 **재접속 시 잔여 1건 폐기 수정은 실측하지 못했다.**
- 기기에 남은 옛 `server.properties`(포트 5000) 문제는 위에 적은 대로 사용자 안내가 필요하다.

### 처리 완료 · android-engineer · 2026-08-13T22:11:30+0900

완료기준 1·2·3·4 실측 통과 · 샘플 26장 정답표 대조 24/26 PASS(실패 2장은 REQ-0012 로 이관) · 개정4 반영 · 검증 중 내 버그 3개(세대 오라벨·백프레셔 없음·오버레이 좌표계) 수정 · 실기기와 TCP 실전송은 미확인

