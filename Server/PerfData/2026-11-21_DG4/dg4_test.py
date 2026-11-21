# DG4 던전 맵 시험(원시 소켓, 계정 Test1~Test4). 사용: python -I -X utf8 dg4_test.py <시나리오> [...]  (서버는 run_dg4_tests.sh가 띄운다)
# DG3c-1의 dungeon_test.py를 그대로 쓰되 새 맵에 맞춰 ① 시작 지점·우두머리 자리 ② 걷기를 "길 따라 걷기"로 바꾼다
# (새 맵은 방 사이가 벽으로 막혀 직선으로 갈 수 없다 — 방·통로 가운데를 잇는 꺾은선을 따라간다. 서버 이동 검증이 거부하면 몬스터에 닿지 못해 시나리오가 실패한다).
#   solo·isolate·cut_load·dup_load·party  DG3c-1과 같은 시나리오(회귀)
#   revive  파티원이 방1에서 몬스터에게 죽고 → 같은 Room의 시작 지점에서 부활
#   clear   (스폰 = 약한 몬스터 지점당 1마리, 내보내기 6초) 방1 → 방2 → 우두머리 방을 걸어가며 전부 잡아 클리어 → 마을로 → 새 인스턴스
#   real    (원본 테이블) 방마다 보이는 몬스터 수·종류(방1 4·방2 6·우두머리 1), 시험 계정 혼자 원본 몬스터와 싸워 봄(결과는 기록용 — 판정하지 않음)
import math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG3c"))
import dungeon_test as dt
mk, pt = dt.mk, dt.pt

dt.DUNGEON_START = (-4000.0, -2600.0)
dt.BOSS_POS = (3300.0, -2400.0)
# 방·통로 가운데를 잇는 길(cm): 시작 방 → 문 → 방1 → 동쪽 문 → 통로 꺾임 → 방2 → 북쪽 문 → 통로 꺾임 → 우두머리 방 문 → 우두머리 방
ROUTE = [(-4000, -2600), (-2850, -2600), (-1400, -2600), (-1400, -1250), (-1400, 2600), (-700, 2600), (600, 2600),
         (2000, 2600), (3300, 2600), (3300, -700), (3300, -2400)]
ROOM1, ROOM2, BOSS = (-1400.0, -2600.0), (600.0, 2600.0), (3300.0, -2400.0)


def _seg_index(x, y):
    """길에서 가장 가까운 구간 번호(i: ROUTE[i] ~ ROUTE[i+1])."""
    best, bi = 1e18, 0
    for i in range(len(ROUTE) - 1):
        ax, ay = ROUTE[i]; bx, by = ROUTE[i + 1]
        vx, vy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy)))
        d = math.hypot(x - (ax + vx * t), y - (ay + vy * t))
        if d < best:
            best, bi = d, i
    return bi


_straight = mk.Client.walk_to


def routed_walk(self, tx, ty, stop_cm=150.0, **kw):
    if getattr(self, "kind", None) != dt.KIND_DUNGEON:
        return _straight(self, tx, ty, stop_cm=stop_cm, **kw)
    i, j = _seg_index(self.x, self.y), _seg_index(tx, ty)
    nodes = [ROUTE[k] for k in range(i + 1, j + 1)] if j > i else [ROUTE[k] for k in range(i, j, -1)]
    for nx, ny in nodes:
        if not _straight(self, nx, ny, stop_cm=60.0, **kw):
            return False
    return _straight(self, tx, ty, stop_cm=stop_cm, **kw)


dt.Client.walk_to = routed_walk
check, pump_all, wait_all, join, close_all = dt.check, dt.pump_all, dt.wait_all, dt.join, dt.close_all


def t_revive():
    a, b = cs = [dt.Client("Test1").enter(), dt.Client("Test2").enter()]
    join(a, b, cs)
    for c in cs:
        dt.to_field(c, cs)
    dt.cooldown()
    nb = len(b.room_changes)
    dt.enter_dungeon(a, cs)
    wait_all(cs, lambda: len(b.room_changes) > nb and b.spawned, 10.0, "동반 입장")
    b.walk_to(ROOM1[0], ROOM1[1], stop_cm=100, limit_s=60)   # 방1 가운데(선공 몬스터 인식 범위 안)
    wait_all(cs, lambda: b.dead, 120.0, "b 사망(몬스터에게 맞음)")
    pump_all(cs, b.revive_delay / 1000.0 + 0.5)
    n, r = len(b.room_changes), b.revives
    b.send(mk.CS_REVIVE)
    wait_all(cs, lambda: b.revives > r, 10.0, "던전 부활")
    check("in-room", len(b.room_changes) == n and b.kind == dt.KIND_DUNGEON, "던전 부활이 Room 이동이 됨")
    d = math.hypot(b.x - dt.DUNGEON_START[0], b.y - dt.DUNGEON_START[1])
    check("start", d <= 400.0, f"부활 위치가 시작 지점에서 {d:.0f}cm")
    close_all(cs)


def alive_near(c, pos, r=1300.0):
    return [i for i, o in c.monsters().items() if i not in c.dead_ids and math.hypot(o[1] - pos[0], o[2] - pos[1]) <= r]


def t_real():
    a = dt.Client("Test1").enter()
    cs = [a]
    dt.to_field(a, cs)
    dt.cooldown()
    dt.enter_dungeon(a, cs)
    pump_all(cs, 5.0)
    check("start-safe", not a.dead, "시작 방에 서 있기만 했는데 죽음(벽 너머 인식?)")
    n1 = len(alive_near(a, ROOM1))
    check("room1-count", n1 == 4, f"방1 몬스터 {n1}마리(기대 4)")
    seen2 = len(alive_near(a, ROOM2)) + len(alive_near(a, BOSS, 1600.0))
    print(f"INFO real: 시작 방에서 보이는 몬스터 — 방1 {n1}, 방2·우두머리 방 {seen2}(시야 50m 밖이면 0)")
    # 원본 몬스터와 싸워 본다(기록용): 방1을 비우면 방2까지 가서 수를 센다
    t0 = time.time()
    ok1 = dt.fight_until(a, [], lambda: not alive_near(a, ROOM1), limit=240.0)
    print(f"INFO real: 방1 정리 {'성공' if ok1 else '실패'} ({time.time() - t0:.0f}초, 부활 {a.revives}회)")
    if ok1:
        a.walk_to(-1400.0, 2600.0, stop_cm=100, limit_s=60)   # 통로 꺾임(방2에서 약 20m)
        pump_all(cs, 2.0)
        n2 = len(alive_near(a, ROOM2))
        check("room2-count", n2 == 6, f"방2 몬스터 {n2}마리(기대 6)")
        check("corridor-safe", not a.dead, "통로에 서 있는데 죽음(벽 너머 인식?)")
    close_all(cs)


CASES = dict(dt.CASES)
CASES.update({"revive": t_revive, "real": t_real})

if __name__ == "__main__":
    failed = 0
    for name in sys.argv[1:]:
        items0, gold0 = mk.db_totals()
        try:
            CASES[name]()
            time.sleep(3.0)
            items1, gold1 = mk.db_totals()
            if items1 != items0 or gold1 != gold0:
                raise AssertionError(f"DB 합계가 다름: 아이템 {items0} -> {items1}, 골드 {gold0} -> {gold1}")
            print(f"PASS {name}  (DB 아이템 {items1}, 골드 {gold1})")
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: {type(e).__name__}: {e}")
            time.sleep(3.0)
        sys.stdout.flush()
    sys.exit(1 if failed else 0)
