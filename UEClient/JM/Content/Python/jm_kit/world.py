"""맵 제작 도구(L2, Docs/DESIGN_WORLD.md): 지형 정의(JSON) → 높이맵(.r16) → Landscape.

지형은 손으로 조각하지 않고 식으로 만든다(평지·완만한 기복·둔덕·가장자리 능선).
Landscape 생성은 C++ ULandscapeBuildLibrary(JMEditorTools)가 하고 여기서는 높이 계산과 레벨 준비만 한다.
좌표: 정의 파일은 m 단위, x = UE X(북), y = UE Y(동). 높이는 cm로 바꿔 uint16(32768 = 0)에 담는다.
"""

import array
import json
import math
import os
import random

import unreal

_DIR = os.path.dirname(__file__)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAL = unreal.EditorAssetLibrary


def load_def(name):
    """지형 정의 world_<name>.json."""
    with open(os.path.join(_DIR, f"world_{name}.json"), encoding="utf-8") as f:
        return json.load(f)


def _smooth(t):
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def height_fn(d):
    """정의 → h(x, y) (m)."""
    rolls = []
    for r in d.get("rolling", []):
        a = math.radians(r["dir_deg"])
        k = 2.0 * math.pi / r["wave_m"]
        rolls.append((r["amp_m"], k * math.cos(a), k * math.sin(a), r.get("phase", 0.0)))
    flats = d.get("flats", [])
    mounds = d.get("mounds", [])
    b = d["border"]
    wob_k = 2.0 * math.pi / b.get("wobble_wave_m", 50.0)

    def h(x, y):
        v = 0.0
        for amp, kx, ky, ph in rolls:
            v += amp * math.sin(kx * x + ky * y + ph)
        for m in mounds:
            dist = math.hypot(x - m["x"], y - m["y"])
            if dist < m["r"]:
                v += m["h"] * _smooth(1.0 - dist / m["r"])
        # 평지: 안쪽은 고정 높이, blend 폭에 걸쳐 주변 지형으로 이어진다
        for f in flats:
            dist = math.hypot(x - f["x"], y - f["y"])
            if dist < f["r"] + f["blend"]:
                w = 1.0 - _smooth((dist - f["r"]) / f["blend"])
                v = v * (1.0 - w) + f["h"] * w
        # 가장자리 능선: 네모 경계에 굴곡을 줘서 직선으로 보이지 않게 한다
        edge = max(abs(x), abs(y)) + b.get("wobble_m", 0.0) * math.sin(wob_k * (x if abs(y) > abs(x) else y))
        v += b["height_m"] * _smooth((edge - b["inner_m"]) / b["width_m"])
        return v

    return h


def _height_grid(d):
    """정의 → 높이 격자(m). grid[iy][ix], 행 = Y, 열 = X."""
    n = int(d["size"])
    sp = d["spacing_cm"] / 100.0
    half = (n - 1) * 0.5 * sp
    h = height_fn(d)
    return [[h(-half + ix * sp, -half + iy * sp) for ix in range(n)] for iy in range(n)]


def build_heights(d, out_path, grid=None):
    """높이맵을 만들어 .r16으로 저장하고 통계를 돌려준다."""
    n = int(d["size"])
    sp = d["spacing_cm"] / 100.0
    zs = d["z_scale"]
    half = (n - 1) * 0.5 * sp

    if grid is None:
        grid = _height_grid(d)
    data = array.array("H", bytes(2 * n * n))
    lo, hi = 1e9, -1e9
    i = 0
    for row in grid:
        for v in row:
            lo = min(lo, v)
            hi = max(hi, v)
            q = int(round(32768 + v * 100.0 * 128.0 / zs))
            data[i] = 0 if q < 0 else (65535 if q > 65535 else q)
            i += 1
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        data.tofile(f)

    # 경사 통계: 걷는 구역(능선 안쪽)의 최대 경사, 전체에서 30°~60°(만들지 않기로 한 구간)의 비율
    inner = d["border"]["inner_m"] - d["border"].get("wobble_m", 0.0)
    walk_max = 0.0
    mid = total = 0
    for iy in range(1, n - 1):
        y = -half + iy * sp
        for ix in range(1, n - 1):
            x = -half + ix * sp
            gx = (grid[iy][ix + 1] - grid[iy][ix - 1]) / (2 * sp)
            gy = (grid[iy + 1][ix] - grid[iy - 1][ix]) / (2 * sp)
            deg = math.degrees(math.atan(math.hypot(gx, gy)))
            total += 1
            if 30.0 < deg < 60.0:
                mid += 1
            if max(abs(x), abs(y)) < inner and deg > walk_max:
                walk_max = deg
    return {
        "file": out_path, "size": n, "side_m": round((n - 1) * sp, 1),
        "height_min_m": round(lo, 2), "height_max_m": round(hi, 2),
        "walk_max_slope_deg": round(walk_max, 1),
        "slope_30_60_percent": round(100.0 * mid / max(total, 1), 2),
    }


def new_level(level_path):
    """빈 레벨(World Partition 아님)을 만들고 연다. 이미 있으면 열기만 한다."""
    if EAL.does_asset_exist(level_path):
        LES.load_level(level_path)
        return f"이미 있어서 열기만 함: {level_path}"
    if not LES.new_level(level_path, False):
        raise RuntimeError(f"레벨 생성 실패: {level_path}")
    return f"새 레벨: {level_path}"


def basic_lighting():
    """야외 주간 기본 조명(없는 것만 추가): 태양·하늘빛·대기·안개. 값은 정의의 lighting을 apply_lighting(world_step "lighting")이 넣는다."""
    have = {a.get_actor_label() for a in EAS.get_all_level_actors()}
    added = []

    def spawn(label, cls, loc=(0, 0, 0), rot=None):
        if label in have:
            return None
        a = EAS.spawn_actor_from_class(cls, unreal.Vector(*loc), rot or unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        added.append(label)
        return a

    sun = spawn("Env_Sun", unreal.DirectionalLight, (0, 0, 3000), unreal.Rotator(0, -50, -40))
    if sun:
        sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sky = spawn("Env_SkyLight", unreal.SkyLight, (0, 0, 3000))
    if sky:
        sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky.light_component.set_editor_property("real_time_capture", True)
    spawn("Env_Atmosphere", unreal.SkyAtmosphere)
    spawn("Env_Fog", unreal.ExponentialHeightFog)
    return added


def _saved_path(name, ext):
    return os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "World", f"{name}.{ext}")


def build_landscape(name, material_path=None, replace=True):
    """정의 world_<name>.json대로 높이맵을 만들고 지금 열린 레벨에 Landscape를 만든다(교체 가능).
    정의에 ground가 있으면 재질 레이어 가중치도 함께 넣고, 재질을 안 주면 ground의 지형 재질(있을 때)을 쓴다."""
    d = load_def(name)
    grid = _height_grid(d)
    out = _saved_path(name, "r16")
    stats = build_heights(d, out, grid)
    g = d.get("ground")
    if g and not material_path and EAL.does_asset_exist(_ground_material_path(g)):
        material_path = _ground_material_path(g)
    mat = unreal.load_asset(material_path) if material_path else None
    if material_path and mat is None:
        raise RuntimeError(f"재질을 찾지 못함: {material_path}")
    lib = unreal.LandscapeBuildLibrary
    if g:
        wout = _saved_path(name, "w8")
        stats["layers"] = build_weights(d, wout, grid)["coverage_percent"]
        layer_dir = g["asset_dir"] + "/Layers"
        EAL.make_directory(layer_dir)
        stats["result"] = lib.create_landscape_layered(out, int(d["size"]), float(d["spacing_cm"]), float(d["z_scale"]), mat, bool(replace),
                                                       [L["name"] for L in g["layers"]], wout, layer_dir)
        EAL.save_directory(layer_dir, True, False)
    else:
        stats["result"] = lib.create_landscape(out, int(d["size"]), float(d["spacing_cm"]), float(d["z_scale"]), mat, bool(replace))
    return stats


def height_at(name, points):
    """정의 기준 지면 높이(cm). points = [[x_cm, y_cm], ...] — 마커·소품을 지면에 놓을 때 쓴다."""
    h = height_fn(load_def(name))
    return [round(h(p[0] / 100.0, p[1] / 100.0) * 100.0, 1) for p in points]


# ── 블록아웃(L2b): 기본 도형으로 구역·소품 자리를 잡는다. 라벨 접두사 BO_, 아웃라이너 폴더 Blockout ──
_SHAPES = "/Engine/BasicShapes/"
_BO = "BO_"


def _spawn_shape(d, h, shape, label, x, y, size, yaw=0.0, sink=0.0):
    """기본 도형 하나. size = [x, y, z] m, 바닥이 지면(- sink)에 닿게 놓는다."""
    z = h(x, y) + size[2] * 0.5 - sink
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x * 100.0, y * 100.0, z * 100.0), unreal.Rotator(0, 0, yaw))
    a.static_mesh_component.set_static_mesh(unreal.load_asset(_SHAPES + shape))
    a.set_actor_scale3d(unreal.Vector(size[0], size[1], size[2]))
    a.set_actor_label(_BO + label)
    a.set_folder_path("Blockout")
    return a


def _in_flat(d, x, y, margin=2.0):
    return any(math.hypot(x - f["x"], y - f["y"]) < f["r"] + margin for f in d.get("flats", []))


def _scatter_points(d, item, taken=None):
    """네모 구역 안에 최소 간격을 지키는 점들(시드 고정). 평지(마을·던전 입구) 안은 피한다.
    item["flat_margin_m"]를 주면 평지 가장자리에서 그만큼 더 떨어진다(몬스터 스폰이 마을 벽·포털 앞에 놓이지 않게)."""
    rnd = random.Random(item.get("seed", 1))
    margin = item.get("flat_margin_m", 2.0)
    pts = []
    tries = 0
    while len(pts) < item["count"] and tries < item["count"] * 400:
        tries += 1
        x = item["x"] + rnd.uniform(-item["half_x"], item["half_x"])
        y = item["y"] + rnd.uniform(-item["half_y"], item["half_y"])
        if max(abs(x), abs(y)) > d["border"]["inner_m"] - d["border"].get("wobble_m", 0.0) - 4.0:
            continue
        if _in_flat(d, x, y, margin):
            continue
        if any(math.hypot(x - px, y - py) < item["min_gap_m"] for px, py in pts):
            continue
        if taken and any(math.hypot(x - px, y - py) < 4.0 for px, py in taken):
            continue
        pts.append((x, y))
    return pts, rnd


def clear_blockout():
    """BO_ 라벨 액터를 모두 지운다."""
    n = 0
    for a in EAS.get_all_level_actors():
        if a.get_actor_label().startswith(_BO):
            EAS.destroy_actor(a)
            n += 1
    return n


def blockout(name):
    """정의의 blockout 목록대로 도형을 놓는다(기존 BO_ 액터는 지우고 다시)."""
    d = load_def(name)
    h = height_fn(d)
    removed = clear_blockout()
    counts = {}
    # dress(L2c)가 메시로 바꾼 항목은 다시 놓지 않는다(충돌 도형이 NA_*_Col로 이미 있음)
    dressed = {it.get("from") for it in d.get("dress", {}).get("items", [])}
    for item in d.get("blockout", []):
        kind, label = item["kind"], item["label"]
        if label in dressed:
            continue
        n = 0
        if kind == "box":
            # sink(m): 그만큼 지면 아래로 내린다 — 바닥 상자는 두께만큼 내려 윗면을 지면 높이에 맞춘다(Landscape 없는 레벨, DG3c)
            _spawn_shape(d, h, item["shape"], label, item["x"], item["y"], item["size"], item.get("yaw", 0.0), sink=item.get("sink", 0.0))
            n = 1
        elif kind == "ring":
            rnd = random.Random(item.get("seed", 1))
            r = item["r"]
            steps = max(3, int(round(2.0 * math.pi * r / item["step_m"])))
            jit = item.get("jitter", 0.0)
            for i in range(steps):
                deg = 360.0 * i / steps
                skip = False
                for g in item.get("gaps", []):
                    half = math.degrees(g["width_m"] * 0.5 / r)
                    if abs((deg - g["deg"] + 180.0) % 360.0 - 180.0) < half:
                        skip = True
                if skip:
                    continue
                rr = r * (1.0 + rnd.uniform(-jit, jit) * 0.3)
                x = item["x"] + rr * math.cos(math.radians(deg))
                y = item["y"] + rr * math.sin(math.radians(deg))
                sz = [v * (1.0 + rnd.uniform(-jit, jit)) for v in item["size"]]
                yaw = deg + (rnd.uniform(-40, 40) if jit else 0.0)
                _spawn_shape(d, h, item["shape"], f"{label}_{n:02d}", x, y, sz, yaw, sink=0.5 if jit else 0.0)
                n += 1
        elif kind == "scatter":
            pts, rnd = _scatter_points(d, item)
            for i, (x, y) in enumerate(pts):
                if item["shape"] == "Tree":
                    th = rnd.uniform(7.0, 10.0)
                    _spawn_shape(d, h, "Cylinder", f"{label}_{i:02d}", x, y, [0.6, 0.6, th * 0.5])
                    crown = _spawn_shape(d, h, "Cone", f"{label}_{i:02d}_Top", x, y, [4.5, 4.5, th * 0.7])
                    crown.set_actor_location(crown.get_actor_location() + unreal.Vector(0, 0, th * 35.0), False, False)
                    crown.static_mesh_component.set_collision_profile_name("NoCollision")
                else:
                    lo, hi = item.get("size", [3.0, 5.0])
                    s = rnd.uniform(lo, hi)
                    _spawn_shape(d, h, "Cube", f"{label}_{i:02d}", x, y, [s, s * rnd.uniform(0.7, 1.2), s * rnd.uniform(0.5, 0.9)], rnd.uniform(0, 90), sink=0.4)
                n += 1
        elif kind == "arch":
            yaw = item["yaw"]
            w, hh, t = item["width_m"], item["height_m"], item["thick_m"]
            sx, sy = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
            for side, tag in ((-1, "L"), (1, "R")):
                _spawn_shape(d, h, "Cube", f"{label}_{tag}", item["x"] + side * sx * (w + t) * 0.5, item["y"] + side * sy * (w + t) * 0.5, [t, t, hh], yaw)
            top = _spawn_shape(d, h, "Cube", f"{label}_Top", item["x"], item["y"], [t, w + 2 * t, t], yaw)
            top.set_actor_location(top.get_actor_location() + unreal.Vector(0, 0, hh * 100.0), False, False)
            n = 3
        else:
            raise ValueError(f"모르는 blockout kind: {kind}")
        counts[label] = n
    return {"removed": removed, "placed": counts}


def place_markers(name):
    """정의의 markers대로 ALevelMarker(서버 스폰 원본)와 PIE용 PlayerStart를 놓는다(기존 마커는 지우고 다시)."""
    d = load_def(name)
    h = height_fn(d)
    m = d["markers"]
    lib = unreal.LandscapeBuildLibrary
    removed = lib.clear_level_markers()
    for a in EAS.get_all_level_actors():
        if isinstance(a, unreal.PlayerStart):
            EAS.destroy_actor(a)
    out = []

    def loc(x, y, up_cm):
        return unreal.Vector(x * 100.0, y * 100.0, h(x, y) * 100.0 + up_cm)

    ps = m["player_start"]
    out.append(lib.spawn_level_marker("PlayerStart", 0, loc(ps["x"], ps["y"], 100.0), ps.get("yaw", 0.0), 0, 0.0, 0, "", "Marker_PlayerStart", "", ps.get("area", "")))
    pie = EAS.spawn_actor_from_class(unreal.PlayerStart, loc(ps["x"], ps["y"], 110.0), unreal.Rotator(0, 0, ps.get("yaw", 0.0)))
    pie.set_actor_label("PlayerStart")
    # player_starts(DG3b): {"id", "label", "x", "y", "yaw", "area"}
    for extra in m.get("player_starts", []):
        out.append(lib.spawn_level_marker("PlayerStart", int(extra["id"]), loc(extra["x"], extra["y"], 100.0), float(extra.get("yaw", 0.0)), 0, 0.0, 0, "", f"Marker_PlayerStart_{extra['label']}", "", extra.get("area", "")))
    pid = 1
    taken = []
    for grp in m.get("monster", []):
        pts, _ = _scatter_points(d, grp, taken)
        for (x, y) in pts:
            out.append(lib.spawn_level_marker("MonsterSpawn", pid, loc(x, y, 100.0), 0.0, grp["template_id"], float(grp["radius_cm"]), grp["max_count"], "", f"Marker_Monster_{grp['zone']}_{pid:02d}", grp["zone"], grp.get("area", "")))
            pid += 1
        taken += pts
    po = m.get("portal")
    if po:
        out.append(lib.spawn_level_marker("PortalTrigger", 100, loc(po["x"], po["y"], 100.0), 0.0, po.get("target_point", 0), float(po["radius_cm"]), 0, po["target_map"], "Marker_Portal_Dungeon", "", po.get("area", "")))
    # 포털 여러 개: {"id", "label", "x", "y", "radius_cm", "target_map", "target_point"} — 같은 레벨 안의 짝 포털은 target_map이 이 레벨, target_point가 짝의 id
    for po in m.get("portals", []):
        out.append(lib.spawn_level_marker("PortalTrigger", int(po["id"]), loc(po["x"], po["y"], 100.0), 0.0, int(po.get("target_point", 0)), float(po["radius_cm"]), 0, po["target_map"], f"Marker_Portal_{po['label']}", "", po.get("area", "")))
    # safe_zones(L3): {"id", "label", "x", "y", "radius_cm"}
    for sz in m.get("safe_zones", []):
        out.append(lib.spawn_level_marker("SafeZone", int(sz["id"]), loc(sz["x"], sz["y"], 100.0), 0.0, 0, float(sz["radius_cm"]), 0, "", f"Marker_SafeZone_{sz['label']}", "", sz.get("area", "")))
    # npcs(L4): {"id", "label", "x", "y", "yaw", "template_id"} — 종류(NpcTable ID)만 적는다. 이름·상점·외형은 서버 NpcTable.csv (D-202)
    for npc in m.get("npcs", []):
        out.append(lib.spawn_level_marker("Npc", int(npc["id"]), loc(npc["x"], npc["y"], 100.0), float(npc.get("yaw", 0.0)), int(npc["template_id"]), 0.0, 0, "", f"Marker_Npc_{npc['label']}", "", npc.get("area", "")))
    errors = [o for o in out if o.startswith("ERROR")]
    return {"removed": removed, "placed": len(out) - len(errors), "errors": errors}


def set_game_mode(game_mode_bp):
    """지금 열린 레벨의 GameMode Override. 맵 제작 중에는 서버 없이 걷는 BP_TestGameMode를 쓴다(L2e에서 실제 GameMode로)."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ws = world.get_world_settings()
    cls = unreal.load_asset(game_mode_bp).generated_class() if game_mode_bp else None
    ws.set_editor_property("default_game_mode", cls)
    return str(cls.get_name() if cls else None)


# ── 지면(L2c): 정의의 ground → 재질 레이어 가중치(.w8) + 지형 재질 + Landscape Grass Type ──
# 레이어는 맵(분위기) 단위다. kind: zone(정의 zones의 네모 — 서로 부드럽게 나뉨) → 그 위에 목록 순서대로 pad(원)·road(길 선)·slope(경사)를 덮는다.
MEL = unreal.MaterialEditingLibrary


def _ground_material_path(g):
    return f"{g['asset_dir']}/{g['material']}"


def _value_noise(seed):
    """값 잡음(격자 해시 + 부드러운 보간). 결과 -1 ~ 1, 격자 한 칸 = 입력 1."""
    def hsh(ix, iy):
        v = (ix * 374761393 + iy * 668265263 + seed * 144665) & 0xFFFFFFFF
        v = ((v ^ (v >> 13)) * 1274126177) & 0xFFFFFFFF
        return ((v ^ (v >> 16)) & 0xFFFF) / 32767.5 - 1.0

    def f(x, y):
        ix, iy = math.floor(x), math.floor(y)
        fx, fy = x - ix, y - iy
        sx, sy = fx * fx * (3.0 - 2.0 * fx), fy * fy * (3.0 - 2.0 * fy)
        a, b = hsh(ix, iy), hsh(ix + 1, iy)
        c, e = hsh(ix, iy + 1), hsh(ix + 1, iy + 1)
        return (a + (b - a) * sx) * (1.0 - sy) + (c + (e - c) * sx) * sy

    return f


def _rect_depth(z, x, y):
    """네모 구역 안쪽 깊이(m). 안 = 양수(가장 가까운 변까지), 밖 = 음수."""
    dx = z["half_x"] - abs(x - z["x"])
    dy = z["half_y"] - abs(y - z["y"])
    if dx >= 0.0 and dy >= 0.0:
        return min(dx, dy)
    return -math.hypot(min(dx, 0.0), min(dy, 0.0))


def _seg_dist(px, py, s):
    ax, ay, bx, by = s[0], s[1], s[2], s[3]
    vx, vy = bx - ax, by - ay
    ll = vx * vx + vy * vy
    t = 0.0 if ll <= 0.0 else max(0.0, min(1.0, ((px - ax) * vx + (py - ay) * vy) / ll))
    return math.hypot(px - (ax + vx * t), py - (ay + vy * t))


def build_weights(d, out_path, grid=None):
    """ground.layers대로 정점마다 레이어 가중치(합 255)를 계산해 .w8로 저장한다(레이어 순서대로 size²바이트씩).
    반환: 파일, 레이어 이름, 레이어별 넓이 비율(%)."""
    g = d["ground"]
    layers = g["layers"]
    n = int(d["size"])
    sp = d["spacing_cm"] / 100.0
    half = (n - 1) * 0.5 * sp
    if grid is None:
        grid = _height_grid(d)
    zones = {z["name"]: z for z in d.get("zones", [])}
    zone_layers = [(i, zones[L["zone"]]) for i, L in enumerate(layers) if L["kind"] == "zone"]
    if not zone_layers:
        raise ValueError("ground.layers에 kind = zone인 레이어가 하나는 있어야 함")
    blend = float(g.get("biome_blend_m", 5.0))
    warp = float(g.get("warp_m", 6.0))
    wave = float(g.get("warp_wave_m", 35.0))
    edge = float(g.get("edge_noise_m", 0.8))
    noise_x, noise_y, noise_d = _value_noise(11), _value_noise(29), _value_noise(47)

    # 덮는 레이어 준비: (레이어 번호, 종류, 자료)
    overlays = []
    for i, L in enumerate(layers):
        kind = L["kind"]
        if kind == "zone":
            continue
        if kind == "pad":
            overlays.append((i, "pad", L["pads"]))
        elif kind == "road":
            reach = L["half_m"] + L["blend_m"] + edge + 1.0
            segs = []
            lines = [r["points"] for r in d.get("roads", [])] + L.get("links", [])
            for pts in lines:
                for a, b in zip(pts, pts[1:]):
                    segs.append((a[0], a[1], b[0], b[1], min(a[0], b[0]) - reach, max(a[0], b[0]) + reach, min(a[1], b[1]) - reach, max(a[1], b[1]) + reach))
            overlays.append((i, "road", (segs, L["half_m"], L["blend_m"], L.get("pads", []))))
        elif kind == "slope":
            overlays.append((i, "slope", (float(L["deg"][0]), float(L["deg"][1]))))
        else:
            raise ValueError(f"모르는 ground 레이어 kind: {kind}")

    count = len(layers)
    bufs = [bytearray(n * n) for _ in range(count)]
    sums = [0] * count
    for iy in range(n):
        y = -half + iy * sp
        row = grid[iy]
        up = grid[min(iy + 1, n - 1)]
        dn = grid[max(iy - 1, 0)]
        base = iy * n
        for ix in range(n):
            x = -half + ix * sp
            # 구역 경계를 잡음으로 흔들어 직선으로 보이지 않게 한다
            wx = x + warp * noise_x(x / wave, y / wave)
            wy = y + warp * noise_y(x / wave, y / wave)
            w = [0.0] * count
            deps = [(i, _rect_depth(z, wx, wy)) for i, z in zone_layers]
            top = max(v for _, v in deps)
            tot = 0.0
            for i, v in deps:
                e = math.exp((v - top) / blend)
                w[i] = e
                tot += e
            for i, _ in deps:
                w[i] /= tot

            jitter = edge * noise_d(x / 3.0, y / 3.0)
            for i, kind, data in overlays:
                a = 0.0
                if kind == "pad":
                    for p in data:
                        dist = math.hypot(wx * 0.3 + x * 0.7 - p["x"], wy * 0.3 + y * 0.7 - p["y"])
                        a = max(a, 1.0 - _smooth((dist - p["r"]) / p["blend"]))
                elif kind == "road":
                    segs, hw, bl, pads = data
                    best = 1e9
                    for s in segs:
                        if s[4] <= x <= s[5] and s[6] <= y <= s[7]:
                            best = min(best, _seg_dist(x, y, s))
                    if best < 1e8:
                        a = 1.0 - _smooth((best + jitter - hw) / bl)
                    for p in pads:
                        dist = math.hypot(x - p["x"], y - p["y"]) + jitter
                        a = max(a, 1.0 - _smooth((dist - p["r"]) / p["blend"]))
                else:
                    gx = (row[min(ix + 1, n - 1)] - row[max(ix - 1, 0)]) / (2.0 * sp)
                    gy = (up[ix] - dn[ix]) / (2.0 * sp)
                    deg = math.degrees(math.atan(math.hypot(gx, gy)))
                    a = _smooth((deg + jitter * 3.0 - data[0]) / (data[1] - data[0]))
                if a > 0.0:
                    keep = 1.0 - a
                    for j in range(count):
                        w[j] *= keep
                    w[i] += a

            # 합 255로 맞춤(남는 값은 가장 큰 레이어에)
            tot = sum(w)
            q = [int(v * 255.0 / tot) for v in w]
            big = max(range(count), key=lambda j: w[j])
            q[big] += 255 - sum(q)
            for j in range(count):
                if q[j]:
                    bufs[j][base + ix] = q[j]
                    sums[j] += q[j]

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        for b in bufs:
            f.write(b)
    total = 255.0 * n * n
    return {"file": out_path, "layers": [L["name"] for L in layers],
            "coverage_percent": {L["name"]: round(100.0 * sums[i] / total, 2) for i, L in enumerate(layers)}}


def _sampler_type(tex):
    ST = unreal.MaterialSamplerType
    TC = unreal.TextureCompressionSettings
    cs = tex.get_editor_property("compression_settings")
    srgb = tex.get_editor_property("srgb")
    if cs == TC.TC_NORMALMAP:
        return ST.SAMPLERTYPE_NORMAL
    if cs == TC.TC_MASKS:
        return ST.SAMPLERTYPE_MASKS
    if cs in (TC.TC_GRAYSCALE, TC.TC_ALPHA):
        return ST.SAMPLERTYPE_GRAYSCALE if srgb else ST.SAMPLERTYPE_LINEAR_GRAYSCALE
    return ST.SAMPLERTYPE_COLOR if srgb else ST.SAMPLERTYPE_LINEAR_COLOR


def build_grass_types(name):
    """ground.grass대로 Landscape Grass Type 에셋(<asset_dir>/Grass/LGT_<이름>)을 만들거나 고친다.
    밀도 단위 = 10m × 10m당 개수. 설정하지 못한 속성은 notes에 남긴다."""
    g = load_def(name)["ground"]
    gdir = g["asset_dir"] + "/Grass"
    EAL.make_directory(gdir)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    notes, out = [], []

    def put(obj, key, value):
        try:
            obj.set_editor_property(key, value)
        except Exception as ex:
            notes.append(f"{type(obj).__name__}.{key}: {ex}")

    def per_platform(obj, key, value):
        try:
            v = obj.get_editor_property(key)
            v.set_editor_property("default", value)
            obj.set_editor_property(key, v)
        except Exception as ex:
            notes.append(f"{type(obj).__name__}.{key}: {ex}")

    for gt in g.get("grass", []):
        path = f"{gdir}/LGT_{gt['name']}"
        asset = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
        if asset is None:
            factory_cls = getattr(unreal, "LandscapeGrassTypeFactory", None)
            asset = tools.create_asset(f"LGT_{gt['name']}", gdir, unreal.LandscapeGrassType, factory_cls() if factory_cls else None)
        if asset is None:
            notes.append(f"만들지 못함: {path}")
            continue
        varieties = []
        for v in gt["varieties"]:
            mesh = unreal.load_asset(v["mesh"])
            if mesh is None:
                notes.append(f"메시 없음: {v['mesh']}")
                continue
            gv = unreal.GrassVariety()
            put(gv, "grass_mesh", mesh)
            per_platform(gv, "grass_density", float(v["density"]))
            per_platform(gv, "start_cull_distance", int(v.get("cull_start_cm", 5000)))
            per_platform(gv, "end_cull_distance", int(v.get("cull_end_cm", 7500)))
            put(gv, "use_grid", True)
            put(gv, "placement_jitter", 1.0)
            put(gv, "random_rotation", True)
            put(gv, "align_to_surface", bool(v.get("align", True)))
            put(gv, "use_landscape_lightmap", True)
            sc = v.get("scale", [0.8, 1.2])
            put(gv, "scaling", unreal.GrassScaling.UNIFORM)
            put(gv, "scale_x", unreal.FloatInterval(float(sc[0]), float(sc[1])))
            varieties.append(gv)
        put(asset, "grass_varieties", varieties)
        EAL.save_loaded_asset(asset)
        out.append({"asset": path, "varieties": len(varieties)})
    return {"grass_types": out, "notes": notes}


def build_ground_material(name):
    """ground의 지형 재질을 만든다(있으면 노드를 지우고 다시 — 지형이 참조하는 에셋은 그대로).
    색 = Σ 레이어 가중치 × (타일 색 × 색조) × 먼 거리 얼룩, 노멀·거칠기도 같은 합. vertical 타일은 월드 좌표 옆면 투영(능선).
    ground.grass가 있으면 Grass 출력 노드에 레이어 가중치를 잇는다(Grass Type 에셋이 먼저 있어야 함)."""
    d = load_def(name)
    g = d["ground"]
    adir = g["asset_dir"]
    path = _ground_material_path(g)
    quad_m = d["spacing_cm"] / 100.0
    notes = []
    EAL.make_directory(adir)
    mat = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if mat is not None:
        # delete_all_material_expressions는 한 번에 일부만 지운다(5.8에서 86 → 43개 남음 확인) → 0이 될 때까지 반복
        for _ in range(16):
            if MEL.get_num_material_expressions(mat) == 0:
                break
            MEL.delete_all_material_expressions(mat)
        left = MEL.get_num_material_expressions(mat)
        if left:
            notes.append(f"옛 노드 {left}개가 지워지지 않음")
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(g["material"], adir, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError(f"재질을 만들지 못함: {path}")

    def node(cls, **props):
        e = MEL.create_material_expression(mat, cls, -800, 0)
        for k, v in props.items():
            try:
                e.set_editor_property(k, v)
            except Exception as ex:
                notes.append(f"{cls.__name__}.{k}: {ex}")
        return e

    def link(a, out, b, inp):
        if not MEL.connect_material_expressions(a, out, b, inp):
            notes.append(f"연결 실패: {a.get_name()}[{out}] → {b.get_name()}[{inp}]")

    def op(cls, a, aout, b, bout, **props):
        e = node(cls, **props)
        link(a, aout, e, "A")
        if b is not None:
            link(b, bout, e, "B")
        return e

    def mask(src, out, r=False, gg=False, b=False):
        e = node(unreal.MaterialExpressionComponentMask, r=r, g=gg, b=b, a=False)
        link(src, out, e, "")
        return e

    coords = {}

    def uv(tile_m):
        if tile_m not in coords:
            coords[tile_m] = node(unreal.MaterialExpressionLandscapeLayerCoords, mapping_scale=float(tile_m / quad_m))
        return coords[tile_m]

    def sample(tex_path, uv_node):
        tex = unreal.load_asset(tex_path)
        if tex is None:
            raise RuntimeError(f"텍스처를 찾지 못함: {tex_path}")
        # 텍스처 수가 많아 공유 샘플러를 쓴다(재질당 고유 샘플러 16개 제한)
        e = node(unreal.MaterialExpressionTextureSample, texture=tex, sampler_type=_sampler_type(tex),
                 sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        link(uv_node, "", e, "UVs")
        return e

    flat_normal = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    tex_nodes = {}

    def tex_set(key):
        """타일 하나 → (색 노드, 출력), (노멀 노드, 출력), (거칠기 노드, 출력)"""
        if key in tex_nodes:
            return tex_nodes[key]
        t = g["textures"][key]
        normal = (flat_normal, "")
        if t.get("vertical"):
            wp = node(unreal.MaterialExpressionWorldPosition)
            size = float(t["tile_m"]) * 100.0
            uv_yz = op(unreal.MaterialExpressionDivide, mask(wp, "", gg=True, b=True), "", None, "", const_b=size)
            uv_xz = op(unreal.MaterialExpressionDivide, mask(wp, "", r=True, b=True), "", None, "", const_b=size)
            face_x = sample(t["d"], uv_yz)   # 면이 X(남북)를 향할 때
            face_y = sample(t["d"], uv_xz)   # 면이 Y(동서)를 향할 때
            ab = node(unreal.MaterialExpressionAbs)
            link(node(unreal.MaterialExpressionVertexNormalWS), "", ab, "")
            diff = op(unreal.MaterialExpressionSubtract, mask(ab, "", r=True), "", mask(ab, "", gg=True), "")
            sat = node(unreal.MaterialExpressionSaturate)
            link(op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, diff, "", None, "", const_b=4.0), "", None, "", const_b=0.5), "", sat, "")
            lerp = node(unreal.MaterialExpressionLinearInterpolate)
            link(face_y, "RGB", lerp, "A")
            link(face_x, "RGB", lerp, "B")
            link(sat, "", lerp, "Alpha")
            color = (lerp, "")
            rough = (node(unreal.MaterialExpressionConstant, r=float(t.get("rough", 0.85))), "")
        else:
            u = uv(t["tile_m"])
            dn = sample(t["d"], u)
            color = (dn, "RGB")
            if t.get("n"):
                normal = (sample(t["n"], u), "RGB")
            r = t.get("rough", "alpha" if t["d"].endswith("_D_R") else 0.9)
            rough = (dn, "A") if r == "alpha" else (node(unreal.MaterialExpressionConstant, r=float(r)), "")
        # 타일 색 보정: desat(0~1, 채도 낮춤) → tint(곱)
        if t.get("desat"):
            ds = node(unreal.MaterialExpressionDesaturation)
            link(color[0], color[1], ds, "")
            link(node(unreal.MaterialExpressionConstant, r=float(t["desat"])), "", ds, "Fraction")
            color = (ds, "")
        if t.get("tint"):
            tn = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(t["tint"][0], t["tint"][1], t["tint"][2], 1.0))
            color = (op(unreal.MaterialExpressionMultiply, color[0], color[1], tn, ""), "")
        tex_nodes[key] = (color, normal, rough)
        return tex_nodes[key]

    weights = {}
    acc = {"color": None, "normal": None, "rough": None}

    def add_term(slot, src, out, wnode):
        m = op(unreal.MaterialExpressionMultiply, src, out, wnode, "")
        acc[slot] = m if acc[slot] is None else op(unreal.MaterialExpressionAdd, acc[slot], "", m, "")

    for i, L in enumerate(g["layers"]):
        wn = node(unreal.MaterialExpressionLandscapeLayerSample, parameter_name=L["name"], preview_weight=1.0 if i == 0 else 0.0)
        weights[L["name"]] = wn
        color, normal, rough = tex_set(L["tex"])
        if L.get("tint"):
            tint = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(L["tint"][0], L["tint"][1], L["tint"][2], 1.0))
            color = (op(unreal.MaterialExpressionMultiply, color[0], color[1], tint, ""), "")
        add_term("color", color[0], color[1], wn)
        add_term("normal", normal[0], normal[1], wn)
        add_term("rough", rough[0], rough[1], wn)

    # 먼 거리 얼룩: 한 타일을 크게 깔아 밝기로 곱한다(넓은 풀밭의 반복 무늬 완화)
    final_color = acc["color"]
    mc = g.get("macro")
    if mc:
        far = sample(g["textures"][mc["tex"]]["d"], uv(mc["tile_m"]))
        lum = node(unreal.MaterialExpressionLinearInterpolate, const_a=float(mc["min"]), const_b=float(mc["max"]))
        link(far, "G", lum, "Alpha")
        final_color = op(unreal.MaterialExpressionMultiply, acc["color"], "", lum, "")

    MP = unreal.MaterialProperty
    MEL.connect_material_property(final_color, "", MP.MP_BASE_COLOR)
    MEL.connect_material_property(acc["normal"], "", MP.MP_NORMAL)
    MEL.connect_material_property(acc["rough"], "", MP.MP_ROUGHNESS)
    MEL.connect_material_property(node(unreal.MaterialExpressionConstant, r=float(g.get("specular", 0.25))), "", MP.MP_SPECULAR)

    def grass_source(gt):
        """Grass 출력에 넣을 값. patch가 있으면 레이어 가중치 × 잡음 얼룩(무리 지어 나게) — 풀 맵을 구울 때만 계산되므로 화면 비용은 없다."""
        wn = weights[gt["layer"]]
        pt = gt.get("patch")
        if not pt:
            return wn
        nz = node(unreal.MaterialExpressionNoise, scale=1.0 / (float(pt["wave_m"]) * 100.0), levels=2, output_min=0.0, output_max=1.0, turbulence=False)
        fn = getattr(unreal.NoiseFunction, "NOISEFUNCTION_GRADIENT_ALU", None)
        if fn is not None:
            try:
                nz.set_editor_property("noise_function", fn)
            except Exception as ex:
                notes.append(f"Noise.noise_function: {ex}")
        sub = op(unreal.MaterialExpressionSubtract, nz, "", None, "", const_b=float(pt.get("threshold", 0.5)))
        sat = node(unreal.MaterialExpressionSaturate)
        link(op(unreal.MaterialExpressionMultiply, sub, "", None, "", const_b=float(pt.get("sharp", 8.0))), "", sat, "")
        return op(unreal.MaterialExpressionMultiply, wn, "", sat, "")

    grass_linked = 0
    if g.get("grass"):
        try:
            inputs = []
            for gt in g["grass"]:
                asset = unreal.load_asset(f"{adir}/Grass/LGT_{gt['name']}")
                if asset is None:
                    notes.append(f"Grass Type 없음(먼저 grass 단계): LGT_{gt['name']}")
                    continue
                gi = unreal.GrassInput()
                gi.set_editor_property("name", gt["name"])
                gi.set_editor_property("grass_type", asset)
                inputs.append((gi, gt))
            if inputs:
                go = node(unreal.MaterialExpressionLandscapeGrassOutput, grass_types=[gi for gi, _ in inputs])
                for gi, gt in inputs:
                    if MEL.connect_material_expressions(grass_source(gt), "", go, gt["name"]):
                        grass_linked += 1
                    else:
                        notes.append(f"Grass 출력 연결 실패: {gt['layer']} → {gt['name']}")
        except Exception as ex:
            notes.append(f"Grass 출력 노드: {ex}")

    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return {"material": path, "expressions": MEL.get_num_material_expressions(mat), "grass_linked": grass_linked, "notes": notes}


def apply_ground_material(name):
    """지금 열린 레벨의 Landscape에 ground 재질을 지정한다(지형을 다시 만들지 않음)."""
    path = _ground_material_path(load_def(name)["ground"])
    mat = unreal.load_asset(path)
    if mat is None:
        raise RuntimeError(f"재질을 찾지 못함: {path}")
    done = []
    for a in EAS.get_all_level_actors():
        if isinstance(a, unreal.LandscapeProxy):
            a.set_editor_property("landscape_material", mat)
            done.append(a.get_actor_label())
    return {"material": path, "landscapes": done}


# ── 에셋 점검: 맵에 깔기 전에 메시·텍스처의 무게와 충돌을 본다 ──
def asset_audit(paths=None, dirs=None):
    """스태틱 메시: LOD 수·LOD별 삼각형·크기(m)·단순 충돌 수·Nanite·재질 수. 텍스처: 크기·sRGB·압축.
    dirs를 주면 그 폴더(하위 포함)의 스태틱 메시를 모두 본다."""
    paths = list(paths or [])
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for folder in dirs or []:
        for ad in reg.get_assets_by_path(folder, recursive=True):
            if str(ad.asset_class_path.asset_name) == "StaticMesh":
                paths.append(str(ad.package_name))
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    out = []
    for p in paths:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            item = {"path": p, "type": "StaticMesh"}
            try:
                lods = a.get_num_lods()
                item["lods"] = lods
                item["tris"] = [a.get_num_triangles(i) for i in range(lods)]
                ext = a.get_bounds().box_extent
                item["size_m"] = [round(ext.x * 0.02, 2), round(ext.y * 0.02, 2), round(ext.z * 0.02, 2)]
                item["materials"] = len(a.static_materials)
                item["simple_collision"] = sms.get_simple_collision_count(a)
                # 단순 충돌이 0이고 trace가 기본값이면 캐릭터가 통과한다(캐릭터 이동은 단순 충돌만 봄)
                item["collision_trace"] = str(a.get_editor_property("body_setup").get_editor_property("collision_trace_flag"))
                item["nanite"] = bool(a.get_editor_property("nanite_settings").enabled)
            except Exception as ex:
                item["error"] = str(ex)
            out.append(item)
        elif isinstance(a, unreal.Texture2D):
            out.append({"path": p, "type": "Texture2D", "size": [a.blueprint_get_size_x(), a.blueprint_get_size_y()],
                        "srgb": bool(a.get_editor_property("srgb")), "compression": str(a.get_editor_property("compression_settings"))})
        else:
            out.append({"path": p, "error": "스태틱 메시·텍스처가 아니거나 없음"})
    return out


_PROBE = "PROBE_"


def probe(name, items):
    """메시를 레벨에 임시로 놓아 모습을 본다(라벨 PROBE_*, 충돌 없음). items = [{"mesh", "x", "y"(m), "scale", "yaw"}].
    다시 부르면 기존 PROBE_ 액터를 지우고 놓는다. 빈 목록이면 지우기만 한다. 저장 전에 반드시 지울 것."""
    removed = 0
    for a in EAS.get_all_level_actors():
        if a.get_actor_label().startswith(_PROBE):
            EAS.destroy_actor(a)
            removed += 1
    h = height_fn(load_def(name))
    placed = []
    for i, it in enumerate(items or []):
        mesh = unreal.load_asset(it["mesh"])
        if mesh is None:
            placed.append({"error": f"메시 없음: {it['mesh']}"})
            continue
        x, y = float(it["x"]), float(it["y"])
        a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x * 100.0, y * 100.0, h(x, y) * 100.0), unreal.Rotator(0, 0, float(it.get("yaw", 0.0))))
        a.static_mesh_component.set_static_mesh(mesh)
        a.static_mesh_component.set_collision_profile_name("NoCollision")
        s = float(it.get("scale", 1.0))
        a.set_actor_scale3d(unreal.Vector(s, s, s))
        a.set_actor_label(f"{_PROBE}{i:02d}_{mesh.get_name()}")
        a.set_folder_path("Probe")
        placed.append(a.get_actor_label())
    return {"removed": removed, "placed": placed}


# ── 자연물(L2c): 정의의 dress대로 블록아웃 도형을 메시로 바꾼다. 라벨 접두사 NA_, 아웃라이너 폴더 Nature ──
# 보이는 메시는 충돌을 끄고, 충돌은 숨긴 기본 도형(NA_*_Col — 블록아웃과 같은 크기)이 맡는다.
#  · 나무: 서버 내보내기(MapExportCommandlet)가 메시 삼각형 전체를 쓰므로 나무에 충돌을 켜면 잎·가지까지 내비메시 입력이 된다.
#  · 바위: 팩의 바위 메시는 단순 충돌이 없어 캐릭터가 통과하고(클라), 서버는 삼각형으로 막아 어긋난다.
_NA = "NA_"
PROP_COL_MIN_TOP_M = 1.0   # 소품 충돌 도형 윗면의 최소 높이(지면 위, m) — box_collider 참고


def clear_dress():
    """NA_ 라벨 액터를 모두 지운다."""
    n = 0
    for a in EAS.get_all_level_actors():
        if a.get_actor_label().startswith(_NA):
            EAS.destroy_actor(a)
            n += 1
    return n


def _ring_points(item):
    """blockout의 ring과 같은 순서로 (x, y, 크기, yaw, 흔들림 여부)를 만든다(시드가 같아 블록아웃과 같은 자리)."""
    rnd = random.Random(item.get("seed", 1))
    r = item["r"]
    steps = max(3, int(round(2.0 * math.pi * r / item["step_m"])))
    jit = item.get("jitter", 0.0)
    out = []
    for i in range(steps):
        deg = 360.0 * i / steps
        if any(abs((deg - g["deg"] + 180.0) % 360.0 - 180.0) < math.degrees(g["width_m"] * 0.5 / r) for g in item.get("gaps", [])):
            continue
        rr = r * (1.0 + rnd.uniform(-jit, jit) * 0.3)
        x = item["x"] + rr * math.cos(math.radians(deg))
        y = item["y"] + rr * math.sin(math.radians(deg))
        sz = [v * (1.0 + rnd.uniform(-jit, jit)) for v in item["size"]]
        yaw = deg + (rnd.uniform(-40, 40) if jit else 0.0)
        out.append((x, y, sz, yaw))
    return out


def dress(name, list_rocks=False):
    """정의의 dress.items대로 자연물을 놓는다(기존 NA_ 액터는 지우고 다시, 바꾼 블록아웃 BO_<from>_*도 지움).
    kind: tree(blockout scatter Tree → 나무 + 숨긴 줄기 원기둥), rock(scatter Rock → 바위 + 숨긴 상자),
          ring_rock(ring → 큰 바위 + 숨긴 상자), scatter(충돌 없는 잔돌), place(메시 하나 — collide면 메시 자체 충돌)."""
    d = load_def(name)
    h = height_fn(d)
    spec = d["dress"]
    sets = spec.get("sets", {})
    bo_items = {it["label"]: it for it in d.get("blockout", [])}
    removed = clear_dress()

    roads = [r["points"] for r in d.get("roads", [])]
    for L in d.get("ground", {}).get("layers", []):
        roads += L.get("links", [])
    segs = [(a[0], a[1], b[0], b[1]) for pts in roads for a, b in zip(pts, pts[1:])]

    def road_dist(x, y):
        return min((_seg_dist(x, y, s) for s in segs), default=1e9)

    cache = {}

    def mesh_of(entry):
        p = entry["mesh"]
        if p not in cache:
            m = unreal.load_asset(p)
            if m is None:
                raise RuntimeError(f"메시를 찾지 못함: {p}")
            b = m.get_bounds()
            cache[p] = (m, b.origin, b.box_extent)
        return cache[p]

    def pick(rnd, entries):
        return rnd.choices(entries, [e.get("weight", 1) for e in entries])[0]

    def spawn(entry, label, folder, x, y, scale, yaw, sink_m, center=True, collide=False):
        """메시의 바닥(경계 상자 아래)이 지면 - sink에 오게 놓는다. center면 경계 상자 가운데가 (x, y)에 온다(피벗이 치우친 바위)."""
        m, org, ext = mesh_of(entry)
        px, py = x * 100.0, y * 100.0
        if center:
            c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            px -= (org.x * c - org.y * s) * scale
            py -= (org.x * s + org.y * c) * scale
        z = h(x, y) * 100.0 - (org.z - ext.z) * scale - sink_m * 100.0
        a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(px, py, z), unreal.Rotator(0, 0, yaw))
        a.static_mesh_component.set_static_mesh(m)
        if not collide:
            # set_collision_enabled(NO_COLLISION)만으로는 부족하다: 에디터에서는 꺼진 것으로 보이지만 프로필(BlockAll)이 남아
            # PIE·맵 로드 때 다시 켜진다(2026-11-18 문틀 메시가 문을 막음 — FBX 가져오기가 만든 단순 충돌 덩어리). 프로필을 바꿔야 유지된다.
            a.static_mesh_component.set_collision_profile_name("NoCollision")
        a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        a.set_actor_label(_NA + label)
        a.set_folder_path("Nature/" + folder)
        return a

    def collider(shape, label, x, y, size, yaw, sink):
        a = _spawn_shape(d, h, shape, label, x, y, size, yaw, sink)
        a.set_actor_label(_NA + label + "_Col")
        a.set_folder_path("Nature/Colliders")
        a.static_mesh_component.set_visibility(False)
        a.set_actor_hidden_in_game(True)
        return a

    def sink_of(entry, scale, default=0.2):
        """바위를 묻는 깊이(m) = 메시 높이 × 비율(sets의 sink, 바닥이 울퉁불퉁한 메시는 크게)."""
        _, _, ext = mesh_of(entry)
        return float(entry.get("sink", default)) * 2.0 * ext.z * scale / 100.0

    def rock_radius(entry, scale):
        _, _, ext = mesh_of(entry)
        return max(ext.x, ext.y) * scale * float(entry.get("col_fit", 0.8)) / 100.0

    def rock_collider(entry, label, x, y, scale, yaw):
        """보이는 바위에 맞춘 충돌: 메시 경계 상자의 가로 두 축 × col_fit인 타원 기둥을 바위와 같은 방향으로 놓는다.
        (블록아웃 상자를 그대로 쓰면 모서리가 바위 밖으로 나오거나 바위가 상자보다 1m 넘게 커서 캐릭터가 파고든다 — 2026-11-18 check_dress)
        높이는 보이는 바위의 60%(납작한 바위에서 기둥이 바위 위로 솟아 보이지 않는 벽이 되지 않게), 0.7~3m —
        다만 윗면이 둘레 지면보다 1m 이상 높게 늘린다(아래). 그래야 캐릭터가 걸어 오르지 못하고(걸음 높이 45cm)
        서버 내비메시(오를 수 있는 단차 50cm)도 막는다."""
        _, _, ext = mesh_of(entry)
        fit = float(entry.get("col_fit", 0.8))
        visible = 2.0 * ext.z * scale * (1.0 - float(entry.get("sink", 0.2))) / 100.0
        size = [2.0 * ext.x * scale * fit / 100.0, 2.0 * ext.y * scale * fit / 100.0, max(0.7, min(3.0, visible * 0.6))]
        # 기둥 윗면은 기둥이 덮는 자리의 가장 높은 지면보다 1m 이상 높아야 한다(L2e, D-187).
        # 예전에는 가운데 지면에서 0.4m 묻기만 해서 0.7m 기둥의 윗면이 지면 위 30~67cm였고(경사에서는 오르막 쪽이 더 낮음),
        # 서버 내비메시(단차 50cm)와 UE(걸음 높이 45cm)가 그 위를 걷는 곳으로 봤다 — 18개.
        ca, sa = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        grounds = [h(x, y)]
        for k in range(8):
            t = k * math.pi / 4.0
            px, py = math.cos(t) * size[0] * 0.5, math.sin(t) * size[1] * 0.5
            grounds.append(h(x + px * ca - py * sa, y + px * sa + py * ca))
        bottom = min(h(x, y) - 0.4, min(grounds) - 0.2)
        top = max(h(x, y) - 0.4 + size[2], max(grounds) + 1.0)
        size[2] = top - bottom
        collider("Cylinder", label, x, y, size, yaw, h(x, y) - bottom)

    road_half = 2.0
    for L in d.get("ground", {}).get("layers", []):
        if L["kind"] == "road":
            road_half = L["half_m"]

    def prop_scale(entry, it):
        """소품 배율: width_m(가로 긴 축) 또는 height_m에 맞춘다. 둘 다 없으면 scale(기본 1)."""
        _, _, ext = mesh_of(entry)
        if it.get("width_m"):
            return float(it["width_m"]) * 100.0 / (2.0 * max(ext.x, ext.y))
        if it.get("height_m"):
            return float(it["height_m"]) * 100.0 / (2.0 * ext.z)
        return float(it.get("scale", 1.0))

    def box_collider(entry, label, x, y, scale, yaw, it):
        """놓인 소품에 맞춘 충돌 상자: 메시 경계 상자 × col_fit([가로 X, 가로 Y, 높이] 비율, 기본 0.9·0.9·0.9), 소품과 같은 방향.
        col_shape가 Cylinder면 타원 기둥. collide가 false면 만들지 않는다.
        col_off = [fx, fy]: 상자 중심을 메시 반폭의 비율만큼 옮김(예: 차양의 좌판이 뒤쪽에 있을 때).
        col_extra = [[fx, fy, 폭 m], ...]: 그 자리에 기둥 충돌(정사각 상자, 높이 3m)을 더 놓음(예: 차양 기둥)."""
        if it.get("collide", True) is False:
            return
        _, _, ext = mesh_of(entry)
        fit = it.get("col_fit", [0.9, 0.9, 0.9])
        vis_h = 2.0 * ext.z * scale / 100.0
        height = min(float(it.get("col_max_h", 4.0)), vis_h * fit[2])
        if "col_max_h" not in it:
            # 윗면이 지면 위 1m 이상(작은 소품은 보이는 높이의 90%)이 되게 한다 — 아래 0.2m는 땅에 묻히므로 + 0.2.
            # 낮으면 UE(걸음 높이 45cm)와 서버 내비메시(단차 50cm)가 걸어 넘는 곳으로 본다(D-189, 바위의 D-187과 같은 이유.
            # 2026-11-18 L2d 재검증에서 윗면 41cm인 상자 더미를 캐릭터가 뚫고 지나가 발견). col_max_h를 준 것(차양 좌판)은 그 값을 따른다.
            height = max(height, min(PROP_COL_MIN_TOP_M, 0.9 * vis_h) + 0.2)
        size = [2.0 * ext.x * scale * fit[0] / 100.0, 2.0 * ext.y * scale * fit[1] / 100.0, height]
        cs, sn = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))

        def local(fx, fy):
            lx, ly = fx * ext.x * scale / 100.0, fy * ext.y * scale / 100.0
            return x + lx * cs - ly * sn, y + lx * sn + ly * cs

        ox, oy = local(*it.get("col_off", [0.0, 0.0]))
        collider(it.get("col_shape", "Cube"), label, ox, oy, size, yaw, 0.2)
        for k, (fx, fy, w) in enumerate(it.get("col_extra", [])):
            px, py = local(fx, fy)
            collider("Cube", f"{label}_P{k}", px, py, [w, w, 3.0], yaw, 0.2)

    def clear_of_roads(x, y, need):
        """길 중심선에서 need(m) 이상 떨어지게 옮긴 자리(이미 떨어져 있으면 그대로)."""
        best, hit = 1e9, None
        for ax, ay, bx, by in segs:
            vx, vy = bx - ax, by - ay
            ll = vx * vx + vy * vy
            t = 0.0 if ll <= 0.0 else max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / ll))
            qx, qy = ax + vx * t, ay + vy * t
            dist = math.hypot(x - qx, y - qy)
            if dist < best:
                best, hit = dist, (qx, qy, vx, vy)
        if best >= need:
            return x, y
        qx, qy, vx, vy = hit
        nx, ny = (x - qx, y - qy) if best > 1e-3 else (-vy, vx)
        ln = math.hypot(nx, ny)
        return qx + nx / ln * need, qy + ny / ln * need

    def fit_scale(entry, width_m, height_m=None, cover=1.25):
        """바위가 충돌 상자를 덮게 하는 배율: 가로는 cover배, 높이는 상자보다 낮지 않게."""
        _, _, ext = mesh_of(entry)
        s = cover * width_m * 100.0 / (2.0 * max(ext.x, ext.y))
        if height_m:
            s = max(s, height_m * 100.0 / (2.0 * ext.z))
        return s

    # 나무가 피할 원: dress.avoid(던전 입구·정거장처럼 비워 둘 곳), 마커(스폰·시작·포털), 놓인 바위(아래에서 추가)
    blocks = [(a["x"], a["y"], a["r"]) for a in spec.get("avoid", [])]
    blocks += [(m["x"] / 100.0, m["y"] / 100.0, 1.5) for m in json.loads(unreal.LandscapeBuildLibrary.list_level_markers())]

    counts = {}
    rocks = []
    replaced = set()
    # 바위를 먼저 놓고(나무가 실제 바위 자리를 피하게) 나무는 마지막에
    for it in sorted(spec["items"], key=lambda v: v["kind"] == "tree"):
        kind = it["kind"]
        entries = sets[it["set"]] if "set" in it else it.get("meshes")
        rnd = random.Random(it.get("seed", 100))
        n = 0
        if kind == "tree":
            src = bo_items[it["from"]]
            pts, _ = _scatter_points(d, src)
            for i, (x, y) in enumerate(pts):
                if road_dist(x, y) < it.get("road_clear_m", 4.0):
                    continue
                if any(math.hypot(x - bx, y - by) < br + 1.5 for bx, by, br in blocks):
                    continue
                e = pick(rnd, entries)
                sc = rnd.uniform(*e.get("scale", [0.85, 1.15]))
                spawn(e, f"{it['from']}_{i:03d}", "Trees", x, y, sc, rnd.uniform(0.0, 360.0), 0.15, center=False)
                tr = 2.0 * e.get("trunk_r_m", 0.3) * sc
                collider("Cylinder", f"{it['from']}_{i:03d}", x, y, [tr, tr, 4.0], 0.0, 0.3)
                n += 1
            replaced.add(it["from"])
        elif kind == "rock":
            src = bo_items[it["from"]]
            pts, rnd0 = _scatter_points(d, src)
            lo, hi = src.get("size", [3.0, 5.0])
            for i, (x, y) in enumerate(pts):
                # 블록아웃과 같은 난수 순서 = 같은 상자
                s = rnd0.uniform(lo, hi)
                size = [s, s * rnd0.uniform(0.7, 1.2), s * rnd0.uniform(0.5, 0.9)]
                yaw = rnd0.uniform(0, 90)
                e = pick(rnd, entries)
                sc = fit_scale(e, max(size[0], size[1]), None, it.get("cover", 1.15))
                rr = rock_radius(e, sc)
                x, y = clear_of_roads(x, y, road_half + rr + 1.0)
                yaw = rnd.uniform(0.0, 360.0)
                spawn(e, f"{it['from']}_{i:02d}", "Rocks", x, y, sc, yaw, sink_of(e, sc))
                rock_collider(e, f"{it['from']}_{i:02d}", x, y, sc, yaw)
                blocks.append((x, y, rr))
                rocks.append({"label": f"{it['from']}_{i:02d}", "x": round(x, 1), "y": round(y, 1), "z": round(h(x, y), 1),
                              "mesh": e["mesh"].rsplit("/", 1)[1], "scale": round(sc, 2)})
                n += 1
            replaced.add(it["from"])
        elif kind == "ring_rock":
            src = bo_items[it["from"]]
            for i, (x, y, size, yaw) in enumerate(_ring_points(src)):
                e = pick(rnd, entries)
                sc = fit_scale(e, max(size[0], size[1]), None, it.get("cover", 1.2))
                rr = rock_radius(e, sc)
                x, y = clear_of_roads(x, y, road_half + rr + 1.0)
                yaw = rnd.uniform(0.0, 360.0)
                spawn(e, f"{it['from']}_{i:02d}", "Rocks", x, y, sc, yaw, sink_of(e, sc))
                rock_collider(e, f"{it['from']}_{i:02d}", x, y, sc, yaw)
                blocks.append((x, y, rr))
                n += 1
            replaced.add(it["from"])
        elif kind == "scatter":
            pts, _ = _scatter_points(d, it)
            lo, hi = it["size"]
            for i, (x, y) in enumerate(pts):
                if road_dist(x, y) < it.get("road_clear_m", 3.5):
                    continue
                e = pick(rnd, entries)
                w = rnd.uniform(lo, hi)
                sc = fit_scale(e, w, None, 1.0)
                spawn(e, f"{it['label']}_{i:03d}", "Stones", x, y, sc, rnd.uniform(0.0, 360.0), sink_of(e, sc, 0.25))
                n += 1
        elif kind == "ring_prop":
            # 블록아웃 ring(목책)을 따라 소품을 한 칸씩. 블록아웃 칸은 X = 두께(바깥 방향), Y = 길이(접선) — 소품의 긴 축을 접선에 맞춘다.
            src = bo_items[it["from"]]
            _, _, ext = mesh_of(it)
            long_x = ext.x >= ext.y
            for i, (x, y, size, yaw) in enumerate(_ring_points(src)):
                sc = float(it.get("cover", 1.0)) * size[1] * 100.0 / (2.0 * max(ext.x, ext.y))
                pyaw = yaw + (90.0 if long_x else 0.0) + float(it.get("yaw_off", 0.0))
                spawn(it, f"{it['from']}_{i:02d}", "Village", x, y, sc, pyaw, float(it.get("sink_m", 0.1)))
                box_collider(it, f"{it['from']}_{i:02d}", x, y, sc, pyaw, it)
                n += 1
            # close_gaps_m(L5, D-203): 이 폭 이하인 틈(문 자리)은 양 끝에 칸을 하나씩 더 놓아 문 폭에 딱 맞춘다.
            # 고리 칸은 일정한 각도로 놓여서 틈이 문보다 한쪽으로 넓게 남을 수 있다(동·서문 옆으로 바깥이 보였음).
            if it.get("close_gaps_m"):
                seg_w = float(it.get("cover", 1.0)) * src["size"][1]
                for k, g in enumerate(src.get("gaps", [])):
                    if g["width_m"] > float(it["close_gaps_m"]):
                        continue
                    for side, tag in ((-1, "a"), (1, "b")):
                        deg = g["deg"] + side * math.degrees((g["width_m"] * 0.5 + seg_w * 0.5) / src["r"])
                        gx = src["x"] + src["r"] * math.cos(math.radians(deg))
                        gy = src["y"] + src["r"] * math.sin(math.radians(deg))
                        gyaw = deg + (90.0 if long_x else 0.0) + float(it.get("yaw_off", 0.0))
                        gsc = float(it.get("cover", 1.0)) * src["size"][1] * 100.0 / (2.0 * max(ext.x, ext.y))
                        spawn(it, f"{it['from']}_G{k}{tag}", "Village", gx, gy, gsc, gyaw, float(it.get("sink_m", 0.1)))
                        box_collider(it, f"{it['from']}_G{k}{tag}", gx, gy, gsc, gyaw, it)
                        n += 1
            replaced.add(it["from"])
        elif kind == "gate_prop":
            # 블록아웃 arch(문) 자리에 문틀 소품. 충돌은 양쪽 기둥 자리의 숨긴 상자 둘(가운데는 지나다님).
            src = bo_items[it["from"]]
            yaw = src["yaw"]
            w, hh, t = src["width_m"], src["height_m"], src["thick_m"]
            _, _, ext = mesh_of(it)
            long_x = ext.x >= ext.y
            total = (w + 2.0 * t) * float(it.get("cover", 1.0))   # 문틀 전체 폭(m)
            sc = total * 100.0 / (2.0 * max(ext.x, ext.y))
            pyaw = yaw + (90.0 if long_x else 0.0) + float(it.get("yaw_off", 0.0))
            spawn(it, it["from"], "Village", src["x"], src["y"], sc, pyaw, float(it.get("sink_m", 0.1)))
            # 기둥 충돌: 메시 비율(post_off = 중심에서 기둥까지 / 전체 폭, post_w = 기둥 밑동 폭 / 전체 폭)을 주면 그 자리에, 없으면 블록아웃 기둥 자리
            off = float(it["post_off"]) * total if it.get("post_off") else (w + t) * 0.5
            pw = float(it["post_w"]) * total if it.get("post_w") else t
            sx, sy = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
            if it.get("closed"):
                # 닫힌 문(L5, D-203): 문 전체 폭을 막는 상자 하나. 드나드는 것은 포털(서버 CS_PORTAL)로만 한다.
                collider("Cube", f"{it['from']}_Door", src["x"], src["y"], [float(it.get("door_thick_m", t)), total, min(hh, 4.0)], yaw, 0.2)
            else:
                for side, tag in ((-1, "L"), (1, "R")):
                    collider("Cube", f"{it['from']}_{tag}", src["x"] + side * sx * off, src["y"] + side * sy * off, [pw, pw, min(hh, 4.0)], yaw, 0.2)
            replaced.add(it["from"])
            n = 1
        elif kind == "prop":
            # 소품 하나: from(블록아웃 box)의 자리·방향 또는 x·y·yaw. 크기는 width_m / height_m / scale.
            src = bo_items.get(it.get("from")) or it
            sc = prop_scale(it, it)
            pyaw = float(src.get("yaw", 0.0)) + float(it.get("yaw_off", 0.0))
            label = it.get("label") or it["from"]
            spawn(it, label, "Village", src["x"], src["y"], sc, pyaw, float(it.get("sink_m", 0.05)))
            box_collider(it, label, src["x"], src["y"], sc, pyaw, it)
            if it.get("from"):
                replaced.add(it["from"])
            n = 1
        elif kind == "place":
            spawn(it, it["label"], it.get("folder", "Rocks"), it["x"], it["y"], float(it.get("scale", 1.0)), float(it.get("yaw", 0.0)),
                  float(it.get("sink_m", 0.0)), center=bool(it.get("center", True)), collide=bool(it.get("collide", False)))
            if it.get("replaces"):
                replaced.add(it["replaces"])
            n = 1
        else:
            raise ValueError(f"모르는 dress kind: {kind}")
        key = it.get("group") or it.get("from") or it.get("label")   # 마을 소품은 묶어서 센다
        counts[key] = counts.get(key, 0) + n

    bo_removed = 0
    for a in EAS.get_all_level_actors():
        lab = a.get_actor_label()
        if any(lab == _BO + r or lab.startswith(_BO + r + "_") for r in replaced):
            EAS.destroy_actor(a)
            bo_removed += 1
    out = {"removed": removed, "blockout_removed": bo_removed, "placed": counts}
    if list_rocks:
        out["rocks"] = rocks   # 바위 위치·메시(가까이서 찍어 볼 때)
    return out


def check_dress(name):
    """자연물 배치 점검(레벨의 NA_ 액터 기준). 위반만 돌려준다:
    road = 충돌 도형이 길을 막음, marker = 마커(스폰·시작·포털)가 충돌 도형 안이나 바로 옆, flat = 평지(마을·입구) 안,
    overlap = 나무와 바위가 겹침, wall = 충돌 상자가 보이는 바위 밖으로 나옴(보이지 않는 벽), clip = 보이는 바위가 상자보다 얼마나 큰지(m)."""
    d = load_def(name)
    cols, vis = [], {}
    for a in EAS.get_all_level_actors():
        lab = a.get_actor_label()
        if not lab.startswith(_NA):
            continue
        if lab.endswith("_Col"):
            loc, sc = a.get_actor_location(), a.get_actor_scale3d()
            tree = "Tree" in lab
            cols.append({"label": lab[:-4], "x": loc.x / 100.0, "y": loc.y / 100.0, "tree": tree,
                         "r": (sc.x * 0.5) if tree else 0.5 * max(sc.x, sc.y), "sx": sc.x, "sy": sc.y, "yaw": a.get_actor_rotation().yaw})
        else:
            org, ext = a.get_actor_bounds(False)
            vis[lab] = (org.x / 100.0, org.y / 100.0, ext.x / 100.0, ext.y / 100.0)

    roads = [r["points"] for r in d.get("roads", [])]
    road_half = 2.0
    for L in d.get("ground", {}).get("layers", []):
        if L["kind"] == "road":
            roads += L.get("links", [])
            road_half = L["half_m"]
    segs = [(a[0], a[1], b[0], b[1]) for pts in roads for a, b in zip(pts, pts[1:])]
    markers = json.loads(unreal.LandscapeBuildLibrary.list_level_markers())

    village = [_NA + (it.get("label") or it["from"]) for it in d.get("dress", {}).get("items", []) if it.get("group") == "village"]

    def is_village(label):
        return any(label == v or label.startswith(v + "_") for v in village)

    out = {"road": [], "marker": [], "flat": [], "overlap": [], "wall": []}
    clip = []
    rocks = [c for c in cols if not c["tree"]]
    for c in cols:
        rd = min((_seg_dist(c["x"], c["y"], s) for s in segs), default=1e9)
        if rd < road_half + c["r"]:
            out["road"].append({"label": c["label"], "road_dist_m": round(rd, 1), "r": round(c["r"], 1)})
        for m in markers:
            dist = math.hypot(c["x"] - m["x"] / 100.0, c["y"] - m["y"] / 100.0)
            if dist < c["r"] + (1.0 if m["type"] == "MonsterSpawn" else 2.5):
                out["marker"].append({"label": c["label"], "marker": m["label"], "dist_m": round(dist, 1), "r": round(c["r"], 1)})
        for f in d.get("flats", []):
            # 마을 소품(group village)은 평지 안에 놓는 것이 정상이라 뺀다
            if math.hypot(c["x"] - f["x"], c["y"] - f["y"]) < f["r"] and not is_village(c["label"]):
                out["flat"].append({"label": c["label"], "flat": f["name"]})
        if c["tree"]:
            for r in rocks:
                if math.hypot(c["x"] - r["x"], c["y"] - r["y"]) < c["r"] + r["r"]:
                    out["overlap"].append({"tree": c["label"], "rock": r["label"]})
        elif c["label"] in vis:
            # 충돌 타원이 보이는 바위의 경계 상자(축 정렬) 밖으로 얼마나 나오는지, 안쪽으로는 얼마나 작은지
            # (경계 상자 기준이라 바위 윤곽이 상자보다 작은 만큼은 못 잡는다 → 캡처로 함께 본다)
            vx, vy, ex, ey = vis[c["label"]]
            cs, sn = math.cos(math.radians(c["yaw"])), math.sin(math.radians(c["yaw"]))
            a, b = c["sx"] * 0.5, c["sy"] * 0.5
            hx, hy = math.hypot(a * cs, b * sn), math.hypot(a * sn, b * cs)
            worst = max(abs(c["x"] - vx) + hx - ex, abs(c["y"] - vy) + hy - ey)
            if worst > 0.1:
                out["wall"].append({"label": c["label"], "out_m": round(worst, 2)})
            clip.append(0.5 * ((ex - hx) + (ey - hy)))
    res = {k: {"count": len(v), "examples": v[:6]} for k, v in out.items()}
    res["colliders"] = {"trees": len(cols) - len(rocks), "rocks": len(rocks)}
    if clip:
        res["clip_m"] = {"min": round(min(clip), 2), "mean": round(sum(clip) / len(clip), 2), "max": round(max(clip), 2)}
    return res


def _light_value(v):
    """정의의 값 → 에디터 프로퍼티 값: {"color": [r,g,b]} = Color, {"linear": [r,g,b]} = LinearColor, {"vec4": [x,y,z,w]} = Vector4."""
    if isinstance(v, dict):
        if "color" in v:
            c = v["color"]
            return unreal.Color(int(c[2]), int(c[1]), int(c[0]), 255)  # Color(b, g, r, a)
        if "linear" in v:
            c = v["linear"]
            return unreal.LinearColor(c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0)
        if "vec4" in v:
            c = v["vec4"]
            return unreal.Vector4(c[0], c[1], c[2], c[3])
    return v


def apply_lighting(name, dump=False, actors=None, pie=False):
    """본 조명(L2f): 정의의 lighting.actors대로 조명 액터를 만들거나(없을 때) 값을 넣는다 — 다시 불러도 같은 결과.
    pie = True면 PIE 월드의 액터에 임시로 넣고(비교 측정), actors를 주면 정의 대신 그 목록을 쓴다.
    항목: label, class(unreal 클래스 이름), component(값을 넣을 컴포넌트 클래스 이름), movable, location_m, rotation{pitch, yaw},
    props(컴포넌트 프로퍼티), actor_props(액터 프로퍼티), settings(후처리 볼륨의 PostProcessSettings — override_* 를 함께 켠다).
    dump = True면 값을 넣지 않고 지금 값만 읽어 돌려준다."""
    d = load_def(name)
    if pie:
        # 비교 측정용: PIE 월드의 조명 액터에 임시로 넣는다(저장되지 않음, 없는 액터는 만들지 않음). actors로 다른 값을 줄 수 있다
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return {"error": "PIE가 실행 중이 아님"}
        level_actors = unreal.GameplayStatics.get_all_actors_of_class(worlds[0], unreal.Actor)
    else:
        level_actors = EAS.get_all_level_actors()
    by_label = {a.get_actor_label(): a for a in level_actors}
    out = []
    for it in (actors if actors is not None else d.get("lighting", {}).get("actors", [])):
        label = it["label"]
        a = by_label.get(label)
        info = {"label": label, "created": False}
        if a is None:
            if dump or pie:
                info["missing"] = True
                out.append(info)
                continue
            a = EAS.spawn_actor_from_class(getattr(unreal, it["class"]), unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
            a.set_actor_label(label)
            a.set_folder_path("Lighting")
            info["created"] = True
        comp = a.get_component_by_class(getattr(unreal, it["component"])) if it.get("component") else None
        if dump:
            cur = {}
            st = a.get_editor_property("settings") if it.get("settings") else None
            for src, keys in ((comp, it.get("props", {})), (st, it.get("settings", {}))):
                for k in keys:
                    try:
                        cur[k] = str(src.get_editor_property(k))
                    except Exception as e:
                        cur[k] = f"ERROR {e}"
            rot = a.get_actor_rotation()
            info.update({"rotation": {"pitch": round(rot.pitch, 1), "yaw": round(rot.yaw, 1)}, "now": cur})
            out.append(info)
            continue
        if it.get("movable") and a.root_component:
            a.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        if "location_m" in it:
            x, y, z = it["location_m"]
            a.set_actor_location(unreal.Vector(x * 100.0, y * 100.0, z * 100.0), False, False)
        if "rotation" in it:
            a.set_actor_rotation(unreal.Rotator(0.0, float(it["rotation"].get("pitch", 0.0)), float(it["rotation"].get("yaw", 0.0))), False)
        errors = []
        for k, v in it.get("actor_props", {}).items():
            try:
                a.set_editor_property(k, _light_value(v))
            except Exception as e:  # 프로퍼티 이름이 틀리면 그 항목만 알림
                errors.append(f"{k}: {e}")
        for k, v in it.get("props", {}).items():
            try:
                comp.set_editor_property(k, _light_value(v))
            except Exception as e:
                errors.append(f"{k}: {e}")
        if it.get("settings"):
            st = a.get_editor_property("settings")
            for k, v in it["settings"].items():
                try:
                    st.set_editor_property(k, _light_value(v))
                    st.set_editor_property("override_" + k, True)
                except Exception as e:
                    errors.append(f"{k}: {e}")
            a.set_editor_property("settings", st)
        if errors:
            info["errors"] = errors
        out.append(info)
    return out


# ── PIE 걷기(DG3b 2인 PIE 확인): 실제 플레이어를 입력 없이 목표 지점까지 걷게 한다 ──
# 위치는 건드리지 않고 매 프레임 이동 입력(add_movement_input)만 넣으므로 이동 패킷·서버 검증은 손으로 걸을 때와 같다.
# 모듈을 다시 불러와도(run_step 호출마다) 기록이 남도록 unreal 모듈에 둔다
if not hasattr(unreal, "_jm_pie_walks"):
    unreal._jm_pie_walks = {}
_PIE_WALKS = unreal._jm_pie_walks   # PIE 창 번호 → {"handle", "path", "t0", "timeout", "tol", "state"}


def pie_walk(args):
    """args = {"world": 창 번호, "path": [[x_m, y_m], ...] 또는 "x"·"y", "tol": 도착 판정 m(기본 0.6), "timeout": 초(기본 40)} — 바로 돌아오고 걷기는 뒤에서 진행.
    {"world": n, "status": true}면 진행 상태만, {"world": n, "stop": true}면 멈춤. 직선으로만 가므로 막히는 곳은 path로 돌아간다."""
    import time
    idx = int(args.get("world", 0))
    cur = _PIE_WALKS.get(idx)

    def _stop(state, w=None):
        w = w or _PIE_WALKS.get(idx)
        if w and w.get("handle") is not None:
            unreal.unregister_slate_post_tick_callback(w["handle"])
            w["handle"] = None
        if w:
            w["state"] = state

    def _pawn():
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if idx >= len(worlds):
            return None
        return unreal.GameplayStatics.get_player_pawn(worlds[idx], 0)

    def _report():
        w = _PIE_WALKS.get(idx)
        pawn = _pawn()
        out = {"world": idx, "state": w["state"] if w else "none", "left": len(w["path"]) if w else 0}
        if pawn is not None:
            loc = pawn.get_actor_location()
            out.update({"pawn": pawn.get_class().get_name(), "x": round(loc.x / 100.0, 2), "y": round(loc.y / 100.0, 2), "z": round(loc.z / 100.0, 2)})
        return out

    if args.get("status"):
        return _report()
    if cur:
        _stop("stopped")
    if args.get("stop"):
        return _report()
    path = args.get("path") or [[args["x"], args["y"]]]
    if _pawn() is None:
        return {"error": "PIE 창 %d에 플레이어 폰이 없음" % idx}
    walk = {"handle": None, "path": [[float(p[0]) * 100.0, float(p[1]) * 100.0] for p in path], "t0": time.time(),
            "timeout": float(args.get("timeout", 40.0)), "tol": float(args.get("tol", 0.6)) * 100.0, "state": "walking"}
    _PIE_WALKS[idx] = walk

    def _tick(_dt):
        try:
            pawn = _pawn()
            if pawn is None:
                return _stop("no_pawn", walk)
            if time.time() - walk["t0"] > walk["timeout"]:
                return _stop("timeout", walk)
            loc = pawn.get_actor_location()
            while walk["path"] and math.hypot(walk["path"][0][0] - loc.x, walk["path"][0][1] - loc.y) <= walk["tol"]:
                walk["path"].pop(0)
            if not walk["path"]:
                return _stop("arrived", walk)
            dx, dy = walk["path"][0][0] - loc.x, walk["path"][0][1] - loc.y
            d = math.hypot(dx, dy)
            pawn.add_movement_input(unreal.Vector(dx / d, dy / d, 0.0), 1.0, False)
        except Exception as e:   # PIE가 끝나 월드가 사라진 경우 등 — 콜백을 반드시 뗀다
            _stop("error: %s" % e, walk)

    walk["handle"] = unreal.register_slate_post_tick_callback(_tick)
    return _report()


# pie_exec가 받는 콘솔 명령(첫 단어, 소문자): 측정과 렌더 설정,
# 그리고 게임의 개발용 콘솔 명령(AJMPlayerController — 서버가 검증하는 요청만 보냄: Give·Equip·Shop*, L4에서 추가)
_PIE_EXEC_EXACT = {"csvprofile", "stat", "togglefullscreen", "give", "equip", "shopopen", "shopbuy", "shopsell", "shopclose", "netmovestats", "autoattack", "attackstats", "portal", "partyinvite", "partyrespond", "partyleave", "partyshow"}
_PIE_EXEC_PREFIX = ("t.", "r.", "sg.", "grass.", "foliage.", "jm.")


# ── 범용 진입점(MCP world_step): 새 단계를 추가해도 툴셋(= 에디터 재시작)을 고치지 않는다 ──
def run_step(name, step, args=None):
    """step: weights / grass / material / apply_material / ground(grass → material → apply_material) / audit / probe / dress / clear_dress / check_dress / show_colliders / colliders / near / pie_pawn / pie_chars / prop_import / prop_info / lighting / pie_exec / pie_view / pie_walk / pie_revive. pie_* 단계는 args.world로 PIE 창 번호(0부터)를 고른다."""
    args = args or {}
    if step.startswith("dg_"):
        # 던전 맵 도구(DG4): jm_kit.dungeon
        import importlib
        from jm_kit import dungeon
        importlib.reload(dungeon)
        return dungeon.run(name, step, args)
    if step == "probe":
        return probe(name, args.get("items"))
    if step == "dress":
        return dress(name, bool(args.get("list_rocks")))
    if step == "clear_dress":
        return {"removed": clear_dress()}
    if step in ("prop_import", "prop_info"):
        # 맵 소품(L2d): args = {"fbx": 경로, "prop": 이름, "target_tris": LOD0 삼각형 수} / {"prop": 이름}
        import importlib
        from jm_kit import props
        importlib.reload(props)
        if step == "prop_import":
            return props.import_prop(args["fbx"], args["prop"], args.get("target_tris"))
        return props.prop_info(args["prop"])
    if step == "check_dress":
        return check_dress(name)
    if step == "show_colliders":
        # 숨긴 충돌 도형을 잠깐 보이게(캡처로 보이는 메시와 비교) — 끝나면 show = false로 되돌릴 것
        n = 0
        for a in EAS.get_all_level_actors():
            if a.get_actor_label().startswith(_NA) and a.get_actor_label().endswith("_Col"):
                a.static_mesh_component.set_visibility(bool(args.get("show")))
                n += 1
        return {"colliders": n, "visible": bool(args.get("show"))}
    if step == "colliders":
        # 걷기 확인용: 숨긴 충돌 도형 몇 개의 위치(m)와 크기(m). args.prefix = 라벨 접두사(예 NA_TreeNW, NA_MeadowRockN)
        out = []
        for a in EAS.get_all_level_actors():
            lab = a.get_actor_label()
            if lab.startswith(args.get("prefix", _NA)) and lab.endswith("_Col"):
                loc, sc = a.get_actor_location(), a.get_actor_scale3d()
                out.append({"label": lab, "x": round(loc.x / 100.0, 2), "y": round(loc.y / 100.0, 2), "ground": round(height_fn(load_def(name))(loc.x / 100.0, loc.y / 100.0), 2),
                            "size": [round(sc.x, 2), round(sc.y, 2), round(sc.z, 2)], "yaw": round(a.get_actor_rotation().yaw, 1)})
                if len(out) >= int(args.get("limit", 3)):
                    break
        return out
    if step == "near":
        # (x, y) m 주변 r m 안의 액터: 라벨·클래스·위치·충돌 여부 — "무엇이 막고 있나"를 찾을 때. args = {"x", "y", "r"}
        out = []
        cx, cy, rr = float(args["x"]) * 100.0, float(args["y"]) * 100.0, float(args.get("r", 6.0)) * 100.0
        for a in EAS.get_all_level_actors():
            if isinstance(a, unreal.LandscapeProxy):
                continue
            org, ext = a.get_actor_bounds(False)
            dx = max(abs(org.x - cx) - ext.x, 0.0)
            dy = max(abs(org.y - cy) - ext.y, 0.0)
            if math.hypot(dx, dy) > rr:
                continue
            col = None
            if isinstance(a, unreal.StaticMeshActor):
                col = f"{a.static_mesh_component.get_collision_profile_name()} / {a.static_mesh_component.get_collision_enabled()}"
            out.append({"label": a.get_actor_label(), "class": a.get_class().get_name(), "collision": col,
                        "center_m": [round(org.x / 100.0, 1), round(org.y / 100.0, 1), round(org.z / 100.0, 1)],
                        "half_m": [round(ext.x / 100.0, 1), round(ext.y / 100.0, 1), round(ext.z / 100.0, 1)]})
        return out
    if step == "pie_revive":
        # PIE 창의 부활 창 버튼 처리 함수를 부른다(버튼을 누른 것과 같이 CS_REVIVE를 보냄 — 대기 시간 등 검증은 서버). args.world = 창 번호
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return {"error": "PIE not running"}
        w = worlds[min(int(args.get("world", 0)), len(worlds) - 1)]
        found = unreal.WidgetLibrary.get_all_widgets_of_class(w, unreal.load_class(None, "/Script/JM.ReviveWidget"), False)
        shown = [x for x in found if x.is_visible()]
        for x in shown:
            x.call_method("OnReviveClicked")
        return {"widgets": len(found), "clicked": len(shown)}
    if step == "pie_walk":
        return pie_walk(args)
    if step == "pie_pawn":
        # PIE 중인 플레이어 폰의 위치(m)와 그 자리의 지면 높이(정의 기준) — 서버 없는 걷기 자동 확인(TestAuto.txt)에서 씀
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return {"error": "PIE가 실행 중이 아님"}
        pawn = unreal.GameplayStatics.get_player_pawn(worlds[min(int(args.get("world", 0)), len(worlds) - 1)], 0)
        if pawn is None:
            return {"error": "플레이어 폰이 없음"}
        loc = pawn.get_actor_location()
        x, y = loc.x / 100.0, loc.y / 100.0
        return {"pawn": pawn.get_class().get_name(), "x": round(x, 2), "y": round(y, 2), "z": round(loc.z / 100.0, 2),
                "ground": round(height_fn(load_def(name))(x, y), 2), "speed": round(pawn.get_velocity().length() / 100.0, 2)}
    if step == "pie_chars":
        # PIE 중인 월드의 캐릭터(내 플레이어·다른 플레이어·몬스터) 위치와 발 높이 - 지면 높이(정의 기준, cm).
        # 서버가 보낸 높이대로 놓인 캐릭터가 땅에 묻히거나 뜨는지 볼 때(L2e — 내비메시는 지형보다 평균 15cm 높음). args.limit = 최대 개수
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return {"error": "PIE가 실행 중이 아님"}
        w = worlds[min(int(args.get("world", 0)), len(worlds) - 1)]
        h = height_fn(load_def(name))
        out = {"world": w.get_name(), "chars": []}
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Character):
            loc = a.get_actor_location()
            half = a.capsule_component.get_scaled_capsule_half_height()
            out["chars"].append({"class": a.get_class().get_name(), "x": round(loc.x), "y": round(loc.y), "z": round(loc.z, 1), "half_height": round(half, 1),
                                 "feet_minus_ground_cm": round(loc.z - half - h(loc.x / 100.0, loc.y / 100.0) * 100.0, 1),
                                 "falling": bool(a.character_movement.is_falling()) if a.character_movement else None})
            if len(out["chars"]) >= int(args.get("limit", 40)):
                break
        return out
    if step == "lighting":
        # 본 조명(L2f): 정의의 lighting대로 조명 액터 값을 넣음. args = {"dump": true}면 지금 값만 읽음
        return apply_lighting(name, bool(args.get("dump")), args.get("actors"), bool(args.get("pie")))
    if step == "pie_exec":
        # PIE 월드에서 콘솔 명령 실행(L2f 성능 측정용, D-190). 임의 명령을 막으려고 측정·렌더 설정 명령만 받는다.
        # args = {"cmds": ["t.MaxFPS 0", "r.VSync 0", "csvprofile start", ...]}
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return {"error": "PIE가 실행 중이 아님"}
        w = worlds[min(int(args.get("world", 0)), len(worlds) - 1)]
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        done, refused = [], []
        for cmd in args.get("cmds", []):
            head = cmd.strip().split(" ")[0].lower()
            if head in _PIE_EXEC_EXACT or head.startswith(_PIE_EXEC_PREFIX):
                unreal.SystemLibrary.execute_console_command(w, cmd, pc)
                done.append(cmd)
            else:
                refused.append(cmd)
        size = unreal.WidgetLayoutLibrary.get_viewport_size(w)
        return {"world": w.get_name(), "done": done, "refused": refused, "viewport_px": [int(size.x), int(size.y)]}
    if step == "pie_view":
        # PIE 화면의 시점을 고정 카메라로 옮긴다(측정 지점 이동 — 캐릭터 위치는 서버 권위라 건드리지 않음).
        # args = {"x", "y", "z"(m), "pitch", "yaw"} / {"clear": true}면 플레이어 시점으로 되돌림
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return {"error": "PIE가 실행 중이 아님"}
        w = worlds[min(int(args.get("world", 0)), len(worlds) - 1)]
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        if pc is None:
            return {"error": "플레이어 컨트롤러가 없음"}
        # 게임 월드에 액터를 만드는 Python 함수가 없어 플레이어 폰의 카메라 컴포넌트를 절대 좌표로 바꿔 측정 지점에 둔다
        # (스프링 암을 따라가지 않게 됨 — PIE 안에서만 바뀌고 저장되지 않는다).
        pawn = pc.get_controlled_pawn()
        cams = pawn.get_components_by_class(unreal.CameraComponent) if pawn else []
        if not cams:
            return {"error": "플레이어 카메라가 없음"}
        cam = cams[0]
        if args.get("clear"):
            cam.set_absolute(False, False, False)
            cam.set_relative_location_and_rotation(unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), False, False)
            return {"view": "player"}
        loc = unreal.Vector(float(args["x"]) * 100.0, float(args["y"]) * 100.0, float(args["z"]) * 100.0)
        rot = unreal.Rotator(0.0, float(args.get("pitch", -10.0)), float(args.get("yaw", 0.0)))
        cam.set_editor_property("use_pawn_control_rotation", False)
        cam.set_absolute(True, True, False)
        cam.set_world_location_and_rotation(loc, rot, False, False)
        got = cam.get_world_location()
        return {"view": [round(got.x / 100.0, 1), round(got.y / 100.0, 1), round(got.z / 100.0, 1)], "pitch": rot.pitch, "yaw": rot.yaw}
    if step == "weights":
        return build_weights(load_def(name), _saved_path(name, "w8"))
    if step == "grass":
        return build_grass_types(name)
    if step == "material":
        return build_ground_material(name)
    if step == "apply_material":
        return apply_ground_material(name)
    if step == "ground":
        return {"grass": build_grass_types(name), "material": build_ground_material(name), "apply": apply_ground_material(name)}
    if step == "audit":
        return asset_audit(args.get("paths"), args.get("dirs"))
    raise ValueError(f"모르는 step: {step}")
