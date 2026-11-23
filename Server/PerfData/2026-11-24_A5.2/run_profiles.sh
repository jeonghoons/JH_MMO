#!/bin/bash
# A5.2 프로파일별 짧은 램프(검증 게이트 200명, D-147) + A5.1과 같은 조건의 사냥 250명 비교. 결과: runs/<이름>/
# 사용: bash run_profiles.sh [이름...]  (생략하면 전부). 계정 봇 실행은 run_one.sh가 매번 bot_accounts.sql로 봇 계정을 되돌린다.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ALL="hunt250_cmp idle_200 move_200 chat_200 hunt_200 loot_200 trade_200 auction_200"
for N in ${*:-$ALL}; do
  case "$N" in
    hunt250_cmp) LOGIN=dummy   bash "$HERE/run_one.sh" $N 0 250 50 75 "" "--profile hunt" ;;
    idle_200)    LOGIN=dummy   bash "$HERE/run_one.sh" $N 0 200 50 90 "" "--profile idle" ;;
    move_200)    LOGIN=dummy   bash "$HERE/run_one.sh" $N 0 200 50 90 "" "--profile move" ;;
    chat_200)    LOGIN=dummy   bash "$HERE/run_one.sh" $N 0 200 50 90 "" "--profile chat" ;;
    hunt_200)    LOGIN=dummy   bash "$HERE/run_one.sh" $N 0 200 50 90 "" "--profile hunt" ;;
    loot_200)    LOGIN=account bash "$HERE/run_one.sh" $N 0 200 50 90 "" "--profile loot" ;;
    trade_200)   LOGIN=account bash "$HERE/run_one.sh" $N 0 200 50 90 "" "--profile trade" ;;
    auction_200) LOGIN=account bash "$HERE/run_one.sh" $N 0 200 50 90 "" "--profile auction" ;;
    *) echo "unknown $N" ;;
  esac
done
