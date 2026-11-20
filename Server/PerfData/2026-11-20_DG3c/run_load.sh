#!/bin/bash
# DG3c-1 부하·회귀 측정(Continent01, Release, 같은 PC, 에디터 닫음). 이미 끝난 실행은 다시 돌리지 않는다. 사용: bash run_load.sh
# 측정 1회는 run_one.sh, 요약은 summarize.py.
#  ① 회귀: hunt_250·spread_250 (더미를 사냥터 Room에 바로, 던전 없음) — DG2 기록(noparty)과 비교. 예약 잡이 약한 참조로 바뀐 영향을 본다
#  ② 인스턴스 수별: 봇을 2명씩 파티로 묶어 던전 입구 앞(사냥터)에 세우고 파티장이 입장 → 인스턴스 50·100·250개(봇 100·200·500명)에서 안의 몬스터를 사냥
#  ③ 생성·닫기 반복: 인스턴스 100개 규모에서 들어간 뒤 평균 20초마다 나왔다가 다시 들어감(PortalNoRange — 출구까지 걷지 않고 요청)
HERE="$(cd "$(dirname "$0")" && pwd)"
ONE="$HERE/run_one.sh"
ENTRANCE="19020,-18800,300"   # 던전 입구 포털 100(반경 400cm) 안
MAPS="--map ../MapResource/Export/Continent01/Logic/Continent01_Logic.json --dungeon-map ../MapResource/Export/Continent01_Dungeon/Logic/Continent01_Dungeon_Logic.json"
DUNGEON="--profile hunt --party-size 2 --dungeon $MAPS"
done_run() { grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; }
one() {
  local name="$1"
  if done_run "$name"; then echo "[load] 있음: $name"; else bash "$ONE" "$@"; fi
}

DUMMYROOM=field one hunt_250 0 250 50 75 "" "--profile hunt"
DUMMYROOM=field one spread_250 0 250 50 75
DUMMYROOM=field one dg_050 0 100 20 90 "$ENTRANCE" "$DUNGEON"
DUMMYROOM=field one dg_100 0 200 20 90 "$ENTRANCE" "$DUNGEON"
DUMMYROOM=field one dg_250 0 500 20 110 "$ENTRANCE" "$DUNGEON"
DUMMYROOM=field PORTALNORANGE=true one dg_100_churn 0 200 20 120 "$ENTRANCE" "$DUNGEON --dungeon-stay-sec 20"
echo "[load] 끝"
