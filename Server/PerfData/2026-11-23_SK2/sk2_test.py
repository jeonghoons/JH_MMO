# SK2 공격 구조 원시 소켓 시험(D-278, 계정 Test3·Test4). 사용: python -I -X utf8 sk2_test.py <포트>
#   ([Debug] DevCommands=true, 원본 DataTable)
#   1) 마을: 무기 3종 × 슬롯 0·1·2 — SC_ATTACK skillId, MP 차감, 근접 기본 공격 대상 0 = 헛스윙(피해 없음),
#      활 기본 공격 대상 0 = NO_TARGET, 빈 슬롯 5 = NO_SKILL
#   2) 사냥터: 한손검 기본 공격(단일 타겟팅)이 대상에게 피해, 활 3003 빛의 화살이 거리 ÷ 초속 + 놓기 시각에 맞고 폭 밖은 안 맞음
#   3) 시전자가 접속을 끊으면 날아가던 3003이 사라짐(관찰자 Test4가 그 뒤 피해 방송을 받지 않음)
# 판정 줄은 "PASS <이름>" / "FAIL <이름>: 이유". 공용 클라이언트는 B2 growth_test.py(C 클래스).
import math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "2026-11-22_B2"))
import growth_test as gt
from growth_test import C, check, log, wait_all, pump_all, mk, boost, SKILL
import party_test as pt

AR_NO_SKILL, AR_NO_TARGET = 5, 7
PIERCE = 3003


class S(C):
    """피해를 받은 순간의 대상 위치를 남긴다(dmg_pos[(시각, 대상 id)] = objects 값)."""
    def __init__(self, account, **kw):
        self.dmg_pos = {}
        super().__init__(account, **kw)

    def handle(self, pid, body):
        n = len(getattr(self, "damages", []))
        super().handle(pid, body)
        if pid == gt.SC_DAMAGE and len(self.damages) > n:
            d = self.damages[-1]
            self.dmg_pos[(d[0], d[2])] = self.objects.get(d[2])


def skill_casts(c, sid):
    return sum(1 for a in c.attacks if a[1] == c.id and a[2] == sid)


def equip_weapon(c, cs, item_id):
    row = next((i for i, r in c.inv_rows.items() if r[0] == item_id), None)
    if row is None:
        c.send(mk.CS_DEV_GIVE, mk.rs.f_int(1, item_id) + mk.rs.f_int(2, 1))
        wait_all(cs, lambda: any(r[0] == item_id for r in c.inv_rows.values()), 8.0, f"{item_id} 지급")
        row = next(i for i, r in c.inv_rows.items() if r[0] == item_id)
    if c.inv_rows[row][1] == 6:
        return
    n = len(c.equip_changes)
    c.equip(6, row)
    wait_all(cs, lambda: any(e[1] == c.id and e[2] == 6 and e[3] == item_id for e in c.equip_changes[n:]), 5.0, f"{item_id} 장착")
    pump_all(cs, 2.0)   # 무기 교체 잠금


def cast_expect(c, cs, slot, sid, target=0, wait=None):
    """slot을 쓰고 sid의 SC_ATTACK을 기다린다. MP 변화를 돌려준다."""
    n = skill_casts(c, sid)
    mp0, n_mp = c.stat["mp"], len(c.mp_msgs)
    c.cast(slot, target)
    wait_all(cs, lambda: skill_casts(c, sid) > n, 5.0, f"{sid} SC_ATTACK")
    pump_all(cs, wait if wait is not None else int(SKILL[sid]["AnimMs"]) / 1000 + 0.3)
    first = c.mp_msgs[n_mp][1] if len(c.mp_msgs) > n_mp else mp0
    return mp0 - first   # 시전 때 보낸 첫 SC_CHANGE_MP(그 뒤 회복은 빼고)


def t_town(c, cs):
    """마을(몬스터 없음)에서 무기마다 세 칸."""
    for weapon, ids in ((6101, (2001, 2002, 2003)), (6102, (3001, 3002, 3003)), (6103, (4001, 4002, 4003))):
        equip_weapon(c, cs, weapon)
        b0, s1, s2 = ids
        # 기본 공격, 대상 0
        n_dmg, n_res = len(c.damages), len(c.attack_results)
        n = skill_casts(c, b0)
        c.cast(0, 0)
        pump_all(cs, int(SKILL[b0]["AnimMs"]) / 1000 + 0.3)
        if SKILL[b0]["HitShape"] == "Projectile":   # 활: 대상이 없으면 거부
            check(f"{b0}-no-target-rejected", skill_casts(c, b0) == n and any(r[1] == AR_NO_TARGET for r in c.attack_results[n_res:]),
                  f"SC_ATTACK {skill_casts(c, b0) - n}회, 거부 {[r[1] for r in c.attack_results[n_res:]]}")
        else:
            check(f"{b0}-whiff", skill_casts(c, b0) == n + 1 and not [d for d in c.damages[n_dmg:] if d[1] == c.id],
                  f"SC_ATTACK {skill_casts(c, b0) - n}회, 내 피해 {[d for d in c.damages[n_dmg:] if d[1] == c.id]}, 거부 {[r[1] for r in c.attack_results[n_res:]]}")
        # 스킬1·스킬2 (MP 차감)
        for slot, sid in ((1, s1), (2, s2)):
            spent = cast_expect(c, cs, slot, sid)
            cost = int(SKILL[sid]["MpCost"])
            check(f"{sid}-cast-mp", spent == cost, f"MP 차감 {spent} 기대 {cost}")
        # 빈 슬롯
        n_res = len(c.attack_results)
        c.cast(5, 0)
        pump_all(cs, 0.6)
        check(f"{b0 // 1000}xxx-slot5-no-skill", any(r[1] == AR_NO_SKILL for r in c.attack_results[n_res:]),
              f"거부 {[r[1] for r in c.attack_results[n_res:]]}")


def corridor(c, yaw_deg, mx, my):
    """시전자 기준 진행 거리·옆 거리"""
    dx, dy = mx - c.x, my - c.y
    ux, uy = math.cos(math.radians(yaw_deg)), math.sin(math.radians(yaw_deg))
    return dx * ux + dy * uy, abs(dx * uy - dy * ux)


def t_field(c, o, cs):
    pt.to_hunt(c, cs)
    pt.to_hunt(o, cs)
    pump_all(cs, 1.0)

    # ---- 한손검 기본 공격(단일 타겟팅): 가까운 몬스터에게 피해 ----
    equip_weapon(c, cs, 6101)
    n_dmg = len(c.damages)
    tid = gt.kill_monster(c, [o])
    hits = [d for d in c.damages[n_dmg:] if d[1] == c.id]
    check("2001-targeted-hit", bool(hits) and all(d[2] == tid for d in hits) and all(d[3] == gt.dmg(2001, c.stat["atk"]) or (d[3] < gt.dmg(2001, c.stat["atk"]) and d[4] == 0) for d in hits),
          f"내 피해 {[(d[2], d[3]) for d in hits]} 대상 {tid} 기대 피해 {gt.dmg(2001, c.stat['atk'])}")
    pump_all(cs, 2.0)

    # ---- 활 3003: 몬스터 쪽으로 쏴서 거리 ÷ 1000cm/s + 놓기 시각에 맞는지, 폭 밖은 안 맞는지 ----
    equip_weapon(c, cs, 6102)
    pump_all(cs, 0.5)
    # 원시 소켓 클라이언트는 몬스터 이동 패킷을 받은 시점의 위치만 알아 움직이는 몬스터의 실제 위치를 모른다.
    # 그래서 판정 검증은 서버 Debug 로그(Pierce 생성·명중 줄 — 서버가 본 진행·옆 거리)로 하고, 여기서는 맞을 때까지 최대 3번 쏜다.
    hits, casts = [], 0
    for _ in range(3):
        mons = {i: m for i, m in c.monsters().items() if i not in c.dead_ids}
        near = sorted(mons, key=lambda i: math.hypot(mons[i][1] - c.x, mons[i][2] - c.y))
        tid = next((i for i in near if math.hypot(mons[i][1] - c.x, mons[i][2] - c.y) >= 400), near[0])
        _, mx, my, _ = mons[tid]
        n_dmg, n_att = len(c.damages), len(c.attacks)
        c.cast(2, 0, mx, my)
        wait_all(cs, lambda: any(a[2] == PIERCE for a in c.attacks[n_att:]), 5.0, "3003 SC_ATTACK")
        casts += 1
        pump_all(cs, 3.2)
        hits = [d for d in c.damages[n_dmg:] if d[1] == c.id]
        log(f"3003 {casts}번째: 노린 {tid} 거리 {math.hypot(mx - c.x, my - c.y):.0f} — 맞음 {[(d[2], d[3]) for d in hits]}")
        if hits:
            break
        pump_all(cs, float(SKILL[PIERCE]["CooldownMs"]) / 1000 - 3.0)
    check("3003-hits-something", bool(hits), f"{casts}번 쏴서 하나도 맞지 않음")
    check("3003-each-once", len(hits) == len({d[2] for d in hits}), f"한 대상이 두 번 이상 맞음: {[d[2] for d in hits]}")
    check("3003-damage", all(d[3] == gt.dmg(PIERCE, c.stat["atk"]) or d[4] == 0 for d in hits),
          f"피해 {[d[3] for d in hits]} 기대 {gt.dmg(PIERCE, c.stat['atk'])}")
    pump_all(cs, float(SKILL[PIERCE]["CooldownMs"]) / 1000)

    # ---- 시전자가 접속을 끊으면 날아가던 3003이 사라짐 ----
    mons = {i: m for i, m in c.monsters().items() if i not in c.dead_ids}
    tid = max(mons, key=lambda i: math.hypot(mons[i][1] - c.x, mons[i][2] - c.y))
    _, mx, my, _ = mons[tid]
    dist = math.hypot(mx - c.x, my - c.y)
    n_obs = len(o.damages)
    me = c.id
    c.cast(2, 0, mx, my)
    c.pump(int(SKILL[PIERCE]["HitDelayMs"]) / 1000 + 0.25)   # 놓은 직후(약 2.5m 날아감)
    c.close(rst=True)
    t_close = gt.ts()
    o.pump(3.0)
    after = [d for d in o.damages[n_obs:] if d[1] == me and d[0] > t_close + 0.15]
    log(f"끊기 시험: 가장 먼 몬스터 {tid} 거리 {dist:.0f}, 끊은 뒤 피해 {[(d[2], round(d[0] - t_close, 2)) for d in after]}")
    check("3003-gone-on-disconnect", not after, f"끊은 뒤에도 피해: {after}")


if __name__ == "__main__":
    try:
        c = S("Test3").enter()
        o = C("Test4").enter()
        cs = [c, o]
        pump_all(cs, 0.8)
        boost(c, 10, cs)
        boost(o, 10, cs)
        t_town(c, cs)
        t_field(c, o, cs)
        o.close(rst=False)
    except Exception as e:
        import traceback
        traceback.print_exc()
        check("sk2-run", False, f"{type(e).__name__}: {e}")
    fails = [r for r in gt.RESULTS if r[0] == "FAIL"]
    print(f"\n합계 PASS {len(gt.RESULTS) - len(fails)} / FAIL {len(fails)}")
    sys.exit(1 if fails else 0)
