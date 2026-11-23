#!/bin/bash
# Q1 회귀: 퀘스트를 넣은 서버로 앞 항목의 원시 소켓 시나리오를 다시 돌린다 — B2(A2.3b) growth_test main·err, A4.3 auction_test plain·race.
# 사용: bash run_regress.sh [growth_main] [growth_err] [auction_plain] [auction_race]   (생략하면 전부). 결과: runs/regress_<이름>/
# 앞 항목 폴더의 runs/(커밋된 결과)는 건드리지 않는다 — 시나리오 스크립트만 빌려 쓰고 서버·결과는 이 폴더에 둔다(A4.3 run_regress.sh와 같은 방식).
#   growth_* : 포트 8897(growth_test.py가 포트를 인자로 받는다). 시험 전에 계정을 되돌린 뒤 growth_pre.sql로 A2.3b 시험의 전제(Test1~4 = 1레벨, Test1 HP 300)를 맞춘다.
#   auction_*: 포트 8888(auction_test.py는 기본 포트로 붙는다 — 주 줄 서버가 꺼져 있어야 한다). 시험 전에 계정을 되돌리고 A4.3 reset_db.sql을 돌린다.
# 끝나면 시험 계정을 시험 전 상태로 되돌린다(restore_accounts.sql).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRV="$HERE/../../Server"
REPO="$HERE/../../.."
B2="$HERE/../2026-11-22_B2"
A43="$HERE/../2026-11-21_A4.3"
WHAT="${*:-growth_main growth_err auction_plain auction_race}"
TOTAL_FAIL=0

sql() { sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -f 65001 -h -1 -W -i "$(cygpath -w "$1")"; }

for W in $WHAT; do
  RUN="$HERE/runs/regress_$W"
  rm -rf "$RUN"; mkdir -p "$RUN"
  PORT=8897; DEV=true; NORANGE=false; WANT=0
  case "$W" in
    growth_main)   WANT=39 ;;
    growth_err)    DEV=false; WANT=9 ;;
    auction_plain) PORT=8888; NORANGE=true; WANT=4 ;;
    auction_race)  PORT=8888; NORANGE=true; WANT=2 ;;
    *) echo "모르는 이름: $W"; TOTAL_FAIL=1; continue ;;
  esac
  sed -e "s/^Port=.*/Port=$PORT/" -e "s/^DevCommands=.*/DevCommands=$DEV/" -e "s/^PortalNoRange=.*/PortalNoRange=$NORANGE/" -e "s/^Level=.*/Level=Debug/" \
      -e "s#^Dir=.*#Dir=../PerfData/2026-11-22_Q1/runs/regress_$W/logs#" -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  unset JH_DATA_DIR
  sql "$HERE/restore_accounts.sql" > "$RUN/restore_before.txt" 2>&1 || { echo "REGRESS FAIL $W (계정 복원 실패)"; TOTAL_FAIL=1; continue; }
  case "$W" in
    growth_*)  sql "$HERE/growth_pre.sql" >> "$RUN/restore_before.txt" 2>&1 || { echo "REGRESS FAIL $W (전제 맞추기 실패)"; TOTAL_FAIL=1; continue; } ;;
    auction_*) sql "$A43/reset_db.sql" >> "$RUN/restore_before.txt" 2>&1 || { echo "REGRESS FAIL $W (경매 정리 실패)"; TOTAL_FAIL=1; continue; } ;;
  esac
  if [ "$W" = "growth_err" ]; then
    # B2 run_tests.sh의 err 묶음과 같은 SkillTable 사본(2002 MpCost 100·CooldownMs 1)
    cp -r "$REPO/Common/DataTable" "$RUN/data"
    python -I -X utf8 - "$RUN/data/SkillTable.csv" <<'EOF'
import csv, io, sys
p = sys.argv[1]
rows = list(csv.reader(open(p, encoding="utf-8", newline="")))
h = rows[0]
for r in rows[1:]:
    if r and r[0] == "2002":
        r[h.index("MpCost")] = "100"; r[h.index("CooldownMs")] = "1"
out = io.StringIO(); csv.writer(out, lineterminator="\n").writerows(rows)
open(p, "w", encoding="utf-8", newline="").write(out.getvalue())
EOF
    export JH_DATA_DIR="$(cygpath -w "$RUN/data")"
  fi

  cd "$SRV"
  ../x64/Debug/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  SPID=$!
  sleep 1
  WPID="$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='GameServer.exe'\" | Where-Object { \$_.CommandLine -like '*2026-11-22_Q1*runs*regress_$W*' } | Select-Object -First 1).ProcessId" | tr -d '\r')"
  for i in $(seq 1 60); do
    if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then break; fi
    sleep 1
  done
  sleep 7
  echo "== regress $W (server pid $WPID, port $PORT, DevCommands=$DEV PortalNoRange=$NORANGE, data ${JH_DATA_DIR:-repo})" | tee "$RUN/test_out.txt"
  case "$W" in
    growth_main)   (cd "$B2" && python -I -X utf8 growth_test.py $PORT main) 2>&1 | tee -a "$RUN/test_out.txt" ;;
    growth_err)    (cd "$B2" && python -I -X utf8 growth_test.py $PORT err) 2>&1 | tee -a "$RUN/test_out.txt" ;;
    auction_plain) (cd "$A43" && python -I -X utf8 auction_test.py basic partial rejects dungeon) 2>&1 | tee -a "$RUN/test_out.txt" ;;
    auction_race)  (cd "$A43" && python -I -X utf8 auction_test.py race cancelrace) 2>&1 | tee -a "$RUN/test_out.txt" ;;
  esac
  sleep 12
  [ -n "$WPID" ] && taskkill //F //PID "$WPID" > /dev/null 2>&1
  wait $SPID 2>/dev/null
  unset JH_DATA_DIR
  cd "$HERE"

  pick="sessions=[0-9]+|rooms=[0-9]+|players=[0-9]+|itemOp\([^)]*\)=\S+ \S+|login\(total kick/busy/forced\)=\S+|world\(channels/roster/chMax\)=\S+|roomMove\([^)]*\)=\S+ \S+|party\([^)]*\)=\S+ \S+ \S+|roomsAlive=[0-9]+|dungeon\([^)]*\)=\S+ \S+|auction\([^)]*\)=\S+ \S+ \S+ \S+|quest\([^)]*\)=\S+ \S+ \S+|mailNotice=[0-9]+"
  last="$(grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "$pick" | tr '\n' ' ')"
  echo "$last" | tee "$RUN/metrics_last.txt"
  bad=0; why=""
  echo "$last" | grep -q "sessions=0 " || { bad=1; why="$why sessions"; }
  echo "$last" | grep -q "players=0 " || { bad=1; why="$why players"; }
  echo "$last" | grep -qE "itemOp\([^)]*\)=\S+ 0 " || { bad=1; why="$why itemOpInflight"; }
  echo "$last" | grep -qE "forced\)=[0-9]+/[0-9]+/0 " || { bad=1; why="$why forced"; }
  echo "$last" | grep -qE "chMax\)=[0-9]+/0/0 " || { bad=1; why="$why roster"; }
  echo "$last" | grep -qE "ownerBad\)=\S+ 0/0/0" || { bad=1; why="$why roomMove"; }
  echo "$last" | grep -qE "dungeon\([^)]*\)=0/([0-9]+)/\1 0" || { bad=1; why="$why dungeon"; }
  echo "$last" | grep -qE "auction\([^)]*\)=[0-9]+/0 " || { bad=1; why="$why auctionDbInFlight"; }
  echo "$last" | grep -qE "quest\([^)]*\)=\S+ [0-9]+/0 0 " || { bad=1; why="$why questSave"; }
  errs="$(cat "$RUN"/logs/*.log | grep -cE " ERROR ")"
  [ "$errs" = "0" ] || { bad=1; why="$why ERROR"; grep -hE " ERROR " "$RUN"/logs/*.log | cut -c1-240 | head -5; }
  fails="$(grep -c "^FAIL" "$RUN/test_out.txt")"
  passes="$(grep -c "^PASS" "$RUN/test_out.txt")"
  echo "ERROR 줄 수: $errs" | tee -a "$RUN/test_out.txt"
  if [ "$bad" = "0" ] && [ "$fails" = "0" ] && [ "$passes" = "$WANT" ]; then
    echo "REGRESS PASS $W (PASS $passes/$WANT)" | tee -a "$RUN/test_out.txt"
  else
    echo "REGRESS FAIL $W (잔류·오류:$why, PASS $passes/$WANT, FAIL $fails, ERROR 로그 $errs)" | tee -a "$RUN/test_out.txt"; TOTAL_FAIL=1
  fi
done

sql "$HERE/restore_accounts.sql" > "$HERE/runs/restore_after_regress.txt" 2>&1 || { echo "계정 복원 실패"; TOTAL_FAIL=1; }
cat "$HERE/runs/restore_after_regress.txt"
exit $TOTAL_FAIL
