# IT3 완료 기준 Claude 테스트: 새 아이템(01~03등급·MP 물약·전리품)으로 장착·경매 등록·버리기/줍기·상점 판매·몬스터 드랍/줍기.
# 사용: python -I -X utf8 it3_items.py 8888 [계정]   (서버는 DevCommands=true 설정 사본으로 띄움)
# 지급한 아이템은 끝에 상점에 팔아 정리한다(골드만 판매가만큼 늘어남). 장착은 원래대로 되돌린다.
import math, os, sys, time
PD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
for d in ("2026-11-20_DG3b", "2026-11-20_DG2", "2026-11-20_DG3c", "2026-11-21_DG4", "2026-11-21_A4.3", "2026-11-22_B2", "2026-11-22_Q1"):
    sys.path.insert(0, os.path.join(PD, d))
import quest_test as qt
gt, at, pt, mk, rs = qt.gt, qt.at, qt.pt, qt.mk, qt.rs
check, log, pump_all, wait_all = gt.check, gt.log, pt.pump_all, pt.wait_all

ACCOUNT = sys.argv[2] if len(sys.argv) > 2 else "Test3"
CS_SHOP_OPEN, CS_SHOP_SELL, SC_SHOP_OPEN = 1031, 1033, 2029
SHOP_NPC = (-14150.0, -2200.0)
ITEMS = gt.table("ItemTable.csv")
NEW_IDS = {i for i in ITEMS if (i // 100) % 10 in (2, 3, 4) or i in (6103, 8002, 8003, 8004) or 8051 <= i <= 8055}

class C(qt.Q):
    def __init__(self, account, **kw):
        self.shop_opens = []
        super().__init__(account, **kw)
    def handle(self, pid, body):
        if pid == SC_SHOP_OPEN:
            m = mk.decode(body)
            self.shop_opens.append([mk.first(mk.decode(raw), 1) for raw in m.get(2, [])])
        super().handle(pid, body)
    def shop_npc(self):
        return next((i for i, (t, k, x, y) in self.npcs.items() if k == qt.NPC_KIND_SHOP), 0)

def equip_and_check(c, part, item_id):
    dbid = next(d for d, r in c.items.items() if r[0] == item_id and r[2] == 0)
    before = dict(c.stat); n = len(c.equip_changes)
    c.pump(0.6); c.equip(part, dbid)
    wait_all([c], lambda: any(e[1] == c.id for e in c.equip_changes[n:]) or c.order[-1:] == [2013], 6.0, f"장착 {item_id}")
    got = [e for e in c.equip_changes[n:] if e[1] == c.id]
    check(f"equip-{item_id}", got and got[-1][3] == item_id, f"장착 결과 없음/다름 {got}")
    pump_all([c], 2.3)  # 무기 교체 잠금
    return before, got[-1][4] if got else None

def sell(c, dbid, count):
    g0, n_upd, n_res = c.gold, len(c.inv_updates), len(c.item_results)
    c.pump(at.GAP)
    c.send(CS_SHOP_SELL, rs.f_int(1, c.shop_npc()) + rs.f_int(2, dbid) + rs.f_int(3, count))
    wait_all([c], lambda: len(c.inv_updates) > n_upd or len(c.item_results) > n_res, 6.0, "판매 응답")
    return c.gold - g0, c.item_results[n_res:]

def main():
    c = C(ACCOUNT).enter()
    pump_all([c], 1.0)
    orig = {r[2]: (d, r[0]) for d, r in c.items.items() if r[2]}
    gold0 = c.gold
    log(f"{ACCOUNT} 골드 {gold0}, 장착 {[v[1] for k, v in sorted(orig.items())]}, 가방 {len(c.bag())}/{c.max_slots}")

    # 1) 지급(개발 명령) — 새 아이템 8종
    gifts = [(1401, 1), (6403, 1), (6203, 1), (8002, 5), (8004, 2), (8051, 5), (8055, 3), (8053, 2)]
    for iid, n in gifts:
        new = at.give(c, iid, n)
        check(f"give-{iid}", new and c.items[new[0]][0] == iid and c.items[new[0]][1] == n, f"{new} {[c.items.get(x) for x in new]}")

    # 2) 장착: 03 투구(+MP), 03 양손검, 되돌리기
    b, st = equip_and_check(c, 1, 1401)
    h = ITEMS[1401]; h0 = ITEMS[orig[1][1]]
    check("stat-1401", st and st["max_hp"] - b["max_hp"] == int(h["BonusMaxHp"]) - int(h0["BonusMaxHp"]) and st["max_mp"] - b["max_mp"] == int(h["BonusMaxMp"]) - int(h0["BonusMaxMp"]), f"{b} -> {st}")
    b, st = equip_and_check(c, 6, 6403)
    w = ITEMS[6403]; w0 = ITEMS[orig[6][1]]
    check("stat-6403", st and st["atk"] - b["atk"] == int(w["BonusAttackDamage"]) - int(w0["BonusAttackDamage"]), f"{b} -> {st}")
    for part in (1, 6):
        n = len(c.equip_changes); c.pump(0.6); c.equip(part, orig[part][0])
        wait_all([c], lambda: any(e[1] == c.id and e[3] == orig[part][1] for e in c.equip_changes[n:]), 6.0, f"복원 {part}")
        pump_all([c], 2.3)
    check("restore-equip", {r[2]: r[0] for r in c.items.values() if r[2]} == {k: v[1] for k, v in orig.items()}, f"{c.items}")

    # 3) 경매: 03 투구 등록 → 검색에 보임 → 취소 → 수령으로 되찾음
    at.to_npc(c)
    head = next(d for d, r in c.bag(1401).items())
    res, listing = at.register(c, head, 1, 500)
    check("auction-register-1401", res == at.A_SUCCESS and head not in c.items, f"result {res}")
    found, _ = at.search_all(c, 1)
    check("auction-search-1401", any(l["id"] == listing and l["item"] == 1401 for l in found), f"{found[:3]}")
    check("auction-cancel", at.cancel(c, listing) == at.A_SUCCESS, "")
    view = at.my(c)
    mail = at.find_mail(view, listing, at.MAIL_CANCELLED)
    check("auction-mail", mail is not None and mail["item"] == 1401, f"{view['mails']}")
    check("auction-claim", mail and at.claim(c, mail["id"]) == at.A_SUCCESS and c.bag(1401), f"{c.bag(1401)}")

    # 4) 버리기 → 바닥 드랍(새 아이템 ID) → 줍기
    d8053 = next(iter(c.bag(8053)))
    before = set(c.drops); n_upd = len(c.inv_updates)
    c.pump(at.GAP); c.send(qt.CS_DROP_ITEM, rs.f_int(1, d8053))
    wait_all([c], lambda: set(c.drops) - before, 6.0, "버린 드랍")
    did = next(iter(set(c.drops) - before))
    check("drop-8053", c.drops[did][0] == 8053 and d8053 not in c.items, f"{c.drops[did]}")
    r = pt.try_pickup(c, (did, *c.drops[did][1:]), [c])
    pump_all([c], 0.5)
    check("pickup-8053", r == "ok" and sum(x[1] for x in c.bag(8053).values()) == 2, f"{r} {c.bag(8053)}")

    # 5) 사냥터: 몬스터 처치 → 새 아이템 드랍 → 줍기 (새 아이템이 나올 때까지 최대 8마리)
    sword = next((d for d, r in c.items.items() if r[0] == 6301 and r[2] == 0), None)
    if sword:
        n = len(c.equip_changes); c.pump(0.6); c.equip(6, sword)
        wait_all([c], lambda: any(e[1] == c.id and e[3] == 6301 for e in c.equip_changes[n:]), 6.0, "강철 검"); pump_all([c], 2.3)
    pt.to_hunt(c)
    seen, picked = [], []
    for k in range(8):
        before = set(c.drops)
        qt.hunt_one(c, [])
        pump_all([c], 1.0)
        new = [(i, *c.drops[i]) for i in set(c.drops) - before]
        seen += [n[1] for n in new]
        for i, iid, x, y, z in new:
            if iid in NEW_IDS:
                r = pt.try_pickup(c, (i, x, y, z), [c])
                picked.append((iid, r))
        if picked:
            break
    log(f"처치 {k + 1}마리, 드랍 {seen}, 주운 새 아이템 {picked}")
    check("monster-drop-new", picked, f"새 아이템 드랍 없음 {seen}")
    check("pickup-monster-drop", all(r == "ok" for _, r in picked), f"{picked}")
    if sword:
        n = len(c.equip_changes); c.pump(0.6); c.equip(6, orig[6][0])
        wait_all([c], lambda: any(e[1] == c.id and e[3] == orig[6][1] for e in c.equip_changes[n:]), 6.0, "무기 복원"); pump_all([c], 2.3)
    at.to_town(c)

    # 6) 상점: 품목에 MP 물약, 새 아이템 판매가 = SellPrice × 수량 — 지급한 것·주운 것 정리
    walk = [(-14600, 1000), (-14500, -800), (SHOP_NPC[0], SHOP_NPC[1] + 300)]
    at.walk_all([(c, at.route_to_npc(c.x, c.y)[:-1] + walk)])
    check("at-shop", c.shop_npc() and math.hypot(c.x - SHOP_NPC[0], c.y - SHOP_NPC[1]) <= 380, f"({c.x:.0f},{c.y:.0f}) {c.npcs}")
    n = len(c.shop_opens); c.pump(at.GAP); c.send(CS_SHOP_OPEN, rs.f_int(1, c.shop_npc()))
    wait_all([c], lambda: len(c.shop_opens) > n, 6.0, "상점 열기")
    check("shop-list-mp", 8002 in c.shop_opens[-1] and 8004 in c.shop_opens[-1], f"{c.shop_opens[-1]}")
    sold = []
    targets = {1401, 6403, 6203, 8002, 8004, 8051, 8055, 8053} | {iid for iid, _ in picked}
    for d, row in list(c.bag().items()):
        if row[0] in targets:
            got, rej = sell(c, d, row[1])
            sold.append((row[0], row[1], got))
            check(f"sell-{row[0]}", not rej and got == int(ITEMS[row[0]]["SellPrice"]) * row[1] and d not in c.items, f"+{got} 기대 {int(ITEMS[row[0]]['SellPrice']) * row[1]} {rej}")
    log(f"판매 {sold}, 골드 {gold0} -> {c.gold}")
    c.close(rst=False); time.sleep(3.0)
    fails = [r for r in gt.RESULTS if r[0] != "PASS"]
    print(f"총 {len(gt.RESULTS)}건, 실패 {len(fails)}건", flush=True)

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        import traceback; traceback.print_exc()
        print(f"FAIL exception: {e}")
