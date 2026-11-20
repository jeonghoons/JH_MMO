# DG3b Room 이동 시험(원시 소켓, 계정 Test1~Test4). 사용: python -I -X utf8 move_test.py <시나리오> [...]
# 서버는 run_move_tests.sh가 시나리오에 맞는 [Debug] 설정으로 띄운다(DevCommands=true 필요). 판정 줄은 "PASS <이름>" / "FAIL <이름>: 이유".
#   basic     마을 입장(몬스터 안 보임·마을 포털 3개) → 문 포털 → 사냥터(SC_ROOM_CHANGE → 내 SC_ADD_OBJECT → SC_PORTAL_LIST 순서) → 되돌아옴, 중복 요청 5번은 이동 1번
#   starts    로그인 8번의 시작 지점이 4곳에 고르게 나뉨(지점마다 2번, 반경 3m 안)
#   notavail  갈 수 없는 포털(던전 입구 100 — 사냥터 Room에서)은 SC_MOVE_RESULT NOT_AVAILABLE
#   wait      (DbSaveDelayMs 1500) 골드 지급 직후 포털: 지급 결과가 먼저 오고 그 뒤 이동, 대기 중 지급은 BUSY, 골드는 정확히 +1
#   timeout   (DbSaveDelayMs 7000) 지급 직후 포털: 5초 뒤 TIMEOUT, 마을에 남음, 지급은 반영, 그 뒤 포털은 정상
#   cut_wait  (DbSaveDelayMs 1500) 지급 + 포털 뒤 대기 중에 접속을 끊음 → 곧바로 다시 로그인되고 골드 +1 반영
#   cut_a     (MoveHandoffDelayMs 800) 포털 요청 0.3초 뒤(서비스 → 떠나기 사이) 접속을 끊음 → 곧바로 다시 로그인됨
#   cut_b     (MoveHandoffDelayMs 800) 포털 요청 1.2초 뒤(떠나기 → 들어오기 사이) 접속을 끊음 → 곧바로 다시 로그인됨
#   trade     Test1·Test2가 같은 포털 앞에서 거래를 연 뒤 Test1이 포털 → 양쪽 SC_TRADE_END(MOVED), Test2는 마을에 남음
#   revive    (DummyRoom=field, DummySpawnZone=보스 옆) 더미가 사냥터에서 죽은 뒤 부활 요청 → 마을 Room(SC_ROOM_CHANGE → 내 SC_ADD_OBJECT → SC_PORTAL_LIST → SC_REVIVE), 시작 지점 근처, 다시 포털로 나감
import math, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # -I 는 스크립트 폴더를 경로에 넣지 않는다
import movekit as mk

STARTS = [(-14200, 0), (-9800, 0), (-14300, 5200), (-14700, -5000)]
RESULTS = []


def check(name, cond, why=""):
    if not cond:
        raise AssertionError(why or name)


def relogin_time(account, limit=25.0):
    """끊은 직후 같은 계정으로 다시 로그인해 걸린 시간을 잰다(이전 접속의 정리가 빠지면 LoginCloseForceSec 30초 가까이 걸린다)."""
    t0 = time.time()
    c = mk.Client(account)
    took = time.time() - t0
    c.enter()
    return c, took


def to_field(c):
    pid = c.goto_portal()
    n = len(c.room_changes)
    c.use_portal(pid)
    c.pump_until(lambda: len(c.room_changes) > n, 8.0, "SC_ROOM_CHANGE(사냥터)")
    c.pump(0.6)
    return pid


def t_basic():
    c = mk.Client("Test1").enter()
    check("town", not c.monsters(), f"마을에서 몬스터가 보임: {len(c.monsters())}")
    check("portals", sorted(c.portals) == [101, 102, 103], f"마을 포털 목록 {sorted(c.portals)}")
    near = min(math.hypot(c.x - sx, c.y - sy) for sx, sy in STARTS)
    check("start", near <= 450, f"시작 위치가 시작 지점에서 {near:.0f}cm")
    pid = c.goto_portal()
    seq0 = c.seq
    c.log.clear()
    for _ in range(5):
        c.use_portal(pid)       # 같은 요청 5번
    c.pump_until(lambda: c.room_changes, 8.0, "SC_ROOM_CHANGE")
    c.pump(1.5)
    check("once", len(c.room_changes) == 1, f"이동 {len(c.room_changes)}번(1번이어야 함)")
    t, kind, x, y, seq = c.room_changes[0]
    check("kind", kind == mk.KIND_FIELD, f"kind {kind}")
    check("seq", seq == seq0 + 1, f"보정 순번 {seq0} -> {seq}")
    order = [p for _, p in c.log if p in (mk.SC_ROOM_CHANGE, mk.SC_ADD_OBJECT, mk.SC_PORTAL_LIST)]
    check("order", order[:3] == [mk.SC_ROOM_CHANGE, mk.SC_ADD_OBJECT, mk.SC_PORTAL_LIST], f"패킷 순서 {order[:4]}")
    check("fportals", sorted(c.portals) == [201, 202, 203], f"사냥터 포털 목록 {sorted(c.portals)}")
    tx, ty = c.portals[pid + 100][0], c.portals[pid + 100][1]
    check("pos", math.hypot(x - tx, y - ty) < 10, f"도착 위치 ({x:.0f},{y:.0f}) != 포털 {pid + 100} ({tx:.0f},{ty:.0f})")
    # 사냥터에서 이동이 받아들여지는지(보정 순번이 이어지는지): 조금 걷고, 되돌아온다
    c.walk_to(c.x + 300, c.y, stop_cm=10)
    c.walk_to(tx, ty, stop_cm=50)
    time.sleep(1.1)   # 포털 재사용 대기
    c.use_portal(pid + 100)
    c.pump_until(lambda: len(c.room_changes) == 2, 8.0, "SC_ROOM_CHANGE(마을)")
    c.pump(0.8)
    check("back", c.room_changes[1][1] == mk.KIND_TOWN and sorted(c.portals) == [101, 102, 103] and not c.monsters(), "마을로 돌아온 상태가 다름")
    check("npc", any(o[0] == mk.TYPE_NPC for o in c.objects.values()) or math.hypot(c.x + 14150, c.y + 2200) > 5000, "마을 NPC가 안 보임")
    c.close()


def t_starts():
    seen = []
    clients = []
    for i in range(8):
        c = mk.Client(f"bot_start{i}", dummy=False) if False else mk.Client("bot_raw", dummy=True, tag=f"d{i}").enter()
        clients.append(c)
        idx = min(range(4), key=lambda k: math.hypot(c.x - STARTS[k][0], c.y - STARTS[k][1]))
        dist = math.hypot(c.x - STARTS[idx][0], c.y - STARTS[idx][1])
        seen.append((idx, dist))
    for c in clients:
        c.close()
    counts = [sum(1 for i, _ in seen if i == k) for k in range(4)]
    check("spread", counts == [2, 2, 2, 2], f"시작 지점별 인원 {counts}")
    check("radius", all(d <= 450 for _, d in seen), f"시작 지점에서의 거리 {[round(d) for _, d in seen]}")


def t_notavail():
    c = mk.Client("Test1").enter()
    to_field(c)
    time.sleep(1.1)
    c.use_portal(100)   # 던전 입구(다른 레벨) — 멀리 있어도 사유는 "갈 수 없음"이 먼저다
    c.pump(1.5)
    check("notavail", [r for _, r in c.move_results] == [mk.MOVE_NOT_AVAILABLE], f"SC_MOVE_RESULT {c.move_results}")
    check("stay", len(c.room_changes) == 1, "이동이 일어남")
    c.close()


def t_wait():
    c = mk.Client("Test1").enter()
    pid = c.goto_portal()
    gold0 = c.gold
    c.give_gold(1)
    time.sleep(0.05)
    t0 = time.time()
    c.use_portal(pid)
    time.sleep(0.2)
    c.give_gold(1)      # 대기 중: BUSY여야 한다
    c.pump_until(lambda: c.room_changes, 8.0, "SC_ROOM_CHANGE")
    c.pump(1.0)
    moved_at = c.room_changes[0][0]
    check("gave", len(c.inv_updates) == 1 and c.gold == gold0 + 1, f"골드 {gold0} -> {c.gold}, 갱신 {len(c.inv_updates)}번")
    check("after", c.inv_updates[0][0] <= moved_at, "지급 결과보다 이동이 먼저 옴")
    check("waited", moved_at - t0 >= 1.0, f"이동이 {moved_at - t0:.2f}초 만에 옴(지급을 기다리지 않음)")
    check("busy", [r for _, q, r in c.item_results if q == mk.CS_DEV_GIVE] == [mk.ITEM_BUSY], f"대기 중 지급 응답 {c.item_results}")
    # 도착한 Room에서 아이템 작업이 다시 되는지
    c.give_gold(1)
    c.pump_until(lambda: len(c.inv_updates) == 2, 6.0, "사냥터에서 지급")
    check("field-give", c.gold == gold0 + 2, f"골드 {c.gold}")
    c.close()
    return 2   # 일부러 준 골드


def t_timeout():
    c = mk.Client("Test1").enter()
    pid = c.goto_portal()
    gold0 = c.gold
    c.give_gold(1)
    time.sleep(0.05)
    t0 = time.time()
    c.use_portal(pid)
    c.pump_until(lambda: c.move_results, 9.0, "SC_MOVE_RESULT")
    took = c.move_results[0][0] - t0
    check("timeout", c.move_results[0][1] == mk.MOVE_TIMEOUT and 4.0 <= took <= 6.6, f"결과 {c.move_results[0][1]} {took:.1f}초")
    check("stay", not c.room_changes, "이동이 일어남")
    c.pump_until(lambda: c.inv_updates, 6.0, "지급 결과")
    check("gave", c.gold == gold0 + 1, f"골드 {c.gold}")
    time.sleep(1.1)
    c.use_portal(pid)
    c.pump_until(lambda: c.room_changes, 6.0, "시간 초과 뒤 다시 포털")
    c.close()
    return 1


def cut_case(delay_s, give):
    c = mk.Client("Test1").enter()
    pid = c.goto_portal()
    gold0 = c.gold
    if give:
        c.give_gold(1)
        time.sleep(0.05)
    c.use_portal(pid)
    time.sleep(delay_s)
    c.close(rst=True)
    c2, took = relogin_time("Test1")
    check("relogin", took < 8.0, f"다시 로그인에 {took:.1f}초(정리 신호가 늦거나 빠짐)")
    check("town", sorted(c2.portals) == [101, 102, 103] and not c2.monsters(), "다시 들어온 곳이 마을이 아님")
    if give:
        check("gold", c2.gold == gold0 + 1, f"골드 {gold0} -> {c2.gold}")
    c2.close()
    return 1 if give else 0


# 시작 지점 → 북문 포털 101까지의 꺾임점(cm). 직선으로 가면 가판대·상자에 막혀 서버가 이동을 보정한다.
ROUTE_101 = [
    [],                                        # 광장: 북쪽으로 곧장
    [],                                        # 북문 안쪽
    [(-12000, 5200), (-10500, 1000)],          # 동문 안쪽: 상자 더미 북쪽으로 돌아서
    [(-14700, -1200), (-12500, 0)],            # 서문 안쪽: 가판대 줄 사이 통로로 나와서
]


def to_portal_101(c):
    idx = min(range(4), key=lambda k: math.hypot(c.x - STARTS[k][0], c.y - STARTS[k][1]))
    for wx, wy in ROUTE_101[idx]:
        c.walk_to(wx, wy, stop_cm=60)
    return c.goto_portal(101)


def t_trade():
    a = mk.Client("Test1").enter()
    b = mk.Client("Test2").enter()
    pid = to_portal_101(a)
    b.pump(0.1)
    to_portal_101(b)
    a.pump(0.3); b.pump(0.3)
    check("see", b.id in a.objects, f"Test1이 Test2를 못 봄: a=({a.x:.0f},{a.y:.0f}) b=({b.x:.0f},{b.y:.0f}) a가 보는 플레이어 {list(a.players())} b가 보는 플레이어 {list(b.players())} a.id={a.id} b.id={b.id} a.closed={a.closed}")
    a.send(mk.CS_TRADE_REQUEST, mk.rs.f_int(1, b.id))
    b.pump_until(lambda: b.trade_requests, 5.0, "SC_TRADE_REQUEST")
    b.send(mk.CS_TRADE_RESPOND, mk.rs.f_int(1, 1))
    a.pump(0.6); b.pump(0.3)
    a.use_portal(pid)
    a.pump_until(lambda: a.room_changes, 8.0, "SC_ROOM_CHANGE")
    a.pump(0.5); b.pump(1.0)
    check("a-end", a.trade_ends == [mk.TRADE_MOVED], f"Test1 거래 종료 {a.trade_ends}")
    check("b-end", b.trade_ends == [mk.TRADE_MOVED], f"Test2 거래 종료 {b.trade_ends}")
    check("b-stay", not b.room_changes, "Test2도 이동함")
    check("unseen", a.id not in b.objects and b.id not in a.objects, "다른 Room인데 서로 보임")
    a.close(); b.close()


def t_revive():
    # 서버 설정: DummyRoom=field + DummySpawnZone=보스 옆(run_move_tests.sh의 revive 묶음). 더미는 사냥터 Room의 보스(선공, 인식 12m) 옆에 놓여 곧 맞아 죽는다.
    c = mk.Client("bot_raw", dummy=True, tag="dummy").enter()
    check("field", sorted(c.portals) == [201, 202, 203] and c.monsters(), f"사냥터 Room이 아님: 포털 {sorted(c.portals)} 몬스터 {len(c.monsters())}")
    c.pump_until(lambda: c.dead, 90.0, "보스에게 죽기")
    c.send(mk.CS_REVIVE)   # 대기 시간 전: 버려져야 한다
    c.pump(c.revive_delay / 1000.0 + 0.4)
    check("early", not c.room_changes and c.dead, "대기 시간 전에 부활함")
    c.send(mk.CS_REVIVE)
    c.send(mk.CS_REVIVE)   # 중복 요청
    c.log.clear()
    c.pump_until(lambda: c.room_changes, 8.0, "부활 이동 SC_ROOM_CHANGE")
    c.pump(1.5)
    check("town", c.room_changes[-1][1] == mk.KIND_TOWN, f"kind {c.room_changes[-1][1]}")
    check("once", len(c.room_changes) == 1 and c.revives == 1, f"이동 {len(c.room_changes)}번 부활 {c.revives}번")
    order = [p for _, p in c.log if p in (mk.SC_ROOM_CHANGE, mk.SC_ADD_OBJECT, mk.SC_PORTAL_LIST, mk.SC_REVIVE)]
    check("order", order[:4] == [mk.SC_ROOM_CHANGE, mk.SC_ADD_OBJECT, mk.SC_PORTAL_LIST, mk.SC_REVIVE], f"패킷 순서 {order[:5]}")
    near = min(math.hypot(c.x - sx, c.y - sy) for sx, sy in STARTS)
    check("start", near <= 450 and not c.dead, f"부활 위치가 시작 지점에서 {near:.0f}cm, dead={c.dead}")
    check("portals", sorted(c.portals) == [101, 102, 103] and not c.monsters(), f"포털 {sorted(c.portals)} 몬스터 {len(c.monsters())}")
    # 부활한 뒤 마을에서 움직이고 다시 사냥터로 나갈 수 있는지(보정 순번이 이어지는지)
    to_field(c)
    check("again", c.room_changes[-1][1] == mk.KIND_FIELD, "부활 뒤 포털 이동 실패")
    c.close()


CASES = {
    "basic": t_basic, "starts": t_starts, "notavail": t_notavail, "wait": t_wait, "timeout": t_timeout,
    "cut_wait": lambda: cut_case(0.3, True), "cut_a": lambda: cut_case(0.3, False), "cut_b": lambda: cut_case(1.2, False),
    "trade": t_trade, "revive": t_revive,
}

if __name__ == "__main__":
    failed = 0
    for name in sys.argv[1:]:
        items0, gold0 = mk.db_totals()
        given = 0
        try:
            given = CASES[name]() or 0
            time.sleep(2.5)   # 마지막 저장이 끝나기를 기다린다
            items1, gold1 = mk.db_totals()
            if items1 != items0 or gold1 != gold0 + given:
                raise AssertionError(f"DB 합계가 다름: 아이템 {items0} -> {items1}, 골드 {gold0} -> {gold1} (준 골드 {given})")
            print(f"PASS {name}  (DB 아이템 {items1}, 골드 {gold0} + {given} = {gold1})")
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: {type(e).__name__}: {e}")
        time.sleep(1.0)
    sys.exit(1 if failed else 0)
