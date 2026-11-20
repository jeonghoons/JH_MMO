#!/bin/bash
# DG3b 부하 측정(Continent01, Release, 같은 PC, 에디터 닫음). 이미 끝난 실행은 다시 돌리지 않는다. 사용: bash run_matrix.sh
#  ① 검증 게이트: 200명 짧은 램프(실제 규칙 — 마을에서 시작, 사냥 봇은 문 포털로 나감)
#  ② 부하 회귀 A/B: hunt_250·spread_250 — pre = DG3a 커밋(16e83fe) exe(채널당 Room 하나), after = 작업 트리(더미를 사냥터 Room에 바로: DUMMYROOM=field).
#     PC 상태가 시간대에 따라 흔들리므로 pre와 after를 바로 이어서 2회씩 잰다.
#  ③ 채널 500명: 마을 250 + 사냥터 250(split) / 마을 500(town) / 사냥터 500(field) — 합격선 틱 지연 p99 20ms(D-057), 핑 p99
HERE="$(cd "$(dirname "$0")" && pwd)"
PRE_EXE="$HERE/pre/GameServer.exe"
done_run() { grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; }
one() {  # 이름 나머지 = run_one.sh 인자 (환경변수 DUMMYROOM·SERVER_EXE는 호출한 쪽이 준다)
  local name="$1"
  if done_run "$name"; then echo "[matrix] 있음: $name"; else bash "$HERE/run_one.sh" "$@"; fi
}

one gate_hunt200_town 0 200 50 60 "" "--profile hunt"
one gate_move200_town 0 200 50 60

for r in r1 r2; do
  SERVER_EXE="$PRE_EXE" one hunt_250_pre_$r 0 250 50 75 "" "--profile hunt"
  DUMMYROOM=field one hunt_250_after_$r 0 250 50 75 "" "--profile hunt"
  SERVER_EXE="$PRE_EXE" one spread_250_pre_$r 0 250 50 75
  DUMMYROOM=field one spread_250_after_$r 0 250 50 75
done
one hunt_250_town 0 250 50 75 "" "--profile hunt"

for r in r1 r2; do
  DUMMYROOM=split one ch500_split_$r 0 500 100 75
  DUMMYROOM=town one ch500_town_$r 0 500 100 75
  DUMMYROOM=field one ch500_field_$r 0 500 100 75
  DUMMYROOM=field one ch500_field_hunt_$r 0 500 100 75 "" "--profile hunt"
done
echo "[matrix] 끝"
