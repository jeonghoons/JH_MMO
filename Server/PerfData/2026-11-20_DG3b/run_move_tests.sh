#!/bin/bash
# DG3b Room 이동 시험: 시나리오 묶음마다 맞는 [Debug] 설정으로 서버를 띄우고 move_test.py를 돌린 뒤, 서버를 끄고 [metrics]의 잔류를 본다.
# 사용: bash run_move_tests.sh [Debug|Release] [묶음 이름...]   (묶음을 생략하면 전부). 결과: runs/move_<묶음>/
# 저장소의 ServerConfig.ini는 건드리지 않는다. 계정 Test1~Test4(비밀번호 1234)가 DB에 있어야 한다.
CONF="${1:-Release}"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"
ALL="plain wait timeout handoff revive"
GROUPS_TO_RUN="${*:-$ALL}"
TOTAL_FAIL=0

run_group() {  # 이름 "sed 식" 시나리오...
  local name="$1" edits="$2"; shift 2
  local RUN="$HERE/runs/move_$name"
  rm -rf "$RUN"; mkdir -p "$RUN"
  sed -e "s/^DevCommands=.*/DevCommands=true/" -e "s/^Level=.*/Level=Debug/" -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/move_$name/logs#" $edits "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  cd "$SRV"
  ../x64/$CONF/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 3
  cd "$HERE"
  python -I -X utf8 move_test.py "$@" 2>&1 | tee "$RUN/result.txt"
  sleep 12   # 마지막 [metrics] 줄에 정리 뒤 상태가 찍히게
  taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
  # 잔류: 마지막 [metrics]의 sessions·players·명부·강제 정리, 이동 지표(abortBusy/orphan/ownerBad는 0이어야 함)
  grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "sessions=[0-9]+|players=[0-9]+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|kind\(town/field players\)=\S+|roomMove\(.*" | tr '\n' ' ' | tee "$RUN/metrics_last.txt"; echo
  local last; last="$(cat "$RUN/metrics_last.txt")"
  local bad=0
  echo "$last" | grep -q "sessions=0 " || bad=1
  echo "$last" | grep -q "players=0 " || bad=1
  echo "$last" | grep -qE "forced\)=[0-9]+/[0-9]+/0 " || bad=1
  echo "$last" | grep -qE "chMax\)=[0-9]+/0/0 " || bad=1
  echo "$last" | grep -qE "ownerBad\)=\S+ 0/0/0" || bad=1
  local errs; errs="$(grep -hcE " ERROR " "$RUN"/logs/*.log)"
  [ "$errs" = "0" ] || { bad=1; grep -hE " ERROR " "$RUN"/logs/*.log | cut -c1-220 | head -5; }
  local fails; fails="$(grep -c "^FAIL" "$RUN/result.txt")"
  local passes; passes="$(grep -c "^PASS" "$RUN/result.txt")"
  [ "$passes" = "$#" ] || fails=$((fails + $# - passes))
  if [ "$bad" = "0" ] && [ "$fails" = "0" ]; then echo "GROUP PASS $name"; else echo "GROUP FAIL $name (잔류·오류 $bad, 시나리오 실패 $fails, ERROR 로그 $errs)"; TOTAL_FAIL=1; fi
}

for g in $GROUPS_TO_RUN; do
  case "$g" in
    plain)   run_group plain "" basic starts notavail trade ;;
    wait)    run_group wait "-e s/^DbSaveDelayMs=.*/DbSaveDelayMs=1500/" wait cut_wait ;;
    timeout) run_group timeout "-e s/^DbSaveDelayMs=.*/DbSaveDelayMs=7000/" timeout ;;
    handoff) run_group handoff "-e s/^MoveHandoffDelayMs=.*/MoveHandoffDelayMs=800/" cut_a cut_b basic ;;
    trade)   run_group trade "" trade trade trade ;;
    revive)  run_group revive "-e s/^DummyRoom=.*/DummyRoom=field/ -e s/^DummySpawnZone=.*/DummySpawnZone=19800,7300,500/" revive ;;
    *) echo "모르는 묶음: $g" ;;
  esac
done
exit $TOTAL_FAIL
