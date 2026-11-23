#!/bin/bash
# DG3c-1 chaos(DG2의 것에 던전을 더함): 서버 하나를 띄워 둔 채, 던전 입구 앞(사냥터)에서 시작해 2명씩 파티를 맺고 던전에 들어가
# 평균 8초 뒤 나왔다가 다시 들어가는 chaos 사냥 봇 400명(평균 6초마다 파티 탈퇴 — 던전 안 탈퇴 = 마을로 내보냄)을
# ① 끝까지 한 번 돌리고 ② 3~12초 뒤 강제 종료하기를 10번 한다(입장 요청·로딩 대기·동반 이동·내보내기 도중에 끊기는 경우가 섞임).
# 마지막 봇이 사라진 뒤 [metrics]가 시작 때로 돌아오는지 본다: sessions·players·world(roster/chMax)·party(now) 0, roomMove의 abortBusy/orphan/ownerBad 0,
# dungeon(now) 0·만든 수 = 닫은 수, roomsAlive = rooms = 2, monsters = 시작 값. 결과: runs/chaos/.
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; RUN="$HERE/runs/chaos"
ENTRANCE="19020,-18800,300"
rm -rf "$RUN"; mkdir -p "$RUN"
sed -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/chaos/logs#" -e "s/^DummyRoom=.*/DummyRoom=field/" -e "s/^DummySpawnZone=.*/DummySpawnZone=$ENTRANCE/" \
    -e "s/^PortalNoRange=.*/PortalNoRange=true/" -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 8
ARGS="--bots 400 --ramp-rate 100 --threads 4 --move-interval 200 --body male --profile hunt --chaos --zone $ENTRANCE --party-size 2 --party-churn-sec 6 --dungeon --dungeon-stay-sec 8 --map ../MapResource/Export/Continent01/Logic/Continent01_Logic.json --dungeon-map ../MapResource/Export/Continent01_Dungeon/Logic/Continent01_Dungeon_Logic.json"
echo "$ARGS" > "$RUN/args.txt"
../x64/Release/LoadTest.exe $ARGS --duration 45 > "$RUN/loadtest_full.txt" 2>&1; echo "full exit $?" >> "$RUN/args.txt"
sleep 12
for k in 1 2 3 4 5 6 7 8 9 10; do
  ../x64/Release/LoadTest.exe $ARGS --duration 60 > "$RUN/loadtest_kill$k.txt" 2>&1 &
  sleep $((2 + k)); taskkill //IM LoadTest.exe //F > /dev/null 2>&1; echo "kill $k after $((2 + k))s" >> "$RUN/args.txt"
  sleep 12
done
sleep 12
tasklist | grep -i GameServer.exe > /dev/null && echo "server alive" > "$RUN/summary.txt" || echo "SERVER DEAD" > "$RUN/summary.txt"
taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
echo "WARN/ERROR 줄 수: $(cat "$RUN"/logs/*.log | grep -cE " (WARN|ERROR) ")" >> "$RUN/summary.txt"
grep -hE " (WARN|ERROR) " "$RUN"/logs/*.log | cut -c1-220 | sort -k5 | uniq -c -f4 | head -10 >> "$RUN/summary.txt"
PICK="sessions=[0-9]+|rooms=[0-9]+|players=[0-9]+|monsters=[0-9]+|forced\)=\S+|chMax\)=\S+|ownerBad\)=\S+ \S+|party\([^)]*\)=\S+|roomsAlive=[0-9]+|dungeon\([^)]*\)=\S+ \S+|sessionObjs/handles idleKick/sendqKick\)=\S+"
echo "first | $(grep -h "\[metrics\]" "$RUN"/logs/*.log | head -1 | grep -oE "$PICK" | tr '\n' ' ')" >> "$RUN/summary.txt"
echo "peak dungeons | $(grep -h "\[metrics\]" "$RUN"/logs/*.log | grep -oE "dungeon\([^)]*\)=[0-9]+" | grep -oE "[0-9]+$" | sort -n | tail -1)" >> "$RUN/summary.txt"
echo "last  | $(grep -h "\[metrics\]" "$RUN"/logs/*.log | tail -1 | grep -oE "$PICK" | tr '\n' ' ')" >> "$RUN/summary.txt"
grep -h "summary\] \(dungeon\|room\|party\|attempts\)" "$RUN/loadtest_full.txt" | cut -c1-260 >> "$RUN/summary.txt"
cat "$RUN/summary.txt"
