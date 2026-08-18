#!/bin/sh
# 창 G 기준선 주입 — 라운드 반복 감독기 (루트 요청 · 2026-08-18)
#
# 🔴 **정지 조건을 끄지 않는다.** 장치가 오프라인이면 각 주입기는 규칙대로 멈추고,
#    이 감독기가 잠깐 쉬었다가 **새 라운드**를 연다.
#    ⚠ 조건을 완화해 계속 돌리면 **죽은 장치에 계속 넣게 된다** — 그래서 재시작 쪽을 골랐다.
#    (조건을 지키면서 부하를 이어 가는 방법이 조건을 무르게 하는 것보다 낫다)
#
# ⚠ 라운드 수는 **유한하다.** 감시 없는 무한 루프를 만들지 않는다.
ROUNDS=${1:-8}
# 🔑 **회차 표는 부하 인자가 아니다.** 라운드·인스턴스·건수·간격은 절대 바꾸지 않되,
#    rid 접두와 기록 파일은 창마다 갈라야 한다 — 안 그러면 append-only 로그에서 짝이 섞인다.
TAG=${2:-G}
LOG=net/inject-win${TAG}-2026-08-18.log
r=1
while [ "$r" -le "$ROUNDS" ]; do
  echo "$(date '+%Y-%m-%d %H:%M:%S')  라운드 $r/$ROUNDS 시작" >> "$LOG"
  i=1
  for S in A1 A2 A3 A4 A5 B1 B2 B3; do
    python3 net/downlink_inject.py --port 9900 --allow-production --slot "$S" \
      --interval 0.3 --start-delay 0 --count 60 --rid-prefix "inj${TAG}${r}-$i" \
      --stop-queue-full 20 --log "$LOG" > /dev/null 2>&1 &
    i=$((i + 1))
  done
  wait
  echo "$(date '+%Y-%m-%d %H:%M:%S')  라운드 $r 종료" >> "$LOG"
  sleep 5
  r=$((r + 1))
done
echo "$(date '+%Y-%m-%d %H:%M:%S')  창 ${TAG} 주입 전체 종료" >> "$LOG"
