"""키트 구성 캐릭터를 테스트 레벨에 조립하고 SceneCapture로 확인한다 (MC1.2).

- 테스트 레벨 /Game/Dev/MC/L_KitTest (Content/Dev는 git 제외)
- 캐릭터 = 리더(키트 몸 전체 메시 Body_Combined — 애니메이션 재생, 그대로 보이는 몸, 몸 섹션은 마스크 MID) + 눈·이·헤어 + 의상
  (2026-11-05: 키트 부위 5개 대신 몸 하나 — DECISIONS)
- 애니메이션은 에디터 월드에서 틱하지 않으므로 Simulate(editor_play_simulate) 중에 shot_game()으로 찍는다.
  shot_game용 캡처 액터 "T_CapG"는 Simulate 시작 전에 레벨에 있어야 한다(setup_level이 만든다).
- 캐릭터 정면은 +Y(매니퀸 규약). 정면 사진은 +Y 쪽에서 -Y를 보고 찍는다.

예)
  import importlib, jm_kit.preview as pv; importlib.reload(pv)
  pv.setup_level(); pv.clear("K")
  pv.spawn("KM", "M", (0, 0, 0), outfit="kit", anim=pv.ANIM["jog"])
"""
import unreal

from . import body_mask, garment, profiles

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
    if "T_Atmos" not in have:
        EAS.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0)).set_actor_label("T_Atmos")
    if "T_Fog" not in have:
        EAS.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0)).set_actor_label("T_Fog")
    _basic_lighting()
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


FIXTURES = ("T_Sun", "T_Back", "T_Sky", "T_Atmos", "T_Fog", "T_PPV", "T_Floor", "T_Fill", "T_CapG")   # 레벨 고정 액터 — clear()가 지우지 않음


def _basic_lighting():
    """UE 기본 레벨과 같은 조명 구성: 태양(대기 연동) + 하늘 대기 + 실시간 캡처 하늘광 + 높이 안개.
    태양은 캐릭터 정면(+Y) 왼쪽 위에서 비춘다(정면 사진이 어둡지 않게). 매번 값을 다시 넣는다(기존 레벨에도 적용)."""
    acts = {a.get_actor_label(): a for a in EAS.get_all_level_actors()}
    sun = acts["T_Sun"]
    sun.set_actor_rotation(unreal.Rotator(0, -40, -120), False)   # (roll, pitch, yaw): 아래로 40°, 정면 왼쪽에서
    lc = sun.light_component
    lc.set_intensity(10.0)   # UE 기본 레벨 태양 밝기(lux)
    lc.set_editor_property("atmosphere_sun_light", True)
    lc.set_editor_property("cast_shadows", True)
    sky = acts["T_Sky"].light_component
    sky.set_editor_property("real_time_capture", True)
    sky.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
    sky.set_intensity(2.5)   # 뒤쪽(태양 반대편)도 보이게 하늘광을 조금 강하게
    sky.recapture_sky()
    fog = acts["T_Fog"].component
    fog.set_editor_property("fog_density", 0.005)
    acts["T_Fill"].point_light_component.set_intensity(40)
    acts["T_Floor"].set_actor_scale3d(unreal.Vector(600, 600, 1))   # 600m — 캐릭터를 어디에 세워도 바닥 위
    acts["T_Floor"].static_mesh_component.set_material(0, unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial"))
    if "T_Back" not in acts:   # 뒤쪽 보조광(그림자 없음) — 키트 만화 셰이더는 그늘이 아주 어두워 뒷모습이 안 보임
        acts["T_Back"] = EAS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 600))
        acts["T_Back"].set_actor_label("T_Back")
    back = acts["T_Back"]
    back.set_actor_rotation(unreal.Rotator(0, -30, 70), False)   # 뒤(-Y) 오른쪽 위에서 앞으로
    bl = back.light_component
    bl.set_intensity(4.0)
    bl.set_editor_property("cast_shadows", False)
    bl.set_editor_property("atmosphere_sun_light", False)
    if "T_PPV" not in acts:
        ppv = EAS.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
        ppv.set_actor_label("T_PPV")
        acts["T_PPV"] = ppv
    ppv = acts["T_PPV"]
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_min_brightness", True)
    s.set_editor_property("override_auto_exposure_max_brightness", True)
    s.set_editor_property("auto_exposure_min_brightness", 1.0)   # 노출 고정(캡처 사진마다 밝기가 달라지지 않게)
    s.set_editor_property("auto_exposure_max_brightness", 1.0)
    ppv.set_editor_property("settings", s)


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
    """리더(= 보이는 몸, 키트 Body_Combined) + 눈·이·헤어 + 의상 조립(2026-11-05 몸 하나로).
    outfit: "kit"(키트 기본 의상 전부), None(맨몸), 또는 {슬롯: 메시 경로}.
    입은 슬롯이 덮는 몸 영역은 몸 섹션 마스크(M_Body_Masked MID)로 지운다(profiles.mask_params). mask_override: 파라미터 dict로 직접 지정({}면 키트 원본 머티리얼).
    extra_hidden: 통째로 숨길 부위(parts 키, 예 "hair"). anim: 반복 재생할 애니메이션 경로(ANIM 참고). 반환: 리더 액터."""
    bp = profiles.body(body)
    raw = dict(bp["outfit"]) if outfit == "kit" else dict(outfit or {})
    # 값이 "/"로 시작하지 않으면 items.json 아이템 키: 스켈레탈은 체형별 메시로, 정적(attach)은 본 부착으로
    items, statics = {}, {}
    for slot, v in raw.items():
        if v.startswith("/"):
            items[slot] = v
            continue
        it = garment.load_items()[v]
        if "attach" in it:
            statics[slot] = garment.static_attachment(v, body)
        else:
            items[slot] = it["out"].format(body=body)
    hidden = profiles.hidden_parts(list(items.keys()) + list(statics.keys())) | set(extra_hidden)
    for s in statics.values():
        hidden |= set(s["hide_parts"])
    lead = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(*loc))
    lead.set_actor_label(f"{prefix}_Leader")
    lc = lead.skeletal_mesh_component
    lc.set_skeletal_mesh_asset(unreal.load_asset(bp["leader"]))
    lc.set_editor_property("visibility_based_anim_tick_option", unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    if anim:
        lc.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        d = unreal.SingleAnimationPlayData()
        d.set_editor_property("anim_to_play", unreal.load_asset(anim))
        d.set_editor_property("saved_looping", True)
        d.set_editor_property("saved_playing", True)
        lc.set_editor_property("animation_data", d)
    mask = profiles.mask_params(items) if mask_override is None else mask_override
    if mask:
        body_mask.apply(lc, body, mask)   # 몸 섹션만 MID — 장비가 덮는 영역만 지움(얼굴 섹션은 키트 원본)
    for part, mesh in bp["parts"].items():
        a = _follower(prefix, part, mesh, lead, loc)
        if part in hidden:
            a.skeletal_mesh_component.set_visibility(False)
    for slot, mesh in items.items():
        _follower(prefix, f"item_{slot}", mesh, lead, loc)
    for slot, s in statics.items():   # 정적 아이템(머리·무기): 리더의 본에 부착, 상대 변환은 데이터에서
        a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc))
        a.set_actor_label(f"{prefix}_item_{slot}")
        a.static_mesh_component.set_static_mesh(unreal.load_asset(s["mesh"]))
        a.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        a.attach_to_component(lc, s["socket"], unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET,
                              unreal.AttachmentRule.SNAP_TO_TARGET, False)
        a.static_mesh_component.set_relative_transform(s["transform"], False, True)
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


WHITE_BG = "/Game/Dev/MC/M_WhiteBG"   # 언릿 흰색(테스트용, git 제외 경로)


def shot_ortho(name, center, width, w=1024, h=1024, from_axis="+y", show_only=None, white_bg=True):
    """직교 정면 사진(EditImage 원본용 — 키트 의상 틀 촬영). center 정면(+Y)에서 -Y를 본다.
    show_only: 이 액터들만 찍기. white_bg: 뒤에 흰 판(어두운 의상의 실루엣이 배경에 묻히지 않게)."""
    world = UES.get_editor_world()
    cx, cy, cz = center
    loc = {"+y": (cx, cy + 400, cz), "+x": (cx + 400, cy, cz)}[from_axis]
    cap = EAS.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(*loc), unreal.Rotator())
    bg = None
    if white_bg and EAL.does_asset_exist(WHITE_BG):
        back = {"+y": (cx, cy - 80, cz), "+x": (cx - 80, cy, cz)}[from_axis]
        bg = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*back), unreal.Rotator(0, 0, 0))
        bg.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
        bg.set_actor_rotation(unreal.Rotator(90, 0, 0) if from_axis == "+y" else unreal.Rotator(0, 90, 0), False)
        bg.set_actor_scale3d(unreal.Vector(width / 50.0, width / 50.0, 1))
        bg.static_mesh_component.set_material(0, unreal.load_asset(WHITE_BG))
        bg.set_actor_label("T_PhotoBG")
        if show_only:
            show_only = list(show_only) + [bg]
    try:
        c = cap.capture_component2d
        c.projection_type = unreal.CameraProjectionMode.ORTHOGRAPHIC
        c.ortho_width = width
        if show_only:
            c.set_editor_property("primitive_render_mode", unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
            for a in show_only:
                c.show_only_actor_components(a, False)
        cam = unreal.Vector(*loc)
        cap.set_actor_location_and_rotation(cam, unreal.MathLibrary.find_look_at_rotation(cam, unreal.Vector(*center)), False, False)
        rt = unreal.RenderingLibrary.create_render_target2d(world, w, h, unreal.TextureRenderTargetFormat.RTF_RGBA8)
        c.texture_target = rt
        c.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        c.capture_every_frame = False
        c.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, rt, SHOT_DIR, name + ".png")
    finally:
        EAS.destroy_actor(cap)
        if bg:
            EAS.destroy_actor(bg)
    return f"{SHOT_DIR}/{name}.png"


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
