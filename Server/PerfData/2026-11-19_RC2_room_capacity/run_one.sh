#!/bin/bash
# RC2 측정 1회: 서버를 새로 띄우고(설정 사본 + 정원 1000짜리 임시 DataTable) 봇을 돌린 뒤 서버를 끈다. 결과는 runs/<이름>/ 에 남긴다.
# 사용: run_one.sh <이름> <워커 수(0=기본)> <봇 수> <지속 초> [zone "x,y,r"(cm) — 서버 DummySpawnZone과 봇 --zone에 같이 준다] [봇 추가 인자]
# 송신 간격(SendFlushIntervalMs)·타이머 해상도(TimerResolutionMs)는 저장소 ServerConfig.ini 값 그대로 쓴다(10 / 1, D-207).
# 저장소의 ServerConfig.ini·Common/DataTable은 건드리지 않는다(datatable/ = Common/DataTable 사본에서 Continent01 RoomCapacity만 1000).
set -u
NAME="$1"; WORKERS="$2"; BOTS="$3"; DUR="$4"; ZONE="${5:-}"; EXTRA="${6:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SET="$(basename "$HERE")"
SRV="$HERE/../../Server"
RUN="$HERE/runs/$NAME"
rm -rf "$RUN"; mkdir -p "$RUN"

sed -e "s/^WorkerThreads=.*/WorkerThreads=$WORKERS/" \
    -e "s/^DummySpawnZone=.*/DummySpawnZone=$ZONE/" \
    -e "s#^Dir=.*#Dir=../PerfData/$SET/runs/$NAME/logs#" \
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

ARGS="--bots $BOTS --ramp-rate 50 --duration $DUR --threads 4 --move-interval 200 --body male $EXTRA --out $(cygpath -w "$RUN/loadtest.csv")"
if [ -n "$ZONE" ]; then ARGS="$ARGS --zone $ZONE"; fi
echo "$(date +%s) $ARGS" > "$RUN/args.txt"
../x64/Release/LoadTest.exe $ARGS > "$RUN/loadtest_console.txt" 2>&1
echo "loadtest exit $?" >> "$RUN/args.txt"

sleep 2
taskkill //IM GameServer.exe //F > /dev/null 2>&1
wait

# 이 실행의 metrics.csv 행만 server_metrics.csv로 뽑아 둔다(run_id = 로그 파일 이름의 시작 시각)
LOG="$(ls "$RUN/logs"/GameServer_*.log 2>/dev/null | head -1)"
RID="$(basename "$LOG" .log)"; RID="${RID#GameServer_}"
if [ -n "$RID" ]; then
  head -1 "$SRV/metrics.csv" > "$RUN/server_metrics.csv"
  grep "^$RID," "$SRV/metrics.csv" >> "$RUN/server_metrics.csv"
  echo "run_id $RID" >> "$RUN/args.txt"
fi
echo "done $NAME"
