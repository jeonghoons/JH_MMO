#!/bin/bash
# A5.4c 한 Room 밀집(PC 단독): 계정 봇 전원을 마을 Room에(DUMMYROOM=town), --mix 기본, 초당 100명, 램프 뒤 120초. 고성능 — 끝나면 균형 조정으로.
HERE="$(cd "$(dirname "$0")" && pwd)"
trap 'powercfg //setactive 381b4222-f694-41f0-9685-ff5bb260df2e' EXIT
powercfg //setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
MIX="--mix idle=25,move=15,chat=10,hunt=20,loot=20,trade=5,auction=5"
LOGIN=account DUMMYROOM=town bash "$HERE/run_one.sh" dense_250 0 250 100 123 "" "$MIX"
sleep 60
LOGIN=account DUMMYROOM=town bash "$HERE/run_one.sh" dense_500 0 500 100 125 "" "$MIX"
