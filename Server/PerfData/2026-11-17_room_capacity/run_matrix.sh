#!/bin/bash
# 측정 매트릭스(DESIGN_WORLD §14-2, D-176 — 2026-11-17 사용자 의견으로 순서 변경).
# 큰 인원부터 내려가며 잰다. 워커 설정(6 = 기본, 1)마다 2회 모두 통과한 인원이 나오면 그 설정은 더 작은 인원을 건너뛴다(작을수록 가벼움).
# 밀집은 기존 기록(PERF_BASELINE 핫존: 반경 10m, 25·50·100명)과 같은 조건을 먼저 재고, 100명을 통과한 설정만 250명으로 올린다.
# 이미 끝난 실행(runs/<이름>/args.txt에 "loadtest exit 0")은 다시 돌리지 않는다.
HERE="$(cd "$(dirname "$0")" && pwd)"
ONE="$HERE/run_one.sh"
run() {  # 이름 맵 워커 봇 초 [zone]
  if grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; then echo "[matrix] 있음: $1"; return; fi
  bash "$ONE" "$@"
}
passed() { [ "$(python -I -X utf8 "$HERE/analyze.py" --pass "$1")" = "O" ]; }

# 1) 기준 재현(옛 맵 핫존 100, 45초)과 유휴
run base_paragon_hot100_w6 ParagonSample 0 100 45 "3150,-1130,1000"
run base_paragon_hot100_w1 ParagonSample 1 100 45 "3150,-1130,1000"
run idle_c01_w6 Continent01 0 1 40

# 워커 설정 하나로 한 인원을 2회 재고, 둘 다 통과면 0을 돌려준다
pair() {  # 접두사 인원 워커이름 워커수 zone
  run "${1}_${2}_${3}_r1" Continent01 "$4" "$2" 75 "$5"
  run "${1}_${2}_${3}_r2" Continent01 "$4" "$2" 75 "$5"
  passed "${1}_${2}_${3}_r1" && passed "${1}_${2}_${3}_r2"
}
descend() {  # 접두사 zone 인원들(큰 것부터)
  local prefix="$1" zone="$2"; shift 2
  local done6=0 done1=0
  for n in "$@"; do
    if [ $done6 = 0 ]; then if pair "$prefix" "$n" w6 0 "$zone"; then done6=1; echo "[matrix] $prefix w6: ${n}명 통과 → 더 작은 인원은 건너뜀"; else echo "[matrix] $prefix w6: ${n}명 초과"; fi; fi
    if [ $done1 = 0 ]; then if pair "$prefix" "$n" w1 1 "$zone"; then done1=1; echo "[matrix] $prefix w1: ${n}명 통과 → 더 작은 인원은 건너뜀"; else echo "[matrix] $prefix w1: ${n}명 초과"; fi; fi
  done
}

# 2) 분산: 500명부터. 150명 아래는 재지 않는다(2026-11-18 사용자: 채널 500명이 목표라 150명 아래 결과는 필요 없음)
descend spread "" 500 250 150

# 3) 밀집: 기존 기록(PERF_BASELINE 핫존)과 같은 반경 10m, 시작 지점 (-8800, 0). 100명 이하는 옛 기록과 기준 재현으로 갈음하고 250 → 150만 잰다
HOT="-8800,0,1000"
descend hot "$HOT" 250 150

# 4) 500명을 넓은 원에 모은 경우(워커 1만) — run_extra.sh
bash "$HERE/run_extra.sh"
echo "[matrix] 끝"
