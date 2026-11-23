# A2.3b 성장 + MP 원시 소켓 시험(계정 Test1~Test4). 사용: python -I -X utf8 growth_test.py <포트> <묶음>
#   main  ([Debug] DevCommands=true, 원본 DataTable) — 로그인 값·자연 회복·스킬 MP·처치 경험치(1인)·파티 분배(2인)·DEV 경험치 여러 레벨·30 상한·잘못된 값·
#         활 피해 공식(3001·3002 틱)·재접속 유지
#   err   (DevCommands=false, SkillTable 사본: 2002 MpCost 100·CooldownMs 1) — DEV 경험치 거부, MP 0에서 스킬 연타, 잘못된 패킷
# 판정 줄은 "PASS <이름>" / "FAIL <이름>: 이유". 시각은 서버 패킷을 받은 시각(초, 시험 시작 기준)으로 남긴다.
# 공용 코드는 DG3b movekit.py·DG2 party_test.py(로그인·사냥 자리·처치·파티)를 그대로 쓴다.
import csv, math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG3b"))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-20_DG2"))
import movekit as mk
import party_test as pt

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8899
_orig_connect = mk.rs.connect
mk.rs.connect = lambda **kw: _orig_connect(port=PORT, **kw)

SC_ATTACK, SC_DAMAGE, SC_CHANGE_HP, SC_ATTACK_RESULT = 2008, 2009, 2028, 2032
SC_GAIN_EXP, SC_LEVEL_UP, SC_CHANGE_MP = 2039, 2040, 2041
CS_CHANGE_EQUIP = 1009
AR_ACTING, AR_COOLDOWN, AR_NO_SKILL, AR_NOT_ENOUGH_MP = 3, 4, 5, 11
ITEM_NO_ITEM, ITEM_NOT_ALLOWED, ITEM_TOO_FREQUENT = 7, 6, 5
T0 = time.time()
DATA = os.environ.get("JH_DATA_DIR") or os.path.join(HERE, "..", "..", "..", "Common", "DataTable")
RESULTS = []


def ts(t=None):
    return (t or time.time()) - T0


def log(msg):
    print(f"[{ts():7.2f}] {msg}", flush=True)


def check(name, cond, why=""):
    if cond:
        RESULTS.append(("PASS", name, ""))
        print(f"PASS {name}", flush=True)
    else:
        RESULTS.append(("FAIL", name, why))
        print(f"FAIL {name}: {why}", flush=True)
    return cond


def table(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as f:
        return {int(r["Name"]): r for r in csv.DictReader(f)}


PSTAT = {int(r["Level"]): r for r in table("PlayerStatTable.csv").values()}
SKILL = table("SkillTable.csv")
MON = table("MonsterTable.csv")
ITEMS = table("ItemTable.csv")
MAX_LEVEL = max(PSTAT)


def stat_of(raw):
    m = mk.decode(raw)
    return {"max_hp": mk.first(m, 1), "hp": mk.first(m, 2), "atk": mk.first(m, 3), "max_mp": mk.first(m, 6), "mp": mk.first(m, 7)}


def f_i64(field, v):
    return mk.rs.varint(field << 3) + mk.rs.varint(v & ((1 << 64) - 1))


class C(pt.Client):
    """party_test.Client + 성장·MP 패킷 기록"""

    def __init__(self, account, **kw):
        self.info = {}           # 내 SC_ADD_OBJECT의 ObjectInfo 필드
        self.others = {}         # 다른 플레이어 id -> 그 ObjectInfo(디코드)
        self.exp_msgs = []       # (t, exp, gained)
        self.levelups = []       # (t, object_id, level, stat)
        self.mp_msgs = []        # (t, mp)
        self.hp_msgs = []        # (t, object_id, hp)
        self.damages = []        # (t, attacker, target, damage, remain)
        self.attacks = []        # (t, attacker, skill, seq)
        self.attack_results = [] # (t, result, seq, slot)
        self.inv_rows = {}       # item_db_id -> (item_id, equip_part, slot)
        self.equip_changes = []  # (t, object_id, part, item_id, stat)
        self.level, self.exp = 0, 0
        self.stat = {"max_hp": 0, "hp": 0, "atk": 0, "max_mp": 0, "mp": 0}
        super().__init__(account, **kw)

    def handle(self, pid, body):
        now = ts()
        if pid == mk.SC_ADD_OBJECT:
            info = mk.decode(mk.first(mk.decode(body), 1, b""))
            if mk.first(info, 1) == self.id:
                self.info = info
                self.level = mk.first(info, 11)
                self.exp = mk.first(info, 14)
                self.stat = stat_of(mk.first(info, 5, b""))
            elif mk.first(info, 2) == mk.TYPE_PLAYER:
                self.others[mk.first(info, 1)] = info
        elif pid == SC_GAIN_EXP:
            m = mk.decode(body)
            self.exp = mk.first(m, 1)
            self.exp_msgs.append((now, mk.first(m, 1), mk.first(m, 2)))
        elif pid == SC_LEVEL_UP:
            m = mk.decode(body)
            st = stat_of(mk.first(m, 3, b""))
            self.levelups.append((now, mk.first(m, 1), mk.first(m, 2), st))
            if mk.first(m, 1) == self.id:
                self.level, self.stat = mk.first(m, 2), st
        elif pid == SC_CHANGE_MP:
            mp = mk.first(mk.decode(body), 1)
            self.mp_msgs.append((now, mp))
            self.stat["mp"] = mp
        elif pid == SC_CHANGE_HP:
            m = mk.decode(body)
            self.hp_msgs.append((now, mk.first(m, 1), mk.first(m, 2)))
            if mk.first(m, 1) == self.id:
                self.stat["hp"] = mk.first(m, 2)
        elif pid == SC_DAMAGE:
            m = mk.decode(body)
            self.damages.append((now, mk.first(m, 1), mk.first(m, 2), mk.first(m, 3), mk.first(m, 4)))
            if mk.first(m, 2) == self.id:
                self.stat["hp"] = mk.first(m, 4)
        elif pid == SC_ATTACK:
            m = mk.decode(body)
            self.attacks.append((now, mk.first(m, 1), mk.first(m, 2), mk.first(m, 8)))
        elif pid == SC_ATTACK_RESULT:
            m = mk.decode(body)
            self.attack_results.append((now, mk.first(m, 1), mk.first(m, 2), mk.first(m, 3)))
        elif pid in (mk.SC_INVENTORY, mk.SC_INVENTORY_UPDATE):
            for raw in mk.decode(body).get(1, []):
                r = mk.decode(raw)
                self.inv_rows[mk.first(r, 1)] = (mk.first(r, 2), mk.first(r, 4), mk.first(r, 5))
            if pid == mk.SC_INVENTORY_UPDATE:
                for gone in mk.decode(body).get(2, []):
                    self.inv_rows.pop(gone, None)
        elif pid == 2011:   # SC_CHANGE_EQUIP
            m = mk.decode(body)
            self.equip_changes.append((now, mk.first(m, 1), mk.first(m, 2), mk.first(m, 3), stat_of(mk.first(m, 4, b""))))
            if mk.first(m, 1) == self.id:
                self.stat = stat_of(mk.first(m, 4, b""))
        super().handle(pid, body)

    def cast(self, slot, target_id=0, tx=None, ty=None):
        if tx is None:
            tx, ty = self.x + 300.0, self.y
        self.attack(target_id, tx, ty, slot=slot)
        return self.attack_seq

    def dev_exp(self, amount):
        self.send(mk.CS_DEV_GIVE, f_i64(3, amount))

    def equip(self, part, db_id):
        self.send(CS_CHANGE_EQUIP, mk.rs.f_int(1, part) + mk.rs.f_int(3, db_id))


def pump_all(cs, s):
    pt.pump_all(cs, s)


def wait_all(cs, cond, timeout, what):
    pt.wait_all(cs, cond, timeout, what)


def expected_stat(level, account_items):
    """PlayerStatTable[level] + 장착 아이템 보너스 (클라이언트가 받은 장착 행 기준)"""
    row = PSTAT[level]
    hp, atk, mp = int(row["MaxHp"]), int(row["AttackDamage"]), int(row["MaxMp"])
    for item_id, part, slot in account_items.values():
        if part:
            it = ITEMS[item_id]
            hp += int(it["BonusMaxHp"])
            atk += int(it["BonusAttackDamage"])
    return {"max_hp": hp, "atk": atk, "max_mp": mp}


def regen(m, pct):
    """서버와 같은 회복량: 최대치 × 비율 ÷ 100 반올림, 최소 1"""
    return max(1, (m * pct + 50) // 100)


def boost(cl, level, everyone):
    """DEV 경험치로 cl을 level까지 올린다(새 몬스터 수치는 1001 = Lv5 기준이라 1레벨 시험 계정은 오래 싸우면 죽는다 — D-253 §7)."""
    if cl.level >= level:
        return
    need = sum(int(PSTAT[l]["ExpToNext"]) for l in range(cl.level, level)) - cl.exp
    n = len(cl.levelups)
    time.sleep(0.15)
    cl.dev_exp(need)
    wait_all(everyone, lambda: len(cl.levelups) > n and cl.level >= level, 5.0, f"{cl.tag} 레벨 {level}")
    pump_all(everyone, 0.3)
    log(f"{cl.tag} 레벨 {cl.level}로 올림(DEV {need}) stat {cl.stat}")


def dmg(skill_id, atk):
    s = SKILL[skill_id]
    return int(s["BaseDamage"]) + atk * int(s["AttackScale"]) // 100


def kill_monster(killer, others, skill_slot=None, limit=120.0):
    """killer가 가장 가까운 몬스터를 잡는다. skill_slot을 주면 내 첫 타격이 들어간 뒤(= 전투 중) 그 칸을 한 번 쓴다. 잡은 몬스터 id."""
    everyone = [killer, *others]
    end = time.time() + limit
    target, used_skill = None, skill_slot is None
    n_dmg0, n_res0 = len(killer.damages), len(killer.attack_results)
    last_hit_t = time.time()
    while time.time() < end:
        mons = {i: o for i, o in killer.monsters().items() if i not in killer.dead_ids}
        if target not in mons:
            if not mons:
                raise RuntimeError(f"{killer.tag} 주변에 몬스터가 없음")
            target = min(mons, key=lambda i: math.hypot(mons[i][1] - killer.x, mons[i][2] - killer.y))
            last_hit_t = time.time()
        _, mx, my, mz = mons[target]
        if math.hypot(mx - killer.x, my - killer.y) > 170 or time.time() - last_hit_t > 6.0:
            killer.walk_to(mx, my, stop_cm=100, tz=mz, limit_s=20)
            last_hit_t = time.time()
            continue
        hit_me = [d for d in killer.damages[n_dmg0:] if d[1] == killer.id and d[2] == target]
        if hit_me:
            last_hit_t = max(last_hit_t, T0 + hit_me[-1][0])
        slot = 0
        if not used_skill and hit_me:
            slot, used_skill = skill_slot, True
        killer.attack(target, mx, my, slot=slot)
        pump_all(everyone, 1.6 if slot == 0 else 2.4)   # 동작 시간만큼 기다린다(앞 동작 중 요청은 ACTING으로 거부됨)
        if killer.dead:
            raise RuntimeError(f"{killer.tag}이 죽음")
        # 부채꼴은 옆 몬스터도 맞힌다 — 내 타격으로 HP가 0이 된 몬스터가 있으면 그것이 잡은 몬스터
        killed = [d[2] for d in killer.damages[n_dmg0:] if d[1] == killer.id and d[4] == 0]
        if killed or target in killer.dead_ids:
            pump_all(everyone, 0.5)
            return killed[0] if killed else target
    raise TimeoutError(f"{killer.tag}이 몬스터를 잡지 못함: target={target} 내 위치 ({killer.x:.0f},{killer.y:.0f}) 몬스터 {killer.monsters().get(target)} "
                       f"내 타격 {[d[3] for d in killer.damages[n_dmg0:] if d[1] == killer.id]} 거부 {[r[1] for r in killer.attack_results[n_res0:]]}")


# =====================================================================================================
def t_main():
    a = C("Test1").enter()   # 맨손, HP 300/650(시험 전 DB 값)
    b = C("Test2").enter()   # 천 한손검
    cs = [a, b]
    pump_all(cs, 1.0)

    # ---- 1. 로그인 값: 본인 ObjectInfo에 경험치·MP, 남의 ObjectInfo에는 경험치 없음 ----
    exp_a = expected_stat(a.level, a.inv_rows)
    check("login-self-exp", 14 in a.info or a.exp == 0, f"exp 필드 {a.info.get(14)}")
    check("login-mp-full", a.stat["max_mp"] == exp_a["max_mp"] and a.stat["mp"] == a.stat["max_mp"], f"Test1 stat {a.stat} 기대 {exp_a}")
    check("login-stat", a.stat["max_hp"] == exp_a["max_hp"] and a.stat["atk"] == exp_a["atk"], f"Test1 stat {a.stat} 기대 {exp_a}")
    other = b.others.get(a.id, {})
    check("other-no-exp", 14 not in other, f"Test2가 받은 Test1 ObjectInfo에 exp {other.get(14)}")
    log(f"Test1 level={a.level} exp={a.exp} stat={a.stat} / Test2 level={b.level} exp={b.exp} stat={b.stat}")
    boost(b, 5, cs)   # Test2는 사냥을 하므로 1001 기준 레벨(5)로

    # ---- 2. HP 자연 회복(전투 밖 5초마다 20%) — Test1은 HP가 덜 찬 채 들어옴. Test2도 AOI로 받음 ----
    if a.stat["hp"] < a.stat["max_hp"]:
        start_hp, t_enter = a.stat["hp"], ts()
        wait_all(cs, lambda: a.stat["hp"] >= a.stat["max_hp"], 30.0, "Test1 HP 가득")
        pump_all(cs, 0.5)
        mine = [(t, hp) for t, oid, hp in a.hp_msgs if oid == a.id]
        seen = [(t, hp) for t, oid, hp in b.hp_msgs if oid == a.id]
        step = regen(a.stat["max_hp"], 20)
        gaps = [round(mine[i][0] - mine[i - 1][0], 2) for i in range(1, len(mine))]
        hps = [hp for _, hp in mine]
        want = []
        h = start_hp
        while h < a.stat["max_hp"]:
            h = min(a.stat["max_hp"], h + step)
            want.append(h)
        log(f"HP 회복 Test1 {start_hp} → {hps} 시각 {[round(t, 2) for t, _ in mine]} 간격 {gaps} (입장 {t_enter:.2f})")
        check("hp-regen-values", hps == want, f"받은 {hps} 기대 {want}")
        check("hp-regen-interval", all(4.5 <= g <= 5.5 for g in gaps), f"간격 {gaps}")
        check("hp-regen-aoi", [hp for _, hp in seen] == hps, f"Test2가 본 값 {seen}")
        check("mp-full-no-packet", not a.mp_msgs, f"MP가 가득인데 SC_CHANGE_MP {a.mp_msgs}")
    else:
        check("hp-regen-skip", False, f"Test1 HP가 이미 가득 {a.stat}")

    # ---- 3. 스킬 MP 소모(Test2, 검 스킬 2002 MpCost 20) + 전투 밖 MP 회복(초당 5%) ----
    cost = int(SKILL[2002]["MpCost"])
    mp0 = b.stat["mp"]
    n_att = len(b.attacks)
    seq = b.cast(1)
    wait_all(cs, lambda: len(b.attacks) > n_att, 5.0, "Test2 2002 SC_ATTACK")
    check("mp-spend", b.mp_msgs and b.mp_msgs[0][1] == mp0 - cost, f"SC_CHANGE_MP {b.mp_msgs[:1]} 기대 {mp0 - cost}")
    check("mp-before-attack", b.mp_msgs and b.mp_msgs[0][0] <= b.attacks[n_att][0], "SC_CHANGE_MP가 SC_ATTACK보다 늦음")
    wait_all(cs, lambda: b.stat["mp"] >= b.stat["max_mp"], 15.0, "Test2 MP 가득")
    pump_all(cs, 2.5)
    rg = b.mp_msgs[1:]
    vals = [v for _, v in rg]
    gaps = [round(rg[i][0] - rg[i - 1][0], 2) for i in range(1, len(rg))]
    step = regen(b.stat["max_mp"], 5)
    log(f"MP 회복 Test2 {mp0 - cost} → {vals} 간격 {gaps}")
    check("mp-regen-idle", vals == [min(b.stat["max_mp"], mp0 - cost + step * (i + 1)) for i in range(len(vals))] and vals[-1] == b.stat["max_mp"], f"값 {vals} (초당 {step})")
    check("mp-regen-interval", all(0.8 <= g <= 1.2 for g in gaps), f"간격 {gaps}")
    check("mp-regen-stops", len(b.mp_msgs) == 1 + len(vals), "가득 찬 뒤에도 SC_CHANGE_MP")

    # ---- 4. 처치 경험치(1인): Test2가 사냥터에서 1001을 잡음. 첫 공격은 2002(전투 중 MP 회복 1%), 피해 공식 확인 ----
    pt.to_hunt(b, [a])
    exp_before = b.exp
    n_mp, n_dmg, n_hp, n_att = len(b.mp_msgs), len(b.damages), len(b.hp_msgs), len(b.attacks)
    mid = kill_monster(b, [a], skill_slot=1)
    t_kill = ts()
    wait_all(cs, lambda: len(b.exp_msgs) >= 1, 3.0, "Test2 SC_GAIN_EXP")
    mon_exp = int(MON[1001]["Exp"])
    check("kill-exp-solo", b.exp_msgs[-1][1:] == (exp_before + mon_exp, mon_exp), f"SC_GAIN_EXP {b.exp_msgs[-1]} 기대 ({exp_before + mon_exp}, {mon_exp})")
    check("kill-exp-not-others", not a.exp_msgs, f"Test1(마을)이 경험치를 받음 {a.exp_msgs}")
    mine = [d for d in b.damages[n_dmg:] if d[1] == b.id]
    # SC_DAMAGE는 실제로 깎은 양이라 마지막 타격은 남은 HP만큼(잘림)이다 — 마지막 타격의 remain_hp가 0이면 빼고 본다
    hits = sorted({d[3] for d in mine if d[4] > 0})
    want = {dmg(2002, b.stat["atk"]), dmg(2001, b.stat["atk"])}
    own = [(round(t, 2), s) for t, at, s, q in b.attacks[n_att:] if at == b.id]
    log(f"Test2 → 몬스터 피해 {[(d[3], d[4]) for d in mine]} (기대 2002 {dmg(2002, b.stat['atk'])} / 2001 {dmg(2001, b.stat['atk'])}), 몬스터 HP {MON[1001]['MaxHp']}, 내 시전 {own}, 거부 {b.attack_results}")
    check("damage-formula-sword", set(hits) <= want and dmg(2002, b.stat["atk"]) in hits, f"피해 값 {hits} 기대 {sorted(want)}")
    taken = [d for d in b.damages[n_dmg:] if d[2] == b.id]
    mon_hit = int(SKILL[9001]["BaseDamage"]) + int(MON[1001]["AttackDamage"]) * int(SKILL[9001]["AttackScale"]) // 100
    check("damage-formula-monster", all(d[3] == mon_hit for d in taken), f"몬스터 → Test2 피해 {[d[3] for d in taken]} 기대 {mon_hit}")
    # 전투 중 MP 회복은 초당 1%(최대 MP 100이면 1), 전투가 끝나고 5초 뒤부터 5%
    pump_all(cs, 12.0)
    last_hit = max([d[0] for d in b.damages[n_dmg:] if d[1] == b.id or d[2] == b.id] or [t_kill])
    seq_mp = b.mp_msgs[n_mp:]
    deltas = [(seq_mp[i][0], seq_mp[i][1] - seq_mp[i - 1][1]) for i in range(1, len(seq_mp))]   # 첫 값 = 2002 소모
    in_combat = [d for t, d in deltas if t < last_hit + 4.8]
    out_combat = [d for t, d in deltas if t > last_hit + 5.2 and d > 0]
    log(f"Test2 MP {[(round(t, 2), v) for t, v in seq_mp]} 마지막 타격 {last_hit:.2f}")
    one, five = regen(b.stat["max_mp"], 1), regen(b.stat["max_mp"], 5)
    check("mp-regen-combat", in_combat and all(d == one for d in in_combat), f"전투 중 증가 {in_combat} (기대 {one})")
    check("mp-regen-after-combat", out_combat and all(d == five or seq_mp[-1][1] == b.stat["max_mp"] for d in out_combat), f"전투 뒤 증가 {out_combat} (기대 {five})")
    # HP 회복: 전투가 끝난 뒤(마지막 타격 + 5초)부터 5초마다
    hp_after = [(t, hp) for t, oid, hp in b.hp_msgs[n_hp:] if oid == b.id]
    log(f"Test2 HP 회복 {[(round(t, 2), hp) for t, hp in hp_after]} (마지막 타격 {last_hit:.2f})")
    if taken:
        check("hp-regen-after-combat", hp_after and hp_after[0][0] >= last_hit + 9.0 and hp_after[0][0] <= last_hit + 11.5,
              f"첫 회복 {hp_after[:1]} (마지막 타격 + 10초 예상 {last_hit + 10:.2f})")

    # ---- 5. 파티 분배(2인): Test2(파티장)가 Test1·Test3을 초대. Test1은 사냥터의 시야 안, Test3은 마을(다른 Room) ----
    c = C("Test3").enter()
    cs = [a, b, c]
    pt.join(b, a, cs)
    pt.join(b, c, cs)
    pt.to_hunt(a, [b, c])
    wait_all(cs, lambda: a.id in b.objects, 10.0, "Test2 시야에 Test1")
    ea, eb, ec = len(a.exp_msgs), len(b.exp_msgs), len(c.exp_msgs)
    xa, xb = a.exp, b.exp
    kill_monster(b, [a, c])
    wait_all(cs, lambda: len(a.exp_msgs) > ea and len(b.exp_msgs) > eb, 3.0, "파티 SC_GAIN_EXP")
    pump_all(cs, 0.5)
    share = mon_exp * (9 + 2) // (10 * 2)
    log(f"파티 분배: Test1 {a.exp_msgs[ea:]} Test2 {b.exp_msgs[eb:]} Test3 {c.exp_msgs[ec:]} (1인 몫 {mon_exp}, 2인 몫 {share})")
    check("party-share-2", a.exp_msgs[ea][1:] == (xa + share, share) and b.exp_msgs[eb][1:] == (xb + share, share), f"Test1 {a.exp_msgs[ea:]} Test2 {b.exp_msgs[eb:]} 기대 {share}")
    check("party-other-room-excluded", len(c.exp_msgs) == ec, f"Test3(마을)이 받음 {c.exp_msgs[ec:]}")

    # ---- 6. DEV 경험치로 여러 레벨 한 번에(Test1): SC_LEVEL_UP 한 번(본인·AOI), 스탯 = 테이블, HP·MP 가득, 파티 창 레벨 ----
    amount = 4000
    lvl, e = a.level, a.exp + amount
    while lvl < MAX_LEVEL and e >= int(PSTAT[lvl]["ExpToNext"]):
        e -= int(PSTAT[lvl]["ExpToNext"]); lvl += 1
    nl_a, nl_b, ns_b, ns_c = len(a.levelups), len(b.levelups), len(b.states), len(c.states)
    a.dev_exp(amount)
    wait_all(cs, lambda: len(a.exp_msgs) > ea + 1 and len(b.levelups) > nl_b, 5.0, "DEV 경험치 결과")
    pump_all(cs, 1.0)
    want = expected_stat(lvl, a.inv_rows)
    lu = a.levelups[nl_a:]
    log(f"DEV {amount}: Test1 levelups {lu} exp {a.exp_msgs[-1]} 기대 level {lvl} exp {e} stat {want}")
    check("levelup-once", len(lu) == 1 and lu[0][2] == lvl, f"SC_LEVEL_UP {lu}")
    st = lu[0][3] if lu else {}
    check("levelup-stat", st.get("max_hp") == want["max_hp"] and st.get("atk") == want["atk"] and st.get("max_mp") == want["max_mp"], f"{st} 기대 {want}")
    check("levelup-full", st.get("hp") == st.get("max_hp") and st.get("mp") == st.get("max_mp"), f"{st}")
    check("levelup-exp", a.exp_msgs[-1][1:] == (e, amount), f"SC_GAIN_EXP {a.exp_msgs[-1]} 기대 ({e}, {amount})")
    check("levelup-aoi", len(b.levelups) == nl_b + 1 and b.levelups[-1][1:3] == (a.id, lvl), f"Test2가 받은 {b.levelups[nl_b:]}")
    check("levelup-order", a.levelups[-1][0] <= a.exp_msgs[-1][0], "SC_GAIN_EXP가 SC_LEVEL_UP보다 먼저 옴")
    lv_in = lambda cl: {m[0]: m[2] for m in cl.states[-1][2]}.get(a.id) if cl.states else None
    check("party-level-update", len(b.states) > ns_b and len(c.states) > ns_c and lv_in(b) == lvl and lv_in(c) == lvl, f"Test2 {b.states[ns_b:]} Test3 {c.states[ns_c:]}")

    # ---- 7. 30 상한, 잘못된 값 ----
    time.sleep(0.15)
    a.dev_exp(1_000_000_000)
    wait_all(cs, lambda: a.level == MAX_LEVEL, 5.0, "레벨 30")
    pump_all(cs, 0.5)
    want = expected_stat(MAX_LEVEL, a.inv_rows)
    check("cap-level", a.level == MAX_LEVEL and a.exp == 0 and a.exp_msgs[-1][1] == 0, f"level {a.level} exp {a.exp_msgs[-1]}")
    check("cap-stat", a.stat["max_hp"] == want["max_hp"] and a.stat["atk"] == want["atk"] and a.stat["max_mp"] == want["max_mp"], f"{a.stat} 기대 {want}")
    n_lu, n_ex = len(a.levelups), len(a.exp_msgs)
    time.sleep(0.15)
    a.dev_exp(100)
    pump_all(cs, 1.0)
    check("cap-no-more", len(a.levelups) == n_lu and a.exp_msgs[n_ex:] and a.exp_msgs[-1][1:] == (0, 0), f"levelups +{len(a.levelups) - n_lu} exp {a.exp_msgs[n_ex:]}")
    for bad in (-5, 1_000_000_001):
        n_res = len(a.item_results)
        time.sleep(0.15)
        a.dev_exp(bad)
        wait_all(cs, lambda: len(a.item_results) > n_res, 3.0, f"DEV {bad} 거부")
        check(f"dev-exp-bad-{bad}", a.item_results[-1][1:] == (mk.CS_DEV_GIVE, ITEM_NO_ITEM), f"{a.item_results[-1]}")
    # 30레벨이 처치하면 몫은 받되 쌓이지 않는다(gained 0)
    n_ex = len(a.exp_msgs)
    kill_monster(a, [b, c])
    wait_all(cs, lambda: len(a.exp_msgs) > n_ex, 3.0, "30레벨 처치 SC_GAIN_EXP")
    check("cap-kill", a.exp_msgs[-1][1:] == (0, 0), f"{a.exp_msgs[n_ex:]}")

    # ---- 8. 활 피해 공식(Test4: 6102 장착 → 3001 기본·3002 장판 틱) — 끝나면 검으로 되돌림 ----
    d = C("Test4").enter()
    cs4 = [a, b, c, d]
    pump_all(cs4, 0.5)
    bow = next(i for i, r in d.inv_rows.items() if r[0] == 6102 and r[1] == 0)
    sword = next(i for i, r in d.inv_rows.items() if r[1] == 6)
    d.equip(6, bow)
    wait_all(cs4, lambda: any(e[1] == d.id and e[3] == 6102 for e in d.equip_changes), 5.0, "활 장착")
    pump_all(cs4, 2.0)   # 무기 교체 잠금
    boost(d, 5, cs4)
    pt.to_hunt(d, [a, b, c])
    atk = d.stat["atk"]
    n_dmg = len(d.damages)
    mons = {i: o for i, o in d.monsters().items() if i not in d.dead_ids}
    target = min(mons, key=lambda i: math.hypot(mons[i][1] - d.x, mons[i][2] - d.y))
    _, mx, my, mz = mons[target]
    if math.hypot(mx - d.x, my - d.y) > 900:
        d.walk_to(mx, my, stop_cm=700, tz=mz, limit_s=20)
    # 3002 장판은 몬스터를 끌어온 뒤 내 자리에 깐다(2026-11-23 검증 게이트): 몬스터 발밑에 깔면 맞은 몬스터가 곧바로 쫓아 나와
    # (경직은 공격만 막고 이동은 막지 않음, 속도 400) 틱 수가 실행마다 1~5번으로 갈렸다. 실제 쓰임(내 앞에 깔고 붙은 몬스터가 머묾)과 같게 한다.
    d.attack(target, mx, my, slot=0)   # 3001로 끌어오기(1001은 맞아야 쫓아옴)
    t_pull = time.time()
    while time.time() - t_pull < 10.0:
        pump_all(cs4, 0.2)
        if target in d.dead_ids or target not in d.objects:
            break
        _, mx, my, mz = d.objects[target]
        if math.hypot(mx - d.x, my - d.y) <= 250 and time.time() - t_pull > 1.9:   # 붙었고 기본 공격 동작(1.73초)도 끝남
            break
    log(f"Test4 3002 시전: 몬스터 거리 {math.hypot(mx - d.x, my - d.y):.0f}cm ({time.time() - t_pull:.1f}초 뒤)")
    d.attack(target, d.x, d.y, slot=1)   # 3002 장판(지점 = 내 자리)
    pump_all(cs4, 1.0)
    for _ in range(12):
        if target in d.dead_ids or d.dead:
            break
        _, mx, my, mz = d.objects.get(target, (0, mx, my, mz))
        d.attack(target, mx, my, slot=0)
        pump_all(cs4, 1.9)
    pump_all(cs4, 1.0)
    vals = [x[3] for x in d.damages[n_dmg:] if x[1] == d.id and x[4] > 0]   # 마지막 타격(잘림)은 뺀다
    log(f"Test4(활, 공격력 {atk}) → 몬스터 피해 {vals} (기대 3001 {dmg(3001, atk)}, 3002 틱 {dmg(3002, atk)})")
    check("damage-formula-bow", dmg(3001, atk) in vals and vals.count(dmg(3002, atk)) >= 2 and set(vals) <= {dmg(3001, atk), dmg(3002, atk)}, f"{vals}")
    died = d.dead
    if died:   # 죽었으면 장착을 못 하므로 부활부터(마을에서 부활)
        pump_all(cs4, d.revive_delay / 1000.0 + 0.5)
        d.send(mk.CS_REVIVE)
        wait_all(cs4, lambda: not d.dead, 10.0, "Test4 부활")
        pump_all(cs4, 1.0)
    elif target not in d.dead_ids:   # 덜 잡았으면 마저 잡는다(쫓아오는 몬스터를 두고 가지 않게)
        kill_monster(d, [a, b, c])
    pump_all(cs4, 1.5)
    d.equip(6, sword)
    wait_all(cs4, lambda: any(e[1] == d.id and e[3] == 6101 for e in d.equip_changes), 5.0, "검 되돌림")
    pump_all(cs4, 1.0)

    # ---- 9. 재접속: 레벨·경험치 유지(DB), MP 가득 ----
    b.cast(1)   # Test2 MP를 깎은 채로 나감
    pump_all(cs4, 0.8)
    keep = {"Test1": (a.level, a.exp), "Test2": (b.level, b.exp)}
    mp_b_left = b.stat["mp"]
    for x in cs4:
        x.close(rst=False)
    time.sleep(3.0)
    a2, b2 = C("Test1").enter(), C("Test2").enter()
    pump_all([a2, b2], 1.0)
    log(f"재접속: Test1 level {a2.level} exp {a2.exp} stat {a2.stat} / Test2 level {b2.level} exp {b2.exp} stat {b2.stat} (나갈 때 MP {mp_b_left})")
    check("relogin-level-exp", (a2.level, a2.exp) == keep["Test1"] and (b2.level, b2.exp) == keep["Test2"], f"{(a2.level, a2.exp)} {(b2.level, b2.exp)} 기대 {keep}")
    check("relogin-mp-full", a2.stat["mp"] == a2.stat["max_mp"] and b2.stat["mp"] == b2.stat["max_mp"] and mp_b_left < b2.stat["max_mp"], f"{a2.stat} {b2.stat}")
    for x in (a2, b2):
        x.close(rst=False)
    time.sleep(2.0)
    db = mk.db_scalar("SELECT CONCAT(P.Level, ',', P.Exp) FROM Player_Info P JOIN User_Account A ON A.AccountId = P.AccountId WHERE A.LoginId = N'Test1'")
    check("db-saved", db == f"{keep['Test1'][0]},{keep['Test1'][1]}", f"DB {db}")


def t_err():
    b = C("Test2").enter()
    x = C("Test3").enter()
    cs = [b, x]
    pump_all(cs, 0.8)
    # DevCommands=false → NOT_ALLOWED
    n = len(b.item_results)
    b.dev_exp(500)
    wait_all(cs, lambda: len(b.item_results) > n, 3.0, "DEV 거부")
    check("dev-exp-disabled", b.item_results[-1][1:] == (mk.CS_DEV_GIVE, ITEM_NOT_ALLOWED) and not b.exp_msgs, f"{b.item_results[-1]} exp {b.exp_msgs}")
    # MP 0에서 스킬 연타(사본: 2002 MpCost 100, CooldownMs 1)
    cost = int(SKILL[2002]["MpCost"])
    check("err-table", cost == 100 and int(SKILL[2002]["CooldownMs"]) == 1, f"MpCost {cost} — 사본 데이터가 아님")
    mp0 = b.stat["mp"]
    n_att = len(b.attacks)
    b.cast(1)
    wait_all(cs, lambda: len(b.attacks) > n_att, 3.0, "첫 2002")
    check("spend", b.mp_msgs and b.mp_msgs[0][1] == mp0 - cost and mp0 - cost < cost, f"{b.mp_msgs[:1]} (전 {mp0})")
    n_res, n_att = len(b.attack_results), len(b.attacks)
    for _ in range(30):
        b.cast(1)
        pump_all(cs, 0.15)
    pump_all(cs, 0.5)
    res = [r[1] for r in b.attack_results[n_res:]]
    accepted = [r for r in b.attacks[n_att:] if r[1] == b.id]
    mps = [v for _, v in b.mp_msgs]
    log(f"연타 30회: 거부 {res} 받아들임 {len(accepted)} MP {mps}")
    check("spam-rejected", len(res) == 30 and set(res) <= {AR_ACTING, AR_NOT_ENOUGH_MP} and AR_NOT_ENOUGH_MP in res, f"{res}")
    check("spam-no-cast", not accepted, f"{accepted}")
    check("spam-mp-nonneg", all(0 <= v <= b.stat["max_mp"] for v in mps) and mps == sorted(mps), f"{mps}")
    # 잘못된 패킷: 깨진 본문의 CS_ATTACK·CS_DEV_GIVE, 없는 칸 → 서버는 계속 동작(다른 연결이 정상)
    n_res = len(b.attack_results)
    time.sleep(2.5)
    b.cast(3)   # 범위 안(0~5)이지만 검에 없는 칸 → NO_SKILL. 범위 밖(6~)은 핸들러가 응답 없이 버린다(기존 동작)
    pump_all(cs, 0.5)
    check("bad-slot", b.attack_results[n_res:] and b.attack_results[-1][1] == AR_NO_SKILL, f"{b.attack_results[n_res:]}")
    n_res = len(b.attack_results)
    b.cast(7)
    pump_all(cs, 0.5)
    check("out-of-range-slot-dropped", len(b.attack_results) == n_res and not b.closed, f"{b.attack_results[n_res:]}")
    b.send(mk.CS_ATTACK, b"\xff\xff\xff\xff\xff")
    b.send(mk.CS_DEV_GIVE, b"\x18\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff")
    pump_all(cs, 1.0)
    log(f"깨진 패킷 뒤 Test2 연결 {'끊김' if b.closed else '유지'}")
    x.send_move(x.x + 50, x.y, x.z)
    pump_all(cs, 0.5)
    n_ex = len(x.exp_msgs)
    alive = not x.closed
    x.sock.sendall(mk.rs.ping())
    try:
        got = x.reader.wait_for(mk.rs.SC_PONG, 3.0)
    except TimeoutError:
        got = None
    check("server-alive", alive and got is not None, "다른 연결이 응답을 받지 못함")
    for c in cs:
        c.close(rst=False)


CASES = {"main": t_main, "err": t_err}
if __name__ == "__main__":
    for name in sys.argv[2:]:
        try:
            CASES[name]()
        except Exception as e:
            import traceback
            traceback.print_exc()
            check(f"{name}-run", False, f"{type(e).__name__}: {e}")
        time.sleep(3.0)
    fails = [r for r in RESULTS if r[0] == "FAIL"]
    print(f"\n합계 PASS {len(RESULTS) - len(fails)} / FAIL {len(fails)}")
    sys.exit(1 if fails else 0)
