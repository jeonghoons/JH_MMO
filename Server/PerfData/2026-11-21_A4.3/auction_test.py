# A4.3 경매장 시험(원시 소켓, 계정 Test1~Test4) — AU2a 재기획(D-281)에 맞춰 고침. 사용: python -I -X utf8 auction_test.py <시나리오> [...]
# 서버는 run_auction_tests.sh가 시나리오에 맞는 설정·데이터 테이블로 띄운다(DevCommands=true, PortalNoRange=true).
# 판정 줄은 "PASS <이름>" / "FAIL <이름>: 이유". 공용 코드는 DG3b의 movekit.py, DG2의 party_test.py, DG3c의 dungeon_test.py를 그대로 쓴다.
# 시험에 쓰는 아이템은 개발 지급으로 받는다(천 두건 1101·천 상의 2101·HP 물약 8001·MP 물약 8002). 실행기가 묶음마다 서버를 띄우기 전에
# reset_db.sql로 시험 계정의 경매 흔적(등록·우편·경매 보관/수령함 아이템·지급받은 시험 아이템)을 지우고 골드를 1000으로 맞춘다.
# 요청 간격(검색 AuctionSearchIntervalMs·그 밖 AuctionActionIntervalMs)에 걸리면(TOO_FREQUENT) 도우미가 기다렸다 다시 보낸다 — 간격 자체는 interval에서 본다.
#   basic     NPC에서 멀면 등록·취소·수령 TOO_FAR → 마을 NPC 앞에서 등록(1개당 가격) → 이름 검색·내 목록 → 본인 물품 구매는 OWN_ITEM
#             → 사냥터 Room의 다른 유저가 검색·구매(골드만 줄어듦) → 다 팔린 등록 구매는 SOLD → 사냥터에서 수령은 TOO_FAR → 양쪽 NPC에서 수령
#   partial   물약 스택 일부(5개)만 등록 → 원래 행이 5 줄어듦, 구매자가 5개 묶음을 수령(기존 스택에 합치지 않고 새 행)
#   partbuy   부분 구매(D-281): 30개 등록 → 10개 구매(남은 20) → 25개는 NOT_ENOUGH_COUNT(남은 20) → 20개 구매(다 팔림) → 1개 더는 SOLD,
#             판매 대금 우편 두 통(70·140)·구매품 우편 두 통(10·20개)·Auction_Sale 두 행, InitialCount = 판매 합
#   race      ① 1개짜리 등록에 두 명이 동시에 구매(4회) → 한 명만 성공, 다른 한 명은 SOLD
#             ② 10개짜리 등록에 세 명이 동시에 4개씩(4회) → 정확히 두 명 성공, 한 명은 NOT_ENOUGH_COUNT(남은 2) — 수량 예약
#   cancelrace 같은 등록에 판매자 취소와 구매가 동시에(8회) → 한쪽만 성공(취소가 이기면 구매는 ALREADY_CLOSED, 구매가 이기면 취소는 LISTING_BUSY·NO_LISTING)
#             + 5개짜리 등록에 2개 구매와 취소가 동시에 → 구매가 이기면 남은 3개가 취소 반환
#   rejects   1개당 가격 범위·총액 > GoldMax·수량·장착 중·거래에 올린 행·없는 등록/우편·남의 등록 취소·남의 우편·골드 부족·남은 수량 초과·
#             구매 수량 0·필터(slot ≠ 0)·정렬 값·요청 간격·더미 봇(검색만 허용)
#   dungeon   던전 안에서는 검색·구매·내 목록 모두 IN_DUNGEON
#   search    (AuctionFetchRows 4·AuctionPageRows 2) 이름 검색(부분 일치·공백·영문 대소문자 무시·없는 이름)·정렬 3종 × 방향이 DB 순서와 같음,
#             커서로 이어 받기(사이에 등록·구매가 끼어도 겹침·빠짐 없음), SORT·MORE는 요청의 검색어를 무시하고 서버가 기억한 것을 씀
#   interval  (실제 간격 5000·1000ms) 검색 5초·정렬/다음/내 목록 1초, 검색과 정렬의 간격은 따로
#   expire    (AuctionDurationSec 6·AuctionExpireCheckSec 1) 취소 반환, 만료 반환(우편 종류 3 — 부분 판매 뒤에는 남은 수량), 만료된 등록 구매는 ALREADY_CLOSED,
#             만료 시각 언저리의 구매 연타 → 등록마다 팔림/만료 가운데 하나로만 끝남
#   limits    (AuctionMaxListings 2·GoldMax 3000·AuctionMaxPrice 2500) 등록 수 초과 LISTING_LIMIT, 수령 시 골드 상한 GOLD_FULL,
#             인벤토리가 가득 차면 아이템 우편은 INVENTORY_FULL·골드 우편은 받아짐
#   fail      (DbItemFailEvery 2) 롤백된 등록·구매·수령은 DB_ERROR이고 바뀐 것이 없음, 다시 요청하면 성공(DB 상태를 다시 읽는 동안은 LISTING_BUSY)
#   busy      (AuctionDbMaxInFlight 1·DbSaveDelayMs 1500) 경매 DB 작업이 진행 중이면 다른 사람의 요청은 TRY_LATER, 끝나면 받아짐
#   cut       (DbSaveDelayMs 1500) 등록·구매 요청 직후 접속 해제 → 곧 다시 로그인되고 DB 값과 같음(등록은 목록에 있음, 구매는 골드가 줄어 있음)
#   movewait  (DbSaveDelayMs 1500) 구매 요청 직후 문 포털 → 구매 결과가 먼저 오고 그다음 Room 이동(이동이 결과를 기다림)
#   restart1 / restart2  등록·우편을 남기고 서버를 다시 띄움 → 등록이 그대로 검색되고 우편이 수령됨.
#             꺼져 있는 동안 기간이 지난 등록(실행기가 restart_sql로 ExpireAt을 과거로 바꿈)은 시작 후 첫 검사에서 만료 반환
import csv, json, math, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG3b"))   # -I 는 스크립트 폴더를 경로에 넣지 않는다
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG2"))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG3c"))
import movekit as mk
import move_test as mt
import party_test as pt
import dungeon_test as dt

rs = mk.rs
CS_CHANGE_EQUIP = 1009
CS_TRADE_SET_OFFER, CS_TRADE_CANCEL = 1020, 1023
CS_AUCTION_SEARCH, CS_AUCTION_REGISTER, CS_AUCTION_BUY, CS_AUCTION_CANCEL, CS_AUCTION_MY, CS_MAIL_CLAIM = 1024, 1025, 1026, 1027, 1028, 1029
SC_AUCTION_LIST, SC_AUCTION_MY, SC_AUCTION_RESULT = 2024, 2025, 2026
(A_SUCCESS, A_NOT_IN_ROOM, A_DEAD, A_BUSY, A_TOO_FREQUENT, A_NOT_ALLOWED, A_NO_LISTING, A_SOLD, A_OWN_ITEM, A_NOT_ENOUGH_GOLD,
 A_LISTING_LIMIT, A_BAD_PRICE, A_NO_ITEM, A_IN_TRADE, A_INVENTORY_FULL, A_GOLD_FULL, A_DB_ERROR, A_TOO_FAR, A_IN_DUNGEON, A_TRY_LATER, A_NO_MAIL,
 A_NOT_ENOUGH_COUNT, A_LISTING_BUSY, A_ALREADY_CLOSED) = range(1, 25)
Q_SEARCH, Q_SORT, Q_MORE = 0, 1, 2                 # AuctionQuery
SORT_UNIT, SORT_TOTAL, SORT_REMAIN = 0, 1, 2       # AuctionSort
MAIL_SALE, MAIL_BOUGHT, MAIL_EXPIRED, MAIL_CANCELLED = 1, 2, 3, 4
NPC_KIND_AUCTION = 2
ITEM_HEAD, ITEM_TOP, ITEM_POTION, ITEM_MP = 1101, 2101, 8001, 8002   # 천 두건·천 상의·HP 물약·MP 물약
AUCTION_NPC = (-15600.0, 3500.0)   # 경매 관리인 마커(cm) — 교역소 앞
GAP = 0.15                          # 서버의 아이템 요청 최소 간격(ItemMinIntervalMs 100)보다 조금 길게
STATE_FILE = os.path.join(HERE, "runs", "restart_state.json")
ITEM_TABLE = os.path.join(HERE, "..", "..", "..", "Common", "DataTable", "ItemTable.csv")

check, pump_all, wait_all, close_all = pt.check, pt.pump_all, pt.wait_all, pt.close_all
GIVEN = {"items": 0, "gold": 0}   # 이번 시나리오에서 개발 지급으로 늘린 양(DB 합계 비교에서 뺀다)


def signed(v):
    return v - (1 << 64) if v >= (1 << 63) else v


def unpack_varints(data):
    out, i = [], 0
    while i < len(data):
        v, shift = 0, 0
        while True:
            b = data[i]; i += 1
            v |= (b & 0x7F) << shift; shift += 7
            if not b & 0x80: break
        out.append(v)
    return out


def repeated_ints(msg, field):
    """proto3의 반복 정수는 묶여(packed) 오지만, 하나씩 와도 읽는다."""
    out = []
    for v in msg.get(field, []):
        out.extend(unpack_varints(v) if isinstance(v, (bytes, bytearray)) else [v])
    return out


def parse_item(raw):
    m = mk.decode(raw)
    return mk.first(m, 1), [mk.first(m, 2), mk.first(m, 3), mk.first(m, 4), signed(mk.first(m, 5))]   # dbid, [item_id, count, equip, slot]


def parse_listing(raw):
    m = mk.decode(raw)
    count, unit = mk.first(m, 3), mk.first(m, 4)
    return {"id": mk.first(m, 1), "item": mk.first(m, 2), "count": count, "unit": unit, "price": count * unit,
            "remain": mk.first(m, 5), "mine": bool(mk.first(m, 6))}


def text_of(v):
    return v.decode("utf-8") if isinstance(v, (bytes, bytearray)) else ""


class Client(dt.Client):
    def __init__(self, account, **kw):
        self.items = {}          # item_db_id -> [item_id, count, equip_part, slot]
        self.max_slots = 0
        self.results = []        # 파티 결과와 따로 쓰기 위해 아래 aresults를 쓴다
        self.aresults = []       # (request, result, id, remain)
        self.lists = []          # SC_AUCTION_LIST — parse_list 참고
        self.mys = []            # {"listings": [...], "mails": [...], "more", "max_listings", "min_price", "max_price"}
        self.npcs = {}           # id -> (template_id, npc_kind, x, y)
        self.order = []          # 받은 순서 확인용: 패킷 id
        self.search_wait = 5.1   # 요청 간격(초) — 받은 SC_AUCTION_LIST의 값으로 바뀐다
        self.action_wait = 1.1
        super().__init__(account, **kw)

    def handle(self, pid, body):
        self.order.append(pid)
        if pid == mk.SC_INVENTORY:
            m = mk.decode(body)
            self.items = dict(parse_item(raw) for raw in m.get(1, []))
            self.max_slots = mk.first(m, 3)
        elif pid == mk.SC_INVENTORY_UPDATE:
            m = mk.decode(body)
            for raw in m.get(1, []):
                dbid, row = parse_item(raw)
                self.items[dbid] = row
            for dbid in repeated_ints(m, 2):
                self.items.pop(dbid, None)
        elif pid == mk.SC_ADD_OBJECT:
            info = mk.decode(mk.first(mk.decode(body), 1, b""))
            if mk.first(info, 2) == mk.TYPE_NPC:
                x, y, z, yaw = mk.pos_of(mk.first(info, 4, b""))
                self.npcs[mk.first(info, 1)] = (mk.first(info, 6), mk.first(info, 13), x, y)
        elif pid == mk.SC_REMOVE_OBJECT:
            self.npcs.pop(mk.first(mk.decode(body), 1), None)
        elif pid == mk.SC_ROOM_CHANGE:
            self.npcs = {}
        elif pid == SC_AUCTION_LIST:
            m = mk.decode(body)
            got = {"slot": mk.first(m, 1), "total": mk.first(m, 3), "listings": [parse_listing(raw) for raw in m.get(5, [])],
                   "query": mk.first(m, 6), "sort": mk.first(m, 7), "desc": bool(mk.first(m, 8)), "has_more": bool(mk.first(m, 9)),
                   "keys": [signed(k) for k in repeated_ints(m, 10)], "keyword": text_of(mk.first(m, 11, b"")),
                   "page_rows": mk.first(m, 12), "search_ms": mk.first(m, 13), "action_ms": mk.first(m, 14)}
            self.lists.append(got)
            self.search_wait = got["search_ms"] / 1000.0 + 0.1
            self.action_wait = got["action_ms"] / 1000.0 + 0.1
        elif pid == SC_AUCTION_MY:
            m = mk.decode(body)
            mails = []
            for raw in m.get(2, []):
                mm = mk.decode(raw)
                mails.append({"id": mk.first(mm, 1), "kind": mk.first(mm, 2), "gold": mk.first(mm, 3), "item": mk.first(mm, 4), "count": mk.first(mm, 5), "listing": mk.first(mm, 6)})
            self.mys.append({"listings": [parse_listing(raw) for raw in m.get(1, [])], "mails": mails, "more": bool(mk.first(m, 3)),
                             "max_listings": mk.first(m, 4), "min_price": mk.first(m, 5), "max_price": mk.first(m, 6)})
        elif pid == SC_AUCTION_RESULT:
            m = mk.decode(body)
            self.aresults.append((mk.first(m, 1), mk.first(m, 2), mk.first(m, 3), mk.first(m, 4)))
        super().handle(pid, body)

    # ---- 경매 요청(보내기만) ----
    def auction_npc(self):
        for nid, (template, kind, x, y) in self.npcs.items():
            if kind == NPC_KIND_AUCTION:
                return nid
        return 0

    def send_search(self, query=Q_SEARCH, keyword="", sort=SORT_UNIT, desc=False, cursor=(0, 0), slot=0):
        body = rs.f_int(1, slot) + rs.f_int(3, query) + (rs.f_str(4, keyword) if keyword else b"") + rs.f_int(5, sort) + rs.f_int(6, 1 if desc else 0)
        body += rs.f_int(7, cursor[0]) + rs.f_int(8, cursor[1])
        self.send(CS_AUCTION_SEARCH, body)

    def send_register(self, dbid, count, unit_price, npc=None):
        self.send(CS_AUCTION_REGISTER, rs.f_int(1, self.auction_npc() if npc is None else npc) + rs.f_int(2, dbid) + rs.f_int(3, count) + rs.f_int(4, unit_price))

    def send_buy(self, listing, count=1):
        self.send(CS_AUCTION_BUY, rs.f_int(1, listing) + rs.f_int(2, count))

    def send_cancel(self, listing, npc=None):
        self.send(CS_AUCTION_CANCEL, rs.f_int(1, self.auction_npc() if npc is None else npc) + rs.f_int(2, listing))

    def send_my(self):
        self.send(CS_AUCTION_MY)

    def send_claim(self, mail, npc=None):
        self.send(CS_MAIL_CLAIM, rs.f_int(1, self.auction_npc() if npc is None else npc) + rs.f_int(2, mail))

    def bag(self, item_id=None):
        """인벤토리 위치(장착 아님)의 행: {dbid: [item_id, count, equip, slot]}"""
        return {d: r for d, r in self.items.items() if r[2] == 0 and (item_id is None or r[0] == item_id)}

    def free_slots(self):
        return self.max_slots - len(self.bag())


# ---- 요청하고 결과를 기다리는 도우미(요청 간격에 걸리면 기다렸다 다시 보낸다) ----
def act_full(c, send, request, others=(), timeout=8.0):
    """요청을 보내고 그 요청의 SC_AUCTION_RESULT를 기다려 (result, id, remain)을 돌려준다."""
    for _ in range(8):
        c.pump(GAP)
        n = len(c.aresults)
        send()
        wait_all([c, *others], lambda: any(r[0] == request for r in c.aresults[n:]), timeout, f"{c.tag} SC_AUCTION_RESULT({request}) — 받은 것 {c.aresults[n:]}")
        got = [r for r in c.aresults[n:] if r[0] == request][0]
        if got[1] != A_TOO_FREQUENT:
            return got[1], got[2], got[3]
        pump_all([c, *others], c.action_wait)
    raise AssertionError(f"{c.tag} 요청 {request}이 계속 TOO_FREQUENT")


def act(c, send, request, others=(), timeout=8.0):
    result, rid, _ = act_full(c, send, request, others, timeout)
    return result, rid


def register(c, dbid, count, unit_price, others=(), npc=None):
    return act(c, lambda: c.send_register(dbid, count, unit_price, npc), CS_AUCTION_REGISTER, others)


def buy(c, listing, others=(), count=1):
    return act(c, lambda: c.send_buy(listing, count), CS_AUCTION_BUY, others)[0]


def buy_full(c, listing, count, others=()):
    """(result, remain)"""
    result, _, remain = act_full(c, lambda: c.send_buy(listing, count), CS_AUCTION_BUY, others)
    return result, remain


def cancel(c, listing, others=(), npc=None):
    return act(c, lambda: c.send_cancel(listing, npc), CS_AUCTION_CANCEL, others)[0]


def claim(c, mail, others=(), npc=None):
    return act(c, lambda: c.send_claim(mail, npc), CS_MAIL_CLAIM, others)[0]


def search(c, keyword="", others=(), sort=SORT_UNIT, desc=False, query=Q_SEARCH, cursor=(0, 0)):
    """검색 응답 한 묶음(SC_AUCTION_LIST). 요청 간격에 걸리면 기다렸다 다시, 그 밖의 거부면 AssertionError."""
    for _ in range(8):
        c.pump(GAP)
        n, r = len(c.lists), len(c.aresults)
        c.send_search(query, keyword, sort, desc, cursor)
        wait_all([c, *others], lambda: len(c.lists) > n or len(c.aresults) > r, 6.0, f"{c.tag} SC_AUCTION_LIST")
        if len(c.lists) > n:
            return c.lists[-1]
        check("search", c.aresults[-1][:2] == (CS_AUCTION_SEARCH, A_TOO_FREQUENT), f"{c.tag} 검색이 거부됨: {c.aresults[r:]}")
        pump_all([c, *others], c.search_wait if query == Q_SEARCH else c.action_wait)
    raise AssertionError(f"{c.tag} 검색이 계속 TOO_FREQUENT")


def search_all(c, keyword="", others=(), sort=SORT_UNIT, desc=False):
    """그 검색어의 등록 전부(커서로 끝까지) + 첫 응답의 total"""
    got = search(c, keyword, others, sort, desc)
    out, total = list(got["listings"]), got["total"]
    while got["has_more"]:
        got = search(c, keyword, others, sort, desc, Q_MORE, (got["keys"][-1], got["listings"][-1]["id"]))
        out.extend(got["listings"])
    return out, total


def my(c, others=()):
    for _ in range(8):
        c.pump(GAP)
        n, r = len(c.mys), len(c.aresults)
        c.send_my()
        wait_all([c, *others], lambda: len(c.mys) > n or len(c.aresults) > r, 8.0, f"{c.tag} SC_AUCTION_MY")
        if len(c.mys) > n:
            return c.mys[-1]
        check("my", c.aresults[-1][:2] == (CS_AUCTION_MY, A_TOO_FREQUENT), f"{c.tag} 내 목록이 거부됨: {c.aresults[r:]}")
        pump_all([c, *others], c.action_wait)
    raise AssertionError(f"{c.tag} 내 목록이 계속 TOO_FREQUENT")


def give(c, item_id, count, others=()):
    """개발 지급. 새로 생긴 행(dbid 목록)을 돌려준다(골드면 빈 목록)."""
    c.pump(GAP)
    before, n = set(c.items), len(c.inv_updates)
    c.send(mk.CS_DEV_GIVE, rs.f_int(1, item_id) + rs.f_int(2, count))
    wait_all([c, *others], lambda: len(c.inv_updates) > n, 6.0, f"{c.tag} 개발 지급 {item_id} x{count} (거부 {c.item_results[-2:]})")
    GIVEN["gold" if item_id == 0 else "items"] += count
    return sorted(set(c.items) - before)


def give_one(c, item_id, others=()):
    new = give(c, item_id, 1, others)
    check("give", len(new) == 1 and c.items[new[0]][0] == item_id, f"{c.tag} 지급 결과가 한 행이 아님: {new}")
    return new[0]


def stack_row(c, item_id, need, others=()):
    """need개를 지급하고, 그 종류의 인벤토리 행 가운데 수량이 가장 많은 행을 돌려준다(MaxStack 100 — need ≤ 100이면 need개 이상)."""
    give(c, item_id, need, others)
    rows = c.bag(item_id)
    row = max(rows, key=lambda d: rows[d][1])
    check("stack", rows[row][1] >= need, f"{c.tag} {item_id} 행 수량 {rows[row][1]} < {need}")
    return row


# ---- 걷기: 마을 안에서 경매 NPC 앞까지. 직선으로 가면 모닥불·깃발·상자에 막혀 서버가 이동을 보정하므로 꺾임점을 따른다 ----
def route_to_npc(x, y):
    if y > 2500:        # 동문 안쪽: 교역소와 상자 더미 사이 통로로
        pts = [(-14750, 5000), (-14750, 3300)]
    elif y < -2500:     # 서문 안쪽: 가판대 줄 사이 통로 → 광장 동쪽
        pts = [(-14800, -3000), (-14500, -800), (-14600, 1000), (-15000, 2400)]
    elif x > -12000:    # 북문 안쪽
        pts = [(-13000, 0), (-14600, 1000), (-15000, 2400)]
    else:               # 광장
        pts = [(-14600, 1000), (-15000, 2400)]
    return pts + [(AUCTION_NPC[0], AUCTION_NPC[1] - 250.0)]


def walk_all(plan, speed=380.0, step_s=0.2, limit_s=120.0):
    """plan = [(client, [(x, y), ...])] — 모두 함께 한 걸음씩 걷는다(기다리는 쪽이 유휴로 끊기지 않게)."""
    state = [[c, list(route)] for c, route in plan]
    start = time.time()
    while any(route for _, route in state):
        if time.time() - start > limit_s:
            raise TimeoutError("걷기 시간 초과: " + ", ".join(f"{c.tag}({c.x:.0f},{c.y:.0f})" for c, r in state if r))
        for c, route in state:
            if not route:
                continue
            tx, ty = route[0]
            dx, dy = tx - c.x, ty - c.y
            dist = math.hypot(dx, dy)
            if dist <= 60.0:
                route.pop(0)
                if not route:
                    c.send_move(c.x, c.y, c.z)   # 멈춤
                continue
            step = min(speed * step_s, dist - 59.0)
            c.x += dx / dist * step
            c.y += dy / dist * step
            c.send_move(c.x, c.y, c.z, dx / dist * speed, dy / dist * speed)
        pump_all([c for c, _ in state], step_s)
    pump_all([c for c, _ in state], 0.3)


def to_npc(*clients, others=()):
    """경매 NPC 이용 거리 안까지 걷는다."""
    walk_all([(c, route_to_npc(c.x, c.y)) for c in clients] + [(o, []) for o in others])
    for c in clients:
        d = math.hypot(c.x - AUCTION_NPC[0], c.y - AUCTION_NPC[1])
        check("at-npc", c.auction_npc() != 0 and d <= 380.0, f"{c.tag}이 경매 NPC 앞이 아님: 거리 {d:.0f}cm, 보이는 NPC {c.npcs}")


def to_field(c, others=()):
    """마을 → 북문 포털 → 사냥터 Room(다른 Room)."""
    pt.to_field(c, others, north=True)
    check("field", c.kind == mk.KIND_FIELD, f"{c.tag}이 사냥터가 아님(kind {c.kind})")


def to_town(c, others=()):
    """사냥터 → 가장 가까운 문 포털 → 마을 Room."""
    time.sleep(1.1)   # 포털 재사용 대기
    pid = c.goto_portal()
    n = len(c.room_changes)
    c.use_portal(pid)
    wait_all([c, *others], lambda: len(c.room_changes) > n and c.kind == mk.KIND_TOWN, 10.0, f"{c.tag} 마을로 SC_ROOM_CHANGE")
    pump_all([c, *others], 0.6)


def login(*accounts):
    return [Client(a).enter() for a in accounts]


def find_mail(view, listing, kind):
    got = [m for m in view["mails"] if m["listing"] == listing and m["kind"] == kind]
    return got[0] if got else None


def find_mails(view, listing, kind):
    return [m for m in view["mails"] if m["listing"] == listing and m["kind"] == kind]


def need_gold(c, amount, others=()):
    """골드가 amount 이상이 되게 개발 지급한다."""
    if c.gold < amount:
        give(c, 0, amount - c.gold, others)


# ---- DB 확인 ----
def db_rows(query):
    """여러 행·여러 열(정수만) — [(v1, v2, ...), ...]"""
    out = subprocess.run(["sqlcmd", "-S", r".\SQLEXPRESS", "-d", "GameServer", "-E", "-I", "-h", "-1", "-W", "-s", "|", "-Q", "SET NOCOUNT ON; " + query],
                         capture_output=True, text=True, timeout=60).stdout.strip().splitlines()
    return [tuple(int(v) for v in line.split("|")) for line in out if line.strip()]


def totals():
    items = int(mk.db_scalar("SELECT ISNULL(SUM(CAST([Count] AS BIGINT)), 0) FROM Player_Item"))
    gold = int(mk.db_scalar("SELECT (SELECT ISNULL(SUM(Gold), 0) FROM Player_Info) + (SELECT ISNULL(SUM(Gold), 0) FROM Player_Mail)"))
    return items, gold


def db_broken():
    """경매 데이터가 서로 어긋난 행 수(0이어야 한다): 판매 중인데 경매 보관에 없는(또는 수량이 다른) 아이템, 등록 없이 경매 보관에 있는 아이템,
    우편이 가리키는데 수령함에 없는 아이템, 우편 없이 수령함에 있는 아이템, 칸 번호가 남은 경매 보관·수령함 행,
    처음 수량 ≠ 남은 수량 + 판매 합(D-281), 판매 대금 ≠ 수량 × 1개당 가격, 팔림(State 2)인데 남은 수량이 0이 아님."""
    q = ("SELECT (SELECT COUNT(*) FROM Auction_Listing L WHERE L.State = 1 AND NOT EXISTS (SELECT 1 FROM Player_Item I WHERE I.ItemDbId = L.ItemDbId AND I.Location = 3 AND I.OwnerId = L.SellerId AND I.[Count] = L.[Count]))"
         " + (SELECT COUNT(*) FROM Player_Item I WHERE I.Location = 3 AND NOT EXISTS (SELECT 1 FROM Auction_Listing L WHERE L.ItemDbId = I.ItemDbId AND L.State = 1))"
         " + (SELECT COUNT(*) FROM Player_Mail M WHERE M.ItemDbId IS NOT NULL AND NOT EXISTS (SELECT 1 FROM Player_Item I WHERE I.ItemDbId = M.ItemDbId AND I.Location = 4 AND I.OwnerId = M.PlayerId))"
         " + (SELECT COUNT(*) FROM Player_Item I WHERE I.Location = 4 AND NOT EXISTS (SELECT 1 FROM Player_Mail M WHERE M.ItemDbId = I.ItemDbId))"
         " + (SELECT COUNT(*) FROM Player_Item I WHERE I.Location IN (3, 4) AND I.BagSlot <> -1)"
         " + (SELECT COUNT(*) FROM Auction_Listing L WHERE L.InitialCount <> L.[Count] + ISNULL((SELECT SUM(S.[Count]) FROM Auction_Sale S WHERE S.ListingId = L.ListingId), 0))"
         " + (SELECT COUNT(*) FROM Auction_Sale S INNER JOIN Auction_Listing L ON L.ListingId = S.ListingId WHERE S.Gold <> S.[Count] * L.UnitPrice)"
         " + (SELECT COUNT(*) FROM Auction_Listing L WHERE L.State = 2 AND L.[Count] <> 0)")
    return int(mk.db_scalar(q))


def db_active():
    return int(mk.db_scalar("SELECT COUNT(*) FROM Auction_Listing WHERE State = 1"))


def db_state(listing):
    return int(mk.db_scalar(f"SELECT CAST(State AS INT) FROM Auction_Listing WHERE ListingId = {listing}") or 0)


def db_count(listing):
    return int(mk.db_scalar(f"SELECT [Count] FROM Auction_Listing WHERE ListingId = {listing}") or -1)


def list_matches_db(c, others=()):
    """서비스의 목록(검색 전체)과 DB의 판매 중인 등록 수가 같다."""
    time.sleep(0.3)
    listings, total = search_all(c, "", others)
    active = db_active()
    check("list=db", total == active and len(listings) == active, f"목록 {total}건(받은 {len(listings)}건) != DB 판매 중 {active}건")


def norm(text):
    """서버의 DataManager::NormalizeSearchText와 같게: 공백 제거 + 영문 소문자"""
    return "".join(ch for ch in text if not ch.isspace()).translate(str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"))


def items_matching(keyword):
    names = {int(r["Name"]): r["DisplayName"] for r in csv.DictReader(open(ITEM_TABLE, encoding="utf-8-sig"))}
    k = norm(keyword)
    return {i for i, n in names.items() if k in norm(n)} if k else set(names)


def expected_order(keyword, sort, desc):
    """DB의 판매 중인 등록을 서버와 같은 규칙으로 정렬한 ListingId 목록"""
    rows = db_rows("SELECT ListingId, ItemId, [Count], UnitPrice, DATEDIFF_BIG(MILLISECOND, '2000-01-01', ExpireAt) FROM Auction_Listing WHERE State = 1")
    ids = items_matching(keyword)
    keyed = []
    for lid, item, count, unit, expire in rows:
        if item not in ids:
            continue
        value = unit if sort == SORT_UNIT else (unit * count if sort == SORT_TOTAL else expire)
        keyed.append((value, lid))
    keyed.sort(reverse=desc)
    return [lid for _, lid in keyed]


# =========================================================================================================
def t_basic():
    a, b = cs = login("Test1", "Test2")
    # NPC에서 멀면(시작 지점) 등록·취소·수령은 TOO_FAR
    head = give_one(a, ITEM_HEAD, cs)
    check("far-register", register(a, head, 1, 300, cs, npc=0)[0] == A_TOO_FAR, "먼 곳의 등록이 TOO_FAR가 아님")
    check("far-cancel", cancel(a, 1, cs, npc=0) == A_TOO_FAR, "먼 곳의 취소가 TOO_FAR가 아님")
    check("far-claim", claim(a, 1, cs, npc=0) == A_TOO_FAR, "먼 곳의 수령이 TOO_FAR가 아님")

    to_npc(a, others=[b])
    npc = a.auction_npc()
    gold_a = a.gold
    result, listing = register(a, head, 1, 300, cs)
    check("register", result == A_SUCCESS and listing > 0, f"등록 결과 {result}")
    check("escrow", head not in a.items, "등록한 행이 인벤토리에 남아 있음")
    check("db-escrow", mk.db_scalar(f"SELECT CAST(Location AS INT) FROM Player_Item WHERE ItemDbId = {head}") == "3", "DB에서 경매 보관(Location 3)이 아님")

    # 이름 검색·내 목록
    found = [l for l in search_all(a, "두건", cs)[0] if l["id"] == listing]
    check("search", found and found[0]["item"] == ITEM_HEAD and found[0]["count"] == 1 and found[0]["unit"] == 300 and found[0]["mine"] and found[0]["remain"] > 86000, f"검색 결과 {found}")
    check("other-name", not [l for l in search_all(a, "상의", cs)[0] if l["id"] == listing], "다른 이름 검색에 보임")
    view = my(a, cs)
    check("my", [l for l in view["listings"] if l["id"] == listing] and view["max_listings"] == 10 and view["min_price"] == 1 and view["max_price"] == 100000000, f"내 목록 {view}")
    check("own", buy(a, listing, cs) == A_OWN_ITEM, "본인 물품 구매가 OWN_ITEM이 아님")

    # 먼 곳(마을 시작 지점)의 다른 유저: 진짜 NPC id를 써도 거리 밖이면 TOO_FAR
    check("far-npc", claim(b, 1, cs, npc=npc) == A_TOO_FAR, "NPC id가 맞아도 거리 밖이면 TOO_FAR여야 함")

    # 사냥터 Room(다른 Room)의 유저가 검색·구매
    need_gold(b, 500, cs)
    a.pump(0.1)
    to_field(b, [a])
    seen = [l for l in search_all(b, "", cs)[0] if l["id"] == listing]
    check("search-other-room", seen and not seen[0]["mine"], f"다른 Room에서 검색 결과 {seen}")
    gold_b, items_b = b.gold, len(b.items)
    check("buy", buy(b, listing, cs) == A_SUCCESS, "구매 실패")
    check("buy-gold", b.gold == gold_b - 300 and len(b.items) == items_b, f"구매 뒤 골드 {gold_b} -> {b.gold}, 아이템 행 {items_b} -> {len(b.items)}")
    check("sold-again", buy(b, listing, cs) == A_SOLD, "다 팔린 등록을 다시 사면 SOLD여야 함")
    check("field-claim", claim(b, 1, cs, npc=npc) == A_TOO_FAR, "사냥터에서 수령이 TOO_FAR가 아님")
    check("gone", not [l for l in search_all(b, "", cs)[0] if l["id"] == listing], "팔린 등록이 검색에 남음")

    # 판매자 수령(판매 대금)
    view = my(a, cs)
    check("my-gone", not [l for l in view["listings"] if l["id"] == listing], "팔린 등록이 내 목록에 남음")
    mail = find_mail(view, listing, MAIL_SALE)
    check("sale-mail", mail and mail["gold"] == 300 and mail["item"] == 0, f"판매 대금 우편 {view['mails']}")
    check("claim-gold", claim(a, mail["id"], cs) == A_SUCCESS and a.gold == gold_a + 300, f"대금 수령 뒤 골드 {gold_a} -> {a.gold}")
    check("claim-twice", claim(a, mail["id"], cs) == A_NO_MAIL, "받은 우편을 다시 받으면 NO_MAIL이어야 함")

    # 구매자 수령(구매품) — 마을로 돌아와 NPC 앞에서
    to_town(b, [a])
    to_npc(b, others=[a])
    view = my(b, cs)
    mail = find_mail(view, listing, MAIL_BOUGHT)
    check("item-mail", mail and mail["item"] == ITEM_HEAD and mail["count"] == 1 and mail["gold"] == 0, f"구매품 우편 {view['mails']}")
    check("claim-item", claim(b, mail["id"], cs) == A_SUCCESS, "구매품 수령 실패")
    check("got-item", head in b.items and b.items[head][:3] == [ITEM_HEAD, 1, 0] and 0 <= b.items[head][3] < b.max_slots, f"받은 행 {b.items.get(head)}")
    check("db-owner", mk.db_scalar(f"SELECT CAST(Location AS INT) FROM Player_Item WHERE ItemDbId = {head} AND OwnerId = (SELECT TOP 1 BuyerId FROM Auction_Sale WHERE ListingId = {listing})") == "1", "DB에서 구매자의 인벤토리가 아님")
    list_matches_db(a, cs)
    close_all(cs)


def t_partial():
    a, b = cs = login("Test1", "Test2")
    row = stack_row(a, ITEM_POTION, 20, cs)
    have = a.items[row][1]
    need_gold(b, 200, cs)
    to_npc(a, b)
    check("too-many", register(a, row, have + 1, 50, cs)[0] == A_NO_ITEM, "가진 것보다 많이 등록")
    check("zero", register(a, row, 0, 50, cs)[0] == A_NO_ITEM, "0개 등록")
    result, listing = register(a, row, 5, 10, cs)
    check("register", result == A_SUCCESS and a.items[row][1] == have - 5, f"일부 등록 뒤 원래 행 {a.items.get(row)} (처음 {have})")
    seen = [l for l in search_all(b, "HP 물약", cs)[0] if l["id"] == listing]
    check("search", seen and seen[0]["item"] == ITEM_POTION and seen[0]["count"] == 5 and seen[0]["unit"] == 10 and seen[0]["price"] == 50, f"검색 {seen}")
    check("buy", buy(b, listing, cs, 5) == A_SUCCESS, "구매 실패")
    mail = find_mail(my(b, cs), listing, MAIL_BOUGHT)
    check("mail", mail and mail["item"] == ITEM_POTION and mail["count"] == 5, f"우편 {mail}")
    before = set(b.items)
    check("claim", claim(b, mail["id"], cs) == A_SUCCESS, "수령 실패")
    got = sorted(set(b.items) - before)
    check("got", len(got) == 1 and b.items[got[0]][:3] == [ITEM_POTION, 5, 0] and got[0] != row, f"받은 행 {[b.items[d] for d in got]} (기존 스택에 합치지 않고 새 행)")
    list_matches_db(a, cs)
    close_all(cs)


def t_partbuy():
    a, b = cs = login("Test1", "Test2")
    row = stack_row(a, ITEM_POTION, 30, cs)
    need_gold(b, 1000, cs)
    to_npc(a, b)
    result, listing = register(a, row, 30, 7, cs)
    check("register", result == A_SUCCESS, f"등록 {result}")
    check("db-initial", db_rows(f"SELECT InitialCount, [Count], UnitPrice FROM Auction_Listing WHERE ListingId = {listing}") == [(30, 30, 7)], "DB 처음 수량·남은 수량·단가")

    gold_b = b.gold
    result, remain = buy_full(b, listing, 10, cs)
    check("buy-10", result == A_SUCCESS and remain == 20 and b.gold == gold_b - 70, f"10개 구매: 결과 {result}, 남은 {remain}, 골드 {gold_b} -> {b.gold}")
    seen = [l for l in search_all(b, "HP 물약", cs)[0] if l["id"] == listing]
    check("search-20", seen and seen[0]["count"] == 20 and seen[0]["price"] == 140, f"검색 {seen}")
    result, remain = buy_full(b, listing, 25, cs)
    check("too-many", result == A_NOT_ENOUGH_COUNT and remain == 20 and b.gold == gold_b - 70, f"25개 구매: 결과 {result}, 남은 {remain}")
    result, remain = buy_full(b, listing, 20, cs)
    check("buy-rest", result == A_SUCCESS and remain == 0 and b.gold == gold_b - 210, f"남은 20개 구매: 결과 {result}, 남은 {remain}, 골드 {b.gold}")
    check("sold", buy(b, listing, cs) == A_SOLD, "다 팔린 뒤 구매가 SOLD가 아님")
    check("db-sold", db_state(listing) == 2 and db_count(listing) == 0, f"DB 상태 {db_state(listing)}, 남은 {db_count(listing)}")
    check("db-sales", sorted(db_rows(f"SELECT [Count], Gold FROM Auction_Sale WHERE ListingId = {listing}")) == [(10, 70), (20, 140)], "Auction_Sale 행")

    view = my(a, cs)
    check("sale-mails", sorted(m["gold"] for m in find_mails(view, listing, MAIL_SALE)) == [70, 140], f"판매 대금 우편 {view['mails']}")
    gold_a = a.gold
    for m in find_mails(view, listing, MAIL_SALE):
        check("claim-sale", claim(a, m["id"], cs) == A_SUCCESS, "대금 수령")
    check("gold-a", a.gold == gold_a + 210, f"판매자 골드 {gold_a} -> {a.gold}")
    view = my(b, cs)
    bought = find_mails(view, listing, MAIL_BOUGHT)
    check("bought-mails", sorted(m["count"] for m in bought) == [10, 20] and all(m["item"] == ITEM_POTION for m in bought), f"구매품 우편 {view['mails']}")
    before = set(b.items)
    for m in bought:
        check("claim-item", claim(b, m["id"], cs) == A_SUCCESS, "구매품 수령")
    got = sorted(b.items[d][1] for d in set(b.items) - before)
    check("got", got == [10, 20], f"받은 행 수량 {got}")
    list_matches_db(a, cs)
    close_all(cs)


def t_race():
    a, b, c, d = cs = login("Test1", "Test2", "Test3", "Test4")
    heads = [give_one(a, ITEM_HEAD, cs) for _ in range(4)]
    row = stack_row(a, ITEM_POTION, 40, cs)
    for x in (b, c, d):
        need_gold(x, 2000, cs)
    to_npc(a, others=[b, c, d])

    # ① 1개짜리 등록에 두 명이 동시에
    listings = []
    for h in heads:
        result, listing = register(a, h, 1, 100, cs)
        check("register", result == A_SUCCESS, f"등록 실패 {result}")
        listings.append(listing)
    wins = {b.tag: 0, c.tag: 0}
    for i, listing in enumerate(listings):
        pump_all(cs, 1.1)   # 경매 요청 간격
        nb, nc = len(b.aresults), len(c.aresults)
        first, second = (b, c) if i % 2 == 0 else (c, b)   # 거의 동시에, 먼저 보내는 쪽을 번갈아
        first.send_buy(listing); second.send_buy(listing)
        wait_all(cs, lambda: len(b.aresults) > nb and len(c.aresults) > nc, 8.0, "두 구매 결과")
        rb, rc = b.aresults[-1][1], c.aresults[-1][1]
        check("one-winner", sorted([rb, rc]) == sorted([A_SUCCESS, A_SOLD]), f"등록 {listing}: 결과 {rb}·{rc} (한 명만 성공, 다른 쪽은 SOLD)")
        wins[b.tag if rb == A_SUCCESS else c.tag] += 1
        check("db-sold", db_state(listing) == 2, f"등록 {listing}의 DB 상태 {db_state(listing)}")
    check("gold", b.gold == 2000 - 100 * wins[b.tag] and c.gold == 2000 - 100 * wins[c.tag], f"골드 {b.gold}·{c.gold}, 이긴 수 {wins}")

    # ② 10개짜리 등록에 세 명이 동시에 4개씩 — 수량 예약으로 정확히 두 명만 성공
    gold0 = {x.tag: x.gold for x in (b, c, d)}
    potions = []
    for _ in range(4):
        result, listing = register(a, row, 10, 5, cs)
        check("register-p", result == A_SUCCESS, f"등록 실패 {result}")
        potions.append(listing)
    bought = {x.tag: 0 for x in (b, c, d)}
    for i, listing in enumerate(potions):
        pump_all(cs, 1.1)
        n = {x.tag: len(x.aresults) for x in (b, c, d)}
        order = [(b, c, d), (c, d, b), (d, b, c), (b, d, c)][i]
        for x in order:
            x.send_buy(listing, 4)
        wait_all(cs, lambda: all(len(x.aresults) > n[x.tag] for x in (b, c, d)), 8.0, "세 구매 결과")
        got = {x.tag: x.aresults[-1] for x in (b, c, d)}
        ok = [t for t, r in got.items() if r[1] == A_SUCCESS]
        short = [t for t, r in got.items() if r[1] == A_NOT_ENOUGH_COUNT and r[3] == 2]
        check("two-winners", len(ok) == 2 and len(short) == 1, f"등록 {listing}: 결과 {got} (두 명 성공·한 명 NOT_ENOUGH_COUNT 남은 2)")
        for t in ok:
            bought[t] += 4
        check("db-left", db_state(listing) == 1 and db_count(listing) == 2, f"등록 {listing}: DB 상태 {db_state(listing)}, 남은 {db_count(listing)}")
    for x in (b, c, d):
        check("gold-p", x.gold == gold0[x.tag] - 5 * bought[x.tag], f"{x.tag} 골드 {gold0[x.tag]} -> {x.gold}, 산 수 {bought[x.tag]}")
    view = my(a, cs)
    check("sale-mails", len([m for m in view["mails"] if m["listing"] in listings and m["kind"] == MAIL_SALE and m["gold"] == 100]) == 4, f"판매 대금 우편 {len(view['mails'])}건")
    print(f"  race: 통째 이긴 수 {wins}, 부분 산 수 {bought}")
    list_matches_db(a, cs)
    # 남은 물약 등록(각 2개)을 취소해 둔다 — 다음 시나리오가 1인 등록 수 상한(10)에 걸리지 않게
    for listing in potions:
        check("cleanup", cancel(a, listing, cs) == A_SUCCESS, f"등록 {listing} 정리 취소")
    close_all(cs)


def t_cancelrace():
    a, b = cs = login("Test1", "Test2")
    rows = [give_one(a, ITEM_HEAD, cs) for _ in range(8)]
    row = stack_row(a, ITEM_POTION, 5, cs)
    need_gold(b, 2000, cs)
    to_npc(a, others=[b])
    listings = [register(a, r, 1, 100, cs)[1] for r in rows]
    check("register", all(l > 0 for l in listings), f"등록 {listings}")
    bought = cancelled = 0
    for i, listing in enumerate(listings):
        pump_all(cs, 1.1)   # 경매 요청 간격
        na, nb = len(a.aresults), len(b.aresults)
        if i % 2 == 0:
            a.send_cancel(listing); b.send_buy(listing)
        else:
            b.send_buy(listing); a.send_cancel(listing)
        wait_all(cs, lambda: len(a.aresults) > na and len(b.aresults) > nb, 8.0, "취소·구매 결과")
        ra, rb = a.aresults[-1][1], b.aresults[-1][1]
        ok = (ra == A_SUCCESS and rb == A_ALREADY_CLOSED) or (rb == A_SUCCESS and ra in (A_LISTING_BUSY, A_NO_LISTING))
        check("one-wins", ok, f"등록 {listing}: 취소 {ra}, 구매 {rb} (한쪽만 성공해야 함)")
        state = db_state(listing)
        check("db", state == (4 if ra == A_SUCCESS else 2), f"등록 {listing}의 DB 상태 {state} (취소 {ra}, 구매 {rb})")
        cancelled += ra == A_SUCCESS
        bought += rb == A_SUCCESS
    check("gold", b.gold == 2000 - 100 * bought, f"구매자 골드 {b.gold}, 산 수 {bought}")

    # 5개짜리에 2개 구매와 취소가 동시에: 구매가 이기면 취소는 LISTING_BUSY → 다시 취소하면 남은 3개가 반환
    result, plisting = register(a, row, 5, 10, cs)
    check("register-p", result == A_SUCCESS, f"등록 {result}")
    pump_all(cs, 1.1)
    na, nb = len(a.aresults), len(b.aresults)
    b.send_buy(plisting, 2); a.send_cancel(plisting)
    wait_all(cs, lambda: len(a.aresults) > na and len(b.aresults) > nb, 8.0, "부분 구매·취소 결과")
    ra, rb = a.aresults[-1][1], b.aresults[-1][1]
    if rb == A_SUCCESS:
        check("p-busy", ra == A_LISTING_BUSY, f"부분 구매가 이겼는데 취소 {ra}")
        check("p-cancel", cancel(a, plisting, cs) == A_SUCCESS, "구매가 끝난 뒤 취소")
        back = 3
    else:
        check("p-cancel-first", ra == A_SUCCESS and rb == A_ALREADY_CLOSED, f"취소 {ra}, 구매 {rb}")
        back = 5
    # 수령함은 오래된 것부터 한 쪽(20통)만 보이므로(앞 시나리오의 우편이 많다) 우편은 DB에서 센다
    back_mail = db_rows(f"SELECT I.[Count] FROM Player_Mail M INNER JOIN Player_Item I ON I.ItemDbId = M.ItemDbId WHERE M.ListingId = {plisting} AND M.Kind = {MAIL_CANCELLED}")
    check("p-back", back_mail == [(back,)] and db_state(plisting) == 4 and db_count(plisting) == back, f"취소 반환 {back_mail}, DB 남은 {db_count(plisting)}")
    ids = ",".join(str(x) for x in listings)
    kinds = dict(db_rows(f"SELECT Kind, COUNT(*) FROM Player_Mail WHERE ListingId IN ({ids}) AND PlayerId = (SELECT SellerId FROM Auction_Listing WHERE ListingId = {listings[0]}) GROUP BY Kind"))
    check("mails", kinds.get(MAIL_CANCELLED, 0) == cancelled and kinds.get(MAIL_SALE, 0) == bought, f"우편 종류별 {kinds} (취소 {cancelled}, 판매 {bought})")
    print(f"  cancelrace: 취소 {cancelled}, 구매 {bought}, 부분 {'구매 먼저' if back == 3 else '취소 먼저'}")
    list_matches_db(a, cs)
    close_all(cs)


def t_rejects():
    a, b, c = cs = login("Test1", "Test2", "Test3")
    head = give_one(a, ITEM_HEAD, cs)
    row = stack_row(a, ITEM_POTION, 21, cs)
    to_npc(a, c, others=[b])
    npc = a.auction_npc()
    # 1개당 가격 범위·총액 > GoldMax·없는 행·장착 중인 행
    check("price-0", register(a, head, 1, 0, cs)[0] == A_BAD_PRICE, "가격 0")
    check("price-max", register(a, head, 1, 100000001, cs)[0] == A_BAD_PRICE, "1개당 가격 상한 초과")
    check("total-over", register(a, row, 21, 100000000, cs)[0] == A_BAD_PRICE, "총액(21 × 1억) > GoldMax")
    check("no-row", register(a, 999999999999, 1, 10, cs)[0] == A_NO_ITEM, "없는 행")
    equipped = [d for d, r in a.items.items() if r[2] != 0]
    check("has-equip", equipped, "장착한 아이템이 없어 시험할 수 없음")
    check("equipped", register(a, equipped[0], 1, 10, cs)[0] == A_NO_ITEM, "장착 중인 행 등록")
    check("not-auction-npc", register(a, head, 1, 10, cs, npc=npc + 12345)[0] == A_TOO_FAR, "없는 NPC id")

    # 거래에 올린 행은 등록할 수 없다
    a.send(mk.CS_TRADE_REQUEST, rs.f_int(1, c.id))
    wait_all(cs, lambda: c.trade_requests, 5.0, "SC_TRADE_REQUEST")
    c.send(mk.CS_TRADE_RESPOND, rs.f_int(1, 1))
    pump_all(cs, 0.5)
    a.send(CS_TRADE_SET_OFFER, mk.f_msg(1, rs.f_int(1, head) + rs.f_int(3, 1)))
    pump_all(cs, 0.5)
    check("in-trade", register(a, head, 1, 10, cs)[0] == A_IN_TRADE, "거래에 올린 행 등록이 IN_TRADE가 아님")
    a.send(CS_TRADE_CANCEL)
    wait_all(cs, lambda: a.trade_ends, 5.0, "거래 취소")

    # 등록 하나를 두고: 남의 등록 취소·남은 수량 초과·수량 0·골드 부족·없는 등록
    result, listing = register(a, head, 1, 5000, cs)
    check("register", result == A_SUCCESS, f"등록 {result}")
    check("cancel-others", cancel(c, listing, cs) == A_NO_LISTING, "남의 등록 취소")
    result, remain = buy_full(b, listing, 2, cs)
    check("over-count", result == A_NOT_ENOUGH_COUNT and remain == 1, f"남은 수량보다 많이: {result}, 남은 {remain}")
    check("zero-count", buy(b, listing, cs, 0) == A_NOT_ALLOWED, "수량 0 구매")
    check("no-gold", b.gold < 5000 and buy(b, listing, cs) == A_NOT_ENOUGH_GOLD, f"골드 부족(가진 골드 {b.gold})")
    check("no-listing", buy(b, 999999999999, cs) == A_NO_LISTING, "없는 등록 구매")
    check("bad-id", buy(b, 0, cs) == A_NO_LISTING, "등록 id 0")
    check("no-mail", claim(a, 999999999999, cs) == A_NO_MAIL, "없는 우편 수령")

    # 남의 우편은 받을 수 없다: a가 취소해 생긴 우편을 c가 받으려 함. 취소한 등록 구매는 ALREADY_CLOSED
    check("cancel", cancel(a, listing, cs) == A_SUCCESS, "취소 실패")
    check("cancel-twice", cancel(a, listing, cs) == A_NO_LISTING, "취소한 등록을 다시 취소")
    check("buy-cancelled", buy(b, listing, cs) == A_ALREADY_CLOSED, "취소한 등록 구매가 ALREADY_CLOSED가 아님")
    mail = find_mail(my(a, cs), listing, MAIL_CANCELLED)
    check("cancel-mail", mail and mail["item"] == ITEM_HEAD, f"취소 반환 우편 {mail}")
    check("others-mail", claim(c, mail["id"], cs) == A_NO_MAIL, "남의 우편 수령")
    check("claim-back", claim(a, mail["id"], cs) == A_SUCCESS and head in a.items, "취소 반환 수령")

    # 검색 값 범위(필터는 나중 — slot 0만, 정렬 0~2)·요청 간격
    for name, kw in (("slot-9", dict(slot=9)), ("slot-1", dict(slot=1)), ("sort-5", dict(sort=5))):
        pump_all(cs, a.search_wait)
        n = len(a.aresults)
        a.send_search(**kw)
        wait_all(cs, lambda: len(a.aresults) > n, 5.0, f"{name} 결과")
        check("bad-" + name, a.aresults[-1][:2] == (CS_AUCTION_SEARCH, A_NOT_ALLOWED), f"{name}: {a.aresults[-1]}")
    pump_all(cs, a.search_wait)
    n, l = len(a.aresults), len(a.lists)
    a.send_search(); a.send_search()   # 간격 없이 두 번
    wait_all(cs, lambda: len(a.aresults) > n and len(a.lists) > l, 5.0, "연속 검색")
    check("too-frequent", a.aresults[-1][:2] == (CS_AUCTION_SEARCH, A_TOO_FREQUENT) and len(a.lists) == l + 1, f"연속 검색: 결과 {a.aresults[n:]}, 목록 {len(a.lists) - l}개")

    # 더미 봇: 검색만 된다
    d = Client("auction_dummy", dummy=True).enter()
    everyone = cs + [d]
    got = search(d, "", everyone)
    check("dummy-search", got["page_rows"] == 10, f"더미 봇 검색 {got['page_rows']}")
    pump_all(everyone, 1.1)
    n = len(d.aresults)
    d.send_my()
    wait_all(everyone, lambda: len(d.aresults) > n, 5.0, "더미 내 목록 결과")
    check("dummy-my", d.aresults[-1][:2] == (CS_AUCTION_MY, A_NOT_ALLOWED), f"더미 내 목록 {d.aresults[-1]}")
    check("dummy-buy", buy(d, 1, everyone) == A_NOT_ALLOWED, "더미 구매")
    list_matches_db(a, everyone)
    close_all(everyone)


def t_dungeon():
    a, = cs = login("Test1")
    to_field(a)
    dt.cooldown()
    dt.enter_dungeon(a)
    check("in-dungeon", a.kind == dt.KIND_DUNGEON, f"던전이 아님(kind {a.kind})")
    for name, send, request in (("search", lambda: a.send_search(), CS_AUCTION_SEARCH), ("my", a.send_my, CS_AUCTION_MY), ("buy", lambda: a.send_buy(1), CS_AUCTION_BUY)):
        pump_all(cs, GAP)
        n = len(a.aresults)
        send()
        wait_all(cs, lambda: len(a.aresults) > n, 5.0, f"던전 안 {name} 결과")
        check("dungeon-" + name, a.aresults[-1][:2] == (request, A_IN_DUNGEON), f"던전 안 {name}: {a.aresults[-1]}")
    check("no-list", not a.lists and not a.mys, "던전 안에서 목록이 옴")
    dt.cooldown()
    dt.exit_dungeon(a)
    check("outside", search(a, "", cs)["page_rows"] == 10, "던전에서 나온 뒤 검색")
    close_all(cs)


def t_search():
    # AuctionFetchRows 4, AuctionPageRows 2 — 묶음이 작아 커서로 여러 번 이어 받는다
    a, b = cs = login("Test1", "Test2")
    hp = stack_row(a, ITEM_POTION, 18, cs)
    mp = stack_row(a, ITEM_MP, 7, cs)
    heads = [give_one(a, ITEM_HEAD, cs) for _ in range(2)]
    top = give_one(a, ITEM_TOP, cs)
    need_gold(b, 1000, cs)
    to_npc(a, others=[b])
    # (행, 수량, 1개당) — 1개당 같은 값(20)·총액 같은 값(30·20 vs …)이 섞이게
    plan = [(hp, 5, 30), (hp, 2, 10), (hp, 10, 20), (hp, 1, 20), (mp, 3, 15), (mp, 4, 25), (heads[0], 1, 300), (heads[1], 1, 100), (top, 1, 200)]
    mine = []
    for r, n, unit in plan:
        result, listing = register(a, r, n, unit, cs)
        check("register", result == A_SUCCESS, f"등록 {result}")
        mine.append(listing)

    got = search(b, "", cs)
    check("echo", got["page_rows"] == 2 and len(got["listings"]) <= 4 and got["keyword"] == "" and got["query"] == Q_SEARCH, f"응답 {got}")
    # 이름 검색: 부분 일치·공백·영문 대소문자 무시·없는 이름
    for kw in ("hp", " Hp 물 ", "물약", "MP", "두건", "천"):
        listings, total = search_all(b, kw, cs)
        want = expected_order(kw, SORT_UNIT, False)
        check("keyword", [l["id"] for l in listings] == want and total == len(want), f"'{kw}': 받은 {[l['id'] for l in listings]} 기대 {want} (total {total})")
    got = search(b, "없는이름xyz", cs)
    check("no-match", got["total"] == 0 and not got["listings"] and not got["has_more"], f"없는 이름 {got}")

    # 정렬 3종 × 방향 — DB 순서와 같고, 키 값이 정렬 값과 같다
    for kw in ("", "물약"):
        for sort in (SORT_UNIT, SORT_TOTAL, SORT_REMAIN):
            for desc in (False, True):
                listings, total = search_all(b, kw, cs, sort, desc)
                want = expected_order(kw, sort, desc)
                ids = [l["id"] for l in listings]
                check("order", ids == want and len(set(ids)) == len(ids) == total, f"'{kw}' sort {sort} desc {desc}: 받은 {ids} 기대 {want}")
    got = search(b, "", cs, SORT_TOTAL)
    check("keys", got["keys"] == [l["price"] for l in got["listings"]], f"총액 정렬 키 {got['keys']} != 가격 {[l['price'] for l in got['listings']]}")

    # 커서: 첫 묶음을 받은 뒤 더 싼 등록이 새로 오고 다음 묶음의 등록 하나가 팔려도 겹치거나 빠지지 않는다
    first = search(b, "", cs, SORT_UNIT, False)
    first_ids = [l["id"] for l in first["listings"]]
    cursor = (first["keys"][-1], first_ids[-1])
    rest_before = [x for x in expected_order("", SORT_UNIT, False) if x not in first_ids]
    cheap = give_one(a, ITEM_TOP, cs)
    result, cheap_id = register(a, cheap, 1, 1, cs)                   # 1개당 1 — 커서보다 앞
    check("cheap", result == A_SUCCESS, f"등록 {result}")
    sold = [x for x in rest_before if x in mine][0]
    check("sold-mid", buy(b, sold, cs, db_count(sold)) == A_SUCCESS, "다음 묶음의 등록 구매")
    rest = []
    got = search(b, "xyz-ignored", cs, SORT_UNIT, False, Q_MORE, cursor)
    rest.extend(got["listings"])
    check("more-keyword", got["keyword"] == "" and got["query"] == Q_MORE, f"MORE가 요청의 검색어를 썼음: {got['keyword']}")
    while got["has_more"]:
        got = search(b, "", cs, SORT_UNIT, False, Q_MORE, (got["keys"][-1], got["listings"][-1]["id"]))
        rest.extend(got["listings"])
    rest_ids = [l["id"] for l in rest]
    check("cursor", rest_ids == [x for x in rest_before if x != sold] and cheap_id not in rest_ids and not set(rest_ids) & set(first_ids),
          f"커서 뒤: 받은 {rest_ids} 기대 {[x for x in rest_before if x != sold]} (새 등록 {cheap_id}, 팔린 {sold})")

    # SORT도 요청의 검색어를 무시하고 기억한 것("물약")을 쓴다
    search(b, "물약", cs)
    got = search(b, "두건", cs, SORT_TOTAL, True, Q_SORT)
    check("sort-keyword", got["keyword"] == "물약" and all(l["item"] in (ITEM_POTION, ITEM_MP, 8003, 8004) for l in got["listings"]), f"SORT 응답 {got['keyword']} {[l['item'] for l in got['listings']]}")
    list_matches_db(a, cs)
    close_all(cs)


def t_interval():
    # 실제 간격: 검색 5000ms, 그 밖 1000ms(정렬·다음 묶음·구매·등록·취소·수령이 함께, 내 목록은 따로)
    a, = cs = login("Test1")
    pump_all(cs, 1.0)
    got = search(a, "", cs)
    check("values", got["search_ms"] == 5000 and got["action_ms"] == 1000 and got["page_rows"] == 10, f"간격 값 {got['search_ms']}·{got['action_ms']}·{got['page_rows']}")
    t0 = time.time()

    def raw(send, request):
        n, l, m = len(a.aresults), len(a.lists), len(a.mys)
        send()
        wait_all(cs, lambda: len(a.aresults) > n or len(a.lists) > l or len(a.mys) > m, 5.0, f"{request} 응답")
        return A_TOO_FREQUENT if len(a.aresults) > n and a.aresults[-1][1] == A_TOO_FREQUENT else A_SUCCESS

    check("search-again", raw(lambda: a.send_search(), CS_AUCTION_SEARCH) == A_TOO_FREQUENT, "5초 안 검색이 받아짐")
    check("sort-ok", raw(lambda: a.send_search(Q_SORT, sort=SORT_TOTAL), CS_AUCTION_SEARCH) == A_SUCCESS, "검색 직후 정렬이 막힘(간격이 따로여야 함)")
    check("sort-again", raw(lambda: a.send_search(Q_SORT), CS_AUCTION_SEARCH) == A_TOO_FREQUENT, "1초 안 정렬이 받아짐")
    # 내 목록은 같은 길이의 간격을 따로 잰다(AU2b — 자동으로 나가는 내 목록 요청이 등록·구매를 막지 않게)
    check("my-own", raw(a.send_my, CS_AUCTION_MY) == A_SUCCESS, "정렬 직후 내 목록이 막힘(내 목록은 간격이 따로여야 함)")
    check("my-again", raw(a.send_my, CS_AUCTION_MY) == A_TOO_FREQUENT, "1초 안 내 목록이 또 받아짐")
    pump_all(cs, 1.1)
    check("more-ok", raw(lambda: a.send_search(Q_MORE, cursor=(1, 1)), CS_AUCTION_SEARCH) == A_SUCCESS, "1초 뒤 다음 묶음")
    pump_all(cs, 1.1)
    check("my-ok", raw(a.send_my, CS_AUCTION_MY) == A_SUCCESS, "1초 뒤 내 목록")
    check("sort-after-my", raw(lambda: a.send_search(Q_SORT), CS_AUCTION_SEARCH) == A_SUCCESS, "내 목록 직후 정렬이 막힘(간격이 따로여야 함)")
    check("search-still", time.time() - t0 < 4.5 and raw(lambda: a.send_search(), CS_AUCTION_SEARCH) == A_TOO_FREQUENT, "5초 전 검색이 받아짐")
    pump_all(cs, max(0.0, 5.2 - (time.time() - t0)))
    check("search-ok", raw(lambda: a.send_search(), CS_AUCTION_SEARCH) == A_SUCCESS, "5초 뒤 검색")
    close_all(cs)


def t_expire():
    # AuctionDurationSec 6, AuctionExpireCheckSec 1
    a, b = cs = login("Test1", "Test2")
    rows = [give_one(a, ITEM_HEAD, cs) for _ in range(8)]
    prow = stack_row(a, ITEM_POTION, 5, cs)
    need_gold(b, 2000, cs)
    to_npc(a, others=[b])
    # 취소 반환
    result, listing = register(a, rows[0], 1, 100, cs)
    check("register", result == A_SUCCESS, f"등록 {result}")
    check("cancel", cancel(a, listing, cs) == A_SUCCESS and rows[0] not in a.items, "취소")
    mail = find_mail(my(a, cs), listing, MAIL_CANCELLED)
    check("cancel-mail", mail is not None, "취소 반환 우편이 없음")
    check("cancel-claim", claim(a, mail["id"], cs) == A_SUCCESS and rows[0] in a.items, "취소 반환 수령")

    # 만료 반환: 6초 뒤 목록에서 사라지고 우편(종류 3)이 온다. 물약 5개 중 2개가 팔린 등록은 남은 3개가 돌아온다
    result, listing = register(a, rows[0], 1, 100, cs)
    t0 = time.time()
    check("register2", result == A_SUCCESS, f"등록 {result}")
    result, plisting = register(a, prow, 5, 10, cs)
    check("register-p", result == A_SUCCESS, f"등록 {result}")
    check("buy-2", buy(b, plisting, cs, 2) == A_SUCCESS, "물약 2개 구매")
    seen = [l for l in search_all(b, "", cs)[0] if l["id"] == listing]
    check("remain", seen and seen[0]["remain"] <= 6, f"남은 시간 {seen}")
    wait_all(cs, lambda: time.time() - t0 > 8.5, 12.0, "만료 대기")
    check("expired-buy", buy(b, listing, cs) == A_ALREADY_CLOSED, "만료된 등록 구매가 ALREADY_CLOSED가 아님")
    check("expired-gone", not [l for l in search_all(b, "", cs)[0] if l["id"] in (listing, plisting)], "만료된 등록이 검색에 남음")
    view = my(a, cs)
    mail = find_mail(view, listing, MAIL_EXPIRED)
    check("expire-mail", mail and mail["item"] == ITEM_HEAD and not [l for l in view["listings"] if l["id"] == listing], f"만료 반환 우편 {view['mails']}")
    pmail = find_mail(view, plisting, MAIL_EXPIRED)
    check("expire-partial", pmail and pmail["count"] == 3 and db_state(plisting) == 3 and db_count(plisting) == 3, f"부분 판매 뒤 만료 반환 {pmail}")
    check("db-expired", db_state(listing) == 3, f"DB 상태 {db_state(listing)}")
    check("expire-claim", claim(a, mail["id"], cs) == A_SUCCESS and rows[0] in a.items, "만료 반환 수령")

    # 만료 시각 언저리의 구매: 등록마다 팔림(2)·만료(3) 가운데 하나로만 끝난다
    listings = []
    t0 = time.time()
    for row in rows[1:]:
        result, lid = register(a, row, 1, 10, cs)
        check("register3", result == A_SUCCESS, f"등록 {result}")
        listings.append(lid)
    wait_all(cs, lambda: time.time() - t0 > 5.6, 8.0, "만료 직전까지 대기")
    outcomes = {}
    for lid in listings:   # 하나씩 — 앞의 것은 만료 전, 뒤의 것은 만료 뒤에 걸린다
        outcomes[lid] = buy(b, lid, cs)
        pump_all(cs, 0.1)
    wait_all(cs, lambda: time.time() - t0 > 13.0 + len(listings), 30.0, "나머지 만료 대기")
    sold = expired = 0
    for lid in listings:
        state = db_state(lid)
        check("final", (outcomes[lid] == A_SUCCESS and state == 2) or (outcomes[lid] in (A_ALREADY_CLOSED, A_NO_LISTING, A_SOLD) and state == 3), f"등록 {lid}: 구매 결과 {outcomes[lid]}, DB 상태 {state}")
        sold += state == 2
        expired += state == 3
    check("gold", b.gold == 2000 - 20 - 10 * sold, f"구매자 골드 {b.gold}, 산 수 {sold}")
    print(f"  expire: 팔림 {sold}, 만료 {expired}")
    check("db", db_broken() == 0, f"경매 데이터가 어긋남: {db_broken()}행")
    list_matches_db(a, cs)
    close_all(cs)


def t_limits():
    # AuctionMaxListings 2, GoldMax 3000, AuctionMaxPrice 2500
    a, b = cs = login("Test1", "Test2")
    rows = [give_one(a, ITEM_HEAD, cs) for _ in range(3)]
    need_gold(b, 2500, cs)
    to_npc(a, b)
    first = register(a, rows[0], 1, 2500, cs)
    second = register(a, rows[1], 1, 10, cs)
    check("two", first[0] == A_SUCCESS and second[0] == A_SUCCESS, f"등록 {first}·{second}")
    check("limit", register(a, rows[2], 1, 10, cs)[0] == A_LISTING_LIMIT and rows[2] in a.items, "세 번째 등록이 LISTING_LIMIT이 아님")
    check("over-max-price", register(a, rows[2], 1, 2501, cs)[0] == A_BAD_PRICE, "AuctionMaxPrice 초과")
    # 하나가 팔리면 다시 등록할 수 있다
    check("buy", buy(b, first[1], cs) == A_SUCCESS, "구매")
    third = register(a, rows[2], 1, 10, cs)
    check("after-sold", third[0] == A_SUCCESS, f"팔린 뒤 등록 {third}")

    # 골드 상한: a의 골드 1000 + 대금 2500 > 3000 → GOLD_FULL, 바뀐 것 없음
    mail = find_mail(my(a, cs), first[1], MAIL_SALE)
    gold_a = a.gold
    check("gold-full", mail and claim(a, mail["id"], cs) == A_GOLD_FULL and a.gold == gold_a, f"골드 상한: 우편 {mail}, 골드 {gold_a} -> {a.gold}")
    check("still-there", find_mail(my(a, cs), first[1], MAIL_SALE) is not None, "거부된 우편이 사라짐")

    # 인벤토리 가득: b의 빈 칸을 모두 채운 뒤 구매품 수령 → INVENTORY_FULL. 골드 우편은 가득 차도 받아진다
    free = b.free_slots()
    for _ in range(free):
        give_one(b, ITEM_TOP, cs)
    check("full", b.free_slots() == 0, f"빈 칸 {b.free_slots()}")
    item_mail = find_mail(my(b, cs), first[1], MAIL_BOUGHT)
    check("inv-full", item_mail and claim(b, item_mail["id"], cs) == A_INVENTORY_FULL, "가득 찬 인벤토리로 아이템 수령")
    # b가 등록해 a가 사면 b에게 골드 우편이 생긴다(b는 가득 찬 상태)
    some = sorted(b.bag(ITEM_TOP))[0]
    sale = register(b, some, 1, 20, cs)
    check("b-register", sale[0] == A_SUCCESS, f"b 등록 {sale}")
    check("a-buy", buy(a, sale[1], cs) == A_SUCCESS, "a 구매")
    # 방금 등록으로 한 칸이 비었으니 다시 채운다
    give_one(b, ITEM_TOP, cs)
    gold_b = b.gold
    gold_mail = find_mail(my(b, cs), sale[1], MAIL_SALE)
    check("gold-when-full", gold_mail and b.free_slots() == 0 and claim(b, gold_mail["id"], cs) == A_SUCCESS and b.gold == gold_b + 20, f"가득 찬 상태의 골드 수령: {gold_b} -> {b.gold}")
    check("db", db_broken() == 0, f"경매 데이터가 어긋남: {db_broken()}행")
    list_matches_db(a, cs)
    close_all(cs)


def t_fail():
    # DbItemFailEvery 2: 아이템 트랜잭션을 두 번에 한 번 일부러 롤백한다(개발 지급 포함)
    a, b = cs = login("Test1", "Test2")

    def give_retry(c, item_id, count):
        """롤백되면 SC_ITEM_RESULT(DB_ERROR)가 온다 — 될 때까지 다시 준다."""
        for _ in range(4):
            c.pump(GAP)
            before, n, r = set(c.items), len(c.inv_updates), len(c.item_results)
            c.send(mk.CS_DEV_GIVE, rs.f_int(1, item_id) + rs.f_int(2, count))
            wait_all(cs, lambda: len(c.inv_updates) > n or len(c.item_results) > r, 6.0, "지급 결과")
            if len(c.inv_updates) > n:
                GIVEN["gold" if item_id == 0 else "items"] += count
                return sorted(set(c.items) - before)
        raise AssertionError("지급이 계속 실패")

    head = give_retry(a, ITEM_HEAD, 1)[0]
    if b.gold < 500:
        give_retry(b, 0, 500 - b.gold)
    to_npc(a, b)

    def until_ok(fn, what):
        """DB_ERROR면 바뀐 것이 없어야 하고, 다시 하면 성공해야 한다. (성공 결과, DB_ERROR 횟수)"""
        errors = 0
        for _ in range(8):
            got = fn()
            code = got[0] if isinstance(got, tuple) else got
            if code == A_SUCCESS:
                return got, errors
            if code == A_LISTING_BUSY:
                # 롤백 직후에는 서비스가 그 등록의 DB 상태를 다시 읽는 동안 처리 중으로 남는다 — 곧 풀린다
                pump_all(cs, 0.3)
                continue
            check(what, code == A_DB_ERROR, f"{what}: 결과 {code}")
            errors += 1
        raise AssertionError(f"{what}: 계속 실패")

    errors_total = 0
    for _ in range(4):
        result, listing = register(a, head, 1, 100, cs)
        if result == A_SUCCESS:
            break
        check("register-rollback", result == A_DB_ERROR and head in a.items, f"롤백된 등록: 결과 {result}, 행이 인벤토리에 {'있음' if head in a.items else '없음'}")
        errors_total += 1
    check("listed", result == A_SUCCESS and listing > 0 and head not in a.items, "등록 뒤 상태")
    gold_b = b.gold
    _, errors = until_ok(lambda: buy(b, listing, cs), "구매")   # 롤백된 뒤 DB를 다시 읽어 수량이 맞춰져야 다시 살 수 있다
    errors_total += errors
    check("paid-once", b.gold == gold_b - 100, f"구매자 골드 {gold_b} -> {b.gold}")
    mail = find_mail(my(b, cs), listing, MAIL_BOUGHT)
    check("mail", mail is not None, "구매품 우편이 없음")
    _, errors = until_ok(lambda: claim(b, mail["id"], cs), "수령")
    errors_total += errors
    check("got", head in b.items, "수령한 행이 없음")
    sale = find_mail(my(a, cs), listing, MAIL_SALE)
    gold_a = a.gold
    _, errors = until_ok(lambda: claim(a, sale["id"], cs), "대금 수령")
    errors_total += errors
    check("sale-once", a.gold == gold_a + 100, f"판매자 골드 {gold_a} -> {a.gold}")
    check("saw-rollback", errors_total >= 1, "롤백이 한 번도 일어나지 않음(DbItemFailEvery 설정 확인)")
    print(f"  fail: DB_ERROR {errors_total}번 뒤 모두 성공")
    check("db", db_broken() == 0, f"경매 데이터가 어긋남: {db_broken()}행")
    list_matches_db(a, cs)
    close_all(cs)


def t_busy():
    # AuctionDbMaxInFlight 1, DbSaveDelayMs 1500: a의 등록이 DB에 가 있는 동안 b의 요청은 TRY_LATER
    a, b = cs = login("Test1", "Test2")
    head = give_one(a, ITEM_HEAD, cs)
    to_npc(a, b)
    pump_all(cs, 1.1)
    na, nb, mb = len(a.aresults), len(b.aresults), len(b.mys)
    a.send_register(head, 1, 100)
    pump_all(cs, 0.3)
    b.send_my()
    pump_all(cs, 0.3)
    b.send_buy(999999999999)
    wait_all(cs, lambda: len(b.aresults) >= nb + 2, 5.0, "b의 TRY_LATER 둘")
    check("try-later", [r[:2] for r in b.aresults[nb:nb + 2]] == [(CS_AUCTION_MY, A_TRY_LATER), (CS_AUCTION_BUY, A_TRY_LATER)] and len(b.mys) == mb, f"b 결과 {b.aresults[nb:]}")
    check("a-pending", len(a.aresults) == na, "a의 등록이 벌써 끝남(지연 설정 확인)")
    wait_all(cs, lambda: len(a.aresults) > na, 8.0, "a의 등록 결과")
    check("a-ok", a.aresults[-1][:2] == (CS_AUCTION_REGISTER, A_SUCCESS), f"a 결과 {a.aresults[-1]}")
    listing = a.aresults[-1][2]
    # 끝난 뒤에는 받아진다
    view = my(b, cs)
    check("after", view is not None, "끝난 뒤의 내 목록")
    check("cancel", cancel(a, listing, cs) == A_SUCCESS, "취소")
    mail = find_mail(my(a, cs), listing, MAIL_CANCELLED)
    check("claim", claim(a, mail["id"], cs) == A_SUCCESS, "수령")
    close_all(cs)


def relogin(account, limit=10.0):
    start = time.time()
    while True:
        try:
            c = Client(account)
            c.enter()
            return c, time.time() - start
        except Exception:
            if time.time() - start > limit:
                raise
            time.sleep(0.3)


def t_cut():
    # DbSaveDelayMs 1500: 요청이 DB에 가 있는 동안 접속을 끊는다
    a, b = cs = login("Test1", "Test2")
    head = give_one(a, ITEM_HEAD, cs)
    need_gold(b, 500, cs)
    to_npc(a, others=[b])
    pump_all(cs, 1.1)
    a.send_register(head, 1, 100)
    time.sleep(0.3)
    a.close(rst=True)
    a2, took = relogin("Test1")
    cs = [a2, b]
    check("relogin", took < 8.0, f"다시 로그인에 {took:.1f}초(정리 신호가 늦거나 빠짐)")
    check("escrowed", head not in a2.items, "끊긴 등록의 아이템이 인벤토리에 남음")
    listing = int(mk.db_scalar(f"SELECT ListingId FROM Auction_Listing WHERE ItemDbId = {head} AND State = 1") or 0)
    check("db-listed", listing > 0, "DB에 등록이 없음")
    seen = [l for l in search_all(b, "", cs)[0] if l["id"] == listing]
    check("in-list", seen and seen[0]["unit"] == 100, "서비스 목록에 없음")

    # 구매 요청 직후 구매자가 끊김
    gold_b = b.gold
    pump_all(cs, 1.1)
    b.send_buy(listing)
    time.sleep(0.3)
    b.close(rst=True)
    b2, took = relogin("Test2")
    cs = [a2, b2]
    check("relogin-b", took < 8.0, f"다시 로그인에 {took:.1f}초")
    check("paid", b2.gold == gold_b - 100, f"끊긴 구매의 골드 {gold_b} -> {b2.gold}")
    check("db-sold", db_state(listing) == 2, f"DB 상태 {db_state(listing)}")
    to_npc(b2, others=[a2])
    mail = find_mail(my(b2, cs), listing, MAIL_BOUGHT)
    check("claim", mail and claim(b2, mail["id"], cs) == A_SUCCESS and head in b2.items, "끊긴 구매의 구매품 수령")
    check("db", db_broken() == 0, f"경매 데이터가 어긋남: {db_broken()}행")
    list_matches_db(a2, cs)
    close_all(cs)


def t_movewait():
    # DbSaveDelayMs 1500: 구매가 DB에 가 있는 동안 문 포털을 쓰면 이동이 결과를 기다린다
    a, b = cs = login("Test1", "Test2")
    head = give_one(a, ITEM_HEAD, cs)
    need_gold(b, 500, cs)
    to_npc(a, others=[b])
    result, listing = register(a, head, 1, 100, cs)
    check("register", result == A_SUCCESS, f"등록 {result}")
    pid = mt.to_portal_101(b)
    pump_all(cs, 0.3)
    b.order.clear()
    n, r = len(b.room_changes), len(b.aresults)
    b.send_buy(listing)
    time.sleep(0.2)
    b.use_portal(pid)
    wait_all(cs, lambda: len(b.room_changes) > n and len(b.aresults) > r, 10.0, f"구매 결과와 Room 이동 (이동 결과 {b.move_results})")
    check("bought", b.aresults[-1][:2] == (CS_AUCTION_BUY, A_SUCCESS), f"구매 결과 {b.aresults[-1]}")
    check("order", b.order.index(SC_AUCTION_RESULT) < b.order.index(mk.SC_ROOM_CHANGE), "구매 결과보다 Room 이동이 먼저 옴")
    check("field", b.kind == mk.KIND_FIELD, "사냥터가 아님")
    close_all(cs)


def t_restart1():
    a, b = cs = login("Test1", "Test2")
    rows = [give_one(a, ITEM_HEAD, cs), give_one(a, ITEM_TOP, cs), give_one(a, ITEM_HEAD, cs)]
    need_gold(b, 500, cs)
    to_npc(a, others=[b])
    sold = register(a, rows[0], 1, 120, cs)      # 팔려서 우편 두 통이 남는다
    kept = register(a, rows[1], 1, 777, cs)      # 판매 중으로 남는다
    late = register(a, rows[2], 1, 55, cs)       # 꺼져 있는 동안 기간이 지난다(실행기가 ExpireAt을 과거로)
    check("register", sold[0] == kept[0] == late[0] == A_SUCCESS, f"등록 {sold}·{kept}·{late}")
    check("buy", buy(b, sold[1], cs) == A_SUCCESS, "구매")
    os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
    json.dump({"sold": sold[1], "kept": kept[1], "late": late[1], "rows": rows, "gold_a": a.gold}, open(STATE_FILE, "w"))
    close_all(cs)


def restart_sql():
    """서버가 꺼져 있는 동안 실행기가 부른다: late 등록의 기간을 지나게 한다."""
    s = json.load(open(STATE_FILE))
    mk.db_scalar(f"UPDATE Auction_Listing SET ExpireAt = DATEADD(SECOND, -30, SYSUTCDATETIME()) WHERE ListingId = {s['late']}; SELECT 1")


def t_restart2():
    s = json.load(open(STATE_FILE))
    a, b = cs = login("Test1", "Test2")
    # 판매 중이던 등록은 그대로 검색된다
    seen = [l for l in search_all(b, "상의", cs)[0] if l["id"] == s["kept"]]
    check("kept", seen and seen[0]["unit"] == 777 and seen[0]["item"] == ITEM_TOP and seen[0]["remain"] > 80000, f"재시작 뒤 검색 {seen}")
    # 꺼져 있던 동안 기간이 지난 등록은 첫 검사에서 만료 반환됐다
    check("late-gone", not [l for l in search_all(b, "", cs)[0] if l["id"] == s["late"]] and db_state(s["late"]) == 3, f"밀린 만료: DB 상태 {db_state(s['late'])}")
    to_npc(a, b)
    view = my(a, cs)
    check("my-kept", [l["id"] for l in view["listings"] if l["id"] in (s["sold"], s["kept"], s["late"])] == [s["kept"]], f"내 등록 {view['listings']}")
    sale, back = find_mail(view, s["sold"], MAIL_SALE), find_mail(view, s["late"], MAIL_EXPIRED)
    check("mails", sale and sale["gold"] == 120 and back and back["item"] == ITEM_HEAD, f"수령함 {view['mails']}")
    check("claim-sale", claim(a, sale["id"], cs) == A_SUCCESS and a.gold == s["gold_a"] + 120, f"대금 수령: 골드 {a.gold}")
    check("claim-back", claim(a, back["id"], cs) == A_SUCCESS and s["rows"][2] in a.items, "만료 반환 수령")
    item = find_mail(my(b, cs), s["sold"], MAIL_BOUGHT)
    check("claim-item", item and claim(b, item["id"], cs) == A_SUCCESS and s["rows"][0] in b.items, "구매품 수령")
    # 남아 있던 등록도 재시작 뒤에 팔린다
    need_gold(b, 800, cs)
    check("buy-kept", buy(b, s["kept"], cs) == A_SUCCESS, "재시작 전 등록 구매")
    check("db", db_broken() == 0, f"경매 데이터가 어긋남: {db_broken()}행")
    list_matches_db(a, cs)
    close_all(cs)


CASES = {"basic": t_basic, "partial": t_partial, "partbuy": t_partbuy, "race": t_race, "cancelrace": t_cancelrace, "rejects": t_rejects,
         "dungeon": t_dungeon, "search": t_search, "interval": t_interval, "expire": t_expire, "limits": t_limits, "fail": t_fail,
         "busy": t_busy, "cut": t_cut, "movewait": t_movewait, "restart1": t_restart1, "restart2": t_restart2}

if __name__ == "__main__":
    if sys.argv[1:] == ["restart_sql"]:
        restart_sql()
        sys.exit(0)
    failed = 0
    for name in sys.argv[1:]:
        GIVEN["items"] = GIVEN["gold"] = 0
        items0, gold0 = totals()
        try:
            CASES[name]()
            time.sleep(3.0)   # 접속 정리(마지막 저장)가 끝나야 같은 계정으로 바로 다시 로그인된다
            items1, gold1 = totals()
            # 경매는 아이템·골드를 옮길 뿐이다: 전체 합계(인벤토리·장착·경매 보관·수령함 아이템, 보유 골드 + 수령함 골드)는 지급한 만큼만 늘어야 한다.
            if items1 != items0 + GIVEN["items"] or gold1 != gold0 + GIVEN["gold"]:
                raise AssertionError(f"DB 합계가 다름: 아이템 {items0} + 지급 {GIVEN['items']} -> {items1}, 골드 {gold0} + 지급 {GIVEN['gold']} -> {gold1}")
            broken = db_broken()
            if broken:
                raise AssertionError(f"경매 데이터가 어긋남: {broken}행")
            print(f"PASS {name}  (DB 아이템 {items1}, 골드 {gold1})")
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: {type(e).__name__}: {e}")
            time.sleep(3.0)
        sys.stdout.flush()
    sys.exit(1 if failed else 0)
