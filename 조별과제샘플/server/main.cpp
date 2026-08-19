// main.cpp — **엔트리 포인트** (2026-08-19 · REQ-0272 1단계)
//
// 🔴 **이 파일은 지금 `server.cpp` 가 원래 자리에서 `#include` 한다.**
//   그래서 전처리 결과가 이전과 **한 글자도 다르지 않고**, 산출물(`.o`)이 **0 차이**여야 한다.
//   그 대조가 "떼어 냈지만 아직 아무것도 안 바꿨다"를 **값으로** 증명한다.
//   (arduino 가 펌웨어 846줄을 이 방법으로 빼고 `hex` 0 을 얻었다. 서버도 같은 방법을 쓴다.)
//
// ⏳ **다음 단계에서 뒤집는다** — `main.cpp` 가 번역 단위가 되고 서버 조각들을 include 하는 쪽으로.
//   🔴 **그때부터 축이 생긴다.** 지금은 축이 없다는 것을 먼저 증명해 두는 것이 목적이다.
//
// ⚠ 그러므로 **이 파일을 단독으로 컴파일하지 마라.** 빌드는 여전히 `c++ … server.cpp` 다.

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
