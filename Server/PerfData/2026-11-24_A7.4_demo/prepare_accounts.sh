#!/bin/bash
# A7.4 촬영 전 시험 계정 준비(D-311): 서버를 끈 채로 Q1 고정 상태로 되돌린 뒤 촬영용 차이를 넣는다. 촬영마다(리허설 뒤 본 촬영 전 포함) 다시 돌린다.
# 사용: bash prepare_accounts.sh          촬영 상태로
#       bash prepare_accounts.sh restore  고정 상태로만(촬영 뒤 되돌리기)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
if tasklist | grep -qi "GameServer.exe"; then echo "서버가 켜져 있음 — 먼저 끄세요"; exit 1; fi
SQL() { sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -f 65001 -W -i "$(cygpath -w "$1")"; }
SQL "$HERE/../2026-11-22_Q1/restore_accounts.sql"
[ "${1:-}" = "restore" ] && { echo "고정 상태로 되돌림"; exit 0; }
SQL "$HERE/demo_accounts.sql"
