# 네이티브 산출물의 출처 — `릴리즈/android`

> ### 🔑 **바이너리는 출처를 안 갖고 다닌다.** 여기 안 적으면 다음 사람이 재현을 못 한다.

## 무엇이 들어 있나

| 파일 | 크기 | SHA-256 (앞 16자) |
|---|---|---|
| `app/src/main/jniLibs/arm64-v8a/libdigitcam.so` | 13,878,528 B | `f0cc394bbd6ee8b6` |
| `app/src/main/assets/models/ppocr_det_v4.onnx` | 4,745,517 B | (아래 명령으로 확인) |
| `app/src/main/assets/models/korean_rec_static.onnx` | 3,216,845 B | |
| `app/src/main/assets/models/korean_dict.txt` | 14,480 B | |

```bash
shasum -a 256 app/src/main/jniLibs/*/*.so app/src/main/assets/models/*
```

## 누가 · 언제 · 무엇에서 만들었나

```
만든 이   : android-engineer (이 저장소의 android 빌드 파이프라인)
만든 때   : 2026-08-27
원본 소스 : `android/app/src/main/cpp/**`  (소유자: cpp-engineer)
원본 모델 : `cpp/digitcam/models/`          (소유자: cpp-engineer)
빌드 커밋 : `8f397ffc`  (android 트리 기준. 네이티브 소스가 그 시점 상태였다)
```

### 만든 명령 — **이 폴더가 아니라 원본 저장소에서**

```bash
cd <저장소>/android
ANDROID_HOME=$HOME/Library/Android/sdk ./gradlew assembleDebug
# → app/build/intermediates/stripped_native_libs/debug/stripDebugDebugSymbols/out/lib/arm64-v8a/libdigitcam.so
```

### 그 빌드에 필요했던 것 — **여기서는 필요 없다**

```
NDK        29.0.14206865
CMake      4.1.2
OpenCV SDK ~/opencv-android-sdk/OpenCV-android-sdk/sdk   ← 🔴 **저장소 밖의 기계 로컬 SDK**
```
> ### ★ 이것이 이 릴리즈가 `.so` 를 싣는 진짜 이유다.
> **소스를 옮겨 놓아도 OpenCV SDK 가 없으면 못 만든다.** 산출물을 실으면 그 의존이 통째로 사라진다.
🔵 OpenCV 는 **정적 링크**돼 있다(`OpenCV_SHARED=OFF`) — 그래서 `libopencv_java4.so` 가 따로 필요 없다.
그 24MB 가 `libdigitcam.so` 안에 들어 있는 것이고, 13.8MB 라는 크기가 그 결과다.

---

## 🔴 ABI — **`arm64-v8a` 하나만 싣는다**

```
✅ arm64-v8a  실제 배포 대상(SM-N971N · 2026-08-27 설치 확인)
❌ x86_64     **뺐다**
```

### 뺀 이유 — 없어서가 아니라 **출처를 못 대서**다

원본 저장소를 실측했더니:
```
app/build/intermediates/cxx/…/obj/x86_64/libdigitcam.so                 78,633,080 B  (안 스트립)
app/build/intermediates/merged_native_libs/…/lib/x86_64/libdigitcam.so  🔴 **없다**
app/build/intermediates/stripped_native_libs/…/lib/x86_64/libdigitcam.so 🔴 **없다**
그런데 지금 APK 안에는                                                  47,007,304 B  **있다**
```
> ### 🔴 **merge·strip 단계에 없는 것이 APK 에는 있다.** 어느 빌드에서 왔는지 값으로 못 댄다.
> ### ★ **출처를 못 대는 바이너리를 릴리즈에 넣지 않는다.** 그게 이 파일의 존재 이유다.

⚠ **x86_64 가 필요해지면**(에뮬레이터로 돌릴 때) 원본 저장소에서 새로 빌드해 넣고 **여기에 그 커밋을 적어라.**
🔑 다시 빌드하면 되는 것이지 지금 것을 옮겨 오는 것이 아니다 — 옮기면 같은 문제가 따라온다.

---

## 대조 — 이 릴리즈로 만든 APK vs 2026-08-27 폰에 설치한 APK

```
릴리즈 APK  a04d54bf76df9399…   63,429,176 B   549 항목
설치한 APK  d9971766ec20d0d3…  110,823,786 B   552 항목
```
🔴 **전체 해시가 다르다. 그 이유가 이것 하나다** — `x86_64` 항목 **셋**(`libdigitcam`·`libimage_processing_util_jni`·`libsurface_util_jni`).

### ✅ 공통 항목은 **바이트 단위로 같다**(실측)
```
classes.dex                        ✅  aa5392ba1fc6
classes3.dex                       ✅  00359e27f97b
assets/models/ppocr_det_v4.onnx    ✅  d2a7720d45a5
assets/server.properties           ✅  428b7319ea75
lib/arm64-v8a/libdigitcam.so       ✅  f0cc394bbd6e
```
> ### 🔑 **"같은 소스면 같은 해시" 는 *같은 것을 담을 때* 만 참이다.**
> 담는 목록을 바꿨으니 전체 해시는 당연히 다르다. **다른 이유를 값으로 댈 수 있으면 그것으로 족하다.**

⚠ 그리고 **전체 해시가 같아질 수는 없다**: 무엇을 담을지가 다르고, 담더라도 압축·항목 순서가
빌드 환경에 따라 갈릴 수 있다. **항목별 대조가 더 강한 검증이다** — 무엇이 어떻게 다른지까지 말해 준다.
