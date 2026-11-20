# DG2 파티 시험(원시 소켓, 계정 Test1~Test4). 사용: python -I -X utf8 party_test.py <시나리오> [...]
# 서버는 run_party_tests.sh가 시나리오에 맞는 설정·데이터 테이블로 띄운다. 판정 줄은 "PASS <이름>" / "FAIL <이름>: 이유".
# 공용 코드는 DG3b의 movekit.py(protobuf 해석·걷기·포털)를 그대로 쓴다.
#   basic     id 초대 → 수락 → 양쪽 SC_PARTY_STATE 일치, 이름 초대, 파티원(파티장 아님)의 초대는 NOT_LEADER, 거절은 DECLINED, 4명 결성, 탈퇴
#   rooms     파티원이 사냥터로 이동하면 전원의 목록에서 있는 곳이 바뀜, 다른 Room에 있는 사람을 이름으로 초대
#   leader    파티장 탈퇴 → 먼저 들어온 사람이 파티장, 2명에서 1명 탈퇴 → 해산(남은 사람도 파티 없음), 파티 없이 탈퇴는 NOT_IN_PARTY
#   cut       파티장·파티원 접속 해제 → 위임·해산, 초대 대기 중에 받는 쪽·보낸 쪽이 끊김 → 상대에게 NO_TARGET, 끊긴 초대에 답하면 NO_INVITE
#   errors    NO_TARGET(없는 이름·자기 자신)·TOO_FREQUENT·TARGET_IN_PARTY·TARGET_BUSY·NO_INVITE·NOT_IN_PARTY
#   loot      사냥터에서 파티장이 잡은 몬스터의 드랍: 파티 밖 사람은 NOT_OWNER, 파티원은 주움. 탈퇴한 뒤에는 NOT_OWNER
#   full      (PartyMaxMembers 3) 3명 파티에서 네 번째 초대는 PARTY_FULL
#   timeout   (PartyInviteTimeoutSec 3) 답하지 않으면 양쪽에 TIMEOUT, 그 뒤 응답은 NO_INVITE, 다시 초대 가능
#   channel   (ChannelCapacity 2) 세 번째 로그인은 채널 2 → 초대는 OTHER_CHANNEL
#   midmove   (MoveHandoffDelayMs 800) Room 이동 중에 초대를 수락·탈퇴해도 파티 번호 사본이 맞음(드랍 줍기로 확인)
import math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG3b"))   # -I 는 스크립트 폴더를 경로에 넣지 않는다
import movekit as mk
import move_test as mt   # 시작 지점 → 북문 포털 101 경로(to_portal_101)

CS_PICKUP_ITEM = 1013
CS_PARTY_INVITE, CS_PARTY_RESPOND, CS_PARTY_LEAVE = 1035, 1036, 1037
SC_PARTY_INVITE, SC_PARTY_STATE, SC_PARTY_RESULT = 2033, 2034, 2035
R_NO_TARGET, R_OTHER_CHANNEL, R_TARGET_IN_PARTY, R_TARGET_BUSY, R_PARTY_FULL, R_NOT_LEADER = 1, 2, 3, 4, 5, 6
R_DECLINED, R_TIMEOUT, R_NO_INVITE, R_NOT_IN_PARTY, R_TOO_FREQUENT = 8, 9, 10, 11, 12
ITEM_TOO_FAR, ITEM_NOT_OWNER = 8, 9
INVITE_GAP = 1.15   # 서버의 초대 최소 간격(1초)보다 조금 길게
HUNT = (-2700.0, -2300.0, -20.0)   # 북문 밖 포털 201에서 가장 가까운 스폰(meadow_n, 중심 (-1797, -2795) 반경 12m)의 가장자리


def check(name, cond, why=""):
    if not cond:
        raise AssertionError(why or name)


class Client(mk.Client):
    def __init__(self, account, **kw):
        self.name = ""
        self.invites = []        # (from_id, from_name, timeout_sec)
        self.states = []         # (party_id, leader_id, [(id, name, level, kind), ...])
        self.results = []        # (request, result)
        self.dead_ids = set()
        self.drops = {}          # id -> (item_id, x, y, z)
        self.last_invite = 0.0
        self.last_ping = time.time()
        super().__init__(account, **kw)

    def pump(self, seconds):
        # 서버는 60초 동안 아무것도 받지 못한 연결을 끊는다(A5.0) — 기다리기만 하는 클라이언트도 핑을 보낸다.
        if not self.closed and time.time() - self.last_ping > 5.0:
            self.last_ping = time.time()
            self.sock.sendall(mk.rs.ping())
        super().pump(seconds)

    def handle(self, pid, body):
        if pid == mk.SC_ADD_OBJECT:
            info = mk.decode(mk.first(mk.decode(body), 1, b""))
            oid = mk.first(info, 1)
            if oid == self.id:
                self.name = mk.first(info, 10, b"").decode("utf-8")
            elif mk.first(info, 2) == mk.TYPE_ITEM:
                x, y, z, yaw = mk.pos_of(mk.first(info, 4, b""))
                self.drops[oid] = (mk.first(info, 6), x, y, z)
        elif pid == mk.SC_REMOVE_OBJECT:
            self.drops.pop(mk.first(mk.decode(body), 1), None)
        elif pid == mk.SC_ROOM_CHANGE:
            self.drops = {}
        elif pid == mk.SC_DEAD:
            self.dead_ids.add(mk.first(mk.decode(body), 1))
        elif pid == SC_PARTY_INVITE:
            m = mk.decode(body)
            self.invites.append((mk.first(m, 1), mk.first(m, 2, b"").decode("utf-8"), mk.first(m, 3)))
        elif pid == SC_PARTY_STATE:
            m = mk.decode(body)
            members = []
            for raw in m.get(3, []):
                mm = mk.decode(raw)
                members.append((mk.first(mm, 1), mk.first(mm, 2, b"").decode("utf-8"), mk.first(mm, 3), mk.first(mm, 4)))
            self.states.append((mk.first(m, 1), mk.first(m, 2), members))
        elif pid == SC_PARTY_RESULT:
            m = mk.decode(body)
            self.results.append((mk.first(m, 1), mk.first(m, 2)))
        super().handle(pid, body)

    # ---- 파티 ----
    def invite(self, target=None, name=None):
        wait = self.last_invite + INVITE_GAP - time.time()
        if wait > 0:
            self.pump(wait)
        self.last_invite = time.time()
        body = mk.rs.f_int(1, target.id) if target is not None else mk.rs.f_str(2, name)
        self.send(CS_PARTY_INVITE, body)

    def respond(self, accept):
        self.send(CS_PARTY_RESPOND, mk.rs.f_int(1, 1) if accept else b"")

    def leave(self):
        self.send(CS_PARTY_LEAVE)

    def party(self):
        """마지막으로 받은 상태: (party_id, leader_id, [구성원 id...]) — 받은 적이 없거나 파티 없음이면 (0, 0, [])."""
        if not self.states:
            return (0, 0, [])
        pid, leader, members = self.states[-1]
        return (pid, leader, [m[0] for m in members])

    def kinds(self):
        return {m[0]: m[3] for m in self.states[-1][2]} if self.states else {}

    def take_results(self):
        out, self.results = self.results, []
        return out


def pump_all(clients, seconds=0.6):
    end = time.time() + seconds
    while time.time() < end:
        for c in clients:
            if not c.closed:
                c.pump(0.03)


def wait_all(clients, cond, timeout, what):
    end = time.time() + timeout
    while not cond():
        if time.time() >= end:
            raise TimeoutError(f"시간 초과: {what}")
        pump_all(clients, 0.1)


def join(leader, member, everyone, by_name=False):
    """leader가 member를 초대하고 member가 수락한다. everyone = 패킷을 계속 받아 줄 클라이언트 전부."""
    n = len(member.invites)
    if by_name:
        leader.invite(name=member.name)
    else:
        leader.invite(target=member)
    wait_all(everyone, lambda: len(member.invites) > n, 5.0, f"{member.tag}의 SC_PARTY_INVITE (초대한 쪽 결과 {leader.results})")
    member.respond(True)
    wait_all(everyone, lambda: member.id in member.party()[2] and member.id in leader.party()[2], 5.0, f"{member.tag} 합류 SC_PARTY_STATE (결과 {member.results})")
    pump_all(everyone, 0.3)


def same_party(clients, leader, order):
    ids = [c.id for c in order]
    views = [c.party() for c in clients]
    check("state", all(v[0] != 0 and v[0] == views[0][0] and v[1] == leader.id and v[2] == ids for v in views),
          f"파티 상태가 다름: 기대 leader={leader.id} members={ids}, 받은 것 {views}")


def login(n):
    return [Client(f"Test{i}").enter() for i in range(1, n + 1)]


def close_all(clients):
    for c in clients:
        c.close()


def to_field(c, others=(), north=False):
    pid = mt.to_portal_101(c) if north else c.goto_portal()
    n = len(c.room_changes)
    c.use_portal(pid)
    wait_all([c, *others], lambda: len(c.room_changes) > n, 10.0, f"{c.tag} SC_ROOM_CHANGE")
    pump_all([c, *others], 0.6)
    return pid


# ---------------------------------------------------------------------------------------------------------
def t_basic():
    a, b, c, d = cs = login(4)
    check("names", all(x.name for x in cs) and len({x.name for x in cs}) == 4, f"이름 {[x.name for x in cs]}")
    # id로 초대 → 수락
    a.invite(target=b)
    wait_all(cs, lambda: b.invites, 5.0, "SC_PARTY_INVITE")
    check("invite", b.invites[0][0] == a.id and b.invites[0][1] == a.name and b.invites[0][2] == 30, f"초대 내용 {b.invites[0]} (보낸 사람 {a.id} {a.name})")
    check("none-yet", not a.states and not b.states, "수락 전에 상태가 옴")
    b.respond(True)
    wait_all(cs, lambda: a.states and b.states, 5.0, "SC_PARTY_STATE")
    same_party([a, b], a, [a, b])
    check("member-info", a.states[-1][2][1][1] == b.name and a.states[-1][2][1][2] >= 1 and a.states[-1][2][1][3] == mk.KIND_TOWN, f"구성원 정보 {a.states[-1][2]}")
    # 이름으로 초대
    join(a, c, cs, by_name=True)
    same_party([a, b, c], a, [a, b, c])
    # 파티원(파티장 아님)의 초대
    c.invite(target=d)
    wait_all(cs, lambda: c.results, 5.0, "NOT_LEADER")
    check("not-leader", c.take_results() == [(CS_PARTY_INVITE, R_NOT_LEADER)] and not d.invites, f"결과가 다름, d의 초대 {d.invites}")
    # 거절
    a.invite(target=d)
    wait_all(cs, lambda: d.invites, 5.0, "d의 초대")
    d.respond(False)
    wait_all(cs, lambda: a.results, 5.0, "DECLINED")
    check("declined", a.take_results() == [(CS_PARTY_INVITE, R_DECLINED)] and not d.states, "거절 결과가 다름")
    # 4명
    join(a, d, cs)
    same_party(cs, a, [a, b, c, d])
    # 탈퇴
    b.leave()
    wait_all(cs, lambda: b.party()[2] == [] and len(a.party()[2]) == 3, 5.0, "탈퇴 반영")
    pump_all(cs, 0.3)
    check("left", b.states[-1] == (0, 0, []), f"탈퇴한 사람의 상태 {b.states[-1]}")
    same_party([a, c, d], a, [a, c, d])
    check("no-results", not any(x.results for x in cs), f"남은 결과 패킷 {[x.results for x in cs]}")
    close_all(cs)


def t_rooms():
    a, b, c = cs = login(3)
    join(a, b, cs)
    to_field(b, cs)
    wait_all(cs, lambda: a.kinds().get(b.id) == mk.KIND_FIELD and b.kinds().get(b.id) == mk.KIND_FIELD, 5.0, "있는 곳 갱신")
    check("kinds", a.kinds() == {a.id: mk.KIND_TOWN, b.id: mk.KIND_FIELD} and a.states[-1] == b.states[-1], f"a {a.states[-1]} b {b.states[-1]}")
    check("unseen", b.id not in a.objects, "다른 Room인데 보임")
    # 다른 Room(사냥터)에 있는 사람을 이름으로 초대: 파티장은 마을에 있다
    to_field(c, cs)
    join(a, c, cs, by_name=True)
    same_party(cs, a, [a, b, c])
    check("kinds3", a.kinds() == {a.id: mk.KIND_TOWN, b.id: mk.KIND_FIELD, c.id: mk.KIND_FIELD}, f"{a.kinds()}")
    # 오브젝트 id로도 다른 Room의 사람을 초대할 수 있다(명부 기준) — 탈퇴시킨 뒤 다시
    c.leave()
    wait_all(cs, lambda: c.party()[2] == [] and len(a.party()[2]) == 2, 5.0, "탈퇴")
    join(a, c, cs)
    same_party(cs, a, [a, b, c])
    close_all(cs)


def t_leader():
    a, b, c = cs = login(3)
    join(a, b, cs)
    join(a, c, cs)
    a.leave()
    wait_all(cs, lambda: a.party()[2] == [] and b.party()[1] == b.id, 5.0, "위임")
    pump_all(cs, 0.3)
    same_party([b, c], b, [b, c])
    # 새 파티장은 초대할 수 있다
    join(b, a, cs)
    same_party(cs, b, [b, c, a])
    a.leave()
    wait_all(cs, lambda: len(b.party()[2]) == 2, 5.0, "탈퇴")
    # 2명에서 1명이 나가면 해산
    c.leave()
    wait_all(cs, lambda: b.states[-1] == (0, 0, []) and c.states[-1] == (0, 0, []), 5.0, "해산")
    b.leave()
    wait_all(cs, lambda: b.results, 5.0, "NOT_IN_PARTY")
    check("not-in-party", b.take_results() == [(CS_PARTY_LEAVE, R_NOT_IN_PARTY)], "결과가 다름")
    # 해산한 뒤 다시 파티를 맺을 수 있다
    join(c, b, cs)
    same_party([b, c], c, [c, b])
    close_all(cs)


def t_cut():
    a, b, c, d = cs = login(4)
    join(a, b, cs)
    join(a, c, cs)
    a.close(rst=True)                       # 파티장 접속 해제 → 위임
    wait_all([b, c], lambda: b.party()[1] == b.id and b.party()[2] == [b.id, c.id], 10.0, "파티장 접속 해제 뒤 위임")
    pump_all([b, c], 0.3)
    same_party([b, c], b, [b, c])
    c.close(rst=True)                       # 파티원 접속 해제 → 해산
    wait_all([b], lambda: b.states[-1] == (0, 0, []), 10.0, "파티원 접속 해제 뒤 해산")
    # 초대를 받은 쪽이 끊김
    b.invite(target=d)
    wait_all([b, d], lambda: d.invites, 5.0, "d의 초대")
    d.close(rst=True)
    wait_all([b], lambda: b.results, 10.0, "받는 쪽 접속 해제 → NO_TARGET")
    check("target-gone", b.take_results() == [(CS_PARTY_INVITE, R_NO_TARGET)], "결과가 다름")
    # 초대를 보낸 쪽이 끊김
    time.sleep(1.0)
    e = Client("Test4").enter()
    e.invite(target=b)
    wait_all([b, e], lambda: b.invites, 5.0, "b의 초대")
    e.close(rst=True)
    wait_all([b], lambda: b.results, 10.0, "보낸 쪽 접속 해제 → NO_TARGET")
    check("from-gone", b.take_results() == [(CS_PARTY_RESPOND, R_NO_TARGET)], "결과가 다름")
    b.respond(True)
    wait_all([b], lambda: b.results, 5.0, "NO_INVITE")
    check("no-invite", b.take_results() == [(CS_PARTY_RESPOND, R_NO_INVITE)] and b.party()[2] == [], "결과가 다름")
    b.close()


def t_errors():
    a, b, c = cs = login(3)

    def expect(who, request, result, what):
        wait_all(cs, lambda: who.results, 5.0, what)
        got = who.take_results()
        check(what, got == [(request, result)], f"{what}: 받은 것 {got}")

    a.invite(name="이런이름은없음")
    expect(a, CS_PARTY_INVITE, R_NO_TARGET, "없는 이름")
    a.invite(target=a)
    expect(a, CS_PARTY_INVITE, R_NO_TARGET, "자기 자신")
    # 간격
    a.pump(INVITE_GAP)
    a.send(CS_PARTY_INVITE, mk.rs.f_int(1, c.id))
    a.send(CS_PARTY_INVITE, mk.rs.f_int(1, c.id))
    a.last_invite = time.time()
    expect(a, CS_PARTY_INVITE, R_TOO_FREQUENT, "연달아 초대")
    wait_all(cs, lambda: c.invites, 5.0, "c의 초대")
    # c에게 a의 초대가 와 있다 → b의 초대는 TARGET_BUSY
    b.invite(target=c)
    expect(b, CS_PARTY_INVITE, R_TARGET_BUSY, "대상에 다른 초대")
    c.respond(True)
    wait_all(cs, lambda: c.party()[2] == [a.id, c.id], 5.0, "합류")
    # 파티 중인 사람 초대
    b.invite(target=c)
    expect(b, CS_PARTY_INVITE, R_TARGET_IN_PARTY, "파티 중인 대상")
    b.respond(True)
    expect(b, CS_PARTY_RESPOND, R_NO_INVITE, "초대 없이 수락")
    b.respond(False)
    expect(b, CS_PARTY_RESPOND, R_NO_INVITE, "초대 없이 거절")
    b.leave()
    expect(b, CS_PARTY_LEAVE, R_NOT_IN_PARTY, "파티 없이 탈퇴")
    # 초대를 받아 둔 사람이 스스로 파티를 만들면 받아 둔 초대는 지워진다
    c.leave()
    wait_all(cs, lambda: a.states[-1] == (0, 0, []), 5.0, "해산")
    a.invite(target=b)                      # b에게 a의 초대(대기)
    wait_all(cs, lambda: b.invites, 5.0, "b의 초대")
    b.invite(target=c)                      # b가 c를 초대하고 c가 수락 → b는 파티장이 되고 a의 초대는 지워짐
    wait_all(cs, lambda: c.invites and len(c.invites) >= 2, 5.0, "c의 초대")
    c.respond(True)
    wait_all(cs, lambda: a.results and b.results, 5.0, "지워진 초대 통지")
    check("dropped", a.take_results() == [(CS_PARTY_INVITE, R_TARGET_IN_PARTY)] and b.take_results() == [(CS_PARTY_RESPOND, R_NO_INVITE)], "지워진 초대 통지가 다름")
    same_party([b, c], b, [b, c])
    close_all(cs)


# ---- 드랍 ----
def kill_one(killer, others, limit=150.0):
    """killer가 가장 가까운 몬스터를 잡고, 그 뒤 새로 생긴 골드 드랍 (id, x, y)를 돌려준다."""
    everyone = [killer, *others]
    end = time.time() + limit
    before = set(killer.drops)
    target = None
    while time.time() < end:
        mons = {i: o for i, o in killer.monsters().items() if i not in killer.dead_ids}
        if target not in mons:
            check("monsters", mons, f"{killer.tag} 주변에 몬스터가 없음 ({killer.x:.0f},{killer.y:.0f})")
            target = min(mons, key=lambda i: math.hypot(mons[i][1] - killer.x, mons[i][2] - killer.y))
        _, mx, my, mz = mons[target]
        if math.hypot(mx - killer.x, my - killer.y) > 180:
            killer.walk_to(mx, my, stop_cm=130, tz=mz, limit_s=20)
            for o in others:
                o.pump(0.01)
            continue
        killer.attack(target, mx, my)
        pump_all(everyone, 0.6)
        check("alive", not killer.closed, f"{killer.tag}이 끊김")
        if killer.dead:
            # 시험 계정의 HP는 이전 시험에서 저장된 값이라 낮을 수 있다. 죽으면 마을에서 부활(HP 100%)해 돌아와 이어 간다.
            pump_all(everyone, killer.revive_delay / 1000.0 + 0.5)
            n = len(killer.room_changes)
            killer.send(mk.CS_REVIVE)
            wait_all(everyone, lambda: len(killer.room_changes) > n and not killer.dead, 10.0, f"{killer.tag} 부활")
            pump_all(everyone, 0.5)
            to_hunt(killer, others)
            before = set(killer.drops)
            target = None
            end = time.time() + limit
            continue
        if target in killer.dead_ids:
            pump_all(everyone, 0.8)
            gold = [(i, d[1], d[2], d[3]) for i, d in killer.drops.items() if i not in before and d[0] == 0]
            check("gold-drop", gold, f"골드 드랍이 없음: {killer.drops}")
            return gold[0]
    raise TimeoutError(f"{killer.tag}이 몬스터를 잡지 못함")


def try_pickup(who, drop, everyone):
    """drop 옆으로 가서 줍기를 보낸다. 'ok' 또는 거부 사유 번호를 돌려준다."""
    did, dx, dy, dz = drop
    result = ITEM_TOO_FAR
    for _ in range(4):   # 걷는 동안 서버가 이동을 보정했으면(지형 높이·장애물) 보정된 자리에서 다시 걷는다
        who.walk_to(dx, dy, stop_cm=120, tz=dz, limit_s=40)
        pump_all(everyone, 0.4)
        n_upd, n_res = len(who.inv_updates), len(who.item_results)
        who.send(CS_PICKUP_ITEM, mk.rs.f_int(1, did))
        wait_all(everyone, lambda: len(who.inv_updates) > n_upd or len(who.item_results) > n_res, 6.0, f"{who.tag} 줍기 응답")
        if len(who.item_results) == n_res:
            return "ok"
        _, request, result = who.item_results[-1]
        check("request", request == CS_PICKUP_ITEM, f"다른 요청의 결과 {request}")
        if result != ITEM_TOO_FAR:
            break
    return result


def field_group(n):
    cs = login(n)
    for c in cs:
        to_hunt(c, cs)
    return cs


def to_hunt(c, others=()):
    """북문으로 나가 사냥 자리(몬스터가 시야에 드는 곳)까지 걷는다."""
    to_field(c, others, north=True)
    c.walk_to(HUNT[0], HUNT[1], stop_cm=150, tz=HUNT[2], limit_s=60)
    c.walk_to(HUNT[0], HUNT[1], stop_cm=150, tz=HUNT[2], limit_s=20)   # 서버가 보정했으면 보정된 자리에서 한 번 더


def t_loot():
    # 처치는 한 번만 한다(시험 계정은 약해서 연달아 싸우면 죽는다). 마을에 있는 d를 파티에 넣어 b가 나가도 파티가 남게 한다.
    a, b, c, d = cs = login(4)
    for x in (a, b, c):
        to_hunt(x, cs)
    join(a, b, cs)
    join(a, d, cs)
    drop = kill_one(a, [b, c, d])
    got = try_pickup(c, drop, cs)
    check("outsider", got == ITEM_NOT_OWNER, f"파티 밖 사람의 줍기: {got}")
    # 탈퇴한 사람은 못 줍는다(드랍은 탈퇴 전에 생김)
    b.leave()
    wait_all(cs, lambda: b.states[-1] == (0, 0, []) and a.party()[2] == [a.id, d.id], 5.0, "탈퇴")
    pump_all(cs, 0.5)
    got = try_pickup(b, drop, cs)
    check("left", got == ITEM_NOT_OWNER, f"탈퇴한 사람의 줍기: {got}")
    # 드랍이 생긴 뒤에 든 파티원은 주울 수 있다(줍는 순간의 파티 번호) — 막타 본인이 아닌 파티원의 줍기
    join(a, c, cs)
    got = try_pickup(c, drop, cs)
    check("member", got == "ok", f"뒤에 든 파티원의 줍기: {got}")
    close_all(cs)


def t_midmove():
    # 서버: MoveHandoffDelayMs 800 — 포털 요청 뒤 약 1.6초 동안 이동 중이다.
    a, b, c = login(3)
    cs = [a, b, c]
    to_hunt(a, cs)
    to_hunt(c, cs)
    # b가 마을에서 초대를 받아 두고, 포털을 쓴 직후(이동 중)에 수락한다.
    a.invite(target=b)
    pid = mt.to_portal_101(b)
    wait_all(cs, lambda: b.invites, 5.0, "b의 초대")
    n = len(b.room_changes)
    b.use_portal(pid)
    time.sleep(0.4)
    b.respond(True)
    wait_all(cs, lambda: len(b.room_changes) > n and b.id in a.party()[2], 10.0, "이동 + 합류")
    pump_all(cs, 1.0)
    same_party([a, b], a, [a, b])
    check("kind", a.kinds().get(b.id) == mk.KIND_FIELD, f"있는 곳 {a.kinds()}")
    b.walk_to(HUNT[0], HUNT[1], stop_cm=150, tz=HUNT[2], limit_s=60)
    drop = kill_one(a, [b, c])
    got = try_pickup(c, drop, cs)
    check("outsider", got == ITEM_NOT_OWNER, f"파티 밖 사람의 줍기: {got}")
    got = try_pickup(b, drop, cs)
    check("member", got == "ok", f"이동 중에 합류한 파티원의 줍기: {got}")
    # 이동 중에 탈퇴: 사냥터 → 마을 포털 직후 탈퇴한다. 파티는 남아 있어야 하므로(2명이면 해산) 마을에 있는 d를 먼저 넣는다.
    d = Client("Test4").enter()
    cs.append(d)
    join(a, d, cs)
    fx, fy, fz, fr = b.portals[201]
    b.walk_to(fx, fy, stop_cm=120, tz=fz, limit_s=60)
    fpid = b.goto_portal(201)
    n = len(b.room_changes)
    b.use_portal(fpid)
    time.sleep(0.4)
    b.leave()
    wait_all(cs, lambda: len(b.room_changes) > n and b.states[-1] == (0, 0, []), 10.0, "이동 + 탈퇴")
    pump_all(cs, 1.2)
    same_party([a, d], a, [a, d])
    to_hunt(b, cs)
    to_hunt(d, cs)
    drop = kill_one(d, [a, b, c])   # 두 번째 처치는 HP가 온전한 d가 한다
    got = try_pickup(b, drop, cs)
    check("left", got == ITEM_NOT_OWNER, f"이동 중에 탈퇴한 사람의 줍기: {got} (파티 번호 사본이 남아 있으면 주워진다)")
    got = try_pickup(a, drop, cs)
    check("member2", got == "ok", f"파티원의 줍기: {got}")
    close_all(cs)


def t_full():
    a, b, c, d = cs = login(4)
    join(a, b, cs)
    join(a, c, cs)
    a.invite(target=d)
    wait_all(cs, lambda: a.results, 5.0, "PARTY_FULL")
    check("full", a.take_results() == [(CS_PARTY_INVITE, R_PARTY_FULL)] and not d.invites, "결과가 다름")
    # 한 명이 나가면 다시 초대할 수 있다
    c.leave()
    wait_all(cs, lambda: len(a.party()[2]) == 2, 5.0, "탈퇴")
    join(a, d, cs)
    same_party([a, b, d], a, [a, b, d])
    close_all(cs)


def t_timeout():
    a, b = cs = login(2)
    a.invite(target=b)
    wait_all(cs, lambda: b.invites, 5.0, "초대")
    check("sec", b.invites[0][2] == 3, f"응답 시간 {b.invites[0][2]}")
    t0 = time.time()
    wait_all(cs, lambda: a.results and b.results, 8.0, "TIMEOUT")
    took = time.time() - t0
    check("timeout", a.take_results() == [(CS_PARTY_INVITE, R_TIMEOUT)] and b.take_results() == [(CS_PARTY_RESPOND, R_TIMEOUT)] and 2.0 <= took <= 4.5, f"결과가 다르거나 {took:.1f}초")
    b.respond(True)
    wait_all(cs, lambda: b.results, 5.0, "NO_INVITE")
    check("no-invite", b.take_results() == [(CS_PARTY_RESPOND, R_NO_INVITE)] and not a.states, "결과가 다름")
    join(a, b, cs)
    # 수락한 초대의 시간 초과 잡은 아무 일도 하지 않는다
    pump_all(cs, 4.0)
    check("quiet", not a.results and not b.results, f"수락 뒤에 결과가 옴: {a.results} {b.results}")
    same_party(cs, a, [a, b])
    close_all(cs)


def t_channel():
    a, b, c = cs = login(3)   # 채널 정원 2 → c는 채널 2
    a.invite(name=c.name)
    wait_all(cs, lambda: a.results, 5.0, "OTHER_CHANNEL")
    check("other-channel", a.take_results() == [(CS_PARTY_INVITE, R_OTHER_CHANNEL)] and not c.invites, "결과가 다름")
    check("unseen", c.id not in a.objects and a.id not in c.objects, "다른 채널인데 보임")
    join(a, b, cs)
    close_all(cs)


CASES = {"basic": t_basic, "rooms": t_rooms, "leader": t_leader, "cut": t_cut, "errors": t_errors, "loot": t_loot,
         "full": t_full, "timeout": t_timeout, "channel": t_channel, "midmove": t_midmove}

if __name__ == "__main__":
    failed = 0
    for name in sys.argv[1:]:
        try:
            CASES[name]()
            print(f"PASS {name}")
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: {type(e).__name__}: {e}")
        time.sleep(3.0)   # 접속 정리(마지막 저장)가 끝나야 같은 계정으로 바로 다시 로그인된다
    sys.exit(1 if failed else 0)
