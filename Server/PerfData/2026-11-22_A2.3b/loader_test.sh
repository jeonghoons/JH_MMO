#!/bin/bash
# A2.3b 로더 범위 검사: 데이터 테이블 사본 하나에 잘못된 값 하나씩 넣고 --roll-drops 1(테이블만 읽고 종료)로 시작이 중단되는지 본다.
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; REPO="$HERE/../../.."
OUT="$HERE/runs/loader"; rm -rf "$OUT"; mkdir -p "$OUT"
run() {  # 이름 파일 파이썬식(행 r, 머리 h를 고침)
  D="$OUT/$1"; cp -r "$REPO/Common/DataTable" "$D"
  python -I -X utf8 - "$D/$2" "$3" <<'PY'
import csv, io, sys
p, expr = sys.argv[1], sys.argv[2]
rows = [r for r in csv.reader(open(p, encoding="utf-8", newline="")) if r]
h = rows[0]; body = rows[1:]
def col(n): return h.index(n)
exec(expr)
o = io.StringIO(); csv.writer(o, lineterminator="\n").writerows([h] + body); open(p, "w", encoding="utf-8", newline="").write(o.getvalue())
PY
  (cd "$SRV" && JH_DATA_DIR="$(cygpath -w "$D")" ../x64/Debug/GameServer.exe --roll-drops 1 > "$D.txt" 2>&1); code=$?
  msg="$(grep -m1 "ERROR" "$D.txt" | sed 's/.*\[Data\] //' | cut -c1-160)"
  echo "$1 exit=$code $msg"
}
run ok         PlayerStatTable.csv "pass"
run maxmp0     PlayerStatTable.csv "body[0][col('MaxMp')]='0'"
run exp_neg    PlayerStatTable.csv "body[3][col('ExpToNext')]='-1'"
run exp_zero   PlayerStatTable.csv "body[3][col('ExpToNext')]='0'"
run max_nonzero PlayerStatTable.csv "body[-1][col('ExpToNext')]='100'"
run level_gap  PlayerStatTable.csv "del body[9]"
run no_mpcol   PlayerStatTable.csv "i=col('MaxMp'); h.pop(i); [r.pop(i) for r in body]"
run mpcost_neg SkillTable.csv "body[2][col('MpCost')]='-1'"
run scale_big  SkillTable.csv "body[3][col('AttackScale')]='1001'"
run scale_neg  SkillTable.csv "body[3][col('AttackScale')]='-5'"
run mon_lv0    MonsterTable.csv "body[0][col('Level')]='0'"
run mon_expneg MonsterTable.csv "body[0][col('Exp')]='-1'"
