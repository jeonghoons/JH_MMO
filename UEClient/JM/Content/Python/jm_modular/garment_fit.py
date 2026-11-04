"""의상(정적 메시)을 몸에 맞추고 매니퀸 스켈레톤으로 스키닝한다 (MC2.1).

프로토타입(HANDOFF_MODULAR_EQUIPMENT_VARCO.md 부록 C·D·E)을 검증한 뒤 보완한 버전.
  1. 작은 조각 제거(열린 껍질이 부피 0으로 지워지지 않게 부피 기준 끔)
  2. (선택) 폴리 감소: 슬롯 예산까지 apply_simplify_to_triangle_count
  3. 맞춤: 몸 부위 상자 대비 비율·오프셋(체형 독립 프로파일) 또는 장갑 pair 모드(좌우 분할 → 손 본)
  4. (선택) 관통 보정: 몸 안쪽으로 얕게 들어간 정점을 몸 표면 밖으로 밀어냄
  5. 가중치 전이 후 **슬롯에 허용된 본으로만 정리** — 프로토타입은 소매 끝이 손가락 본,
     어깨 보호대가 팔뚝 본을 받아 움직일 때 찢어졌다
  6. 머티리얼 슬롯은 생성 옵션으로 전달(MC1.3 섹션 버그), 의상 머티리얼은 양면(안쪽 면이 검은 구멍으로 보임)
  7. 참조 중 에셋은 덮어쓰지 않음, verify()로 섹션·허용 본·관통·뜸 측정
"""
import json
import math
import os
import unreal

from . import body_parts

A = unreal.GeometryScript_AssetUtils
BW = unreal.GeometryScript_BoneWeights
NA = unreal.GeometryScript_NewAssetUtils
T = unreal.GeometryScript_MeshTransforms
R = unreal.GeometryScript_MeshRepair
ED = unreal.GeometryScript_MeshEdits
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
SP = unreal.GeometryScript_MeshSpatial
SIM = unreal.GeometryScript_MeshSimplification
EAL = unreal.EditorAssetLibrary

PROFILE_PATH = os.path.join(os.path.dirname(__file__), "garment_profiles.json")
# 관통 보정 기본값: 몸 안쪽 4cm 이내 정점을 표면 밖 0.3cm로 (MC2.1 측정: 보이는 부위 관통 2~8% → 1% 미만). 프로파일에서 null이면 끔
DEFAULT_PUSH = {"max_depth_cm": 4.0, "margin_cm": 0.3}

# 슬롯 기본 규칙: 허용 본 접두사, 숨기는 몸 부위(장착 시 기본값 — 아이템별 데이터가 우선), 삼각형 예산(DESIGN_ART_CONCEPT 3장)
SLOT_RULES = {
    "top":    {"bones": ("pelvis", "spine", "clavicle", "neck", "upperarm", "lowerarm"), "tris": 6000},
    "bottom": {"bones": ("pelvis", "spine_01", "thigh", "calf"), "tris": 5000},
    "boots":  {"bones": ("calf", "foot", "ball"), "tris": 3000},
    "gloves": {"bones": ("lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky"), "tris": 2000},
}


def load_profiles():
    with open(PROFILE_PATH, encoding="utf-8") as f:
        return {k: v for k, v in json.load(f).items() if not k.startswith("_")}


def save_profile(key, prof):
    with open(PROFILE_PATH, encoding="utf-8") as f:
        data = json.load(f)
    data[key] = prof
    with open(PROFILE_PATH, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")


# ---------------------------------------------------------------- 몸 정보
def _body(body):
    """체형 key → (원본 메시, 몸 부위 폴더/접두사, DynamicMesh, 본 정보)"""
    bp = body_parts.load_profile(body)
    sm = unreal.load_asset(bp["source_mesh"])
    dm = body_parts._copy_lod(sm, 0)
    dm, infos = BW.get_all_bones_info(dm)
    return sm, bp, dm, infos


def region_box(body, parts, z_clip=None):
    """몸 부위 에셋들의 기준 자세 경계 상자 합집합 → (lo, hi)"""
    bp = body_parts.load_profile(body)
    lo, hi = [1e9] * 3, [-1e9] * 3
    for g in parts:
        bb = unreal.load_asset(f"{bp['out_dir']}/{bp['asset_prefix']}_{g.capitalize()}").get_bounds()
        for i, (oc, ec) in enumerate(zip((bb.origin.x, bb.origin.y, bb.origin.z), (bb.box_extent.x, bb.box_extent.y, bb.box_extent.z))):
            lo[i] = min(lo[i], oc - ec)
            hi[i] = max(hi[i], oc + ec)
    if z_clip:
        lo[2], hi[2] = z_clip
    return lo, hi


def _raw_size(static_path):
    bb = unreal.load_asset(static_path).get_bounds()
    return [bb.box_extent.x * 2, bb.box_extent.y * 2, bb.box_extent.z * 2]


def calibrate(source, body, parts, scale, center, z_clip=None):
    """한 체형에서 손으로 맞춘 스케일·중심을 '몸 부위 상자 대비 비율'로 바꾼다(다른 체형에 그대로 적용)."""
    lo, hi = region_box(body, parts, z_clip)
    size = [h - l for l, h in zip(lo, hi)]
    ctr = [(h + l) / 2 for l, h in zip(lo, hi)]
    raw = _raw_size(source)
    return {"parts": list(parts), "z_clip": z_clip,
            "ratio": [raw[i] * scale[i] / size[i] for i in range(3)],
            "rel_off": [(center[i] - ctr[i]) / size[i] for i in range(3)]}


def fit_transform(source, body, fit):
    lo, hi = region_box(body, fit["parts"], fit.get("z_clip"))
    size = [h - l for l, h in zip(lo, hi)]
    ctr = [(h + l) / 2 for l, h in zip(lo, hi)]
    raw = _raw_size(source)
    return ([fit["ratio"][i] * size[i] / raw[i] for i in range(3)],
            [ctr[i] + fit["rel_off"][i] * size[i] for i in range(3)])


# ---------------------------------------------------------------- 메시 처리
def _copy_static(path):
    dm = unreal.DynamicMesh()
    dm, _ = A.copy_mesh_from_static_mesh(unreal.load_asset(path), dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    return dm


def _tri_count(dm):
    return len(body_parts._valid_tris(dm))


def _remove_small(dm, min_tris):
    before = _tri_count(dm)
    ro = unreal.GeometryScriptRemoveSmallComponentOptions()
    ro.min_triangle_count = min_tris
    ro.min_volume = -1.0e12   # 열린 껍질(옷)은 부피가 0 이하 → 기본값이면 통째로 지워짐
    ro.min_area = 0.0
    dm = R.remove_small_components(dm, ro)
    return dm, before - _tri_count(dm)


def _simplify(dm, target):
    if not target or _tri_count(dm) <= target:
        return dm
    opt = unreal.GeometryScriptSimplifyMeshOptions()
    return SIM.apply_simplify_to_triangle_count(dm, target, opt)


def _place_fit(dm, scale, center, yaw):
    dm = T.scale_mesh(dm, unreal.Vector(*scale), unreal.Vector(0, 0, 0), True)
    dm = T.rotate_mesh(dm, unreal.Rotator(roll=0, pitch=0, yaw=yaw), unreal.Vector(0, 0, 0))
    return T.translate_mesh(dm, unreal.Vector(*center))


def _place_pair(src_dm_fn, bones, pair):
    """한 쌍(장갑 등) 메시를 x 부호로 좌우 분할해 각 손 본 위치·방향으로 옮긴다(프로토타입 gloves.py)."""
    src = src_dm_fn()
    src, vids, _ = Q.get_all_vertex_i_ds(src)
    pos = {}
    for v in L.convert_index_list_to_array(vids):
        p, _ = Q.get_vertex_position(src, v)
        pos[v] = (p.x, p.y, p.z)
    src, tl, _ = Q.get_all_triangle_indices(src, False)
    tris = L.convert_triangle_list_to_array(tl)
    result = unreal.DynamicMesh()
    for side, sign, bname in (("right", -1, "hand_r"), ("left", 1, "hand_l")):
        half = src_dm_fn()
        kill, keep = [], []
        for tid, t in enumerate(tris):
            if t.x < 0:
                continue
            cx = (pos[t.x][0] + pos[t.y][0] + pos[t.z][0]) / 3.0
            if cx * sign > 0:
                keep += [pos[t.x], pos[t.y], pos[t.z]]
            else:
                kill.append(tid)
        half, _n = ED.delete_triangles_from_mesh(half, L.convert_array_to_index_list(kill, unreal.GeometryScriptIndexType.TRIANGLE), False)
        c = [sum(p[i] for p in keep) / len(keep) for i in range(3)]
        half = T.translate_mesh(half, unreal.Vector(-c[0], -c[1], -c[2]))
        s = pair["scale"]
        half = T.scale_mesh(half, unreal.Vector(s, s, s), unreal.Vector(0, 0, 0), True)
        phi = pair["pitch"] if side == "right" else -pair["pitch"]
        rot = unreal.Rotator(roll=0, pitch=phi, yaw=0)
        half = T.rotate_mesh(half, rot, unreal.Vector(0, 0, 0))
        d = unreal.MathLibrary.quat_rotate_vector(rot.quaternion(), unreal.Vector(0, 0, 1))
        hb = bones[bname]
        half = T.translate_mesh(half, unreal.Vector(hb.x + d.x * pair["reach"], hb.y + d.y * pair["reach"] + pair.get("y_off", 0.0), hb.z + d.z * pair["reach"] + pair.get("z_off", 0.0)))
        result = ED.append_mesh(result, half, unreal.Transform())
    return result


def _push_out(dm, body_dm, bvh, max_depth, margin):
    """몸 안쪽 max_depth 이내로 들어간 정점을 몸 표면 밖 margin까지 밀어낸다. 반환: (dm, 밀어낸 수)"""
    opt = unreal.GeometryScriptSpatialQueryOptions()
    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    moved = 0
    for v in L.convert_index_list_to_array(vids):
        p, _ = Q.get_vertex_position(dm, v)
        _m, inside, _o = SP.is_point_inside_mesh(body_dm, bvh, p, opt)
        if not inside:
            continue
        _m, hit, _o = SP.find_nearest_point_on_mesh(body_dm, bvh, p, opt)
        q = hit.position
        d = math.sqrt((q.x - p.x) ** 2 + (q.y - p.y) ** 2 + (q.z - p.z) ** 2)
        if d < 1e-4 or d > max_depth:
            continue
        k = (d + margin) / d
        dm, _ok = ED.set_vertex_position(dm, v, unreal.Vector(p.x + (q.x - p.x) * k, p.y + (q.y - p.y) * k, p.z + (q.z - p.z) * k), True)
        moved += 1
    return dm, moved


def _restrict_bones(dm, infos, allowed):
    """허용되지 않은 본의 가중치를 허용된 조상 본(없으면 정점에서 가장 가까운 허용 본)으로 옮긴다."""
    name = {i.index: str(i.name) for i in infos}
    parent = {i.index: i.parent_index for i in infos}
    wpos = {i.index: i.world_transform.translation for i in infos}
    ok = {i for i, n in name.items() if n.lower().startswith(allowed)}

    def target(b, p):
        a = parent.get(b, -1)
        while a is not None and a >= 0:
            if a in ok:
                return a
            a = parent.get(a, -1)
        return min(ok, key=lambda i: (wpos[i].x - p.x) ** 2 + (wpos[i].y - p.y) ** 2 + (wpos[i].z - p.z) ** 2)

    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    changed = 0
    for v in L.convert_index_list_to_array(vids):
        dm, ws, valid = BW.get_vertex_bone_weights(dm, v)
        if not valid or all(w.bone_index in ok for w in ws if w.weight > 0.0):
            continue
        p, _ = Q.get_vertex_position(dm, v)
        acc = {}
        for w in ws:
            if w.weight <= 0.0:
                continue
            b = w.bone_index if w.bone_index in ok else target(w.bone_index, p)
            acc[b] = acc.get(b, 0.0) + w.weight
        total = sum(acc.values()) or 1.0
        out = []
        for b, wt in sorted(acc.items(), key=lambda x: -x[1]):
            bw = unreal.GeometryScriptBoneWeight()
            bw.bone_index = b
            bw.weight = wt / total
            out.append(bw)
        dm, _ok = BW.set_vertex_bone_weights(dm, v, out)
        changed += 1
    return dm, changed


def _make_two_sided(material):
    """임포트된 의상 머티리얼을 양면으로. Material 에셋일 때만(인스턴스는 부모에서 설정)."""
    if isinstance(material, unreal.Material) and not material.get_editor_property("two_sided"):
        material.set_editor_property("two_sided", True)
        unreal.MaterialEditingLibrary.recompile_material(material)
        EAL.save_loaded_asset(material)
        return True
    return False


# ---------------------------------------------------------------- 생성
def build(key, body, out_path=None, allow_refs=(), prof=None):
    """garment_profiles.json의 key(또는 prof dict)를 체형 body에 맞춰 스켈레탈 메시로 만든다. 반환: 보고서."""
    prof = prof or load_profiles()[key]
    slot = prof["slot"]
    rule = SLOT_RULES[slot]
    source = prof["source"]
    out_path = out_path or prof["out"].format(body=body)
    body_sm, _bp, body_dm, infos = _body(body)
    bones = {str(i.name): i.world_transform.translation for i in infos}
    report = {"key": key, "body": body, "out": out_path}

    def src_dm():
        dm, removed = _remove_small(_copy_static(source), prof.get("min_component_tris", 50))
        report["removed_small_tris"] = removed
        dm = _simplify(dm, prof.get("target_tris", rule["tris"]) if prof.get("simplify", True) else 0)
        return dm

    if prof.get("pair"):
        dm = _place_pair(src_dm, bones, prof["pair"])
    else:
        scale, center = fit_transform(source, body, prof["fit"])
        dm = _place_fit(src_dm(), scale, center, prof.get("yaw", 0.0))
        report["scale"], report["center"] = [round(s, 3) for s in scale], [round(c, 1) for c in center]
    report["tris"] = _tri_count(dm)

    push = prof.get("push_out", DEFAULT_PUSH)
    if push:
        body_dm, bvh = SP.build_bvh_for_mesh(body_dm)
        dm, report["pushed_verts"] = _push_out(dm, body_dm, bvh, push["max_depth_cm"], push["margin_cm"])

    wopt = unreal.GeometryScriptTransferBoneWeightsOptions()
    method = prof.get("weights", {}).get("method", "closest")
    wopt.transfer_method = unreal.TransferBoneWeightsMethod.INPAINT_WEIGHTS if method == "inpaint" else unreal.TransferBoneWeightsMethod.CLOSEST_POINT_ON_SURFACE
    wopt.output_target_mesh_bones = unreal.OutputTargetMeshBones.SOURCE_BONES
    if method == "inpaint":
        wopt.num_smoothing_iterations = prof["weights"].get("smooth_iters", 0)
        wopt.smoothing_strength = prof["weights"].get("smooth_strength", 0.0)
    dm = BW.transfer_bone_weights_from_mesh(body_dm, dm, wopt)
    allowed = tuple(prof.get("allowed_bones", rule["bones"]))
    dm, report["restricted_verts"] = _restrict_bones(dm, infos, allowed)

    st = unreal.load_asset(source)
    mats = unreal.Map(unreal.Name, unreal.MaterialInterface)
    for s in st.static_materials:
        mats[s.material_slot_name] = s.material_interface
        if prof.get("two_sided", True) and _make_two_sided(s.material_interface):
            report.setdefault("two_sided", []).append(s.material_interface.get_name())
    copt = unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions()
    copt.set_editor_property("materials", mats)
    body_parts._check_overwrite(out_path, allow_refs)
    EAL.make_directory(out_path.rsplit("/", 1)[0])
    sk, outcome = NA.create_new_skeletal_mesh_asset_from_mesh(dm, body_sm.skeleton, out_path, copt)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS or sk is None:
        raise RuntimeError(f"에셋 생성 실패: {out_path}")
    sk.set_editor_property("physics_asset", None)
    EAL.save_loaded_asset(sk)
    return report


# ---------------------------------------------------------------- 검사
def measure(item_path, body, slot, visible_parts=None, float_cm=3.0, step=1):
    """수치 검사. visible_parts를 주면 그 몸 부위들(장착 시 보이는 부위)에 대한 관통만 센다."""
    bp = body_parts.load_profile(body)
    sk = unreal.load_asset(item_path)
    if visible_parts:
        ref = unreal.DynamicMesh()
        for g in visible_parts:
            ref = ED.append_mesh(ref, body_parts._copy_lod(unreal.load_asset(f"{bp['out_dir']}/{bp['asset_prefix']}_{g.capitalize()}"), 0), unreal.Transform())
    else:
        ref = body_parts._copy_lod(unreal.load_asset(bp["source_mesh"]), 0)
    ref, bvh = SP.build_bvh_for_mesh(ref)
    full = body_parts._copy_lod(unreal.load_asset(bp["source_mesh"]), 0)
    full, fbvh = SP.build_bvh_for_mesh(full)
    dm = body_parts._copy_lod(sk, 0)
    dm, infos = BW.get_all_bones_info(dm)
    names = {i.index: str(i.name) for i in infos}
    allowed = SLOT_RULES[slot]["bones"]
    opt = unreal.GeometryScriptSpatialQueryOptions()
    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    vids = list(L.convert_index_list_to_array(vids))[::step]
    inside = floating = foreign = 0
    dists = []
    for v in vids:
        p, _ = Q.get_vertex_position(dm, v)
        _m, ins, _o = SP.is_point_inside_mesh(ref, bvh, p, opt)
        inside += 1 if ins else 0
        _m, fin, _o = SP.is_point_inside_mesh(full, fbvh, p, opt)
        _m, hit, _o = SP.find_nearest_point_on_mesh(full, fbvh, p, opt)
        d = math.sqrt((hit.position.x - p.x) ** 2 + (hit.position.y - p.y) ** 2 + (hit.position.z - p.z) ** 2)
        dists.append(d)
        floating += 1 if (not fin and d > float_cm) else 0
        dm, ws, _ = BW.get_vertex_bone_weights(dm, v)
        foreign += 1 if any(w.weight > 0.0 and not names.get(w.bone_index, "").lower().startswith(allowed) for w in ws) else 0
    dists.sort()
    n = len(vids)
    sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    return {"verts": n, "tris": _tri_count(dm),
            ("inside_visible%" if visible_parts else "inside%"): round(100.0 * inside / n, 1),
            f"float>{float_cm:g}cm%": round(100.0 * floating / n, 1),
            "dist_p50": round(dists[n // 2], 2), "dist_p95": round(dists[int(n * 0.95)], 2),
            "foreign_bone_verts": foreign,
            "sections": [sub.get_lod_material_slot(sk, 0, i) for i in range(sub.get_num_sections(sk, 0))],
            "skeleton_ok": sk.skeleton == unreal.load_asset(bp["source_mesh"]).skeleton}
