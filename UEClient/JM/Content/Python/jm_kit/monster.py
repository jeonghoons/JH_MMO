"""몬스터 스켈레탈 메시·애니메이션 가져오기(MN1, DECISIONS D-167).

VARCO 3D에서 리깅(+동작)한 FBX는 메시·뼈대·애니메이션이 한 파일에 들어 있다.
  1. import_mesh(리깅 FBX, "Wolf") → /Game/Character/Monster/Wolf/SKM_Wolf (+ 스켈레톤·머티리얼·텍스처)
  2. import_anims(동작 FBX 목록, "Wolf") → /Game/Character/Monster/Wolf/Anims/AS_Wolf_<이름> (같은 스켈레톤, 메시는 다시 가져오지 않음)
  3. info("Wolf") → 뼈 수·크기·시퀀스 길이 확인

FBX 원본은 UEClient/JM/SourceArt/Monster/<이름>/(*.fbx는 git 제외)에 둔다 — 경로가 길면 가져오기가 조용히 실패한다.
"""
import os
import re

import unreal

EAL = unreal.EditorAssetLibrary

ROOT = "/Game/Character/Monster"


def _dir(name):
    return f"{ROOT}/{name}"


def _mesh_path(name):
    return f"{_dir(name)}/SKM_{name}"


def _camel(stem):
    words = re.findall(r"[A-Za-z0-9]+", stem)
    return "".join(w[:1].upper() + w[1:] for w in words)


def _import(fbx_path, dest_dir, dest_name, ui):
    if not os.path.isfile(fbx_path):
        raise FileNotFoundError(fbx_path)
    t = unreal.AssetImportTask()
    t.filename = fbx_path
    t.destination_path = dest_dir
    t.destination_name = dest_name
    t.automated = True
    t.save = True
    t.replace_existing = True
    t.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    return [p.split(".")[0] for p in t.imported_object_paths]


def _mesh_info(mesh):
    b = mesh.get_bounds()
    ext = b.box_extent
    sk = mesh.get_editor_property("skeleton")
    mats = [str(m.get_editor_property("material_interface").get_path_name()) if m.get_editor_property("material_interface") else None
            for m in mesh.get_editor_property("materials")]
    return {
        "mesh": mesh.get_path_name().split(".")[0],
        "skeleton": sk.get_path_name().split(".")[0] if sk else None,
        "size_cm": [round(ext.x * 2, 1), round(ext.y * 2, 1), round(ext.z * 2, 1)],
        "origin_cm": [round(b.origin.x, 1), round(b.origin.y, 1), round(b.origin.z, 1)],
        "materials": mats,
    }


def import_mesh(fbx_path, name, uniform_scale=1.0):
    """리깅된 FBX → SKM_<name>(+ 스켈레톤·머티리얼·텍스처). 반환: {mesh, skeleton, size_cm, materials, imported}."""
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_as_skeletal = True
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    o.import_animations = False
    o.import_materials = True
    o.import_textures = True
    o.create_physics_asset = False
    o.automated_import_should_detect_type = False
    o.skeletal_mesh_import_data.set_editor_property("import_uniform_scale", float(uniform_scale))
    paths = _import(fbx_path, _dir(name), f"SKM_{name}", o)
    mesh = unreal.load_asset(_mesh_path(name))
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"스켈레탈 메시 가져오기 실패: {fbx_path} → {paths}")
    out = _mesh_info(mesh)
    out["imported"] = paths
    return out


def import_anims(fbx_paths, name, uniform_scale=1.0):
    """동작 FBX(메시 포함이어도 애니메이션만) → Anims/AS_<name>_<파일 이름>. 반환: [{sequence, seconds, frames}]."""
    mesh = unreal.load_asset(_mesh_path(name))
    if mesh is None:
        raise RuntimeError(f"{_mesh_path(name)} 없음 — import_mesh 먼저")
    sk = mesh.get_editor_property("skeleton")
    dest = f"{_dir(name)}/Anims"
    out = []
    for f in fbx_paths:
        o = unreal.FbxImportUI()
        o.import_mesh = False
        o.import_as_skeletal = True
        o.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
        o.import_animations = True
        o.import_materials = False
        o.import_textures = False
        o.skeleton = sk
        o.automated_import_should_detect_type = False
        o.anim_sequence_import_data.set_editor_property("import_uniform_scale", float(uniform_scale))
        asset_name = f"AS_{name}_" + _camel(os.path.splitext(os.path.basename(f))[0])
        paths = _import(f, dest, asset_name, o)
        seqs = [p for p in paths if isinstance(unreal.load_asset(p), unreal.AnimSequence)]
        if not seqs:
            raise RuntimeError(f"애니메이션 가져오기 실패: {f} → {paths}")
        out += [_seq_info(p) for p in seqs]
    return out


def _seq_info(path):
    s = unreal.load_asset(path)
    return {"sequence": path, "seconds": round(s.get_play_length(), 3),
            "frames": unreal.AnimationLibrary.get_num_frames(s)}


def info(name):
    """가져온 몬스터의 메시 크기·뼈 수·시퀀스 목록."""
    mesh = unreal.load_asset(_mesh_path(name))
    if mesh is None:
        raise RuntimeError(f"{_mesh_path(name)} 없음")
    out = _mesh_info(mesh)
    out["bones"] = [str(b) for b in mesh.get_editor_property("skeleton").get_reference_pose().get_bone_names()]
    anim_dir = f"{_dir(name)}/Anims"
    out["anims"] = [_seq_info(p.split(".")[0]) for p in EAL.list_assets(anim_dir, recursive=False)
                    if isinstance(unreal.load_asset(p.split(".")[0]), unreal.AnimSequence)] if EAL.does_directory_exist(anim_dir) else []
    return out
