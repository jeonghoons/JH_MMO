#!/bin/bash
# DG2 파티 시험: 묶음마다 맞는 설정·데이터 테이블로 서버를 띄우고 party_test.py를 돌린 뒤, 서버를 끄고 [metrics]의 잔류를 본다.
# 사용: bash run_party_tests.sh [Debug|Release] [묶음 이름...]   (묶음을 생략하면 전부). 결과: runs/party_<묶음>/
# 저장소의 ServerConfig.ini·DataTable은 건드리지 않는다(사본을 고쳐 --config·JH_DATA_DIR로 준다). 계정 Test1~Test4(비밀번호 1234)가 DB에 있어야 한다.
CONF="${1:-Release}"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; TABLES="$HERE/../../../Common/DataTable"
ALL="plain loot small channel midmove"
GROUPS_TO_RUN="${*:-$ALL}"
TOTAL_FAIL=0

# csv_set <파일> <열 이름> <값>: 첫 데이터 행(그 열)의 값을 바꾼다. WorldTable은 MapName이 Continent01인 행만.
csv_set() {
  python -I -X utf8 - "$1" "$2" "$3" <<'PY'
import sys
path, col, val = sys.argv[1:4]
lines = open(path, encoding="utf-8-sig", newline="").read().split("\n")
head = lines[0].rstrip("\r").split(",")
i = head.index(col)
for n in range(1, len(lines)):
    cells = lines[n].rstrip("\r").split(",")
    if len(cells) <= i: continue
    if "MapName" in head and cells[head.index("MapName")] != "Continent01": continue
    cells[i] = val
    lines[n] = ",".join(cells) + ("\r" if lines[n].endswith("\r") else "")
    break
open(path, "w", encoding="utf-8", newline="").write("\n".join(lines))
PY
}

run_group() {  # 이름 "sed 식" "테이블 수정(파일:열:값 ...)" 시나리오...
  local name="$1" edits="$2" tables="$3"; shift 3
  local RUN="$HERE/runs/party_$name"
  rm -rf "$RUN"; mkdir -p "$RUN"
  sed -e "s/^DevCommands=.*/DevCommands=true/" -e "s/^Level=.*/Level=Debug/" -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/party_$name/logs#" $edits -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  unset JH_DATA_DIR
  if [ -n "$tables" ]; then
    cp -r "$TABLES" "$RUN/data"
    for t in $tables; do IFS=: read -r file col val <<< "$t"; csv_set "$RUN/data/$file" "$col" "$val"; done
    export JH_DATA_DIR="$(cygpath -w "$RUN/data")"
  fi
  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  cd "$SRV"
  ../x64/$CONF/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 3
  cd "$HERE"
  python -I -X utf8 party_test.py "$@" 2>&1 | tee "$RUN/result.txt"
  sleep 12   # 마지막 [metrics] 줄에 정리 뒤 상태가 찍히게
  taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
  unset JH_DATA_DIR
  # 잔류: 마지막 [metrics]의 sessions·players·명부·강제 정리·파티(지금 값 셋은 0), 이동 지표(abortBusy/orphan/ownerBad는 0)
  grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "sessions=[0-9]+|players=[0-9]+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|roomMove\([^)]*\)=\S+ \S+|party\(.*" | tr '\n' ' ' | tee "$RUN/metrics_last.txt"; echo
  local last; last="$(cat "$RUN/metrics_last.txt")"
  local bad=0
  echo "$last" | grep -q "sessions=0 " || bad=1
  echo "$last" | grep -q "players=0 " || bad=1
  echo "$last" | grep -qE "forced\)=[0-9]+/[0-9]+/0 " || bad=1
  echo "$last" | grep -qE "chMax\)=[0-9]+/0/0 " || bad=1
  echo "$last" | grep -qE "ownerBad\)=\S+ 0/0/0" || bad=1
  echo "$last" | grep -qE "disbanded\)=0/0/0 " || bad=1
  local errs; errs="$(cat "$RUN"/logs/*.log | grep -cE " ERROR ")"
  [ "$errs" = "0" ] || { bad=1; grep -hE " ERROR " "$RUN"/logs/*.log | cut -c1-220 | head -5; }
  local fails; fails="$(grep -c "^FAIL" "$RUN/result.txt")"
  local passes; passes="$(grep -c "^PASS" "$RUN/result.txt")"
  [ "$passes" = "$#" ] || fails=$((fails + $# - passes))
  if [ "$bad" = "0" ] && [ "$fails" = "0" ]; then echo "GROUP PASS $name"; else echo "GROUP FAIL $name (잔류·오류 $bad, 시나리오 실패 $fails, ERROR 로그 $errs)"; TOTAL_FAIL=1; fi
}

for g in $GROUPS_TO_RUN; do
  case "$g" in
    plain)   run_group plain "" "" basic rooms leader cut errors ;;
    loot)    run_group loot "" "EconomyTable.csv:DropOwnerSec:55" loot ;;   # 걸어가서 줍는 동안 소유 시간(30초)이 끝나지 않게
    small)   run_group small "" "EconomyTable.csv:PartyMaxMembers:3 EconomyTable.csv:PartyInviteTimeoutSec:3" full timeout ;;
    channel) run_group channel "" "WorldTable.csv:ChannelCapacity:2" channel ;;
    midmove) run_group midmove "-e s/^MoveHandoffDelayMs=.*/MoveHandoffDelayMs=800/" "EconomyTable.csv:DropOwnerSec:55" midmove ;;
    *) echo "모르는 묶음: $g" ;;
  esac
done
exit $TOTAL_FAIL
