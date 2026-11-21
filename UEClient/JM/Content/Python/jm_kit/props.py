"""맵 소품(L2d 마을 등): VARCO로 만든 FBX를 정적 메시로 가져와 /Game/Environment/<테마>/<분류>/<이름>/에 정리한다(D-245).

테마 폴더: Village/Props·Village/Architecture(성벽·문)·Dungeon/Props·Common/Props(여러 테마에서 씀). name에 "Village/Props/Cart"처럼
경로를 주거나, 이미 있는 소품이면 이름만 줘도 된다(테마 폴더에서 찾음).

장비용 garment.import_source와 같은 흐름이지만 items.json과 무관하다:
FBX 가져오기(짧은 경로) → 이름 규칙(SM_<이름>, M_<이름>, T_<이름>_BaseColor/Normal/ORM) → 장비 공용 PBR(M_Equip_PBR) 인스턴스 → LOD 3단계.
배치는 jm_kit.world의 dress(kind "prop"·"ring_prop")가 한다. MCP: world_step(name, "prop_import" / "prop_info", args).
"""

import unreal

from jm_kit import garment

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
PROP_ROOT = "/Game/Environment"
THEMES = ("Village/Props", "Village/Architecture", "Dungeon/Props", "Common/Props")


def prop_dir(name):
    """소품 폴더 경로. name이 "테마/분류/이름"이면 그대로, 이름만이면 테마 폴더에서 찾는다(없으면 테마를 적으라고 실패)."""
    if "/" in name:
        return f"{PROP_ROOT}/{name}"
    for th in THEMES:
        if EAL.does_directory_exist(f"{PROP_ROOT}/{th}/{name}"):
            return f"{PROP_ROOT}/{th}/{name}"
    raise RuntimeError(f"소품 폴더 없음: {name} — 새 소품은 테마를 붙여 주세요(예 Village/Props/{name}, 테마: {THEMES})")


def _kind_of(asset, asset_name):
    """가져온 에셋이 무엇인지(메시·머티리얼·텍스처 종류). 모르면 None."""
    n = asset_name.lower()
    if isinstance(asset, unreal.StaticMesh):
        return "mesh"
    if isinstance(asset, unreal.MaterialInterface):
        return "material"
    if isinstance(asset, unreal.Texture):
        if "normal" in n:
            return "Normal"
        if "orm" in n or "rough" in n or "metal" in n or "occlusion" in n:
            return "ORM"
        if "base" in n or "color" in n or "albedo" in n or "diffuse" in n:
            return "BaseColor"
    return None


def import_prop(fbx_path, name, target_tris=None):
    """FBX 하나를 소품으로 가져온다(같은 이름이 있으면 덮어씀). target_tris를 주면 LOD0을 그 수로 줄이고 LOD1·2를 50·25%로 만든다.
    반환: 메시 경로, 삼각형 수, 크기(m), 머티리얼·텍스처 지정 결과, 못 한 것(notes)."""
    garment._not_playing()
    dest = prop_dir(name)
    name = name.rsplit("/", 1)[-1]
    notes = []
    # 다시 가져올 때는 폴더를 먼저 비운다. 안 비우면 옛 M_/T_ 에셋이 먼저 잡혀 새 메시에 옛 텍스처가 물린다(2026-11-18 목책 재생성).
    # 레벨에 놓인 액터가 이 메시를 쓰고 있으면 참조가 끊기므로 가져온 뒤 dress를 다시 부를 것.
    if EAL.does_directory_exist(dest):
        for a in EAL.list_assets(dest, recursive=False):
            EAL.delete_asset(a.split(".")[0])
    src = garment.import_fbx(fbx_path, dest)
    names = {"mesh": f"SM_{name}", "material": f"M_{name}", "BaseColor": f"T_{name}_BaseColor", "Normal": f"T_{name}_Normal", "ORM": f"T_{name}_ORM"}
    found = {}
    for a in EAL.list_assets(dest, recursive=False):
        p = a.split(".")[0]
        kind = _kind_of(unreal.load_asset(p), p.rsplit("/", 1)[1])
        if kind is None:
            notes.append(f"분류 못 함: {p}")
            continue
        if kind in found and found[kind] != p:
            notes.append(f"{kind}가 둘 이상: {p}")
            continue
        found[kind] = p
    for kind, p in list(found.items()):
        new = f"{dest}/{names[kind]}"
        if p != new:
            if EAL.does_asset_exist(new):
                EAL.delete_asset(new)
            if EAL.rename_asset(p, new):
                found[kind] = new
            else:
                notes.append(f"이름 변경 실패: {p} → {new}")
    reds = [a for a in EAL.list_assets(dest, recursive=False) if str(EAL.find_asset_data(a).asset_class_path.asset_name) == "ObjectRedirector"]
    if reds:
        unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers([unreal.load_object(None, a) for a in reds])

    # 머티리얼: 장비 공용 PBR 인스턴스로(UE 5.8 FBX 가져오기가 만드는 인스턴스는 ORM을 쓰지 않아 금속이 검게 보인다 — MC2.3)
    mat = unreal.load_asset(found["material"]) if "material" in found else None
    if isinstance(mat, unreal.MaterialInstanceConstant) and all(k in found for k in ("BaseColor", "Normal", "ORM")):
        orm = unreal.load_asset(found["ORM"])
        orm.set_editor_property("srgb", False)
        orm.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        EAL.save_loaded_asset(orm)
        MEL.set_material_instance_parent(mat, unreal.load_asset(garment.ensure_pbr_master()))
        MEL.clear_all_material_instance_parameters(mat)
        for param in ("BaseColor", "Normal", "ORM"):
            MEL.set_material_instance_texture_parameter_value(mat, param, unreal.load_asset(found[param]))
        MEL.update_material_instance(mat)
        EAL.save_loaded_asset(mat)
    else:
        notes.append(f"공용 PBR로 바꾸지 못함(머티리얼 {type(mat).__name__}, 텍스처 {sorted(k for k in found if k not in ('mesh', 'material'))})")

    sm_path = found.get("mesh", src)
    sm = unreal.load_asset(sm_path)
    if mat is not None:
        for i in range(len(sm.static_materials)):
            sm.set_material(i, mat)
    lods = garment.reduce_static_lods(sm_path, target_tris) if target_tris else {"source_tris": sm.get_num_triangles(0), "lod_tris": [sm.get_num_triangles(0)]}
    EAL.save_loaded_asset(sm)
    out = prop_info(name)
    out.update({"lods": lods, "notes": notes})
    return out


def prop_info(name):
    """소품 메시의 크기(m)·경계 중심(m)·LOD별 삼각형·머티리얼 수. 배치 배율과 방향을 정할 때 본다."""
    d = prop_dir(name)
    name = name.rsplit("/", 1)[-1]
    path = f"{d}/SM_{name}"
    sm = unreal.load_asset(path)
    if sm is None:
        return {"error": f"메시 없음: {path}"}
    b = sm.get_bounds()
    return {"mesh": path,
            "size_m": [round(b.box_extent.x * 0.02, 3), round(b.box_extent.y * 0.02, 3), round(b.box_extent.z * 0.02, 3)],
            "center_m": [round(b.origin.x * 0.01, 3), round(b.origin.y * 0.01, 3), round(b.origin.z * 0.01, 3)],
            "tris": [sm.get_num_triangles(i) for i in range(sm.get_num_lods())],
            "materials": len(sm.static_materials)}
