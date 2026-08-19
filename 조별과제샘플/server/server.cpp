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
#include "server_seam.h"
#include "parking.h"     // 🔴 **공개 조립 API** — 사용 코드가 읽는 유일한 헤더
#include "ridpool.h"     // rid 발행·격리·영속 — 전선에 안 닿는 축
// ⚠ `lot.h` 는 `Zone`·`ParkingLot` 을 쓰므로 **그 뒤에** include 한다(아래 zone.h 다음).     // 이음매 계약 (REQ-0096 B→C): DeviceEvent / DeviceCommand

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

// 🔴 상대경로를 **해석된 절대경로**로 바꾼다 (REQ-0248 · 2026-08-19)
//
//   `serve_file()` 과 `data_log.json` 이 cwd 상대라 **같은 바이너리가 어디서 떴느냐에 따라
//   다른 실체를 읽고 쓴다.** 오늘 그것 때문에 반나절이 들었다 — 로그에 "무엇을 열었나"가 없었다.
//   ⚠ 파일이 없어도 **경로는 답할 수 있어야 한다**(그게 곧 "왜 404 인가"의 답이다).
//     그래서 `realpath` 로 풀리지 않으면 **cwd 를 붙여서라도** 절대경로를 만든다.
static std::string abs_path(const std::string& rel) {
    if (!rel.empty() && rel[0] == '/') return rel;
#ifndef _WIN32
    char rp[1024];
    if (realpath(rel.c_str(), rp)) return std::string(rp);
#endif
    std::string cw = cur_cwd();
    if (cw.empty() || cw == "?") return rel;
    return cw + "/" + rel;
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

// ── 🔴 `RidPool` 의 본문 — 로그·디렉터리 도우미가 여기 있으므로 여기에 둔다 (REQ-0272 3단계)
void RidPool::reserveBlock() {
    reserved_to_ = cursor_ + RID_PERSIST_BLOCK;
    if (!persist_on_ || file_.empty() || no_disk_) return;
    ensure_parent_dir(file_);
    std::ofstream o(file_.c_str(), std::ios::out | std::ios::trunc);
    if (!o) {
        persist_on_ = false;   // 🔴 한 번 실패하면 끈다. 매 발행마다 실패 로그를 쏟지 않는다
        logf("!", "🔴 rid 커서를 못 쓴다 — " + file_
                  + " · **재시작 보호가 꺼졌다.** 다음 재시작이 장치 멱등 캐시와 겹칠 수 있다");
        return;
    }
    o << reserved_to_ << "\n";
}

void RidPool::loadCursor(const std::string& path, long long seed_when_missing, bool no_disk) {
    file_ = path;
    no_disk_ = no_disk;
    if (file_.empty()) {
        persist_on_ = false;
        cursor_ = seed_when_missing % RID_SPACE;      // 🔴 1 에서 시작하지 않는다
        reserved_to_ = cursor_;
        logf("!", "🔴 rid 커서를 영속할 수 없다 — 경로가 없다. 임의 지점 "
                  + std::to_string(cursor_ % RID_SPACE)
                  + " 에서 시작한다. **이 기동은 장치 멱등 캐시와 겹칠 수 있다**");
        return;
    }
    persist_on_ = true;
    std::ifstream f(file_.c_str());
    long long v = -1;
    if (f && (f >> v) && v >= 0) {
        cursor_ = v; reserved_to_ = v;
        reserveBlock();          // 기동 때 곧바로 적는다 — 하행이 없던 기동에서도 파일이 생긴다
        logf("=", "rid 커서 이어받음 — " + std::to_string(v)
                  + " (전선값 " + std::to_string(v % RID_SPACE) + ") · 예약 "
                  + std::to_string(reserved_to_) + " · " + file_);
        return;
    }
    cursor_ = seed_when_missing % RID_SPACE;
    reserved_to_ = cursor_;
    reserveBlock();
    logf("!", "🔴 rid 커서 파일을 못 읽었다(" + file_
              + ") — 임의 지점 " + std::to_string(cursor_ % RID_SPACE)
              + " 에서 시작한다. ⚠ **이 기동만 장치 멱등 캐시와 겹칠 수 있다(약 "
              + std::to_string(DEV_RID_CACHE_N) + "/" + std::to_string((int)RID_SPACE)
              + "). 정상 기동과 구별해서 읽어라**");
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
#include "node.h"
#include "zone.h"
#include "lot.h"         // 🔴 주차장 지형이 스스로 답한다 — 전선에 안 닿는 축
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
    // 🔴 **지형과 판은 `Lot` 의 것이다**(REQ-0272 3단계). 여기서는 그것을 들고만 있다.
    //   🔑 **판(`epoch`)은 지형을 바꾸는 함수 안에서 그 줄 옆에서 올린다**(설계 §6.8).
    //     떨어뜨리면 **"바꿨는데 안 올린 경로"** 가 생기고, 그건 **탐지 장치가 있는데 안 울리는 것**이라
    //     아예 없는 것보다 나쁘다. → `bump_epoch()` 하나만 그 일을 한다.
    Lot lot;
    std::string last_screen_build_;   // 🔑 같은 판본을 반복해 찍지 않는다(요청마다 오면 로그가 덮인다)
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

#include "metrics.h"
#include "nodes.h"
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

#include "wsjson.h"
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

#include "persist.h"
#include "downlink.h"

#include "uplink.h"

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

#include "wsapi.h"
#include "http.h"
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

    // ── 🔴 `run()` 을 셋으로 갈랐다 (REQ-0272 1단계 · 2026-08-19)
    //   호출자가 **바꿀 수 있는 것**(언제 열지 · 어떻게 돌지)을 밖으로 낸다.
    //   한 박자 **안**의 순서는 못 바꾸므로 안에 남는다 — 우리 판별자 그대로다.
    //   ⚠ 기계적 분할이다. 루프 몸통에 최상위 `continue`/`break`/`return` 이 **하나도 없는 것**을
    //     먼저 확인하고 옮겼다 — 있으면 의미가 조용히 바뀐다.
    bool openPorts() {
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
            // 🔴 **정적 자원·영속 상태의 해석된 절대경로를 찍는다** (REQ-0248 · 2026-08-19)
            //
            //   `serve_file()`(index.html)과 `data_log.json` 이 **cwd 상대**다.
            //   2026-08-19 에 그 때문에 `:9900` 이 이틀 된 화면을 내줬고 **로그에 흔적이 없어**
            //   "무엇을 열고 있나"를 사후에 못 물었다 — 반나절이 들었다.
            //   🔑 `cwd=` 는 경계 줄에 이미 찍는다. 그런데 **cwd 를 안다고 경로를 아는 것은 아니다** —
            //     읽는 사람이 머릿속에서 이어 붙여야 한다. **서버가 이어 붙여서 찍는다.**
            //   ⚠ 파일이 없어도 경로는 찍는다 — 그게 곧 "왜 404 인가"의 답이다.
            {
                char pb[512];
                snprintf(pb, sizeof(pb), "정적 자원 — index.html %s%s · data_log.json %s",
                         abs_path("index.html").c_str(),
                         std::ifstream("index.html").good() ? "" : " 🔴(없다 — GET / 는 404 다)",
                         abs_path("data_log.json").c_str());
                logf("=", pb);
            }
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

        return true;
    }

    // 한 박자 : 수신 → 자리 판정 → 하행 송신 → 화면 방송. **false 면 그만 돈다**
    //
    // 🔴 **여기에 코드를 더할 때 알아야 할 것** (REQ-0272 · 2026-08-19)
    //   이 본문은 원래 `while (!g_stop) { … }` 의 몸통이었다. 기계적으로 옮겼고,
    //   옮기기 전에 **최상위 `continue`/`break`/`return` 이 하나도 없는 것**을 확인했다.
    //   그 확인이 이 분할의 안전망 전부였다.
    //
    //   ✅ **그런데 옮긴 뒤로 둘은 *구조적으로* 막혔다** — 실험으로 확인했다:
    //      최상위 `continue;` → `error: 'continue' statement not in loop statement` (컴파일 실패)
    //      `break;` 도 같다. **루프가 아니므로 컴파일러가 잡는다.**
    //
    //   🔴 **남는 위험은 `return` 하나다.** 최상위에 `return false;` 를 쓰면 **컴파일된다.**
    //      그리고 그 뜻은 "이 박자를 건너뛴다"가 아니라 **"서버를 멈춘다"** 이다.
    //      ⚠ 옛 코드에서 `continue` 였을 자리에 무심코 `return` 을 쓰면 **서버가 조용히 죽는다.**
    //      → 이 박자를 건너뛰려면 **`return true;`** 다. 멈추는 것은 `g_stop` 이 정한다.
    //
    //   🔴 **`false` 를 내는 경로는 지금 *둘* 이고 둘 다 `g_stop` 이 정한다** (2026-08-19 실측):
    //      ① 들머리 `if (g_stop) return false;`   ② 끝의 `return !g_stop;`
    //      **`return true` 는 0곳이다** — 아직 아무도 박자를 건너뛰지 않는다.
    //      🔑 **이 수가 늘면 신호다.** 셋째 `false` 는 `g_stop` 이 아닌 이유로 서버를 멈춘다는 뜻이고,
    //        그건 **호출자가 모르는 정지 조건**이 생겼다는 것이다. 늘릴 거면 명세 §11.6 에 이유를 적어라.

    bool serveOneTick() {
        if (g_stop) return false;
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
        return !g_stop;
    }

    void closeDown() {

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
    }

    // 옛 진입점 — 셋을 순서대로 부른다. **이 순서가 곧 흐름이다.**
    int run() {
        if (!openPorts()) return 1;
        while (serveOneTick()) { }
        closeDown();
        return 0;
    }
};

// 🔴 자가검증은 `selftest.h` 로 나갔다 (REQ-0272 · 이동만 · `.o` 0 차이로 증명)
//   ⚠ **이 자리를 옮기지 마라** — 전처리 결과가 같아야 그 증명이 성립한다.
#include "selftest.h"

// 🔴 **엔트리 포인트는 `main.cpp` 로 나갔다** (REQ-0272 1단계 · 2026-08-19)
//   원래 자리에서 그대로 `#include` 한다 — **전처리 결과가 같아야 `.o` 0 차이가 성립한다.**
//   ⚠ `#include` 위치를 옮기는 순간 그것은 "분리"가 아니라 "변경"이다. 이 자리를 지켜라.
// ── 🔴 프로세스 초기화 — **호출자가 몰라도 되는 것** (REQ-0272 2단계 · 2026-08-19)
//
//   전에는 `main()` 이 `WSAStartup`·`SIGPIPE`·`SIGINT/TERM` 을 직접 챙겼다.
//   **사용자 요구는 "난이도 높은 설정은 최대한 자동화"** 이고, 이건 그 전형이다 —
//   Winsock 이 무엇인지 몰라도 주차장을 만들 수 있어야 한다.
//
//   🔴 **`ParkingServer` 의 생성자로 옮기지 않았다.** 이유가 있다:
//     `--selftest` 는 `ParkingServer` 를 만들지 않는데 **소켓쌍에 쓰므로 `SIGPIPE` 가 필요하다.**
//     생성자로 옮기면 **자가검증 경로에서 그 보호가 조용히 벗겨진다** — 죽어야 알 수 있는 종류다.
//   ✅ 그래서 **정적 객체**로 둔다. `main` 보다 먼저 돌고 **두 경로를 다 덮는다.**
//   ⚠ 다른 정적에 의존하지 않는다(시그널 등록과 Winsock 시작뿐) — 초기화 순서 문제가 없다.
struct ProcessInit {
    ProcessInit() {
#ifdef _WIN32
        WSADATA w;
        if (WSAStartup(MAKEWORD(2,2), &w) != 0) std::cerr << "Winsock 초기화 실패\n";
#else
        signal(SIGPIPE, SIG_IGN);   // 끊긴 소켓에 write 해도 프로세스가 죽지 않게
#endif
        // 소크 시험은 Ctrl-C 로 끝난다 — 그때 요약을 남기고 정상 종료한다(REQ-0065)
        signal(SIGINT,  on_stop_signal);
        signal(SIGTERM, on_stop_signal);
    }
    ~ProcessInit() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};
static ProcessInit g_process_init;

// ── 🔴 공개 조립 API 구현 (REQ-0272 1단계 · 2026-08-19)
//
//   헤더(`parking.h`)에는 **자료구조가 하나도 안 나온다.** 여기 `Impl` 이 `Server` 를 들고 있고
//   호출자는 그 존재를 모른다 — 그것이 은닉이다.
//   ⏳ 지금은 **얇은 층**이다. `Server` 3,900줄을 쪼개는 것은 3단계이고, 그때 이 층은 안 바뀐다.
//   🔑 그래서 **사용 코드가 먼저 돌고** 내부는 나중에 정리된다.

Spot& Spot::sensor(const std::string& name) {
    lot_->areas_[idx_].sensors.push_back(name);
    return *this;
}

Spot ParkingLot::spot(const std::string& id) {
    for (size_t i = 0; i < areas_.size(); i++)
        if (areas_[i].id == id) return Spot(this, i);      // 같은 자리를 두 번 불러도 안전하다
    Area a; a.id = id; a.kind = "parking";
    areas_.push_back(a);
    return Spot(this, areas_.size() - 1);
}

void ParkingLot::gate(const std::string& id, Gate::Kind kind) {
    for (size_t i = 0; i < areas_.size(); i++)
        if (areas_[i].id == id) { areas_[i].kind = (kind == Gate::IN) ? "entrance" : "exit"; return; }
    Area a; a.id = id; a.kind = (kind == Gate::IN) ? "entrance" : "exit";
    areas_.push_back(a);
}

struct ParkingServer::Impl {
    ParkingLot lot;      // 🔑 **사본을 든다** — 호출자가 뒤에 표를 바꿔도 서버가 안 흔들린다
    Server     srv;
};

ParkingServer::ParkingServer(const ParkingLot& lot) : p_(new Impl) {
    p_->lot = lot;
    p_->srv.lot_ = &p_->lot;
}
ParkingServer::~ParkingServer() { delete p_; }

bool ParkingServer::openPorts()    { return p_->srv.openPorts(); }
bool ParkingServer::serveOneTick() { return p_->srv.serveOneTick(); }
void ParkingServer::closeDown()    { p_->srv.closeDown(); }

#include "main.cpp"



