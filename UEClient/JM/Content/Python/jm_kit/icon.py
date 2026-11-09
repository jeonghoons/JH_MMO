"""장비 아이콘 촬영(MC2.4) — 최종 메시를 SceneCapture로 같은 각도·조명·투명 배경으로 찍어 T_Icon_<Item>을 만든다.

- 규격(DESIGN_ART_CONCEPT §5): 512×512 PNG, 원본 SourceArt/Icons/, 에셋은 아이템 폴더(Content/Equipment/<Tier>/<Slot>/T_Icon_<Item>).
  테두리(등급 색)는 UI가 그린다.
- 각도: 방어구 = 정면 3/4(캐릭터 정면 +Y에서 왼쪽(+X, 태양 쪽)으로 YAW_ARMOR, 위에서 PITCH_ARMOR),
  무기 = 긴 축을 화면 대각선 45°(손잡이 왼쪽 아래 → 끝 오른쪽 위)로 눕혀 정면에서.
- 메시: 스켈레탈 장비는 남자(M) 결과 메시의 기준 자세, 머리 장비는 남자 머리에 씌운 배치 그대로,
  장갑은 오른손만(손목 끝 톱니를 평면으로 잘라 임시 정적 메시로, items.json icon.yaw -90 = 손등 쪽).
- 촬영: 1024²에 색(FinalColorLDR)과 덮임(SceneColorHDR 알파)을 따로 찍고, 덮인 영역에 맞춰 정사각형으로 잘라 512로 줄인다.
  조명은 테스트 레벨(TestLevel) 고정 조명, 대기·안개는 캡처에서 끈다(가장자리가 하늘색으로 물들지 않게).
- 아이템별로 items.json "icon": {"yaw", "pitch", "margin"}로 덮어쓸 수 있다.

예) import importlib, jm_kit.icon as ic; importlib.reload(ic); ic.shoot(["Plate_Top"])
"""
import math
import os
import struct
import zlib

import unreal

from . import garment, glove_paint, preview, profiles

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EAL = unreal.EditorAssetLibrary
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
ED = unreal.GeometryScript_MeshEdits

SRC_DIR = os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "..", "..", "SourceArt", "Icons"))
SIZE = 512            # 최종 아이콘
RENDER = 1024         # 촬영 해상도(줄여서 가장자리를 부드럽게)
FOV = 20.0            # 좁은 화각 — 원근 왜곡을 줄임
LOC = (3000.0, 0.0, 150.0)   # 촬영 위치(테스트 캐릭터가 서는 원점에서 떨어진 곳)
YAW_ARMOR, PITCH_ARMOR = 35.0, 12.0
YAW_WEAPON, PITCH_WEAPON = 0.0, 0.0
MARGIN = 0.07         # 덮인 영역 바깥 여백(한 변 비율)
BODY = "M"
LABEL = "T_IconSubject"
CUFF_TRIM_CM = 4.0   # 장갑 아이콘: 손목 끝(톱니)에서 이만큼 손 쪽을 평면으로 자름
INFO = {}
TMP_MESH = "/Game/Dev/MC/SM_IconTmp"   # 장갑 오른손 임시 메시(Content/Dev는 git 제외)


# ---------------------------------------------------------------- 피사체 배치
def _dest_dir(it):
    d = it["source"].rsplit("/", 1)[0]
    return d[:-len("/Source")] if d.endswith("/Source") else d


def _move(tf, off):
    t = tf.translation
    return unreal.Transform(location=unreal.Vector(t.x + off[0], t.y + off[1], t.z + off[2]),
                            rotation=tf.rotation.rotator(), scale=tf.scale3d)


def _static_actor(mesh, tf):
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*LOC))
    a.set_actor_label(LABEL)
    c = a.static_mesh_component
    c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_static_mesh(unreal.load_asset(mesh))
    c.set_forced_lod_model(1)   # 1 = LOD0
    a.set_actor_transform(tf, False, True)
    return a


def _weapon_transform(mesh):
    """긴 축 → 화면 대각선(오른쪽 위), 중간 축 → 화면 안 수직 방향, 가장 얇은 축 → 카메라(+Y). 손잡이(긴 축 최소 끝)는 왼쪽 아래."""
    b = unreal.load_asset(mesh).get_bounds()
    e = (b.box_extent.x, b.box_extent.y, b.box_extent.z)
    o = (b.origin.x, b.origin.y, b.origin.z)
    lo, mid, thin = sorted(range(3), key=lambda i: -e[i])
    r = math.sqrt(0.5)
    tgt = {lo: (r, 0.0, r), mid: (-r, 0.0, r), thin: (0.0, 1.0, 0.0)}   # 카메라는 +Y에서 -Y를 봄 → 화면 오른쪽 = +X
    x, y, z = tgt[0], tgt[1], tgt[2]
    det = (x[1] * y[2] - x[2] * y[1]) * z[0] + (x[2] * y[0] - x[0] * y[2]) * z[1] + (x[0] * y[1] - x[1] * y[0]) * z[2]
    if det < 0:   # 반사 행렬이 되지 않게 얇은 축 방향을 뒤집음(앞뒤 면만 바뀜)
        tgt[thin] = (0.0, -1.0, 0.0)
        x, y, z = tgt[0], tgt[1], tgt[2]
    rot = unreal.MathLibrary.make_rotation_from_axes(unreal.Vector(*x), unreal.Vector(*y), unreal.Vector(*z))
    c = rot.quaternion().rotate_vector(unreal.Vector(*o))
    return unreal.Transform(location=unreal.Vector(LOC[0] - c.x, LOC[1] - c.y, LOC[2] - c.z), rotation=rot, scale=unreal.Vector(1, 1, 1))


def _shield_transform(mesh, at):
    """방패(MC4.2f): 앞면 축(items.json attach.face) → 카메라(+Y), 위 축(attach.up) → +Z, 상자 중심을 촬영 위치에."""
    b = unreal.load_asset(mesh).get_bounds()
    o = unreal.Vector(b.origin.x, b.origin.y, b.origin.z)
    (fi, fs), (ui, us), _s, _back = garment.shield_frame(at, (o.x, o.y, o.z), (b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2))
    mesh_rot = unreal.MathLibrary.make_rot_from_xz(garment._unit(fi, fs), garment._unit(ui, us))
    want = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0, 1, 0), unreal.Vector(0, 0, 1))
    tf = unreal.MathLibrary.compose_transforms(unreal.MathLibrary.invert_transform(unreal.Transform(rotation=mesh_rot)), unreal.Transform(rotation=want))
    c = tf.rotation.rotate_vector(o)
    return unreal.Transform(location=unreal.Vector(LOC[0] - c.x, LOC[1] - c.y, LOC[2] - c.z), rotation=tf.rotation.rotator(), scale=unreal.Vector(1, 1, 1))


def _right_glove_actor(mesh):
    """장갑 결과 메시에서 오른손 쪽 삼각형만 남긴 임시 정적 메시 액터(기준 자세)."""
    hand_x = garment._bone_ref(BODY, "hand_r").translation.x
    dm = garment._dm_skel(mesh)
    dm, tids, _ = Q.get_all_triangle_i_ds(dm)
    drop = []
    for t in L.convert_index_list_to_array(tids):
        idx, _ = Q.get_triangle_indices(dm, t)
        xs = [Q.get_vertex_position(dm, v)[0].x for v in (idx.x, idx.y, idx.z)]
        if sum(xs) / 3.0 * hand_x <= 0:
            drop.append(t)
    r = ED.delete_triangles_from_mesh(dm, L.convert_array_to_index_list(drop, unreal.GeometryScriptIndexType.TRIANGLE))
    dm = r[0] if isinstance(r, tuple) else r
    if garment._tris(dm) == 0:
        raise RuntimeError(f"오른손 삼각형이 없음: {mesh} (hand_r x = {hand_x:.1f})")
    # 손목 끝은 삼각형 단위로 잘려 톱니 모양(게임에서는 소매에 가려짐) → 팔 축에 수직인 평면으로 매끈하게 자른다
    h = garment._bone_ref(BODY, "hand_r").translation
    e = garment._bone_ref(BODY, "lowerarm_r").translation
    ax, n = garment._norm(garment._sub((h.x, h.y, h.z), (e.x, e.y, e.z)))   # 팔꿈치 → 손
    d = garment.GLOVE_CUFF_CM - CUFF_TRIM_CM
    org = unreal.Vector(h.x - ax[0] * d, h.y - ax[1] * d, h.z - ax[2] * d)
    frame = unreal.Transform(location=org, rotation=unreal.MathLibrary.make_rot_from_z(unreal.Vector(*[-c for c in ax])))
    po = unreal.GeometryScriptMeshPlaneCutOptions()
    po.fill_holes = False
    before = garment._tris(dm)
    r = unreal.GeometryScript_MeshBooleans.apply_mesh_plane_cut(dm, frame, po)
    dm = r[0] if isinstance(r, tuple) else r
    INFO[mesh] = {"tris_before_cut": before, "tris_after_cut": garment._tris(dm)}
    # DynamicMeshActor는 렌더 프록시가 프레임 끝에 생겨 같은 호출의 캡처에 안 잡힘 → 임시 정적 메시로(git 제외 경로)
    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.enable_recompute_normals = False
    opts.enable_recompute_tangents = True
    if EAL.does_asset_exist(TMP_MESH):
        EAL.delete_asset(TMP_MESH)
    sm, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, TMP_MESH, opts)
    if sm is None:
        raise RuntimeError(f"임시 정적 메시 생성 실패: {outcome}")
    mats = [m.material_interface for m in unreal.load_asset(mesh).materials]
    sms = sm.get_editor_property("static_materials")
    for k in range(len(sms)):
        sm.set_material(k, mats[min(k, len(mats) - 1)])
    EAL.save_loaded_asset(sm)
    return _static_actor(TMP_MESH, unreal.Transform(location=unreal.Vector(*LOC)))


def _skeletal_actor(mesh):
    a = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(*LOC))
    a.set_actor_label(LABEL)
    c = a.skeletal_mesh_component
    c.set_skeletal_mesh_asset(unreal.load_asset(mesh))
    c.set_forced_lod(1)
    return a


def _subject(key, it):
    """(액터, 방어구 여부)."""
    if "attach" in it:
        at = it["attach"]
        if at["fit"] == "length":
            return _static_actor(it["source"], _weapon_transform(it["source"])), False
        if at["fit"] == "shield":
            return _static_actor(it["source"], _shield_transform(it["source"], at)), True   # 방패: 방어구처럼 정면 3/4
        s = garment.static_attachment(key, BODY)   # 머리 장비: 머리에 씌운 배치 그대로
        world = unreal.MathLibrary.compose_transforms(s["transform"], garment._bone_ref(BODY, s["socket"]))
        return _static_actor(s["mesh"], _move(world, LOC)), True
    mesh = it["out"].format(body=BODY)
    if it["slot"] == "gloves":
        return _right_glove_actor(mesh), True
    return _skeletal_actor(mesh), True


# ---------------------------------------------------------------- 촬영
def _bounds_center_radius(actor):
    o, e = actor.get_actor_bounds(False)
    return (o.x, o.y, o.z), math.sqrt(e.x * e.x + e.y * e.y + e.z * e.z)


def _cam(center, radius, yaw, pitch):
    dist = radius / math.sin(math.radians(FOV / 2)) * 1.05
    y, p = math.radians(yaw), math.radians(pitch)
    d = (math.sin(y) * math.cos(p), math.cos(y) * math.cos(p), math.sin(p))   # yaw 0 = 정면(+Y), +yaw = +X 쪽
    return tuple(center[i] + d[i] * dist for i in range(3))


def _capture(world, actor, cam, target, source, fmt):
    cap = EAS.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(*cam), unreal.Rotator())
    try:
        c = cap.capture_component2d
        c.set_editor_property("primitive_render_mode", unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        c.show_only_actor_components(actor, False)
        c.set_editor_property("show_flag_settings", [unreal.EngineShowFlagsSetting(show_flag_name=n, enabled=False)
                                                     for n in ("Atmosphere", "Fog", "Cloud")])
        v = unreal.Vector(*cam)
        cap.set_actor_location_and_rotation(v, unreal.MathLibrary.find_look_at_rotation(v, unreal.Vector(*target)), False, False)
        rt = unreal.RenderingLibrary.create_render_target2d(world, RENDER, RENDER, fmt)
        c.texture_target = rt
        c.fov_angle = FOV
        c.capture_source = source
        c.capture_every_frame = False
        c.capture_scene()
        return rt
    finally:
        EAS.destroy_actor(cap)


def _shoot_rgba(world, actor, cam, target):
    """반환: RENDER² 크기의 [r, g, b, a] 리스트(0~1, 직선 알파)."""
    rt_c = _capture(world, actor, cam, target, unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    rt_a = _capture(world, actor, cam, target, unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR, unreal.TextureRenderTargetFormat.RTF_RGBA16F)
    # 내보내기가 렌더링을 끝까지 기다리게 한다 — 빼면 방금 만든 임시 메시(장갑)가 빈 화면으로 찍혔음. 파일은 문제 확인용 마지막 원본
    unreal.RenderingLibrary.export_render_target(world, rt_c, preview.SHOT_DIR, "icon_last_color.png")
    col = unreal.RenderingLibrary.read_render_target(world, rt_c, True)
    alp = unreal.RenderingLibrary.read_render_target_raw(world, rt_a, False)
    inv = alp[0].a > 0.5   # SceneColorHDR 알파 = 1 - 덮임(빈 배경 = 1). 모서리 픽셀로 방향 확인
    px = []
    for c, a in zip(col, alp):
        cov = (1.0 - a.a) if inv else a.a
        cov = 0.0 if cov < 0.004 else (1.0 if cov > 0.996 else cov)
        px.append((c.r / 255.0, c.g / 255.0, c.b / 255.0, cov))
    return px


def _crop_resize(px, margin):
    """덮인 영역에 맞춰 정사각형 자르기 → SIZE²로 면적 평균(미리 곱한 알파). 반환: (픽셀, 덮인 영역 비율, 잘린 변 px)."""
    n = RENDER
    xs = [i % n for i, p in enumerate(px) if p[3] > 0.02]
    ys = [i // n for i, p in enumerate(px) if p[3] > 0.02]
    if not xs:
        raise RuntimeError(f"촬영 결과가 비었음(피사체가 화면 밖이거나 표시 목록 문제) — 마지막 원본 {preview.SHOT_DIR}/icon_last_color.png, 알파 최대 {max(p[3] for p in px):.3f}")
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    side = max(x1 - x0, y1 - y0) / (1.0 - 2 * margin)
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    sx, sy = cx - side / 2, cy - side / 2
    step = side / SIZE
    out = []
    for oy in range(SIZE):
        ya, yb = sy + oy * step, sy + (oy + 1) * step
        rows = range(max(0, int(math.floor(ya))), min(n, int(math.ceil(yb))))
        for ox in range(SIZE):
            xa, xb = sx + ox * step, sx + (ox + 1) * step
            r = g = b = a = w = 0.0
            for yy in rows:
                wy = min(yb, yy + 1) - max(ya, yy)
                base = yy * n
                for xx in range(max(0, int(math.floor(xa))), min(n, int(math.ceil(xb)))):
                    ww = wy * (min(xb, xx + 1) - max(xa, xx))
                    p = px[base + xx]
                    r += p[0] * p[3] * ww; g += p[1] * p[3] * ww; b += p[2] * p[3] * ww; a += p[3] * ww
                    w += ww
            tot = step * step   # 화면 밖 = 투명
            aa = a / tot
            out.append((r / a, g / a, b / a, aa) if a > 1e-6 else (0.0, 0.0, 0.0, 0.0))
    fill = max(x1 - x0, y1 - y0) / side
    return out, round(fill, 3), round(side)


def _write_png(path, px, w, h):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            p = px[y * w + x]
            raw += bytes(max(0, min(255, int(round(c * 255)))) for c in p)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def _import(png, dest_dir):
    tex = glove_paint.import_texture(png, dest_dir)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)   # UserInterface2D(RGBA)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("srgb", True)
    EAL.save_loaded_asset(tex)
    return tex.get_path_name().split(".")[0]


def _sheet(results, path, cell=256, cols=7):
    """확인용 시트(중간 회색 배경 위에 합성) → Saved/MCShots."""
    keys = list(results)
    rows = (len(keys) + cols - 1) // cols
    W, H = cell * cols, cell * rows
    bg = (0.22, 0.22, 0.24)
    img = [bg + (1.0,)] * (W * H)
    k = SIZE // cell
    for i, key in enumerate(keys):
        px = results[key]
        ox, oy = (i % cols) * cell, (i // cols) * cell
        for y in range(cell):
            for x in range(cell):
                p = px[(y * k) * SIZE + x * k]
                a = p[3]
                img[(oy + y) * W + ox + x] = tuple(p[j] * a + bg[j] * (1 - a) for j in range(3)) + (1.0,)
    _write_png(path, img, W, H)
    return path


def shoot(keys=None, import_assets=True, sheet=True):
    """items.json 아이템 아이콘을 찍는다(keys 없으면 전부). 반환: {키: {png, asset, fill, crop_px}, "_sheet": 경로}."""
    garment._not_playing()
    items = garment.load_items()
    keys = list(keys or items.keys())
    world = UES.get_editor_world()
    preview.clear(LABEL)
    rep, pixels = {}, {}
    for key in keys:
        it = items[key]
        actor, armor = _subject(key, it)
        try:
            ov = it.get("icon", {})
            yaw = ov.get("yaw", YAW_ARMOR if armor else YAW_WEAPON)
            pitch = ov.get("pitch", PITCH_ARMOR if armor else PITCH_WEAPON)
            center, radius = _bounds_center_radius(actor)
            if radius < 1.0:
                raise RuntimeError(f"{key}: 피사체 경계가 비었음(반지름 {radius:.2f})")
            px = _shoot_rgba(world, actor, _cam(center, radius, yaw, pitch), center)
        finally:
            EAS.destroy_actor(actor)
        out, fill, side = _crop_resize(px, ov.get("margin", MARGIN))
        png = os.path.join(SRC_DIR, f"T_Icon_{key}.png")
        _write_png(png, out, SIZE, SIZE)
        r = {"png": png, "fill": fill, "crop_px": side}
        if it.get("out") and it["out"].format(body=BODY) in INFO:
            r["glove"] = INFO[it["out"].format(body=BODY)]
        if import_assets:
            r["asset"] = _import(png, _dest_dir(it))
        rep[key], pixels[key] = r, out
    if EAL.does_asset_exist(TMP_MESH):
        EAL.delete_asset(TMP_MESH)
    if sheet:
        rep["_sheet"] = _sheet(pixels, os.path.join(preview.SHOT_DIR, "icons_sheet.png"))
    return rep
