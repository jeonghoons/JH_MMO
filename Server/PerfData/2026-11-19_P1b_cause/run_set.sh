#!/bin/bash
# P1b 원인 조사 — 같은 빌드로 핫존 100·분산 1,000을 2회씩 잰다(Continent01, Release, 같은 PC, 에디터 닫음. 봇 인자는 P1a 기준선과 같다).
# 사용: run_set.sh <꼬리표> [T(ms), 기본 10]   → runs/hot100_t<T>_<꼬리표>_r1·r2, runs/spread1000_t<T>_<꼬리표>_r1·r2
# 이미 끝난 실행은 다시 돌리지 않는다.
HERE="$(cd "$(dirname "$0")" && pwd)"
TAG="$1"; T="${2:-10}"
run() {
  if grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; then echo "[set] 있음: $1"; return; fi
  FLUSH_MS="$T" bash "$HERE/run_one.sh" "$@"
}
HOT="-14200,0,1000"
for r in 1 2; do
  run hot100_t${T}_${TAG}_r$r 0 100 50 45 "$HOT"
  run spread1000_t${T}_${TAG}_r$r 0 1000 200 50
done
echo "[set] 끝"
