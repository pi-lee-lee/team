#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
    typedef int SOCKET;
    const int INVALID_SOCKET = -1;
    const int SOCKET_ERROR = -1;
    void closesocket(int s) { close(s); }
#endif

// 데이터를 주기적으로 송신하는 스레드 함수
void sendWorker(SOCKET clientSocket) {
    std::string message = "1";
    while (true) {
        int bytesSent = send(clientSocket, message.c_str(), message.length(), 0);
        if (bytesSent == SOCKET_ERROR) {
            std::cout << "\n[오류] 데이터 송신 실패 (연결 끊김)" << std::endl;
            break;
        }
        std::cout << "[송신] " << message << std::endl;
        
        // 1초 대기
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

int main() {
#ifdef _WIN32
    // 윈도우 소켓(WSA) 초기화
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup 실패" << std::endl;
        return 1;
    }
#endif

    // 1. 소켓 생성
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "소켓 생성 실패" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 포트 재사용 설정 (바인드 에러 방지)
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // 2. 주소 구조체 설정 및 바인드
    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // 모든 인터페이스로부터 접속 허용
    serverAddr.sin_port = htons(9991);       // 요청하신 포트 번호 9999

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "바인드(Bind) 실패" << std::endl;
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 3. 리슨(Listen) 시작
    if (listen(serverSocket, 3) == SOCKET_ERROR) {
        std::cerr << "리슨(Listen) 실패" << std::endl;
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "===========================================" << std::endl;
    std::cout << " TCP 서버가 시작되었습니다. 포트: 9999" << std::endl;
    std::cout << " 아두이노(ESP-01)의 접속을 기다리는 중..." << std::endl;
    std::cout << "===========================================" << std::endl;

    // 4. 클라이언트 접속 수락 (Accept)
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "접속 수락 실패" << std::endl;
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 접속한 클라이언트 IP 출력
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
    std::cout << "\n[연결 완료] 클라이언트 접속함: " << clientIP << std::endl;

    // 5. 송신 전용 독립 스레드 생성 및 시작
    std::thread senderThread(sendWorker, clientSocket);
    senderThread.detach(); // 메인 스레드와 분리하여 독립 동작

    // 6. 메인 스레드에서는 계속 데이터 수신(Receive) 처리
    char buffer[1024];
    while (true) {
        std::memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesReceived > 0) {
            // 수신 데이터 출력
            std::cout << "[수신] " << buffer << std::endl;
        } else if (bytesReceived == 0) {
            std::cout << "\n[안내] 클라이언트가 연결을 종료했습니다." << std::endl;
            break;
        } else {
            std::cout << "\n[오류] 데이터 수신 중 에러 발생" << std::endl;
            break;
        }
    }

    // 7. 자원 정리
    closesocket(clientSocket);
    closesocket(serverSocket);
#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "서버 프로그램이 종료되었습니다." << std::endl;
    return 0;
}
