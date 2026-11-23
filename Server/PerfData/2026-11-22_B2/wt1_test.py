# WT1 양손검 원시 소켓 시험(계정 Test3). 사용: python -I -X utf8 wt1_test.py <포트>
#   장착 흐름: 방패(7101) 착용 → 양손검(6103) 장착 시 방패 자동 해제(SC_CHANGE_EQUIP part 7 item 0) →
#   양손검 착용 중 방패 장착 거부(SC_CHANGE_EQUIP_RESULT NEED_SWORD = 8) → 기본 공격 4001·스킬1 4002 시전 →
#   한손검(6101)으로 되돌리고 방패 다시 착용(시험 뒤 상태 복원).
# 공용 클라이언트·패킷 도구는 growth_test.py(C 클래스)를 그대로 쓴다.
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python -I는 스크립트 폴더를 path에 넣지 않는다
import growth_test as gt
from growth_test import C, check, log, wait_all, pump_all, mk

NEED_SWORD = 8
SC_CHANGE_EQUIP_RESULT = 2013


class W(C):
    def __init__(self, account, **kw):
        self.eq_results = []   # SC_CHANGE_EQUIP_RESULT.result (거부)
        super().__init__(account, **kw)

    def handle(self, pid, body):
        if pid == SC_CHANGE_EQUIP_RESULT:
            self.eq_results.append(mk.first(mk.decode(body), 1))
        super().handle(pid, body)


def bag_row(c, item_id):
    return next((i for i, r in c.inv_rows.items() if r[0] == item_id and r[1] == 0), None)


def t_wt1():
    c = W("Test3").enter()
    cs = [c]
    pump_all(cs, 0.5)
    # 지급(DEV) — 이미 있으면 한 개가 더 쌓인다
    c.send(mk.CS_DEV_GIVE, mk.rs.f_int(1, 6103) + mk.rs.f_int(2, 1))
    pump_all(cs, 0.3)   # 요청 간격(EconomyTable.ItemMinIntervalMs 100)·진행 중 작업(BUSY) — 붙여 보내면 두 번째가 거부된다(2026-11-23 검증 게이트에서 발견)
    c.send(mk.CS_DEV_GIVE, mk.rs.f_int(1, 7101) + mk.rs.f_int(2, 1))
    wait_all(cs, lambda: bag_row(c, 6103) is not None and bag_row(c, 7101) is not None, 8.0, "6103·7101 지급")
    # 시작 무기·보조손을 기억해 두고 끝에 되돌린다. 시작 무기가 한손검이 아니면 먼저 한손검으로 바꾼다(2026-11-23 검증 게이트 — 계정 상태에 기대지 않게).
    orig_weapon = next((i for i, r in c.inv_rows.items() if r[1] == 6), None)
    orig_offhand = next((i for i, r in c.inv_rows.items() if r[1] == 7), None)
    if c.inv_rows.get(orig_weapon, (0,))[0] != 6101:
        pump_all(cs, 0.6)   # 지급 직후의 요청 간격
        c.equip(6, bag_row(c, 6101))
        wait_all(cs, lambda: any(e[1] == c.id and e[2] == 6 and e[3] == 6101 for e in c.equip_changes), 5.0, f"시작 한손검 장착(거부 {c.eq_results})")
        pump_all(cs, 2.3)   # 무기 교체 잠금
    sword_now = next((i for i, r in c.inv_rows.items() if r[0] == 6101 and r[1] == 6), None)
    check("start-sword-equipped", sword_now is not None, "시작 무기를 한손검(6101)으로 맞추지 못함")

    # 1) 방패 착용(한손검 상태)
    shield = bag_row(c, 7101)
    c.equip(7, shield)
    wait_all(cs, lambda: any(e[1] == c.id and e[2] == 7 and e[3] == 7101 for e in c.equip_changes), 5.0, "방패 착용")
    pump_all(cs, 1.5)

    # 2) 양손검 장착 → 방패 자동 해제
    two = bag_row(c, 6103)
    n_before = len(c.equip_changes)
    c.equip(6, two)
    wait_all(cs, lambda: any(e[1] == c.id and e[2] == 6 and e[3] == 6103 for e in c.equip_changes), 5.0, "양손검 장착")
    pump_all(cs, 0.5)
    evts = [e for e in c.equip_changes[n_before:] if e[1] == c.id]
    off_removed = any(e[2] == 7 and e[3] == 0 for e in evts)
    check("twohand-removes-offhand", off_removed, f"방패 해제 방송 없음: {[(e[2], e[3]) for e in evts]}")
    check("twohand-equipped", any(e[2] == 6 and e[3] == 6103 for e in evts), f"양손검 장착 방송 없음: {[(e[2], e[3]) for e in evts]}")
    pump_all(cs, 2.0)   # 무기 교체 잠금

    # 3) 양손검 상태에서 방패 장착 거부
    n_att = len(c.equip_changes)
    shield2 = bag_row(c, 7101)
    c.equip(7, shield2)
    pump_all(cs, 2.0)
    shield_on = any(e[1] == c.id and e[2] == 7 and e[3] == 7101 for e in c.equip_changes[n_att:])
    check("twohand-rejects-offhand", not shield_on and NEED_SWORD in c.eq_results,
          f"방패 착용 방송 {shield_on}, 거부 결과 {c.eq_results}")

    # 4) 기본 공격(4001)과 스킬 1(4002)
    n4001 = sum(1 for a in c.attacks if a[2] == 4001)
    c.cast(0)
    wait_all(cs, lambda: sum(1 for a in c.attacks if a[2] == 4001) > n4001, 5.0, "4001 SC_ATTACK")
    pump_all(cs, 2.0)   # 기본 공격 행동 시간
    n4002 = sum(1 for a in c.attacks if a[2] == 4002)
    mp0 = c.stat["mp"]
    c.cast(1)
    wait_all(cs, lambda: sum(1 for a in c.attacks if a[2] == 4002) > n4002, 5.0, "4002 SC_ATTACK")
    pump_all(cs, 2.5)
    check("twohand-skills", sum(1 for a in c.attacks if a[2] == 4001) > n4001 and sum(1 for a in c.attacks if a[2] == 4002) > n4002,
          f"기본 {sum(1 for a in c.attacks if a[2] == 4001)}회, 스킬1 {sum(1 for a in c.attacks if a[2] == 4002)}회 (MP {mp0} → {c.stat['mp']})")

    # 5) 복원: 한손검 → (시작에 방패를 꼈으면) 방패 → 시작 무기
    sword = next(i for i, r in c.inv_rows.items() if r[0] == 6101 and r[1] == 0)
    c.equip(6, sword)
    wait_all(cs, lambda: any(e[1] == c.id and e[2] == 6 and e[3] == 6101 for e in c.equip_changes[n_att:]), 5.0, "한손검 되돌림")
    pump_all(cs, 2.3)
    if orig_offhand is not None:
        c.equip(7, orig_offhand)
        pump_all(cs, 1.5)
    if orig_weapon is not None and orig_weapon != sword:
        n_back = len(c.equip_changes)
        c.equip(6, orig_weapon)
        wait_all(cs, lambda: any(e[1] == c.id and e[2] == 6 for e in c.equip_changes[n_back:]), 5.0, "시작 무기 되돌림")
        pump_all(cs, 1.5)
    c.close(rst=False)


if __name__ == "__main__":
    try:
        t_wt1()
    except Exception as e:
        import traceback
        traceback.print_exc()
        check("wt1-run", False, f"{type(e).__name__}: {e}")
    fails = [r for r in gt.RESULTS if r[0] == "FAIL"]
    print(f"\n합계 PASS {len(gt.RESULTS) - len(fails)} / FAIL {len(fails)}")
    sys.exit(1 if fails else 0)
