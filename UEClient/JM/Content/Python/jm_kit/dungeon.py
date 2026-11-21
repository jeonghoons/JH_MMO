"""던전 맵 도구(DG4, Docs/DESIGN_WORLD.md): 실내 레벨에 필요한 것만 모은다. MCP world_step의 "dg_*" 단계로 부른다(jm_kit.world.run_step).

 · dg_surface: CC0 석재 텍스처(색·노멀·ARM) → /Game/Environment/Dungeon/Materials 의 텍스처와 재질 인스턴스.
   재질 M_Dungeon_Surface는 월드 좌표로 세 방향에서 입히므로(triplanar) 상자를 어떤 크기로 늘려도 타일 크기가 같다.
"""

import math
import os
import random

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
DG_DIR = "/Game/Environment/Dungeon"   # D-245(옛 World/Dungeon)
MAT_DIR = DG_DIR + "/Materials"
MASTER = MAT_DIR + "/M_Dungeon_Surface"

# 세 방향 투영. 가중치는 면 방향(정점 노멀)의 4제곱 — 상자는 면마다 한 방향만 남는다.
_HLSL_HEAD = """float3 w = pow(abs(N), 4.0); w /= (w.x + w.y + w.z);
float2 uz = float2(WP.y, -WP.x) / Tile;
float2 ux = float2(WP.y, -WP.z) / Tile;
float2 uy = float2(WP.x, -WP.z) / Tile;
"""
_HLSL_RGB = _HLSL_HEAD + """return Texture2DSample(Tex, TexSampler, uz).rgb * w.z + Texture2DSample(Tex, TexSampler, ux).rgb * w.x + Texture2DSample(Tex, TexSampler, uy).rgb * w.y;"""
# 노멀: 압축(BC5)은 RG만 가지므로 Z를 다시 만든다. 투영마다 접선(U 방향)·종접선(V 방향, DirectX 노멀 = 아래)을 월드 축으로 옮긴다.
_HLSL_NRM = _HLSL_HEAD + """float3 a = float3(Texture2DSample(Tex, TexSampler, uz).xy * 2.0 - 1.0, 0.0); a.z = sqrt(saturate(1.0 - dot(a.xy, a.xy)));
float3 b = float3(Texture2DSample(Tex, TexSampler, ux).xy * 2.0 - 1.0, 0.0); b.z = sqrt(saturate(1.0 - dot(b.xy, b.xy)));
float3 c = float3(Texture2DSample(Tex, TexSampler, uy).xy * 2.0 - 1.0, 0.0); c.z = sqrt(saturate(1.0 - dot(c.xy, c.xy)));
float3 sg = sign(N);
float3 n = float3(-a.y, a.x, a.z * sg.z) * w.z + float3(b.z * sg.x, b.x, -b.y) * w.x + float3(c.x, c.z * sg.y, -c.y) * w.y;
return normalize(n);"""


def _import_tex(src, dest_name, kind):
    """파일 하나를 MAT_DIR/<dest_name>으로 가져온다. kind: color / normal / mask."""
    t = unreal.AssetImportTask()
    t.filename = src
    t.destination_path = MAT_DIR
    t.destination_name = dest_name
    t.automated = True
    t.save = True
    t.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset(f"{MAT_DIR}/{dest_name}")
    if tex is None:
        raise RuntimeError(f"텍스처 임포트 실패: {src}")
    if kind == "normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "mask":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    else:
        tex.set_editor_property("srgb", True)
    EAL.save_loaded_asset(tex)
    return tex


def ensure_master(rebuild=False):
    """M_Dungeon_Surface(월드 정렬 석재). 매개변수: BaseColor·Normal·ARM(텍스처), TileCm, Tint, Brightness, RoughMul, NormalFlat(0 = 노멀 그대로, 1 = 평평)."""
    notes = []
    mat = unreal.load_asset(MASTER) if EAL.does_asset_exist(MASTER) else None
    if mat is not None and not rebuild:
        return mat, notes
    EAL.make_directory(MAT_DIR)
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Dungeon_Surface", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    else:
        for _ in range(16):   # 한 번에 일부만 지워진다(world.build_ground_material과 같은 함정)
            if MEL.get_num_material_expressions(mat) == 0:
                break
            MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("tangent_space_normal", False)

    def node(cls, x, y, **props):
        e = MEL.create_material_expression(mat, cls, x, y)
        for k, v in props.items():
            try:
                e.set_editor_property(k, v)
            except Exception as ex:
                notes.append(f"{cls.__name__}.{k}: {ex}")
        return e

    def link(a, out, b, inp):
        if not MEL.connect_material_expressions(a, out, b, inp):
            notes.append(f"연결 실패: {a.get_name()}[{out}] → {b.get_name()}[{inp}]")

    wp = node(unreal.MaterialExpressionWorldPosition, -1400, 0)
    vn = node(unreal.MaterialExpressionVertexNormalWS, -1400, 200)
    tile = node(unreal.MaterialExpressionScalarParameter, -1400, 400, parameter_name="TileCm", default_value=200.0)

    def tri(pname, y, sampler, code):
        tex = node(unreal.MaterialExpressionTextureObjectParameter, -1400, y, parameter_name=pname, sampler_type=sampler)
        c = node(unreal.MaterialExpressionCustom, -900, y, code=code, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description="Triplanar_" + pname)
        ins = []
        for n in ("Tex", "WP", "N", "Tile"):
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        link(tex, "", c, "Tex")
        link(wp, "", c, "WP")
        link(vn, "", c, "N")
        link(tile, "", c, "Tile")
        return c

    col = tri("BaseColor", 600, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, _HLSL_RGB)
    nrm = tri("Normal", 900, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, _HLSL_NRM)
    arm = tri("ARM", 1200, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, _HLSL_RGB)

    tint = node(unreal.MaterialExpressionVectorParameter, -900, 300, parameter_name="Tint", default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    bright = node(unreal.MaterialExpressionScalarParameter, -900, 450, parameter_name="Brightness", default_value=1.0)
    m1 = node(unreal.MaterialExpressionMultiply, -500, 500)
    link(col, "", m1, "A")
    link(tint, "", m1, "B")
    m2 = node(unreal.MaterialExpressionMultiply, -300, 500)
    link(m1, "", m2, "A")
    link(bright, "", m2, "B")
    MEL.connect_material_property(m2, "", unreal.MaterialProperty.MP_BASE_COLOR)

    flat = node(unreal.MaterialExpressionScalarParameter, -900, 1050, parameter_name="NormalFlat", default_value=0.0)
    lerp = node(unreal.MaterialExpressionLinearInterpolate, -500, 900)
    link(nrm, "", lerp, "A")
    link(vn, "", lerp, "B")
    link(flat, "", lerp, "Alpha")
    norm = node(unreal.MaterialExpressionNormalize, -300, 900)
    link(lerp, "", norm, "VectorInput")
    MEL.connect_material_property(norm, "", unreal.MaterialProperty.MP_NORMAL)

    ao = node(unreal.MaterialExpressionComponentMask, -500, 1200, r=True, g=False, b=False, a=False)
    link(arm, "", ao, "")
    MEL.connect_material_property(ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    rg = node(unreal.MaterialExpressionComponentMask, -500, 1350, r=False, g=True, b=False, a=False)
    link(arm, "", rg, "")
    rmul = node(unreal.MaterialExpressionScalarParameter, -900, 1500, parameter_name="RoughMul", default_value=1.0)
    rm = node(unreal.MaterialExpressionMultiply, -300, 1350)
    link(rg, "", rm, "A")
    link(rmul, "", rm, "B")
    MEL.connect_material_property(rm, "", unreal.MaterialProperty.MP_ROUGHNESS)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat, notes


def surface(args):
    """args = {"name": 이름, "base"·"normal"·"arm": 파일 경로(없으면 기존 텍스처 사용), "tile_cm", "tint": [r, g, b], "brightness", "rough_mul", "rebuild_master"}.
    → T_<이름>_BaseColor/Normal/ARM, MI_Dungeon_<이름>. 값만 바꿀 때는 파일 경로 없이 다시 부른다."""
    mat, notes = ensure_master(bool(args.get("rebuild_master")))
    name = args["name"]
    tex = {}
    for key, suffix, kind in (("base", "BaseColor", "color"), ("normal", "Normal", "normal"), ("arm", "ARM", "mask")):
        dest = f"T_{name}_{suffix}"
        if args.get(key):
            if not os.path.isfile(args[key]):
                raise RuntimeError(f"파일 없음: {args[key]}")
            tex[suffix] = _import_tex(args[key], dest, kind)
        else:
            tex[suffix] = unreal.load_asset(f"{MAT_DIR}/{dest}")
            if tex[suffix] is None:
                raise RuntimeError(f"텍스처 없음: {MAT_DIR}/{dest}")
    mi_name = f"MI_Dungeon_{name}"
    mi_path = f"{MAT_DIR}/{mi_name}"
    mi = unreal.load_asset(mi_path) if EAL.does_asset_exist(mi_path) else None
    if mi is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(mi_name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, mat)
    for suffix, t in tex.items():
        MEL.set_material_instance_texture_parameter_value(mi, suffix, t)
    MEL.set_material_instance_scalar_parameter_value(mi, "TileCm", float(args.get("tile_cm", 200.0)))
    MEL.set_material_instance_scalar_parameter_value(mi, "Brightness", float(args.get("brightness", 1.0)))
    MEL.set_material_instance_scalar_parameter_value(mi, "RoughMul", float(args.get("rough_mul", 1.0)))
    c = args.get("tint", [1.0, 1.0, 1.0])
    MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(c[0], c[1], c[2], 1.0))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return {"material": mi_path, "master": MASTER, "notes": notes}


# ── 조립(dg_build): 정의의 "dungeon" 구획대로 구조물·소품·불빛을 놓는다. 라벨 접두사 DG_, 아웃라이너 폴더 Dungeon ──
# 좌표는 m(x = 북, y = 동, z = 바닥 높이). 보이는 메시는 모두 NoCollision이고 충돌은 상자(surfaces의 collide)와 숨긴 도형(props의 col)이 맡는다
# — 서버 내보내기가 충돌 메시의 렌더 삼각형을 그대로 쓰고, 팩 메시는 단순 충돌이 없는 것이 많아 클라이언트와 어긋나기 때문(world.dress와 같은 규칙).
_DG = "DG_"
_SHAPES = "/Engine/BasicShapes/"


def clear_build(prefix=_DG):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    n = 0
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith(prefix):
            eas.destroy_actor(a)
            n += 1
    return n


def build(name, only=None):
    """정의 world_<name>.json의 dungeon 구획을 레벨에 놓는다(기존 DG_ 액터는 지우고 다시).
    surfaces: 상자(바닥·벽·뒤판) {"label", "x", "y", "size": [x, y, z], "z": 바닥 높이, "yaw", "mat", "collide"(기본 true), "shape"(기본 Cube)}
    runs:     메시를 줄지어 {"label", "mesh" 또는 "meshes": [...], "from": [x, y], "to": [x, y], "z", "scale", "yaw_off", "gap_m", "seed"}
              — 메시의 긴 가로축을 선 방향에 맞추고 그 길이(+ gap_m) 간격으로 놓는다. 끝에서 남는 길이는 간격을 고르게 벌려 맞춘다.
    props:    메시 하나 {"label", "mesh", "x", "y", "z", "yaw", "pitch", "roll", "scale" / "width_m" / "height_m", "center"(기본 true),
              "col": {"size": [x, y, z], "shape", "off": [x, y], "z"}} — col이 있으면 숨긴 충돌 도형(윗면이 바닥 위 1m 이상이어야 함)
    lights:   점광원 {"label", "x", "y", "z", "color": [r, g, b](0~255), "intensity", "radius_m", "shadows"(기본 false), "source_cm", "fx": 파티클 경로, "fx_scale"}"""
    from jm_kit import world
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    d = world.load_def(name)
    spec = d["dungeon"]
    removed = clear_build() if only is None else 0   # only를 주면 지우지 않고 그 종류만 더 놓는다
    counts, notes = {}, []
    cache = {}

    def mesh_of(path):
        if path not in cache:
            m = unreal.load_asset(path)
            if m is None:
                raise RuntimeError(f"메시를 찾지 못함: {path}")
            b = m.get_bounds()
            cache[path] = (m, b.origin, b.box_extent)
        return cache[path]

    def put_mesh(path, label, folder, x, y, z, yaw, scale, pitch=0.0, roll=0.0, center=True):
        """메시 경계 상자의 바닥이 z(m), 가운데가 (x, y)에 오게 놓는다(pitch·roll을 주면 피벗 기준으로 놓음)."""
        m, org, ext = mesh_of(path)
        px, py, pz = x * 100.0, y * 100.0, z * 100.0
        if not pitch and not roll:
            if center:
                c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
                px -= (org.x * c - org.y * s) * scale
                py -= (org.x * s + org.y * c) * scale
            pz -= (org.z - ext.z) * scale
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(px, py, pz), unreal.Rotator(roll, pitch, yaw))
        a.static_mesh_component.set_static_mesh(m)
        a.static_mesh_component.set_collision_profile_name("NoCollision")
        a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        a.set_actor_label(_DG + label)
        a.set_folder_path("Dungeon/" + folder)
        return a

    def put_box(label, folder, shape, x, y, z, size, yaw, mat=None, collide=True, hidden=False):
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x * 100.0, y * 100.0, (z + size[2] * 0.5) * 100.0), unreal.Rotator(0, 0, yaw))
        a.static_mesh_component.set_static_mesh(unreal.load_asset(_SHAPES + shape))
        a.set_actor_scale3d(unreal.Vector(size[0], size[1], size[2]))
        if mat:
            mi = unreal.load_asset(mat)
            if mi is None:
                notes.append(f"재질 없음: {mat}")
            else:
                a.static_mesh_component.set_material(0, mi)
        if not collide:
            a.static_mesh_component.set_collision_profile_name("NoCollision")
        if hidden:
            a.static_mesh_component.set_visibility(False)
            a.set_actor_hidden_in_game(True)
        a.set_actor_label(_DG + label)
        a.set_folder_path("Dungeon/" + folder)
        return a

    def want(kind):
        return only is None or kind in only

    if want("surfaces"):
        for it in spec.get("surfaces", []):
            put_box(it["label"], "Structure", it.get("shape", "Cube"), it["x"], it["y"], float(it.get("z", 0.0)), it["size"], float(it.get("yaw", 0.0)),
                    it.get("mat"), bool(it.get("collide", True)))
            if it.get("collide", True) and 0.0 < float(it.get("z", 0.0)) + it["size"][2] < 1.0 and float(it.get("z", 0.0)) + it["size"][2] > 0.05:
                notes.append(f"충돌 윗면이 1m 미만: {it['label']}")
        counts["surfaces"] = len(spec.get("surfaces", []))

    if want("runs"):
        n = 0
        for it in spec.get("runs", []):
            rnd = random.Random(it.get("seed", 7))
            paths = it.get("meshes") or [it["mesh"]]
            (x0, y0), (x1, y1) = it["from"], it["to"]
            length = math.hypot(x1 - x0, y1 - y0)
            line_yaw = math.degrees(math.atan2(y1 - y0, x1 - x0))
            sc = float(it.get("scale", 1.0))
            # 첫 메시 길이로 칸 수를 정하고 남는 길이는 간격에 고르게 나눈다
            _, _, ext0 = mesh_of(paths[0])
            seg = 2.0 * max(ext0.x, ext0.y) * sc / 100.0 + float(it.get("gap_m", 0.0))
            cnt = max(1, int(round(length / seg)))
            step = length / cnt
            for i in range(cnt):
                p = rnd.choice(paths)
                _, _, ext = mesh_of(p)
                t = (i + 0.5) * step / length if length > 0 else 0.5
                yaw = line_yaw + (0.0 if ext.x >= ext.y else 90.0) + float(it.get("yaw_off", 0.0))
                if it.get("flip_random"):
                    yaw += rnd.choice((0.0, 180.0))
                a = put_mesh(p, f"{it['label']}_{i:02d}", "Structure", x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, float(it.get("z", 0.0)), yaw, sc)
                if it.get("stretch"):
                    # 칸이 빈틈·겹침 없이 맞닿게 긴 축만 늘이거나 줄인다(겹치면 같은 면이 겹쳐 깜빡인다). 메시 가운데가 피벗에서 벗어난 만큼 자리도 다시 맞춘다
                    k = step * 100.0 / (2.0 * max(ext.x, ext.y) * sc)
                    lx, ly = (k, 1.0) if ext.x >= ext.y else (1.0, k)
                    a.set_actor_scale3d(unreal.Vector(sc * lx, sc * ly, sc))
                    _, org, _ = mesh_of(p)
                    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
                    loc = a.get_actor_location()
                    cx, cy = (x0 + (x1 - x0) * t) * 100.0, (y0 + (y1 - y0) * t) * 100.0
                    ox, oy = org.x * sc * lx, org.y * sc * ly
                    a.set_actor_location(unreal.Vector(cx - (ox * c - oy * s), cy - (ox * s + oy * c), loc.z), False, False)
                n += 1
        counts["runs"] = n

    if want("props"):
        for it in spec.get("props", []):
            _, _, ext = mesh_of(it["mesh"])
            if it.get("width_m"):
                sc = float(it["width_m"]) * 100.0 / (2.0 * max(ext.x, ext.y))
            elif it.get("height_m"):
                sc = float(it["height_m"]) * 100.0 / (2.0 * ext.z)
            else:
                sc = float(it.get("scale", 1.0))
            z = float(it.get("z", 0.0))
            yaw = float(it.get("yaw", 0.0))
            put_mesh(it["mesh"], it["label"], "Props", it["x"], it["y"], z, yaw, sc, float(it.get("pitch", 0.0)), float(it.get("roll", 0.0)), bool(it.get("center", True)))
            col = it.get("col")
            if col:
                ox, oy = col.get("off", [0.0, 0.0])
                c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
                cz = float(col.get("z", z))
                put_box(it["label"] + "_Col", "Colliders", col.get("shape", "Cube"), it["x"] + ox * c - oy * s, it["y"] + ox * s + oy * c, cz,
                        col["size"], yaw + float(col.get("yaw", 0.0)), None, True, hidden=True)
                if cz + col["size"][2] < 1.0:
                    notes.append(f"충돌 윗면이 1m 미만: {it['label']}")
        counts["props"] = len(spec.get("props", []))

    if want("lights"):
        for it in spec.get("lights", []):
            loc = unreal.Vector(it["x"] * 100.0, it["y"] * 100.0, it["z"] * 100.0)
            a = eas.spawn_actor_from_class(unreal.PointLight, loc, unreal.Rotator(0, 0, 0))
            a.set_actor_label(_DG + it["label"])
            a.set_folder_path("Dungeon/Lights")
            a.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
            c = a.get_component_by_class(unreal.PointLightComponent)
            col = it.get("color", [255, 170, 90])
            c.set_editor_property("light_color", unreal.Color(int(col[2]), int(col[1]), int(col[0]), 255))
            c.set_editor_property("intensity", float(it.get("intensity", 8.0)))
            c.set_editor_property("attenuation_radius", float(it.get("radius_m", 12.0)) * 100.0)
            c.set_editor_property("source_radius", float(it.get("source_cm", 8.0)))
            c.set_editor_property("cast_shadows", bool(it.get("shadows", False)))
            for k, v in it.get("props", {}).items():
                try:
                    c.set_editor_property(k, v)
                except Exception as ex:
                    notes.append(f"{it['label']}.{k}: {ex}")
            if it.get("fx"):
                ps = unreal.load_asset(it["fx"])
                if ps is None:
                    notes.append(f"이펙트 없음: {it['fx']}")
                else:
                    e = eas.spawn_actor_from_object(ps, loc, unreal.Rotator(0, 0, 0))
                    e.set_actor_label(_DG + it["label"] + "_Fx")
                    e.set_folder_path("Dungeon/Lights")
                    s = float(it.get("fx_scale", 1.0))
                    e.set_actor_scale3d(unreal.Vector(s, s, s))
        counts["lights"] = len(spec.get("lights", []))

    return {"removed": removed, "placed": counts, "notes": notes}


def run(name, step, args):
    if step == "dg_surface":
        return surface(args)
    if step == "dg_build":
        return build(name, args.get("only"))
    if step == "dg_clear":
        return {"removed": clear_build()}
    raise ValueError(f"모르는 step: {step}")
