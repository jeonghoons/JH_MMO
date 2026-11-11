"""에셋 참조 검사 (MC4.2e — 플레이어가 Paragon 애니메이션을 참조하지 않는지).

AssetRegistry의 패키지 의존(하드·소프트)을 루트에서 끝까지 따라가 금지 경로에 닿는 사슬을 찾는다.
예)
  import jm_kit.refs as R
  R.find(["/Game/Character/Mannequin/Anims/ABP_PlayerBase"], ["/Game/Paragon"])
"""
import unreal

AR = unreal.AssetRegistryHelpers.get_asset_registry()

# 플레이어 쪽 루트(2026-11-10): 공용 AnimBP·레이어·몽타주가 모인 폴더 + 플레이어 BP·데이터 테이블
PLAYER_ROOTS = (
    "/Game/Character/Mannequin/Anims",
    "/Game/_Core/Blueprints/Character/BP_MyPlayer",
    "/Game/_Core/Blueprints/Character/BP_Player",
    "/Game/_Core/Blueprints/Test",
    "/Game/Data/DT_SkillTable",
    "/Game/Data/DT_BodyTable",
    "/Game/Data/DT_WeaponTypeTable",
    "/Game/Data/DT_ItemTable",
)
PARAGON_PREFIXES = ("/Game/Paragon", "/Game/Character/Greystone", "/Game/Character/Sparrow")


def _packages(root):
    """폴더면 그 아래 모든 패키지, 아니면 그 패키지 하나."""
    if unreal.EditorAssetLibrary.does_directory_exist(root):
        return sorted({p.split(".")[0] for p in unreal.EditorAssetLibrary.list_assets(root, recursive=True)})
    return [root]


def _deps(pkg):
    opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                                include_searchable_names=False, include_soft_management_references=False,
                                                include_hard_management_references=False)
    return [str(d) for d in (AR.get_dependencies(unreal.Name(pkg), opt) or [])]


def find(roots=PLAYER_ROOTS, forbidden=PARAGON_PREFIXES, skip=()):
    """roots에서 닿는 패키지 중 forbidden 접두사로 시작하는 것과 그 사슬(루트 → … → 금지).
    skip 접두사로 시작하는 패키지는 따라가지 않는다(예: 몬스터·이펙트처럼 계속 쓰기로 한 것)."""
    start = [p for r in roots for p in _packages(r)]
    parent = {p: None for p in start}
    queue = list(start)
    hits = []
    while queue:
        cur = queue.pop()
        for d in _deps(cur):
            if not d.startswith("/Game/") or d in parent:
                continue
            parent[d] = cur
            if any(d.startswith(f) for f in forbidden):
                chain, x = [], d
                while x is not None:
                    chain.append(x)
                    x = parent[x]
                cls = [str(a.asset_class_path.asset_name) for a in AR.get_assets_by_package_name(unreal.Name(d))]
                hits.append({"asset": d, "class": ",".join(cls), "chain": " ← ".join(chain)})
                continue
            if any(d.startswith(s) for s in skip):
                continue
            queue.append(d)
    return {"roots": len(start), "visited": len(parent), "hits": hits}


# ── Paragon 팩 정리(2026-11-12): 남길 에셋이 기대는 팩 에셋을 끝까지 모아 다른 폴더로 Move ──

def _all_packages(root, exclude):
    """root 아래 모든 패키지(exclude 접두사 제외)."""
    pkgs = {str(a.package_name) for a in AR.get_assets_by_path(unreal.Name(root), recursive=True)}
    return sorted(p for p in pkgs if not any(p.startswith(e) for e in exclude))


def plan(roots, targets, deleting=()):
    """roots(폴더·패키지)에서 의존을 끝까지 따라가 targets 접두사 안에서 닿는 패키지 전체(팩 내부 사슬 포함).
    deleting 접두사에 닿으면 막는 참조(blockers)로 보고한다 — 지울 폴더를 남길 에셋이 쓰는 경우.
    roots에 "/Game"을 주면 targets·deleting 바깥 전체가 루트."""
    excl = tuple(targets) + tuple(deleting)
    start = []
    for r in roots:
        start += _all_packages(r, excl) if unreal.EditorAssetLibrary.does_directory_exist(r) else [r]
    seen = set(start)
    queue = list(start)
    moves, blockers, missing = set(), [], set()
    while queue:
        cur = queue.pop()
        for d in _deps(cur):
            if not d.startswith("/Game/"):
                continue
            if any(d.startswith(x) for x in deleting) and not any(cur.startswith(t) for t in targets):
                blockers.append({"from": cur, "to": d})
            if d in seen:
                continue
            seen.add(d)
            if any(d.startswith(t) for t in targets):
                if not AR.get_assets_by_package_name(unreal.Name(d)):
                    missing.add(d)
                    continue
                moves.add(d)
                queue.append(d)
    return {"roots": len(start), "visited": len(seen), "moves": sorted(moves), "blockers": blockers, "missing": sorted(missing)}


def _dest(pkg, mapping):
    for src, dst in mapping:
        if pkg.startswith(src):
            return dst + pkg[len(src):]
    return None


def move(packages, mapping, dry_run=True):
    """packages를 mapping [(원본 접두사, 대상 접두사)]대로 Move(AssetTools.rename_assets — 참조 갱신·리디렉터 생성),
    그다음 원본 쪽 리디렉터를 Fix Up하고 저장한다. dry_run이면 대상 경로·충돌만 보고."""
    plan_rows, conflicts = [], []
    for p in packages:
        dst = _dest(p, mapping)
        if dst is None:
            conflicts.append({"pkg": p, "why": "no mapping"})
            continue
        if AR.get_assets_by_package_name(unreal.Name(dst)):
            conflicts.append({"pkg": p, "why": "exists", "dst": dst})
        plan_rows.append((p, dst))
    if dry_run or conflicts:
        return {"dry_run": dry_run, "count": len(plan_rows), "conflicts": conflicts, "sample": plan_rows[:10]}

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    data, failed_load = [], []
    for p, dst in plan_rows:
        assets = AR.get_assets_by_package_name(unreal.Name(p))
        name = p.rsplit("/", 1)[1]
        obj = None
        for a in assets:
            if str(a.asset_name) == name:
                obj = a.get_asset()
        if obj is None and assets:
            obj = assets[0].get_asset()
        if obj is None:
            failed_load.append(p)
            continue
        data.append(unreal.AssetRenameData(obj, dst.rsplit("/", 1)[0], dst.rsplit("/", 1)[1]))
    ok = tools.rename_assets(data)

    # 원본 접두사 아래 남은 리디렉터 Fix Up(참조자를 새 경로로 다시 저장하고 리디렉터 삭제)
    redirs = []
    for src, _ in mapping:
        root = src.rstrip("/")
        for a in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            if str(a.asset_class_path.asset_name) == "ObjectRedirector":
                r = a.get_asset()
                if r:
                    redirs.append(r)
    fixed = 0
    if redirs:
        try:
            tools.fixup_referencers(redirs, False, unreal.RedirectFixupMode.DELETE_FIXED_UP_REDIRECTORS)
        except TypeError:
            tools.fixup_referencers(redirs)
        fixed = len(redirs)
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    left = [str(a.package_name) for src, _ in mapping
            for a in AR.get_assets_by_path(unreal.Name(src.rstrip("/")), recursive=True)
            if str(a.asset_class_path.asset_name) == "ObjectRedirector"]
    return {"renamed": ok, "count": len(data), "failed_load": failed_load, "redirectors_fixed": fixed,
            "redirectors_left": left, "saved": saved}
