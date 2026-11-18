#!/bin/bash
# P1b(송신 묶기) 전 기준선 매트릭스 — Continent01, Release, 같은 PC, 에디터 닫음. 인자는 PERF_BASELINE_2026-11-06.md §1과 같다.
# 이미 끝난 실행(runs/<이름>/args.txt에 "loadtest exit 0")은 다시 돌리지 않는다. P1b 뒤에는 이 폴더를 복사해 같은 스크립트로 다시 잰다.
HERE="$(cd "$(dirname "$0")" && pwd)"
run() {  # 이름 워커 봇 초당접속 초 [zone] [추가 인자]
  if grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; then echo "[matrix] 있음: $1"; return; fi
  bash "$HERE/run_one.sh" "$@"
}
HOT="-14200,0,1000"   # 마을 광장(PlayerStart) 반경 10m — 안전 구역 안
for r in r1 r2; do
  run hot100_w6_$r 0 100 50 45 "$HOT"
  run hot100_w1_$r 1 100 50 45 "$HOT"
  run hot50_w6_$r  0 50  50 45 "$HOT"
  run hot25_w6_$r  0 25  50 45 "$HOT"
  run spread1000_w6_$r 0 1000 200 50
  run login1000_w6_$r  0 1000 1000 30
  run spread200_w6_$r  0 200 50 60
  run hunt200_w6_$r    0 200 50 60 "" "--profile hunt"
done
echo "[matrix] 끝"
