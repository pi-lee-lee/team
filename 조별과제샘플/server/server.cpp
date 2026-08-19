// 주차 관제 서버 — 명세: docs/net/parking-protocol.md (v1, 얼림)
//
// 한 프로세스가 리스너 둘을 연다:
//   TCP  9991 — 아두이노. LF 종단 압축 ASCII 라인 (명세 §2)
//   HTTP 9900 — index.html / data_log.json 정적 서빙 + GET /ws 를 WebSocket 으로 업그레이드 (§5)
//
// 외부 라이브러리를 쓰지 않는다. SHA-1 · base64 · WebSocket 프레이밍을 직접 구현했다.
//
// 빌드
//   macOS/Linux : c++ -std=c++11 -O2 -o server server.cpp
//   Windows     : cl /EHsc /std:c++14 /utf-8 server.cpp ws2_32.lib
//                 ^^^^^^^ **/utf-8 을 빼지 마라.**
//
//   이 파일은 BOM 없는 UTF-8 이고 안내 문구가 한글이다("예약되었습니다" 등).
//   BOM 이 없으면 MSVC 는 소스를 시스템 ANSI 코드페이지(한국어 윈도우면 CP949)로 읽어
//   그 문자열을 깨뜨린다. **컴파일은 그대로 통과한다** — 그래서 빌드 로그로는 절대 못 잡고
//   브라우저 화면에서 처음 보인다. BOM 을 넣어 해결하지 않는 이유는 POSIX 도구들이
//   BOM 을 성가셔하기 때문이다. 플래그로 해결하는 것이 맞다.
//
// 실행
//   ./server                  # 아두이노 TCP 9991 · HTTP/WS 9900
//   ./server --selftest       # SHA-1+base64 와 라인 체크섬 자가검증 (RFC 6455 §1.3 벡터)
//
// 하드웨어 없이 시험하려면 (명세 §10):
//   python3 ../net/fake_arduino.py                      # 가짜 아두이노
//   python3 ../net/ws_probe.py --listen 5               # 브라우저 대신 WS 관찰
//
// ⚠ 이 파일을 고칠 때 알아둘 것: now_ms() 와 epoch_ms() 는 **원점이 다르다**(각 함수 주석 참조).
//   POSIX 에서는 우연히 둘 다 Unix epoch 라 섞어 써도 티가 안 나지만, 윈도우에서는
//   now_ms() 가 부팅 후 경과 시간이라 섞는 순간 수십 년짜리 값이 나온다.
//
// 왜 크로스 플랫폼인가: 원본은 Winsock 전용이라 이 팀의 macOS 에서 빌드조차 되지 않았다.
// 빌드도 못 하는 코드는 검증할 수 없고, 검증할 수 없으면 "구현했다"고 말할 수 없다.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
// <ctime> 를 직접 넣는다. POSIX 에서는 아래 <sys/time.h> 가 우연히 끌어와서 통과했지만
// 그 include 는 #else 안이라 Windows 에는 없다 — 즉 macOS 빌드 성공은 우연이었다.
#include <ctime>
#include <csignal>      // sig_atomic_t / SIGINT — 윈도우에도 있어야 하므로 공통 블록에 둔다
#include <string>
#include <vector>
#include <algorithm>    // 복구시간 중앙값(REQ-0072) — nth_element
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

// ---------------------------------------------------------------- 플랫폼 어댑터
#ifdef _WIN32
  // 이 두 define 은 반드시 <winsock2.h> **앞**에 와야 한다. 뒤에 두면 아무 효과가 없다.
  //  · FD_SETSIZE: 윈도우 기본값이 64 다. FD_SET 은 넘치면 **조용히 무시**하므로
  //    브라우저가 여러 대 붙었을 때 원인 없는 무응답으로 보인다. 비용은 fd_set 크기뿐이다.
  //  · NOMINMAX: <windows.h> 의 min/max 매크로가 표준 함수와 충돌하는 것을 막는다.
  //    지금은 std::min/max 를 안 쓰지만 나중에 누가 쓰면 그때 깨진다. 비용 0 의 예방책이다.
  #define FD_SETSIZE 256
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <direct.h>     // _mkdir — 로그 디렉터리 생성 (REQ-0111 로그 계약)
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define BAD_SOCK INVALID_SOCKET
  static void closesock(sock_t s) { closesocket(s); }
  static int  sockerr() { return WSAGetLastError(); }
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <sys/stat.h>   // mkdir — 로그 디렉터리 생성 (REQ-0111 로그 계약)
  #include <sys/file.h>   // flock — 한 로그 파일에 두 인스턴스가 붙는 것을 막는다
  #include <fcntl.h>      // open  — 위 잠금용 fd
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
  #include <signal.h>
  #ifdef __APPLE__
    #include <mach-o/dyld.h>   // _NSGetExecutablePath — 경계 줄의 bin= 필드
  #endif
  typedef int sock_t;
  #define BAD_SOCK (-1)
  static void closesock(sock_t s) { close(s); }
  static int  sockerr() { return errno; }
#endif

// ---------------------------------------------------------------- 상수 (명세대로)
static const int  PORT_ARDUINO   = 9991;
static const int  PORT_HTTP      = 9900;
static const int  PORT_PHONE     = 5500;    // digitcam 폰 수신 (docs/net/digitcam-protocol.md)
// 폰 프레임은 JSON 이라 아두이노 라인(64B)보다 길다. digitcam 명세 §9 가 권하는 방어적 상한.
static const size_t MAX_PHONE_LINE = 1024;
static const size_t MAX_PLATE_BYTES = 32;   // 번호판 저장 상한(신형 10B, 여유 포함)
// §2.1 — 명세값은 64 다. **명세를 바꾸는 것이 아니라 계측기를 넓히는 수단**으로
// `--max-line N` 을 둔다(시험 인스턴스 전용).
//
// 🔴 왜 필요한가: `AT+CIPSEND` 의 실제 최대 길이를 재려면 장치가 64B 를 넘겨 보내야 하는데,
// 서버가 그것을 **체크섬 검사 전에** `과길이` 로 버리면 **AT 잘림이 아니라 내 상수를 재게 된다.**
// 그리고 `ard_buf` 를 도착 도중에 비우므로 **몇 바이트까지 왔는지조차 안 남는다** —
// 그게 정확히 그 시험이 재려는 값이다.
static int        MAX_LINE       = 64;      // §2.1 (기본값. --max-line 으로만 바뀐다)
// 🔴 `ACK_TIMEOUT_MS` 는 여기 없다 — `DOWN_SLOT_MS` 에서 유도하므로 그 아래에 있다(§7.3).
static const int  ACK_MAX_TRIES  = 3;       // 최초 + 재전송 2회
static const int  OFFLINE_MS     = 3500;    // §3.4
static const int  SELECT_TICK_MS = 200;     // 타이머를 돌리기 위한 select 최대 대기

// ── 하행 슬롯(반송파) 상수 — `docs/net/DESIGN-server-slot-queue.md`
//
// 🔑 **이벤트가 전송을 만들지 않는다.** 명령은 큐에 담기고 **장치 프레임이 도착한 순간**
// (= 내 창의 시작) 큐 전부가 **한 거래로 묶여** 나간다. 창 시작은 시계가 아니라 사건이므로
// 장치가 느려지면 내 창도 같이 밀린다 — **자기교정된다**(원장 §8.19).
static const int  DOWN_SLOT_MS       = 1200;  // 슬롯 주기(사용자 확정: 1.2초)
static const int  DOWN_WIN_MS        = 600;   // 내 창의 이론 폭(0.6초). RTT 를 여기서 깎는다
static const int  DOWN_WIN_MARGIN_MS = 50;    // 여유 — 내 처리시간·타이머 해상도
static const int  DOWN_WIN_MIN_MS    = 150;   // W_srv 하한. RTT 가 튀어도 창이 0 이 되면 안 된다
// `S` 가 안 오면 창이 영영 안 열린다 → 포기하고 쏜다(설계 §3).
// 🔴 **`OFFLINE_MS`(3500)보다 작아야 한다** — 그 뒤에 쏘면 서버가 이미 "장치 없음"으로
// 판단한 상태에 쏘는 것이다. 2슬롯 = 2400 은 그 조건을 만족한다.
static const int  DOWN_DMAX_MS       = 2 * DOWN_SLOT_MS;
// 한 줄 상한. `RX_CAP=96`(client.ino:368) 안에 `"+IPD,<n>:"` 접두(8~9B)가 **포함된다**.
// **배치 전체가 아니라 줄 하나의 제약**이다.
static const int  DOWN_LINE_MAX_B    = 87;

// 🔴🔴 **이 값은 "무엇에 의존하는가"를 같이 읽어야 한다.** 원장 `docs/net/LEDGER.md` §8.23.
//
// ⚠ 초안은 `576 = 600ms × 960 B/s`(9600 baud **가정**)였다. **계산값이었고 임시였다.**
// arduino 4판이 **실측**을 냈다(`docs/arduino/MEASURE-2026-08-17-rxmax-bytes.md`):
//   **창(0.6초)당 540 바이트** · 물리 상한 576B 의 94% ·
//   🔑 프레임 크기를 3.4배 바꾸니 **건수는 9~32 로 갈렸고 바이트는 ±1%** — 불변량이 바이트임이 관측됐다.
//
// **그래도 540 을 박지 않는다.** 실측에서 빼야 할 것이 셋이고, 하나는 정량화가 안 됐다:
//   ① `SoftwareSerial` — 이 측정은 **하드웨어 UART(맥 직결)** 다. 실기는 `cli()` 가 남는다.
//      **540 은 상한이지 보장이 아니다.** 페널티는 **아직 안 쟀다** → 그 미지분을 여유로 남긴다.
//   ② `+IPD,<n>:` 접두 9B — ESP 가 **TCP 조각마다** 붙인다(배치가 이걸 아낀다).
//   ③ 한 줄 상한 87B(`RX_CAP=96` − 접두) — 배치 전체가 아니라 **줄 하나**의 제약(위 상수).
//
// 🔴🔴 **`320` 에서 `192` 로 내렸다** — 시간 축을 계산하니 320 이 최악에서 겹친다(arduino 검산).
//
// **바이트가 아니라 "장치 UART 를 몇 ms 붙잡는가"가 진짜 제약이다:**
// ```
//   장치 송신 종료(최악)  = slotStart + due_max(400) + txTime_max(468)  = slotStart + 868
//        ⚠ txTime_max 는 185 가 아니다 — `waitForPrompt` 최대 300ms + 더미 채움 168ms 가
//          붙는 주기가 있다(client.ino:106). 185 는 "프롬프트가 3ms 에 온다"를 전제한 값이었다.
//   다음 장치 송신        = slotStart + 1200
//   → 내가 쓸 수 있는 시간 = 332ms − d1        (d1 = 장치→서버 망 지연, **미측정**)
//
//   320B 를 흘리는 시간   = 320 ÷ 960 B/s + `+IPD` 접두 9B = 342ms   → 🔴 **이미 넘는다**
//   192B 를 흘리는 시간   = 192 ÷ 960 B/s + 9B             = 209ms   → ✅ d1 에 123ms 여유
// ```
// 🔑 **정상 경로(txTime 185)에서는 615ms 가 있어 320 도 넉넉했다.**
// **최악은 "프롬프트를 놓친 주기"이고, 그 주기가 하필 배치가 큰 주기와 겹칠 수 있다.**
// **`resync` 계수기가 오르는 구간이 그것이다** — 그 구간의 겹침은 이 계산 밖이었다.
//
// ⚠ **의존성 셋을 같이 읽어라. 하나라도 바뀌면 다시 계산한다:**
//   ① `960 B/s`(9600 baud 가정)  ② arduino 의 `txTime_max`  ③ 🔴 **`d1 + d2`(망 왕복) — 미측정**
//   → ③ 은 **내 요약의 `왕복 최대 Nms(n=K)`** 가 창 B 에서 처음 값을 낸다(하행 송신→ACK 도착).
//     **그 값이 나오기 전까지 여유를 "확보됐다"고 말하지 않는다.**
//
// 192B 는 여전히 장치 64B 링버퍼의 **3배**라 `ssovf` 시험에 충분하고, 명령 28B 기준 **약 6~7건**을
// 한 거래에 싣는다. 넘치면 버리지 않고 **다음 창으로 미룬다** — 보수적으로 잡는 비용이 낮다.
//
// 🔴 **이 값은 "회선의 한계"가 아니다. "이론 최악을 덮는 값"이다** (arduino 검산):
// ```
//   이론 최악 : due 400 + txTime 468 = 868  → 여유 332ms → 192B(209ms) 를 고른 근거
//   실측      : due   0 + txTime  48 =  48  → 여유 1152ms → 192B 는 그 **18%**
// ```
// **약 20배 보수적이다.** 안전 설계를 최악으로 하는 것은 맞지만, **나중에 처리량이 부족해질 때
// 근거를 갖고 다시 볼 수 있어야 한다.** 그 문을 여는 조건 셋을 미리 정해 둔다:
//   ① `[SLOT] due=` 분포에서 400 근처가 안 나온다 → `due` 항을 줄일 근거
//   ② `resync` 계수가 0 에 가깝다 → `txTime=468`(프롬프트 늦게 받은 주기)도 과대라는 근거
//   ③ 내 요약 `왕복 최대` 의 실기 첫 값 → `d1+d2` 상한이 확정된다
// **셋이 다 나오면 재계산한다. 그 전에는 이 값으로 간다.**
// → `--down-cap=<B>` 로 재빌드 없이 갈아끼운다.
static int        DOWN_BATCH_CAP_B   = 192;
// 큐 깊이는 **독립 상수가 아니라 유도값이다** — "마감 안에 나갈 수 있는 양".
//   깊이(B) = cap(B/창) × (마감 ÷ 슬롯주기) = 576 × 4 = 2304
// 그래서 `cap` 이 바뀌면 깊이가 저절로 따라온다. 두 값이 따로 놀면 마감을 넘긴 항목이
// 큐에 남는데 아무도 그걸 설명 못 한다.
static const int  DOWNQ_WAIT_CAP_MS  = 4 * DOWN_SLOT_MS;   // 4.8초 — 큐 대기 마감(임시)
// 🔴 착지 위상을 **의도적으로 겨냥하는 시험**을 위해 옛 거동(즉시 송신)을 남긴다.
// 원장 §8.17 이 아직 못 갈랐다고 적어 둔 물음("`R`/`C` 라서 안전한가, 착지가 좋았던 것인가")은
// 하행을 원하는 순간에 쏠 수단이 없으면 **영영 못 가른다.** 기본값은 off 다.
static bool       DOWN_IMMEDIATE     = false;
// ── 🔴 건수 상한 — **바이트와 다른 축이다** (REQ-0206/0207 · 2026-08-18)
//
// **바이트 상한만으로는 못 막는다**: 명령이 작을수록 헐거워진다.
//   시뮬 9B → 192B 가 **21건**을 통과시킨다 · 예약 28B → 6건에서 걸린다
//   🔴 **가장 작은 명령이 가장 위험한데, 사용자가 연타한 것이 정확히 그것이었다.**
//
// **왜 건수인가**: 회선은 바이트를 나르지만 **장치는 건당 ACK 하나를 만든다.**
// **같은 시스템에 단위가 다른 제약이 둘 있다** — 하나를 부정하며 다른 하나를 같이 지웠던 것을 되돌린다.
//
// **유도**(arduino 확정): 장치의 슬롯당 ACK 배출 **설계 보장 6**
//   (실측 8 은 `S` 가 짧고 rid 3자리일 때만 · `tmask` 나 5자리 rid 면 6으로 떨어진다 → 설계값은 6)
// ```
//   유입 N · 배출 D. 한 슬롯을 놓치면 적체 2N →  다음 슬롯 2N−D  →  그다음 3N−2D
//   2슬롯에 회복하려면  3N − 2D ≤ 0  →  **N ≤ 2D/3**  →  D=6 이면 **4**
// ```
// 🔴 **유입 = 배출이면 한 번만 밀려도 영구히 못 따라잡는다**(ρ=1). 그래서 여유가 필수다.
// ⚠ **실측이 이 값을 더 강하게 만든다**: 연타 구간 `ack=` 분포가 `{0:133, 3:1, 8:7}` 이고
//   8짜리 대부분이 미전송이었다 — **부하에서 배출률이 6 밑으로 떨어진다. 4 를 올리지 마라.**
static const int  DEV_ACK_DRAIN_PER_SLOT = 6;   // arduino 확정 · REQ-0206
// ⚠ monitor 실측 8 은 **조건부다**(S 가 짧고 rid 3자리일 때만). tmask 가 붙거나
//    rid 5자리면 6 으로 떨어진다 → **설계 보장은 6.** 실측을 설계값으로 승격시키지 않는다.
static const int  DOWN_BATCH_MAX_N = (2 * DEV_ACK_DRAIN_PER_SLOT) / 3; // = 4 · 리터럴 아님

// 🔴🔴 **`6` 은 상수가 아니라 함수다** (arduino 정정 2026-08-18)
//   배출 = (BATCH_CAP − S_worst − 1) ÷ (ACK_worst + 1) = (160 − 60 − 1) ÷ 16 = 6
//   세 값에 의존한다: `BATCH_CAP`(장치가 정한 160) · **`S_worst`** · `ACK_worst`(rid 자릿수).
//   ⚠ **`S_worst` 가 모듈 유동화 개정에서 커진다** — 자리 비트열이 지금은 10칸 고정이지만
//     개정 후에는 `n` 가변이고 전선 상한이 `n ≤ 15` 다:
//        n=10 → S_worst 60B → 배출 **6**      n=15 → S_worst 75B → 배출 **5**
//   🔴 그러면 유입 4 가 조용히 1건 초과가 된다. **그 순간을 서버가 알아야 한다.**
//
// 🔑 **그래서 이 값을 서버가 다시 계산하지 않는다.** `BATCH_CAP`·`ACK_worst` 는 arduino 의 값이고
//   **파생값은 원본을 가진 쪽이 계산한다**(CLAUDE.md). 사본을 두면 같은 규칙이 두 곳에 생기고
//   갈리는 순간 어느 쪽이 맞는지 알 수 없다.
//   → **최종형은 장치가 등록(`D`)에서 자기 배출률을 선언하는 것**이다(설계 개정에 넣는다).
//   → **그때까지는 전제가 깨진 것을 감지만 한다.** 서버가 실제로 볼 수 있는 것은 `S` 의 길이뿐이고,
//     그것이면 충분하다 — **`S_worst` 가 이 가정을 넘으면 배출 6 의 전제가 깨진 것**이다.
static const int  DEV_S_WORST_ASSUMED_B = 60;   // n=10 기준 · arduino REQ-0206

// §5 등록 — **상수가 아니라 유도값이다.** 슬롯 주기가 바뀌면 따라 움직인다.
// 접속 → 자기 송신 창까지 최대 1슬롯 · `S`(승격) 1슬롯 · `D` 1슬롯 = 3슬롯이 최소 경로다.
static const int  REG_TIMEOUT_MS = 3 * DOWN_SLOT_MS;   // = 3600ms
static const int  REG_Q_MAX      = 3;                  // `Q` 상한 — **비용이 서버에 있어서 서버가 둔다**
static const int  GETMAP_MAX_PER_SEC = 5;   // 화면 하나가 1초에 이보다 자주 물으면 뭔가 잘못된 것이다
static const int  REG_MODS_MAX   = 32;                 // 방어적 상한. 전선 예산은 장치가 더 낮게 건다

// 🔴🔴 **송신 시점 문턱 — 증거가 있는 창과 없는 창을 다르게 취급한다** (REQ-0210)
// `S` 가 도착해서 여는 창에는 **증거가 있다**: 장치가 방금 말했고 지금 수신 구간이다.
// **창 포기는 증거가 없다** — `S` 가 안 왔다는 것 말고 아는 것이 없다. 그런데 지금까지
// **같은 양(4건)을 같은 확신으로 쏘고 있었다.**
//
// 증거 없이 보내는 양은 **판정에 필요한 최소 표본**이다. 링크가 사는지 죽었는지는
// **한 건이면 답이 나온다**(ACK 이 오면 다음 창이 정상으로 열린다). 더 보내도 정보는
// 안 늘고 **노출만 배로 는다.**
// ⚠ **이 값은 유도값이 아니라 정의다.** REQ-0210 이 "유도 못 한 것은 그렇게 적어라"고
//   요구했으므로 분명히 해 둔다 — *최소 표본 = 1* 은 계산 결과가 아니라 탐침의 뜻 자체다.
static const int  DOWN_PROBE_N = 1;

// 🔴🔴 **자리 인수 유예 — 다른 IP 가 조용한 자리를 가져가도 되는 시간** (REQ-0217)
// ⚠ **루트가 제안한 판별식("최근 프레임이 있으면 침입자")은 실측으로 반증된다.**
//   서버 로그의 정상 재접속 85건에서 **공백 최소가 `0초`**다(0·1·2·2·3·4…).
//   장치가 TCP 를 다시 세울 때 **옛 소켓의 마지막 프레임 직후에 새 연결이 올 수 있다.**
//   → **공백만으로 갈랐다면 우리 보드의 정상 재접속을 7건 이상 막았다.**
//
// 🔑 **그래서 1차 판별자는 시간이 아니라 IP 다.** `devid` 가 고유하지 않은 것이 문제의 뿌리이고,
//   그때 남는 유일한 구분자가 커널이 주는 주소다.
//     같은 IP → 같은 장치의 TCP 재접속 → **공백과 무관하게 교체 허용**
//     다른 IP → 두 대다 → 아래 유예로 판정
// 이 값은 **다른 IP 일 때만** 쓰이므로 우리 재접속을 막지 않는다. 그래서 크게 잡는 쪽이 안전하다.
//   하한: 정상 주기(1슬롯)보다 충분히 커야 한다
//   상한: 장치의 자체 재접속(T2 약 8초)보다 작아야 한다 — 안 그러면 자리를 못 비켜 준다
// 🔴🔴 **이 값은 `IP 가 다를 때만` 쓰인다. 그 종속을 빼고 읽으면 반드시 틀린다.**
//   *"정상 재접속 문턱"* 으로 읽으면 **`0초` 재접속을 막는 값**이 된다(위 실측).
//   같은 IP 의 재접속은 이 상수를 **지나지 않는다** — 공백을 아예 묻지 않는다.
//   ⚠ 그래서 계약 줄에도 `(IP 다를 때만)` 을 붙여 찍는다. 값만 옮겨 적히는 것을 막으려는 것이다.
static const int  TAKEOVER_GRACE_MS = 5 * DOWN_SLOT_MS;   // = 6000ms · 다른 IP 가 조용한 자리를 가져갈 때만


// ── ACK 마감 — 🔴 **값이 아니라 유도식이다** (창 C~E 실측이 입력을 확정했다)
//
// **옛 값 `1500` 은 우회 경로 시절의 값이었다.** `sendAck` 이 슬롯 창을 안 보고 즉시 나가던 때
// 정상 왕복이 ≈135ms 였고 1500 은 그 11배였다. **그 우회를 막자 정상 왕복이 ≈1230ms 가 됐고**
// (창 C 1229 · 창 D 1225 · 창 E 1230 — **세 창 · 두 망 · 두 펌웨어에서 고정**)
// **1500 은 여유가 271ms 뿐인 값이 됐다** → **늦은 ACK 이 상시 재전송을 만들었다**(창 C 에서 16건 중 12건).
// ⚠ **아무도 이 상수를 건드리지 않았는데 틀린 값이 됐다** — 남의 코드가 규율을 지키게 되면서.
//
// 🔴 **`rtt_max` 를 쓰지 않는다.** 창 D 최대는 **1599ms**(중앙 1225)였다 —
// **최대는 이상치를 포함하므로 문턱을 거기 맞추면 실패 감지가 그만큼 느려진다.**
// 그건 이 마감이 하려는 일의 반대다.
//
// **유도**: ACK 은 장치의 **다음 송신 창**을 기다린다 → 한 슬롯.
//          그 창을 **한 번 놓친 경우까지 봐준다** → 한 슬롯 더.
//          → `2 × DOWN_SLOT_MS` = 2400ms. 정상 1230 대비 여유 1170ms.
// 🔑 **리터럴로 두지 않는 이유**: 슬롯 주기가 바뀌면 이 값이 **따라 움직여야 한다.**
// 박아 두면 `DOWN_SLOT_MS` 를 고친 사람이 이 상수를 잊고, 그러면 오늘과 똑같은 일이 다시 난다.
static const int  ACK_TIMEOUT_MS = 2 * DOWN_SLOT_MS;   // §7.3 · 유도값(2400ms)

// ── 🔴 A[1] `rid` 폭 고정과 격리 — 정본은 `docs/net/DESIGN-rid-width-and-quarantine.md`
//
// **왜 폭을 자르나**: `N=3` 의 이득이 `rid` 자릿수에 걸려 있다(원장 §8.23-(58)).
//   rid 3자리 → ACK 13B → N=3 이면 62B ✅ (대역 밖)
//   rid 4자리 → ACK 14B → N=3 이면 65B 🔴 (이득 소멸)
// 단조 증가는 uint16 끝까지 자라므로 **설정을 안 바꿔도 지표가 저절로 움직인다.**
// 그래서 고정하는 것은 "지금 값"이 아니라 **자릿수의 상한**이다.
static const uint16_t RID_SPACE = 1000;    // rid ∈ [0,999] — 최대 3자리
static const uint16_t RID_NONE  = 0xFFFF;  // "발행 못 했다" — RID_SPACE 밖이라 유효값과 안 겹친다

// 🔴 **폭보다 먼저 답해야 하는 것은 재사용 주기다** (arduino `docs/arduino/LEDGER.md` §25.3).
// 장치는 최근 서로 다른 `rid` **16개**를 멱등 캐시에 들고 있고, 그 안에서 값이 재등장하면
// **명령을 적용하지 않고 옛 ACK 을 재전송한다.** 서버는 ACK 을 받으므로 타임아웃도 안 뜬다 —
// **실패가 성공처럼 보인다.** `RID_SPACE=1000` 은 되돌아오기까지 1000건이라 이 창을 넘는다.
// ⚠ **이 값 아래로 내려갈 때는 반드시 이 상수를 같이 봐라. 16 이하면 구조적으로 항상 충돌한다.**
static const int  DEV_RID_CACHE_N = 16;    // arduino CACHE_N (그쪽 소스 판독 · 실기 실측 아님)

// 격리 — 해제된 rid 를 얼마나 묵혔다 재사용하나. **리터럴 금지: 상수에서 유도한다.**
// 근거: 서버가 한 명령을 포기하는 것은 `ACK_MAX_TRIES` 번의 타임아웃을 다 쓴 뒤다.
//       그보다 늦게 오는 ACK 은 **재전송 예산 전체보다 늦은 것**이므로 없는 것으로 다룬다.
// ⚠ **이건 가정이지 관측이 아니다** — 장치가 그보다 늦게 ACK 하지 않는다는 것을 잰 적이 없다.
//   그래서 `ack_unknown_rid` 를 요약에 낸다. **가정이 깨지면 그 칸이 오른다.**
static const long long RID_QUARANTINE_MS = (long long)ACK_TIMEOUT_MS * ACK_MAX_TRIES;  // 7200ms

// 🔴 **(B) 재시작을 건너 단조성을 유지한다** (루트 결정 REQ-0246 · 설계 §4)
//
// **이건 새 위험이 아니라 이미 일어난 고장이다** — arduino §25.3 에 실측 기록이 있다:
//   *서버 재시작으로 `wire_rid` 가 1부터 다시 시작했고, 장치는 재부팅을 안 해서 옛 rid 가
//    캐시에 남아 명령이 삼켜졌다.* 🔴 **그때 서버는 ACK 을 받아 성공으로 기록한다.**
//
// **블록 예약 방식**: 한 번에 `RID_PERSIST_BLOCK` 만큼 앞을 예약해 디스크에 적고, 그 안에서는
// 디스크를 안 만진다. 🔑 **적는 값이 실제 사용보다 항상 *앞서* 있다** — 그래서 갑자기 죽어도
// 재시작이 **방금 쓴 값을 다시 내주지 않는다**(건너뛸 뿐이고 건너뛰는 것은 무해하다).
// ⚠ 반대로 "쓴 뒤에 적는" 방식이면 크래시 때 **가장 최근에 쓴 값들**을 재발행하게 되어
//   장치 캐시와 겹칠 확률이 가장 높은 구간을 정확히 다시 밟는다.
static const long long RID_PERSIST_BLOCK = 256;
static const int  SOAK_REPORT_MS = 60000;   // 소크 관측 주기 보고(REQ-0065)
static const int  LOG_KEEP       = 2;       // §9.1 최신 2건
static const uint64_t WS_MAX_FRAME = 64 * 1024;  // 클라이언트가 선언한 길이의 상한

// 전송 타임아웃 (SO_SNDTIMEO). **지어낸 값이 아니다** — 명세의 숫자에서 끌어냈다:
//
//  · 아두이노 전선의 최대 프레임은 64바이트(§2.1)이고, 병목인 9600bps SoftwareSerial 에서
//    AT 오버헤드까지 합쳐 한 프레임을 밀어내는 데 약 80ms 다(§3.1). 1000ms 는 그 12배 이상이라
//    정상 링크에서 버스트(재하달 여러 건)가 몰려도 걸리지 않는다.
//  · **§7.3 의 ACK 타임아웃 1500ms 보다 짧아야 한다.** 길면 send() 안에 갇혀 있는 동안
//    재전송 타이머가 제 시각을 놓친다 — 타임아웃이 타임아웃을 망친다.
//  · **§3.4 의 오프라인 판정 3500ms 보다 훨씬 짧아야 한다.** 길면 "아두이노가 죽었다"를
//    알아채야 할 시간에 서버가 send() 안에서 멈춰 있는 셈이 된다.
//
// 즉 80ms ≪ 1000ms < 1500ms ≪ 3500ms.
//
// ⚠ 이 값은 `send_raw()` **한 번 전체**의 마감시각으로도 쓴다. `SO_SNDTIMEO` 는 `send()`
// **한 호출**에만 걸리므로, 상대가 조금씩만 빼가면 루프가 계속 돌아 총 정지 시간이 얼마든지
// 길어진다(실측: 1초 타임아웃인데 여러 번 호출하느라 1.93초). 그래서 둘 다 건다 —
// 소켓 옵션은 한 호출을, 마감시각은 한 프레임 전체를 막는다.
static const int SEND_TIMEOUT_MS = 1000;

// ---------------------------------------------------------------- 좀비 소켓 회수 (REQ-0072)
// 실측 사례: 세션#32 가 12:06:41 이후 프레임 0건인데 **17분 넘게 ESTABLISHED 로 살아 있었다.**
// 장치가 FIN 없이 죽으면(전원 붕괴·ESP 행) 서버는 그 fd 를 영원히 들고 있는다 — 365일
// 상시가동에서 fd 누수 경로다. 재접속 자체는 막히지 않는다(새 연결이 옛 소켓을 대체한다).
// 그래서 이건 결함 수리가 아니라 **위생**이다.
//
// ⚠ 두 장치는 중복이 아니다. **잡는 고장이 서로 다르다.**
//   · keepalive(OS)  : 상대가 통째로 사라진 경우 — 전원 붕괴·망 분리. ACK 가 아예 안 온다.
//   · 유휴 마감(앱)  : 상대의 TCP 스택은 살아서 ACK 를 꼬박 보내는데 **앱이 조용한** 경우.
//                      ESP 가 행에 걸려 +IPD 경로만 죽은 바로 그 사고가 여기다.
//                      keepalive 는 이걸 **원리적으로 못 잡는다** — 커널이 대신 응답하니까.
//
// ARD_IDLE_CLOSE_MS = 60000 의 근거 (지어낸 값이 아니다. 저장소 안의 숫자에서 끌어냈다):
//
//   하한 — **"이 소켓 위에서 장치가 다시 말할 수 있는 최장 정체"보다 커야 한다.**
//     끊고 재연결하는 경우는 옛 소켓이 이미 죽어 있으니 여기서 지킬 것이 없다. 지켜야 할 것은
//     **소켓은 멀쩡한데 ESP 만 `busy p...` 로 물려 있다가 스스로 풀리는** 경우다. 그 정체의
//     상한을 장치 쪽 사다리 숫자로 잡는다(조별과제샘플/client.ino):
//       · CIPCLOSE 사다리 3회      = 약 2.5초        (client.ino:505)
//       · AT+RST 한 사이클         = 약 14초         (client.ino:504)
//       · CWJAP 응답 대기 상한     = 최대 30초       (client.ino:524)
//       → 최악 약 44초. 60초는 그 위다.
//   상한 — 목적이 fd 회수이므로 길수록 의미가 준다. 60초면 fd 하나가 최대 1분만 낭비된다.
//   그리고 **§3.4 의 3.5초에 바로 끊지 않는다.** 3.5초는 "화면에 오프라인이라 쓴다"는
//   표시 판정이지 소켓 수명이 아니다. 거기서 끊으면 잠깐 늦은 프레임까지 죽인다.
//
//   즉 순서를 못 박는다:  3.5초(표시) ≪ 25초(keepalive) < 60초(유휴 마감)
//
// ⚠⚠ **어느 시계로 재는가가 이 값보다 중요하다.** 유휴는 `ard_last_ms`(체크섬을 통과한 `S`
// 프레임 시각, §3.4 가 쓰는 바로 그 시계)로 잰다. `sess_last_line_ms`(수신한 모든 줄)로 재면
// **AT 잡음이나 깨진 줄을 흘리는 장치가 영원히 살아남는다** — 줄은 계속 오니 유휴가 리셋되는데
// device_online() 은 계속 false 다. 그게 정확히 회수해야 할 좀비다(§6.2 잡음 유입 참조).
static const int ARD_IDLE_CLOSE_MS = 60000;

// keepalive 파라미터 — 탐지까지 10 + 5×3 = **25초**. 유휴 마감(60초)보다 먼저 걸리게 둔다.
// OS 가 잡을 수 있는 고장은 OS 가 먼저 잡는 편이 낫다(앱이 안 깨어나도 fd 가 돌아온다).
// KEEPALIVE_IDLE_S 는 §3.4 의 3.5초보다 넉넉히 위라 **건강한 1Hz 링크에서는 한 번도 안 쏜다.**
static const int KEEPALIVE_IDLE_S  = 10;
static const int KEEPALIVE_INTVL_S = 5;
static const int KEEPALIVE_CNT     = 3;

// 복구시간 표본 상한 — 중앙값을 내려면 표본을 들고 있어야 하는데 365일 상시가동에서
// 무한히 쌓을 수는 없다. 넘으면 **더 담지 않고 그 사실을 요약에 적는다**(조용히 버리지 않는다).
static const size_t RECOV_SAMPLE_MAX = 10000;

// ---------------------------------------------------------------- 다중 노드 (REQ-0083)
// 조원들이 각자 노드를 올린다(주차 센서·모터 제어·기타 센서). 옛 구조는 소켓을 하나만 들어
// **두 번째 노드가 첫 번째를 끊었다.** 그게 배포의 1순위 차단 요인이었다.
//
// MAX_ARD_NODES = 8 의 근거:
//   조별과제 규모가 4~6대다(루트 확인). 거기에 개발용 예비 2대를 더해 8 로 둔다.
//   위로 더 올릴 이유가 없다 — 노드마다 1Hz 하트비트가 오므로 8대면 초당 8프레임이고,
//   그 이상은 이 서버의 단일 스레드 select 루프가 아니라 다른 구조를 논해야 할 규모다.
//   **상한에 걸리면 새 연결을 거절한다(가장 오래된 것을 쫓아내지 않는다).**
//   이유: 살아서 잘 돌고 있는 노드를 끊고 정체 모를 새 소켓을 들이는 것은 **확실한 것을 버리고
//   불확실한 것을 얻는 거래**다. 같은 device_id 의 재접속은 자리를 물려받으므로(아래) 거절이
//   재부팅한 노드를 막지도 않는다.
static const size_t MAX_ARD_NODES = 8;

// **id 를 아직 모르는 소켓**의 상한과 마감.
// 첫 프레임을 받아야 device_id 를 아는데 소켓은 그 전에 이미 존재한다. 그 사이의 소켓을
// 무한히 받아 주면 그것이 곧 자원 고갈이다.
//
// UNKNOWN_TIMEOUT_MS = 7000 의 근거 — **지어낸 값이 아니라 §3.4 에서 끌어냈다**:
//   §3.4 는 3.5초(하트비트 3회분 + 여유) 무프레임이면 "말하지 않는 장치"로 판정한다.
//   붙자마자 말을 안 하는 소켓은 **명세 자신의 기준으로 이미 죽은 것**이다.
//   다만 접속 직후에는 AT 잡음이 먼저 새어 들어와 첫 줄이 깨질 수 있으므로(§6.2)
//   판정 기준의 2배를 준다 = 7초. 1Hz 이므로 그 안에 **유효 프레임 기회가 7번** 있다.
//
// ── 2026-08-16 실측으로 재확인함 (REQ-0111). 이 값을 고치려는 사람이 읽을 것 ──
//
// ⚠ **이 시계는 부팅이 아니라 `accept` 에서 시작한다.** 헷갈리기 쉬워 한 번 틀렸다:
//   "장치 기동이 12초인데 마감이 7초라 정상 부팅을 끊는다"는 결론이 나왔었는데 **틀렸다.**
//   장치는 WiFi 결합·IP 확보를 **끝낸 뒤에야** TCP 를 연다(시리얼 추적으로 확인:
//   `IP 확보 → CIPSTART → CONNECT → 첫 S 프레임`이 전부 같은 초). 그러므로 그 12초는
//   **소켓이 존재하지도 않는 구간**이라 이 마감이 볼 수 없다.
//
//   실측(accept 131건 전량):
//     · 식별 성공 75건 — accept→device_id 확정이 **최대 1.0초** (중앙 0.0). 7초 초과 **0건**.
//     · 프레임 없이 마감된 56건 — 그 뒤 프레임이 온 것은 **중앙 404초·최대 1380초** 뒤.
//   두 분포가 1초와 404초로 완전히 갈린다. **7초와 12초 사이에는 아무것도 없다** —
//   문턱을 그 사이 어디에 두든 판정이 같다는 뜻이라, 지금 값을 바꿀 이유가 없다.
//
// 🔴 **줄이지 마라 — 그때는 이야기가 다르다.** 1~2초까지 낮추면 정상 세션을 끊기 시작하고,
//   끊긴 장치는 **0.90초 주기 무백오프**로 즉시 재접속한다(펌웨어 실측). 그러면
//   `수락 → 마감 → 재접속` 이 영원히 도는 사이클을 **서버가 스스로 만들어 낸다.**
//   실제로 그 모양의 증상을 08-16 에 관측했고 원인은 다른 것이었는데, 마감을 줄이면
//   같은 증상을 진짜로 만들 수 있다. 7초 = 실측 최악(1.0초)의 7배이고,
//   **그 여유가 이 함정과의 거리다.**
static const int UNKNOWN_TIMEOUT_MS = OFFLINE_MS * 2;
static const size_t MAX_UNKNOWN_SOCKS = MAX_ARD_NODES;   // id 없는 소켓이 노드 예산을 넘지 못한다

static const char* SLOT_ID[10] = {"A1","A2","A3","A4","A5","B1","B2","B3","B4","B5"};
#include "server_device.h"   // 디바이스 계층 잎 유틸 (REQ-0096 A): SHA-1·base64·ws_accept·체크섬
#include "server_seam.h"     // 이음매 계약 (REQ-0096 B→C): DeviceEvent / DeviceCommand

// 타이머 전용 — 상대 시각 (윈도우: 부팅 후 경과)
static long long now_ms() {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}
// 바깥으로 나가는 값 전용 — Unix epoch 절대 시각 (ts, reserved_at)
static long long epoch_ms() {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)((t - 116444736000000000ULL) / 10000ULL);
#else
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}
// ---------------------------------------------------------------- 종료 신호 (REQ-0065)
// 소크 시험은 Ctrl-C 로 끝난다. 그때 **한 줄 요약을 남기고** 죽어야 관측이 완성된다.
// 핸들러에서 하는 일은 플래그 하나 세우는 것뿐이다 — 여기서 logf 를 부르면 비동기 안전하지 않다.
static volatile sig_atomic_t g_stop = 0;
static void on_stop_signal(int) { g_stop = 1; }

// ---------------------------------------------------------------- 시험용 포트 이동 (REQ-0072)
// **왜 필요한가**: 유휴 마감이 실제로 소켓을 닫는지 확인하려면 진짜 연결을 붙여 봐야 하는데,
// 운영 인스턴스가 9991/9900/5500 을 잡고 있으면 두 번째 인스턴스가 뜨지 못한다.
// 소크 이력을 들고 있는 프로세스를 재시작해서 확인하는 것은 **관측을 부수고 관측하는 짓**이다.
// 그래서 세 포트를 한꺼번에 옮기는 이음매를 둔다. **판정에는 전혀 관여하지 않는다**
// (--selftest 의 no_disk 와 같은 성격의 이음매다).
// 0 이 아니면 기동 배너에 크게 찍어 시험 인스턴스를 운영으로 착각할 수 없게 한다.
static int g_port_offset = 0;
// 🔴 **주차 노드 잠금**(REQ-0217 ④). 빈 문자열 = 잠금 없음 = 종전 `first-S-wins` 그대로.
// ⚠ **`first-S-wins` 를 없애지 않는다.** 이건 관측 환경용 잠금장치이지 프로토콜 변경이 아니다.
// 왜 필요한가: 우리가 devid 를 바꿔도 **조원 보드는 여전히 `P1` 로 붙고 서버는 그것을 받는다.**
// 더 나쁜 경우 — 조원 `P1` 이 **먼저** `S` 를 보내면 `first-S-wins` 가 **그 보드를 주차 노드로
// 지정**하고 우리는 보조 노드(상행 전용)로 밀려 **하행을 못 받는다.** 지금보다 나쁘다.
static std::string g_park_dev_pin;

// ---------------------------------------------------------------- 로그 계약 v0.1 (REQ-0111)
// 명세: docs/net/server-log-contract.md
//
// **왜 있는가 — 지어낸 요구가 아니라 2026-08-16 에 두 번 당한 것이다.**
//  (1) 타임스탬프에 날짜가 없었다. 21시간짜리 로그가 자정을 넘자 어제 16:00 과 오늘 16:00 이
//      같아 보였고, **두 사람이 각각 독립적으로** 어제 줄을 오늘 것으로 읽어 "두 프로세스가
//      동시에 기록 중"이라는 잘못된 판단을 했다. 실제로는 순차 종료였다.
//  (2) 로그 경로를 셸 리다이렉션이 정했다. 그래서 나중에 뜬 인스턴스가 다른 곳에 쓰는 바람에
//      관측이 조용히 끊겼고, 아무도 "그 프로세스는 어디에 쓰고 있나"를 답할 수 없었다.
//  (3) 기동 배너에 신원이 없었다. 도는 바이너리가 어느 소스에서 나왔는지 알 수 없어
//      mtime 비교로 추리해야 했다.

// 로그 형식 버전. **경계 줄의 첫 필드다.**
// monitor 의 집계 도구 9개가 전부 "줄이 HH:MM:SS 로 시작한다"를 가정하고 짜여 있고,
// 옛 로그(형식 1)와 새 로그를 **한 파일 안에서 동시에** 다뤄야 한다 — 옛 로그가 판정 근거라
// 계속 읽어야 하기 때문이다. 이 필드가 있으면 파서가 경계에서 스스로 전환한다.
// 없으면 관측자는 **날짜 유무를 휴리스틱으로 추측**해야 하고, 그 추측이 오늘 사고를 냈다.
// 3 (REQ-0118): 소크 요약에 `재연결내역(…)` 과 `승격전버림(…)` 두 칸이 추가됐고,
//   재연결 시 판정 한 줄(`재연결 판정: …`)이 새로 나간다. **기존 칸은 형태를 안 바꿨다** —
//   순수 추가라 옛 파서가 깨지지는 않지만, 새 칸이 있는지 없는지를 파서가 알아야 하므로 올린다.
#define LOG_FORMAT_VERSION 3

// ⚠ **소스 식별자다. 파일명이나 mtime 이 아니다.** mtime 은 파일 복사만으로도 바뀌어서
// "도는 바이너리가 이 소스에서 나왔나"를 증명하지 못한다(08-16 에 실제로 mtime 으로 추리했다).
// 빌드할 때 넘겨라: -DBUILD_ID='"'$(git rev-parse --short HEAD)'"'
// 안 넘기면 아래 기본값이 쓰이는데, **그것이 소스 해시가 아님을 값 자체로 드러낸다.**
#ifndef BUILD_ID
  #define BUILD_ID "unknown-source(compiled " __DATE__ " " __TIME__ ")"
#endif

// 화면으로 나가는 것을 로그 파일에도 그대로 흘린다.
// **출력 지점마다 두 번 쓰지 않는 이유**: 한 군데만 빠뜨려도 파일이 화면보다 덜 남는데,
// 그 차이는 사고가 난 뒤에야 발견된다. 스트림버퍼에서 한 번에 가르면 빠뜨릴 곳이 없다.
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* a, std::streambuf* b) : a_(a), b_(b) {}
protected:
    int overflow(int c) {
        if (c == EOF) return 0;
        if (a_->sputc((char)c) == EOF) return EOF;
        if (b_->sputc((char)c) == EOF) return EOF;
        return c;
    }
    int sync() { return (a_->pubsync() == 0 && b_->pubsync() == 0) ? 0 : -1; }
private:
    std::streambuf* a_;
    std::streambuf* b_;
};

static std::ofstream    g_logfile;
static TeeBuf*          g_tee       = 0;
static std::streambuf*  g_cout_orig = 0;
static std::string      g_log_path;

// 기본 로그 경로. **`/tmp` 를 쓰지 않는다** — 08-16 에 `/tmp` 의 캡처 파일이 unlink 된 채
// 프로세스만 fd 를 붙들고 있어서 113KB 를 통째로 잃었고, macOS 는 재부팅 시 `/tmp` 를 비운다.
// 저장소 안에도 두지 않는다 — 추적되지 않는 파일은 `git clean` 에 쓸려 나간다(같은 날 겪었다).
static std::string default_log_path() {
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = getenv("USERPROFILE");
#endif
    std::string base = (home && *home) ? (std::string(home) + "/parking-logs/parking-server")
                                       : std::string("parking-server");   // 최후 수단: 현재 디렉터리
    // 🔴 **시험 인스턴스는 기본 경로를 쓸 수 없다.**
    // monitor 의 요구: 한 파일에 두 인스턴스가 쓰는 것을 "탐지"가 아니라 "불가능"으로 만들 것.
    // 오늘 그 가능성 때문에 관측자가 "두 프로세스가 동시에 썼다"고 의심할 수밖에 없었고,
    // 실제로 그 의심이 잘못된 발표로 이어졌다. 규칙으로 부탁하지 않고 경로를 갈라 버린다.
    if (g_port_offset != 0) base += ".test+" + std::to_string(g_port_offset);
    return base + ".log";
}

// 🔴 A[1](B) — `rid` 커서 영속 파일. **반드시 절대경로다.**
//
// **cwd 상대경로로 두면 다른 디렉터리에서 기동하는 순간 커서가 조용히 사라지고,
//   그때 나오는 거동이 정확히 "보호가 없는 상태"다 — 막으려던 것으로 조용히 되돌아간다.**
// 루트가 오늘 그 함정을 실측했다: `data_log.json`·`index.html` 이 cwd 상대라
// 인스턴스마다 다른 파일을 열고 있었다(REQ-0240 · 이틀 전 화면이 서빙되던 건).
//
// 🔑 **로그와 같은 규칙으로 오프셋에 따라 경로를 가른다.** 안 그러면 시험 인스턴스가
//   운영의 커서를 덮어써서 **운영 재시작이 시험이 쓴 자리로 되돌아간다.**
static std::string rid_cursor_path() {
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = getenv("USERPROFILE");
#endif
    if (!home || !*home) return std::string();   // 빈 값 = 영속 불가. 호출자가 크게 남긴다
    std::string base = std::string(home) + "/parking-logs/parking-rid-cursor";
    if (g_port_offset != 0) base += ".test+" + std::to_string(g_port_offset);
    return base + ".txt";
}

// 부모 디렉터리를 **한 단계씩 전부** 만든다.
// ⚠ 한 단계만 만들면 조부모가 없을 때 mkdir 이 ENOENT 로 실패하고, 그 결과는
// "로그가 조용히 안 남는다" 이다. 기본 경로($HOME/parking-logs)는 우연히 한 단계라 통과하지만
// --log= 로 깊은 경로를 주면 그 순간 무너진다. 조용한 실패를 남겨 두지 않는다.
static void ensure_parent_dir(const std::string& path) {
    size_t cut = path.rfind('/');
    if (cut == std::string::npos || cut == 0) return;
    std::string dir = path.substr(0, cut);
    for (size_t i = 1; i <= dir.size(); i++) {
        if (i != dir.size() && dir[i] != '/') continue;
        std::string part = dir.substr(0, i);
#ifdef _WIN32
        _mkdir(part.c_str());
#else
        mkdir(part.c_str(), 0755);      // 이미 있으면 EEXIST — 무시해도 되는 유일한 실패다
#endif
    }
}

static long cur_pid() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

// 경계 줄의 bin= 필드. 실패해도 기동을 막지 않는다 — 모르면 "?" 를 적는다.
static std::string exe_path() {
    char b[1024];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, b, (DWORD)sizeof(b));
    return n ? std::string(b, n) : std::string("?");
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)sizeof(b);
    if (_NSGetExecutablePath(b, &n) != 0) return std::string("?");
    char rp[1024];
    return realpath(b, rp) ? std::string(rp) : std::string(b);
#else
    ssize_t n = readlink("/proc/self/exe", b, sizeof(b) - 1);
    return n > 0 ? std::string(b, (size_t)n) : std::string("?");
#endif
}

// 🔴 경계 줄의 `cwd=` 필드 (2026-08-19 · 루트 결정 · web REQ-0243 이 계기)
//
// **`serve_file()`(`index.html`)과 `data_log.json` 쓰기가 둘 다 cwd 상대다.**
// 그래서 **같은 바이너리라도 어디서 떴느냐에 따라 다른 실체를 읽고 쓴다** —
// 오늘 `data_log.json` 이 세 디렉터리에 있었고(`learn` · `조별과제샘플` · `parking-bin`),
// `:9900` 이 이틀 된 화면을 내주고 있었다(REQ-0240/0243).
//
// 🔴 **그때 cwd 를 사후에 복원할 방법이 없었다.** 프로세스가 죽으면 `lsof` 로도 못 본다.
//   web 이 `data_log.json` 의 mtime 으로 역추적을 시도했는데 **그 디렉터리를 쓰는 프로세스가
//   여럿이라 성립하지 않았다.** 🔑 **cwd 는 사후에 파일로 복원할 값이 아니라 기동 때 찍을 값이다.**
//
// ⚠ 실패해도 기동을 막지 않는다 — 모르면 "?" 를 적는다(`bin=` 과 같은 규율).
static std::string cur_cwd() {
    char b[1024];
#ifdef _WIN32
    return _getcwd(b, (int)sizeof(b)) ? std::string(b) : std::string("?");
#else
    return getcwd(b, sizeof(b)) ? std::string(b) : std::string("?");
#endif
}

static std::string iso8601(long long ep_ms) {
    time_t tt = (time_t)(ep_ms / 1000);
    char b[40];
    strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S%z", localtime(&tt));
    return std::string(b);
}

// 로그 파일을 연다. **실패해도 기동을 막지 않는다** — 관측이 없다고 서비스를 멈추는 것은
// 손해가 더 크다. 다만 화면에 크게 알려서 "조용히 관측이 없는" 상태가 되지 않게 한다.
// 같은 로그 파일에 두 인스턴스가 붙는 것을 **기계로** 막는다.
// 경로를 가르는 것(default_log_path)은 실수를 막지만 `--log` 로 같은 경로를 명시하면 뚫린다.
// 여기서 배타 잠금을 걸어 그마저 불가능하게 한다.
// flock 은 **fd 에 걸리므로 프로세스가 죽으면 커널이 자동으로 푼다** — 죽은 잠금이 남지 않는다.
// (Windows 는 미구현. 그쪽은 경로 분리까지만 보장한다 — 계약서에 한계로 적어 둔다.)
#ifndef _WIN32
static int g_log_lock_fd = -1;
static bool lock_log_exclusive(const std::string& path) {
    g_log_lock_fd = open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    if (g_log_lock_fd < 0) return true;            // 못 열면 여기서 막지 않는다. open_log 가 판정한다.
    if (flock(g_log_lock_fd, LOCK_EX | LOCK_NB) == 0) return true;
    close(g_log_lock_fd);
    g_log_lock_fd = -1;
    return false;
}
#endif

static void open_log(const std::string& path) {
    g_log_path = path.empty() ? default_log_path() : path;
    ensure_parent_dir(g_log_path);
#ifndef _WIN32
    if (!lock_log_exclusive(g_log_path)) {
        // **여기서는 기동을 막는다.** 로그를 못 여는 것(관측 없음)과 달리, 이건 다른 인스턴스가
        // 이미 그 파일에 쓰고 있다는 뜻이다. 그대로 뜨면 두 인스턴스의 줄이 섞여
        // **관측자가 카운터를 잘못 읽는다** — 오늘 하루를 잡아먹은 바로 그 실패다.
        std::cerr << "⚠ 다른 인스턴스가 이미 이 로그를 쓰고 있다: " << g_log_path << "\n"
                  << "   같은 파일에 둘이 쓰면 인스턴스 경계가 무의미해진다. 기동하지 않는다.\n"
                  << "   시험 인스턴스라면 --port-offset= 을 주면 경로가 자동으로 갈린다.\n";
        exit(1);
    }
#endif
    g_logfile.open(g_log_path.c_str(), std::ios::out | std::ios::app);
    if (!g_logfile.is_open()) {
        // ⚠ **경로를 지우고 실패 사실로 바꾼다.** 그냥 두면 경계 줄이 `log=<경로>` 라고
        // 적는데 그 파일은 존재하지도 않는다 — **계약이 거짓말을 하는 것**이고,
        // 읽는 사람은 "로그가 저기 있는데 왜 비었지"로 시간을 쓴다. 관측 도구가
        // 신뢰하는 필드라 더더욱 안 된다.
        std::cerr << "⚠ 로그 파일을 열지 못했다: " << g_log_path
                  << " — 화면에만 남는다(관측이 끊긴 것으로 보일 수 있다)\n";
        g_log_path = "(열기실패:" + g_log_path + ")";
        return;
    }
    g_cout_orig = std::cout.rdbuf();
    g_tee = new TeeBuf(g_cout_orig, g_logfile.rdbuf());
    std::cout.rdbuf(g_tee);
}

static void logf(const char* mark, const std::string& msg) {
    long long t = epoch_ms() / 1000;
    time_t tt = (time_t)t;
    // ⚠ **날짜를 빼지 마라.** 이 줄에서 날짜가 빠져 있던 탓에 08-16 에 오독이 두 번 났다.
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&tt));
    std::cout << ts << "  " << mark << " " << msg << std::endl;
}
// prev_up < 0 = 기준선 없음(첫 프레임·새 연결 직후) → 판정하지 않는다.
static bool uptime_says_reboot(long long prev_up, long long up) {
    if (prev_up < 0 || up < 0) return false;      // 기준선 없음 / 깨진 값은 판정하지 않는다
    long long fwd = ((up - prev_up) % UPTIME_WRAP + UPTIME_WRAP) % UPTIME_WRAP;
    return fwd > UPTIME_MAX_FWD;
}

// ---------------------------------------------------------------- 다중 노드 (REQ-0083)
// **주차 노드는 하나다. 나머지는 보조 노드다.** 왜 대칭이 아닌가:
//   `R`/`C`/`T` 하행은 **주차 칸을 가진 장치**로만 가야 하는데, 전선에는 "내가 주차 노드다"라고
//   말하는 필드가 없다. `S` 는 10칸 비트를 통째로 실을 뿐 소유권을 주장하지 않는다.
//   그래서 **없는 규약을 지어내지 않고**, 관측 가능한 규칙 하나로 정한다:
//
//        ▶ **첫 `S` 를 보낸 장치가 그 서버 수명 동안 주차 노드다(first-S-wins).**
//
//   두 번째 장치가 `S` 를 보내면 **충돌로 로그에 크게 남기고 하행을 주지 않는다.**
//   조용히 둘 중 하나를 고르면 "예약이 가끔 엉뚱한 데로 간다"가 되는데, 그건 며칠 뒤에
//   원인을 못 찾는 종류의 사고다. **가정이 깨지는 순간이 로그에 보이는 것**이 이 규칙의 값이다.
//   근본 해법은 전선에 역할 필드를 두는 것이고, 그건 v1.4 급 변경이라 명세에 제안으로만 남긴다.
//
// 보조 노드는 **상행 전용**이다(수신·계측만). 하행을 받을 방법이 아직 명세에 없다.

// 아직 `device_id` 를 모르는 소켓. 첫 유효 프레임에서 승격된다.
// 🔴🔴 **접속 IP·포트를 남긴다** (REQ-0215)
// 종전에는 `accept(fd, NULL, NULL)` 로 **상대 주소를 그냥 버렸다.** 그래서 2026-08-18 에
// *"같은 망의 조원들이 동일 카피 보드(`device=P1`)를 돌리고 있었다"* 가 확인됐을 때
// **서버 자료만으로는 어느 세션이 누구 것인지 가릴 방법이 없었다.**
// 🔑 **`device_id` 가 고유하다는 전제가 깨지면 전선 위 식별자는 전부 무력해진다.**
//    그때 남는 유일한 구분자가 **IP·포트**다 — 그것은 전선이 아니라 커널이 준다.
// ⚠ 완전하지 않다: NAT 뒤나 DHCP 재할당이면 같은 IP 가 다른 장치일 수 있다.
//   **그래도 "구분자가 하나도 없다"와는 질이 다르다.**
static std::string peer_str(sock_t fd) {
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getpeername(fd, (struct sockaddr*)&ss, &sl) != 0) return "?";
    char host[64] = {0};
    if (ss.ss_family == AF_INET) {
        struct sockaddr_in* a4 = (struct sockaddr_in*)&ss;
        const unsigned char* q = (const unsigned char*)&a4->sin_addr;
        snprintf(host, sizeof(host), "%u.%u.%u.%u:%u",
                 q[0], q[1], q[2], q[3], (unsigned)ntohs(a4->sin_port));
        return std::string(host);
    }
    return "?";
}

// IP 만(포트 제외). **동일 장치 판별은 IP 로 한다** — 포트는 재접속마다 바뀐다.
static std::string peer_host(const std::string& hp) {
    size_t c = hp.rfind(':');
    return (c == std::string::npos) ? hp : hp.substr(0, c);
}

// 🔴🔴 **`Node` — 통신 단위 하나**(REQ-0203 1단계 · 설계 `docs/net/DESIGN-bridge-node-module-zone.md` §1)
// 지금은 **주차 노드 하나뿐**이라 `Server` 안에 `park` 로 한 벌만 산다. 나중에 `map<devid,Node>` 가 된다.
//
// 🔑 **이 단계의 목적은 "그릇"만 바꾸는 것이다. 거동이 바뀔 자리가 없어야 한다.**
//   기존 이름(`ard_last_ms` 등)을 **참조 별칭**으로 남겨 **호출부 약 150곳을 한 곳도 안 고친다.**
//   ⚠ 참조 멤버는 `operator=` 를 지운다 — **`Server` 가 어디서도 대입되지 않는 것을 먼저 확인했다**
//     (선언 13곳 전부 생성이고 대입 0곳).
// ⚠ **낱말**: 여기의 `Node` 는 통신 단위(ESP 하나 = 소켓 하나 = 자기 1.2초 주기)다.
//   **주차 자리가 아니다.** 자리는 `Zone`(§0 낱말표).
struct Node {
    // ⚠ **필드 이름을 `AuxNode` 쪽에 맞췄다**(REQ-0203 2단계). 반대로 하면 보조 노드
    //   호출부 20여 곳을 고쳐야 하는데, 이쪽은 **별칭 7줄만** 바꾸면 된다.
    //   🔑 **적게 고치는 쪽을 고른 것이 아니라, 고치는 자리가 내가 방금 만든 자리인 쪽**을 골랐다.
    std::string devid;          // 전선의 장치 id. "" = 아직 미정(first-S-wins)
    sock_t      fd;             // 이 노드의 연결
    std::string peer;           // "IP:포트" — `devid` 가 고유하지 않을 때 유일한 구분자(REQ-0215)
    std::string buf;            // 수신 조립 버퍼
    bool        seen;           // 유효 프레임을 한 번이라도 받았나
    long long   last_ms;        // 마지막 **유효** 프레임 수신 시각(단조 시계) — 유휴 마감의 기준
    long long   last_epoch_ms;  // 같은 사건의 벽시계 — **로그 대조용이지 계산용이 아니다**

    // ── `AuxNode` 에서 흡수한 것들 (REQ-0203 2단계) ─────────────────────────────
    // 🔑 **보조 노드는 처음부터 `Node` 의 부분집합이었다.** 두 구조체를 유지하면
    //    "노드마다 있는 상태"가 두 곳에 나뉘어 **다음 단계(`map<devid,Node>`)에서 합칠 수 없다.**
    long long   connected_ms;     // 접속 시각
    long long   frames;           // 누적 유효 프레임
    long long   drops;            // 버린 줄
    bool        online;           // §3.4 엣지 판정용 직전 상태
    int         offline_episodes;

    // ── 등록(§5 `D`/`Q`) ────────────────────────────────────────────────────────
    // 🔴 **이 단계에서 등록은 *관측*이지 제어가 아니다.** 하행 경로를 한 줄도 안 바꾼다 —
    //    지금 하행(`R`·`C`·`T`·`M`)은 **자리(슬롯 번호)** 로 주소를 정하므로 모듈 구성과 무관하다.
    //    **모듈 신원 기반 명령이 생길 때** 비로소 이 상태가 제어에 쓰인다(설계 §5).
    // ⚠ 그래서 옛 펌웨어(등록을 모르는 노드)도 **종전 그대로 동작한다.** 분기를 안 만든다.
    int         reg_n;          // 선언된 모듈 수. -1 = `D,*` 를 아직 못 받았다
    int         reg_drain;      // 선언된 슬롯당 ACK 배출 하한. -1 = 미선언
    bool        reg_done;       // `reg_n` 개를 다 받았다
    bool        reg_giveup;     // `Q` 3회에도 안 와서 굳혔다(node_unregistered)
    long long   reg_first_ms;   // 승격(첫 `S`) 시각 — `REG_TIMEOUT` 의 기준
    int         q_sent;         // 보낸 `Q` 수
    long long   last_q_ms;      // 마지막 `Q` — 슬롯당 1회로 묶는다
    std::vector<std::pair<std::string, std::string> > mods;   // (name, kind) · **순서가 곧 idx**

    // ── 🔴 ②-c — **자리 비트열은 노드의 것이다** (REQ-0262/0263 · 2026-08-19)
    //   전에는 `Server` 에 한 벌뿐이었다. 그래서 보조 노드의 모듈은 값 경로가 없었고
    //   `state_json()` 이 그것을 정직하게 `known:false` 로 냈다(설계 §8.9).
    //   🔴 **이 필드가 `Node` 로 오기 전에 보조 노드의 줄을 파서에 넣으면**
    //     그 `S` 가 **주 노드의 비트열을 덮는다.** 그래서 ②-c 가 ②-b 보다 먼저다.
    int         mod_bits[REG_MODS_MAX];   // 자리 비트열(자리 10칸을 넘는 비트 포함)
    int         mod_bits_n;               // 해독한 비트 수. **0 = 안 읽었다**(모른다이지 0 이 아니다)

    Node() : fd(BAD_SOCK), seen(false), last_ms(0), last_epoch_ms(0),
             connected_ms(0), frames(0), drops(0), online(false), offline_episodes(0),
             reg_n(-1), reg_drain(-1), reg_done(false), reg_giveup(false),
             reg_first_ms(0), q_sent(0), last_q_ms(0), mod_bits_n(0) {}

    void reg_reset() {          // 세션이 새로 서면 등록도 처음부터다
        reg_n = -1; reg_drain = -1; reg_done = false; reg_giveup = false;
        reg_first_ms = 0; q_sent = 0; last_q_ms = 0; mods.clear();
    }
};

// 🔴 **`AuxNode` 는 이제 `Node` 다**(REQ-0203 2단계). 이름만 남긴다 —
// 호출부 20여 곳이 `AuxNode` 로 적혀 있고 **그것을 지금 바꾸면 이 단계가 커진다.**
// ⚠ **다음 단계에서 이 이름도 없앤다.** 지금 남긴 이유는 **한 단계에 한 가지만 바꾸려는 것**이다.
// 🔑 별명이라 **보조 노드도 등록 상태를 갖게 된다** — 쓰지 않을 뿐 구조가 이미 준비된다.
typedef Node AuxNode;

struct UnknownSock {
    sock_t fd;
    std::string buf;
    long long since_ms;
    std::string peer;          // "IP:포트" — 승격 시 세션으로 넘어간다
    UnknownSock() : fd(BAD_SOCK), since_ms(0) {}
};

// ---------------------------------------------------------------- 상태
// 🔴🔴 **`Zone` — 주소를 갖는 장소 하나**(설계 §0·§1 · REQ-0203 4a)
// ⚠ **`Node`(통신 단위)와 다른 것이다.** 자리는 여러 노드에 걸칠 수 있고, 한 노드가 여러 자리를 가질 수 있다.
// 🔑 **`cells` 를 목록으로 둔다** — 지금은 **항상 길이 1**이고 그것을 코드가 강제한다.
//   좌표를 필드에 하나 박으면 큰 자리가 생길 때 **자료형이 바뀌어 읽는 모든 곳이 같이 바뀐다.**
//   목록이면 **길이 제한만 푼다.** ⚠ **목록으로 두는 것과 아무 길이나 받는 것은 다르다** —
//   후자는 **검증되지 않은 경로를 여는 것**이라 지금 막는다.
struct Zone {
    std::string id;                                     // 전역 고유 · **좌표에서 유도하지 않는다**
    std::string kind;                                   // parking | entrance | exit
    std::vector<std::pair<int,int> > cells;             // (row, col) · **지금은 길이 1**
    std::vector<std::pair<std::string,std::string> > modules;  // 🔴 **(devid, name)** — name 만 쓰면 안 된다
};

struct Slot {
    int occupied;            // 아두이노가 진실값을 준다
    int reserved;            // 서버가 durable owner (§7.4)
    std::string user_id;     // 빈 문자열 = null
    long long reserved_at;   // 0 = null
    Slot() : occupied(0), reserved(0), reserved_at(0) {}
};

struct Pending {             // 아두이노에 내려보내고 ACK 를 기다리는 요청
    uint16_t wire_rid;
    sock_t   ws_fd;          // 요청한 브라우저 (BAD_SOCK = 서버 자체 재동기화)
    std::string browser_rid;
    std::string slot, user_id;   // user_id = **전선에 실제로 나간 값**(ASCII 0*8 또는 빈 값)
    std::string plate;           // 서버가 보관할 원래 값(UTF-8 번호판 등). 전선에 안 나간다
    char kind;               // 'R' | 'C' | 'T' | 'M' | 'G'
    // 🔴 `kind=='G'` 전용 — **모듈 인덱스**다. `slot` 은 사람이 읽을 자리 이름이고
    //   전선에 나가는 것은 이 숫자다. **둘을 한 칸에 넣지 않는다** — 자리 이름과 모듈 순서는
    //   다른 것이고(§5), 섞으면 재등록으로 순서가 바뀔 때 무엇이 틀렸는지 못 가린다.
    int  mod_idx;
    char top;                // kind=='T' 일 때의 op: 'A'|'D'|'S'|'X'
    long long sent_ms;
    int tries;
    // 🔴 **큐에 있는 동안은 ACK 시계를 돌리지 않는다.** 하행이 창을 기다리는 사이에
    // `ACK_TIMEOUT_MS` 가 흐르면 **전선에 나가기도 전에 재전송이 걸린다** — 그러면
    // 같은 rid 가 큐에 두 번 들어가고, 내가 없애려던 증폭이 큐 안에서 다시 생긴다.
    // `tick()` 은 이 값이 true 인 항목을 건너뛴다. `sent_ms` 는 **실제 송신 시각**이다.
    bool queued;
    // 🔴 ctor 가 없어서 `dispatch` 가 `top`·`queued` 를 안 세운 채 복사해 왔다.
    // 지금은 모든 경로가 곧바로 덮으므로 실동작은 맞지만 **`-Wall -Wextra` 가 이걸 안 잡는다** —
    // `Server` 의 여섯 칸이 무경고로 통과했던 것과 같은 이유다. 여기서 닫는다.
    Pending() : wire_rid(0), ws_fd(BAD_SOCK), kind(0), mod_idx(-1), top(0), sent_ms(0), tries(0), queued(false) {}
};

struct Conn {
    enum Kind { HTTP, WS } kind;
    std::string inbuf;
    // 🔴 **`get_map` 상한을 연결별로 둔다** (2026-08-19).
    //   전역 창이면 **화면 여섯이 재접속하는 것만으로 상한을 넘긴다** — 각자 한 번씩 물었는데
    //   누군가는 거절당한다. 서버 재기동 직후가 정확히 그 상황이고, **거절당한 화면은
    //   지형을 못 받아 빈 채로 남는다.**
    //   ⚠ 주석은 원래 *"화면 하나가 1초에"* 라고 말하고 있었다 — **구현이 그 말과 달랐다.**
    //   🔑 고친 것은 상한값이 아니라 **누구를 세는가**다.
    long long getmap_win_ms; int getmap_in_win;
    Conn() : kind(HTTP), getmap_win_ms(0), getmap_in_win(0) {}
};

struct Server {
    Slot slots[10];

    // ── 지형 (REQ-0203 4a) ──────────────────────────────────────────────────────
    // 🔴 **`5` 를 코드에 박지 않는다.** 기본값이고 설정에서 온다(설계 §0).
    int grid_rows, grid_cols;
    std::vector<Zone> zones;
    // 🔑 **판(`epoch`)은 서버만 올리고, 지형을 바꾸는 함수 안에서 그 줄 옆에서 올린다**(설계 §6.8).
    //   떨어뜨리면 **"바꿨는데 안 올린 경로"** 가 생기고, 그건 **탐지 장치가 있는데 안 울리는 것**이라
    //   아예 없는 것보다 나쁘다.
    long long map_epoch;
    // 🔴🔴 **`epoch` 는 *이 서버 인스턴스 안에서만* 단조다**(설계 §6.8).
    //   재기동하면 0 부터 다시 시작하므로, 화면이 옛 판을 들고 있으면 **새 서버의 판 1 을
    //   "늦게 온 옛 프레임"으로 무시한다** — 그러면 화면이 영영 새 맵을 안 받고 **오류도 안 뜬다.**
    // 🔑 **"화면이 재접속 때 판을 버려라"를 규칙으로 두면 그건 두 곳에 있는 규칙이다.**
    //   **값으로 실으면 화면이 안 잊는다.** → 봉투마다 `srv_id` 를 같이 보낸다.
    std::string srv_id;
    // `get_map` 남용 방어 — **정합성용이 아니라 서버 보호용**이다(설계 §6.8)
    long long getmap_win_ms; int getmap_in_win; long long getmap_rejects;
    sock_t lsn_ard, lsn_http, lsn_phone;
    std::map<sock_t, std::string> phones;   // 폰 연결 → 수신 버퍼 (연결마다 따로!)
    // ⚠ `ard` 는 이제 "아두이노 연결"이 아니라 **주차 노드의 연결**이다(REQ-0083).
    // 이름을 안 바꾼 이유: 이 필드에 얽힌 상태·지표가 79곳인데, 그 전부를 건드리면
    // **하위호환을 코드로 증명할 수 없다.** 지금 도는 단일 노드(P1)가 그대로 동작해야 하고
    // 그게 조원 배포 전까지 유일한 실물이다. 그래서 주차 노드 경로는 **한 줄도 안 바꾸고**,
    // 보조 노드를 옆에 붙이는 쪽을 택했다. 단일 노드 동작이 구조적으로 보존된다.
    // 🔴 **진실은 여기 있다**(REQ-0203 1단계). 아래 별칭들은 전부 이 안을 가리킨다.
    Node park;
    // ⚠ **별칭은 옛 이름을 유지하기 위한 것이다.** 새 코드는 `park.<필드>` 를 직접 쓴다 —
    //   별칭을 늘리지 마라. 2단계에서 `map<devid,Node>` 로 올릴 때 **별칭이 많을수록 못 올린다.**
    sock_t&      ard      = park.fd;   // **주차 노드**의 연결 (여전히 하나)
    std::string& park_dev = park.devid;  // 주차 노드의 device_id. "" = 미정(first-S-wins)
    std::string& ard_peer = park.peer;   // 현재 주차 노드 소켓의 "IP:포트" (REQ-0215)
    // 🔴 **같은 devid 로 동시에 붙은 것을 센다.** 0 이 아니면 관측 자료가 오염된 것이다 —
    // **어느 보드가 응답했는지 모르는 채로 지표를 읽게 된다.**
    long long reg_ok;          // 등록 완료 수
    long long reg_bad;         // 🔴 형식·개수·이름 위반. **조용히 넘어가지 않는다**
    long long reg_qsent;       // 보낸 `Q` 수(누적)
    long long reg_giveups;     // `Q` 3회에도 안 와서 굳힌 수
    long long reg_widthbad;
    // 🔴 등록 전이라 자리 비트열을 못 푼 프레임 수. **0 이 아니면 그 구간 자리 상태가 없다**
    long long occ_undecoded; bool occ_undecoded_warned;    // 🔴 삼중 검산 ③ 불일치 — `S` 의 hex 폭이 선언 `n` 과 안 맞는다
    // 🔴 **분모를 찍는다** — 2026-08-19 에 자가 치유(§7.6)가 hex 전환 이후 **한 번도 안 돌았는데
    //   아무 흔적이 없었다.** 폭 검사가 *닫히는* 쪽으로 실패해서 오독조차 안 남긴 것이다.
    //   ⚠ **`0` 이 안 남는 결함은 계수로 못 잡는다** — 그래서 "몇 번 고쳤나"(분자)가 아니라
    //   **"몇 번 검사했나"(분모)** 를 센다. `heal_checks == 0` 이면 그 자체가 빨강이다.
    // 🔴 **`occ` 의 비트는 자리 점유만이 아니다** — 비트 `>= SLOT_N` 은 **액추에이터의 현재 상태**다
    //   (arduino REQ-0228 답변 · 명세 §5 "위험 다섯째"). **조작 완료를 판정하는 값이 이것이다.**
    //   ⚠ 새 칸을 만들면 같은 값이 두 곳에 생기고 **갈리는 순간 어느 쪽이 맞는지 알 방법이 없다.**
    //   🔑 그래서 칸을 안 늘리고 **이미 오는 값을 제대로 읽는 쪽**을 골랐다(전선 예산 증가 0).
    // ~~int mod_bits[REG_MODS_MAX];~~ → **`Node` 로 내렸다**(②-c). 노드마다 한 벌이다.
    // 🔴 **마지막으로 요청한 값**(모듈 인덱스 → 0/1). **완료 판정은 대조이지 존재 확인이 아니다.**
    //   ⚠ 처음엔 "그 모듈의 에코가 있으면 settled" 로 짰는데 **그건 "무엇이 됐는가"를 안 본다** —
    //     열라고 했는데 닫혀 있어도 `settled` 가 됐다. **거짓 완료를 막으려던 값이 거짓 완료를 만든다.**
    //   🔑 그리고 `pend` 는 ACK 에 지워지므로 **거기서는 대조할 수 없다.** 별도로 들고 있어야 한다.
    std::map<int,int> gate_want;
    // 재등록 직전의 모듈 목록. `D,*` 에서 찍고 등록 완료에서 대조한 뒤 비운다.
    std::vector<std::pair<std::string,std::string> > prev_mods_snapshot;
    long long mod_order_changed;   // 🔴 약속이 깨진 횟수. **0 이 정상이고 1 이상은 조사 대상이다**
    // ~~int mod_bits_n;~~ → **`Node` 로 내렸다**(②-c).
    // 🔴 **분모를 같이 만든다.** `장치거절 0` 만 찍으면 **`0` 이 혼자 서서 건강처럼 보인다** —
    //   `치유 0/386` 은 분모가 보여서 `0` 이 건강임을 말할 수 있는데, 이건 말할 수 없다.
    //   ⚠ **`0/0` 은 최소한 `0/0` 이라고 말해 준다. 분모 칸이 없는 것이 더 나쁘다**(monitor 지적).
    // 🔴 **자리가 아니라 이름으로 모은다**(monitor·루트 합의 · 2026-08-19).
    //   `1/2/3` 처럼 순서로 기억하는 숫자가 오늘 우리를 여러 번 넘어뜨렸다
    //   (`등록 145/133/146` 이 셋 다 맞았는데 어느 자리를 세는지가 안 붙어 있었다).
    //   🔑 **이름을 붙이면 순서를 기억할 필요가 없다.**
    //
    //   띄움 M ─(전선에 나갔나)→ 도달 D ─(답이 왔나)→ 응답 K ─(무엇이라 했나)→ 거절 N
    //   ⚠ **각 화살표가 다른 고장이다**: 큐에서 죽음 · 나갔는데 무응답 · 갔고 거절당함.
    //     두 수만 두면 **무응답이 성공에 섞여 좋아 보이는 쪽으로 틀린다** — 가장 나쁜 방향이다.
    long long gate_q;            // 띄움 — 큐에 들어간 수
    long long gate_sent;         // 도달 — 실제로 전선에 나간 수
    long long gate_ans;          // 응답 — ACK 이 온 수 (result 무관)
    long long dev_reject;        // 🔴 `result=3`(장치가 거절) — `ack_fail_count`(안 갔다)와 **다른 칸**이다
    long long heal_checks;       // §7.6 예약 불일치 **검사**가 실제로 돈 횟수 (분모)
    long long heal_fires;        // 그중 불일치를 찾아 `C` 를 재하달한 횟수 (분자)
    long long res_undecoded;     // `S` 의 예약 마스크를 못 읽은 프레임 수
    bool      res_undecoded_warned;
    long long dup_devid_reject;   // ①에서 거절한 횟수 = **다른 IP 가 산 자리를 노렸다**
    long long takeover_grace;     // 유예를 넘겨 교체한 횟수 = 진짜 죽은 것으로 판단
    std::map<std::string, AuxNode> aux;   // device_id → 보조 노드 (상행 전용)
    std::vector<UnknownSock> unknown;     // id 미상 소켓 — 첫 유효 프레임에서 승격
    int  aux_conflicts;                // `S` 를 보냈지만 주차 노드가 아닌 장치 수(가정 붕괴 신호)
    int  admit_rejects;                // 상한 초과로 거절한 연결 수
    std::string& ard_buf   = park.buf;
    bool&        ard_seen  = park.seen;
    long long&   ard_last_ms = park.last_ms;

    // ── 하행 슬롯 큐 (docs/net/DESIGN-server-slot-queue.md · 반송파 슬롯 구조)
    //
    // **이벤트가 전송을 만들지 않는다.** 명령이 생기면 큐에 담고, **장치 프레임이 도착한
    // 순간(= 내 창의 시작)** 에 큐 전부를 **한 거래로 묶어** 보낸다.
    //
    // 🔑 **창 시작은 시계가 아니라 사건이다** — 장치가 느려지면 내 창도 같이 밀린다(자기교정).
    // 그래서 위상을 추정할 필요가 없다. 추정은 상대가 정상일 때만 맞는데, 우리가 고치려는
    // 것은 상대가 비정상인 순간이다(원장 §8.19).
    struct DownQ {
        std::string line;        // 전선에 나갈 완성된 줄(LF 포함)
        sock_t      ws_fd;
        std::string brid;        // 브라우저 rid
        std::string slot;
        uint16_t    wire_rid;
        long long   deadline_ms; // enqueue 때 **한 번 박는다**
        // 🔑 **분류는 이미 코드에 있었다**: `ws_fd == BAD_SOCK` = 서버 내부 발생.
        // 재동기·치유·은퇴·폰 배정이 전부 그것이고, 브라우저 조작만 fd 를 갖는다.
        // **기준**: "이 명령이 없으면 서버와 장치가 갈린 채로 남는가?" → 그렇다면 중요다.
        // 🔴 **치유 명령이 연타에 밀려나면 치유 자체가 죽는다.**
        bool        important;   // true = 중요(거절 금지 · 배출 우선)
    };
    std::vector<DownQ> downq;
    long long downq_bytes;
    long long batch_count;       // 배치 거래 수
    long long batch_lines;       // 배치로 나간 줄 수
    // 🔴 **정반대인 둘을 한 칸에 세지 않는다**(원장 §8.16 이 `error` 에서 겪은 그것이다).
    //   `q_rejected` = 큐가 넘쳤다 → **설계 문제**(cap·깊이를 다시 봐야 한다)
    //   `q_nodev`    = 장치가 없다 → **링크 문제**(cap 과 아무 상관 없다)
    // 합쳐 두면 `큐거절 3` 을 보고 cap 을 올리려 하는데 실제로는 장치가 없었던 것일 수 있다.
    long long q_rejected;        // queue_full 로 거절한 수(두 축의 합)
    // 🔴 **바이트 축과 건수 축을 갈라 센다.** 같은 `queue_full` 이라도 처방이 다르다:
    //   바이트 초과 → `cap`(전선이 못 나른다) · 건수 초과 → 장치 ACK 배출률(장치가 못 삼킨다)
    //   합쳐 놓으면 `cap` 을 올려 놓고 왜 안 낫는지 몇 시간 찾게 된다(§8.23-(38) 과 같은 함정).
    long long q_full_n;          // 그중 **건수 축**으로 거절한 수
    int       s_max_b;           // 관측된 `S` 프레임 최대 길이 — 배출 6 의 전제 감시
    bool      s_worst_warned;    // 전제 붕괴 경고를 한 번만 낸다
    long long q_nodev;           // 장치 연결이 없어 거절한 수
    long long q_dup;             // already_pending 로 거절한 수
    // ⚠ **창 수**다(창당 1). 줄 수로 세면 같은 줄이 창마다 다시 세어져 누계가 부풀려진다 —
    // 원장 §4.2 의 "28건은 실질 3개 사건" 과 같은 형태의 함정이다.
    long long q_deferred;        // 바이트 상한에 걸려 뭔가를 남긴 **창의 수**
    long long q_dropped_link;    // 세션이 끝나 큐에서 버린 줄 수 — **조용히 사라지지 않게 센다**
    // ⚠ **RTT 를 추측하지 않는다.** `pend` 에 `sent_ms` 가 있고 ACK 도착 시각을 아니까
    // **왕복이 실측으로 쌓인다.** 그 값은 `RTT + 장치 처리시간` 이라 **RTT 의 상한**이고,
    // 창을 좁히는 방향이라 **보수적 = 안전**하다.
    // 🔑 세션마다 초기화한다 — 옛 세션의 병리적 한 건이 새 세션의 창을 영구히 조이면 안 된다.
    long long rtt_max_ms;        // 이 세션의 최대 왕복(창 계산에 쓰는 값)
    long long rtt_last_ms;       // 마지막 왕복 — 보고용
    long long rtt_n;             // 표본 수. **0 이면 "안 쟀다"이지 "0ms"가 아니다**
    long long win_skips;         // 창을 이미 지나 다음 `S` 를 기다린 횟수
    long long dmax_flushes;      // `S` 가 안 와서 창을 포기하고 쏜 횟수
    // 🔴 **창 포기도 박자를 지킨다.** `tick()` 은 200ms 마다 도는데 조건이 "조용하다" 하나면
    // 장치가 침묵하는 내내 **200ms 버스트**가 된다(재전송이 1500ms 마다 큐를 채우므로 상시화된다).
    // 그러면 **장치가 가장 힘들어하는 구간에서 슬롯 규율이 통째로 꺼진다** —
    // ⚠ **서버에 조용한 것은 장치가 송신 안 한다는 뜻이 아니다**(`busy s...`·`[TX-RESYNC]` 가
    // 정확히 그 상태다). 설계 §3 의 "포기하고 쏜다"는 단수다. 슬롯당 1회로 묶는다.
    long long last_dmax_ms;
    // 🔴 **창 포기는 침묵 한 번에 한 번이다** (REQ-0210 · 슬롯당 1회에서 바꿈)
    // 종전 규칙은 **침묵이 길수록 더 많이 쏘았다**: 8초 침묵이면 2.4초에 첫 포기, 그 뒤
    // 슬롯마다 한 번 = **약 5회**. 하필 **장치가 가장 못 받는 구간에서 가장 많이 쏜 것**이다.
    // 🔑 **재시도는 조건이 바뀌었을 때 뜻이 있다. `S` 가 안 온 동안 조건은 안 바뀐다** —
    //   같은 근거로 다시 쏘는 것은 새 시도가 아니라 같은 시도의 반복이다.
    // → 장치가 **한 줄이라도 말하면** 다시 무장한다(그것이 "조건이 바뀌었다"의 관측 가능한 형태).
    bool      dmax_armed;
    // §9.1 `device.last_frame_ts` 전용 — **epoch 시각**이다. ard_last_ms 를 쓰면 안 된다:
    // 그건 now_ms() 기반(윈도우에서는 부팅 후 경과)이라 바깥으로 나가면 수십 년짜리 값이 된다(28행 경고).
    long long&   ard_last_epoch_ms = park.last_epoch_ms;
    long long ard_uptime;
    long  ard_seq;
    std::string ard_dev;

    // ── 세션을 가로지르는 uptime 기억 (REQ-0118 (A)) ──────────────────────────
    // **왜 별도 필드인가**: `ard_uptime` 은 재연결 때 -1 로 버려진다. 그건 결함이 아니라
    // §7.4 가 의도한 것이다 — 기준선 없이 판정하면 연 480회 오탐이 돌아온다.
    // 그래서 그 필드는 **손대지 않는다.** 대신 세션을 넘겨 살아남는 기억을 따로 둔다.
    //
    // **무엇을 고치는가**: 지금 서버는 재연결을 전부 "재부팅"이라고 부른다. 실제로는
    // 08-16 기준선에서 새 연결 36건 중 **35건이 재부팅이 아니라 링크 재접속**이었다.
    // 17:21:36 에 서버가 "재부팅 감지"를 찍었지만 uptime 은 1286 → 1295 로 **늘었다.**
    // 장치는 죽지 않았는데 장부에는 재부팅으로 남는다.
    //
    // ⚠ **이 값은 보고 전용이다. 어떤 판정에도 먹이지 않는다.**
    //   `uptime_says_reboot()` 이 보는 것은 여전히 `ard_uptime` 뿐이다.
    long long xs_uptime;          // 직전 세션에서 마지막으로 본 uptime (-1 = 기억 없음)
    long long xs_last_ms;         // 그 프레임이 **도착한** 시각(now_ms). 공백 G 를 재는 데 쓴다
    std::string xs_dev;           // 그 uptime 의 주인 — 다른 장치면 비교하지 않는다
    long long xs_reconnect_reboot;   // 재연결인데 uptime 이 되감김 = 진짜 재부팅
    long long xs_reconnect_link;     // 재연결인데 uptime 이 이어짐 = 링크만 다시 선 것
    long long xs_reconnect_unknown;  // 기억이 없거나 장치가 달라 못 가른 것

    // ── 이음매 큐 (REQ-0096 단계 C) ────────────────────────────────────────────
    // 디바이스 계층이 도메인 함수를 **직접 부르지 않게** 하는 것이 이 단계의 목표다.
    // "장치에서 이런 일이 있었다"만 담고, 그것이 무엇을 뜻하는지는 도메인이 정한다
    // (`server_seam.h` 의 계약). 소비는 `run()` 루프 끝에서 한 번에 한다.
    //
    // ⚠ 단계 C 는 **한 종류씩** 옮긴다. 지금 옮긴 것은 `DEV_DISCONNECT` 뿐이고
    // 나머지 호출은 아직 직접 호출로 남아 있다 — 중간 상태지만 **매 조각이 동작한다.**
    std::vector<DeviceEvent> pending_events;

    std::map<sock_t, Conn> conns;      // HTTP/WS 클라이언트
    std::map<uint16_t, Pending> pend;
    // 🔴 발행 커서. **단조 증가**하고 전선에 나가는 값은 `rid_cursor % RID_SPACE` 다.
    //   단조로 두는 이유: 디스크에 적을 때 "몇 바퀴째인지"가 필요 없어지고, 재시작 이어받기가
    //   **더하기 하나**가 된다. 순환값만 들면 "앞"과 "뒤"가 모호해진다.
    long long rid_cursor;
    long long rid_reserved_to;         // 디스크에 **미리 적어 둔** 상한. 넘으면 새 블록을 예약한다
    bool      rid_persist_on;          // 🔴 기본 false — `rid_cursor_load()` 를 부른 실기 경로에서만 켠다
    std::string rid_cursor_file;       // 절대경로. 빈 값이면 영속 불가
    // 격리표 — rid → (재사용 가능 시각, **해제 순번**). `pend` 를 떠날 때 여기 들어간다.
    //
    // 🔴 **순번을 시각과 따로 든다.** 처음엔 시각 하나만 들고 "가장 이른 시각"을 가장 오래 묵은
    //   것으로 삼았는데 **자가검증이 그것을 깨뜨렸다**: 발행이 빠르면 `now_ms()` 가 같은 값을
    //   돌려주어 **동률**이 되고, 동률에서는 늘 같은 칸이 뽑혀 **재사용 간격이 1** 이 됐다.
    //   ⚠ 그 값이면 장치 멱등창(16) 안에서 재사용이 일어나 **명령이 조용히 삼켜진다.**
    //   🔑 **시각으로 순서를 매기면 시계 해상도가 곧 순서다.** 순서가 필요하면 순번을 따로 들어라.
    struct RidQ {
        long long until_ms;            // 이 시각 전에는 재사용하지 않는다
        long long seq;                 // 해제 순번 — **강제 해제 때 FIFO 를 보장한다**
        RidQ() : until_ms(0), seq(0) {}
    };
    std::map<uint16_t, RidQ> rid_quar;
    long long rid_rel_seq;             // 해제 순번 발급기(단조)
    long long rid_alloc_n;             // 발행 누계 (분모)
    long long rid_skips;               // 발행하려다 pend/격리라 건너뛴 횟수
    long long rid_forced;              // 🔴 공간이 차서 격리를 조기 해제한 횟수. 0 이 정상
    long long rid_exhausted;           // 🔴 pend 가 공간을 다 먹어 발행 자체를 못 한 횟수
    long long ack_unknown_rid;         // pend 에 없는 rid 의 ACK — 늦은 ACK/재전송 중복
    long long ack_slot_mismatch;       // 🔴 ACK 에코 자리 ≠ 서버가 보낸 자리 (멱등 캐시 서명)
    long long mod_name_conflict;       // 🔴 다른 노드가 이미 잡힌 모듈 이름을 다시 주장 (REQ-0260 전까지의 한계)
    // 🔴 **누적이 아니라 계기(gauge)다** — "지금 갈린 자리가 몇 개인가".
    //   처음에 `state` 를 낼 때마다 올리는 누적으로 만들었더니 **방송 횟수를 세고 있었다**
    //   (실측 `센서갈림 32` — 사건은 둘인데 방송이 32번이었다).
    //   🔑 **파생 상태는 사건이 아니다. 사건처럼 세면 방송 빈도에 비례해 부풀고 창끼리 비교가 안 된다.**
    int  sensor_split_now;             // 지금 두 센서가 갈린 주차 자리 수
    long long not_reservable_n;        // 자리가 아닌 id 로 온 예약/취소 요청 (B* 등)
    std::string last_bits;             // data_log 중복 쓰기 방지

    // §7.5 예약 은퇴 — occupied 1→0 전이를 보려면 직전 프레임이 기준선으로 필요하다.
    // base_valid=false 면 기준선이 없다는 뜻이고, 그 프레임에서는 판정하지 않는다.
    int  base_occ[10];
    bool base_valid;

    // §12A 테스트 모드. **이 두 값의 출처는 S 프레임의 tmask 이지 서버의 기억이 아니다.**
    // T 의 ACK 를 받았다고 무장됐다고 단정하면, 해제 ACK 를 놓쳤을 때 서버는 "해제됨"이라
    // 믿고 화면은 가짜 값을 경고 없이 진짜로 표시한다 — §12A.6 이 막으려는 사고 자체다.
    // tmask 를 진실로 삼으면 다음 프레임에서 저절로 맞춰진다(§12A.4).
    bool test_armed;
    int  test_ovr[10];        // 1 = 그 칸의 occupied 는 주입된 값
    int  base_ovr[10];        // 직전 프레임의 오버라이드 비트 — 되돌림 판정용(§7.5-3)

    // --selftest 전용 이음매 두 개. **판정에는 절대 관여하지 않는다.**
    // resync_count : 재동기화가 실제로 몇 번 일어났는지 세어 §7.4 오탐을 증명한다.
    // no_disk      : 자가검증이 작업 디렉터리의 data_log.json 을 덮어쓰지 않게 막는다.
    int  resync_count;
    bool no_disk;

    // ---------- 소크 관측 (REQ-0065)
    // "2시간 안 끊겼다"는 **로그가 증명해야** 한다. 접속·종료 순간만 찍히면 그 사이의 침묵을
    // 아무도 증언하지 않는다 — 조용한 로그와 죽은 링크가 똑같이 보인다.
    // 핵심 지표는 **프레임 수신 간 최대 공백**이다. 평균은 링크가 반쯤 죽어도 예쁘게 나온다.
    long long soak_start_ms;      // 프로세스 기동(now_ms)
    int  ard_sessions;            // ARD 접속 횟수
    long long sess_start_ms;      // 현 세션 시작. 0 = 세션 없음
    long long sess_frames;        // 현 세션 수신 줄
    long long sess_last_line_ms;  // 현 세션 마지막 줄 수신 시각
    long long sess_max_gap_ms;    // 현 세션 최대 공백
    long long all_frames;         // 누적 수신 줄
    long long all_max_gap_ms;     // 누적 최대 공백 — **세션 경계는 넘지 않는다**(끊긴 시간은 공백이 아니다)
    long long all_max_gap_at;     // 그 공백이 끝난 시각(epoch_ms)
    long long link_down_ms;       // ARD 연결이 없던 누적 시간
    long long link_down_since;    // 0 = 연결돼 있음
    // 재부팅 감지를 **원인별로** 센다. §7.4 는 "새 연결이 1차 신호, uptime 추론은 2차 방어선"이라고
    // 정했는데, 그 주장이 실기에서 맞는지는 이 두 숫자의 비율로만 확인된다.
    // (불변식: reboot_by_conn + reboot_by_uptime == resync_count)
    int  reboot_by_conn;
    int  reboot_by_uptime;
    int  offline_episodes;        // 3.5초 무프레임 판정 횟수(§3.4)
    // ⚠ 아래 셋은 **모순 대조용**이다(죽은 탐지기 규칙 5). 상행만 세면
    // "조용한 링크"와 "시끄럽지만 전부 버려지는 링크"(AT 잡음 §6.2)가 요약에서 똑같아 보이고,
    // 상행만 보면 **하행이 통째로 죽어도 요약이 멀쩡하다**(실기에서 +IPD 0건이던 바로 그 경우).
    // 버린 줄은 **원인별로** 센다. 합만 세면 "전선에서 깨졌다"와 "장치가 프로토콜 아닌 것을
    // 흘린다"가 같은 숫자가 된다 — 진단이 정반대인데도. 특히 `모름` 이 오르면 AT 응답 같은
    // 비프로토콜 텍스트가 소켓에 새고 있다는 뜻이라 체크섬 손상과 완전히 다른 사건이다.
    long long drop_cksum;         // 체크섬 불일치 — 전선/장치에서 바이트가 깨졌다
    long long drop_overlong;      // 64B 초과 줄(§2.1)
    long long drop_unknown;       // 모르는 타입 문자 — 비프로토콜 텍스트 유입 의심
    long long drop_noise;         // LF 없이 64B 초과 → 버퍼 비움(잡음)
    // 승격 전(id 미상) 소켓에서 버린 줄 (REQ-0118 (F)).
    // **왜 필요한가**: 이 경로의 줄은 지금까지 **어느 카운터도 안 올리고 사라졌다.**
    // 그래서 "안 왔다"와 "왔는데 못 셌다"가 구별되지 않았다 — 08-16 에 `TX-RESYNC 4 : 버린줄 0`
    // 을 두고 팀이 판정을 못 내린 원인이 이것이다. 그리고 **마감된 소켓 56건이 전부 이 경로**라
    // 지금 그 소켓들이 무엇을 받았는지 아무도 모른다. 0 이 "해당 없음"인지 "못 셈"인지 가른다.
    long long drop_prepromo;      // 승격 전 소켓에서 버린 줄(체크섬 실패·비S프레임 포함)
    long long drop_prepromo_buf;  // 승격 전 소켓 버퍼가 상한을 넘겨 통째로 비운 횟수
    long long retx_count;         // 하행 재전송 횟수(§7.3)
    long long ack_fail_count;     // 하행 ACK 최종 실패 횟수
    bool ard_online;              // 온·오프라인 **엣지** 판정용 직전 상태(§3.4). 아래 루프 주석 참조
    long long last_report_ms;     // 주기 보고 시각

    // ---------- 복구 계측 (REQ-0072)
    // 왜 "최대공백"으로 부족한가 — 공백은 **얼마나 나빴나**만 말하고 **스스로 돌아왔나**를
    // 말하지 않는다. 사용자 요구가 "오류에서 정상으로 복구하는 구조"로 바뀌었으므로
    // 판정 지표도 바뀌어야 한다: **끊긴 뒤 몇 초 만에 저절로 프레임이 다시 왔는가.**
    // 이 숫자가 없으면 장치 쪽 복구 사다리(REQ-0071)가 듣는지를 사람이 로그를 눈으로 세어
    // 판정하게 된다 — 실제로 오늘 그렇게 했다.
    long long offline_since_ms;   // 오프라인이 **시작된** 시각(now_ms). 0 = 온라인
    int  offline_at_session;      // 그 순간의 ard_sessions — 같은 연결/재연결 복구를 가른다
    std::vector<long long> recov_ms;  // 복구시간 표본(중앙값용). RECOV_SAMPLE_MAX 에서 멈춘다
    long long recov_dropped;      // 상한을 넘겨 못 담은 표본 수 — 조용히 버리지 않는다
    int  recov_same_conn;         // 같은 TCP 연결에서 프레임이 되돌아왔다(링크가 살아 있었다)
    int  recov_reconn;            // 재연결한 뒤에야 되돌아왔다(사다리가 소켓을 다시 세웠다)
    long long recov_worst_ms;     // 최악 복구시간
    int  zombie_reaps;            // 유휴 마감으로 회수한 소켓 수 — **앱 경로**
    int  keepalive_reaps;         // ETIMEDOUT 으로 죽은 소켓 수 — **OS 경로**

    // ⚠ `srv_id` 는 **기동마다 달라야 한다.** 벽시계 + 소스 식별자를 쓴다 —
    //   시각만 쓰면 같은 초에 두 번 뜨면 겹치고, 소스 식별자만 쓰면 재기동이 구분 안 된다.
    void init_srv_id() {
        char b[96];
        snprintf(b, sizeof(b), "%s-%lld", BUILD_ID, (long long)epoch_ms());
        srv_id = b;
    }

    // 🔑 `park` 의 필드들은 **`Node` 의 생성자가 초기화한다.** 여기 목록에 다시 적지 않는다 —
    //   적으면 참조에 임시값을 묶으려는 것이 되어 컴파일이 안 된다(그게 이 설계의 안전장치다).
    Server() : grid_rows(5), grid_cols(5), map_epoch(0),   // 선언 순서(-Wreorder)
               getmap_win_ms(0), getmap_in_win(0), getmap_rejects(0),
               lsn_ard(BAD_SOCK), lsn_http(BAD_SOCK), lsn_phone(BAD_SOCK),
               reg_ok(0), reg_bad(0), reg_qsent(0), reg_giveups(0), reg_widthbad(0),
               occ_undecoded(0), occ_undecoded_warned(false),
               mod_order_changed(0),
               gate_q(0), gate_sent(0), gate_ans(0), dev_reject(0),
               heal_checks(0), heal_fires(0), res_undecoded(0), res_undecoded_warned(false),
               dup_devid_reject(0), takeover_grace(0),  // 선언 순서와 일치시킨다(-Wreorder)
               aux_conflicts(0), admit_rejects(0),
               // 🔴 이 여섯(+다섯)은 **선언만 돼 있고 초기화 목록에 없었다.** 쓰기 시작하는 순간
               // 쓰레기값이 지표로 나간다 — 원장이 통째로 경고하는 그 형태다. 여기서 닫는다.
               downq_bytes(0), batch_count(0), batch_lines(0),
               q_rejected(0), q_full_n(0), s_max_b(0), s_worst_warned(false),
           q_nodev(0), q_dup(0),
               q_deferred(0), q_dropped_link(0),
               rtt_max_ms(0), rtt_last_ms(0), rtt_n(0), win_skips(0), dmax_flushes(0),
               last_dmax_ms(0), dmax_armed(true),
               ard_uptime(-1), ard_seq(-1),
               ard_dev("?"),
               xs_uptime(-1), xs_last_ms(0), xs_dev(""),
               xs_reconnect_reboot(0), xs_reconnect_link(0),
               xs_reconnect_unknown(0),
               rid_cursor(1), rid_reserved_to(0), rid_persist_on(false),
               rid_rel_seq(0),
               rid_alloc_n(0), rid_skips(0), rid_forced(0), rid_exhausted(0),
               ack_unknown_rid(0), ack_slot_mismatch(0), mod_name_conflict(0), sensor_split_now(0), not_reservable_n(0),
               base_valid(false), test_armed(false),
               resync_count(0), no_disk(false),
               soak_start_ms(0), ard_sessions(0), sess_start_ms(0), sess_frames(0),
               sess_last_line_ms(0), sess_max_gap_ms(0), all_frames(0), all_max_gap_ms(0),
               all_max_gap_at(0), link_down_ms(0), link_down_since(0),
               reboot_by_conn(0), reboot_by_uptime(0), offline_episodes(0),
               drop_cksum(0), drop_overlong(0), drop_unknown(0), drop_noise(0),
               drop_prepromo(0), drop_prepromo_buf(0),
               retx_count(0), ack_fail_count(0),                  // 선언 순서와 일치시킨다(-Wreorder)
               ard_online(false), last_report_ms(0),
               offline_since_ms(0), offline_at_session(0), recov_dropped(0),
               recov_same_conn(0), recov_reconn(0), recov_worst_ms(0),
               zombie_reaps(0), keepalive_reaps(0) {
        for (int i = 0; i < 10; i++) { base_occ[i] = 0; test_ovr[i] = 0; base_ovr[i] = 0; }
    }

    // ---------- 소크 관측 보조 (REQ-0065)
    static std::string hms(long long ms) {
        if (ms < 0) ms = 0;
        long long s = ms / 1000;
        char b[32];
        snprintf(b, sizeof(b), "%02lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
        return std::string(b);
    }
    static std::string secs(long long ms) {
        char b[32];
        snprintf(b, sizeof(b), "%.1f초", ms / 1000.0);
        return std::string(b);
    }
    // 요약 안에서 "언제"를 가리키는 값(최대공백@… 등)도 **줄 앞머리와 같은 완전 형식**으로 쓴다.
    // v0.1 에서 연도를 뺐다가 monitor 지적으로 되돌렸다: "경계 줄이 연도를 확정해 준다"는
    // **파일을 처음부터 순서대로 읽을 때만** 참인데, 관측자는 5MB 를 통째로 못 올려서
    // **항상 잘라 읽는다.** 잘린 조각에서 `@08-16 16:52:47` 은 다시 연도가 없다.
    // 이 계약의 요지는 **부분 문자열만 봐도 절대시각이 확정되는 것**이므로 예외를 두지 않는다.
    // 비용은 요약이 분당 1줄이라 줄 앞머리의 1/60 이다.
    static std::string clock_at(long long ep_ms) {
        if (ep_ms <= 0) return std::string("-");
        time_t tt = (time_t)(ep_ms / 1000);
        char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", localtime(&tt));
        return std::string(b);
    }

    // ⚠ **아직 닫히지 않은 침묵도 공백이다.** 공백을 "줄과 줄 사이"로만 세면
    // 장치가 조용히 멈춘 바로 그 사고가 최대공백에 **안 잡힌다** — 다음 줄이 영영 안 오기 때문이다.
    // (실제로 겪었다: 22분간 무프레임이었는데 세션 최대공백이 3.3초로 찍혔다.)
    long long live_gap_ms() const {
        if (!sess_start_ms || !sess_last_line_ms) return 0;
        return now_ms() - sess_last_line_ms;
    }
    void fold_live_gap() {                       // 진행 중이던 침묵을 확정한다
        long long g = live_gap_ms();
        if (g > sess_max_gap_ms) sess_max_gap_ms = g;
        if (g > all_max_gap_ms) { all_max_gap_ms = g; all_max_gap_at = epoch_ms(); }
    }

    // ---------- 복구 계측 보조 (REQ-0072)
    // **평균이 아니라 중앙값을 쓴다.** 복구시간은 한쪽으로 길게 끌리는 분포다(대부분 몇 초,
    // 가끔 재연결로 수십 초). 평균은 그 꼬리 하나에 끌려가 "보통 얼마나 걸리나"를 못 말한다.
    // 최악값은 따로 낸다 — 중앙값과 최악을 **같이** 봐야 "대체로 빠른데 가끔 나쁘다"가 읽힌다.
    long long recov_median_ms() const {
        if (recov_ms.empty()) return -1;                  // -1 = 표본 없음(0 과 구별한다)
        std::vector<long long> v = recov_ms;              // 정렬이 원본을 흔들면 안 된다
        size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + mid, v.end());
        if (v.size() % 2) return v[mid];
        long long hi = v[mid];
        std::nth_element(v.begin(), v.begin() + (mid - 1), v.end());
        return (v[mid - 1] + hi) / 2;
    }

    // 아두이노 소켓의 유휴를 재는 기준 시각.
    // ⚠ **`ard_last_ms`(체크섬을 통과한 S 프레임)를 쓴다.** 이유는 ARD_IDLE_CLOSE_MS 주석에 있다 —
    // 모든 수신 줄(`sess_last_line_ms`)로 재면 잡음을 흘리는 장치가 영원히 회수되지 않는다.
    // `sess_start_ms` 와 큰 쪽을 쓰는 이유는 둘이다:
    //   · 붙기만 하고 **한 줄도 안 보내는** 연결도 회수 대상이다(그 자체로 누수 경로다).
    //   · `ard_last_ms` 는 세션 경계에서 리셋되지 않는다. 새 연결 직후 옛 값으로 재면
    //     **갓 붙은 정상 연결을 그 자리에서 끊는다.** sess_start_ms 가 그 사고를 막는다.
    long long ard_idle_base_ms() const {
        return (ard_last_ms > sess_start_ms) ? ard_last_ms : sess_start_ms;
    }

    // 아두이노 소켓에만 keepalive 를 건다. **모든 accept 에 일괄로 걸지 않는다** —
    // HTTP/WS/폰 연결의 수명 정책은 이 요청의 범위가 아니고, 조용히 바꿀 일도 아니다.
    //
    // 반환값은 **OS 가 실제로 받아들인 값을 되읽은 것**이다(요청한 값이 아니다).
    // setsockopt 는 조용히 무시되거나 값이 잘릴 수 있고, 그러면 "켰다고 믿는데 안 켜진"
    // 상태가 된다 — 로그에 요청값을 찍으면 그 거짓말을 그대로 기록하게 된다.
    // 되읽어 찍으면 켜졌는지 아닌지가 로그만 보고 판정된다.
    static std::string set_keepalive(sock_t s) {
        int on = 1;
        if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&on, sizeof(on)) != 0)
            return "keepalive 설정 실패(errno=" + std::to_string(sockerr()) + ")";
#ifdef _WIN32
        // ⚠ 윈도우는 SO_KEEPALIVE 만으로는 **기본 유휴가 2시간**이라 사실상 안 걸린다.
        // 세부 조정은 SIO_KEEPALIVE_VALS(mstcpip.h)가 필요한데 **이 기기에서 컴파일 검증을
        // 할 수 없어 넣지 않았다.** 그래서 윈도우에서는 OS 경로가 하중을 받지 못하고
        // 유휴 마감(ARD_IDLE_CLOSE_MS)이 **유일한** 회수 장치가 된다 — 명세에도 그렇게 적었다.
        // 안 되는 것을 된다고 적는 것보다, 못 한 것을 못 했다고 적는 편이 낫다.
        return "keepalive on(윈도우 기본 유휴 2시간 — 세부조정 없음, 유휴 마감이 주 장치)";
#else
        int idle = KEEPALIVE_IDLE_S, intvl = KEEPALIVE_INTVL_S, cnt = KEEPALIVE_CNT;
  #ifdef TCP_KEEPIDLE                        // 리눅스
        setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, (const char*)&idle, sizeof(idle));
        const int IDLE_OPT = TCP_KEEPIDLE;
  #elif defined(TCP_KEEPALIVE)               // macOS/BSD — 같은 뜻의 다른 이름이다
        setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE, (const char*)&idle, sizeof(idle));
        const int IDLE_OPT = TCP_KEEPALIVE;
  #endif
  #ifdef TCP_KEEPINTVL
        setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&intvl, sizeof(intvl));
  #endif
  #ifdef TCP_KEEPCNT
        setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, (const char*)&cnt, sizeof(cnt));
  #endif
        // ---- 되읽기. 여기서 찍는 숫자는 **커널이 들고 있는 값**이다.
        int ron = 0, ridle = -1, rintvl = -1, rcnt = -1;
        socklen_t sl = sizeof(int);
        getsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&ron, &sl);
  #if defined(TCP_KEEPIDLE) || defined(TCP_KEEPALIVE)
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, IDLE_OPT, (char*)&ridle, &sl);
  #endif
  #ifdef TCP_KEEPINTVL
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, (char*)&rintvl, &sl);
  #endif
  #ifdef TCP_KEEPCNT
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, (char*)&rcnt, &sl);
  #endif
        char b[160];
        snprintf(b, sizeof(b),
                 "keepalive %s · 유휴 %ds · 간격 %ds · 횟수 %d → 탐지 약 %ds (커널 되읽기)",
                 ron ? "on" : "**off**", ridle, rintvl, rcnt,
                 (ridle > 0 && rintvl > 0 && rcnt > 0) ? ridle + rintvl * rcnt : -1);
        return std::string(b);
#endif
    }

    // recv 가 -1 을 준 이유가 **keepalive 가 죽였기 때문인지**를 가른다.
    // 안 가르면 "수신 오류" 한 문자열에 전부 섞여 keepalive 가 한 번이라도 일했는지
    // 증명할 길이 없어진다 — 켜 놓고 안 켜진 것과 구별이 안 되는 계측은 계측이 아니다.
    static bool err_is_timeout(int e) {
#ifdef _WIN32
        return e == WSAETIMEDOUT;
#else
        return e == ETIMEDOUT;
#endif
    }

    // 한 줄로 소크 전체를 재구성할 수 있게 한다. 주기 보고와 종료 요약이 같은 문장을 쓴다.
    std::string soak_line() const {
        long long t = now_ms();
        long long gap_max = all_max_gap_ms;
        long long live = live_gap_ms();
        if (live > gap_max) gap_max = live;       // 진행 중인 침묵도 포함해서 보고한다
        long long down = link_down_ms + (link_down_since ? t - link_down_since : 0);
        std::string s = "소크 " + hms(t - soak_start_ms)
            + " · 세션 " + std::to_string(ard_sessions) + "회";
        if (sess_start_ms) s += "(현재 #" + std::to_string(ard_sessions)
                                + " 연결중 " + hms(t - sess_start_ms) + ")";
        else               s += "(현재 끊김)";
        s += " · 프레임 " + std::to_string(all_frames)
           + " · 최대공백 " + secs(gap_max) + (live > all_max_gap_ms ? "(진행중)" : "@" + clock_at(all_max_gap_at))
           + " · 재부팅감지 " + std::to_string(reboot_by_conn + reboot_by_uptime)
           + "(새연결 " + std::to_string(reboot_by_conn)
           + " / uptime추론 " + std::to_string(reboot_by_uptime) + ")"
           // ⚠ 위 `재부팅감지` 는 **새 연결을 전부 재부팅으로 센다.** 그게 과대집계라는 것이
           // 08-16 에 밝혀졌고(36건 중 35건이 링크 재접속), 아래가 실제로 갈라 놓은 값이다.
           // 위 필드는 옛 집계 도구 호환을 위해 **형태를 그대로 두었다** — 이름 정정은
           // 계약 변경이라 monitor 와 합의 후 별도 교체로 한다(REQ-0118 (A) 2단계).
           + " · 재연결내역(재부팅 " + std::to_string(xs_reconnect_reboot)
           + "/링크재접속 " + std::to_string(xs_reconnect_link)
           + "/미상 " + std::to_string(xs_reconnect_unknown) + ")"
           + " · 오프라인 " + std::to_string(offline_episodes) + "회"
           + " · 링크없음 " + hms(down)
           // 상행만 보면 하행이 통째로 죽어도 요약이 멀쩡하다. 네 숫자를 나란히 둔다.
           // 버린줄은 합과 원인을 함께 — 원인이 갈리면 진단이 갈린다(잡음 유입 vs 바이트 손상).
           + " · 버린줄 " + std::to_string(drop_cksum + drop_overlong + drop_unknown + drop_noise)
           + "(체크섬 " + std::to_string(drop_cksum)
           + "/과길이 " + std::to_string(drop_overlong)
           + "/모름 " + std::to_string(drop_unknown)
           + "/잡음 " + std::to_string(drop_noise) + ")"
           // 승격 전 소켓에서 버린 줄 — 위 4칸 어디에도 안 잡히던 것이다(REQ-0118 (F)).
           // **0 이면 "안 왔다"가 확정된다.** 전에는 0 이 "못 셌다"일 수도 있었다.
           + " · 승격전버림 " + std::to_string(drop_prepromo)
           + "(버퍼비움 " + std::to_string(drop_prepromo_buf) + ")"
           + " · 재전송 " + std::to_string(retx_count)
           + " · ACK실패 " + std::to_string(ack_fail_count)
           // ── 하행 슬롯 큐. **분모를 같이 찍는다** — `거절 0` 이 "건강"인지 "한 번도 안 돌았다"인지
           // 갈리려면 `배치` 가 옆에 있어야 한다(원장 §5.2). 배치 0 이면 하행이 아예 없었던 것이다.
           + " · 배치 " + std::to_string(batch_count)
           + "(줄 " + std::to_string(batch_lines)
           + "/미룬창 " + std::to_string(q_deferred)
           + "/창포기 " + std::to_string(dmax_flushes)
           + "/창놓침 " + std::to_string(win_skips) + ")"
           // 🔴 **네 칸을 갈라 둔다** — 합치면 `큐거절 3` 을 보고 cap 을 올리려 하는데
           // 실제로는 장치가 없었던 것일 수 있다. 원인이 갈리면 진단이 갈린다.
           + " · 큐넘침 " + std::to_string(q_rejected)
           + "(건수축 " + std::to_string(q_full_n)
           + "/장치없음 " + std::to_string(q_nodev)
           + "/중복 " + std::to_string(q_dup)
           + "/링크버림 " + std::to_string(q_dropped_link) + ")"
           // 🔑 **한 번 뜨는 경고는 놓친다.** 주기 요약에 계속 보이게 둔다 —
           // 이 칸이 가정을 넘으면 하행 건수 상한의 근거가 이미 깨진 것이다.
           // 🔴 **0 이 아니면 그 창의 장치 지표는 읽지 마라** — 두 보드가 섞였다는 뜻이다.
           // 계측 신뢰도 지표라서 다른 칸보다 앞에 둔다.
           // 🔑 **등록은 지금 관측 전용이다**(하행 경로를 안 바꾼다). 그래서 계수만 낸다.
           // `완료 0` 이 "옛 펌웨어라 등록을 안 한다"인지 "새 펌웨어인데 실패한다"인지는
           // **`질의`·`포기` 가 옆에 있어야 갈린다** — 분모를 같이 찍는 그 규율이다.
           + " · 등록 완료 " + std::to_string(reg_ok)
           + "(형식오류 " + std::to_string(reg_bad)
           + "/질의 " + std::to_string(reg_qsent)
           + "/포기 " + std::to_string(reg_giveups)
           + "/폭불일치 " + std::to_string(reg_widthbad)
           + "/미해독 " + std::to_string(occ_undecoded)
           + (reg_widthbad ? "🔴" : "") + ")"
           + " · devid거절 " + std::to_string(dup_devid_reject)
           + (dup_devid_reject ? "🔴" : "")
           + "/유예교체 " + std::to_string(takeover_grace)
           + " · S최대 " + std::to_string(s_max_b) + "B/가정 "
           + std::to_string(DEV_S_WORST_ASSUMED_B) + "B"
           + (s_worst_warned ? "🔴" : "")
           + " · 대기 " + std::to_string(downq.size())
           + "줄(" + std::to_string(downq_bytes) + "B/" + std::to_string(downq_max_b()) + "B)"
           // 🔴 `RTT` 는 **실측이고 표본 수를 같이 낸다.** `n=0` 이면 그 값은 "0ms"가 아니라
           // **"안 쟀다"** 이고, 그때 `W_srv` 는 상한(600−여유)에 그대로 앉아 있다.
           + " · 왕복 최대 " + std::to_string(rtt_max_ms) + "ms"
           + "(마지막 " + std::to_string(rtt_last_ms)
           + "/n=" + std::to_string(rtt_n) + ")"
           + " · W_srv " + std::to_string(w_srv()) + "ms"
           + " · " + recovery_phrase()
           + " · 회수 " + std::to_string(zombie_reaps + keepalive_reaps)
           + "(유휴 " + std::to_string(zombie_reaps)
           + "/keepalive " + std::to_string(keepalive_reaps) + ")";
        // 🔴 **분모를 찍는다** — `치유 0/0` 과 `치유 0/1200` 은 완전히 다른 문장이다.
        //   앞은 **검사가 안 돈 것**(2026-08-19 에 7시간 그랬다)이고 뒤는 **건강한 것**이다.
        //   ⚠ 분자만 찍으면 둘이 똑같이 `0` 으로 보인다. 그래서 검사 수를 같이 낸다.
        // 🔑 **이름을 붙여 낸다.** `M − N` 은 성공이 아니다 — 무응답이 그 안에 섞인다.
        s += " · G 띄움 " + std::to_string(gate_q)
           + "·도달 " + std::to_string(gate_sent)
           + "·응답 " + std::to_string(gate_ans)
           + "·거절 " + std::to_string(dev_reject);
        // 🔴 **계수를 만들고 요약에 안 내보냈다**(2026-08-19 발견). monitor 에게 *"`순서변경` 을 봐라"* 라고
        //   알려 둔 상태였다 — **없는 칸을 보라고 한 것이다.** 그쪽 파서는 표지 기준이라 조용히 못 찾는다.
        //   🔑 §"조건을 적었으면 그것을 보는 감시를 같은 자리에 만들어라" 의 **한 걸음 뒤 판본**이다:
        //   감시는 만들었는데 **그 결과를 볼 자리를 안 만들었다.** 세는 것과 보이는 것은 다른 일이다.
        s += " · 순서변경 " + std::to_string(mod_order_changed);
        // 🔴 계수를 만들었으면 **볼 자리도 만든다**(원장 규칙 셋째). 분모는 `등록 완료` 다.
        s += " · 비자리예약 " + std::to_string(not_reservable_n)
           + (not_reservable_n > 0 ? " 🔴" : "");
        s += " · 센서갈림 " + std::to_string(sensor_split_now) + "자리"
           + (sensor_split_now > 0 ? " 🔴" : "");
        s += " · 이름충돌 " + std::to_string(mod_name_conflict)
           + (mod_name_conflict > 0 ? " 🔴" : "");
        // 🔴 **화면 수를 찍는다** (2026-08-19). `S` 처리 안에서 `push_snapshot`·`state` 방송이 돌고
        //   그 비용은 **붙어 있는 화면 수에 비례**한다. 창 M 에서 하행 송신 지연이
        //   `≥2ms 9.4% → 40.9%` 로 늘었는데 **그 축이 아무 데도 안 남아서 원인을 못 갈랐다.**
        //   ⚠ **없는 축은 사후에 복원할 수 없다.** 지금부터 남긴다.
        //   🔑 최대값을 같이 낸다 — 평균만 내면 **한 창에 몰린 접속이 안 보인다.**
        {
            int ws_n = 0;
            for (std::map<sock_t, Conn>::const_iterator it = conns.begin(); it != conns.end(); ++it)
                if (it->second.kind == Conn::WS) ws_n++;
            s += " · 화면 " + std::to_string(ws_n) + "(최대 " + std::to_string(ws_peak) + ")";
        }
        s += " · 치유 " + std::to_string(heal_fires) + "/" + std::to_string(heal_checks)
           + (heal_checks == 0 ? " 🔴검사0" : "")
           + " · 예약미해독 " + std::to_string(res_undecoded);
        // 🔴 A[1] — **세는 것과 보이는 것은 다른 일이다.** `mod_order_changed` 를 만들어 놓고
        //   요약에 안 내보내 **monitor 에게 없는 칸을 보라고 한** 사고가 있었다. 같은 것을 반복하지 않는다.
        //   🔑 **분모를 같이 낸다** — `발행` 이 없으면 `건너뜀 0` 은 건강이 아니라 **표본 0** 이다.
        //   각 칸이 낮아지는 *다른* 이유는 `docs/net/DESIGN-rid-width-and-quarantine.md` §5 표에 있다.
        s += " · rid 발행 " + std::to_string(rid_alloc_n)
           + "(다음 " + std::to_string((unsigned)(rid_cursor % RID_SPACE))
           + "/" + std::to_string((unsigned)RID_SPACE)
           + (rid_persist_on ? " 영속" : " 🔴영속꺼짐") + ")"
           + " 격리 " + std::to_string((long long)rid_quar.size())
           + " 건너뜀 " + std::to_string(rid_skips)
           + " 강제 " + std::to_string(rid_forced)
           + (rid_forced > 0 ? " 🔴" : "")
           + (rid_exhausted > 0 ? (" 고갈 " + std::to_string(rid_exhausted) + " 🔴") : "")
           + " · ACK 미상rid " + std::to_string(ack_unknown_rid)
           + " 자리불일치 " + std::to_string(ack_slot_mismatch)
           + (ack_slot_mismatch > 0 ? " 🔴" : "");
        return s;
    }

    // ---------- 복구 지표 문장 (REQ-0072)
    // **"끊긴 뒤 몇 초 만에 스스로 돌아왔는가"** 를 한 구절로 말한다.
    //
    // ⚠ `미복구` 는 정의상 **0 아니면 1**이다. 다음 오프라인 에피소드가 열리려면 그 전에
    // 온라인으로 올라와야 하고(엣지 판정), 온라인으로 올라왔다는 것이 곧 복구이기 때문이다.
    // 그래서 "미복구 Z회" 를 여러 건으로 부풀리지 않고 **지금 몇 초째 못 돌아오고 있는지**를
    // 같이 찍는다 — 요약에서 눈에 띄어야 할 것은 횟수가 아니라 그 진행 시간이다.
    // (진짜로 여러 번 셀 수 있는 "못 돌아왔다" 지표는 위의 `회수`(유휴 마감) 쪽이다.)
    std::string recovery_phrase() const {
        std::string s = "복구 " + std::to_string(recov_same_conn + recov_reconn) + "회";
        if (recov_same_conn + recov_reconn) {
            s += "(같은연결 " + std::to_string(recov_same_conn)
               + "/재연결 " + std::to_string(recov_reconn) + ")";
            long long med = recov_median_ms();
            s += " 중앙 " + (med < 0 ? std::string("-") : secs(med))
               + " 최악 " + secs(recov_worst_ms);
            if (recov_dropped) s += "(표본상한 초과 " + std::to_string(recov_dropped) + "건 제외)";
        }
        // 진행 중인 미복구는 **가장 시끄럽게** 적는다. 이번 사고에서 요약만 보고는 안 보였고
        // 프레임 수가 멈춘 것을 사람이 눈치채야 했다 — 그 실패를 되풀이하지 않으려는 칸이다.
        if (offline_since_ms)
            s += " · 🔴미복구 1회(진행 " + secs(now_ms() - offline_since_ms) + ")";
        else
            s += " · 미복구 0";
        return s;
    }

    // 세션이 끝나는 길은 두 갈래다(recv 실패, send 실패). 둘 다 이리로 모은다 —
    // 한쪽만 계측하면 "끊겼는데 세션이 계속 열려 있는" 장부가 만들어진다.
    // (why 를 const char* 에서 std::string 으로 넓혔다 — 끊긴 이유에 errno 를 실어야
    //  keepalive 가 죽인 것과 그냥 수신 오류를 로그에서 가를 수 있다. REQ-0072)
    void end_ard_session(const std::string& why) {
        if (sess_start_ms) {
            fold_live_gap();                     // 끝나지 않은 침묵을 공백으로 확정하고 닫는다
            logf("-ARD", "세션#" + std::to_string(ard_sessions) + " 종료(" + why + ") — 지속 "
                         + hms(now_ms() - sess_start_ms)
                         + " · 프레임 " + std::to_string(sess_frames)
                         + " · 최대공백 " + secs(sess_max_gap_ms)
                         + " · 상대 " + (ard_peer.empty() ? std::string("?") : ard_peer));
            sess_start_ms = 0;
        }
        if (!link_down_since) link_down_since = now_ms();
        // 설계 §2 — **세션이 끝나면 하행 큐를 비운다.** 옛 큐를 새 세션에 쏘면 장치가 모르는
        // 상태에 명령이 떨어진다. 비운 항목은 `device_offline` 로 **반드시 답한다**(§4-B 보장).
        // 🔴 **장치가 사라지면 모듈 상태를 모른다.** 안 지우면 `state` 가 끊긴 뒤에도
        //   `known:true` 로 낡은 값을 **사실로 주장한다** — 차단봉이 열려 있다고 그려 놓고
        //   실제로는 볼 수 없는 상태다. **모르면 덜 주장한다**(같은 블록에 적어 둔 규칙이다).
        //   ⚠ `z.modules` 는 남는다(지형은 유지) — **없어지는 것은 값이지 구조가 아니다.**
        park.mod_bits_n = 0;
        gate_want.clear();          // 대조할 기준도 이 세션 것이다. 다음 세션으로 넘기지 않는다
        clear_downq("세션 종료");
    }

    bool device_online() const {
        return ard != BAD_SOCK && ard_seen && (now_ms() - ard_last_ms) < OFFLINE_MS;
    }

    // ---------- 다중 노드 (REQ-0083) ----------------------------------------
    // §2.3 `devid ::= 1*8( ALPHA / DIGIT / "_" / "-" )`
    static bool valid_devid(const std::string& d) {
        if (d.empty() || d.size() > 8) return false;
        for (size_t i = 0; i < d.size(); i++) {
            char c = d[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    // 한 줄에서 device_id 를 꺼낸다. **체크섬을 통과한 `S` 프레임만 믿는다.**
    // 접속 직후에는 AT 잡음이 섞여 들어오므로(§6.2) 아무 줄이나 신뢰하면
    // **쓰레기가 device_id 가 되어** 그 이름으로 노드 자리를 하나 영구히 잡아먹는다.
    static bool peek_devid(const std::string& line, std::string& out) {
        std::vector<std::string> f;
        if (!verify_line(line, f)) return false;
        if (f.size() < 6 || f[0] != "S") return false;
        if (!valid_devid(f[5])) return false;
        out = f[5];
        return true;
    }

    // 주차 노드 자리를 넘겨받는다. **옛 소켓 대체는 같은 device_id 일 때만** 일어난다 —
    // 그것이 REQ-0083 이 고치는 핵심이다(옛 구조는 id 를 모른 채 무조건 대체했다).
    void adopt_as_parking(sock_t c, const std::string& dev) {
        if (ard != BAD_SOCK) {
            closesock(ard); conns.erase(ard);
            end_ard_session("같은 device_id(" + dev + ") 재접속으로 대체");
        }
        ard = c; ard_buf.clear();
        park_dev = dev;
        ard_sessions++;
        sess_start_ms = now_ms();
        sess_frames = 0; sess_last_line_ms = 0; sess_max_gap_ms = 0;
        // 창 계산에 쓰는 왕복 표본은 **세션마다 새로 쌓는다** — 옛 세션의 병리적 한 건이
        // 새 세션의 창을 영구히 조이면, 창이 좁아진 이유를 나중에 아무도 설명 못 한다.
        rtt_max_ms = 0; rtt_last_ms = 0; rtt_n = 0;
        if (link_down_since) { link_down_ms += now_ms() - link_down_since; link_down_since = 0; }
        reboot_by_conn++;
        park.reg_reset();          // 🔑 세션이 새로 서면 등록도 처음부터다
        ard_peer = peer_str(c);
        logf("+ARD", "주차 노드 접속 — 세션#" + std::to_string(ard_sessions)
                     + " · device=" + dev + " · 상대 " + ard_peer);
        ard_seq = -1; ard_uptime = -1;
        base_valid = false;                 // §7.5-1
        // 🔴 **재하달을 담기 전에 큐를 비운다.** `ard` 가 이미 BAD_SOCK 이었던 경로에서는
        // `end_ard_session()` 이 안 불렸을 수 있어 **옛 큐가 새 세션으로 넘어온다.**
        // 그 경우 장치는 자기가 모르는 rid 의 명령을 받는다(설계 §2).
        clear_downq("새 세션 시작");
        resync_reservations("새 연결");
    }

    // 보조 노드 자리에 넣는다(상행 전용).
    void adopt_as_aux(sock_t c, const std::string& dev) {
        std::map<std::string, AuxNode>::iterator it = aux.find(dev);
        if (it != aux.end() && it->second.fd != BAD_SOCK) {
            closesock(it->second.fd);
            logf("-AUX", "보조 노드 " + dev + " — 같은 device_id 재접속으로 대체 (프레임 "
                         + std::to_string(it->second.frames) + ")");
        }
        AuxNode& a = aux[dev];
        // 🔴 **자기 `devid` 를 채운다** (2026-08-19 · ②-b 가 드러냈다).
        //   맵의 **키**에만 id 가 있고 노드 자신의 필드는 비어 있었다. 아무도 안 읽어서 안 보였다 —
        //   ②-b 로 보조 노드의 `D` 가 파서에 들어가자 `bind_modules(n)` 이 그 빈 값을 썼고
        //   **지형에 `("", "A1")` 같은 결속이 생겼다.** 로그도 `노드  등록 결속` 으로 나왔다.
        //   🔑 **읽는 사람이 없던 필드는 틀려도 안 보인다.** 새 독자가 생기는 순간 드러난다.
        a.devid = dev;
        a.fd = c; a.buf.clear();
        a.connected_ms = now_ms();
        a.last_ms = now_ms();               // 유휴 마감 기준선. 0 이면 즉시 회수 대상이 된다
        a.online = false;
        logf("+AUX", "보조 노드 접속 — device=" + dev
                     + " · 현재 노드 " + std::to_string(aux.size() + (ard != BAD_SOCK ? 1 : 0))
                     + "/" + std::to_string(MAX_ARD_NODES) + " · **상행 전용**(하행 경로 없음)");
    }

    // 주차 노드 버퍼를 줄 단위로 비운다. 수신 경로와 **똑같은 규칙**이어야 하므로
    // 승격 직후에도 이걸 부른다(첫 프레임이 1초 늦게 처리되는 일이 없게).
    void drain_ard_buf() {
        size_t i;
        while ((i = ard_buf.find('\n')) != std::string::npos) {
            std::string line = ard_buf.substr(0, i);
            ard_buf.erase(0, i + 1);
            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
            // 🔑 **버릴 때도 길이를 남긴다.** 예전에는 "64B 초과" 라고만 적고 버려서
            // **몇 바이트였는지**를 못 봤다 — `AT+CIPSEND` 잘림 시험에서 그것이 측정값이다.
            if (line.size() + 1 > (size_t)MAX_LINE) {
                drop_overlong++;
                char b[96];
                snprintf(b, sizeof(b), "상한(%dB) 초과 줄 — 버림 · **rx=%zuB**",
                         MAX_LINE, line.size());
                logf("!", b);
                continue;
            }
            if (!line.empty()) on_ard_line(park, line);
        }
        if (ard_buf.size() > (size_t)MAX_LINE) {
            drop_noise++;
            logf("!", "LF 없이 64B 초과 — 버퍼 비움");
            ard_buf.clear();
        }
    }

    // id 미상 소켓 마감 + 보조 노드 유휴 회수 (REQ-0083).
    // 주차 노드의 유휴 마감(§3.5)과 **같은 근거·같은 상수**를 쓴다 — 노드마다 다른 규칙을 두면
    // "왜 저 노드만 안 끊기지"를 나중에 아무도 설명 못 한다.
    void reap_nodes() {
        long long t = now_ms();
        for (size_t k = 0; k < unknown.size(); ) {
            if (unknown[k].fd != BAD_SOCK && t - unknown[k].since_ms >= UNKNOWN_TIMEOUT_MS) {
                logf("✂", "id 미상 소켓 마감 — " + std::to_string(UNKNOWN_TIMEOUT_MS)
                          + "ms 안에 유효 프레임이 없었다(§3.4 판정의 2배)");
                closesock(unknown[k].fd);
                unknown.erase(unknown.begin() + k);
            } else k++;
        }
        std::vector<std::string> reap;
        for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
            AuxNode& a = it->second;
            if (a.fd == BAD_SOCK) continue;
            if (t - a.last_ms >= ARD_IDLE_CLOSE_MS) reap.push_back(it->first);
            // §3.4 오프라인 엣지 — 노드별로 따로 센다. 합치면 한 노드가 죽어도
            // 다른 노드에 가려 안 보인다(요청 항목 4).
            bool on = (t - a.last_ms) < OFFLINE_MS;
            if (on != a.online) {
                a.online = on;
                if (!on) { a.offline_episodes++;
                    logf("!", "보조 노드 " + it->first + " 오프라인(누적 "
                              + std::to_string(a.offline_episodes) + "회)"); }
                else logf("=", "보조 노드 " + it->first + " 온라인 복귀");
            }
        }
        for (size_t k = 0; k < reap.size(); k++) {
            zombie_reaps++;
            logf("✂", "보조 노드 " + reap[k] + " 회수 — "
                      + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초 무프레임(유휴 마감)");
            closesock(aux[reap[k]].fd);
            aux.erase(reap[k]);
        }
    }

    // ── REQ-0203 4a: 지형 ───────────────────────────────────────────────────────
    // 🔴 **`epoch` 를 올리는 곳은 여기 셋뿐이다.** 지형을 바꾸는 줄 바로 옆이다.
    void bump_epoch(const std::string& why) {
        map_epoch++;
        logf("=", "지형 판 " + std::to_string(map_epoch) + " (" + why + ")");
        // 🔑 **판이 오르면 곧바로 보낸다.** 안 보내면 화면은 `state.epoch` 불일치를 보고
        //   `get_map` 을 물어야 하고, **그 사이 "모른다"로 그리는 창이 생긴다.**
        //   보내는 쪽이 먼저 움직이면 그 창이 없다.
        push_map();
    }

    // 기본 지형. ⚠ **기본값이지 고정값이 아니다** — 설정 적재가 생기면 이 함수를 대체한다.
    void build_default_zones() {
        zones.clear();
        // 🔴 **주차 자리는 5개다. 10개가 아니다** (사용자 확정 (A) · 2026-08-19 · 명세 §9)
        //   각 자리에 **주차확인센서가 둘**이다 — 영역1 ← 센서 A1(#0) · B1(#5). 이중화다.
        //   ⚠ 옛 지형은 10개를 만들었고 **그것이 화면의 "10자리"였다.**
        //   🔑 `Zone.id` 는 **A1~A5 를 유지한다** — 전선 `R,<rid>,A1,<user>` 과 `slots[]` 색인이
        //     그 이름을 쓰므로 바꾸면 **arduino 축까지 움직인다.** 화면 라벨은 web 이 붙인다.
        //   ⏳ id 개명(영역1~5)은 별건 — 굽기와 같은 경계에서 나중에.
        for (int i = 0; i < 5; i++) {
            Zone z; z.id = SLOT_ID[i]; z.kind = "parking";
            // ⚠ 칸은 **하나로 둔다.** 두 센서를 격자에서 한 칸으로 그릴지 두 칸으로 그릴지는
            //   **표현이고 web 의 몫**이다. 물리 배치를 서버가 지어내지 않는다(명세 §9.1).
            z.cells.push_back(std::make_pair(i / grid_cols, i % grid_cols));
            zones.push_back(z);
        }
        Zone e; e.id = "E1"; e.kind = "entrance";
        e.cells.push_back(std::make_pair(grid_rows - 1, 0));
        zones.push_back(e);
        Zone x; x.id = "X1"; x.kind = "exit";
        x.cells.push_back(std::make_pair(grid_rows - 1, grid_cols - 1));
        zones.push_back(x);
        // ~~🔴 길이 1 을 여기서 강제한다~~ → **강제를 푼다**(2026-08-19 · 명세 §9.1).
        //   ⚠ **지금 정의는 여전히 전부 길이 1 이다.** 강제를 푼 것은 web 이 여러 칸을 필요로 할 때
        //   **서버가 조용히 잘라 버리지 않게** 하려는 것이다 — 전에는 `resize(1)` 로 말없이 깎았다.
        //   🔴 대신 **길이가 1 이 아니면 로그에 남긴다.** 깎지는 않는다.
        for (size_t i = 0; i < zones.size(); i++)
            if (zones[i].cells.size() != 1)
                logf("=", "자리 " + zones[i].id + " 의 칸 수 "
                          + std::to_string(zones[i].cells.size()) + " — 1이 아니다(허용한다. 화면이 순회한다)");
        bump_epoch("기본 지형 구성");
    }

    Zone* zone_find(const std::string& id) {
        for (size_t i = 0; i < zones.size(); i++) if (zones[i].id == id) return &zones[i];
        return NULL;
    }

    // 등록이 끝난 노드의 모듈을 자리에 붙인다.
    // ⚠ **결속 규칙은 지금 부트스트랩이다**: 모듈 `name` 이 자리 `id` 와 같으면 그 자리에 붙는다.
    //   지금 펌웨어가 `D,A1,IP` 처럼 자리 이름을 그대로 쓰기 때문에 성립한다.
    //   🔴 **설정이 생기면 이 규칙을 설정이 대체한다** — 그때까지 이름이 곧 결속이라는 것을 적어 둔다.
    // ── 🔴 센서 이름 → 자리 id (사용자 확정 (A) · 명세 §9)
    //   한 자리에 센서가 둘이다: 영역1 ← `A1`(#0) · `B1`(#5).
    //   ⚠ 전에는 `zone_find(name)` 으로 **이름이 곧 자리**였고 그래서 자리가 10개였다.
    //   🔑 이 함수가 그 결합을 끊는다 — **자리는 5개, 센서는 10개.**
    static std::string zone_of_module(const std::string& nm) {
        if (nm.size() == 2 && nm[0] == 'B' && nm[1] >= '1' && nm[1] <= '5')
            return std::string("A") + nm[1];      // B3 → 자리 A3 의 둘째 센서
        return nm;                                  // A1..A5 · E1 · X1 은 그대로
    }

    void bind_modules(Node& n) {
        bool changed = false;
        for (size_t i = 0; i < n.mods.size(); i++) {
            Zone* z = zone_find(zone_of_module(n.mods[i].first));
            if (!z) continue;
            std::pair<std::string,std::string> key(n.devid, n.mods[i].first);
            bool dup = false;
            // 🔴 **다른 노드가 같은 *이름* 을 주장하면 결속하지 않는다** (2026-08-19 · 모의 노드로 잡았다)
            //
            // 결속은 `zone_find(name)` 으로 **이름만 보고** 자리를 찾는다. 그래서 두 노드가
            // 둘 다 `A1` 을 선언하면 **자리 A1 에 모듈이 둘** 붙었다 — 실측으로 그렇게 됐다.
            // 🔑 설계는 *"전역 신원은 `(devid,name)` 복합 키"* 인데 **자리 결속에서 그게 안 지켜졌다.**
            //
            // ⚠ 여기서 **둘 다 받아들이면** 그 자리의 값이 어느 노드 것인지 화면이 못 가른다.
            //   ⚠ **조용히 덮으면** 나중에 붙은 노드가 앞엣것을 지워 원인을 못 찾는다.
            //   → **먼저 잡은 노드가 유지되고, 뒤엣것은 거절하고 센다.** first-S-wins 와 같은 규율이다.
            // 🔴 이것은 지형이 자리마다 어느 모듈을 갖는지 **명시하기 전까지의 규칙**이다.
            //   지형이 그것을 말하게 되면(REQ-0260) 이 추측은 사라진다.
            bool taken_by_other = false;
            for (size_t k = 0; k < z->modules.size(); k++) {
                if (z->modules[k] == key) dup = true;
                else if (z->modules[k].second == key.second) taken_by_other = true;
            }
            if (taken_by_other) {
                mod_name_conflict++;
                if (mod_name_conflict <= 3 || mod_name_conflict % 100 == 0)
                    logf("🔴", "모듈 이름 충돌 — 자리 " + z->id + " 의 이름 '" + key.second
                               + "' 을 노드 " + (n.devid.empty() ? std::string("(미승격)") : n.devid)
                               + " 가 다시 주장한다. **먼저 잡은 노드를 유지하고 이것은 결속하지 않는다.**"
                               " 누적 " + std::to_string(mod_name_conflict)
                               + " · 자리 결속이 아직 이름 기반이라 생기는 한계다(REQ-0260)");
                continue;
            }
            if (!dup) { z->modules.push_back(key); changed = true; }
        }
        if (changed) bump_epoch("노드 " + n.devid + " 등록 결속");
    }

    // ── REQ-0203 3a: **노드를 하나로 훑는 길** ──────────────────────────────────
    // 🔴 **색인을 저장하지 않는다. 부를 때마다 만든다.**
    //   저장하면 `park`·`aux` 가 바뀔 때마다 갱신해야 하고, **한 곳만 빠뜨리면 색인이 낡는다** —
    //   그건 "노드가 사라진 것처럼 보이는" 결함이고 로그에도 안 남는다.
    //   **낡을 수 없는 구조가 갱신을 잘 하는 것보다 낫다.**
    // ⚠ 지금은 **아무도 안 쓴다**(자가검증만). 다음 단계에서 라우팅이 이걸 탄다 —
    //   **먼저 길을 놓고 그 다음에 차를 올린다.** 한 단계에 둘을 하면 거동 변화 0 을 못 보인다.
    std::vector<Node*> all_nodes() {
        std::vector<Node*> v;
        if (!park.devid.empty() || park.fd != BAD_SOCK) v.push_back(&park);
        for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it)
            v.push_back(&it->second);
        return v;
    }

    // 🔑 **`kind` 첫 글자가 "명령을 받는가"를 답한다**(설계 §5 · `O` = 받는다).
    // 🔴 **실패 방향을 못 박는다: `O` 가 아니면 명령을 보내지 않는다.**
    //    알 수 없는 글자도 **명령 금지 쪽으로 떨어진다** — 모르는 장치에 명령을 보내는 것이
    //    안 보내는 것보다 위험하다. **모르는 `kind` 를 거절하지는 않는다**(거절하면 새 모듈 하나가
    //    옛 서버에서 노드 전체를 미등록으로 만든다).
    // ── 🔴 `S` 의 비트필드를 **한 정의로** 읽는다 (2026-08-19 · 원장 §8.23-(66))
    //
    // **왜 함수인가**: 같은 프레임에 비트필드가 셋 있는데(`occ`·`res`·`ovr`) **각자 해독하고 있었다.**
    // hex 전환 때 `occ` 만 고쳤고 나머지 둘은 10진 전제로 남았다 —
    // `res` 는 **조건이 영영 거짓이 되어 자가 치유가 죽었고**, `ovr` 은 잠복이었다.
    // 🔑 **전선 형식이 바뀌면 그 형식을 읽는 자리를 전부 세야 한다. 정의가 하나면 셀 필요가 없다.**
    //
    // ⚠ **폭으로 형식을 가르지 않는다.** `n=10` 이면 hex 폭 3, 10진 폭 10 이라 지금은 갈리지만
    //   `n=40` 이면 hex 폭도 10 이 되어 **두 형식이 같은 폭을 갖는다.** 그래서 판별자는 폭이 아니라
    //   **등록 여부**다: 등록됐으면 `n` 을 아니까 hex, 아니면 옛 10진(폭 10)으로만 받는다.
    //
    // 반환 true = 해독했다 / false = 못 읽었다(`out` 은 전부 0).
    // 🔴 **모르면 0 을 채우고 false 를 낸다 — 짐작해서 풀지 않는다.** 짐작한 값은 폭도 체크섬도
    //   통과하고 자리만 어긋난다(그게 `49c07f6` 이 고친 고장이다).
    // 🔴 **10 에서 자르지 않는다.** `occ` 의 비트 `>= 10` 은 **액추에이터 상태**다
    //   (arduino REQ-0228 답변 · 명세 §5 "위험 다섯째"가 이미 그렇게 정했다):
    //     비트 0..9   = 주차 자리 점유 (`kind` 가 `I*`)
    //     비트 10..   = "지금 열려 있나" 같은 **출력 모듈의 현재 상태** (`kind` 가 `O*`)
    //   ⚠ **같은 비트열인데 의미가 다르고, 그 구분은 `kind` 에 있다.**
    //   🔑 그래서 이 값은 "자리 점유"가 아니라 **"모듈 상태"** 다. 10 에서 자르면
    //      **조작 완료를 판정할 값이 조용히 버려진다** — 화면은 영영 "진행 중"에 머문다.
    //
    // `out` 은 `REG_MODS_MAX` 칸. 반환 = 해독한 비트 수(0 = 못 읽음).
    int decode_mod_bits(const std::string& fld, int* out) const {
        for (int i = 0; i < REG_MODS_MAX; i++) out[i] = 0;
        if (park.reg_done && park.reg_n > 0) {
            const int n = park.reg_n;
            // ⚠ `REG_MODS_MAX` 가 32 라 `unsigned long`(32비트 보장)로는 아슬아슬하다.
            //   `strtoull` 로 받는다 — **폭이 상한에 닿아도 값이 안 잘린다.**
            unsigned long long v = strtoull(fld.c_str(), NULL, 16);
            for (int i = 0; i < n && i < REG_MODS_MAX; i++)
                out[i] = ((v >> (n - 1 - i)) & 1ULL) ? 1 : 0;
            return n;
        }
        if (fld.size() == 10) {                  // 미등록 + 폭 10 → 옛 10진 펌웨어(하위호환)
            for (int i = 0; i < 10; i++) out[i] = (fld[i] == '1') ? 1 : 0;
            return 10;
        }
        return 0;
    }
    static bool kind_commandable(const std::string& k) { return !k.empty() && k[0] == 'O'; }
    // ⚠ `atoi` 는 숫자가 아니면 **조용히 0 을 준다.** `D,*,IP,…`(이름이 `*` 인 모듈)이
    //   `drain=0` 으로 통과하면 유도식이 0 을 먹는다. **형식 검사를 값 변환 앞에 둔다.**
    static bool all_digits(const std::string& x) {
        if (x.empty() || x.size() > 5) return false;
        for (size_t i = 0; i < x.size(); i++) if (x[i] < '0' || x[i] > '9') return false;
        return true;
    }
    int reg_cmdable() const {
        int n = 0;
        for (size_t i = 0; i < park.mods.size(); i++)
            if (kind_commandable(park.mods[i].second)) n++;
        return n;
    }

    // id 미상 소켓 하나를 승격한다. 반환 false = 자리가 없어 거절했다(소켓은 닫힌다).
    bool promote_unknown(sock_t c, const std::string& dev) {
        // (0) 🔴 **잠금이 걸려 있으면 지정된 devid 만 주차 노드가 된다**(REQ-0217 ④).
        //     다른 devid 는 **거절이 아니라 보조 노드**로 들어간다 — 상행은 받되 하행은 안 준다.
        //     🔑 **그래야 그들이 로그에 보인다.** 끊어 버리면 다시 "안 보이게" 된다.
        if (!g_park_dev_pin.empty() && dev != g_park_dev_pin && park_dev.empty()) {
            logf("!", "주차 노드 잠금(" + g_park_dev_pin + ") — device=" + dev
                      + " (" + peer_str(c) + ") 는 보조 노드로 받는다. **하행 없음**");
        }
        // (1) 주차 노드가 아직 없다 → first-S-wins 로 이 장치가 주차 노드다
        // (2) 같은 device_id 의 재접속 → 자리를 물려받는다(옛 동작을 이 경우로 한정한 것)
        const bool pin_ok = g_park_dev_pin.empty() || dev == g_park_dev_pin;
        if ((park_dev.empty() && pin_ok) || park_dev == dev) {
            if (park_dev.empty())
                logf("=", "주차 노드 지정 — device=" + dev
                          + " (first-S-wins: 첫 S 프레임을 보낸 장치가 주차 노드다)");
            // 🔴🔴 **동시 접속 감지 — 1차 판별자는 시간이 아니라 IP 다** (REQ-0217)
            // 종전 규칙은 *"같은 devid = 같은 장치"* 를 전제했다. **조원들의 동일 카피 보드가
            // 전부 `P1` 이라 그 전제가 깨졌고, 그래서 이 경로가 조용히 통과했다.**
            // ⚠ **"최근 프레임이 있으면 침입자"로 갈라선 안 된다** — 실측 반증이 상수 주석에 있다
            //   (정상 재접속 85건 중 공백 0·1·2초가 실재한다).
            if (ard != BAD_SOCK) {
                const std::string np = peer_str(c);
                const std::string oh = peer_host(ard_peer), nh = peer_host(np);
                const bool known = (!oh.empty() && oh != "?" && !nh.empty() && nh != "?");
                // 🔴 **판별자가 없으면 막지 않는다 — 실패 방향을 고른 것이다.**
                // 주소를 못 얻는 경우(비 IPv4 소켓·`getpeername` 실패)에 거절 쪽으로 넘어지면
                // **우리 보드의 정상 재접속이 영영 막힌다.** 반대 방향의 손해는 "종전과 같다"뿐이다.
                // ⚠ 이 갈래는 자가검증 ⑬-(가)가 잡아 준 것이다 — 처음엔 거절 쪽으로 넘어졌다.
                if (!known) {
                    logf("!", "⚠ 같은 device_id(" + dev + ") 재접속인데 **주소를 못 얻어 판별 불가**"
                              " (기존 '" + ard_peer + "' → 새 '" + np + "') — 종전대로 교체한다");
                    adopt_as_parking(c, dev);
                    return true;
                }
                if (oh == nh) {
                    // 같은 IP = 같은 장치의 TCP 재접속. **공백을 묻지 않는다.**
                    adopt_as_parking(c, dev);
                    return true;
                }
                const long long quiet = ard_seen ? (now_ms() - ard_last_ms) : TAKEOVER_GRACE_MS;
                if (quiet < TAKEOVER_GRACE_MS) {
                    dup_devid_reject++;
                    logf("!!", "🔴 같은 device_id(" + dev + ") · **다른 IP** — 기존 " + ard_peer
                               + " 이 " + std::to_string(quiet)
                               + "ms 전까지 프레임을 보내는 중인데 " + np
                               + " 이 자리를 요구했다. **두 대다. 거절한다.** 누적 "
                               + std::to_string(dup_devid_reject) + "회");
                    // **확실한 것을 버리고 불확실한 것을 얻지 않는다**(MAX_ARD_NODES 와 같은 원칙).
                    // 🔑 관측에서 중요한 것은 이기는 것이 아니라 **누가 잡았는지 아는 것**이다.
                    logf("!!", "⚠ 같은 망에 동일 devid 보드가 있다 — 이 시각 전후의 장치 지표를 "
                               "우리 보드의 것으로 읽지 마라");
                    closesock(c);
                    return false;
                }
                takeover_grace++;
                logf("!", "⚠ 같은 device_id(" + dev + ") · 다른 IP(" + ard_peer + " → " + np
                          + ") 인데 기존이 " + std::to_string(quiet) + "ms 조용하다 — 교체 허용. "
                          "**우리 보드의 IP 가 바뀐 것일 수도, 남의 보드가 죽은 자리를 가져간 것일 수도 있다.** "
                          "누적 " + std::to_string(takeover_grace) + "회");
            }
            adopt_as_parking(c, dev);
            return true;
        }
        // (3) 이미 아는 보조 노드의 재접속
        if (aux.count(dev)) { adopt_as_aux(c, dev); return true; }
        // (4) 새 장치 — 상한 확인
        size_t total = aux.size() + (ard != BAD_SOCK ? 1 : 0);
        if (total >= MAX_ARD_NODES) {
            admit_rejects++;
            logf("!", "노드 상한(" + std::to_string(MAX_ARD_NODES) + ") 초과 — device=" + dev
                      + " 거절. **살아 있는 노드를 쫓아내지 않는다.** 누적 "
                      + std::to_string(admit_rejects) + "회");
            closesock(c);
            return false;
        }
        adopt_as_aux(c, dev);
        return true;
    }
    static int slot_index(const std::string& s) {
        for (int i = 0; i < 10; i++) if (s == SLOT_ID[i]) return i;
        return -1;
    }
    // ── 두 이름공간 (rid 를 §4.1 에서 나눈 것과 같은 구조) ─────────────────────
    //   브라우저 ↔ 서버 · 서버 내부 저장 : **번호판 전체** (UTF-8, JSON 이라 자유)
    //   서버 → 아두이노 (전선 userid)    : ASCII 0*8 (§2.3) — 못 담으면 **빈 값**
    //
    // 전선에 번호판을 태우려고 valid_userid() 를 느슨하게 만들면 REQ-0023 이 막은 버그
    // (잘린 값이 체크섬과 함께 형식상 유효한 라인으로 나가는 것)가 되살아난다.
    // 아두이노는 userid 를 쓰지 않는다(§2.4 — 무시해도 되지만 필드는 있어야 한다).
    // 그래서 담을 수 없으면 그냥 비운다. 잃는 것이 없다.
    static std::string wire_userid(const std::string& browser_user) {
        return valid_userid(browser_user) ? browser_user : std::string();
    }
    // 브라우저 쪽 값은 UTF-8 이라 문자 집합을 강제하지 않는다. 다만 저장이 무한히 커지지
    // 않게 길이만 막고, 제어문자는 거른다(로그·JSON 오염 방지).
    static bool valid_browser_user(const std::string& u) {
        if (u.size() > MAX_PLATE_BYTES) return false;
        for (size_t i = 0; i < u.size(); i++)
            if ((unsigned char)u[i] < 0x20) return false;
        return true;
    }

    // 명세 §2.3 `userid ::= 0*8( ALPHA / DIGIT / "_" / "-" )`
    // 검증하지 않으면 snprintf 가 조용히 잘라서 **형식상 유효하지만 내용이 잘린 라인**이 나간다.
    // 메모리 안전 문제는 없지만(잘린다) 64B 상한(§2.1)을 넘거나 엉뚱한 user_id 가 기록되고,
    // 증상은 "가끔 예약이 안 된다"(재전송 3회 후 ack_timeout)로 보여 원인을 찾기 어렵다.
    static bool valid_userid(const std::string& u) {
        if (u.size() > 8) return false;
        for (size_t i = 0; i < u.size(); i++) {
            char c = u[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    // 모든 **접속된** 소켓에 전송 타임아웃을 건다.
    // 이게 없으면 상대가 안 빼갈 때 send() 가 무한정 막히고, 단일 스레드라 **서버 전체가 선다.**
    // 실제로 그렇게 죽었다 — 로그도 오류도 없이 멈춰서 단서가 0 이었다.
    // select() 의 "읽기 준비"는 쓰기에 대해 아무것도 보장하지 않는다는 점이 핵심이다.
    static void set_send_timeout(sock_t s) {
#ifdef _WIN32
        DWORD ms = (DWORD)SEND_TIMEOUT_MS;                  // 윈도우는 밀리초 DWORD
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
        struct timeval tv;                                   // POSIX 는 timeval
        tv.tv_sec  = SEND_TIMEOUT_MS / 1000;
        tv.tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif
    }

    // ---------- 송신 helper
    // 반환 false = 밀어 넣지 못했다(타임아웃 또는 오류) → **호출자가 그 연결을 끊어야 한다.**
    // 부분 전송 뒤 타임아웃이면 전선에 **잘린 줄**이 남는다. 붙들고 있으면 상태가 어긋나므로 버린다.
    bool send_raw(sock_t fd, const char* p, size_t n, const char* who) {
        size_t off = 0;
        // **전체 마감시각을 따로 둔다.** SO_SNDTIMEO 는 `send()` **한 번**에 걸리는 것이지
        // 이 루프 전체에 걸리는 것이 아니다. 상대가 아주 조금씩만 빼가면 매 호출이 조금씩
        // 진전해 루프가 계속 돌고, 총 정지 시간은 얼마든지 길어진다(느린 클라이언트 정체).
        // 실측으로 확인했다 — 1초 타임아웃인데 send() 를 여러 번 부르느라 1.93초가 걸렸다.
        const long long deadline = now_ms() + SEND_TIMEOUT_MS;
        while (off < n) {                       // 부분 write 는 정상이다
            if (now_ms() > deadline) {
                char b[192];
                snprintf(b, sizeof(b),
                         "%s 로 전송이 %d ms 를 넘겼다 — %zu/%zu 바이트. 연결을 끊는다",
                         who, SEND_TIMEOUT_MS, off, n);
                logf("!", b);
                return false;
            }
            int w =
#ifdef _WIN32
                ::send(fd, p + off, (int)(n - off), 0);
#else
                (int)::send(fd, p + off, n - off, 0);
#endif
            if (w <= 0) {
                char b[192];
                snprintf(b, sizeof(b),
                         "%s 로 전송 실패 — %zu/%zu 바이트만 밀어넣음 (err %d). "
                         "상대가 안 빼가는 것으로 보고 연결을 끊는다",
                         who, off, n, sockerr());
                logf("!", b);
                return false;
            }
            off += (size_t)w;
        }
        return true;
    }
    // 아두이노 송신 실패 뒷정리. **두 경로(`send_ard`·`flush_downq`)가 여기로 모인다** —
    // 한쪽만 정리하면 "끊겼는데 세션이 열려 있는" 장부가 만들어진다(§899 와 같은 이유).
    void ard_send_failed() {
        closesock(ard); ard = BAD_SOCK; ard_buf.clear();
        end_ard_session("송신 실패");        // 세션 장부를 여기서도 닫는다(REQ-0065)
        // 단계 C: 직접 호출 → 이벤트. ⚠ **이 자리는 복구된 이음매 표에 없었다** —
        // 표는 5곳이라 했지만 실제로는 여기가 여섯 번째다. 표를 지도로 믿지 마라.
        emit_dev(DEV_DISCONNECT, park_dev, "송신 실패");
    }
    void send_ard(const std::string& line) {
        if (ard == BAD_SOCK) return;
        if (!send_raw(ard, line.data(), line.size(), "아두이노")) {
            ard_send_failed();
            return;
        }
        // ---------- 착지 위상 계측 (DESIGN-downlink-window.md §3.5)
        //
        // **하행이 장치 주기의 어디에 떨어졌는가**를 ms 로 남긴다. 값을 바꾸지 않고 찍기만 한다.
        //
        // 🔴 **왜 로그 본문에 넣나**: 줄 앞머리 시각이 **1초 해상도**라(§433 `%H:%M:%S`)
        // 조용한 창(약 1.0초)과 장치 활성 구간(약 113ms)을 **구별할 수 없다.**
        // `ard_last_ms` 는 ms 단위로 이미 있으므로, 그 차이를 본문에 적으면 그 순간 잴 수 있다.
        // → 이것 없이 `W`(송신 허용 창)를 정하면 **추측이다.**
        //
        // ⚠ **`S` 를 한 번도 못 받은 상태를 따로 표시한다.** `ard_seen` 이 false 면
        // `ard_last_ms` 는 **의미 없는 0** 이고, 그때 차이를 찍으면 **거대한 가짜 값**이 된다.
        // 승격 직후 `resync_reservations()` 가 쏘는 자리가 정확히 그 경우라
        // **그 구분이 `resync` 게이트 면제 판단의 입력**이 된다(설계 §3.4).
        //
        // 🔑 `wire_rid` 를 같이 찍는 이유: 장치 쪽에서 **`+IPD` 도착 위상**을 따로 재는데,
        // **짝지을 식별자가 없으면 양쪽이 각각 반쪽**이다("언제 쐈나" 대 "언제 받았나").
        {
            char ph[64];
            if (!ard_seen)
                snprintf(ph, sizeof(ph), " (S미수신)");
            else
                snprintf(ph, sizeof(ph), " (S+%lldms)", (long long)(now_ms() - ard_last_ms));
            logf("→ARD", line.substr(0, line.size() - 1) + ph);
            return;
        }
    }

    // ═══════ 하행 슬롯 큐 (docs/net/DESIGN-server-slot-queue.md) ═══════════════
    //
    // 🔑 **이 구조는 위상을 추정하지 않는다.** `S` 도착이 트리거이므로 장치가 알려준다.
    // 스케줄이면 병리 구간(`SEND OK` 가 2~5초)에서 예측이 가장 크게 틀리는데,
    // **가장 크게 틀리는 때가 하필 가장 위험한 때다.** 트리거는 그 문제가 없다.

    // 내 창의 실제 폭. `W_srv = 600 − RTT − 여유`.
    // 🔴 **`RTT` 는 상수가 아니라 실측이다.** 값을 박으면 Wi-Fi 가 붐빌 때 조용히 깨진다.
    // `rtt_max_ms` 는 ACK 왕복의 최대값이고 그건 `RTT + 장치 처리시간` 이라 **RTT 의 상한**이다 —
    // 창을 좁히는 방향이므로 **보수적 = 안전**하다.
    long long w_srv() const {
        long long w = (long long)DOWN_WIN_MS - rtt_max_ms - DOWN_WIN_MARGIN_MS;
        if (w < DOWN_WIN_MIN_MS) w = DOWN_WIN_MIN_MS;
        return w;
    }
    // 큐 깊이(바이트). **유도값이다** — "마감 안에 나갈 수 있는 양".
    long long downq_max_b() const {
        return (long long)DOWN_BATCH_CAP_B * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS);
    }

    // ── 🔴 **큐를 떠난 뒤의 최악 예산** (REQ-0166 · web 지적으로 추가)
    //
    // `expires_ms` 가 재는 것은 **큐 대기까지**다. 그러면 **이탈 후를 덮는 값이 화면 쪽에만
    // 있게 되는데**, web 의 그 값(6000)이 내 실제 최악보다 작았다. 화면이 서버보다 먼저
    // 롤백하면 사용자가 다시 누르고 **새 rid 로 큐에 한 건 더** 쌓인다 — 우리가 닫으려던 고리다.
    // → **서버가 이 값을 같이 준다.** 화면은 받은 값을 쓰고 짐작하지 않는다.
    //
    // 🔑 **리터럴을 쓰지 않고 상수에서 계산한다.** 그래야 `ACK_TIMEOUT_MS`·`ACK_MAX_TRIES`·
    // `DOWN_DMAX_MS` 를 나중에 누가 바꿔도 **이 값이 조용히 틀릴 수 없다.**
    // (web 이 제시한 (다)안 — 합의 상수를 화면에 박기 — 의 약점이 정확히 "값이 바뀌면
    //  조용히 틀린다" 였다. 계산으로 두면 그 약점이 구조적으로 사라진다.)
    //
    // 시도 한 번의 최악 = `ACK_TIMEOUT_MS` + **틱 해상도**(`SELECT_TICK_MS`).
    //   ⚠ 내가 처음 6900 이라 적을 때 **이 틱 해상도를 안 셌다.** 타임아웃은 `tick()` 이
    //   발견하는데 그건 최대 200ms 늦게 돈다. 3회면 600ms 가 빠져 있었다.
    // 시도 사이의 창 대기 최악 = `DOWN_DMAX_MS`.
    //   ⚠ 평시엔 한 슬롯(1200)이지만 **장치가 침묵하면 창 포기 경로(2400)가 상한**이다.
    //   그 경로를 안 세면 하필 병리 구간에서 예산이 모자란다.
    long long ack_budget_ms() const {
        return (long long)ACK_MAX_TRIES * (ACK_TIMEOUT_MS + SELECT_TICK_MS)
             + (long long)(ACK_MAX_TRIES - 1) * DOWN_DMAX_MS;
    }

    // 큐에 있던 항목을 **전선에 못 내보낸 채** 끝낼 때.
    // 🔴 설계 §4-B 의 보장: **`queued` 를 보냈으면 그 뒤에 반드시 `ack` 또는 `error` 가 간다.**
    // 그 보장이 없으면 화면의 중간 상태가 영구가 된다.
    // ⚠ 코드는 `device_offline` 이다 — **전선에 안 나갔다는 뜻**이고 원장 §8.16 이 요구하는 구분이다.
    // `ack_timeout` 을 쓰면 "나갔는데 응답이 없다"는 **거짓 문장**이 로그에 남는다.
    void fail_down_item(const DownQ& q, const char* code, const char* msg) {
        std::map<uint16_t, Pending>::iterator it = pend.find(q.wire_rid);
        if (it != pend.end()) { pend.erase(it); rid_release(q.wire_rid); }
        if (q.ws_fd != BAD_SOCK) send_err(q.ws_fd, q.brid, code, msg);
    }

    // 세션이 끝나면 큐를 비운다(설계 §2). **옛 큐를 새 세션에 쏘면 안 된다** —
    // 장치는 자기 번호를 새로 시작하고, 그 사이 상태도 우리가 모른다.
    void clear_downq(const char* why) {
        if (downq.empty()) return;
        char b[192];
        snprintf(b, sizeof(b), "하행 큐 비움(%s) — %zu줄 · %lldB. **전선에 나가지 않았다**",
                 why, downq.size(), downq_bytes);
        logf("✂", b);
        q_dropped_link += (long long)downq.size();
        // 🔴 **먼저 떼어낸다.** `send_err` 가 `dead` 를 건드리고, 실패 경로가 다시
        // `end_ard_session` → `clear_downq` 로 들어올 수 있다. 비운 뒤에 답하면 재진입이 안전하다.
        std::vector<DownQ> tmp;
        tmp.swap(downq);
        downq_bytes = 0;
        for (size_t i = 0; i < tmp.size(); i++)
            fail_down_item(tmp[i], "device_offline", "센서 연결이 끊겨 요청이 취소되었습니다");
    }

    // 하행 한 줄을 큐에 넣는다. **여기서 전송은 일어나지 않는다.**
    //   announce : 브라우저에 `queued` 를 보내나 (재전송은 안 보낸다 — §expires_ms 단조성)
    //   force    : 깊이 상한을 무시하나 (재전송은 이미 약속한 것이므로 거절하지 않는다)
    // 반환값 false = 거절했다(호출자가 `pend` 에서 지워야 한다).
    bool enqueue_down(Pending& p, const std::string& line, bool announce, bool force) {
        // 🔴 **장치가 없으면 큐에 담지 않는다.** 담으면 아무도 빼가지 않아 다음 세션까지
        // 살아남고, 그러면 **장치가 모르는 상태에 옛 명령이 떨어진다**(설계 §2 가 금지한 것).
        // 옛 거동은 `send_ard` 가 조용히 no-op 해서 **아무 말 없이 사라졌다** — 그건 더 나쁘다.
        // (폰 경로 `on_plate()` 가 장치 없이도 `dispatch` 를 부를 수 있어 실제로 도달한다.
        //  selftest 에서 큐가 쌓이는 것을 보고 찾았다 — 소켓 없는 경로가 그 모양이다.)
        if (ard == BAD_SOCK) {
            q_nodev++;                 // 🔴 `q_rejected`(큐 넘침)와 **다른 칸**이다
            logf("!", "장치 연결이 없다 — 하행 거절 rid=" + std::to_string(p.wire_rid));
            if (p.ws_fd != BAD_SOCK)
                send_err(p.ws_fd, p.browser_rid, "device_offline", "센서가 연결되어 있지 않습니다");
            return false;
        }
        // 옛 거동 — **착지 위상을 의도적으로 겨냥하는 시험 전용**(원장 §8.17 의 미해결 물음).
        if (DOWN_IMMEDIATE) {
            p.queued = false; p.tries = (p.tries < 1 ? 1 : p.tries); p.sent_ms = now_ms();
            send_ard(line);
            return true;
        }
        // 한 줄 상한 감시선. `RX_CAP=96` 에서 `+IPD` 접두를 뺀 87B 다. 우리 줄은 30B 안쪽이라
        // 정상적으로는 안 걸린다 — **걸리면 그건 새 프레임 종류가 들어온 신호다.**
        if ((long long)line.size() > (long long)DOWN_LINE_MAX_B + 1) {
            char b[128];
            snprintf(b, sizeof(b), "하행 한 줄이 %zuB — 장치 RX_CAP(96, 접두 포함) 위험. 그대로 보낸다",
                     line.size());
            logf("!", b);
        }
        const bool important = (p.ws_fd == BAD_SOCK);   // 서버 내부 발생 = 중요(§분류)

        // 🔴 **사용자 계열만 건수로 막는다. 중요는 거절하지 않는다.**
        // 사용자 조작은 **거절하면 사용자가 안다**(다시 누르면 된다). 중요는 버리면
        // **아무도 모르게 서버와 장치가 갈린 채 남는다** — 그래서 문턱을 안 건다.
        // 깊이는 유도값이다: **한 창에 `DOWN_BATCH_MAX_N` 이 나가므로 마감 안에 나갈 수 있는 양**
        //   = 4건/창 × (4800ms ÷ 1200ms) = **16건**
        if (!force && !important) {
            int user_n = 0;
            for (size_t k = 0; k < downq.size(); k++) if (!downq[k].important) user_n++;
            const int user_cap = DOWN_BATCH_MAX_N * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS);
            if (user_n >= user_cap) {
                q_rejected++; q_full_n++;
                char b[160];
                snprintf(b, sizeof(b), "사용자 계열 큐 상한(%d건) 초과 — 거절 rid=%u",
                         user_cap, p.wire_rid);
                logf("!", b);
                // 🔑 **침묵이 아니라 거절이다**(§8.20) — 조용히 버리면 사용자가 더 세게 누른다
                if (p.ws_fd != BAD_SOCK)
                    send_err(p.ws_fd, p.browser_rid, "queue_full",
                             "요청이 밀려 있습니다. 잠시 후 다시 눌러 주세요");
                return false;
            }
        }
        if (!force && downq_bytes + (long long)line.size() > downq_max_b()) {
            q_rejected++;
            char b[160];
            snprintf(b, sizeof(b), "하행 큐 상한(%lldB) 초과 — 거절 rid=%u (대기 %zu줄 %lldB)",
                     downq_max_b(), p.wire_rid, downq.size(), downq_bytes);
            logf("!", b);
            // 🔴 **조용히 버리지 않는다.** 버림과 거절은 다르다(원장 §8.20) —
            // 버림은 아무도 모르게 사라지고, **거절은 사용자에게 말한다.**
            if (p.ws_fd != BAD_SOCK)
                send_err(p.ws_fd, p.browser_rid, "queue_full", "요청이 밀려 있습니다. 잠시 후 다시 눌러 주세요");
            return false;
        }
        DownQ q;
        q.line = line; q.ws_fd = p.ws_fd; q.brid = p.browser_rid; q.slot = p.slot;
        q.wire_rid = p.wire_rid;
        // 마감은 **enqueue 때 한 번만 박는다.** 그래야 `expires_ms = deadline − now` 가
        // 같은 rid 에 대해 **단조 비증가**가 된다(설계 §4-B). `ahead` 로 다시 계산하면
        // 앞이 밀릴 때 값이 늘어 단조성이 깨진다.
        q.deadline_ms = now_ms() + DOWNQ_WAIT_CAP_MS;
        q.important = important;
        downq.push_back(q);
        downq_bytes += (long long)line.size();
        // 🔴 **`tries` 를 건드리지 않는다.** 재전송이 이 함수를 다시 타므로 여기서 0 으로 밀면
        // 재시도 횟수가 영구히 리셋되어 **ACK_MAX_TRIES 가 무력해지고 무한 재전송이 된다.**
        // "전선에 나갔나"는 `queued` 하나로 표현한다 — 표시자를 두 개 두면 반드시 갈린다.
        p.queued = true;
        if (announce && p.ws_fd != BAD_SOCK) send_queued(q);
        return true;
    }

    // 창이 열렸다(= `S` 가 도착했다). **큐 전부를 한 거래로 묶어** 내보낸다.
    //   ignore_window : `S` 가 안 와서 창을 포기하는 경로(설계 §3)
    // `max_n` = 이번 창에 전선에 올릴 **최대 건수**. 기본은 유도값이고, 증거 없는
    // 경로(창 포기)는 `DOWN_PROBE_N` 을 넘긴다 — **송신 지점이 한 곳이라 우회가 없다.**
    void flush_downq(const char* why, bool ignore_window, int max_n = DOWN_BATCH_MAX_N) {
        if (downq.empty() || ard == BAD_SOCK) return;
        long long t = now_ms();
        if (!ignore_window && ard_seen) {
            // 창 안으로 얼마나 들어왔나. 지났으면 **다음 `S` 를 기다린다** —
            // 늦게 쏘면 장치의 송신 구간을 침범하고, 그게 §8.15 의 UART 혼재다.
            long long into = t - ard_last_ms;
            if (into >= w_srv()) {
                win_skips++;
                char b[160];
                snprintf(b, sizeof(b), "창을 지났다(S+%lldms ≥ W_srv %lldms) — 다음 S 를 기다린다"
                                       " · 대기 %zu줄 (%s)", into, w_srv(), downq.size(), why);
                logf("…", b);
                return;
            }
        }
        // 바이트 상한까지만 담는다. **남은 것은 버리지 않고 다음 창으로 미룬다.**
        std::string payload;
        std::vector<DownQ> batch;
        // 🔴 **중요 계열을 먼저 담는다.** 각 계열 안에서는 FIFO 다(예약/취소가 뒤집히면 안 된다).
        // **계열 사이에는 순서 보장이 필요 없다** — 상태 정정과 새 의도는 서로 독립이다.
        for (int pass = 0; pass < 2; pass++) {
            const bool want = (pass == 0);
            for (size_t k = 0; k < downq.size(); ) {
                const DownQ& q = downq[k];
                if (q.important != want) { k++; continue; }
                // ⚠ 첫 줄은 상한을 넘어도 담는다 — 안 그러면 큰 줄 하나가 큐를 영구히 막는다.
                if (!payload.empty() &&
                    payload.size() + q.line.size() > (size_t)DOWN_BATCH_CAP_B) break;
                // 🔴 **건수 상한 — 바이트와 다른 축이다.** 장치의 ACK 배출률에서 유도됐다.
                if ((int)batch.size() >= max_n) break;
                payload += q.line;
                batch.push_back(q);
                downq_bytes -= (long long)q.line.size();
                downq.erase(downq.begin() + k);
            }
            if ((int)batch.size() >= max_n) break;
        }
        if (downq_bytes < 0) downq_bytes = 0;
        if (batch.empty()) return;

        if (!send_raw(ard, payload.data(), payload.size(), "아두이노")) {
            // 🔴 떼어낸 배치는 이미 큐에 없다 — **여기서 따로 닫아야** 화면의 중간 상태가 안 남는다.
            // (실패 경로가 `clear_downq` 로 나머지를 닫는다. 순서상 이 둘이 겹치지 않는다.)
            for (size_t i = 0; i < batch.size(); i++)
                fail_down_item(batch[i], "device_offline", "센서로 보내지 못했습니다");
            ard_send_failed();
            return;
        }
        batch_count++;
        batch_lines += (long long)batch.size();

        // 🔑 **줄마다 따로 찍는다.** 배치 요약만 남기면 장치 쪽 `+IPD` 도착 위상과
        // **짝지을 식별자가 사라진다**(어느 rid 가 언제 나갔나). 그 짝이 반쪽이면 위상 계측이 죽는다.
        for (size_t i = 0; i < batch.size(); i++) {
            std::map<uint16_t, Pending>::iterator it = pend.find(batch[i].wire_rid);
            if (it != pend.end()) {
                it->second.queued = false;
                it->second.sent_ms = t;                       // ACK 시계는 **여기서** 시작한다
                // 🔑 **도달**은 여기서 센다 — 큐에 든 것(`띄움`)과 다르다.
                //   세션이 끊기면 큐의 것은 버려지므로 **띄움 > 도달** 인 구간이 생긴다.
                //   ⚠ 그 차이가 "큐에서 죽었다"이고, `도달 − 응답` 이 "나갔는데 답이 없다"다.
                //     **둘은 다른 고장이라 한 칸에 못 넣는다.**
                if (it->second.kind == 'G') gate_sent++;
                if (it->second.tries < 1) it->second.tries = 1;
            }
            char ph[64];
            if (!ard_seen) snprintf(ph, sizeof(ph), " (S미수신)");
            else           snprintf(ph, sizeof(ph), " (S+%lldms)", (long long)(t - ard_last_ms));
            std::string l = batch[i].line;
            if (!l.empty() && l[l.size()-1] == '\n') l.erase(l.size()-1);
            logf("→ARD", l + ph);
        }
        {
            char b[192];
            snprintf(b, sizeof(b), "배치 %zu줄 · %zuB · 한 거래 (%s) · cap %dB · 남은 큐 %zu줄",
                     batch.size(), payload.size(), why, DOWN_BATCH_CAP_B, downq.size());
            logf("⇉", b);
        }
        // 미룬 것을 **조용히 미루지 않는다** — 안 적으면 지연이 아무 데도 안 보인다(원장 §5.2).
        if (!downq.empty()) {
            q_deferred++;
            char b[160];
            snprintf(b, sizeof(b), "바이트 상한에 걸려 %zu줄(%lldB)을 다음 창으로 미뤘다",
                     downq.size(), downq_bytes);
            logf("…", b);
        }
    }

    // ---------- WebSocket 프레임 (§5.2)
    void ws_send(sock_t fd, const std::string& payload) {
        std::string f;
        f += char(0x81);                                  // FIN + text
        size_t n = payload.size();
        if (n < 126) {
            f += char((unsigned char)n);                  // 서버→클라이언트는 마스킹 금지 (n<126 확정)
        } else if (n <= 0xFFFF) {
            f += char(126);                               // 16비트 확장 길이 — 스냅샷이 여기 온다
            f += char((n >> 8) & 0xFF);
            f += char(n & 0xFF);
        } else {
            f += char(127);
            for (int i = 7; i >= 0; i--) f += char((n >> (8*i)) & 0xFF);
        }
        f += payload;
        // 실패하면 여기서 erase 하지 않는다 — broadcast 순회 중이면 반복자가 무효화된다
        // (예전에 SIGSEGV 를 냈던 바로 그 실수다). 표시만 해 두고 루프 끝에서 거둔다.
        if (!send_raw(fd, f.data(), f.size(), "WS 클라이언트")) dead.push_back(fd);
    }
    void ws_broadcast(const std::string& payload) {
        for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it)
            if (it->second.kind == Conn::WS) ws_send(it->first, payload);
    }
    std::vector<sock_t> dead;      // 전송 실패로 끊어야 할 연결. 루프 끝에서 거둔다
    void reap_dead() {
        for (size_t i = 0; i < dead.size(); i++) {
            sock_t fd = dead[i];
            if (conns.count(fd)) { closesock(fd); conns.erase(fd); logf("-WS", "전송 실패로 연결 종료"); }
            if (phones.count(fd)) { closesock(fd); phones.erase(fd); }
        }
        dead.clear();
    }

    // ---------- JSON 만들기
    static std::string jstr(const std::string& s) {
        std::string o = "\"";
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b,sizeof(b),"\\u%04x",c); o += b; }
            else o += c;
        }
        return o + "\"";
    }
    // ── REQ-0203 4b: `map` 봉투 (설계 §6.8) ─────────────────────────────────────
    // 🔴 **`slots[]`(옛 `snapshot`)를 대체하지 않는다. 더한다.**
    //   화면이 `map` 을 받은 적 없으면 옛 경로로 그리고, 받으면 격자로 바꾼다(web 의 이중 경로).
    //   **그래야 이 배포가 화면을 안 깨뜨리고, 화면 배포도 서버를 안 기다린다.**
    // ⚠ **한 번에 완결해서 보낸다. 조각내지 않는다** — 화면이 "모른다"로 그리는 창을 짧게 하려는 것이다.
    std::string map_json() {
        std::ostringstream o;
        o << "{\"type\":\"map\",\"srv_id\":" << jstr(srv_id) << ",\"epoch\":" << map_epoch
          << ",\"grid\":{\"rows\":" << grid_rows << ",\"cols\":" << grid_cols << "}"
          << ",\"zones\":[";
        for (size_t i = 0; i < zones.size(); i++) {
            const Zone& z = zones[i];
            if (i) o << ",";
            o << "{\"id\":" << jstr(z.id) << ",\"kind\":" << jstr(z.kind) << ",\"cells\":[";
            for (size_t c = 0; c < z.cells.size(); c++) {
                if (c) o << ",";
                o << "[" << z.cells[c].first << "," << z.cells[c].second << "]";
            }
            o << "],\"modules\":[";
            for (size_t m = 0; m < z.modules.size(); m++) {
                if (m) o << ",";
                const std::string& dv = z.modules[m].first;
                const std::string& nm = z.modules[m].second;
                // `kind` 와 `idx` 는 **그 모듈을 등록한 노드**가 갖고 있다 — 이름만으로 찾으면 안 된다(복합 키).
                std::string kind; int idx = -1;
                std::vector<Node*> ns = all_nodes();
                for (size_t k = 0; k < ns.size(); k++) {
                    if (ns[k]->devid != dv) continue;
                    for (size_t j = 0; j < ns[k]->mods.size(); j++)
                        if (ns[k]->mods[j].first == nm) { kind = ns[k]->mods[j].second; idx = (int)j; }
                }
                o << "{\"devid\":" << jstr(dv) << ",\"name\":" << jstr(nm)
                  << ",\"kind\":" << jstr(kind) << ",\"idx\":" << idx << "}";
            }
            o << "]}";
        }
        o << "]}";
        return o.str();
    }
    // 🔴 **빈 지형을 `map` 으로 내보내지 않는다** (2026-08-19).
    //   첫 배포에서 `build_default_zones()` 가 기동 경로에 없어 `zones` 가 빈 채 나갔다.
    //   그때 화면은 **"자리가 0개인 주차장"** 을 정상 상태로 그렸다 —
    //   🔑 **빈 지형은 답이 아니라 고장이다. 답인 척하면 아무도 안 본다.**
    //   ⚠ 그래서 **안 보내고 시끄럽게 남긴다.** 화면은 지형이 없으면 옛 것을 유지하거나
    //     "아직 못 받았다"로 남는데, **둘 다 "0개"라고 그리는 것보다 낫다.**
    // 🔑 **선언 자리에서 초기화한다** — 생성자 목록에 넣으면 선언 순서에 묶여
    //   (`-Wreorder`) 다음 사람이 자리를 옮길 때 조용히 경고가 난다.
    // 🔑 **선언 자리에서 초기화한다**(C++11 NSDMI). 생성자 목록에 넣으면 선언 순서에
    //   묶여(`-Wreorder`) 다음 사람이 이 멤버를 옮길 때마다 경고가 난다.
    bool map_empty_warned = false;
    // 🔑 `mutable` 을 쓰지 않는다 — **요약을 만드는 함수가 상태를 바꾸면 안 된다.**
    //   갱신은 `ws_upgrade` 에서 하고 여기서는 읽기만 한다.
    int  ws_peak = 0;      // 이 인스턴스에서 동시에 붙었던 화면 수의 최대
    void push_map() {
        if (zones.empty()) {
            if (!map_empty_warned) {
                map_empty_warned = true;
                logf("!", "🔴 지형이 비어 있다 — `map` 을 보내지 않는다. "
                          "**기동 경로가 build_default_zones() 를 안 불렀을 수 있다**");
            }
            return;
        }
        ws_broadcast(map_json());
    }

    // ── REQ-0203 4c: `state` 봉투 + `actions` (설계 §6.5·§6.8·§6.9) ──────────────
    // 🔴 **`actions` 는 서버가 계산해 *값으로* 준다.** 화면이 "모듈 목록 + 노드 생사"로 조합하지 않는다 —
    //   조합 규칙이 화면에도 생기면 두 답이 갈리고 **관대한 쪽이 이기면 죽은 노드에 명령이 나간다.**
    // 🔑 **키의 존재/부재가 한 비트를 나른다**: 없으면 "그 조작은 지금 이 자리에 없다"(버튼을 안 그린다),
    //   있는데 `ok:false` 면 "뜻은 있는데 막혔다". **뜻 없는 것을 `ok:false` 로 보내면 화면이
    //   영영 안 풀리는 막힌 버튼을 그린다.**
    Node* node_of(const std::string& devid) {
        std::vector<Node*> ns = all_nodes();
        for (size_t i = 0; i < ns.size(); i++) if (ns[i]->devid == devid) return ns[i];
        return NULL;
    }
    // 자리 하나의 막힌 이유. 빈 문자열 = 안 막혔다. **코드 다섯 중에서만 고른다**(§6.5).
    std::string zone_block_reason(const Zone& z) {
        // 🔴 **2026-08-19 — "모듈이 없다"와 "아직 등록을 안 받았다"는 다른 상태다.**
        //   모듈은 등록(`D`)이 끝나야 자리에 붙는다(`bind_modules`). 그래서 **등록 전에는
        //   모든 자리가 `module_absent` 로 보인다** — 화면은 그것을 *"이 자리엔 장비가 없다"*(영구)
        //   로 읽는데 실제로는 **곧 붙는다**(일시). 🔑 **영구와 일시를 같은 코드로 답하면
        //   화면이 영영 안 그리는 자리를 만든다.**
        //   ⚠ `reg_giveup` 은 "형성 중"이 아니다 — 포기한 뒤에는 `module_absent` 가 정직하다.
        if (z.modules.empty()) {
            std::vector<Node*> ns = all_nodes();
            bool any_unreg = false;
            for (size_t i = 0; i < ns.size(); i++)
                if (!ns[i]->reg_done && !ns[i]->reg_giveup) any_unreg = true;
            if (ns.empty())  return "node_offline";        // 노드가 아예 없다
            if (any_unreg)   return "node_unregistered";   // 형성 중이다
            return "module_absent";                        // 등록이 끝났는데 이 자리엔 안 붙었다
        }
        for (size_t i = 0; i < z.modules.size(); i++) {
            Node* n = node_of(z.modules[i].first);
            if (!n) return "module_absent";
            // ⚠ **주차 노드만 생사 판정이 있다**(`device_online`). 보조 노드는 `online` 필드를 쓴다.
            bool up = (n == &park) ? device_online() : n->online;
            if (!up) return "node_offline";
            if (!n->reg_done) return "node_unregistered";
        }
        // 같은 자리에 이미 전선/큐에 나간 명령이 있으면 사용자 조작을 겹치지 않는다
        for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.slot == z.id) return "pending";
        return "";
    }
    void emit_action(std::ostringstream& o, bool& first,
                     const char* name, const std::string& reason) {
        if (!first) o << ",";
        first = false;
        o << jstr(name) << ":{\"ok\":" << (reason.empty() ? "true" : "false")
          << ",\"reason\":" << (reason.empty() ? std::string("null") : jstr(reason)) << "}";
    }
    std::string state_json() {
        std::ostringstream o;
        int split_now = 0;                     // 이번 판의 갈린 자리 수 — 끝에서 계기에 옮긴다
        o << "{\"type\":\"state\",\"srv_id\":" << jstr(srv_id)
          << ",\"epoch\":" << map_epoch << ",\"ts_ms\":" << epoch_ms() << ",\"zones\":[";
        for (size_t i = 0; i < zones.size(); i++) {
            const Zone& z = zones[i];
            if (i) o << ",";
            const std::string blk = zone_block_reason(z);
            int si = slot_index(z.id);                 // 예약 상태는 여전히 slots[] 가 원본이다
            o << "{\"id\":" << jstr(z.id);
            // ── 🔴 `occupied` 를 **그 자리의 센서들에서 유도한다** (명세 §9.2)
            //   한 자리에 센서가 둘이므로 두 값에서 하나를 만들어야 한다. **서버가 계산한다** —
            //   화면이 조합하게 두면 규칙이 두 곳에 생긴다(§"파생 값은 원본을 가진 쪽이 계산한다").
            int v_known = 0, v_total = 0, v_ones = 0;
            for (size_t m = 0; m < z.modules.size(); m++) {
                const Node* mn = node_by_devid(z.modules[m].first);
                int mi = -1;
                if (mn)
                    for (size_t k = 0; k < mn->mods.size(); k++)
                        if (mn->mods[k].first == z.modules[m].second) { mi = (int)k; break; }
                if (mi < 0) continue;
                // 🔑 **점유 센서만 센다.** 차단봉(OBV)은 자리 점유를 말하지 않는다(명세 §8.1).
                if (!mn || mn->mods[mi].second != "IP") continue;
                v_total++;
                if (mi < mn->mod_bits_n) { v_known++; if (mn->mod_bits[mi]) v_ones++; }
            }
            if (z.kind == "parking") {
                // 🔴 **OR 다.** 두 오류의 대가가 대칭이 아니다 —
                //   빈 자리를 "찼다"고 하면 손해는 자리 하나이고,
                //   찬 자리를 "비었다"고 하면 **운전자가 가서 못 댄다.**
                //   ⚠ AND 로 하면 센서 하나가 죽었을 때 그 자리가 영영 "비었다"로 보인다.
                const bool occ = (v_ones > 0);
                const char* vs = (v_total == 0 || v_known == 0) ? "unknown"
                               : (v_known == v_total ? "known" : "partial");
                // 🔴 `value_state` 는 **모든 자리에 항상 싣는다.** 선택적이면 화면이 안 본다.
                //   ⚠ `unknown` 일 때 `occupied:false` 는 **"비었다"가 아니라 "모른다"** 이다.
                o << ",\"occupied\":" << (occ ? "true" : "false")
                  << ",\"value_state\":" << jstr(vs)
                  << ",\"value_known\":" << v_known << ",\"value_total\":" << v_total;
                if (si >= 0) o << ",\"reserved\":" << (slots[si].reserved ? "true" : "false");
                // 🔴 **둘 다 아는데 값이 갈리면 센다.** 이중화의 목적이 고장 감지인데
                //   세지 않으면 이중화가 아무 일도 안 한다(명세 §9.2).
                if (v_known >= 2 && v_ones > 0 && v_ones < v_known) split_now++;
            } else if (si >= 0) {
                o << ",\"occupied\":" << (slots[si].occupied ? "true" : "false")
                  << ",\"reserved\":" << (slots[si].reserved ? "true" : "false");
            }
            o << ",\"actions\":{";
            bool first = true;
            if (z.kind == "parking" && si >= 0) {
                // 🔑 **뜻이 있는 것만 넣는다.** 예약이 없는 자리의 `cancel` 은 **키 자체를 안 보낸다**
                if (!slots[si].reserved) {
                    // 🔴 **차가 있는 자리는 예약할 수 없다** (REQ-0235 · web 이 찾음 · 2026-08-19).
                    //   장치가 그렇게 판정한다 — `client.ino`: `if (occMask & bit) result = 1;`
                    //   그리고 설계 의도가 그 위 주석에 있다: **`occupied=1 ∧ reserved=1` 은
                    //   "예약하고 나서 주차한" 성공 상태**다. **반대 순서는 없다.**
                    //
                    //   ⚠ **여는 근거와 막는 근거가 다르면 반드시 창이 생긴다.**
                    //     여기서 `ok:true` 를 주면 화면이 버튼을 그리고, 누르면 장치가 `result=1` 로
                    //     거절한다 — **사용자에게는 "눌렀는데 안 되는 버튼"이다.**
                    //   🔑 `actions` 가 있는 이유가 **실패할 것을 안 내놓는 것**이다.
                    //
                    //   ⚠ **키를 빼지 않고 `ok:false` 로 준다**: 차가 나가면 예약이 가능해지므로
                    //     *"뜻이 없다"* 가 아니라 *"뜻은 있는데 막혔다"* 다(§6.5 의 존재/부재 규칙).
                    const std::string rr = (blk.empty() && slots[si].occupied) ? "occupied" : blk;
                    emit_action(o, first, "reserve", rr);
                } else {
                    emit_action(o, first, "cancel",  blk);
                }
            } else if (z.kind == "entrance" || z.kind == "exit") {
                emit_action(o, first, "open_gate",  blk);
                emit_action(o, first, "close_gate", blk);
            }
            o << "}";
            // §3.5 완료 판정 — 🔴 **ACK 이 아니라 `S` 의 에코 비트로 판정한다.**
            //   ACK 은 *"명령이 도착해 적용됐다"* 까지다. **실제로 열렸는지는 장치가 매 슬롯 에코한다.**
            //   ⚠ ACK 으로 판정하면 장치가 받고 **못 움직였을 때** 화면이 "열렸다"로 그린다 — `거짓 완료`.
            //   🔑 에코가 성립하는 조건: **비교할 값이 매 슬롯 온다**(원장 §8.21 이 실측한 그 조건).
            //   값 셋만 쓴다: `pending`(내가 건 명령이 아직 있다) · `settled`(에코가 있다) · `unknown`(모른다).
            {
                bool any_pending = false;
                for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it)
                    if (it->second.kind == 'G' && it->second.slot == z.id) any_pending = true;
                const int gi = gate_index_of(z);
                // **닫힌 집합 넷**: `pending` · `settled` · `mismatch` · `unknown`
                //   pending  — 내가 건 명령이 아직 떠 있다
                //   settled  — 마지막으로 **요청한 값과 에코가 같다**
                //   🔴 mismatch — 요청과 에코가 **다르다.** 장치가 못 했거나 되돌아갔다.
                //                **이 값이 없으면 거짓 완료가 `settled` 로 보인다.**
                //   unknown  — 이 세션에 명령한 적이 없거나 비트를 못 읽었다
                const char* comp = "unknown";
                std::map<int,int>::const_iterator wi = gate_want.find(gi);
                if (any_pending)                                  comp = "pending";
                else if (gi >= 0 && gi < park.mod_bits_n && wi != gate_want.end())
                    comp = (park.mod_bits[gi] == wi->second) ? "settled" : "mismatch";
                o << ",\"completion\":\"" << comp << "\",\"modules\":[";
            }
            for (size_t m = 0; m < z.modules.size(); m++) {
                if (m) o << ",";
                // 🔴 **이제 값이 있다** — `S` 의 비트열에서 뽑는다(비트 `idx`).
                //   ⚠ **모르면 여전히 `known:false` 다.** 등록 전이거나 폭을 못 읽었으면 `mod_bits_n == 0` 이고,
                //     그때 `value:false` 를 내면 **화면이 "닫혀 있다"를 사실로 그린다.** 모름과 거짓은 다르다.
                // 🔴 ②-d — **그 모듈의 노드**에서 색인과 비트를 읽는다(주 노드 고정을 걷어낸다).
                const Node* mn = node_by_devid(z.modules[m].first);
                int mi = -1;
                if (mn)
                    for (size_t k = 0; k < mn->mods.size(); k++)
                        if (mn->mods[k].first == z.modules[m].second) { mi = (int)k; break; }
                const bool known = (mn && mi >= 0 && mi < mn->mod_bits_n);
                // 🔴 **`known:false` 에는 사유를 붙인다** (명세 §8.10 · 2026-08-19)
                //   전에는 원인 셋이 **같은 모양**으로 나왔다 — 원인이 다른데 표시가 같으면 아무도 못 고친다.
                //   ⚠ 어휘는 §6.5 의 기존 코드를 먼저 쓰고 없는 것만 새로 만들었다.
                //   ⚠ `reason` 은 **`known:false` 일 때만** 싣는다(존재/부재 규칙).
                const char* why = 0;
                if (!known) {
                    const bool off = mn && (mn == &park ? !device_online() : !mn->online);
                    if (!mn)                      why = "node_unregistered";
                    else if (mi < 0)              why = "module_absent";
                    else if (off)                 why = "node_offline";
                    else if (mn != &park && mn->mod_bits_n == 0)
                        // 🔑 보조 노드는 `S` 가 아직 파서에 안 들어간다(②-b 가 `D` 만 넣었다).
                        //   **구조적으로 값 경로가 없다** — "해독 실패"와 다른 사건이다.
                        why = "bits_unavailable";
                    else if (mn->mod_bits_n == 0) why = "bits_undecoded";
                    else                          why = "bits_out_of_range";
                }
                o << "{\"devid\":" << jstr(z.modules[m].first)
                  << ",\"name\":" << jstr(z.modules[m].second)
                  << ",\"idx\":" << mi
                  << ",\"value\":" << (known ? (mn->mod_bits[mi] ? "true" : "false") : "null")
                  << ",\"known\":" << (known ? "true" : "false");
                if (why) o << ",\"reason\":" << jstr(why);
                o << "}";
            }
            o << "]}";
        }
        o << "]}";
        sensor_split_now = split_now;      // 🔑 누적이 아니라 **지금 값**으로 덮는다
        return o.str();
    }
    void push_state() { ws_broadcast(state_json()); }

    std::string snapshot_json() {
        std::ostringstream o;
        o << "{\"type\":\"snapshot\",\"ts\":" << epoch_ms()
          << ",\"device\":{\"online\":" << (device_online() ? "true" : "false")
          << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq);
        // §9.1 — **파일 폴백(`data_log.json`)과 같은 키·같은 단위·같은 정의**로 낸다(REQ-0132).
        // 값은 `ard_last_epoch_ms` = **마지막 유효 S 프레임을 받은 서버 시각(epoch ms)** 이고,
        // "스냅샷을 만든 시각"(위 `ts`)과 **다른 것**이다. 둘이 갈리는 게 신선도 표시의 요점이다.
        //
        // ⚠ 전에는 이 키가 **WS 에만 없고 파일에만 있었다.** 그래서 신선도 표시가
        // **폴백일 때만 동작하고 정상 운영 중에는 영영 "알 수 없음"** 이었다 —
        // 기능이 가장 필요한 경로에서만 빠져 있던 것이다(web-engineer 크롬 실측).
        //
        // 한 번도 프레임을 못 받았으면 **`null`** 이다. `0` 을 내보내면 화면이 그것을
        // epoch 로 읽어 **1970년으로부터의 나이**를 그린다 — 누락보다 나쁘다.
        o << ",\"last_frame_ts\":";
        if (ard_last_epoch_ms > 0) o << ard_last_epoch_ms; else o << "null";
        o << "}";
        // §5.3 test_mode — 출처는 S 의 tmask 다(§12A.4). 서버가 T 를 보냈다는 사실이 아니다.
        int novr = 0;
        for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) novr++;
        o << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
          << ",\"override_count\":" << novr << "}";
        o << ",\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) o << ",";
            o << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved << ",\"user_id\":";
            if (slots[i].user_id.empty()) o << "null"; else o << jstr(slots[i].user_id);
            o << ",\"reserved_at\":";
            if (slots[i].reserved_at == 0) o << "null"; else o << slots[i].reserved_at;
            o << ",\"overridden\":" << ((test_armed && test_ovr[i]) ? 1 : 0);
            o << "}";
        }
        o << "]}";
        return o.str();
    }
    void push_snapshot() {
        ws_broadcast(snapshot_json());
        // 🔑 **옛 봉투와 같은 순간에 새 봉투를 낸다** — 같은 서버 상태에서 파생시키므로
        //   두 경로가 서로 다른 말을 할 수 없다(web §1.2 의 "한 화면이 두 진실" 방지).
        ws_broadcast(state_json());
    }

    // ---------- 이음매: 디바이스 → 도메인 (REQ-0096 단계 C)
    // **디바이스 계층은 이것만 부른다.** 무엇을 할지는 도메인이 정한다.
    // **방금 넣은 이벤트의 참조를 돌려준다.** 호출자가 `pending_events.back()` 을 다시 집으면
    // "emit_dev 는 반드시 push 한다"는 전제가 코드가 아니라 우연에 걸린다 — 나중에 여기에
    // 조기 반환(중복 억제·devid 가드·상한)이 하나 들어오는 순간 빈 벡터에 back() 을 부른다.
    // 경고 0 · 자가검증 통과로 다 빠져나가고 운영에서만 터지는 형태다.
    DeviceEvent& emit_dev(uint8_t kind, const std::string& dev, const std::string& reason) {
        DeviceEvent e;
        seam_clear_event(&e);
        e.kind = kind;
        seam_set_dev(e.device_id, dev.c_str());
        // reason 은 표시·기록용이라 잘려도 판정이 안 바뀐다. 다만 **잘렸다는 사실은 남긴다.**
        size_t n = sizeof(e.reason) - 1;
        if (reason.size() <= n) {
            memcpy(e.reason, reason.c_str(), reason.size());
            e.reason[reason.size()] = '\0';
        } else {
            memcpy(e.reason, reason.c_str(), n - 1);
            e.reason[n - 1] = '~';          // 잘림 표시
            e.reason[n] = '\0';
        }
        pending_events.push_back(e);
        return pending_events.back();
    }

    // DEV_ACK 전용 — 계약(`server_seam.h`)이 `rid`·`result` 를 정의해 뒀으므로 **채워서 낸다.**
    // 지금 소비자는 이 둘을 안 읽지만, 0 으로 두면 이벤트가 "rid 0 번 명령의 성공"이라고
    // 거짓말한다. 값이 있는데 안 싣는 것과 없는 것은 다르다.
    void emit_dev_ack(const std::string& dev, uint16_t rid, uint8_t result,
                      const std::string& reason) {
        DeviceEvent& e = emit_dev(DEV_ACK, dev, reason);   // 반환 참조를 쓴다(back() 아님)
        e.rid    = rid;
        e.result = result;
    }

    // 도메인이 이벤트를 소비한다. `run()` 루프 끝에서 한 번에 부른다.
    // **여기서 하는 일이 옮기기 전의 직접 호출과 같아야 한다** — 단계 C 는 구조만 바꾸고
    // 동작은 안 바꾼다. 같은 틱 안에서 소비하므로 지연도 없다(브라우저가 보는 것은 동일).
    void drain_dev_events() {
        if (pending_events.empty()) return;
        bool need_snapshot = false, need_log = false;
        for (size_t i = 0; i < pending_events.size(); i++) {
            switch (pending_events[i].kind) {
                case DEV_DISCONNECT:
                    need_snapshot = true;   // 화면에 "센서 끊김"이 뜨게(옮기기 전과 동일)
                    break;
                case DEV_ACK:
                    // 옮기기 전 A 분기 끝의 `write_log_if_changed(); push_snapshot();` 과 같다.
                    // ⚠ **예약 상태 변경(slots[])은 아직 A 분기에 그대로 있다.** 그것까지 옮기면
                    // `send_ack` 와 상태 변경의 순서가 바뀐다 — 이음매 1 의 마지막(DEV_SENSORS)에서
                    // 같은 문제를 한꺼번에 다룬다. 여기서는 **기록·화면만** 옮긴다.
                    need_log = true; need_snapshot = true;
                    break;
                case DEV_ONLINE:
                case DEV_OFFLINE:
                    // ⚠ 이 두 종류는 **파일 쓰기까지** 해야 한다(§9.4 개정 9).
                    // 안 하면 장치가 조용할 때 `device.online` 이 영영 false 로 안 남는다 —
                    // 필드가 가장 필요한 순간에 거짓말을 한다.
                    need_log = true; need_snapshot = true;
                    break;
                default:
                    break;                  // 아직 안 옮긴 종류 — 직접 호출이 담당한다
            }
        }
        pending_events.clear();
        // 한 틱에 여러 건이 겹쳐도 각각 한 번이면 된다(같은 내용을 두 번 보낼 이유가 없다).
        // 순서는 옮기기 전과 같게 **기록 먼저, 그다음 화면**이다.
        if (need_log) write_log_if_changed();
        if (need_snapshot) push_snapshot();
    }

    void send_err(sock_t fd, const std::string& rid, const char* code, const char* msg) {
        std::ostringstream o;
        o << "{\"type\":\"error\",\"rid\":" << (rid.empty() ? std::string("null") : jstr(rid))
          << ",\"code\":\"" << code << "\",\"message\":" << jstr(msg) << "}";
        if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());
    }
    // §4-B (REQ-0155 에서 web 과 합의한 계약) — **"받았고 아직 안 보냈다"**
    //
    // 🔴 **별도 타입이어야 한다. `ack` 에 필드로 얹으면 안 된다.** web 의 dispatcher 는
    // 모르는 타입을 조용히 무시하므로 별도 타입은 옛 화면에서 안전하지만, `ack` 에 얹으면
    // **옛 화면이 `type==='ack'` 만 보고 pending 을 지우며 "예약되었습니다"를 띄운다** —
    // **전선에 나가지도 않은 요청을 성공으로 선언한다.**
    //
    // `ahead`      : 앞에 몇 건. "대기 중"만으로는 **멈춘 것과 밀린 것**을 못 가른다.
    // `expires_ms` : 🔴 **지속시간이다. 절대 시각이 아니다.** epoch 로 주면 시계 어긋난 만큼 틀린다.
    //   그리고 이 값이 재는 것은 **"큐에서 나갈 때까지"**이고 **최종 결말까지가 아니다** —
    //   web 이 자기 타이머를 `expires_ms + 6초` 로 잡으므로, 여기에 총예산을 넣으면 **이중 계산**이 된다.
    void send_queued(const DownQ& q) {
        int ahead = 0;
        for (size_t i = 0; i < downq.size(); i++) {
            if (downq[i].wire_rid == q.wire_rid) break;
            ahead++;
        }
        long long left = q.deadline_ms - now_ms();
        if (left < 0) left = 0;                 // 마감을 넘겨도 음수를 내보내지 않는다
        std::ostringstream o;
        o << "{\"type\":\"queued\",\"rid\":" << jstr(q.brid)
          << ",\"slot\":" << (q.slot.empty() || q.slot == "??" ? std::string("null")
                                                              : std::string("\"") + q.slot + "\"")
          << ",\"ahead\":" << ahead
          << ",\"expires_ms\":" << left
          // 🔴 **이탈 후 예산**(REQ-0166). `expires_ms` 는 큐 대기까지만 덮으므로
          // 이 둘을 더한 것이 화면이 기다려야 하는 전부다. **화면이 짐작하지 않는다.**
          // ⚠ 필드를 더하는 것이 안전한 이유: `queued` 자체가 새 타입이라
          // **모르는 화면은 프레임 통째로 무시한다** — 옛 화면을 깨뜨리는 경로가 없다.
          << ",\"ack_budget_ms\":" << ack_budget_ms() << "}";
        if (q.ws_fd != BAD_SOCK && conns.count(q.ws_fd)) ws_send(q.ws_fd, o.str());
    }
    void send_ack(sock_t fd, const std::string& rid, const std::string& slot, int result,
                  char kind = 'R', char top = 0) {
        const char* m = "예약되었습니다";
        if (kind == 'T')      m = "테스트 값을 적용했습니다";
        else if (kind == 'C') m = "예약을 취소했습니다";
        else if (kind == 'M') m = "시뮬레이션 한 걸음 진행했습니다";
        // 🔴 **`G` 가 없어서 차단봉 조작 ACK 이 "예약되었습니다" 로 나갔다**(2026-08-19 발견).
        //   ⚠ 시험이 전부 `ws_fd = BAD_SOCK` 이라 **이 줄이 한 번도 안 돌았다** —
        //     `send_ack` 자체를 건너뛰는 경로였다. **시험이 실기와 다르게 밟은 것**이다.
        else if (kind == 'G') m = (top == '1') ? "차단봉을 열었습니다" : "차단봉을 닫았습니다";
        if (result == 1) m = "이미 주차된 자리입니다";
        else if (result == 2) m = "이미 예약된 자리입니다";
        // 🔴 `result=3` 의 뜻이 종류마다 다르다. `G` 에서는 **"장치가 수행할 수 없다"** 이지
        //   "잘못된 요청"이 아니다 — 사용자가 할 일이 다르다(다시 눌러도 같다 vs 요청을 고쳐라).
        else if (result == 3) m = (kind == 'G') ? "장치가 이 조작을 수행할 수 없습니다"
                                                : "잘못된 요청입니다";
        else if (result == 4) m = "테스트 모드가 꺼져 있습니다";
        // result=5 는 **성공이 아니다.** 버튼을 눌렀는데 아무 일도 안 난 것을
        // 화면이 성공으로 표시하면 안 되므로 값과 문구를 따로 둔다(§12B.4).
        else if (result == 5) m = "바꿀 시뮬 자리가 없습니다";
        std::ostringstream o;
        o << "{\"type\":\"ack\",\"rid\":" << jstr(rid) << ",\"slot\":";
        // 무장/해제처럼 자리가 없는 응답은 null 이다 — 전선의 "??" 를 그대로 흘리지 않는다(§5.4).
        if (slot.empty() || slot == "??") o << "null"; else o << "\"" << slot << "\"";
        o << ",\"result\":" << result << ",\"message\":" << jstr(m) << "}";
        if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());   // 요청자에게만
    }

    // ---------- data_log.json (§9)
    std::string bits(bool reserved_bits) {
        std::string s;
        for (int i = 0; i < 10; i++)
            s += char('0' + (reserved_bits ? slots[i].reserved : slots[i].occupied));
        return s;
    }
    std::vector<std::string> log_entries;
    void write_log_if_changed() {
        if (no_disk) return;                 // --selftest 는 파일을 건드리지 않는다
        // 테스트 상태도 키에 넣는다 — 안 넣으면 무장/주입이 파일에 반영되지 않고
        // 폴백 화면이 낡은 "해제" 상태를 계속 보여 준다(개정 4 가 막으려는 바로 그 증상).
        // §9.4(개정 9) — **키에 online 을 넣는다.** 안 넣으면 장치가 죽어도 비트열이 그대로라
        // 파일이 안 써지고 `device.online` 이 영원히 true 로 남는다 — 필드가 거짓말을 한다.
        // 넣으면 오프라인/복귀 **전이가 그 자체로 상태 변화**가 되어 그 순간 한 건이 기록된다.
        // (`last_frame_ts` 는 키에 넣지 않는다. 넣으면 매 프레임이 변화가 되어 초당 한 번 쓴다.)
        bool online_now = device_online();
        std::string key = bits(false) + "|" + bits(true) + "|" + (test_armed ? "A" : "-");
        for (int i = 0; i < 10; i++) key += char('0' + ((test_armed && test_ovr[i]) ? 1 : 0));
        key += online_now ? "|O" : "|X";
        if (key == last_bits) return;            // §9.4 — 내용이 같으면 안 쓴다
        last_bits = key;

        std::ostringstream e;
        e << "{\"ts\":" << epoch_ms() << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq)
          // §9.1(개정 9) — 장치 생사. 이게 없으면 폴백 화면이 **죽은 장치의 낡은 값을
          // 실측처럼 그린다**(개정 4의 test_mode 누락과 같은 종류의 구멍이었다).
          // last_frame_ts 는 epoch 시각이고 "파일을 쓴 시각"이 아니다 — 둘이 갈리는 게 요점이다.
          << ",\"device\":{\"online\":" << (online_now ? "true" : "false")
          // ⚠ 한 번도 프레임을 못 받았으면 `0` 이 아니라 **`null`** 이다(REQ-0132).
          // `0` 은 epoch 로 읽히므로 화면이 **1970년으로부터의 나이**를 그린다.
          // WS 스냅샷(`snapshot_json()`)과 **같은 규칙**이어야 한다 — 두 경로가 갈리면
          // 폴백 여부에 따라 화면이 다른 말을 한다.
          << ",\"last_frame_ts\":" << (ard_last_epoch_ms > 0
                                       ? std::to_string(ard_last_epoch_ms) : std::string("null"))
          << "}"
          << ",\"occupied\":\"" << bits(false) << "\",\"reserved\":\"" << bits(true) << "\"";
        // §9.1(개정 4) — 무장 여부와 칸별 주입 표시를 **파일에도** 넣는다.
        // WS 가 끊기면 브라우저는 이 파일로 폴백하는데, 이 두 필드가 없으면
        // **폴백 화면이 주입값을 실측처럼 그린다** — §12A.6 이 폴백 경로에서만 깨지고 있었다.
        {
            int n = 0;
            for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) n++;
            e << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
              << ",\"override_count\":" << n << "}";
        }
        e << ",\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) e << ",";
            e << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved
              << ",\"overridden\":" << ((test_armed && test_ovr[i]) ? 1 : 0) << "}";
        }
        e << "]}";

        log_entries.insert(log_entries.begin(), e.str());       // 최신이 앞
        while ((int)log_entries.size() > LOG_KEEP) log_entries.pop_back();

        std::string body = "[\n";
        for (size_t i = 0; i < log_entries.size(); i++) {
            body += "  " + log_entries[i];
            if (i + 1 < log_entries.size()) body += ",";
            body += "\n";
        }
        body += "]\n";

        atomic_write_log(body);
    }

    // tmp 에 다 쓰고 원자적으로 갈아끼운다 (§9.2). 잠금도 복사본도 필요 없다.
    // **쓰기 경로는 이것 하나다** — 기동 시 빈 배열을 만들 때도 이 함수를 쓴다.
    void atomic_write_log(const std::string& body) {
        const char* tmp = "data_log.json.tmp";
        const char* dst = "data_log.json";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) { logf("!", "data_log tmp 열기 실패"); return; }
            f << body;
            f.flush();
        }
        // 실패하면 **원인 코드를 반드시 남긴다.** 안 남기면 data_log.json 이 조용히 옛 내용을
        // 유지하는데 로그에는 아무것도 없다. 윈도우에서 백신·편집기가 파일을 잠깐 잡아
        // ERROR_SHARING_VIOLATION(32) 이 나는 것은 흔한 일이라 이 구분이 실제로 필요하다.
#ifdef _WIN32
        // 윈도우는 대상이 있으면 rename 이 실패한다 (§9.3).
        // "먼저 지우고 rename" 으로 흉내내면 그 틈에 읽은 클라이언트가 404 를 본다 — 원자성이 깨진다.
        if (!MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
            DWORD e = GetLastError();
            char b[128];
            snprintf(b, sizeof(b), "MoveFileEx 실패 GetLastError=%lu%s",
                     (unsigned long)e,
                     e == 32 ? " (ERROR_SHARING_VIOLATION — 다른 프로그램이 파일을 잡고 있다)" : "");
            logf("!", b);
        }
#else
        if (rename(tmp, dst) != 0) {
            char b[128];
            snprintf(b, sizeof(b), "rename 실패 errno=%d (%s)", errno, strerror(errno));
            logf("!", b);
        }
#endif
    }

    // ── 🔴 A[1](B) 커서 영속 — **재시작을 건너 단조성을 유지한다**
    //
    // ⚠ **기본은 꺼져 있다**(`rid_persist_on = false`). 자가검증이 만드는 수십 개의 `Server` 가
    //   실기 커서 파일을 덮어쓰는 것을 **구조적으로** 막는다 — 시험이 운영 상태를 건드리면
    //   그건 시험이 아니라 사고다. **실기 기동 경로만 `rid_cursor_load()` 를 부른다.**
    void rid_reserve_block() {
        rid_reserved_to = rid_cursor + RID_PERSIST_BLOCK;
        if (!rid_persist_on || rid_cursor_file.empty() || no_disk) return;
        ensure_parent_dir(rid_cursor_file);
        std::ofstream o(rid_cursor_file.c_str(), std::ios::out | std::ios::trunc);
        if (!o) {
            rid_persist_on = false;    // 🔴 한 번 실패하면 끈다. 매 발행마다 실패 로그를 쏟지 않는다
            logf("!", "🔴 rid 커서를 못 쓴다 — " + rid_cursor_file
                      + " · **재시작 보호가 꺼졌다.** 다음 재시작이 장치 멱등 캐시와 겹칠 수 있다");
            return;
        }
        o << rid_reserved_to << "\n";
    }

    // 기동 때 한 번. 🔴 **읽은 값을 로그에 찍는다** — 못 읽은 것은 사고인데
    //   값이 안 찍히면 정상 기동과 구별이 안 된다(루트 지시 REQ-0246 ②·③).
    void rid_cursor_load() {
        rid_cursor_file = rid_cursor_path();
        if (rid_cursor_file.empty()) {
            rid_persist_on = false;
            rid_cursor = (long long)(epoch_ms() % RID_SPACE);      // 🔴 1 에서 시작하지 않는다
            rid_reserved_to = rid_cursor;
            logf("!", "🔴 rid 커서를 영속할 수 없다 — HOME 이 없다. 임의 지점 "
                      + std::to_string(rid_cursor % RID_SPACE)
                      + " 에서 시작한다. **이 기동은 장치 멱등 캐시와 겹칠 수 있다**");
            return;
        }
        rid_persist_on = true;
        std::ifstream f(rid_cursor_file.c_str());
        long long v = -1;
        if (f && (f >> v) && v >= 0) {
            rid_cursor = v;
            rid_reserved_to = v;
            // 🔴 **기동 때 곧바로 한 블록을 예약해 적는다.** 첫 발행까지 미루면
            //   **하행이 한 건도 없던 기동에서는 파일이 안 생기고**, 다음 재시작이
            //   "못 읽었다" 갈래로 빠져 **거짓 경보**가 된다. 그리고 그 경보가 반복되면
            //   진짜 사고가 났을 때 아무도 안 본다.
            rid_reserve_block();
            logf("=", "rid 커서 이어받음 — " + std::to_string(v)
                      + " (전선값 " + std::to_string(v % RID_SPACE) + ") · 예약 "
                      + std::to_string(rid_reserved_to) + " · " + rid_cursor_file);
            return;
        }
        // 🔴 없거나 깨졌다. **1부터 시작하지 않는다**(루트 지시 ④).
        //   임의 지점이 겹칠 확률과 1에서 시작할 때 겹칠 확률은 다르다 —
        //   1은 **직전 인스턴스가 방금 지나온 자리**일 수 있고 임의 지점은 그 편향이 없다.
        //   ⚠ 그래도 **보장이 아니다.** 그래서 이 줄을 크게 남긴다.
        rid_cursor = (long long)(epoch_ms() % RID_SPACE);
        rid_reserved_to = rid_cursor;
        rid_reserve_block();              // 곧바로 파일을 만든다 — 다음 기동은 이 갈래로 안 온다
        logf("!", "🔴 rid 커서 파일을 못 읽었다(" + rid_cursor_file
                  + ") — 임의 지점 " + std::to_string(rid_cursor % RID_SPACE)
                  + " 에서 시작한다. ⚠ **이 기동만 장치 멱등 캐시와 겹칠 수 있다(약 "
                  + std::to_string(DEV_RID_CACHE_N) + "/" + std::to_string((int)RID_SPACE)
                  + "). 정상 기동과 구별해서 읽어라**");
    }

    // ── 🔴 A[1] `rid` 발행 — 정본 `docs/net/DESIGN-rid-width-and-quarantine.md`
    //
    // **하드 규칙 ①**: `pend` 에 있는 rid 는 **절대** 발행하지 않는다. 어기면 ACK 이 엉뚱한
    //   명령에 붙어 **자료를 조용히 오염시킨다.** 압력이 아무리 높아도 이 규칙은 안 푼다.
    // **소프트 규칙 ②**: 해제된 rid 는 `RID_QUARANTINE_MS` 묵힌다(늦은 ACK 보호).
    //   공간이 차면 **가장 오래 묵은 것부터 조기 해제**하고 `rid_forced` 를 올린다.
    //   🔑 거절 경로를 새로 만들지 않는 이유: 화면 계약(`blocked_reason`)에 사유가 하나 늘면
    //   **이 배포가 재려는 `>=64B` 진입률 축에 다른 축이 섞인다**(설계 §3.3).
    uint16_t alloc_rid() {
        const long long t = now_ms();
        // 1차 — 커서에서 한 바퀴. `pend` 도 아니고 격리도 안 걸린 첫 값.
        for (uint16_t i = 0; i < RID_SPACE; i++) {
            uint16_t cand = (uint16_t)(rid_cursor % RID_SPACE);
            rid_cursor++;
            // 🔴 **커서가 예약 범위를 넘으면 디스크에 다음 블록을 적는다.**
            //   건너뛴 칸도 커서를 먹는다 — 그래야 크래시 뒤에 그 칸들도 안 되밟는다.
            if (rid_cursor >= rid_reserved_to) rid_reserve_block();
            if (pend.count(cand)) { rid_skips++; continue; }
            std::map<uint16_t, RidQ>::iterator q = rid_quar.find(cand);
            if (q != rid_quar.end()) {
                if (t < q->second.until_ms) { rid_skips++; continue; }
                rid_quar.erase(q);                  // 격리 기간이 지났다
            }
            rid_alloc_n++;
            return cand;
        }
        // 2차 — 한 바퀴가 다 막혔다. **격리만 조기 해제한다. `pend` 는 안 건드린다.**
        // 🔴 **해제 *순번* 이 가장 작은 것을 고른다. 시각이 아니다.**
        //   시각으로 고르면 같은 밀리초에 해제된 것들이 동률이 되어 **같은 칸이 반복해서 뽑히고**
        //   재사용 간격이 1까지 떨어진다 — 자가검증 ㉜(a)가 실제로 그것을 잡았다.
        uint16_t best = RID_NONE; long long best_seq = 0;
        for (std::map<uint16_t, RidQ>::iterator it = rid_quar.begin();
             it != rid_quar.end(); ++it) {
            if (pend.count(it->first)) continue;    // 하드 규칙 ①
            if (best == RID_NONE || it->second.seq < best_seq) { best = it->first; best_seq = it->second.seq; }
        }
        if (best == RID_NONE) {
            // `pend` 가 1000칸을 다 먹은 극단. **여기서만 발행을 포기한다.**
            rid_exhausted++;
            return RID_NONE;
        }
        rid_quar.erase(best);
        rid_forced++; rid_alloc_n++;
        return best;
    }

    // `pend` 를 떠난 rid 를 격리에 넣는다. 🔴 **모든 해제 지점에서 부른다** —
    // 한 곳이라도 빠지면 그 rid 는 격리 없이 재사용되어 이 변경 이전 거동으로 되돌아간다.
    // ⚠ 그리고 그 누락은 **아무 증상도 안 낸다.** 늦은 ACK 이 와야 드러난다.
    void rid_release(uint16_t rid) {
        if (rid >= RID_SPACE) return;   // 옛 판이 남긴 큰 값 — 순환 공간 밖이라 격리 대상이 아니다
        RidQ q;
        q.until_ms = now_ms() + RID_QUARANTINE_MS;
        q.seq      = ++rid_rel_seq;     // 🔑 시계 해상도와 무관한 **순서**
        rid_quar[rid] = q;
    }

    // ---------- 아두이노로 요청 내리기
    void dispatch(char kind, sock_t ws_fd, const std::string& brid,
                  const std::string& slot, const std::string& uid) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) {
            logf("!", "rid 공간 고갈 — 하행 발행 포기 (pend=" + std::to_string(pend.size()) + ")");
            return;
        }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot;
        p.plate = uid;                       // 서버가 기억할 원본(번호판일 수 있다)
        p.user_id = wire_userid(uid);        // 전선에 나갈 값 — ASCII 0*8 아니면 빈 값
        p.kind = kind;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;

        char buf[64];
        // **p.user_id 를 쓴다. 인자 uid 를 쓰면 안 된다** — uid 는 번호판일 수 있고
        // 그러면 UTF-8 이 그대로 전선에 나가 §2.1(ASCII 전용)과 §2.3 을 위반한다.
        // 실제로 그 버그를 냈다: `R,1,B2,980가4568,F7` 이 나가 아두이노가 죽었다.
        if (kind == 'R') snprintf(buf, sizeof(buf), "R,%u,%s,%s,", rid, slot.c_str(), p.user_id.c_str());
        else             snprintf(buf, sizeof(buf), "C,%u,%s,", rid, slot.c_str());
        // 🔑 **전송하지 않는다. 큐에 담는다.** 나가는 것은 다음 창(= 다음 `S` 도착)이다.
        if (!enqueue_down(pend[rid], build_line(buf), true, false)) { pend.erase(rid); rid_release(rid); }
    }

    // §2.4 `T` — 테스트 모드 제어. R/C 와 같은 pend 표·재전송·타임아웃을 그대로 탄다.
    // Pending.user_id 를 tval 보관에 재사용하고, slot 에 "??" 가 들어갈 수 있다.
    void dispatch_test(sock_t ws_fd, const std::string& brid,
                       char op, const std::string& slot, const std::string& tval) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — T 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot; p.user_id = tval; p.kind = 'T';
        p.top = op;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        // ⚠ **`T` 도 같은 큐를 탄다** — 하행이면 규율을 지킨다(설계 §7 은 "안 정했다"였고
        // 여기서 정한다). 대가가 있다: `T` 는 §8.7·§8.17 에서 **착지 타이밍을 재는 도구**였는데
        // 큐에 태우면 안전한 창으로 강제되어 **"착지 시점을 의도적으로 맞춘 시험"의 수단이 사라진다.**
        // → 그 시험이 필요하면 `--down-immediate` 로 옛 거동을 쓴다. **도구가 바뀐 사실은 원장 §8.23.**
        if (!enqueue_down(pend[rid], build_line(test_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
    }
    // §12B — 시뮬레이터 한 걸음. **무장 여부로 막지 않는다**(테스트 모드와 별개).
    void dispatch_sim(sock_t ws_fd, const std::string& brid) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — M 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.kind = 'M'; p.top = 0;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        if (!enqueue_down(pend[rid], build_line(sim_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
    }
    // ── 🔴 자리 조작 `G` — **전선 형식은 arduino 의 파서에서 읽어 맞췄다**(REQ-0228 · 말로 받지 않았다)
    //
    //   `G,<rid>,<idx>,<op>,<ck>`
    //     idx : **모듈 인덱스**(등록 `D` 순서). 장치는 `idx >= SLOT_N && idx < moduleCount()` 만 받는다
    //     op  : **0 이 아니면 열기, 0 이면 닫기**
    //   ACK  : `A,<rid>,G<d>,<result>,<ck>`  (d = idx % 10) · result 0=수행 · **3=수행 불가**
    //
    // ⚠ **ACK 의 둘째 칸이 자리 이름이 아니다**(`G0` 같은 값). 서버의 ACK 경로가 이미
    //   `slot_index(slot) < 0` 이면 `p.slot` 으로 되돌리므로 그대로 동작한다 — **상관 키는 `rid`** 다.
    // 🔴 **완료는 이 ACK 이 아니라 다음 `S` 의 비트 `idx` 로 판정한다.**
    //   ACK 은 *"명령이 도착해 적용됐다"* 까지이고, **실제로 열렸는지는 장치가 매 슬롯 에코한다.**
    static std::string gate_prefix(const Pending& p) {
        char buf[64];
        snprintf(buf, sizeof(buf), "G,%u,%d,%d,", p.wire_rid, p.mod_idx, (p.top == '1') ? 1 : 0);
        return std::string(buf);
    }
    // 자리에 붙은 **명령 가능한 모듈**의 인덱스. 없으면 -1.
    // 🔑 **자리 → 모듈 라우팅은 여기 한 곳에만 있다**(설계 §6.8) — 화면은 자리만 지목한다.
    // ── 🔴 ②-d — **그 모듈을 등록한 노드를 찾는다** (REQ-0262 · 2026-08-19)
    //   전에는 `state_json()` 이 **주 노드만** 보고 나머지는 `known:false` 로 냈다.
    //   그건 **버그가 아니라 옳은 답**이었다 — 값 경로가 없었으니까(설계 §8.9).
    //   ②-c 로 비트열이 노드별이 됐고 ②-b 로 보조 노드 등록이 들어오므로 **이제 채울 수 있다.**
    const Node* node_by_devid(const std::string& dev) const {
        // ⚠ **빈 `devid` 를 걸러내지 않는다.** 처음에 `if (dev.empty()) return 0;` 을 넣었다가
        //   **자가검증 셋이 깨졌다**(E1·X1 값, 세션 종료 뒤 known). 이유:
        //   승격 전 주차 노드는 `devid` 가 비어 있고 그 상태로 등록하면 지형에 `("", "E1")` 이 들어간다.
        //   옛 코드는 `z.modules[m].first == park.devid` 로 **빈 값끼리 맞춰** 동작했다.
        //   🔑 방어를 넣으면서 **거동을 같이 바꿨다** — 시험이 아니었으면 화면에서 값이 조용히 사라졌다.
        //   보조 노드의 빈 `devid` 는 `adopt_as_aux()` 에서 이미 고쳤으므로 여기서 또 막을 필요가 없다.
        if (dev == park.devid) return &park;
        std::map<std::string, AuxNode>::const_iterator it = aux.find(dev);
        return (it != aux.end()) ? &it->second : 0;
    }

    // ⚠ **이 함수는 주차 노드만 본다. 그대로 둔다** — 보조 노드는 **상행 전용**이라
    //   하행 경로가 없다. 없는 노드에 조작 색인을 만들어 주면 **누를 수 있는 버튼**이 생기고
    //   눌러도 아무 일이 안 난다. 🔑 §"조용히 성공으로 답하지 않는 것이 지금의 정답" 그대로다.
    int gate_index_of(const Zone& z) const {
        for (size_t m = 0; m < z.modules.size(); m++) {
            if (z.modules[m].first != park.devid) continue;      // 지금은 주차 노드만 명령을 받는다
            const std::string& nm = z.modules[m].second;
            for (size_t i = 0; i < park.mods.size(); i++)
                if (park.mods[i].first == nm && kind_commandable(park.mods[i].second))
                    return (int)i;
        }
        return -1;
    }
    void dispatch_gate(sock_t ws_fd, const std::string& brid,
                       const std::string& slot, int idx, bool open) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — G 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot; p.kind = 'G'; p.mod_idx = idx;
        p.top = open ? '1' : '0';
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        gate_want[idx] = open ? 1 : 0;      // 🔑 **대조할 값을 여기서 남긴다**(ACK 이 지우기 전에)
        // 🔑 **큐에 들어간 것만 센다.** 거절되면 전선에 안 나갔으므로 장치거절의 분모가 아니다 —
        //   분모에 넣으면 "장치가 멀쩡한데 거절률이 낮아 보이는" 착시가 생긴다.
        if (!enqueue_down(pend[rid], build_line(gate_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
        else gate_q++;
    }
    static std::string sim_prefix(const Pending& p) {
        char buf[32];
        snprintf(buf, sizeof(buf), "M,%u,", p.wire_rid);
        return std::string(buf);
    }
    static std::string test_prefix(const Pending& p) {
        char buf[64];
        snprintf(buf, sizeof(buf), "T,%u,%c,%s,%s,",
                 p.wire_rid, p.top, p.slot.c_str(), p.user_id.c_str());
        return std::string(buf);
    }

    // ---------- 타이머 (§7.3 재전송 / §3.4 offline)
    void tick() {
        long long t = now_ms();
        // ── 창을 포기하고 쏘는 경로 (설계 §3)
        // `S` 가 안 오면 창이 영영 안 열려 **하행이 조용히 사라진다.** 안 쏘는 것보다
        // 나가서 실패하는 것이 낫다 — 조용한 소실이 더 나쁘다.
        // ⚠ `DOWN_DMAX_MS`(2400) < `OFFLINE_MS`(3500) 이므로, 서버가 "장치 없음"으로
        // 판단하기 **전에** 한 번은 시도한다.
        // ⚠ `ard_seen` 이 false 면 `ard_last_ms` 는 의미 없는 0 이라 이 조건이 즉시 참이다 —
        // **그게 맞다.** 승격 직후 재하달은 창 개념이 없고, 보통은 같은 틱의 첫 `S` 처리가
        // 먼저 내보내므로 여기까지 오지 않는다.
        // ⚠ **슬롯당 1회로 묶는다**(위 `last_dmax_ms` 주석). 안 묶으면 200ms 버스트가 된다.
        // ── §5 등록 질의 `Q` ────────────────────────────────────────────────────
        // 🔴 **상한은 서버에 둔다 — 비용이 여기 있다.** `Q` 한 번이 **창당 1거래를 독점**하고
        //    그동안 그 노드의 하행이 못 나간다. 장치가 답하는 비용은 배치 하나이고 내 창을 안 먹는다.
        //    **비용 없는 쪽에 상한을 두면 복구 경로만 지운다**(설계 §5 · arduino 합의).
        // ⚠ **`Q` 를 보내는 것 자체가 이 소켓이 승격됐다는 뜻이다** — 승격 전이면 보낼 곳을 모른다.
        if (ard != BAD_SOCK && ard_seen && !park.reg_done && !park.reg_giveup
            && park.reg_first_ms && (t - park.reg_first_ms) > REG_TIMEOUT_MS
            && (t - park.last_q_ms) >= DOWN_SLOT_MS) {
            if (park.q_sent >= REG_Q_MAX) {
                park.reg_giveup = true;
                reg_giveups++;
                logf("!", "등록 포기 — `Q` " + std::to_string(REG_Q_MAX)
                          + "회에도 `D` 가 안 왔다. node_unregistered 로 굳힌다 (device="
                          + park.devid + ")");
                // 🔑 **끝없이 묻는 것보다 "모른다"를 확정하는 것이 낫다.** 안 그러면 이 노드가
                //    매 창을 질의로 먹는다. 재무장은 **새 세션**이다(설계 §5 의 열린 자리).
            } else {
                park.q_sent++; park.last_q_ms = t; reg_qsent++;
                // `Q,<ck>` — **인자 없음.** 주소는 소켓이 갖고 있고, 이 시점의 devid 는
                // 서버가 확신할 수 없는 값이다(미등록이라 묻는 것이다 · 설계 §5).
                std::string q = build_line("Q,");
                if (!send_raw(ard, q.data(), q.size(), "아두이노")) ard_send_failed();
                else logf("→ARD", "Q (등록 질의 " + std::to_string(park.q_sent) + "/"
                                  + std::to_string(REG_Q_MAX) + ")");
            }
        }

        // 🔴 `dmax_armed` 가 주 조건이다. 슬롯 문턱은 **그물로 남긴다** —
        //    재무장 논리에 결함이 생겨도(예: 줄이 폭주) 버스트로 돌아가지 않게.
        if (!downq.empty() && ard != BAD_SOCK && (t - ard_last_ms) > DOWN_DMAX_MS
            && dmax_armed && (t - last_dmax_ms) >= DOWN_SLOT_MS) {
            last_dmax_ms = t;
            dmax_armed = false;        // 장치가 한 줄이라도 말할 때까지 다시 안 쏜다
            dmax_flushes++;            // 그래서 이 수가 "몇 번 포기했나"로 읽힌다
            // ⚠ **탐침 크기로 쏜다**(4건 아님) — 증거가 없는 자리다.
            flush_downq("S 가 안 온다 — 창 포기(탐침)", true, DOWN_PROBE_N);
        }
        std::vector<uint16_t> dead;
        for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it) {
            Pending& p = it->second;
            // 🔴 **큐에서 기다리는 동안은 ACK 시계가 안 돈다.** 안 그러면 전선에 나가기도 전에
            // 재전송이 걸려 같은 rid 가 큐에 두 번 들어간다 — 큐가 없애려던 증폭이 큐 안에서 생긴다.
            if (p.queued) continue;
            if (t - p.sent_ms < ACK_TIMEOUT_MS) continue;
            if (p.tries >= ACK_MAX_TRIES) {
                ack_fail_count++;                         // 하행 건강 지표(소크 요약)
                logf("!", "ACK 타임아웃 최종 실패 wire_rid=" + std::to_string(p.wire_rid));
                send_err(p.ws_fd, p.browser_rid, "ack_timeout", "센서가 응답하지 않습니다");
                dead.push_back(it->first);
                continue;
            }
            p.tries++;
            retx_count++;                                 // 하행 건강 지표(소크 요약)
            p.sent_ms = t;
            char buf[64];
            std::string line;
            if (p.kind == 'G') line = gate_prefix(p);      // 🔑 같은 rid → 장치가 멱등 캐시로 같은 답을 준다
            else if (p.kind == 'M') line = sim_prefix(p);  // 재전송이 두 걸음이 되면 안 된다(§12B.4)
            else if (p.kind == 'T') line = test_prefix(p); // 테스트도 같은 wire_rid 로 재전송
            else if (p.kind == 'R') {
                snprintf(buf, sizeof(buf), "R,%u,%s,%s,", p.wire_rid, p.slot.c_str(), p.user_id.c_str());
                line = buf;
            } else {
                snprintf(buf, sizeof(buf), "C,%u,%s,", p.wire_rid, p.slot.c_str());
                line = buf;
            }
            logf("↻", "재전송 " + std::to_string(p.tries) + "/" + std::to_string(ACK_MAX_TRIES)
                      + " (같은 wire_rid=" + std::to_string(p.wire_rid) + ") — 큐에 다시 넣는다");
            // 🔴 **재전송도 하행이다 — 같은 큐를 탄다.** 창 밖에서 쏘면 규율이 무의미해진다.
            //   announce=false : `queued` 를 다시 보내면 같은 rid 의 `expires_ms` 가 **늘어나** 단조성이 깨진다
            //   force=true     : 이미 약속한 건이므로 깊이 상한으로 거절하지 않는다
            // ⚠ 대가: 재시도 간격이 창 주기에 종속된다. `ACK_TIMEOUT_MS`(1500) + 창 대기(≤1200)
            //   → 3회 소진이 최악 ≈6.9초(전에는 4.5초). web 타이머는 `expires_ms(≤4800) + 6000`
            //   = 10.8초라 **아직 안 겹친다.** `ACK_TIMEOUT_MS` 는 **일부러 안 건드렸다** —
            //   한 교체에 변경 둘을 넣으면 깨질 때 어느 것 때문인지 못 가른다(원장 §6).
            // 🔴 **반환값을 봐야 한다.** 링크가 끊긴 뒤에는 `end_ard_session` 이 큐만 비우고
            // **전선에 나가 있던 pend 는 남는다.** 그 건이 여기로 와서 거절되면(`ard==BAD_SOCK`)
            // 이미 `device_offline` 로 답이 갔는데, 반환값을 무시하면 `pend` 에 남아
            // 몇 틱 뒤 **`ack_timeout` 으로 한 번 더** 답한다.
            // ⚠ 그 이름은 **"전선에 나갔다. 3회 재전송까지 했다"** 를 뜻한다(§8.16) —
            // 서버가 스스로 거절한 건에 붙이면 **로그에 거짓 문장이 남고 `ack_fail_count` 도 오염된다.**
            // 08-17 07:54:40 줄이 정확히 그 형태였다. 같은 것을 새 코드에 만들지 않는다.
            if (!enqueue_down(p, build_line(line), false, true)) {
                dead.push_back(it->first);   // ⚠ 루프 안에서 erase 하면 반복자가 무효화된다
                continue;
            }
        }
        for (size_t i = 0; i < dead.size(); i++) { pend.erase(dead[i]); rid_release(dead[i]); }
    }

    // 그 자리에 아직 ACK 를 못 받은 요청이 있는가.
    // 있으면 세계관이 갈라진 게 아니라 "진행 중"일 뿐이므로 손대면 안 된다(§7.6).
    bool has_pending_for(const std::string& slot) const {
        for (std::map<uint16_t, Pending>::const_iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.slot == slot) return true;
        return false;
    }

    // ---------- §7.5 예약 은퇴
    // 예약된 자리에서 차가 빠지면(occupied 1→0) 그 예약은 소진된 것이다.
    // **ACK 를 기다리지 않고 서버 쪽에서 즉시 확정한다** — ACK 실패 시 살려 두면
    // 애초에 이걸 만들게 한 "죽은 자리" 버그가 은퇴 경로로 되돌아온다.
    void retire(int i) {
        logf("⏏", std::string("예약 소진(occupied 1→0) — ") + SLOT_ID[i]
                  + " 은퇴시킨다 (user=" + (slots[i].user_id.empty() ? "-" : slots[i].user_id) + ")");
        slots[i].reserved = 0;
        slots[i].user_id.clear();
        slots[i].reserved_at = 0;
        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");   // 아두이노에도 반영시킨다
    }

    // ---------- 아두이노 재부팅 → 재동기화 (§7.4)
    void resync_reservations(const char* why) {
        resync_count++;                      // --selftest 계측용 (§7.4 오탐 증명)
        std::vector<int> live;
        for (int i = 0; i < 10; i++) if (slots[i].reserved) live.push_back(i);
        logf("⟳", std::string("재부팅 감지(") + why + ") — 살아 있는 예약 "
                  + std::to_string(live.size()) + "건 재하달");
        for (size_t k = 0; k < live.size(); k++)
            dispatch('R', BAD_SOCK, "", SLOT_ID[live[k]], slots[live[k]].user_id);
    }

    // ---------- 아두이노 라인 처리
    // ⏳ **호환 껍데기 — 자가검증 전용.** 실기 호출부 둘은 이미 `park` 를 명시로 넘긴다.
    //   자가검증 50여 곳을 이 조각에서 같이 고치면 **거동 변화 0 의 증명 범위가 넓어진다.**
    //   🔴 ②-b(소켓별 버퍼 라우팅)에서 없앤다. **그때까지 새 호출부는 이걸 쓰지 마라.**
    void on_ard_line(const std::string& line) { on_ard_line(park, line); }

    // 🔴 ②-a (REQ-0263/0262) — **노드를 인자로 받는다.** 시그니처만 바꾼다.
    //   지금 호출부는 둘 다 `park` 를 넘기므로 **거동 변화 0 이어야 하고, 산출물 대조로 증명한다.**
    //   🔑 이것이 슬롯 경로 추출의 전제다 — `park` 32회 참조가 인자로 바뀌지 않으면
    //     "노드 안에서 닫힌 파서"가 안 되고, 그러면 떼어 내도 전역을 계속 만진다(설계 §8.13).
    void on_ard_line(Node& n, const std::string& line) {
        // 🔑 **받은 바이트 수를 같이 남긴다** — `AT+CIPSEND` 조용한 잘림(③)의 유일한 판별자다.
        // 장치는 `SEND OK` 를 받으면 **성공으로 세므로 잘린 것을 모른다.**
        // **서버가 "몇 바이트 받았나"를 적어야 "보낸 만큼 왔나"를 대조할 수 있다.**
        // ⚠ 64B 이하에서는 값이 뻔하지만, **긴 줄 시험에서 이 한 칸이 시험의 본체**가 된다.
        {
            char rb[24];
            snprintf(rb, sizeof(rb), " (rx=%zuB)", line.size());
            logf("←ARD", line + rb);
        }

        // 🔴 **전제 감시 — 조건을 적었으면 그것을 보는 코드를 같은 자리에 둔다**(CLAUDE.md).
        // `DOWN_BATCH_MAX_N` 은 배출 6 에서 유도됐고, 배출 6 은 `S_worst ≤ 60B` 를 전제한다.
        // ⚠ **개정에서 그 전제가 깨지는데 서버는 아무것도 안 하고 계속 4건을 보낸다** —
        //   그게 이 감시가 없을 때의 모습이다. **적어 두기만 하면 다음 사람이 안 본다.**
        // 🔑 새 배출률을 **여기서 계산하지 않는다**(arduino 의 상수를 복제하게 된다).
        //   서버는 **"전제가 깨졌다"까지만 말하고**, 값은 원본을 가진 쪽이 준다.
        // 🔑 **장치가 말했다 = 조건이 바뀌었다.** 창 포기를 다시 무장한다(REQ-0210).
        //    `S` 만이 아니라 **어떤 줄이든** 무장한다 — 깨진 줄도 "링크가 살아 있다"는 증거다.
        dmax_armed = true;

        if (!line.empty() && line[0] == 'S') {
            if ((int)line.size() > s_max_b) s_max_b = (int)line.size();
            if ((int)line.size() > DEV_S_WORST_ASSUMED_B && !s_worst_warned) {
                s_worst_warned = true;
                char wb[224];
                snprintf(wb, sizeof(wb),
                         "전제 붕괴 — S 프레임 %dB > 가정 %dB. "
                         "DEV_ACK_DRAIN_PER_SLOT=%d(→ 하행 %d건/창)의 근거가 깨졌다. "
                         "arduino 에 배출률 재확인 필요(자리 수 n 이 커졌을 가능성)",
                         (int)line.size(), DEV_S_WORST_ASSUMED_B,
                         DEV_ACK_DRAIN_PER_SLOT, DOWN_BATCH_MAX_N);
                logf("!!", wb);
            }
        }

        // ---------- 소크 관측 (REQ-0065) — **체크섬 검사보다 먼저 센다.**
        // 여기서 재는 것은 "쓸 만한 프레임"이 아니라 **수신 자체의 공백**이다.
        // 깨진 줄도 링크가 살아 있었다는 증거이므로 공백을 끊는다.
        {
            long long t = now_ms();
            if (sess_last_line_ms) {
                long long gap = t - sess_last_line_ms;
                if (gap > sess_max_gap_ms) sess_max_gap_ms = gap;
                if (gap > all_max_gap_ms) { all_max_gap_ms = gap; all_max_gap_at = epoch_ms(); }
            }
            sess_last_line_ms = t;
            sess_frames++; all_frames++;
        }

        std::vector<std::string> f;
        if (!verify_line(line, f)) { drop_cksum++; logf("!", "체크섬 불일치 — 버림"); return; }
        if (f.empty()) return;

        if (f[0] == "S" && f.size() >= 6) {
            // 🔴 **삼중 검산 ③ — `S` 의 자리 폭이 선언 `n` 과 맞는가**(설계 §5).
            //    ①선언 n · ②실제 `D` 줄 수 · ③hex 폭 이 **한 함수(`moduleCount()`)에서 나오므로
            //    갈릴 수 없다. 갈리면 그 자체가 결함 신호다.**
            // ⚠ **등록이 끝난 노드에만 적용한다** — 옛 펌웨어는 등록을 안 하므로 이 검사를 안 탄다.
            //    (옛 10진 형식은 폭이 `n` 이고 hex 는 `ceil(n/4)` 라 규칙이 다르다)
            if (!n.reg_first_ms) n.reg_first_ms = now_ms();   // `REG_TIMEOUT` 의 기준
            if (n.reg_done && n.reg_n > 0) {
                const size_t want = (size_t)((n.reg_n + 3) / 4);
                if (f[2].size() != want) {
                    reg_widthbad++;
                    char wb[176];
                    snprintf(wb, sizeof(wb),
                             "🔴 자리 폭 불일치 — S 의 폭 %zu, 선언 n=%d 이면 %zu 여야 한다. "
                             "같은 함수에서 나온 값이 갈렸다 (누적 %lld)",
                             f[2].size(), n.reg_n, want, reg_widthbad);
                    logf("!!", wb);
                }
            }
            long seq = atol(f[1].c_str());
            long long up = atoll(f[4].c_str());
            // §7.4(개정 8) — 순환을 접은 uptime 전진량 하나로 본다.
            // **seq 는 판정에 쓰지 않는다**: 1Hz 에서 18.2시간마다 합법적으로 0 을 지나간다.
            bool reboot = uptime_says_reboot(ard_uptime, up);

            // ── 세션을 가로지르는 판정 (REQ-0118 (A)) — **보고 전용** ──────────────
            // 새 세션의 첫 프레임(`ard_uptime < 0`)에서만 본다. 지금까지 서버는 이 순간을
            // 무조건 "재부팅"이라 불렀지만, 직전 세션의 uptime 을 기억하면 갈린다:
            //   · 되감겼다  → 장치가 정말 재부팅했다
            //   · 이어진다  → 장치는 살아 있었고 **링크만 다시 선 것**이다
            // 08-16 기준선에서 새 연결 36건 중 35건이 후자였다. 지금까지 전부 전자로 셌다.
            //
            // ⚠ 위 `reboot` 변수에는 **손대지 않는다.** 그것은 §7.4 판정이고 여기는 장부다.
            if (ard_uptime < 0) {
                // 공백 G = 직전 프레임 도착 → 지금(초). **되감김만 보면 안 된다**:
                // 재부팅했는데 공백이 길면 새 uptime 이 옛 값을 넘어서 "안 죽었다"로 오분류된다.
                // (monitor 가 자기 규칙 `uptime>120` 에서 같은 함정을 밟았다 — 2026-08-16)
                //
                // 안 죽었다면 지금 uptime = 옛 uptime + G 이므로 **반드시 G 이상**이다.
                // 죽었다면 부팅이 공백 안에서 일어났으므로 **G 미만**이다. 그래서 G 가 기준이다.
                // 허용오차 2초: 장치 시계는 초 단위 절삭이고 도착 시각도 틱에 걸린다.
                long long G = xs_last_ms ? (now_ms() - xs_last_ms) / 1000 : -1;
                if (xs_uptime < 0 || xs_dev != f[5] || G < 0) {
                    xs_reconnect_unknown++;      // 기억 없음/다른 장치 — 모른다고 말한다
                } else if (up < xs_uptime || up + 2 < G) {
                    xs_reconnect_reboot++;
                    logf("⟳", "재연결 판정: **재부팅** — uptime " + std::to_string(xs_uptime)
                              + " → " + std::to_string(up) + " · 공백 " + std::to_string(G) + "초"
                              + (up < xs_uptime ? " (되감김)" : " (공백보다 짧은 가동시간)"));
                } else {
                    xs_reconnect_link++;
                    logf("=", "재연결 판정: **링크 재접속**(장치는 안 죽었다) — uptime "
                              + std::to_string(xs_uptime) + " → " + std::to_string(up)
                              + " (+" + std::to_string(up - xs_uptime) + "초) · 공백 "
                              + std::to_string(G) + "초");
                }
            }
            xs_uptime = up; xs_dev = f[5]; xs_last_ms = now_ms();   // 세션이 끊겨도 남는다

            ard_seq = seq; ard_uptime = up; ard_dev = f[5];
            ard_last_ms = now_ms(); ard_last_epoch_ms = epoch_ms(); ard_seen = true;

            // 🔴🔴 **자리 비트열 해독** — 형식이 둘이고 **틀리면 오류 없이 값만 어긋난다**
            //   옛 펌웨어 : 10진 문자열 `0110000010` — **한 칸에 한 자리**
            //   새 펌웨어 : hex `182` — 전체를 `n` 비트 정수로 보고 **슬롯 i = 비트 (n−1−i)**
            // ⚠ **`n` 을 모르면 hex 를 못 푼다.** 그리고 **첫 `S` 는 `D` 보다 먼저 온다**(명세 §5) —
            //   그때 `n=10` 을 가정해 풀면 **폭 검사도 체크섬도 통과하고 모든 비트가 어긋난다.**
            //   🔑 **모르는 값으로 해독하느니 안 하는 것이 낫다**(arduino 권고 · 채택).
            // ⚠ 2026-08-19 에 이 구멍이 **실기에서 열려 있었다** — 장치가 hex 를 보내는데
            //   서버가 10진으로 읽어 **엉뚱한 자리를 점유로 표시했다.** 폭 검사를 넣어 둔 것이
            //   **"처리된 것처럼" 보이게 만들었다.**
            int occ[10];
            int mod_state[REG_MODS_MAX];
            // 🔑 해독 규칙은 `decode_slot_bits()` 한 곳에 있다(원장 §8.23-(66)).
            int  occ_bits  = decode_mod_bits(f[2], mod_state);
            bool occ_known = (occ_bits > 0);
            for (int i = 0; i < 10; i++) occ[i] = mod_state[i];
            // 🔑 **자리 열 개를 넘는 비트를 보관한다** — 조작 완료 판정이 이것을 읽는다.
            //   ⚠ 못 읽었으면 `mod_bits_n = 0` 으로 남긴다. **모른다와 0 은 다르다.**
            n.mod_bits_n = occ_bits;
            if (occ_bits > 0) for (int i = 0; i < REG_MODS_MAX; i++) n.mod_bits[i] = mod_state[i];
            if (!occ_known) {
                // 🔴 미등록 + hex 로 보이는 폭 → **해독하지 않는다.** 등록 뒤 다음 `S` 부터 읽는다
                if (!occ_undecoded_warned) {
                    occ_undecoded_warned = true;
                    logf("!", "자리 비트열을 해독하지 않는다 — 등록 전이라 n 을 모른다(폭 "
                              + std::to_string(f[2].size()) + "). **등록되면 읽는다**");
                }
            }
            occ_undecoded += occ_known ? 0 : 1;

            // §2.4 tmask — **선택 필드다. 없으면 해제로 본다**(§2.1 규칙 8, 옛 펌웨어 수용).
            // f 는 체크섬을 뺀 필드들이므로 7번째(index 6)가 있으면 그것이 tmask 다.
            // ⚠ **2026-08-19 — 잠복 결함을 미리 닫는다.** 예전 조건은 `f[6].size() >= 10` 이라
            //   **10진 폭을 전제**했다. 지금은 장치가 이 칸을 아예 안 보내서(필드 6개) 안 돌지만,
            //   **tmask 가 hex 로 오는 순간 `res` 와 똑같이 조용히 죽는다.**
            //   🔑 arduino 가 확인해 줬다 — *"세 필드를 같이 바꿨으므로 읽는 쪽도 셋이다."*
            //   **잠복은 "나중에 밟는다"는 뜻이고 그때는 또 아무도 안 본다. 지금 닫는다.**
            int ovr[10];
            int ovr_bits_[REG_MODS_MAX];
            bool armed = false;
            for (int i = 0; i < 10; i++) ovr[i] = 0;
            if (f.size() >= 7 && f[6] != "-" && !f[6].empty()) {
                // 🔴 **칸이 있다는 것 자체가 "무장"이다.** 해독 성공 여부와 분리한다 —
                //   못 읽는다고 `armed=false` 로 답하면 **장치가 시험 모드인데 서버가 정상이라 믿는다.**
                //   못 읽으면 무장은 알리되 덮어쓰기 목록은 비운다(안전한 방향).
                armed = true;
                if (decode_mod_bits(f[6], ovr_bits_) > 0)
                    for (int i = 0; i < 10; i++) ovr[i] = ovr_bits_[i];
            }

            if (reboot) {
                reboot_by_uptime++;                  // 소크 관측(REQ-0065) — 2차 방어선이 실제로 몇 번 걸리는가
                resync_reservations("uptime 전진량이 한 바퀴에 가깝다");
            }

            // §7.5 — occupied 1→0 이면 예약 소진.
            // 재부팅했거나 기준선이 없으면 **판정하지 않고 기준선만 세운다**(§7.5-1).
            // 이걸 빼면 재부팅으로 occupied 가 초기화될 때 있지도 않은 1→0 전이가 잡혀
            // 방금 재하달한 예약을 그 자리에서 죽인다.
            if (base_valid && !reboot) {
                for (int i = 0; i < 10; i++) {
                    if (!(base_occ[i] == 1 && occ[i] == 0)) continue;      // 1→0 전이가 아니면 무관
                    if (slots[i].reserved != 1) continue;
                    if (has_pending_for(SLOT_ID[i])) continue;
                    // §7.5-3 **되돌림은 출차가 아니다.** 오버라이드 비트가 같이 1→0 이면
                    // 가짜 값을 걷어낸 것이지 차가 빠진 게 아니다. 이 구분이 없으면
                    // 해제(D) 한 번에 열 칸이 되돌아가면서 **예약이 통째로 은퇴한다.**
                    if (base_ovr[i] == 1 && ovr[i] == 0) {
                        logf("=", std::string("되돌림 감지 — ") + SLOT_ID[i]
                                  + " 은퇴시키지 않는다(테스트 오버라이드 해제)");
                        continue;
                    }
                    retire(i);
                }
            } else {
                logf("=", "은퇴 기준선 설정 — 이 프레임은 전이 판정을 건너뛴다");
            }
            for (int i = 0; i < 10; i++) {
                base_occ[i] = occ[i]; slots[i].occupied = occ[i];
                base_ovr[i] = ovr[i]; test_ovr[i] = ovr[i];
            }
            base_valid = true;
            if (test_armed != armed)
                logf("*", std::string("테스트 모드 ") + (armed ? "무장" : "해제")
                          + " (출처: S 의 tmask)");
            test_armed = armed;

            // reserved 는 서버가 durable owner 다(§7.4). 아두이노 값으로 덮지 않는다.
            // 다만 서버가 0 인데 아두이노가 1 이면 세계관이 갈라진 것이므로 C 를 다시 내려 맞춘다(§7.6).
            // 미결 요청이 있는 자리는 제외 — 아직 ACK 를 못 받았을 뿐인 정상 상태다.
            // 🔴 **2026-08-19 정정**: 이 조건이 예전에는 `f[3].size() >= 10` 이었다.
            //   장치가 hex 로 바뀌자(`21:12:09`) 폭이 3 이 되어 **조건이 영영 거짓** —
            //   자가 치유가 **한 번도 안 돌았는데 로그에 아무 흔적이 없었다.**
            //   ⚠ 오독이 아니라 **건너뜀**이라 `0` 조차 안 남았다. 그래서 **분모를 센다**(§8.23-(66)).
            int res_bits_[REG_MODS_MAX];
            int res_n_ = (f.size() >= 4) ? decode_mod_bits(f[3], res_bits_) : 0;
            if (res_n_ <= 0) {
                res_undecoded++;
                if (f.size() >= 4 && !res_undecoded_warned) {
                    res_undecoded_warned = true;
                    logf("!", "예약 마스크를 해독하지 않는다 — 등록 전이라 n 을 모른다(폭 "
                              + std::to_string(f[3].size()) + "). **등록되면 읽는다**");
                }
            } else {
                heal_checks++;               // 🔑 **분모** — 이 검사가 실제로 돌았다
                for (int i = 0; i < 10; i++)
                    if (res_bits_[i] && slots[i].reserved == 0 && !has_pending_for(SLOT_ID[i])) {
                        heal_fires++;
                        logf("⚠", std::string("불일치: 아두이노 ") + SLOT_ID[i]
                                  + " reserved=1, 서버 0 → C 재하달");
                        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");
                    }
            }
            // ═══ 내 창이 열렸다. 큐 전부를 한 거래로 내보낸다 ═══════════════════════
            //
            // 🔴 **왜 S 분기의 맨 끝인가**: 이 분기 안에서 하행이 새로 생긴다 —
            // 불일치 감지(`dispatch('C')`)와 uptime 재동기화(`resync_reservations`)가 그것이다.
            // flush 를 `ard_last_ms` 갱신 직후(위쪽)에 두면 **그 치유 `C` 가 다음 창까지 밀려**
            // 원장 §8.21 이 실측한 "갈린 구간 ≈1초"가 조용히 ≈2.2초로 늘어난다.
            // **실측값을 낡게 만드는 배치를 고르지 않는다.**
            //
            // ⚠ 그리고 이 자리 때문에 **승격 직후 두 줄의 순서가 뒤집힌다**(원장 §7.2):
            //     전: `⟳ 재하달` → `→ARD R` → `←ARD S`
            //     후: `⟳ 재하달`(큐) → `←ARD S` → `→ARD R`   ← **같은 틱이다**
            // `adopt_as_parking()` 이 `resync_reservations()` 를 부른 직후 `drain_ard_buf()` 가
            // 첫 프레임을 같은 틱에서 처리하므로 지연은 사실상 0 이고, **순서만 바뀐다.**
            // 🔑 **이것은 회귀가 아니라 이 설계의 정의다** — 서버는 장치가 말한 뒤에 말한다.
            // 면제(승격 시 즉시 송신)를 두지 않은 이유: 그 순간의 위상은 `ard_last_ms` 가
            // 아직 옛 세션 값이라 **측정할 수 없고**, 측정 못 하는 순간에 쏘는 것이 §8.15 다.
            // 🔴 **2026-08-19 — 방송보다 먼저 쏜다.** 전에는 이 아래 둘이 앞에 있었다:
            //     write_log_if_changed()  (파일 쓰기)
            //     push_snapshot()         (**붙어 있는 화면 수에 비례**하는 방송)
            //   창 M 에서 하행 송신 지연이 `≥2ms 9.4% → 40.9%` 로 늘었고, 후보 하나가 이것이었다.
            //   🔑 **하행은 슬롯 창에 묶여 있고 방송은 안 묶여 있다.** 묶인 쪽을 먼저 보낸다.
            //   ⚠ **원래 배치의 이유는 그대로 지킨다**: 이 분기 안에서 하행이 새로 생기는 자리는
            //     `resync_reservations()`(재부팅 감지)와 불일치 치유 `dispatch('C')` 인데
            //     **둘 다 이 줄보다 위에 있다.** 아래 둘은 하행을 만들지 않는다 —
            //     그래서 순서를 바꿔도 §8.21 이 실측한 "갈린 구간 ≈1초"가 안 늘어난다.
            //   🔑 **화면이 스냅샷을 몇 ms 늦게 받는 것은 아무것도 안 깨뜨린다. 장치는 다르다.**
            flush_downq("S 도착 — 창 시작", false);
            write_log_if_changed();
            push_snapshot();
        }
        // ── §5 등록 프레임 `D` ─────────────────────────────────────────────────
        // 🔴 **관측이지 제어가 아니다**(Node::reg_* 주석). 하행 경로를 안 바꾼다.
        // ⚠ 이 분기가 없으면 `D` 는 `drop_unknown`("AT 잡음 유입 의심")으로 떨어져
        //   **세션마다 11씩 오르며 거짓 경보를 만든다.**
        else if (f[0] == "D" && f.size() >= 3) {
            if (f[1] == "*") {
                // `D,*,<drain>,<n>` — 묶음의 맨 앞. **자기 완결적이라 언제 와도 같은 뜻이다.**
                // 🔴 `*` 는 모듈 `name` 이 될 수 없다(설계 §5). 여기 걸리는 것이 그 방어다 —
                //    필드 수·숫자 형식 둘 다 안 맞으면 **모듈로도 배출률로도 안 읽힌다.**
                if (f.size() < 4 || !all_digits(f[2]) || !all_digits(f[3])) {
                    reg_bad++;
                    logf("!", "D,* 형식 위반 — 버림 (name 이 '*' 인 모듈이거나 숫자가 아니다)");
                    return;
                }
                // 🔴 **명세 §7.4 가 약속한 검사다 — 적어 놓고 안 만들면 지켜지는 것처럼 보인다.**
                //   약속: *"새 모듈은 목록 끝에만 붙인다. 중간에 끼우지 않는다."*
                //   깨지면 **`idx` 가 밀려 전선에 나가 있던 `G` 가 다른 모듈을 친다** — 조용히.
                //   ⚠ 이 검사는 *"약속을 어겼나"* 만 잡는다. *"왜 어겼나"* 는 arduino 만 안다.
                prev_mods_snapshot = n.mods;   // 🔑 `D,*` 와 등록 완료는 **다른 프레임**이다 — 멤버로 잇는다
                n.reg_reset();
                n.reg_drain = atoi(f[2].c_str());
                n.reg_n     = atoi(f[3].c_str());
                if (n.reg_n < 0 || n.reg_n > REG_MODS_MAX) {
                    reg_bad++;
                    logf("!", "D,* 의 n=" + f[3] + " 이 범위 밖(0~"
                              + std::to_string(REG_MODS_MAX) + ") — 등록 무효");
                    n.reg_n = -1; return;
                }
                char b[160];
                snprintf(b, sizeof(b), "등록 시작 — 선언 drain=%d · n=%d (device=%s)",
                         n.reg_drain, n.reg_n, n.devid.c_str());
                logf("=", b);
                return;
            }
            // `D,<name>,<kind>` — 모듈 한 줄. **순서가 곧 `idx` 다**(등록 순서가 비트 자리를 정한다).
            if (n.reg_n < 0) { reg_bad++; logf("!", "D,* 없이 모듈 줄이 왔다 — 버림"); return; }
            if ((int)n.mods.size() >= n.reg_n) {
                reg_bad++;
                logf("!", "선언 n=" + std::to_string(n.reg_n) + " 보다 모듈 줄이 많다 — 버림");
                return;
            }
            n.mods.push_back(std::make_pair(f[1], f[2]));
            if ((int)n.mods.size() == n.reg_n) {
                n.reg_done = true;
                reg_ok++;
                bind_modules(n);          // 🔑 등록이 지형을 바꾼다 → epoch 이 여기서 오른다
                // 🔴 **삼중 검산 ①②** — 선언 `n` 과 실제 줄 수. ③(hex 폭)은 `S` 에서 본다.
                // 🔴 **앞부분 대조** — 겹치는 구간이 그대로인가. 하나라도 다르면 `idx` 가 밀린 것이다.
                if (!prev_mods_snapshot.empty()) {
                    size_t n_ov = prev_mods_snapshot.size() < n.mods.size()
                                ? prev_mods_snapshot.size() : n.mods.size();
                    for (size_t i = 0; i < n_ov; i++)
                        if (prev_mods_snapshot[i] != n.mods[i]) {
                            mod_order_changed++;
                            logf("!", "🔴 재등록에서 모듈 순서가 바뀌었다 — idx " + std::to_string(i)
                                      + " 가 `" + prev_mods_snapshot[i].first + "` → `"
                                      + n.mods[i].first + "`. **끝에만 붙인다는 약속이 깨졌다** "
                                      "(명세 §7.4). 떠 있는 조작 명령을 버린다");
                            // ⚠ **떠 있는 `G` 를 버린다.** 그 `idx` 는 이제 다른 모듈을 가리킨다 —
                            //   보내 놓고 결과를 기다리는 것보다 **실패로 끝내는 것이 정직하다.**
                            for (std::map<uint16_t, Pending>::iterator it = pend.begin();
                                 it != pend.end(); ) {
                                if (it->second.kind == 'G') {
                                    if (it->second.ws_fd != BAD_SOCK)
                                        send_err(it->second.ws_fd, it->second.browser_rid,
                                                 "node_unregistered",
                                                 "장치 구성이 바뀌어 조작을 취소했습니다");
                                    uint16_t drid = it->first;
                                    pend.erase(it++);
                                    rid_release(drid);
                                } else ++it;
                            }
                            gate_want.clear();
                            break;
                        }
                }
                prev_mods_snapshot.clear();
                char b[192];
                snprintf(b, sizeof(b),
                         "등록 완료 — n=%d · drain=%d · 명령가능 %d개 (device=%s)",
                         n.reg_n, n.reg_drain, reg_cmdable(), n.devid.c_str());
                logf("=", b);
            }
            return;
        }
        else if (f[0] == "A" && f.size() >= 4) {
            uint16_t rid = (uint16_t)atoi(f[1].c_str());
            std::string slot = f[2];
            int result = atoi(f[3].c_str());
            std::map<uint16_t, Pending>::iterator it = pend.find(rid);
            if (it == pend.end()) {
                // 🔴 **계수한다.** 여기는 격리 가정(설계 §3.1)이 깨졌을 때 오르는 칸이다 —
                //   `RID_QUARANTINE_MS` 보다 늦게 오는 ACK 이 없다는 것을 **잰 적이 없다.**
                //   ⚠ 재전송 중복도 같은 칸에 온다. **둘을 여기서 못 가른다** — 값이 오르면
                //   그때 로그 시각으로 갈라야 한다. 그 한계를 요약 표에 적어 뒀다.
                ack_unknown_rid++;
                logf("!", "모르는 rid 의 ACK — 무시 (재전송 중복일 수 있다) rid=" + std::to_string(rid));
                return;
            }
            Pending p = it->second;
            pend.erase(it);
            rid_release(rid);

            // ── 왕복 실측 (설계 §1) — **`W_srv` 를 상수로 두지 않기 위한 유일한 입력**
            // 이 값은 `RTT + 장치 처리시간` 이라 **RTT 의 상한**이고, 창을 좁히는 방향이라
            // 보수적 = 안전하다. **재전송된 건은 표본에서 뺀다** — `sent_ms` 가 마지막 시도
            // 시각이라 값은 맞지만, 병리 구간이 섞이면 창이 그 최악에 영구히 묶인다.
            if (!p.queued && p.sent_ms > 0 && p.tries <= 1) {
                long long rtt = now_ms() - p.sent_ms;
                if (rtt >= 0 && rtt < DOWN_SLOT_MS * 4) {   // 그 이상은 표본이 아니라 사건이다
                    rtt_last_ms = rtt; rtt_n++;
                    if (rtt > rtt_max_ms) rtt_max_ms = rtt;
                }
            }

            // ── 🔴 에코 자리 대조 — **장치 멱등 캐시 재전송의 서명**을 여기서 본다
            //
            // arduino `docs/arduino/LEDGER.md` §25.3: 장치는 최근 서로 다른 `rid` **16개**를
            // 캐시에 들고 있고, 그 안에서 값이 재등장하면 **명령을 적용하지 않고 옛 ACK 을
            // 재전송한다.** 그러면 **에코된 자리가 옛 명령의 자리**다.
            // ⚠ **서버는 ACK 을 받으므로 `ack_timeout` 이 안 뜬다 — 실패가 성공처럼 보인다.**
            // 🔑 그래서 이 계수가 **`rid` 폭을 줄인 이 변경의 안전 지표**다(설계 §5).
            //
            // ⚠ **탐지만 한다. 거동은 안 바꾼다.** 바로 아래 줄이 "전선 값이 유효한 자리면
            //   그걸 쓴다"인데 주석은 "전선 값을 믿지 않는다"라고 적혀 있다 — **둘이 어긋나 있고
            //   그것을 고치는 것은 거동 변경**이라 이 배포에 안 넣는다(설계 §6.1 · 루트 결정 대기).
            if (slot != "??" && slot_index(slot) >= 0 && !p.slot.empty() && slot != p.slot) {
                ack_slot_mismatch++;
                logf("!", "🔴 ACK 에코 자리 불일치 rid=" + std::to_string(rid)
                          + " 보낸자리=" + p.slot + " 에코=" + slot
                          + " — 장치 멱등 캐시 재전송 의심(arduino §25.3)");
            }

            // result=3 이면 slot 은 "??" 다(§2.4). 전선의 자리 값을 믿지 않고
            // **매핑표의 원래 자리**를 쓴다 — 상관 키는 rid 이고 원본은 서버가 들고 있다.
            if (slot == "??" || slot_index(slot) < 0) slot = p.slot;
            int idx = slot_index(slot);
            // **예약 상태를 건드리는 것은 R/C 뿐이다.** T 를 여기에 섞으면
            // 테스트 주입 ACK 가 cancel 로 취급돼 **그 칸의 예약을 지워 버린다.**
            if (result == 0 && idx >= 0 && (p.kind == 'R' || p.kind == 'C')) {
                if (p.kind == 'R') {
                    slots[idx].reserved = 1;
                    // 화면·스냅샷에는 **원본**을 보여 준다(번호판이면 번호판 그대로).
                    // 전선에 나간 값(p.user_id)은 잘렸거나 비어 있을 수 있다.
                    slots[idx].user_id = p.plate;
                    slots[idx].reserved_at = epoch_ms();
                } else {
                    slots[idx].reserved = 0;
                    slots[idx].user_id.clear();
                    slots[idx].reserved_at = 0;
                }
            }
            // 테스트 결과는 여기서 상태에 반영하지 않는다 — 무장/오버라이드의 진실은
            // 다음 S 프레임의 tmask 다(§12A.4). ACK 는 "명령이 처리됐다"까지만 말한다.
            // 🔴 **`result=3` 은 `ack_timeout` 과 다른 칸에 센다.**
            //   `3` = *"장치가 못 한다"* → **재시도해도 같다.**
            //   `ack_timeout` = *"안 갔다"* → **재시도에 뜻이 있다.**
            //   ⚠ 합쳐 두면 `실패 N` 을 보고 재시도를 늘리는데, 절반은 늘려도 소용이 없다.
            //   (§8.16 의 `error` 한 칸 · §8.23-(38) 과 같은 형태다.)
            if (p.kind == 'G') gate_ans++;       // 🔑 **응답** — result 와 무관하게 답이 온 것
            if (result == 3) {
                dev_reject++;
                logf("!", std::string("장치가 거절했다(result=3) — ") + p.kind + " " + slot
                          + " rid=" + std::to_string(rid) + ". **재시도는 뜻이 없다**");
            }
            if (p.ws_fd != BAD_SOCK) send_ack(p.ws_fd, p.browser_rid, slot, result, p.kind, p.top);
            // 이음매 1: 직접 호출 → 이벤트. **같은 틱의 drain 이 같은 일을 한다**(2481행).
            // 한 틱에 ACK 가 여러 건 겹치면 기록·화면이 건별 → 1회로 접힌다 — 이미 옮긴
            // 3종과 같은 성질이고, 브라우저가 보는 최종 상태는 같다.
            emit_dev_ack(park_dev, rid, (uint8_t)result,
                         std::string("ACK ") + p.kind + " " + slot
                         + " result=" + std::to_string(result));
        }
        else {
            drop_unknown++;
            // ⚠ **주석 정정(2026-08-18)**: 예전에는 이 칸이 오르면 **AT 잡음 유입**을 의심했다.
            // 등록(`D`)이 들어온 뒤로는 **그 해석이 더는 유일하지 않다** — `D` 는 위에서 처리되므로
            // 여기 안 오지만, **새 프레임 종류가 생기면 옛 서버에서 여기로 떨어진다.**
            // 🔑 **`모름` 이 오르면 "잡음"이 아니라 "내가 모르는 프레임"부터 의심해라.**
            logf("!", "모르는 타입 — 조용히 버림");
        }
    }

    // 코드포인트를 UTF-8 로. digitcam 명세 §4.3 의 \uXXXX 복원에 쓴다.
    static void utf8_append(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }

    // ---------- 브라우저 → 서버 (§5.4)
    static std::string jget(const std::string& s, const char* key) {
        std::string pat = std::string("\"") + key + "\"";
        size_t i = s.find(pat);
        if (i == std::string::npos) return "";
        i = s.find(':', i + pat.size());
        if (i == std::string::npos) return "";
        i++;
        while (i < s.size() && (s[i]==' '||s[i]=='\t')) i++;
        if (i < s.size() && s[i] == '"') {
            i++; std::string o;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    char e = s[i+1];
                    // digitcam 명세 §4.3: 한글을 원문 UTF-8 로 보내든 \uXXXX 로 보내든
                    // **수신 측이 둘 다 복원**해야 한다. 이걸 안 하면 이스케이프로 보내는
                    // 송신기에서 번호판이 "123가4568" 로 저장돼 매칭이 전부 빗나간다.
                    if (e == 'u' && i + 5 < s.size()) {
                        unsigned cp = 0; bool ok = true;
                        for (int k = 0; k < 4; k++) {
                            char c = s[i+2+k]; int v;
                            if (c >= '0' && c <= '9') v = c - '0';
                            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                            else { ok = false; break; }
                            cp = (cp << 4) | (unsigned)v;
                        }
                        if (ok) { utf8_append(o, cp); i += 6; continue; }
                    }
                    if (e == 'n') { o += '\n'; i += 2; continue; }
                    if (e == 't') { o += '\t'; i += 2; continue; }
                    if (e == 'r') { o += '\r'; i += 2; continue; }
                    o += e; i += 2; continue;                 // \" \\ \/ 등
                }
                o += s[i++];
            }
            return o;
        }
        std::string o;
        while (i < s.size() && s[i] != ',' && s[i] != '}') o += s[i++];
        while (!o.empty() && (o[o.size()-1]==' ')) o.erase(o.size()-1);
        return o;
    }
    void on_ws_message(sock_t fd, const std::string& msg) {
        logf("←WS", msg.substr(0, 120));
        std::string type = jget(msg, "type");
        std::string rid  = jget(msg, "rid");
        std::string slot = jget(msg, "slot");
        std::string uid  = jget(msg, "user_id");
        if (uid == "null") uid.clear();

        // ---- REQ-0203 4b: `get_map` (설계 §6.8)
        // 🔑 **접속 시의 `map` 과 같은 봉투를 쓴다.** 다른 타입을 만들면 같은 것을 두 형식으로
        //   만들게 되고 한쪽만 고치는 날이 온다.
        // 🔴 **창을 기다리지 않고 즉답한다** — 이건 하행 전선이 아니라 화면 소켓이다.
        if (type == "get_map") {
            // ⚠ **연속 요청 상한.** 화면이 `epoch` 비교를 잘못 구현하면 **무한 재요청**이 된다.
            //   `get_map` 은 상태를 안 바꾸니 몇 번 와도 안전하고, **상한은 서버 보호용이지
            //   정합성용이 아니다** — 그래서 거절해도 화면의 정확성은 안 깨진다.
            const long long t = now_ms();
            // 🔑 **이 연결의 창**을 본다. 남의 화면이 물은 것은 이 화면의 몫을 안 먹는다.
            std::map<sock_t, Conn>::iterator ci = conns.find(fd);
            long long& win = (ci != conns.end()) ? ci->second.getmap_win_ms : getmap_win_ms;
            int&       cnt = (ci != conns.end()) ? ci->second.getmap_in_win : getmap_in_win;
            if (t - win >= 1000) { win = t; cnt = 0; }
            if (++cnt > GETMAP_MAX_PER_SEC) {
                getmap_rejects++;
                logf("!", "get_map 이 한 화면에서 1초에 " + std::to_string(cnt)
                          + "회 — 상한 초과로 거절(누적 " + std::to_string(getmap_rejects) + ")");
                send_err(fd, rid, "rate_limited", "요청이 너무 잦습니다");
                return;
            }
            if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, map_json());
            return;
        }

        // ---- REQ-0203 4d: 자리 조작 요청 (설계 §6.8 상행)
        // 🔑 **화면은 자리 하나만 지목한다**(`slot`). **모듈을 지목하지 않는다** —
        //   "어느 모듈이 그 조작을 맡는가"가 화면에도 생기면 서버 라우팅과 규칙이 두 곳이 되고,
        //   갈리면 **엉뚱한 모듈에 명령이 간다.** 자리 → 모듈 라우팅은 여기 있다.
        if (type == "open_gate" || type == "close_gate") {
            Zone* z = zone_find(slot);
            if (!z) { send_err(fd, rid, "module_absent", "그런 자리가 없습니다"); return; }
            if (z->kind != "entrance" && z->kind != "exit") {
                // ⚠ **조용히 무시하지 않는다.** 화면이 안 보낼 조작이지만 **보내면 이유를 답한다**
                send_err(fd, rid, "module_absent", "이 자리에는 차단봉이 없습니다");
                return;
            }
            const std::string blk = zone_block_reason(*z);
            if (!blk.empty()) { send_err(fd, rid, blk.c_str(), "지금은 조작할 수 없습니다"); return; }

            // 🔑 **자리 → 모듈 라우팅은 `gate_index_of()` 한 곳에만 있다.** 화면은 자리만 지목한다.
            const int gidx = gate_index_of(*z);
            if (gidx < 0) {
                // 🔴 **자리와 노드는 멀쩡한데 *명령 가능한 모듈*이 없다.**
                //   ⚠ **조용히 성공으로 답하지 않는다** — 그러면 화면이 "열렸다"로 그리고
                //     **아무 일도 안 일어난 것을 사람이 모른다**(그게 `거짓 완료` 다).
                logf("!", "조작 " + type + " 요청 — 자리 " + slot
                          + " 에 명령 가능한 모듈이 없다(kind 가 `O*` 인 것). 거절한다");
                send_err(fd, rid, "not_supported", "이 조작을 맡을 모듈이 이 자리에 없습니다");
                return;
            }
            // 🔴 **장치가 `idx >= SLOT_N` 만 받는다**(arduino 파서). 자리 센서 인덱스를 보내면
            //   장치가 `result=3` 으로 거절한다 — **여기서 먼저 막아 전선을 낭비하지 않는다.**
            //   ⚠ 그래도 장치의 검사를 없애지 않는다. **양쪽이 각자 확인하는 것이 낫다.**
            if (gidx < 10) {
                logf("!", "조작 거절 — 모듈 인덱스 " + std::to_string(gidx)
                          + " 가 자리 구간(0~9)이다. **명령 가능 모듈은 자리 뒤에 온다**");
                send_err(fd, rid, "not_supported", "이 자리의 모듈은 조작 대상이 아닙니다");
                return;
            }
            dispatch_gate(fd, rid, slot, gidx, type == "open_gate");
            return;
        }

        // ---- §12B 시뮬레이터 한 걸음 (개정 5)
        // **무장 여부를 확인하지 않는다.** 테스트 모드와 별개라는 것이 이 기능의 요구다(§12B.3).
        if (type == "sim_step") {
            if (!device_online()) {
                send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
                return;
            }
            dispatch_sim(fd, rid);
            return;
        }

        // ---- §12A 테스트 모드 (개정 3)
        if (type == "test_arm" || type == "test_disarm" ||
            type == "test_set" || type == "test_clear") {
            if (!device_online()) {
                send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
                return;
            }
            char op = (type == "test_arm") ? 'A' : (type == "test_disarm") ? 'D'
                    : (type == "test_set") ? 'S' : 'X';
            std::string tslot = "??", tval = "-";
            if (op == 'S' || op == 'X') {
                if (slot_index(slot) < 0) {
                    send_err(fd, rid, "bad_request", "그런 자리가 없습니다");
                    return;
                }
                tslot = slot;
                if (op == 'S') {
                    std::string occ = jget(msg, "occupied");
                    if (occ != "0" && occ != "1") {
                        send_err(fd, rid, "bad_request", "occupied 는 0 또는 1 이어야 합니다");
                        return;
                    }
                    tval = occ;
                }
            }
            dispatch_test(fd, rid, op, tslot, tval);
            return;
        }

        if (type != "reserve" && type != "cancel") {
            send_err(fd, rid, "bad_request", "알 수 없는 요청입니다");
            return;
        }
        if (slot_index(slot) < 0) {
            send_err(fd, rid, "bad_request", "그런 자리가 없습니다");
            return;
        }
        // 🔴 **`B1..B5` 는 이제 자리가 아니라 센서다** (사용자 확정 (A) · 명세 §9.3)
        //
        // `slot_index("B5")` 는 여전히 5 를 돌려준다 — `slots[10]` 이 모듈 값의 원천이라 남기 때문이다.
        // ⚠ **그대로 두면 예약이 *성공* 하고 화면 어디에도 안 보인다.** 자리가 5개인 화면에
        //   `B5` 예약이 조용히 성립하면 **그 자리는 예약된 채 아무 표시가 없다.**
        //   🔑 **없는 것이 보이는 것보다, 있는 것이 안 보이는 것이 나쁘다.**
        //
        // 사유 코드는 `not_supported` 와 **다르다**: 저건 "할 수 있는 일인데 수단이 없다"이고
        // 이건 **"애초에 그 대상이 아니다"** 다.
        {
            Zone* zt = zone_find(slot);
            if (!zt || zt->kind != "parking") {
                not_reservable_n++;
                logf("!", "예약 대상이 아닌 id — 거절 " + slot
                          + " (자리가 아니라 센서다. 명세 §9.3) 누적 "
                          + std::to_string(not_reservable_n));
                send_err(fd, rid, "not_reservable", "그 이름은 예약할 수 있는 자리가 아닙니다");
                return;
            }
        }
        // 브라우저 user_id 는 **번호판이 들어올 수 있으므로 UTF-8 을 허용한다.**
        // 전선으로 나갈 값은 wire_userid() 가 따로 좁힌다 — 두 이름공간이다.
        if (!valid_browser_user(uid)) {
            logf("!", "user_id 가 너무 길거나 제어문자 포함(" + std::to_string(uid.size()) + "B) — 거절");
            send_err(fd, rid, "bad_request", "user_id 가 너무 깁니다");
            return;
        }
        if (!device_online()) {                                   // §7.1
            send_err(fd, rid, "device_offline", "센서가 연결되어 있지 않습니다");
            return;
        }
        // 🔴 같은 자리에 이미 진행 중인 하행이 있으면 **명시적으로 거절한다**(설계 §4-B).
        // 지금까지 이 검사는 `1721`·`1750`(S 프레임 판정)에서만 쓰였고 **브라우저 경로에는
        // 없었다** — 그래서 연타가 그대로 `dispatch` 를 여러 번 만들었다. 큐가 생긴 뒤에는
        // 그것이 곧 큐에 여러 건이 쌓이는 경로다.
        // ⚠ 이제 `pend` 는 **큐에서 기다리는 건까지 포함**하므로(dispatch 가 먼저 `pend` 를
        // 만든다) 이 검사가 큐 대기분도 덮는다.
        // ⚠ **`queue_full` 과 코드를 갈라 둔다** — 화면 문구가 완전히 다르다:
        // 하나는 시스템 사정이고 하나는 "이미 처리 중"이다. 지금까지는 둘 다 `error` 라 못 갈랐다.
        if (has_pending_for(slot)) {
            q_dup++;
            logf("!", "같은 자리에 진행 중인 하행이 있다 — 거절 " + slot);
            send_err(fd, rid, "already_pending", "그 자리는 이미 처리 중입니다");
            return;
        }
        // 서버가 아는 상태로 미리 거를 수도 있지만, 최종 판정은 아두이노 ACK 다(§7.2).
        dispatch(type == "reserve" ? 'R' : 'C', fd, rid, slot, uid);
    }

    // ---------- HTTP / WS 업그레이드
    static std::string header(const std::string& req, const char* name) {
        std::string low, ln = name;
        for (size_t i = 0; i < req.size(); i++) low += (char)tolower((unsigned char)req[i]);
        for (size_t i = 0; i < ln.size(); i++) ln[i] = (char)tolower((unsigned char)ln[i]);
        size_t i = low.find("\n" + ln + ":");
        if (i == std::string::npos) return "";
        i = req.find(':', i + 1) + 1;
        size_t e = req.find('\r', i);
        if (e == std::string::npos) e = req.find('\n', i);
        std::string v = req.substr(i, e - i);
        while (!v.empty() && (v[0]==' '||v[0]=='\t')) v.erase(0,1);
        while (!v.empty() && (v[v.size()-1]==' '||v[v.size()-1]=='\r')) v.erase(v.size()-1);
        return v;
    }
    void serve_file(sock_t fd, std::string path) {
        // 🔴 **쿼리를 가장 먼저 떼어낸다. 이 순서가 이 함수의 요점이다.**
        // 전에는 `"/"` 판정이 앞에 있고 쿼리 제거가 뒤에 있어서 `GET /?demo=1` 이 404 였다:
        //   `/?demo=1` 은 `"/"` 와 다르므로 index.html 로 **안 바뀌고**, 그 뒤 `?` 앞을 자르면
        //   `fn` 이 **빈 문자열**이 되어 열기에 실패했다.
        //   (web-engineer 가 크롬으로 실측 · REQ-0132. `/index.html?x` 는 200 인데 `/?x` 만 404 였다)
        // **쿼리는 경로가 아니다.** 경로에 관한 어떤 판정보다도 앞에서 떼는 것이 맞고,
        // 그래야 `/?x` · `/index.html?x` · `/data_log.json?t=…` 가 **한 규칙**으로 처리된다.
        size_t q = path.find('?');
        if (q != std::string::npos) path = path.substr(0, q);
        if (path.empty() || path == "/") path = "/index.html";
        // 경로 탈출 차단 — 데모여도 디렉터리를 서빙하는 코드에 이건 기본이다
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
            const char* r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r), "HTTP 클라이언트");
            return;
        }
        std::string fn = path.substr(1);

        std::ifstream f(fn.c_str(), std::ios::binary);
        if (!f) {
            const char* r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r), "HTTP 클라이언트");
            return;
        }
        std::ostringstream ss; ss << f.rdbuf();
        std::string body = ss.str();
        const char* ct = "application/octet-stream";
        if (fn.size() > 5 && fn.substr(fn.size()-5) == ".html") ct = "text/html; charset=utf-8";
        else if (fn.size() > 5 && fn.substr(fn.size()-5) == ".json") ct = "application/json; charset=utf-8";
        else if (fn.size() > 3 && fn.substr(fn.size()-3) == ".js") ct = "application/javascript";
        else if (fn.size() > 4 && fn.substr(fn.size()-4) == ".css") ct = "text/css";

        std::ostringstream h;
        h << "HTTP/1.1 200 OK\r\nContent-Type: " << ct
          << "\r\nContent-Length: " << body.size()
          << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        std::string head = h.str();
        send_raw(fd, head.data(), head.size(), "HTTP 클라이언트");
        send_raw(fd, body.data(), body.size(), "HTTP 클라이언트");
    }
    // 반환 false = 이 연결을 닫아라. **여기서 직접 erase 하지 않는다** —
    // 호출부가 conns 를 순회 중이라 안에서 지우면 반복자가 무효화된다(실제로 SIGSEGV 를 냈다).
    bool on_http(sock_t fd, Conn& c) {
        size_t end = c.inbuf.find("\r\n\r\n");
        if (end == std::string::npos) return true;
        std::string req = c.inbuf.substr(0, end);
        c.inbuf.erase(0, end + 4);

        size_t sp1 = req.find(' ');
        size_t sp2 = req.find(' ', sp1 + 1);
        std::string path = (sp1 != std::string::npos && sp2 != std::string::npos)
                         ? req.substr(sp1 + 1, sp2 - sp1 - 1) : "/";

        std::string key = header(req, "Sec-WebSocket-Key");
        if (!key.empty() && path.substr(0, 3) == "/ws") {
            std::string acc = ws_accept(key);
            std::string r = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n";
            send_raw(fd, r.data(), r.size(), "WS 업그레이드");
            c.kind = Conn::WS;
            // 🔑 최대치는 **승격되는 자리**에서 갱신한다. 요약을 만드는 함수는 `const` 이고
            //   **읽는 함수가 상태를 바꾸면 안 된다**(그래서 `mutable` 을 안 썼다).
            {
                int ws_n = 0;
                for (std::map<sock_t, Conn>::const_iterator it2 = conns.begin();
                     it2 != conns.end(); ++it2)
                    if (it2->second.kind == Conn::WS) ws_n++;
                if (ws_n > ws_peak) ws_peak = ws_n;
            }
            logf("+WS", "업그레이드 완료");
            ws_send(fd, snapshot_json());          // 접속 즉시 현재 상태(옛 봉투)
            // 🔴 **접속 즉시 새 봉투도 보낸다**(REQ-0203 4b/4c · web 실기 대조가 이 구멍을 찾았다).
            //   전에는 `state` 가 `push_snapshot()` 안에서만 방송돼서 **장치가 스냅샷을 밀 때까지
            //   새 클라이언트가 못 받았고, 장치가 없으면 영영 못 받았다.**
            //   ⚠ **화면에 우회가 없다** — `get_map` 은 있는데 `get_state` 는 없다.
            // 🔑 **`map` 을 먼저, `state` 를 뒤에 보낸다.** 지형이 먼저 서면 화면이 상태를 바로 그린다 —
            //   반대 순서면 화면이 `state` 를 들고 지형을 기다려야 하고, **그 대기 규칙이 화면에 생긴다.**
            //   **순서를 보내는 쪽이 지키면 받는 쪽에 규칙이 안 생긴다.**
            ws_send(fd, map_json());
            ws_send(fd, state_json());
            return true;
        }
        serve_file(fd, path);
        return false;                              // 정적 응답은 Connection: close
    }
    // WS 프레임 파싱 — 클라이언트 프레임은 **항상 마스킹**돼 있다(§5.2)
    bool ws_pump(sock_t fd, Conn& c) {
        while (true) {
            if (c.inbuf.size() < 2) return true;
            const unsigned char* p = (const unsigned char*)c.inbuf.data();
            int opcode = p[0] & 0x0F;
            bool masked = (p[1] & 0x80) != 0;
            uint64_t len = p[1] & 0x7F;
            size_t off = 2;
            if (len == 126) {
                if (c.inbuf.size() < 4) return true;
                len = (uint64_t(p[2]) << 8) | p[3]; off = 4;
            } else if (len == 127) {
                if (c.inbuf.size() < 10) return true;
                len = 0;
                for (int i = 0; i < 8; i++) len = (len << 8) | p[2+i];
                off = 10;
            }
            if (!masked) { logf("!", "마스킹 안 된 클라이언트 프레임 — 연결 종료"); return false; }
            // 선언된 길이를 그대로 믿으면 inbuf 가 무한히 커진다. 브라우저→서버 메시지는
            // 예약/취소 JSON 뿐이라 수백 바이트면 충분하다.
            if (len > WS_MAX_FRAME) {
                logf("!", "WS 프레임 길이 " + std::to_string((unsigned long long)len)
                          + " 바이트 — 상한 초과, 연결 종료");
                return false;
            }
            if (c.inbuf.size() < off + 4 + len) return true;
            unsigned char mask[4];
            for (int i = 0; i < 4; i++) mask[i] = p[off + i];
            std::string payload;
            payload.reserve((size_t)len);
            for (uint64_t i = 0; i < len; i++)
                payload += char(p[off + 4 + i] ^ mask[i % 4]);
            c.inbuf.erase(0, (size_t)(off + 4 + len));

            if (opcode == 0x8) return false;                       // close
            if (opcode == 0x9) {                                   // ping → pong
                // ping 페이로드는 125바이트 이하만 정상이다(제어 프레임 규칙). 넘으면 잘라 답한다.
                if (payload.size() > 125) payload.resize(125);
                std::string f; f += char(0x8A); f += char((unsigned char)payload.size()); f += payload;
                send_raw(fd, f.data(), f.size(), "WS pong");
                continue;
            }
            if (opcode == 0x1) on_ws_message(fd, payload);
        }
    }

    // ================= 폰(digitcam) 수신 · 번호판 매칭 =========================
    // 수신부는 net/test_server.py 에서 들어왔다 — 바이트 버퍼에 모으고 LF 로 자른 뒤
    // **그 다음에** 디코딩한다. 한글이 recv 경계에서 갈리기 때문이고, 그 파일이
    // 그 경우를 실측으로 검증해 뒀다(digitcam 명세 §8.3, §10.6).
    std::map<std::string, std::string> plate_slot;   // 번호판 → 배정된 자리

    int find_slot_by_user(const std::string& plate) const {
        for (int i = 0; i < 10; i++)
            if (slots[i].reserved && slots[i].user_id == plate) return i;
        return -1;
    }
    // 빈자리 선택 규칙: **§1 의 전선 인덱스 순서(A1..A5,B1..B5)에서 가장 앞.**
    // 근거 — 결정적이라 같은 입력이면 항상 같은 결과가 나오고(재현 가능한 시험),
    // 명세가 이미 정한 유일한 자리 순서라 새 순서를 발명하지 않는다.
    // 입구에서 가까운 순 같은 물리 배치 기준은 **지금 아무도 갖고 있지 않다** —
    // 배치도가 생기면 이 함수 하나만 바꾸면 된다.
    int pick_free_slot() const {
        for (int i = 0; i < 10; i++)
            if (!slots[i].occupied && !slots[i].reserved) return i;
        return -1;
    }

    void on_plate(const std::string& plate, const std::string& dev) {
        logf("←폰", "번호판 " + plate + " (device=" + dev + ")");

        // (1) 같은 번호판이 또 왔다 — 이미 배정된 자리가 살아 있으면 새 입차가 아니다.
        //     fresh 는 상승 엣지지만 인식이 끊겼다 다시 잡히면 새 엣지가 뜬다.
        //     **시간 창을 두지 않고 상태로 판정한다** — 타이머는 근거 없는 숫자를 만든다.
        std::map<std::string, std::string>::iterator it = plate_slot.find(plate);
        if (it != plate_slot.end()) {
            int i = slot_index(it->second);
            if (i >= 0 && slots[i].user_id == plate && (slots[i].reserved || slots[i].occupied)) {
                logf("=", "중복 수신 — " + plate + " 는 이미 " + it->second
                          + " 에 배정돼 있다. 새로 배정하지 않는다");
                return;
            }
            plate_slot.erase(it);   // 그 사이 은퇴/취소됐다 → 다시 배정 가능
        }

        // (2) 이미 주차된 차의 번호판이 게이트에 나타났다.
        //     오인식일 수도, 실제로 나갔다 다시 온 것일 수도 있다 — 서버는 구분할 수 없다.
        //     **새 자리를 배정하지 않는다.** 배정하면 같은 차가 두 칸을 먹는다.
        //     사람이 볼 수 있게 로그로 올린다.
        for (int i = 0; i < 10; i++) {
            if (slots[i].occupied && slots[i].user_id == plate) {
                logf("!", "이미 주차된 번호판이 게이트에서 인식됨 — " + plate + " (" + SLOT_ID[i]
                          + "). 오인식이거나 재진입이다. 새 배정 없음");
                return;
            }
        }

        // (3) 예약이 있으면 그 자리로 확정한다. 키는 번호판이다.
        int i = find_slot_by_user(plate);
        if (i >= 0) {
            plate_slot[plate] = SLOT_ID[i];
            logf("✓", "예약 확인 — " + plate + " → " + SLOT_ID[i] + " (안내 대상 자리)");
            push_snapshot();
            return;
        }

        // (4) 예약이 없으면 빈자리를 배정한다(사용자 요구: "안 하면 빈자리").
        int f = pick_free_slot();
        if (f < 0) {
            // 조용히 지나가지 않는다. 브라우저로 올리려면 새 WS 메시지가 필요한데
            // 그건 명세 변경이라 하지 않았다(REQ-0037 은 명세 무변경). 루트에 보고했다.
            logf("!", "빈자리 없음 — " + plate + " 배정 실패. 차단기를 열 자리가 없다");
            return;
        }
        if (!device_online()) {
            logf("!", "아두이노 미연결 — " + plate + " 배정 보류(예약을 내릴 수 없다)");
            return;
        }
        plate_slot[plate] = SLOT_ID[f];
        logf("✓", "예약 없음 → 빈자리 배정 " + plate + " → " + SLOT_ID[f]);
        dispatch('R', BAD_SOCK, "", SLOT_ID[f], plate);   // 예약의 주인은 서버다(§7.4)
    }

    void on_phone_line(const std::string& line) {
        // digitcam 명세 §5: plain 모드는 번호판 문자열 한 줄, json 모드는 오브젝트 하나.
        std::string plate, dev;
        if (!line.empty() && line[0] == '{') {
            plate = jget(line, "value");
            dev = jget(line, "device");
        } else {
            plate = line;
        }
        if (plate.empty()) {
            // §5.2 — 송신 측은 빈 값을 보내지 않지만 수신 측은 견딘다.
            logf("!", "폰: 빈 값 — 무시");
            return;
        }
        if (plate.size() > MAX_PLATE_BYTES) {
            logf("!", "폰: 번호판이 너무 길다(" + std::to_string(plate.size()) + "B) — 무시");
            return;
        }
        // **신뢰도 임계값을 두지 않는다.** 폰이 이미 min_confidence 로 걸렀고,
        // conf 는 문자별 최소값이라 문자 수가 늘수록 낮아진다(digitcam 명세 §4.4 경고).
        // 서버가 절대값으로 자르면 **긴 번호판만 유독 거절된다.**
        on_plate(plate, dev.empty() ? std::string("?") : dev);
    }

    // 기동 시 data_log.json 이 없으면 **빈 배열로 만들어 둔다.**
    //
    // 왜: 서버는 §9.4 대로 상태가 바뀔 때만 파일을 쓴다. 아두이노가 안 붙으면 한 번도 안 쓴다.
    // 그런데 화면은 WS 가 붙기 전부터 폴백 폴링을 돌리므로 **아무 문제 없는 첫 실행에서
    // 404 를 받아 빨간 오류를 띄운다.** 이런 것이 쌓이면 진짜 오류를 무시하게 된다.
    //
    // `[]` 는 §9.1 형태에서 "아직 기록이 없다"를 정직하게 표현한다 —
    // 없는 파일보다 낫고, 지어낸 값을 넣는 것보다도 낫다.
    //
    // **이미 있으면 절대 덮지 않는다.** 덮으면 재시작할 때마다 직전 2건이 날아간다.
    void ensure_log_exists() {
        if (no_disk) return;                 // 시험용 인스턴스는 파일을 만들지도 않는다
        std::ifstream f("data_log.json", std::ios::binary);
        if (f.good()) {
            logf("=", "data_log.json 이 이미 있다 — 그대로 둔다(직전 기록 보존)");
            return;
        }
        atomic_write_log("[]\n");     // 쓰기 경로는 원자적 교체 하나뿐이다
        logf("=", "data_log.json 이 없어 빈 배열로 만들었다");
    }

    // ---------- 소켓 준비
    sock_t listen_on(int port) {
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == BAD_SOCK) return BAD_SOCK;
        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons((u_short)port);
        if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
            std::cerr << "바인드 실패 포트 " << port << " (err " << sockerr() << ")\n";
            closesock(s); return BAD_SOCK;
        }
        if (listen(s, 8) != 0) { closesock(s); return BAD_SOCK; }
        return s;
    }

    int run() {
        lsn_ard   = listen_on(PORT_ARDUINO + g_port_offset);
        lsn_http  = listen_on(PORT_HTTP    + g_port_offset);
        lsn_phone = listen_on(PORT_PHONE   + g_port_offset);
        // 🔴 포트를 못 잡았으면 **그 사실을 로그에 남기고** 죽는다.
        // 08-16 에 `프레임 0` 짜리 짧은 인스턴스가 여럿 있었는데, 관측자가 그것이
        // "장치가 안 붙은 것"인지 "포트를 못 잡은 것"인지 **가를 수 없어서** 한참 헤맸다.
        // 0 이 "나쁨"인지 "해당 없음"인지 가르는 것 — 그게 관측의 절반이다.
        if (lsn_ard == BAD_SOCK || lsn_http == BAD_SOCK || lsn_phone == BAD_SOCK) {
            std::cout << "=== INSTANCE"
                      << " logfmt=" << LOG_FORMAT_VERSION
                      << " pid=" << cur_pid()
                      << " start=" << iso8601(epoch_ms())
                      << " bin=" << exe_path()
                      << " build=" << BUILD_ID
                      << " ports=" << (PORT_ARDUINO + g_port_offset)
                      << ","      << (PORT_HTTP    + g_port_offset)
                      << ","      << (PORT_PHONE   + g_port_offset)
                      << " offset=" << g_port_offset
                      << " cwd=" << cur_cwd()
                      << " log=" << (g_log_path.empty() ? std::string("(화면만)") : g_log_path)
                      << " ===" << std::endl;
            std::cout << "=== INSTANCE-END"
                      << " logfmt=" << LOG_FORMAT_VERSION
                      << " pid=" << cur_pid()
                      << " stop=" << iso8601(epoch_ms())
                      << " reason=port_bind_fail"
                      << " frames=0 sessions=0"
                      << " ===" << std::endl;
            return 1;
        }

        // ---------- 인스턴스 경계 줄 (REQ-0111 로그 계약 §2.2) — 배너보다 **먼저** 나간다.
        // 기계가 읽는 줄이다. 한 파일에 인스턴스가 여러 개 쌓여도 이 줄로 자르면 섞이지 않는다.
        // 실제 리슨에 성공한 뒤에 찍으므로 ports= 는 **추측이 아니라 사실**이다.
        std::cout << "=== INSTANCE"
                  << " logfmt=" << LOG_FORMAT_VERSION
                  << " pid=" << cur_pid()
                  << " start=" << iso8601(epoch_ms())
                  << " bin=" << exe_path()
                  << " build=" << BUILD_ID
                  << " ports=" << (PORT_ARDUINO + g_port_offset)
                  << ","      << (PORT_HTTP    + g_port_offset)
                  << ","      << (PORT_PHONE   + g_port_offset)
                  << " offset=" << g_port_offset
                  << " cwd=" << cur_cwd()
                  << " log=" << (g_log_path.empty() ? std::string("(화면만)") : g_log_path)
                  << " ===" << std::endl;

        if (g_port_offset)
            std::cout << "*** 시험 인스턴스 — 포트 +" << g_port_offset
                      << " 이동됨. 운영이 아니다(REQ-0072 이음매) ***\n";
        std::cout << "주차 관제 서버 — 아두이노 TCP " << (PORT_ARDUINO + g_port_offset)
                  << " · HTTP/WS " << (PORT_HTTP + g_port_offset)
                  << " · 폰(digitcam) " << (PORT_PHONE + g_port_offset) << "\n"
                  << "명세: docs/net/parking-protocol.md\n"
                  << "-----------------------------------------------------------\n";
        std::cout.flush();
        ensure_log_exists();

        // 소크 관측(REQ-0065) — 기동 시각과 "아직 링크 없음" 상태를 장부에 연다
        soak_start_ms = now_ms();
        last_report_ms = soak_start_ms;
        link_down_since = soak_start_ms;
        // 🔑 **서버가 자기 계약값을 기동 시 찍는다** (web 지적 2026-08-18)
        // web: *"서버 값 변화를 잡을 수 있는 검사가 팀에 없다"* — 하니스가 값을 주입하고
        // 같은 값으로 단언하므로 **서버가 바뀌어도 빨간불이 안 된다.** 그렇다고 특정 값을 박으면
        // **고친 쪽이 벌을 받는 검사**가 된다.
        // → **검사로 막지 말고 관측 가능하게 만든다.** 그러면 "지금 서버가 보내는 값"이
        //   남의 원장 사본이 아니라 **로그에 남는 사실**이 된다.
        {
            char cb[288];
            snprintf(cb, sizeof(cb),
                     "계약값 — ACK_TIMEOUT %dms(=2x슬롯) · ACK_MAX_TRIES %d · "
                     "ack_budget_ms %lld · 큐대기마감 %dms · cap %dB/%d건 · "
                     "사용자큐상한 %d건 · 인수유예 %dms(=5x슬롯 · **IP 다를 때만**) · 노드잠금 %s · W_srv %lldms",
                     ACK_TIMEOUT_MS, ACK_MAX_TRIES, ack_budget_ms(),
                     DOWNQ_WAIT_CAP_MS, DOWN_BATCH_CAP_B, DOWN_BATCH_MAX_N,
                     DOWN_BATCH_MAX_N * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS),
                     TAKEOVER_GRACE_MS,
                     g_park_dev_pin.empty() ? "(none, first-S-wins)" : g_park_dev_pin.c_str(),
                     w_srv());
            logf("=", cb);
            // 🔴 A[1] — **폭 계약을 기동 로그에 값으로 낸다.** 남의 원장 사본이 아니라
            //   "지금 이 서버가 쓰는 값"이 로그에 남아야 arduino 가 `ACK_worst` 를 다시 계산할 수 있다.
            {
                char rb[224];
                snprintf(rb, sizeof(rb),
                         "rid 계약 — 공간 %u([0,%u] · 최대 %d자리) · 격리 %lldms(=ACK_TIMEOUT×ACK_MAX_TRIES) "
                         "· 장치 멱등창 %d (arduino §25.3)",
                         (unsigned)RID_SPACE, (unsigned)(RID_SPACE - 1),
                         (int)std::to_string((unsigned)(RID_SPACE - 1)).size(),
                         RID_QUARANTINE_MS, DEV_RID_CACHE_N);
                logf("=", rb);
            }
            // 🔴 **여기서 부르지 않으면 커서가 안 이어진다.** 생성자에 두지 않은 것은 일부러다 —
            //   자가검증이 만드는 `Server` 들이 실기 커서 파일을 덮어쓰지 못하게 하려는 것이다.
            //   ⚠ 이 한 줄이 빠지면 **아무 경고 없이** 매 기동이 1에서 시작한다(=옛 거동).
            //   그래서 기동 로그에 값을 찍는다 — 빠진 것이 로그의 *부재*로 보이게.
            rid_cursor_load();
            // 🔴 **조건을 적었으면 그것을 보는 감시를 같은 자리에 만든다.**
            //   재사용 주기가 장치 멱등창(16)에 가까워지면 명령이 **조용히 삼켜진다.**
            //   ⚠ 여유 4배는 임의로 고른 문턱이다 — **관측용이지 증명이 아니다.**
            if ((int)RID_SPACE <= DEV_RID_CACHE_N * 4) {
                logf("!", "🔴 rid 공간이 장치 멱등창의 4배 이하다 — 재사용이 캐시 안에서 일어날 수 있다."
                          " 명령이 조용히 삼켜진다(arduino §25.3). 폭을 줄인 사람이 이 줄을 읽어야 한다");
            }
            init_srv_id();
            // 🔴 **여기서 부르지 않으면 자리가 비어 있고 `map` 이 빈 배열로 나간다.**
            //   첫 배포에서 실제로 그랬다 — **자가검증에서만 부르고 있었다.**
            //   ⚠ 자가검증이 스스로 지형을 만들어 쓰기 때문에 **시험은 전부 통과한다.**
            //   **"시험 경로 ≠ 실기 경로"의 가장 조용한 형태다.**
            build_default_zones();
            logf("=", "서버 인스턴스 id — " + srv_id
                      + " (🔑 `epoch` 는 **이 id 안에서만** 단조다. id 가 바뀌면 판을 비교하지 마라)");
        }
        logf("⏱", "소크 관측 시작 — " + std::to_string(SOAK_REPORT_MS / 1000) + "초마다 요약, 종료(Ctrl-C) 시 한 줄 총평");

        while (!g_stop) {
            fd_set rd; FD_ZERO(&rd);
            sock_t mx = 0;
            FD_SET(lsn_ard, &rd);  if (lsn_ard  > mx) mx = lsn_ard;
            FD_SET(lsn_http, &rd); if (lsn_http > mx) mx = lsn_http;
            FD_SET(lsn_phone, &rd); if (lsn_phone > mx) mx = lsn_phone;
            for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            if (ard != BAD_SOCK) { FD_SET(ard, &rd); if (ard > mx) mx = ard; }
            // ⚠ REQ-0083 — **여기에 하나라도 빠뜨리면 그 노드는 조용히 귀머거리가 된다.**
            // 오류도 로그도 없이 영영 readable 이 안 될 뿐이라, 방화벽 신원 사고와 같은 형태다.
            // 아래 둘(보조 노드·id 미상 소켓)은 select 대상에 **반드시** 들어가야 한다.
            for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
                if (it->second.fd == BAD_SOCK) continue;
                FD_SET(it->second.fd, &rd);
                if (it->second.fd > mx) mx = it->second.fd;
            }
            for (size_t k = 0; k < unknown.size(); k++) {
                if (unknown[k].fd == BAD_SOCK) continue;
                FD_SET(unknown[k].fd, &rd);
                if (unknown[k].fd > mx) mx = unknown[k].fd;
            }
            for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            // 타이머(재전송·offline)를 돌려야 하므로 NULL 을 주면 안 된다.
            // 소켓이 조용해도 이 주기로 깨어나 tick() 을 돈다.
            timeval tv; tv.tv_sec = 0; tv.tv_usec = SELECT_TICK_MS * 1000;
            int n = select((int)mx + 1, &rd, NULL, NULL, &tv);

            if (n > 0) {
                if (FD_ISSET(lsn_ard, &rd)) {
                    sock_t c = accept(lsn_ard, NULL, NULL);
                    if (c != BAD_SOCK) {
                        // ⚠ REQ-0083 — **여기서 옛 소켓을 끊지 않는다. 그게 조원 배포의 차단 요인이었다.**
                        // 이 시점엔 device_id 를 모른다(첫 프레임을 받아야 안다). 그런데 옛 구조는
                        // 모르는 채로 기존 연결을 끊었고, 그래서 **노드 2대가 서로 밀어냈다.**
                        // 이제는 대기열에 넣고 **첫 유효 프레임에서 승격**한다(promote_unknown).
                        set_send_timeout(c);
                        std::string ka = set_keepalive(c);
                        // 상한 초과는 **거절**한다. 살아 있는 노드를 쫓아내지 않는 이유는
                        // MAX_ARD_NODES 주석에 있다(확실한 것을 버리고 불확실한 것을 얻지 않는다).
                        if (unknown.size() >= MAX_UNKNOWN_SOCKS) {
                            admit_rejects++;
                            logf("!", "id 미상 소켓이 상한(" + std::to_string(MAX_UNKNOWN_SOCKS)
                                      + ")에 찼다 — 새 연결 거절. 누적 " + std::to_string(admit_rejects) + "회");
                            closesock(c);
                        } else {
                            UnknownSock u; u.fd = c; u.since_ms = now_ms();
                            u.peer = peer_str(c);
                            unknown.push_back(u);
                            logf("+?", "연결 수락 — 상대 " + u.peer
                                       + " · device_id 대기 중(" + std::to_string(UNKNOWN_TIMEOUT_MS / 1000)
                                       + "초 안에 유효 프레임 없으면 끊는다) · " + ka);
                        }
                    }
                }
                if (FD_ISSET(lsn_http, &rd)) {
                    sock_t c = accept(lsn_http, NULL, NULL);
                    if (c != BAD_SOCK) { set_send_timeout(c); conns[c] = Conn(); }
                }
                if (FD_ISSET(lsn_phone, &rd)) {
                    sock_t c = accept(lsn_phone, NULL, NULL);
                    if (c != BAD_SOCK) {
                        set_send_timeout(c);
                        phones[c] = std::string(); logf("+폰", "digitcam 접속");
                    }
                }
                // 폰 연결들 — 순회 중 map 을 건드리지 않도록 fd 를 먼저 모은다
                {
                    std::vector<sock_t> pr, pdrop;
                    for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it)
                        if (FD_ISSET(it->first, &rd)) pr.push_back(it->first);
                    for (size_t k = 0; k < pr.size(); k++) {
                        sock_t fd = pr[k];
                        char b[2048];
                        int r = (int)recv(fd, b, sizeof(b), 0);
                        if (r <= 0) { pdrop.push_back(fd); continue; }
                        std::string& pb = phones[fd];
                        pb.append(b, r);
                        size_t i;
                        while ((i = pb.find('\n')) != std::string::npos) {
                            std::string line = pb.substr(0, i);
                            pb.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (!line.empty()) on_phone_line(line);
                        }
                        if (pb.size() > MAX_PHONE_LINE) {
                            logf("!", "폰: LF 없이 상한 초과 — 버퍼 비움");
                            pb.clear();
                        }
                    }
                    for (size_t k = 0; k < pdrop.size(); k++) {
                        // LF 없이 남은 조각은 완성된 줄이 아니다 — 값으로 쓰지도, 조용히 버리지도 않는다
                        if (phones.count(pdrop[k]) && !phones[pdrop[k]].empty())
                            logf("!", "폰 해제 — 미종단 잔여 "
                                      + std::to_string(phones[pdrop[k]].size()) + "B 버림");
                        else logf("-폰", "digitcam 연결 종료");
                        closesock(pdrop[k]); phones.erase(pdrop[k]);
                    }
                }
                if (ard != BAD_SOCK && FD_ISSET(ard, &rd)) {
                    char b[2048];
                    int r = (int)recv(ard, b, sizeof(b), 0);
                    if (r <= 0) {
                        // ⚠ 이유를 **원인별로** 남긴다(REQ-0072). keepalive 가 죽인 소켓은
                        // recv 가 ETIMEDOUT 으로 돌아오는데, 그걸 "수신 오류" 한 문자열에
                        // 섞으면 keepalive 가 한 번이라도 일했는지 증명할 수 없다 —
                        // 켠 것과 안 켠 것이 로그에서 똑같아 보이는 계측은 계측이 아니다.
                        std::string why;
                        if (r == 0) why = "상대가 닫음";
                        else {
                            int e = sockerr();
                            if (err_is_timeout(e)) {
                                keepalive_reaps++;
                                why = "keepalive 시간초과(errno=" + std::to_string(e) + ")";
                            } else {
                                why = "수신 오류(errno=" + std::to_string(e) + ")";
                            }
                        }
                        end_ard_session(why);
                        closesock(ard); ard = BAD_SOCK; ard_buf.clear();
                        emit_dev(DEV_DISCONNECT, park_dev, why);   // 단계 C
                    } else {
                        ard_buf.append(b, r);
                        size_t i;
                        while ((i = ard_buf.find('\n')) != std::string::npos) {
                            std::string line = ard_buf.substr(0, i);
                            ard_buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            // 🔑 **버릴 때도 길이를 남긴다.** 예전에는 "64B 초과" 라고만 적고 버려서
            // **몇 바이트였는지**를 못 봤다 — `AT+CIPSEND` 잘림 시험에서 그것이 측정값이다.
            if (line.size() + 1 > (size_t)MAX_LINE) {
                drop_overlong++;
                char b[96];
                snprintf(b, sizeof(b), "상한(%dB) 초과 줄 — 버림 · **rx=%zuB**",
                         MAX_LINE, line.size());
                logf("!", b);
                continue;
            }
                            if (!line.empty()) on_ard_line(park, line);
                        }
                        if (ard_buf.size() > (size_t)MAX_LINE) {
                            drop_noise++;
                            logf("!", "LF 없이 64B 초과 — 버퍼 비움");
                            ard_buf.clear();
                        }
                    }
                }

                // ---------- id 미상 소켓 (REQ-0083) — 첫 유효 프레임에서 승격
                {
                    std::vector<size_t> gone;
                    for (size_t k = 0; k < unknown.size(); k++) {
                        sock_t fd = unknown[k].fd;
                        if (fd == BAD_SOCK || !FD_ISSET(fd, &rd)) continue;
                        char b[1024];
                        int r = (int)recv(fd, b, sizeof(b), 0);
                        if (r <= 0) {
                            logf("-?", "id 미상 소켓이 승격 전에 끊겼다");
                            closesock(fd); unknown[k].fd = BAD_SOCK; gone.push_back(k);
                            continue;
                        }
                        unknown[k].buf.append(b, r);
                        std::string dev, rest;
                        bool found = false;
                        size_t i;
                        while (!found && (i = unknown[k].buf.find('\n')) != std::string::npos) {
                            std::string line = unknown[k].buf.substr(0, i);
                            unknown[k].buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (peek_devid(line, dev)) {
                                found = true;
                                // **승격해도 이 프레임을 잃지 않는다.** 되돌려 놓고 넘긴다 —
                                // 첫 프레임은 §7.4 기준선을 세우는 줄이라 버리면 안 된다.
                                rest = line + "\n" + unknown[k].buf;
                            } else if (!line.empty()) {
                                // (REQ-0118 (F)) 여기서 버려지는 줄을 **이제 센다.**
                                // 전에는 아무 카운터도 안 올리고 사라져서, 이 소켓이 침묵했는지
                                // 말했는데 못 알아들었는지 구별할 방법이 없었다.
                                drop_prepromo++;
                            }
                        }
                        if (found) {
                            unknown[k].fd = BAD_SOCK; gone.push_back(k);
                            if (promote_unknown(fd, dev)) {
                                if (ard == fd)                      { ard_buf = rest; drain_ard_buf(); }
                                else if (aux.count(dev) && aux[dev].fd == fd) aux[dev].buf = rest;
                            }
                        } else if (unknown[k].buf.size() > (size_t)MAX_LINE * 4) {
                            // 잡음만 흘리는 소켓이 메모리를 먹지 않게 한다(§6.2)
                            // (REQ-0118 (F)) 통째로 버리는 것도 **말없이 하지 않는다.**
                            drop_prepromo_buf++;
                            logf("!", "승격 전 소켓 버퍼 상한 초과 — 통째로 비움("
                                      + std::to_string(unknown[k].buf.size()) + "B)");
                            unknown[k].buf.clear();
                        }
                    }
                    for (size_t j = gone.size(); j > 0; j--) unknown.erase(unknown.begin() + gone[j-1]);
                }

                // ---------- 보조 노드 (REQ-0083) — **상행 전용**
                {
                    std::vector<std::string> dead;
                    for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
                        AuxNode& a = it->second;
                        if (a.fd == BAD_SOCK || !FD_ISSET(a.fd, &rd)) continue;
                        char b[1024];
                        int r = (int)recv(a.fd, b, sizeof(b), 0);
                        if (r <= 0) { dead.push_back(it->first); continue; }
                        a.buf.append(b, r);
                        size_t i;
                        while ((i = a.buf.find('\n')) != std::string::npos) {
                            std::string line = a.buf.substr(0, i);
                            a.buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (line.empty()) continue;
                            std::vector<std::string> f;
                            if (!verify_line(line, f)) { a.drops++; continue; }
                            a.frames++; a.last_ms = now_ms(); a.last_epoch_ms = epoch_ms();
                            // ── 🔴 ②-b — **보조 노드의 등록(`D`)을 파서에 넣는다** (REQ-0262/0263)
                            //
                            // 전에는 이 줄들이 **계수만 되고 버려졌다.** 그래서 보조 노드의 모듈은
                            // `map`/`state` 에 값이 없었고 `state_json()` 이 `known:false` 로 냈다.
                            //
                            // 🔴 **`D` 만 넣는다. `S` 는 아직 아니다.** 이유가 구조적이다:
                            //   `S` 분기는 `n.mod_bits`(노드별 ✅) 말고도 **`slots[]`·`base_occ`·
                            //   `resync_reservations`·하행 flush** 를 건드리는데 **그건 전부 서버 공유다.**
                            //   보조 노드의 `S` 를 넣으면 **그 노드의 점유가 주차 자리를 덮는다** —
                            //   ②-c 로 비트열은 갈랐지만 **자리는 아직 안 갈렸다.**
                            //   🔑 ①→② 에서 막았던 것과 같은 형태다: **값 경로가 없는데 값을 만들어 낸다.**
                            //   → `S` 는 효과를 이음매(`DEV_SENSORS`)로 뺀 뒤에(설계 §8.13.2) 넣는다.
                            //
                            // ⚠ `D` 는 안전하다 — 그 분기는 `n.mods`·`n.reg_*` 와 `bind_modules(n)` 만
                            //   건드리고 **전부 그 노드의 것**이다(②-a 로 인자화됐다).
                            if (f[0] == "D") { on_ard_line(a, line); continue; }
                            // 🔴 보조 노드가 `S` 를 보낸다 = **주차 노드가 둘이라는 뜻**이다.
                            // first-S-wins 가정이 깨지는 순간이므로 조용히 넘기지 않는다.
                            // (조용히 둘 중 하나를 고르면 "예약이 가끔 엉뚱한 데로 간다"가 되고,
                            //  그건 며칠 뒤에 원인을 못 찾는 종류의 사고다.)
                            if (f[0] == "S") {
                                aux_conflicts++;
                                if (aux_conflicts <= 3 || aux_conflicts % 200 == 0)
                                    logf("🔴", "역할 충돌 — 보조 노드 " + it->first
                                               + " 도 S 를 보낸다(주차 노드는 " + park_dev
                                               + "). **이 노드에는 하행을 주지 않는다.** 누적 "
                                               + std::to_string(aux_conflicts)
                                               + " · 전선에 역할 필드가 없어 생기는 한계다(명세 §2.4-D 참조)");
                            }
                        }
                        if (a.buf.size() > (size_t)MAX_LINE * 2) { a.drops++; a.buf.clear(); }
                    }
                    for (size_t k = 0; k < dead.size(); k++) {
                        logf("-AUX", "보조 노드 " + dead[k] + " 연결 종료 — 프레임 "
                                     + std::to_string(aux[dead[k]].frames)
                                     + " · 버린줄 " + std::to_string(aux[dead[k]].drops));
                        if (aux[dead[k]].fd != BAD_SOCK) closesock(aux[dead[k]].fd);
                        aux.erase(dead[k]);
                    }
                }
                // 준비된 fd 를 먼저 모은 뒤 처리한다. 순회 중에 conns 를 건드리면
                // 반복자가 무효화된다 — 그대로 두면 SIGSEGV 다(실제로 겪었다).
                std::vector<sock_t> ready, drop;
                for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it)
                    if (FD_ISSET(it->first, &rd)) ready.push_back(it->first);

                for (size_t k = 0; k < ready.size(); k++) {
                    sock_t fd = ready[k];
                    if (!conns.count(fd)) continue;
                    Conn& c = conns[fd];               // map 의 참조는 erase 전까지 안정적이다
                    char b[4096];
                    int r = (int)recv(fd, b, sizeof(b), 0);
                    if (r <= 0) { drop.push_back(fd); continue; }
                    c.inbuf.append(b, r);
                    if (c.kind == Conn::HTTP) {
                        if (!on_http(fd, c)) { drop.push_back(fd); continue; }
                    }
                    if (c.kind == Conn::WS && !ws_pump(fd, c)) drop.push_back(fd);
                }
                for (size_t k = 0; k < drop.size(); k++) {
                    if (conns.count(drop[k])) { closesock(drop[k]); conns.erase(drop[k]); }
                }
            }

            reap_dead();                              // 전송 실패로 표시된 연결을 여기서 정리
            reap_nodes();                             // id 미상 마감 + 보조 노드 회수 (REQ-0083)
            tick();                                   // 소켓이 조용해도 매 주기 돈다

            // ---------- 좀비 아두이노 소켓 회수 — 유휴 마감 (REQ-0072)
            // 장치가 FIN 없이 죽으면 이 fd 는 영원히 안 돌아온다. keepalive 는 상대 커널이
            // 살아 ACK 를 보내면 **원리적으로 못 잡는다**(ESP 가 행에 걸린 바로 그 경우).
            // 그래서 앱이 직접 마감한다. 기준 시계는 ard_idle_base_ms() — 주석에 이유가 있다.
            if (ard != BAD_SOCK && sess_start_ms) {
                long long idle = now_ms() - ard_idle_base_ms();
                if (idle >= ARD_IDLE_CLOSE_MS) {
                    zombie_reaps++;
                    logf("✂", "아두이노 소켓 회수 — " + secs(idle) + " 무프레임(유휴 마감 "
                              + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초) · 누적 "
                              + std::to_string(zombie_reaps) + "회");
                    // 닫는 순서는 recv 실패 경로(위)와 **똑같이** 간다. 새 정리 경로를 만들면
                    // 한쪽만 고쳐지는 장부가 생긴다.
                    end_ard_session("유휴 마감 " + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초");
                    closesock(ard); ard = BAD_SOCK; ard_buf.clear();
                    emit_dev(DEV_DISCONNECT, park_dev,             // 단계 C
                             "유휴 마감 " + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초");
                }
            }
            // ---------- 온·오프라인 **엣지** 판정 (§3.4)
            // ⚠ 옛 코드는 `was_online = device_online()` 을 루프 맨 위에서 재고 맨 아래에서
            // 다시 재 비교했다. 두 시점의 간격이 마이크로초라 **3.5초 경계는 언제나 두 반복
            // '사이'에서 넘어간다** — 즉 엣지가 잡히는 반복이 존재하지 않았다.
            // 그래서 "링크는 열려 있는데 프레임만 끊긴" 경우(ESP 가 조용히 멈춘 바로 그 경우)
            // 오프라인 판정이 **한 번도 뜨지 않았다.** 소켓이 닫힐 때만 떴고, 그때는
            // device_online() 이 ard==BAD_SOCK 때문에 뒤집힌 것이라 판정이 아니라 부작용이었다.
            // 피해는 로그만이 아니다 — 이 자리의 push_snapshot() 이 브라우저에 "센서 끊김"을
            // 알리는 유일한 경로인데, 장치가 조용하면 다른 이벤트도 없어서
            // **화면이 마지막 상태를 계속 진짜처럼 보여 준다.**
            // 상태를 멤버로 들고 비교하는 평범한 엣지 검출로 바꾼다. 복귀도 대칭으로 찍는다.
            bool now_online = device_online();
            if (now_online != ard_online) {
                ard_online = now_online;
                if (now_online) {
                    // ---------- 복구시간 확정 (REQ-0072)
                    // 복구시간 = **오프라인이 시작된 순간 → 다음 프레임이 실제로 도착한 순간.**
                    // 끝점을 now_ms() 가 아니라 `ard_last_ms`(그 프레임의 수신 시각)로 잡는다 —
                    // 엣지는 select 주기(200ms) 뒤에 잡히므로 now 로 재면 매번 그만큼 부풀려진다.
                    std::string extra;
                    if (offline_since_ms) {
                        long long r = ard_last_ms - offline_since_ms;
                        if (r < 0) r = 0;
                        // 같은 연결에서 되살아났나, 재연결해서 되살아났나 — **이 구분이
                        // 장치 쪽 복구 사다리(REQ-0071)가 듣는지를 판정한다.** 합쳐 세면
                        // "링크가 잠깐 조용했다" 와 "소켓을 새로 세워야 했다" 가 같아 보인다.
                        bool same = (ard_sessions == offline_at_session);
                        if (same) recov_same_conn++; else recov_reconn++;
                        if (r > recov_worst_ms) recov_worst_ms = r;
                        if (recov_ms.size() < RECOV_SAMPLE_MAX) recov_ms.push_back(r);
                        else recov_dropped++;             // 조용히 버리지 않는다 — 요약에 적힌다
                        extra = " — 복구 " + secs(r) + (same ? "(같은 연결)" : "(재연결)");
                        offline_since_ms = 0;
                    }
                    logf("=", "아두이노 온라인 복귀" + extra);
                } else {
                    offline_episodes++;                   // 소크 관측(REQ-0065)
                    // 오프라인이 **시작된** 시각을 잡는다. 엣지가 잡힌 시각이 아니다.
                    //   · 3.5초 무프레임으로 뒤집힌 경우 → 마지막 프레임 + OFFLINE_MS 가 그 순간
                    //   · 소켓이 닫혀 뒤집힌 경우       → 지금이 그 순간(미래 값을 쓰면 안 되므로 min)
                    long long t = now_ms();
                    long long off_at = ard_last_ms ? (ard_last_ms + OFFLINE_MS) : t;
                    offline_since_ms = (off_at < t) ? off_at : t;
                    offline_at_session = ard_sessions;
                    logf("!", "아두이노 오프라인 판정(" + std::to_string(OFFLINE_MS)
                              + "ms 무프레임) — 누적 " + std::to_string(offline_episodes) + "회");
                }
                // ⚠ **파일 쓰기를 여기서 반드시 해야 한다**(§9.4 개정 9). 다른 두 호출 지점은
                // 둘 다 on_ard_line 안이라 **장치가 조용하면 아예 실행되지 않는다.**
                // 이 줄이 없으면 `device.online` 은 키에 넣어도 영영 false 로 기록되지 못한다 —
                // 필드가 **가장 필요한 순간에** 거짓말을 한다.
                // 단계 C: 직접 호출 → 이벤트. **소비자가 같은 틱에 같은 일을 한다**(아래 drain).
                emit_dev(now_online ? DEV_ONLINE : DEV_OFFLINE, park_dev,
                         now_online ? "프레임 복귀" : "3.5초 무프레임");
            }

            // ---------- 이음매 소비 (REQ-0096 단계 C)
            // **같은 틱 안에서** 소비한다 — 디바이스가 이번 반복에 낸 이벤트는 이번 반복에
            // 도메인이 처리한다. 다음 틱으로 미루면 최대 200ms 가 밀리고, 그건 옮기기 전과
            // 다른 동작이다. 단계 C 는 구조만 바꾸고 동작은 안 바꾼다.
            drain_dev_events();

            // 주기 보고 — **조용한 로그와 죽은 서버를 구별할 수 있게** 한다.
            // 이 줄이 없으면 "2시간 동안 아무 일 없었다"와 "1분 만에 멈췄다"가 같은 모양이다.
            if (now_ms() - last_report_ms >= SOAK_REPORT_MS) {
                last_report_ms = now_ms();
                logf("⏱", soak_line());
            }
        }

        // ---------- 소크 종료 요약 (REQ-0065) — 한 줄로 끝난다
        if (sess_start_ms) end_ard_session("서버 종료");
        logf("▣", "소크 종료 · " + soak_line());

        // ---------- 인스턴스 종료 경계 (REQ-0111 로그 계약 §2.3)
        // ⚠ **이 줄이 없다고 "아직 살아 있다"로 읽으면 안 된다.** SIGKILL·정전·패닉은
        // 이 줄을 남길 기회를 주지 않는다. 실제로 08-16 에 pid 36998 이 이 줄 없이 죽었다.
        // **생존 판정은 로그가 아니라 pid 로 해야 한다.** 이 줄은 "정상 종료였다"만 증명한다.
        // 누계를 이 줄에 싣는다 — **로그 뒤쪽이 잘려도 이 한 줄로 인스턴스 총계가 복원된다.**
        std::cout << "=== INSTANCE-END"
                  << " logfmt=" << LOG_FORMAT_VERSION
                  << " pid=" << cur_pid()
                  << " stop=" << iso8601(epoch_ms())
                  << " reason=normal"
                  << " frames=" << all_frames
                  << " sessions=" << ard_sessions
                  << " ===" << std::endl;
        return 0;
    }
};

// ---------------------------------------------------------------- 자가검증
// §7.4 자가검증용 — `S` 한 줄을 만든다. on_ard_line 이 받는 형태(종단자 없음)다.
// 시험용 — 임의 프레임에 올바른 체크섬을 붙인다. **`cksum` 을 시험이 다시 구현하지 않는다** —
// 다시 구현하면 둘이 갈리고, 갈린 채로 통과하면 시험이 실기를 안 재는 것이 된다.
static std::string t_line(const std::string& prefix) { return prefix + cksum(prefix); }

static std::string s_line(long seq, const char* occ, const char* res, long long up) {
    char b[96];
    snprintf(b, sizeof(b), "S,%ld,%s,%s,%lld,P1,", seq, occ, res, up);
    std::string p(b);
    return p + cksum(p);
}

static int selftest() {
    int bad = 0;
    // RFC 6455 §1.3 예제 벡터 — SHA-1 과 base64 를 한 번에 검증한다
    std::string acc = ws_accept("dGhlIHNhbXBsZSBub25jZQ==");
    std::cout << "Sec-WebSocket-Accept : " << acc << "\n"
              << "기대값               : s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\n";
    if (acc != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") { std::cout << "  ✗ SHA-1/base64 불일치\n"; bad++; }
    else std::cout << "  ✓ 일치\n";

    // 명세 §2.5 의 예제 라인 8건
    const char* L[] = {
        "S,1234,0110100011,0000100000,3600,P1,33",
        "S,0,0000000000,0000000000,0,P1,32",
        "S,65535,1111111111,1111111111,4294967,P1,31",
        "A,42,B3,0,06", "A,42,B3,2,04",
        "R,42,B3,u17,56", "R,7,A1,,15", "C,43,B3,19",
        // v1.2 (개정 3) — tmask 가 붙은 S, 그리고 T 4종
        "S,1236,0110100011,0000100000,3602,P1,-,32",
        "S,1234,0110100011,0000100000,3600,P1,0000000000,1F",
        "S,65535,1111111111,1111111111,4294967,DEVICE12,1111111111,67",
        "A,42,??,3,74", "A,50,??,0,74", "A,52,A3,4,00",
        "T,50,A,??,-,11", "T,51,D,??,-,15", "T,52,S,A3,1,6F", "T,54,X,A3,-,7E",
        // v1.4 (개정 5) — 시뮬 한 걸음
        "M,60,4B", "M,65535,7D", "A,60,A3,0,05", "A,60,??,5,72",
    };
    const int NL = (int)(sizeof(L) / sizeof(L[0]));   // 줄을 추가하고 개수를 못 고치는 사고를 막는다
    for (int i = 0; i < NL; i++) {
        std::vector<std::string> f;
        bool ok = verify_line(L[i], f);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << L[i] << "\n";
        if (!ok) bad++;
    }

    // ---------------- §7.4 (개정 8) 재부팅 판정 — 오탐을 없애면서 정탐을 잃지 않았는가
    // (A) 판정식 표 — 명세 §7.4 의 표를 그대로 옮겼다.
    std::cout << "\n[§7.4-A] 판정식 (fwd > " << UPTIME_MAX_FWD << " 이면 재부팅)\n";
    struct RbCase { long long prev, up; bool want; const char* name; };
    RbCase RB[] = {
        { 3600,    3601, false, "평시 1Hz" },
        { 3600,    4200, false, "링크 침묵 10분" },
        { 4294966,    1, false, "millis() 49.7일 랩" },
        { 4294000, 5000, false, "랩을 걸쳐 100분 침묵" },
        { -1,         5, false, "기준선 없음(첫 프레임)" },
        { 3600,       2, true,  "진짜 재부팅" },
        { 3,          1, true,  "부트 루프" },
        { 1000000,    2, true,  "11.6일 가동 뒤 재부팅" },
    };
    for (int i = 0; i < (int)(sizeof(RB) / sizeof(RB[0])); i++) {
        bool got = uptime_says_reboot(RB[i].prev, RB[i].up);
        bool ok  = (got == RB[i].want);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << RB[i].prev << " → " << RB[i].up
                  << " : " << (got ? "재부팅" : "정상")
                  << " (기대 " << (RB[i].want ? "재부팅" : "정상") << ")  " << RB[i].name << "\n";
        if (!ok) bad++;
    }

    // (B) **실제 호출 지점**까지 통과시킨다. 판정식이 맞아도 호출부가 틀리면 의미가 없으므로
    //     S 프레임을 만들어 on_ard_line 에 먹이고 resync_reservations 호출 횟수를 센다.
    std::cout << "\n[§7.4-B] on_ard_line → resync_reservations 실제 호출 횟수\n";
    const char* OCC = "0110100011";
    const char* RES = "0000100000";
    {   // (1) REQ-0062 의 재현 조건 — seq 를 65530 에서 시작시켜 랩을 넘긴다.
        //     옛 규칙(seq < ard_seq)이었다면 65535→0 프레임에서 반드시 터졌다.
        Server s; s.no_disk = true;
        long long up = 3600;
        long seqv = 65530;
        for (int k = 0; k < 8; k++) {
            s.on_ard_line(s_line(seqv, OCC, RES, up));
            seqv = (seqv + 1) & 0xFFFF;                  // uint16 순환
            up++;                                        // uptime 은 정상 전진
        }
        bool ok = (s.resync_count == 0);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "seq 65530→65535→0→1, uptime 정상 : resync="
                  << s.resync_count << " (기대 0)\n";
        if (!ok) bad++;
    }
    {   // (2) millis() 49.7일 랩도 재동기화를 부르면 안 된다
        Server s; s.no_disk = true;
        s.on_ard_line(s_line(100, OCC, RES, 4294966));   // 기준선
        s.on_ard_line(s_line(101, OCC, RES, 1));         // 랩
        bool ok = (s.resync_count == 0);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "uptime 4294966→1 : resync="
                  << s.resync_count << " (기대 0)\n";
        if (!ok) bad++;
    }
    {   // (3) 정탐 — 진짜 재부팅은 여전히 잡혀야 한다
        Server s; s.no_disk = true;
        s.on_ard_line(s_line(500, OCC, RES, 3600));      // 기준선
        s.on_ard_line(s_line(501, OCC, RES, 3601));      // 평시
        s.on_ard_line(s_line(9,   OCC, RES, 2));         // 재부팅(seq 도 작아졌다)
        bool ok = (s.resync_count == 1);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "uptime 3601→2 : resync="
                  << s.resync_count << " (기대 1)\n";
        if (!ok) bad++;
    }
    {   // (4) 정탐 — seq 가 **앞으로** 가는 재부팅. 옛 규칙은 이걸 놓쳤다.
        Server s; s.no_disk = true;
        s.on_ard_line(s_line(60000, OCC, RES, 5000));    // 기준선
        s.on_ard_line(s_line(60001, OCC, RES, 3));       // 재부팅인데 seq 는 전진
        bool ok = (s.resync_count == 1);
        std::cout << (ok ? "  ✓ " : "  ✗ ") << "uptime 5000→3 (seq 전진) : resync="
                  << s.resync_count << " (기대 1)\n";
        if (!ok) bad++;
    }

#ifndef _WIN32
    // ══ (5) 하행 슬롯 큐 — **코드 판독이 아니라 바이트를 받아 본다** ═══════════════════
    //
    // 🔑 `socketpair` 를 `ard` 자리에 꽂는다. `send_raw` 가 진짜 `send()` 를 하고 내가 반대편에서
    // 읽으므로 **"한 거래로 나갔는가"를 전선 바이트로 확인**할 수 있다.
    // ⚠ 이것은 **실물 왕복이 아니다** — 장치도 ESP 도 없다. 검증하는 것은 **내 큐 규율**이고,
    // 실기 도달은 시험 인스턴스(루트 순서)에서 따로 봐야 한다. 두 개를 같은 확신도로 말하지 않는다.
    {
        std::cout << "\n[슬롯 큐] socketpair 로 전선 바이트 대조\n";
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            std::cout << "  ! socketpair 실패 — 이 묶음을 건너뛴다\n";
        } else {
            Server s; s.no_disk = true;
            s.ard = sv[0]; s.park_dev = "P1";
            s.ard_seen = true; s.ard_last_ms = now_ms();
            char b[1024];

            // ① **이벤트가 전송을 만들지 않는다** — dispatch 3건 뒤에도 전선은 조용해야 한다
            s.dispatch('R', BAD_SOCK, "", "A1", "00000000");
            s.dispatch('R', BAD_SOCK, "", "A2", "00000000");
            s.dispatch('C', BAD_SOCK, "", "B3", "");
            int r0 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            bool ok1 = (s.downq.size() == 3 && s.batch_count == 0 && r0 < 0);
            std::cout << (ok1 ? "  ✓ " : "  ✗ ") << "dispatch 3건 → 큐 " << s.downq.size()
                      << "줄 · 전선 " << (r0 < 0 ? 0 : r0) << "B (기대: 큐 3 · 전선 0)\n";
            if (!ok1) bad++;

            // ② 창이 열리면 **한 거래**로 나간다 — recv 한 번에 세 줄이 다 들어온다
            s.flush_downq("selftest 창", false);
            int r1 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            std::string got(b, r1 > 0 ? r1 : 0);
            int lf = 0;
            for (size_t i = 0; i < got.size(); i++) if (got[i] == '\n') lf++;
            bool ok2 = (lf == 3 && s.batch_count == 1 && s.batch_lines == 3 && s.downq.empty());
            std::cout << (ok2 ? "  ✓ " : "  ✗ ") << "flush → 한 거래에 " << lf << "줄 · "
                      << (r1 > 0 ? r1 : 0) << "B · 배치 " << s.batch_count
                      << " (기대: 3줄 · 배치 1)\n";
            if (!ok2) bad++;

            // ③ FIFO — 예약/취소가 뒤집히면 안 된다
            size_t pA1 = got.find("R,1,A1"), pA2 = got.find("R,2,A2"), pB3 = got.find("C,3,B3");
            bool ok3 = (pA1 != std::string::npos && pA2 != std::string::npos &&
                        pB3 != std::string::npos && pA1 < pA2 && pA2 < pB3);
            std::cout << (ok3 ? "  ✓ " : "  ✗ ") << "FIFO 순서 R,A1 → R,A2 → C,B3\n";
            if (!ok3) bad++;

            // ④ 바이트 상한 — 넘치면 **버리지 않고 미룬다**
            int save_cap = DOWN_BATCH_CAP_B;
            DOWN_BATCH_CAP_B = 40;                    // 한 줄(약 20B)이 두 개면 넘는 값
            s.ard_last_ms = now_ms();
            s.dispatch('R', BAD_SOCK, "", "A3", "00000000");
            s.dispatch('R', BAD_SOCK, "", "A4", "00000000");
            s.dispatch('R', BAD_SOCK, "", "A5", "00000000");
            size_t before = s.downq.size();
            s.flush_downq("selftest cap", false);
            int r2 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            bool ok4 = (r2 > 0 && r2 <= 44 && !s.downq.empty() && s.downq.size() < before
                        && s.q_deferred == 1);
            std::cout << (ok4 ? "  ✓ " : "  ✗ ") << "cap 40B → 전선 " << (r2 > 0 ? r2 : 0)
                      << "B · 남은 큐 " << s.downq.size() << "줄 · 미룬창 " << s.q_deferred
                      << " (기대: 남은 큐 >0 · 미룬창 1 · 버림 0)\n";
            if (!ok4) bad++;
            DOWN_BATCH_CAP_B = save_cap;

            // ⑤ 창을 지나면 **다음 S 를 기다린다**(장치 송신 구간을 침범하지 않는다)
            s.ard_last_ms = now_ms() - (DOWN_WIN_MS + 10);
            long long skips0 = s.win_skips;
            s.flush_downq("selftest 창밖", false);
            int r3 = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
            bool ok5 = (s.win_skips == skips0 + 1 && r3 < 0);
            std::cout << (ok5 ? "  ✓ " : "  ✗ ") << "창 밖 flush → 창놓침 " << s.win_skips
                      << " · 전선 " << (r3 < 0 ? 0 : r3) << "B (기대: +1 · 0B)\n";
            if (!ok5) bad++;

            // ⑥ 🔴 **재전송이 `tries` 를 리셋하지 않는다** — 이 검사가 없으면 무한 재전송을
            //    그냥 내보냈을 것이다(구현 중에 실제로 그 코드를 썼고 여기서 막았다).
            s.ard_last_ms = now_ms();
            s.flush_downq("selftest 잔여", true);
            while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}   // 전선 비우기
            uint16_t rid = 0;
            for (std::map<uint16_t, Pending>::iterator it = s.pend.begin();
                 it != s.pend.end(); ++it)
                if (!it->second.queued) { rid = it->first; break; }
            bool ok6 = false;
            if (rid) {
                int t0 = s.pend[rid].tries;
                s.pend[rid].sent_ms = now_ms() - (ACK_TIMEOUT_MS + 50);
                s.tick();                                    // 재전송 → 큐로
                bool requeued = s.pend.count(rid) && s.pend[rid].queued;
                int t1 = s.pend.count(rid) ? s.pend[rid].tries : -1;
                s.ard_last_ms = now_ms();
                s.flush_downq("selftest 재전송", false);
                int t2 = s.pend.count(rid) ? s.pend[rid].tries : -1;
                ok6 = (requeued && t1 == t0 + 1 && t2 == t1);
                std::cout << (ok6 ? "  ✓ " : "  ✗ ") << "재전송: tries " << t0 << "→" << t1
                          << "→" << t2 << " · 큐 경유 " << (requeued ? "예" : "아니오")
                          << " (기대: 1→2→2 · 큐 경유)\n";
            } else {
                std::cout << "  ✗ 재전송 검사용 pend 를 못 찾았다\n";
            }
            if (!ok6) bad++;

            // ⑦ 세션이 끝나면 큐를 비우고 **답을 남긴다**(조용히 사라지지 않는다)
            s.ard_last_ms = now_ms();
            s.dispatch('R', BAD_SOCK, "", "B1", "00000000");
            size_t q0 = s.downq.size();
            long long dropped0 = s.q_dropped_link;
            s.clear_downq("selftest 세션종료");
            bool ok7 = (s.downq.empty() && s.downq_bytes == 0 &&
                        s.q_dropped_link == dropped0 + (long long)q0);
            std::cout << (ok7 ? "  ✓ " : "  ✗ ") << "세션 종료 → 큐 비움 · 링크버림 "
                      << s.q_dropped_link << " (기대: 큐 0 · 버린 수가 세어짐)\n";
            if (!ok7) bad++;

            // ⑧ 🔴 **창 포기가 슬롯당 1회로 묶인다** — `tick()` 은 200ms 마다 도는데
            //    조건이 "조용하다" 하나면 침묵 구간 내내 200ms 버스트가 된다.
            //    **장치가 가장 힘들어하는 구간에서 슬롯 규율이 통째로 꺼지는 것**이라
            //    이 검사가 없으면 다음 사람이 조건을 단순화하면서 되돌린다.
            s.ard = sv[0];
            s.ard_last_ms = now_ms() - (DOWN_DMAX_MS + 100);   // 오래 조용했다
            s.last_dmax_ms = 0;
            s.dispatch('R', BAD_SOCK, "", "B2", "00000000");
            s.dispatch('R', BAD_SOCK, "", "B4", "00000000");
            s.dispatch('R', BAD_SOCK, "", "B6", "00000000");
            long long dm0 = s.dmax_flushes;
            size_t q_before = s.downq.size();
            s.tick();                                          // 첫 포기 — 나가야 한다
            s.tick();                                          // 곧바로 또 — **나가면 안 된다**
            s.tick();
            // 🔴 **여기가 옛 규칙과 갈리는 자리다.** 슬롯이 지나도 장치가 조용하면
            //    **다시 쏘면 안 된다**(옛 규칙은 슬롯당 1회라 여기서 또 쐈다).
            s.last_dmax_ms = now_ms() - (DOWN_SLOT_MS + 100);
            s.tick();
            bool ok8 = (s.dmax_flushes == dm0 + 1);
            std::cout << (ok8 ? "  ✓ " : "  ✗ ") << "장치 침묵 중 4틱(슬롯 경과 포함) → 창포기 "
                      << (s.dmax_flushes - dm0) << "회 (기대 1 — **침묵당 1회**)\n";
            if (!ok8) bad++;

            // 🔴 탐침 크기 — 증거 없이 4건을 쏘지 않는다
            bool ok8b = (q_before >= 3 && s.downq.size() == q_before - DOWN_PROBE_N);
            std::cout << (ok8b ? "  ✓ " : "  ✗ ") << "탐침 크기: 큐 " << q_before
                      << " → " << s.downq.size() << " (기대 " << DOWN_PROBE_N << "건만 나감)\n";
            if (!ok8b) bad++;

            // 🔴 재무장 — 장치가 한 줄이라도 말하면 조건이 바뀐 것이다
            s.on_ard_line("X,noise");                          // 깨진 줄도 살아 있다는 증거다
            s.ard_last_ms = now_ms() - (DOWN_DMAX_MS + 100);   // 그 줄은 오래전이었다고 둔다
            s.last_dmax_ms = now_ms() - (DOWN_SLOT_MS + 100);
            s.tick();
            bool ok8c = (s.dmax_flushes == dm0 + 2);
            std::cout << (ok8c ? "  ✓ " : "  ✗ ") << "장치가 말한 뒤 → 창포기 "
                      << (s.dmax_flushes - dm0) << "회 (기대 2 — 재무장됨)\n";
            if (!ok8c) bad++;
            while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}

            // ⑨ 🔴 **링크가 끊긴 뒤 거짓 `ack_timeout` 이 안 나간다**
            //    `end_ard_session` 은 큐만 비우고 **전선에 나가 있던 pend 는 남는다.**
            //    그 건이 재전송으로 와서 거절되면 이미 `device_offline` 로 답이 갔는데,
            //    `tick()` 이 반환값을 무시하면 몇 틱 뒤 **`ack_timeout` 으로 한 번 더** 답한다.
            //    ⚠ 그 이름은 "전선에 나갔다. 3회 재전송까지 했다"를 뜻한다(§8.16) —
            //    서버가 스스로 거절한 건에 붙으면 **로그에 거짓 문장이 남는다.**
            {
                s.ard_last_ms = now_ms();
                s.dispatch('R', BAD_SOCK, "", "B5", "00000000");
                s.flush_downq("selftest ⑨ 전선에 올린다", false);
                uint16_t rid9 = 0;
                for (std::map<uint16_t, Pending>::iterator it = s.pend.begin();
                     it != s.pend.end(); ++it)
                    if (!it->second.queued) rid9 = it->first;
                long long fail0 = s.ack_fail_count;
                s.ard = BAD_SOCK;                       // 링크가 끊긴 상태
                if (rid9) s.pend[rid9].sent_ms = now_ms() - (ACK_TIMEOUT_MS + 50);
                s.tick(); s.tick(); s.tick(); s.tick();  // 3회 소진보다 많이 돌린다
                bool ok9 = (rid9 && s.pend.count(rid9) == 0
                            && s.ack_fail_count == fail0 && s.q_nodev > 0);
                std::cout << (ok9 ? "  ✓ " : "  ✗ ") << "링크 끊긴 뒤 재전송: pend "
                          << (s.pend.count(rid9) ? "남음" : "정리됨")
                          << " · ACK실패 " << (s.ack_fail_count - fail0)
                          << " · 장치없음 " << s.q_nodev
                          << " (기대: 정리됨 · ACK실패 0 · 장치없음 >0)\n";
                if (!ok9) bad++;
            }

            // ⑩ 🔴 **건수 상한 — 바이트가 남아도 `DOWN_BATCH_MAX_N` 에서 끊긴다**
            //    바이트만으로는 못 막는 자리다: 작은 명령 6건은 바이트 상한에 한참 못 미친다.
            {
                s.ard = sv[0];                       // ⑨ 가 끊어 놓았다 — 되살린다
                s.ard_last_ms = now_ms();
                s.downq.clear(); s.downq_bytes = 0;
                while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}
                for (int k = 0; k < 6; k++) s.dispatch_sim(BAD_SOCK, "");   // M = 9B 짜리 6건
                size_t before = s.downq.size();
                s.flush_downq("selftest 건수상한", false);
                bool ok10 = (before == 6 && (int)s.downq.size() == 6 - DOWN_BATCH_MAX_N);
                std::cout << (ok10 ? "  ✓ " : "  ✗ ") << "건수 상한: 담긴 " << before
                          << "건 중 " << (before - s.downq.size()) << "건 나감 · "
                          << s.downq.size() << "건 남음 (기대 6 담김 · "
                          << DOWN_BATCH_MAX_N << " 나감)\n";
                if (!ok10) bad++;
            }

            // ⑪ 🔴 **중요 계열이 먼저 나간다** — 치유 명령이 연타에 밀리면 치유가 죽는다
            {
                s.ard = sv[0];
                s.ard_last_ms = now_ms();
                while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}
                s.downq.clear(); s.downq_bytes = 0;
                for (int k = 0; k < 3; k++) s.dispatch_sim(sv[1], "user");  // 사용자(fd 있음)
                s.dispatch('C', BAD_SOCK, "", "B4", "");                    // 중요(내부 발생)
                s.flush_downq("selftest 우선순위", false);
                int r = (int)recv(sv[1], b, sizeof(b), MSG_DONTWAIT);
                std::string got(b, r > 0 ? r : 0);
                bool ok11 = (got.compare(0, 2, "C,") == 0);   // 중요가 맨 앞이어야 한다
                std::cout << (ok11 ? "  ✓ " : "  ✗ ") << "중요 우선: 첫 줄이 "
                          << got.substr(0, 2) << " (기대 C,)\n";
                if (!ok11) bad++;
            }

            // ⑫ 🔴 **사용자는 거절되고 중요는 거절되지 않는다** — 이 비대칭이 이번 변경의 핵심이다.
            //    사용자 거절은 **화면에 뜬다**(다시 누르면 된다). 중요를 거절하면 **아무도 모르게**
            //    서버와 장치가 갈린 채 남는다. 대칭으로 "단순화"하면 그 침묵이 돌아온다.
            {
                s.ard = sv[0];
                s.ard_last_ms = now_ms();
                s.downq.clear(); s.downq_bytes = 0;
                while (recv(sv[1], b, sizeof(b), MSG_DONTWAIT) > 0) {}
                const int cap_n = DOWN_BATCH_MAX_N * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS);
                long long rej0 = s.q_full_n;
                for (int k = 0; k < cap_n + 3; k++) s.dispatch_sim(sv[1], "u");  // 사용자 연타
                size_t user_q = s.downq.size();
                long long rej = s.q_full_n - rej0;
                s.dispatch('C', BAD_SOCK, "", "B9", "");                         // 중요 — 넘쳐도 들어간다
                bool ok12 = ((int)user_q == cap_n && rej == 3 &&
                             (int)s.downq.size() == cap_n + 1 && s.downq.back().important);
                std::cout << (ok12 ? "  ✓ " : "  ✗ ") << "사용자 " << cap_n
                          << "건에서 막힘(거절 " << rej << ") · 중요는 통과 → 큐 "
                          << s.downq.size() << " (기대 사용자 " << cap_n
                          << " · 거절 3 · 큐 " << (cap_n + 1) << ")\n";
                if (!ok12) bad++;
                s.downq.clear(); s.downq_bytes = 0;
            }

            // ⑬ 🔴 **전제 감시가 실제로 울린다** — 감시는 "넣었다"가 아니라 "울린다"로 확인한다.
            //    울리지 않는 감시는 **검사되지 않는 조건이 검사되는 것처럼 보이게** 만든다(CLAUDE.md).
            {
                bool w0 = s.s_worst_warned;
                s.on_ard_line(std::string("S,1,") + std::string(DEV_S_WORST_ASSUMED_B, 'x'));
                bool ok13 = (!w0 && s.s_worst_warned &&
                             s.s_max_b > DEV_S_WORST_ASSUMED_B);
                std::cout << (ok13 ? "  ✓ " : "  ✗ ") << "전제 감시: S "
                          << s.s_max_b << "B > 가정 " << DEV_S_WORST_ASSUMED_B
                          << "B → 경고 " << (s.s_worst_warned ? "울림" : "안 울림")
                          << " (기대 울림)\n";
                if (!ok13) bad++;
                // ⚠ 가정 이하는 울리면 안 된다 — 늘 울리는 경고는 아무도 안 본다
                Server s2;
                s2.on_ard_line("S,1,short");
                bool ok13b = (!s2.s_worst_warned && s2.s_max_b == 9);
                std::cout << (ok13b ? "  ✓ " : "  ✗ ") << "가정 이하 S(9B) → 경고 "
                          << (s2.s_worst_warned ? "울림" : "안 울림") << " (기대 안 울림)\n";
                if (!ok13b) bad++;
            }

            // 🔑 **루프백 TCP 쌍** — `socketpair` 는 주소가 없어 IP 판별을 못 밟는다.
            //    거절 경로가 이 REQ 의 본체이므로 **진짜 주소가 붙은 소켓**으로 시험한다.
            struct LoopPair {
                sock_t a, b, lsn;
                LoopPair() : a(BAD_SOCK), b(BAD_SOCK), lsn(BAD_SOCK) {
                    lsn = socket(AF_INET, SOCK_STREAM, 0);
                    if (lsn == BAD_SOCK) return;
                    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
                    sa.sin_family = AF_INET; sa.sin_port = 0;
                    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    if (bind(lsn, (struct sockaddr*)&sa, sizeof(sa)) != 0) return;
                    socklen_t sl = sizeof(sa);
                    if (getsockname(lsn, (struct sockaddr*)&sa, &sl) != 0) return;
                    if (listen(lsn, 1) != 0) return;
                    a = socket(AF_INET, SOCK_STREAM, 0);
                    if (a == BAD_SOCK) return;
                    if (connect(a, (struct sockaddr*)&sa, sizeof(sa)) != 0) { a = BAD_SOCK; return; }
                    b = accept(lsn, NULL, NULL);
                }
                ~LoopPair() {
                    if (a != BAD_SOCK) closesock(a);
                    if (b != BAD_SOCK) closesock(b);
                    if (lsn != BAD_SOCK) closesock(lsn);
                }
            };

            // ⑬ 🔴 **자리 인수 판정** (REQ-0217 ①) — 세 갈래를 전부 밟는다.
            //    ⚠ `socketpair` 는 `getpeername` 이 이름 없는 주소를 주므로 `peer_str` 이 "?" 다.
            //      그래서 **같은 IP 경로**가 자연히 성립한다 — 그 경로부터 확인하고,
            //      다른 IP 는 `ard_peer` 를 손으로 바꿔 만든다(실제 판정 코드는 같은 것을 탄다).
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;

                // (가) 🔴 **주소를 못 얻는다 → 막지 않는다**(socketpair 가 그 상황을 그대로 만든다)
                t.ard_last_ms = now_ms();                    // 방금 프레임을 받았다
                long long r0 = t.dup_devid_reject;
                bool okA = t.promote_unknown(sv[1], "P1") && t.dup_devid_reject == r0;
                std::cout << (okA ? "  ✓ " : "  ✗ ") << "주소 판별 불가 · 공백 0ms → 교체 허용 "
                          << "(거절 쪽으로 넘어지면 우리 보드가 영영 막힌다)\n";
                if (!okA) bad++;
            }
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;
                t.ard_peer = "?:1";                          // 새 소켓과 같은 host("?")
                t.ard_last_ms = now_ms();
                bool okA2 = (t.promote_unknown(sv[1], "P1") && t.dup_devid_reject == 0);
                std::cout << (okA2 ? "  ✓ " : "  ✗ ") << "같은 IP · 공백 0ms → 교체 허용 "
                          << "(정상 재접속 85건 중 공백 0·1·2초가 실재한다)\n";
                if (!okA2) bad++;
                t.ard = BAD_SOCK;
            }
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;
                t.ard_peer = "10.0.0.99:1234";               // 🔴 다른 IP 로 만든다
                LoopPair lp;                                  // 새 소켓엔 진짜 주소가 붙는다

                // (나) 다른 IP + 기존이 최근에 말했다 → 거절
                t.ard_last_ms = now_ms();
                bool okB = (lp.b != BAD_SOCK && t.promote_unknown(lp.b, "P1") == false
                            && t.dup_devid_reject == 1 && t.takeover_grace == 0);
                lp.b = BAD_SOCK;                              // promote 가 닫았다
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "다른 IP · 기존이 말하는 중 → 거절 "
                          << "(거절 " << t.dup_devid_reject << " · 유예교체 "
                          << t.takeover_grace << " · 기대 1/0)\n";
                if (!okB) bad++;
            }
            {
                Server t;
                t.ard = sv[0]; t.park_dev = "P1"; t.ard_seen = true;
                t.ard_peer = "10.0.0.99:1234";
                LoopPair lp;

                // (다) 다른 IP + 기존이 유예를 넘겨 조용하다 → 교체 허용(경고 남김)
                t.ard_last_ms = now_ms() - (TAKEOVER_GRACE_MS + 100);
                bool okC = (lp.b != BAD_SOCK && t.promote_unknown(lp.b, "P1") == true
                            && t.dup_devid_reject == 0 && t.takeover_grace == 1);
                lp.b = BAD_SOCK;
                std::cout << (okC ? "  ✓ " : "  ✗ ") << "다른 IP · 기존이 "
                          << TAKEOVER_GRACE_MS << "ms 조용 → 교체 허용 (거절 "
                          << t.dup_devid_reject << " · 유예교체 " << t.takeover_grace
                          << " · 기대 0/1)\n";
                if (!okC) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑭ 🔴 **주차 노드 잠금** (REQ-0217 ④) — 잠금 미지정 시 거동이 안 바뀌는 것까지 본다
            {
                Server t; t.ard = BAD_SOCK;
                g_park_dev_pin = "P1A";
                bool tookAux = t.promote_unknown(sv[1], "P1");   // 잠금과 다른 devid
                bool okD = (tookAux && t.park_dev != "P1");      // 주차 노드가 되면 안 된다
                std::cout << (okD ? "  ✓ " : "  ✗ ") << "잠금 P1A · device=P1 의 S → 주차 노드 '"
                          << t.park_dev << "' (기대: P1 이 아님 — 보조로 받는다)\n";
                if (!okD) bad++;
                t.ard = BAD_SOCK;
            }
            {
                Server t; t.ard = BAD_SOCK;
                g_park_dev_pin = "";                             // 잠금 없음 = 종전 동작
                bool okE = (t.promote_unknown(sv[1], "P1") && t.park_dev == "P1");
                std::cout << (okE ? "  ✓ " : "  ✗ ") << "잠금 없음 → first-S-wins 그대로 (주차 노드 '"
                          << t.park_dev << "' · 기대 P1)\n";
                if (!okE) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑮ 🔴 **등록 `D` — 정상 경로**(설계 §5). 슬롯1 `S` 승격 → 슬롯2 `D`
            {
                Server t; t.ard = sv[0]; t.park.devid = "P1"; t.ard_seen = true;
                t.on_ard_line(t_line("D,*,7,3,"));                  // drain=7 · n=3
                t.on_ard_line(t_line("D,A1,IP,"));
                t.on_ard_line(t_line("D,A2,OG,"));
                bool mid = (!t.park.reg_done && t.park.mods.size() == 2);
                t.on_ard_line(t_line("D,A3,OB,"));
                bool ok15 = (mid && t.park.reg_done && t.park.reg_n == 3
                             && t.park.reg_drain == 7 && t.reg_ok == 1 && t.reg_bad == 0
                             && t.reg_cmdable() == 2);   // OG·OB 만 명령 가능
                std::cout << (ok15 ? "  ✓ " : "  ✗ ") << "등록: n=3 다 받아야 완료 · drain "
                          << t.park.reg_drain << " · 명령가능 " << t.reg_cmdable()
                          << " (기대 완료 · 7 · 2)\n";
                if (!ok15) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑯ 🔴 **`*` 는 모듈 이름이 될 수 없다** — 안 막으면 배출률 선언으로 파싱된다
            {
                Server t; t.ard = sv[0]; t.ard_seen = true;
                t.on_ard_line(t_line("D,*,IP,"));            // name 이 `*` 인 모듈처럼 생긴 줄
                bool ok16 = (t.reg_bad == 1 && t.park.reg_n < 0 && t.park.reg_drain < 0);
                std::cout << (ok16 ? "  ✓ " : "  ✗ ") << "`*` 이름 거부: 형식오류 "
                          << t.reg_bad << " · drain " << t.park.reg_drain
                          << " (기대 1 · -1 — atoi 가 0 을 주지 않았다)\n";
                if (!ok16) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑰ 🔴 **삼중 검산 ③ — `S` 의 폭이 선언 `n` 과 갈리면 잡는다**
            //    ①②만으로는 못 잡는다. 같은 함수에서 나온 값이 갈렸다는 뜻이라 큰 신호다.
            {
                Server t; t.ard = sv[0]; t.ard_seen = true;
                t.on_ard_line(t_line("D,*,7,10,"));
                for (int i = 0; i < 10; i++) t.on_ard_line(t_line("D,X,IP,"));
                long long w0 = t.reg_widthbad;
                t.on_ard_line(t_line("S,1,18B,000,389,P1,"));        // 폭 3 = ceil(10/4) ✅
                bool okA = (t.reg_widthbad == w0);
                t.on_ard_line(t_line("S,2,18BC,000,389,P1,"));       // 🔴 폭 4 — 갈렸다
                bool ok17 = (okA && t.reg_widthbad == w0 + 1);
                std::cout << (ok17 ? "  ✓ " : "  ✗ ") << "폭 검산: 폭3 통과 · 폭4 잡힘 (불일치 "
                          << t.reg_widthbad << " · 기대 1)\n";
                if (!ok17) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑱ 🔴 **옛 펌웨어는 이 검사를 안 탄다** — 등록을 안 하므로 `reg_done` 이 false 다.
            //    이게 없으면 **지금 도는 장치가 매 프레임 폭불일치를 찍는다.**
            {
                Server t; t.ard = sv[0]; t.ard_seen = true;
                t.on_ard_line(t_line("S,1,0110000010,0000000000,389,P1,"));   // 옛 10진 폭 10
                bool ok18 = (t.reg_widthbad == 0 && t.reg_bad == 0);
                std::cout << (ok18 ? "  ✓ " : "  ✗ ") << "옛 펌웨어(등록 없음): 폭불일치 "
                          << t.reg_widthbad << " (기대 0 — 검사를 안 탄다)\n";
                if (!ok18) bad++;
                t.ard = BAD_SOCK;
            }

            // ⑲ 🔴 **`Q` 상한과 포기** — 조건을 적었으면 그것을 보는 검사를 같은 자리에.
            //    창 A 에서 "정지 조건을 적어 놓고 그것을 보는 코드가 없어" 130건이 더 돌았다.
            {
                // 🔴 **자기 소켓쌍을 쓴다. 공용 `sv` 를 쓰면 안 된다** —
                //   앞 시험(⑬-가)이 `promote_unknown(sv[1], …)` 로 그 fd 를 `ard` 에 넣었고
                //   그 `Server` 의 **소멸자가 `sv[1]` 을 닫는다.** 그러면 여기 `send_raw` 가 실패하고
                //   `ard_send_failed()` 가 소켓을 닫아 **질의가 1회에서 멈춘다.**
                //   ⚠ 처음에 실제로 그렇게 나왔고, **시험이 재려던 것(상한)이 아니라 전송 실패를 쟀다.**
                LoopPair lq;
                Server t; t.ard = lq.a; t.ard_seen = true; t.park.devid = "P1";
                t.ard_last_ms = now_ms();
                t.park.reg_first_ms = now_ms() - (REG_TIMEOUT_MS + 100);   // 마감 지났다
                for (int i = 0; i < 6; i++) {          // 상한보다 많이 돌린다
                    t.park.last_q_ms = 0;              // 슬롯 문턱은 통과시킨다
                    t.tick();
                }
                bool ok19 = (t.reg_qsent == REG_Q_MAX && t.park.reg_giveup
                             && t.reg_giveups == 1);
                std::cout << (ok19 ? "  ✓ " : "  ✗ ") << "Q 상한: 6틱에 질의 " << t.reg_qsent
                          << "회 · 포기 " << t.reg_giveups << " (기대 " << REG_Q_MAX << " · 1)\n";
                if (!ok19) bad++;
                t.ard = BAD_SOCK;      // 소멸자가 lq.a 를 닫지 않게 — LoopPair 가 닫는다
            }

            // ⑳ 🔴 **등록이 끝났으면 `Q` 를 안 보낸다** — 이게 없으면 매 슬롯 질의가 나가고
            //    **그 노드의 하행이 통째로 굶는다**(질의가 창당 1거래를 독점한다).
            {
                LoopPair lr;
                Server t; t.ard = lr.a; t.ard_seen = true;
                t.ard_last_ms = now_ms();
                t.on_ard_line(t_line("D,*,7,1,"));
                t.on_ard_line(t_line("D,A1,IP,"));
                t.park.reg_first_ms = now_ms() - (REG_TIMEOUT_MS + 100);
                for (int i = 0; i < 4; i++) { t.park.last_q_ms = 0; t.tick(); }
                bool ok20 = (t.park.reg_done && t.reg_qsent == 0 && t.reg_giveups == 0);
                std::cout << (ok20 ? "  ✓ " : "  ✗ ") << "등록 완료 뒤 질의 " << t.reg_qsent
                          << "회 (기대 0)\n";
                if (!ok20) bad++;
                t.ard = BAD_SOCK;
            }

            // ㉑ 🔴 **`all_nodes()` 가 주차 + 보조를 다 훑는다** (REQ-0203 3a)
            //    ⚠ 색인을 저장하지 않는 선택의 값을 여기서 확인한다 — **`aux` 를 바꾼 뒤
            //      아무것도 갱신하지 않고 바로 물어봐도 맞아야 한다.**
            {
                Server t;
                bool e0 = t.all_nodes().empty();                 // 아무것도 없을 때
                t.park.devid = "P1"; t.park.fd = sv[0];
                bool one = (t.all_nodes().size() == 1);
                t.aux["P2"].fd = sv[1];                          // 갱신 호출 없이 바로
                t.aux["P3"].fd = sv[1];
                std::vector<Node*> v = t.all_nodes();
                bool three = (v.size() == 3 && v[0] == &t.park);  // 주차가 먼저다
                bool ok21 = (e0 && one && three);
                std::cout << (ok21 ? "  ✓ " : "  ✗ ") << "all_nodes(): 0 → 1 → "
                          << v.size() << " (기대 3 · 주차가 앞 · **갱신 호출 없이**)\n";
                if (!ok21) bad++;
                t.park.fd = BAD_SOCK; t.aux.clear();
            }

            // ㉒ 🔴 **지형 구성과 판(`epoch`)** (REQ-0203 4a)
            {
                Server t;
                t.build_default_zones();
                // 🔴 **7영역으로 바뀌었다**(사용자 확정 (A) · 명세 §9). 옛 기대는 12 였다 —
                //   주차 자리가 10 이 아니라 **5**(각 자리에 센서 둘)이고 + 입구 + 출구 = 7.
                //   ⚠ 이 시험이 옛 계약을 들고 있어서 지형을 바꾸자 곧바로 빨간불이 됐다. **그게 제 일이다.**
                Zone* a1 = t.zone_find("A1"); Zone* e1 = t.zone_find("E1");
                Zone* b1 = t.zone_find("B1");        // 🔴 이제 자리가 아니라 **센서 이름**이다
                bool ok22 = (t.zones.size() == 7 && a1 && e1
                             && a1->kind == "parking" && e1->kind == "entrance"
                             && a1->cells.size() == 1 && t.map_epoch == 1
                             && b1 == 0);            // **B1 은 자리로 존재하지 않아야 한다**
                std::cout << (ok22 ? "  ✓ " : "  ✗ ") << "지형: 자리 " << t.zones.size()
                          << " · A1 칸수 " << (a1 ? a1->cells.size() : 0)
                          << " · 판 " << t.map_epoch
                          << " · B1 자리없음 " << (b1 == 0 ? "예" : "🔴아니오")
                          << " (기대 7 · 1 · 1 · 예)\n";
                if (!ok22) bad++;

                // 🔴 **등록이 지형을 바꾸면 판이 오른다**
                t.park.devid = "P1";
                t.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                t.park.mods.push_back(std::make_pair(std::string("A2"), std::string("OG")));
                t.bind_modules(t.park);
                bool ok23 = (t.map_epoch == 2 && a1->modules.size() == 1
                             && a1->modules[0].first == "P1" && a1->modules[0].second == "A1");
                std::cout << (ok23 ? "  ✓ " : "  ✗ ") << "결속: A1 모듈 "
                          << a1->modules.size() << "개 (devid=" << (a1->modules.empty() ? "" : a1->modules[0].first)
                          << ") · 판 " << t.map_epoch << " (기대 1 · P1 · 2)\n";
                if (!ok23) bad++;

                // 🔴 **같은 등록을 다시 받아도 판이 안 오른다** — 안 그러면 재접속마다 화면이 맵을 다시 받는다
                t.bind_modules(t.park);
                bool ok24 = (t.map_epoch == 2 && a1->modules.size() == 1);
                std::cout << (ok24 ? "  ✓ " : "  ✗ ") << "같은 결속 반복 → 판 " << t.map_epoch
                          << " · 모듈 " << a1->modules.size() << " (기대 2 · 1 — 안 오른다)\n";
                if (!ok24) bad++;

                // 🔴 **상태만 바뀌는 것은 판을 안 올린다** — 올리면 봉투를 가른 이유가 사라진다
                long long e0 = t.map_epoch;
                t.slots[0].occupied = 1; t.slots[0].reserved = 1;
                bool ok25 = (t.map_epoch == e0);
                std::cout << (ok25 ? "  ✓ " : "  ✗ ") << "점유·예약 변경 → 판 " << t.map_epoch
                          << " (기대 " << e0 << " — 상태는 판을 안 올린다)\n";
                if (!ok25) bad++;
            }

            // ㉓ 🔴 **`map` 봉투** (REQ-0203 4b) — web 이 이 형식에 맞춰 이미 구현했다
            {
                Server t;
                t.build_default_zones();
                t.park.devid = "P1";
                t.park.mods.push_back(std::make_pair(std::string("A1"), std::string("IP")));
                t.bind_modules(t.park);
                std::string j = t.map_json();
                bool ok26 = (j.find("\"type\":\"map\"") != std::string::npos
                             && j.find("\"epoch\":2") != std::string::npos
                             && j.find("\"rows\":5") != std::string::npos
                             && j.find("\"cells\":[[0,0]]") != std::string::npos
                             && j.find("\"devid\":\"P1\",\"name\":\"A1\",\"kind\":\"IP\",\"idx\":0")
                                != std::string::npos);
                // 🔴 **`srv_id` 가 봉투에 있어야 한다** — 없으면 재기동 뒤 화면이 새 맵을 무시한다
                t.init_srv_id();
                std::string j3 = t.map_json();
                bool ok26b = (j3.find("\"srv_id\":") != std::string::npos && !t.srv_id.empty());
                std::cout << (ok26b ? "  ✓ " : "  ✗ ") << "srv_id 가 map 봉투에 있다 ("
                          << t.srv_id.substr(0, 24) << "…)\n";
                if (!ok26b) bad++;

                std::cout << (ok26 ? "  ✓ " : "  ✗ ") << "map 봉투: type·epoch·grid·cells·모듈("
                          << "devid+name+kind+idx) 전부 포함\n";
                if (!ok26) bad++;

                // 🔴 **`kind`·`idx` 를 노드에서 찾는다 — 이름만으로 찾으면 안 된다**(복합 키)
                //    다른 노드가 같은 `name` 을 가져도 섞이면 안 된다.
                Node& other = t.aux["P9"]; other.devid = "P9";
                other.mods.push_back(std::make_pair(std::string("A1"), std::string("OB")));
                std::string j2 = t.map_json();
                bool ok27 = (j2.find("\"devid\":\"P1\",\"name\":\"A1\",\"kind\":\"IP\"")
                             != std::string::npos
                             && j2.find("\"kind\":\"OB\"") == std::string::npos);
                std::cout << (ok27 ? "  ✓ " : "  ✗ ") << "복합 키: 다른 노드의 같은 이름 모듈이 "
                          << "섞이지 않는다 (기대: P1 의 IP 만)\n";
                if (!ok27) bad++;
                t.aux.clear();
            }

            // ㉔ 🔴 **`get_map` 상한** — 화면이 비교를 잘못 구현하면 무한 재요청이 된다
            {
                Server t; t.build_default_zones();
                long long r0 = t.getmap_rejects;
                for (int i = 0; i < GETMAP_MAX_PER_SEC + 3; i++)
                    t.on_ws_message(BAD_SOCK, "{\"type\":\"get_map\",\"rid\":\"r\"}");
                bool ok28 = (t.getmap_rejects == r0 + 3);
                std::cout << (ok28 ? "  ✓ " : "  ✗ ") << "get_map 상한: " << (GETMAP_MAX_PER_SEC + 3)
                          << "회 중 거절 " << t.getmap_rejects << " (기대 3)\n";
                if (!ok28) bad++;
            }

            // ㉔-나 🔴 **상한은 화면마다다 — 남의 화면이 내 몫을 먹지 않는다** (2026-08-19)
            //   주석은 원래 *"화면 하나가 1초에"* 라고 말했는데 **구현은 서버 전역**이었다.
            //   그러면 **화면 여섯이 재접속하는 것만으로** 정상 요청이 거절되고,
            //   거절당한 화면은 **지형을 못 받아 빈 채로 남는다.**
            //   🔑 고친 것은 상한값이 아니라 **누구를 세는가**다. 그래서 시험도 "둘이 각각"이다.
            {
                sock_t p1[2], p2[2];
                bool okpair = (socketpair(AF_UNIX, SOCK_STREAM, 0, p1) == 0
                               && socketpair(AF_UNIX, SOCK_STREAM, 0, p2) == 0);
                if (okpair) {
                    Server t; t.build_default_zones(); t.init_srv_id();
                    t.conns[p1[0]].kind = Conn::WS;
                    t.conns[p2[0]].kind = Conn::WS;
                    long long r0 = t.getmap_rejects;
                    // 각 화면이 **상한만큼** 묻는다 → 합은 상한의 두 배지만 **거절은 0 이어야 한다**
                    for (int i = 0; i < GETMAP_MAX_PER_SEC; i++) {
                        t.on_ws_message(p1[0], "{\"type\":\"get_map\",\"rid\":\"a\"}");
                        t.on_ws_message(p2[0], "{\"type\":\"get_map\",\"rid\":\"b\"}");
                    }
                    bool ok28b = (t.getmap_rejects == r0);
                    std::cout << (ok28b ? "  ✓ " : "  ✗ ") << "화면 둘이 각각 "
                              << GETMAP_MAX_PER_SEC << "회 → 거절 " << (t.getmap_rejects - r0)
                              << " (기대 0 · 전역 상한이면 " << GETMAP_MAX_PER_SEC << ")\n";
                    if (!ok28b) bad++;

                    // 그리고 **한 화면이 넘기면 그 화면만** 거절된다
                    for (int i = 0; i < 3; i++)
                        t.on_ws_message(p1[0], "{\"type\":\"get_map\",\"rid\":\"a\"}");
                    bool ok28c = (t.getmap_rejects == r0 + 3);
                    std::cout << (ok28c ? "  ✓ " : "  ✗ ") << "넘긴 화면만 거절된다 (거절 "
                              << (t.getmap_rejects - r0) << " · 기대 3)\n";
                    if (!ok28c) bad++;
                    t.conns.clear();
                    closesock(p1[0]); closesock(p1[1]); closesock(p2[0]); closesock(p2[1]);
                }
            }

            // ㉔-다 🔴 **빈 지형은 `map` 으로 안 나간다** — 첫 배포가 정확히 그 상태였다
            //   그때 화면은 **"자리 0개인 주차장"** 을 정상으로 그렸다.
            //   🔑 **빈 지형은 답이 아니라 고장이다. 답인 척하면 아무도 안 본다.**
            {
                sock_t p3[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, p3) == 0) {
                    Server t; t.init_srv_id();          // 🔑 **`build_default_zones()` 를 일부러 안 부른다**
                    t.conns[p3[0]].kind = Conn::WS;
                    t.push_map();
                    char b2[256];
                    int r = (int)recv(p3[1], b2, sizeof(b2), MSG_DONTWAIT);
                    bool ok28d = (t.zones.empty() && r <= 0);
                    std::cout << (ok28d ? "  ✓ " : "  ✗ ") << "빈 지형 → map 미송신 (전선 "
                              << (r > 0 ? r : 0) << "B · 기대 0)\n";
                    if (!ok28d) bad++;
                    t.conns.clear();
                    closesock(p3[0]); closesock(p3[1]);
                }
            }

            // ㉕ 🔴 **`state` 봉투와 `actions`** (REQ-0203 4c)
            {
                Server t; t.build_default_zones(); t.init_srv_id();
                std::string j = t.state_json();
                // 🔴 **2026-08-19 — 이 시험은 예전에 `module_absent` 를 기대했다. 바꿨다.**
                //   여기는 **노드가 아예 없는** 상태다(`all_nodes()` 가 빈다).
                //   `module_absent` 는 *"이 자리엔 장비가 없다"* = **영구**이고, 화면은 버튼을
                //   **안 그린다.** 그런데 진실은 *"아직 아무도 안 붙었다"* = **일시**다.
                //   🔑 **코드에 맞춰 시험을 고친 것이 아니다** — 어느 답이 화면에 옳은지로 골랐다.
                //   ⚠ 그리고 **셋을 다 밟는다.** 하나만 보면 나머지 둘이 서로 바뀌어도 통과한다.
                bool a1 = (j.find("\"reserve\":{\"ok\":false,\"reason\":\"node_offline\"}")
                           != std::string::npos)
                       && (j.find("\"cancel\"") == std::string::npos);
                std::cout << (a1 ? "  ✓ " : "  ✗ ") << "노드 없음 → reserve 막힘(node_offline) · "
                          << "예약 없으니 **cancel 키 자체가 없다**\n";
                if (!a1) bad++;

                // (나) — 노드가 붙었는데 **등록 중**이면 `node_unregistered`
                {
                    Server u; u.build_default_zones(); u.init_srv_id();
                    u.park.devid = "P1"; u.park.reg_done = false; u.park.reg_giveup = false;
                    bool b1 = (u.state_json().find("\"reason\":\"node_unregistered\"")
                               != std::string::npos);
                    std::cout << (b1 ? "  ✓ " : "  ✗ ") << "등록 중 → node_unregistered"
                              << "(**곧 붙는다**를 영구 부재로 답하지 않는다)\n";
                    if (!b1) bad++;

                    // (라) 🔴 **차가 있는 자리는 예약을 못 내놓는다** (REQ-0235)
                    //   ⚠ 이 시험이 없으면 "여는 근거와 막는 근거가 다른" 상태가 다시 생긴다 —
                    //     화면이 버튼을 그리고 장치가 `result=1` 로 거절하는 **눌렀는데 안 되는 버튼**.
                    {
                        // ⚠ **장치가 온라인이어야 이 갈래에 닿는다.** 안 세우면 `node_offline` 이
                        //   먼저 걸려 **막힌 이유가 달라진 채로 시험이 빨강**이 된다 —
                        //   처음에 그렇게 실패했고, 그게 "시험이 실기 상태를 안 세운" 것이다.
                        sock_t ws_[2];
                        if (socketpair(AF_UNIX, SOCK_STREAM, 0, ws_) != 0) { ws_[0] = ws_[1] = BAD_SOCK; }
                        Server w; w.build_default_zones(); w.init_srv_id();
                        w.ard = ws_[0]; w.ard_seen = true; w.ard_last_ms = now_ms();
                        w.park.devid = "P1"; w.park.reg_done = true;
                        for (int i = 0; i < 10; i++)
                            w.park.mods.push_back(std::make_pair(std::string(SLOT_ID[i]),
                                                                 std::string("IP")));
                        w.bind_modules(w.park);
                        w.slots[2].occupied = 1;              // A3 만 점유
                        std::string jw = w.state_json();
                        size_t pa = jw.find("\"id\":\"A3\"");
                        std::string a3 = (pa == std::string::npos) ? "" : jw.substr(pa, 400);
                        bool d1 = (a3.find("\"reserve\":{\"ok\":false,\"reason\":\"occupied\"}")
                                   != std::string::npos);
                        std::cout << (d1 ? "  ✓ " : "  ✗ ") << "점유된 A3 → reserve 가 "
                                  << "**ok:false/occupied** (키는 남긴다 — 차가 나가면 가능해진다)\n";
                        if (!d1) bad++;

                        // 🔑 **빈 자리는 그대로 열려 있어야 한다** — 안 그러면 전부 막아 놓고 통과한다
                        size_t pb = jw.find("\"id\":\"A1\"");
                        std::string a1 = (pb == std::string::npos) ? "" : jw.substr(pb, 400);
                        bool d2 = (a1.find("\"reserve\":{\"ok\":true") != std::string::npos);
                        std::cout << (d2 ? "  ✓ " : "  ✗ ") << "빈 A1 → reserve 는 열려 있다"
                                  << "(전부 막아 놓고 통과하는 것을 막는다)\n";
                        if (!d2) bad++;
                        w.ard = BAD_SOCK;
                        if (ws_[0] != BAD_SOCK) closesock(ws_[0]);
                        if (ws_[1] != BAD_SOCK) closesock(ws_[1]);
                    }

                    // (다) — 등록이 **끝났는데** 이 자리엔 안 붙었다 → 그때가 `module_absent` 다
                    u.park.reg_done = true;
                    bool c1 = (u.state_json().find("\"reason\":\"module_absent\"")
                               != std::string::npos);
                    std::cout << (c1 ? "  ✓ " : "  ✗ ") << "등록 끝 + 미결속 → module_absent"
                              << "(이때는 영구 부재가 정직하다)\n";
                    if (!c1) bad++;
                }

                // 예약되면 `reserve` 가 사라지고 `cancel` 이 나온다 — **존재/부재가 한 비트다**
                t.slots[0].reserved = 1;
                std::string j2 = t.state_json();
                bool a2 = (j2.find("\"cancel\":{") != std::string::npos);
                std::cout << (a2 ? "  ✓ " : "  ✗ ") << "예약 뒤 → cancel 키가 생긴다\n";
                if (!a2) bad++;

                // 입출구는 조작이 둘이다 — 🔑 **`bool` 하나였으면 못 갈렸을 자리**
                bool a3 = (j2.find("\"open_gate\":{") != std::string::npos
                           && j2.find("\"close_gate\":{") != std::string::npos);
                std::cout << (a3 ? "  ✓ " : "  ✗ ") << "입출구: open_gate·close_gate 둘 다 나온다\n";
                if (!a3) bad++;

                // 🔴 `state` 가 `srv_id`·`epoch` 를 싣는다 — 화면이 낡음을 스스로 안다
                bool a4 = (j2.find("\"srv_id\":") != std::string::npos
                           && j2.find("\"epoch\":1") != std::string::npos);
                std::cout << (a4 ? "  ✓ " : "  ✗ ") << "state 에 srv_id·epoch 가 실린다\n";
                if (!a4) bad++;
            }

            // ㉗ 🔴 **기동 경로가 지형을 만드는가** — 시험이 자기 지형을 만들면 이걸 못 잡는다
            //    실제로 첫 배포에서 `zones` 가 빈 채 나갔고 **시험은 전부 통과했다.**
            {
                Server t;                                   // 🔑 **아무것도 안 부른 상태**
                bool empty0 = t.zones.empty() && t.map_epoch == 0;
                std::string j = t.map_json();
                bool ok31 = (empty0 && j.find("\"zones\":[]") != std::string::npos);
                std::cout << (ok31 ? "  ✓ " : "  ✗ ") << "새 Server 는 지형이 비어 있다 — "
                          << "**기동 경로가 build_default_zones() 를 불러야 한다**"
                          << " (map 의 zones=" << (j.find("\"zones\":[]") != std::string::npos ? "빈 배열" : "채워짐")
                          << ")\n";
                if (!ok31) bad++;
            }

            // ㉖ 🔴 **조작 요청 라우팅** (REQ-0203 4d) — **거절 사유가 갈려야 한다**
            {
                LoopPair lw;                       // 화면 소켓 자리(진짜 fd 가 있어야 응답이 나간다)
                Server t; t.build_default_zones(); t.init_srv_id();
                t.conns[lw.a].kind = Conn::WS;     // 🔑 **WS 로 승격해야 응답이 나간다**

                // ① 없는 자리
                t.on_ws_message(lw.a, "{\"type\":\"open_gate\",\"slot\":\"ZZ\",\"rid\":\"1\"}");
                // ② 차단봉이 없는 주차 자리 — **조용히 무시하지 않는다**
                t.on_ws_message(lw.a, "{\"type\":\"open_gate\",\"slot\":\"A1\",\"rid\":\"2\"}");
                // ③ 입구인데 모듈이 안 붙었다 → module_absent
                t.on_ws_message(lw.a, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"3\"}");
                // ⚠ **여기도 재시도해야 한다.** 처음엔 한 번만 읽었고 디버그 빌드에서는 우연히
                //   통과했다 — 🔴 **`-O2` 빌드가 더 빨라서 그 우연이 깨졌다.**
                //   **배포용 빌드를 돌려 본 것이 이 결함을 드러냈다.**
                // 🔴 **한 번의 `recv` 에 단언하지 않는다** (2026-08-19 수리)
                //   전에는 첫 성공 `recv` 하나로 판정하고 **그 바이트 수를 찍었다.** 그 값이
                //   `93B` 와 `199B` 사이를 오갔다 — **응답 셋이 한 번에 오기도 하고 나뉘어 오기도 한다.**
                //   🔑 **부분 읽기는 정상이다.** TCP 는 스트림이지 메시지가 아니다(내 도메인 제1규칙).
                //   ⚠ 그런데 그 비결정 줄이 **출력 대조라는 판별 도구 자체를 흔들었다** —
                //     ②-a·②-c 대조에서 매번 잡음으로 나왔다. 판별자를 쓰기 직전이 고칠 마지막 기회다.
                //   ⚠ 그리고 더 나쁜 것: 첫 조각만 읽고 단언하면 **셋 중 하나만 보고 통과**할 수 있다.
                //     §"헛통과" 부류다 — 밟긴 밟았는데 다른 것을 밟았다.
                //   → **더 안 올 때까지 모으고, 찍는 값도 바이트가 아니라 *건수* 로 바꾼다.**
                char rb[4096]; std::string got; int idle = 0;
                for (int tries = 0; tries < 4000 && idle < 200; tries++) {
                    int n = (int)recv(lw.b, rb, sizeof(rb), MSG_DONTWAIT);
                    if (n > 0) { got.append(rb, n); idle = 0; } else idle++;
                }
                int msgs = 0;
                for (size_t q = got.find("\"rid\""); q != std::string::npos; q = got.find("\"rid\"", q + 1)) msgs++;
                bool ok29 = (got.find("module_absent") != std::string::npos && msgs == 3);
                std::cout << (ok29 ? "  ✓ " : "  ✗ ") << "조작 요청: 없는 자리·차단봉 없는 자리·"
                          << "미결속 입구 → 전부 사유가 간다(응답 " << msgs << "건 · 기대 3)\n";
                if (!ok29) bad++;

                // 🔴 ④ 모듈이 붙고 노드가 살아 있어도 **전선 명령이 없으면 not_supported**
                //    ⚠ 조용히 성공으로 답하면 화면이 "열렸다"로 그리고 아무 일도 안 일어난다
                Zone* e1 = t.zone_find("E1");
                e1->modules.push_back(std::make_pair(std::string("P1"), std::string("E1")));
                // 🔑 **자기 소켓쌍을 준다.** 앞 응답 셋이 남은 버퍼에서 읽으면
                //    **시험이 재려던 것이 아니라 앞 시험의 응답을 재게 된다** — 실제로 그랬다.
                LoopPair lx; t.conns[lx.a].kind = Conn::WS;
                t.park.devid = "P1"; t.park.fd = lx.a; t.park.seen = true;
                t.park.last_ms = now_ms(); t.park.reg_done = true;
                t.on_ws_message(lx.a, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"4\"}");
                // ⚠ **루프백 TCP 는 `socketpair` 와 달리 즉시 도착하지 않는다.**
                //   한 번만 읽고 `-1`(EWOULDBLOCK)을 "안 보냈다"로 읽으면 **없는 결함이 생긴다** —
                //   실제로 그렇게 나왔다. 짧게 재시도한다.
                // 🔑 여기도 **모아서 읽는다**(앞 시험과 같은 이유 · 부분 읽기는 정상이다)
                std::string g2; int idle2 = 0;
                for (int tries = 0; tries < 4000 && idle2 < 200; tries++) {
                    int n2 = (int)recv(lx.b, rb, sizeof(rb), MSG_DONTWAIT);
                    if (n2 > 0) { g2.append(rb, n2); idle2 = 0; } else idle2++;
                }
                bool ok30 = (g2.find("not_supported") != std::string::npos);
                if (!ok30) std::cout << "    [진단] lx.a=" << lx.a << " lx.b=" << lx.b
                                     << " conns=" << t.conns.count(lx.a) << " 받은B=" << g2.size() << "\n";
                std::cout << (ok30 ? "  ✓ " : "  ✗ ") << "준비된 자리인데 전선 명령이 없다 → "
                          << "**not_supported**(거짓 완료를 안 만든다)\n";
                if (!ok30) bad++;
                t.park.fd = BAD_SOCK; t.conns.clear();
            }

            // ㉘ 🔴🔴 **자리 비트열 해독** — 이 결함이 2026-08-19 에 **실기에서 살아 있었다**
            //    장치가 hex 를 보내는데 서버가 10진으로 읽어 **엉뚱한 자리를 점유로 표시했다.**
            //    폭 검사도 체크섬도 통과했다. **검사가 있어서 처리된 것처럼 보였다.**
            {
                Server t; t.ard = sv[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                // ① 등록 전 hex 폭 → **해독하지 않는다**(n 을 모른다)
                t.on_ard_line(t_line("S,1,182,000,5,P1,"));
                bool okA = (t.occ_undecoded == 1 && t.slots[0].occupied == 0);
                std::cout << (okA ? "  ✓ " : "  ✗ ") << "등록 전 hex → 해독 안 함(미해독 "
                          << t.occ_undecoded << " · A1 점유 " << t.slots[0].occupied << ")\n";
                if (!okA) bad++;

                // ② 등록 뒤 → hex 로 푼다. `182` = 0110000010 → **A2·A3·B4**
                t.on_ard_line(t_line("D,*,7,10,"));
                for (int i = 0; i < 10; i++) t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                t.on_ard_line(t_line("S,2,182,000,6,P1,"));
                bool okB = (t.slots[0].occupied == 0 && t.slots[1].occupied == 1
                            && t.slots[2].occupied == 1 && t.slots[8].occupied == 1
                            && t.slots[9].occupied == 0);
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "등록 뒤 hex `182` → A2·A3·B4 점유"
                          << " (A1=" << t.slots[0].occupied << " A2=" << t.slots[1].occupied
                          << " B4=" << t.slots[8].occupied << " · 기대 0·1·1)\n";
                if (!okB) bad++;
                t.ard = BAD_SOCK;
            }
            {
                // ③ 🔴 **옛 10진 펌웨어는 등록을 안 해도 그대로 읽힌다** — 회귀를 안 만든다
                Server t; t.ard = sv[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                t.on_ard_line(t_line("S,1,0110000010,0000000000,5,P1,"));
                bool okC = (t.slots[1].occupied == 1 && t.slots[2].occupied == 1
                            && t.slots[8].occupied == 1 && t.occ_undecoded == 0);
                std::cout << (okC ? "  ✓ " : "  ✗ ") << "옛 10진(미등록) → 그대로 읽는다 "
                          << "(미해독 " << t.occ_undecoded << " · 기대 0)\n";
                if (!okC) bad++;
                t.ard = BAD_SOCK;
            }

            // ㉙ 🔴🔴 **자가 치유(§7.6)가 실제로 돈다** — 2026-08-19 에 이 분기가 죽어 있었다
            //    hex 전환 뒤 조건이 `f[3].size() >= 10` 이라 **영영 거짓**이었다.
            //    ⚠ **오독이 아니라 건너뜀이라 로그에 `0` 조차 안 남았다.** 그래서 **분모를 검사한다** —
            //    "몇 번 고쳤나"가 아니라 **"몇 번 검사했나"** 가 이 결함을 잡는 유일한 값이다.
            {
                // 🔴 **자기 소켓을 쓴다. 공유 `sv` 를 안 쓴다.**
                //   앞선 시험이 `sv[0]` 을 닫아 두면 이 시험은 **하행이 안 나가는데 계수가 전부 0** 이라
                //   *"치유가 안 나간다"* 로 오독된다 — 실제로 그렇게 한 번 헤맸다.
                //   🔑 §8.23 의 "시험끼리 자원을 공유하면 실패 원인이 남의 시험에 있다" 그대로다.
                sock_t ms[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, ms) != 0) { ms[0] = ms[1] = BAD_SOCK; }
                Server t; t.ard = ms[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                t.on_ard_line(t_line("D,*,7,10,"));
                for (int i = 0; i < 10; i++) t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));

                // 장치는 A1 이 예약됐다고 말하는데 서버는 0 이다 → `C` 재하달로 맞춰야 한다.
                // `res` = hex `200` = 비트 9 = **슬롯 0(A1)**  (n=10 · 슬롯 i = 비트 n-1-i)
                long long q0 = t.heal_fires, c0 = t.heal_checks;
                t.on_ard_line(t_line("S,2,000,200,6,P1,"));
                bool okD = (t.heal_checks > c0);
                std::cout << (okD ? "  ✓ " : "  ✗ ") << "hex 예약 마스크로 **치유 검사가 돈다**"
                          << " (검사 " << (t.heal_checks - c0) << " · 기대 >0)\n";
                if (!okD) bad++;

                bool okE = (t.heal_fires == q0 + 1);
                std::cout << (okE ? "  ✓ " : "  ✗ ") << "불일치를 찾아 C 를 재하달한다 (재하달 "
                          << (t.heal_fires - q0) << " · 기대 1)\n";
                if (!okE) bad++;

                // 🔴 **비트 순서까지 확인한다.** `200` 이 A1 이어야 한다 — 뒤집혀도 건수는 1 이라
                //    위 두 검사만으로는 **자리가 어긋난 것을 못 잡는다**(§8.23-(58) 과 같은 함정).
                // 🔑 **내부 상태가 아니라 전선에 나간 바이트를 읽는다** — `pend` 는 ACK 나
                //    링크 실패로 비워질 수 있어서 **"안 보인다"가 "안 나갔다"를 뜻하지 않는다.**
                //    오늘 `49c07f6` 을 확인한 방법과 같다: **나가는 값을 본다.**
                // 🔑 **하행은 이 창에 안 나간다 — 다음 `S` 가 창을 연다**(설계 §3).
                //   ⚠ 처음에 이걸 빼먹어 전선이 비어 있었고, 하마터면 *"치유가 안 나간다"* 로
                //     읽을 뻔했다. **큐에 있는 것과 안 나간 것은 다르다.**
                //   두 번째 `S` 는 `res=000` 으로 보낸다 — 새 불일치를 안 만들어야 위 건수가 1 로 남는다.
                t.on_ard_line(t_line("S,3,000,000,7,P1,"));
                char wb[512]; std::string wire;
                for (;;) {
                    int r = (int)recv(ms[1], wb, sizeof(wb), MSG_DONTWAIT);
                    if (r <= 0) break;
                    wire.append(wb, (size_t)r);
                }
                bool okF = (wire.find(",A1,") != std::string::npos);
                std::cout << (okF ? "  ✓ " : "  ✗ ") << "전선에 나간 `C` 의 대상이 **A1** 이다"
                          << "(비트 순서가 뒤집혀도 건수는 1 이라 여기서만 갈린다)\n";
                if (!okF) { bad++; std::cout << "      전선(" << wire.size() << "B): " << wire
                                             << " · 큐 " << t.downq.size()
                                             << " · 배치 " << t.batch_count << "\n"; }
                t.ard = BAD_SOCK;
                if (ms[0] != BAD_SOCK) closesock(ms[0]);
                if (ms[1] != BAD_SOCK) closesock(ms[1]);
            }

            // ㉚ 🔴🔴 **자리 조작 `G` — 전선까지 간다** (REQ-0228 · arduino 파서에서 형식을 읽어 맞췄다)
            //    ⚠ **이 시험은 실기가 아직 못 만드는 상태를 만든다** — `명령가능 0개` 인 지금은
            //      `dispatch_gate` 가 실기에서 한 번도 안 돈다. **가상 모듈이 구워지면 그때 돈다.**
            //      🔑 그래서 **등록 순서를 실기와 똑같이** 세운다(자리 10 뒤에 차단봉 2) —
            //      순서를 다르게 세우면 `idx` 가 실기와 달라져 **시험만 통과하는 코드**가 된다.
            {
                sock_t gs[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, gs) == 0) {
                    Server t; t.build_default_zones(); t.init_srv_id();
                    t.ard = gs[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                    // n=12 : 자리 10(IP) + E1·X1 (OBV = 차단봉 · 가상)
                    t.on_ard_line(t_line("D,*,7,12,"));
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OBV,"));
                    t.on_ard_line(t_line("D,X1,OBV,"));

                    bool okG0 = (t.park.reg_done && t.park.reg_n == 12 && t.reg_cmdable() == 2);
                    std::cout << (okG0 ? "  ✓ " : "  ✗ ") << "OBV 2개가 명령가능으로 세어진다 ("
                              << t.reg_cmdable() << " · 기대 2 · `V` 접미는 종류를 안 바꾼다)\n";
                    if (!okG0) bad++;

                    // 창을 열고(첫 S) 큐를 비운 뒤 조작을 건다
                    t.on_ard_line(t_line("S,1,000,000,5,P1,"));
                    char gb[512];
                    while (recv(gs[1], gb, sizeof(gb), MSG_DONTWAIT) > 0) {}   // 전선 비우기

                    t.on_ws_message(BAD_SOCK, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"g1\"}");
                    t.on_ard_line(t_line("S,2,000,000,6,P1,"));                // 다음 창에 나간다
                    std::string gw;
                    for (;;) { int r = (int)recv(gs[1], gb, sizeof(gb), MSG_DONTWAIT);
                               if (r <= 0) break; gw.append(gb, (size_t)r); }
                    // 🔴 **`idx` 는 10 이어야 한다** — E1 은 자리 10개 **뒤**에 등록된 첫 모듈이다.
                    //    장치는 `idx >= SLOT_N` 만 받으므로 여기가 어긋나면 전부 `result=3` 이 된다.
                    bool okG1 = (gw.find("G,") != std::string::npos
                                 && gw.find(",10,1,") != std::string::npos);
                    std::cout << (okG1 ? "  ✓ " : "  ✗ ") << "open_gate(E1) → 전선에 `G,<rid>,10,1,`"
                              << (okG1 ? "" : std::string(" · 실제: ") + gw) << "\n";
                    if (!okG1) bad++;

                    // 닫기는 op=0
                    t.on_ws_message(BAD_SOCK, "{\"type\":\"close_gate\",\"slot\":\"X1\",\"rid\":\"g2\"}");
                    t.on_ard_line(t_line("S,3,000,000,7,P1,"));
                    gw.clear();
                    for (;;) { int r = (int)recv(gs[1], gb, sizeof(gb), MSG_DONTWAIT);
                               if (r <= 0) break; gw.append(gb, (size_t)r); }
                    bool okG2 = (gw.find(",11,0,") != std::string::npos);
                    std::cout << (okG2 ? "  ✓ " : "  ✗ ") << "close_gate(X1) → `,11,0,`"
                              << (okG2 ? "" : std::string(" · 실제: ") + gw) << "\n";
                    if (!okG2) bad++;

                    // 🔴 **완료는 ACK 이 아니라 다음 `S` 의 에코 비트다**
                    //    🔑 **자리 `i` 는 비트 `(n−1−i)`** 다(§5). `n=12` 이므로 **모듈 10 = 비트 1** →
                    //    hex `0x002` = `002`. ⚠ 처음에 `080`(비트 7)을 적었다가 시험이 잡았다 —
                    //    비트 7 은 모듈 4(A5)라 **자리 점유로 들어간다.** 값이 그럴듯해서 안 보인다.
                    t.on_ard_line(t_line("S,4,002,000,8,P1,"));
                    std::string js = t.state_json();
                    bool okG3 = (js.find("\"name\":\"E1\",\"idx\":10,\"value\":true,\"known\":true")
                                 != std::string::npos);
                    std::cout << (okG3 ? "  ✓ " : "  ✗ ") << "에코 비트 10 → E1 모듈 value=true·known=true"
                              << " (**ACK 이 아니라 `S` 가 답한다**)\n";
                    if (!okG3) bad++;

                    bool okG4 = (js.find("\"name\":\"X1\",\"idx\":11,\"value\":false,\"known\":true")
                                 != std::string::npos);
                    std::cout << (okG4 ? "  ✓ " : "  ✗ ") << "비트 11 이 0 → X1 value=false"
                              << "(비트 순서가 뒤집히면 여기서 갈린다)\n";
                    if (!okG4) bad++;

                    // 🔴 `result=3` 은 **`ack_timeout` 과 다른 칸**이다
                    long long d0 = t.dev_reject;
                    uint16_t rid3 = 0;
                    for (std::map<uint16_t, Pending>::iterator it = t.pend.begin();
                         it != t.pend.end(); ++it) if (it->second.kind == 'G') rid3 = it->first;
                    if (rid3) {
                        char ab[64];
                        snprintf(ab, sizeof(ab), "A,%u,G0,3,", rid3);
                        t.on_ard_line(t_line(ab));
                    }
                    bool okG5 = (t.dev_reject == d0 + 1);
                    std::cout << (okG5 ? "  ✓ " : "  ✗ ") << "result=3 → 장치거절 계수 "
                              << (t.dev_reject - d0) << " (기대 1 · ACK실패와 다른 칸)\n";
                    if (!okG5) bad++;

                    // ㉚-마 🔴 **화면 소켓을 실제로 물린다** — 위 검사들은 전부 `ws_fd = BAD_SOCK` 이라
                    //   **`send_ack` 자체를 건너뛰었다.** 그래서 `kind=='G'` 문구가 한 번도 안 돌았고
                    //   **차단봉을 열었는데 "예약되었습니다" 가 나가는 것을 아무도 못 봤다.**
                    //   ⚠ 그리고 `enqueue_down` 이 `ws_fd == BAD_SOCK` 을 **중요(거절 금지)** 로 분류하므로
                    //     실기(화면 조작 = 사용자 계열)와 **다른 갈래를 밟고 있었다.**
                    {
                        sock_t ws[2];
                        if (socketpair(AF_UNIX, SOCK_STREAM, 0, ws) == 0) {
                            t.conns[ws[0]].kind = Conn::WS;
                            // 🔴 **먼저 떠 있는 `G` 를 닫는다.** 안 그러면 `zone_block_reason` 이
                            //   `pending` 으로 새 요청을 거절한다 — **그게 옳은 거동이고**,
                            //   시험이 그것을 모르고 짜여 있었다(처음에 여기서 실패했다).
                            //   🔑 **같은 자리에 조작을 겹치지 않는 것**이 설계다. 시험이 설계를 따라야 한다.
                            for (std::map<uint16_t, Pending>::iterator it = t.pend.begin();
                                 it != t.pend.end(); ) {
                                if (it->second.kind == 'G') { char ab0[64];
                                    snprintf(ab0, sizeof(ab0), "A,%u,G0,0,", it->first);
                                    uint16_t rr = it->first; ++it; t.on_ard_line(t_line(ab0)); (void)rr; }
                                else ++it;
                            }
                            t.on_ws_message(ws[0], "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"g9\"}");
                            t.on_ard_line(t_line("S,5,002,000,9,P1,"));
                            uint16_t rg = 0;
                            for (std::map<uint16_t, Pending>::iterator it = t.pend.begin();
                                 it != t.pend.end(); ++it)
                                if (it->second.kind == 'G') rg = it->first;
                            char wsb[1024]; while (recv(ws[1], wsb, sizeof(wsb), MSG_DONTWAIT) > 0) {}
                            if (rg) { char ab[64]; snprintf(ab, sizeof(ab), "A,%u,G0,0,", rg);
                                      t.on_ard_line(t_line(ab)); }
                            std::string wsout;
                            for (;;) { int r = (int)recv(ws[1], wsb, sizeof(wsb), MSG_DONTWAIT);
                                       if (r <= 0) break; wsout.append(wsb, (size_t)r); }
                            bool okG6 = (wsout.find("차단봉을 열었습니다") != std::string::npos);
                            std::cout << (okG6 ? "  ✓ " : "  ✗ ") << "화면 ACK 문구가 `차단봉을 열었습니다`"
                                      << " (**예전엔 `예약되었습니다` 가 나갔다**)\n";
                            if (!okG6) bad++;
                            t.conns.clear();
                            closesock(ws[0]); closesock(ws[1]);
                        }
                    }

                    // ㉚-바 🔴 **완료 판정은 대조다** — "에코가 있다"가 아니라 "요청한 값과 같다"
                    //   E1 은 열라고 했고 비트 10 이 1 이다 → settled
                    //   X1 은 닫으라고 했고 비트 11 이 **1 이면** → 🔴 mismatch (장치가 안 했다)
                    t.on_ard_line(t_line("S,6,003,000,10,P1,"));   // 비트 1·0 = 모듈 10·11 둘 다 1
                    {
                        std::string js2 = t.state_json();
                        size_t px = js2.find("\"id\":\"X1\"");
                        std::string xseg = (px == std::string::npos) ? "" : js2.substr(px, 400);
                        bool okG7 = (xseg.find("\"completion\":\"mismatch\"") != std::string::npos);
                        std::cout << (okG7 ? "  ✓ " : "  ✗ ") << "닫으라 했는데 열려 있다 → "
                                  << "**completion=mismatch** (`settled` 로 답하면 그게 거짓 완료다)\n";
                        if (!okG7) bad++;

                        size_t pe = js2.find("\"id\":\"E1\"");
                        std::string eseg = (pe == std::string::npos) ? "" : js2.substr(pe, 400);
                        bool okG8 = (eseg.find("\"completion\":\"settled\"") != std::string::npos);
                        std::cout << (okG8 ? "  ✓ " : "  ✗ ") << "열라 했고 열려 있다 → completion=settled\n";
                        if (!okG8) bad++;
                    }

                    // ㉚-사 🔴 **세션이 끊기면 모듈 값을 모른다** — 낡은 값을 사실로 주장하지 않는다
                    t.end_ard_session("selftest 세션종료");
                    {
                        std::string js3 = t.state_json();
                        bool okG9 = (js3.find("\"name\":\"E1\",\"idx\":10,\"value\":null,\"known\":false")
                                     != std::string::npos);
                        std::cout << (okG9 ? "  ✓ " : "  ✗ ") << "세션 종료 뒤 모듈 값이 known=false"
                                  << " (**낡은 값을 known=true 로 주장하지 않는다**)\n";
                        if (!okG9) bad++;
                    }

                    // ㉚-아 🔴 **계수 넷의 불변식** — 하나가 안 오르면 여기서 걸린다
                    //   띄움 ≥ 도달 ≥ 응답 ≥ 거절.  각 화살표가 **다른 고장**이다:
                    //     띄움−도달 = 큐에서 죽음 · 도달−응답 = 나갔는데 무응답 · 응답−거절 = 성공
                    //   ⚠ **선언만 하고 안 올리는 계수**를 이 불변식이 잡는다 —
                    //     오늘 `mod_order_changed` 를 요약에 안 내보내 monitor 에게 없는 칸을 보라고 했고,
                    //     `gate_sent` 도 선언만 하고 안 올릴 뻔했다. **같은 결함을 두 번 하지 않으려고 넣는다.**
                    bool okC = (t.gate_q >= t.gate_sent && t.gate_sent >= t.gate_ans
                                && t.gate_ans >= t.dev_reject
                                && t.gate_q > 0 && t.gate_sent > 0 && t.gate_ans > 0);
                    std::cout << (okC ? "  ✓ " : "  ✗ ") << "G 계수 불변식 — 띄움 " << t.gate_q
                              << " ≥ 도달 " << t.gate_sent << " ≥ 응답 " << t.gate_ans
                              << " ≥ 거절 " << t.dev_reject << " (전부 >0)\n";
                    if (!okC) bad++;

                    t.ard = BAD_SOCK;
                    closesock(gs[0]); closesock(gs[1]);
                }
            }

            // ㉛ 🔴 **명세 §7.4 가 약속한 "재등록 앞부분 대조"가 실제로 도는가**
            //    ⚠ 적어 놓고 안 만들면 **지켜지는 것처럼 보인다.** 그래서 시험이 이 자리에 있다.
            {
                sock_t os_[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, os_) == 0) {
                    Server t; t.build_default_zones(); t.init_srv_id();
                    t.ard = os_[0]; t.ard_seen = true; t.ard_last_ms = now_ms();
                    t.on_ard_line(t_line("D,*,7,12,"));
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OBV,"));
                    t.on_ard_line(t_line("D,X1,OBV,"));
                    t.on_ard_line(t_line("S,1,000,000,5,P1,"));
                    t.on_ws_message(BAD_SOCK, "{\"type\":\"open_gate\",\"slot\":\"E1\",\"rid\":\"z1\"}");
                    size_t pend0 = t.pend.size();

                    // 🔴 **중간에 끼워 넣은 재등록** — A1 앞에 새 모듈이 들어와 전부 밀린다
                    long long c0 = t.mod_order_changed;
                    t.on_ard_line(t_line("D,*,7,13,"));
                    t.on_ard_line(t_line("D,Z9,IP,"));                 // ← 끼어든 것
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OBV,"));
                    t.on_ard_line(t_line("D,X1,OBV,"));

                    bool okZ1 = (t.mod_order_changed == c0 + 1);
                    std::cout << (okZ1 ? "  ✓ " : "  ✗ ") << "중간 삽입 재등록을 잡는다 (순서변경 "
                              << (t.mod_order_changed - c0) << " · 기대 1)\n";
                    if (!okZ1) bad++;

                    bool okZ2 = (pend0 > 0 && t.pend.empty());
                    std::cout << (okZ2 ? "  ✓ " : "  ✗ ") << "떠 있던 `G` 를 버린다 (전 "
                              << pend0 << " → 후 " << t.pend.size()
                              << " · **그 idx 는 이제 다른 모듈이다**)\n";
                    if (!okZ2) bad++;

                    // 끝에만 붙이는 재등록은 **안 걸려야 한다** — 안 그러면 정상 확장을 막는다
                    long long c1 = t.mod_order_changed;
                    t.on_ard_line(t_line("D,*,7,13,"));
                    t.on_ard_line(t_line("D,Z9,IP,"));
                    for (int i = 0; i < 10; i++)
                        t.on_ard_line(t_line(std::string("D,") + SLOT_ID[i] + ",IP,"));
                    t.on_ard_line(t_line("D,E1,OBV,"));
                    t.on_ard_line(t_line("D,X1,OBV,"));
                    bool okZ3 = (t.mod_order_changed == c1);
                    std::cout << (okZ3 ? "  ✓ " : "  ✗ ") << "같은 순서 재등록은 안 걸린다 (거짓 경보 "
                              << (t.mod_order_changed - c1) << " · 기대 0)\n";
                    if (!okZ3) bad++;

                    t.ard = BAD_SOCK;
                    closesock(os_[0]); closesock(os_[1]);
                }
            }

            // ㉞ 🔴 **`known:false` 의 사유가 갈린다** (명세 §8.10)
            //    ⚠ `ws_probe` 는 메시지를 잘라 찍어 `state` 전문을 못 읽는다 —
            //      그래서 **여기서 `state_json()` 문자열을 직접 본다.** 도구 한계를 시험으로 메운다.
            {
                Server t; t.build_default_zones(); t.init_srv_id();
                // (가) 주 노드가 등록도 안 된 상태 → 그 자리 모듈이 아예 없다(모듈 배열이 빈다)
                std::string j0 = t.state_json();
                bool okA = (j0.find("\"value_state\":\"unknown\"") != std::string::npos);

                // (나) 보조 노드가 등록만 됐다 → **값 경로가 없다** = bits_unavailable
                Node& aux2 = t.aux["P9"]; aux2.devid = "P9"; aux2.online = true;
                aux2.mods.push_back(std::make_pair(std::string("A2"), std::string("IP")));
                t.bind_modules(aux2);
                std::string j1 = t.state_json();
                bool okB = (j1.find("\"reason\":\"bits_unavailable\"") != std::string::npos);

                // (다) 그 보조 노드가 오프라인이면 사유가 바뀐다
                aux2.online = false;
                std::string j2 = t.state_json();
                bool okC = (j2.find("\"reason\":\"node_offline\"") != std::string::npos);

                bool ok34 = okA && okB && okC;
                std::cout << (ok34 ? "  ✓ " : "  ✗ ") << "known:false 사유가 갈린다 — "
                          << "미등록 unknown(" << (okA ? "예" : "🔴아니오") << ") · "
                          << "보조노드 bits_unavailable(" << (okB ? "예" : "🔴아니오") << ") · "
                          << "오프라인 node_offline(" << (okC ? "예" : "🔴아니오") << ")\n";
                if (!ok34) bad++;
                t.aux.clear();
            }

            // ㉝ 🔴 **`B*` 는 예약 대상이 아니다** (사용자 확정 (A) · 명세 §9.3)
            //    ⚠ `slot_index("B5")` 는 여전히 5 를 준다 — 그래서 **막지 않으면 예약이 성공한다.**
            //      성공했는데 화면에 안 보이는 것이 이 변경에서 가장 나쁜 결말이다.
            {
                LoopPair lb; Server t; t.build_default_zones(); t.init_srv_id();
                t.conns[lb.a].kind = Conn::WS;
                t.park.devid = "P1"; t.park.fd = lb.a; t.park.seen = true; t.park.last_ms = now_ms();
                t.on_ws_message(lb.a, "{\"type\":\"reserve\",\"slot\":\"B5\",\"rid\":\"b1\",\"user_id\":\"00000000\"}");
                t.on_ws_message(lb.a, "{\"type\":\"reserve\",\"slot\":\"A3\",\"rid\":\"b2\",\"user_id\":\"00000000\"}");
                char bb[4096]; std::string gb; int idleb = 0;
                for (int tries = 0; tries < 4000 && idleb < 200; tries++) {
                    int n = (int)recv(lb.b, bb, sizeof(bb), MSG_DONTWAIT);
                    if (n > 0) { gb.append(bb, n); idleb = 0; } else idleb++;
                }
                // 🔑 **B5 는 거절되고 A3 은 안 거절돼야 한다.** 둘 다 봐야 "전부 막혔다"와 갈린다.
                const bool okB = (gb.find("not_reservable") != std::string::npos)
                                 && (t.not_reservable_n == 1)
                                 && (gb.find("\"rid\":\"b2\",\"slot\":\"A3\"") != std::string::npos
                                     || gb.find("queued") != std::string::npos
                                     || t.pend.size() >= 1);
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "B5 예약은 not_reservable 로 거절 · A3 은 통과"
                          << " (비자리예약 " << t.not_reservable_n << " · 기대 1)\n";
                if (!okB) bad++;
                t.park.fd = BAD_SOCK; t.conns.clear();
            }

            // ㉜ 🔴 **A[1] `rid` 폭 고정과 격리** — 정본 `docs/net/DESIGN-rid-width-and-quarantine.md`
            //
            // 🔴 **분모를 먼저 적는다.** 아래 검사가 **밟지 못하는 것**:
            //   · 장치 멱등 캐시의 실제 삼킴 — 실기 장치가 있어야 한다. 여기서는 못 만든다
            //   · 늦은 ACK 의 실제 최대 지연 — 격리 값(§3.1)은 **가정이고 잰 적이 없다**
            //   · 재시작 충돌(§4) — 확률 1.6% 사건이라 시험으로 재현할 수 없다
            //   **그러므로 이 항목이 전부 ✓ 라도 "안전이 증명됐다"가 아니다.**
            {
                // (a)(d) 2000회 발행 — **폭 상한**과 **재사용 간격**을 같이 본다.
                //   🔑 (d)가 핵심이다: 폭을 줄이면 재사용 주기가 짧아지고, 그것이
                //   장치 멱등창(16) 안에 들어가면 **명령이 조용히 삼켜진다**(arduino §25.3).
                Server t;
                std::vector<int> last_at(RID_SPACE, -1);
                int minDist = 1 << 30; bool inRange = true; size_t maxDigits = 0;
                for (int i = 0; i < 2000; i++) {
                    uint16_t r = t.alloc_rid();
                    if (r == RID_NONE || r >= RID_SPACE) { inRange = false; break; }
                    size_t dg = std::to_string((unsigned)r).size();
                    if (dg > maxDigits) maxDigits = dg;
                    if (last_at[r] >= 0 && i - last_at[r] < minDist) minDist = i - last_at[r];
                    last_at[r] = i;
                    t.rid_release(r);          // 곧바로 해제 → 격리에 들어간다
                }
                bool okA = inRange && maxDigits <= 3 && minDist >= DEV_RID_CACHE_N;
                std::cout << (okA ? "  ✓ " : "  ✗ ") << "rid 폭 ≤3자리(실측 " << maxDigits
                          << ") · 최소 재사용 간격 " << minDist
                          << " ≥ 장치 멱등창 " << DEV_RID_CACHE_N << "\n";
                if (!okA) bad++;
            }
            {
                // (b) 🔴 **하드 규칙 — `pend` 에 있는 rid 는 절대 발행하지 않는다.**
                //     999칸을 `pend` 로 막고 한 칸만 비워 둔다. 그 칸이 나와야 한다.
                Server t;
                for (uint16_t r = 0; r < RID_SPACE; r++) if (r != 500) t.pend[r] = Pending();
                uint16_t got = t.alloc_rid();
                bool okB = (got == 500 && t.rid_skips > 0 && t.rid_forced == 0);
                std::cout << (okB ? "  ✓ " : "  ✗ ") << "pend 를 피해 유일한 빈 칸을 고른다 (got "
                          << got << " · 기대 500 · 건너뜀 " << t.rid_skips << ")\n";
                if (!okB) bad++;
                t.pend.clear();
            }
            {
                // (c) 격리 시간 안에는 안 나온다 — **한 칸만 시간이 지난 상태로 둔다.**
                Server t;
                for (uint16_t r = 0; r < RID_SPACE; r++) t.rid_release(r);
                t.rid_quar[321].until_ms = now_ms() - 1;        // 이 칸만 격리가 풀렸다
                uint16_t got = t.alloc_rid();
                bool okC = (got == 321 && t.rid_forced == 0);
                std::cout << (okC ? "  ✓ " : "  ✗ ") << "격리 중인 rid 를 건너뛴다 (got " << got
                          << " · 기대 321 · 강제 " << t.rid_forced << " 기대 0)\n";
                if (!okC) bad++;
            }
            {
                // (e) 전부 격리 중이면 **가장 오래 묵은 것**을 강제 해제한다(§3.3).
                //     ⚠ 거절 경로를 새로 만들지 않는다 — 화면 계약에 축이 늘면 이 배포의 측정이 흐려진다.
                //     🔴 **순번 FIFO 로 고른다**(시각이 아니다 — 같은 밀리초 동률이 순서를 무너뜨린다).
                //        654 를 **가장 먼저** 해제해 두면 나머지 999개가 뒤에 쌓여도 그것이 나와야 한다.
                Server t;
                t.rid_release(654);                            // 가장 먼저 해제 = 순번 1
                for (uint16_t r = 0; r < RID_SPACE; r++) if (r != 654) t.rid_release(r);
                uint16_t got = t.alloc_rid();
                bool okE = (got == 654 && t.rid_forced == 1);
                std::cout << (okE ? "  ✓ " : "  ✗ ") << "공간이 차면 **가장 먼저 해제된** 격리를 내준다 (got "
                          << got << " · 기대 654 · 강제 " << t.rid_forced << ")\n";
                if (!okE) bad++;
            }
            {
                // (f) 🔴 **실기 호출 지점이 `alloc_rid()` 를 타는가.**
                //     ⚠ 위 (a)~(e)는 `alloc_rid()` 를 직접 부른다 — **네 발행 지점 중 하나가
                //     옛 `next_rid++` 로 남아 있어도 전부 통과한다.** 그 구멍을 여기서 막는다.
                //     (CLAUDE.md §"자기 시험의 분모를 아는 것은 자기뿐이다")
                Server t; t.ard = BAD_SOCK;
                long long n0 = t.rid_alloc_n;
                t.dispatch_sim(BAD_SOCK, "selftest-M");
                bool okF = (t.rid_alloc_n == n0 + 1);
                std::cout << (okF ? "  ✓ " : "  ✗ ") << "dispatch_sim 이 alloc_rid 를 탄다 (발행 "
                          << (t.rid_alloc_n - n0) << " · 기대 1)\n";
                if (!okF) bad++;
            }
            {
                // (g) 🔴 **커서 영속은 기본이 꺼져 있다 — 시험이 실기 커서를 덮어쓰지 못하게.**
                //     ⚠ 이 검사가 지키는 것은 서버의 성질이 아니라 **자가검증의 안전**이다.
                //     여기가 켜져 있으면 위 (a)의 2000회 발행이 운영 커서를 앞으로 밀어 버린다.
                //     🔑 그리고 **예약값은 커서보다 항상 앞서 있어야 한다** — 그게 (B)의 전부다.
                //        뒤에 있으면 크래시 뒤에 **방금 쓴 값을 다시 내준다.**
                Server t;
                bool offByDefault = (t.rid_persist_on == false);
                for (int i = 0; i < 700; i++) t.alloc_rid();
                bool ahead = (t.rid_reserved_to >= t.rid_cursor);
                bool okG = offByDefault && ahead;
                std::cout << (okG ? "  ✓ " : "  ✗ ") << "커서 영속 기본 꺼짐(" << (offByDefault ? "예" : "🔴아니오")
                          << ") · 예약 " << t.rid_reserved_to << " ≥ 커서 " << t.rid_cursor << "\n";
                if (!okG) bad++;
            }

            s.ard = BAD_SOCK;              // 소멸자가 이 fd 를 건드리지 않게
            closesock(sv[0]); closesock(sv[1]);
        }
    }
#endif

    std::cout << (bad ? "자가검증 실패\n" : "자가검증 통과\n");
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2,2), &w) != 0) { std::cerr << "Winsock 초기화 실패\n"; return 1; }
#else
    signal(SIGPIPE, SIG_IGN);   // 끊긴 소켓에 write 해도 프로세스가 죽지 않게
#endif
    // 소크 시험은 Ctrl-C 로 끝난다 — 그때 요약을 남기고 정상 종료한다(REQ-0065)
    signal(SIGINT,  on_stop_signal);
    signal(SIGTERM, on_stop_signal);
    int rc;
    // 로그 경로(REQ-0111 로그 계약 §2.4) — 비워 두면 기본 경로를 쓴다.
    // **셸 리다이렉션에 맡기지 않는 이유**: 08-16 에 나중 뜬 인스턴스가 다른 곳에 쓰는 바람에
    // 관측이 조용히 끊겼다. 어디에 쓰는지는 서버가 정하고, 경계 줄에 log= 로 적어 둔다.
    std::string log_path;
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        if (a.compare(0, 6, "--log=") == 0) log_path = a.substr(6);
        // 🔴 **계측기를 넓히는 수단이지 명세 변경이 아니다**(§2.1 은 64 그대로).
        // `AT+CIPSEND` 최대 길이 시험에서만 쓴다 — 서버가 64B 초과를 **체크섬 검사 전에**
        // 버리면 **AT 잘림이 아니라 이 상수를 재게 된다.**
        if (a.compare(0, 11, "--max-line=") == 0) {
            int v = atoi(a.substr(11).c_str());
            if (v >= 64 && v <= 1024) {
                MAX_LINE = v;
                std::cout << "*** --max-line=" << v << " — **시험용**이다. 명세 §2.1 은 64 다 ***\n";
            } else {
                std::cerr << "--max-line 은 64~1024 여야 한다 (받은 값: " << a.substr(11) << ")\n";
                return 2;
            }
        }
        // 🔴 **임시값을 갈아끼우는 손잡이.** arduino 의 `rxmax`(바이트)가 나오면 이걸로 넣는다 —
        // 재빌드가 필요하면 "나중에 하자"가 되고, 그러면 임시값이 영구값이 된다.
        if (a.compare(0, 11, "--down-cap=") == 0) {
            int v = atoi(a.substr(11).c_str());
            if (v >= 32 && v <= 4096) {
                DOWN_BATCH_CAP_B = v;
                std::cout << "*** --down-cap=" << v << "B — 하행 배치 상한(창당). "
                          << "큐 깊이는 " << (v * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS))
                          << "B 로 같이 움직인다 ***\n";
            } else {
                std::cerr << "--down-cap 은 32~4096 이어야 한다 (받은 값: " << a.substr(11) << ")\n";
                return 2;
            }
        }
        // 🔴 옛 거동(이벤트 시점에 즉시 송신). **착지 위상을 겨냥하는 시험 전용**이다 —
        // 원장 §8.17 의 미해결 물음이 이 수단 없이는 영영 안 갈린다.
        if (a == "--down-immediate") {
            DOWN_IMMEDIATE = true;
            std::cout << "*** --down-immediate — 하행이 **창을 무시하고 즉시** 나간다. "
                      << "슬롯 규율이 꺼진 상태다(시험 전용) ***\n";
        }
    }
    // 시험용 포트 이동(REQ-0072) — 운영 인스턴스를 안 죽이고 두 번째를 띄우기 위한 이음매
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        if (a.compare(0, 14, "--port-offset=") != 0) continue;
        int off = atoi(a.c_str() + 14);
        // ⚠ **범위를 안 보면 조용히 엉뚱한 포트에 붙는다.** 음수면 0번 포트로, 큰 값이면
        // htons 에서 잘려 아무 포트로 간다 — 그리고 그건 나중에 "서버가 이상하다"로 보고된다.
        // 시험용 이음매가 그런 식으로 사람을 속이면 안 되므로 **의심스러우면 안 뜬다.**
        if (off <= 0 || PORT_ARDUINO + off > 65535) {
            std::cerr << "--port-offset 은 1 ~ " << (65535 - PORT_ARDUINO)
                      << " 사이여야 한다 (받은 값: " << off << ")\n";
            return 1;
        }
        g_port_offset = off;
    }
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        if (a.compare(0, 11, "--park-dev=") != 0) continue;
        g_park_dev_pin = a.substr(11);
        if (g_park_dev_pin.empty()) {
            std::cerr << "--park-dev= 에 devid 를 줘라 (예: --park-dev=P1A)\n";
            return 1;
        }
    }
    // --selftest 는 로그 파일을 열지 않는다. 자가검증 출력이 운영 로그에 섞이면
    // 인스턴스 경계 없이 사람이 만든 줄이 끼어드는 셈이라, 계약이 지키려는 것을 스스로 깬다.
    if (argc > 1 && std::string(argv[1]) == "--selftest") rc = selftest();
    else {
        open_log(log_path);
        Server s;
        rc = s.run();
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return rc;
}
