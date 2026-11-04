"""장비 메시의 뼈대가 키트와 같은지 검사한다 (MC1.3).

Leader Pose는 본 이름으로 리더의 포즈를 따라가지만, 정점은 **자기 메시의 바인드 포즈** 기준으로 스키닝된다.
바인드 포즈가 리더(키트 몸)와 다르면 팔을 돌릴수록 부위·의상이 몸에서 벌어진다(MC2.2 첫 시도 실패 원인:
생성 메시가 프로젝트 SK_Mannequin 기준 자세 161본으로 바인딩, 키트는 89본·여자 어깨 x 14.8 vs 19.0).

검사 항목 (하나라도 다르면 실패):
  1. 본 개수  2. 본 이름·순서  3. 부모 인덱스  4. 바인드 포즈(컴포넌트 공간) 위치·회전
  5. 가중치가 실린 본이 모두 리더에 있음

예)
  import importlib, jm_kit.skeleton_check as sc; importlib.reload(sc)
  sc.check("/Game/Equipment/Cloth/Top/SK_Cloth_Top_M", "M", slot="top")   # 기준 = 키트 상의
  sc.self_test()                                                           # 키트 자체 + 실패해야 하는 경우
"""
import math

import unreal

from . import profiles

BW = unreal.GeometryScript_BoneWeights
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
POS_TOL_CM = 0.01
# 키트 자체도 부위·의상과 리더의 손가락 본 회전이 최대 0.07° 다르다(위치는 같음, 애니메이션 정상 — MC1.3 측정).
# 잡아야 할 경우는 훨씬 크다(템플릿 Manny vs 키트 남자: 0.92cm / 8.08°).
ROT_TOL_DEG = 0.5


def _dm(mesh_path, lod=0):
    sk = unreal.load_asset(mesh_path)
    if sk is None:
        raise RuntimeError(f"메시 없음: {mesh_path}")
    dm = unreal.DynamicMesh()
    read = unreal.GeometryScriptMeshReadLOD()
    read.lod_index = lod
    dm, _ = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(sk, dm, unreal.GeometryScriptCopyMeshFromAssetOptions(), read)
    return dm


def bones(mesh_path):
    """[(이름, 부모 인덱스, 컴포넌트 공간 바인드 Transform)] — 메시의 참조 스켈레톤 순서."""
    dm, infos = BW.get_all_bones_info(_dm(mesh_path))
    return [(str(i.name), i.parent_index, i.world_transform) for i in infos]


def _rot_diff_deg(a, b):
    qa, qb = a.rotation, b.rotation
    dot = abs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w)
    return math.degrees(2.0 * math.acos(min(1.0, dot)))


def _weighted_bones(mesh_path):
    dm = _dm(mesh_path)
    dm, infos = BW.get_all_bones_info(dm)
    names = {i.index: str(i.name) for i in infos}
    dm, vids, _ = Q.get_all_vertex_i_ds(dm)
    used = set()
    for v in L.convert_index_list_to_array(vids):
        dm, ws, _ = BW.get_vertex_bone_weights(dm, v)
        used.update(names.get(w.bone_index) for w in ws if w.weight > 0.0)
    return used


def reference(body, slot=None):
    """검사 기준 메시: 슬롯에 키트 의상이 있으면 그 의상, 없으면(장갑 등) 리더."""
    bp = profiles.body(body)
    return bp["outfit"].get(slot) or bp["leader"]


def compare(mesh_path, ref_path, leader_path=None, max_report=8):
    """mesh와 ref의 뼈대 비교. 반환: {"ok", "errors": [...], 요약}."""
    a, b = bones(mesh_path), bones(ref_path)
    errors = []
    if len(a) != len(b):
        errors.append(f"본 개수 {len(a)} ≠ 기준 {len(b)}")
    na, nb = [x[0] for x in a], [x[0] for x in b]
    if na != nb:
        extra = [n for n in na if n not in nb]
        missing = [n for n in nb if n not in na]
        if extra or missing:
            errors.append(f"본 이름 다름 — 추가 {extra[:max_report]}{'…' if len(extra) > max_report else ''} / 없음 {missing[:max_report]}")
        else:
            errors.append("본 순서 다름")
    else:
        pa = [x[1] for x in a]
        if pa != [x[1] for x in b]:
            errors.append("부모 인덱스 다름")
        worst_p = worst_r = (0.0, "")
        for (n, _p, ta), (_n, _q, tb) in zip(a, b):
            d = math.dist((ta.translation.x, ta.translation.y, ta.translation.z), (tb.translation.x, tb.translation.y, tb.translation.z))
            r = _rot_diff_deg(ta, tb)
            worst_p = max(worst_p, (d, n))
            worst_r = max(worst_r, (r, n))
        if worst_p[0] > POS_TOL_CM:
            errors.append(f"바인드 위치 다름 — 최대 {worst_p[0]:.2f}cm ({worst_p[1]})")
        if worst_r[0] > ROT_TOL_DEG:
            errors.append(f"바인드 회전 다름 — 최대 {worst_r[0]:.2f}° ({worst_r[1]})")
    if leader_path:
        leader = {x[0] for x in bones(leader_path)}
        outside = sorted(_weighted_bones(mesh_path) - leader)
        if outside:
            errors.append(f"리더에 없는 본에 가중치 {outside[:max_report]}")
    return {"ok": not errors, "mesh": mesh_path, "reference": ref_path, "bones": len(a), "errors": errors}


def check(mesh_path, body, slot=None):
    """장비 메시 검사: 기준 = 키트 의상(슬롯) 또는 리더, 가중치 본은 리더에 있어야 함."""
    return compare(mesh_path, reference(body, slot), profiles.body(body)["leader"])


def self_test():
    """키트 자체는 통과, 알려진 잘못된 경우는 실패해야 한다. 반환: (전체 통과 여부, 줄 목록)."""
    lines, ok = [], True
    for body in ("M", "F"):
        bp = profiles.body(body)
        for name, path in list(bp["parts"].items()) + [(f"outfit_{k}", v) for k, v in bp["outfit"].items()]:
            if name == "hair":
                continue   # 헤어는 자체 스켈레톤(머리 본만) — 키트도 리더 포즈로 쓰지만 89본 기준과는 다름
            r = compare(path, bp["leader"], bp["leader"])
            ok &= r["ok"]
            lines.append(f"[{'통과' if r['ok'] else '실패'}] {body} {name}: {'; '.join(r['errors']) or '같음'}")
    must_fail = [
        ("템플릿 Manny(SK_Mannequin 161본)", "/Game/Character/Mannequin/Meshes/SKM_Manny_Simple", profiles.body("M")["leader"]),
        ("남자 상의 vs 여자 리더(바인드 다름)", profiles.body("M")["outfit"]["top"], profiles.body("F")["leader"]),
    ]
    for label, mesh, ref in must_fail:
        if not unreal.EditorAssetLibrary.does_asset_exist(mesh):
            lines.append(f"[건너뜀] {label}: 메시 없음 {mesh}")
            continue
        r = compare(mesh, ref)
        ok &= not r["ok"]
        lines.append(f"[{'통과' if not r['ok'] else '실패'}] 실패해야 함 — {label}: {'; '.join(r['errors']) or '차이 없음(검사가 못 잡음)'}")
    return ok, lines
