# 시험용 데이터 테이블 사본을 고친다(DG4). 사용: python -I -X utf8 tweak_tables.py <사본 폴더> <묶음>
#   clear: 던전의 모든 스폰을 선공하지 않는 약한 몬스터(1001) 지점당 1마리로(우두머리도), 클리어 뒤 내보내기를 6초로
#          — 약한 시험 계정이 혼자 길을 따라가며 전부 잡아 클리어할 수 있게(길·스폰 자리·클리어 절차 확인용)
import os, sys
folder, mode = sys.argv[1], sys.argv[2]


def rewrite(name, fn):
    path = os.path.join(folder, name)
    raw = open(path, "rb").read().decode("utf-8-sig")
    crlf = "\r\n" in raw
    rows = [r.split(",") for r in raw.replace("\r\n", "\n").rstrip("\n").split("\n")]
    head = rows[0]
    for row in rows[1:]:
        fn(dict(zip(head, row)), row, head)
    nl = "\r\n" if crlf else "\n"
    open(path, "wb").write((nl.join(",".join(r) for r in rows) + nl).encode("utf-8"))


if mode == "clear":
    def world(d, row, head):
        if d["MapName"] == "Continent01_Dungeon":
            row[head.index("DungeonClearExitSec")] = "6"

    def spawn(d, row, head):
        if d["MapName"] == "Continent01_Dungeon":
            row[head.index("MonsterTemplateId")] = "1001"
            row[head.index("CountPerSpawn")] = "1"

    rewrite("WorldTable.csv", world)
    rewrite("SpawnTable.csv", spawn)
else:
    sys.exit(f"모르는 묶음: {mode}")
