#!/bin/bash
# RC2 Room 수용 인원 재측정 매트릭스(P1b 뒤, 500m Continent01, Room 하나, 워커 6 = 기본). 각 조합 2회.
# 이미 끝난 실행(runs/<이름>/args.txt에 "loadtest exit 0")은 다시 돌리지 않는다.
HERE="$(cd "$(dirname "$0")" && pwd)"
ONE="$HERE/run_one.sh"
run() {  # 이름 워커 봇 초 [zone] [추가 인자]
  if grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; then echo "[matrix] 있음: $1"; return; fi
  bash "$ONE" "$@"
}
passed() { [ "$(python -I -X utf8 "$HERE/analyze.py" --pass "$1")" = "O" ]; }
pair() {  # 접두사 인원 [zone] [추가 인자] — 2회 재고 둘 다 통과면 0
  run "${1}_${2}_r1" 0 "$2" 75 "${3:-}" "${4:-}"
  run "${1}_${2}_r2" 0 "$2" 75 "${3:-}" "${4:-}"
  if passed "${1}_${2}_r1" && passed "${1}_${2}_r2"; then echo "[matrix] ${1} ${2}명: 통과"; return 0; fi
  echo "[matrix] ${1} ${2}명: 초과"; return 1
}

HOT="-14200,0,1000"    # 마을 광장(시작 지점) 반경 10m
TOWN="-15000,0,6800"   # 마을 전체(중심 (-150m, 0), 평지 반경 68m — DESIGN_WORLD §15)

run idle 0 1 40

# A·B) 분산(레벨 전체 무작위): 500·250은 항상, 그 뒤 한계 찾기
pair spread 500; S500=$?
pair spread 250; S250=$?
if [ $S500 = 0 ]; then
  if pair spread 750; then pair spread 1000; fi
elif [ $S250 = 0 ]; then
  pair spread 375
fi

# C) 밀집(광장 반경 10m): 전원이 한 화면
pair hot 250 "$HOT"
pair hot 150 "$HOT"

# D) 마을 전체에 모임: 분할 뒤 "마을 Room" 정원의 근거
pair town 500 "$TOWN"
if ! pair town 250 "$TOWN"; then pair town 150 "$TOWN"; fi

# E) 분산 + 전투
pair hunt 250 "" "--profile hunt"
echo "[matrix] 끝"
