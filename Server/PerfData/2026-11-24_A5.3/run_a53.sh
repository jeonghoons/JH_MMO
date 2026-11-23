#!/bin/bash
# A5.3 클라이언트 지표 확인: A5.2 mix_1000과 같은 인자(계정 봇 1,000명, --mix 기본 비율 D-298, 50명/초, 150초). 결과: runs/mix_1000/
HERE="$(cd "$(dirname "$0")" && pwd)"
LOGIN=account bash "$HERE/run_one.sh" mix_1000 0 1000 50 150 "" "--mix idle=25,move=15,chat=10,hunt=20,loot=20,trade=5,auction=5"
