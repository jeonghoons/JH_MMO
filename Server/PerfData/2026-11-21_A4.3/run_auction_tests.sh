#!/bin/bash
# A4.3 경매장 시험: 묶음마다 시험 계정의 경매 흔적을 지우고(reset_db.sql), 맞는 설정·데이터 테이블로 서버를 띄워 auction_test.py를 돌린 뒤,
# 서버를 끄고 [metrics]의 잔류를 본다.
# 사용: bash run_auction_tests.sh [Debug|Release] [묶음 이름...]   (묶음을 생략하면 전부). 결과: runs/auction_<묶음>/
# 저장소의 ServerConfig.ini·DataTable은 건드리지 않는다(사본을 고쳐 --config·JH_DATA_DIR로 준다). 계정 Test1~Test4(비밀번호 1234)가 DB에 있어야 한다.
# 모든 묶음은 [Debug] DevCommands=true(시험 아이템·골드 지급)·PortalNoRange=true(던전 입구를 걷지 않고 요청).
# 경매 요청 간격(D-281 — 검색 5초·그 밖 1초)은 시험 시간을 줄이려고 모든 묶음에서 300ms로 줄인다(DEFAULT_TWEAK). 실제 값은 interval 묶음이 본다.
CONF="${1:-Debug}"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; TABLES="$HERE/../../../Common/DataTable"
ALL="plain race search interval expire limits fail busy cut restart"
DEFAULT_TWEAK="AuctionSearchIntervalMs=300 AuctionActionIntervalMs=300"
GROUPS_TO_RUN="${*:-$ALL}"
TOTAL_FAIL=0

start_server() {  # RUN 폴더
  cd "$SRV"
  ../x64/$CONF/GameServer.exe --config "$(cygpath -w "$1/ServerConfig.ini")" >> "$1/server_console.txt" 2>&1 &
  for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 7   # 첫 [metrics] 줄(5초)이 시험 전 상태로 찍히게
  cd "$HERE"
}

stop_server() {
  sleep 12   # 마지막 [metrics] 줄에 정리 뒤 상태가 찍히게
  taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
}

run_group() {  # 이름 "sed 식" "EconomyTable 수정(열=값 ..., 없으면 빈 글)" 시나리오...   (시나리오에 "--restart"가 있으면 그 자리에서 서버를 다시 띄운다)
  local name="$1" edits="$2" tables="$DEFAULT_TWEAK $3"; shift 3
  local RUN="$HERE/runs/auction_$name"
  rm -rf "$RUN"; mkdir -p "$RUN"
  sed -e "s/^DevCommands=.*/DevCommands=true/" -e "s/^PortalNoRange=.*/PortalNoRange=true/" -e "s/^Level=.*/Level=Debug/" \
      -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/auction_$name/logs#" $edits "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  unset JH_DATA_DIR
  if [ -n "$tables" ]; then
    cp -r "$TABLES" "$RUN/data"
    python -I -X utf8 "$HERE/tweak_tables.py" "$RUN/data" $tables || { echo "GROUP FAIL $name (테이블 수정 실패)"; TOTAL_FAIL=1; return; }
    export JH_DATA_DIR="$(cygpath -w "$RUN/data")"
  fi
  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -i "$(cygpath -w "$HERE/reset_db.sql")" > "$RUN/reset.txt" 2>&1 || { echo "GROUP FAIL $name (DB 정리 실패)"; cat "$RUN/reset.txt"; TOTAL_FAIL=1; return; }
  start_server "$RUN"
  local batch=() count=0
  : > "$RUN/result.txt"
  for s in "$@" "--end"; do
    if [ "$s" = "--restart" ] || [ "$s" = "--end" ]; then
      if [ ${#batch[@]} -gt 0 ]; then python -I -X utf8 auction_test.py "${batch[@]}" 2>&1 | tee -a "$RUN/result.txt"; fi
      batch=()
      if [ "$s" = "--restart" ]; then
        stop_server
        python -I -X utf8 auction_test.py restart_sql   # 꺼져 있는 동안 기간이 지난 등록을 만든다
        start_server "$RUN"
      fi
    else
      batch+=("$s"); count=$((count + 1))
    fi
  done
  stop_server
  unset JH_DATA_DIR
  # 잔류: 마지막 [metrics]의 sessions·players·명부·강제 정리, 이동 지표(abortBusy/orphan/ownerBad는 0), 진행 중인 아이템 작업·경매 DB 작업 0,
  # 던전 0, 살아 있는 Room 객체 수 = 보관소의 Room 수
  local pick="sessions=[0-9]+|rooms=[0-9]+|players=[0-9]+|itemOp\([^)]*\)=\S+ \S+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|roomMove\([^)]*\)=\S+ \S+|roomsAlive=[0-9]+|dungeon\([^)]*\)=\S+ \S+|auction\([^)]*\)=\S+ \S+ \S+ \S+"
  grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "$pick" | tr '\n' ' ' | tee "$RUN/metrics_last.txt"; echo
  local last; last="$(cat "$RUN/metrics_last.txt")"
  val() { echo "$1" | grep -oE "$2" | head -1 | grep -oE "[0-9]+$"; }
  local bad=0 why=""
  echo "$last" | grep -q "sessions=0 " || { bad=1; why="$why sessions"; }
  echo "$last" | grep -q "players=0 " || { bad=1; why="$why players"; }
  echo "$last" | grep -qE "itemOp\([^)]*\)=\S+ 0 " || { bad=1; why="$why itemOpInflight"; }
  echo "$last" | grep -qE "forced\)=[0-9]+/[0-9]+/0 " || { bad=1; why="$why forced"; }
  echo "$last" | grep -qE "chMax\)=[0-9]+/0/0 " || { bad=1; why="$why roster"; }
  echo "$last" | grep -qE "ownerBad\)=\S+ 0/0/0" || { bad=1; why="$why roomMove"; }
  echo "$last" | grep -qE "dungeon\([^)]*\)=0/([0-9]+)/\1 0" || { bad=1; why="$why dungeon"; }
  echo "$last" | grep -qE "auction\([^)]*\)=[0-9]+/0 " || { bad=1; why="$why auctionDbInFlight"; }
  [ "$(val "$last" "rooms=[0-9]+")" = "$(val "$last" "roomsAlive=[0-9]+")" ] || { bad=1; why="$why roomsAlive"; }
  # 서비스의 목록 수(마지막 [metrics]) = DB의 판매 중인 등록 수
  local listed; listed="$(echo "$last" | grep -oE "auction\([^)]*\)=[0-9]+" | grep -oE "[0-9]+$")"
  local active; active="$(sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -h -1 -W -Q "SET NOCOUNT ON; SELECT COUNT(*) FROM Auction_Listing WHERE State = 1" | tr -d '[:space:]')"
  [ "$listed" = "$active" ] || { bad=1; why="$why listings(목록 $listed != DB $active)"; }
  local errs; errs="$(cat "$RUN"/logs/*.log | grep -cE " ERROR ")"
  [ "$errs" = "0" ] || { bad=1; why="$why ERROR"; grep -hE " ERROR " "$RUN"/logs/*.log | cut -c1-240 | head -5; }
  local fails; fails="$(grep -c "^FAIL" "$RUN/result.txt")"
  local passes; passes="$(grep -c "^PASS" "$RUN/result.txt")"
  [ "$passes" = "$count" ] || fails=$((fails + count - passes))
  if [ "$bad" = "0" ] && [ "$fails" = "0" ]; then echo "GROUP PASS $name"; else echo "GROUP FAIL $name (잔류·오류:$why, 시나리오 실패 $fails, ERROR 로그 $errs)"; TOTAL_FAIL=1; fi
}

for g in $GROUPS_TO_RUN; do
  case "$g" in
    plain)   run_group plain "" "" basic partial partbuy rejects dungeon ;;
    race)    run_group race "" "" race cancelrace ;;
    search)  run_group search "" "AuctionFetchRows=4 AuctionPageRows=2" search ;;
    interval) run_group interval "" "AuctionSearchIntervalMs=5000 AuctionActionIntervalMs=1000" interval ;;
    expire)  run_group expire "" "AuctionDurationSec=6 AuctionExpireCheckSec=1" expire ;;
    limits)  run_group limits "" "AuctionMaxListings=2 GoldMax=3000 AuctionMaxPrice=2500" limits ;;
    fail)    run_group fail "-e s/^DbItemFailEvery=.*/DbItemFailEvery=2/" "" fail ;;
    busy)    run_group busy "-e s/^AuctionDbMaxInFlight=.*/AuctionDbMaxInFlight=1/ -e s/^DbSaveDelayMs=.*/DbSaveDelayMs=1500/" "" busy ;;
    cut)     run_group cut "-e s/^DbSaveDelayMs=.*/DbSaveDelayMs=1500/" "" cut movewait ;;
    restart) run_group restart "" "" restart1 --restart restart2 ;;
    *) echo "모르는 묶음: $g" ;;
  esac
done
exit $TOTAL_FAIL
