#!/bin/bash
# P1b(송신 묶기) 뒤 전체 매트릭스 — P1a 기준선(../2026-11-19_P1a_baseline/run_matrix.sh)과 같은 시나리오·인자.
# Continent01, Release, 같은 PC, 에디터 닫음. after = 작업 트리 빌드(T=10ms, 타이머 해상도 1ms), pre = P1b 전 exe(커밋 c7b74a0을 따로 빌드).
# PC 상태가 시간대에 따라 달라 CPU 수치가 흔들리므로(2026-11-19 오후), 시나리오마다 pre와 after를 바로 이어서 잰다.
# 사용: PRE_EXE=<P1b 전 GameServer.exe 경로> bash run_matrix.sh   (PRE_EXE를 안 주면 after만 잰다)
# 이미 끝난 실행은 다시 돌리지 않는다.
HERE="$(cd "$(dirname "$0")" && pwd)"
done_run() { grep -q "loadtest exit 0" "$HERE/runs/$1/args.txt" 2>/dev/null; }
pair() {  # 이름 회차 워커 봇 초당접속 초 [zone] [추가 인자]
  local name="$1" r="$2"; shift 2
  if [ -n "${PRE_EXE:-}" ]; then
    if done_run "${name}_pre_$r"; then echo "[matrix] 있음: ${name}_pre_$r"; else SERVER_EXE="$PRE_EXE" OLD_EXE=1 bash "$HERE/run_one.sh" "${name}_pre_$r" "$@"; fi
  fi
  if done_run "${name}_after_$r"; then echo "[matrix] 있음: ${name}_after_$r"; else FLUSH_MS=10 TIMER_MS=1 bash "$HERE/run_one.sh" "${name}_after_$r" "$@"; fi
}
HOT="-14200,0,1000"   # 마을 광장(PlayerStart) 반경 10m — 안전 구역 안
for r in r1 r2; do
  pair hot100_w6 $r 0 100 50 45 "$HOT"
  pair hot100_w1 $r 1 100 50 45 "$HOT"
  pair hot50_w6 $r  0 50  50 45 "$HOT"
  pair hot25_w6 $r  0 25  50 45 "$HOT"
  pair spread1000_w6 $r 0 1000 200 50
  pair login1000_w6 $r  0 1000 1000 30
  pair spread200_w6 $r  0 200 50 60
  pair hunt200_w6 $r    0 200 50 60 "" "--profile hunt"
done
echo "[matrix] 끝"
