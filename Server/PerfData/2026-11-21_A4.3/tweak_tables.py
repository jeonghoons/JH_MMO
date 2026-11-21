# 시험용 데이터 테이블 사본의 EconomyTable 값을 바꾼다. 사용: python -I -X utf8 tweak_tables.py <사본 폴더> <열=값> [...]
# 예: tweak_tables.py runs/auction_expire/data AuctionDurationSec=6 AuctionExpireCheckSec=1
import os, sys

folder, edits = sys.argv[1], dict(e.split("=", 1) for e in sys.argv[2:])
path = os.path.join(folder, "EconomyTable.csv")
raw = open(path, "rb").read()
bom = raw[:3] == b"\xef\xbb\xbf"
text = raw.decode("utf-8-sig")
crlf = "\r\n" in text
lines = text.replace("\r\n", "\n").rstrip("\n").split("\n")
head, row = lines[0].split(","), lines[1].split(",")
for name, value in edits.items():
    if name not in head:
        sys.exit(f"EconomyTable에 없는 열: {name}")
    row[head.index(name)] = value
lines[1] = ",".join(row)
out = ("\r\n" if crlf else "\n").join(lines) + ("\r\n" if crlf else "\n")
open(path, "wb").write((b"\xef\xbb\xbf" if bom else b"") + out.encode("utf-8"))
print("EconomyTable:", ", ".join(f"{k}={v}" for k, v in edits.items()))
