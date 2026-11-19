#!/bin/bash
# F4 재현·확인: 서버를 띄우고 봇 50명을 한 화면(마을 광장 반경 10m)에 세워 움직이게 한 뒤, 수신을 멈춘 클라이언트(stall_recv.py)를 붙인다.
# 서버 프로세스의 핸들 수를 1초마다 server_handles.csv에 남기고, [metrics]의 sessions·sendQ(·수정 뒤에는 sessionObjs·handles)와 함께 본다.
# 사용: f4.sh <이름> [stall_recv.py 인자...]      결과: runs/<이름>/
# 순서: 서버 시작 → 5초 → 봇 50명(110초) → 15초 → stall_recv(기본 20초 뒤 깨진 패킷, 그 뒤 60초 붙잡음) → 봇 종료 → 15초 → 서버 종료
set -u
NAME="$1"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"; SET="$(basename "$HERE")"; SRV="$HERE/../../Server"; RUN="$HERE/runs/$NAME"
ZONE="-14200,0,1000"
rm -rf "$RUN"; mkdir -p "$RUN"
sed -e "s/^DummySpawnZone=.*/DummySpawnZone=$ZONE/" \
    -e "s#^Dir=.*#Dir=../PerfData/$SET/runs/$NAME/logs#" \
    "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 5

powershell -NoProfile -Command "\$p = Get-Process GameServer -ErrorAction SilentlyContinue; if (\$p) { 'epoch_ms,handles,working_set_kb'; while (-not \$p.HasExited) { '{0},{1},{2}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), \$p.HandleCount, [int64](\$p.WorkingSet64 / 1024); Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/server_handles.csv" 2>/dev/null &

echo "$(date +%s) start bots" > "$RUN/args.txt"
../x64/Release/LoadTest.exe --bots 50 --ramp-rate 50 --duration 110 --threads 2 --move-interval 200 --body male --zone $ZONE > "$RUN/loadtest_console.txt" 2>&1 &
BOTPID=$!
sleep 15
echo "$(date +%s) start stall $*" >> "$RUN/args.txt"
python -I -X utf8 "$HERE/stall_recv.py" "$@" > "$RUN/stall.txt" 2>&1
echo "$(date +%s) stall end" >> "$RUN/args.txt"
wait $BOTPID 2>/dev/null
sleep 15
tasklist | grep -i GameServer.exe > /dev/null && echo "server alive" >> "$RUN/args.txt" || echo "SERVER DEAD" >> "$RUN/args.txt"
taskkill //IM GameServer.exe //F > /dev/null 2>&1
wait
grep -h "\[metrics\]" "$RUN"/logs/*.log | sed -E 's/^(\S+ \S+).*(sessions=[0-9]+).*(players=[0-9]+).*(sendQ\([^ ]* [^ ]*|sendQ[^ ]*).*/\1 \2 \3 \4/' > "$RUN/metrics_brief.txt"
echo "done $NAME"
