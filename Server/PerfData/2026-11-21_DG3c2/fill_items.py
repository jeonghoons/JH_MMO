# Test1~Test4에 ItemTable의 모든 종류를 채운다(개발 지급 명령 CS_DEV_GIVE — 서버 [Debug] DevCommands=true 필요).
# 이미 가진 종류(가방·장착)는 건너뛰고, 장비는 1개, 쌓이는 아이템은 MaxStack까지 채운다.
import csv, sys, time
D = r"C:\Users\kjhkjh\KJH\JH_MMO\Server\PerfData\2026-11-20_DG3c"
sys.path.insert(0, D)
import dungeon_test as dt
mk = dt.mk
rs = mk.rs

rows = list(csv.DictReader(open(r"C:\Users\kjhkjh\KJH\JH_MMO\Common\DataTable\ItemTable.csv", encoding="utf-8-sig")))
ITEMS = [(int(r["Name"]), int(r["MaxStack"]), r["DisplayName"]) for r in rows]


class Client(dt.Client):
    def __init__(self, account, **kw):
        self.inv = {}        # item_db_id -> (item_id, count, equip_part, slot)
        self.max_slots = 0
        super().__init__(account, **kw)

    def _item(self, raw):
        m = mk.decode(raw)
        self.inv[mk.first(m, 1)] = (mk.first(m, 2), mk.first(m, 3) or 0, mk.first(m, 4) or 0, mk.first(m, 5))

    def handle(self, pid, body):
        if pid == mk.SC_INVENTORY:
            m = mk.decode(body)
            self.inv = {}
            for raw in m.get(1, []):
                self._item(raw)
            self.max_slots = mk.first(m, 3) or 0
        elif pid == mk.SC_INVENTORY_UPDATE:
            m = mk.decode(body)
            for raw in m.get(1, []):
                self._item(raw)
            for db_id in m.get(2, []):
                self.inv.pop(db_id, None)
        super().handle(pid, body)

    def counts(self):
        out = {}
        for item_id, count, part, slot in self.inv.values():
            out[item_id] = out.get(item_id, 0) + count
        return out

    def bag_used(self):
        return sum(1 for v in self.inv.values() if not v[2])


for acct in sys.argv[1:]:
    c = Client(acct)
    if not c.spawned:
        c.enter()
    dt.pump_all([c], 1.0)
    before = c.counts()
    given = []
    for item_id, max_stack, name in ITEMS:
        need = max(max_stack, 1) - before.get(item_id, 0)
        if need <= 0:
            continue
        n_res = len(c.item_results)
        n_upd = len(c.inv_updates)
        c.send(mk.CS_DEV_GIVE, rs.f_int(1, item_id) + rs.f_int(2, need))
        c.pump_until(lambda: len(c.inv_updates) > n_upd or len(c.item_results) > n_res, 5.0, f"{acct} {item_id} 지급 응답")
        if len(c.item_results) > n_res:
            given.append(f"{item_id} 거부(result={c.item_results[-1][2]})")
        else:
            given.append(f"{item_id}x{need}")
        dt.pump_all([c], 0.25)   # ItemMinIntervalMs 100
    after = c.counts()
    missing = [i for i, s, n in ITEMS if after.get(i, 0) < max(s, 1)]
    print(f"{acct}: 지급 {given if given else '없음(이미 모두 있음)'}")
    print(f"  가방 {c.bag_used()}/{c.max_slots}칸, 종류별 수량 {dict(sorted(after.items()))}, 빠진 종류 {missing}")
    c.close()
    time.sleep(0.5)
