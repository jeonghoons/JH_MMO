"""서버 없는 테스트 레벨(/Game/Maps/TestLevel)의 PIE 조작용 블루프린트 (DECISIONS 2026-11-09 "테스트 레벨").

- BP_TestPlayer(부모 C++ ATestPlayer — Source/JM/Player/TestPlayer.h): 실제 게임과 같은 외형 경로(UEquipmentComponent →
  BodyTable·ItemTable·WeaponTypeTable, ABP_PlayerBase), 패킷 없음, 좌클릭/1번 = SkillTable 몽타주 로컬 재생.
  콘솔 `TestEquip <part> <itemId>`로 로컬 장비 교체.
- BP_TestGameMode(부모 GameModeBase): DefaultPawn = BP_TestPlayer, 기본 PlayerController. preview.setup_level()이
  TestLevel의 GameMode Override로 지정한다.

예)
  import importlib, jm_kit.testbed as tb; importlib.reload(tb)
  tb.ensure_blueprints(); tb.set_override("/Game/Character/Mannequin/Anims/Bow/AM_Bow_Attack"); tb.set_override(None)
"""
import unreal

EAL = unreal.EditorAssetLibrary
DIR = "/Game/_Core/Blueprints/Test"
PLAYER_BP = DIR + "/BP_TestPlayer"
# 메시 오프셋(Z −90·Yaw −90 등)은 실제 플레이어 BP에서 복사한다 — C++ 부모에서 바로 만든 BP는 오프셋이 없어
# 메시가 90° 돌아가고 캡슐 중심에 떠 있었다(MC4.2c에서 발견: "오른쪽을 봄·공중에 뜸")
MESH_SOURCE_BP = "/Game/_Core/Blueprints/Character/BP_MyPlayer"
GAME_MODE_BP = DIR + "/BP_TestGameMode"
_IN = "/Game/Input/"
INPUTS = {"default_mapping_context": _IN + "IMC_Default", "move_action": _IN + "Actions/IA_Move",
          "look_action": _IN + "Actions/IA_Look", "mouse_look_action": _IN + "Actions/IA_MouseLook",
          "attack_action": _IN + "Actions/IA_Attack", "skill1_action": _IN + "Actions/IA_Skill1"}
DEFAULT_ITEMS = [1101, 2101, 3101, 4101, 5101, 6102]   # 천 세트 + 천 활(ItemTable)
TEST_MAX_WALK_SPEED = 500.0   # 서버 스탯 PlayerStatTable 1001 MoveSpeed와 같게(UE에는 이 테이블이 없음, 테스트 전용)


def _make_bp(path, parent):
    bp = unreal.load_asset(path)
    if bp is not None:
        return bp, False
    d, n = path.rsplit("/", 1)
    f = unreal.BlueprintFactory()
    f.set_editor_property("parent_class", parent)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(n, d, unreal.Blueprint, f)
    if bp is None:
        raise RuntimeError(f"블루프린트 생성 실패: {path}")
    return bp, True


def _cdo(bp):
    return unreal.get_default_object(bp.generated_class())


def _save(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_loaded_asset(bp, False)


def _mesh_tf(cdo):
    m = cdo.get_editor_property("mesh")
    return m.get_editor_property("relative_location"), m.get_editor_property("relative_rotation")


def _sync_mesh_offset(pbp):
    """BP_TestPlayer 메시의 상대 위치·회전을 MESH_SOURCE_BP와 같게(다르면 고치고 저장). 반환: 고쳤는지."""
    src = unreal.load_asset(MESH_SOURCE_BP)
    if src is None:
        raise RuntimeError(f"메시 오프셋 원본 없음: {MESH_SOURCE_BP}")
    loc, rot = _mesh_tf(_cdo(src))
    cdo = _cdo(pbp)
    cur_loc, cur_rot = _mesh_tf(cdo)
    if (cur_loc - loc).length() < 0.01 and all(abs(a - b) < 0.01 for a, b in ((cur_rot.pitch, rot.pitch), (cur_rot.yaw, rot.yaw), (cur_rot.roll, rot.roll))):
        return False
    m = cdo.get_editor_property("mesh")
    m.set_editor_property("relative_location", loc)
    m.set_editor_property("relative_rotation", rot)
    _save(pbp)
    return True


def ensure_blueprints(reset=False):
    """BP_TestPlayer·BP_TestGameMode를 만든다(있으면 그대로, reset=True면 기본값을 다시 넣는다). 메시 오프셋은 항상
    BP_MyPlayer와 맞춘다. 반환: 경로·기본값 요약."""
    pbp, made_p = _make_bp(PLAYER_BP, unreal.TestPlayer)
    _sync_mesh_offset(pbp)
    if made_p or reset:
        cdo = _cdo(pbp)
        for prop, path in INPUTS.items():
            a = unreal.load_asset(path)
            if a is None:
                raise RuntimeError(f"입력 에셋 없음: {path}")
            cdo.set_editor_property(prop, a)
        cdo.set_editor_property("test_player_type", 1)
        cdo.set_editor_property("test_items", DEFAULT_ITEMS)
        cdo.get_editor_property("character_movement").set_editor_property("max_walk_speed", TEST_MAX_WALK_SPEED)
        _save(pbp)

    gbp, made_g = _make_bp(GAME_MODE_BP, unreal.GameModeBase)
    if made_g or reset:
        g = _cdo(gbp)
        g.set_editor_property("default_pawn_class", pbp.generated_class())
        g.set_editor_property("player_controller_class", unreal.PlayerController)
        _save(gbp)
    return summary()


def _player_cdo():
    bp = unreal.load_asset(PLAYER_BP)
    if bp is None:
        raise RuntimeError(f"{PLAYER_BP} 없음 — ensure_blueprints 먼저")
    return bp, _cdo(bp)


def set_override(montage_path):
    """좌클릭(슬롯 0)에 SkillTable 대신 재생할 몽타주. None/빈 문자열이면 해제(SkillTable 사용)."""
    bp, cdo = _player_cdo()
    m = None
    if montage_path:
        m = unreal.load_asset(montage_path)
        if not isinstance(m, unreal.AnimMontage):
            raise RuntimeError(f"몽타주가 아님: {montage_path}")
    cdo.set_editor_property("attack_montage_override", m)
    _save(bp)
    return summary()


def set_loadout(items, player_type=None):
    """시작 장비(6칸, EquipPart 순서, 빈 슬롯 0)와 체형(1 MALE, 2 FEMALE)."""
    items = [int(i) for i in items]
    if len(items) != 6:
        raise ValueError(f"장비는 6칸: {items}")
    bp, cdo = _player_cdo()
    cdo.set_editor_property("test_items", items)
    if player_type is not None:
        cdo.set_editor_property("test_player_type", int(player_type))
    _save(bp)
    return summary()


def summary():
    out = {}
    p = unreal.load_asset(PLAYER_BP)
    if p is not None:
        c = _cdo(p)
        m = c.get_editor_property("attack_montage_override")
        out["player"] = {"path": PLAYER_BP, "player_type": c.get_editor_property("test_player_type"),
                         "items": list(c.get_editor_property("test_items")),
                         "override": m.get_path_name().split(".")[0] if m else None,
                         "max_walk_speed": c.get_editor_property("character_movement").get_editor_property("max_walk_speed"),
                         "inputs_ok": all(c.get_editor_property(k) is not None for k in INPUTS),
                         "mesh_offset": [str(v) for v in _mesh_tf(c)]}
        src = unreal.load_asset(MESH_SOURCE_BP)
        if src is not None:
            out["mesh_offset_source"] = [str(v) for v in _mesh_tf(_cdo(src))]
    g = unreal.load_asset(GAME_MODE_BP)
    if g is not None:
        pawn = _cdo(g).get_editor_property("default_pawn_class")
        out["game_mode"] = {"path": GAME_MODE_BP, "default_pawn": pawn.get_name() if pawn else None}
    return out
