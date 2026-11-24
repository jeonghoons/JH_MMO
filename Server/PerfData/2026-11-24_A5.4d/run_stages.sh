#!/bin/bash
# A5.4d 서버 PC 단계 실행기(A5.4c run_stages.sh 사본 — D-306·D-307): 단계 이름 = d_<A5.4c 단계>(노트북 run_bots.ps1 -Stages d_...와 같아야 함),
# 서버 설정 사본에 [Network] MoveBatchMs=${MOVEBATCH:-33}(P2). 나머지(봇 수·속도·배치·전원 계획)는 A5.4c와 같다.
# 단계마다: 봇 계정 초기화 → 서버 시작(BindIp=0.0.0.0, 설정 사본) → 노트북 봇이 들어오기를 기다림(players > 0)
#          → s5000이면 PC 몫 LoadTest(2,500명, 계정 0~2,499, 초당 100) → 플레이어가 최고치에 오른 뒤 0이 되고 PC 몫도 끝나면
#          → 서버를 내림(노트북은 포트가 닫히면 다음 단계로) → server_metrics.csv·db_check 저장. 결과: runs/<단계>/
# 전원 계획 고성능(D-303) — 끝나면(중간에 끊겨도) 균형 조정으로 되돌린다.
# 사용: [MOVEBATCH=33] bash run_stages.sh [단계...]   (생략하면 d_smoke d_s1000_1 d_s1000_2 d_s2500_1 d_s2500_2 d_s5000_1 d_s5000_2)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SET="$(basename "$HERE")"
SRV="$HERE/../../Server"
BALANCED=381b4222-f694-41f0-9685-ff5bb260df2e
HIGH=8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
MIX="idle=25,move=15,chat=10,hunt=20,loot=20,trade=5,auction=5"
trap 'taskkill //IM GameServer.exe //F > /dev/null 2>&1; powercfg //setactive $BALANCED' EXIT
powercfg //setactive $HIGH

SQL() { sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -f 65001 -h -1 -W -i "$(cygpath -w "$1")"; }
players_now() { grep -o "players=[0-9]*" "$1" 2>/dev/null | tail -1 | cut -d= -f2; }
log() { echo "[stages $(date +%H:%M:%S)] $*"; }

# 단계: 노트북 봇 수, 노트북 지속 초, PC 몫 봇 수(0 = 없음), 노트북을 기다리는 최대 분
stage_def() {
  case "${1#d_}" in
    smoke)   echo "20 30 0 60" ;;
    s1000_*) echo "1000 125 0 30" ;;
    s2500_*) echo "2500 135 0 30" ;;
    s5000_*) echo "2500 145 2500 30" ;;
    *) echo "" ;;
  esac
}

[ -f "$HERE/bot_accounts.sql" ] || python -I "$HERE/make_bot_accounts.py" > /dev/null   # 생성 파일(커밋하지 않음)

for STAGE in ${*:-d_smoke d_s1000_1 d_s1000_2 d_s2500_1 d_s2500_2 d_s5000_1 d_s5000_2}; do
  DEF="$(stage_def "$STAGE")"
  [ -z "$DEF" ] && { log "모르는 단계 $STAGE"; continue; }
  read LBOTS LDUR PCBOTS WAITMIN <<< "$DEF"
  TARGET=$((LBOTS + PCBOTS))
  RUN="$HERE/runs/$STAGE"
  rm -rf "$RUN"; mkdir -p "$RUN"

  sed -e "s/^BindIp=.*/BindIp=0.0.0.0/" \
      -e "s/^WorkerThreads=.*/WorkerThreads=0/" \
      -e "s/^DummySpawnZone=.*/DummySpawnZone=/" \
      -e "s/^DummyRoom=.*/DummyRoom=spread/" \
      -e "s/^DummyTownPercent=.*/DummyTownPercent=30/" \
      -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" \
      -e "s/^BotAccountPrefix=.*/BotAccountPrefix=bot_/" \
      -e "s/^Connections=.*/Connections=4/" \
      -e "s/^MoveBatchMs=.*/MoveBatchMs=${MOVEBATCH:-33}/" \
      -e "s/^Level=.*/Level=Info/" \
      -e "s#^Dir=.*#Dir=../PerfData/$SET/runs/$STAGE/logs#" \
      "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"

  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  SQL "$HERE/bot_accounts.sql" > "$RUN/db_reset.txt" 2>&1 || { log "bot_accounts.sql 실패"; cat "$RUN/db_reset.txt"; exit 1; }
  SQL "$HERE/db_check.sql" > "$RUN/db_before.txt" 2>&1

  cd "$SRV"
  unset JH_DATA_DIR
  ../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
  for i in $(seq 1 60); do (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null && break; sleep 1; done
  powershell -NoProfile -Command "\$p = Get-Process GameServer -ErrorAction SilentlyContinue; if (\$p) { 'epoch_ms,user_ms,kernel_ms,ws_mb'; while (-not \$p.HasExited) { '{0},{1},{2},{3}' -f [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(), [int64]\$p.UserProcessorTime.TotalMilliseconds, [int64]\$p.PrivilegedProcessorTime.TotalMilliseconds, [int64](\$p.WorkingSet64/1MB); Start-Sleep -Milliseconds 1000; \$p.Refresh() } }" > "$RUN/server_cpu.csv" 2>/dev/null &
  echo "$(date +%s) stage $STAGE laptopBots $LBOTS pcBots $PCBOTS target $TARGET moveBatchMs ${MOVEBATCH:-33}" > "$RUN/args.txt"
  log "$STAGE 서버 대기 중 — 노트북 봇을 기다림(최대 ${WAITMIN}분)"

  # 노트북 봇이 들어오기를 기다림
  START=$(date +%s); STARTED=0
  while [ $(( $(date +%s) - START )) -lt $((WAITMIN * 60)) ]; do
    P="$(players_now "$RUN/server_console.txt")"; [ "${P:-0}" -gt 0 ] && { STARTED=1; break; }
    sleep 2
  done
  if [ $STARTED -eq 0 ]; then log "$STAGE 노트북 봇이 오지 않음 — 멈춤"; echo "timeout waiting laptop" >> "$RUN/args.txt"; exit 2; fi
  log "$STAGE 노트북 봇 들어옴"

  PCPID=""
  if [ "$PCBOTS" -gt 0 ]; then
    ../x64/Release/LoadTest.exe --bots $PCBOTS --ramp-rate 100 --duration $LDUR --threads 4 --move-interval 200 --body male --login account \
      --account-start 0 --town-wander --mix $MIX --out "$(cygpath -w "$RUN/pc_loadtest.csv")" > "$RUN/pc_loadtest_console.txt" 2>&1 &
    PCPID=$!
    log "$STAGE PC 몫 봇 $PCBOTS명 시작"
  fi

  # 끝 판정: 최고 인원을 기록하고, 0으로 돌아오고 PC 몫도 끝나면 끝(시간 제한 = 지속 + 300초)
  PEAK=0; LIMIT=$(( $(date +%s) + LDUR + 300 ))
  while [ "$(date +%s)" -lt $LIMIT ]; do
    sleep 5
    P="$(players_now "$RUN/server_console.txt")"; P=${P:-0}
    [ "$P" -gt "$PEAK" ] && PEAK=$P
    PCDONE=1; [ -n "$PCPID" ] && kill -0 $PCPID 2>/dev/null && PCDONE=0
    if [ "$PEAK" -gt 0 ] && [ "$P" -eq 0 ] && [ $PCDONE -eq 1 ]; then break; fi
  done
  [ -n "$PCPID" ] && wait $PCPID 2>/dev/null
  echo "peak players $PEAK (target $TARGET)" >> "$RUN/args.txt"
  sleep 6   # 마지막 지표 한 줄
  taskkill //IM GameServer.exe //F > /dev/null 2>&1
  wait 2>/dev/null
  sleep 1
  SQL "$HERE/db_check.sql" > "$RUN/db_after.txt" 2>&1

  LOG="$(ls "$RUN/logs"/GameServer_*.log 2>/dev/null | head -1)"
  RID="$(basename "$LOG" .log)"; RID="${RID#GameServer_}"
  if [ -n "$RID" ]; then
    head -1 "$SRV/metrics.csv" > "$RUN/server_metrics.csv"
    grep "^$RID," "$SRV/metrics.csv" >> "$RUN/server_metrics.csv"
    echo "run_id $RID" >> "$RUN/args.txt"
  fi
  cd "$HERE"
  log "$STAGE 끝 — 최고 $PEAK명 / 목표 $TARGET명"
  sleep 20   # 노트북이 포트 닫힘을 보고 TIME_WAIT를 기다릴 여유
done
log "모든 단계 끝"
