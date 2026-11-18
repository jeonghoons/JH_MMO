"""텍스처 원본 줄이기 (L2f — 팩 텍스처가 장당 100MB를 넘어 GitHub에 올릴 수 없어서, D-192).

uasset 안에 든 원본 비트맵 자체를 줄인다(엔진의 TextureAssetActions — 에디터 "Resize Source"와 같은 기능).
`MaxTextureSize`·`LODBias`는 굽는 크기만 바꾸고 파일 크기는 그대로라 쓰지 않는다.
엔진이 LODBias를 함께 낮춰 주므로 화면에 나오는 크기는 "원래 나오던 크기와 max_size 중 작은 쪽"이 된다.
"""
import os

import unreal

AR = unreal.AssetRegistryHelpers.get_asset_registry()


def _file_mb(pkg):
    try:
        path = unreal.PackageName.long_package_name_to_filename(pkg, ".uasset")
    except Exception:
        path = os.path.join(unreal.Paths.project_content_dir(), pkg[len("/Game/"):] + ".uasset")
    return round(os.path.getsize(path) / 1048576.0, 1) if os.path.exists(path) else None


def _state(tex):
    row = {}
    for k in ("compression_settings", "srgb", "lod_bias", "max_texture_size", "lod_group", "mip_gen_settings"):
        try:
            row[k] = str(tex.get_editor_property(k))
        except Exception:
            pass
    try:
        row["built"] = "%dx%d" % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y())
    except Exception:
        pass
    return row


def reduce(packages, max_size=2048, dry_run=True):
    """packages 가운데 텍스처의 원본을 긴 변 max_size 이하로 줄이고 저장한다.
    dry_run이면 대상 목록(파일 크기·현재 설정)만 돌려준다. 압축 설정·sRGB가 바뀌면 changed_settings에 적는다."""
    rows, errors, changed_settings = [], [], []
    before_mb = after_mb = 0.0
    for pkg in packages:
        assets = AR.get_assets_by_package_name(unreal.Name(pkg))
        if not assets or not any(str(a.asset_class_path.asset_name).startswith("Texture") for a in assets):
            continue
        tex = assets[0].get_asset()
        if not isinstance(tex, unreal.Texture):
            continue
        mb0 = _file_mb(pkg) or 0.0
        st0 = _state(tex)
        row = {"pkg": pkg, "mb_before": mb0, "before": st0}
        before_mb += mb0
        if dry_run:
            after_mb += mb0
            rows.append(row)
            continue
        res = unreal.TextureAssetActionsBlueprintFns.resize_texture_source(tex, int(max_size))
        row["result"] = str(res).split(".")[-1].split(":")[0]
        if res == unreal.TextureAssetActionsBlueprintReturn.ERROR:
            errors.append(pkg)
        elif res == unreal.TextureAssetActionsBlueprintReturn.SUCCESS:
            unreal.EditorAssetLibrary.save_loaded_asset(tex, False)
        st1 = _state(tex)
        mb1 = _file_mb(pkg) or 0.0
        after_mb += mb1
        row["mb_after"] = mb1
        row["after"] = st1
        for k in ("compression_settings", "srgb", "lod_group"):
            if st0.get(k) != st1.get(k):
                changed_settings.append({"pkg": pkg, "key": k, "from": st0.get(k), "to": st1.get(k)})
        rows.append(row)
    rows.sort(key=lambda r: -r["mb_before"])
    return {"dry_run": dry_run, "max_size": max_size, "textures": len(rows), "mb_before": round(before_mb, 1),
            "mb_after": round(after_mb, 1), "errors": errors, "changed_settings": changed_settings, "rows": rows}
