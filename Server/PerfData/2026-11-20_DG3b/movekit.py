# DG3b Room 이동 시험 공용 코드(원시 소켓). rawsock.py 위에 protobuf 해석·클라이언트 상태를 얹는다. 표준 라이브러리만 쓴다.
import math, os, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rawsock as rs

CS_MOVE, CS_ATTACK, CS_REVIVE, CS_DEV_GIVE = 1004, 1008, 1012, 1017
CS_TRADE_REQUEST, CS_TRADE_RESPOND, CS_PORTAL = 1018, 1019, 1034
SC_LOGIN, SC_ADD_OBJECT, SC_REMOVE_OBJECT, SC_MOVE_OBJECT, SC_DEAD, SC_REVIVE = 2000, 2003, 2004, 2006, 2010, 2015
SC_INVENTORY, SC_INVENTORY_UPDATE, SC_ITEM_RESULT = 2016, 2017, 2018
SC_TRADE_REQUEST, SC_TRADE_STATE, SC_TRADE_END, SC_TRADE_RESULT = 2020, 2021, 2022, 2023
SC_TELEPORT, SC_PORTAL_LIST, SC_ROOM_CHANGE, SC_MOVE_RESULT = 2030, 2031, 2036, 2037
TYPE_PLAYER, TYPE_MONSTER, TYPE_NPC, TYPE_ITEM = 1, 2, 3, 4
KIND_TOWN, KIND_FIELD = 1, 2
MOVE_TIMEOUT, MOVE_NOT_AVAILABLE = 4, 5
ITEM_SUCCESS, ITEM_BUSY = 1, 4
TRADE_MOVED = 17


# ---- protobuf 해석(필요한 만큼): {필드 번호: [값...]} — varint는 int, 32비트는 bytes(4), 길이 구분은 bytes ----
def decode(data):
    out, i, n = {}, 0, len(data)
    while i < n:
        key, shift = 0, 0
        while True:
            b = data[i]; i += 1
            key |= (b & 0x7F) << shift; shift += 7
            if not b & 0x80: break
        field, wire = key >> 3, key & 7
        if wire == 0:
            v, shift = 0, 0
            while True:
                b = data[i]; i += 1
                v |= (b & 0x7F) << shift; shift += 7
                if not b & 0x80: break
        elif wire == 5:
            v = data[i:i + 4]; i += 4
        elif wire == 1:
            v = data[i:i + 8]; i += 8
        elif wire == 2:
            ln, shift = 0, 0
            while True:
                b = data[i]; i += 1
                ln |= (b & 0x7F) << shift; shift += 7
                if not b & 0x80: break
            v = data[i:i + ln]; i += ln
        else:
            raise ValueError("wire %d" % wire)
        out.setdefault(field, []).append(v)
    return out


def first(msg, field, default=0):
    return msg[field][0] if field in msg else default


def fl(msg, field):
    return struct.unpack("<f", msg[field][0])[0] if field in msg else 0.0


def pos_of(data):
    m = decode(data)
    return fl(m, 1), fl(m, 2), fl(m, 3), fl(m, 4)


def f_float(field, v):
    return rs.varint(field << 3 | 5) + struct.pack("<f", v)


def f_msg(field, body):
    return rs.varint(field << 3 | 2) + rs.varint(len(body)) + body


class Client:
    """Test 계정(또는 더미) 한 명. pump()가 받은 패킷으로 상태를 맞춘다."""

    def __init__(self, account, dummy=False, tag=None):
        self.tag = tag or account
        self.sock = rs.connect()
        self.reader = rs.Reader(self.sock)
        self.sock.sendall(rs.login(account, "1234", dummy=dummy))
        body = self.reader.wait_for(SC_LOGIN, 40.0)
        if body is None:
            raise RuntimeError(f"[{self.tag}] SC_LOGIN 없음(끊김)")
        info = decode(first(decode(body), 1, b""))
        self.id = first(info, 1)
        self.x = self.y = self.z = self.yaw = 0.0
        self.seq = 0
        self.objects = {}        # id -> (type, x, y, z)
        self.portals = {}        # id -> (x, y, z, r)
        self.room_changes = []   # (time, kind, x, y, seq)
        self.move_results = []
        self.item_results = []   # (time, request, result)
        self.inv_updates = []    # (time, gold)
        self.trade_requests = []
        self.trade_ends = []
        self.gold = None
        self.dead = False
        self.revive_delay = 0
        self.revives = 0
        self.spawned = False
        self.closed = False
        self.log = []            # (time, packet id) 순서 확인용

    def enter(self):
        self.sock.sendall(rs.enter_room())
        self.pump_until(lambda: self.spawned, 10.0, "내 SC_ADD_OBJECT")
        self.pump(0.5)
        return self

    def handle(self, pid, body):
        now = time.time()
        self.log.append((now, pid))
        if pid == SC_ADD_OBJECT:
            info = decode(first(decode(body), 1, b""))
            oid, typ = first(info, 1), first(info, 2)
            x, y, z, yaw = pos_of(first(info, 4, b""))
            if oid == self.id:
                # 처음 한 번만 내 위치로 쓴다(Room 이동 뒤에는 SC_ROOM_CHANGE가 먼저 준다)
                if not self.spawned:
                    self.x, self.y, self.z, self.yaw = x, y, z, yaw
                self.spawned = True
            else:
                self.objects[oid] = (typ, x, y, z)
        elif pid == SC_MOVE_OBJECT:
            # 다른 오브젝트의 이동(SC_MOVE_PACKET.object_info) — 몬스터를 따라가려면 위치를 맞춰 둔다
            info = decode(first(decode(body), 1, b""))
            oid = first(info, 1)
            m = decode(body)
            if oid == self.id:
                # 내 이동이 거부돼 서버가 위치를 되돌렸다(보정): 위치와 보정 순번을 받아야 이후 이동이 버려지지 않는다
                self.x, self.y, self.z, _ = pos_of(first(info, 4, b""))
                self.seq = first(m, 3)
                self.corrections = getattr(self, "corrections", 0) + 1
            elif oid in self.objects:
                x, y, z, yaw = pos_of(first(info, 4, b""))
                self.objects[oid] = (self.objects[oid][0], x, y, z)
        elif pid == SC_REMOVE_OBJECT:
            self.objects.pop(first(decode(body), 1), None)
        elif pid == SC_PORTAL_LIST:
            self.portals = {}
            for p in decode(body).get(1, []):
                m = decode(p)
                self.portals[first(m, 1)] = (fl(m, 2), fl(m, 3), fl(m, 4), fl(m, 5))
        elif pid == SC_ROOM_CHANGE:
            m = decode(body)
            self.x, self.y, self.z, self.yaw = pos_of(first(m, 4, b""))
            self.seq = first(m, 5)
            self.objects = {}
            self.portals = {}
            self.room_changes.append((now, first(m, 2), self.x, self.y, self.seq))
        elif pid == SC_TELEPORT:
            m = decode(body)
            if first(m, 1) == self.id:
                self.x, self.y, self.z, self.yaw = pos_of(first(m, 2, b""))
                self.seq = first(m, 3)
        elif pid == SC_MOVE_RESULT:
            self.move_results.append((now, first(decode(body), 1)))
        elif pid == SC_ITEM_RESULT:
            m = decode(body)
            self.item_results.append((now, first(m, 1), first(m, 2)))
        elif pid == SC_INVENTORY:
            self.gold = first(decode(body), 2)
        elif pid == SC_INVENTORY_UPDATE:
            self.gold = first(decode(body), 3)
            self.inv_updates.append((now, self.gold))
        elif pid == SC_DEAD:
            m = decode(body)
            if first(m, 1) == self.id:
                self.dead = True
                self.revive_delay = first(m, 2)
        elif pid == SC_REVIVE:
            m = decode(body)
            if first(m, 1) == self.id:
                self.dead = False
                self.revives += 1
                self.x, self.y, self.z, self.yaw = pos_of(first(m, 2, b""))
                self.seq = first(m, 4)
        elif pid == SC_TRADE_REQUEST:
            self.trade_requests.append(first(decode(body), 1))
        elif pid == SC_TRADE_END:
            self.trade_ends.append(first(decode(body), 1))

    def pump(self, seconds):
        """seconds 동안 받은 패킷을 처리한다."""
        end = time.time() + seconds
        while not self.closed:
            left = end - time.time()
            if left <= 0:
                return
            try:
                got = self.reader.read_packet(left)
            except TimeoutError:
                return
            if got is None:
                self.closed = True
                return
            self.handle(*got)

    def pump_until(self, cond, timeout, what=""):
        end = time.time() + timeout
        while not cond():
            if self.closed:
                raise RuntimeError(f"[{self.tag}] 끊김 — 기다리던 것: {what}")
            if time.time() >= end:
                raise TimeoutError(f"[{self.tag}] 시간 초과: {what}")
            self.pump(0.05)

    # ---- 보내기 ----
    def send(self, pid, body=b""):
        self.sock.sendall(rs.packet(pid, body))

    def send_move(self, x, y, z, vx=0.0, vy=0.0):
        pos = f_float(1, x) + f_float(2, y) + f_float(3, z) + f_float(4, self.yaw) + f_float(5, vx) + f_float(6, vy)
        self.send(CS_MOVE, f_msg(1, pos) + rs.f_int(3, self.seq))

    def walk_to(self, tx, ty, stop_cm=150.0, speed=380.0, step_s=0.2, tz=None, limit_s=120.0):
        """(tx, ty)에서 stop_cm 안까지 걷는다. 서버가 받아들인 위치를 따로 알 수 없어 보낸 위치를 내 위치로 본다(속도는 스탯보다 느리게)."""
        start = time.time()
        z0 = self.z
        total = math.hypot(tx - self.x, ty - self.y)
        while time.time() - start < limit_s:
            dx, dy = tx - self.x, ty - self.y
            dist = math.hypot(dx, dy)
            if dist <= stop_cm:
                self.send_move(self.x, self.y, self.z)   # 멈춤
                self.pump(0.1)
                return True
            step = min(speed * step_s, dist - stop_cm + 1.0)
            self.x += dx / dist * step
            self.y += dy / dist * step
            if tz is not None and total > 0:
                self.z = z0 + (tz - z0) * (1.0 - max(0.0, dist - step) / total)
            self.send_move(self.x, self.y, self.z, dx / dist * speed, dy / dist * speed)
            self.pump(step_s)
            if self.dead or self.closed:
                return False
        return False

    def nearest_portal(self):
        best = None
        for pid, (x, y, z, r) in self.portals.items():
            d = math.hypot(x - self.x, y - self.y)
            if best is None or d < best[1]:
                best = (pid, d, x, y)
        return best

    def goto_portal(self, portal_id=None):
        """가장 가까운(또는 지정한) 포털 반경 안까지 걷고 그 id를 돌려준다."""
        if portal_id is None:
            portal_id = self.nearest_portal()[0]
        x, y, z, r = self.portals[portal_id]
        if not self.walk_to(x, y, stop_cm=120.0):
            raise RuntimeError(f"[{self.tag}] 포털 {portal_id}까지 걷지 못함 (dead={self.dead})")
        return portal_id

    def use_portal(self, portal_id):
        self.send(CS_PORTAL, rs.f_int(1, portal_id))

    def attack(self, target_id, tx, ty, slot=0):
        self.attack_seq = getattr(self, "attack_seq", 0) + 1
        yaw = math.degrees(math.atan2(ty - self.y, tx - self.x))
        self.send(CS_ATTACK, rs.f_int(1, target_id) + rs.f_int(2, slot) + f_float(3, yaw) + f_float(4, tx) + f_float(5, ty) + rs.f_int(6, self.attack_seq))

    def give_gold(self, amount=1):
        self.send(CS_DEV_GIVE, rs.f_int(1, 0) + rs.f_int(2, amount))

    def close(self, rst=True):
        if self.closed:
            return
        self.closed = True
        try:
            if rst:
                rs.rst_close(self.sock)
            else:
                self.sock.close()
        except OSError:
            pass

    def monsters(self):
        return {i: o for i, o in self.objects.items() if o[0] == TYPE_MONSTER}

    def players(self):
        return {i: o for i, o in self.objects.items() if o[0] == TYPE_PLAYER}


# ---- DB 합계(sqlcmd — 필터 인덱스 때문에 -I) ----
def db_scalar(query):
    out = subprocess.run(["sqlcmd", "-S", r".\SQLEXPRESS", "-d", "GameServer", "-E", "-I", "-h", "-1", "-W", "-Q", "SET NOCOUNT ON; " + query],
                         capture_output=True, text=True, timeout=60).stdout.strip().splitlines()
    return out[0].strip() if out else ""


def db_totals():
    """(아이템 수량 합계, 골드 합계) — 시험 전후가 같아야 한다(일부러 준 양은 따로 뺀다)."""
    items = db_scalar("SELECT ISNULL(SUM(CAST([Count] AS BIGINT)), 0) FROM Player_Item")
    gold = db_scalar("SELECT ISNULL(SUM(Gold), 0) FROM Player_Info")
    return int(items), int(gold)
