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
