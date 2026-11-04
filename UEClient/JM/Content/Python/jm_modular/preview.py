"""모듈형 캐릭터 시각 검증 도구 (MC1.3·MC2.1에서 사용, MC2.2·MC4에서 재사용).

- 테스트 레벨(기본 /Game/Dev/MC/L_BodyTest, git 제외)에 몸 부위 + 의상을 리더 포즈로 조립
- SceneCapture2D로 PNG 저장 → Claude가 Read로 확인 (에디터 뷰포트가 가려져 있어도 동작)
- 애니메이션은 에디터에서 틱하지 않으므로 Simulate(ue_play) 중에 shot_game()으로 찍는다.
  캡처 액터 "T_CapG"는 Simulate 시작 전에 레벨에 있어야 한다(setup_level이 만든다).
"""
import unreal

from . import body_parts

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAL = unreal.EditorAssetLibrary
SHOT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir() + "MCShots")
DEV = "/Game/Dev/MC"
LEVEL = f"{DEV}/L_BodyTest"
DEBUG_COLORS = {"head": (0.9, 0.2, 0.2), "hair": (0.5, 0.3, 0.1), "torso": (0.2, 0.7, 0.2), "arms": (0.2, 0.3, 0.9),
                "hands": (0.9, 0.8, 0.1), "legs": (0.7, 0.2, 0.8), "feet": (0.1, 0.8, 0.8)}
KW = unreal.AttachmentRule.KEEP_WORLD


def setup_level():
    """테스트 레벨을 열거나 만든다(조명·바닥·Simulate용 캡처 액터 포함)."""
    if UES.get_game_world() is not None:
        raise RuntimeError("Simulate 중에는 레벨을 열 수 없음 — stop_simulate() 후 다음 호출에서 실행")
    if EAL.does_asset_exist(LEVEL):
        LES.load_level(LEVEL)
    else:
        LES.new_level(LEVEL)
        sun = EAS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, -35, -110))
        sun.set_actor_label("T_Sun")
        EAS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300)).set_actor_label("T_Sky")
        floor = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
        floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
        floor.set_actor_scale3d(unreal.Vector(40, 40, 1))
        floor.set_actor_label("T_Floor")
    if not any(a.get_actor_label() == "T_CapG" for a in EAS.get_all_level_actors()):
        EAS.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 600, 120), unreal.Rotator(0, 0, -90)).set_actor_label("T_CapG")
    LES.save_current_level()
    return LEVEL


def clear(prefix):
    for a in EAS.get_all_level_actors():
        if a.get_actor_label().startswith(prefix):
            EAS.destroy_actor(a)


def debug_material(part):
    """부위별 단색(Unlit) 머티리얼 인스턴스."""
    mpath = f"{DEV}/M_DebugColor"
    if not EAL.does_asset_exist(mpath):
        at = unreal.AssetToolsHelpers.get_asset_tools()
        mel = unreal.MaterialEditingLibrary
        m = at.create_asset("M_DebugColor", DEV, unreal.Material, unreal.MaterialFactoryNew())
        p = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -400, 0)
        p.set_editor_property("parameter_name", "Color")
        fr = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -400, 300)
        one = mel.create_material_expression(m, unreal.MaterialExpressionOneMinus, -250, 300)
        mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -100, 100)
        mel.connect_material_expressions(fr, "", one, "")
        mel.connect_material_expressions(p, "", mul, "A")
        mel.connect_material_expressions(one, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        m.set_editor_property("two_sided", True)
        mel.recompile_material(m)
        EAL.save_loaded_asset(m)
    ipath = f"{DEV}/MI_Debug_{part}"
    if EAL.does_asset_exist(ipath):
        return unreal.load_asset(ipath)
    at = unreal.AssetToolsHelpers.get_asset_tools()
    mi = at.create_asset(f"MI_Debug_{part}", DEV, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", unreal.load_asset(mpath))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*DEBUG_COLORS[part], 1.0))
    EAL.save_loaded_asset(mi)
    return mi


def _follower(prefix, label, mesh, lead, loc):
    a = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(*loc))
    a.set_actor_label(f"{prefix}_{label}")
    c = a.skeletal_mesh_component
    c.set_skeletal_mesh_asset(unreal.load_asset(mesh))
    c.set_leader_pose_component(lead.skeletal_mesh_component)
    a.attach_to_actor(lead, "", KW, KW, KW, False)
    return a


def spawn(prefix, body, loc, items=(), hidden=(), colored=False, anim=None):
    """리더(원본 메시, 숨김) + 몸 부위 + 의상 조립.
    items: 의상 스켈레탈 메시 경로 목록, hidden: 숨길 몸 부위, colored: 몸 부위 단색 표시, anim: 반복 재생할 애니메이션 경로."""
    bp = body_parts.load_profile(body)
    lead = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(*loc))
    lead.set_actor_label(f"{prefix}_Leader")
    lc = lead.skeletal_mesh_component
    lc.set_skeletal_mesh_asset(unreal.load_asset(bp["source_mesh"]))
    lc.set_visibility(False)
    lc.set_editor_property("visibility_based_anim_tick_option", unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    if anim:
        lc.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        d = unreal.SingleAnimationPlayData()
        d.set_editor_property("anim_to_play", unreal.load_asset(anim))
        d.set_editor_property("saved_looping", True)
        d.set_editor_property("saved_playing", True)
        lc.set_editor_property("animation_data", d)
    for part, paths in body_parts.part_meshes(body).items():
        for j, path in enumerate(paths):
            a = _follower(prefix, f"{part}{j}", path, lead, loc)
            c = a.skeletal_mesh_component
            c.set_editor_property("use_bounds_from_leader_pose_component", True)
            if colored and j == 0:
                mi = debug_material(part)
                for i in range(c.get_num_materials()):
                    c.set_material(i, mi)
            if part in hidden:
                c.set_visibility(False)
    for i, path in enumerate(items):
        _follower(prefix, f"item{i}", path, lead, loc).skeletal_mesh_component.set_editor_property("use_bounds_from_leader_pose_component", True)
    return lead


def _capture(world, cap, name, cam_loc, target, fov, w, h):
    cam = unreal.Vector(*cam_loc)
    cap.set_actor_location_and_rotation(cam, unreal.MathLibrary.find_look_at_rotation(cam, unreal.Vector(*target)), False, False)
    comp = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(world, w, h, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    comp.texture_target = rt
    comp.fov_angle = fov
    comp.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    comp.capture_every_frame = False
    comp.capture_scene()
    unreal.RenderingLibrary.export_render_target(world, rt, SHOT_DIR, name + ".png")
    return f"{SHOT_DIR}/{name}.png"


def shot(name, cam_loc, target, fov=40.0, w=1000, h=800):
    """에디터 월드 캡처(기준 자세)."""
    world = UES.get_editor_world()
    cap = EAS.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(*cam_loc), unreal.Rotator())
    try:
        return _capture(world, cap, name, cam_loc, target, fov, w, h)
    finally:
        EAS.destroy_actor(cap)


def shot_game(name, cam_loc, target, fov=40.0, w=1000, h=800):
    """Simulate 중인 게임 월드 캡처(애니메이션 재생 중)."""
    gw = UES.get_game_world()
    if gw is None:
        raise RuntimeError("Simulate 중이 아님 (ue_play 후 호출)")
    cap = [a for a in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.SceneCapture2D) if a.get_actor_label() == "T_CapG"][0]
    return _capture(gw, cap, name, cam_loc, target, fov, w, h)


def stop_simulate():
    LES.editor_request_end_play()
