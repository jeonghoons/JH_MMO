#!/bin/bash
# A7.4 시연 촬영용 서버 실행기(D-311). 측정 기록이 아니라 영상 속 수치의 근거로 지표를 남긴다.
# A5.4d run_stages.sh와 같은 서버 설정(BindIp=0.0.0.0, MoveBatchMs 33, 봇 계정 분산 배치, 고성능 전원 계획)으로 서버를 띄우고,
# PIE 2명(Test1·Test2)이 먼저 들어와 채널 1을 잡으면(players >= PIE_COUNT) PC 몫 DummyClient 봇을 띄운다. 노트북 봇(run_bots.ps1 -Stages demo)은 포트가 열리면 알아서 들어온다.
# 끝내기: 이 폴더에 stop 파일을 만들면(touch stop) DummyClient·서버를 내리고 지표를 정리한다. 중간에 끊겨도 trap이 서버를 내리고 전원 계획을 되돌린다.
# 사용: [PCBOTS=2000] [PCRATE=100] [PIE_COUNT=2] [BG=0] [TOWNPCT=30] [DEV=true] bash run_demo.sh <이름>
#   촬영 1·2(플레이): BG=30 PCBOTS=0 TOWNPCT=34 bash run_demo.sh play — 배경 봇 30명(더미 로그인, 창 없는 LoadTest)이 서버 시작과 함께 들어와
#     마을 약 10명 + 사냥터 스폰 지점마다 고루 약 20명(DummyRoom=spread, 사용자 결정 2026-11-24). 채팅·거래·경매·줍기 봇은 넣지 않음(화면·플레이 방해 없게)
#   촬영 3(부하): bash run_demo.sh load — PIE 2명 뒤 DummyClient 2,000명(계정 bot_0~1999) + 노트북 3,000명
#   DATA=<폴더> = 촬영 전용 데이터 사본(JH_DATA_DIR — 던전 몬스터·우두머리 HP를 낮춘 것, 촬영 1·2만)
#   DEV=true = [Debug] DevCommands(개발 지급 — 촬영 준비 때만), PORTALNORANGE=true = 포털 거리 검사 끔(자동 플레이의 순간이동 — 촬영 1·2만)
set -u
NAME="${1:-demo}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SET="$(basename "$HERE")"
SRV="$HERE/../../Server"
A54D="$HERE/../2026-11-24_A5.4d"
RUN="$HERE/runs/$NAME"
PCBOTS="${PCBOTS:-2000}"; PCRATE="${PCRATE:-100}"; PIE_COUNT="${PIE_COUNT:-2}"; BG="${BG:-0}"; TOWNPCT="${TOWNPCT:-30}"
BGMIX="move=40,hunt=50,idle=10"
MIX="idle=25,move=15,chat=10,hunt=20,loot=20,trade=5,auction=5"
BALANCED=381b4222-f694-41f0-9685-ff5bb260df2e
HIGH=8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
rm -rf "$RUN"; mkdir -p "$RUN"; rm -f "$HERE/stop"

cleanup() {
  taskkill //IM DummyClient.exe //F > /dev/null 2>&1
  taskkill //IM LoadTest.exe //F > /dev/null 2>&1
  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  powercfg //setactive $BALANCED
}
trap cleanup EXIT
powercfg //setactive $HIGH

SQL() { sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -f 65001 -h -1 -W -i "$(cygpath -w "$1")"; }
players_now() { grep -o "players=[0-9]*" "$RUN/server_console.txt" 2>/dev/null | tail -1 | cut -d= -f2; }
log() { echo "[demo $(date +%H:%M:%S)] $*"; }

sed -e "s/^BindIp=.*/BindIp=0.0.0.0/" \
    -e "s/^WorkerThreads=.*/WorkerThreads=0/" \
    -e "s/^DummySpawnZone=.*/DummySpawnZone=/" \
    -e "s/^DummyRoom=.*/DummyRoom=spread/" \
    -e "s/^DummyTownPercent=.*/DummyTownPercent=$TOWNPCT/" \
    -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" \
    -e "s/^BotAccountPrefix=.*/BotAccountPrefix=bot_/" \
    -e "s/^DevCommands=.*/DevCommands=${DEV:-false}/" \
    -e "s/^PortalNoRange=.*/PortalNoRange=${PORTALNORANGE:-false}/" \
    -e "s/^Connections=.*/Connections=4/" \
    -e "s/^MoveBatchMs=.*/MoveBatchMs=33/" \
    -e "s/^Level=.*/Level=Info/" \
    -e "s#^Dir=.*#Dir=../PerfData/$SET/runs/$NAME/logs#" \
    "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"

taskkill //IM GameServer.exe //F > /dev/null 2>&1
if [ "$PCBOTS" -gt 0 ]; then
  [ -f "$A54D/bot_accounts.sql" ] || python -I "$A54D/make_bot_accounts.py" > /dev/null   # 생성 파일(커밋하지 않음)
  SQL "$A54D/bot_accounts.sql" > "$RUN/db_reset.txt" 2>&1 || { log "bot_accounts.sql 실패"; cat "$RUN/db_reset.txt"; exit 1; }
fi

cd "$SRV"
if [ -n "${DATA:-}" ]; then export JH_DATA_DIR="$(cygpath -w "$DATA")"; else unset JH_DATA_DIR; fi   # DATA = 촬영 전용 데이터 사본(Tools/Demo/demo_data.py)
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null && break; sleep 1; done
powershell -NoProfile -Command "\$p = Get-Process GameServer -ErrorAction SilentlyContinue; if (\$p) { 'epoch_ms,user_ms,kernel_ms,ws_mb'; while (-not \$p.HasExited) { '{0},{1},{2},{3}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), [int64]\$p.UserProcessorTime.TotalMilliseconds, [int64]\$p.PrivilegedProcessorTime.TotalMilliseconds, [int64](\$p.WorkingSet64/1MB); Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/server_cpu.csv" 2>/dev/null &
echo "$(date +%s) demo $NAME pcBots $PCBOTS pcRate $PCRATE pieCount $PIE_COUNT bg $BG townPct $TOWNPCT dev ${DEV:-false} power $(powercfg //getactivescheme | tr -d '\r')" > "$RUN/args.txt"
if [ "$BG" -gt 0 ]; then
  BGARGS="--bots $BG --ramp-rate 10 --duration 14400 --threads 1 --move-interval 200 --body male --login dummy --town-wander --mix $BGMIX --out $(cygpath -w "$RUN/bg_loadtest.csv")"
  echo "$(date +%s) background $BGARGS" >> "$RUN/args.txt"
  ../x64/Release/LoadTest.exe $BGARGS > "$RUN/bg_console.txt" 2>&1 &
  log "배경 봇 ${BG}명 시작(마을 ${TOWNPCT}% · 나머지 사냥터 고루)"
fi
log "서버 시작 — PIE로 로그인하세요"

DCPID=""
if [ "$PCBOTS" -gt 0 ]; then
  while [ ! -f "$HERE/stop" ]; do
    P="$(players_now)"; [ "${P:-0}" -ge "$PIE_COUNT" ] && break
    sleep 2
  done
  if [ ! -f "$HERE/stop" ]; then
    log "PIE 들어옴 — DummyClient ${PCBOTS}명(초당 ${PCRATE}) 시작"
    DCARGS="--bots $PCBOTS --ramp-rate $PCRATE --threads 4 --move-interval 200 --body male --login account --account-start 0 --town-wander --mix $MIX --server-proc GameServer.exe"
    echo "$(date +%s) dummyclient $DCARGS" >> "$RUN/args.txt"
    ../x64/Release/DummyClient.exe $DCARGS > "$RUN/dummyclient_console.txt" 2>&1 &
    DCPID=$!
  fi
fi

while [ ! -f "$HERE/stop" ]; do sleep 2; done
log "stop — 정리"
rm -f "$HERE/stop"
taskkill //IM DummyClient.exe //F > /dev/null 2>&1
taskkill //IM LoadTest.exe //F > /dev/null 2>&1
sleep 3
taskkill //IM GameServer.exe //F > /dev/null 2>&1
sleep 1

LOG="$(ls "$RUN/logs"/GameServer_*.log 2>/dev/null | head -1)"
RID="$(basename "$LOG" .log)"; RID="${RID#GameServer_}"
if [ -n "$RID" ]; then
  head -1 "$SRV/metrics.csv" > "$RUN/server_metrics.csv"
  grep "^$RID," "$SRV/metrics.csv" >> "$RUN/server_metrics.csv"
  echo "run_id $RID" >> "$RUN/args.txt"
fi
log "끝 $NAME"
