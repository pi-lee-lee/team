# 릴리즈 — 안드로이드 앱 (digitcam)

> **이 폴더만으로 빌드된다.** 저장소의 다른 폴더를 안 본다 — 아래에서 값으로 확인했다.

## 📌 이 폴더가 **2026-08-27 픽스 판과 같은 판인가** — 값으로

폰에 설치된 것은 `app-debug.apk` **SHA-256 `d9971766ec20d0d3…e56650c3`** 다.
이 폴더로 빌드하면 `a04d54bf76df9399…` 가 나온다. **전체 해시는 다르다.** 그 차이를 전수로 갈랐다:

```
항목 수      릴리즈 549  ·  설치본 552
공통 549개 중 **내용이 다른 것 : 0**        ← 🔵 전수 대조(zip 항목마다 SHA-256)
릴리즈에만 : 0
설치본에만 : 3  →  lib/x86_64/libdigitcam.so · libimage_processing_util_jni.so · libsurface_util_jni.so
```
> ### ★ **폰에서 도는 것(arm64)은 한 바이트도 다르지 않다.** 차이는 `x86_64` 셋뿐이고,
> ### 그건 **에뮬레이터용**이라 실기 동작과 무관하다. 뺀 이유는 `libs/PROVENANCE.md`(출처를 못 댄다).

🔵 **네이티브 라이브러리도 그 판이 맞다** — `lib/arm64-v8a/libdigitcam.so` 가 양쪽 모두
`f0cc394bbd6e…`(동일). `PROVENANCE.md` 의 커밋 `8f397ffc` 에서 나온 그것이다.

⚠ **전체 해시는 같아질 수 없다** — 담는 목록이 다르다.
🔑 그리고 **항목별 대조가 더 강하다**: 전체 해시는 *"다르다"* 만 말하지만 이건 **무엇이 왜** 다른지 말한다.

## 필요한 것

```
✅ Android SDK (platform 35 · build-tools)
✅ JDK 17
❌ NDK        — 필요 없다
❌ CMake      — 필요 없다
❌ OpenCV SDK — 필요 없다   ← 🔵 인식 라이브러리를 **빌드된 `.so` 로** 싣기 때문이다
```
🔑 그 셋이 필요 없는 것이 이 릴리즈의 요점이다. 자세한 것은 `libs/PROVENANCE.md`.

## 빌드

```bash
cp local.properties.example local.properties
# local.properties 의 sdk.dir 을 **이 기계의 경로**로 고친다
#   (또는 환경변수 ANDROID_HOME 을 잡으면 이 파일이 없어도 된다)

./gradlew assembleDebug
# → app/build/outputs/apk/debug/app-debug.apk   (약 60 MB)
```

## 설치

```bash
<sdk>/platform-tools/adb install -r app/build/outputs/apk/debug/app-debug.apk
```
🔴 **`-r`(덮어쓰기)를 써라.** 지우고 새로 깔면 폰의 설정과 촬영 기록이 같이 지워진다.
⚠ 2026-08-27 에 `-r` 를 줬는데도 **데이터가 지워진 일이 있었다**(USB 가 끊겨 설치가 한 번 실패한 뒤).
🔑 그래서 **설치 전에 폰의 자료를 먼저 꺼내라** — 절차는 `docs/android/DEPLOY-2026-08-28.md` 0단계.

## 🔴 첫 실행에 반드시 할 것 — 순서대로

### ① 카메라 권한
앱을 처음 켜면 권한을 묻는다. **허용**을 눌러야 한다.
안 하면 서버 요청에 `no_camera_permission` 으로 답하고 **기다려도 안 풀린다**(사람이 눌러야 한다).
```bash
# 화면을 못 만질 때는 이걸로도 된다
<sdk>/platform-tools/adb shell pm grant com.example.digitcam android.permission.CAMERA
```

### ② 🔴 서버 주소·포트를 넣는다 — **번들 기본값으로는 안 붙는다**

```
번들 기본값 : server.host=**(비어 있다)** · server.port=8911
2026-08-27 현장값 : server.host=**192.168.0.29** · server.port=**5500**
```
> ### 🔴 `server.host` 가 비면 **전송을 안 한다.** 화면에 `서버 미설정` 이 뜬다.
> ### ⚠ 그리고 포트가 **둘**이다 — 번들은 `8911`(주차장 서버 PORT_PHONE), 현장은 `5500`.
> ### **어느 쪽이 맞는지는 서버 담당(socket)에게 확인해라.** 여기 적은 것은 *그날 실제로 붙어 있던 값*이다.

**바꾸는 법 — 둘 중 하나**
```
[가] 앱 화면 → [설정] → 서버 주소·포트 입력 → [저장]     ← 폰만으로 된다. 권장
[나] 파일을 꺼내서 고치고 다시 넣는다:
     adb pull  /sdcard/Android/data/com.example.digitcam/files/server.properties ./sp
     (편집)
     adb push  ./sp /sdcard/Android/data/com.example.digitcam/files/server.properties
     → 앱 화면의 [설정 다시 읽기]
```
⚠ 폰에서 `adb shell sed -i` 로 고치지 마라 — `/sdcard` 는 기기마다 파일 시스템이 달라
**조용히 실패**할 수 있고, 오류가 안 나서 *"고쳤는데 그대로네"* 가 된다.

### ③ 붙었는지 확인
```bash
<sdk>/platform-tools/adb logcat -d -s AppConfig:I TcpSender:I | tail -20
```
```
✅ `server.host=192.168.0.29  (외부)`      ← 폰 파일의 값이 쓰이고 있다
✅ `PING 수신 시작 seq=N`                   ← 🔵 **서버에 실제로 붙었다는 유일한 증거**
🔴 `server.host=  (외부)` 이고 PING 이 없다 → ②를 안 한 것이다
```

## 설정이 어디서 오나 — **폰 파일이 이긴다**

앱은 폰의 `/sdcard/Android/data/com.example.digitcam/files/server.properties` 를 읽는다.
**처음 실행하면 `app/src/main/assets/server.properties` 가 그리로 복사된다.**

```
🔴 그 뒤로는 **폰의 복사본이 이긴다.** assets 를 고치고 다시 깔아도 안 먹는다
   ★ 2026-08-26 에 이걸로 하루를 썼다 — assets 를 고쳤는데 폰의 옛 값이 계속 쓰였다
🔑 다만 **키 단위**다 — 폰 파일에 **없는 키만** assets 가 채운다(한 실행 로그에 둘이 섞여 찍힌다)
✅ 지금 무엇이 쓰이는지는 로그로 확인한다: `adb logcat -d -s AppConfig`
   각 키 옆에 `(외부)` 인지 `(assets)` 인지 찍힌다
⚠ **폐기된 키가 폰에 남아 있으면 경고가 뜬다**(예: `camera.shot_timeout_ms`).
   그건 고장이 아니라 **새 판이 깔렸다는 증거**다 — 지우려면 위 [나] 로 지운다
```

## 이 폴더가 자립한다는 것을 어떻게 확인했나 — **값으로**

```
① 저장소 **밖**(`/tmp`)으로 복사해서 빌드 → ✅ BUILD SUCCESSFUL
   ★ 같은 저장소 안에서 빌드하면 상대 경로가 우연히 맞아 **자립 안 해도 통과한다**
② 그 빌드에 CMake·NDK·OpenCV 작업이 **하나도 안 돌았다**
③ APK 안의 `libdigitcam.so` 가 이 폴더의 것과 **바이트 단위로 같다**
   f0cc394bbd6e… (양쪽 동일)
④ 🔴 **빨간불** — `.so` 와 `models/` 를 **처음부터 뺀 사본**을 빌드했더니
   APK 에 그 항목들이 **없다** → 다른 데서 오는 것이 아니다
```

### ⚠ 그런데 ④에서 **빌드는 성공한다** — 그 사실을 감추지 않는다

```
🔴 안드로이드는 네이티브 라이브러리가 없어도 **빌드가 안 깨진다.**
   깨지는 곳은 **실행 시점**이다 — `UnsatisfiedLinkError`
```
> ### ★ 그러니 *"빼면 빌드가 실패한다"* 는 안드로이드에서 **성립하지 않는 판별자**다.
> ### 🔑 옳은 판별자는 **"그 항목이 APK 에서 사라지는가"** 이고, ④가 그것이다.

## 알아 둘 것

```
· ABI 는 **arm64-v8a 하나**다. 이유는 `libs/PROVENANCE.md` (x86_64 를 뺀 것은 출처를 못 대서다)
· 그래서 이 APK 는 **x86_64 에뮬레이터에서 안 돈다**. 실기기(arm64)용이다
· 단위 시험 : `./gradlew testDebugUnitTest` — **138개 · 실패 0**(이 폴더 사본에서 실측). 네이티브 없이 돈다
```
