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
static const int  ACK_TIMEOUT_MS = 1500;    // §7.3
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
// 🔑 **그리고 도메인이 이미 상한을 준다 — 자리가 10개뿐이다.**
//   최악의 하행 버스트는 "예약 전부 재하달" = 10 × 28B = **280B** 다. 그 위로는 실익이 없다.
//   → `320B` = 도메인 최악(280) + 여유, 실측 상한(540)의 59%. **크게 잡을 이유가 없는 쪽으로 잡는다.**
//   넘치면 버리는 게 아니라 **다음 창으로 미룬다** — 상한을 보수적으로 잡는 비용이 낮다.
// → 실기(`SoftwareSerial`) 재측정이 오면 `--down-cap=<B>` 로 재빌드 없이 갈아끼운다.
static int        DOWN_BATCH_CAP_B   = 320;
// 큐 깊이는 **독립 상수가 아니라 유도값이다** — "마감 안에 나갈 수 있는 양".
//   깊이(B) = cap(B/창) × (마감 ÷ 슬롯주기) = 576 × 4 = 2304
// 그래서 `cap` 이 바뀌면 깊이가 저절로 따라온다. 두 값이 따로 놀면 마감을 넘긴 항목이
// 큐에 남는데 아무도 그걸 설명 못 한다.
static const int  DOWNQ_WAIT_CAP_MS  = 4 * DOWN_SLOT_MS;   // 4.8초 — 큐 대기 마감(임시)
// 🔴 착지 위상을 **의도적으로 겨냥하는 시험**을 위해 옛 거동(즉시 송신)을 남긴다.
// 원장 §8.17 이 아직 못 갈랐다고 적어 둔 물음("`R`/`C` 라서 안전한가, 착지가 좋았던 것인가")은
// 하행을 원하는 순간에 쏠 수단이 없으면 **영영 못 가른다.** 기본값은 off 다.
static bool       DOWN_IMMEDIATE     = false;
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
struct AuxNode {
    sock_t fd;
    std::string buf;
    long long connected_ms;      // 접속 시각(now_ms)
    long long last_ms;           // 마지막 **유효** 프레임(now_ms) — 유휴 마감의 기준
    long long last_epoch_ms;     // 바깥으로 내보낼 때 쓰는 절대 시각
    long long frames;            // 누적 유효 프레임
    long long drops;             // 버린 줄
    bool online;                 // §3.4 엣지 판정용 직전 상태
    int  offline_episodes;
    AuxNode() : fd(BAD_SOCK), connected_ms(0), last_ms(0), last_epoch_ms(0),
                frames(0), drops(0), online(false), offline_episodes(0) {}
};

// 아직 `device_id` 를 모르는 소켓. 첫 유효 프레임에서 승격된다.
struct UnknownSock {
    sock_t fd;
    std::string buf;
    long long since_ms;
    UnknownSock() : fd(BAD_SOCK), since_ms(0) {}
};

// ---------------------------------------------------------------- 상태
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
    char kind;               // 'R' | 'C' | 'T'
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
    Pending() : wire_rid(0), ws_fd(BAD_SOCK), kind(0), top(0), sent_ms(0), tries(0), queued(false) {}
};

struct Conn {
    enum Kind { HTTP, WS } kind;
    std::string inbuf;
    Conn() : kind(HTTP) {}
};

struct Server {
    Slot slots[10];
    sock_t lsn_ard, lsn_http, lsn_phone;
    std::map<sock_t, std::string> phones;   // 폰 연결 → 수신 버퍼 (연결마다 따로!)
    // ⚠ `ard` 는 이제 "아두이노 연결"이 아니라 **주차 노드의 연결**이다(REQ-0083).
    // 이름을 안 바꾼 이유: 이 필드에 얽힌 상태·지표가 79곳인데, 그 전부를 건드리면
    // **하위호환을 코드로 증명할 수 없다.** 지금 도는 단일 노드(P1)가 그대로 동작해야 하고
    // 그게 조원 배포 전까지 유일한 실물이다. 그래서 주차 노드 경로는 **한 줄도 안 바꾸고**,
    // 보조 노드를 옆에 붙이는 쪽을 택했다. 단일 노드 동작이 구조적으로 보존된다.
    sock_t ard;                        // **주차 노드**의 연결 (여전히 하나)
    std::string park_dev;              // 주차 노드의 device_id. "" = 아직 미정(first-S-wins)
    std::map<std::string, AuxNode> aux;   // device_id → 보조 노드 (상행 전용)
    std::vector<UnknownSock> unknown;     // id 미상 소켓 — 첫 유효 프레임에서 승격
    int  aux_conflicts;                // `S` 를 보냈지만 주차 노드가 아닌 장치 수(가정 붕괴 신호)
    int  admit_rejects;                // 상한 초과로 거절한 연결 수
    std::string ard_buf;
    bool  ard_seen;
    long long ard_last_ms;

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
    };
    std::vector<DownQ> downq;
    long long downq_bytes;
    long long batch_count;       // 배치 거래 수
    long long batch_lines;       // 배치로 나간 줄 수
    // 🔴 **정반대인 둘을 한 칸에 세지 않는다**(원장 §8.16 이 `error` 에서 겪은 그것이다).
    //   `q_rejected` = 큐가 넘쳤다 → **설계 문제**(cap·깊이를 다시 봐야 한다)
    //   `q_nodev`    = 장치가 없다 → **링크 문제**(cap 과 아무 상관 없다)
    // 합쳐 두면 `큐거절 3` 을 보고 cap 을 올리려 하는데 실제로는 장치가 없었던 것일 수 있다.
    long long q_rejected;        // queue_full 로 거절한 수
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
    // §9.1 `device.last_frame_ts` 전용 — **epoch 시각**이다. ard_last_ms 를 쓰면 안 된다:
    // 그건 now_ms() 기반(윈도우에서는 부팅 후 경과)이라 바깥으로 나가면 수십 년짜리 값이 된다(28행 경고).
    long long ard_last_epoch_ms;
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
    uint16_t next_rid;
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

    Server() : lsn_ard(BAD_SOCK), lsn_http(BAD_SOCK), lsn_phone(BAD_SOCK), ard(BAD_SOCK),
               aux_conflicts(0), admit_rejects(0),      // 선언 순서와 일치시킨다(-Wreorder)
               ard_seen(false), ard_last_ms(0),
               // 🔴 이 여섯(+다섯)은 **선언만 돼 있고 초기화 목록에 없었다.** 쓰기 시작하는 순간
               // 쓰레기값이 지표로 나간다 — 원장이 통째로 경고하는 그 형태다. 여기서 닫는다.
               downq_bytes(0), batch_count(0), batch_lines(0),
               q_rejected(0), q_nodev(0), q_dup(0),
               q_deferred(0), q_dropped_link(0),
               rtt_max_ms(0), rtt_last_ms(0), rtt_n(0), win_skips(0), dmax_flushes(0),
               last_dmax_ms(0),
               ard_last_epoch_ms(0), ard_uptime(-1), ard_seq(-1),
               ard_dev("?"),
               xs_uptime(-1), xs_last_ms(0), xs_dev(""),
               xs_reconnect_reboot(0), xs_reconnect_link(0),
               xs_reconnect_unknown(0),
               next_rid(1), base_valid(false), test_armed(false),
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
           + "(장치없음 " + std::to_string(q_nodev)
           + "/중복 " + std::to_string(q_dup)
           + "/링크버림 " + std::to_string(q_dropped_link) + ")"
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
                         + " · 최대공백 " + secs(sess_max_gap_ms));
            sess_start_ms = 0;
        }
        if (!link_down_since) link_down_since = now_ms();
        // 설계 §2 — **세션이 끝나면 하행 큐를 비운다.** 옛 큐를 새 세션에 쏘면 장치가 모르는
        // 상태에 명령이 떨어진다. 비운 항목은 `device_offline` 로 **반드시 답한다**(§4-B 보장).
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
        logf("+ARD", "주차 노드 접속 — 세션#" + std::to_string(ard_sessions)
                     + " · device=" + dev);
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
            if (!line.empty()) on_ard_line(line);
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

    // id 미상 소켓 하나를 승격한다. 반환 false = 자리가 없어 거절했다(소켓은 닫힌다).
    bool promote_unknown(sock_t c, const std::string& dev) {
        // (1) 주차 노드가 아직 없다 → first-S-wins 로 이 장치가 주차 노드다
        // (2) 같은 device_id 의 재접속 → 자리를 물려받는다(옛 동작을 이 경우로 한정한 것)
        if (park_dev.empty() || park_dev == dev) {
            if (park_dev.empty())
                logf("=", "주차 노드 지정 — device=" + dev
                          + " (first-S-wins: 첫 S 프레임을 보낸 장치가 주차 노드다)");
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

    // 큐에 있던 항목을 **전선에 못 내보낸 채** 끝낼 때.
    // 🔴 설계 §4-B 의 보장: **`queued` 를 보냈으면 그 뒤에 반드시 `ack` 또는 `error` 가 간다.**
    // 그 보장이 없으면 화면의 중간 상태가 영구가 된다.
    // ⚠ 코드는 `device_offline` 이다 — **전선에 안 나갔다는 뜻**이고 원장 §8.16 이 요구하는 구분이다.
    // `ack_timeout` 을 쓰면 "나갔는데 응답이 없다"는 **거짓 문장**이 로그에 남는다.
    void fail_down_item(const DownQ& q, const char* code, const char* msg) {
        std::map<uint16_t, Pending>::iterator it = pend.find(q.wire_rid);
        if (it != pend.end()) pend.erase(it);
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
    void flush_downq(const char* why, bool ignore_window) {
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
        while (!downq.empty()) {
            const DownQ& q = downq.front();
            // ⚠ 첫 줄은 상한을 넘어도 담는다 — 안 그러면 큰 줄 하나가 큐를 영구히 막는다.
            if (!payload.empty() &&
                payload.size() + q.line.size() > (size_t)DOWN_BATCH_CAP_B) break;
            payload += q.line;
            batch.push_back(q);
            downq_bytes -= (long long)q.line.size();
            downq.erase(downq.begin());
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
    void push_snapshot() { ws_broadcast(snapshot_json()); }

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
          << ",\"expires_ms\":" << left << "}";
        if (q.ws_fd != BAD_SOCK && conns.count(q.ws_fd)) ws_send(q.ws_fd, o.str());
    }
    void send_ack(sock_t fd, const std::string& rid, const std::string& slot, int result,
                  char kind = 'R') {
        const char* m = "예약되었습니다";
        if (kind == 'T')      m = "테스트 값을 적용했습니다";
        else if (kind == 'C') m = "예약을 취소했습니다";
        else if (kind == 'M') m = "시뮬레이션 한 걸음 진행했습니다";
        if (result == 1) m = "이미 주차된 자리입니다";
        else if (result == 2) m = "이미 예약된 자리입니다";
        else if (result == 3) m = "잘못된 요청입니다";
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

    // ---------- 아두이노로 요청 내리기
    void dispatch(char kind, sock_t ws_fd, const std::string& brid,
                  const std::string& slot, const std::string& uid) {
        uint16_t rid = next_rid++;
        if (next_rid == 0) next_rid = 1;
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
        if (!enqueue_down(pend[rid], build_line(buf), true, false)) pend.erase(rid);
    }

    // §2.4 `T` — 테스트 모드 제어. R/C 와 같은 pend 표·재전송·타임아웃을 그대로 탄다.
    // Pending.user_id 를 tval 보관에 재사용하고, slot 에 "??" 가 들어갈 수 있다.
    void dispatch_test(sock_t ws_fd, const std::string& brid,
                       char op, const std::string& slot, const std::string& tval) {
        uint16_t rid = next_rid++;
        if (next_rid == 0) next_rid = 1;
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
        if (!enqueue_down(pend[rid], build_line(test_prefix(p)), true, false)) pend.erase(rid);
    }
    // §12B — 시뮬레이터 한 걸음. **무장 여부로 막지 않는다**(테스트 모드와 별개).
    void dispatch_sim(sock_t ws_fd, const std::string& brid) {
        uint16_t rid = next_rid++;
        if (next_rid == 0) next_rid = 1;
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.kind = 'M'; p.top = 0;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        if (!enqueue_down(pend[rid], build_line(sim_prefix(p)), true, false)) pend.erase(rid);
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
        if (!downq.empty() && ard != BAD_SOCK && (t - ard_last_ms) > DOWN_DMAX_MS
            && (t - last_dmax_ms) >= DOWN_SLOT_MS) {
            last_dmax_ms = t;
            dmax_flushes++;            // 그래서 이 수가 "몇 번 포기했나"로 읽힌다
            flush_downq("S 가 안 온다 — 창 포기", true);
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
            if (p.kind == 'M') line = sim_prefix(p);       // 재전송이 두 걸음이 되면 안 된다(§12B.4)
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
        for (size_t i = 0; i < dead.size(); i++) pend.erase(dead[i]);
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
    void on_ard_line(const std::string& line) {
        // 🔑 **받은 바이트 수를 같이 남긴다** — `AT+CIPSEND` 조용한 잘림(③)의 유일한 판별자다.
        // 장치는 `SEND OK` 를 받으면 **성공으로 세므로 잘린 것을 모른다.**
        // **서버가 "몇 바이트 받았나"를 적어야 "보낸 만큼 왔나"를 대조할 수 있다.**
        // ⚠ 64B 이하에서는 값이 뻔하지만, **긴 줄 시험에서 이 한 칸이 시험의 본체**가 된다.
        {
            char rb[24];
            snprintf(rb, sizeof(rb), " (rx=%zuB)", line.size());
            logf("←ARD", line + rb);
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

            int occ[10];
            for (int i = 0; i < 10; i++)
                occ[i] = (i < (int)f[2].size() && f[2][i] == '1') ? 1 : 0;

            // §2.4 tmask — **선택 필드다. 없으면 해제로 본다**(§2.1 규칙 8, 옛 펌웨어 수용).
            // f 는 체크섬을 뺀 필드들이므로 7번째(index 6)가 있으면 그것이 tmask 다.
            int ovr[10];
            bool armed = false;
            for (int i = 0; i < 10; i++) ovr[i] = 0;
            if (f.size() >= 7 && f[6] != "-" && f[6].size() >= 10) {
                armed = true;
                for (int i = 0; i < 10; i++) ovr[i] = (f[6][i] == '1') ? 1 : 0;
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
            if (f.size() >= 4 && f[3].size() >= 10) {
                for (int i = 0; i < 10; i++)
                    if (f[3][i] == '1' && slots[i].reserved == 0 && !has_pending_for(SLOT_ID[i])) {
                        logf("⚠", std::string("불일치: 아두이노 ") + SLOT_ID[i]
                                  + " reserved=1, 서버 0 → C 재하달");
                        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");
                    }
            }
            write_log_if_changed();
            push_snapshot();

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
            flush_downq("S 도착 — 창 시작", false);
        }
        else if (f[0] == "A" && f.size() >= 4) {
            uint16_t rid = (uint16_t)atoi(f[1].c_str());
            std::string slot = f[2];
            int result = atoi(f[3].c_str());
            std::map<uint16_t, Pending>::iterator it = pend.find(rid);
            if (it == pend.end()) { logf("!", "모르는 rid 의 ACK — 무시 (재전송 중복일 수 있다)"); return; }
            Pending p = it->second;
            pend.erase(it);

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
            if (p.ws_fd != BAD_SOCK) send_ack(p.ws_fd, p.browser_rid, slot, result, p.kind);
            // 이음매 1: 직접 호출 → 이벤트. **같은 틱의 drain 이 같은 일을 한다**(2481행).
            // 한 틱에 ACK 가 여러 건 겹치면 기록·화면이 건별 → 1회로 접힌다 — 이미 옮긴
            // 3종과 같은 성질이고, 브라우저가 보는 최종 상태는 같다.
            emit_dev_ack(park_dev, rid, (uint8_t)result,
                         std::string("ACK ") + p.kind + " " + slot
                         + " result=" + std::to_string(result));
        }
        else {
            drop_unknown++;
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
            logf("+WS", "업그레이드 완료");
            ws_send(fd, snapshot_json());          // 접속 즉시 현재 상태
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
                            unknown.push_back(u);
                            logf("+?", "연결 수락 — device_id 대기 중(" + std::to_string(UNKNOWN_TIMEOUT_MS / 1000)
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
                            if (!line.empty()) on_ard_line(line);
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
