#!/bin/bash
# P1b 송신 간격(SendFlushIntervalMs = T) 고르기 — Continent01, Release, 같은 PC, 에디터 닫음. 인자는 P1a 기준선과 같다.
# 이름 끝의 tNN이 T(ms). 이미 끝난 실행은 다시 돌리지 않는다.
HERE="$(cd "$(dirname "$0")" && pwd)"
run() {  # T 이름 워커 봇 초당접속 초 [zone] [추가 인자]
  local t="$1"; shift
  if grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; then echo "[matrix] 있음: $1"; return; fi
  FLUSH_MS="$t" bash "$HERE/run_one.sh" "$@"
}
HOT="-14200,0,1000"
for t in 0 5 10 20 30; do run $t hot100_w6_t$t 0 100 50 45 "$HOT"; done
for t in 5 10 20; do
  run $t hot25_w6_t$t 0 25 50 45 "$HOT"
  run $t hot50_w6_t$t 0 50 50 45 "$HOT"
  run $t spread1000_w6_t$t 0 1000 200 50
done
echo "[matrix] 끝"
