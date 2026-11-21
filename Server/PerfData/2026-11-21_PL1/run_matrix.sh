#!/bin/bash
# PL1 사전 램프(Continent01, Release, 같은 PC, 에디터 닫음). 이미 끝난 실행은 다시 돌리지 않는다. 사용: bash run_matrix.sh [묶음...]
# 더미는 모두 사냥터 Room에 바로(DUMMYROOM=field — DG3b·A4.3 회귀와 같은 조건). 채널 정원 500(WorldTable)이라 750명부터 채널 2(Room 4개).
#  main  : 사냥(hunt)·분산(move) 각각 250 → 500 → 750 → 1,000. 250은 초당 접속 50·75초(기존 hunt_250·spread_250과 같은 인자), 그 위는 초당 100·90초.
#  sentinel: 맨 앞·맨 뒤 spread_250 — "PC 상태 두 모드"(핑 p99 약 15 / 약 57ms) 확인용.
#  diag  : 원인 가르기 — idle_1000(핑만: 이동 패킷이 없을 때), spread_1000_bt8(봇 스레드 8: 봇 쪽 한계인지), spread_1000_w12(서버 워커 12), spread_1000_mi1000(이동 간격 1초 — 들어오는 패킷 양을 사냥 1,000명 수준으로: 양 탓인지 이동 양상 탓인지).
HERE="$(cd "$(dirname "$0")" && pwd)"
done_run() { grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; }
one() {  # 이름 나머지 = run_one.sh 인자
  local name="$1"
  if done_run "$name"; then echo "[matrix] 있음: $name"; else DUMMYROOM=field bash "$HERE/run_one.sh" "$@"; fi
}
GROUPS_="${*:-main diag}"

if [[ " $GROUPS_ " == *" main "* ]]; then
  one s0_spread_250 0 250 50 75
  one hunt_250 0 250 50 75 "" "--profile hunt"
  one hunt_500 0 500 100 75 "" "--profile hunt"
  one hunt_750 0 750 100 90 "" "--profile hunt"
  one hunt_1000 0 1000 100 90 "" "--profile hunt"
  one spread_250 0 250 50 75
  one spread_500 0 500 100 75
  one spread_750 0 750 100 90
  one spread_1000 0 1000 100 90
  one s1_spread_250 0 250 50 75
fi
if [[ " $GROUPS_ " == *" diag "* ]]; then
  one idle_1000 0 1000 100 90 "" "--profile idle"
  BOTTHREADS=8 one spread_1000_bt8 0 1000 100 90
  one spread_1000_w12 12 1000 100 90
  one spread_1000_mi1000 0 1000 100 90 "" "--move-interval 1000"
fi
echo "[matrix] 끝"
