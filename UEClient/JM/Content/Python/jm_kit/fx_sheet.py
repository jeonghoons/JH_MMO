"""FX 라이브러리 미리보기 시트(FX_CATALOG 3단계) — Content/FX의 파티클을 하나씩 재생해 두 시점을 찍고 분류별 시트로 모은다.

- 파티클은 에디터 월드에서 틱하지 않으므로 **Simulate 중**(preview.start_simulate)에 게임 월드로 스폰한다.
  한 Python 호출 안에서는 프레임이 진행되지 않으므로 슬레이트 post-tick 콜백으로 프레임마다 상태를 진행한다
  (start() → 바로 반환, status()로 진행 확인, 끝나면 Saved/MCShots/fx_sheet_<분류>.png + fx_sheet_status.json).
- 카메라는 캐릭터 정면(+Y) 오른쪽 앞 3/4, 거리는 에셋 고정 바운딩(bb)에 맞춘다. 캐릭터(KM_Leader, preview.spawn)가 원점에 있으면
  Buff/Life/Trail/Cast는 캐릭터 몸(spine_03·발밑)에 붙여 스폰하고, 나머지는 원점 바닥에 놓는다.
- Trail(AnimTrail)·Paragon 본에 묶인 이미터는 여기서는 비어 보인다(카탈로그 "손볼 것" 그대로) — 시트는 그 확인용이기도 하다.

예) import importlib, jm_kit.fx_sheet as fs; importlib.reload(fs)
    pv.setup_level(); pv.spawn("KM", "M", (0, 0, 0)); pv.start_simulate()  →  다음 호출에서 fs.start(category="Hit")
"""
import json
import os
import time

import unreal

from . import icon, preview

UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EAL = unreal.EditorAssetLibrary
FX_ROOT = "/Game/FX"
CATEGORIES = ("Trail", "Hit", "Proj", "Cast", "AoE", "Buff", "Life")
ATTACHED = {"Trail", "Buff", "Life", "Cast"}   # 캐릭터 몸 위치(spine_03)에 재생 — Cast는 발밑. 그 밖은 원점 바닥
BODY_SOCKET = "spine_03"
TIMES = (0.3, 1.0)
CELL = 320            # 시트 한 칸(가로), 세로는 4:3
COLS = 4
RENDER = (640, 480)
EXPOSURE_BIAS = 0.0    # 캡처 컴포넌트 쪽 보정(효과가 없어 0 — 노출은 _set_exposure로 PPV에서 잡는다)
EXPOSURE_EV = 5.0      # Simulate 월드 PPV 고정 노출(EV100). 테스트 레벨 기본 1.0은 밤 노출이라 바닥이 희게 날아가고 가산 파티클이 안 보임
SHOW_FLOOR = False     # 바닥을 빼면 배경이 검게 되어 가산(불꽃·섬광) 파티클이 확실히 보인다(지면 기준은 캐릭터 발로 본다)
STATUS_PATH = os.path.join(preview.SHOT_DIR, "fx_sheet_status.json")

_job = None


def list_fx(category=None):
    """Content/FX/<분류>/의 파티클 경로 목록(분류 없으면 전부)."""
    # EditorAssetLibrary는 Simulate/PIE 중에 동작하지 않으므로 에셋 레지스트리로 읽는다
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    out = []
    for cat in ([category] if category else CATEGORIES):
        for ad in reg.get_assets_by_path(f"{FX_ROOT}/{cat}", recursive=True):
            if str(ad.asset_class_path.asset_name) == "ParticleSystem":
                out.append(str(ad.package_name))
    return sorted(out)


def _bb(fx):
    if fx.get_editor_property("use_fixed_relative_bounding_box"):
        b = fx.get_editor_property("fixed_relative_bounding_box")
        mn, mx = b.min, b.max
        return (mx.x - mn.x, mx.y - mn.y, mx.z - mn.z)
    return (300.0, 300.0, 300.0)


def _camera(bb, attached, base=(0.0, 0.0, 0.0)):
    span = max(bb[0], bb[1], bb[2] * 0.8)
    dist = min(3500.0, max(450.0, span * 1.35))
    tz = 90.0 if attached else min(150.0, max(40.0, bb[2] * 0.25))
    # 캐릭터 발(base) 기준, 정면(+Y) 오른쪽 앞(+X), 약간 위에서
    return (base[0] + dist * 0.62, base[1] + dist * 0.62, base[2] + tz + dist * 0.33), (base[0], base[1], base[2] + tz)


def _set_exposure(gw, ev):
    """Simulate 월드의 PostProcessVolume 노출을 EV100 고정값으로(테스트 레벨 기본 1.0은 밤 노출이라 가산 파티클이 날아감). 게임 월드 사본만 바뀐다."""
    for ppv in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.PostProcessVolume):
        s = ppv.get_editor_property("settings")
        s.set_editor_property("override_auto_exposure_min_brightness", True)
        s.set_editor_property("override_auto_exposure_max_brightness", True)
        s.set_editor_property("auto_exposure_min_brightness", ev)
        s.set_editor_property("auto_exposure_max_brightness", ev)
        ppv.set_editor_property("settings", s)


LEADER_LABEL = "KM_Leader"   # preview.spawn("KM", ...)의 리더 — 레벨에 다른 "*_Leader" 시험 액터가 있을 수 있어 정확히 이 라벨만


def _leader(gw):
    for a in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.SkeletalMeshActor):
        if a.get_actor_label() == LEADER_LABEL:
            return a
    return None


class _Job:
    def __init__(self, paths, times, label, attach=None):
        self.paths, self.times, self.label, self.attach = paths, tuple(times), label, attach
        self.i, self.t_index, self.comp, self.t0 = 0, 0, None, None
        self.results = {}
        self.cells = {}     # (path) -> [픽셀 리스트 per time]
        self.done, self.error, self.sheets = False, None, []
        self.handle = None

    def tick(self, _dt):
        try:
            self._step()
        except Exception as e:   # 한 에셋의 실패는 기록하고 다음으로
            self._fail(str(e))

    def _fail(self, msg):
        if self.i < len(self.paths):
            self.results[self.paths[self.i]] = {"error": msg}
        self._cleanup_comp()
        self.i += 1
        self.t_index = 0
        if self.i >= len(self.paths):
            self._finish()

    def _cleanup_comp(self):
        if self.comp is not None:
            try:
                self.comp.destroy_component(self.comp)
            except Exception:
                pass
            self.comp = None
        if getattr(self, "fx_asset", None) is not None:
            try:
                self.fx_asset.set_editor_property("lod_method", self.lod_method_orig)
            except Exception:
                pass
            self.fx_asset = None

    def _step(self):
        if self.done:
            return
        gw = UES.get_game_world()
        if gw is None:
            self.error = "Simulate가 끝남 — 중단"
            self._finish()
            return
        if self.i >= len(self.paths):
            self._finish()
            return
        path = self.paths[self.i]
        now = unreal.GameplayStatics.get_time_seconds(gw)
        if self.comp is None:
            fx = unreal.load_asset(path)
            # Cascade LOD는 플레이어 카메라 거리로 정해지는데 Simulate엔 플레이어가 없어 먼 LOD(불꽃·섬광 이미터 꺼짐)가 잡힌다
            # → 에셋의 LOD 방식을 잠시 DirectSet(LOD 0)으로 바꿔 스폰하고 정리 때 되돌린다(저장하지 않음)
            self.fx_asset, self.lod_method_orig = fx, fx.get_editor_property("lod_method")
            fx.set_editor_property("lod_method", unreal.ParticleSystemLODMethod.PARTICLESYSTEMLODMETHOD_DIRECT_SET)
            cat = path.split("/")[3] if path.startswith(FX_ROOT) else ""
            attached = cat in ATTACHED if self.attach is None else bool(self.attach)
            lead = _leader(gw) if attached else None
            # spawn_emitter_attached는 Simulate 월드에서 아무것도 그리지 않았다(2026-11-10) → 캐릭터는 서 있으므로 본 위치에 그냥 스폰한다
            base = _leader(gw)
            base_loc = base.get_actor_location() if base is not None else unreal.Vector(0, 0, 0)
            loc = base_loc
            if lead is not None and cat != "Cast":
                loc = lead.skeletal_mesh_component.get_socket_location(BODY_SOCKET)
            if lead is None:
                attached = False
            self.comp = unreal.GameplayStatics.spawn_emitter_at_location(
                gw, fx, loc, unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1), False)
            self.t0 = now
            self.bb = _bb(fx)
            self.cam, self.target = _camera(self.bb, attached, (base_loc.x, base_loc.y, base_loc.z))
            self.results[path] = {"bb": [round(v) for v in self.bb], "attached": attached,
                                  "loc": [round(loc.x), round(loc.y), round(loc.z)], "shots": []}
            return
        if now - self.t0 < self.times[self.t_index]:
            return
        name = f"fx_{path.split('/')[-1]}_t{int(round(self.times[self.t_index] * 100)):03d}"
        cap = [a for a in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.SceneCapture2D) if a.get_actor_label() == "T_CapG"]
        if not cap:
            raise RuntimeError("T_CapG 없음 — preview.setup_level() 후 Simulate")
        cam = unreal.Vector(*self.cam)
        cap[0].set_actor_location_and_rotation(cam, unreal.MathLibrary.find_look_at_rotation(cam, unreal.Vector(*self.target)), False, False)
        comp = cap[0].capture_component2d
        rt = unreal.RenderingLibrary.create_render_target2d(gw, RENDER[0], RENDER[1], unreal.TextureRenderTargetFormat.RTF_RGBA8)
        comp.texture_target = rt
        comp.fov_angle = 45.0
        comp.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        comp.capture_every_frame = False
        # 캐릭터(KM_*)·바닥·이 파티클만 찍고(레벨의 다른 시험 액터 제외), 대기·안개를 꺼 배경을 어둡게 + 노출을 낮춰 가산 파티클이 보이게
        comp.set_editor_property("primitive_render_mode", unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        comp.clear_show_only_components()
        for a in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.Actor):
            lb = a.get_actor_label()
            if lb.startswith("KM_") or (SHOW_FLOOR and lb == "T_Floor"):
                comp.show_only_actor_components(a, True)
        comp.show_only_component(self.comp)
        comp.set_editor_property("show_flag_settings", [unreal.EngineShowFlagsSetting(show_flag_name=n, enabled=False)
                                                        for n in ("Atmosphere", "Fog", "Cloud")])
        pp = comp.post_process_settings
        pp.set_editor_property("override_auto_exposure_bias", True)
        pp.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
        comp.post_process_settings = pp
        comp.post_process_blend_weight = 1.0
        comp.capture_scene()
        unreal.RenderingLibrary.export_render_target(gw, rt, preview.SHOT_DIR, name + ".png")
        px = unreal.RenderingLibrary.read_render_target(gw, rt, True)
        self.cells.setdefault(path, []).append(px)
        self.results[path]["shots"].append(f"{preview.SHOT_DIR}/{name}.png")
        self.t_index += 1
        if self.t_index >= len(self.times):
            self._cleanup_comp()
            self.i += 1
            self.t_index = 0

    def _finish(self):
        self.done = True
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        try:
            self.sheets = _sheets(self.cells, self.label, len(self.times))
        except Exception as e:
            self.error = (self.error or "") + f" 시트 합성 실패: {e}"
        _write_status(self)


def _sheets(cells, label, n_times):
    """분류별 시트: 한 행 = 에셋 하나(이름 칸 없음 — 파일명 순서는 status.json 참조), 열 = 시점."""
    by_cat = {}
    for path, pxs in cells.items():
        by_cat.setdefault(path.split("/")[3], []).append((path, pxs))
    out = []
    cw, ch = CELL, CELL * RENDER[1] // RENDER[0]
    sx, sy = RENDER[0] / cw, RENDER[1] / ch
    for cat, items in by_cat.items():
        cols = n_times
        rows = len(items)
        W, H = cw * cols, ch * rows
        img = [(0.08, 0.08, 0.09, 1.0)] * (W * H)
        for r, (path, pxs) in enumerate(items):
            for c, px in enumerate(pxs[:cols]):
                ox, oy = c * cw, r * ch
                for y in range(ch):
                    row = int(y * sy) * RENDER[0]
                    for x in range(cw):
                        p = px[row + int(x * sx)]
                        img[(oy + y) * W + ox + x] = (p.r / 255.0, p.g / 255.0, p.b / 255.0, 1.0)
        p = os.path.join(preview.SHOT_DIR, f"fx_sheet_{label}_{cat}.png")
        icon._write_png(p, img, W, H)
        out.append({"category": cat, "png": p, "order": [path for path, _ in items]})
    return out


def _write_status(job):
    os.makedirs(preview.SHOT_DIR, exist_ok=True)
    with open(STATUS_PATH, "w", encoding="utf-8") as f:
        json.dump(status(), f, ensure_ascii=False, indent=1)


def start(keys=None, category=None, times=TIMES, label="all", attach=None):
    """촬영 작업을 시작한다(Simulate 중이어야 함). keys: 에셋 경로 또는 이름 목록, category: 분류 하나(둘 다 없으면 전부).
    attach: None = 분류별 기본(ATTACHED), True/False = 전부 캐릭터 부착/원점."""
    global _job
    if UES.get_game_world() is None:
        raise RuntimeError("Simulate 중이 아님 — preview.start_simulate() 후 다음 호출에서 start()")
    if _job is not None and not _job.done:
        raise RuntimeError("이미 진행 중 — status()로 확인")
    paths = list_fx(category)
    if keys:
        want = set(keys)
        paths = [p for p in paths if p in want or p.split("/")[-1] in want]
    if not paths:
        raise RuntimeError(f"찍을 파티클이 없음 — {FX_ROOT}/{category or '*'} (keys={keys})")
    _set_exposure(UES.get_game_world(), EXPOSURE_EV)
    # 주의: 에디터가 뒤에 있으면("Use Less CPU when in Background") GPU 파티클이 진행되지 않아 불꽃·섬광이 빈 화면으로 찍힌다.
    # 설정은 Python에 노출되지 않으므로 호출 전에 에디터 창을 앞으로 가져온다(PowerShell AppActivate).
    _job = _Job(paths, times, label if not category else category, attach)
    _job.handle = unreal.register_slate_post_tick_callback(_job.tick)
    return {"count": len(paths), "times": list(times), "status": STATUS_PATH}


def status():
    if _job is None:
        return {"state": "idle"}
    j = _job
    return {"state": "done" if j.done else "running", "progress": f"{min(j.i, len(j.paths))}/{len(j.paths)}",
            "error": j.error, "sheets": j.sheets, "results": j.results}


def stop():
    """진행 중인 작업을 중단한다(시트는 그때까지 찍은 것으로 합성)."""
    if _job is not None and not _job.done:
        _job._cleanup_comp()
        _job._finish()
    return status()
