#!/bin/bash
# DG3a 부하 회귀 — "수정 전과 같음" 판정(D-222). P1b 매트릭스(../2026-11-19_P1b_after/run_matrix.sh)와 같은 8개 시나리오 + 부하 회귀 hunt_250·spread_250.
# Continent01, Release, 같은 PC, 에디터 닫음. pre = 커밋 d23413c(DG3a 전) exe + datatable_pre(RoomCapacity 500),
# after = 작업 트리 빌드 + datatable_post(ChannelCapacity 500) — 양쪽 모두 500명마다 Room 하나라 Room 구성이 같다.
# PC 상태가 시간대에 따라 흔들리므로 시나리오마다 pre와 after를 바로 이어서 잰다. 이미 끝난 실행은 다시 돌리지 않는다.
# 사용: bash run_matrix.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
PRE_EXE="$HERE/pre/GameServer.exe"
done_run() { grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; }
pair() {  # 이름 회차 워커 봇 초당접속 초 [zone] [추가 인자]
  local name="$1" r="$2"; shift 2
  if done_run "${name}_pre_$r"; then echo "[matrix] 있음: ${name}_pre_$r"; else SERVER_EXE="$PRE_EXE" bash "$HERE/run_one.sh" "${name}_pre_$r" "$@"; fi
  if done_run "${name}_after_$r"; then echo "[matrix] 있음: ${name}_after_$r"; else bash "$HERE/run_one.sh" "${name}_after_$r" "$@"; fi
}
HOT="-14200,0,1000"   # 마을 광장(PlayerStart) 반경 10m — 안전 구역 안
for r in r1 r2; do
  pair hot100_w6 $r 0 100 50 45 "$HOT"
  pair hot100_w1 $r 1 100 50 45 "$HOT"
  pair hot50_w6 $r  0 50  50 45 "$HOT"
  pair hot25_w6 $r  0 25  50 45 "$HOT"
  pair spread1000_w6 $r 0 1000 200 50
  pair login1000_w6 $r  0 1000 1000 30
  pair spread200_w6 $r  0 200 50 60
  pair hunt200_w6 $r    0 200 50 60 "" "--profile hunt"
  pair hunt_250 $r      0 250 50 75 "" "--profile hunt"
  pair spread_250 $r    0 250 50 75
done
echo "[matrix] 끝"
