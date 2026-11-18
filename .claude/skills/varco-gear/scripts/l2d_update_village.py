# world_continent01.json의 dress에 마을 소품(L2d) 항목을 넣는다(다시 돌리면 기존 마을 항목을 지우고 다시 넣음).
import json
import sys

p = sys.argv[1]
with_wall = len(sys.argv) > 2 and sys.argv[2] == "wall"
d = json.load(open(p, encoding="utf-8"))
P = "/Game/World/Props/"
M = {n: f"{P}{n}/SM_{n}" for n in ("Palisade", "GateFrame", "MarketStall", "Banner", "Campfire", "CratesBarrels", "TradeAwning", "Cart", "InnTent")}
gate = {"cover": 1.25, "post_off": 0.29, "post_w": 0.17}
banner = {"height_m": 6.5, "col_fit": [0.3, 0.55, 0.3]}
stall = {"width_m": 4.8, "col_fit": [0.95, 0.8, 0.6]}
crates = {"col_fit": [0.75, 0.75, 0.6]}
# 차양: 지붕 아래로 지나다니므로 충돌은 뒤쪽 좌판(col_off)과 네 귀퉁이 기둥(col_extra)만.
# 가운데 기둥 둘은 자리가 가운데가 아니어서 충돌을 두지 않았다(캐릭터가 통과함 — 캡처로 자리를 잰 뒤 추가할 것)
awning = {"col_fit": [0.8, 0.2, 0.3], "col_max_h": 1.2, "col_off": [0.0, -0.55],
          "col_extra": [[-0.9, -0.86, 0.6], [0.9, -0.86, 0.6], [-0.9, 0.86, 0.6], [0.9, 0.86, 0.6]]}

items = [
    # 문 셋 + 정거장 문
    {"kind": "gate_prop", "from": "GateN", "mesh": M["GateFrame"], **gate},
    {"kind": "gate_prop", "from": "GateE", "mesh": M["GateFrame"], **gate},
    {"kind": "gate_prop", "from": "GateW", "mesh": M["GateFrame"], **gate},
    {"kind": "gate_prop", "from": "StationGate", "mesh": M["GateFrame"], **gate},
    # 가운데 모닥불(시작·부활 지점)
    {"kind": "prop", "from": "Campfire", "mesh": M["Campfire"], "width_m": 3.4, "col_shape": "Cylinder", "col_fit": [0.85, 0.85, 0.5]},
    # 깃발: 북문 안쪽(블록아웃 자리) + 동·서문 안쪽 + 정거장
    {"kind": "prop", "from": "BannerN_L", "mesh": M["Banner"], "yaw_off": 90.0, **banner},
    {"kind": "prop", "from": "BannerN_R", "mesh": M["Banner"], "yaw_off": 90.0, **banner},
    {"kind": "prop", "label": "BannerE_L", "mesh": M["Banner"], "x": -143.5, "y": 67.5, "yaw": 180.0, **banner},
    {"kind": "prop", "label": "BannerE_R", "mesh": M["Banner"], "x": -156.5, "y": 67.5, "yaw": 180.0, **banner},
    {"kind": "prop", "label": "BannerW_L", "mesh": M["Banner"], "x": -143.5, "y": -67.5, "yaw": 0.0, **banner},
    {"kind": "prop", "label": "BannerW_R", "mesh": M["Banner"], "x": -156.5, "y": -67.5, "yaw": 0.0, **banner},
    {"kind": "prop", "from": "StationPost_L", "mesh": M["Banner"], "yaw_off": -60.0, **banner},
    {"kind": "prop", "from": "StationPost_R", "mesh": M["Banner"], "yaw_off": -60.0, **banner},
    # 넓은 마을이 비어 보이지 않게 같은 소품을 더 놓는다(상점가 연장, 광장 깃발, 문 옆 짐)
    {"kind": "prop", "label": "MarketStall_5", "mesh": M["MarketStall"], "x": -138.0, "y": -22.0, "yaw": 95.0, **stall},
    {"kind": "prop", "label": "MarketStall_6", "mesh": M["MarketStall"], "x": -138.0, "y": -58.0, "yaw": 82.0, **stall},
    {"kind": "prop", "label": "MarketStall_7", "mesh": M["MarketStall"], "x": -162.0, "y": -22.0, "yaw": -96.0, **stall},
    {"kind": "prop", "label": "MarketStall_8", "mesh": M["MarketStall"], "x": -162.0, "y": -58.0, "yaw": -84.0, **stall},
    {"kind": "prop", "label": "MarketCrates_1", "mesh": M["CratesBarrels"], "x": -131.5, "y": -40.0, "yaw": 30.0, "width_m": 2.6, **crates},
    {"kind": "prop", "label": "MarketCrates_2", "mesh": M["CratesBarrels"], "x": -168.5, "y": -40.0, "yaw": 210.0, "width_m": 2.6, **crates},
    {"kind": "prop", "label": "MarketCart", "mesh": M["Cart"], "x": -156.0, "y": -51.0, "yaw": 80.0, "width_m": 3.2, "col_fit": [0.8, 0.8, 0.6]},
    {"kind": "prop", "label": "PlazaBanner_1", "mesh": M["Banner"], "x": -138.0, "y": -12.0, "yaw": 135.0, **banner},
    {"kind": "prop", "label": "PlazaBanner_2", "mesh": M["Banner"], "x": -138.0, "y": 12.0, "yaw": 45.0, **banner},
    {"kind": "prop", "label": "PlazaBanner_3", "mesh": M["Banner"], "x": -162.0, "y": -12.0, "yaw": 225.0, **banner},
    {"kind": "prop", "label": "PlazaBanner_4", "mesh": M["Banner"], "x": -162.0, "y": 12.0, "yaw": -45.0, **banner},
    {"kind": "prop", "label": "GateNCart", "mesh": M["Cart"], "x": -87.0, "y": -10.0, "yaw": 200.0, "width_m": 3.4, "col_fit": [0.8, 0.8, 0.6]},
    {"kind": "prop", "label": "GateNCrates", "mesh": M["CratesBarrels"], "x": -86.0, "y": 10.5, "yaw": 60.0, "width_m": 2.8, **crates},
    {"kind": "prop", "label": "GateECrates", "mesh": M["CratesBarrels"], "x": -140.0, "y": 60.0, "yaw": 150.0, "width_m": 2.8, **crates},
    {"kind": "prop", "label": "GateWCrates", "mesh": M["CratesBarrels"], "x": -160.0, "y": -60.0, "yaw": -20.0, "width_m": 2.8, **crates},
    {"kind": "prop", "label": "TradeAwning_2", "mesh": M["TradeAwning"], "x": -172.0, "y": 40.0, "yaw": 180.0, "width_m": 9.0, **awning},
    {"kind": "prop", "label": "TradeCrates_3", "mesh": M["CratesBarrels"], "x": -166.0, "y": 31.0, "yaw": 300.0, "width_m": 2.4, **crates},
    # 서쪽 상점가: 두 줄이 가운데 길(x = -150)을 마주 본다(가판대 앞 = 메시 +Y)
    {"kind": "prop", "from": "MarketStall_1", "mesh": M["MarketStall"], "yaw_off": 90.0, **stall},
    {"kind": "prop", "from": "MarketStall_2", "mesh": M["MarketStall"], "yaw_off": 90.0, **stall},
    {"kind": "prop", "from": "MarketStall_3", "mesh": M["MarketStall"], "yaw_off": -90.0, **stall},
    {"kind": "prop", "from": "MarketStall_4", "mesh": M["MarketStall"], "yaw_off": -90.0, **stall},
    # 동쪽 거래소 = 큰 차양(앞 = +Y가 마을 가운데(서쪽)를 봄). 지붕 아래로 지나다니므로 충돌은 가운데 좌판만
    {"kind": "prop", "from": "TradeHall", "mesh": M["TradeAwning"], "width_m": 12.0, "yaw_off": 180.0, **awning},
    # 창고 = 상자·통 더미 + 수레
    {"kind": "prop", "from": "Storage", "mesh": M["CratesBarrels"], "width_m": 3.4, **crates},
    {"kind": "prop", "label": "StorageCrates_2", "mesh": M["CratesBarrels"], "x": -131.0, "y": 42.5, "yaw": 110.0, "width_m": 2.8, **crates},
    {"kind": "prop", "label": "StorageCrates_3", "mesh": M["CratesBarrels"], "x": -137.5, "y": 42.0, "yaw": 200.0, "width_m": 2.4, **crates},
    {"kind": "prop", "label": "StorageCart", "mesh": M["Cart"], "x": -136.0, "y": 33.0, "yaw": -35.0, "width_m": 3.4, "col_fit": [0.8, 0.8, 0.6]},
    {"kind": "prop", "from": "TradeCrates_1", "mesh": M["CratesBarrels"], "width_m": 2.2, **crates},
    {"kind": "prop", "from": "TradeCrates_2", "mesh": M["CratesBarrels"], "width_m": 1.8, "yaw_off": 140.0, **crates},
    # 남쪽 여관 = 천막(입구 = 메시 +X가 북쪽 마을 가운데를 봄). 안으로 들어가지 않으므로 통째로 막는다
    {"kind": "prop", "from": "Inn", "mesh": M["InnTent"], "width_m": 11.0, "yaw_off": -90.0, "col_fit": [0.85, 0.85, 0.5]},
    {"kind": "prop", "label": "InnTent_2", "mesh": M["InnTent"], "x": -192.0, "y": 13.0, "yaw": -75.0, "width_m": 8.0, "col_fit": [0.85, 0.85, 0.5]},
    {"kind": "prop", "label": "InnTent_3", "mesh": M["InnTent"], "x": -192.0, "y": -25.0, "yaw": -105.0, "width_m": 8.0, "col_fit": [0.85, 0.85, 0.5]},
    {"kind": "prop", "from": "InnCrates", "mesh": M["CratesBarrels"], "width_m": 2.0, "yaw_off": 70.0, **crates},
    {"kind": "prop", "label": "InnCart", "mesh": M["Cart"], "x": -190.0, "y": -17.0, "yaw": 20.0, "width_m": 3.2, "col_fit": [0.8, 0.8, 0.6]},
]
if with_wall:
    # 목책: 블록아웃 고리(벽 반경 72m, 한 칸 6m)를 따라 한 칸씩. 칸 사이가 뜨지 않게 길이를 조금 겹친다
    items.insert(0, {"kind": "ring_prop", "from": "VillageWall", "mesh": M["Palisade"], "cover": 1.04, "col_fit": [1.0, 0.8, 0.9], "sink_m": 0.25})

for it in items:
    it["group"] = "village"
dress = d["dress"]
dress["items"] = [it for it in dress["items"] if it.get("group") != "village"] + items
if "ring_prop" not in dress["_comment"]:
    dress["_comment"] += " 마을 소품(L2d, group village): ring_prop(blockout ring을 따라 한 칸씩), gate_prop(arch 자리의 문틀 — 충돌은 기둥 둘, post_off·post_w = 메시 비율), prop(from 블록아웃 box 자리 또는 x·y·yaw, 크기 width_m·height_m, 충돌 col_fit [가로X, 가로Y, 높이]·col_shape·col_max_h·collide)."
json.dump(d, open(p, "w", encoding="utf-8", newline="\n"), ensure_ascii=False, indent=2)
print("ok", len(items))
