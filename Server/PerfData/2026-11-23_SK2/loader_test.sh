#!/bin/bash
# SK2 로더 오류 시험: SkillTable 사본의 3003(Pierce) 행을 망가뜨려 서버가 이유를 찍고 시작을 멈추는지 본다.
HERE="$(cd "$(dirname "$0")" && pwd)"
SRV="$HERE/../../Server"
DATA="$HERE/../../../Common/DataTable"
OUT="$HERE/runs/loader"; rm -rf "$OUT"; mkdir -p "$OUT"
case_run() {   # $1 이름, $2 sed 식(3003 행에 적용)
  local D="$OUT/$1"; mkdir -p "$D/data"; cp "$DATA"/*.csv "$D/data/"
  sed -i "/^3003,/ $2" "$D/data/SkillTable.csv"
  grep "^3003," "$D/data/SkillTable.csv" | cut -d, -f1-16 > "$D/row.txt"
  sed -e "s/^Port=.*/Port=8895/" -e "s#^Dir=.*#Dir=#" "$SRV/ServerConfig.ini" > "$D/ServerConfig.ini"
  (cd "$SRV" && JH_DATA_DIR="$(cygpath -w "$D/data")" timeout 20 ../x64/Debug/GameServer.exe --config "$(cygpath -w "$D/ServerConfig.ini")" > "$D/console.txt" 2>&1); local rc=$?
  if grep -q "Pierce 스킬은" "$D/console.txt" && [ $rc -ne 124 ]; then echo "PASS loader-$1 (종료 코드 $rc)"; else echo "FAIL loader-$1 (종료 코드 $rc)"; tail -5 "$D/console.txt"; fi
  grep -m1 "Pierce 스킬은" "$D/console.txt"
}
# 열: Name,WeaponType,Slot,TargetingType,HitShape,HitDelayMs,CooldownMs,AnimMs,MoveLockMs,CastRange,Radius,Angle,Range,Width,BaseDamage,ProjSpeed
case_run width0   's/^\(\([^,]*,\)\{13\}\)80,/\10,/'
case_run speed0   's/^\(\([^,]*,\)\{15\}\)1000,/\10,/'
case_run range0   's/^\(\([^,]*,\)\{12\}\)2500,/\10,/'
case_run targeted 's/^3003,Bow,2,NonTargeted,/3003,Bow,2,Targeted,/; s/^\(\([^,]*,\)\{9\}\)0,/\11000,/'
