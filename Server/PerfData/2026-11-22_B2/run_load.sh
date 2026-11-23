#!/bin/bash
# A2.3b 짧은 부하(PL1 run_one.sh 복사 — 포트 8899, 이 스크립트가 띄운 서버 PID만 끔, 서버 CPU도 그 PID만 잰다).
# 사용: [SERVER_EXE=<Release exe>] [DATA=<DataTable 폴더>] run_load.sh <이름> <봇 수> <초당 접속> <지속 초> [봇 추가 인자]
#   A/B: 수정 전(c5dfa90 원본 exe + 원본 DataTable) vs 수정 후(이 브랜치 exe + 새 DataTable). 더미는 사냥터 Room에 바로(DummyRoom=field — PL1과 같음).
set -u
NAME="$1"; BOTS="$2"; RAMP="$3"; DUR="$4"; EXTRA="${5:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRV="$HERE/../../Server"
RUN="$HERE/runs/$NAME"
PORT=8897
rm -rf "$RUN"; mkdir -p "$RUN"

sed -e "s/^Port=.*/Port=$PORT/" \
    -e "s/^DummyRoom=.*/DummyRoom=field/" \
    -e "s#^Dir=.*#Dir=../PerfData/2026-11-22_B2/runs/$NAME/logs#" \
    -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"

cd "$SRV"
if [ -n "${DATA:-}" ]; then export JH_DATA_DIR="$(cygpath -w "$DATA")"; else unset JH_DATA_DIR; fi
"${SERVER_EXE:-../x64/Release/GameServer.exe}" --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
SPID=$!
sleep 1
# 실제 GameServer.exe의 Windows PID(명령줄에 이 실행의 설정 경로가 있는 것만 — 주 줄의 서버는 건드리지 않는다)
WPID="$(powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter \"Name='GameServer.exe'\" | Where-Object { \$_.CommandLine -like '*2026-11-22_B2*runs*$NAME*' } | Select-Object -First 1).ProcessId" | tr -d '\r')"
for i in $(seq 1 60); do
  if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then break; fi
  sleep 1
done
sleep 3

powershell -NoProfile -Command "\$p = Get-Process -Id $WPID -ErrorAction SilentlyContinue; if (\$p) { 'epoch_ms,user_ms,kernel_ms'; while (-not \$p.HasExited) { '{0},{1},{2}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), [int64]\$p.UserProcessorTime.TotalMilliseconds, [int64]\$p.PrivilegedProcessorTime.TotalMilliseconds; Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/server_cpu.csv" 2>/dev/null &

ARGS="--bots $BOTS --ramp-rate $RAMP --duration $DUR --port $PORT --threads 4 --move-interval 200 --body male $EXTRA --out $(cygpath -w "$RUN/loadtest.csv")"
echo "$(date +%s) $ARGS" > "$RUN/args.txt"
echo "exe ${SERVER_EXE:-after} data ${DATA:-repo} pid $WPID" >> "$RUN/args.txt"
../x64/Release/LoadTest.exe $ARGS > "$RUN/loadtest_console.txt" 2>&1
echo "loadtest exit $?" >> "$RUN/args.txt"

sleep 2
[ -n "$WPID" ] && taskkill //F //PID "$WPID" > /dev/null 2>&1
wait

LOG="$(ls "$RUN/logs"/GameServer_*.log 2>/dev/null | head -1)"
RID="$(basename "$LOG" .log)"; RID="${RID#GameServer_}"
if [ -n "$RID" ]; then
  head -1 "$SRV/metrics.csv" > "$RUN/server_metrics.csv"
  grep "^$RID," "$SRV/metrics.csv" >> "$RUN/server_metrics.csv"
  echo "run_id $RID" >> "$RUN/args.txt"
fi
echo "done $NAME"
