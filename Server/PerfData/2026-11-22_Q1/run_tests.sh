#!/bin/bash
# Q1 퀘스트 + 수령함 알림 시험: 묶음마다 시험 계정을 시험 전 상태로 되돌리고(restore_accounts.sql), 맞는 설정·데이터 테이블 사본으로
# 서버(Debug, 포트 8897)를 띄워 quest_test.py를 돌린 뒤, 서버를 끄고 [metrics]의 잔류를 본다. 끝나면 계정을 한 번 더 되돌린다.
# 사용: bash run_tests.sh [묶음 이름...]   (생략하면 main cut dungeon mail err fail). 결과: runs/<묶음>/
# 저장소의 ServerConfig.ini·Common/DataTable은 건드리지 않는다(사본을 고쳐 --config·JH_DATA_DIR로 준다).
# 다른 GameServer(주 줄)는 건드리지 않게 이 스크립트가 띄운 PID만 끈다(B2 run_tests.sh와 같은 방식).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRV="$HERE/../../Server"
REPO="$HERE/../../.."
PORT=8897
BUNDLES="${*:-main cut dungeon mail err fail}"
TOTAL_FAIL=0

# -f 65001: SQL 파일을 UTF-8로 읽는다 — 빠지면 sqlcmd가 CP949로 읽어 한글 주석의 끝 바이트가 줄바꿈을 먹고 다음 줄의 문장이 주석에 붙는다(그 문장이 조용히 빠짐).
# 복원 뒤 시험 계정에 퀘스트 행이 남아 있으면(Quests 열이 0이 아니면) 실패로 본다.
restore() {
  sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -f 65001 -h -1 -W -i "$(cygpath -w "$HERE/restore_accounts.sql")" > "$1" 2>&1 || return 1
  grep -qE "^4 71 0\s*$" "$1"
}

for B in $BUNDLES; do
  RUN="$HERE/runs/$B"
  rm -rf "$RUN"; mkdir -p "$RUN"
  DEV=true; DELAY=0; FAILEVERY=0; NORANGE=false; TABLES=""
  case "$B" in
    main)    ;;
    cut)     DELAY=1500 ;;
    dungeon) NORANGE=true; TABLES=dungeon ;;
    mail)    TABLES=mail ;;
    err)     DEV=false; DELAY=400 ;;
    fail)    FAILEVERY=2 ;;
    *) echo "모르는 묶음: $B"; TOTAL_FAIL=1; continue ;;
  esac
  sed -e "s/^Port=.*/Port=$PORT/" \
      -e "s/^DevCommands=.*/DevCommands=$DEV/" \
      -e "s/^PortalNoRange=.*/PortalNoRange=$NORANGE/" \
      -e "s/^DbSaveDelayMs=.*/DbSaveDelayMs=$DELAY/" \
      -e "s/^DbItemFailEvery=.*/DbItemFailEvery=$FAILEVERY/" \
      -e "s/^Level=.*/Level=Debug/" \
      -e "s#^Dir=.*#Dir=../PerfData/2026-11-22_Q1/runs/$B/logs#" \
      -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  unset JH_DATA_DIR
  if [ -n "$TABLES" ]; then
    cp -r "$REPO/Common/DataTable" "$RUN/data"
    python -I -X utf8 "$HERE/tweak_tables.py" "$RUN/data" "$TABLES" || { echo "GROUP FAIL $B (테이블 수정 실패)"; TOTAL_FAIL=1; continue; }
    export JH_DATA_DIR="$(cygpath -w "$RUN/data")"
  fi
  restore "$RUN/restore_before.txt" || { echo "GROUP FAIL $B (계정 복원 실패)"; cat "$RUN/restore_before.txt"; TOTAL_FAIL=1; continue; }

  cd "$SRV"
  ../x64/Debug/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  SPID=$!
  sleep 1
  # 실제 GameServer.exe의 Windows PID(명령줄에 이 묶음의 설정 경로가 있는 것만 — 주 줄의 서버는 건드리지 않는다)
  WPID="$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='GameServer.exe'\" | Where-Object { \$_.CommandLine -like '*2026-11-22_Q1*runs*$B*' } | Select-Object -First 1).ProcessId" | tr -d '\r')"
  for i in $(seq 1 60); do
    if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then break; fi
    sleep 1
  done
  sleep 7   # 첫 [metrics] 줄(5초)이 시험 전 상태로 찍히게
  cd "$HERE"
  echo "== $B (server pid $WPID, DevCommands=$DEV DbSaveDelayMs=$DELAY DbItemFailEvery=$FAILEVERY PortalNoRange=$NORANGE, data ${JH_DATA_DIR:-repo})" | tee "$RUN/test_out.txt"
  python -I -X utf8 "$HERE/quest_test.py" $PORT $B 2>&1 | tee -a "$RUN/test_out.txt"
  sleep 12   # 마지막 [metrics] 줄에 정리 뒤 상태가 찍히게
  [ -n "$WPID" ] && taskkill //F //PID "$WPID" > /dev/null 2>&1
  wait $SPID 2>/dev/null
  unset JH_DATA_DIR

  # 잔류: 마지막 [metrics]의 sessions·players·명부·강제 정리, 진행 중인 아이템 작업·퀘스트 진행 저장·경매 DB 작업 0, 이동 지표(abortBusy/orphan/ownerBad 0),
  # 파티·던전 0, 살아 있는 Room 객체 수 = 보관소의 Room 수, 서비스의 경매 목록 수 = DB의 판매 중 등록 수, ERROR 로그 0
  pick="sessions=[0-9]+|rooms=[0-9]+|players=[0-9]+|itemOp\([^)]*\)=\S+ \S+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|roomMove\([^)]*\)=\S+ \S+|party\([^)]*\)=\S+ \S+ \S+|roomsAlive=[0-9]+|dungeon\([^)]*\)=\S+ \S+|auction\([^)]*\)=\S+ \S+ \S+ \S+|quest\([^)]*\)=\S+ \S+ \S+|mailNotice=[0-9]+"
  last="$(grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "$pick" | tr '\n' ' ')"
  echo "$last" | tee "$RUN/metrics_last.txt"
  val() { echo "$1" | grep -oE "$2" | head -1 | grep -oE "[0-9]+$"; }
  bad=0; why=""
  echo "$last" | grep -q "sessions=0 " || { bad=1; why="$why sessions"; }
  echo "$last" | grep -q "players=0 " || { bad=1; why="$why players"; }
  echo "$last" | grep -qE "itemOp\([^)]*\)=\S+ 0 " || { bad=1; why="$why itemOpInflight"; }
  echo "$last" | grep -qE "forced\)=[0-9]+/[0-9]+/0 " || { bad=1; why="$why forced"; }
  echo "$last" | grep -qE "chMax\)=[0-9]+/0/0 " || { bad=1; why="$why roster"; }
  echo "$last" | grep -qE "ownerBad\)=\S+ 0/0/0" || { bad=1; why="$why roomMove"; }
  echo "$last" | grep -qE "disbanded\)=0/0/0 " || { bad=1; why="$why party"; }
  echo "$last" | grep -qE "dungeon\([^)]*\)=0/([0-9]+)/\1 0" || { bad=1; why="$why dungeon"; }
  echo "$last" | grep -qE "auction\([^)]*\)=[0-9]+/0 " || { bad=1; why="$why auctionDbInFlight"; }
  echo "$last" | grep -qE "quest\([^)]*\)=\S+ [0-9]+/0 0 " || { bad=1; why="$why questSave(fail·inflight)"; }
  [ "$(val "$last" "rooms=[0-9]+")" = "$(val "$last" "roomsAlive=[0-9]+")" ] || { bad=1; why="$why roomsAlive"; }
  listed="$(echo "$last" | grep -oE "auction\([^)]*\)=[0-9]+" | grep -oE "[0-9]+$")"
  active="$(sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -h -1 -W -Q "SET NOCOUNT ON; SELECT COUNT(*) FROM Auction_Listing WHERE State = 1" | tr -d '[:space:]')"
  [ "$listed" = "$active" ] || { bad=1; why="$why listings(목록 $listed != DB $active)"; }
  errs="$(cat "$RUN"/logs/*.log | grep -cE " ERROR ")"
  warns="$(cat "$RUN"/logs/*.log | grep -cE " WARN ")"
  [ "$errs" = "0" ] || { bad=1; why="$why ERROR"; grep -hE " ERROR " "$RUN"/logs/*.log | cut -c1-240 | head -5; }
  fails="$(grep -c "^FAIL" "$RUN/test_out.txt")"
  passes="$(grep -c "^PASS" "$RUN/test_out.txt")"
  echo "ERROR 줄 수: $errs / WARN 줄 수: $warns" | tee -a "$RUN/test_out.txt"
  if [ "$bad" = "0" ] && [ "$fails" = "0" ] && [ "$passes" != "0" ]; then
    echo "GROUP PASS $B (PASS $passes)" | tee -a "$RUN/test_out.txt"
  else
    echo "GROUP FAIL $B (잔류·오류:$why, PASS $passes, FAIL $fails, ERROR 로그 $errs)" | tee -a "$RUN/test_out.txt"; TOTAL_FAIL=1
  fi
done

# 시험 계정을 시험 전 상태로(주 줄이 화면 확인에 쓴다)
restore "$HERE/runs/restore_after.txt" || { echo "계정 복원 실패"; cat "$HERE/runs/restore_after.txt"; TOTAL_FAIL=1; }
exit $TOTAL_FAIL
