#!/bin/bash
# A5.4b 로그인 처리량 한계(D-302): 계정 봇(--login account, 매번 봇 계정 초기화) + --profile idle(핑만)로
#   초당 접속 50 → 100 → 200 → 400. 접속이 그 속도로 약 20초 이어지게 인원을 정하고 램프 뒤 약 30초 유지.
#   전원 계획 고성능(D-303) — 끝나면(중간에 끊겨도) 균형 조정으로 되돌린다.
#   실행 전마다 TIME_WAIT 소켓 수를 보고 3,000을 넘으면 줄 때까지 기다린다(봇이 먼저 닫아 포트가 약 2분 묶임).
# 사용: [DBCONN=4] [TAG=] bash run_login.sh [속도...]   (생략하면 50 100 200 400). 결과: runs/login_<속도><TAG>/
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
BALANCED=381b4222-f694-41f0-9685-ff5bb260df2e
HIGH=8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
trap 'powercfg //setactive $BALANCED' EXIT
powercfg //setactive $HIGH
sleep 5

wait_time_wait() {
  while true; do
    local n; n=$(netstat -an | grep -c TIME_WAIT)
    [ "$n" -le 3000 ] && { echo "[login] TIME_WAIT $n"; return; }
    echo "[login] TIME_WAIT $n — 기다림"; sleep 15
  done
}

for RATE in ${*:-50 100 200 400}; do
  case "$RATE" in
    50)  BOTS=1000; DUR=50 ;;
    100) BOTS=2000; DUR=50 ;;
    200) BOTS=4000; DUR=50 ;;
    400) BOTS=5000; DUR=45 ;;
    *)   BOTS=$((RATE * 20 > 5000 ? 5000 : RATE * 20)); DUR=50 ;;
  esac
  wait_time_wait
  LOGIN=account bash "$HERE/run_one.sh" login_${RATE}${TAG:-} 0 $BOTS $RATE $DUR "" "--profile idle"
done
echo "[login] done"
