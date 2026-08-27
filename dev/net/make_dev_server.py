#!/usr/bin/env python3
"""net/make_dev_server.py — 운영 트리에서 **dev_server 를 한 번 뽑아낸다**

🔴 **이것은 상시 도구가 아니다. *유래 기록* 이다.**
   사용자 확정: *"두벌이여도 된다. 개발자는 dev_server만 이용한다."*
   → dev_server 는 **독립 트리**다. 이 스크립트로 계속 재생성하지 않는다.
     (재생성으로 운영하면 dev 쪽 손질이 매번 날아간다.)

🔑 **그런데 유래를 적어 두는 값은 따로 있다**: *"무엇을 어떻게 걷어냈나"* 가 코드가 아니라
   글로만 있으면 **다음 사람이 재현할 수 없다.** 이 파일이 그 답이다.

   쓰기 : python3 net/make_dev_server.py <출력경로>
   ⚠ 기존 dev_server 를 덮어쓰지 않는다 — 빈 디렉터리에만 쓴다.
"""
import io, os, shutil, sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "조별과제샘플", "server")

# ── 아예 안 가져가는 파일 ────────────────────────────────────────────────
#   자가검증 5파일 2,300줄. 🔑 **개발자가 자기 lot.cpp 를 고칠 때 안 돌린다** —
#   운영 배포 관문이지 디버그 도구가 아니다.
DROP = ("selftest.h", "selftest_spot.h", "selftest_wire.h", "selftest_contract.h",
        "selftest_zones.h", "selftest_downq.h", "selftest_nodes.h", "selftest_ledger.h",
        "server_test", "server")

# ── 텍스트 치환 (파일 → [(찾을 것, 바꿀 것), …]) ─────────────────────────
#   ⚠ **찾을 것이 없으면 실패한다.** 조용히 건너뛰면 "적용된 줄 알고" 틀린다.
EDITS = {
 "server.cpp": [
   ('#include "selftest.h"\n', ''),
 ],
 "entry.h": [
   ('    if (argc > 1 && std::string(argv[1]) == "--selftest") rc = selftest();\n    else {\n        open_log(log_path);\n',
    '    {\n'
    '        // 🔴 **`open_log()` 를 안 부른다.** 출력은 stdout 하나다(사용자 확정).\n'
    '        //   ⚠ 끄면 사라진다. 남겨야 하면 `./srv > dev.log` 로 직접 받아라.\n'
    '        (void)log_path;\n'),
   ('int main(int argc, char** argv) {\n    int rc;',
    '// dev_server 판 — 🔴 **콘솔이 기본이다.** 파일 로그도 자가검증도 없다.\n'
    'int main(int argc, char** argv) {\n    int rc = 0;'),
 ],
 "config.h": [
   ('static const int  PORT_ARDUINO   = 8888;\nstatic const int  PORT_HTTP      = 9990;\nstatic const int  PORT_PHONE     = 8911;',
    '// 🔴 **dev 기본 포트 — 운영과 *일부러* 다르다** (운영: 웹 9990 · 아두이노 8888 · 카메라 8911)\n'
    '//   근거는 **실패 방향의 비대칭**이다:\n'
    '//     dev 값으로 운영에 쓰면  → **안 붙는다.** 즉시 드러난다\n'
    '//     운영 값으로 dev 에 쓰면 → 🔴 **운영에 붙는다.** devid 가 겹치면 남의 노드를 쫓아내고,\n'
    '//                               **로컬에서는 멀쩡해 보인다**\n'
    '//   > **틀렸을 때 조용한 쪽을 기본값으로 두지 마라.**\n'
    'static const int  PORT_ARDUINO   = 9991;\nstatic const int  PORT_HTTP      = 9900;\nstatic const int  PORT_PHONE     = 5500;'),
 ],
 "serve.h": [
   ('                logf("⏱", soak_line());',
    '                // 🔴 dev 는 60초 소크 요약(1,300자)을 **안 찍는다.** 운영 관측이다.\n'
    '                //   콘솔에서 그 줄은 못 읽고, 개발자가 보려던 프레임 로그를 밀어낸다.'),
 ],
}


def strip_block(s, start, end_marker, replacement):
    """start 부터 end_marker 직전까지를 replacement 로 바꾼다. 못 찾으면 죽는다."""
    i = s.index(start)
    j = s.index(end_marker, i)
    return s[:i] + replacement + s[j:]


def main():
    if len(sys.argv) != 2:
        print(__doc__); return 2
    out = sys.argv[1]
    if os.path.exists(out) and os.listdir(out):
        print("🔴 %s 가 비어 있지 않다. 덮어쓰지 않는다." % out); return 1
    os.makedirs(out, exist_ok=True)

    for f in sorted(os.listdir(SRC)):
        if f in DROP or f.startswith("selftest"):
            continue
        if not (f.endswith(".h") or f.endswith(".cpp")):
            continue
        shutil.copy(os.path.join(SRC, f), os.path.join(out, f))

    for f, pairs in EDITS.items():
        p = os.path.join(out, f)
        s = io.open(p, encoding="utf-8").read()
        for a, b in pairs:
            if s.count(a) != 1:
                print("🔴 %s: 앵커가 %d개 — %s" % (f, s.count(a), a[:50])); return 1
            s = s.replace(a, b, 1)
        io.open(p, "w", encoding="utf-8").write(s)

    # ── 영속을 끈다 : `~/parking-logs` 를 **아예 안 건드린다** ──────────────
    p = os.path.join(out, "runtime.h")
    s = io.open(p, encoding="utf-8").read()
    for anchor, note in (
        ('    std::string base = (home && *home) ? (std::string(home) + "/parking-logs/parking-server")',
         '    (void)home;\n    // 🔴 **dev 는 로그 파일을 안 만든다.** 출력은 stdout 하나다.\n    return std::string();\n'),
        ('    std::string base = std::string(home) + "/parking-logs/parking-nodes";',
         '    // 🔴 **dev 는 노드 대장을 영속하지 않는다** — 운영의 `~/parking-logs` 를 안 건드린다.\n'
         '    //   ⚠ 재기동하면 "누가 있었나"를 잊는다. **개발에는 그게 맞다.**\n    return std::string();\n'),
        ('    std::string base = std::string(home) + "/parking-logs/parking-rid-cursor";',
         '    // 🔴 **dev 는 rid 커서를 영속하지 않는다.** 매 기동 임의 지점에서 시작한다.\n    return std::string();\n'),
    ):
        if s.count(anchor) != 1:
            print("🔴 runtime.h 앵커 %d개: %s" % (s.count(anchor), anchor[:50])); return 1
        s = s.replace(anchor, note + anchor, 1)
    io.open(p, "w", encoding="utf-8").write(s)

    print("✅ %s 에 만들었다 — 이제 손으로 다듬어라. 이 스크립트를 다시 돌리지 마라." % out)
    print("   남은 손질: listen.h 의 배너/계약값 덤프/기계용 INSTANCE 줄")
    return 0


if __name__ == "__main__":
    sys.exit(main())
