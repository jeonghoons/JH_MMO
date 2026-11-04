"""의상 맞춤·스키닝 (MC2.1 재작성).

원칙: **키트 기본 의상(틀)을 복제해 형상만 새 메시로 바꾼다.** 뼈대·바인드 포즈·물리 에셋은 틀에서 그대로 물려받으므로
Leader Pose에서 몸과 벌어질 수 없다(이전 도구는 새 스켈레탈 메시를 만들다 프로젝트 SK_Mannequin 바인드로 묶여 실패 — DECISIONS 2026-11-04).

순서
  1. 원본(정적 메시, 예: VARCO FBX 임포트)을 복사, (선택) 슬롯 예산까지 단순화
  2. 틀 상자에 맞춤: 크기 = ratio × 틀 크기, 중심 = 틀 중심 + rel_off × 틀 크기 (EditImage로 틀 실루엣을 유지했다면 ratio 1, rel_off 0)
  3. 몸 안쪽으로 얕게 들어간 정점을 몸 밖으로(관통 보정)
  4. 가중치: 틀 표면 tpl_max_cm 안의 정점 = 틀 가중치, 먼 정점 = 몸(리더) 가중치. 틀과 리더는 본 89개·순서가 같다(MC1.3)
  5. 슬롯 허용 본으로 정리(허용 안 된 본 → 허용된 조상 본)
  6. 틀 에셋을 out_path로 복제 → copy_mesh_to_skeletal_mesh로 LOD0 형상·가중치·머티리얼만 교체
  7. skeleton_check.check 통과 여부를 보고서에 넣는다

예)
  import importlib, jm_kit.garment as g; importlib.reload(g)
  src = g.import_fbx("C:/.../top.fbx", "/Game/Dev/MC/Source/top")
  g.build(src, "M", "top", "/Game/Dev/MC/Fit/SK_TestTop_M")
"""
import math

import unreal

from . import profiles, skeleton_check

A = unreal.GeometryScript_AssetUtils
BW = unreal.GeometryScript_BoneWeights
T = unreal.GeometryScript_MeshTransforms
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
SP = unreal.GeometryScript_MeshSpatial
ED = unreal.GeometryScript_MeshEdits
SIM = unreal.GeometryScript_MeshSimplification
EAL = unreal.EditorAssetLibrary

# 슬롯 규칙: 허용 본 접두사, 삼각형 예산(DESIGN_ART_CONCEPT 3장). 틀이 없는 슬롯(장갑)은 리더 기준(MC2.2 장갑에서 보완)
SLOT_RULES = {
    "top":    {"bones": ("pelvis", "spine", "clavicle", "neck", "upperarm", "lowerarm"), "tris": 6000},
    "bottom": {"bones": ("pelvis", "spine_01", "thigh", "calf"), "tris": 5000},
    "boots":  {"bones": ("calf", "foot", "ball"), "tris": 3000},
    "gloves": {"bones": ("lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky"), "tris": 2000},
}


# ---------------------------------------------------------------- 입출력
def import_fbx(fbx_path, dest_dir, replace=True):
    """FBX를 정적 메시(+머티리얼·텍스처)로 가져온다. 반환: 정적 메시 경로. 경로가 길면 조용히 실패하므로 짧은 경로에서 가져올 것."""
    t = unreal.AssetImportTask()
    t.filename = fbx_path
    t.destination_path = dest_dir
    t.automated = True
    t.save = True
    t.replace_existing = replace
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_as_skeletal = False
    o.import_materials = True
    o.import_textures = True
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    o.static_mesh_import_data.combine_meshes = True
    t.options = o
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    meshes = [p.split(".")[0] for p in t.imported_object_paths if isinstance(unreal.load_asset(p), unreal.StaticMesh)]
    if not meshes:
        raise RuntimeError(f"임포트 실패(경로 길이 확인): {fbx_path} → {list(t.imported_object_paths)}")
    return meshes[0]


def _dm_static(path):
    dm = unreal.DynamicMesh()
    dm, _ = A.copy_mesh_from_static_mesh(unreal.load_asset(path), dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    return dm


def _dm_skel(path):
    return skeleton_check._dm(path)


def _tris(dm):
    dm, ids, _ = Q.get_all_triangle_i_ds(dm)
    return len(L.convert_index_list_to_array(ids))


def _bbox(dm):
    b = Q.get_mesh_bounding_box(dm)
    return [b.min.x, b.min.y, b.min.z], [b.max.x, b.max.y, b.max.z]


# ---------------------------------------------------------------- 맞춤
def template_path(body, slot):
    return profiles.body(body)["outfit"].get(slot)


def _place(dm, body, slot, ratio, rel_off, yaw):
    """원본을 틀 상자에 맞춘다(원본 상자 중심 → 원점 → 회전 → 축별 스케일 → 목표 중심)."""
    tlo, thi = _bbox(_dm_skel(template_path(body, slot)))
    tsize = [h - l for l, h in zip(tlo, thi)]
    tctr = [(h + l) / 2 for l, h in zip(tlo, thi)]
    if yaw:
        dm = T.transform_mesh(dm, unreal.Transform(rotation=unreal.Rotator(0, 0, yaw)))
    lo, hi = _bbox(dm)
    raw = [h - l for l, h in zip(lo, hi)]
    ctr = [(h + l) / 2 for l, h in zip(lo, hi)]
    scale = [ratio[i] * tsize[i] / raw[i] for i in range(3)]
    target = [tctr[i] + rel_off[i] * tsize[i] for i in range(3)]
    dm = T.translate_mesh(dm, unreal.Vector(-ctr[0], -ctr[1], -ctr[2]))
    dm = T.scale_mesh(dm, unreal.Vector(*scale), unreal.Vector(0, 0, 0))
    dm = T.translate_mesh(dm, unreal.Vector(*target))
    return dm, [round(s, 3) for s in scale], [round(c, 1) for c in target]


def _simplify(dm, target):
    if not target or _tris(dm) <= target:
        return dm
    return SIM.apply_simplify_to_triangle_count(dm, target, unreal.GeometryScriptSimplifyMeshOptions())


def _dist(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


def _push_out(dm, body_dm, bvh, max_depth, margin):
    """몸 안쪽 max_depth 이내 정점을 몸 표면 밖 margin까지. 반환: (dm, 옮긴 수)"""
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
        d = _dist(p, q)
        if d < 1e-4 or d > max_depth:
            continue
        k = (d + margin) / d
        dm, _ok = ED.set_vertex_position(dm, v, unreal.Vector(p.x + (q.x - p.x) * k, p.y + (q.y - p.y) * k, p.z + (q.z - p.z) * k), True)
        moved += 1
    return dm, moved


# ---------------------------------------------------------------- 가중치
def _transfer(src_dm, dst_dm):
    opt = unreal.GeometryScriptTransferBoneWeightsOptions()
    opt.transfer_method = unreal.TransferBoneWeightsMethod.CLOSEST_POINT_ON_SURFACE
    opt.output_target_mesh_bones = unreal.OutputTargetMeshBones.SOURCE_BONES
    return BW.transfer_bone_weights_from_mesh(src_dm, dst_dm, opt)


def _bone_names(dm):
    dm, infos = BW.get_all_bones_info(dm)
    return [str(i.name) for i in infos], infos


def _set_weights(dm, v, ws):
    r = BW.set_vertex_bone_weights(dm, v, ws)
    return r[0] if isinstance(r, tuple) else r


def _blend(dm_tpl_w, dm_body_w, tpl_dm, max_cm):
    """같은 정점 ID의 두 가중치 중 틀 표면 max_cm 안이면 틀, 아니면 몸. 반환: (dm_tpl_w, 틀 가중치 정점 수)."""
    tpl_dm, bvh = SP.build_bvh_for_mesh(tpl_dm)
    opt = unreal.GeometryScriptSpatialQueryOptions()
    dm_tpl_w, vids, _ = Q.get_all_vertex_i_ds(dm_tpl_w)
    near = 0
    for v in L.convert_index_list_to_array(vids):
        p, _ = Q.get_vertex_position(dm_tpl_w, v)
        _m, hit, _o = SP.find_nearest_point_on_mesh(tpl_dm, bvh, p, opt)
        if _dist(p, hit.position) <= max_cm:
            near += 1
            continue
        dm_body_w, ws, _ = BW.get_vertex_bone_weights(dm_body_w, v)
        dm_tpl_w = _set_weights(dm_tpl_w, v, ws)
    return dm_tpl_w, near


def _restrict(dm, allowed):
    """허용 접두사 밖의 본 가중치를 가장 가까운 허용 조상 본으로 옮긴다. 반환: (dm, 바꾼 정점 수)."""
    names, infos = _bone_names(dm)
    parent = {i.index: i.parent_index for i in infos}
    ok = {i.index for i in infos if str(i.name).lower().startswith(allowed)}

    def target(b):
        while b >= 0 and b not in ok:
            b = parent.get(b, -1)
        return b

    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    changed = 0
    for v in L.convert_index_list_to_array(vids):
        dm, ws, _ = BW.get_vertex_bone_weights(dm, v)
        if all(w.bone_index in ok for w in ws if w.weight > 0):
            continue
        acc = {}
        for w in ws:
            if w.weight <= 0:
                continue
            b = w.bone_index if w.bone_index in ok else target(w.bone_index)
            if b < 0:
                b = min(ok)   # 조상 중 허용 본이 없음(드묾) → 루트에 가까운 허용 본
            acc[b] = acc.get(b, 0.0) + w.weight
        tot = sum(acc.values()) or 1.0
        new = []
        for b, wt in acc.items():
            bw = unreal.GeometryScriptBoneWeight()
            bw.bone_index = b
            bw.weight = wt / tot
            new.append(bw)
        dm = _set_weights(dm, v, new)
        changed += 1
    return dm, changed


# ---------------------------------------------------------------- 머티리얼
def _two_sided(mat):
    """의상 안쪽 면이 검게 보이지 않게 양면으로(정적 메시가 가져온 머티리얼만)."""
    if isinstance(mat, unreal.Material) and not mat.get_editor_property("two_sided"):
        mat.set_editor_property("two_sided", True)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        EAL.save_loaded_asset(mat)
        return True
    return False


# ---------------------------------------------------------------- 생성
def build(src_static, body, slot, out_path, ratio=(1.0, 1.0, 1.0), rel_off=(0.0, 0.0, 0.0), yaw=0.0,
          target_tris=None, push=(4.0, 0.3), tpl_max_cm=6.0, overwrite=False):
    """src_static(정적 메시)을 체형 body의 슬롯 틀에 맞춰 out_path에 스켈레탈 메시로 만든다. 반환: 보고서 dict.
    target_tris: None = 슬롯 예산, 0 = 단순화 안 함."""
    rule = SLOT_RULES[slot]
    tpl = template_path(body, slot)
    if not tpl:
        raise RuntimeError(f"{body} {slot}: 키트 틀 없음 — 틀 없는 슬롯은 MC2.2에서 보완")
    leader = profiles.body(body)["leader"]
    if EAL.does_asset_exist(out_path) and not overwrite:
        raise RuntimeError(f"이미 있음(overwrite=True로 덮어쓰기): {out_path}")
    rep = {"body": body, "slot": slot, "out": out_path, "template": tpl}

    # 1·2 형상
    dm = _dm_static(src_static)
    rep["src_tris"] = _tris(dm)
    dm = _simplify(dm, rule["tris"] if target_tris is None else target_tris)
    dm, rep["scale"], rep["center"] = _place(dm, body, slot, ratio, rel_off, yaw)
    rep["tris"] = _tris(dm)

    # 3 관통 보정
    body_dm = _dm_skel(leader)
    if push:
        body_dm, bvh = SP.build_bvh_for_mesh(body_dm)
        dm, rep["pushed_verts"] = _push_out(dm, body_dm, bvh, push[0], push[1])

    # 4 가중치(틀·몸 본 목록이 같은지 먼저 확인)
    tpl_dm = _dm_skel(tpl)
    if _bone_names(tpl_dm)[0] != _bone_names(body_dm)[0]:
        raise RuntimeError("틀과 리더의 본 목록이 다름 — skeleton_check.self_test 확인")
    copy = ED.append_mesh(unreal.DynamicMesh(), dm, unreal.Transform())   # 정점 ID 같은 사본
    dm = _transfer(tpl_dm, dm)
    copy = _transfer(body_dm, copy)
    dm, rep["template_weight_verts"] = _blend(dm, copy, tpl_dm, tpl_max_cm)

    # 5 허용 본
    dm, rep["restricted_verts"] = _restrict(dm, rule["bones"])

    # 6 틀 복제 → 형상 교체
    if EAL.does_asset_exist(out_path):
        EAL.delete_asset(out_path)
    EAL.make_directory(out_path.rsplit("/", 1)[0])
    sk = EAL.duplicate_asset(tpl, out_path)
    if sk is None:
        raise RuntimeError(f"틀 복제 실패: {tpl} → {out_path}")
    st = unreal.load_asset(src_static)
    mats = [s.material_interface for s in st.static_materials]
    names = [s.material_slot_name for s in st.static_materials]
    rep["two_sided"] = [m.get_name() for m in mats if _two_sided(m)]
    copt = unreal.GeometryScriptCopyMeshToAssetOptions()
    copt.set_editor_property("replace_materials", True)
    copt.set_editor_property("new_materials", mats)
    copt.set_editor_property("new_material_slot_names", names)
    copt.set_editor_property("remap_bone_indices_to_match_asset", True)
    lod = unreal.GeometryScriptMeshWriteLOD()
    lod.lod_index = 0
    _dm_out, outcome = A.copy_mesh_to_skeletal_mesh(dm, sk, copt, lod)   # 반환은 (원본 DynamicMesh, 결과)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"형상 교체 실패: {out_path}")
    EAL.save_loaded_asset(sk)

    # 7 뼈대 검사
    chk = skeleton_check.check(out_path, body, slot)
    rep["skeleton_ok"], rep["skeleton_errors"] = chk["ok"], chk["errors"]
    rep["physics_asset"] = sk.physics_asset.get_name() if sk.physics_asset else None
    return rep


def measure(item_path, body, visible_parts=("head", "hands", "legs", "feet"), float_cm=3.0, step=3):
    """수치 검사: 보이는 몸 부위 안으로 들어간 정점 %, 몸에서 float_cm 넘게 뜬 정점 %, 거리 p50/p95."""
    bp = profiles.body(body)
    ref = unreal.DynamicMesh()
    for part in visible_parts:
        ref = ED.append_mesh(ref, _dm_skel(bp["parts"][part]), unreal.Transform())
    ref, bvh = SP.build_bvh_for_mesh(ref)
    full, fbvh = SP.build_bvh_for_mesh(_dm_skel(bp["leader"]))
    dm = _dm_skel(item_path)
    opt = unreal.GeometryScriptSpatialQueryOptions()
    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    vids = list(L.convert_index_list_to_array(vids))[::step]
    inside = floating = 0
    dists = []
    for v in vids:
        p, _ = Q.get_vertex_position(dm, v)
        _m, ins, _o = SP.is_point_inside_mesh(ref, bvh, p, opt)
        inside += 1 if ins else 0
        _m, fin, _o = SP.is_point_inside_mesh(full, fbvh, p, opt)
        _m, hit, _o = SP.find_nearest_point_on_mesh(full, fbvh, p, opt)
        d = _dist(p, hit.position)
        dists.append(d)
        floating += 1 if (not fin and d > float_cm) else 0
    dists.sort()
    n = len(vids)
    return {"verts_sampled": n, "tris": _tris(dm), "inside_visible%": round(100.0 * inside / n, 1),
            f"float>{float_cm:g}cm%": round(100.0 * floating / n, 1),
            "dist_p50": round(dists[n // 2], 2), "dist_p95": round(dists[int(n * 0.95)], 2)}
