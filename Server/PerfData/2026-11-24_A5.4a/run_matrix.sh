#!/bin/bash
# A5.4a 이동 틱 지연 원인 조사(D-242): PL1 분산(이동만)과 같은 조건(DUMMYROOM=field, 더미 로그인, --profile move, 이동 200ms)으로
#   전원 계획(균형 조정 / 고성능) × 서버 TcpNoDelay(false / true) × 인원(250 = 2회씩, 1,000 = 1회씩).
#   끝나면(중간에 끊겨도) 전원 계획을 균형 조정으로 되돌린다. 결과: runs/<전원>_<인원>_<nodelay>_<회차>/
# 사용: bash run_matrix.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
BALANCED=381b4222-f694-41f0-9685-ff5bb260df2e
HIGH=8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
trap 'powercfg //setactive $BALANCED' EXIT

for POWER in bal hp; do
  if [ "$POWER" = hp ]; then powercfg //setactive $HIGH; else powercfg //setactive $BALANCED; fi
  sleep 5
  for ROUND in 1 2; do
    for ND in false true; do
      DUMMYROOM=field NODELAY=$ND bash "$HERE/run_one.sh" ${POWER}_m250_nd${ND}_$ROUND 0 250 50 75 "" "--profile move"
    done
  done
  for ND in false true; do
    DUMMYROOM=field NODELAY=$ND bash "$HERE/run_one.sh" ${POWER}_m1000_nd${ND}_1 0 1000 100 90 "" "--profile move"
  done
done
echo "[matrix] done"
