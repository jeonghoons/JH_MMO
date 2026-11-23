#!/bin/bash
# WT1 시험 실행: DevCommands=true + 원본 DataTable로 Debug 서버(포트 8897)를 띄우고 wt1_test.py를 돌린 뒤 서버를 끈다.
HERE="$(cd "$(dirname "$0")" && pwd)"
SRV="$HERE/../../Server"
PORT=8897
RUN="$HERE/runs/wt1"
rm -rf "$RUN"; mkdir -p "$RUN"
sed -e "s/^Port=.*/Port=$PORT/" \
    -e "s/^DevCommands=.*/DevCommands=true/" \
    -e "s#^Dir=.*#Dir=../PerfData/2026-11-22_B2/runs/wt1/logs#" \
    -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
unset JH_DATA_DIR
cd "$SRV"
../x64/Debug/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
SPID=$!
sleep 1
WPID="$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='GameServer.exe'\" | Where-Object { \$_.CommandLine -like '*2026-11-22_B2*runs*wt1*' } | Select-Object -First 1).ProcessId" | tr -d '\r')"
for i in $(seq 1 60); do
  if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then break; fi
  sleep 1
done
sleep 2
python -I -X utf8 "$HERE/wt1_test.py" $PORT 2>&1 | tee "$RUN/test_out.txt"
sleep 2
[ -n "$WPID" ] && taskkill //F //PID "$WPID" > /dev/null 2>&1
wait $SPID 2>/dev/null
grep -c "\[ERROR\]" "$RUN"/logs/*.log 2>/dev/null | sed "s/^/ERROR 줄 수: /" | tee -a "$RUN/test_out.txt"
