"""환경 에셋 폴더 정리(2026-11-21, D-245) — World/·FX/Dep를 테마 기준 Environment/·맵 전용 Maps/<맵>/·외부 팩 _ThirdParty/로 옮긴다.

현업 방식: 소품은 여러 맵에서 다시 쓰이므로 맵이 아니라 테마(마을·던전·공용)로 나누고, 레벨과 그 맵에만 쓰는 것(지형 재질·풀·레이어)만
맵 폴더에 둔다. 외부 팩(Fab·Paragon·KiteDemo) 원본 의존은 _ThirdParty 한곳에 모아 공개 전에 빼기 쉽게 한다(D-227).
레벨 파일은 /Game/Maps/<이름> 그대로 — 클라이언트가 서버가 준 맵 이름으로 이 경로를 연다(D-185).

부르는 법: WorldTools.world_step(name="", step="env_<단계>", args_json)
  env_plan                 옮길 목록·대응 없는 것·이미 있는 대상
  env_move  {dry_run}      옮기기(참조 갱신) → 맵이 아닌 참조자 저장. 맵은 리디렉터로 남음 → env_maps
  env_maps                 남은 리디렉터를 참조하는 맵 목록
  env_resave_map {map}     그 맵을 열어 다시 저장(한 번에 맵 하나 — 저장 안 된 맵이 있으면 거부)
  env_redirectors {dry_run}  참조자가 없어진 리디렉터 삭제
  env_tables {write}       맵 정의 JSON(world_*.json)의 옛 경로 문자열 치환
"""
import json
import os
import re

import unreal

from . import equip_layout as EQL

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

_V, _A, _C, _D = "/Game/Environment/Village/Props", "/Game/Environment/Village/Architecture", "/Game/Environment/Common/Props", "/Game/Environment/Dungeon/Props"
PROP_THEME = {
    "Banner": _V, "Cart": _V, "InnTent": _V, "MarketStall": _V, "TradeAwning": _V,
    "Palisade": _A, "Rampart": _A, "GateFrame": _A, "ClosedGate": _A,
    "Campfire": _C, "CratesBarrels": _C,   # 마을·던전 둘 다 씀
    "BonePile": _D, "Brazier": _D, "GuardianStatue": _D, "Sarcophagus": _D, "WallTorch": _D, "WeaponRack": _D,
}
# (옛 접두사, 새 접두사) — 긴 것부터 맞춘다
PREFIXES = sorted([(f"/Game/World/Props/{n}/", f"{d}/{n}/") for n, d in PROP_THEME.items()] + [
    ("/Game/World/Dungeon/", "/Game/Environment/Dungeon/"),
    ("/Game/World/Continent01/", "/Game/Maps/Continent01/"),
    ("/Game/World/Dep/KiteDemo/", "/Game/_ThirdParty/KiteDemo/"),
    ("/Game/World/Dep/ParagonProps/", "/Game/_ThirdParty/ParagonProps/"),
    ("/Game/FX/Dep/", "/Game/_ThirdParty/ParagonFX/"),
], key=lambda m: -len(m[0]))
OLD_ROOTS = ("/Game/World", "/Game/FX/Dep")
PY_DIR = os.path.dirname(__file__)
JSONS = ("world_continent01.json", "world_continent01_dungeon.json")


def dest(pkg):
    for src, dst in PREFIXES:
        if pkg.startswith(src):
            return dst + pkg[len(src):]
    return None


def plan():
    rows, unmapped = [], []
    for r in OLD_ROOTS:
        for p in EQL._packages(r):
            d = dest(p)
            (rows.append((p, d)) if d else unmapped.append(p))
    return {"count": len(rows), "unmapped": unmapped, "exists": [d for _p, d in rows if EAL.does_asset_exist(d)],
            "by_prefix": {s: sum(1 for p, _d in rows if p.startswith(s)) for s, _ in PREFIXES}, "sample": rows[:8]}


def move(dry_run=True):
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중 — 종료 후 다시 호출")
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    pl = plan()
    if dry_run or pl["exists"] or pl["unmapped"] or dirty:
        pl.update({"dry_run": dry_run, "dirty_maps": dirty})
        return pl
    rows = [(p, dest(p)) for r in OLD_ROOTS for p in EQL._packages(r)]
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    data, failed = [], []
    for p, d in rows:
        obj = unreal.load_asset(p)
        if obj is None:
            failed.append(p)
            continue
        data.append(unreal.AssetRenameData(obj, d.rsplit("/", 1)[0], d.rsplit("/", 1)[1]))
    ok = tools.rename_assets(data)
    # 맵이 아닌 참조자만 저장(열려 있는 맵도 저장되지 않게 맵 패키지는 뺀다 — 맵은 env_resave_map으로 하나씩)
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    return {"renamed": ok, "count": len(data), "failed_load": failed, "saved_content": saved, "maps": maps_with_redirectors()}


def _redirector_pkgs():
    return [str(a.package_name) for a in EQL._redirectors(OLD_ROOTS)]


def maps_with_redirectors():
    reds = set(_redirector_pkgs())
    maps = set()
    for r in reds:
        for ref in EQL._referencers(r):
            if ref in reds:
                continue
            if any(str(a.asset_class_path.asset_name) == "World" for a in AR.get_assets_by_package_name(unreal.Name(ref))):
                maps.add(ref)
    return {"redirectors": len(reds), "maps": sorted(maps)}


def resave_map(level):
    UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if UES.get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중 — 종료 후 다시 호출")
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    if dirty:
        raise RuntimeError(f"저장되지 않은 맵이 있음 — 저장하거나 되돌린 뒤 다시 호출: {dirty}")
    if UES.get_editor_world().get_outermost().get_name() != level:
        LES.load_level(level)
    ok = LES.save_current_level()
    return {"level": level, "saved": ok}


def redirectors(dry_run=True):
    return EQL.fix_redirectors(OLD_ROOTS, dry_run=dry_run) if dry_run else _delete_free_redirectors()


def _delete_free_redirectors():
    reds = _redirector_pkgs()
    deleted, kept = [], []
    for r in reds:
        left = [x for x in EQL._referencers(r) if x not in reds]
        if left:
            kept.append({"redirector": r, "referencers": left})
        elif EAL.delete_asset(r):
            deleted.append(r)
    return {"deleted": len(deleted), "kept": kept}


def tables(write=False):
    """맵 정의 JSON의 옛 경로 문자열을 새 경로로(JSON 구조는 건드리지 않고 문자열만)."""
    out = {}
    for name in JSONS:
        path = os.path.join(PY_DIR, name)
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        n = 0
        for src, dst in PREFIXES:
            n += text.count(src)
            text = text.replace(src, dst)
        # 접두사 끝 "/"가 없는 폴더 경로(예 "asset_dir": "/Game/World/Continent01")
        for src, dst in PREFIXES:
            s, d = src.rstrip("/"), dst.rstrip("/")
            k = len(re.findall(re.escape(s) + r'(?=")', text))
            n += k
            text = re.sub(re.escape(s) + r'(?=")', d, text)
        left = sorted(set(re.findall(r'/Game/(?:World|FX/Dep)/[^"]*', text)))
        json.loads(text)   # 깨지지 않았는지
        if write:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(text)
        out[name] = {"replaced": n, "left": left}
    return {"written": write, "files": out}


def run(step, args=None):
    args = args or {}
    if step == "env_plan":
        return plan()
    if step == "env_move":
        return move(args.get("dry_run", True))
    if step == "env_maps":
        return maps_with_redirectors()
    if step == "env_resave_map":
        return resave_map(args["map"])
    if step == "env_redirectors":
        return redirectors(args.get("dry_run", True))
    if step == "env_refs":
        return {p: EQL._referencers(p) for p in args["pkgs"]}
    if step == "env_tables":
        return tables(bool(args.get("write", False)))
    raise ValueError(f"알 수 없는 단계: {step}")
