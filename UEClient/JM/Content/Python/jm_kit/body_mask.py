"""몸 마스킹 (MC2.2 보완, 2026-11-05).

장비가 덮는 몸 영역만 머티리얼에서 지운다(부위 통째 숨김 대신). 옷 끝 근처의 몸은 남겨 소매·밑단과 손목·발목 사이에
틈이 생겨도 빈 공간이 아니라 피부가 보이게 한다. 키트 메시·머티리얼 원본은 고치지 않는다.

- 마스크 텍스처 T_BodyMask_<M|F> (몸 텍스처 UV 공간, 1024²)
    R = 영역 ID × 40 (0 없음, 1 몸통, 2 팔, 3 다리, 4 발, 5 손, 6 엉덩이(pelvis))   G = 팔·다리 끝(손목·발목)에서 축을 따라 잰 거리 cm (0~100 → 0~255)
- M_Body_Masked = 키트 M_CoreC_Comic 사본 + OpacityMask(블렌드 Masked)
    숨김 = 몸통×HideTorso + 팔×HideArms×(거리 > ArmKeepCm) + (다리×(거리 > LegKeepCm) + 엉덩이)×HideLegs + 발×HideFeet + 손×HideHands
- 몸 = 키트 Body_Combined 한 메시(2026-11-05) — 마스크도 이 메시의 몸 섹션에서 만든다
- MI_Body_<M|F> = 키트 MI_CoreC_Base_<M|F>_Body 사본(부모만 M_Body_Masked, BodyMask 지정) — 외형 파라미터는 키트 값 그대로

예)
  import importlib, jm_kit.body_mask as bm; importlib.reload(bm)
  bm.build_mask("M"); bm.build_mask("F"); bm.build_material()
"""
import math
import os
import struct
import zlib

import unreal

from . import garment, profiles, skeleton_check

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
BW = unreal.GeometryScript_BoneWeights
MAT = unreal.GeometryScript_Materials

SIZE = 1024
REGION = {"torso": 1, "arm": 2, "leg": 3, "foot": 4, "hand": 5, "hip": 6}
R_SCALE = 40
DIST_MAX_CM = 100.0
DILATE_PX = 3
MASK_DIR = "/Game/Character/Body/Masks"
MAT_DIR = "/Game/Character/Body/Materials"
MASTER_SRC = "/Game/CoreC/01CoreCFiles/MasterMaterials/M_CoreC_Comic"
MASTER = f"{MAT_DIR}/M_Body_Masked"
SRC_PNG_DIR = os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "..", "..", "SourceArt", "Masks"))
PARAMS = ("HideTorso", "HideArms", "ArmKeepCm", "HideLegs", "LegKeepCm", "HideFeet", "HideHands")
ARM = {"l": ("upperarm_l", "lowerarm_l", "hand_l"), "r": ("upperarm_r", "lowerarm_r", "hand_r")}
LEG = {"l": ("thigh_l", "calf_l", "foot_l"), "r": ("thigh_r", "calf_r", "foot_r")}


def _region_of(bone):
    b = bone.lower()
    if b.startswith(("upperarm", "lowerarm")):
        return "arm"
    if b.startswith(("hand", "thumb", "index", "middle", "ring", "pinky")):
        return "hand"
    if b.startswith(("thigh", "calf")):
        return "leg"
    if b.startswith(("foot", "ball")):
        return "foot"
    if b.startswith("pelvis"):
        return "hip"     # 엉덩이·사타구니 — 하의가 덮음(상의만 입으면 보여야 함)
    return "torso"   # spine·clavicle·neck·head·root


def _body_slot_index(sk):
    for i, m in enumerate(sk.materials):
        if "body" in str(m.material_slot_name).lower():
            return i
    return 0


def _vertex_data(mesh_path, chains, dm=None):
    """정점별 (영역, 끝에서 거리 cm). 거리 = 같은 쪽 팔·다리 축에서 total - t (몸통·발은 DIST_MAX_CM).
    dm을 주면 에셋 대신 그 메시를 쓴다(예: 단순화한 몸 — garment._kit_hand_mesh)."""
    dm = dm if dm is not None else skeleton_check._dm(mesh_path)
    dm, infos = BW.get_all_bones_info(dm)
    names = {i.index: str(i.name) for i in infos}
    pos = {str(i.name): i.world_transform.translation for i in infos}
    segs = {k: garment._chain(pos, v) for k, v in chains.items()}
    data = {}
    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    for v in L.convert_index_list_to_array(vids):
        dm, ws, _ = BW.get_vertex_bone_weights(dm, v)
        top = max(ws, key=lambda w: w.weight) if ws else None
        bone = names.get(top.bone_index, "") if top else ""
        region = _region_of(bone)
        # 손·발은 손목·발목 너머라 거리 0. (100으로 두면 손목을 걸친 '팔' 삼각형이 보간으로 거리가 커져 숨겨진다 — 손목 구멍, 2026-11-05)
        dist = 0.0 if region in ("hand", "foot") else DIST_MAX_CM
        if region in ("arm", "leg"):
            side = "l" if bone.lower().endswith("_l") or "_l_" in bone.lower() or bone.lower().endswith("_l_00") else "r"
            key = f"{region}_{side}"
            if key in segs:
                sg, total = segs[key]
                p, _ = Q.get_vertex_position(dm, v)
                h = garment._on_chain(garment._v(p), sg, total, extend=10.0)
                if h:
                    dist = max(0.0, min(DIST_MAX_CM, total - h[0]))
        data[v] = (region, dist)
    return dm, data


CONSERVATIVE_PX = 1.5   # 삼각형 가장자리 바깥 이 거리(픽셀)까지 그 섬이 소유 — Nearest 샘플링이 섬 경계 텍셀에서 이웃 섬을 집지 않게


def _raster(tris, img_r, img_g, overlap):
    """tris: [((u,v)×3, 영역 ID, (거리×3))]. 겹침(다른 영역이 이미 칠한 픽셀) 수를 overlap[0]에 더한다.
    1차: 픽셀 중심이 삼각형 안 → 소유. 2차(보수적): 가장자리 CONSERVATIVE_PX 안의 빈 픽셀을 채운다.
    2차에서 손 영역은 다른 영역이 2차로 채운 픽셀도 덮는다(손목 근처에 숨김 영역이 새어 구멍이 나던 문제, 2026-11-05)."""
    strict = bytearray(SIZE * SIZE)
    hand = REGION["hand"]
    for conservative in (False, True):
        order = sorted(tris, key=lambda t: t[1] == hand) if conservative else tris
        for (a, b, c), rid, (da, db, dc) in order:
            ax, ay = a[0] * SIZE, a[1] * SIZE
            bx, by = b[0] * SIZE, b[1] * SIZE
            cx, cy = c[0] * SIZE, c[1] * SIZE
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-12:
                continue
            pad = CONSERVATIVE_PX + 1 if conservative else 1
            x0, x1 = max(0, int(min(ax, bx, cx) - pad)), min(SIZE - 1, int(max(ax, bx, cx) + pad))
            y0, y1 = max(0, int(min(ay, by, cy) - pad)), min(SIZE - 1, int(max(ay, by, cy) + pad))
            area2 = abs(den)
            h = (area2 / (math.hypot(bx - cx, by - cy) or 1.0), area2 / (math.hypot(cx - ax, cy - ay) or 1.0),
                 area2 / (math.hypot(ax - bx, ay - by) or 1.0))   # 각 꼭짓점의 맞은편 변까지 높이(픽셀)
            r = rid * R_SCALE
            for y in range(y0, y1 + 1):
                py = y + 0.5
                row = y * SIZE
                for x in range(x0, x1 + 1):
                    px = x + 0.5
                    w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
                    w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
                    w2 = 1.0 - w0 - w1
                    i = row + x
                    if not conservative:
                        if w0 < -1e-4 or w1 < -1e-4 or w2 < -1e-4:
                            continue
                        if img_r[i] and img_r[i] != r:
                            overlap[0] += 1
                        strict[i] = 1
                    else:
                        if strict[i] or min(w0 * h[0], w1 * h[1], w2 * h[2]) < -CONSERVATIVE_PX:
                            continue
                        if img_r[i] and not (rid == hand and img_r[i] != r):
                            continue
                        w0, w1, w2 = max(w0, 0.0), max(w1, 0.0), max(w2, 0.0)
                        s = (w0 + w1 + w2) or 1.0
                        w0, w1, w2 = w0 / s, w1 / s, w2 / s
                    img_r[i] = r
                    img_g[i] = int(min(255, max(0, (w0 * da + w1 * db + w2 * dc) / DIST_MAX_CM * 255)))


def _dilate(img_r, img_g, n):
    """UV 섬 가장자리 바깥으로 n픽셀 번지게(샘플링 시 빈 픽셀 = 숨김 안 됨이 경계에 새지 않게)."""
    for _ in range(n):
        src_r, src_g = bytes(img_r), bytes(img_g)
        for y in range(SIZE):
            row = y * SIZE
            for x in range(SIZE):
                i = row + x
                if src_r[i]:
                    continue
                for j in (i - 1 if x else -1, i + 1 if x < SIZE - 1 else -1, i - SIZE, i + SIZE):
                    if 0 <= j < SIZE * SIZE and src_r[j]:
                        img_r[i], img_g[i] = src_r[j], src_g[j]
                        break


def _write_png(path, img_r, img_g):
    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)
        row = y * SIZE
        for x in range(SIZE):
            raw += bytes((img_r[row + x], img_g[row + x], 0))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def build_mask(body):
    """체형 body의 마스크 텍스처를 만든다(키트 Body_Combined LOD0의 몸 섹션 삼각형). 반환: 보고서."""
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중 — 멈춘 뒤 실행")
    bp = profiles.body(body)
    chains = {f"arm_{s}": v for s, v in ARM.items()}
    chains.update({f"leg_{s}": v for s, v in LEG.items()})
    img_r, img_g = bytearray(SIZE * SIZE), bytearray(SIZE * SIZE)
    overlap = [0]
    rep = {"body": body, "tris": {}}
    for part, path in (("body", bp["leader"]),):
        sk = unreal.load_asset(path)
        body_mid = _body_slot_index(sk)
        dm, vdata = _vertex_data(path, chains)
        dm, tids, _ = Q.get_all_triangle_i_ds(dm)
        tris = []
        for t in L.convert_index_list_to_array(tids):
            mid, _ok = MAT.get_triangle_material_id(dm, t)
            if mid != body_mid:
                continue
            idx, _ = Q.get_triangle_indices(dm, t)
            uv = Q.get_triangle_u_vs(dm, 0, t)
            vs = (idx.x, idx.y, idx.z)
            regs = [vdata[v][0] for v in vs]
            region = max(set(regs), key=regs.count)
            tris.append((((uv[0].x, uv[0].y), (uv[1].x, uv[1].y), (uv[2].x, uv[2].y)), REGION[region], tuple(vdata[v][1] for v in vs)))
        rep["tris"][part] = len(tris)
        _raster(tris, img_r, img_g, overlap)
    rep["overlap_px"] = overlap[0]
    counts = {}
    for r in img_r:
        if r:
            counts[r // R_SCALE] = counts.get(r // R_SCALE, 0) + 1
    rep["region_px"] = {k: counts.get(v, 0) for k, v in REGION.items()}
    _dilate(img_r, img_g, DILATE_PX)
    png = os.path.join(SRC_PNG_DIR, f"T_BodyMask_{body}.png")
    _write_png(png, img_r, img_g)
    rep["png"] = png
    rep["texture"] = _import_mask(png)
    return rep


def _import_mask(png):
    t = unreal.AssetImportTask()
    t.filename = png
    t.destination_path = MASK_DIR
    t.automated = True
    t.save = True
    t.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    path = f"{MASK_DIR}/{os.path.splitext(os.path.basename(png))[0]}"
    tex = unreal.load_asset(path)
    if tex is None:
        raise RuntimeError(f"마스크 임포트 실패: {png}")
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
    EAL.save_loaded_asset(tex)
    return path


# ---------------------------------------------------------------- 머티리얼
def _scalar(m, name, x, y, default=0.0):
    e = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    e.set_editor_property("group", "BodyMask")
    return e


def _const(m, v, x, y):
    e = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", v)
    return e


def _op(m, cls, a, b, x, y, pa="A", pb="B"):
    e = MEL.create_material_expression(m, cls, x, y)
    MEL.connect_material_expressions(a, "", e, pa)
    if b is not None:
        MEL.connect_material_expressions(b, "", e, pb)
    return e


def _is_region(m, rr, rid, x, y):
    """saturate(1 - |R·255/R_SCALE - rid|·2) — 영역 ID가 rid면 1."""
    d = _op(m, unreal.MaterialExpressionSubtract, rr, _const(m, float(rid), x - 150, y + 40), x, y)
    ab = MEL.create_material_expression(m, unreal.MaterialExpressionAbs, x + 120, y)
    MEL.connect_material_expressions(d, "", ab, "")
    mul = _op(m, unreal.MaterialExpressionMultiply, ab, _const(m, 2.0, x + 120, y + 60), x + 240, y)
    one = _op(m, unreal.MaterialExpressionOneMinus, mul, None, x + 360, y, pa="")
    sat = MEL.create_material_expression(m, unreal.MaterialExpressionSaturate, x + 480, y)
    MEL.connect_material_expressions(one, "", sat, "")
    return sat


def _beyond(m, dist, keep, x, y):
    """saturate((거리 - keep) × 10) — 거리 > keep이면 1."""
    d = _op(m, unreal.MaterialExpressionSubtract, dist, keep, x, y)
    mul = _op(m, unreal.MaterialExpressionMultiply, d, _const(m, 10.0, x, y + 60), x + 120, y)
    sat = MEL.create_material_expression(m, unreal.MaterialExpressionSaturate, x + 240, y)
    MEL.connect_material_expressions(mul, "", sat, "")
    return sat


def build_material():
    """M_Body_Masked(키트 마스터 사본 + 마스크)와 MI_Body_<M|F>를 만든다. 반환: 경로 목록."""
    EAL.make_directory(MAT_DIR)
    if EAL.does_asset_exist(MASTER):
        EAL.delete_asset(MASTER)
    m = EAL.duplicate_asset(MASTER_SRC, MASTER)
    X, Y = -2400, 1400
    tex = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, X, Y)
    tex.set_editor_property("parameter_name", "BodyMask")
    tex.set_editor_property("group", "BodyMask")
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    tex.set_editor_property("texture", unreal.load_asset(f"{MASK_DIR}/T_BodyMask_M"))
    mask_r = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, X + 250, Y)
    mask_r.set_editor_property("r", True); mask_r.set_editor_property("g", False); mask_r.set_editor_property("b", False); mask_r.set_editor_property("a", False)
    MEL.connect_material_expressions(tex, "RGB", mask_r, "")
    mask_g = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, X + 250, Y + 200)
    mask_g.set_editor_property("r", False); mask_g.set_editor_property("g", True); mask_g.set_editor_property("b", False); mask_g.set_editor_property("a", False)
    MEL.connect_material_expressions(tex, "RGB", mask_g, "")
    rid = _op(m, unreal.MaterialExpressionMultiply, mask_r, _const(m, 255.0 / R_SCALE, X + 250, Y + 80), X + 450, Y)
    dist = _op(m, unreal.MaterialExpressionMultiply, mask_g, _const(m, DIST_MAX_CM, X + 250, Y + 280), X + 450, Y + 200)
    p = {n: _scalar(m, n, X + 450, Y + 400 + i * 70, 8.0 if n.endswith("KeepCm") else 0.0) for i, n in enumerate(PARAMS)}
    torso = _op(m, unreal.MaterialExpressionMultiply, _is_region(m, rid, REGION["torso"], X + 700, Y - 200), p["HideTorso"], X + 1300, Y - 200)
    arm = _op(m, unreal.MaterialExpressionMultiply, _is_region(m, rid, REGION["arm"], X + 700, Y), p["HideArms"], X + 1300, Y)
    arm = _op(m, unreal.MaterialExpressionMultiply, arm, _beyond(m, dist, p["ArmKeepCm"], X + 1000, Y + 120), X + 1450, Y)
    leg = _op(m, unreal.MaterialExpressionMultiply, _is_region(m, rid, REGION["leg"], X + 700, Y + 300), p["HideLegs"], X + 1300, Y + 300)
    leg = _op(m, unreal.MaterialExpressionMultiply, leg, _beyond(m, dist, p["LegKeepCm"], X + 1000, Y + 420), X + 1450, Y + 300)
    hip = _op(m, unreal.MaterialExpressionMultiply, _is_region(m, rid, REGION["hip"], X + 700, Y + 850), p["HideLegs"], X + 1300, Y + 850)
    leg = _op(m, unreal.MaterialExpressionAdd, leg, hip, X + 1550, Y + 400)
    foot = _op(m, unreal.MaterialExpressionMultiply, _is_region(m, rid, REGION["foot"], X + 700, Y + 600), p["HideFeet"], X + 1300, Y + 600)
    s = _op(m, unreal.MaterialExpressionAdd, torso, arm, X + 1600, Y)
    s = _op(m, unreal.MaterialExpressionAdd, s, leg, X + 1700, Y + 100)
    s = _op(m, unreal.MaterialExpressionAdd, s, foot, X + 1800, Y + 200)
    hand = _op(m, unreal.MaterialExpressionMultiply, _is_region(m, rid, REGION["hand"], X + 700, Y + 1100), p["HideHands"], X + 1300, Y + 1100)
    s = _op(m, unreal.MaterialExpressionAdd, s, hand, X + 1850, Y + 300)
    sat = MEL.create_material_expression(m, unreal.MaterialExpressionSaturate, X + 1900, Y + 200)
    MEL.connect_material_expressions(s, "", sat, "")
    vis = _op(m, unreal.MaterialExpressionOneMinus, sat, None, X + 2000, Y + 200, pa="")
    MEL.connect_material_property(vis, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("opacity_mask_clip_value", 0.5)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    out = [MASTER]
    for b in ("M", "F"):
        src = f"/Game/CoreC/02BaseBody/Materials/MI_CoreC_Base_{b}_Body"
        dst = f"{MAT_DIR}/MI_Body_{b}"
        if EAL.does_asset_exist(dst):
            EAL.delete_asset(dst)
        mi = EAL.duplicate_asset(src, dst)
        MEL.set_material_instance_parent(mi, m)
        MEL.set_material_instance_texture_parameter_value(mi, "BodyMask", unreal.load_asset(f"{MASK_DIR}/T_BodyMask_{b}"))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        out.append(dst)
    return out


def apply(component, body, params):
    """몸 부위 컴포넌트의 몸 머티리얼 슬롯에 MI_Body_<body> 기반 MID를 씌우고 마스크 파라미터를 넣는다. 반환: MID."""
    sk = component.get_skeletal_mesh_asset()
    idx = _body_slot_index(sk)
    mid = component.create_dynamic_material_instance(idx, unreal.load_asset(f"{MAT_DIR}/MI_Body_{body}"))
    for n in PARAMS:
        mid.set_scalar_parameter_value(n, float(params.get(n, 8.0 if n.endswith("KeepCm") else 0.0)))
    return mid
