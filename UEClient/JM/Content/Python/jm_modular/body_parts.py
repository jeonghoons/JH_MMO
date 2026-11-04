"""베이스 바디를 몸 부위 스켈레탈 메시로 나눈다 (MC1.3).

프로토타입(HANDOFF_MODULAR_EQUIPMENT_VARCO.md 부록 B)을 검증한 뒤 보완한 버전.
- 정점마다 가중치가 가장 큰 본으로 부위를 정하고, 삼각형은 세 정점의 다수결로 정한다.
- 목: 목 본 가중치 합(neck_min_weight 이상) 또는 목 본 축 원기둥(neck_radius_cm, neck_below_cm) 안의 몸통 정점을 머리 부위에 넣는다.
  (프로토타입의 |x| < 10cm 상자는 앞뒤 깊이 제한이 없어 가슴·등 윗부분까지 머리 부위로 들어가 상의를 뚫고 나왔다)
- 발목: 절대 높이 대신 발 본 높이 기준(ankle_above_foot_cm)으로 종아리 아래쪽을 발 부위에 넣는다.
- 손목: 손 본에서 wrist_radius_cm 안의 팔뚝을 손 부위에 넣는다.
- 원본의 LOD마다 같은 규칙으로 나눠 LOD 수를 유지한다.
- 머티리얼 슬롯을 생성 옵션으로 넘기고, LOD마다 섹션 슬롯이 원본 머티리얼 ID와 같은지 검사한다.
  (프로토타입은 생성 뒤 슬롯을 덮어써서 섹션이 슬롯 0 하나로 합쳐졌다 — 손이 몸통 머티리얼로 그려짐)
- 같은 이름 에셋이 다른 곳에서 참조 중이면 덮어쓰지 않고 중단한다.
"""
import json
import os
import unreal

A = unreal.GeometryScript_AssetUtils
BW = unreal.GeometryScript_BoneWeights
NA = unreal.GeometryScript_NewAssetUtils
Q = unreal.GeometryScript_MeshQueries
ED = unreal.GeometryScript_MeshEdits
L = unreal.GeometryScript_List
MAT = unreal.GeometryScript_Materials
EAL = unreal.EditorAssetLibrary

PARTS = ["head", "hair", "torso", "arms", "hands", "legs", "feet"]
PROFILE_PATH = os.path.join(os.path.dirname(__file__), "body_profiles.json")


def load_profile(key):
    with open(PROFILE_PATH, encoding="utf-8") as f:
        return json.load(f)[key]


def base_part(bone):
    b = bone.lower()
    if b.startswith(("head", "neck")):
        return "head"
    if b.startswith(("hand", "thumb", "index", "middle", "ring", "pinky")):
        return "hands"
    if b.startswith(("upperarm", "lowerarm")):
        return "arms"
    if b.startswith(("foot", "ball")):
        return "feet"
    if b.startswith(("thigh", "calf", "pelvis")):
        return "legs"
    return "torso"


def _copy_lod(sm, lod):
    dm = unreal.DynamicMesh()
    read = unreal.GeometryScriptMeshReadLOD()
    read.lod_type = unreal.GeometryScriptLODType.SOURCE_MODEL
    read.lod_index = lod
    dm, outcome = A.copy_mesh_from_skeletal_mesh(sm, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), read)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"LOD {lod} 복사 실패: {sm.get_path_name()}")
    return dm


def _dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2) ** 0.5


def classify(dm, prof, slot_names):
    """삼각형 ID → 부위. 반환: (tri_part, 통계)"""
    dm, infos = BW.get_all_bones_info(dm)
    names = {i.index: str(i.name) for i in infos}
    pos = {str(i.name): i.world_transform.translation for i in infos}
    neck_idx = {i.index for i in infos if str(i.name) in prof["neck_bones"]}
    hair_slots = {slot_names.index(s) for s in prof["hair_material_slots"] if s in slot_names}
    neck = pos[prof["neck_bones"][0]]
    stats = {"neck": 0, "ankle": 0, "wrist": 0, "no_weight": 0, "hair_tris": 0}

    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    vpart = {}
    for v in L.convert_index_list_to_array(vids):
        dm, bw, ok = BW.get_largest_vertex_bone_weight(dm, v)
        if not ok:
            stats["no_weight"] += 1
            vpart[v] = "torso"
            continue
        bone = names.get(bw.bone_index, "root")
        part = base_part(bone)
        low = bone.lower()
        if part == "torso" and neck_idx:
            # 목 기둥: 목 본 가중치가 있거나, 목 본 축에서 수평 neck_radius_cm 안이면서 neck_01 아래 neck_below_cm 위
            dm, ws, _ = BW.get_vertex_bone_weights(dm, v)
            p, _ = Q.get_vertex_position(dm, v)
            in_col = ((p.x - neck.x) ** 2 + (p.y - neck.y) ** 2) ** 0.5 < prof["neck_radius_cm"] and p.z > neck.z - prof["neck_below_cm"]
            if in_col or sum(w.weight for w in ws if w.bone_index in neck_idx) >= prof["neck_min_weight"]:
                part = "head"
                stats["neck"] += 1
        elif part == "legs" and low.startswith("calf"):
            p, _ = Q.get_vertex_position(dm, v)
            foot = pos["foot_l" if low.endswith("_l") else "foot_r"]
            if p.z < foot.z + prof["ankle_above_foot_cm"]:
                part = "feet"
                stats["ankle"] += 1
        elif part == "arms" and low.startswith("lowerarm"):
            p, _ = Q.get_vertex_position(dm, v)
            hand = pos["hand_l" if low.endswith("_l") else "hand_r"]
            if _dist(p, hand) < prof["wrist_radius_cm"]:
                part = "hands"
                stats["wrist"] += 1
        vpart[v] = part

    dm, tl, _ = Q.get_all_triangle_indices(dm, False)
    tpart = {}
    for tid, tri in enumerate(L.convert_triangle_list_to_array(tl)):
        if tri.x < 0:
            continue
        if hair_slots:
            mid, _ = MAT.get_triangle_material_id(dm, tid)
            if mid in hair_slots:
                tpart[tid] = "hair"
                stats["hair_tris"] += 1
                continue
        ps = [vpart[tri.x], vpart[tri.y], vpart[tri.z]]
        tpart[tid] = max(set(ps), key=lambda g: (ps.count(g), g == ps[0]))
    return tpart, stats


def _create_options(sm):
    """머티리얼 슬롯을 생성 옵션으로 넘긴다.
    (프로토타입처럼 생성 뒤 materials를 덮어쓰면 섹션이 슬롯 0 하나로 합쳐져,
     예: 매니퀸 손(슬롯 1)이 몸통 머티리얼(슬롯 0)로 그려진다)"""
    opt = unreal.GeometryScriptCreateNewSkeletalMeshAssetOptions()
    mats = unreal.Map(unreal.Name, unreal.MaterialInterface)
    for slot in sm.materials:
        mats[slot.material_slot_name] = slot.material_interface
    opt.set_editor_property("materials", mats)
    return opt


def _valid_tris(dm):
    """삭제 후 남은(유효한) 삼각형 ID. get_num_triangle_i_ds는 최대 ID라 삭제된 칸도 센다."""
    dm, ids, _ = Q.get_all_triangle_i_ds(dm)
    return L.convert_index_list_to_array(ids)


def _material_ids(dm):
    ids = set()
    for t in _valid_tris(dm):
        mid, _ = MAT.get_triangle_material_id(dm, t)
        ids.add(mid)
    return sorted(ids)


def verify(key, out_dir=None, prefix=None):
    """만든 부위 에셋 검사. build() 직후에는 메시가 비동기로 빌드 중이라 섹션이 비어 보이므로 따로 호출한다.
    - LOD마다 섹션이 가리키는 슬롯 = 삼각형 머티리얼 ID 집합
    - 부위 LOD0 삼각형 합 = 원본 LOD0 삼각형 수
    반환: {부위: [LOD별 슬롯]}. 어긋나면 예외."""
    prof = load_profile(key)
    out_dir = out_dir or prof["out_dir"]
    prefix = prefix or prof["asset_prefix"]
    sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    sm = unreal.load_asset(prof["source_mesh"])
    result, total = {}, 0
    for part in PARTS:
        path = f"{out_dir}/{prefix}_{part.capitalize()}"
        if not EAL.does_asset_exist(path):
            continue
        sk = unreal.load_asset(path)
        result[part] = []
        for lod in range(len(sk.get_editor_property("lod_info"))):
            dm = _copy_lod(sk, lod)
            slots = sorted(sub.get_lod_material_slot(sk, lod, i) for i in range(sub.get_num_sections(sk, lod)))
            ids = _material_ids(dm)
            if slots != ids:
                raise RuntimeError(f"{path} LOD{lod} 섹션 슬롯 {slots} != 머티리얼 ID {ids} (빌드 중이면 잠시 후 다시)")
            if lod == 0:
                total += len(_valid_tris(dm))
            result[part].append(slots)
    src = len(_valid_tris(_copy_lod(sm, 0)))
    if total != src:
        raise RuntimeError(f"부위 삼각형 합 {total} != 원본 {src}")
    return result


def _check_overwrite(path, allow_refs):
    """덮어써도 되는지 확인. 다른 에셋·레벨이 참조 중이면 예외."""
    if not EAL.does_asset_exist(path):
        return
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    refs = [str(r) for r in (ar.get_referencers(path, unreal.AssetRegistryDependencyOptions()) or [])]
    refs = [r for r in refs if r != path and not any(r.startswith(a) for a in allow_refs)]
    if refs:
        raise RuntimeError(f"{path} 를 참조하는 곳이 있어 덮어쓰지 않음: {refs}")
    EAL.delete_asset(path)


def build(key, out_dir=None, prefix=None, allow_refs=(), dry_run=False):
    """프로파일 key(예: "M")로 몸 부위 에셋을 만든다. 반환: 보고서 dict."""
    prof = load_profile(key)
    out_dir = out_dir or prof["out_dir"]
    prefix = prefix or prof["asset_prefix"]
    sm = unreal.load_asset(prof["source_mesh"])
    if sm is None:
        raise RuntimeError(f"원본 메시 없음: {prof['source_mesh']}")
    slot_names = [str(s.material_slot_name) for s in sm.materials]
    lod_count = len(sm.get_editor_property("lod_info"))

    lods = []   # LOD마다 (tpart, stats, total)
    for lod in range(lod_count):
        dm = _copy_lod(sm, lod)
        tpart, stats = classify(dm, prof, slot_names)
        lods.append((tpart, stats, Q.get_num_triangle_i_ds(dm)))

    report = {"source": prof["source_mesh"], "lods": lod_count, "parts": {}, "lod_stats": [], "skipped": []}
    for tpart, stats, total in lods:
        counts = {p: 0 for p in PARTS}
        for p in tpart.values():
            counts[p] += 1
        report["lod_stats"].append({"total": total, "assigned": len(tpart), "counts": counts, "rules": stats})
        if len(tpart) != total:
            raise RuntimeError(f"분류되지 않은 삼각형: {total - len(tpart)}")

    if dry_run:
        return report

    EAL.make_directory(out_dir)
    for part in PARTS:
        if sum(1 for p in lods[0][0].values() if p == part) == 0:
            report["skipped"].append(part)   # 예: 매니퀸은 머리카락이 없음
            continue
        meshes = []
        for lod, (tpart, _s, _t) in enumerate(lods):
            dm = _copy_lod(sm, lod)
            kill = [t for t, p in tpart.items() if p != part]
            dm, _n = ED.delete_triangles_from_mesh(dm, L.convert_array_to_index_list(kill, unreal.GeometryScriptIndexType.TRIANGLE), False)
            if not _valid_tris(dm):
                break   # 낮은 LOD에서 부위가 사라지면 그 위 LOD까지만
            meshes.append(dm)
        path = f"{out_dir}/{prefix}_{part.capitalize()}"
        _check_overwrite(path, allow_refs)
        sk, outcome = NA.create_new_skeletal_mesh_asset_from_mesh_lods(meshes, sm.skeleton, path, _create_options(sm))
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS or sk is None:
            raise RuntimeError(f"에셋 생성 실패: {path}")
        sk.set_editor_property("physics_asset", None)
        EAL.save_loaded_asset(sk)
        report["parts"][part] = {"path": path, "lods": len(meshes), "tris_lod0": sum(1 for p in lods[0][0].values() if p == part)}
    return report
