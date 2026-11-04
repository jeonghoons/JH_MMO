"""키트 구성 캐릭터를 테스트 레벨에 조립하고 SceneCapture로 확인한다 (MC1.2).

- 테스트 레벨 /Game/Dev/MC/L_KitTest (Content/Dev는 git 제외)
- 캐릭터 = 리더(키트 몸 전체 메시, 숨김, 애니메이션 재생) + 키트 몸 부위 + 의상. 키트 BP_TPCharacter_CoreC_<M|F>와 같은 구성.
- 애니메이션은 에디터 월드에서 틱하지 않으므로 Simulate(editor_play_simulate) 중에 shot_game()으로 찍는다.
  shot_game용 캡처 액터 "T_CapG"는 Simulate 시작 전에 레벨에 있어야 한다(setup_level이 만든다).
- 캐릭터 정면은 +Y(매니퀸 규약). 정면 사진은 +Y 쪽에서 -Y를 보고 찍는다.

예)
  import importlib, jm_kit.preview as pv; importlib.reload(pv)
  pv.setup_level(); pv.clear("K")
  pv.spawn("KM", "M", (0, 0, 0), outfit="kit", anim=pv.ANIM["jog"])
"""
import unreal

from . import body_mask, profiles

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAL = unreal.EditorAssetLibrary
SHOT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir() + "MCShots")
LEVEL = "/Game/Dev/MC/L_KitTest"
KW = unreal.AttachmentRule.KEEP_WORLD
_A = "/Game/Character/Mannequin/Anims/Unarmed/"
ANIM = {"idle": _A + "MM_Idle", "jog": _A + "Jog/MF_Unarmed_Jog_Fwd", "walk": _A + "Walk/MF_Unarmed_Walk_Fwd",
        "attack1": _A + "Attack/MM_Attack_01", "attack2": _A + "Attack/MM_Attack_02", "attack3": _A + "Attack/MM_Attack_03"}


def setup_level():
    """테스트 레벨을 열거나 만든다(조명·바닥·Simulate용 캡처 액터 포함)."""
    if UES.get_game_world() is not None:
        raise RuntimeError("Simulate 중에는 레벨을 열 수 없음 — stop_simulate() 후 다시 호출")
    if EAL.does_asset_exist(LEVEL):
        LES.load_level(LEVEL)
    else:
        LES.new_level(LEVEL)
    have = {a.get_actor_label() for a in EAS.get_all_level_actors()}
    if "T_Sun" not in have:
        EAS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, -35, -60)).set_actor_label("T_Sun")
    if "T_Sky" not in have:
        EAS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300)).set_actor_label("T_Sky")
    if "T_Floor" not in have:
        floor = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
        floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
        floor.set_actor_scale3d(unreal.Vector(40, 40, 1))
        floor.set_actor_label("T_Floor")
    if "T_Fill" not in have:
        fill = EAS.spawn_actor_from_class(unreal.PointLight, unreal.Vector(0, 350, 250))
        fill.point_light_component.set_intensity(80)
        fill.point_light_component.set_attenuation_radius(2000)
        fill.set_actor_label("T_Fill")
    if "T_CapG" not in have:
        EAS.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 600, 120), unreal.Rotator(0, 0, -90)).set_actor_label("T_CapG")
    LES.save_current_level()
    return LEVEL


FIXTURES = ("T_Sun", "T_Sky", "T_Floor", "T_Fill", "T_CapG")   # 레벨 고정 액터 — clear()가 지우지 않음


def clear(prefix):
    """라벨이 prefix로 시작하는 캐릭터 액터를 지운다(레벨 고정 액터 FIXTURES는 제외)."""
    for a in EAS.get_all_level_actors():
        label = a.get_actor_label()
        if label.startswith(prefix) and label not in FIXTURES:
            EAS.destroy_actor(a)


def _follower(prefix, label, mesh, lead, loc):
    a = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(*loc))
    a.set_actor_label(f"{prefix}_{label}")
    c = a.skeletal_mesh_component
    c.set_skeletal_mesh_asset(unreal.load_asset(mesh))
    c.set_leader_pose_component(lead.skeletal_mesh_component)
    c.set_editor_property("use_bounds_from_leader_pose_component", True)
    a.attach_to_actor(lead, "", KW, KW, KW, False)
    return a


def spawn(prefix, body, loc, outfit="kit", extra_hidden=(), anim=None, mask_override=None):
    """리더 + 키트 몸 부위 + 의상 조립.
    outfit: "kit"(키트 기본 의상 전부), None(맨몸), 또는 {슬롯: 메시 경로}.
    입은 슬롯이 덮는 몸 영역은 몸 마스크(M_Body_Masked MID)로 지운다(profiles.mask_params). mask_override: 파라미터 dict로 직접 지정({}면 마스크 머티리얼 없이 키트 원본).
    extra_hidden: 통째로 숨길 몸 부위. anim: 반복 재생할 애니메이션 경로(ANIM 참고). 반환: 리더 액터."""
    bp = profiles.body(body)
    items = dict(bp["outfit"]) if outfit == "kit" else dict(outfit or {})
    hidden = profiles.hidden_parts(items.keys()) | set(extra_hidden)
    lead = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(*loc))
    lead.set_actor_label(f"{prefix}_Leader")
    lc = lead.skeletal_mesh_component
    lc.set_skeletal_mesh_asset(unreal.load_asset(bp["leader"]))
    lc.set_visibility(False)
    lc.set_editor_property("visibility_based_anim_tick_option", unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    if anim:
        lc.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        d = unreal.SingleAnimationPlayData()
        d.set_editor_property("anim_to_play", unreal.load_asset(anim))
        d.set_editor_property("saved_looping", True)
        d.set_editor_property("saved_playing", True)
        lc.set_editor_property("animation_data", d)
    mask = profiles.mask_params(items) if mask_override is None else mask_override
    masked = set(profiles.load().get("masked_parts", ())) if mask else set()
    for part, mesh in bp["parts"].items():
        a = _follower(prefix, part, mesh, lead, loc)
        if part in hidden:
            a.skeletal_mesh_component.set_visibility(False)
        elif part in masked:
            body_mask.apply(a.skeletal_mesh_component, body, mask)   # 장비가 덮는 영역만 지움(2026-11-05 몸 마스킹)
    for slot, mesh in items.items():
        _follower(prefix, f"item_{slot}", mesh, lead, loc)
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
    """에디터 월드 캡처(애니메이션 첫 프레임 또는 기준 자세)."""
    world = UES.get_editor_world()
    cap = EAS.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(*cam_loc), unreal.Rotator())
    try:
        return _capture(world, cap, name, cam_loc, target, fov, w, h)
    finally:
        EAS.destroy_actor(cap)


def shot_game(name, cam_loc, target, fov=40.0, w=1000, h=800):
    """Simulate 중인 게임 월드 캡처(애니메이션 재생 중). 같은 Python 호출 안에서는 프레임이 진행되지 않는다."""
    gw = UES.get_game_world()
    if gw is None:
        raise RuntimeError("Simulate 중이 아님 (start_simulate 후 다음 호출에서)")
    cap = [a for a in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.SceneCapture2D) if a.get_actor_label() == "T_CapG"][0]
    return _capture(gw, cap, name, cam_loc, target, fov, w, h)


def look_at(cam_loc, target):
    """에디터 뷰포트 카메라를 옮긴다(사용자가 같은 장면을 보도록)."""
    cam = unreal.Vector(*cam_loc)
    UES.set_level_viewport_camera_info(cam, unreal.MathLibrary.find_look_at_rotation(cam, unreal.Vector(*target)))


def start_simulate():
    LES.editor_play_simulate()


def stop_simulate():
    LES.editor_request_end_play()
