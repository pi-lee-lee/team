// runtime.h — 프로세스 바닥. 시각·시그널·로그 파일·경로·pid·소켓 주소 유틸.
// ═══════════════════════════════════════════════════════════════════
// 🔴 **단독 컴파일용이 아니다.** `server.cpp` 안 그 자리에 include 된다(파일 스코프).
//
// 🔑 **여기 있는 것은 도메인이 아니라 *바닥* 이다.** 주차장도 전선도 모른다.
//   시각을 만들고, 로그를 열고, 경로를 절대화하고, 상대 주소를 문자열로 만든다.
//
// ⚠ **두 시계가 있다. 섞지 마라.**
//   `now_ms()`   = 상대 시각(타이머 전용). 벽시계가 아니다
//   `epoch_ms()` = 벽시계. 로그·`ts_ms` 용
//   섞으면 "구간이 음수"나 "1970년" 같은 값이 나오는데 **둘 다 그럴듯해 보이는 날이 있다.**
// ═══════════════════════════════════════════════════════════════════
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
// ---------------------------------------------------------------- 종료 신호
// 소크 시험은 Ctrl-C 로 끝난다. 그때 **한 줄 요약을 남기고** 죽어야 관측이 완성된다.
// 핸들러에서 하는 일은 플래그 하나 세우는 것뿐이다 — 여기서 logf 를 부르면 비동기 안전하지 않다.
static volatile sig_atomic_t g_stop = 0;
static void on_stop_signal(int) { g_stop = 1; }

// ---------------------------------------------------------------- 시험용 포트 이동
// **왜 필요한가**: 유휴 마감이 실제로 소켓을 닫는지 확인하려면 진짜 연결을 붙여 봐야 하는데,
// 운영 인스턴스가 9991/9900/5500 을 잡고 있으면 두 번째 인스턴스가 뜨지 못한다.
// 소크 이력을 들고 있는 프로세스를 재시작해서 확인하는 것은 **관측을 부수고 관측하는 짓**이다.
// 그래서 세 포트를 한꺼번에 옮기는 이음매를 둔다. **판정에는 전혀 관여하지 않는다**
// (`no_disk` 와 같은 성격의 이음매다 — 디스크를 안 건드리게 하는 문).
// 0 이 아니면 기동 배너에 크게 찍어 시험 인스턴스를 운영으로 착각할 수 없게 한다.
// 🔴 **`g_port_offset` 을 없앴다** (사용자 확정):
//   *"일반적으로 오프셋방식은 잘 안쓴다. 차라리 `--port-web`, `--port-ardu`, `--port-cam` 으로"*
//   🔑 근거가 관용만이 아니다 — **오프셋은 "포트가 고정 간격으로 놓여 있다"를 전제**하는데
//     새 기본값(9990 · 8888 · 8911)에는 그 전제가 없다. `offset=23` 이면 8888+23 = **8911**,
//     곧 아두이노가 카메라 포트를 잡는다. **오프셋이 원래 주던 보장이 이미 깨졌다.**
//   ⚠ 호환용으로 남기지 않았다 — **깨진 기능을 남기면 되는 줄 알고 쓴다.**
static int g_port_web  = PORT_HTTP;
static int g_port_ardu = PORT_ARDUINO;
static int g_port_cam  = PORT_PHONE;

// 🔴 **영속 파일 경로를 가르는 꼬리표.** 기본 포트면 빈 문자열, 아니면 `.test+<웹포트>`.
//
//   🔑 오프셋이 없으므로 **인스턴스를 구별하는 값**이 따로 필요하다 —
//   웹 포트를 쓴다(사람이 브라우저에 치는 값이라 로그와 대조하기 쉽다).
//
// 🔴 **이 갈래가 없으면 시험 인스턴스가 운영의 rid 커서·노드 대장·로그를 덮어쓴다.**
//   그러면 운영을 재시작했을 때 **시험이 쓴 자리로 되돌아간다.** 규칙으로 부탁하지 않고
//   경로를 갈라서 **불가능**으로 만든다(monitor 요구).
//   ⚠ 오프셋을 없애면서 이 보호가 같이 사라질 뻔했다 — 컴파일러가 잡았다.
static std::string instance_tag() {
    if (g_port_web == PORT_HTTP && g_port_ardu == PORT_ARDUINO && g_port_cam == PORT_PHONE)
        return std::string();
    return ".test+" + std::to_string(g_port_web);
}
// 🔴 **주차 노드 잠금**(④). 빈 문자열 = 잠금 없음 = 종전 `first-S-wins` 그대로.
// ⚠ **`first-S-wins` 를 없애지 않는다.** 이건 관측 환경용 잠금장치이지 프로토콜 변경이 아니다.
// 왜 필요한가: 우리가 devid 를 바꿔도 **조원 보드는 여전히 `P1` 로 붙고 서버는 그것을 받는다.**
// 더 나쁜 경우 — 조원 `P1` 이 **먼저** `S` 를 보내면 `first-S-wins` 가 **그 보드를 주차 노드로
// 지정**하고 우리는 보조 노드(상행 전용)로 밀려 **하행을 못 받는다.** 지금보다 나쁘다.
static std::string g_park_dev_pin;

// ---------------------------------------------------------------- 로그 계약 v0.1
// 명세: docs/net/server-log-contract.md
//
// **왜 이 셋이 계약인가** — 하나라도 빠지면 로그가 조용히 오독된다:
//  (1) **타임스탬프에 날짜를 넣는다.** 없으면 자정을 넘긴 로그에서 어제 16:00 과 오늘 16:00 이
//      같아 보이고, 읽는 사람이 **없는 동시 기록을 만들어 낸다.**
//  (2) **로그 경로를 서버가 정한다.** 셸 리다이렉션에 맡기면 나중에 뜬 인스턴스가 다른 곳에 써서
//      관측이 조용히 끊기고, *"그 프로세스는 어디에 쓰고 있나"* 에 아무도 못 답한다.
//  (3) **기동 배너에 신원을 넣는다.** 없으면 도는 바이너리가 어느 소스에서 나왔는지
//      mtime 비교로 추리해야 한다.
//  📖 세 가지가 실제로 일으킨 오독은 docs/net/LEDGER.md 에 있다.

// 로그 형식 버전. **경계 줄의 첫 필드다.**
// monitor 의 집계 도구 9개가 전부 "줄이 HH:MM:SS 로 시작한다"를 가정하고 짜여 있고,
// 옛 로그(형식 1)와 새 로그를 **한 파일 안에서 동시에** 다뤄야 한다 — 옛 로그가 판정 근거라
// 계속 읽어야 하기 때문이다. 이 필드가 있으면 파서가 경계에서 스스로 전환한다.
// 없으면 관측자는 **날짜 유무를 휴리스틱으로 추측**해야 한다. 그 추측은 틀린다.
// 3: 소크 요약에 `재연결내역(…)` 과 `승격전버림(…)` 두 칸이 추가됐고,
//   재연결 시 판정 한 줄(`재연결 판정: …`)이 새로 나간다. **기존 칸은 형태를 안 바꿨다** —
//   순수 추가라 옛 파서가 깨지지는 않지만, 새 칸이 있는지 없는지를 파서가 알아야 하므로 올린다.
//
// 🔴 4: `=== INSTANCE` 줄의 **칸 이름이 하나 바뀌었다.**
//     `offset=<정수>`  →  `default=yes|no`
//   ⚠ **순수 추가가 아니다. 이름이 바뀌었다** — 3 과 4 는 같은 질문에 다른 칸으로 답한다.
//     (`offset=0` 과 `default=yes` 가 같은 뜻: *"이건 운영 기본 포트다"*)
//   그리고 영속 파일 꼬리표도 `.test+<오프셋>` → **`.test+<웹포트>`** 로 바뀌었다.
//
// 🔴🔴 **먼저 올렸어야 했다. 안 올리고 배포했다.**
//   그래서 `parking-server.log` 안에 **`logfmt=3` 인데 `default=` 인 줄이 하나** 있다
//. 그 줄부터 이 판본을 올린 기동 전까지가 **모호 구간**이다.
//   🔑 **판본이 있는데 안 올리면, 판본이 없는 것보다 나쁘다** —
//     파서가 "3 이니 옛 형식"이라고 **확신하고** 틀린다.
//   ⚠ 규약: **칸을 더하면 올린다. 칸 이름을 바꾸면 반드시 올린다.**
//     그리고 **무엇이 바뀌었는지 여기 한 줄로 적는다** — 번호만 올리면 받는 쪽은
//     "전부 다시 봐야 하나"를 판단할 수 없다.
#define LOG_FORMAT_VERSION 4

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
// 🔴 `default_log_path()` 도 같이 죽었다 (dev 판) — 로그 파일 계층에 딸린 것이다.

// 🔴 A[1](B) — `rid` 커서 영속 파일. **반드시 절대경로다.**
//
// **cwd 상대경로로 두면 다른 디렉터리에서 기동하는 순간 커서가 조용히 사라지고,
//   그때 나오는 거동이 정확히 "보호가 없는 상태"다 — 막으려던 것으로 조용히 되돌아간다.**
// 루트가 오늘 그 함정을 실측했다: `data_log.json`·`index.html` 이 cwd 상대라
// 인스턴스마다 다른 파일을 열고 있었다.
//
// 🔑 **로그와 같은 규칙으로 오프셋에 따라 경로를 가른다.** 안 그러면 시험 인스턴스가
//   운영의 커서를 덮어써서 **운영 재시작이 시험이 쓴 자리로 되돌아간다.**
// 노드 대장 경로 — **rid 커서와 같은 규율이다**(명세 `DESIGN-node-ledger.md` §1).
// 🔴 `data_log.json` 처럼 cwd 상대로 두지 않는다. 다른 디렉터리에서 띄우면
//   **대장이 조용히 새로 생기고**, 증상은 "재기동했더니 등록이 다 사라졌다"로 나타난다.
static std::string node_ledger_path() {
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = getenv("USERPROFILE");
#endif
    if (!home || !*home) return std::string();   // 빈 값 = 영속 불가. 호출자가 크게 남긴다
    // 🔴 **dev 는 노드 대장을 영속하지 않는다** — 운영의 `~/parking-logs` 를 안 건드린다.
    //   ⚠ 재기동하면 "누가 있었나"를 잊는다. **개발에는 그게 맞다.**
    return std::string();
    std::string base = std::string(home) + "/parking-logs/parking-nodes";
    base += instance_tag();     // 🔑 기본 포트가 아니면 경로가 갈린다 (아래 주석)
    return base + ".txt";
}

static std::string rid_cursor_path() {
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = getenv("USERPROFILE");
#endif
    if (!home || !*home) return std::string();   // 빈 값 = 영속 불가. 호출자가 크게 남긴다
    // 🔴 **dev 는 rid 커서를 영속하지 않는다.** 매 기동 임의 지점에서 시작한다.
    return std::string();
    std::string base = std::string(home) + "/parking-logs/parking-rid-cursor";
    base += instance_tag();     // 🔑 기본 포트가 아니면 경로가 갈린다 (아래 주석)
    return base + ".txt";
}

// ── 🔴🔴 **경로 구분자를 한 곳에서 다룬다** (2026-08-27 · 사용자가 VS 빌드에서 오류를 냈다)
//
//   ⚠ 윈도우는 `\` 를 쓰고 POSIX 는 `/` 를 쓴다. 그런데 **윈도우는 `/` 도 받아들인다** —
//     그래서 **만들 때는 `/` 로 만들고, 찾을 때는 둘 다 본다**. 그것이 가장 적은 변경이다.
//   🔴 자리마다 `rfind('/')` 를 고치면 **다음에 또 빠진다.** 그 실패를 여기서 한 번에 막는다.
//     ★ 오늘 `realpath` 가 정확히 그렇게 빠졌다 — 같은 함수인데 **한 곳만 `#ifndef` 로 감쌌다.**
static bool is_sep(char c) { return c == '/' || c == '\\'; }
static size_t last_sep(const std::string& p) {
    for (size_t i = p.size(); i > 0; i--) if (is_sep(p[i-1])) return i - 1;
    return std::string::npos;
}
// 절대경로인가 — POSIX 는 `/…`, 윈도우는 `C:\…` 또는 `\…`
static bool is_abs_path(const std::string& p) {
    if (p.empty()) return false;
    if (is_sep(p[0])) return true;
    return p.size() >= 3 && p[1] == ':' && is_sep(p[2]);   // `C:\` · `C:/`
}

// 부모 디렉터리를 **한 단계씩 전부** 만든다.
// ⚠ 한 단계만 만들면 조부모가 없을 때 mkdir 이 ENOENT 로 실패하고, 그 결과는
// "로그가 조용히 안 남는다" 이다. 기본 경로($HOME/parking-logs)는 우연히 한 단계라 통과하지만
// --log= 로 깊은 경로를 주면 그 순간 무너진다. 조용한 실패를 남겨 두지 않는다.
static void ensure_parent_dir(const std::string& path) {
    size_t cut = last_sep(path);
    if (cut == std::string::npos || cut == 0) return;
    std::string dir = path.substr(0, cut);
    for (size_t i = 1; i <= dir.size(); i++) {
        if (i != dir.size() && !is_sep(dir[i])) continue;
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
// 🔴 `exe_path()` 가 여기 있었다. **dev 판에서 지웠다** — 그것을 쓰던
//   기계용 줄(`=== INSTANCE`)과 파일 로그를 걷어내면서 **딸려 죽었다.**
//   🔑 컴파일러가 `-Wunused-function` 으로 알려 줬다 — 걷어낸 것이 무엇을
//     같이 죽였는지는 **사람이 세는 것보다 컴파일러가 정확하다.**

// 🔴 경계 줄의 `cwd=` 필드
//
// **`serve_file()`(`index.html`)과 `data_log.json` 쓰기가 둘 다 cwd 상대다.**
// 그래서 **같은 바이너리라도 어디서 떴느냐에 따라 다른 실체를 읽고 쓴다** —
// 오늘 `data_log.json` 이 세 디렉터리에 있었고(`learn` · `조별과제샘플` · `parking-bin`),
// `:9900` 이 이틀 된 화면을 내주는 일이 생긴다.
//
// 🔴 **그때 cwd 를 사후에 복원할 방법이 없었다.** 프로세스가 죽으면 `lsof` 로도 못 본다.
//   web 이 `data_log.json` 의 mtime 으로 역추적을 시도했는데 **그 디렉터리를 쓰는 프로세스가
//   여럿이라 성립하지 않았다.** 🔑 **cwd 는 사후에 파일로 복원할 값이 아니라 기동 때 찍을 값이다.**
//
// ⚠ 실패해도 기동을 막지 않는다 — 모르면 "?" 를 적는다(`bin=` 과 같은 규율).
// 🔴 **정적 자원·상태 파일의 뿌리.** 기동 때 **절대경로로 얼린다.**
//   지금까지 `serve_file()` 이 **cwd 상대경로**로 열었다 — 그래서 **다른 디렉터리에서 띄우면
//   실체가 통째로 바뀌는데 아무 신호가 없다.** 그것으로 `:9900` 이
//   **이틀 낡은 화면을 내줬다**(존재형 검사 둘이 다 통과했다).
// 🔑 **거동은 안 바꾼다** — 기동 시점의 cwd 를 그대로 쓴다(아무도 `chdir` 을 안 한다).
//   바뀌는 것은 **그 값이 로그에 남고, 나중에 cwd 가 움직여도 안 따라간다**는 것뿐이다.
// ⚠ 그러니 이것은 "경로를 고른다" 가 아니라 **"고른 것을 못 잃게 한다"** 이다.
static std::string g_docroot;      // 절대경로 · 끝에 `/` 없음. 빈 값이면 아직 안 얼렸다

// 🔴🔴 **뿌리가 둘이다 — 성질이 다르기 때문이다**(REQ-0497 · 2026-08-27)
//   `g_docroot`  : **읽기 전용 자산**(화면 셋). 배포마다 통째로 갈린다. **web 소유**다
//   `g_dataroot` : **쓰기 상태**(`data_log.json`). 배포를 건너 **살아남아야** 한다. 서버 소유다
//   ★ 한 폴더에 두면 **배포마다 상태가 자산과 섞인다** — 소유권 규약 때문이 아니라
//     **수명이 다르기 때문**이다(소유권은 우리 규약이고, 수명은 어디서나 참이다).
//   ⚠ **URL 은 안 가른다** — 화면의 `fetch('data_log.json')` 은 브라우저가 푸는 상대 경로라
//     `/index.html` 과 **같은 URL 경로**여야 한다. 가르는 것은 **파일 자리**뿐이다(web 합의).
static std::string g_dataroot;     // 절대경로 · 끝에 `/` 없음. 빈 값이면 `g_docroot` 를 따른다

// 🔴 **8081 자리 선택 서비스가 배정에 관여하나**(사용자 지시 2026-08-27 · `--no-chooser`)
//   ⚠ **기본은 켜짐**이다 — 끄는 것이 예외이고, 예외는 **명시로만** 들어와야 한다.
//   ★ 끄면 `③ 차단봉 열기` 와 `⑤⑥ 안내등 ON` 이 **같은 초**에 난다(그것을 보이려는 것이다).
//   🔵 8081 **화면은 그대로 산다** — 접속·표시는 되고 **배정에만 관여 안 한다**.
static bool g_chooser_on = true;

// 뿌리 기준 절대경로. 🔑 **파일을 여는 모든 자리가 이것을 거쳐야** 한 곳만 고치면 된다.
static std::string doc_path(const std::string& rel) {
    if (g_docroot.empty()) return rel;             // 얼리기 전(시험 등) — 종전 그대로
    return g_docroot + "/" + rel;
}

// 🔵 **실행파일이 있는 디렉터리의 형제 폴더**를 절대경로로 준다 — `<exe>/../<name>`
//   ★ 이것이 릴리즈 배치의 기준이다 : `릴리즈/server/srv` → `릴리즈/web`.
//     **폴더를 통째로 옮겨도 상대 관계가 유지된다.**
//   🔴 `argv[0]` 을 쓴다 — `realpath` 로 심볼릭 링크와 `./` 를 편다.
//     ⚠ 못 풀면 **빈 문자열**을 준다. 그러면 호출부가 *"못 정했다"* 를 알 수 있다 —
//       조용히 cwd 로 떨어지면 **이 함수를 만든 이유가 사라진다.**
static std::string g_argv0;        // main 이 첫 줄에 채운다
static std::string exe_sibling_dir(const std::string& name) {
    if (g_argv0.empty()) return std::string();
    char buf[4096];
    // 🔴🔴 **`realpath` 는 POSIX 전용이다.** 윈도우 대응물은 `_fullpath` 이고 **인자 순서가 반대**다.
    //   ⚠ 2026-08-27 에 사용자가 VS 에서 *"realpath 식별자를 찾을 수 없다"* 로 막혔다.
    //   ★ 같은 파일 안 `abs_path()` 는 이미 `#ifndef _WIN32` 로 감싸 놓고 **여기만 안 감쌌다** —
    //     §"주 경로엔 있고 보조 경로엔 없다" 그대로다. **한 함수를 두 곳에서 쓰면 두 곳 다 봐라.**
#ifdef _WIN32
    if (!_fullpath(buf, g_argv0.c_str(), sizeof(buf))) return std::string();
#else
    if (!realpath(g_argv0.c_str(), buf)) return std::string();
#endif
    std::string p(buf);
    // 🔑 `last_sep()` 이 `/` 와 `\` 를 **둘 다** 본다 — 윈도우 경로는 `\` 로 온다.
    size_t s1 = last_sep(p);                        // 실행파일 이름을 뗀다
    if (s1 == std::string::npos) return std::string();
    p.erase(s1);
    size_t s2 = last_sep(p);                        // 그 디렉터리의 부모
    if (s2 == std::string::npos) return std::string();
    // ⚠ 만들 때는 `/` 로 만든다 — **윈도우도 `/` 를 받아들인다.** 섞이지만 열린다.
    return p.substr(0, s2) + "/" + name;
}

// 🔴 **서버가 쓰는 파일은 이쪽이다.** `doc_path` 와 섞어 쓰지 마라 —
//   섞으면 배포가 상태를 덮거나, 서버가 **남의 폴더에 쓴다**.
static std::string data_path(const std::string& rel) {
    if (!g_dataroot.empty()) return g_dataroot + "/" + rel;
    return doc_path(rel);                          // 안 정했으면 종전 그대로(한 뿌리)
}

static std::string cur_cwd() {
    char b[1024];
#ifdef _WIN32
    return _getcwd(b, (int)sizeof(b)) ? std::string(b) : std::string("?");
#else
    return getcwd(b, sizeof(b)) ? std::string(b) : std::string("?");
#endif
}

// 🔴 상대경로를 **해석된 절대경로**로 바꾼다
//
//   `serve_file()` 과 `data_log.json` 이 cwd 상대라 **같은 바이너리가 어디서 떴느냐에 따라
//   다른 실체를 읽고 쓴다.** ⚠ 로그에 "무엇을 열었나" 가 없으면 그것을 찾는 데 반나절이 든다.
//   ⚠ 파일이 없어도 **경로는 답할 수 있어야 한다**(그게 곧 "왜 404 인가"의 답이다).
//     그래서 `realpath` 로 풀리지 않으면 **cwd 를 붙여서라도** 절대경로를 만든다.
static std::string abs_path(const std::string& rel) {
    if (is_abs_path(rel)) return rel;
#ifndef _WIN32
    char rp[1024];
    if (realpath(rel.c_str(), rp)) return std::string(rp);
#endif
    std::string cw = cur_cwd();
    if (cw.empty() || cw == "?") return rel;
    return cw + "/" + rel;
}


// 🔴 `iso8601()` 가 여기 있었다. **dev 판에서 지웠다** — 그것을 쓰던
//   기계용 줄(`=== INSTANCE`)과 파일 로그를 걷어내면서 **딸려 죽었다.**
//   🔑 컴파일러가 `-Wunused-function` 으로 알려 줬다 — 걷어낸 것이 무엇을
//     같이 죽였는지는 **사람이 세는 것보다 컴파일러가 정확하다.**

// 로그 파일을 연다. **실패해도 기동을 막지 않는다** — 관측이 없다고 서비스를 멈추는 것은
// 손해가 더 크다. 다만 화면에 크게 알려서 "조용히 관측이 없는" 상태가 되지 않게 한다.
// 같은 로그 파일에 두 인스턴스가 붙는 것을 **기계로** 막는다.
// 경로를 가르는 것(default_log_path)은 실수를 막지만 `--log` 로 같은 경로를 명시하면 뚫린다.
// 여기서 배타 잠금을 걸어 그마저 불가능하게 한다.
// flock 은 **fd 에 걸리므로 프로세스가 죽으면 커널이 자동으로 푼다** — 죽은 잠금이 남지 않는다.
// (Windows 는 미구현. 그쪽은 경로 분리까지만 보장한다 — 계약서에 한계로 적어 둔다.)
#ifndef _WIN32
static int g_log_lock_fd = -1;
// 🔴 `lock_log_exclusive()` 도 같이 죽었다 (dev 판) — 로그 파일 계층에 딸린 것이다.
#endif

// 🔴 `open_log()` 가 여기 있었다. **dev 판에서 지웠다** — 그것을 쓰던
//   기계용 줄(`=== INSTANCE`)과 파일 로그를 걷어내면서 **딸려 죽었다.**
//   🔑 컴파일러가 `-Wunused-function` 으로 알려 줬다 — 걷어낸 것이 무엇을
//     같이 죽였는지는 **사람이 세는 것보다 컴파일러가 정확하다.**

static void logf(const char* mark, const std::string& msg) {
    long long t = epoch_ms() / 1000;
    time_t tt = (time_t)t;
    // ⚠ **날짜를 빼지 마라.** 이 줄에서 날짜가 빠져 있던 탓에 08-16 에 오독이 두 번 났다.
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&tt));
    std::cout << ts << "  " << mark << " " << msg << std::endl;
}

// ── 🔴 `RidPool` 의 본문 — 로그·디렉터리 도우미가 여기 있으므로 여기에 둔다
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

// ---------------------------------------------------------------- 다중 노드
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
// 🔴🔴 **접속 IP·포트를 남긴다**
// ⚠ `accept(fd, NULL, NULL)` 로 **상대 주소를 버리지 마라.** 버리면
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
