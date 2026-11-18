#!/bin/bash
# 측정 1회: 서버를 새로 띄우고(임시 설정·임시 DataTable) 봇을 돌린 뒤 서버를 끈다. 결과는 runs/<이름>/ 에 남긴다.
# 사용: run_one.sh <이름> <맵> <워커 수(0=기본)> <봇 수> <지속 초> [zone "x,y,r"(cm) — 서버 DummySpawnZone과 봇 --zone에 같이 준다]
set -u
NAME="$1"; MAP="$2"; WORKERS="$3"; BOTS="$4"; DUR="$5"; ZONE="${6:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRV="$HERE/../../Server"
RUN="$HERE/runs/$NAME"
rm -rf "$RUN"; mkdir -p "$RUN"

# 설정: 저장소의 ServerConfig.ini에서 레벨·워커·더미 구역·로그 폴더만 바꾼다
sed -e "s/^MapName=.*/MapName=$MAP/" -e "s/^WorkerThreads=.*/WorkerThreads=$WORKERS/" \
    -e "s/^DummySpawnZone=.*/DummySpawnZone=$ZONE/" \
    -e "s#^Dir=.*#Dir=../PerfData/2026-11-17_room_capacity/runs/$NAME/logs#" \
    "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"

taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
JH_DATA_DIR="$(cygpath -w "$HERE/datatable")" ../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do
  if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi
  sleep 1
done
sleep 3

# 서버 CPU(사용자·커널 누적 ms)를 1초마다 기록
powershell -NoProfile -Command "\$p = Get-Process GameServer -ErrorAction SilentlyContinue; if (\$p) { 'epoch_ms,user_ms,kernel_ms'; while (-not \$p.HasExited) { '{0},{1},{2}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), [int64]\$p.UserProcessorTime.TotalMilliseconds, [int64]\$p.PrivilegedProcessorTime.TotalMilliseconds; Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/server_cpu.csv" 2>/dev/null &

ARGS="--bots $BOTS --ramp-rate 50 --duration $DUR --threads 4 --move-interval 200 --body male --out $(cygpath -w "$RUN/loadtest.csv")"
if [ -n "$ZONE" ]; then ARGS="$ARGS --zone $ZONE"; fi
echo "$(date +%s) $ARGS" > "$RUN/args.txt"
../x64/Release/LoadTest.exe $ARGS > "$RUN/loadtest_console.txt" 2>&1
echo "loadtest exit $?" >> "$RUN/args.txt"

sleep 2
taskkill //IM GameServer.exe //F > /dev/null 2>&1
wait
echo "done $NAME"
