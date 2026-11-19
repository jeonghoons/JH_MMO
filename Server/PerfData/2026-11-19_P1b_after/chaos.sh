#!/bin/bash
# chaos 램프 후 세션 잔류 확인(P1b 완료 기준): 서버 하나를 띄워 둔 채 ① chaos 봇을 끝까지 ② chaos 봇을 도중에 강제 종료 × 3 을 돌리고,
# 마지막 봇이 사라진 뒤 [metrics]의 sessions·players·sendQ가 0으로 돌아오는지 본다. 결과: runs/chaos/.
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; RUN="$HERE/runs/chaos"
rm -rf "$RUN"; mkdir -p "$RUN"
sed -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/chaos/logs#" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 3
ARGS="--bots 300 --ramp-rate 60 --threads 4 --move-interval 200 --body male --profile hunt --chaos"
echo "$ARGS" > "$RUN/args.txt"
../x64/Release/LoadTest.exe $ARGS --duration 30 > "$RUN/loadtest_full.txt" 2>&1; echo "full exit $?" >> "$RUN/args.txt"
for k in 1 2 3; do
  ../x64/Release/LoadTest.exe $ARGS --duration 60 > "$RUN/loadtest_kill$k.txt" 2>&1 &
  sleep $((6 + k * 3)); taskkill //IM LoadTest.exe //F > /dev/null 2>&1; echo "kill $k" >> "$RUN/args.txt"; sleep 2
done
sleep 14
tasklist | grep -i GameServer.exe > /dev/null && echo "server alive" >> "$RUN/args.txt" || echo "SERVER DEAD" >> "$RUN/args.txt"
taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
echo done
