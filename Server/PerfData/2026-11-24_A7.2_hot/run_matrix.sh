#!/bin/bash
# A7.2 핫존 재측정(P2 뒤 판정 범위 D-210): P1b와 같은 봇 인자(더미 이동 봇, --ramp-rate 50 --duration 45 --zone -14200,0,1000, 워커 기본 6)로
# 핫존 50·100명을 MoveBatchMs 0(P2 전 송신) / 33(P2) 각 2회. DG3b 뒤 마을이 별도 Room이라 DUMMYROOM=town(구역 좌표가 마을 안), --town-wander 없음.
# 같은 날 같은 exe의 A/B 비교 — 11-19 P1b 값과의 절대 비교는 Room 분할·맵 변경 뒤라 참고만.
HERE="$(cd "$(dirname "$0")" && pwd)"
for R in 1 2; do
  for N in 50 100; do
    for MB in 0 33; do
      DUMMYROOM=town TOWNWANDER=false MOVEBATCH=$MB bash "$HERE/run_one.sh" hot${N}_mb${MB}_r$R 0 $N 50 45 "-14200,0,1000" ""
      sleep 20
    done
  done
done
echo "matrix done"
