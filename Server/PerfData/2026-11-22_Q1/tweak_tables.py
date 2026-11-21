# Q1 시험용 데이터 테이블 사본을 고친다. 사용: python -I -X utf8 tweak_tables.py <사본 폴더> <묶음>
#   mail    : 경매 등록 기간 8초·만료 검사 1초(만료 알림을 기다리지 않게)
#   dungeon : 던전 우두머리 1005를 시험 계정이 잡을 수 있게 — HP 400, 공격력 700(5레벨 파티원은 두 대에 죽고 30레벨은 버팀), 비선공(맞기 전에는 공격하지 않음),
#             걸어서 복귀(HP 유지). 던전의 다른 스폰은 비선공 1001 한 마리씩, 클리어 뒤 내보내기 8초.
#             (퀘스트 3의 대상은 1005 그대로다 — 우두머리의 종류는 바꾸지 않는다)
import os, sys
folder, mode = sys.argv[1], sys.argv[2]


def rewrite(name, fn):
    path = os.path.join(folder, name)
    raw = open(path, "rb").read()
    bom = raw[:3] == b"\xef\xbb\xbf"
    text = raw.decode("utf-8-sig")
    crlf = "\r\n" in text
    rows = [r.split(",") for r in text.replace("\r\n", "\n").rstrip("\n").split("\n")]
    head = rows[0]
    for row in rows[1:]:
        fn(dict(zip(head, row)), row, head)
    nl = "\r\n" if crlf else "\n"
    open(path, "wb").write((b"\xef\xbb\xbf" if bom else b"") + (nl.join(",".join(r) for r in rows) + nl).encode("utf-8"))


def setter(values, when=lambda d: True):
    def fn(d, row, head):
        if when(d):
            for k, v in values.items():
                row[head.index(k)] = str(v)
    return fn


if mode == "mail":
    rewrite("EconomyTable.csv", setter({"AuctionDurationSec": 8, "AuctionExpireCheckSec": 1}))
elif mode == "dungeon":
    rewrite("MonsterTable.csv", setter({"MaxHp": 400, "AttackDamage": 700, "AggroRangeCm": 0, "ReturnMode": 0}, lambda d: d["Name"] == "1005"))
    rewrite("SpawnTable.csv", setter({"MonsterTemplateId": 1001, "CountPerSpawn": 1}, lambda d: d["MapName"] == "Continent01_Dungeon" and d["Zone"] != "boss"))
    rewrite("WorldTable.csv", setter({"DungeonClearExitSec": 8}, lambda d: d["MapName"] == "Continent01_Dungeon"))
else:
    sys.exit(f"모르는 묶음: {mode}")
print("테이블 사본:", mode)
