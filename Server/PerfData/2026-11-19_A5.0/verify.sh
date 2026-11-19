#!/bin/bash
# A5.0 기능 확인(서버 하나, Release, IdleTimeoutSec=10으로 줄인 설정): 유휴 타임아웃 → 로그인+입장 연속 500회 → 오류 입력.
# 단계마다 15초 기다린 뒤의 [metrics] 한 줄(sessions·players·conn)을 verify_summary.txt에 남긴다. 결과: runs/verify/
# 계정 Test1~Test4(비밀번호 1234)는 DB에 있어야 한다.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; SET="$(basename "$HERE")"; SRV="$HERE/../../Server"; RUN="$HERE/runs/verify"
PY="python -I -X utf8"
rm -rf "$RUN"; mkdir -p "$RUN"
sed -e "s/^IdleTimeoutSec=.*/IdleTimeoutSec=10/" \
    -e "s#^Dir=.*#Dir=../PerfData/$SET/runs/verify/logs#" \
    "$SRV/ServerConfig.ini" > "$RUN/ServerConfig.ini"
taskkill //IM GameServer.exe //F > /dev/null 2>&1
cd "$SRV"
../x64/Release/GameServer.exe --config "$(cygpath -w "$RUN/ServerConfig.ini")" > "$RUN/server_console.txt" 2>&1 &
for i in $(seq 1 60); do if (exec 3<>/dev/tcp/127.0.0.1/8888) 2>/dev/null; then break; fi; sleep 1; done; sleep 5

SUM="$RUN/verify_summary.txt"; : > "$SUM"
mark() { sleep 12; echo "-- $1 ($(date +%H:%M:%S))" | tee -a "$SUM"; }   # 지표가 두 번 찍히기를 기다린다. 그 시각의 값은 아래 metrics_brief.txt에서 본다
step() { echo "== $*" | tee -a "$SUM"; "$@" 2>&1 | tail -4 | tee -a "$SUM"; }

mark "시작"
# 1) 유휴 타임아웃: 로그인 전 / 로그인 뒤 / 입장 뒤 침묵은 10~15초 사이에 끊기고, 5초마다 핑을 보내면 30초 동안 끊기지 않는다
step $PY "$HERE/idle.py" --mode connect --count 5 --limit 25 --expect closed
step $PY "$HERE/idle.py" --mode login --count 5 --limit 25 --expect closed
step $PY "$HERE/idle.py" --mode ingame --count 5 --limit 25 --expect closed
step $PY "$HERE/idle.py" --mode ingame --account Test1 --limit 25 --expect closed
step $PY "$HERE/idle.py" --mode ping --count 5 --limit 30 --ping-interval 5 --expect alive
mark "유휴 뒤"
# 유휴로 끊긴 개별 계정이 접속 목록에 남지 않았으면 바로 다시 로그인된다(남았으면 30초 강제 제거까지 BUSY)
step $PY "$HERE/idle.py" --mode ping --account Test1 --limit 6 --ping-interval 2 --expect alive
mark "재로그인 뒤"
# 2) 로그인 + 입장 연속 500회
step $PY "$HERE/login_loop.py" --count 500 --close mixed
mark "연속 500(dummy) 뒤"
step $PY "$HERE/login_loop.py" --count 500 --close mixed --accounts Test1,Test2,Test3,Test4
sleep 35   # 중복 접속 대기·강제 제거 시간(30초)이 지나도 남는 것이 없는지
mark "연속 500(Test1~4) 뒤"
# 3) 오류 입력
step $PY "$HERE/login_loop.py" --count 400 --errors
mark "오류 입력 400 뒤"

tasklist | grep -i GameServer.exe > /dev/null && echo "server alive" | tee -a "$SUM" || echo "SERVER DEAD" | tee -a "$SUM"
taskkill //IM GameServer.exe //F > /dev/null 2>&1; wait
echo "WARN/ERROR 줄 수: $(grep -hcE " (WARN|ERROR) " "$RUN"/logs/*.log)" | tee -a "$SUM"
grep -hE " (WARN|ERROR) " "$RUN"/logs/*.log | cut -c1-220 | sort -k5 | uniq -c -f4 | head -20 >> "$SUM"
grep -h "\[metrics\]" "$RUN"/logs/*.log | grep -oE "^\S+ \S+|sessions=[0-9]+|players=[0-9]+| in/s=[0-9]+|sendQ\S+|login\(total kick/busy/forced\)=\S+|conn\(.*" | paste - - - - - - - | cut -c12- > "$RUN/metrics_brief.txt"
echo done
