# A7.4 시연 촬영 전용 데이터 사본(D-311): Common/DataTable을 복사하고 MonsterTable의 몇 칸(MaxHp·AttackDamage)만 바꾼다 — 던전 몬스터를 한 방에,
# 우두머리를 스킬 2대에 잡히게 해 반복 구간을 짧게 찍는다. 저장소의 데이터는 건드리지 않고, 촬영 서버만 JH_DATA_DIR로 이 사본을 읽는다.
# 사용(저장소 루트): python -I -X utf8 Tools/Demo/demo_data.py <사본 폴더> [몬스터id[:열]=값 ...]   열 생략 = MaxHp
#   기본: 1004=60 1005=600 1001=120 1001:AttackDamage=5 (사냥터 몬스터도 빨리 잡히고 무리에 맞아 죽지 않게)
import csv
import io
import os
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "Common", "DataTable")

dst = os.path.abspath(sys.argv[1])
hp = {("1004", "MaxHp"): "60", ("1005", "MaxHp"): "600", ("1001", "MaxHp"): "120", ("1001", "AttackDamage"): "5"}
for a in sys.argv[2:]:
    k, v = a.split("=")
    mid, _, c = k.partition(":")
    hp[(mid, c or "MaxHp")] = v

if os.path.isdir(dst):
    shutil.rmtree(dst)
shutil.copytree(SRC, dst)

path = os.path.join(dst, "MonsterTable.csv")
raw = open(path, "rb").read()
bom = raw[:3] == b"\xef\xbb\xbf"
text = raw.decode("utf-8-sig")
nl = "\r\n" if "\r\n" in text else "\n"
rows = list(csv.reader(io.StringIO(text)))
head = rows[0]
changed = []
for r in rows[1:]:
    for (mid, c), v in hp.items():
        if r and r[0] == mid:
            col = head.index(c)
            changed.append(f"{mid}.{c}: {r[col]} -> {v}")
            r[col] = v
buf = io.StringIO()
csv.writer(buf, lineterminator=nl).writerows(rows)
open(path, "wb").write((b"\xef\xbb\xbf" if bom else b"") + buf.getvalue().encode("utf-8"))
print("data copy:", dst)
print(", ".join(changed))
