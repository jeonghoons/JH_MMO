# 시험용 데이터 테이블 사본을 고친다. 사용: python -I -X utf8 tweak_tables.py <사본 폴더> <묶음>
#   clear: 우두머리를 약한 몬스터(1001)로, 방 몬스터는 선공하지 않는 1001만 지점당 1마리로(1002 행 삭제), 클리어 뒤 내보내기를 6초로 — 약한 시험 계정이 혼자 클리어할 수 있게
import csv, os, sys

folder, mode = sys.argv[1], sys.argv[2]


def rewrite(name, fn):
    path = os.path.join(folder, name)
    raw = open(path, "rb").read()
    bom = raw[:3] == b"\xef\xbb\xbf"
    text = raw.decode("utf-8-sig")
    crlf = "\r\n" in text
    rows = list(csv.reader(text.replace("\r\n", "\n").rstrip("\n").split("\n")))
    head = rows[0]
    kept = [head]
    for row in rows[1:]:
        if fn(dict(zip(head, row)), row, head) != "drop":
            kept.append(row)
    rows = kept
    out = ("\r\n" if crlf else "\n").join(",".join(r) for r in rows) + ("\r\n" if crlf else "\n")
    open(path, "wb").write((b"\xef\xbb\xbf" if bom else b"") + out.encode("utf-8"))


if mode == "clear":
    def world(d, row, head):
        if d["MapName"] == "Continent01_Dungeon":
            row[head.index("DungeonClearExitSec")] = "6"

    def spawn(d, row, head):
        if d["MapName"] != "Continent01_Dungeon":
            return
        if d["Zone"] == "boss":
            row[head.index("MonsterTemplateId")] = "1001"
        elif d["MonsterTemplateId"] != "1001":
            return "drop"
        else:
            row[head.index("CountPerSpawn")] = "1"

    rewrite("WorldTable.csv", world)
    rewrite("SpawnTable.csv", spawn)
else:
    sys.exit(f"모르는 묶음: {mode}")
