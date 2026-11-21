#!/bin/bash
# B2 로더 범위 검사: 데이터 테이블 사본 하나에 잘못된 값 하나씩 넣고 --roll-drops 1(테이블만 읽고 종료)로 시작이 중단되는지 본다.
# 정상 값(ok)은 종료 코드 0, 잘못된 값은 ERROR 한 줄과 함께 시작 중단.
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; REPO="$HERE/../../.."
OUT="$HERE/runs/loader"; rm -rf "$OUT"; mkdir -p "$OUT"
run() {  # 이름 파일 파이썬식(행 body, 머리 h, col()을 고침)
  D="$OUT/$1"; cp -r "$REPO/Common/DataTable" "$D"
  python -I -X utf8 - "$D/$2" "$3" <<'PY'
import csv, io, sys
p, expr = sys.argv[1], sys.argv[2]
rows = [r for r in csv.reader(open(p, encoding="utf-8", newline="")) if r]
h = rows[0]; body = rows[1:]
def col(n): return h.index(n)
def row(name): return next(r for r in body if r[0] == str(name))
exec(expr)
o = io.StringIO(); csv.writer(o, lineterminator="\n").writerows([h] + body); open(p, "w", encoding="utf-8", newline="").write(o.getvalue())
PY
  (cd "$SRV" && JH_DATA_DIR="$(cygpath -w "$D")" ../x64/Debug/GameServer.exe --roll-drops 1 > "$D.txt" 2>&1); code=$?
  msg="$(grep -m1 "ERROR" "$D.txt" | sed 's/.*\[Data\] //' | cut -c1-160)"
  echo "$1 exit=$code $msg"
}
run ok            EconomyTable.csv "pass"
run combat0       EconomyTable.csv "body[0][col('CombatHoldMs')]='0'"
run combat_big    EconomyTable.csv "body[0][col('CombatHoldMs')]='60001'"
run hpint0        EconomyTable.csv "body[0][col('HpRegenIntervalSec')]='0'"
run hppct101      EconomyTable.csv "body[0][col('HpRegenPercent')]='101'"
run hppct_neg     EconomyTable.csv "body[0][col('HpRegenPercent')]='-1'"
run mpc0          EconomyTable.csv "body[0][col('MpRegenPercentCombat')]='0'"
run mpi_big       EconomyTable.csv "body[0][col('MpRegenPercentIdle')]='101'"
run no_hpint      EconomyTable.csv "i=col('HpRegenIntervalSec'); h.pop(i); [r.pop(i) for r in body]"
run item_ok       ItemTable.csv "pass"
run sword_mp      ItemTable.csv "row(6101)[col('BonusMaxMp')]='5'"
run head_mp_big   ItemTable.csv "row(1301)[col('BonusMaxMp')]='1'; row(1301)[col('Slot')]='Head'; row(2301)[col('BonusMaxMp')]='3'"
run top_atk       ItemTable.csv "row(2301)[col('BonusAttackDamage')]='5'"
run gloves_atk_ok ItemTable.csv "row(4301)[col('BonusAttackDamage')]='5'"
run loot_useval   ItemTable.csv "row(8051)[col('UseValue')]='1'"
run heal_mp0      ItemTable.csv "row(8002)[col('UseValue')]='0'"
run tier_bad      ItemTable.csv "row(1101)[col('Tier')]='Ornate'"
run epic_grade    ItemTable.csv "row(1301)[col('Grade')]='Epic'"
run unknown_eff   ItemTable.csv "row(8002)[col('UseEffect')]='HealXp'"
run skill_type    SkillTable.csv "row(4001)[col('WeaponType')]='Staff'"
run skill_prefix  SkillTable.csv "row(4002)[col('Name')]='1002'"
run drop_bad_item DropTable.csv "row(1001)[col('ItemId')]='999999'"
run shop_bad_item ShopTable.csv "row(1001)[col('ItemId')]='999999'"
