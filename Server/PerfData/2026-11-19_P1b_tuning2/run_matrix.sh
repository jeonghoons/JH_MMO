#!/bin/bash
# P1b 송신 간격(SendFlushIntervalMs = T) 고르기 2차 — 원인 조사(../2026-11-19_P1b_cause/) 수정 반영 뒤.
# Continent01, Release, 같은 PC, 에디터 닫음, 타이머 해상도 1ms. 봇 인자는 P1a 기준선과 같다.
# 이름: <시나리오>_t<T>_r<회차>. 이미 끝난 실행은 다시 돌리지 않는다. 회차를 바깥 반복으로 두어 시간에 따른 PC 상태 변화가 T마다 고르게 섞이게 한다.
HERE="$(cd "$(dirname "$0")" && pwd)"
export TIMER_MS=1
run() {  # T 이름 워커 봇 초당접속 초 [zone]
  local t="$1"; shift
  if grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; then echo "[matrix] 있음: $1"; return; fi
  FLUSH_MS="$t" bash "$HERE/run_one.sh" "$@"
}
HOT="-14200,0,1000"
for r in 1 2; do
  for t in 5 10 15 20; do
    run $t hot100_t${t}_r$r 0 100 50 45 "$HOT"
    run $t hot50_t${t}_r$r 0 50 50 45 "$HOT"
    run $t hot25_t${t}_r$r 0 25 50 45 "$HOT"
    run $t spread1000_t${t}_r$r 0 1000 200 50
  done
done
echo "[matrix] 끝"
