#!/bin/bash
# A2.3b 원시 소켓 시험: 묶음마다 설정(·데이터 테이블) 사본으로 서버(Debug, 포트 8899)를 띄우고 growth_test.py를 돌린 뒤 서버를 끈다.
# 사용: bash run_tests.sh [main] [err]
#   main = DevCommands=true + 원본 DataTable / err = DevCommands=false + SkillTable 사본(2002 MpCost 100·CooldownMs 1)
# 저장소의 ServerConfig.ini·Common/DataTable은 건드리지 않는다. 다른 GameServer(주 줄)는 건드리지 않게 이 스크립트가 띄운 PID만 끈다.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRV="$HERE/../../Server"
REPO="$HERE/../../.."
PORT=8897
BUNDLES="${*:-main err}"

for B in $BUNDLES; do
  RUN="$HERE/runs/$B"
  rm -rf "$RUN"; mkdir -p "$RUN"
  DEV=false; [ "$B" = "main" ] && DEV=true
  sed -e "s/^Port=.*/Port=$PORT/" \
      -e "s/^DevCommands=.*/DevCommands=$DEV/" \
      -e "s#^Dir=.*#Dir=../PerfData/2026-11-22_B2/runs/$B/logs#" \
      -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
  unset JH_DATA_DIR
  if [ "$B" = "err" ]; then
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
print("SkillTable 사본: 2002 MpCost=100 CooldownMs=1")
EOF
    export JH_DATA_DIR="$(cygpath -w "$RUN/data")"
  fi

  cd "$SRV"
  ../x64/Debug/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  SPID=$!
  sleep 1
  # 실제 GameServer.exe의 Windows PID(명령줄에 이 묶음의 설정 경로가 있는 것만 — 주 줄의 서버는 건드리지 않는다)
  WPID="$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='GameServer.exe'\" | Where-Object { \$_.CommandLine -like '*2026-11-22_B2*runs*$B*' } | Select-Object -First 1).ProcessId" | tr -d '\r')"
  for i in $(seq 1 60); do
    if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then break; fi
    sleep 1
  done
  sleep 2
  echo "== $B (server pid $WPID, DevCommands=$DEV, data ${JH_DATA_DIR:-repo})" | tee "$RUN/test_out.txt"
  python -I -X utf8 "$HERE/growth_test.py" $PORT $B 2>&1 | tee -a "$RUN/test_out.txt"
  sleep 3
  [ -n "$WPID" ] && taskkill //F //PID "$WPID" > /dev/null 2>&1
  wait $SPID 2>/dev/null
  grep -c "\[ERROR\]" "$RUN"/logs/*.log 2>/dev/null | sed "s/^/ERROR 줄 수: /" | tee -a "$RUN/test_out.txt"
  grep -c "\[WARN\]" "$RUN"/logs/*.log 2>/dev/null | sed "s/^/WARN 줄 수: /" | tee -a "$RUN/test_out.txt"
  tail -2 "$RUN"/logs/*.log | grep "metrics" | tail -1 >> "$RUN/test_out.txt"
  cd "$HERE"
done
