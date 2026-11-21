# -*- coding: utf-8 -*-
"""L5(D-203): 지형 정의의 마을 목책·문틀을 높은 방벽·닫힌 문으로 바꾼다.

world_continent01.json의 dress.items에서
  - ring_prop(VillageWall)의 메시를 SM_Rampart로,
  - 마을 문 셋(GateN·GateE·GateW)의 gate_prop을 SM_ClosedGate + closed(문 전체를 막는 충돌 하나)로 바꾼다.
정거장 문(StationGate)은 열린 문틀 그대로 둔다. 여러 번 실행해도 결과가 같다.
바꾼 뒤 에디터에서 world_step dress → check_dress → 저장 → MapExport → NavGenerator 순서.

사용: python -I -X utf8 l5_wall_gate.py
"""
import json
import os

PATH = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "UEClient", "JM", "Content", "Python", "jm_kit", "world_continent01.json")
# 소품 폴더는 테마 기준(D-245, 2026-11-21 — 옛 /Game/World/Props/). P + "<이름>/SM_<이름>" → /Game/Environment/<테마>/<분류>/<이름>/SM_<이름>
_THEME = {**dict.fromkeys(("Banner", "Cart", "InnTent", "MarketStall", "TradeAwning"), "/Game/Environment/Village/Props"),
          **dict.fromkeys(("Palisade", "Rampart", "GateFrame", "ClosedGate"), "/Game/Environment/Village/Architecture"),
          **dict.fromkeys(("Campfire", "CratesBarrels"), "/Game/Environment/Common/Props"),
          **dict.fromkeys(("BonePile", "Brazier", "GuardianStatue", "Sarcophagus", "WallTorch", "WeaponRack"), "/Game/Environment/Dungeon/Props")}


class _PropRoot(str):
    def __add__(self, rest):
        return f"{_THEME[rest.split('/', 1)[0]]}/{rest}"


P = _PropRoot()
RAMPART = P + "Rampart/SM_Rampart"
GATE = P + "ClosedGate/SM_ClosedGate"
VILLAGE_GATES = ("GateN", "GateE", "GateW")

# 방벽: 블록아웃 한 칸(6.3m) × cover = 폭 약 6.55m, 높이는 메시 비율대로(약 8m — 마을 안 카메라 최고 높이 5.4m에서도
# 바깥 지면이 가려지는 높이는 6m 이상, TESTING.md 2026-11-19 L5). col_fit = [두께, 길이, 높이] 비율.
# close_gaps_m: 이 폭 이하인 틈(문 자리 10m)의 양 끝에 칸을 하나씩 더 놓아 문 옆으로 바깥이 보이지 않게 한다(남쪽 70m 틈은 능선이 막음).
WALL = {"cover": 1.04, "col_fit": [1.0, 1.0, 0.9], "sink_m": 0.3, "close_gaps_m": 20}
# 닫힌 문: 목책이 비워 둔 문 자리(폭 10m)를 덮는 폭. total = (width_m + 2 × thick_m) × cover = 10 × 1.06 = 10.6m.
GATE_FIT = {"cover": 1.06, "closed": True, "door_thick_m": 1.2, "sink_m": 0.2}

with open(PATH, encoding="utf-8") as f:
    d = json.load(f)

changed = []
for it in d["dress"]["items"]:
    if it.get("kind") == "ring_prop" and it.get("from") == "VillageWall":
        it["mesh"] = RAMPART
        it.update(WALL)
        changed.append("VillageWall")
    elif it.get("kind") == "gate_prop" and it.get("from") in VILLAGE_GATES:
        it["mesh"] = GATE
        it.pop("post_off", None)
        it.pop("post_w", None)
        it.update(GATE_FIT)
        changed.append(it["from"])

# 남쪽 틈(deg 180, 폭 70m)을 없앤다: 능선이 막는다고 보고 비워 뒀지만 벽 끝과 능선 사이로 걸어 나갈 수 있었다
# (2026-11-19 NavGenerator --query route로 확인 — 문을 막아도 마을 안이 바깥과 이어져 있었음). 벽을 한 바퀴 두른다.
for bo in d["blockout"]:
    if bo.get("kind") == "ring" and bo.get("label") == "VillageWall":
        before = len(bo["gaps"])
        bo["gaps"] = [g for g in bo["gaps"] if g["deg"] != 180]
        if len(bo["gaps"]) != before:
            changed.append("VillageWall south gap removed")

with open(PATH, "w", encoding="utf-8", newline="\n") as f:
    json.dump(d, f, ensure_ascii=False, indent=2)
    f.write("\n")
print("changed:", changed)
