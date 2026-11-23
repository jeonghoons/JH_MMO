#!/bin/bash
# A5.3 측정 1회(A5.2 run_one.sh 사본 — 동작 같음): 서버를 새로 띄우고(설정 사본) 봇을 돌린 뒤 서버를 끈다. 결과는 runs/<이름>/ 에 남긴다(loadtest.csv + A5.3 loadtest.summary.csv).
# 사용: [LOGIN=dummy|account] [DUMMYROOM=spread] [TOWNPCT=30] [BOTTHREADS=4] [DBCONN=4] run_one.sh <이름> <워커 수(0=기본)> <봇 수> <초당 접속> <지속 초> [zone "x,y,r"] [봇 추가 인자]
# LOGIN=account(기본 dummy): 서버를 띄우기 전에 bot_accounts.sql로 봇 계정(bot_0~bot_4999, D-296)을 처음 상태로 되돌리고, 봇은 --login account로 일반 로그인한다.
#   설정 사본에 [Debug] BotAccountPrefix=bot_ 를 넣어 봇 계정도 DummyRoom(spread)으로 배치한다(D-297). 끝나면 db_check.sql 결과를 db_before.txt·db_after.txt로 남긴다.
# LOGIN=dummy: A5.1과 같다(AllowDummyLogin=true, is_dummy 로그인 — DB 없는 빠른 회귀).
# 프로필은 봇 추가 인자로 준다: "--profile hunt" 또는 "--mix idle=25,move=15,chat=10,hunt=20,loot=20,trade=5,auction=5"(D-298).
# DBCONN = [Database] Connections(기본 4). 저장소의 ServerConfig.ini·Common/DataTable·DB의 Test1~4는 건드리지 않는다.
set -u
NAME="$1"; WORKERS="$2"; BOTS="$3"; RAMP="$4"; DUR="$5"; ZONE="${6:-}"; EXTRA="${7:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SET="$(basename "$HERE")"
SRV="$HERE/../../Server"
RUN="$HERE/runs/$NAME"
LOGIN="${LOGIN:-dummy}"
rm -rf "$RUN"; mkdir -p "$RUN"

PREFIX=""
[ "$LOGIN" = "account" ] && PREFIX="bot_"
sed -e "s/^WorkerThreads=.*/WorkerThreads=$WORKERS/" \
    -e "s/^DummySpawnZone=.*/DummySpawnZone=$ZONE/" \
    -e "s/^DummyRoom=.*/DummyRoom=${DUMMYROOM:-spread}/" \
    -e "s/^DummyTownPercent=.*/DummyTownPercent=${TOWNPCT:-30}/" \
    -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" \
    -e "s/^BotAccountPrefix=.*/BotAccountPrefix=$PREFIX/" \
    -e "s/^PortalNoRange=.*/PortalNoRange=${PORTALNORANGE:-false}/" \
    -e "s/^Connections=.*/Connections=${DBCONN:-4}/" \
    -e "s/^Level=.*/Level=${LOGLEVEL:-Info}/" \
    -e "s/^MaxDungeonInstances=.*/MaxDungeonInstances=${MAXDUNGEON:-250}/" \
    -e "s#^Dir=.*#Dir=../PerfData/$SET/runs/$NAME/logs#" \
    "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"

SQL() { sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -f 65001 -h -1 -W -i "$(cygpath -w "$1")"; }

taskkill //IM GameServer.exe //F > /dev/null 2>&1
if [ "$LOGIN" = "account" ]; then
  [ -f "$HERE/bot_accounts.sql" ] || python -I "$HERE/make_bot_accounts.py" > /dev/null   # 생성 파일(커밋하지 않음) — ItemTable에서 다시 만든다
  SQL "$HERE/bot_accounts.sql" > "$RUN/db_reset.txt" 2>&1 || { echo "bot_accounts.sql failed"; cat "$RUN/db_reset.txt"; exit 1; }
  SQL "$HERE/db_check.sql" > "$RUN/db_before.txt" 2>&1
fi
cd "$SRV"
unset JH_DATA_DIR
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do
  if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi
  sleep 1
done
sleep 3

# 서버 CPU(사용자·커널 누적 ms)를 1초마다 기록
powershell -NoProfile -Command "\$p = Get-Process GameServer -ErrorAction SilentlyContinue; if (\$p) { 'epoch_ms,user_ms,kernel_ms'; while (-not \$p.HasExited) { '{0},{1},{2}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), [int64]\$p.UserProcessorTime.TotalMilliseconds, [int64]\$p.PrivilegedProcessorTime.TotalMilliseconds; Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/server_cpu.csv" 2>/dev/null &

ARGS="--bots $BOTS --ramp-rate $RAMP --duration $DUR --threads ${BOTTHREADS:-4} --move-interval 200 --body male --login $LOGIN --town-wander $EXTRA --out $(cygpath -w "$RUN/loadtest.csv")"
if [ -n "$ZONE" ]; then ARGS="$ARGS --zone $ZONE"; fi
echo "$(date +%s) $ARGS" > "$RUN/args.txt"
echo "login $LOGIN dummyRoom ${DUMMYROOM:-spread} townPct ${TOWNPCT:-30} dbConn ${DBCONN:-4}" >> "$RUN/args.txt"
../x64/Release/LoadTest.exe $ARGS > "$RUN/loadtest_console.txt" 2>&1 &
LT=$!
powershell -NoProfile -Command "Start-Sleep -Milliseconds 500; \$p = Get-Process LoadTest -ErrorAction SilentlyContinue | Select-Object -First 1; if (\$p) { 'epoch_ms,user_ms,kernel_ms'; while (-not \$p.HasExited) { '{0},{1},{2}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), [int64]\$p.UserProcessorTime.TotalMilliseconds, [int64]\$p.PrivilegedProcessorTime.TotalMilliseconds; Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/bot_cpu.csv" 2>/dev/null &
if [ -n "${KILLAFTER:-}" ]; then
  # 오류 시험: 이 초가 지나면 봇 프로세스를 강제 종료한다(거래·경매 도중 접속 끊김). 서버는 몇 초 더 두어 정리를 본다
  sleep "$KILLAFTER"; taskkill //IM LoadTest.exe //F > /dev/null 2>&1; echo "killed LoadTest after ${KILLAFTER}s" >> "$RUN/args.txt"; sleep 8
fi
wait $LT
echo "loadtest exit $?" >> "$RUN/args.txt"

sleep 3
taskkill //IM GameServer.exe //F > /dev/null 2>&1
wait

if [ "$LOGIN" = "account" ]; then
  sleep 1
  SQL "$HERE/db_check.sql" > "$RUN/db_after.txt" 2>&1
fi

# 이 실행의 metrics.csv 행만 server_metrics.csv로 뽑아 둔다(run_id = 로그 파일 이름의 시작 시각)
LOG="$(ls "$RUN/logs"/GameServer_*.log 2>/dev/null | head -1)"
RID="$(basename "$LOG" .log)"; RID="${RID#GameServer_}"
if [ -n "$RID" ]; then
  head -1 "$SRV/metrics.csv" > "$RUN/server_metrics.csv"
  grep "^$RID," "$SRV/metrics.csv" >> "$RUN/server_metrics.csv"
  echo "run_id $RID" >> "$RUN/args.txt"
fi
echo "done $NAME"
