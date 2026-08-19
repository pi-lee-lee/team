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
    // ─── 목차 ─────────────────────────────────────────────────────────
    //  🔑 **이 구조체는 29줄이고 그중 대부분이 아래 목차다.**
    //    각 줄은 `struct Server` 의 **몸통 조각**이고 그 자리에 펼쳐진다.
    //    6,321줄을 이 형태로 옮겼고 매 단계 **`.o` 가 바이트 동일**했다(REQ-0272).
    //  🔴 **순서가 문법이다** — 뒤엣것이 앞엣것을 쓴다. 함부로 바꾸면 컴파일이 깨진다.
    //  🔒 표시는 `.claude/protected.json` 잠금 — 장치 쪽과 한 계약의 양끝이다.
    // ──────────────────────────────────────────────────────────────────
#include "state.h"         // 무엇을 기억하는가 — 필드·계수기·중첩 타입 전부
#include "metrics.h"       // 소크 관측 · 복구 계측 · 지표 문장 (REQ-0065/0072)
#include "nodes.h"         // 다중 노드 — 등록·승격·결속·라우팅 (REQ-0083)
#include "wire.h"          // 송신 helper · WebSocket 프레임 (§5.2)
#include "wsjson.h"        // 봉투 만들기 — map / state / snapshot JSON
#include "seam.h"          // 이음매: 디바이스 → 도메인 (REQ-0096 C)
#include "persist.h"       // data_log.json 읽기·쓰기 (§9)
#include "downlink.h"      // 🔒 하행 — 발행·재전송·회수 (전선 계약. 잠금)
#include "uplink.h"        // 🔒 상행 — 장치 프레임 파서 (전선 계약. 잠금)
#include "wsapi.h"         // 브라우저 → 서버 — WS 명령 수신 (§5.4)
#include "http.h"          // HTTP 요청 · WS 업그레이드 · 정적 파일
#include "listen.h"        // 포트 열고 닫기 — openPorts / closeDown
#include "serve.h"         // 한 박자 — serveOneTick (수신→판정→하행→방송)
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



