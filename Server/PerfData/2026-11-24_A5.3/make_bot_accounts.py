# A5.2 봇 계정(bot_0 ~ bot_{N-1}) 생성 SQL을 만든다(D-296). 사용: python -I make_bot_accounts.py [계정 수=5000] [출력=bot_accounts.sql]
# 만든 SQL은 한 트랜잭션으로 ① 'bot_' 계정의 흔적(경매·우편·퀘스트·빠른 칸·아이템·캐릭터·계정)을 지우고 ② 같은 내용으로 다시 넣는다.
# 그래서 실행할 때마다 봇 계정이 같은 처음 상태가 된다(서버를 끈 채로 실행). Test1~4·dummy는 건드리지 않는다.
# 외형(D-294와 같은 규칙)·인벤토리·골드는 ItemTable에서 고르며 시드가 고정이라 매번 같다.
import csv, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
COUNT = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'bot_accounts.sql')
PASSWORD = '1234'
SEED = 20261124

def read_table(name):
    with open(os.path.join(REPO, 'Common', 'DataTable', name), encoding='utf-8-sig', newline='') as f:
        return list(csv.DictReader(f))

items = read_table('ItemTable.csv')
stats = read_table('PlayerStatTable.csv')
hp1 = int(next(r for r in stats if int(r['Level']) == 1)['MaxHp'])

SLOT_NO = {'Head': 1, 'Top': 2, 'Bottom': 3, 'Gloves': 4, 'Boots': 5, 'Weapon': 6, 'Offhand': 7}
armor = {s: [int(r['Name']) for r in items if r['Slot'] == s] for s in ('Head', 'Top', 'Bottom', 'Gloves', 'Boots')}
weapons = {k: [int(r['Name']) for r in items if r['Slot'] == 'Weapon' and r['WeaponType'] == k] for k in ('Sword', 'Bow', 'TwoHandSword')}
offhand = [int(r['Name']) for r in items if r['Slot'] == 'Offhand']
heal = [int(r['Name']) for r in items if r['UseEffect'] in ('HealHp', 'HealMp')]
max_stack = {int(r['Name']): int(r['MaxStack']) for r in items}
gear = [i for s in armor.values() for i in s] + [i for w in weapons.values() for i in w] + offhand
for k, v in list(armor.items()) + list(weapons.items()) + [('Offhand', offhand), ('heal', heal)]:
    assert v, f'ItemTable에 후보가 없음: {k}'

rng = random.Random(SEED)
accounts = []   # (idx, login, playerType, gold)
rows = []       # (idx, itemId, count, location, equipSlot, bagSlot)
for i in range(COUNT):
    ptype = 1 if rng.random() < 0.5 else 2
    gold = rng.randint(1000, 5000)
    accounts.append((i, f'bot_{i}', ptype, gold))
    for s, cand in armor.items():
        if rng.random() >= 0.15:
            rows.append((i, rng.choice(cand), 1, 2, SLOT_NO[s], -1))
    kind = rng.choice(('Sword', 'Bow', 'TwoHandSword'))
    rows.append((i, rng.choice(weapons[kind]), 1, 2, 6, -1))
    if kind == 'Sword' and rng.random() < 0.5:
        rows.append((i, rng.choice(offhand), 1, 2, 7, -1))
    # 인벤토리: 물약 두 스택 + 거래·경매에 쓸 장비 3~5개
    bag = 0
    for pid in rng.sample(heal, 2):
        rows.append((i, pid, min(20, max_stack[pid]), 1, 0, bag)); bag += 1
    for _ in range(rng.randint(3, 5)):
        rows.append((i, rng.choice(gear), 1, 1, 0, bag)); bag += 1

def chunks(seq, n=900):
    for k in range(0, len(seq), n):
        yield seq[k:k + n]

out = []
w = out.append
w('-- 자동 생성: make_bot_accounts.py (수정하지 말 것). 서버를 끈 채로 실행.')
w('-- sqlcmd -S ".\\SQLEXPRESS" -d GameServer -E -I -b -f 65001 -h -1 -W -i bot_accounts.sql')
w('SET NOCOUNT ON; SET XACT_ABORT ON;')
w('BEGIN TRAN;')
w("DECLARE @P TABLE (PlayerId BIGINT PRIMARY KEY);")
w("INSERT INTO @P SELECT p.PlayerId FROM Player_Info p JOIN User_Account a ON a.AccountId = p.AccountId WHERE a.LoginId LIKE 'bot[_]%';")
w('DELETE s FROM Auction_Sale s WHERE s.BuyerId IN (SELECT PlayerId FROM @P) OR s.ListingId IN (SELECT ListingId FROM Auction_Listing WHERE SellerId IN (SELECT PlayerId FROM @P));')
w('DELETE FROM Player_Mail WHERE PlayerId IN (SELECT PlayerId FROM @P) OR ListingId IN (SELECT ListingId FROM Auction_Listing WHERE SellerId IN (SELECT PlayerId FROM @P));')
w('DELETE FROM Auction_Listing WHERE SellerId IN (SELECT PlayerId FROM @P);')
w('DELETE FROM Player_Quest WHERE PlayerId IN (SELECT PlayerId FROM @P);')
w('DELETE FROM Player_QuickSlot WHERE PlayerId IN (SELECT PlayerId FROM @P);')
w('DELETE FROM Trade_Log WHERE PlayerA IN (SELECT PlayerId FROM @P) OR PlayerB IN (SELECT PlayerId FROM @P);')
w('DELETE FROM Player_Item WHERE OwnerId IN (SELECT PlayerId FROM @P);')
w('DELETE FROM Player_Info WHERE PlayerId IN (SELECT PlayerId FROM @P);')
w("DELETE FROM User_Account WHERE LoginId LIKE 'bot[_]%';")
w('CREATE TABLE #B (Idx INT PRIMARY KEY, LoginId VARCHAR(50), PlayerType INT, Gold BIGINT);')
w('CREATE TABLE #I (Idx INT, ItemId INT, Cnt INT, Loc TINYINT, EquipSlot TINYINT, BagSlot INT);')
for part in chunks(accounts):
    w('INSERT INTO #B VALUES ' + ','.join(f"({i},'{l}',{t},{g})" for i, l, t, g in part) + ';')
for part in chunks(rows):
    w('INSERT INTO #I VALUES ' + ','.join(f'({a},{b},{c},{d},{e},{f})' for a, b, c, d, e, f in part) + ';')
w(f"INSERT INTO User_Account (LoginId, LoginPassword) SELECT LoginId, '{PASSWORD}' FROM #B ORDER BY Idx;")
w('INSERT INTO User_Info (AccountId) SELECT a.AccountId FROM User_Account a JOIN #B b ON b.LoginId = a.LoginId;')
w(f'INSERT INTO Player_Info (AccountId, Name, PlayerType, [Level], Exp, Hp, Gold) SELECT a.AccountId, b.LoginId, b.PlayerType, 1, 0, {hp1}, b.Gold FROM User_Account a JOIN #B b ON b.LoginId = a.LoginId ORDER BY b.Idx;')
w('INSERT INTO Player_Item (OwnerId, ItemId, [Count], Location, EquipSlot, BagSlot) SELECT p.PlayerId, i.ItemId, i.Cnt, i.Loc, i.EquipSlot, i.BagSlot FROM #I i JOIN #B b ON b.Idx = i.Idx JOIN Player_Info p ON p.Name = b.LoginId;')
w('DROP TABLE #B; DROP TABLE #I;')
w('COMMIT TRAN;')
w("SELECT COUNT(*) AS bots, (SELECT COUNT(*) FROM Player_Item it JOIN Player_Info p ON p.PlayerId = it.OwnerId WHERE p.Name LIKE 'bot[_]%') AS items FROM Player_Info WHERE Name LIKE 'bot[_]%';")
with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(out) + '\n')
print(f'{OUT}: accounts {len(accounts)}, item rows {len(rows)}')
