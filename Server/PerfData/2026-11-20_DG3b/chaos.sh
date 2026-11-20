#!/bin/bash
# DG3b chaos: 서버 하나를 띄워 둔 채 ① 마을에서 시작해 문 포털로 나가는 chaos 사냥 봇 600명(주기적으로 포털도 씀)을 끝까지 ② 같은 봇을 도중에 강제 종료 x 10 을 돌리고,
# 마지막 봇이 사라진 뒤 [metrics]의 sessions·players·sendQ·conn·world(roster/chMax)가 시작 때 값으로 돌아오는지, roomMove의 abortBusy/orphan/ownerBad가 0인지 본다. 결과: runs/chaos/.
# 강제 종료 시점이 3~12초라 로그인 직후·포털 이동 도중에 끊기는 경우가 섞인다. sessionObjs의 바닥은 1이다(Listener가 다음 접속용 세션을 하나 미리 만들어 둔다).
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; RUN="$HERE/runs/chaos"
rm -rf "$RUN"; mkdir -p "$RUN"
sed -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/chaos/logs#" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 8
brief() { grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "sessions=[0-9]+|players=[0-9]+|sendQ\(total/sessionMax\)=[0-9/]+|conn\([^)]*\)=[0-9/ ]+" | tr '\n' ' '; }
echo "start | $(brief)" > "$RUN/summary.txt"
ARGS="--bots 600 --ramp-rate 120 --threads 4 --move-interval 200 --body male --profile hunt --chaos --portal-interval 3000"
echo "$ARGS" > "$RUN/args.txt"
../x64/Release/LoadTest.exe $ARGS --duration 30 > "$RUN/loadtest_full.txt" 2>&1; echo "full exit $?" >> "$RUN/args.txt"
sleep 12; echo "full | $(brief)" >> "$RUN/summary.txt"
for k in 1 2 3 4 5 6 7 8 9 10; do
  ../x64/Release/LoadTest.exe $ARGS --duration 60 > "$RUN/loadtest_kill$k.txt" 2>&1 &
  sleep $((2 + k)); taskkill //IM LoadTest.exe //F > /dev/null 2>&1; echo "kill $k after $((2 + k))s" >> "$RUN/args.txt"
  sleep 12; echo "kill $k | $(brief)" >> "$RUN/summary.txt"
done
sleep 10; echo "end | $(brief)" >> "$RUN/summary.txt"
tasklist | grep -i GameServer.exe > /dev/null && echo "server alive" >> "$RUN/summary.txt" || echo "SERVER DEAD" >> "$RUN/summary.txt"
taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
echo "WARN/ERROR 줄 수: $(grep -hcE " (WARN|ERROR) " "$RUN"/logs/*.log)" >> "$RUN/summary.txt"
grep -hE " (WARN|ERROR) " "$RUN"/logs/*.log | cut -c1-200 | head -10 >> "$RUN/summary.txt"
# 서버가 떠 있는 동안에는 위 brief가 로그를 읽지 못해 비어 나온다 — 판정은 서버를 끈 뒤 뽑는 아래 표로 한다
grep -h "\[metrics\]" "$RUN"/logs/*.log | grep -oE "^\S+ \S+|sessions=[0-9]+|players=[0-9]+|sendQ\S+|forced\)=\S+|chMax\)=\S+|ownerBad\)=\S+ \S+|handles idleKick/sendqKick\)=\S+" | paste - - - - - - - - | cut -c12- > "$RUN/metrics_brief.txt"
cat "$RUN/summary.txt"
