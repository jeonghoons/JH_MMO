#!/bin/bash
# Q1 로더 검사: 데이터 테이블 사본 하나에 잘못된 값 하나씩 넣고 --roll-drops 1(테이블만 읽고 종료 — DB·포트를 열지 않음)로 시작이 중단되는지 본다.
# 정상 값(ok·…_ok)은 종료 코드 0, 잘못된 값은 ERROR 한 줄과 함께 시작 중단(B2 loader_test.sh와 같은 방식). 결과: runs/loader/, loader_out.txt
HERE="$(cd "$(dirname "$0")" && pwd)"; SRV="$HERE/../../Server"; REPO="$HERE/../../.."
OUT="$HERE/runs/loader"; rm -rf "$OUT"; mkdir -p "$OUT"
run() {  # 이름 파일 파이썬식(행 body, 머리 h, col()·row()를 써서 고침. 파일 경로는 p)
  D="$OUT/$1"; cp -r "$REPO/Common/DataTable" "$D"
  python -I -X utf8 - "$D/$2" "$3" <<'PY'
import csv, io, os, sys
p, expr = sys.argv[1], sys.argv[2]
rows = [r for r in csv.reader(open(p, encoding="utf-8", newline="")) if r]
h = rows[0]; body = rows[1:]
keep = True
def col(n): return h.index(n)
def row(name): return next(r for r in body if r[0] == str(name))
exec(expr)
if keep:
    o = io.StringIO(); csv.writer(o, lineterminator="\n").writerows([h] + body); open(p, "w", encoding="utf-8", newline="").write(o.getvalue())
else:
    os.remove(p)
PY
  (cd "$SRV" && JH_DATA_DIR="$(cygpath -w "$D")" ../x64/Debug/GameServer.exe --roll-drops 1 > "$D.txt" 2>&1); code=$?
  msg="$(grep -m1 "ERROR" "$D.txt" | sed 's/.*\[Data\] //' | sed 's/^.*runs.loader.//' | cut -c1-170)"
  echo "$1 exit=$code $msg"
}
run ok                QuestTable.csv "pass"
run dup_id            QuestTable.csv "row(2)[0]='1'"
run id_zero           QuestTable.csv "row(1)[0]='0'"
run name_empty        QuestTable.csv "row(1)[col('DisplayName')]=''"
run npc_missing       QuestTable.csv "row(1)[col('NpcId')]='1999'"
run npc_shop          QuestTable.csv "row(1)[col('NpcId')]='1001'"
run npc_auction       QuestTable.csv "row(1)[col('NpcId')]='1002'"
run kind_bad          QuestTable.csv "row(1)[col('Kind')]='Talk'"
run kill_no_monster   QuestTable.csv "row(1)[col('TargetId')]='8052'"
run collect_no_item   QuestTable.csv "row(2)[col('TargetId')]='1001'"
run count_zero        QuestTable.csv "row(1)[col('Count')]='0'"
run collect_too_many  QuestTable.csv "row(2)[col('Count')]='2971'"
run collect_max_ok    QuestTable.csv "row(2)[col('Count')]='2970'"
run level_zero        QuestTable.csv "row(1)[col('RecommendLevel')]='0'"
run level_over        QuestTable.csv "row(1)[col('RecommendLevel')]='31'"
run exp_negative      QuestTable.csv "row(1)[col('RewardExp')]='-1'"
run gold_over         QuestTable.csv "row(1)[col('RewardGold')]='2000000001'"
run reward_no_item    QuestTable.csv "row(1)[col('RewardItemId')]='999999'"
run reward_count_0    QuestTable.csv "row(1)[col('RewardItemCount')]='0'"
run reward_over_stack QuestTable.csv "row(2)[col('RewardItemCount')]='2'"
run reward_none_ok    QuestTable.csv "row(1)[col('RewardItemId')]='0'; row(1)[col('RewardItemCount')]='0'"
run reward_none_count QuestTable.csv "row(1)[col('RewardItemId')]='0'"
run no_npc_column     QuestTable.csv "i=col('NpcId'); h.pop(i); [r.pop(i) for r in body]"
run no_desc_ok        QuestTable.csv "i=col('Description'); h.pop(i); [r.pop(i) for r in body]"
run no_file           QuestTable.csv "keep=False"
run npc_row_gone      NpcTable.csv   "body[:]=[r for r in body if r[0]!='1003']"
