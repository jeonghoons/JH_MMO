#!/bin/bash
# spread_250 추가 표본(r3~r6): 매트릭스 r1·r2에서 PC 상태가 두 모드(핑 p99 약 15 / 약 57ms)로 갈려, pre·after 순서를 번갈아 가며 더 잰다.
HERE="$(cd "$(dirname "$0")" && pwd)"
pre()   { SERVER_EXE="$HERE/pre/GameServer.exe" bash "$HERE/run_one.sh" "spread_250_pre_$1" 0 250 50 75; }
after() { bash "$HERE/run_one.sh" "spread_250_after_$1" 0 250 50 75; }
after r3; pre r3; pre r4; after r4; after r5; pre r5; pre r6; after r6
echo "[extra] 끝"
