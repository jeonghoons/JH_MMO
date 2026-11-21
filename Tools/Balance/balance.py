"""IT1 기획 설계안 수치 검사기 (Docs/DESIGN_BALANCE.md).

원칙(2026-11-22 사용자): 포트폴리오용이라 수치는 쉬운 숫자로 먼저 정하고, 목표 플레이 시간은 두지 않는다.
이 스크립트는 맨 위의 쉬운 숫자를 그대로 쓰고, 전투 감각(처치 시간·맞아 죽는 시간·무기 비교)만 계산·검사한다.
몬스터 HP·공격력만 전투 감각 기준에서 계산하고 HP는 50, 공격력은 10 단위로 반올림한다.

결과:
  Tools/Balance/preview/*.csv      — 뒤 항목(A2.3b·IT3·WT1·Q1)이 Common/DataTable에 옮길 값의 미리보기
  Tools/Balance/preview/report.md  — 검사표
  Tools/Balance/preview/dash_*.json — 그래프 페이지(claude.ai Data 아티팩트)가 읽는 데이터

표준 라이브러리만 씁니다. 실행: python -I Tools/Balance/balance.py
  --write  : 위 미리보기를 만든 뒤 Common/DataTable의 해당 CSV에 이 스크립트가 맡은 수치 열만 덮어쓰고 바뀐 줄을 보여 준다(A2.3b).
             외형·경로 열(Mesh, Fx, Montage, Icon …)과 맡지 않은 열은 그대로 둔다. 새 열은 정해진 자리에 끼워 넣는다.
             예외: SKILL_ASSET_FILL의 외형 값은 그 칸이 비어 있을 때만 채운다(이미 있는 값은 덮지 않음).
             같은 CSV에 다시 돌려도 결과가 같다 — 다른 줄에서 CSV를 고친 뒤 합칠 때는 그쪽 CSV에 다시 돌리면 된다.
  --diff   : --write가 바꿀 줄만 보여 주고 쓰지 않는다.
  지금 --write가 맡는 테이블: PlayerStatTable·MonsterTable·SkillTable (A2.3b). ItemTable·DropTable·ShopTable은 IT3에서 추가.
"""
import csv
import difflib
import io
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "preview")
DATA = os.path.normpath(os.path.join(HERE, "..", "..", "Common", "DataTable"))

# ============================================================ 쉬운 숫자 (여기만 고침)
LEVEL_MAX = 30


def exp_to_next(lv):          # 다음 레벨까지 경험치 = 500 × 레벨
    return 500 * lv if lv < LEVEL_MAX else 0


def base_stat(lv):            # (HP, 공격력, MP) = 500 + 100/레벨, 0 + 2/레벨, 100 + 10/레벨
    return (500 + 100 * (lv - 1), 2 * (lv - 1), 100 + 10 * (lv - 1))


# 회복. 전투 = 마지막으로 때리거나 맞은 뒤 5초
COMBAT_HOLD_SEC = 5
MP_REGEN_COMBAT, MP_REGEN_IDLE = 0.01, 0.05          # MP: 초당 최대치 대비
HP_REGEN_TICK_SEC, HP_REGEN_TICK_RATE = 5, 0.20      # HP: 전투 밖에서만 5초마다 최대치의 20% (D-253 사용자 결정)

# 장비 등급 00~03과 교체 레벨(D-251)
GRADES = [0, 1, 2, 3]
GRADE_SWITCH_LV = {0: 1, 1: 10, 2: 20, 3: 30}
GRADE_ENUM = {0: "Common", 1: "Uncommon", 2: "Rare", 3: "Epic"}       # Epic = 새 값(IT3)
TIER_ENUM = {0: "Cloth", 1: "Leather", 2: "Plate", 3: "Ornate"}       # Ornate = 새 값(IT3)
GRADE_WORD = {0: "천", 1: "가죽", 2: "강철", 3: "근위"}

ARMOR_HP = [20, 50, 100, 200]            # 투구·상의·하의·장갑·신발 각각 최대 HP
SHIELD_HP = [50, 100, 200, 300]          # 방패 최대 HP
WEAPON_ATK = {                           # 무기 공격력
    "Sword": [10, 30, 60, 100],
    "Bow": [10, 30, 60, 100],
    "TwoHandSword": [10, 40, 90, 150],
}
HEAD_MP = [0, 20, 50, 100]               # 투구 추가 능력치: 최대 MP
GLOVES_ATK = [0, 5, 10, 20]              # 장갑 추가 능력치: 공격력
SELL_PRICE = [20, 50, 100, 300]          # 장비 판매가
SHOP_GEAR_BUY = 100                      # 상점은 00등급만 판다

# 스킬. 피해 = BaseDamage + 공격력 × AttackScale / 100 (AttackScale = 새 열 제안, 100이면 지금 공식과 같음 — D-112)
SKILLS = {
    # id: (무기, 슬롯, BaseDamage, AnimMs, CooldownMs, MpCost, 맞는 횟수, AttackScale%)
    2001: ("Sword", 0, 50, 1500, 0, 0, 1, 100),
    2002: ("Sword", 1, 120, 2333, 5000, 20, 1, 100),
    3001: ("Bow", 0, 40, 1733, 0, 0, 1, 80),
    3002: ("Bow", 1, 30, 700, 5000, 20, 3, 30),   # 장판 3초 × 1초 틱(틱마다 공격력 30% → 3틱 90%)
    4001: ("TwoHandSword", 0, 60, 1267, 0, 0, 1, 100),   # AnimMs = AM_TwoHandSword_Attack 몽타주 길이 1.267초 (WT1)
    4002: ("TwoHandSword", 1, 100, 1867, 6000, 30, 1, 100),  # AnimMs = AM_TwoHandSword_Skill1 1.867초 (WT1)
}
WEAPON_SKILLS = {"Sword": (2001, 2002), "Bow": (3001, 3002), "TwoHandSword": (4001, 4002)}
# WT1 새 행(SkillTable에 없을 때 만든다): 판정 시점·행동 잠금은 ExportSkillTiming 전까지의 임시값(보고에 "재실행 필요")
TWO_HAND_ROW = {
    # id: (HitDelayMs, MoveLockMs — 2026-11-22 ExportSkillTiming 값, Radius, Angle, HitFx, Montage)
    4001: (650, 850, 230, 180, "/Game/FX/Hit/P_Hit_Slash.P_Hit_Slash",
           "/Game/Character/Mannequin/Anims/TwoHandSword/AM_TwoHandSword_Attack.AM_TwoHandSword_Attack"),
    4002: (1000, 1400, 320, 360, "",
           "/Game/Character/Mannequin/Anims/TwoHandSword/AM_TwoHandSword_Skill1.AM_TwoHandSword_Skill1"),
}

# 몬스터: 경험치는 쉬운 숫자로 고정. HP·공격력은 전투 감각 기준(기준 플레이어 = 그 대역 레벨 + 그 등급 전신, 한손검 + 방패)
MONSTER_BASE_DAMAGE = 10                 # 9001~9005 BaseDamage 모두 10 (한 대 피해 = 공격력 + 10)
MONSTER_ATTACK_SPEED = {False: 1.0, True: 0.5}   # MonsterTable.AttackSpeed — 우두머리는 2초에 한 번, 대신 한 대가 셈
MONSTER_ANIM_SEC = 1.0                   # 몬스터 공격 AnimMs 1000 (공격 간격 = AnimMs ÷ AttackSpeed, 경직은 빼고 계산)
MONSTER_HIT_STUN_MS = {False: 800, True: 0}      # MonsterTable.HitStunMs — 일반 800 그대로, 우두머리 0(묶어 두지 못하게)
MONSTER_SKILL_IDS = (9001, 9002, 9003, 9004, 9005)   # 몬스터 기본 공격 스킬(모두 BaseDamage = MONSTER_BASE_DAMAGE)
# --write가 SkillTable의 클라이언트 전용 외형 칸을 채우는 값(D-254). 칸이 비어 있을 때만 쓴다 — 이미 다른 값이 있으면 그대로 둔다.
SKILL_ASSET_FILL = {
    1001: {"HitFx": "/Game/FX/Hit/P_Hit_Blunt.P_Hit_Blunt"},     # 맨손: 피격자 위치 1회
    2001: {"HitFx": "/Game/FX/Hit/P_Hit_Slash.P_Hit_Slash"},     # 검 기본 공격
    3001: {"HitFx": "/Game/FX/Hit/P_Hit_Arrow.P_Hit_Arrow",      # 활 기본 공격: 맞은 자리
           "Fx": "/Game/FX/Proj/P_Proj_Arrow.P_Proj_Arrow"},     # 화살 액터
}
MONSTERS = {
    # id: (이름, 표시 레벨, 경험치, 기준 레벨, 기준 등급, 처치 시간 기준(초), 맞아 죽는 시간 기준(초), 우두머리)
    1001: ("초원 하급", 5, 100, 5, 0, 4.5, 25.0, False),
    1002: ("숲 중급", 15, 300, 15, 1, 5.0, 20.0, False),
    1003: ("숲 우두머리", 20, 3000, 20, 1, 40.0, 30.0, True),
    1004: ("던전 일반", 25, 1000, 25, 2, 5.5, 20.0, False),
    1005: ("던전 우두머리", 30, 5000, 28, 2, 75.0, 30.0, True),
}
BAND_MONSTER = {"A": 1001, "B": 1002, "C": 1004}  # A = 1~9 초원, B = 10~19 숲, C = 20~29 던전
DUNGEON_NORMALS = 10                     # room1 4 + room2 6 (D-230)
BIG_HP_POTION = 600
DPS_SIM_SEC = 60                         # 지속 DPS = 허수아비를 60초 때린 평균(MP 소모·회복 포함)

CONSUMABLES = [
    # id, 이름, 효과, 값, 재사용(ms), 구매가, 판매가
    (8001, "HP 물약", "HealHp", 200, 5000, 30, 5),
    (8002, "MP 물약", "HealMp", 100, 5000, 30, 5),
    (8003, "큰 HP 물약", "HealHp", 600, 5000, 90, 15),
    (8004, "큰 MP 물약", "HealMp", 300, 5000, 90, 15),
]
LOOT = [
    # id, 이름, 떨어뜨리는 몬스터, 판매가
    (8051, "거친 가죽 조각", 1001, 10),
    (8052, "뒤틀린 뿔", 1002, 20),
    (8053, "숲 군주의 송곳니", 1003, 100),
    (8054, "이끼 낀 돌조각", 1004, 30),
    (8055, "던전 군주의 문장", 1005, 300),
]

# 드랍(만분율, 아이템 한 종류마다 독립 — D-157). 그 등급 9종에 같은 확률
GEAR_DROP = {
    1001: {1: 20},                 # 0.2%
    1002: {1: 100, 2: 10},         # 1%, 0.1%
    1003: {1: 1000, 2: 500},       # 10%, 5%
    1004: {2: 200},                # 2%
    1005: {2: 1000, 3: 500},       # 10%, 5%
}
GOLD_DROP = {1001: (10, 30), 1002: (30, 60), 1003: (300, 600), 1004: (50, 100), 1005: (1000, 2000)}
LOOT_DROP = {1001: (4000, 1, 1), 1002: (4000, 1, 2), 1003: (10000, 2, 3), 1004: (5000, 1, 2), 1005: (10000, 2, 4)}
POTION_DROP = {  # 몬스터: [(아이템, 만분율, 최소, 최대)]
    1001: [(8001, 1500, 1, 1), (8002, 1000, 1, 1)],
    1002: [(8001, 1500, 1, 2), (8002, 1000, 1, 1)],
    1003: [(8003, 10000, 2, 3), (8004, 10000, 1, 2)],
    1004: [(8003, 1000, 1, 1), (8004, 1000, 1, 1)],
    1005: [(8003, 10000, 3, 5), (8004, 10000, 2, 3)],
}

QUEST_NPC = 1003   # 경비대장(NpcTable) — 세 퀘스트 모두 이 NPC에게서 받고 완료한다 (Q1, D-259)
QUESTS = [
    # id, 이름, NPC, 종류, 대상, 수량, 권장 레벨, 경험치, 골드, 보상 아이템, 수량, 설명(클라이언트 전용 — 서버는 읽지 않는다. 쉼표를 쓰지 않는다)
    (1, "초원 정리", QUEST_NPC, "Kill", 1001, 15, 1, 1000, 300, 8001, 5, "마을 밖 초원의 하급 몬스터를 15마리 처치하세요."),
    (2, "뿔 모으기", QUEST_NPC, "Collect", 8052, 10, 10, 5000, 1000, 4201, 1, "뒤틀린 뿔 10개를 모아 경비대장에게 가져오세요. 완료하면 뿔을 가져갑니다."),
    (3, "던전의 군주", QUEST_NPC, "Kill", 1005, 1, 20, 10000, 3000, 8004, 5, "던전의 우두머리를 처치하세요. 같은 던전의 파티원이 처치해도 인정됩니다."),
]
QUEST_HEADER = ["Name", "DisplayName", "NpcId", "Kind", "TargetId", "Count", "RecommendLevel", "RewardExp", "RewardGold",
                "RewardItemId", "RewardItemCount", "Description"]

# ============================================================ 계산


def simple(v, step):
    return int(max(step, round(v / step) * step))


SLOT_DIGIT = {"Head": 1, "Top": 2, "Bottom": 3, "Gloves": 4, "Boots": 5, "Weapon": 6, "Offhand": 7}
SLOT_WORD = {"Head": "투구", "Top": "상의", "Bottom": "하의", "Gloves": "장갑", "Boots": "장화", "Offhand": "방패"}
WEAPON_SERIAL = {"Sword": 1, "Bow": 2, "TwoHandSword": 3}
WEAPON_WORD = {"Sword": "한손검", "Bow": "활", "TwoHandSword": "양손검"}


def item_id(slot, grade, serial=1):
    # 규칙(§8): [슬롯][등급 + 1][일련번호 01~99]
    return SLOT_DIGIT[slot] * 1000 + (grade + 1) * 100 + serial


def build_items():
    items = []
    for g in GRADES:
        for slot in ("Head", "Top", "Bottom", "Gloves", "Boots"):
            name = "천 두건" if (g == 0 and slot == "Head") else f"{GRADE_WORD[g]} {SLOT_WORD[slot]}"
            items.append(dict(id=item_id(slot, g), slot=slot, grade=g, weapon="None", atk=GLOVES_ATK[g] if slot == "Gloves" else 0,
                              hp=ARMOR_HP[g], mp=HEAD_MP[g] if slot == "Head" else 0, sell=SELL_PRICE[g], name=name,
                              starter=1 if g == 0 else 0))
        for w, serial in WEAPON_SERIAL.items():
            items.append(dict(id=item_id("Weapon", g, serial), slot="Weapon", grade=g, weapon=w, atk=WEAPON_ATK[w][g], hp=0, mp=0,
                              sell=SELL_PRICE[g], name=f"{GRADE_WORD[g]} {WEAPON_WORD[w]}", starter=1 if (g == 0 and w == "Sword") else 0))
        items.append(dict(id=item_id("Offhand", g), slot="Offhand", grade=g, weapon="None", atk=0, hp=SHIELD_HP[g], mp=0,
                          sell=SELL_PRICE[g], name=f"{GRADE_WORD[g]} 방패", starter=0))
    return items


ITEMS = build_items()
ITEM_BY_ID = {it["id"]: it for it in ITEMS}


def gear_of(grade, weapon):
    ids = [item_id(s, grade) for s in ("Head", "Top", "Bottom", "Gloves", "Boots")]
    ids.append(item_id("Weapon", grade, WEAPON_SERIAL[weapon]))
    if weapon != "TwoHandSword":
        ids.append(item_id("Offhand", grade))
    return ids


def player(lv, grade, weapon="Sword"):
    hp, atk, mp = base_stat(lv)
    for i in gear_of(grade, weapon):
        it = ITEM_BY_ID[i]
        hp, atk, mp = hp + it["hp"], atk + it["atk"], mp + it["mp"]
    return dict(lv=lv, grade=grade, weapon=weapon, hp=hp, atk=atk, mp=mp)


def dps(p, sec=DPS_SIM_SEC):
    """허수아비를 sec초 때린 평균 피해(스킬은 쿨타임마다, MP가 모자라면 기본 공격)."""
    basic, skill = (SKILLS[s] for s in WEAPON_SKILLS[p["weapon"]])
    t, dmg, mp, skill_ready = 0.0, 0.0, p["mp"], 0.0
    while t < sec:
        if t >= skill_ready and mp >= skill[5]:
            dmg += (skill[2] + p["atk"] * skill[7] / 100) * skill[6]
            mp -= skill[5]
            skill_ready = t + skill[4] / 1000
            dt = skill[3] / 1000
        else:
            dmg += basic[2] + p["atk"] * basic[7] / 100
            dt = basic[3] / 1000
        t += dt
        mp = min(p["mp"], mp + p["mp"] * MP_REGEN_COMBAT * dt)
    return dmg / t


def build_monsters():
    out = {}
    for mid, (name, show_lv, exp, ref_lv, ref_g, ttk, ttd, boss) in MONSTERS.items():
        p = player(ref_lv, ref_g)
        sim = max(DPS_SIM_SEC, ttk)
        speed = MONSTER_ATTACK_SPEED[boss]
        every = MONSTER_ANIM_SEC / speed
        mhp = simple(dps(p, sim) * ttk, 50)
        atk = simple(p["hp"] / ttd * every - MONSTER_BASE_DAMAGE, 10)
        hit = atk + MONSTER_BASE_DAMAGE
        bow, th = player(ref_lv, ref_g, "Bow"), player(ref_lv, ref_g, "TwoHandSword")
        t_sw = mhp / dps(p, sim)
        pots = max(0, math.ceil((hit / every * t_sw - p["hp"] * 0.8) / BIG_HP_POTION)) if boss else None
        out[mid] = dict(id=mid, name=name, level=show_lv, boss=boss, hp=mhp, attack=atk, hit=hit, attackSpeed=speed, exp=exp,
                        refLevel=ref_lv, refGrade=f"{ref_g:02d}",
                        ttkSword=round(t_sw, 1), ttkBow=round(mhp / dps(bow, sim), 1), ttkTwoHand=round(mhp / dps(th, sim), 1),
                        ttdSword=round(p["hp"] / hit * every, 1), ttdTwoHand=round(th["hp"] / hit * every, 1),
                        bigPotions=pots)
    return out


MONS = build_monsters()
DUNGEON_RUN_EXP = DUNGEON_NORMALS * MONS[1004]["exp"] + MONS[1005]["exp"]


def band_of(lv):
    return "A" if lv < 10 else "B" if lv < 20 else "C"


def level_rows():
    rows = []
    for lv in range(1, LEVEL_MAX + 1):
        hp, atk, mp = base_stat(lv)
        need = exp_to_next(lv)
        if lv < LEVEL_MAX:
            b = band_of(lv)
            kills = round(need / MONS[BAND_MONSTER[b]]["exp"], 1)
            runs = round(need / DUNGEON_RUN_EXP, 2) if b == "C" else None
        else:
            b, kills, runs = "-", 0, None
        rows.append({"level": lv, "expToNext": need, "hp": hp, "atk": atk, "mp": mp, "band": b,
                     "killsToNext": kills, "dungeonRuns": runs})
    return rows


def grade_rows():
    rows = []
    for g in GRADES:
        lv = GRADE_SWITCH_LV[g]
        a, b, c = (player(lv, g, w) for w in ("Sword", "Bow", "TwoHandSword"))
        rows.append({"grade": f"{g:02d}", "name": GRADE_WORD[g], "level": lv, "hpSword": a["hp"], "atkSword": a["atk"],
                     "hpTwoHand": c["hp"], "atkTwoHand": c["atk"], "mp": a["mp"],
                     "dpsSword": round(dps(a)), "dpsBow": round(dps(b)), "dpsTwoHand": round(dps(c)),
                     "armorHp": ARMOR_HP[g], "shieldHp": SHIELD_HP[g], "weaponAtk": WEAPON_ATK["Sword"][g],
                     "twoHandAtk": WEAPON_ATK["TwoHandSword"][g], "headMp": HEAD_MP[g], "glovesAtk": GLOVES_ATK[g],
                     "sellPrice": SELL_PRICE[g]})
    return rows


def band_kills():
    """대역을 지나는 동안의 처치 수 = 대역 경험치 합 ÷ 몬스터 경험치(던전은 1회 = 일반 10 + 우두머리)."""
    a = sum(exp_to_next(l) for l in range(1, 10))
    b = sum(exp_to_next(l) for l in range(10, 20))
    c = sum(exp_to_next(l) for l in range(20, 30))
    runs = c / DUNGEON_RUN_EXP
    return [("1~10", f"초원 하급 {a / MONS[1001]['exp']:.0f}마리", {1001: a / MONS[1001]["exp"]}),
            ("10~20", f"숲 중급 {b / MONS[1002]['exp']:.0f}마리", {1002: b / MONS[1002]["exp"]}),
            ("20~30", f"던전 {runs:.1f}회", {1004: runs * DUNGEON_NORMALS, 1005: runs})]


def drop_expect_rows():
    rows = []
    for band, src, ks in band_kills():
        per = {}
        for mid, k in ks.items():
            for g, rate in GEAR_DROP[mid].items():
                per[g] = per.get(g, 0) + k * rate / 10000
        for g, e in sorted(per.items()):
            rows.append({"band": band, "source": src, "grade": f"{g:02d}", "totalPieces": round(e * 9, 1), "perItem": round(e, 1)})
    return rows


# ============================================================ 출력


def write_csv(name, header, rows):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def drop_rows():
    rows, n = [], 1001
    for mid in MONSTERS:
        g0, g1 = GOLD_DROP[mid]
        rows.append([n, mid, 0, 10000, g0, g1]); n += 1
        for it, rate, c0, c1 in POTION_DROP[mid]:
            rows.append([n, mid, it, rate, c0, c1]); n += 1
        loot = next(l for l in LOOT if l[2] == mid)
        r, c0, c1 = LOOT_DROP[mid]
        rows.append([n, mid, loot[0], r, c0, c1]); n += 1
        for g, rate in GEAR_DROP[mid].items():
            for it in ITEMS:
                if it["grade"] == g:
                    rows.append([n, mid, it["id"], rate, 1, 1]); n += 1
    return rows


def write_dash(levels, grades, drops):
    """그래프 페이지가 읽는 JSON: {rows, meta}. meta에 이 스크립트 원문을 싣는다."""
    with open(os.path.abspath(__file__), encoding="utf-8") as f:
        code = f.read()
    meta = [{"label": "balance.py", "value": code, "type": "code", "lang": "python"},
            {"label": "입력", "value": "스크립트 맨 위 쉬운 숫자 (Tools/Balance/balance.py, 2026-11-22 IT1 확정 — D-253)"}]

    def dump(name, rows):
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            json.dump({"rows": rows, "meta": meta}, f, ensure_ascii=False)

    dump("dash_levels.json", levels)
    dump("dash_monsters.json", list(MONS.values()))
    dump("dash_grades.json", grades)
    dump("dash_drops.json", drops)


def checks(lines, grades):
    errors, warns = [], []
    for it in ITEMS:
        i = it["id"]
        if not (1000 <= i <= 9999 and i // 1000 == SLOT_DIGIT[it["slot"]] and (i // 100) % 10 == it["grade"] + 1 and i % 100 != 0):
            errors.append(f"ID 규칙 위반 {i}")
    for c in CONSUMABLES:
        if not (8001 <= c[0] <= 8049):
            errors.append(f"소비 아이템 ID 구간 위반 {c[0]}")
        if c[5] < c[6]:
            errors.append(f"구매가 < 판매가 {c[0]}")
    for l in LOOT:
        if not (8051 <= l[0] <= 8099):
            errors.append(f"전리품 ID 구간 위반 {l[0]}")
    if SHOP_GEAR_BUY < SELL_PRICE[0]:
        errors.append("00등급 구매가 < 판매가")
    ids = [it["id"] for it in ITEMS] + [c[0] for c in CONSUMABLES] + [l[0] for l in LOOT]
    if len(ids) != len(set(ids)):
        errors.append("아이템 ID 중복")
    # 전투 감각 검사(경고)
    for m in MONS.values():
        if not m["boss"] and not (4.0 <= m["ttkSword"] <= 6.0):
            warns.append(f"{m['id']} 처치 시간 {m['ttkSword']}초 (기준 4~6초)")
        if not m["boss"] and not (18.0 <= m["ttdSword"] <= 30.0):
            warns.append(f"{m['id']} 맞아 죽는 시간 {m['ttdSword']}초 (기준 18~30초)")
    for g in grades:
        bow, th = g["dpsBow"] / g["dpsSword"], g["dpsTwoHand"] / g["dpsSword"]
        if not (0.8 <= bow <= 1.0):
            warns.append(f"{g['grade']}등급 활/검 DPS {bow:.2f} (기준 0.8~1.0)")
        if not (1.25 <= th <= 1.55):   # D-257
            warns.append(f"{g['grade']}등급 양손검/검 DPS {th:.2f} (기준 1.25~1.55, D-257)")
    lines.append("## 검사")
    lines.append("")
    lines.append("- 오류 없음" if not errors else "\n".join(f"- **오류: {e}**" for e in errors))
    lines.append("- 전투 감각 기준 안" if not warns else "\n".join(f"- 경고: {w}" for w in warns))
    lines.append("")
    return errors


# ============================================================ Common/DataTable 반영 (--write, A2.3b)


def fmt_num(v):
    """CSV 표기: 정수는 그대로, 실수는 소수 한 자리 이상(1.0, 0.5)."""
    if isinstance(v, float):
        return f"{v:.1f}" if v == round(v, 1) else repr(v)
    return str(v)


def merge_table(name, new_cols, updates, fill_if_empty=None, add_rows=None):
    """CSV 한 개에 이 스크립트가 맡은 칸만 덮어쓴 새 내용을 돌려준다(쓰지는 않음).
    new_cols : [(열 이름, 이 열 뒤에 넣음, 기본값)] — 없을 때만 끼워 넣는다(있으면 자리·값 그대로 두고 updates로 덮음)
    updates  : {ID: {열: 값}} — 그 행의 그 칸을 덮는다. 없는 ID는 오류(add_rows로만 행을 만든다)
    fill_if_empty : {ID: {열: 값}} — 그 칸이 비어 있을 때만 채운다(외형 열 예외)
    add_rows : {ID: {열: 값}} — 없는 행이면 이 값으로 만든다(나머지 칸은 template 행 = 첫 행의 값). 있으면 updates와 같게 덮는다
    """
    path = os.path.join(DATA, name)
    with open(path, encoding="utf-8", newline="") as f:
        before = f.read()
    rows = list(csv.reader(io.StringIO(before)))
    header, body = rows[0], [r for r in rows[1:] if r]
    for col, after, default in new_cols:
        if col in header:
            continue
        at = header.index(after) + 1
        header.insert(at, col)
        for r in body:
            r.insert(at, default)
    idx = {c: i for i, c in enumerate(header)}
    by_id = {r[0]: r for r in body}
    for rid, cols in (add_rows or {}).items():
        if str(rid) not in by_id:
            r = list(body[0])
            r[0] = str(rid)
            body.append(r)
            by_id[str(rid)] = r
        updates = dict(updates)
        updates[rid] = {**updates.get(rid, {}), **cols}
    for rid, cols in updates.items():
        r = by_id.get(str(rid))
        if r is None:
            raise SystemExit(f"[--write] {name}: 행 {rid} 이 없습니다")
        for col, v in cols.items():
            r[idx[col]] = fmt_num(v)
    for rid, cols in (fill_if_empty or {}).items():
        r = by_id.get(str(rid))
        if r is None:
            continue
        for col, v in cols.items():
            if r[idx[col]] == "":
                r[idx[col]] = v
    body.sort(key=lambda r: int(r[0]))
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(header)
    w.writerows(body)
    return path, before, out.getvalue()


def datatable_plan():
    """A2.3b가 맡는 세 테이블의 새 내용. [(경로, 이전, 이후)]"""
    plans = []
    # PlayerStatTable: 레벨 1~30 (Name = 1000 + 레벨). AttackSpeed·MoveSpeed는 첫 행 값 그대로(맡지 않음)
    stat_rows = {}
    for r in level_rows():
        stat_rows[1000 + r["level"]] = {"Level": r["level"], "MaxHp": r["hp"], "AttackDamage": r["atk"], "MaxMp": r["mp"], "ExpToNext": r["expToNext"]}
    plans.append(merge_table("PlayerStatTable.csv", [("MaxMp", "MoveSpeed", "0"), ("ExpToNext", "MaxMp", "0")], {}, add_rows=stat_rows))
    # MonsterTable: 표시 레벨·경험치(새 열), HP·공격력·공격 속도·경직
    mon = {mid: {"Level": m["level"], "Exp": m["exp"], "MaxHp": m["hp"], "AttackDamage": m["attack"], "AttackSpeed": float(m["attackSpeed"]),
                 "HitStunMs": MONSTER_HIT_STUN_MS[m["boss"]]} for mid, m in MONS.items()}
    plans.append(merge_table("MonsterTable.csv", [("Level", "BodyRadiusCm", "1"), ("Exp", "Level", "0")], mon))
    # SkillTable: MpCost·AttackScale(새 열, 기본 0·100), BaseDamage. 아직 CSV에 없는 스킬(양손검 4001·4002 — WT1)은 건너뛴다
    with open(os.path.join(DATA, "SkillTable.csv"), encoding="utf-8", newline="") as f:
        existing = {r[0] for r in list(csv.reader(f))[1:] if r}
    sk, sk_new = {}, {}
    for sid, s in SKILLS.items():
        if str(sid) in existing:
            sk[sid] = {"BaseDamage": s[2], "MpCost": s[5], "AttackScale": s[7]}
        elif sid in TWO_HAND_ROW:
            hit, lock, radius, angle, hit_fx, montage = TWO_HAND_ROW[sid]
            sk_new[sid] = {"WeaponType": "TwoHandSword", "Slot": s[1], "TargetingType": "NonTargeted", "HitShape": "Sector",
                           "HitDelayMs": hit, "CooldownMs": s[4], "AnimMs": s[3], "MoveLockMs": lock, "CastRange": 0,
                           "Radius": radius, "Angle": angle, "Range": 0, "Width": 0, "BaseDamage": s[2], "ProjSpeed": 0,
                           "DurationMs": 0, "TickMs": 0, "AimMoveSpeedMul": "1.0", "AimMaxMs": 0, "MpCost": s[5],
                           "AttackScale": s[7], "Fx": "", "FxBaseRadius": 0, "HitFx": hit_fx, "Montage": montage, "Icon": ""}
    for sid in MONSTER_SKILL_IDS:
        sk[sid] = {"BaseDamage": MONSTER_BASE_DAMAGE}
    plans.append(merge_table("SkillTable.csv", [("MpCost", "AimMaxMs", "0"), ("AttackScale", "MpCost", "100"), ("HitFx", "FxBaseRadius", "")],
                             sk, fill_if_empty=SKILL_ASSET_FILL, add_rows=sk_new))
    plans.extend(item_shop_drop_plans())
    return plans


ITEM_PATH_COLS = ["Icon", "MeshM", "MeshF", "StaticMesh", "AttachSocket", "AttachM", "AttachF", "Mask", "HideHair",
                  "BackSocket", "BackM", "BackF"]


def item_shop_drop_plans():
    """IT3: ItemTable(기존 행은 능력치·이름·등급 열만, 새 행은 외형 칸을 비움), ShopTable·DropTable(전체 다시 씀)."""
    plans = []
    with open(os.path.join(DATA, "ItemTable.csv"), encoding="utf-8", newline="") as f:
        existing = {r[0] for r in list(csv.reader(f))[1:] if r}
    rows = {}
    for it in ITEMS:
        cols = dict(Slot=it["slot"], Grade=GRADE_ENUM[it["grade"]], Tier=TIER_ENUM[it["grade"]], WeaponType=it["weapon"],
                    BonusAttackDamage=it["atk"], BonusMaxHp=it["hp"], BonusMaxMp=it["mp"], Starter=it["starter"], RequiredLevel=1,
                    MaxStack=1, UseEffect="None", UseValue=0, UseCooldownMs=0, SellPrice=it["sell"], DisplayName=it["name"])
        rows[it["id"]] = cols
    for c in CONSUMABLES:
        rows[c[0]] = dict(Slot="None", Grade="Common", Tier="None", WeaponType="None", BonusAttackDamage=0, BonusMaxHp=0,
                          BonusMaxMp=0, Starter=0, RequiredLevel=1, MaxStack=20, UseEffect=c[2], UseValue=c[3],
                          UseCooldownMs=c[4], SellPrice=c[6], DisplayName=c[1])
    for l in LOOT:
        rows[l[0]] = dict(Slot="None", Grade="Common", Tier="None", WeaponType="None", BonusAttackDamage=0, BonusMaxHp=0,
                          BonusMaxMp=0, Starter=0, RequiredLevel=1, MaxStack=99, UseEffect="None", UseValue=0,
                          UseCooldownMs=0, SellPrice=l[3], DisplayName=l[1])
    updates, adds = {}, {}
    for rid, cols in rows.items():
        if str(rid) in existing:
            updates[rid] = cols
        else:
            adds[rid] = {**cols, **{c: "" for c in ITEM_PATH_COLS}}
    plans.append(merge_table("ItemTable.csv", [("BonusMaxMp", "BonusMaxHp", "0")], updates, add_rows=adds))

    # ShopTable: 물약 4종 + 00등급 9종 (전체 다시 씀)
    shop = [[1001 + i, 1, c[0], c[5]] for i, c in enumerate(CONSUMABLES)]
    for it in ITEMS:
        if it["grade"] == 0:
            shop.append([1001 + len(shop), 1, it["id"], SHOP_GEAR_BUY])
    plans.append(full_table("ShopTable.csv", ["Name", "ShopId", "ItemId", "BuyPrice"], shop))
    # DropTable: 몬스터별 전부 다시 씀 (IT3)
    plans.append(full_table("DropTable.csv", ["Name", "MonsterId", "ItemId", "DropRate", "MinCount", "MaxCount"], drop_rows()))
    # QuestTable: 전체 다시 씀 (Q1 — 서버·UE 공용, Description은 클라이언트 전용)
    plans.append(full_table("QuestTable.csv", QUEST_HEADER, [list(q) for q in QUESTS]))
    return plans


def full_table(name, header, rows):
    path = os.path.join(DATA, name)
    before = ""   # 아직 없는 테이블은 새로 만든다
    if os.path.exists(path):
        with open(path, encoding="utf-8", newline="") as f:
            before = f.read()
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(header)
    w.writerows(rows)
    return path, before, out.getvalue()


def write_datatables(really_write):
    changed = 0
    for path, before, after in datatable_plan():
        name = os.path.basename(path)
        if before == after:
            print(f"[--write] {name}: 바뀐 것 없음")
            continue
        changed += 1
        diff = difflib.unified_diff(before.splitlines(), after.splitlines(), f"a/{name}", f"b/{name}", lineterm="", n=0)
        print("\n".join(diff))
        if really_write:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(after)
            print(f"[--write] {name}: 씀")
    if not really_write:
        print(f"[--diff] 바뀔 파일 {changed}개 (쓰지 않음)")


def main():
    L = ["# IT1 검사표 (balance.py가 만든 파일 — 직접 고치지 않음)", ""]
    levels = level_rows()
    grades = grade_rows()
    drops = drop_expect_rows()

    L += ["## 레벨별 기본 스탯·경험치", "",
          "다음 레벨 경험치 = 500 × 레벨, HP = 500 + 100/레벨, 공격 = 2/레벨, MP = 100 + 10/레벨. 처치 수는 참고(그 대역 일반 몬스터만 잡을 때).", "",
          "| 레벨 | 다음 레벨 경험치 | 기본 HP | 기본 공격 | 기본 MP | 대역 | 필요 처치 수 | 던전 횟수 |",
          "|---|---|---|---|---|---|---|---|"]
    rows = []
    for r in levels:
        runs = "-" if r["dungeonRuns"] is None else f"{r['dungeonRuns']:.2f}"
        L.append(f"| {r['level']} | {r['expToNext']} | {r['hp']} | {r['atk']} | {r['mp']} | {r['band']} | {r['killsToNext']:g} | {runs} |")
        rows.append([1000 + r["level"], r["level"], r["hp"], r["atk"], 1.0, 500, r["mp"], r["expToNext"]])
    L.append("")
    write_csv("PlayerStatTable.preview.csv", ["Name", "Level", "MaxHp", "AttackDamage", "AttackSpeed", "MoveSpeed", "MaxMp", "ExpToNext"], rows)

    L += ["## 장비 능력치", "", "| ID | 이름 | 등급 | 부위 | 무기 | 공격 | HP | MP | 판매가 | 기본 지급 |", "|---|---|---|---|---|---|---|---|---|---|"]
    rows = []
    for it in sorted(ITEMS, key=lambda x: (x["grade"], x["id"])):
        L.append(f"| {it['id']} | {it['name']} | {it['grade']:02d} | {it['slot']} | {it['weapon']} | {it['atk']} | {it['hp']} | {it['mp']} | {it['sell']} | {it['starter']} |")
        rows.append([it["id"], it["slot"], GRADE_ENUM[it["grade"]], TIER_ENUM[it["grade"]], it["weapon"], it["atk"], it["hp"], it["mp"], it["starter"], 1, 1, "None", 0, 0, it["sell"], it["name"]])
    for c in CONSUMABLES:
        rows.append([c[0], "None", "Common", "None", "None", 0, 0, 0, 0, 1, 20, c[2], c[3], c[4], c[6], c[1]])
    for l in LOOT:
        rows.append([l[0], "None", "Common", "None", "None", 0, 0, 0, 0, 1, 99, "None", 0, 0, l[3], l[1]])
    write_csv("ItemTable.preview.csv", ["Name", "Slot", "Grade", "Tier", "WeaponType", "BonusAttackDamage", "BonusMaxHp", "BonusMaxMp", "Starter", "RequiredLevel", "MaxStack", "UseEffect", "UseValue", "UseCooldownMs", "SellPrice", "DisplayName"], rows)
    L.append("")

    L += ["## 등급 교체 레벨의 전신 능력치와 무기별 지속 DPS (허수아비 60초, MP 포함)", "",
          "| 레벨 | 등급 | HP(검+방패) | 공격(검) | HP(양손검) | 공격(양손검) | MP | DPS 검 | 활 | 양손검 | 활/검 | 양손검/검 |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for g in grades:
        L.append(f"| {g['level']} | {g['grade']} | {g['hpSword']} | {g['atkSword']} | {g['hpTwoHand']} | {g['atkTwoHand']} | {g['mp']} | "
                 f"{g['dpsSword']} | {g['dpsBow']} | {g['dpsTwoHand']} | {g['dpsBow'] / g['dpsSword']:.2f} | {g['dpsTwoHand'] / g['dpsSword']:.2f} |")
    L.append("")

    L += ["## 몬스터 (기준 = 한손검 + 방패 혼자, 몬스터 경직은 빼고 계산)", "",
          "| ID | 이름 | 표시 레벨 | HP | 공격력 | 한 대 피해 | 공격 속도 | 경험치 | 기준 플레이어 | 처치 검(초) | 활 | 양손검 | 맞아 죽는 시간 검+방패 | 양손검 | 혼자 잡을 때 큰 HP 물약 |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    rows = []
    for m in MONS.values():
        pots = "-" if m["bigPotions"] is None else m["bigPotions"]
        L.append(f"| {m['id']} | {m['name']} | {m['level']} | {m['hp']} | {m['attack']} | {m['hit']} | {m['attackSpeed']} | {m['exp']} | Lv{m['refLevel']} {m['refGrade']} | "
                 f"{m['ttkSword']} | {m['ttkBow']} | {m['ttkTwoHand']} | {m['ttdSword']} | {m['ttdTwoHand']} | {pots} |")
        rows.append([m["id"], m["level"], m["hp"], m["attack"], m["attackSpeed"], m["exp"]])
    L += ["", f"- 몬스터 스킬 9001~9005의 `BaseDamage`는 모두 {MONSTER_BASE_DAMAGE}(한 대 피해 = 공격력 + {MONSTER_BASE_DAMAGE}).",
          f"- 던전 1회(일반 {DUNGEON_NORMALS} + 우두머리) 경험치 {DUNGEON_RUN_EXP:,}.", ""]
    write_csv("MonsterTable.preview.csv", ["Name", "Level", "MaxHp", "AttackDamage", "AttackSpeed", "Exp"], rows)

    L += ["## 대역을 지나는 동안 얻는 장비 기대값 (처치 수 = 대역 경험치 ÷ 몬스터 경험치)", "",
          "| 대역 | 처치 | 등급 | 9종 합계 | 특정 아이템 하나 |", "|---|---|---|---|---|"]
    for d in drops:
        L.append(f"| {d['band']} | {d['source']} | {d['grade']} | {d['totalPieces']} | {d['perItem']} |")
    L += ["", f"- 03 특정 아이템 하나 = 던전 우두머리 평균 {10000 / GEAR_DROP[1005][3]:.0f}회, 03 아무거나 하나 = {10000 / (GEAR_DROP[1005][3] * 9):.1f}회마다. 나머지는 경매장.", ""]
    write_csv("DropTable.preview.csv", ["Name", "MonsterId", "ItemId", "DropRate", "MinCount", "MaxCount"], drop_rows())

    shop = [[1001 + i, 1, c[0], c[5]] for i, c in enumerate(CONSUMABLES)]
    for it in ITEMS:
        if it["grade"] == 0:
            shop.append([1001 + len(shop), 1, it["id"], SHOP_GEAR_BUY])
    write_csv("ShopTable.preview.csv", ["Name", "ShopId", "ItemId", "BuyPrice"], shop)
    write_csv("SkillMp.preview.csv", ["Name", "WeaponType", "Slot", "BaseDamage", "AnimMs", "CooldownMs", "MpCost", "AttackScale"],
              [[sid, s[0], s[1], s[2], s[3], s[4], s[5], s[7]] for sid, s in SKILLS.items()])

    L += ["## 퀘스트 보상", "", "| ID | 이름 | NPC | 종류 | 대상 | 수량 | 권장 레벨 | 경험치 | 골드 | 보상 아이템 |", "|---|---|---|---|---|---|---|---|---|---|"]
    qrows = []
    for q in QUESTS:
        L.append(f"| {q[0]} | {q[1]} | {q[2]} | {q[3]} | {q[4]} | {q[5]} | {q[6]} | {q[7]} | {q[8]} | {q[9]} × {q[10]} |")
        qrows.append(list(q))
    L.append("")
    write_csv("QuestTable.preview.csv", QUEST_HEADER, qrows)

    write_dash(levels, grades, drops)
    errs = checks(L, grades)
    with open(os.path.join(OUT, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    sys.stdout.reconfigure(encoding="utf-8")
    if "--write" in sys.argv or "--diff" in sys.argv:
        # 검사표는 파일에만 남기고, 화면에는 CSV 변경만 보인다
        if errs:
            print("\n".join(L))
            print("[--write] 검사 오류가 있어 CSV를 쓰지 않습니다")
            return 1
        write_datatables("--write" in sys.argv)
        return 0
    print("\n".join(L))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
