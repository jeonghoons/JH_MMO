# -*- coding: utf-8 -*-
"""SK2(D-278) 스킬 연출 에셋 도구 — WorldTools.world_step(name="", step="si_…")로 부른다.

si_material : 바닥 범위 표시 데칼 머티리얼 /Game/FX/Indicator/M_SkillIndicator 를 (다시) 만든다.
  모양(Shape 0 = 원·부채꼴, 1 = 직사각형), AngleDeg, Fill(0~1 — 시전 후 타격 순간까지 차오름), BorderU·BorderV(테두리 두께, UV 비율),
  EdgeColor/EdgeAlpha(조준 테두리 — 하늘색), FillColor/FillAlpha(시전 후 채움 — 주황), InnerAlpha(테두리 안쪽 옅은 바탕).
  데칼 UV를 -1~1로 펴서 계산한다: 원·부채꼴은 가운데가 시전자, 직사각형은 p.x = -1 쪽이 시전자(채움이 거기서 앞으로 나감).
  C++(AJMMyPlayer::UpdateSkillIndicator)가 데칼 크기·회전과 파라미터를 넣는다 — 파라미터 이름을 바꾸면 C++도 함께 고친다.
"""
import unreal

INDICATOR_DIR = "/Game/FX/Indicator"
INDICATOR_NAME = "M_SkillIndicator"

_CODE = r"""
// 데칼 UV 규칙에 기대지 않고 월드 위치에서 직접 -1~1 좌표를 만든다: x = 앞(Yaw 방향)/HalfF, y = 오른쪽/HalfS
float yr = radians(Yaw);
float2 fwd = float2(cos(yr), sin(yr));
float2 rgt = float2(-fwd.y, fwd.x);
float2 d = WP.xy - float2(CenterX, CenterY);
float2 p = float2(dot(d, fwd) / max(HalfF, 1.0), dot(d, rgt) / max(HalfS, 1.0));
float4 none = float4(0, 0, 0, 0);
float3 ec = EdgeColor.rgb;
float3 fc = FillColor.rgb;
float4 edgeC = float4(ec, EdgeAlpha);
float4 fillC = float4(fc, FillAlpha);
float4 innerC = float4(ec, InnerAlpha);
if (Shape < 0.5)
{
    float r = length(p);
    if (r > 1.0) return none;
    float ang = degrees(abs(atan2(p.y, p.x)));
    float hA = AngleDeg * 0.5;
    bool full = AngleDeg >= 359.0;
    if (!full && ang > hA) return none;
    bool edge = (r > 1.0 - BorderU) || (!full && (hA - ang) * 0.01745 * max(r, 0.001) < BorderU);
    if (Fill > 0.0 && r <= Fill) return edge ? float4(fc, min(1.0, FillAlpha + 0.25)) : fillC;
    return edge ? edgeC : innerC;
}
if (abs(p.x) > 1.0 || abs(p.y) > 1.0) return none;
float t = (p.x + 1.0) * 0.5;
bool edgeR = (abs(p.x) > 1.0 - BorderU) || (abs(p.y) > 1.0 - BorderV);
if (Fill > 0.0 && t <= Fill) return edgeR ? float4(fc, min(1.0, FillAlpha + 0.25)) : fillC;
return edgeR ? edgeC : innerC;
"""

# (이름, 종류, 기본값) — 종류 s = 스칼라, v = 벡터(RGB만 씀)
_PARAMS = [
    ("CenterX", "s", 0.0), ("CenterY", "s", 0.0), ("Yaw", "s", 0.0), ("HalfF", "s", 100.0), ("HalfS", "s", 100.0),
    ("Shape", "s", 0.0), ("AngleDeg", "s", 360.0), ("Fill", "s", 0.0),
    ("BorderU", "s", 0.04), ("BorderV", "s", 0.04),
    ("EdgeColor", "v", (0.35, 0.8, 1.0)), ("EdgeAlpha", "s", 0.9),
    ("FillColor", "v", (1.0, 0.45, 0.05)), ("FillAlpha", "s", 0.45),
    ("InnerAlpha", "s", 0.12),
]


def build_indicator_material():
    mel = unreal.MaterialEditingLibrary
    path = f"{INDICATOR_DIR}/{INDICATOR_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.load_asset(path)
        mel.delete_all_material_expressions(mat)
    else:
        if not unreal.EditorAssetLibrary.does_directory_exist(INDICATOR_DIR):
            unreal.EditorAssetLibrary.make_directory(INDICATOR_DIR)
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(INDICATOR_NAME, INDICATOR_DIR, unreal.Material, unreal.MaterialFactoryNew())

    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", _CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", "SkillIndicator")
    inputs = []
    for name in ["WP"] + [n for n, _, _ in _PARAMS]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    wp = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -700, -300)
    mel.connect_material_expressions(wp, "", custom, "WP")
    y = -100
    for name, kind, default in _PARAMS:
        if kind == "s":
            e = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, y)
            e.set_editor_property("default_value", float(default))
        else:
            e = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, y)
            e.set_editor_property("default_value", unreal.LinearColor(default[0], default[1], default[2], 1.0))
        e.set_editor_property("parameter_name", name)
        mel.connect_material_expressions(e, "", custom, name)
        y += 90

    rgb = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 0, -60)
    for k, v in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(k, v)
    alpha = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 0, 60)
    for k, v in (("r", False), ("g", False), ("b", False), ("a", True)):
        alpha.set_editor_property(k, v)
    mel.connect_material_expressions(custom, "", rgb, "")
    mel.connect_material_expressions(custom, "", alpha, "")
    mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return {"material": path, "params": [n for n, _, _ in _PARAMS]}


def preview_indicator(args):
    """열린 레벨(TestLevel)에 데칼 세 개(원 360°·부채꼴 180°·직사각형)를 yaw 0(+X 앞)으로 놓고 위에서 찍는다.
    +X 쪽에 빨간 막대를 둬 UV 방향을 확인한다. args: {"x": 기준 X(기본 8000), "fill": 0.5, "clear": true면 지우기만}."""
    from jm_kit import preview
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith("SI_PREVIEW"):
            eas.destroy_actor(a)
    if args.get("clear"):
        return {"cleared": True}
    bx = float(args.get("x", 8000.0))
    fill = float(args.get("fill", 0.5))
    mat = unreal.load_asset(f"{INDICATOR_DIR}/{INDICATOR_NAME}")
    made = []
    # (라벨, 중심 y, 모양, 각도, 데칼 반쪽 크기 (앞뒤, 좌우), 중심을 앞으로 미는 거리)
    cases = [("circle", -600.0, 0.0, 360.0, (250.0, 250.0), 0.0),
             ("sector", 0.0, 0.0, 180.0, (250.0, 250.0), 0.0),
             ("rect", 600.0, 1.0, 0.0, (400.0, 60.0), 400.0)]
    for label, cy, shape, ang, (hf, hs), push in cases:
        loc = unreal.Vector(bx + push, cy, 0.0)
        actor = eas.spawn_actor_from_class(unreal.DecalActor, loc, unreal.Rotator(-90.0, 0.0, 0.0))
        actor.set_actor_label(f"SI_PREVIEW_{label}")
        comp = actor.decal
        comp.set_decal_material(mat)
        comp.set_editor_property("decal_size", unreal.Vector(max(hf, hs), max(hf, hs), max(hf, hs)))   # 상자는 영역을 덮기만 하면 됨(모양은 머티리얼이 월드 위치로 계산)
        mid = comp.create_dynamic_material_instance()
        mid.set_scalar_parameter_value("CenterX", bx + push)
        mid.set_scalar_parameter_value("CenterY", cy)
        mid.set_scalar_parameter_value("Yaw", 0.0)
        mid.set_scalar_parameter_value("HalfF", hf)
        mid.set_scalar_parameter_value("HalfS", hs)
        mid.set_scalar_parameter_value("Shape", shape)
        mid.set_scalar_parameter_value("AngleDeg", ang)
        mid.set_scalar_parameter_value("Fill", fill)
        made.append(label)
        # 시전자 자리 표시(흰 공)와 +X 앞 표시(빨간 막대)
        dot = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Sphere"), unreal.Vector(bx, cy, 20.0))
        dot.set_actor_label(f"SI_PREVIEW_dot_{label}")
        dot.set_actor_scale3d(unreal.Vector(0.3, 0.3, 0.3))
        bar = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Cube"), unreal.Vector(bx + 150.0, cy, 20.0))
        bar.set_actor_label(f"SI_PREVIEW_bar_{label}")
        bar.set_actor_scale3d(unreal.Vector(1.5, 0.1, 0.1))
    shot = preview.shot("si_preview", (bx + 250.0, 0.0, 1800.0), (bx + 250.0, 0.0, 0.0), fov=70.0, w=1200, h=900)
    return {"made": made, "shot": shot}


ARROW_DIR = "/Game/FX/Proj/LightArrow"
ARROW_NAME = "Proj_LightArrow"
# 3003 빛의 화살(D-278): 날아갈 때 최종 크기 = 길이 3m, 굵기(가장 넓은 단면) 80cm = 판정 폭. 발사 순간 크기는 SkillTable ProjStartScale.
ARROW_LENGTH_CM = 300.0
ARROW_WIDTH_CM = 80.0
ARROW_GLOW = {"GlowColor": (0.45, 1.0, 0.65), "GlowIntensity": 20.0, "MetalMask": 0.0}


def import_light_arrow(args):
    """VARCO FBX → /Game/FX/Proj/LightArrow/SM_Proj_LightArrow.
    긴 축을 +X(진행 방향)로 돌리고, 길이 ARROW_LENGTH_CM·가장 넓은 단면 ARROW_WIDTH_CM·가운데 원점으로 굽는다(비균등 — 판정 폭에 맞춘 굵기).
    args: {"fbx": 경로, "flip": true면 앞뒤를 뒤집음(촉이 -X로 나왔을 때), "target_tris": 기본 6000}."""
    from jm_kit import garment, props
    EAL = unreal.EditorAssetLibrary
    T = unreal.GeometryScript_MeshTransforms
    A = unreal.GeometryScript_AssetUtils
    garment._not_playing()
    if EAL.does_directory_exist(ARROW_DIR):
        for a in EAL.list_assets(ARROW_DIR, recursive=False):
            EAL.delete_asset(a.split(".")[0])
    src = garment.import_fbx(args["fbx"], ARROW_DIR)
    names = {"mesh": f"SM_{ARROW_NAME}", "material": f"MI_{ARROW_NAME}", "BaseColor": f"T_{ARROW_NAME}_BaseColor",
             "Normal": f"T_{ARROW_NAME}_Normal", "ORM": f"T_{ARROW_NAME}_ORM"}
    found, notes = {}, []
    for a in EAL.list_assets(ARROW_DIR, recursive=False):
        p = a.split(".")[0]
        kind = props._kind_of(unreal.load_asset(p), p.rsplit("/", 1)[1])
        if kind is None or kind in found:
            notes.append(f"건너뜀: {p}")
            continue
        found[kind] = p
    for kind, p in list(found.items()):
        new = f"{ARROW_DIR}/{names[kind]}"
        if p != new and EAL.rename_asset(p, new):
            found[kind] = new
    sm_path = found.get("mesh", src)
    sm = unreal.load_asset(sm_path)

    # 축·크기·원점 굽기: 가장 긴 축 → X, 나머지 두 축은 가장 넓은 쪽이 ARROW_WIDTH_CM가 되게 같은 비율로
    b = sm.get_bounds()
    ext = [b.box_extent.x, b.box_extent.y, b.box_extent.z]
    long_axis = max(range(3), key=lambda i: ext[i])
    dm = garment._dm_static(sm_path)
    dm = T.translate_mesh(dm, unreal.Vector(-b.origin.x, -b.origin.y, -b.origin.z))
    if long_axis == 1:
        dm = T.rotate_mesh(dm, unreal.Rotator(yaw=-90.0), unreal.Vector(0, 0, 0))   # Y → X
    elif long_axis == 2:
        dm = T.rotate_mesh(dm, unreal.Rotator(pitch=90.0), unreal.Vector(0, 0, 0))   # Z → X
    if args.get("flip"):
        dm = T.rotate_mesh(dm, unreal.Rotator(yaw=180.0), unreal.Vector(0, 0, 0))
    bb = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dm)
    size = [bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z]
    sx = ARROW_LENGTH_CM / max(size[0], 0.01)
    sw = ARROW_WIDTH_CM / max(size[1], size[2], 0.01)
    dm = T.scale_mesh(dm, unreal.Vector(sx, sw, sw), unreal.Vector(0, 0, 0), True)
    bb = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dm)
    dm = T.translate_mesh(dm, unreal.Vector(-(bb.max.x + bb.min.x) / 2, -(bb.max.y + bb.min.y) / 2, -(bb.max.z + bb.min.z) / 2))
    opt = unreal.GeometryScriptCopyMeshToAssetOptions()
    opt.set_editor_property("replace_materials", False)
    _dm, outcome = A.copy_mesh_to_static_mesh(dm, sm, opt, unreal.GeometryScriptMeshWriteLOD())
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError("메시 쓰기 실패")
    lods = garment.reduce_static_lods(sm_path, int(args.get("target_tris", 6000)))

    # 재질: 03등급 발광 부모(M_Equip_PBR_Glow)의 인스턴스 — 몸 전체가 에메랄드로 빛나게(MetalMask 0)
    mel = unreal.MaterialEditingLibrary
    mi_path = f"{ARROW_DIR}/{names['material']}"
    mi = unreal.load_asset(found["material"]) if "material" in found else None
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        if EAL.does_asset_exist(mi_path):
            EAL.delete_asset(mi_path)
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(names["material"], ARROW_DIR, unreal.MaterialInstanceConstant,
                                                                   unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, unreal.load_asset(garment.ensure_glow_master()))
    mel.clear_all_material_instance_parameters(mi)
    for param in ("BaseColor", "Normal", "ORM"):
        if param in found:
            tex = unreal.load_asset(found[param])
            if param == "ORM":
                tex.set_editor_property("srgb", False)
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                EAL.save_loaded_asset(tex)
            mel.set_material_instance_texture_parameter_value(mi, param, tex)
        else:
            notes.append(f"텍스처 없음: {param}")
    for k, v in {**garment.GLOW_DEFAULTS, **ARROW_GLOW}.items():
        if isinstance(v, tuple):
            mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
        else:
            mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    mel.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    for i in range(len(sm.static_materials)):
        sm.set_material(i, mi)
    EAL.save_loaded_asset(sm)
    nb = sm.get_bounds()
    return {"mesh": sm_path, "material": mi.get_path_name(), "long_axis_src": "XYZ"[long_axis], "lods": lods,
            "size_cm": [round(nb.box_extent.x * 2, 1), round(nb.box_extent.y * 2, 1), round(nb.box_extent.z * 2, 1)], "notes": notes}


def shot_light_arrow(args):
    """빛의 화살 메시를 열린 레벨(TestLevel)에 놓고 옆·위에서 찍는다(+X = 진행 방향 확인용 빨간 막대). args: {"clear": true}면 지우기만."""
    from jm_kit import preview
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith("SI_ARROW"):
            eas.destroy_actor(a)
    if args.get("clear"):
        return {"cleared": True}
    bx = float(args.get("x", 8000.0))
    arrow = eas.spawn_actor_from_object(unreal.load_asset(f"{ARROW_DIR}/SM_{ARROW_NAME}"), unreal.Vector(bx, 0.0, 120.0))
    arrow.set_actor_label("SI_ARROW_mesh")
    bar = eas.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Cube"), unreal.Vector(bx + 250.0, 0.0, 20.0))
    bar.set_actor_label("SI_ARROW_bar")
    bar.set_actor_scale3d(unreal.Vector(1.0, 0.1, 0.1))
    side = preview.shot("si_arrow_side", (bx, -500.0, 150.0), (bx, 0.0, 120.0), fov=70.0, w=1200, h=700)
    top = preview.shot("si_arrow_top", (bx, -1.0, 700.0), (bx, 0.0, 120.0), fov=70.0, w=1200, h=700)
    return {"side": side, "top": top}


# SK2 동작 비교용으로 가져왔지만 쓰지 않는 공격 동작(사용자 결정 2026-11-23: 따로 모아 git 제외) — (무기 폴더, 이름 끝부분)
UNUSED_ANIMS = {
    "OneHandSword": ["SwordAndShieldAttack2", "SwordAndShieldAttack4", "SwordAndShieldSlash2", "SwordAndShieldSlash3", "SwordAndShieldSlash4",
                     "SwordAndShieldSlash5", "SwordAndShieldKick", "SwordAndShieldCasting", "SwordAndShieldCasting2", "SwordAndShieldPowerUp"],
    "TwoHandSword": ["GreatSwordAttack", "GreatSwordSlideAttack", "GreatSwordKick", "GreatSwordKick2", "GreatSwordCasting", "GreatSwordPowerUp",
                     "SpellCast"],
    "Bow": ["StandingMeleeKick", "StandingMeleePunch", "StandingDodgeBackward", "StandingDiveForward"],
}
MANNY_ANIMS = "/Game/Character/Mannequin/Anims"
MIXAMO_ANIMS = "/Game/Character/Mixamo/Anims"


def move_unused_anims(args):
    """UNUSED_ANIMS의 AS_(매니퀸)·MX_(Mixamo 원본)를 각 Anims/_Unused/<무기>/로 옮긴다. 참조가 있으면 옮기지 않고 보고한다.
    args: {"dry_run": true}면 대상·참조만 보고."""
    EAL = unreal.EditorAssetLibrary
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    moved, skipped = [], []
    for weapon, names in UNUSED_ANIMS.items():
        for n in names:
            for root, src_name in ((MANNY_ANIMS, f"AS_{weapon}_{n}"), (MIXAMO_ANIMS, f"MX_{n}")):
                src = f"{root}/{weapon}/{src_name}"
                if not EAL.does_asset_exist(src):
                    skipped.append(f"없음 {src}")
                    continue
                refs = [str(r) for r in reg.get_referencers(src.rsplit(".", 1)[0], unreal.AssetRegistryDependencyOptions())]
                refs = [r for r in refs if not r.startswith(f"{root}/_Unused")]
                if refs:
                    skipped.append(f"참조 있음 {src}: {refs}")
                    continue
                dst = f"{root}/_Unused/{weapon}/{src_name}"
                if args.get("dry_run"):
                    moved.append(dst)
                elif EAL.rename_asset(src, dst):
                    moved.append(dst)
                else:
                    skipped.append(f"이동 실패 {src}")
    return {"moved": len(moved), "list": moved, "skipped": skipped}


def run(step, args):
    if step == "si_unused_move":
        return move_unused_anims(args)
    if step == "si_arrow_import":
        return import_light_arrow(args)
    if step == "si_arrow_shot":
        return shot_light_arrow(args)
    if step == "si_material":
        return build_indicator_material()
    if step == "si_preview":
        return preview_indicator(args)
    raise ValueError(f"모르는 단계 {step}")
