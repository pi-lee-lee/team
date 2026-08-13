#!/usr/bin/env bash
# team-status.sh — 팀 현황 한 장. 루트 에이전트가 관리감독에 쓰는 기본 도구.
#
#   AGENTS   에이전트별 기동 상태 + 등록 여부 + 미결 요청 수
#   OPEN     날짜를 가로지르는 미결 요청 목록
#   RECENT   최근 요청 이벤트(원장 꼬리)
#
# 사용법: team/bin/team-status.sh [-n <최근 이벤트 수>]
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

n=10
[ "${1:-}" = "-n" ] && n="${2:-10}"

echo "══ AGENTS ══════════════════════════════════════════════════"
# 파이썬을 heredoc 으로 넘기면 stdin 이 스크립트로 점유되어 파이프로 들어온 JSON 을
# 못 읽는다(초기 버전의 버그: 전원 살아 있는데 "미기동"으로 보였다).
# 그래서 목록 조회 자체를 teamctl.py 안에서 하도록 옮겼다.
python3 "$BIN_DIR/teamctl.py" agents-table

echo
echo "══ OPEN (미결 요청) ════════════════════════════════════════"
if [ -d "$OPEN_DIR" ] && [ -n "$(ls -A "$OPEN_DIR" 2>/dev/null)" ]; then
  bash "$BIN_DIR/req.sh" list
else
  echo "  (없음)"
fi

echo
echo "══ RECENT (최근 이벤트) ════════════════════════════════════"
if [ -f "$INDEX" ]; then
  tail -n "$n" "$INDEX"
else
  echo "  (원장 없음 — 아직 요청이 발행되지 않았다)"
fi
