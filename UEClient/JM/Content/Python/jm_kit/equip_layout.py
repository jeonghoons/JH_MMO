"""장비 에셋 폴더 정리(IT0, D-241) — 재질 기준 옛 구조(Equipment/Cloth·Plate·Common)를 등급 기준 새 구조로 옮기고
items.json·ItemTable.csv의 경로를 고친다.

새 구조(D-241):
  Equipment/_Shared/                      M_Equip_PBR, SM_Arrow
  Equipment/_Icons/                       T_Icon_<키>
  Equipment/_Armor/Male|Female/<부위>/    SK_<키>_<M|F> (+ 장갑의 체형 전용 MI_·T_)
  Equipment/_Armor/Unisex/Head/           SM_<키> (투구·두건 — 남녀 공용 정적 메시)
  Equipment/_Armor/Source/<키>/           SM_<키>_Src, M_<키>, T_<키>_BaseColor·Normal·ORM
  Equipment/_Weapon/<OneHandSword|TwoHandSword|Bow|Shield>/  SM_<키>
  Equipment/_Weapon/Source/<키>/          M_<키>, T_<키>_*
키 = <등급 두 자리>_<부위>(예 00_Top, 00_OneHandSword). 지금 판금 세트는 처리를 정할 때까지 옛 이름(Plate_Top 등)을 그대로 두고 폴더만 옮긴다.

부르는 법: WorldTools.world_step(name="", step="eq_<단계>", args_json)
  eq_plan                옮길 목록(옛 → 새), 대응이 없는 에셋, 이미 있는 대상
  eq_move   {dry_run}    옮기기(AssetTools.rename_assets — 참조 갱신) → 저장 → eq_redirectors. 기본 dry_run
  eq_redirectors {dry_run}  옛 폴더에 남은 리디렉터의 참조자를 다시 저장하고, 참조가 없어진 리디렉터 삭제. 기본 dry_run
  eq_tables {write}      items.json 키·경로(+ 정적 장비 src_dir), ItemTable.csv 경로 열(Icon·MeshM·MeshF·StaticMesh). 기본 write False
  eq_unused {paths}      지울 후보(기본 DELETE_CANDIDATES)와 그 참조자 — 참조자가 없는 것만 지울 수 있음
  eq_delete {paths, dry_run}  참조자가 없는 것만 삭제. 기본 dry_run
  eq_gallery             진열 레벨 GALLERY를 (다시) 만든다 — 세트마다 남·녀 × POSES, 이름표
  eq_gallery_shots {label}  Simulate 중(KitTools.preview_start_simulate 다음 호출)에 그룹마다 앞·뒤 촬영
  eq_archive_plan        옛 판금(이름에 _Plate_) → _Archive/OldPlate(하위 경로 유지) 목록과 묶음 밖 참조자(D-244)
  eq_archive_move {dry_run}  그대로 옮기고 Equipment 아래 리디렉터 정리. 기본 dry_run
  eq_archive_items {write}   items.json Plate_* 경로 → 백업 폴더 + "archived": true(load_items가 기본으로 뺌). 기본 write False
  eq_archive_dirs {dry_run}  옮긴 뒤 남은 빈 Plate 폴더 삭제. 기본 dry_run
  eq_glow_master {rebuild}   03등급 발광 부모 머티리얼 M_Equip_PBR_Glow(D-267)
  eq_glow {keys, params, rebuild_master}  M_03_*의 부모를 발광 머티리얼로 — params는 공통 {이름: 값} 또는 {"<키>": {...}}
  eq_icon_warm {keys}     아이콘 촬영 전 데우기(새로 켠 에디터·다시 컴파일한 머티리얼에서 첫 촬영이 비거나 회색) → 몇 초 뒤 icon_shoot → eq_icon_warm_clear
  eq_color_dump {prefix}  등급(예 "01_") 아이템 메시가 쓰는 머티리얼과 최상위 부모(D-279)
  eq_color {prefix, desat, tint, keys}  그 인스턴스에 Desat·Tint(부모에 매개변수가 없으면 그 자리에서 더함 — 기본값이면 외형 그대로)
"""
import json
import os
import re

import unreal

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

EQ = "/Game/Equipment"
OLD_ROOTS = (f"{EQ}/Cloth", f"{EQ}/Plate", f"{EQ}/Common")
SKINNED = ("Top", "Bottom", "Boots", "Gloves")
# 옛 부위 폴더 → (분류 폴더, 새 부위 이름, 메시 폴더)
STATIC = {"Hood": ("_Armor", "Head", "Unisex/Head"), "Helm": ("_Armor", "Head", "Unisex/Head"),
          "Sword": ("_Weapon", "OneHandSword", "OneHandSword"), "Bow": ("_Weapon", "Bow", "Bow"),
          "Shield": ("_Weapon", "Shield", "Shield")}
GRADE = {"Cloth": "00"}   # 판금(Plate)은 처리 대기 — 옛 이름 유지(D-241)
BODY_DIR = {"M": "Male", "F": "Female"}
# 쓰는 곳이 없으면 지울 후보(D-241 — 목록을 사용자에게 보인 뒤 삭제)
DELETE_CANDIDATES = (f"{EQ}/Cloth/Top/Source/T_Cloth_Top_ORM_New", "/Game/Dev/MC/Fit", "/Game/Dev/MU/Retex",
                     "/Game/Dev/MC/Source/test_top")

PY_DIR = os.path.dirname(__file__)
CSV_PATH = os.path.normpath(os.path.join(unreal.Paths.project_dir(), "../../Common/DataTable/ItemTable.csv"))
CSV_PATH_COLS = ("Icon", "MeshM", "MeshF", "StaticMesh")


def new_key(tier, slot):
    """옛 (티어, 부위 폴더) → 새 아이템 키."""
    if tier in GRADE:
        return f"{GRADE[tier]}_{STATIC[slot][1] if slot in STATIC else slot}"
    return f"{tier}_{slot}"


def _packages(root):
    if not EAL.does_directory_exist(root):
        return [root] if EAL.does_asset_exist(root) else []
    return sorted({str(a.package_name) for a in AR.get_assets_by_path(unreal.Name(root), recursive=True)
                   if str(a.asset_class_path.asset_name) != "ObjectRedirector"})


def dest(pkg):
    """옛 패키지 경로 → 새 패키지 경로(대응이 없으면 None)."""
    rel = pkg[len(EQ) + 1:].split("/")
    if rel[0] == "Common":
        return f"{EQ}/_Shared/{rel[-1]}"
    if len(rel) not in (3, 4) or (len(rel) == 4 and rel[2] != "Source"):
        return None
    tier, slot, name = rel[0], rel[1], rel[-1]
    in_src = len(rel) == 4
    key, old = new_key(tier, slot), f"{tier}_{slot}"
    if old not in name:
        return None
    nn = name.replace(old, key)
    if nn.startswith("T_Icon_"):
        return f"{EQ}/_Icons/{nn}"
    if slot in STATIC:
        grp, _ns, folder = STATIC[slot]
        if in_src and nn.startswith("SM_"):
            if tier in GRADE and nn.endswith("_Src"):
                nn = nn[:-len("_Src")]   # 정적 장비는 이 메시를 그대로 쓰므로 원본 표시를 뗌(D-241)
            return f"{EQ}/{grp}/{folder}/{nn}"
        return f"{EQ}/{grp}/Source/{key}/{nn}" if in_src else None
    if slot in SKINNED:
        if in_src:
            return f"{EQ}/_Armor/Source/{key}/{nn}"
        m = re.match(rf"^[A-Z]+_{re.escape(key)}_([MF])(_|$)", nn)
        if m:
            return f"{EQ}/_Armor/{BODY_DIR[m.group(1)]}/{slot}/{nn}"
    return None


def mapping():
    excluded = set(p for c in DELETE_CANDIDATES for p in _packages(c))
    rows, unmapped = [], []
    for root in OLD_ROOTS:
        for p in _packages(root):
            d = None if p in excluded else dest(p)
            (rows.append((p, d)) if d else unmapped.append(p))
    return rows, unmapped


def plan():
    rows, unmapped = mapping()
    return {"count": len(rows), "moves": rows, "unmapped": unmapped,
            "exists": [d for _p, d in rows if EAL.does_asset_exist(d)],
            "dup_dest": sorted({d for _p, d in rows if [x for _q, x in rows].count(d) > 1})}


def _redirectors(roots):
    out = []
    for r in roots:
        for a in AR.get_assets_by_path(unreal.Name(r), recursive=True):
            if str(a.asset_class_path.asset_name) == "ObjectRedirector":
                out.append(a)
    return out


def move(dry_run=True):
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중에는 옮길 수 없음 — 종료(StopPIE) 후 다시 호출")
    pl = plan()
    if dry_run or pl["exists"] or pl["dup_dest"]:
        pl["dry_run"] = dry_run
        return pl
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    data, failed = [], []
    for p, d in pl["moves"]:
        obj = unreal.load_asset(p)
        if obj is None:
            failed.append(p)
            continue
        data.append(unreal.AssetRenameData(obj, d.rsplit("/", 1)[0], d.rsplit("/", 1)[1]))
    ok = tools.rename_assets(data)
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    fix = fix_redirectors(dry_run=False)   # UE 5.8 Python에는 AssetTools.fixup_referencers가 없다(2026-11-21 확인)
    remaining = [p for r in OLD_ROOTS for p in _packages(r)]
    return {"renamed": ok, "count": len(data), "failed_load": failed, "saved": saved, "redirectors": fix,
            "old_remaining": remaining}


def fix_redirectors(roots=OLD_ROOTS, dry_run=True):
    """roots 아래 리디렉터의 참조자를 다시 저장해 새 경로로 굳히고(불러오면 리디렉터를 따라가 실제 에셋을 가리킴),
    참조자가 남지 않은 리디렉터를 지운다. 맵은 에디터에 열려 있는 것만 저장한다(나머지는 skipped — 열어서 다시)."""
    reds = [str(a.package_name) for a in _redirectors(roots)]
    rows = [{"redirector": r, "referencers": _referencers(r)} for r in reds]
    if dry_run:
        return {"dry_run": True, "rows": rows}
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    open_map = world.get_outermost().get_name() if world else ""
    saved, skipped, failed = [], [], []
    for ref in sorted({x for row in rows for x in row["referencers"] if x not in reds}):
        classes = [str(a.asset_class_path.asset_name) for a in AR.get_assets_by_package_name(unreal.Name(ref))]
        if "World" in classes:
            if ref != open_map:
                skipped.append(ref)
                continue
            ok = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        else:
            asset = EAL.load_asset(ref)
            if asset is not None:
                asset.modify()
            ok = bool(asset) and EAL.save_loaded_asset(asset, False)
        (saved if ok else failed).append(ref)
    deleted, kept = [], []
    for r in reds:
        left = [x for x in _referencers(r) if x not in reds]
        if left:
            kept.append({"redirector": r, "referencers": left})
        elif EAL.delete_asset(r):
            deleted.append(r)
    return {"saved": saved, "skipped_maps": skipped, "failed": failed, "deleted": deleted, "kept": kept}


# ── 표 갱신 ─────────────────────────────────────────────

def _load_items_raw():
    with open(os.path.join(PY_DIR, "items.json"), encoding="utf-8") as f:
        return json.load(f)


def _new_items():
    items = _load_items_raw()
    new = {}
    changes = []
    for k, it in items.items():
        if k.startswith("_"):
            new[k] = it
            continue
        tier, slot = k.split("_", 1)
        if tier not in ("Cloth", "Plate"):
            new[k] = it   # 이미 새 키
            continue
        nk = new_key(tier, slot)
        it = dict(it)
        src = it["source"]
        it["source"] = dest(src) or src
        if "out" in it:
            m = dest(it["out"].format(body="M"))
            it["out"] = m.replace("/_Armor/Male/", "/_Armor/{body_dir}/")[:-len("_M")] + "_{body}"
        if slot in STATIC:
            it["src_dir"] = f"{EQ}/{STATIC[slot][0]}/Source/{nk}"
        new[nk] = it
        changes.append({"key": k, "new_key": nk, "source": it["source"], "out": it.get("out"), "src_dir": it.get("src_dir")})
    return new, changes


def _obj(pkg):
    return f"{pkg}.{pkg.rsplit('/', 1)[1]}"


def tables(write=False):
    """items.json(키·source·out·src_dir)과 ItemTable.csv 경로 열을 새 구조로 고친다."""
    new, changes = _new_items()
    # CSV: 옛 객체 경로 → 새 객체 경로(문자열 치환 — 다른 열·BOM·줄바꿈 유지)
    with open(CSV_PATH, encoding="utf-8", newline="") as f:
        text = f.read()
    csv_changes = []
    pkgs = set(re.findall(r"(/Game/Equipment/(?:Cloth|Plate|Common)/[^.,\"]+)\.", text))
    for p in sorted(pkgs, key=len, reverse=True):
        d = dest(p)
        if not d:
            csv_changes.append({"old": p, "new": None})
            continue
        n = text.count(_obj(p))
        text = text.replace(_obj(p), _obj(d))
        csv_changes.append({"old": p, "new": d, "count": n})
    unresolved = [c["old"] for c in csv_changes if c["new"] is None]
    if write:
        if unresolved:
            raise RuntimeError(f"새 경로가 없는 CSV 경로: {unresolved}")
        with open(os.path.join(PY_DIR, "items.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(new, f, ensure_ascii=False, indent=2)
            f.write("\n")
        with open(CSV_PATH, "w", encoding="utf-8", newline="") as f:
            f.write(text)
    missing_assets = [c["new"] for c in csv_changes if c["new"] and not EAL.does_asset_exist(c["new"])]
    return {"written": write, "items": changes, "csv": csv_changes, "unresolved": unresolved,
            "csv_targets_missing": missing_assets}


# ── 지울 후보 ───────────────────────────────────────────

def _referencers(pkg):
    opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                                include_searchable_names=False, include_soft_management_references=False,
                                                include_hard_management_references=False)
    return [str(r) for r in (AR.get_referencers(unreal.Name(pkg), opt) or [])]


def unused(paths=None):
    """지울 후보(폴더·패키지) 아래 패키지마다 후보 밖의 참조자. 참조자가 없는 것만 지울 수 있다."""
    roots = tuple(paths or DELETE_CANDIDATES)
    pkgs = [p for r in roots for p in _packages(r)]
    inside = set(pkgs)
    rows = []
    for p in pkgs:
        refs = [r for r in _referencers(p) if r not in inside]
        rows.append({"pkg": p, "class": ",".join(str(a.asset_class_path.asset_name) for a in AR.get_assets_by_package_name(unreal.Name(p))),
                     "referencers": refs})
    return {"count": len(rows), "free": [r["pkg"] for r in rows if not r["referencers"]],
            "used": [r for r in rows if r["referencers"]], "rows": rows}


def delete(paths=None, dry_run=True):
    u = unused(paths)
    if dry_run or u["used"]:
        return {"dry_run": dry_run, "would_delete": u["free"], "blocked": u["used"]}
    done = [p for p in u["free"] if EAL.delete_asset(p)]
    for r in tuple(paths or DELETE_CANDIDATES):
        if EAL.does_directory_exist(r) and not EAL.list_assets(r, recursive=True):
            EAL.delete_directory(r)
    return {"deleted": done, "failed": [p for p in u["free"] if p not in done]}


# ── 진열 레벨(D-241 "한눈에 보기") ─────────────────────────────

GALLERY = "/Game/Dev/Gallery/L_GearGallery"   # Content/Dev는 git 제외 — 이 단계로 언제든 다시 만든다
GAL = "GG"                                     # 진열 액터 라벨 접두사
_ANIM = "/Game/Character/Mannequin/Anims/"
# (이름표, 애니메이션, 무기, 시작 초, 재생) — 가중치 문제가 잘 드러나는 자세. 멈춘 둘은 posed.POSES의 대표 프레임
# (sword_skill_up 0.875초 = 팔을 머리 위로, bow_draw 1.0초 = 당김 끝 — 양팔 어깨 높이)
POSES = (("Sword Idle", "OneHandSword/AS_OneHandSword_Idle", "sword", 0.0, True),
         ("Sword Run", "OneHandSword/AS_OneHandSword_Run", "sword", 0.0, True),
         ("Sword Skill1", "OneHandSword/AS_OneHandSword_Skill1", "sword", 0.0, True),
         ("Arms Up (stop)", "OneHandSword/AS_OneHandSword_Skill1", "sword", 0.875, False),
         ("Bow Draw (stop)", "Bow/AS_Bow_Attack", "bow", 1.0, False))
ARMOR_SLOTS = ("head", "top", "bottom", "gloves", "boots")
STEP_CM, BODY_GAP_CM, SET_GAP_CM = 140, 160, 400


def _sets():
    """items.json 키의 앞부분(등급 00~05, 처리 대기 Plate)으로 세트를 묶는다. 숫자 등급 먼저."""
    from . import garment
    sets = {}
    for k, it in garment.load_items().items():
        sets.setdefault(k.split("_", 1)[0], {})[k] = it
    return dict(sorted(sets.items(), key=lambda kv: (not kv[0].isdigit(), kv[0])))


def _side(it):
    """무기가 붙는 쪽 — 부착 대상(뼈 hand_r·hand_l 또는 소켓 WeaponSocket_R·_L, D-247)의 끝 글자."""
    s = it.get("attach", {}).get("socket", "").lower()
    return "r" if s.endswith("_r") else "l" if s.endswith("_l") else None


def _outfit(items, weapon):
    out = {it["slot"]: k for k, it in items.items() if it["slot"] in ARMOR_SLOTS}
    pick = lambda f: next((k for k, it in items.items() if f(it)), None)
    sword = pick(lambda it: it["slot"] == "weapon" and _side(it) == "r")
    bow = pick(lambda it: it["slot"] == "weapon" and _side(it) == "l")
    shield = pick(lambda it: it["slot"] == "offhand")
    if weapon == "sword":
        out.update({k: v for k, v in (("weapon", sword), ("offhand", shield)) if v})
    elif bow:
        out["weapon"] = bow
    return out


def _text(label, text, loc, size):
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    a = EAS.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*loc), unreal.Rotator(0, 0, 90))   # 정면(+Y)에서 읽힘
    a.set_actor_label(label)
    t = a.text_render
    t.set_text(text)
    t.set_editor_property("world_size", size)
    t.set_editor_property("text_render_color", unreal.Color(15, 15, 15, 255))
    try:
        t.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    except Exception:
        pass
    return a


def gallery():
    """진열 레벨을 (다시) 만든다: 세트마다 남·녀 × POSES. 애니메이션은 Simulate 중에만 재생된다(에디터 월드는 틱하지 않음)."""
    from . import preview
    UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if UES.get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중 — 종료 후 다시 호출")
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() if p.get_name() not in (GALLERY, FIT_LEVEL)]
    if UES.get_editor_world().get_outermost().get_name() in (GALLERY, FIT_LEVEL):
        LES.save_current_level()   # 진열 레벨은 이 단계가 다시 만드는 것 — 사용자 작업이 아님
    if dirty:   # 저장 안 된 맵이 있으면 레벨 전환이 저장 여부 창을 띄워 MCP 호출이 멈춘다 — 사용자 작업을 대신 저장하지 않음
        raise RuntimeError(f"저장되지 않은 맵이 있음 — 저장하거나 되돌린 뒤 다시 호출: {dirty}")
    # 시험 레벨과 같은 조명·바닥·캡처 액터로 열거나 템플릿에서 새로 만든다.
    # duplicate_asset(시험 레벨) → load_level은 에디터를 죽인다(World Memory Leaks — preview.setup_level 주석)
    preview.setup_level(GALLERY)
    for a in EAS.get_all_level_actors():   # 고정 액터만 남기고 캐릭터·이름표·시험 플레이어를 지움
        cls = a.get_class().get_name()
        if a.get_actor_label() not in preview.FIXTURES and (
                cls in ("SkeletalMeshActor", "StaticMeshActor", "TextRenderActor") or "TestPlayer" in cls):
            EAS.destroy_actor(a)
    groups, x = [], 0
    for tag, items in _sets().items():
        for body in ("M", "F"):
            x0 = x
            for i, (label, anim, weapon, start, playing) in enumerate(POSES):
                prefix = f"{GAL}_{tag}_{body}_{i}"
                lead = preview.spawn(prefix, body, (x, 0, 0), outfit=_outfit(items, weapon), anim=_ANIM + anim)
                if start or not playing:
                    lc = lead.skeletal_mesh_component
                    d = lc.get_editor_property("animation_data")
                    d.set_editor_property("saved_position", float(start))
                    d.set_editor_property("saved_playing", bool(playing))
                    lc.set_editor_property("animation_data", d)
                _text(f"{prefix}_label", label, (x, 40, 215), 12)
                x += STEP_CM
            cx = (x0 + x - STEP_CM) / 2
            _text(f"{GAL}_{tag}_{body}_title", f"{tag}  {'Male' if body == 'M' else 'Female'}", (cx, 40, 240), 22)
            groups.append({"set": tag, "body": body, "center_x": cx, "items": sorted(items)})
            x += BODY_GAP_CM
        x += SET_GAP_CM
    preview.look_at((groups[0]["center_x"], 520, 160), (groups[0]["center_x"], 0, 100))
    LES.save_current_level()
    return {"level": GALLERY, "groups": groups, "length_cm": x}


def gallery_shots(label=""):
    """Simulate 중(preview.start_simulate 다음 호출)에 그룹마다 앞·뒤를 찍는다 → Saved/MCShots/gallery_<세트>_<체형>_<front|back>.png."""
    from . import preview
    out = []
    groups = []
    x = 0
    for tag in _sets():
        for body in ("M", "F"):
            groups.append((tag, body, x + STEP_CM * (len(POSES) - 1) / 2))
            x += STEP_CM * len(POSES) + BODY_GAP_CM
        x += SET_GAP_CM
    for tag, body, cx in groups:
        for side, y in (("front", 640), ("back", -640)):
            out.append(preview.shot_game(f"gallery{label}_{tag}_{body}_{side}", (cx, y, 135), (cx, 0, 105), 62.0, 1600, 800))
    return out


# ── 맞춤 레벨(D-243 — 무기·방패·머리 장비 위치를 사용자가 뷰포트에서 직접 잡음) ─────────────

FIT_LEVEL = "/Game/Dev/Gallery/L_GearFit"
FIT = "FIT"
# 캐릭터 3명/세트·체형: (이름, 자세 키(posed.POSES), 손 무기 종류, 등에 멜 무기 종류들)
FIT_RIGS = (("Sword", "sword_idle", "sword", ()), ("Bow", "bow_idle", "bow", ()), ("Back", "sword_idle", None, ("sword", "bow")))
FIT_STEP, FIT_BODY_GAP, FIT_SET_GAP = 130, 120, 300


def _fit_map_path():
    return os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Fit", "fit_map.json")


def _weapons(items):
    pick = lambda f: next((k for k, it in items.items() if f(it)), None)
    return {"sword": pick(lambda it: it["slot"] == "weapon" and _side(it) == "r"),
            "bow": pick(lambda it: it["slot"] == "weapon" and _side(it) == "l"),
            "shield": pick(lambda it: it["slot"] == "offhand"),
            "head": pick(lambda it: it["slot"] == "head")}


def _attach_static(lead, key, body, which, label, folder):
    from . import garment
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    s = garment.static_attachment(key, body, which)
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, lead.get_actor_location())
    a.set_actor_label(label)
    sc = a.static_mesh_component
    sc.set_static_mesh(unreal.load_asset(s["mesh"]))
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    a.attach_to_component(lead.skeletal_mesh_component, s["socket"], unreal.AttachmentRule.SNAP_TO_TARGET,
                          unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET, False)
    sc.set_relative_transform(s["transform"], False, True)
    a.set_folder_path(folder)
    return {"label": label, "key": key, "body": body, "which": which, "socket": s["socket"], "manual": bool(s.get("manual"))}


def fit_build(sets=None):
    """맞춤 레벨을 (다시) 만든다. 세트 × 남녀 × FIT_RIGS를 posed.rig로 세우고 자세 프레임에 멈춘다(에디터 화면에 그대로 보임).
    조정할 물체: 라벨 FIT_<키>_<M|F>_<attach|back>(아웃라이너 폴더 FIT/<세트>/<체형>). 저장된 수동값이 있으면 그 위치로 놓는다."""
    from . import posed, preview
    UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if UES.get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중 — 종료 후 다시 호출")
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() if p.get_name() not in (GALLERY, FIT_LEVEL)]
    if dirty:
        raise RuntimeError(f"저장되지 않은 맵이 있음 — 저장하거나 되돌린 뒤 다시 호출: {dirty}")
    if UES.get_editor_world().get_outermost().get_name() in (GALLERY, FIT_LEVEL):
        LES.save_current_level()
    preview.setup_level(FIT_LEVEL)
    for a in EAS.get_all_level_actors():
        cls = a.get_class().get_name()
        if a.get_actor_label() not in preview.FIXTURES and (
                cls in ("SkeletalMeshActor", "StaticMeshActor", "TextRenderActor") or "TestPlayer" in cls):
            EAS.destroy_actor(a)
    targets, x = [], 0
    for tag, items in _sets().items():
        if sets and tag not in sets:
            continue
        w = _weapons(items)
        armor = {s: k for k, it in items.items() for s in ("top", "gloves", "bottom", "boots") if it["slot"] == s}
        for body in ("M", "F"):
            folder = f"{FIT}/{tag}/{body}"
            for name, pose_key, hand, backs in FIT_RIGS:
                prefix = f"{FIT}_{tag}_{body}_{name}"
                loadout = dict(armor)
                weapon_key = w.get(hand) if hand else None
                if hand == "sword" and w["shield"]:
                    loadout.update({"sword": weapon_key, "shield": w["shield"]})
                rec = posed.rig(prefix, body, loadout, weapon_key=weapon_key, loc=(x, 0, 0))
                posed.pose(prefix, pose_key)
                for a in EAS.get_all_level_actors():
                    if a.get_actor_label().startswith(prefix):
                        a.set_folder_path(folder)
                # 손 무기·방패: rig가 붙인 액터를 맞춤 대상 라벨로 바꾼다
                for slot, key in (("weapon", weapon_key), ("offhand", w["shield"] if hand == "sword" else None)):
                    if not key:
                        continue
                    a = next(a for a in EAS.get_all_level_actors() if a.get_actor_label() == f"{prefix}_item_{slot}")
                    a.set_actor_label(f"{FIT}_{key}_{body}_attach")
                    targets.append({"label": a.get_actor_label(), "key": key, "body": body, "which": "attach",
                                    "socket": garment_socket(key, "attach")})
                if name == "Sword" and w["head"]:
                    targets.append(_attach_static(rec["lead"], w["head"], body, "attach", f"{FIT}_{w['head']}_{body}_attach", folder))
                for kind in backs:
                    key = w.get(kind)
                    if key and "back" in _items()[key]:
                        targets.append(_attach_static(rec["lead"], key, body, "back", f"{FIT}_{key}_{body}_back", folder))
                _text(f"{prefix}_label", f"{tag} {body} {name}", (x, 40, 215), 12).set_folder_path(folder)
                x += FIT_STEP
            x += FIT_BODY_GAP
        x += FIT_SET_GAP
    os.makedirs(os.path.dirname(_fit_map_path()), exist_ok=True)
    with open(_fit_map_path(), "w", encoding="utf-8") as f:
        json.dump(targets, f, ensure_ascii=False, indent=1)
    preview.look_at((200, 420, 170), (200, 0, 120))
    LES.save_current_level()
    return {"level": FIT_LEVEL, "targets": [t["label"] for t in targets], "length_cm": x,
            "note": "자세는 에디터 화면에만 걸려 있다 — 레벨을 다시 열거나 Simulate를 하면 풀리므로 그때는 fit_build를 다시 부른다"}


def _items():
    from . import garment
    return garment.load_items()


def garment_socket(key, which):
    return _items()[key][which]["socket"]


def _tf_dict(tf):
    q = tf.rotation
    return {"loc": [round(tf.translation.x, 4), round(tf.translation.y, 4), round(tf.translation.z, 4)],
            "rot": [round(q.x, 6), round(q.y, 6), round(q.z, 6), round(q.w, 6)],
            "scale": [round(tf.scale3d.x, 6), round(tf.scale3d.y, 6), round(tf.scale3d.z, 6)]}


def fit_save(write=True):
    """맞춤 레벨에서 사용자가 옮긴 물체의 소켓 기준 위치를 items.json manual.<attach|back>.<M|F>에 쓴다.
    자동 계산값과 거의 같으면(0.05cm·0.1° 이내) 쓰지 않는다. 쓴 뒤 ItemTable 외형 열을 다시 만들고 DataTable을 다시 채운다."""
    from . import garment, item_table
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    with open(_fit_map_path(), encoding="utf-8") as f:
        targets = json.load(f)
    acts = {a.get_actor_label(): a for a in EAS.get_all_level_actors()}
    raw = _load_items_raw()
    changed, same, missing = [], [], []
    for t in targets:
        a = acts.get(t["label"])
        if a is None:
            missing.append(t["label"])
            continue
        sc = a.static_mesh_component
        if str(sc.get_attach_socket_name()) != t["socket"]:
            missing.append(f"{t['label']}: 소켓이 바뀜({sc.get_attach_socket_name()}) — 붙인 대상을 바꾸지 말고 위치만 옮겨 주세요")
            continue
        cur = sc.get_relative_transform()
        ref = garment.static_attachment(t["key"], t["body"], t["which"])["transform"]
        dl = (cur.translation - ref.translation).length()
        ds = (cur.scale3d - ref.scale3d).length()
        rd = unreal.MathLibrary.normalized_delta_rotator(cur.rotation.rotator(), ref.rotation.rotator())
        da = abs(rd.roll) + abs(rd.pitch) + abs(rd.yaw)
        if dl < 0.05 and da < 0.1 and ds < 0.001:
            same.append(t["label"])
            continue
        raw[t["key"]].setdefault("manual", {}).setdefault(t["which"], {})[t["body"]] = _tf_dict(cur)
        changed.append({"label": t["label"], "moved_cm": round(dl, 2), "rot_deg": round(da, 2), "scale": round(ds, 4)})
    if write and changed:
        with open(os.path.join(PY_DIR, "items.json"), "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(raw, ensure_ascii=False, indent=2) + "\n")
        exp = item_table.export(write=True)
    else:
        exp = None
    return {"written": bool(write and changed), "changed": changed, "unchanged": len(same), "missing": missing,
            "table_changes": len(exp["items"]["changed"]) if exp else 0,
            "next": "DataTableReimportTools.reimport_csv(/Game/Data/DT_ItemTable)" if changed else None}


def fit_reset(key, body=None, which=None):
    """수동값을 지워 자동 계산으로 되돌린다(body·which를 비우면 그 아이템의 수동값 전부)."""
    raw = _load_items_raw()
    man = raw.get(key, {}).get("manual", {})
    for w in ([which] if which else list(man)):
        for b in ([body] if body else list(man.get(w, {}))):
            man.get(w, {}).pop(b, None)
        if w in man and not man[w]:
            man.pop(w)
    if not man:
        raw.get(key, {}).pop("manual", None)
    with open(os.path.join(PY_DIR, "items.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(raw, ensure_ascii=False, indent=2) + "\n")
    return {"key": key, "manual_left": raw.get(key, {}).get("manual")}


def head_boxes():
    """체형별 머리 상자(헤어 제외)·머리+헤어 상자 크기(cm) — 머리 장비 맞춤 비교용."""
    from . import garment
    out = {}
    for b in ("M", "F"):
        lo, hi = garment._region_box_bones(b, ("head",))
        hlo, hhi = garment._head_box(b)
        out[b] = {"head": [round(h - l, 2) for l, h in zip(lo, hi)], "head_hair": [round(h - l, 2) for l, h in zip(hlo, hhi)],
                  "head_ctr": [round((h + l) / 2, 2) for l, h in zip(lo, hi)],
                  "head_hair_ctr": [round((h + l) / 2, 2) for l, h in zip(hlo, hhi)]}
    return out


# ── 옛 판금 백업(D-244) ─────────────────────────────────

ARCHIVE = f"{EQ}/_Archive/OldPlate"   # 옛 판금 세트(1301~7301의 옛 외형) — 같은 ID는 02 강철이 이어받음


def _plate_packages():
    """Equipment 아래(백업 폴더 제외) 이름에 _Plate_가 든 패키지(SK_·SM_·M_·MI_·T_·T_Icon_)."""
    return [p for p in _packages(EQ) if not p.startswith(ARCHIVE) and "_Plate_" in p.rsplit("/", 1)[1]]


def archive_plan():
    """옮길 목록(원래 하위 경로 유지), 묶음 밖 참조자, 이미 있는 대상."""
    pkgs = _plate_packages()
    inside = set(pkgs)
    rows, outside, exists = [], [], []
    for p in pkgs:
        d = ARCHIVE + p[len(EQ):]
        rows.append((p, d))
        if EAL.does_asset_exist(d):
            exists.append(d)
        for r in _referencers(p):
            if r not in inside:
                outside.append({"pkg": p, "referencer": r})
    return {"count": len(rows), "moves": rows, "outside_referencers": outside, "exists": exists}


def archive_move(dry_run=True):
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Simulate/PIE 중에는 옮길 수 없음 — 종료(StopPIE) 후 다시 호출")
    pl = archive_plan()
    if dry_run or pl["exists"]:
        pl["dry_run"] = dry_run
        return pl
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    data, failed = [], []
    for p, d in pl["moves"]:
        obj = unreal.load_asset(p)
        if obj is None:
            failed.append(p)
            continue
        data.append(unreal.AssetRenameData(obj, d.rsplit("/", 1)[0], d.rsplit("/", 1)[1]))
    ok = tools.rename_assets(data)
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    fix = fix_redirectors(roots=(EQ,), dry_run=False)
    return {"renamed": ok, "count": len(data), "failed_load": failed, "saved": saved, "redirectors": fix,
            "old_remaining": _plate_packages()}


def archive_items(write=False):
    """items.json의 옛 판금 항목(Plate_*): 문자열 경로 /Game/Equipment/… → 백업 폴더, "archived": true."""
    items = _load_items_raw()
    changes = []

    def fix(v):
        if isinstance(v, str) and v.startswith(EQ + "/") and not v.startswith(ARCHIVE):
            return ARCHIVE + v[len(EQ):]
        if isinstance(v, dict):
            return {k: fix(x) for k, x in v.items()}
        if isinstance(v, list):
            return [fix(x) for x in v]
        return v

    for k, it in items.items():
        if not k.startswith("Plate_"):
            continue
        new = fix(it)
        new["archived"] = True
        changes.append({"key": k, "source": new.get("source"), "out": new.get("out"), "src_dir": new.get("src_dir")})
        items[k] = new
    missing = [c["source"] for c in changes if c["source"] and not EAL.does_asset_exist(c["source"])]
    if write:
        with open(os.path.join(PY_DIR, "items.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(items, f, ensure_ascii=False, indent=2)
            f.write("\n")
    return {"written": write, "items": changes, "source_missing": missing}


def archive_empty_dirs(dry_run=True):
    """옛 판금이 빠진 뒤 Equipment 아래(백업 폴더 제외)에 남은 빈 폴더."""
    empty = []
    for d in EAL.list_assets(EQ, recursive=True, include_folder=True):
        d = d.rstrip("/")
        if d.startswith(ARCHIVE) or not EAL.does_directory_exist(d):
            continue
        if "Plate" in d.rsplit("/", 1)[1] and not EAL.list_assets(d, recursive=True):
            empty.append(d)
    if not dry_run:
        for d in empty:
            EAL.delete_directory(d)
    return {"dry_run": dry_run, "empty": empty}


def run(step, args=None):
    args = args or {}
    if step == "eq_archive_plan":
        return archive_plan()
    if step == "eq_archive_move":
        return archive_move(args.get("dry_run", True))
    if step == "eq_archive_items":
        return archive_items(bool(args.get("write", False)))
    if step == "eq_archive_dirs":
        return archive_empty_dirs(args.get("dry_run", True))
    if step in ("eq_icon_warm", "eq_icon_warm_clear"):
        # 아이콘 촬영 전 데우기(icon.warm) — 이 호출 뒤 몇 초 지나 KitTools.icon_shoot, 끝나면 eq_icon_warm_clear
        import importlib
        from . import icon
        importlib.reload(icon)
        if step == "eq_icon_warm_clear":
            return icon.warm_clear()
        keys = args.get("keys")
        return icon.warm(keys.split(",") if isinstance(keys, str) else keys)
    if step in ("eq_color_dump", "eq_color"):
        # 등급 색 보정(D-279): garment.item_materials / apply_grade_color
        import importlib
        from . import garment
        importlib.reload(garment)
        if step == "eq_color_dump":
            return garment.item_materials(args.get("prefix", "01_"))
        return garment.apply_grade_color(args.get("prefix", "01_"), args.get("desat"), args.get("tint"), args.get("keys"))
    if step in ("eq_glow", "eq_glow_master"):
        # 03등급 재질 발광(D-267): garment.ensure_glow_master / apply_grade_glow
        import importlib
        from . import garment
        importlib.reload(garment)
        if step == "eq_glow_master":
            return {"master": garment.ensure_glow_master(bool(args.get("rebuild", False)))}
        return garment.apply_grade_glow(tuple(args.get("keys") or garment.GLOW_KEYS), args.get("params"),
                                        bool(args.get("rebuild_master", False)))
    if step == "eq_plan":
        return plan()
    if step == "eq_move":
        return move(args.get("dry_run", True))
    if step == "eq_redirectors":
        return fix_redirectors(roots=tuple(args.get("roots") or OLD_ROOTS), dry_run=args.get("dry_run", True))
    if step == "eq_tables":
        return tables(bool(args.get("write", False)))
    if step == "eq_unused":
        return unused(args.get("paths"))
    if step == "eq_delete":
        return delete(args.get("paths"), args.get("dry_run", True))
    if step == "eq_fit":
        return fit_build(args.get("sets"))
    if step == "eq_fit_save":
        return fit_save(args.get("write", True))
    if step == "eq_fit_reset":
        return fit_reset(args["key"], args.get("body"), args.get("which"))
    if step in ("eq_grip_shield", "eq_grip_shield_check"):
        import importlib
        from . import posed
        importlib.reload(posed)
        if step == "eq_grip_shield":
            return posed.grip_search_shield(args.get("body", "M"), args["weapon"], **{k: args[k] for k in ("yz", "yz_step", "tilt") if k in args})
        return posed.grip_check_shield(args.get("body", "M"), args["weapon"], args.get("delta"))
    if step in ("eq_gripm", "eq_gripm_check"):
        # 무기 종류별 여러 프레임(posed.GRIP_FRAMES)으로 소켓 맞춤 — args: body, weapon, kind(sword·shield·bow), gloves, rel
        import importlib
        from . import posed
        importlib.reload(posed)
        if step == "eq_gripm":
            return posed.grip_search_frames(args.get("body", "M"), args["weapon"], args["kind"], args.get("gloves"),
                                            **{k: args[k] for k in ("span", "step", "refine", "tilt", "tilt_step", "touch_cm", "hand_cm") if k in args})
        return posed.grip_check_frames(args.get("body", "M"), args["weapon"], args["kind"], args.get("gloves"), args.get("rel"),
                                       delta=args.get("delta"))
    if step == "eq_hand_axis":
        # 진단: 동작 프레임마다 손바닥 위치를 무기 로컬(칼 축)로 — args: body, weapon, frames [[시퀀스, 초], ...]
        import importlib
        from . import posed
        importlib.reload(posed)
        return posed.hand_axis(args.get("body", "M"), args["weapon"], args["frames"])
    if step == "eq_hand_shots":
        # 진단 촬영: 손잡이 근접(칼 면·칼날 옆) — args: body, weapon, poses [posed.POSES 키], sockets [비교할 손 소켓], label
        import importlib
        from . import posed
        importlib.reload(posed)
        return posed.hand_shots(args.get("body", "M"), args["weapon"], args["poses"], args.get("sockets"), args.get("label", "grip"),
                                 focus=args.get("focus", "both"))
    if step in ("eq_grip", "eq_grip_check"):
        # 소켓 쥐는 위치 찾기(posed.grip_search_socket / grip_check_socket) — 레벨에 임시 리그를 놓았다 지움
        import importlib
        from . import posed
        importlib.reload(posed)
        if step == "eq_grip":
            return posed.grip_search_socket(args.get("body", "M"), args["weapon"], args.get("pose", "sword_idle"), args.get("gloves"),
                                            **{k: args[k] for k in ("span", "step", "refine", "tilt", "tilt_step", "touch_cm", "hand_cm") if k in args})
        return posed.grip_check_socket(args.get("body", "M"), args["weapon"], args.get("pose", "sword_idle"), args.get("gloves"), args.get("rel"))
    if step == "eq_headbox":
        return head_boxes()
    if step == "eq_gallery":
        return gallery()
    if step == "eq_gallery_shots":
        return gallery_shots(args.get("label", ""))
    raise ValueError(f"알 수 없는 단계: {step}")
