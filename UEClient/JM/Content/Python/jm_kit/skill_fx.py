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


def run(step, args):
    if step == "si_material":
        return build_indicator_material()
    if step == "si_preview":
        return preview_indicator(args)
    raise ValueError(f"모르는 단계 {step}")
