#!/bin/bash
# 추가 측정(2026-11-17 사용자 질문: 500명일 때 핫존을 넓히면?): 500명을 레벨 중심의 넓은 원 안에 모은다.
# 분산 500(레벨 전체 = 가장 넓은 경우)이 워커 6에서 붕괴했으므로 워커 1(송신 묶기 뒤 추정)만 잰다. 본 매트릭스가 끝난 뒤 이어서 실행한다(동시에 돌리면 서로의 서버를 끈다).
HERE="$(cd "$(dirname "$0")" && pwd)"
for r in 10000 7500; do
  for rep in 1 2; do
    name="wide$((r / 100))m_500_w1_r${rep}"
    if grep -q "loadtest exit 0" "$HERE/runs/$name/args.txt" 2>/dev/null; then continue; fi
    bash "$HERE/run_one.sh" "$name" Continent01 1 500 75 "0,0,$r"
  done
done
echo "[extra] 끝"
