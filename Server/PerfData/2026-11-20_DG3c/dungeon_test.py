# DG3c-1 인스턴스 던전 시험(원시 소켓, 계정 Test1~Test4). 사용: python -I -X utf8 dungeon_test.py <시나리오> [...]
# 서버는 run_dungeon_tests.sh가 시나리오에 맞는 설정·데이터 테이블로 띄운다. 판정 줄은 "PASS <이름>" / "FAIL <이름>: 이유".
# 공용 코드는 DG3b의 movekit.py·DG2의 party_test.py를 그대로 쓴다. 던전 입구(포털 100)는 사냥터 북서쪽 끝이라
# 서버를 [Debug] PortalNoRange=true로 띄워 사냥터 Room 안 어디서든 요청한다(거리 검사만 건너뜀 — 거리 검사는 DG3b basic이 본다).
#   solo        혼자 입장: SC_ROOM_CHANGE(던전 레벨·kind 3) → CS_ENTER_ROOM 뒤에만 내 SC_ADD_OBJECT·포털(출구, 종류 2)·SC_DUNGEON_STATE(진행 중)·몬스터,
#               출구 → 사냥터의 입구 포털 100 바로 밖, 나온 뒤에도 패킷을 받음(핑), 다시 들어가면 새 인스턴스(몬스터 id가 다름)
#   isolate     두 사람이 각자 입장 → 서로 보이지 않고 몬스터 id가 겹치지 않음
#   party       파티원이 먼저 누르면 NOT_LEADER, 파티장이 누르면 같은 Room의 파티원이 함께 입장(마을의 파티원은 남음), 안에서 초대는 IN_DUNGEON,
#               남은 파티원은 입구에서 합류, 파티 창의 있는 곳 = 던전
#   leave       던전 안에서 탈퇴 → 마을로, 2명에서 1명 탈퇴(해산) → 탈퇴한 사람만 마을로·남은 사람은 그대로, 파티장 접속 해제 → 남은 사람은 그대로
#   cut_load    로딩 대기 중(CS_ENTER_ROOM 전) 접속 해제 → 곧바로 다시 로그인됨
#   dup_load    로딩 대기 중 같은 계정으로 로그인 → 앞 접속이 끊기고 새 접속이 마을에 들어감
#   churn       혼자 입장·퇴장 15번, 파티 둘이 함께 들어갔다가 거의 동시에 나오고 곧바로 다시 요청 10번 → 매번 들어가지고 끊김 없음
#   dead_follow 죽은 파티원도 파티장을 따라 들어가 던전 시작 지점에서 부활(같은 Room — SC_ROOM_CHANGE 없음)   (DummyRoom·DummySpawnZone 필요 없음: 던전 안 몬스터에게 죽는다)
#   cut_a/cut_b (MoveHandoffDelayMs 800) 입구 요청 0.4초 뒤(서비스가 인스턴스를 만들고 자리를 잡은 뒤, 떠나기 전) / 1.2초 뒤(떠난 뒤, 도착 전) 접속 해제
#   party_cut   (MoveHandoffDelayMs 800) 파티장 입장 직후 따라가던 파티원이 접속 해제 → 파티장은 들어가고 정리 뒤 잔류 없음
#   full        (MaxDungeonInstances 1) 두 번째 인스턴스는 DUNGEON_FULL, 앞 사람이 나가면 들어갈 수 있음
#   load_timeout (LoadTimeoutSec 3) CS_ENTER_ROOM을 보내지 않으면 서버가 끊음
#   clear       (우두머리 = 약한 몬스터, DungeonClearExitSec 6) 전부 잡으면 SC_DUNGEON_STATE(CLEARED, 남은 시간) → 밖의 파티원은 DUNGEON_CLEARED
#               → 시간이 지나면 마을로 → 다시 들어가면 새 인스턴스(진행 중). 죽으면 던전 시작 지점에서 부활
import math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG3b"))   # -I 는 스크립트 폴더를 경로에 넣지 않는다
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG2"))
import movekit as mk
import party_test as pt

SC_PONG, SC_KICK, SC_DUNGEON_STATE = 2012, 2027, 2038
KIND_DUNGEON = 3
MOVE_DUNGEON_FULL, MOVE_NOT_LEADER, MOVE_DUNGEON_CLEARED = 1, 2, 3
R_IN_DUNGEON = 7
DSTATE_IN_PROGRESS, DSTATE_CLEARED = 1, 2
PORTAL_KIND_ENTER, PORTAL_KIND_EXIT = 1, 2
ENTRANCE, EXIT = 100, 900
ENTRANCE_POS = (19020.0, -18800.0)   # 사냥터의 던전 입구 마커(cm), 반경 400
ENTRANCE_R = 400.0
WORLD_LEVEL, DUNGEON_LEVEL = "Continent01", "Continent01_Dungeon"
DUNGEON_START = (-2200.0, 0.0)       # 던전 시작 지점 마커(cm)

check, pump_all, wait_all, join, close_all = pt.check, pt.pump_all, pt.wait_all, pt.join, pt.close_all


class Client(pt.Client):
    def __init__(self, account, **kw):
        self.level = WORLD_LEVEL
        self.kind = mk.KIND_TOWN
        self.auto_load = True        # False면 다른 레벨로 옮겨져도 CS_ENTER_ROOM을 보내지 않는다(로딩 대기 시험)
        self.loading = False
        self.dungeon_states = []     # (state, remain_sec)
        self.portal_kinds = {}       # id -> kind
        self.pongs = 0
        self.kicked = False
        self.early = []              # 로딩 대기 중(CS_ENTER_ROOM 전)에 받은 패킷 id — 오브젝트·포털·던전 상태가 오면 안 된다
        super().__init__(account, **kw)

    def handle(self, pid, body):
        if self.loading and pid in (mk.SC_ADD_OBJECT, mk.SC_PORTAL_LIST, SC_DUNGEON_STATE, mk.SC_MOVE_OBJECT):
            self.early.append(pid)
        if pid == mk.SC_ROOM_CHANGE:
            m = mk.decode(body)
            level = mk.first(m, 1, b"").decode("utf-8")
            self.kind = mk.first(m, 2)
            self.portal_kinds = {}
            if level != self.level:
                # 다른 레벨: 실제 클라이언트는 레벨을 연 뒤 CS_ENTER_ROOM을 보낸다. 그때까지 서버는 시야·전투에 넣지 않는다.
                self.level = level
                self.spawned = False
                self.loading = True
                super().handle(pid, body)
                if self.auto_load:
                    self.load_done()
                return
        elif pid == mk.SC_PORTAL_LIST:
            for p in mk.decode(body).get(1, []):
                pm = mk.decode(p)
                self.portal_kinds[mk.first(pm, 1)] = mk.first(pm, 6)
        elif pid == SC_DUNGEON_STATE:
            m = mk.decode(body)
            self.dungeon_states.append((mk.first(m, 1), mk.first(m, 2)))
        elif pid == SC_PONG:
            self.pongs += 1
        elif pid == SC_KICK:
            self.kicked = True
        super().handle(pid, body)

    def load_done(self):
        self.loading = False
        self.sock.sendall(mk.rs.enter_room())

    def monster_ids(self):
        return set(self.monsters())


def to_field(c, others=()):
    """마을 → 북문 포털 → 사냥터 Room."""
    pt.to_field(c, others, north=True)
    check("field", c.kind == mk.KIND_FIELD, f"{c.tag}이 사냥터가 아님(kind {c.kind})")


def enter_dungeon(c, others=(), wait_for=None):
    """입구 요청 → 던전 Room에 들어가 내 SC_ADD_OBJECT와 SC_DUNGEON_STATE를 받을 때까지. wait_for = 대신 지켜볼 사람(파티장이 눌러 따라 들어가는 경우)."""
    who = wait_for or c
    n, s = len(who.room_changes), len(who.dungeon_states)
    c.pump(0.05)
    c.use_portal(ENTRANCE)
    wait_all([c, who, *others], lambda: len(who.room_changes) > n and who.kind == KIND_DUNGEON and who.spawned and len(who.dungeon_states) > s, 12.0,
             f"{who.tag} 던전 입장 (이동 결과 {c.move_results}, kind {who.kind}, level {who.level})")
    pump_all([c, who, *others], 0.5)


def exit_dungeon(c, others=()):
    n = len(c.room_changes)
    c.use_portal(EXIT)
    wait_all([c, *others], lambda: len(c.room_changes) > n and c.spawned, 12.0, f"{c.tag} 던전 퇴장 (이동 결과 {c.move_results})")
    pump_all([c, *others], 0.4)
    check("exit-kind", c.kind == mk.KIND_FIELD and c.level == WORLD_LEVEL, f"출구 뒤 kind {c.kind} level {c.level}")
    d = math.hypot(c.x - ENTRANCE_POS[0], c.y - ENTRANCE_POS[1])
    check("exit-pos", ENTRANCE_R < d <= ENTRANCE_R + 400.0, f"출구 뒤 위치가 입구 앞이 아님: ({c.x:.0f},{c.y:.0f}) 입구에서 {d:.0f}cm")


def expect_ping(c, others=(), what="핑 응답"):
    """닫힌 Room의 보낼 목록에 세션이 남으면 이후 송신이 멈춘다 — 나온 뒤에도 패킷을 받는지 본다."""
    n = c.pongs
    c.sock.sendall(mk.rs.ping())
    wait_all([c, *others], lambda: c.pongs > n, 5.0, f"{c.tag} {what}")


def cooldown():
    time.sleep(1.1)   # 포털 재사용 대기(PortalCooldownMs 1000)


# ---------------------------------------------------------------------------------------------------------
def t_solo():
    a = Client("Test1").enter()
    to_field(a)
    check("entrance", a.portal_kinds.get(ENTRANCE) == PORTAL_KIND_ENTER, f"사냥터 포털 목록에 입구가 없거나 종류가 다름: {a.portal_kinds}")
    cooldown()
    a.auto_load = False
    n = len(a.room_changes)
    a.use_portal(ENTRANCE)
    wait_all([a], lambda: len(a.room_changes) > n, 10.0, "SC_ROOM_CHANGE")
    check("change", a.level == DUNGEON_LEVEL and a.kind == KIND_DUNGEON, f"level {a.level} kind {a.kind}")
    d = math.hypot(a.x - DUNGEON_START[0], a.y - DUNGEON_START[1])
    check("start-pos", d <= 400.0, f"던전 시작 지점에서 {d:.0f}cm ({a.x:.0f},{a.y:.0f})")
    pump_all([a], 1.5)
    check("loading-quiet", not a.early and not a.spawned and not a.objects, f"로딩 대기 중에 온 패킷 {a.early}, 오브젝트 {len(a.objects)}")
    a.load_done()
    wait_all([a], lambda: a.spawned and a.dungeon_states, 10.0, "CS_ENTER_ROOM 뒤 내 SC_ADD_OBJECT·SC_DUNGEON_STATE")
    pump_all([a], 0.6)
    a.auto_load = True
    check("state", a.dungeon_states[-1] == (DSTATE_IN_PROGRESS, 0), f"던전 상태 {a.dungeon_states}")
    check("exit-portal", a.portal_kinds == {EXIT: PORTAL_KIND_EXIT}, f"던전 포털 목록 {a.portal_kinds}")
    first = a.monster_ids()
    check("monsters", len(first) >= 3 and not a.players(), f"몬스터 {len(first)}마리, 다른 플레이어 {list(a.players())}")
    # 로딩이 끝난 뒤의 중복 CS_ENTER_ROOM은 아무 일도 하지 않는다
    a.sock.sendall(mk.rs.enter_room())
    pump_all([a], 0.6)
    check("dup-enter", not a.closed, "중복 CS_ENTER_ROOM에 끊김")
    cooldown()
    exit_dungeon(a)
    expect_ping(a, what="던전에서 나온 뒤 핑 응답(닫힌 Room의 보낼 목록)")
    check("field-view", not (a.monster_ids() & first), "던전 몬스터가 사냥터에서 보임")
    # 다시 들어가면 새 인스턴스
    cooldown()
    enter_dungeon(a)
    second = a.monster_ids()
    check("new-instance", second and not (second & first), f"다시 들어간 인스턴스의 몬스터 id가 겹침: {sorted(first)} / {sorted(second)}")
    cooldown()
    exit_dungeon(a)
    expect_ping(a)
    a.close()


def t_isolate():
    a, b = cs = [Client("Test1").enter(), Client("Test2").enter()]
    for c in cs:
        to_field(c, cs)
    cooldown()
    enter_dungeon(a, cs)
    enter_dungeon(b, cs)
    pump_all(cs, 0.8)
    check("unseen", b.id not in a.objects and a.id not in b.objects, "다른 인스턴스인데 서로 보임")
    ma, mb = a.monster_ids(), b.monster_ids()
    check("monsters", ma and mb and not (ma & mb), f"몬스터 id가 겹침: {sorted(ma)} / {sorted(mb)}")
    # 안에서 움직여도 서로에게 가지 않는다
    a.walk_to(a.x + 200.0, a.y, stop_cm=30.0)
    pump_all(cs, 0.5)
    check("unseen2", a.id not in b.objects, "움직인 뒤 다른 인스턴스에 보임")
    cooldown()
    exit_dungeon(a, cs)
    exit_dungeon(b, cs)
    pump_all(cs, 0.5)
    check("field-see", b.id in a.objects and a.id in b.objects, "입구 앞에서 서로 보이지 않음")
    close_all(cs)


def t_party():
    a, b, c, d = cs = [Client(f"Test{i}").enter() for i in range(1, 5)]
    join(a, b, cs)
    join(a, c, cs)
    to_field(a, cs)
    to_field(b, cs)
    cooldown()
    # 파티원이 먼저: 인스턴스가 없으면 파티장만 만들 수 있다
    b.use_portal(ENTRANCE)
    wait_all(cs, lambda: b.move_results, 5.0, "NOT_LEADER")
    check("not-leader", [r for _, r in b.move_results] == [MOVE_NOT_LEADER] and b.kind == mk.KIND_FIELD, f"결과 {b.move_results}")
    cooldown()
    # 파티장: 같은 Room의 b가 함께 간다. 마을의 c는 남는다.
    nb = len(b.room_changes)
    enter_dungeon(a, cs)
    wait_all(cs, lambda: len(b.room_changes) > nb and b.kind == KIND_DUNGEON and b.spawned, 10.0, "파티원 동반 입장")
    pump_all(cs, 0.8)
    check("together", b.id in a.objects and a.id in b.objects and a.monster_ids() == b.monster_ids() and a.monster_ids(), "같은 인스턴스가 아님")
    check("c-stays", c.kind == mk.KIND_TOWN and c.level == WORLD_LEVEL, f"마을의 파티원이 옮겨짐: kind {c.kind}")
    check("no-notice", [r for _, r in b.move_results] == [MOVE_NOT_LEADER], f"따라간 사람에게 이동 결과가 감: {b.move_results}")
    wait_all(cs, lambda: c.kinds().get(a.id) == KIND_DUNGEON and c.kinds().get(b.id) == KIND_DUNGEON, 5.0, f"파티 창의 있는 곳 {c.kinds()}")
    # 안에 있는 사람은 초대할 수도 초대받을 수도 없다
    a.take_results()
    a.invite(target=d)
    wait_all(cs, lambda: a.results, 5.0, "IN_DUNGEON")
    check("in-dungeon", a.take_results() == [(pt.CS_PARTY_INVITE, R_IN_DUNGEON)] and not d.invites, "안에서 한 초대의 결과가 다름")
    d.invite(target=b)
    wait_all(cs, lambda: d.results, 5.0, "IN_DUNGEON(대상)")
    check("target-in-dungeon", d.take_results() == [(pt.CS_PARTY_INVITE, R_IN_DUNGEON)], "안에 있는 사람을 초대한 결과가 다름")
    # 남은 파티원은 입구에서 합류한다(파티원이지만 인스턴스가 있으므로 본인만)
    to_field(c, cs)
    cooldown()
    enter_dungeon(c, cs)
    check("joined", a.id in c.objects and b.id in c.objects and c.monster_ids() == a.monster_ids(), "합류한 인스턴스가 다름")
    # 한 명이 출구로 나갔다가 다시 들어오면 같은 인스턴스(안에 파티원이 남아 있다)
    before = a.monster_ids()
    cooldown()
    exit_dungeon(b, cs)
    cooldown()
    enter_dungeon(b, cs)
    check("rejoin", b.monster_ids() == before and a.id in b.objects, "다시 들어간 인스턴스가 다름")
    close_all(cs)


def t_leave():
    a, b, c = cs = [Client(f"Test{i}").enter() for i in range(1, 4)]
    join(a, b, cs)
    join(a, c, cs)
    for x in cs:
        to_field(x, cs)
    cooldown()
    nb, nc = len(b.room_changes), len(c.room_changes)
    enter_dungeon(a, cs)
    wait_all(cs, lambda: len(b.room_changes) > nb and len(c.room_changes) > nc and b.spawned and c.spawned, 10.0, "동반 입장")
    pump_all(cs, 0.6)
    # 안에서 탈퇴 → 마을로
    n = len(c.room_changes)
    c.leave()
    wait_all(cs, lambda: len(c.room_changes) > n and c.spawned, 10.0, "탈퇴한 사람이 마을로")
    pump_all(cs, 0.5)
    check("ejected", c.kind == mk.KIND_TOWN and c.level == WORLD_LEVEL and not c.move_results, f"kind {c.kind} level {c.level} 결과 {c.move_results}")
    check("others-stay", a.kind == KIND_DUNGEON and b.kind == KIND_DUNGEON and c.id not in a.objects, "남은 사람이 옮겨졌거나 탈퇴한 사람이 보임")
    expect_ping(c, cs)
    # 탈퇴한 사람은 그 인스턴스에 다시 못 들어간다: 혼자 들어가면 자기 인스턴스
    to_field(c, cs)
    cooldown()
    enter_dungeon(c, cs)
    check("own-instance", a.id not in c.objects and not (c.monster_ids() & a.monster_ids()), "탈퇴한 사람이 파티 인스턴스에 들어감")
    cooldown()
    exit_dungeon(c, cs)
    # 2명에서 1명 탈퇴(해산): 탈퇴한 b만 마을로, a는 안에 남아 주인이 된다
    n = len(b.room_changes)
    na = len(a.room_changes)
    b.leave()
    wait_all(cs, lambda: len(b.room_changes) > n and b.spawned and a.states[-1] == (0, 0, []), 10.0, "해산 + 탈퇴한 사람이 마을로")
    pump_all(cs, 1.0)
    check("last-stays", len(a.room_changes) == na and a.kind == KIND_DUNGEON and not a.closed, "남은 한 사람이 내보내짐")
    check("b-town", b.kind == mk.KIND_TOWN, f"b kind {b.kind}")
    cooldown()
    exit_dungeon(a, cs)
    expect_ping(a, cs)
    # 파티장이 안에서 접속 해제: 남은 파티원은 그대로(위임)
    join(a, b, cs)
    join(a, c, cs)
    to_field(b, cs)
    cooldown()
    nb, nc = len(b.room_changes), len(c.room_changes)
    enter_dungeon(a, cs)
    wait_all(cs, lambda: len(b.room_changes) > nb and len(c.room_changes) > nc and b.spawned and c.spawned, 10.0, "동반 입장 2")
    pump_all(cs, 0.5)
    nb = len(b.room_changes)
    a.close(rst=True)
    wait_all([b, c], lambda: b.party()[1] == b.id, 10.0, "파티장 접속 해제 뒤 위임")
    pump_all([b, c], 1.0)
    check("stay-after-cut", len(b.room_changes) == nb and b.kind == KIND_DUNGEON and c.kind == KIND_DUNGEON and c.id in b.objects, "파티장이 끊긴 뒤 남은 사람이 옮겨짐")
    close_all([b, c])


def relogin_ok(account, limit=8.0):
    """정리가 끝나면 곧바로 다시 로그인된다(막히면 LoginCloseForceSec 30초)."""
    t0 = time.time()
    c = Client(account).enter()
    took = time.time() - t0
    check("relogin", took <= limit, f"다시 로그인에 {took:.1f}초")
    check("town", c.kind == mk.KIND_TOWN and c.level == WORLD_LEVEL, "다시 로그인한 곳이 마을이 아님")
    return c


def t_cut_load():
    a = Client("Test1").enter()
    to_field(a)
    cooldown()
    a.auto_load = False
    n = len(a.room_changes)
    a.use_portal(ENTRANCE)
    wait_all([a], lambda: len(a.room_changes) > n, 10.0, "SC_ROOM_CHANGE")
    pump_all([a], 0.5)
    a.close(rst=True)
    time.sleep(1.5)
    relogin_ok("Test1").close()


def t_dup_load():
    a = Client("Test1").enter()
    to_field(a)
    cooldown()
    a.auto_load = False
    n = len(a.room_changes)
    a.use_portal(ENTRANCE)
    wait_all([a], lambda: len(a.room_changes) > n, 10.0, "SC_ROOM_CHANGE")
    pump_all([a], 0.5)
    b = Client("Test1", tag="Test1-2nd")   # 같은 계정: 앞 접속을 끊고(로딩 대기 중) 정리가 끝난 뒤 로그인된다
    b.enter()
    a.pump(1.0)
    check("kicked", a.closed or a.kicked, "앞 접속이 끊기지 않음")
    check("town", b.kind == mk.KIND_TOWN and b.level == WORLD_LEVEL, "새 접속이 마을이 아님")
    expect_ping(b)
    a.close()
    b.close()


def t_churn():
    a, b = cs = [Client("Test1").enter(), Client("Test2").enter()]
    for c in cs:
        to_field(c, cs)
    seen = set()
    for i in range(15):
        cooldown()
        enter_dungeon(a, cs)
        ids = a.monster_ids()
        check("fresh", ids and not (ids & seen), f"{i}번째 인스턴스의 몬스터 id가 앞의 것과 겹침")
        seen |= ids
        cooldown()
        exit_dungeon(a, cs)
    expect_ping(a, cs)
    # 파티: 둘이 함께 들어갔다가 거의 동시에 나오고, 파티원이 곧바로 입구를 다시 누른다(닫힘과 입장이 겹치는 순간).
    join(a, b, cs)
    for i in range(10):
        cooldown()
        nb = len(b.room_changes)
        enter_dungeon(a, cs)
        wait_all(cs, lambda: len(b.room_changes) > nb and b.spawned and b.kind == KIND_DUNGEON, 10.0, f"{i}번째 동반 입장")
        cooldown()
        na, nb = len(a.room_changes), len(b.room_changes)
        a.use_portal(EXIT)
        b.use_portal(EXIT)
        wait_all(cs, lambda: len(a.room_changes) > na and len(b.room_changes) > nb and a.spawned and b.spawned, 10.0, f"{i}번째 동시 퇴장")
        check("both-field", a.kind == mk.KIND_FIELD and b.kind == mk.KIND_FIELD, f"{i}번째 퇴장 뒤 kind {a.kind}/{b.kind}")
        # 파티원의 요청: 인스턴스가 이미 닫혔으므로 NOT_LEADER여야 한다(닫히는 중인 방에 들어가면 안 된다). 재사용 대기 중이면 응답 없이 버려진다.
        b.move_results = []
        b.use_portal(ENTRANCE)
        pump_all(cs, 0.4)
        check("no-stale-join", b.kind == mk.KIND_FIELD and all(r == MOVE_NOT_LEADER for _, r in b.move_results), f"{i}번째: 닫힌 인스턴스 합류 kind {b.kind} 결과 {b.move_results}")
    expect_ping(a, cs)
    expect_ping(b, cs)
    close_all(cs)


BOSS_POS = (2316.0, -57.0)          # 우두머리 스폰 마커(cm)


def fight_until(c, others, done, limit=300.0, revive=True, near=None):
    """near(없으면 내 위치)에서 가장 가까운 살아 있는 몬스터를 계속 친다. 죽으면 제자리(같은 Room) 부활을 기다렸다가 이어 간다. done()이 참이 되면 끝."""
    everyone = [c, *others]
    end = time.time() + limit
    target = None
    while time.time() < end:
        if done():
            return True
        check("alive-conn", not c.closed, f"{c.tag}이 끊김")
        if c.dead:
            if not revive:
                return False
            pump_all(everyone, c.revive_delay / 1000.0 + 0.5)
            n, r = len(c.room_changes), c.revives
            c.send(mk.CS_REVIVE)
            wait_all(everyone, lambda: c.revives > r, 10.0, f"{c.tag} 부활")
            check("revive-in-room", len(c.room_changes) == n and c.kind == KIND_DUNGEON, "던전 부활이 Room 이동이 됨")
            d = math.hypot(c.x - DUNGEON_START[0], c.y - DUNGEON_START[1])
            check("revive-pos", d <= 400.0, f"부활 위치가 시작 지점에서 {d:.0f}cm")
            target = None
            continue
        mons = {i: o for i, o in c.monsters().items() if i not in c.dead_ids}
        if not mons:
            pump_all(everyone, 0.3)
            continue
        if target not in mons:
            ref = near or (c.x, c.y)
            target = min(mons, key=lambda i: math.hypot(mons[i][1] - ref[0], mons[i][2] - ref[1]))
        _, mx, my, mz = mons[target]
        if math.hypot(mx - c.x, my - c.y) > 180:
            c.walk_to(mx, my, stop_cm=130, limit_s=20)
            continue
        c.attack(target, mx, my)
        pump_all(everyone, 0.6)
    return done()


def t_revive():
    a, b = cs = [Client("Test1").enter(), Client("Test2").enter()]
    join(a, b, cs)
    for c in cs:
        to_field(c, cs)
    cooldown()
    nb = len(b.room_changes)
    enter_dungeon(a, cs)
    wait_all(cs, lambda: len(b.room_changes) > nb and b.spawned, 10.0, "동반 입장")
    # b가 던전 안에서 죽는다(맞기만 한다) → 제자리 부활이 같은 Room·시작 지점인지 본다.
    b.walk_to(1500.0, 0.0, stop_cm=100, limit_s=40)
    wait_all(cs, lambda: b.dead, 120.0, "b 사망(몬스터에게 맞음)")
    pump_all(cs, b.revive_delay / 1000.0 + 0.5)
    n, r = len(b.room_changes), b.revives
    b.send(mk.CS_REVIVE)
    wait_all(cs, lambda: b.revives > r, 10.0, "던전 부활")
    check("in-room", len(b.room_changes) == n and b.kind == KIND_DUNGEON, "던전 부활이 Room 이동이 됨")
    d = math.hypot(b.x - DUNGEON_START[0], b.y - DUNGEON_START[1])
    check("start", d <= 400.0, f"부활 위치가 시작 지점에서 {d:.0f}cm")
    close_all(cs)


def cut_case(delay):
    a = Client("Test1").enter()
    to_field(a)
    cooldown()
    a.use_portal(ENTRANCE)
    time.sleep(delay)
    a.close(rst=True)
    time.sleep(2.5)
    relogin_ok("Test1").close()


def t_party_cut():
    a, b = cs = [Client("Test1").enter(), Client("Test2").enter()]
    join(a, b, cs)
    for c in cs:
        to_field(c, cs)
    cooldown()
    n = len(a.room_changes)
    a.use_portal(ENTRANCE)
    # 인계 지연 800ms: 0.8초에 파티장이 떠나며 b의 따라가기가 시작되고, 1.6초에 b의 응답이 온다 — 그 사이에 b가 끊긴다.
    time.sleep(1.2)
    b.close(rst=True)
    wait_all([a], lambda: len(a.room_changes) > n and a.spawned and a.kind == KIND_DUNGEON, 12.0, "파티장 입장")
    wait_all([a], lambda: a.states[-1] == (0, 0, []), 10.0, "끊긴 파티원 탈퇴(해산)")
    check("alone", not a.players(), f"던전에 다른 사람이 보임: {list(a.players())}")
    cooldown()
    exit_dungeon(a)
    expect_ping(a)
    a.close()
    time.sleep(2.0)
    relogin_ok("Test2").close()


def t_full():
    a, b = cs = [Client("Test1").enter(), Client("Test2").enter()]
    for c in cs:
        to_field(c, cs)
    cooldown()
    enter_dungeon(a, cs)
    b.use_portal(ENTRANCE)
    wait_all(cs, lambda: b.move_results, 5.0, "DUNGEON_FULL")
    check("full", [r for _, r in b.move_results] == [MOVE_DUNGEON_FULL] and b.kind == mk.KIND_FIELD, f"결과 {b.move_results}")
    cooldown()
    exit_dungeon(a, cs)
    cooldown()
    enter_dungeon(b, cs)   # 앞 인스턴스가 닫혔으므로 들어간다
    close_all(cs)


def t_load_timeout():
    a = Client("Test1").enter()
    to_field(a)
    cooldown()
    a.auto_load = False
    n = len(a.room_changes)
    a.use_portal(ENTRANCE)
    wait_all([a], lambda: len(a.room_changes) > n, 10.0, "SC_ROOM_CHANGE")
    t0 = time.time()
    while not a.closed and time.time() - t0 < 10.0:
        a.pump(0.2)
    took = time.time() - t0
    check("closed", a.closed and 2.0 <= took <= 6.5, f"로딩 시한(3초)에 끊기지 않음: closed={a.closed} {took:.1f}초")
    time.sleep(1.5)
    relogin_ok("Test1").close()


def t_clear():
    a, b = cs = [Client("Test1").enter(), Client("Test2").enter()]
    join(a, b, cs)
    to_field(a, cs)
    cooldown()
    enter_dungeon(a, cs)       # b는 마을에 있어 따라오지 않는다
    first = a.monster_ids()
    to_field(b, cs)
    ok = fight_until(a, [b], lambda: a.dungeon_states[-1][0] == DSTATE_CLEARED, near=BOSS_POS)
    check("cleared", ok, f"클리어하지 못함: 상태 {a.dungeon_states[-1]}, 남은 몬스터 {len([i for i in a.monsters() if i not in a.dead_ids])}")
    t_clear_at = time.time()
    state, remain = a.dungeon_states[-1]
    check("remain", 4 <= remain <= 6, f"남은 시간 {remain}초(DungeonClearExitSec 6)")
    # 클리어한 인스턴스에는 새로 들어갈 수 없다
    cooldown()
    b.use_portal(ENTRANCE)
    wait_all(cs, lambda: b.move_results, 5.0, "DUNGEON_CLEARED")
    check("cleared-reject", [r for _, r in b.move_results] == [MOVE_DUNGEON_CLEARED] and b.kind == mk.KIND_FIELD, f"결과 {b.move_results}")
    # 시간이 지나면 마을로
    n = len(a.room_changes)
    wait_all(cs, lambda: len(a.room_changes) > n and a.spawned, 15.0, "클리어 뒤 내보냄")
    took = time.time() - t_clear_at
    check("eject", a.kind == mk.KIND_TOWN and a.level == WORLD_LEVEL and 3.0 <= took <= 9.5, f"kind {a.kind}, 클리어 뒤 {took:.1f}초")
    expect_ping(a, cs)
    # 인스턴스가 닫혔으므로 파티장이 다시 들어가면 새 인스턴스(진행 중), 파티원 b도 함께
    to_field(a, cs)
    cooldown()
    nb = len(b.room_changes)
    enter_dungeon(a, cs)
    wait_all(cs, lambda: len(b.room_changes) > nb and b.spawned and b.kind == KIND_DUNGEON, 10.0, "새 인스턴스에 동반 입장")
    check("new", a.dungeon_states[-1] == (DSTATE_IN_PROGRESS, 0) and a.monster_ids() and not (a.monster_ids() & first), f"새 인스턴스가 아님: 상태 {a.dungeon_states[-1]}")
    close_all(cs)


CASES = {"solo": t_solo, "isolate": t_isolate, "party": t_party, "leave": t_leave, "cut_load": t_cut_load, "dup_load": t_dup_load,
         "churn": t_churn, "revive": t_revive, "cut_a": lambda: cut_case(0.4), "cut_b": lambda: cut_case(1.2),
         "party_cut": t_party_cut, "full": t_full, "load_timeout": t_load_timeout, "clear": t_clear}

if __name__ == "__main__":
    failed = 0
    for name in sys.argv[1:]:
        items0, gold0 = mk.db_totals()
        try:
            CASES[name]()
            time.sleep(3.0)   # 접속 정리(마지막 저장)가 끝나야 같은 계정으로 바로 다시 로그인된다
            items1, gold1 = mk.db_totals()
            # 몬스터를 잡는 시나리오는 드랍을 줍지 않으므로 합계는 그대로여야 한다.
            if items1 != items0 or gold1 != gold0:
                raise AssertionError(f"DB 합계가 다름: 아이템 {items0} -> {items1}, 골드 {gold0} -> {gold1}")
            print(f"PASS {name}  (DB 아이템 {items1}, 골드 {gold1})")
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: {type(e).__name__}: {e}")
            time.sleep(3.0)
        sys.stdout.flush()
    sys.exit(1 if failed else 0)
