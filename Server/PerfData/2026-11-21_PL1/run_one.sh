#!/bin/bash
# PL1 사전 램프 측정 1회(A4.3 run_one.sh 복사 + BOTTHREADS — 봇 워커 스레드 수, 기본 4): 서버를 새로 띄우고(설정 사본) 봇을 돌린 뒤 서버를 끈다. 결과는 runs/<이름>/ 에 남긴다.
# PORTALNORANGE=true면 [Debug] PortalNoRange를 켠다. MAXDUNGEON = 인스턴스 상한(기본 250).
# 사용: [DUMMYROOM=town|field|split] [SERVER_EXE=<경로>] [DATA=<DataTable 폴더>] run_one.sh <이름> <워커 수(0=기본)> <봇 수> <초당 접속> <지속 초> [zone "x,y,r"(cm)] [봇 추가 인자]
# DUMMYROOM = 더미 봇이 처음 들어가는 Room([Debug] DummyRoom — 기본 town = 실제 규칙).
# SERVER_EXE = DG3b 전의 exe로 잴 때(A/B). 그 exe가 모르는 설정 키는 지운 사본을 쓴다.
# DATA = JH_DATA_DIR로 줄 DataTable 폴더(채널 정원을 바꿔 재는 시험). 기본은 저장소 Common/DataTable.
# 저장소의 ServerConfig.ini·Common/DataTable은 건드리지 않는다.
set -u
NAME="$1"; WORKERS="$2"; BOTS="$3"; RAMP="$4"; DUR="$5"; ZONE="${6:-}"; EXTRA="${7:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SET="$(basename "$HERE")"
SRV="$HERE/../../Server"
RUN="$HERE/runs/$NAME"
rm -rf "$RUN"; mkdir -p "$RUN"

sed -e "s/^WorkerThreads=.*/WorkerThreads=$WORKERS/" \
    -e "s/^DummySpawnZone=.*/DummySpawnZone=$ZONE/" \
    -e "s/^DummyRoom=.*/DummyRoom=${DUMMYROOM:-town}/" \
    -e "s/^PortalNoRange=.*/PortalNoRange=${PORTALNORANGE:-false}/" \
    -e "s/^MaxDungeonInstances=.*/MaxDungeonInstances=${MAXDUNGEON:-250}/" \
    -e "s#^Dir=.*#Dir=../PerfData/$SET/runs/$NAME/logs#" \
    "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
if [ -n "${SERVER_EXE:-}" ]; then sed -i -e "/^DummyRoom=/d" -e "/^MoveHandoffDelayMs=/d" -e "/^MoveWaitTimeoutMs=/d" "$RUN/ServerConfig.ini"; fi

taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
if [ -n "${DATA:-}" ]; then export JH_DATA_DIR="$(cygpath -w "$DATA")"; else unset JH_DATA_DIR; fi
"${SERVER_EXE:-../x64/Release/GameServer.exe}" --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do
  if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi
  sleep 1
done
sleep 3

# 서버 CPU(사용자·커널 누적 ms)를 1초마다 기록
powershell -NoProfile -Command "\$p = Get-Process GameServer -ErrorAction SilentlyContinue; if (\$p) { 'epoch_ms,user_ms,kernel_ms'; while (-not \$p.HasExited) { '{0},{1},{2}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), [int64]\$p.UserProcessorTime.TotalMilliseconds, [int64]\$p.PrivilegedProcessorTime.TotalMilliseconds; Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/server_cpu.csv" 2>/dev/null &

ARGS="--bots $BOTS --ramp-rate $RAMP --duration $DUR --threads ${BOTTHREADS:-4} --move-interval 200 --body male $EXTRA --out $(cygpath -w "$RUN/loadtest.csv")"
if [ -n "$ZONE" ]; then ARGS="$ARGS --zone $ZONE"; fi
echo "$(date +%s) $ARGS" > "$RUN/args.txt"
echo "exe ${SERVER_EXE:-after} data ${DATA:-repo} dummyRoom ${DUMMYROOM:-town}" >> "$RUN/args.txt"
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
