#!/bin/bash
# A4.3 회귀: 경매를 넣은 서버로 앞 항목의 원시 소켓 시나리오(DG3b Room 이동·DG4 던전)를 다시 돌리고, 더미 봇 250명 사냥 램프(DG3c의 hunt_250과 같은 인자)를 잰다.
# 사용: bash run_regress.sh [raw] [load]   (생략하면 둘 다). 결과: runs/regress_raw/, runs/hunt_250/
# 앞 항목 폴더의 runs/(커밋된 결과)는 건드리지 않는다 — 시나리오 스크립트만 빌려 쓰고 결과는 이 폴더에 둔다.
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"
WHAT="${*:-raw load}"
FAIL=0

if echo "$WHAT" | grep -qw raw; then
  RUN="$HERE/runs/regress_raw"
  rm -rf "$RUN"; mkdir -p "$RUN"
  sed -e "s/^DevCommands=.*/DevCommands=true/" -e "s/^PortalNoRange=.*/PortalNoRange=true/" -e "s/^Level=.*/Level=Debug/" \
      -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/regress_raw/logs#" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  unset JH_DATA_DIR
  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  cd "$SRV"
  ../x64/Debug/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 7
  : > "$RUN/result.txt"
  # DG3b: 기본 이동·시작 지점·갈 수 없는 포털·거래 중 이동(PortalNoRange를 켜면 거리 거부를 보는 basic의 일부가 달라지므로 거리와 무관한 것만)
  (cd "$HERE/../2026-11-20_DG3b" && python -I -X utf8 move_test.py starts trade) 2>&1 | tee -a "$RUN/result.txt"
  # DG4(지금 던전 맵에 맞춘 DG3c 시나리오): 혼자 입장·격리·로딩 대기 중 끊김·중복 로그인·파티 동반 입장·던전 안 부활
  (cd "$HERE/../2026-11-21_DG4" && python -I -X utf8 dg4_test.py solo isolate cut_load dup_load party revive) 2>&1 | tee -a "$RUN/result.txt"
  sleep 12
  taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
  last="$(grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "sessions=[0-9]+|players=[0-9]+|itemOp\([^)]*\)=\S+ \S+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|roomMove\([^)]*\)=\S+ \S+|party\([^)]*\)=\S+ \S+ \S+|dungeon\([^)]*\)=\S+ \S+|auction\([^)]*\)=\S+" | tr '\n' ' ')"
  echo "$last" | tee "$RUN/metrics_last.txt"
  errs="$(cat "$RUN"/logs/*.log | grep -cE " ERROR ")"
  fails="$(grep -c "^FAIL" "$RUN/result.txt")"; passes="$(grep -c "^PASS" "$RUN/result.txt")"
  ok=1
  echo "$last" | grep -q "sessions=0 " || ok=0
  echo "$last" | grep -q "players=0 " || ok=0
  echo "$last" | grep -qE "forced\)=[0-9]+/[0-9]+/0 " || ok=0
  echo "$last" | grep -qE "chMax\)=[0-9]+/0/0 " || ok=0
  echo "$last" | grep -qE "ownerBad\)=\S+ 0/0/0" || ok=0
  echo "$last" | grep -qE "dungeon\([^)]*\)=0/([0-9]+)/\1 0" || ok=0
  if [ "$ok" = "1" ] && [ "$fails" = "0" ] && [ "$passes" = "8" ] && [ "$errs" = "0" ]; then echo "REGRESS RAW PASS ($passes 시나리오)"; else echo "REGRESS RAW FAIL (PASS $passes/8, FAIL $fails, ERROR 로그 $errs)"; FAIL=1; grep -hE " ERROR " "$RUN"/logs/*.log | cut -c1-220 | head -5; fi
fi

if echo "$WHAT" | grep -qw load; then
  # DG3c의 run_one.sh를 이 폴더에서 실행하도록 복사해 쓴다(결과 폴더가 스크립트 위치 기준이라)
  cp "$HERE/../2026-11-20_DG3c/run_one.sh" "$HERE/run_one.sh"
  DUMMYROOM=field bash "$HERE/run_one.sh" hunt_250 0 250 50 75 "" "--profile hunt"
  python -I -X utf8 - "$HERE/runs/hunt_250/server_metrics.csv" "$HERE/../2026-11-20_DG3c/runs/hunt_250/server_metrics.csv" <<'PY'
import csv, statistics, sys
def read(path):
    rows = [r for r in csv.DictReader(open(path, encoding="utf-8")) if int(float(r["players"])) >= 250]
    late = [float(r["tick_late_p99_ms"]) for r in rows]
    body = [float(r["tick_body_p99_ms"]) for r in rows]
    wait = [float(r["job_wait_p99_ms"]) for r in rows]
    return len(rows), statistics.median(late), max(late), statistics.median(body), max(wait)
for label, path in (("A4.3 ", sys.argv[1]), ("DG3c ", sys.argv[2])):
    try:
        n, late_med, late_max, body_med, wait_max = read(path)
        print(f"{label}hunt_250: 표본 {n}행, 틱 지연 p99 중앙값 {late_med:.1f}ms(최대 {late_max:.1f}), 틱 본문 p99 중앙값 {body_med:.2f}ms, 잡 대기 p99 최대 {wait_max:.0f}ms")
    except Exception as e:
        print(f"{label}hunt_250: 읽지 못함 ({e})")
PY
  grep -E "\[summary\]" "$HERE/runs/hunt_250/loadtest_console.txt" | head -6
  errs="$(cat "$HERE"/runs/hunt_250/logs/*.log | grep -cE " (WARN|ERROR) ")"
  echo "hunt_250 WARN·ERROR 로그 $errs"
fi
exit $FAIL
