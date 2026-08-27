// 서버의 **진짜** cksum/build_line 으로 하행 6종 바이트를 낸다.
// 체크섬을 다시 구현하지 않는 것이 요점이다 — 다시 구현하면 시험이 자기 자신과만 일치한다.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "server_device.h"

static void emit(const char* what, const std::string& prefix) {
  const std::string line = build_line(prefix);
  std::string noeol = line;
  if (!noeol.empty() && noeol[noeol.size() - 1] == '\n') noeol.erase(noeol.size() - 1);
  std::vector<std::string> f;
  const bool ok = verify_line(noeol, f);
  std::printf("%-28s %-28s nf=%zu verify=%s\n", what, noeol.c_str(), f.size(), ok ? "OK" : "NG");
}

int main() {
  char b[80];
  std::printf("# 서버 하행 골든 바이트 — server_device.h 의 build_line/cksum 이 만든 것\n");
  std::printf("# 개행은 뗐다. verify_line 은 개행 없는 줄을 받는다(server_device.h:133)\n");
  std::printf("%-28s %-28s %s\n", "# 무엇", "바이트", "필드수·자가검증");

  std::snprintf(b, sizeof(b), "G,%u,%d,%ld,", 7u, 3, 1L);          emit("G 기본", b);
  std::snprintf(b, sizeof(b), "G,%u,%d,%ld,", 999u, 255, 4294967295L); emit("G 최악길이", b);
  std::snprintf(b, sizeof(b), "G,%u,%d,%ld,", 1u, 0, -1L);         emit("G 음수인자", b);
  std::snprintf(b, sizeof(b), "R,%u,%s,%s,", 12u, "A1", "kim");    emit("R uid 있음", b);
  std::snprintf(b, sizeof(b), "R,%u,%s,%s,", 7u, "A1", "");        emit("R uid 빈값(정상)", b);
  std::snprintf(b, sizeof(b), "R,%u,%s,%s,", 65535u, "B2", "user-_09"); emit("R uid 8자 경계", b);
  std::snprintf(b, sizeof(b), "C,%u,%s,", 13u, "A2");              emit("C 기본", b);
  std::snprintf(b, sizeof(b), "T,%u,%c,%s,%s,", 14u, 'A', "??", "-"); emit("T slot 미상", b);
  std::snprintf(b, sizeof(b), "T,%u,%c,%s,%s,", 20u, 'B', "A1", "1"); emit("T 기본", b);
  std::snprintf(b, sizeof(b), "M,%u,", 15u);                       emit("M (rid 뿐)", b);
  emit("Q (필드 없음)", "Q,");
  return 0;
}
