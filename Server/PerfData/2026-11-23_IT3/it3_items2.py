# it3_items.py의 남은 단계: field = 시작 지점에서 사냥터로 → 처치 → 새 아이템 드랍 줍기, shop = 무기 6102 복원 → 상점 품목·판매로 정리.
# 사용: python -I -X utf8 it3_items2.py 8888 Test3 field|shop
import math, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import it3_items as t
gt, at, pt, mk, rs, qt = t.gt, t.at, t.pt, t.mk, t.rs, t.qt
check, log, pump_all, wait_all = t.check, t.log, t.pump_all, t.wait_all
PHASE = sys.argv[3]
GIVEN = {1401, 6403, 6203, 8002, 8004, 8051, 8055, 8053}

def swap_weapon(c, item_id):
    if any(r[0] == item_id and r[2] == 6 for r in c.items.values()):
        return
    d = next(d for d, r in c.items.items() if r[0] == item_id and r[2] == 0)
    n = len(c.equip_changes); c.pump(0.6); c.equip(6, d)
    wait_all([c], lambda: any(e[1] == c.id and e[3] == item_id for e in c.equip_changes[n:]), 6.0, f"무기 {item_id}")
    pump_all([c], 2.3)

def field(c):
    swap_weapon(c, 6301)
    pt.to_hunt(c)
    seen, picked = [], []
    for k in range(10):
        before = set(c.drops)
        qt.hunt_one(c, [])
        pump_all([c], 1.0)
        new = [(i, *c.drops[i]) for i in set(c.drops) - before]
        seen += [n[1] for n in new]
        for i, iid, x, y, z in new:
            if iid in t.NEW_IDS:
                n0 = sum(r[1] for r in c.bag(iid).values())
                r = pt.try_pickup(c, (i, x, y, z), [c])
                pump_all([c], 0.5)
                picked.append((iid, r, sum(r2[1] for r2 in c.bag(iid).values()) - n0))
        if picked:
            break
    log(f"처치 {k + 1}마리, 새 드랍 item id {seen}(0 = 골드), 주운 새 아이템 (id, 결과, 늘어난 수) {picked}")
    check("monster-drop-new", picked, f"새 아이템 드랍 없음 {seen}")
    check("pickup-monster-drop", picked and all(r == "ok" and inc > 0 for _, r, inc in picked), f"{picked}")

def shop(c):
    swap_weapon(c, 6102)
    walk = [(-14600, 1000), (-14500, -800), (t.SHOP_NPC[0], t.SHOP_NPC[1] + 300)]
    route = at.route_to_npc(c.x, c.y)[:-2] + walk
    at.walk_all([(c, route)])
    check("at-shop", c.shop_npc() and math.hypot(c.x - t.SHOP_NPC[0], c.y - t.SHOP_NPC[1]) <= 380, f"({c.x:.0f},{c.y:.0f}) {c.npcs}")
    n = len(c.shop_opens); c.pump(at.GAP); c.send(t.CS_SHOP_OPEN, rs.f_int(1, c.shop_npc()))
    wait_all([c], lambda: len(c.shop_opens) > n, 6.0, "상점 열기")
    check("shop-list-mp", 8002 in c.shop_opens[-1] and 8004 in c.shop_opens[-1], f"{c.shop_opens[-1]}")
    targets = GIVEN | {int(x) for x in sys.argv[4:]}   # 사냥에서 주운 아이템 id(field 단계 출력)
    g0, sold = c.gold, []
    for d, row in list(c.bag().items()):
        if row[0] in targets:
            got, rej = t.sell(c, d, row[1])
            want = int(t.ITEMS[row[0]]["SellPrice"]) * row[1]
            sold.append((row[0], row[1], got))
            check(f"sell-{row[0]}", not rej and got == want and d not in c.items, f"+{got} 기대 {want} {rej}")
    log(f"판매 {sold}, 골드 {g0} -> {c.gold}, 장착 {sorted((r[2], r[0]) for r in c.items.values() if r[2])}")

c = t.C(sys.argv[2]).enter()
pump_all([c], 1.0)
try:
    field(c) if PHASE == "field" else shop(c)
except Exception as e:
    import traceback; traceback.print_exc(); print(f"FAIL exception: {e}")
c.close(rst=False); time.sleep(3.0)
print(f"총 {len(gt.RESULTS)}건, 실패 {sum(r[0] != 'PASS' for r in gt.RESULTS)}건")
