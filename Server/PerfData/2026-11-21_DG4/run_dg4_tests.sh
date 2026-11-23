#!/bin/bash
# DG4 던전 맵 시험(DG3c-1 run_dungeon_tests.sh를 새 맵에 맞춘 사본): 묶음마다 맞는 설정·데이터 테이블로 서버를 띄우고 dg4_test.py를 돌린 뒤, 서버를 끄고 [metrics]의 잔류를 본다.
# 사용: bash run_dungeon_tests.sh [Debug|Release] [묶음 이름...]   (묶음을 생략하면 전부). 결과: runs/dungeon_<묶음>/
# 저장소의 ServerConfig.ini·DataTable은 건드리지 않는다(사본을 고쳐 --config·JH_DATA_DIR로 준다). 계정 Test1~Test4(비밀번호 1234)가 DB에 있어야 한다.
# 모든 묶음은 [Debug] PortalNoRange=true(사냥터 Room 안 어디서든 입구 포털 요청 — dg4_test.py 머리말).
CONF="${1:-Release}"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; TABLES="$HERE/../../../Common/DataTable"
ALL="plain clear real"
GROUPS_TO_RUN="${*:-$ALL}"
TOTAL_FAIL=0

run_group() {  # 이름 "sed 식" "테이블 수정 묶음(tweak_tables.py, 없으면 빈 글)" 시나리오...
  local name="$1" edits="$2" tables="$3"; shift 3
  local RUN="$HERE/runs/dungeon_$name"
  rm -rf "$RUN"; mkdir -p "$RUN"
  sed -e "s/^PortalNoRange=.*/PortalNoRange=true/" -e "s/^Level=.*/Level=Debug/" -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/dungeon_$name/logs#" $edits -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  unset JH_DATA_DIR
  if [ -n "$tables" ]; then
    cp -r "$TABLES" "$RUN/data"
    python -I -X utf8 "$HERE/tweak_tables.py" "$RUN/data" "$tables" || { echo "GROUP FAIL $name (테이블 수정 실패)"; TOTAL_FAIL=1; return; }
    export JH_DATA_DIR="$(cygpath -w "$RUN/data")"
  fi
  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  cd "$SRV"
  ../x64/$CONF/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 7   # 첫 [metrics] 줄(5초)이 시험 전 상태로 찍히게
  cd "$HERE"
  python -I -X utf8 dg4_test.py "$@" 2>&1 | tee "$RUN/result.txt"
  sleep 12   # 마지막 [metrics] 줄에 정리 뒤 상태가 찍히게
  taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
  unset JH_DATA_DIR
  # 잔류: 마지막 [metrics]의 sessions·players·명부·강제 정리·파티, 이동 지표(abortBusy/orphan/ownerBad는 0),
  # 던전(지금 0·만든 수 = 닫은 수·안에 0명), 살아 있는 Room 객체 수 = 보관소의 Room 수, 몬스터·드랍 수 = 시험 전
  local pick="sessions=[0-9]+|rooms=[0-9]+|players=[0-9]+|monsters=[0-9]+|drops\(now [^)]*\)=[0-9]+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|roomMove\([^)]*\)=\S+ \S+|party\([^)]*\)=\S+ \S+ \S+|roomsAlive=[0-9]+|dungeon\([^)]*\)=\S+ \S+"
  grep -h "\[metrics\]" "$RUN"/logs/*.log | head -1 | grep -oE "$pick" | tr '\n' ' ' > "$RUN/metrics_first.txt"
  grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "$pick" | tr '\n' ' ' | tee "$RUN/metrics_last.txt"; echo
  local first last; first="$(cat "$RUN/metrics_first.txt")"; last="$(cat "$RUN/metrics_last.txt")"
  val() { echo "$1" | grep -oE "$2" | head -1 | grep -oE "[0-9]+$"; }
  local bad=0 why=""
  echo "$last" | grep -q "sessions=0 " || { bad=1; why="$why sessions"; }
  echo "$last" | grep -q "players=0 " || { bad=1; why="$why players"; }
  echo "$last" | grep -qE "forced\)=[0-9]+/[0-9]+/0 " || { bad=1; why="$why forced"; }
  echo "$last" | grep -qE "chMax\)=[0-9]+/0/0 " || { bad=1; why="$why roster"; }
  echo "$last" | grep -qE "ownerBad\)=\S+ 0/0/0" || { bad=1; why="$why roomMove"; }
  echo "$last" | grep -qE "disbanded\)=0/0/0 " || { bad=1; why="$why party"; }
  echo "$last" | grep -qE "dungeon\([^)]*\)=0/([0-9]+)/\1 0" || { bad=1; why="$why dungeon"; }
  [ "$(val "$last" "rooms=[0-9]+")" = "$(val "$last" "roomsAlive=[0-9]+")" ] || { bad=1; why="$why roomsAlive"; }
  [ "$(val "$last" "rooms=[0-9]+")" = "$(val "$first" "rooms=[0-9]+")" ] || { bad=1; why="$why rooms"; }
  [ "$(val "$last" "monsters=[0-9]+")" = "$(val "$first" "monsters=[0-9]+")" ] || { bad=1; why="$why monsters($(val "$first" "monsters=[0-9]+")->$(val "$last" "monsters=[0-9]+"))"; }
  [ "$(val "$last" "drops\(now [^)]*\)=[0-9]+")" = "0" ] || { bad=1; why="$why drops"; }
  local errs; errs="$(cat "$RUN"/logs/*.log | grep -cE " ERROR ")"
  [ "$errs" = "0" ] || { bad=1; why="$why ERROR"; grep -hE " ERROR " "$RUN"/logs/*.log | cut -c1-220 | head -5; }
  local fails; fails="$(grep -c "^FAIL" "$RUN/result.txt")"
  local passes; passes="$(grep -c "^PASS" "$RUN/result.txt")"
  [ "$passes" = "$#" ] || fails=$((fails + $# - passes))
  if [ "$bad" = "0" ] && [ "$fails" = "0" ]; then echo "GROUP PASS $name"; else echo "GROUP FAIL $name (잔류·오류:$why, 시나리오 실패 $fails, ERROR 로그 $errs)"; TOTAL_FAIL=1; fi
}

for g in $GROUPS_TO_RUN; do
  case "$g" in
    plain)   run_group plain "" "" solo isolate cut_load dup_load party revive ;;
    clear)   run_group clear "" "clear" clear ;;
    real)    run_group real "" "" real ;;
    *) echo "모르는 묶음: $g" ;;
  esac
done
exit $TOTAL_FAIL
