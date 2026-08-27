#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// shots.h — 카메라 pull. **서버가 촬영을 요청하고 요청번호로 번호판을 회수한다**
//
//   계약 정본 : docs/net/SPEC-camera-pull.md
//
//   요청번호(`shot_id`) = yyyymmdd × 10000 + seq(1..9999) → 12자리라 **long long 이다**
//
// 🔴 **`rid` 와 다른 것이다.** `rid` 는 아두이노 하행(`G,rid,idx,arg,ck`) 이고 200곳에서 쓴다.
//   같은 이름을 쓰면 두 뜻이 한 낱말에 겹치고, **값이 갈리기 전까지 아무도 못 본다.**
//
// 🔑 **`"-1"` 은 값이 아니라 표지다.** 그래서 폰이 문자 그대로 `-1` 을 보내면 **거절한다** —
//   안 막으면 진짜 번호판과 표지가 같은 값이 된다.
//
// ⚠ 단독 컴파일 불가 — `epoch_ms()`·`instance_tag()` 가 먼저 있어야 한다(`server.cpp` 가 그 순서로 들인다).
//   ★ 기여자가 여는 `parking.h` 는 반대다 — 그쪽은 단독으로 선다.
// ═══════════════════════════════════════════════════════════════════════════

// 한 건. **`plate` 는 `state == CAM_READY` 일 때만 뜻이 있다.**
// ⚠ `has` 를 따로 두지 않고 `state` 하나로 말한다 — 두 필드가 갈리면 어느 쪽이 참인지 모른다.
struct Shot {
    long long   id;        // yyyymmdd*10000 + seq
    int         state;     // CameraState — CAM_PENDING / CAM_READY / CAM_FAILED
    long long   req_ms;    // 요청 시각(벽시계)
    long long   ans_ms;    // 응답 시각. 0 = 아직
    std::string plate;     // CAM_READY 일 때만
    // 🔴 **서버는 이 값을 해석하지 않는다.** 기록만 한다 —
    //   해석하면 같은 규칙이 앱과 서버 두 곳에 생기고, 갈리는 순간 관대한 쪽이 이긴다.
    //   🔑 그래서 어휘가 늘어도 서버 코드가 안 바뀐다. **늘리는 것은 앱의 자유다**
    //   ⚠ 다만 **대장에 남긴다** — 안 남기면 나중에 *"왜 실패했나"* 를 셀 수 없다(android 지적).
    std::string reason;    // CAM_FAILED 일 때만. 앱이 준 낱말 그대로
    Shot() : id(0), state(CAM_PENDING), req_ms(0), ans_ms(0) {}
};

static const size_t SHOT_RING_CAP = 100;   // 사용자 확정: 최근 100개, 오래된 순 삭제
static const int    SHOT_SEQ_MAX  = 9999;  // 사용자 확정: seq 는 10000자리

// 오늘 날짜를 yyyymmdd 로. **벽시계 기준이다**(요청번호가 사람이 읽는 값이라 그렇다).
static long long shot_today_yyyymmdd() {
    time_t tt = (time_t)(epoch_ms() / 1000);
    struct tm* lt = localtime(&tt);
    if (!lt) return 0;
    return (long long)(lt->tm_year + 1900) * 10000
         + (long long)(lt->tm_mon + 1) * 100
         + (long long)lt->tm_mday;
}

// 🔴 대장 파일 경로 — **오프셋이 다르면 갈린다.**
//   안 가르면 **시험 인스턴스가 운영 대장을 덮어쓴다.** `rid` 커서에서 이미 그렇게 했고
//   같은 함수(`instance_tag()`)를 쓴다 — 두 규칙이 갈리지 않게.
static std::string shots_path() {
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = getenv("USERPROFILE");
#endif
    if (!home || !*home) return std::string();   // 빈 값 = 영속 불가. 호출자가 크게 남긴다
    return std::string(home) + "/parking-logs/parking-shots" + instance_tag() + ".txt";
}
