package com.example.digitcam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 전선 형식. 기대값은 **손으로 박은 리터럴**이고 피검체에서 읽어 오지 않는다 —
 * 같은 곳에서 읽으면 둘이 같이 틀렸을 때 대조가 성립한다.
 */
class CameraShotTest {

    // ---- 하행 파싱 ---------------------------------------------------------

    @Test
    fun `SHOOT 한 줄을 요청번호로 읽는다`() {
        val d = CameraShot.parseDownlink("SHOOT,202608210001")
        assertEquals(CameraShot.Downlink.Shoot(202608210001L), d)
    }

    @Test
    fun `요청번호는 12자리라 Int 로는 넘친다`() {
        val d = CameraShot.parseDownlink("SHOOT,202608210001") as CameraShot.Downlink.Shoot
        // Int.MAX_VALUE = 2_147_483_647 보다 크다. Int 로 담았으면 여기서 값이 뭉개진다.
        assertTrue(d.shotId > Int.MAX_VALUE.toLong())
        assertEquals(202608210001L, d.shotId)
    }

    @Test
    fun `CRLF 와 앞뒤 공백을 털어낸다`() {
        assertEquals(
            CameraShot.Downlink.Shoot(202608219999L),
            CameraShot.parseDownlink("  SHOOT,202608219999\r"),
        )
    }

    @Test
    fun `번호가 숫자가 아니면 Malformed - 답할 대상이 없다`() {
        assertTrue(CameraShot.parseDownlink("SHOOT,abc") is CameraShot.Downlink.Malformed)
        assertTrue(CameraShot.parseDownlink("SHOOT,") is CameraShot.Downlink.Malformed)
        // 0 과 음수는 발급될 수 없는 값이다(yyyymmdd × 10000 + seq).
        assertTrue(CameraShot.parseDownlink("SHOOT,0") is CameraShot.Downlink.Malformed)
        assertTrue(CameraShot.parseDownlink("SHOOT,-5") is CameraShot.Downlink.Malformed)
    }

    @Test
    fun `모르는 줄은 Unknown 이고 무시된다 - 전방 호환`() {
        assertTrue(CameraShot.parseDownlink("PING") is CameraShot.Downlink.Unknown)
        assertTrue(CameraShot.parseDownlink("") is CameraShot.Downlink.Unknown)
        // 우리 것과 비슷하지만 다른 낱말도 Unknown 이다 — 서버가 하행을 늘려도 안 깨진다.
        assertTrue(CameraShot.parseDownlink("SHOOTX,1") is CameraShot.Downlink.Unknown)
    }

    // ---- 상행 인코딩 -------------------------------------------------------

    @Test
    fun `성공 응답은 명세 §4 형식이다`() {
        assertEquals(
            """{"shot":202608210001,"value":"12가3456","device":"Pixel 7"}""" + "\n",
            CameraShot.encodeSuccess(202608210001L, "12가3456", "Pixel 7"),
        )
    }

    @Test
    fun `실패 응답은 명세 §4 형식이다`() {
        assertEquals(
            """{"shot":202608210002,"error":"capture_stuck"}""" + "\n",
            CameraShot.encodeError(202608210002L, CameraShot.REASON_CAPTURE_STUCK),
        )
    }

    @Test
    fun `응답은 항상 LF 로 끝난다 - 줄 단위 전선이다`() {
        assertTrue(CameraShot.encodeSuccess(1L, "가", "d").endsWith("\n"))
        assertTrue(CameraShot.encodeError(1L, "x").endsWith("\n"))
    }

    @Test
    fun `한글은 원문 UTF-8 로 나간다 - uXXXX 로 좁히지 않는다`() {
        val line = CameraShot.encodeSuccess(1L, "12가3456", "d")
        assertTrue(line.contains("12가3456"))
        assertTrue(!line.contains("\\u"))
    }

    // ---- 이스케이프 -------------------------------------------------------

    @Test
    fun `제어문자가 줄을 쪼개지 못한다`() {
        // device 이름에 LF 가 섞여 들어오면 한 줄이 두 줄이 되고, 받는 쪽은 반쪽 JSON 을 본다.
        val line = CameraShot.encodeSuccess(1L, "12가3456", "Bad\nName")
        assertEquals(1, line.count { it == '\n' })   // 마지막 종단 LF 하나뿐이다
        assertTrue(line.contains("""Bad\nName"""))
    }

    @Test
    fun `따옴표와 역슬래시를 이스케이프한다`() {
        assertEquals("""a\"b""", CameraShot.jsonEscape("""a"b"""))
        assertEquals("""a\\b""", CameraShot.jsonEscape("""a\b"""))
    }

    // ---- 어휘 경계 ---------------------------------------------------------

    @Test
    fun `앱 사유는 서버가 만드는 낱말과 겹치지 않는다`() {
        // 겹치면 동작은 같지만(둘 다 CAM_FAILED) 대장에서 출처가 섞여, 서버가 접은 것과
        // 폰이 보고한 것을 나중에 구별할 수 없다. 사유를 추가할 때 이 시험이 잡아 준다.
        val overlap = CameraShot.APP_REASONS intersect CameraShot.SERVER_RESERVED_REASONS
        assertEquals(emptySet<String>(), overlap)
        // 🔑 분모를 따로 단언한다 — 두 집합이 비면 교집합도 비어서 위 검사가 헛통과한다.
        //
        // 🔴 6 → **11** (2026-08-27): 앱 고유 6개(권한·카메라·상한·촬영실패·촬영멈춤·그밖의실패)에
        //    **네이티브 사유 5개**(moving·no_plate·blurry·segment_fail·low_conf)가 더해졌다.
        //    ★ 그 다섯은 앱이 지어낸 이름이 아니라 **계약 §5.2 어휘를 그대로 나른 것**이다 —
        //      새 이름을 만들면 로그(네이티브 이름)와 화면(앱 이름)이 갈린다.
        // ⚠ 이 수가 늘면 **web 의 문구 표도 같이 늘어야 한다.** 모르는 코드는 화면에서 뭉개진다.
        // 🔴 **이 수가 안 맞으면 그냥 고치지 마라.** 이건 분모 검사이면서 동시에
        //    **도메인 간 통보 트립와이어**다 — 걸렸다는 것은 어휘를 바꿨다는 뜻이고,
        //    web 의 문구 표가 아직 그것을 모른다는 뜻이다.
        // 🔑 트립와이어는 **걸렸을 때 무엇을 하라고 말해야 한다.** 안 그러면 다음 사람이
        //    수만 올리고 지나간다 — 그러면 화면에서 그 사유가 뭉개진다.
        assertEquals(
            "APP_REASONS 를 바꿨다. 수만 고치지 말고 web 에 새 사유를 알려라 " +
                "(모르는 코드는 화면에서 뭉개진다). 지금 값: ${CameraShot.APP_REASONS.sorted()}",
            11,
            CameraShot.APP_REASONS.size,
        )
        assertEquals(
            "SERVER_RESERVED_REASONS 를 바꿨다면 socket 과 맞춘 것인지 확인해라",
            2,
            CameraShot.SERVER_RESERVED_REASONS.size,
        )
    }

    @Test
    fun `사유 낱말에 공백이 없다`() {
        // 대장 한 줄이 `<shot_id> <state> <req_ms> <ans_ms> <reason> <plate>` 이고 plate 가
        // 공백을 품는다. 사유에 공백이 들어가면 그 줄의 칸이 어긋난다.
        for (r in CameraShot.APP_REASONS) {
            assertTrue("공백이 있다: '$r'", r.none { it.isWhitespace() })
            assertTrue("[a-z_]+ 가 아니다: '$r'", r.all { it in 'a'..'z' || it == '_' })
        }
    }

    @Test
    fun `표지 문자열은 값이 아니다`() {
        // 서버는 번호판 자리의 "-1" 을 거절한다(명세 §2). 그래서 앱이 애초에 안 올린다 —
        // 그 규율은 ShotCoordinator 가 지키고, 여기서는 표지 값 자체를 못박아 둔다.
        assertEquals("-1", CameraShot.PLATE_MARKER)
    }

    // ---- 링크 유지 하행 PING (socket 확정 2026-08-25) ----------------------

    @Test
    fun `PING 은 모르는 줄이 아니라 자기 갈래로 간다`() {
        // 🔑 이 구분이 없으면 30초마다 "모르는 줄" 로그가 쌓여 다른 것을 덮는다.
        val d = CameraShot.parseDownlink("PING,7")
        assertEquals(CameraShot.Downlink.Ping(7L), d)
    }

    @Test
    fun `PING 도 공백과 CR 을 턴다`() {
        assertEquals(CameraShot.Downlink.Ping(1L), CameraShot.parseDownlink("  PING,1\r"))
    }

    @Test
    fun `🔴 번호가 깨져도 PING 이다 - 링크 유지라는 목적은 이뤄졌다`() {
        // 모르는 줄로 떨어뜨리면 "링크가 살아 있다" 는 사실까지 같이 버린다.
        // 못 하는 것은 유실 계산뿐이고, 그것을 -1 로 표시한다.
        assertEquals(CameraShot.Downlink.Ping(-1L), CameraShot.parseDownlink("PING,abc"))
        assertEquals(CameraShot.Downlink.Ping(-1L), CameraShot.parseDownlink("PING,"))
    }

    @Test
    fun `PING 접두를 흉내낸 다른 줄은 PING 이 아니다`() {
        // 접두가 "PING," 이라 쉼표가 없으면 다른 줄이다. 나중에 PINGX 같은 하행이 생겨도 안 먹힌다.
        assertTrue(CameraShot.parseDownlink("PING") is CameraShot.Downlink.Unknown)
        assertTrue(CameraShot.parseDownlink("PINGER,3") is CameraShot.Downlink.Unknown)
    }

    @Test
    fun `SHOOT 은 PING 추가 뒤에도 그대로 읽힌다`() {
        // 🔑 갈래를 하나 더한 뒤 **기존 갈래가 안 깨졌는지**를 같이 못박는다.
        assertEquals(CameraShot.Downlink.Shoot(202608250001L), CameraShot.parseDownlink("SHOOT,202608250001"))
    }
}
