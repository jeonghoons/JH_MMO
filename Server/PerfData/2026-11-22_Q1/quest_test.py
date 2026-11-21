# Q1 퀘스트 + 경매 수령함 알림 원시 소켓 시험(계정 Test1~Test4). 사용: python -I -X utf8 quest_test.py <포트> <묶음>
# 서버는 run_tests.sh가 묶음에 맞는 설정·데이터 테이블 사본으로 띄운다(묶음마다 그 앞에 restore_accounts.sql로 시험 계정을 시험 전 상태로 되돌린다).
#   main    (DevCommands=true, 원본 테이블) 입장 때 SC_QUEST_LIST(인벤토리 다음)·퀘스트 NPC(npc_kind 3) → 먼 곳에서 요청 TOO_FAR → 요청 = 수락 → 다시 요청 = 완료 시도 NOT_FINISHED
#           → 받지 않은 퀘스트에 "완료" 요청 = 수락(D-264) → 1001 처치 15번(매번 SC_QUEST_UPDATE + DB 저장, 다른 퀘스트·다른 사람은 그대로)
#           → 16번째는 그대로 → 완료 보상(아이템 새 행·골드·경험치 순서) → 다시 수락·완료 ALREADY_DONE
#           → 수집: 9개 NOT_ENOUGH_ITEM · 10개 회수 + 보상(빈 칸 재사용·레벨업·바로 저장) · 재료를 미리 가진 채 첫 요청 = 수락(회수 없음) → 다음 요청 = 완료 · 여러 스택(적은 스택부터, 가득 찬 인벤토리에서 회수로 빈 칸에 보상)
#           · 인벤토리 가득 INVENTORY_FULL · 골드 상한 GOLD_FULL → 재접속해도 목록·레벨·골드 유지
#   cut     (DbSaveDelayMs=1500) 처치 직후 끊김 → 곧 다시 로그인되고 처치 수 유지 · 수락 요청 직후 끊김 → 진행 중으로 남음
#           · 완료 요청 직후 끊김 → 완료·회수·보상 아이템·골드와 **보상 경험치까지** 남음
#   dungeon (우두머리 1005를 약하게 한 테이블 사본, PortalNoRange=true) 3인 파티가 던전에 들어가 한 명이 우두머리를 잡으면
#           죽은 파티원·시야 밖(시작 방) 파티원까지 셋 다 처치 인정(경험치는 막타만) → 마을로 나와 완료 보상
#   mail    (AuctionDurationSec 8·AuctionExpireCheckSec 1) 내 등록이 팔리면 판매자에게 SC_MAIL_NOTICE(SALE_GOLD, 물건·대금·등록 id), 구매자에게는 없음
#           · 만료 반환은 EXPIRED · 접속하지 않은 동안 팔리면 다음 입장 때 NONE(SC_QUEST_LIST 다음) · 수령함이 빈 계정은 알림 없음
#   err     (DevCommands=false, DbSaveDelayMs=400) TOO_FAR · 없는 NPC·상점 NPC·없는 퀘스트 id NO_QUEST
#           · 요청 연타(한 번만 수락, 나머지 TOO_FREQUENT/BUSY/NOT_FINISHED) · 1:1 거래 중 IN_TRADE · 더미 봇 NOT_ALLOWED · 사망 DEAD · 깨진 패킷 뒤에도 서버 정상
#   fail    (DbItemFailEvery=2) 롤백된 수락·완료는 DB_ERROR이고 DB·메모리가 그대로, 다시 요청하면 성공
# 판정 줄은 "PASS <이름>" / "FAIL <이름>: 이유". 묶음 끝에서 DB 합계(아이템 수량 합, 보유 골드 + 수령함 골드)가 "개발 지급 + 퀘스트 보상 − 회수(·버린 것)"만큼만 변했는지 본다.
# 공용 코드: DG3b movekit·DG2 party_test·DG3c dungeon_test·DG4 dg4_test(던전 길 따라 걷기)·A4.3 auction_test(아이템·경매 요청)·B2 growth_test(경험치·처치).
import math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
for d in ("2026-11-20_DG3b", "2026-11-20_DG2", "2026-11-20_DG3c", "2026-11-21_DG4", "2026-11-21_A4.3", "2026-11-22_B2"):
    sys.path.insert(0, os.path.join(HERE, "..", d))   # -I 는 스크립트 폴더를 경로에 넣지 않는다
import growth_test as gt     # sys.argv[1] = 포트로 mk.rs.connect를 바꿔 둔다
import auction_test as at
import dg4_test as dg4       # dt.Client.walk_to를 던전 안에서 "길 따라 걷기"로 바꾼다
mk, pt, dt, rs = gt.mk, gt.pt, at.dt, at.rs

CS_QUEST_REQUEST = 1038   # 요청 하나(D-264) — 서버가 상태로 수락·완료를 정한다
CS_QUEST_ACCEPT = CS_QUEST_COMPLETE = CS_QUEST_REQUEST   # 시험 안의 이름(의도)만 남김 — 보내는 패킷은 같다
SC_QUEST_LIST, SC_QUEST_UPDATE, SC_QUEST_RESULT, SC_MAIL_NOTICE = 2042, 2043, 2044, 2045
CS_DROP_ITEM, CS_TRADE_CANCEL = 1014, 1023
Q_NONE, Q_ACTIVE, Q_DONE = 0, 1, 2
(R_SUCCESS, R_NOT_IN_ROOM, R_DEAD, R_BUSY, R_TOO_FREQUENT, R_NOT_ALLOWED, R_NO_QUEST, R_TOO_FAR, R_IN_TRADE, R_ALREADY_ACTIVE, R_ALREADY_DONE,
 R_NOT_ACTIVE, R_NOT_FINISHED, R_NOT_ENOUGH_ITEM, R_INVENTORY_FULL, R_GOLD_FULL, R_DB_ERROR) = range(1, 18)
MAIL_NONE, MAIL_SALE, MAIL_EXPIRED = 0, 1, 3
NPC_KIND_SHOP, NPC_KIND_QUEST = 1, 3
QUEST_NPC = (-9300.0, 600.0)        # 경비대장 마커(cm) — 마을 북문 포털 101 안쪽(Continent01_Logic.json pointId 402)
QUEST_NPC_TEMPLATE = 1003
QUEST_NPC_GUESS = 900000002         # 마을 Room의 세 번째 NPC 마커(NPC_ID_BEGIN + 2) — NPC가 시야에 없을 때(사냥터·죽은 뒤) 쓰는 id
SHOP_RANGE = 400.0
ITEM_HORN, ITEM_JUNK, ITEM_HEAD, ITEM_POTION = 8052, 1101, 1101, 8001
GAP = at.GAP

check, log, pump_all, wait_all = gt.check, gt.log, pt.pump_all, pt.wait_all
QUEST = gt.table("QuestTable.csv")
ECON = gt.table("EconomyTable.csv")[1001]
EXPECT = {"items": 0, "gold": 0}    # 퀘스트 보상(+)·회수(−)·버린 것(−)으로 바뀌어야 하는 DB 합계


class Q(gt.C, at.Client):
    """성장 시험 클라이언트(경험치·레벨·피해) + 경매 시험 클라이언트(아이템·NPC·경매) + 퀘스트·수령함 알림 기록"""

    def __init__(self, account, **kw):
        self.quests = {}           # quest_id -> (state, progress): SC_QUEST_LIST로 채우고 SC_QUEST_UPDATE로 맞춘다
        self.quest_lists = []      # (t, {quest_id: (state, progress)})
        self.quest_updates = []    # (t, quest_id, state, progress)
        self.quest_results = []    # (t, request, quest_id, result)
        self.mail_notices = []     # (t, kind, item_id, count, gold, listing_id)
        self.vel = {}              # 다른 오브젝트 id -> (vx, vy, 받은 시각): 이동 패킷은 구간이 바뀔 때만 오므로 그 사이 위치는 속도로 이어 계산한다
        super().__init__(account, **kw)

    def handle(self, pid, body):
        now = gt.ts()
        if pid == SC_QUEST_LIST:
            got = {}
            for raw in mk.decode(body).get(1, []):
                m = mk.decode(raw)
                got[mk.first(m, 1)] = (mk.first(m, 2), mk.first(m, 3))
            self.quests = dict(got)
            self.quest_lists.append((now, got))
        elif pid == SC_QUEST_UPDATE:
            m = mk.decode(mk.first(mk.decode(body), 1, b""))
            self.quests[mk.first(m, 1)] = (mk.first(m, 2), mk.first(m, 3))
            self.quest_updates.append((now, mk.first(m, 1), mk.first(m, 2), mk.first(m, 3)))
        elif pid == SC_QUEST_RESULT:
            m = mk.decode(body)
            self.quest_results.append((now, mk.first(m, 1), mk.first(m, 2), mk.first(m, 3)))
        elif pid == SC_MAIL_NOTICE:
            m = mk.decode(body)
            self.mail_notices.append((now, mk.first(m, 1), mk.first(m, 2), mk.first(m, 3), mk.first(m, 4), mk.first(m, 5)))
        elif pid in (mk.SC_ADD_OBJECT, mk.SC_MOVE_OBJECT):
            info = mk.decode(mk.first(mk.decode(body), 1, b""))
            if mk.first(info, 1) != self.id:
                pm = mk.decode(mk.first(info, 4, b""))
                self.vel[mk.first(info, 1)] = (mk.fl(pm, 5), mk.fl(pm, 6), time.time())
        super().handle(pid, body)

    def where(self, oid):
        """그 오브젝트의 지금 위치 추정(마지막 이동 패킷의 위치 + 속도 × 지난 시간, 최대 4초분). 서 있으면 속도 0이라 받은 위치 그대로."""
        _, x, y, z = self.objects[oid]
        vx, vy, t = self.vel.get(oid, (0.0, 0.0, 0.0))
        dt = min(max(0.0, time.time() - t), 4.0)
        return x + vx * dt, y + vy * dt, z

    def quest_npc(self):
        for nid, (template, kind, x, y) in self.npcs.items():
            if kind == NPC_KIND_QUEST:
                return nid
        return 0

    def send_quest(self, request, quest_id, npc=None):
        npc = self.quest_npc() if npc is None else npc
        self.send(request, f_i(1, npc) + f_i(2, quest_id))

    def npc_dist(self):
        return math.hypot(self.x - QUEST_NPC[0], self.y - QUEST_NPC[1])


def f_i(field, v):
    return rs.varint(field << 3) + rs.varint(v & ((1 << 64) - 1))   # 음수도 보낼 수 있게(잘못된 값 시험)


# ---- 요청하고 결과를 기다리는 도우미 ----
def quest_req(c, request, quest_id, others=(), npc=None, timeout=8.0):
    """요청을 보내고 그 퀘스트의 SC_QUEST_UPDATE(성공) 또는 SC_QUEST_RESULT(거부)를 기다린다. 성공이면 ("ok", state, progress), 거부면 사유 번호."""
    c.pump(GAP)
    nu, nr = len(c.quest_updates), len(c.quest_results)
    c.send_quest(request, quest_id, npc)
    wait_all([c, *others], lambda: any(u[1] == quest_id for u in c.quest_updates[nu:]) or any(r[1] == request for r in c.quest_results[nr:]), timeout,
             f"{c.tag} 퀘스트 요청 {request} quest {quest_id}의 응답")
    rej = [r for r in c.quest_results[nr:] if r[1] == request]
    if rej:
        return rej[0][3]
    u = [u for u in c.quest_updates[nu:] if u[1] == quest_id][0]
    return ("ok", u[2], u[3])


def accept(c, quest_id, others=(), npc=None):
    return quest_req(c, CS_QUEST_ACCEPT, quest_id, others, npc)


def complete(c, quest_id, others=(), npc=None):
    return quest_req(c, CS_QUEST_COMPLETE, quest_id, others, npc)


def route_to_quest_npc(x, y):
    """마을 안 지금 자리에서 경비대장 앞까지의 꺾임점(경매 NPC 앞·동문·서문 안쪽은 소품을 돌아서 — move_test ROUTE_101·auction_test route_to_npc와 같은 통로)."""
    pts = []
    if x < -15000 and 2500 < y < 4500:   # 경매 관리인 앞: 교역소와 상자 더미 사이 통로로 되돌아 나와서
        pts += [(-14750, 3300), (-14750, 5000)]
        y = 5000
    if y > 2500:        # 동문 안쪽
        pts += [(-12000, 5200), (-10500, 1000)]
    elif y < -2500:     # 서문 안쪽
        pts += [(-14700, -1200), (-12500, 0)]
    return pts + [(QUEST_NPC[0], QUEST_NPC[1] - 230.0)]


def to_quest_npc(*clients, others=()):
    """경비대장 이용 거리 안까지 걷는다."""
    at.walk_all([(c, route_to_quest_npc(c.x, c.y)) for c in clients] + [(o, []) for o in others])
    for c in clients:
        ok = c.quest_npc() != 0 and c.npc_dist() <= SHOP_RANGE - 20
        if not ok:
            raise RuntimeError(f"{c.tag}이 경비대장 앞이 아님: 거리 {c.npc_dist():.0f}cm, 보이는 NPC {c.npcs}, 보정 {getattr(c, 'corrections', 0)}회")


def db(q):
    return mk.db_scalar(q)


def db_quest(account, quest_id):
    """'State,Progress,완료 시각 있음(1/0)' — 행이 없으면 빈 글"""
    return db("SELECT CONCAT(CAST(Q.State AS INT), ',', Q.Progress, ',', CASE WHEN Q.CompletedAt IS NULL THEN 0 ELSE 1 END) FROM Player_Quest Q "
              "JOIN Player_Info P ON P.PlayerId = Q.PlayerId JOIN User_Account A ON A.AccountId = P.AccountId "
              f"WHERE A.LoginId = N'{account}' AND Q.QuestId = {quest_id}")


def db_player(account):
    """(Level, Exp, Gold)"""
    out = db(f"SELECT CONCAT(P.Level, ',', P.Exp, ',', P.Gold) FROM Player_Info P JOIN User_Account A ON A.AccountId = P.AccountId WHERE A.LoginId = N'{account}'")
    return tuple(int(v) for v in out.split(","))


def db_item_sum(account, item_id):
    return int(db("SELECT ISNULL(SUM(I.[Count]), 0) FROM Player_Item I JOIN Player_Info P ON P.PlayerId = I.OwnerId JOIN User_Account A ON A.AccountId = P.AccountId "
                  f"WHERE A.LoginId = N'{account}' AND I.Location = 1 AND I.ItemId = {item_id}"))


def after_exp(level, exp, gain):
    """레벨 level·누적 exp에서 gain을 더한 뒤의 (레벨, 경험치) — 서버 Player::AddExp와 같은 계산"""
    e = exp + gain
    while level < gt.MAX_LEVEL and e >= int(gt.PSTAT[level]["ExpToNext"]):
        e -= int(gt.PSTAT[level]["ExpToNext"])
        level += 1
    return (level, 0 if level >= gt.MAX_LEVEL else e)


def reward_of(quest_id):
    q = QUEST[quest_id]
    return int(q["RewardExp"]), int(q["RewardGold"]), int(q["RewardItemId"]), int(q["RewardItemCount"])


def bag_sum(c, item_id):
    return sum(r[1] for r in c.bag(item_id).values())


def equip_sword(c, everyone, item_id=6301):
    """사냥을 빨리 끝내려고 인벤토리의 강철 한손검으로 바꾼다(끝나면 restore_accounts.sql이 되돌린다)."""
    dbid = next(i for i, r in c.inv_rows.items() if r[0] == item_id and r[1] == 0)
    n = len(c.equip_changes)
    c.pump(GAP)
    c.equip(6, dbid)
    wait_all(everyone, lambda: any(e[1] == c.id and e[3] == item_id for e in c.equip_changes[n:]), 8.0, f"{c.tag} 검 장착")
    pump_all(everyone, 2.2)   # 무기 교체 잠금


def hunt_one(killer, others, only=None, limit=150.0, swing_s=1.65):
    """가장 가까운 몬스터를 잡는다(only를 주면 그 몬스터만). B2 kill_monster와 달리, 사냥터 소품에 막혀 다가가지 못하거나
    8초 동안 한 대도 못 맞힌 대상은 20초 동안 건너뛰고 다른 몬스터로 바꾼다(직선으로만 걷는 시험 클라이언트는 나무·바위 뒤의 몬스터에 닿지 못한다).
    부채꼴이 옆 몬스터도 맞히므로 한 번에 둘이 죽을 수 있다 — 내 타격으로 HP가 0이 된 몬스터 id 목록을 돌려준다."""
    everyone = [killer, *others]
    end = time.time() + limit
    skip = {}                      # 건너뛰는 대상 id -> 다시 볼 시각
    n_dmg0 = len(killer.damages)
    target, t_target = None, 0.0

    def my_hits(tid):
        return [d for d in killer.damages[n_dmg0:] if d[1] == killer.id and d[2] == tid]

    while time.time() < end:
        now = time.time()
        mons = {i: o for i, o in killer.monsters().items() if i not in killer.dead_ids and skip.get(i, 0) < now and (only is None or i == only)}
        if not mons:
            if only is not None and only in killer.dead_ids:
                return [only]
            pump_all(everyone, 0.5)
            continue
        if target not in mons:
            target = min(mons, key=lambda i: math.hypot(mons[i][1] - killer.x, mons[i][2] - killer.y))
            t_target = now
        mx, my, mz = killer.where(target)
        if math.hypot(mx - killer.x, my - killer.y) > 170:
            killer.walk_to(mx, my, stop_cm=110, tz=mz, limit_s=5)
            if killer.dead or killer.closed:
                raise RuntimeError(f"{killer.tag}이 죽거나 끊김")
            if target in killer.objects:
                mx, my, mz = killer.where(target)
            if only is None and math.hypot(mx - killer.x, my - killer.y) > 220 and time.time() - t_target > 8.0:
                skip[target] = time.time() + 20.0   # 다가가지 못함(막힘·보정) → 다른 몬스터로
                target = None
            continue
        killer.attack(target, mx, my, slot=0)
        pump_all(everyone, swing_s)   # 동작 시간만큼 기다린다(앞 동작 중 요청은 ACTING으로 거부됨)
        if killer.dead:
            raise RuntimeError(f"{killer.tag}이 죽음")
        killed = sorted({d[2] for d in killer.damages[n_dmg0:] if d[1] == killer.id and d[4] == 0})
        if killed or target in killer.dead_ids:
            pump_all(everyone, 0.4)
            return killed or [target]
        hits = my_hits(target)
        if hits:
            t_target = max(t_target, gt.T0 + hits[-1][0])
        elif only is None and time.time() - t_target > 8.0:
            skip[target] = time.time() + 20.0       # 닿는 거리인데 맞지 않음 → 다른 몬스터로
            target = None
    raise TimeoutError(f"{killer.tag}이 몬스터를 잡지 못함: target={target} 내 위치 ({killer.x:.0f},{killer.y:.0f}) 내 타격 {[d[3] for d in killer.damages[n_dmg0:] if d[1] == killer.id]} "
                       f"거부 {[r[1] for r in killer.attack_results[-5:]]} 보정 {getattr(killer, 'corrections', 0)}회")


def provoke(c, others, limit=90.0):
    """몬스터 하나를 한 대 쳐서 쫓아오게 한다(맞기 시작하면 끝 — 그 뒤로는 치지 않는다). 닿지 못하는 대상은 건너뛴다."""
    everyone = [c, *others]
    end = time.time() + limit
    skip = {}
    n0 = len(c.damages)
    hurt = lambda: any(d[2] == c.id for d in c.damages[n0:])
    while time.time() < end and not hurt() and not c.dead:
        now = time.time()
        mons = {i: o for i, o in c.monsters().items() if i not in c.dead_ids and skip.get(i, 0) < now}
        if not mons:
            pump_all(everyone, 0.5)
            continue
        target = min(mons, key=lambda i: math.hypot(mons[i][1] - c.x, mons[i][2] - c.y))
        mx, my, mz = c.where(target)
        c.walk_to(mx, my, stop_cm=110, tz=mz, limit_s=5)
        if target in c.objects:
            mx, my, mz = c.where(target)
        if math.hypot(mx - c.x, my - c.y) > 220:
            skip[target] = time.time() + 20.0
            continue
        c.attack(target, mx, my, slot=0)
        pump_all(everyone, 1.65)
        if any(d[1] == c.id and d[2] == target for d in c.damages[n0:]):
            wait_end = time.time() + 10.0   # 맞혔다 — 쫓아와 때릴 때까지 기다린다
            while time.time() < wait_end and not hurt() and not c.dead:
                pump_all(everyone, 0.3)
        else:
            skip[target] = time.time() + 20.0
    if not hurt() and not c.dead:
        raise TimeoutError(f"{c.tag}이 몬스터를 건드리지 못함: 내 위치 ({c.x:.0f},{c.y:.0f}) 보정 {getattr(c, 'corrections', 0)}회")


def expect_totals(name, start):
    """DB 합계가 '개발 지급 + 퀘스트 보상 − 회수'만큼만 변했는가"""
    time.sleep(1.0)
    items1, gold1 = at.totals()
    want_items = start[0] + at.GIVEN["items"] + EXPECT["items"]
    want_gold = start[1] + at.GIVEN["gold"] + EXPECT["gold"]
    check(name, items1 == want_items and gold1 == want_gold,
          f"아이템 {start[0]} → {items1} (기대 {want_items}: 지급 {at.GIVEN['items']} 보상·회수 {EXPECT['items']}), 골드 {start[1]} → {gold1} (기대 {want_gold}: 지급 {at.GIVEN['gold']} 보상 {EXPECT['gold']})")
    check(name + "-auction-data", at.db_broken() == 0, f"경매 데이터가 어긋난 행 {at.db_broken()}개")


def begin():
    at.GIVEN.update(items=0, gold=0)
    EXPECT.update(items=0, gold=0)
    return at.totals()


def close_all(cs):
    for c in cs:
        c.close(rst=False)
    time.sleep(3.0)   # 접속 정리(마지막 저장)가 끝나야 같은 계정으로 바로 다시 로그인된다


# =====================================================================================================
def t_main():
    start = begin()
    a, b = cs = [Q("Test1").enter(), Q("Test2").enter()]
    pump_all(cs, 1.0)

    # ---- 1. 처음 입장: SC_INVENTORY 다음에 SC_QUEST_LIST(비어 있어도 한 번), 수령함이 비어 있으면 알림 없음 ----
    check("list-on-enter", len(a.quest_lists) == 1 and a.quest_lists[0][1] == {} and len(b.quest_lists) == 1, f"Test1 {a.quest_lists} Test2 {b.quest_lists}")
    check("list-after-inventory", mk.SC_INVENTORY in a.order and SC_QUEST_LIST in a.order and a.order.index(mk.SC_INVENTORY) < a.order.index(SC_QUEST_LIST),
          f"순서 {[p for p in a.order if p in (mk.SC_INVENTORY, SC_QUEST_LIST)]}")
    check("no-mail-notice", not a.mail_notices and not b.mail_notices, f"{a.mail_notices} {b.mail_notices}")

    # ---- 2. 먼 곳(시작 지점)에서 수락 → TOO_FAR. NPC가 시야 밖이면 보일 때까지만 다가간다 ----
    if a.quest_npc() == 0:
        a.walk_to(-10600.0, 100.0, stop_cm=60)
        pump_all(cs, 0.5)
    npc = a.npcs.get(a.quest_npc())
    check("npc-kind", npc is not None and npc[0] == QUEST_NPC_TEMPLATE and npc[1] == NPC_KIND_QUEST and math.hypot(npc[2] - QUEST_NPC[0], npc[3] - QUEST_NPC[1]) < 50,
          f"퀘스트 NPC {npc} (보이는 NPC {a.npcs})")
    log(f"Test1 시작 자리 ({a.x:.0f},{a.y:.0f}) 경비대장까지 {a.npc_dist():.0f}cm, NPC id {a.quest_npc()}")
    check("accept-too-far", a.npc_dist() > SHOP_RANGE and accept(a, 1, cs) == R_TOO_FAR, f"거리 {a.npc_dist():.0f}")
    check("too-far-no-row", db_quest("Test1", 1) == "", f"DB {db_quest('Test1', 1)!r}")

    # ---- 3. NPC 앞에서 수락 ----
    to_quest_npc(a, b)
    log(f"경비대장 앞: Test1 {a.npc_dist():.0f}cm Test2 {b.npc_dist():.0f}cm")
    check("accept", accept(a, 1, cs) == ("ok", Q_ACTIVE, 0), "수락 응답이 다름")
    check("accept-db", db_quest("Test1", 1) == "1,0,0", f"DB {db_quest('Test1', 1)!r}")
    check("request-again", accept(a, 1, cs) == R_NOT_FINISHED, "진행 중 다시 요청(= 완료 시도)이 NOT_FINISHED가 아님")
    check("complete-not-finished", complete(a, 1, cs) == R_NOT_FINISHED, "처치 0에서 완료가 NOT_FINISHED가 아님")
    # 요청 하나(D-264): 받지 않은 퀘스트에는 "완료"를 뜻하고 보내도 수락이 된다. Test1은 던전 퀘스트(대상 1005)를 이렇게 받아 둔다 — 1001을 잡아도 오르지 않아야 한다
    check("request-unaccepted-accepts", complete(a, 3, cs) == ("ok", Q_ACTIVE, 0) and db_quest("Test1", 3) == "1,0,0", f"Test1 {a.quests}")
    # 권장 레벨은 막지 않는다: 5레벨 Test2가 권장 10·20 퀘스트를 받는다
    check("accept-any-level", accept(b, 2, cs) == ("ok", Q_ACTIVE, 0) and accept(b, 1, cs) == ("ok", Q_ACTIVE, 0),
          f"Test2 레벨 {b.level} 퀘스트 {b.quests}, Test1 {a.quests}")

    # ---- 4. 처치 15번: 매번 SC_QUEST_UPDATE(+1), 바뀔 때마다 DB 저장. 16번째는 그대로 ----
    equip_sword(a, cs)
    pt.to_hunt(a, [b])
    count = int(QUEST[1]["Count"])
    nu_a, nu_b = len(a.quest_updates), len(b.quest_updates)
    t_hunt = time.time()
    db_seen = {}
    while a.quests[1][1] < count:
        hunt_one(a, [b])
        pump_all(cs, 0.4)
        p = a.quests[1][1]
        if p in (1, 7, count) and p not in db_seen:
            db_seen[p] = db_quest("Test1", 1)
    ups = [u for u in a.quest_updates[nu_a:] if u[1] == 1]
    log(f"처치 {count}번 {time.time() - t_hunt:.0f}초, SC_QUEST_UPDATE {[(u[2], u[3]) for u in ups]}, DB 확인 {db_seen}")
    check("kill-updates", [(u[2], u[3]) for u in ups] == [(Q_ACTIVE, k) for k in range(1, count + 1)], f"{[(u[2], u[3]) for u in ups]}")
    check("kill-saved-each", all(v == f"1,{k},0" for k, v in db_seen.items()) and count in db_seen, f"{db_seen}")
    check("kill-other-quest-same", a.quests[3] == (Q_ACTIVE, 0) and all(u[1] == 1 for u in a.quest_updates[nu_a:]), f"Test1 {a.quests}")
    check("kill-other-player-same", len(b.quest_updates) == nu_b and b.quests[1] == (Q_ACTIVE, 0), f"Test2(마을, 파티 아님) {b.quest_updates[nu_b:]}")
    n = len(a.quest_updates)
    hunt_one(a, [b])
    pump_all(cs, 1.0)
    check("kill-16th-same", len(a.quest_updates) == n and a.quests[1] == (Q_ACTIVE, count) and db_quest("Test1", 1) == f"1,{count},0",
          f"16번째 뒤 {a.quest_updates[n:]} DB {db_quest('Test1', 1)!r}")

    # ---- 5. 완료: SC_INVENTORY_UPDATE(물약 5 새 행 + 골드) → SC_QUEST_UPDATE(COMPLETED) → SC_GAIN_EXP, DB 한 트랜잭션 ----
    at.to_town(a, cs)
    to_quest_npc(a)
    exp_r, gold_r, item_r, cnt_r = reward_of(1)
    gold0, have0, n_order, n_exp = a.gold, bag_sum(a, item_r), len(a.order), len(a.exp_msgs)
    check("complete", complete(a, 1, cs) == ("ok", Q_DONE, count), "완료 응답이 다름")
    pump_all(cs, 0.6)
    EXPECT["items"] += cnt_r; EXPECT["gold"] += gold_r
    seq = [p for p in a.order[n_order:] if p in (mk.SC_INVENTORY_UPDATE, SC_QUEST_UPDATE, gt.SC_GAIN_EXP)]
    check("complete-order", seq == [mk.SC_INVENTORY_UPDATE, SC_QUEST_UPDATE, gt.SC_GAIN_EXP], f"순서 {seq}")
    check("complete-reward", a.gold == gold0 + gold_r and bag_sum(a, item_r) == have0 + cnt_r, f"골드 {gold0} → {a.gold}, 물약 {have0} → {bag_sum(a, item_r)}")
    check("complete-exp-cap", a.exp_msgs[n_exp:] and a.exp_msgs[-1][1:] == ((0, 0) if a.level >= gt.MAX_LEVEL else (a.exp, exp_r)), f"{a.exp_msgs[n_exp:]}")
    check("complete-db", db_quest("Test1", 1) == f"2,{count},1" and db_player("Test1")[2] == gold0 + gold_r and db_item_sum("Test1", item_r) == have0 + cnt_r,
          f"퀘스트 {db_quest('Test1', 1)!r} 플레이어 {db_player('Test1')} 물약 {db_item_sum('Test1', item_r)}")
    check("accept-done", accept(a, 1, cs) == R_ALREADY_DONE and complete(a, 1, cs) == R_ALREADY_DONE, "완료한 퀘스트의 수락·완료가 ALREADY_DONE이 아님")

    # ---- 6. 수집(Test2, 5레벨): 9개는 NOT_ENOUGH_ITEM, 10개면 회수 + 보상(빈 칸 재사용) + 경험치(레벨업·바로 저장) ----
    exp_r, gold_r, item_r, cnt_r = reward_of(2)
    need = int(QUEST[2]["Count"])
    at.give(b, ITEM_HORN, need - 1, cs)
    check("collect-9", complete(b, 2, cs) == R_NOT_ENOUGH_ITEM and bag_sum(b, ITEM_HORN) == need - 1 and db_quest("Test2", 2) == "1,0,0", f"뿔 {bag_sum(b, ITEM_HORN)}")
    at.give(b, ITEM_HORN, 1, cs)
    horn_row = next(iter(b.bag(ITEM_HORN)))
    horn_slot = b.items[horn_row][3]
    lv0, exp0, gold0, n_lu, n_exp, rows0 = b.level, b.exp, b.gold, len(b.levelups), len(b.exp_msgs), set(b.items)
    check("collect-10", complete(b, 2, cs) == ("ok", Q_DONE, 0), "완료 응답이 다름")
    pump_all(cs, 0.8)
    EXPECT["items"] += cnt_r - need; EXPECT["gold"] += gold_r
    new_rows = sorted(set(b.items) - rows0)
    check("collect-taken", horn_row not in b.items and bag_sum(b, ITEM_HORN) == 0 and db_item_sum("Test2", ITEM_HORN) == 0, f"뿔 {b.bag(ITEM_HORN)}")
    check("collect-reward", len(new_rows) == 1 and b.items[new_rows[0]][:2] == [item_r, cnt_r] and b.items[new_rows[0]][3] == horn_slot and b.gold == gold0 + gold_r,
          f"새 행 {[b.items[r] for r in new_rows]} (뿔이 있던 칸 {horn_slot}), 골드 {gold0} → {b.gold}")
    want_lv, want_exp = after_exp(lv0, exp0, exp_r)
    check("collect-exp", b.exp_msgs[n_exp:] and b.exp_msgs[-1][1:] == (want_exp, exp_r) and b.level == want_lv and (want_lv == lv0 or len(b.levelups) == n_lu + 1),
          f"레벨 {lv0} → {b.level}(기대 {want_lv}) 경험치 {b.exp_msgs[n_exp:]} (기대 {want_exp})")
    time.sleep(0.7)
    check("collect-exp-saved-now", db_player("Test2") == (want_lv, want_exp, gold0 + gold_r), f"DB {db_player('Test2')} 기대 {(want_lv, want_exp, gold0 + gold_r)} — 주기 저장을 기다리지 않고 바로 저장돼야 함")
    check("collect-db", db_quest("Test2", 2) == "2,0,1", f"{db_quest('Test2', 2)!r}")

    # ---- 7. 여러 스택(Test3): [99, 5] + 가득 찬 인벤토리 → 적은 스택(5)을 통째로, 큰 스택에서 5 → 회수로 빈 칸에 보상이 들어감 ----
    c, d = Q("Test3").enter(), Q("Test4").enter()
    cs = [a, b, c, d]
    pump_all(cs, 0.5)
    to_quest_npc(c, d, others=[a, b])
    check("multi-accept", accept(d, 2, cs) == ("ok", Q_ACTIVE, 0), "수락")
    stack = int(gt.ITEMS[ITEM_HORN]["MaxStack"])
    at.give(c, ITEM_HORN, stack + 5, cs)
    at.give(c, ITEM_JUNK, c.free_slots(), cs)
    # 요청 하나(D-264): 재료를 이미 다 가진 채 받기 전 퀘스트를 요청하면 첫 요청은 수락만(회수 없음), 다음 요청이 완료
    horns0 = bag_sum(c, ITEM_HORN)
    check("prehold-first-is-accept", accept(c, 2, cs) == ("ok", Q_ACTIVE, 0) and bag_sum(c, ITEM_HORN) == horns0 and db_item_sum("Test3", ITEM_HORN) == horns0,
          f"뿔 {bag_sum(c, ITEM_HORN)} (전 {horns0})")
    small = next(r for r, v in c.bag(ITEM_HORN).items() if v[1] == 5)
    big = next(r for r, v in c.bag(ITEM_HORN).items() if v[1] == stack)
    small_slot, rows0 = c.items[small][3], set(c.items)
    check("multi-full", c.free_slots() == 0, f"빈 칸 {c.free_slots()}")
    check("multi-complete", complete(c, 2, cs) == ("ok", Q_DONE, 0), "가득 찬 인벤토리에서 완료(회수로 빈 칸에 보상)")
    pump_all(cs, 0.6)
    EXPECT["items"] += cnt_r - need; EXPECT["gold"] += gold_r
    new_rows = sorted(set(c.items) - rows0)
    check("multi-small-first", small not in c.items and c.items[big][1] == stack - 5 and db_item_sum("Test3", ITEM_HORN) == stack - 5, f"뿔 {c.bag(ITEM_HORN)}")
    check("multi-reward-slot", len(new_rows) == 1 and c.items[new_rows[0]][0] == item_r and c.items[new_rows[0]][3] == small_slot and c.free_slots() == 0,
          f"새 행 {[c.items[r] for r in new_rows]} (작은 스택이 있던 칸 {small_slot})")

    # ---- 8. 인벤토리 가득(Test4): 스택 20(회수해도 칸이 안 빔) + 빈 칸 0 → INVENTORY_FULL, 한 칸 비우면 통과 직전까지. 골드 상한 → GOLD_FULL ----
    at.give(d, ITEM_HORN, 20, cs)
    at.give(d, ITEM_JUNK, d.free_slots(), cs)
    check("inv-full", d.free_slots() == 0 and complete(d, 2, cs) == R_INVENTORY_FULL, f"빈 칸 {d.free_slots()}")
    check("inv-full-same", bag_sum(d, ITEM_HORN) == 20 and db_item_sum("Test4", ITEM_HORN) == 20 and db_quest("Test4", 2) == "1,0,0", f"뿔 {bag_sum(d, ITEM_HORN)} DB {db_quest('Test4', 2)!r}")
    junk = next(iter(d.bag(ITEM_JUNK)))
    n = len(d.inv_updates)
    d.pump(GAP)
    d.send(CS_DROP_ITEM, rs.f_int(1, junk))
    wait_all(cs, lambda: len(d.inv_updates) > n, 6.0, "Test4 버리기")
    EXPECT["items"] -= 1
    gold_max = int(ECON["GoldMax"])
    at.give(d, 0, gold_max - 500 - d.gold, cs)
    check("gold-full", d.gold == gold_max - 500 and complete(d, 2, cs) == R_GOLD_FULL, f"골드 {d.gold}")
    check("gold-full-same", bag_sum(d, ITEM_HORN) == 20 and db_quest("Test4", 2) == "1,0,0" and db_player("Test4")[2] == gold_max - 500, f"DB {db_quest('Test4', 2)!r} {db_player('Test4')}")

    # ---- 9. 재접속: 목록·레벨·경험치·골드가 DB 값 그대로 ----
    keep = {x.tag: (dict(x.quests), x.level, x.exp, x.gold) for x in cs}
    close_all(cs)
    again = [Q(f"Test{i}").enter() for i in range(1, 5)]
    pump_all(again, 1.0)
    got = {x.tag: (dict(x.quests), x.level, x.exp, x.gold) for x in again}
    log(f"재접속 뒤 퀘스트 {[(x.tag, x.quests) for x in again]}")
    check("relogin-same", got == keep, f"받은 {got} 기대 {keep}")
    check("relogin-list", again[0].quests == {1: (Q_DONE, count), 3: (Q_ACTIVE, 0)} and again[1].quests == {1: (Q_ACTIVE, 0), 2: (Q_DONE, 0)}
          and again[2].quests == {2: (Q_DONE, 0)} and again[3].quests == {2: (Q_ACTIVE, 0)}, f"{[x.quests for x in again]}")
    close_all(again)
    expect_totals("db-totals", start)


# =====================================================================================================
def t_cut():
    # DbSaveDelayMs 1500: DB 작업이 가 있는 동안 접속을 끊는다
    start = begin()
    a, b, c = cs = [Q("Test1").enter(), Q("Test2").enter(), Q("Test3").enter()]
    pump_all(cs, 0.5)
    to_quest_npc(a, b, c)
    check("accept", accept(a, 1, cs) == ("ok", Q_ACTIVE, 0) and accept(b, 2, cs) == ("ok", Q_ACTIVE, 0), "수락")

    # ---- 수락 요청 직후 끊김(Test3): DB에는 들어가고 다음 로그인 때 진행 중으로 읽힌다 ----
    c.pump(GAP)
    c.send_quest(CS_QUEST_ACCEPT, 1)
    time.sleep(0.2)
    c.close(rst=True)
    c2, took = at_relogin("Test3")
    check("cut-accept", c2.quests == {1: (Q_ACTIVE, 0)} and db_quest("Test3", 1) == "1,0,0" and took < 10.0, f"다시 로그인 {took:.1f}초, 퀘스트 {c2.quests} DB {db_quest('Test3', 1)!r}")
    cs = [a, b, c2]

    # ---- 완료 요청 직후 끊김(Test2): 완료·회수·보상 아이템·골드·보상 경험치가 모두 남는다 ----
    exp_r, gold_r, item_r, cnt_r = reward_of(2)
    need = int(QUEST[2]["Count"])
    at.give(b, ITEM_HORN, need, cs)
    lv0, exp0, gold0 = b.level, b.exp, b.gold
    b.pump(GAP)
    b.send_quest(CS_QUEST_COMPLETE, 2)
    time.sleep(0.2)
    b.close(rst=True)
    EXPECT["items"] += cnt_r - need; EXPECT["gold"] += gold_r
    b2, took = at_relogin("Test2")
    cs = [a, b2, c2]
    want = after_exp(lv0, exp0, exp_r)
    log(f"완료 직후 끊김: 다시 로그인 {took:.1f}초, 퀘스트 {b2.quests}, 레벨 {lv0} → {b2.level} 경험치 {exp0} → {b2.exp} (기대 {want}), 골드 {gold0} → {b2.gold}")
    check("cut-complete-quest", b2.quests == {2: (Q_DONE, 0)} and db_quest("Test2", 2) == "2,0,1", f"{b2.quests} DB {db_quest('Test2', 2)!r}")
    check("cut-complete-items", bag_sum(b2, ITEM_HORN) == 0 and bag_sum(b2, item_r) == cnt_r and b2.gold == gold0 + gold_r, f"뿔 {bag_sum(b2, ITEM_HORN)} 보상 {bag_sum(b2, item_r)} 골드 {b2.gold}")
    check("cut-complete-exp", (b2.level, b2.exp) == want and db_player("Test2")[:2] == want, f"레벨·경험치 {(b2.level, b2.exp)} DB {db_player('Test2')} 기대 {want} — 완료는 남았는데 보상 경험치가 사라지면 안 됨")

    # ---- 처치 직후 끊김(Test1): 저장이 DB에 가 있는 동안 끊어도 처치 수가 남는다 ----
    equip_sword(a, cs)
    pt.to_hunt(a, [b2, c2])
    hunt_one(a, [b2, c2])
    got = a.quests[1][1]
    a.close(rst=True)
    a2, took = at_relogin("Test1")
    check("cut-kill", got >= 1 and a2.quests.get(1) == (Q_ACTIVE, got) and db_quest("Test1", 1) == f"1,{got},0" and took < 10.0,
          f"처치 {got}, 다시 로그인 {took:.1f}초, 퀘스트 {a2.quests} DB {db_quest('Test1', 1)!r}")
    close_all([a2, b2, c2])
    expect_totals("db-totals", start)


def at_relogin(account, limit=15.0):
    t0 = time.time()
    while True:
        try:
            c = Q(account)
            c.enter()
            c.pump(0.5)
            return c, time.time() - t0
        except Exception:
            if time.time() - t0 > limit:
                raise
            time.sleep(0.3)


# =====================================================================================================
def t_dungeon():
    # 테이블 사본: 우두머리 1005 = HP 400·공격력 700·비선공·걸어서 복귀, 던전의 다른 스폰은 비선공 1001 한 마리씩, 클리어 뒤 8초에 내보냄
    start = begin()
    a, b, c = cs = [Q("Test1").enter(), Q("Test2").enter(), Q("Test3").enter()]
    pump_all(cs, 0.5)
    to_quest_npc(a, b, c)
    check("accept-3", all(accept(x, 3, cs) == ("ok", Q_ACTIVE, 0) for x in cs), f"{[x.quests for x in cs]}")
    equip_sword(a, cs)
    pt.join(a, b, cs)
    pt.join(a, c, cs)
    for x in cs:
        dt.to_field(x, cs)
    dt.cooldown()
    nb, nc = len(b.room_changes), len(c.room_changes)
    dt.enter_dungeon(a, cs)
    wait_all(cs, lambda: len(b.room_changes) > nb and b.spawned and len(c.room_changes) > nc and c.spawned, 12.0, "파티원 동반 입장")
    pump_all(cs, 0.5)
    check("in-dungeon", all(x.kind == dt.KIND_DUNGEON for x in cs), f"{[x.kind for x in cs]}")

    # a·b는 우두머리 방으로, c는 시작 방에 남는다(우두머리에서 70m 넘게 떨어짐 = 시야 50m 밖)
    bx, by = dg4.BOSS
    at.walk_all([(a, dg4.ROUTE[1:-1] + [(bx, by + 900.0)]), (b, dg4.ROUTE[1:-1] + [(bx, by + 700.0)]), (c, [])], limit_s=180.0)
    pump_all(cs, 0.5)
    boss = [i for i in a.monsters() if i not in a.dead_ids and math.hypot(a.objects[i][1] - bx, a.objects[i][2] - by) < 800]
    check("boss-seen", len(boss) == 1 and c.id not in a.objects, f"우두머리 후보 {boss}, a가 보는 플레이어 {list(a.players())}")
    boss = boss[0]

    # b가 우두머리를 한 대 치고 맞아 죽는다(우두머리는 맞기 전에는 공격하지 않는다)
    n_dmg = len(b.damages)
    for _ in range(10):   # 우두머리가 배회해 첫 휘두르기가 빗나갈 수 있다 — 한 대가 들어갈 때까지만 친다(더 치면 5레벨이 잡아 버릴 수 있다)
        mx, my, mz = b.where(boss)
        if math.hypot(mx - b.x, my - b.y) > 170:
            b.walk_to(mx, my, stop_cm=120, limit_s=20)
            mx, my, mz = b.where(boss)
        b.attack(boss, mx, my, slot=0)
        pump_all(cs, 1.65)
        if any(d[1] == b.id and d[2] == boss for d in b.damages[n_dmg:]) or b.dead:
            break
    check("member-hit-boss", any(d[1] == b.id and d[2] == boss for d in b.damages[n_dmg:]), f"Test2가 우두머리를 맞히지 못함 (거부 {[r[1] for r in b.attack_results[-5:]]})")
    wait_all(cs, lambda: b.dead, 40.0, f"Test2 사망 (준 피해 {[d[3] for d in b.damages[n_dmg:] if d[1] == b.id]}, 받은 피해 {[d[3] for d in b.damages[n_dmg:] if d[2] == b.id]})")
    check("member-dead", b.dead and boss not in a.dead_ids, "파티원이 죽기 전에 우두머리가 죽음")

    # a가 잡는다 → 셋 다 처치 인정(죽은 b·시야 밖 c 포함), 경험치는 막타만
    na, nb_, nc_ = len(a.quest_updates), len(b.quest_updates), len(c.quest_updates)
    ea, eb, ec = len(a.exp_msgs), len(b.exp_msgs), len(c.exp_msgs)
    killed = hunt_one(a, [b, c], only=boss)
    wait_all(cs, lambda: len(a.quest_updates) > na and len(b.quest_updates) > nb_ and len(c.quest_updates) > nc_, 5.0,
             f"처치 인정 SC_QUEST_UPDATE (a {a.quest_updates[na:]} b {b.quest_updates[nb_:]} c {c.quest_updates[nc_:]})")
    pump_all(cs, 0.6)
    dist_c = math.hypot(c.x - bx, c.y - by)
    log(f"우두머리 {killed} 처치: a {a.quest_updates[na:]} b(죽음 {b.dead}) {b.quest_updates[nb_:]} c(우두머리에서 {dist_c:.0f}cm) {c.quest_updates[nc_:]}")
    check("credit-killer", [u[1:] for u in a.quest_updates[na:]] == [(3, Q_ACTIVE, 1)], f"{a.quest_updates[na:]}")
    check("credit-dead-member", b.dead and [u[1:] for u in b.quest_updates[nb_:]] == [(3, Q_ACTIVE, 1)], f"죽음 {b.dead} {b.quest_updates[nb_:]}")
    check("credit-out-of-view-member", dist_c > 5500 and [u[1:] for u in c.quest_updates[nc_:]] == [(3, Q_ACTIVE, 1)], f"거리 {dist_c:.0f} {c.quest_updates[nc_:]}")
    check("exp-only-killer", len(a.exp_msgs) == ea + 1 and len(b.exp_msgs) == eb and len(c.exp_msgs) == ec, f"a {a.exp_msgs[ea:]} b {b.exp_msgs[eb:]} c {c.exp_msgs[ec:]}")
    time.sleep(0.6)
    check("credit-saved", [db_quest(f"Test{i}", 3) for i in (1, 2, 3)] == ["1,1,0"] * 3, f"{[db_quest(f'Test{i}', 3) for i in (1, 2, 3)]}")

    # 클리어 → 내보내기(마을). 죽은 b는 마을에서 부활한다
    wait_all(cs, lambda: all(x.kind == mk.KIND_TOWN and x.spawned for x in cs), 30.0, f"클리어 뒤 마을로 ({[x.kind for x in cs]})")
    pump_all(cs, 0.5)
    if b.dead:
        pump_all(cs, b.revive_delay / 1000.0 + 0.5)
        r = b.revives
        b.send(mk.CS_REVIVE)
        wait_all(cs, lambda: b.revives > r and not b.dead, 12.0, "Test2 부활")
        pump_all(cs, 1.0)
    to_quest_npc(a, b, others=[c])
    exp_r, gold_r, item_r, cnt_r = reward_of(3)
    lv0, exp0, gold0 = b.level, b.exp, b.gold
    check("complete-member", complete(b, 3, cs) == ("ok", Q_DONE, 1), "죽어 있던 파티원의 완료")
    pump_all(cs, 0.8)
    want = after_exp(lv0, exp0, exp_r)
    check("complete-member-reward", (b.level, b.exp) == want and b.gold == gold0 + gold_r and bag_sum(b, item_r) == cnt_r,
          f"레벨·경험치 {(b.level, b.exp)} 기대 {want}, 골드 {gold0} → {b.gold}, 보상 {bag_sum(b, item_r)}")
    check("complete-killer", complete(a, 3, cs) == ("ok", Q_DONE, 1), "막타의 완료")
    EXPECT["items"] += cnt_r * 2; EXPECT["gold"] += gold_r * 2
    check("complete-far", complete(c, 3, cs) == R_TOO_FAR, "먼 곳의 완료가 TOO_FAR가 아님")
    close_all(cs)
    expect_totals("db-totals", start)


# =====================================================================================================
def t_mail():
    # EconomyTable 사본: AuctionDurationSec 8, AuctionExpireCheckSec 1
    start = begin()
    a, b, c = cs = [Q("Test1").enter(), Q("Test2").enter(), Q("Test3").enter()]
    pump_all(cs, 0.5)
    check("no-notice-empty-mailbox", not any(x.mail_notices for x in cs), f"{[x.mail_notices for x in cs]}")
    at.to_npc(a, others=[b, c])

    # ---- 팔림: 판매자에게만 SALE_GOLD(물건·대금·등록 id) ----
    head = at.give_one(a, ITEM_HEAD, cs)
    res, lid = at.register(a, head, 1, 300, cs)
    check("register", res == at.A_SUCCESS and lid > 0, f"등록 {res}")
    check("buy", at.buy(b, lid, cs) == at.A_SUCCESS, "구매")
    wait_all(cs, lambda: a.mail_notices, 5.0, "판매자 SC_MAIL_NOTICE")
    pump_all(cs, 0.5)
    check("sale-notice", [n[1:] for n in a.mail_notices] == [(MAIL_SALE, ITEM_HEAD, 1, 300, lid)], f"{a.mail_notices}")
    check("sale-notice-seller-only", not b.mail_notices and not c.mail_notices, f"구매자 {b.mail_notices} 다른 사람 {c.mail_notices}")
    view = at.my(a, cs)
    check("sale-mail-exists", at.find_mail(view, lid, at.MAIL_SALE) is not None, f"수령함 {view['mails']}")

    # ---- 만료: 판매자에게 EXPIRED(물건·수량) ----
    potion = at.give(a, ITEM_POTION, 3, cs)
    check("potion-row", len(potion) == 1, f"물약 새 행 {potion}")
    res, lid2 = at.register(a, potion[0], 3, 200, cs)
    check("register-2", res == at.A_SUCCESS, f"등록 {res}")
    n = len(a.mail_notices)
    wait_all(cs, lambda: len(a.mail_notices) > n, 20.0, "만료 SC_MAIL_NOTICE")
    pump_all(cs, 0.5)
    check("expire-notice", [x[1:] for x in a.mail_notices[n:]] == [(MAIL_EXPIRED, ITEM_POTION, 3, 0, lid2)], f"{a.mail_notices[n:]}")

    # ---- 접속하지 않은 동안 팔림: 다음 입장 때 NONE(SC_QUEST_LIST 다음), 수령함이 빈 계정은 없음 ----
    head2 = at.give_one(a, ITEM_HEAD, cs)
    res, lid3 = at.register(a, head2, 1, 250, cs)
    check("register-3", res == at.A_SUCCESS, f"등록 {res}")
    a.close(rst=False)
    time.sleep(3.0)
    check("buy-offline", at.buy(b, lid3, [b, c]) == at.A_SUCCESS, "판매자가 없는 동안 구매")
    pump_all([b, c], 0.5)
    a2 = Q("Test1").enter()
    pump_all([a2, b, c], 1.0)
    check("login-notice", [x[1:] for x in a2.mail_notices] == [(MAIL_NONE, 0, 0, 0, 0)], f"{a2.mail_notices}")
    check("login-notice-order", SC_QUEST_LIST in a2.order and SC_MAIL_NOTICE in a2.order and a2.order.index(SC_QUEST_LIST) < a2.order.index(SC_MAIL_NOTICE), "SC_MAIL_NOTICE가 SC_QUEST_LIST보다 먼저 옴")
    c.close(rst=False)
    time.sleep(3.0)
    c2 = Q("Test3").enter()
    pump_all([a2, b, c2], 1.0)
    check("login-no-notice", not c2.mail_notices, f"수령함이 빈 Test3 {c2.mail_notices}")
    # Room을 옮길 때는 다시 보내지 않는다
    n, nl = len(a2.mail_notices), len(a2.quest_lists)
    at.to_field(a2, [b, c2])
    pump_all([a2, b, c2], 1.0)
    check("no-resend-on-room-move", len(a2.mail_notices) == n and len(a2.quest_lists) == nl, f"알림 {a2.mail_notices[n:]} 목록 {a2.quest_lists[nl:]}")
    close_all([a2, b, c2])
    expect_totals("db-totals", start)


# =====================================================================================================
def t_err():
    # DevCommands=false, DbSaveDelayMs=400
    start = begin()
    a, b = cs = [Q("Test1").enter(), Q("Test2").enter()]
    pump_all(cs, 0.8)
    other_npc = next((nid for nid, v in a.npcs.items() if v[1] != NPC_KIND_QUEST), 900000000)   # 상점·경매 NPC(안 보이면 첫 마커의 id)
    if a.quest_npc() == 0:
        a.walk_to(-10600.0, 100.0, stop_cm=60)
        pump_all(cs, 0.5)
    check("too-far", accept(a, 1, cs) == R_TOO_FAR and complete(a, 1, cs) == R_TOO_FAR, f"거리 {a.npc_dist():.0f}")
    check("npc-missing", accept(a, 1, cs, npc=12345) == R_NO_QUEST and complete(a, 1, cs, npc=0) == R_NO_QUEST, "없는 NPC id가 NO_QUEST가 아님")
    check("npc-not-quest", accept(a, 1, cs, npc=other_npc) == R_NO_QUEST, f"상점·경매 NPC({other_npc})가 NO_QUEST가 아님")
    to_quest_npc(a, b)
    check("quest-missing", accept(a, 999, cs) == R_NO_QUEST and accept(a, 0, cs) == R_NO_QUEST and accept(a, -1, cs) == R_NO_QUEST and complete(a, 999, cs) == R_NO_QUEST,
          "없는 퀘스트 id가 NO_QUEST가 아님")

    # ---- 요청 연타: 한 번만 수락, 나머지는 TOO_FREQUENT(간격)·BUSY(DB에 가 있는 동안)·NOT_FINISHED(수락이 끝난 뒤 = 완료 시도) ----
    pump_all(cs, 0.3)
    nu, nr = len(a.quest_updates), len(a.quest_results)
    for i in range(8):
        a.send_quest(CS_QUEST_ACCEPT, 1)
        if i >= 2:   # 앞의 셋은 붙여서(요청 간격 100ms 안), 나머지는 간격보다 길게(DB에 가 있는 400ms 동안은 BUSY)
            pump_all(cs, 0.13)
    pump_all(cs, 1.5)
    res = [r[3] for r in a.quest_results[nr:]]
    oks = [u for u in a.quest_updates[nu:] if u[1] == 1]
    log(f"요청 연타 8회: 성공 {[(u[2], u[3]) for u in oks]} 거부 {res}")
    check("spam-one-success", len(oks) == 1 and oks[0][2:] == (Q_ACTIVE, 0) and len(res) == 7, f"성공 {oks} 거부 {res}")
    check("spam-rejects", set(res) <= {R_TOO_FREQUENT, R_BUSY, R_NOT_FINISHED} and R_TOO_FREQUENT in res and R_BUSY in res, f"{res}")
    check("spam-db-one-row", db_quest("Test1", 1) == "1,0,0", f"{db_quest('Test1', 1)!r}")
    check("after-spam", accept(a, 1, cs) == R_NOT_FINISHED, "연타 뒤 다시 요청(= 완료 시도)")

    # ---- 1:1 거래 중(요청 대기 포함)에는 양쪽 모두 IN_TRADE ----
    a.pump(GAP)
    a.send(mk.CS_TRADE_REQUEST, rs.f_int(1, b.id))
    wait_all(cs, lambda: b.trade_requests, 5.0, "SC_TRADE_REQUEST")
    check("in-trade", accept(a, 3, cs) == R_IN_TRADE and accept(b, 1, cs) == R_IN_TRADE and complete(a, 1, cs) == R_IN_TRADE, "거래 중 요청이 IN_TRADE가 아님")
    a.send(CS_TRADE_CANCEL)
    wait_all(cs, lambda: a.trade_ends and b.trade_ends, 5.0, "거래 취소")
    check("after-trade", accept(b, 1, cs) == ("ok", Q_ACTIVE, 0), "거래가 끝난 뒤 수락")

    # ---- 더미 봇: NOT_ALLOWED, 입장 때 목록도 받지 않는다 ----
    bot = Q("bot_q1", dummy=True).enter()
    cs3 = [a, b, bot]
    pump_all(cs3, 0.5)
    check("dummy", SC_QUEST_LIST not in bot.order and accept(bot, 1, cs3, npc=a.quest_npc()) == R_NOT_ALLOWED and complete(bot, 1, cs3, npc=a.quest_npc()) == R_NOT_ALLOWED,
          f"더미 봇이 받은 목록 {bot.quest_lists}")
    bot.close(rst=False)

    # ---- 깨진 패킷·넘치는 값: 서버는 계속 동작 ----
    # 깨진 본문을 보낸 연결은 서버가 끊는다(기존 동작) — 끊긴 뒤의 송신이 실패하는 것은 시험 실패가 아니다
    try:
        a.send(CS_QUEST_REQUEST, b"\xff\xff\xff\xff\xff")
        a.send(CS_QUEST_REQUEST, b"\x08\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff")
        a.send(CS_QUEST_REQUEST, f_i(1, 2 ** 40) + f_i(2, 2 ** 40))
        a.send(CS_QUEST_REQUEST)
    except ConnectionError as e:
        log(f"깨진 패킷을 보내는 중 Test1 연결이 끊김(예상): {e}")
    pump_all(cs, 1.0)
    b.sock.sendall(rs.ping())
    n = b.pongs
    wait_all(cs, lambda: b.pongs > n, 5.0, "깨진 패킷 뒤 핑 응답")
    check("bad-packets", not b.closed and b.pongs > n, "다른 연결이 응답을 받지 못함")
    log(f"깨진 패킷 뒤 Test1 연결 {'끊김' if a.closed else '유지'}, 그 사이 받은 거부 {[r[3] for r in a.quest_results[-3:]]}")

    # ---- 사망(Test3, 1레벨): 사냥터에서 몬스터에게 죽은 뒤 요청 → DEAD ----
    c = Q("Test3").enter()
    cs = [a, b, c]
    pt.to_hunt(c, [a, b])
    provoke(c, [a, b])
    wait_all(cs, lambda: c.dead, 120.0, f"Test3 사망 (HP {c.stat})")
    check("dead", accept(c, 1, cs, npc=QUEST_NPC_GUESS) == R_DEAD and complete(c, 1, cs, npc=QUEST_NPC_GUESS) == R_DEAD, "죽은 채 요청이 DEAD가 아님")
    close_all(cs)
    expect_totals("db-totals", start)


# =====================================================================================================
def give_try(c, item_id, count, others):
    """개발 지급(DbItemFailEvery로 롤백될 수 있다). 성공이면 True."""
    c.pump(GAP)
    n, r = len(c.inv_updates), len(c.item_results)
    c.send(mk.CS_DEV_GIVE, rs.f_int(1, item_id) + rs.f_int(2, count))
    wait_all([c, *others], lambda: len(c.inv_updates) > n or len(c.item_results) > r, 6.0, f"{c.tag} 개발 지급 응답")
    if len(c.inv_updates) > n:
        at.GIVEN["gold" if item_id == 0 else "items"] += count
        return True
    return False


def make_next_fail(c, others):
    """아이템 작업은 두 번에 한 번 롤백된다 — 골드 1을 성공할 때까지 지급해, 바로 다음 작업이 롤백될 차례가 되게 한다."""
    for _ in range(3):
        if give_try(c, 0, 1, others):
            return
    raise RuntimeError("개발 지급이 계속 실패함")


def t_fail():
    # DbItemFailEvery=2
    start = begin()
    a, b = cs = [Q("Test1").enter(), Q("Test2").enter()]
    pump_all(cs, 0.5)
    to_quest_npc(a, b)

    make_next_fail(a, cs)
    check("accept-rollback", accept(a, 1, cs) == R_DB_ERROR and 1 not in a.quests and db_quest("Test1", 1) == "", f"퀘스트 {a.quests} DB {db_quest('Test1', 1)!r}")
    check("accept-retry", accept(a, 1, cs) == ("ok", Q_ACTIVE, 0) and db_quest("Test1", 1) == "1,0,0", "롤백 뒤 다시 수락")

    exp_r, gold_r, item_r, cnt_r = reward_of(2)
    need = int(QUEST[2]["Count"])
    got = accept(b, 2, cs)
    if got == R_DB_ERROR:
        got = accept(b, 2, cs)
    check("accept-b", got == ("ok", Q_ACTIVE, 0), f"{got}")
    while bag_sum(b, ITEM_HORN) < need:
        give_try(b, ITEM_HORN, need - bag_sum(b, ITEM_HORN), cs)
    make_next_fail(b, cs)
    lv0, exp0, gold0, rows0 = b.level, b.exp, b.gold, dict((k, list(v)) for k, v in b.items.items())
    n_exp = len(b.exp_msgs)
    check("complete-rollback", complete(b, 2, cs) == R_DB_ERROR, "롤백된 완료가 DB_ERROR가 아님")
    pump_all(cs, 0.5)
    check("complete-rollback-same", b.items == rows0 and b.gold == gold0 and b.quests[2] == (Q_ACTIVE, 0) and len(b.exp_msgs) == n_exp and (b.level, b.exp) == (lv0, exp0),
          f"메모리가 바뀜: 골드 {gold0} → {b.gold}, 퀘스트 {b.quests}, 경험치 {b.exp_msgs[n_exp:]}")
    check("complete-rollback-db", db_quest("Test2", 2) == "1,0,0" and db_item_sum("Test2", ITEM_HORN) == need and db_item_sum("Test2", item_r) == 0 and db_player("Test2")[2] == gold0,
          f"DB 퀘스트 {db_quest('Test2', 2)!r} 뿔 {db_item_sum('Test2', ITEM_HORN)} 보상 {db_item_sum('Test2', item_r)} 골드 {db_player('Test2')[2]}")
    check("complete-retry", complete(b, 2, cs) == ("ok", Q_DONE, 0), "롤백 뒤 다시 완료")
    pump_all(cs, 0.8)
    EXPECT["items"] += cnt_r - need; EXPECT["gold"] += gold_r
    check("complete-retry-db", db_quest("Test2", 2) == "2,0,1" and db_item_sum("Test2", ITEM_HORN) == 0 and db_item_sum("Test2", item_r) == cnt_r and db_player("Test2")[2] == gold0 + gold_r,
          f"DB 퀘스트 {db_quest('Test2', 2)!r} 골드 {db_player('Test2')[2]}")
    close_all(cs)
    expect_totals("db-totals", start)


CASES = {"main": t_main, "cut": t_cut, "dungeon": t_dungeon, "mail": t_mail, "err": t_err, "fail": t_fail}
if __name__ == "__main__":
    for name in sys.argv[2:]:
        try:
            CASES[name]()
        except Exception as e:
            import traceback
            traceback.print_exc()
            check(f"{name}-run", False, f"{type(e).__name__}: {e}")
        time.sleep(3.0)
    fails = [r for r in gt.RESULTS if r[0] == "FAIL"]
    print(f"\n합계 PASS {len(gt.RESULTS) - len(fails)} / FAIL {len(fails)}")
    sys.exit(1 if fails else 0)
