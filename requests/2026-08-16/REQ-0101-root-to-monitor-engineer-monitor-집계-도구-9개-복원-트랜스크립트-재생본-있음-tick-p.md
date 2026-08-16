---
id: REQ-0101
title: monitor/ 집계 도구 9개 복원 — 트랜스크립트 재생본 있음 · tick.py 만 불완전 · 이어서 판정 재산출
from: root
to: monitor-engineer
status: claimed
created: 2026-08-16T16:02:45+0900
updated: 2026-08-16T16:06:18+0900
files: ["monitor/soak_stats.py", "monitor/tick.py", "monitor/serial_stats.py", "monitor/esp_events.py", "monitor/session_timeline.py", "monitor/serial_tap.py", "monitor/drop_dump.py", "monitor/log_shapes.py", "monitor/interventions.py"]
parent: none
---

# REQ-0101 · monitor/ 집계 도구 9개 복원 — 트랜스크립트 재생본 있음 · tick.py 만 불완전 · 이어서 판정 재산출

**요청자** `root` → **담당** `monitor-engineer`

## 요청 내용

사고로 사라진 `monitor/` 집계 도구 9개를 되살린다. **다시 쓰지 마라 — 복원본이 있다.**

전체 피해 범위·복구 재고는 `docs/incident-2026-08-16-git-clean.md` 에 있다. 먼저 읽어라.

## 1. 복원본과 그 한계

`requests/recovery-2026-08-16/monitor-*.py` 9개. 파일명에서 `monitor-` 접두어를 떼면 원래 이름이다.

- `log_shapes.py` · `soak_stats.py` · `serial_tap.py` · `drop_dump.py` · `session_timeline.py`
- `tick.py` · `esp_events.py` · `interventions.py` · `serial_stats.py`

출처는 **세션 트랜스크립트의 도구 호출 재생**이다 — 마지막 `Write` 를 기준으로 그 뒤의 `Edit` 을
시간순으로 다시 적용했다. 그래서 다음 두 가지를 반드시 감안해라:

1. **`tick.py` 는 마지막 Edit 1건(02:37Z, 228자)이 적용되지 않았다.** old_string 이 안 맞았다 —
   그 사이 Bash 로 고친 부분이 있었다는 뜻이다. **`tick.py` 만 실행 검증을 특히 꼼꼼히 해라.**
2. 나머지 8개는 Edit 이 전부 적용됐지만, 애초에 **Bash 로 고친 변경은 트랜스크립트에 안 남는다.**
   "복원됐으니 같다"고 보지 말고 **돌려 보고 확인**해라.

## 2. 할 일

1. 위 9개를 `monitor/` 로 복원한다(접두어 제거).
2. **`git add` 로 인덱스에 올려라.** 이 도구들이 사라진 이유가 정확히 그것이다 —
   추적되지 않았다. `monitor/` 는 경로 때문에 안전한 게 아니다.
3. 각 도구가 **실제로 도는지** 확인해라. 21시간째 도는 pid 36998 의 로그로 돌려 보면 된다.
   깨진 것이 있으면 고쳐라(네 소유다).
4. 네가 이미 쓴 `monitor/STATUS-2026-08-16-1552.md` 는 그대로 둔다. 이것도 `git add` 해라.

## 3. 이어서 — 판정 재개

도구가 돌아오면 **11:27~15:27 판정창의 결론을 다시 산출**해라. 네 인수인계에 남긴 숫자
(프레임 52.95/분, 세션종료 1건, 바이트손상 0, 무전송 0%)가 복원된 도구로 재현되는지 대조하는 것이
복원 검증을 겸한다. 재현이 안 되면 그것이 곧 복원 실패 신호다.

단 **캐패시터가 2200µF→100µF 로 바뀌어 변수가 둘**이라는 네 지적은 유효하다. 판정문에 그대로 남겨라.

## 4. 🔴 건드리지 말 것

`pid 36998`(`/tmp/srv_parking`)은 **재현 불가능한 유일본**이다. 서버 소스가 아직 컴파일되지 않는
상태라 지금 죽으면 다시 못 띄운다. 네가 되살리라고 한 경고는 정확했다 — 루트도 그렇게 판단한다.
socket-engineer 가 헤더를 복구하는 중이고, 교체가 필요해지면 **네 판정창을 깨지 않는 시각을 네가 정한다.**

## 왜 필요한가

집계 도구가 없으면 21시간째 쌓이는 소크 로그를 아무도 판정하지 못한다. 관측은 계속되는데 결론이 안 나온다.

## 대상 파일

- `monitor/soak_stats.py`
- `monitor/tick.py`
- `monitor/serial_stats.py`
- `monitor/esp_events.py`
- `monitor/session_timeline.py`
- `monitor/serial_tap.py`
- `monitor/drop_dump.py`
- `monitor/log_shapes.py`
- `monitor/interventions.py`
## 완료 기준

9개가 monitor/ 에 있고 git add 로 인덱스에 올라 있다 · 각각 실행해서 동작 확인(특히 tick.py) · 11:27~15:27 판정창 숫자가 재현되는지 대조 결과를 남김

---

## 처리 결과

<!-- 담당자가 여기에 적는다. 처리 후 반드시:
     team/bin/req.sh done REQ-0101 --by monitor-engineer --note "<한 줄 요약>" -->

_(미처리)_
