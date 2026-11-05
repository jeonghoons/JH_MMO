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


def _not_playing():
    """Simulate/PIE 중에는 이름 변경·저장이 실패하고, 빌드 안 된 메시 복사로 에디터가 죽을 수 있다(MC2.2 크래시)."""
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중 — stop_simulate() 후 다음 호출에서 실행")


def _dm_static(path):
    st = unreal.load_asset(path)
    if st is None or st.get_num_triangles(0) <= 0:
        # 렌더 데이터가 없는 메시를 복사하면 CopyMeshFromStaticMesh가 어서트로 에디터를 종료시킨다
        raise RuntimeError(f"정적 메시가 비었거나 아직 빌드 전: {path}")
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


# ---------------------------------------------------------------- 팔·다리 중심 맞추기
# 정면 이미지 한 장으로 만든 3D는 팔·다리 통의 앞뒤 위치를 모른다 → 상자 맞춤만으로는 소매가 팔에서 수 cm 어긋난다
# (MC2.2 천 상의: 손목 단면 중심이 키트 의상보다 뒤 4.5·아래 4.7cm). 팔다리 축을 따라 단면 중심을 키트 의상에 맞춘다.
LIMBS = {
    "top": [("upperarm_l", "lowerarm_l", "hand_l"), ("upperarm_r", "lowerarm_r", "hand_r")],
    "bottom": [("thigh_l", "calf_l", "foot_l"), ("thigh_r", "calf_r", "foot_r")],
}
LIMB_BIN_CM = 2.0      # 단면 간격
LIMB_RADIUS_CM = 13.0  # 축에서 이 거리 안의 정점만 팔·다리 통으로 본다
LIMB_START = 0.5       # 첫 마디 길이의 이 비율부터 보정 측정(어깨·사타구니 쪽은 몸통 정점이 섞임)
LIMB_RAMP0 = 0.2       # 이 비율까지는 보정 0 → LIMB_START에서 100% (몸통과 이어지는 부분이 찢어지지 않게)
LIMB_TOL_CM = 1.0      # 끝 단면(손목·발목) 중심 차이 허용치


def _v(a):
    return (a.x, a.y, a.z)


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _norm(a):
    n = math.sqrt(_dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n), n


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _chain(bones_pos, names):
    """마디 [(시작점, 방향, 길이, 누적 시작 t, u, v)] — u·v는 단면 평면 축."""
    segs, acc = [], 0.0
    pts = [_v(bones_pos[n]) for n in names]
    for a, b in zip(pts, pts[1:]):
        d, n = _norm(_sub(b, a))
        ref = (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
        u, _ = _norm(_cross(d, ref))
        v = _cross(d, u)
        segs.append((a, d, n, acc, u, v))
        acc += n
    return segs, acc


def _on_chain(p, segs, total, extend=4.0):
    """점 p의 (t, 마디 번호, 단면 좌표 (pu, pv), 축 거리). 마지막 마디는 끝에서 extend cm까지 허용."""
    best = None
    for i, (a, d, n, t0, u, v) in enumerate(segs):
        w = _sub(p, a)
        s = _dot(w, d)
        hi = n + (extend if i == len(segs) - 1 else 0.0)
        if s < (0.0 if i else -extend) or s > hi:
            continue
        perp = (w[0] - s * d[0], w[1] - s * d[1], w[2] - s * d[2])
        r = math.sqrt(_dot(perp, perp))
        if best is None or r < best[3]:
            best = (t0 + s, i, (_dot(perp, u), _dot(perp, v)), r)
    return best


def _bin_centers(points, segs, total, first_len):
    """정점 목록 → {bin: (cu, cv)} (단면 5~95% 범위의 가운데 — 장식 쏠림에 덜 민감)."""
    bins = {}
    for p in points:
        h = _on_chain(p, segs, total)
        if not h or h[3] > LIMB_RADIUS_CM or h[0] < LIMB_START * first_len:
            continue
        bins.setdefault(int(h[0] // LIMB_BIN_CM), []).append(h[2])
    out = {}
    for k, uv in bins.items():
        if len(uv) < 6:
            continue
        us, vs = sorted(x[0] for x in uv), sorted(x[1] for x in uv)
        lo, hi = int(len(us) * 0.05), int(len(us) * 0.95) - 1
        out[k] = ((us[lo] + us[hi]) / 2, (vs[lo] + vs[hi]) / 2)
    return out


def _verts(dm):
    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    ids = list(L.convert_index_list_to_array(vids))
    return ids, [_v(Q.get_vertex_position(dm, v)[0]) for v in ids]


def _fit_lines(centers, segs):
    """마디별로 단면 중심 c(t) = a + b·t 직선 맞춤(최소제곱). 키트 의상은 저폴리라 단면 중심이 단면마다 2~4cm 튀므로
    단면별로 맞추면 그 잡음이 옮겨진다(MC2.2 측정) → 마디 전체의 추세만 맞춘다. 반환: [((au, bu), (av, bv)) | None]."""
    out = []
    for a, d, n, t0, u, v in segs:
        pts = [((k + 0.5) * LIMB_BIN_CM, c) for k, c in centers.items() if t0 <= (k + 0.5) * LIMB_BIN_CM < t0 + n]
        if len(pts) < 2:
            out.append(None)
            continue
        ts = [t for t, _ in pts]
        mt = sum(ts) / len(ts)
        var = sum((t - mt) ** 2 for t in ts) or 1.0
        line = []
        for axis in (0, 1):
            ys = [c[axis] for _, c in pts]
            my = sum(ys) / len(ys)
            b = sum((t - mt) * (y - my) for t, (y) in zip(ts, ys)) / var
            line.append((my - b * mt, b))
        out.append(tuple(line))
    return out


def _eval(lines, i, t):
    ln = lines[i] if i < len(lines) else None
    if ln is None:   # 이 마디에 측정이 없으면 이웃 마디 직선 사용
        ln = next((x for x in lines if x is not None), None)
        if ln is None:
            return (0.0, 0.0)
    return (ln[0][0] + ln[0][1] * t, ln[1][0] + ln[1][1] * t)


def end_offset(points, segs, total, ref_points=None, length=6.0):
    """끝(손목·발목) 앞 length cm 단면의 평균 중심. ref_points를 주면 두 중심의 차이(cm)."""
    def center(pts):
        acc = []
        for p in pts:
            h = _on_chain(p, segs, total, extend=1.0)
            if h and h[3] <= LIMB_RADIUS_CM and h[0] >= total - length:
                acc.append(h[2])
        if not acc:
            return None
        return (sum(a[0] for a in acc) / len(acc), sum(a[1] for a in acc) / len(acc))
    c = center(points)
    if ref_points is None or c is None:
        return c
    r = center(ref_points)
    return None if r is None else math.dist(c, r)


def _recenter_limbs(dm, body, slot):
    """팔·다리 통의 단면 중심을 키트 의상에 맞춘다. 반환: (dm, {팔다리: {"before_cm", "after_cm", "moved"}})."""
    chains = LIMBS.get(slot)
    if not chains:
        return dm, {}
    tpl_dm = _dm_skel(template_path(body, slot))
    tpl_dm, infos = BW.get_all_bones_info(tpl_dm)
    pos = {str(i.name): i.world_transform.translation for i in infos}
    _tids, tpts = _verts(tpl_dm)
    report = {}
    for names in chains:
        segs, total = _chain(pos, names)
        first_len = segs[0][2]
        t_c = _bin_centers(tpts, segs, total, first_len)
        ids, gpts = _verts(dm)
        g_c = _bin_centers(gpts, segs, total, first_len)
        before = end_offset(gpts, segs, total, tpts)
        t_l, g_l = _fit_lines(t_c, segs), _fit_lines(g_c, segs)
        if all(x is None for x in g_l):
            report[names[0]] = {"before_cm": before, "after_cm": before, "moved": 0}
            continue
        moved = 0
        for vid, p in zip(ids, gpts):
            h = _on_chain(p, segs, total)
            if not h or h[3] > LIMB_RADIUS_CM or h[0] < LIMB_RAMP0 * first_len:
                continue
            tc, gc = _eval(t_l, h[1], h[0]), _eval(g_l, h[1], h[0])
            cu, cv = tc[0] - gc[0], tc[1] - gc[1]
            w = min(1.0, (h[0] - LIMB_RAMP0 * first_len) / ((LIMB_START - LIMB_RAMP0) * first_len))
            u, v = segs[h[1]][4], segs[h[1]][5]
            delta = tuple(w * (cu * u[i] + cv * v[i]) for i in range(3))
            dm, _ok = ED.set_vertex_position(dm, vid, unreal.Vector(p[0] + delta[0], p[1] + delta[1], p[2] + delta[2]), True)
            moved += 1
        # 남은 끝 단면 차이(직선으로 못 잡은 굽은 부분)를 마지막 마디에 0 → 1로 퍼뜨려 없앤다
        ids, gpts = _verts(dm)
        gc_end, tc_end = end_offset(gpts, segs, total), end_offset(tpts, segs, total)
        if gc_end and tc_end:
            ru, rv = tc_end[0] - gc_end[0], tc_end[1] - gc_end[1]
            a, d, n, t0, u, v = segs[-1]
            for vid, p in zip(ids, gpts):
                h = _on_chain(p, segs, total)
                if not h or h[3] > LIMB_RADIUS_CM or h[1] != len(segs) - 1:
                    continue
                w = min(1.0, max(0.0, (h[0] - t0) / n))
                delta = tuple(w * (ru * u[i] + rv * v[i]) for i in range(3))
                dm, _ok = ED.set_vertex_position(dm, vid, unreal.Vector(p[0] + delta[0], p[1] + delta[1], p[2] + delta[2]), True)
        _ids2, gpts2 = _verts(dm)
        after = end_offset(gpts2, segs, total, tpts)
        report[names[0]] = {"before_cm": round(before, 1) if before is not None else None,
                            "after_cm": round(after, 1) if after is not None else None, "moved": moved}
    return dm, report


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
    _not_playing()
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
    # 2-1 팔·다리 통 중심을 키트 의상에 맞춤(끝 단면 차이 LIMB_TOL_CM 이하여야 limbs_ok)
    dm, rep["limbs"] = _recenter_limbs(dm, body, slot)
    rep["limbs_ok"] = all(r["after_cm"] is not None and r["after_cm"] <= LIMB_TOL_CM for r in rep["limbs"].values())

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
    if any(m is None for m in mats):
        raise RuntimeError(f"원본 메시 머티리얼 슬롯이 비어 있음(assign_source_material 필요): {src_static}")
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


# ---------------------------------------------------------------- 틀 없는 슬롯 (장갑) — 몸 메시를 복제 바탕으로
GLOVE_CUFF_CM = 6.0   # 장갑 맞춤 상자에 넣는 손목 위 팔 길이


def _region_box(body, region, side, cuff_cm=0.0):
    """키트 몸에서 영역(손 등) + 같은 쪽 팔의 끝 cuff_cm 구간 정점의 상자."""
    from . import body_mask
    chains = {f"arm_{s}": v for s, v in body_mask.ARM.items()}
    dm, vdata = body_mask._vertex_data(profiles.body(body)["leader"], chains)
    dm, ids, _ = Q.get_all_vertex_i_ds(dm)
    pts = []
    for v in L.convert_index_list_to_array(ids):
        reg, dist = vdata[v]
        p = Q.get_vertex_position(dm, v)[0]
        if (p.x < 0) != (side == "r"):
            continue
        if reg == region or (cuff_cm and reg == "arm" and dist <= cuff_cm):
            pts.append((p.x, p.y, p.z))
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return lo, hi


def _place_box(dm, lo, hi, ratio=(1.0, 1.0, 1.0), rel_off=(0.0, 0.0, 0.0)):
    tsize = [h - l for l, h in zip(lo, hi)]
    tctr = [(h + l) / 2 for l, h in zip(lo, hi)]
    slo, shi = _bbox(dm)
    raw = [h - l for l, h in zip(slo, shi)]
    ctr = [(h + l) / 2 for l, h in zip(slo, shi)]
    scale = [ratio[i] * tsize[i] / raw[i] for i in range(3)]
    dm = T.translate_mesh(dm, unreal.Vector(-ctr[0], -ctr[1], -ctr[2]))
    dm = T.scale_mesh(dm, unreal.Vector(*scale), unreal.Vector(0, 0, 0))
    dm = T.translate_mesh(dm, unreal.Vector(*[tctr[i] + rel_off[i] * tsize[i] for i in range(3)]))
    return dm


def build_gloves(src_static, body, out_path, ratio=(1.0, 1.0, 1.0), rel_off=(0.0, 0.0, 0.0), target_tris=None, overwrite=False):
    """오른손 장갑 메시 하나 → 오른손에 맞추고 좌우 반전해 왼손에도 → 몸 가중치 → 키트 몸 메시 복제에 형상 교체."""
    _not_playing()
    rule = SLOT_RULES["gloves"]
    leader = profiles.body(body)["leader"]
    if EAL.does_asset_exist(out_path) and not overwrite:
        raise RuntimeError(f"이미 있음(overwrite=True로 덮어쓰기): {out_path}")
    rep = {"body": body, "slot": "gloves", "out": out_path}
    half = (target_tris if target_tris is not None else rule["tris"]) // 2
    src = _simplify(_dm_static(src_static), half)
    rep["src_tris"] = _tris(src)
    right = _place_box(ED.append_mesh(unreal.DynamicMesh(), src, unreal.Transform()), *_region_box(body, "hand", "r", GLOVE_CUFF_CM), ratio, rel_off)
    left = ED.append_mesh(unreal.DynamicMesh(), src, unreal.Transform())
    left = T.scale_mesh(left, unreal.Vector(-1, 1, 1), unreal.Vector(0, 0, 0))
    left = unreal.GeometryScript_Normals.flip_normals(left)   # 반전으로 뒤집힌 면 방향 되돌림
    left = _place_box(left, *_region_box(body, "hand", "l", GLOVE_CUFF_CM), ratio, rel_off)
    dm = ED.append_mesh(right, left, unreal.Transform())
    rep["tris"] = _tris(dm)
    body_dm, bvh = SP.build_bvh_for_mesh(_dm_skel(leader))
    dm, rep["pushed_verts"] = _push_out(dm, body_dm, bvh, 4.0, 0.3)
    dm = _transfer(body_dm, dm)
    dm, rep["restricted_verts"] = _restrict(dm, rule["bones"])
    _write_skeletal(dm, leader, src_static, out_path, rep, physics_asset=False)
    chk = skeleton_check.check(out_path, body, "gloves")
    rep["skeleton_ok"], rep["skeleton_errors"] = chk["ok"], chk["errors"]
    return rep


def _write_skeletal(dm, base_asset, src_static, out_path, rep, physics_asset=True):
    """base_asset(키트 의상 또는 몸)을 out_path로 복제하고 LOD0 형상·가중치·머티리얼을 dm으로 교체."""
    if EAL.does_asset_exist(out_path):
        EAL.delete_asset(out_path)
    EAL.make_directory(out_path.rsplit("/", 1)[0])
    sk = EAL.duplicate_asset(base_asset, out_path)
    if sk is None:
        raise RuntimeError(f"복제 실패: {base_asset} → {out_path}")
    st = unreal.load_asset(src_static)
    mats = [s.material_interface for s in st.static_materials]
    if any(m is None for m in mats):
        raise RuntimeError(f"원본 메시 머티리얼 슬롯이 비어 있음(assign_source_material 필요): {src_static}")
    rep["two_sided"] = [m.get_name() for m in mats if _two_sided(m)]
    copt = unreal.GeometryScriptCopyMeshToAssetOptions()
    copt.set_editor_property("replace_materials", True)
    copt.set_editor_property("new_materials", mats)
    copt.set_editor_property("new_material_slot_names", [s.material_slot_name for s in st.static_materials])
    copt.set_editor_property("remap_bone_indices_to_match_asset", True)
    lod = unreal.GeometryScriptMeshWriteLOD()
    lod.lod_index = 0
    _out, outcome = A.copy_mesh_to_skeletal_mesh(dm, sk, copt, lod)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"형상 교체 실패: {out_path}")
    if not physics_asset:
        sk.set_editor_property("physics_asset", None)
    EAL.save_loaded_asset(sk)
    rep["physics_asset"] = sk.physics_asset.get_name() if sk.physics_asset else None


# ---------------------------------------------------------------- 정적 부착 (머리·무기)
def _bone_ref(body, bone):
    return {n: t for n, _p, t in skeleton_check.bones(profiles.body(body)["leader"])}[bone]


def _head_box(body):
    lo, hi = _region_box_bones(body, ("head",))
    hb = unreal.load_asset(profiles.body(body)["parts"]["hair"]).get_bounds()
    hlo = [hb.origin.x - hb.box_extent.x, hb.origin.y - hb.box_extent.y, hb.origin.z - hb.box_extent.z]
    hhi = [hb.origin.x + hb.box_extent.x, hb.origin.y + hb.box_extent.y, hb.origin.z + hb.box_extent.z]
    return [min(a, b) for a, b in zip(lo, hlo)], [max(a, b) for a, b in zip(hi, hhi)]


def _region_box_bones(body, prefixes):
    dm = _dm_skel(profiles.body(body)["leader"])
    dm, infos = BW.get_all_bones_info(dm)
    names = {i.index: str(i.name) for i in infos}
    dm, ids, _ = Q.get_all_vertex_i_ds(dm)
    pts = []
    for v in L.convert_index_list_to_array(ids):
        dm, ws, _ = BW.get_vertex_bone_weights(dm, v)
        top = max(ws, key=lambda w: w.weight) if ws else None
        if top and names.get(top.bone_index, "").startswith(prefixes):
            p = Q.get_vertex_position(dm, v)[0]
            pts.append((p.x, p.y, p.z))
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def static_attachment(item_key, body):
    """정적 아이템의 부착 정보 {"mesh", "socket", "transform"(소켓 기준 상대)}. items.json attach 값으로 계산.
    attach.fit = "head"(머리+헤어 상자에 ratio·rel_off) 또는 "length"(가장 긴 축을 length_cm로, grip_frac 지점을 소켓에, rot 회전)."""
    it = load_items()[item_key]
    at = it["attach"]
    st = unreal.load_asset(it["source"])
    b = st.get_bounds()
    raw_c = (b.origin.x, b.origin.y, b.origin.z)
    raw_e = (b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2)
    bone = _bone_ref(body, at["socket"])
    if at["fit"] == "head":
        lo, hi = _head_box(body)
        ratio, rel = at.get("ratio", (1.0, 1.0, 1.0)), at.get("rel_off", (0.0, 0.0, 0.0))
        size = [h - l for l, h in zip(lo, hi)]
        ctr = [(h + l) / 2 + rel[i] * size[i] for i, (l, h) in enumerate(zip(lo, hi))]
        scale = [ratio[i] * size[i] / raw_e[i] for i in range(3)]
        world = unreal.Transform(location=unreal.Vector(*[ctr[i] - raw_c[i] * scale[i] for i in range(3)]),
                                 rotation=unreal.Rotator(0, 0, 0), scale=unreal.Vector(*scale))
        rel_tf = unreal.MathLibrary.compose_transforms(world, unreal.MathLibrary.invert_transform(bone))
    else:   # length: 무기 — 메시 로컬에서 가장 긴 축의 grip_frac 지점이 소켓 원점, 그다음 rot 회전
        axis = max(range(3), key=lambda i: raw_e[i])
        s = at["length_cm"] / raw_e[axis]
        grip = list(raw_c)
        grip[axis] = raw_c[axis] - raw_e[axis] / 2 + at.get("grip_frac", 0.1) * raw_e[axis]
        o = list(at.get("offset", (0.0, 0.0, 0.0)))
        if at.get("align") == "grip":
            # 손 본에서 계산: 긴 축 = 쥐는 축(엄지 방향에서 손가락 방향 성분을 뺀 것), 폭 축 = 손가락 방향, 쥐는 점 = 손바닥 가운데
            side = at["socket"][-1]
            bones = {n: t for n, _p, t in skeleton_check.bones(profiles.body(body)["leader"])}
            hp, mp, tp = (bones[f"{n}_{side}"].translation for n in ("hand", "middle_01", "thumb_01"))
            fing, _ = _norm(_sub(_v(mp), _v(hp)))
            thumb = _sub(_v(tp), _v(hp))
            t_perp = _sub(thumb, tuple(_dot(thumb, fing) * c for c in fing))
            grip_dir, _ = _norm(t_perp)
            if at.get("flip"):
                grip_dir = tuple(-c for c in grip_dir)
            to_local = lambda d: unreal.MathLibrary.inverse_transform_direction(bone, unreal.Vector(*d))
            gl, fl = to_local(grip_dir), to_local(fing)
            axes = {0: "x", 1: "y", 2: "z"}
            # 메시의 긴 축(axis)을 쥐는 축으로: 긴 축이 Z면 X축=손가락, Z축=쥐는 축
            rot = unreal.MathLibrary.make_rot_from_xz(fl, gl) if axes[axis] == "z" else unreal.MathLibrary.make_rot_from_zx(fl, gl)
            palm = unreal.MathLibrary.inverse_transform_location(bone, unreal.Vector(*[(a + b) / 2 for a, b in zip(_v(hp), _v(mp))]))
            o = [o[0] + palm.x, o[1] + palm.y, o[2] + palm.z]
        else:
            r = at.get("rot", (0.0, 0.0, 0.0))
            rot = unreal.Rotator(r[0], r[1], r[2])
        off = rot.quaternion().rotate_vector(unreal.Vector(-grip[0] * s, -grip[1] * s, -grip[2] * s))
        rel_tf = unreal.Transform(location=unreal.Vector(off.x + o[0], off.y + o[1], off.z + o[2]), rotation=rot, scale=unreal.Vector(s, s, s))
    return {"mesh": it["source"], "socket": at["socket"], "transform": rel_tf, "hide_parts": at.get("hide_parts", [])}


def load_items():
    import json, os
    with open(os.path.join(os.path.dirname(__file__), "items.json"), encoding="utf-8") as f:
        return {k: v for k, v in json.load(f).items() if not k.startswith("_")}


def build_item(key, bodies=("M", "F"), overwrite=True):
    """items.json의 아이템 key를 체형별로 만든다. 반환: {체형: 보고서}."""
    it = load_items()[key]
    if "attach" in it:   # 정적 부착 아이템은 만들 메시가 없다(부착값만 계산해 보고)
        return {b: {"attach": str(static_attachment(key, b)["transform"])} for b in bodies}
    fit = it.get("fit", {})
    out = {}
    for b in bodies:
        if it["slot"] == "gloves":
            out[b] = build_gloves(it["source"], b, it["out"].format(body=b), ratio=fit.get("ratio", (1.0, 1.0, 1.0)),
                                  rel_off=fit.get("rel_off", (0.0, 0.0, 0.0)), target_tris=fit.get("target_tris"), overwrite=overwrite)
            continue
        out[b] = build(it["source"], b, it["slot"], it["out"].format(body=b),
                       ratio=fit.get("ratio", (1.0, 1.0, 1.0)), rel_off=fit.get("rel_off", (0.0, 0.0, 0.0)),
                       yaw=fit.get("yaw", 0.0), target_tris=fit.get("target_tris"), overwrite=overwrite)
    return out


def import_source(fbx_path, item_key):
    """VARCO FBX를 items.json의 source 위치로 가져오고 이름을 규칙대로 바꾼다
    (SM_<Item>_Src, M_<Item>, T_<Item>_BaseColor/Normal/ORM). 반환: 정적 메시 경로."""
    _not_playing()
    it = load_items()[item_key]
    dest_dir, sm_name = it["source"].rsplit("/", 1)
    src = import_fbx(fbx_path, dest_dir)
    renames = {src: f"{dest_dir}/{sm_name}"}
    for a in EAL.list_assets(dest_dir, recursive=False):
        p = a.split(".")[0]
        n = p.rsplit("/", 1)[1]
        new = {"material_0": f"M_{item_key}", "base_color": f"T_{item_key}_BaseColor",
               "normal": f"T_{item_key}_Normal", "orm": f"T_{item_key}_ORM"}.get(n)
        if new:
            renames[p] = f"{dest_dir}/{new}"
    for old, new in renames.items():
        if old != new and not EAL.does_asset_exist(new):
            EAL.rename_asset(old, new)
    # 이름 변경으로 남은 리디렉터 정리(머티리얼 → 텍스처 참조)
    reds = [a for a in EAL.list_assets(dest_dir, recursive=False)
            if str(EAL.find_asset_data(a).asset_class_path.asset_name) == "ObjectRedirector"]
    if reds:
        unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers([unreal.load_object(None, a) for a in reds])
    assign_source_material(item_key)
    return it["source"]


def assign_source_material(item_key):
    """원본 정적 메시의 머티리얼 슬롯을 M_<Item>으로 다시 지정하고 저장한다.
    (이름 변경 뒤 정적 메시의 슬롯이 비는 경우가 있었다 — MC2.2 천 상의, 결과 의상이 회색 기본 머티리얼로 나옴)"""
    it = load_items()[item_key]
    dest_dir = it["source"].rsplit("/", 1)[0]
    mat = unreal.load_asset(f"{dest_dir}/M_{item_key}")
    st = unreal.load_asset(it["source"])
    if mat is None or st is None:
        raise RuntimeError(f"원본 메시·머티리얼 없음: {it['source']}, M_{item_key}")
    for i in range(len(st.static_materials)):
        st.set_material(i, mat)
    EAL.save_loaded_asset(st)
    if any(s.material_interface is None for s in st.static_materials):
        raise RuntimeError(f"머티리얼 지정 실패: {it['source']}")
    return mat


def measure(item_path, body, float_cm=3.0, step=3):
    """수치 검사: 몸(키트 Body_Combined) 안으로 들어간 정점 %, 몸에서 float_cm 넘게 뜬 정점 %, 거리 p50/p95.
    (2026-11-05 몸 하나 + 마스킹 — 마스크로 남기는 손목·발목 근처도 보이므로 몸 전체를 기준으로 센다)"""
    bp = profiles.body(body)
    full, fbvh = SP.build_bvh_for_mesh(_dm_skel(bp["leader"]))
    ref, bvh = full, fbvh
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
    return {"verts_sampled": n, "tris": _tris(dm), "inside_body%": round(100.0 * inside / n, 1),
            f"float>{float_cm:g}cm%": round(100.0 * floating / n, 1),
            "dist_p50": round(dists[n // 2], 2), "dist_p95": round(dists[int(n * 0.95)], 2)}
