# IT3 확인: 옛 판금 ID(x301)를 이어받은 02 강철이 시험 계정에서 정상 로드·장착되는지, 끝나면 원래 장착으로 되돌린다.
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "2026-11-20_DG3b"))
import movekit as mk

CS_CHANGE_EQUIP, SC_CHANGE_EQUIP, SC_CHANGE_EQUIP_RESULT = 1009, 2011, 2013
account = sys.argv[1] if len(sys.argv) > 1 else "Test3"

inv = {}
equips = []
rejects = []
c = mk.Client(account)
orig_handle = c.handle
def handle(pid, body):
    if pid == mk.SC_INVENTORY:
        for raw in mk.decode(body).get(1, []):
            m = mk.decode(raw)
            inv[mk.first(m, 1)] = (mk.first(m, 2), mk.first(m, 4), mk.first(m, 5))
    elif pid == SC_CHANGE_EQUIP:
        m = mk.decode(body)
        if mk.first(m, 1) == c.id:
            st = mk.decode(mk.first(m, 4, b""))
            equips.append((mk.first(m, 2), mk.first(m, 3), mk.first(st, 1), mk.first(st, 3), mk.first(st, 6)))
    elif pid == SC_CHANGE_EQUIP_RESULT:
        m = mk.decode(body)
        rejects.append((mk.first(m, 1), mk.first(m, 2), mk.first(m, 3)))
    orig_handle(pid, body)
c.handle = handle
c.enter()
c.pump(1.0)
print(f"[{account}] 인벤토리 {len(inv)}행 받음")
orig = {part: (dbid, iid) for dbid, (iid, part, slot) in inv.items() if part}
print("원래 장착:", {p: iid for p, (d, iid) in sorted(orig.items())})

def equip(part, dbid):
    n = len(equips) + len(rejects)
    c.send(CS_CHANGE_EQUIP, mk.f_varint(1, part) + mk.f_varint(3, dbid) if hasattr(mk, "f_varint") else varint_body(part, dbid))
    c.pump_until(lambda: len(equips) + len(rejects) > n, 5.0, f"장착 결과 part={part}")
    c.pump(0.6)  # 교체 빈도 제한

def varint(v):
    out = b""
    while True:
        b = v & 0x7F; v >>= 7
        if v: out += bytes([b | 0x80])
        else: return out + bytes([b])
def varint_body(part, dbid):
    return bytes([0x08]) + varint(part) + bytes([0x18]) + varint(dbid)

plate = {}
for dbid, (iid, part, slot) in inv.items():
    if part == 0 and iid in (1301, 2301, 3301, 4301, 5301, 6301, 7301):
        plate.setdefault(iid, dbid)
order = [(1, 1301), (2, 2301), (3, 3301), (4, 4301), (5, 5301), (6, 6301), (7, 7301)]
for part, iid in order:
    if iid in plate:
        equip(part, plate[iid])
print("판금 장착 결과(part, item, maxHp, atk, maxMp):")
for e in equips: print("  ", e)
print("거부:", rejects)
ok = len(equips) == len(order) and not rejects

# 되돌리기: 방패 해제 → 원래 무기 → 원래 방어구
n_eq = len(equips)
if 7 not in orig: equip(7, 0)
for part in (6, 1, 2, 3, 4, 5):
    if part in orig: equip(part, orig[part][0])
print("되돌리기 결과:")
for e in equips[n_eq:]: print("  ", e)
print("거부(전체):", rejects)
c.close(rst=False)
print("판정:", "통과" if ok and not rejects else "실패")
