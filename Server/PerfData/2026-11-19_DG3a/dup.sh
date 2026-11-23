#!/bin/bash
# DG3a: 중복 접속·빠른 재접속 회귀(dup_login.py). DbSaveDelayMs=1500으로 마지막 저장을 늦춘 서버에서 돌린다. 결과: runs/dup/
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; RUN="$HERE/runs/dup"
rm -rf "$RUN"; mkdir -p "$RUN"
sed -e "s/^DbSaveDelayMs=.*/DbSaveDelayMs=1500/" -e "s/^Level=.*/Level=Debug/" -e "s#^Dir=.*#Dir=../PerfData/$(basename "$HERE")/runs/dup/logs#" -e "s/^AllowDummyLogin=.*/AllowDummyLogin=true/" "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 3
cd "$HERE"; python -I -X utf8 dup_login.py 2>&1 | tee "$RUN/result.txt"
sleep 12
taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
grep -h "\[metrics\]" "$RUN"/logs/*.log | grep -oE "^\S+ \S+|sessions=[0-9]+|players=[0-9]+|login\(total kick/busy/forced\)=\S+|world\(.*" | paste - - - - - | cut -c12- | tee "$RUN/metrics_brief.txt"
grep -h "\[World\]\|\[Lobby\]" "$RUN"/logs/*.log | cut -c12-200 > "$RUN/world_lobby.txt"; cat "$RUN/world_lobby.txt"
echo "WARN/ERROR: $(grep -hcE " (WARN|ERROR) " "$RUN"/logs/*.log)"
