# 던전 Continent01_Dungeon(DG4)의 지형 정의를 만든다: 방·통로 사각형 → 벽 상자·바닥·Ruins 장식·불빛·마커.
# 사용: python -I -X utf8 dg4_layout.py  (저장소 루트 기준 world_continent01_dungeon.json을 다시 씀)
# 구도를 고칠 때는 아래 ROOMS·CORRIDORS·DOORS만 바꾸고 다시 돌린 뒤 에디터에서 world_step dg_build → world_place_markers.
# 단위 m, x = 북(+), y = 동(+). 걷는 영역은 사각형들의 합집합, 벽은 그 둘레 1m.
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
OUT = os.path.join(ROOT, "UEClient", "JM", "Content", "Python", "jm_kit", "world_continent01_dungeon.json")

R = "/Game/ParagonProps/Monolith/Ruins/Meshes/"
M = "/Game/Environment/Dungeon/Materials/"
# 소품 폴더는 테마 기준(D-245, 2026-11-21 — 옛 /Game/World/Props/). P + "<이름>/SM_<이름>" → /Game/Environment/<테마>/<분류>/<이름>/SM_<이름>
_THEME = {**dict.fromkeys(("Banner", "Cart", "InnTent", "MarketStall", "TradeAwning"), "/Game/Environment/Village/Props"),
          **dict.fromkeys(("Palisade", "Rampart", "GateFrame", "ClosedGate"), "/Game/Environment/Village/Architecture"),
          **dict.fromkeys(("Campfire", "CratesBarrels"), "/Game/Environment/Common/Props"),
          **dict.fromkeys(("BonePile", "Brazier", "GuardianStatue", "Sarcophagus", "WallTorch", "WeaponRack"), "/Game/Environment/Dungeon/Props")}


class _PropRoot(str):
    def __add__(self, rest):
        return f"{_THEME[rest.split('/', 1)[0]]}/{rest}"


P = _PropRoot()
WALL_H = 8.0
FIRE_FX = "/Game/FX/Buff/P_Aura_Embers_Linger"   # 계속 타는 불 파티클이 없어 보유 잔불 이펙트를 쓴다(DG4d)
CELL = 0.5

# 걷는 사각형 (x0, x1, y0, y1)
ROOMS = {
    "start": (-47.0, -33.0, -33.0, -19.0),   # 14 × 14, 시작 지점·출구 포털
    "room1": (-24.0, -4.0, -36.0, -16.0),    # 20 × 20
    "room2": (-4.0, 16.0, 16.0, 36.0),       # 20 × 20
    "boss": (18.0, 48.0, -39.0, -9.0),       # 30 × 30
}
CORRIDORS = {
    "c1": (-33.0, -24.0, -29.5, -22.5),      # 시작 방 → 방1
    "a_east": (-17.5, -10.5, -16.0, 29.5),   # 방1 동쪽 문 → 동쪽으로
    "a_north": (-10.5, -4.0, 22.5, 29.5),    # 꺾어서 북쪽 → 방2 남쪽 문
    "b_north": (16.0, 36.5, 22.5, 29.5),     # 방2 북쪽 문 → 북쪽으로
    "b_west": (29.5, 36.5, -9.0, 22.5),      # 꺾어서 서쪽 → 우두머리 방 동쪽 문
}
# 통로 방향을 폭·길이로 판정할 수 없는 것(길이가 폭과 비슷함)만 적는다 — a_north는 x 방향(a_east 끝 → 방2), y로 판정하면 횃불이 a_east 바닥 위 허공에 놓였음(CL1 리뷰, 2026-11-24)
CORRIDOR_ALONG_X = {"a_north": True}
# 문: (방, 면 N/S/E/W, 면을 따라가는 좌표의 가운데, 통로 폭)
DOORS = [
    ("start", "N", -26.0, 7.0), ("room1", "S", -26.0, 7.0), ("room1", "E", -14.0, 7.0),
    ("room2", "S", 26.0, 7.0), ("room2", "N", 26.0, 7.0), ("boss", "E", 33.0, 7.0),
]
# VARCO 소품(DG4d): 메시 로컬에서 "앞"이 향하는 각도(도) — probe 캡처로 확인해 맞춘다
TORCH_FRONT, STATUE_FRONT, RACK_FRONT = 90.0, 90.0, 90.0
INWARD_YAW = {"N": 180.0, "S": 0.0, "E": -90.0, "W": 90.0}   # 방의 면에서 안쪽을 보는 방향


def torch(props, lights, label, x, y, yaw_in, depth, fx=False):
    """벽걸이 횃불 + 불빛. (x, y) = 벽 가운데선 위의 점, yaw_in = 벽에서 안쪽을 보는 방향, depth = 가운데선에서 횃불 중심까지."""
    import math
    c, sn = math.cos(math.radians(yaw_in)), math.sin(math.radians(yaw_in))
    props.append({"label": "T" + label, "mesh": P + "WallTorch/SM_WallTorch", "x": x + c * depth, "y": y + sn * depth, "z": 2.1, "height_m": 1.2,
                  "yaw": yaw_in - TORCH_FRONT})
    L = {"label": "Torch_" + label, "x": x + c * (depth + 0.45), "y": y + sn * (depth + 0.45), "z": 3.45, "intensity": 30, "radius_m": 13, "color": [255, 196, 140]}
    if fx:
        L["fx"] = FIRE_FX
    lights.append(L)


JAMB = 1.5   # 문설주가 문 안쪽으로 들어오는 폭(양쪽) — 통로 7m, 문 4m


def rects():
    return list(ROOMS.values()) + list(CORRIDORS.values())


def walls():
    """걷는 칸에서 1m 안에 있는 걷지 못하는 칸을 벽으로 보고, 줄 단위로 합쳐 상자 목록을 만든다."""
    rs = rects()
    x0 = min(r[0] for r in rs) - 2.0
    x1 = max(r[1] for r in rs) + 2.0
    y0 = min(r[2] for r in rs) - 2.0
    y1 = max(r[3] for r in rs) + 2.0
    nx, ny = int(round((x1 - x0) / CELL)), int(round((y1 - y0) / CELL))

    def walk(i, j):
        cx, cy = x0 + (i + 0.5) * CELL, y0 + (j + 0.5) * CELL
        return any(r[0] <= cx <= r[1] and r[2] <= cy <= r[3] for r in rs)

    W = [[walk(i, j) for j in range(ny)] for i in range(nx)]
    k = int(round(1.0 / CELL))
    wall = [[False] * ny for _ in range(nx)]
    for i in range(nx):
        for j in range(ny):
            if W[i][j]:
                continue
            for di in range(-k, k + 1):
                for dj in range(-k, k + 1):
                    a, b = i + di, j + dj
                    if 0 <= a < nx and 0 <= b < ny and W[a][b]:
                        wall[i][j] = True
                        break
                if wall[i][j]:
                    break
    # 줄(i)마다 연속 구간을 찾고, 같은 구간이 이어지는 줄끼리 합친다
    boxes, open_runs = [], {}
    for i in range(nx + 1):
        runs = set()
        if i < nx:
            j = 0
            while j < ny:
                if wall[i][j]:
                    s = j
                    while j < ny and wall[i][j]:
                        j += 1
                    runs.add((s, j))
                else:
                    j += 1
        for key in list(open_runs):
            if key not in runs:
                boxes.append((open_runs.pop(key), i, key[0], key[1]))
        for key in runs:
            open_runs.setdefault(key, i)
    out = []
    for n, (i0, i1, j0, j1) in enumerate(sorted(boxes)):
        bx0, bx1, by0, by1 = x0 + i0 * CELL, x0 + i1 * CELL, y0 + j0 * CELL, y0 + j1 * CELL
        out.append({"label": f"Wall_{n:02d}", "x": round((bx0 + bx1) / 2, 3), "y": round((by0 + by1) / 2, 3), "z": 0.0,
                    "size": [round(bx1 - bx0, 3), round(by1 - by0, 3), WALL_H], "mat": M + "MI_Dungeon_WallBlock"})
    return out


def side_line(room, side):
    """방의 한 면: (고정 축 좌표 = 벽 가운데, 따라가는 축의 시작, 끝, 안쪽 방향 부호). N·S 면은 y를 따라가고 E·W 면은 x를 따라간다."""
    x0, x1, y0, y1 = ROOMS[room]
    if side == "N":
        return x1 + 0.5, y0, y1, -1
    if side == "S":
        return x0 - 0.5, y0, y1, 1
    if side == "E":
        return y1 + 0.5, x0, x1, -1
    return y0 - 0.5, x0, x1, 1


def pt(side, fixed, along):
    return [fixed, along] if side in "NS" else [along, fixed]


def build():
    surfaces, runs, props, lights = [], [], [], []
    for name, (x0, x1, y0, y1) in list(ROOMS.items()) + list(CORRIDORS.items()):
        # 바닥: 걷는 사각형 + 벽 밑까지 1m. 같은 재질을 월드 좌표로 입혀 겹쳐도 이음매가 보이지 않는다
        surfaces.append({"label": f"Floor_{name}", "x": (x0 + x1) / 2, "y": (y0 + y1) / 2, "z": -1.0,
                         "size": [x1 - x0 + 2.0, y1 - y0 + 2.0, 1.0], "mat": M + "MI_Dungeon_FloorSlate"})
    surfaces += walls()

    # 방 벽: 아래 4m는 Ruins 석판(JungleWall_02A, 8m × 1m × 4m), 그 위는 벽 상자의 석재가 보인다. 문 양쪽에는 문설주(기둥 조각 3단)
    for room in ROOMS:
        for side in "NSEW":
            fixed, a0, a1, inward = side_line(room, side)
            doors = sorted((c, w) for r, s, c, w in DOORS if r == room and s == side)
            cur, segs = a0, []
            for c, w in doors:
                segs.append((cur, c - w / 2))
                cur = c + w / 2
                for sgn, tag in ((-1, "a"), (1, "b")):
                    jc = c + sgn * (w / 2 - JAMB / 2)
                    x, y = pt(side, fixed, jc)
                    for lv in range(3):
                        props.append({"label": f"Jamb_{room}{side}{tag}{lv}", "mesh": R + "JunglePillarBlockPiece_04A", "x": x, "y": y, "z": 2.5 * lv,
                                      "yaw": 90.0 if side in "NS" else 0.0})
                    props[-3]["col"] = {"size": [JAMB, 2.0, WALL_H] if side in "EW" else [2.0, JAMB, WALL_H], "yaw": -props[-3]["yaw"]}
            segs.append((cur, a1))
            for k, (s0, s1) in enumerate(segs):
                if s1 - s0 < 3.0:
                    continue
                runs.append({"label": f"Slab_{room}{side}{k}", "mesh": R + "JungleWall_02A", "from": pt(side, fixed + inward * 0.2, s0), "to": pt(side, fixed + inward * 0.2, s1), "z": 0.0, "stretch": True})   # 벽 상자보다 0.2m 앞으로(같은 두께라 묻히면 안 보임)
        # 방 모서리 기둥(2.5m 블록 3단) — 모서리 안쪽에 반쯤 묻는다
        x0, x1, y0, y1 = ROOMS[room]
        for ci, (cx, cy) in enumerate(((x0, y0), (x0, y1), (x1, y0), (x1, y1))):
            for lv, mesh in enumerate(("JunglePillarBlock_01A", "JunglePillarBlock_01B", "JunglePillarBlock_01C")):
                props.append({"label": f"Corner_{room}{ci}{lv}", "mesh": R + mesh, "x": cx, "y": cy, "z": 2.5 * lv, "yaw": 90.0 * ((ci + lv) % 4)})
            props[-3]["col"] = {"size": [2.6, 2.6, WALL_H]}
        # 방 불빛: 네 벽 가운데 쪽(문이 있으면 비켜서) 벽에서 1.2m 안, 높이 3.2m
        for side in "NSEW":
            fixed, a0, a1, inward = side_line(room, side)
            has_door = any(r == room and s == side for r, s, c, w in DOORS)
            for frac in ((0.2, 0.8) if has_door else (0.3, 0.7)):
                x, y = pt(side, fixed, a0 + (a1 - a0) * frac)
                torch(props, lights, f"{room}{side}{int(frac * 10)}", x, y, INWARD_YAW[side], 0.95, fx=True)   # 석판(벽 앞 0.2m) 바깥

    # 통로: 약 10m마다 양쪽 벽에 번갈아 기둥(벽에 반쯤 묻음)과 불빛
    for name, (x0, x1, y0, y1) in CORRIDORS.items():
        along_x = CORRIDOR_ALONG_X.get(name, (x1 - x0) > (y1 - y0))
        length = (x1 - x0) if along_x else (y1 - y0)
        n = max(1, int(round(length / 10.0)))
        for i in range(n):
            t = (i + 0.5) / n
            left = i % 2 == 0
            if along_x:
                a, wall_y = x0 + length * t, (y0 - 0.5 if left else y1 + 0.5)
                px, py, yaw_in = a, wall_y, (90.0 if left else -90.0)
                tx, ty = a + (2.4 if length >= 15.0 else 0.0), wall_y     # 기둥 옆 벽에
            else:
                a, wall_x = y0 + length * t, (x0 - 0.5 if left else x1 + 0.5)
                px, py, yaw_in = wall_x, a, (0.0 if left else 180.0)
                tx, ty = wall_x, a + (2.4 if length >= 15.0 else 0.0)
            if length >= 15.0:
                for lv, mesh in enumerate(("JunglePillarBlock_01B", "JunglePillarBlock_01A", "JunglePillarBlock_01C")):
                    props.append({"label": f"Pil_{name}{i}{lv}", "mesh": R + mesh, "x": px, "y": py, "z": 2.5 * lv, "yaw": 90.0 * ((i + lv) % 4)})
                props[-3]["col"] = {"size": [2.6, 2.6, WALL_H]}
            torch(props, lights, f"{name}{i}", tx, ty, yaw_in, 0.75)

    # 우두머리 방: 가운데 원형 문양 바닥(지름 14m), 둘레 네 곳에 받침대 + 불빛(그림자), 잔해
    bx, by = (ROOMS["boss"][0] + ROOMS["boss"][1]) / 2, (ROOMS["boss"][2] + ROOMS["boss"][3]) / 2
    props.append({"label": "BossCircle", "mesh": R + "Ruins_WhiteBuffFloor", "x": bx, "y": by, "z": -0.22})
    for i, (dx, dy) in enumerate(((-9.5, -9.5), (-9.5, 9.5), (9.5, -9.5), (9.5, 9.5))):
        props.append({"label": f"BossBase{i}", "mesh": R + "Jungle_DiamondBase_DecoB", "x": bx + dx, "y": by + dy, "z": 0.0,
                      "col": {"size": [2.3, 2.3, 1.2]}})
        props.append({"label": f"BossBrazier{i}", "mesh": P + "Brazier/SM_Brazier", "x": bx + dx, "y": by + dy, "z": 0.75, "height_m": 1.3})
        lights.append({"label": f"Brazier_boss{i}", "x": bx + dx, "y": by + dy, "z": 2.5, "intensity": 60, "radius_m": 18, "color": [255, 170, 100], "shadows": True, "fx": FIRE_FX})
    props.append({"label": "BossRubble0", "mesh": R + "JungleRubblePile_A", "x": bx + 11.5, "y": by - 3.0, "z": -0.15, "yaw": 40, "col": {"size": [4.2, 3.6, 1.4], "yaw": 0}})
    props.append({"label": "BossAlcove", "mesh": R + "Aclove_Wall_Broken", "x": bx + 12.6, "y": by + 7.0, "z": 0.0, "yaw": 200, "col": {"size": [3.2, 3.2, 2.4]}})

    # 방1·방2: 잔해·부서진 기둥·계단 조각·상자(모두 충돌, 스폰 마커가 놓이는 가운데 ±6m 밖)
    r1x, r1y = -14.0, -26.0
    props += [
        {"label": "R1_Rubble", "mesh": R + "JungleRubblePile_A", "x": r1x + 7.6, "y": r1y - 7.2, "z": -0.15, "yaw": 110, "col": {"size": [4.2, 3.6, 1.4]}},
        {"label": "R1_Crumble", "mesh": R + "JunglePillarBlockCrumble01_A", "x": r1x - 7.8, "y": r1y + 7.6, "z": 0.0, "yaw": 25, "col": {"size": [2.4, 2.4, 2.4]}},
        {"label": "R1_Crates", "mesh": P + "CratesBarrels/SM_CratesBarrels", "x": r1x - 8.0, "y": r1y - 8.0, "z": 0.0, "width_m": 2.4, "yaw": 30, "col": {"size": [2.2, 1.8, 1.3]}},
    ]
    r2x, r2y = 6.0, 26.0
    props += [
        {"label": "R2_Rubble", "mesh": R + "JungleRubblePile_A", "x": r2x - 7.4, "y": r2y - 7.4, "z": -0.15, "yaw": 250, "col": {"size": [4.2, 3.6, 1.4]}},
        {"label": "R2_Alcove", "mesh": R + "Aclove_Wall_Broken", "x": r2x + 7.6, "y": r2y - 7.6, "z": 0.0, "yaw": 135, "col": {"size": [3.2, 3.2, 2.4]}},
        {"label": "R2_Crumble", "mesh": R + "JunglePillarBlockCrumble01_A", "x": r2x + 7.9, "y": r2y + 7.8, "z": 0.0, "yaw": 70, "col": {"size": [2.4, 2.4, 2.4]}},
    ]
    # 시작 방: 모닥불(충돌 낮음 → 윗면 1m 상자) + 상자
    sx, sy = -40.0, -26.0
    props += [
        {"label": "S_Campfire", "mesh": P + "Campfire/SM_Campfire", "x": sx + 2.0, "y": sy + 4.5, "z": 0.0, "width_m": 1.6, "col": {"size": [1.4, 1.4, 1.1], "shape": "Cylinder"}},
        {"label": "S_Crates", "mesh": P + "CratesBarrels/SM_CratesBarrels", "x": sx - 1.5, "y": sy - 5.6, "z": 0.0, "width_m": 2.4, "yaw": 200, "col": {"size": [2.2, 1.8, 1.3]}},
    ]

    def brazier(label, x, y):
        props.append({"label": label, "mesh": P + "Brazier/SM_Brazier", "x": x, "y": y, "z": 0.0, "height_m": 1.3, "col": {"size": [1.1, 1.1, 1.3], "shape": "Cylinder"}})
        lights.append({"label": "Fire_" + label, "x": x, "y": y, "z": 1.75, "intensity": 50, "radius_m": 16, "color": [255, 170, 100], "shadows": True, "fx": FIRE_FX})

    def statue(label, x, y, face):
        props.append({"label": label, "mesh": P + "GuardianStatue/SM_GuardianStatue", "x": x, "y": y, "z": 0.0, "height_m": 4.6, "yaw": face - STATUE_FRONT,
                      "col": {"size": [1.7, 1.7, 4.0]}})

    def sarcophagus(label, x, y, yaw):
        props.append({"label": label, "mesh": P + "Sarcophagus/SM_Sarcophagus", "x": x, "y": y, "z": 0.0, "width_m": 2.5, "yaw": yaw, "col": {"size": [1.15, 2.5, 1.05]}})

    def bones(label, x, y, yaw):
        props.append({"label": label, "mesh": P + "BonePile/SM_BonePile", "x": x, "y": y, "z": -0.05, "width_m": 1.9, "yaw": yaw, "col": {"size": [1.5, 1.5, 1.1], "shape": "Cylinder"}})

    def rack(label, x, y, face):
        props.append({"label": label, "mesh": P + "WeaponRack/SM_WeaponRack", "x": x, "y": y, "z": 0.0, "width_m": 1.9, "yaw": face - RACK_FRONT, "col": {"size": [0.9, 1.9, 1.5], "yaw": RACK_FRONT - 90.0}})

    brazier("R1_Brazier", r1x + 7.5, r1y + 7.5)
    brazier("R2_Brazier", r2x - 7.4, r2y + 7.5)
    statue("S_StatueL", sx + 5.4, sy - 5.2, 180.0)          # 시작 방 북쪽 문 양옆, 들어오는 사람을 마주 봄
    statue("S_StatueR", sx + 5.4, sy + 5.2, 180.0)
    statue("BossStatueL", bx - 13.3, by - 8.0, 0.0)         # 우두머리 방 남쪽 벽, 방 안을 봄
    statue("BossStatueR", bx - 13.3, by + 8.0, 0.0)
    sarcophagus("BossSarcA", bx - 5.0, by - 13.3, 90.0)     # 우두머리 방 서쪽 벽을 따라
    sarcophagus("BossSarcB", bx + 5.0, by - 13.3, 90.0)
    sarcophagus("R2_Sarc", r2x + 0.5, r2y + 8.6, 90.0)
    bones("R1_Bones", r1x + 2.0, r1y + 8.6, 20.0)
    bones("BossBones", bx - 11.0, by + 11.5, 140.0)
    bones("A_Bones", -11.6, 4.0, 250.0)                     # 통로 A 동쪽 벽 옆
    rack("S_Rack", sx - 5.9, sy + 5.3, 0.0)
    rack("R1_Rack", r1x + 9.0, r1y - 2.5, 180.0)

    lights.append({"label": "Fire_start", "x": sx + 2.0, "y": sy + 4.5, "z": 1.0, "intensity": 45, "radius_m": 14, "color": [255, 160, 90], "shadows": True, "fx": FIRE_FX})
    return surfaces, runs, props, lights


def main():
    surfaces, runs, props, lights = build()
    d = {
        "_comment": "던전 레벨 Continent01_Dungeon 정의(DG4). .claude/skills/world-build/scripts/dg4_layout.py가 만든 파일 — 구도는 그 스크립트를 고쳐 다시 만든다. 단위 m, x = 북(+), y = 동(+). Landscape 없음(바닥 윗면 높이 0). 구성: 시작 방(출구 포털) → 방1 → 통로(동쪽으로 갔다 북쪽으로 꺾임) → 방2 → 통로(북쪽으로 갔다 서쪽으로 꺾임) → 우두머리 방. 조립은 world_step dg_build(jm_kit.dungeon), 마커는 world_place_markers, 조명은 world_step lighting. 서버는 구역 이름 boss의 스폰을 우두머리로 본다(SpawnTable).",
        "level": "/Game/Maps/Continent01_Dungeon",
        "size": 128, "spacing_cm": 100.0, "z_scale": 100.0,
        "rolling": [], "border": {"inner_m": 500.0, "width_m": 10.0, "height_m": 0.0}, "flats": [], "mounds": [],
        "blockout": [],
        "rooms": {k: list(v) for k, v in ROOMS.items()},
        "corridors": {k: list(v) for k, v in CORRIDORS.items()},
        "lighting": {
            "_comment": "어두운 실내(지붕 없음 — 밤하늘). 달빛 같은 약한 방향광 + 낮은 하늘빛 + 안개 + 고정 노출. 밝기는 dungeon.lights의 횃불·화로가 맡는다.",
            "actors": [
                {"label": "Env_Sun", "class": "DirectionalLight", "component": "DirectionalLightComponent", "movable": True, "location_m": [0, 0, 30],
                 "rotation": {"pitch": -55.0, "yaw": -30.0},
                 "props": {"intensity": 0.25, "light_color": {"color": [170, 195, 255]}, "atmosphere_sun_light": True, "cast_shadows": False}},
                {"label": "Env_SkyLight", "class": "SkyLight", "component": "SkyLightComponent", "movable": True, "location_m": [0, 0, 30],
                 "props": {"real_time_capture": True, "intensity": 1.6}},
                {"label": "Env_Atmosphere", "class": "SkyAtmosphere", "component": "SkyAtmosphereComponent", "props": {}},
                {"label": "Env_Fog", "class": "ExponentialHeightFog", "component": "ExponentialHeightFogComponent", "location_m": [0, 0, 0],
                 "props": {"fog_density": 0.03, "fog_height_falloff": 0.2, "start_distance": 300.0, "fog_max_opacity": 0.9}},
                {"label": "Env_PostProcess", "class": "PostProcessVolume", "actor_props": {"unbound": True},
                 "settings": {"auto_exposure_min_brightness": -0.5, "auto_exposure_max_brightness": -0.5, "auto_exposure_bias": 0.0, "bloom_intensity": 0.5, "vignette_intensity": 0.35}},
            ],
        },
        "dungeon": {"surfaces": surfaces, "runs": runs, "props": props, "lights": lights},
        "markers": {
            "player_start": {"x": -40.0, "y": -26.0, "yaw": 0, "area": "dungeon"},
            "portals": [{"id": 900, "label": "Exit", "area": "dungeon", "x": -45.0, "y": -26.0, "radius_cm": 200, "target_map": "Continent01", "target_point": 100}],
            "monster": [
                {"zone": "room1", "area": "dungeon", "x": -14.0, "y": -26.0, "half_x": 5, "half_y": 5, "count": 2, "min_gap_m": 7.0, "seed": 41, "template_id": 1004, "radius_cm": 250, "max_count": 2},
                {"zone": "room2", "area": "dungeon", "x": 6.0, "y": 26.0, "half_x": 5, "half_y": 5, "count": 3, "min_gap_m": 5.5, "seed": 42, "template_id": 1004, "radius_cm": 250, "max_count": 2},
                {"zone": "boss", "area": "dungeon", "x": 33.0, "y": -24.0, "half_x": 0.5, "half_y": 0.5, "count": 1, "min_gap_m": 1.0, "seed": 43, "template_id": 1005, "radius_cm": 200, "max_count": 1},
            ],
        },
    }
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=1)
    print("walls", sum(1 for s in surfaces if s["label"].startswith("Wall_")), "runs", len(runs), "props", len(props), "lights", len(lights))


main()
