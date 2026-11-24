#!/bin/bash
# A5.4d P2 전/후 비교(같은 PC — 서버와 봇이 CPU를 나눔, 상대 비교용): MoveBatchMs 0(P2 전 송신) vs 33(P2, D-307).
# 시나리오 = A5.4c와 같은 혼합(계정 봇 --mix 기본) — 한 Room 밀집 500명(마을), 분산 1,000·2,500명(초당 200명 접속).
# 사용: bash run_ab.sh [이름...]   (생략하면 전부, 이름 = dense500 mix1000 mix2500)
HERE="$(cd "$(dirname "$0")" && pwd)"
MIX="--mix idle=25,move=15,chat=10,hunt=20,loot=20,trade=5,auction=5"
CASES="${*:-dense500 mix1000 mix2500}"
for C in $CASES; do
  for MB in 0 33; do
    case "$C" in
      dense500) LOGIN=account DUMMYROOM=town MOVEBATCH=$MB bash "$HERE/run_one.sh" ${C}_mb$MB 0 500 100 125 "" "$MIX" ;;
      mix1000)  LOGIN=account MOVEBATCH=$MB bash "$HERE/run_one.sh" ${C}_mb$MB 0 1000 200 125 "" "$MIX" ;;
      mix2500)  LOGIN=account MOVEBATCH=$MB bash "$HERE/run_one.sh" ${C}_mb$MB 0 2500 200 135 "" "$MIX" ;;
    esac
    sleep 45   # TIME_WAIT·DB 정리
  done
done
echo "ab done"
