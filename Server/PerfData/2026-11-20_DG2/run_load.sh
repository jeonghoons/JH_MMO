#!/bin/bash
# DG2 파티 부하·회귀 측정(Continent01, Release, 같은 PC, 에디터 닫음). 이미 끝난 실행은 다시 돌리지 않는다. 사용: bash run_load.sh
# 측정 1회는 run_one.sh(DG3b의 것과 같음), 요약은 summarize.py.
#  ① 회귀: hunt_250·spread_250 (더미를 사냥터 Room에 바로) — 파티 없이. DG3b 기록(같은 조건의 after)과 비교
#  ② 파티 부하: 같은 조건 + --party-size 4 --party-churn-sec 20 (4명씩 파티, 평균 20초마다 탈퇴 → 다시 초대). ①과 번갈아 2회씩
#  ③ 마을·사냥터에 흩어진 파티: 실제 규칙(마을에서 시작, 사냥 봇은 문 포털로 나감 — 이동할 때마다 SC_PARTY_STATE 재전송)
HERE="$(cd "$(dirname "$0")" && pwd)"
ONE="$HERE/run_one.sh"   # DG3b의 것을 복사(결과가 이 폴더 runs/ 에 남게)
PARTY="--party-size 4 --party-churn-sec 20"
done_run() { grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; }
one() {
  local name="$1"
  if done_run "$name"; then echo "[load] 있음: $name"; else bash "$ONE" "$@"; fi
}

for r in r1 r2; do
  DUMMYROOM=field one hunt_250_noparty_$r 0 250 50 75 "" "--profile hunt"
  DUMMYROOM=field one hunt_250_party_$r 0 250 50 75 "" "--profile hunt $PARTY"
  DUMMYROOM=field one spread_250_noparty_$r 0 250 50 75
  DUMMYROOM=field one spread_250_party_$r 0 250 50 75 "" "$PARTY"
done
one hunt_250_town_party 0 250 50 75 "" "--profile hunt $PARTY"
DUMMYROOM=split one ch500_split_party 0 500 100 75 "" "$PARTY"
echo "[load] 끝"
