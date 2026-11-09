"""Mixamo → 매니퀸 애니메이션 리타깃(MC4.2b, DECISIONS 2026-11-08 "MC4.2 모션 출처").

흐름:
  1. import_source_mesh(Y Bot FBX, With Skin·T-Pose) → /Game/Character/Mixamo/SKM_YBot (+ 스켈레톤)
  2. import_anims(FBX 목록, "Bow") → /Game/Character/Mixamo/Anims/Bow/MX_<이름> (Y Bot 스켈레톤, 원본 보관)
  3. ensure_rigs() → IK_Mixamo·IK_Mannequin(자동 리타깃 정의 + 자동 FBIK)·RTG_Mixamo_Mannequin(기본 오퍼레이션,
     체인 자동 매핑, 대상 포즈 자동 정렬 — Y Bot T자 ↔ 매니퀸 A자)
  4. retarget(시퀀스 목록, "Bow") → /Game/Character/Mannequin/Anims/Bow/AS_Bow_<이름> (SK_Mannequin)

대상 메시는 SKM_Manny_Simple이다. 결과 애니메이션은 대상 메시의 스켈레톤(/Game/Character/Mannequin/Meshes/SK_Mannequin)
소속이 되는데, ABP_PlayerBase·기존 몽타주가 이 스켈레톤을 쓴다(키트 몸 메시는 CoreC 쪽 SK_Mannequin 복사본이라 대상으로 쓰지 않음).
FBX 원본은 UEClient/JM/SourceArt/Mixamo/(*.fbx는 git 제외, 재배포 금지)에 둔다 — 경로가 길면 가져오기가 조용히 실패한다.
"""
import os
import re

import unreal

EAL = unreal.EditorAssetLibrary

SRC_DIR = "/Game/Character/Mixamo"
SRC_MESH = SRC_DIR + "/SKM_YBot"
IK_SRC = SRC_DIR + "/IK_Mixamo"
TGT_MESH = "/Game/Character/Mannequin/Meshes/SKM_Manny_Simple"
TGT_SKELETON = "/Game/Character/Mannequin/Meshes/SK_Mannequin"
RIG_DIR = "/Game/Character/Mannequin/Rigs"
IK_TGT = RIG_DIR + "/IK_Mannequin"
RTG = RIG_DIR + "/RTG_Mixamo_Mannequin"
OUT_ROOT = "/Game/Character/Mannequin/Anims"
POSE_NAME = "JM_AutoAlign"
ROOT_MOTION_OP = "Root Motion"
PIN_OP = "Pin IK Bones"
PIN_OP_TYPE = "/Script/IKRig.IKRetargetPinBoneOp"
# 매니퀸 IK 본(대상) ← 따라갈 본(대상). Mixamo에는 IK 본이 없어 리타깃하면 참조 포즈에 남는데, ABP_PlayerBase의
# CR_Mannequin_FootIK가 ik_foot_* 위치로 발을 당기므로 다리가 꼬이고 몸이 뜬다(MC4.2c) — 템플릿 애니메이션처럼 발·손을 따르게 한다.
IK_DRIFT_BONES = ["ik_foot_l", "ik_foot_r", "ik_hand_gun"]   # 부모가 root에 고정된 IK 본(ik_hand_l·r은 ik_hand_gun 자식)
PIN_PAIRS = [("root", "ik_foot_root"), ("foot_l", "ik_foot_l"), ("foot_r", "ik_foot_r"),
             ("root", "ik_hand_root"), ("hand_r", "ik_hand_gun"), ("hand_l", "ik_hand_l"), ("hand_r", "ik_hand_r")]

SRC = unreal.RetargetSourceOrTarget.SOURCE
TGT = unreal.RetargetSourceOrTarget.TARGET


def _tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _split(path):
    d, n = path.rsplit("/", 1)
    return d, n


def _camel(stem):
    """'Standing Draw Arrow (1)' → 'StandingDrawArrow1'."""
    words = re.findall(r"[A-Za-z0-9]+", stem)
    return "".join(w[:1].upper() + w[1:] for w in words)


def _import(fbx_path, dest_dir, dest_name, ui):
    if not os.path.isfile(fbx_path):
        raise FileNotFoundError(fbx_path)
    t = unreal.AssetImportTask()
    t.filename = fbx_path
    t.destination_path = dest_dir
    t.destination_name = dest_name
    t.automated = True
    t.save = True
    t.replace_existing = True
    t.options = ui
    _tools().import_asset_tasks([t])
    return [p.split(".")[0] for p in t.imported_object_paths]


# ---------------------------------------------------------------- 가져오기
def import_source_mesh(fbx_path):
    """Mixamo Y Bot(With Skin, T-Pose) FBX → SKM_YBot. 반환: {mesh, skeleton, bones}."""
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_as_skeletal = True
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    o.import_animations = False
    o.import_materials = False
    o.import_textures = False
    o.create_physics_asset = False
    o.automated_import_should_detect_type = False
    d, n = _split(SRC_MESH)
    paths = _import(fbx_path, d, n, o)
    mesh = unreal.load_asset(SRC_MESH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"스켈레탈 메시 가져오기 실패: {fbx_path} → {paths}")
    sk = mesh.get_editor_property("skeleton")
    return {"mesh": SRC_MESH, "skeleton": sk.get_path_name().split(".")[0], "imported": paths}


def import_anims(fbx_paths, pack):
    """애니메이션 FBX(Without Skin) 목록 → /Game/Character/Mixamo/Anims/<pack>/MX_<이름>. 반환: 시퀀스 경로 목록."""
    mesh = unreal.load_asset(SRC_MESH)
    if mesh is None:
        raise RuntimeError(f"{SRC_MESH} 없음 — import_source_mesh 먼저")
    sk = mesh.get_editor_property("skeleton")
    dest = f"{SRC_DIR}/Anims/{pack}"
    out = []
    for f in fbx_paths:
        o = unreal.FbxImportUI()
        o.import_mesh = False
        o.import_as_skeletal = True
        o.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
        o.import_animations = True
        o.import_materials = False
        o.import_textures = False
        o.skeleton = sk
        o.automated_import_should_detect_type = False
        name = "MX_" + _camel(os.path.splitext(os.path.basename(f))[0])
        paths = _import(f, dest, name, o)
        seqs = [p for p in paths if isinstance(unreal.load_asset(p), unreal.AnimSequence)]
        if not seqs:
            raise RuntimeError(f"애니메이션 가져오기 실패: {f} → {paths}")
        out += seqs
    return out


# ---------------------------------------------------------------- IK 리그·리타기터
def _make_ik_rig(path, mesh_path, rebuild):
    rig = unreal.load_asset(path)
    if rig is not None and not rebuild:
        return rig, False
    if rig is None:
        d, n = _split(path)
        rig = _tools().create_asset(n, d, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    if not c.set_skeletal_mesh(unreal.load_asset(mesh_path)):
        raise RuntimeError(f"IK 리그 메시 지정 실패: {path} ← {mesh_path}")
    if not c.apply_auto_generated_retarget_definition():
        raise RuntimeError(f"자동 리타깃 정의 실패(뼈대 형식을 인식 못함): {path}")
    c.apply_auto_fbik()
    EAL.save_loaded_asset(rig, False)
    return rig, True


def _chains(rig):
    c = unreal.IKRigController.get_controller(rig)
    return {"root": str(c.get_retarget_root()),
            # FBoneChain.StartBone·EndBone은 Python에 노출되지 않아(VisibleAnywhere) 컨트롤러 getter로 읽는다
            "chains": [f"{ch.chain_name}:{c.get_retarget_chain_start_bone(ch.chain_name)}->{c.get_retarget_chain_end_bone(ch.chain_name)}"
                       for ch in c.get_retarget_chains()]}


def ensure_rigs(rebuild=False):
    """IK 리그 2개 + 리타기터를 만든다(있으면 그대로, rebuild=True면 다시 구성). 반환: 체인·매핑 요약."""
    src, _ = _make_ik_rig(IK_SRC, SRC_MESH, rebuild)
    tgt, _ = _make_ik_rig(IK_TGT, TGT_MESH, rebuild)
    rtg = unreal.load_asset(RTG)
    made = rtg is None
    if made:
        d, n = _split(RTG)
        rtg = _tools().create_asset(n, d, unreal.IKRetargeter, unreal.IKRetargetFactory())
    rc = unreal.IKRetargeterController.get_controller(rtg)
    if made or rebuild:
        rc.set_ik_rig(SRC, src)
        rc.set_ik_rig(TGT, tgt)
        rc.set_preview_mesh(SRC, unreal.load_asset(SRC_MESH))
        rc.set_preview_mesh(TGT, unreal.load_asset(TGT_MESH))
        rc.remove_all_ops()
        rc.add_default_ops()
        rc.assign_ik_rig_to_all_ops(SRC, src)
        rc.assign_ik_rig_to_all_ops(TGT, tgt)
        rc.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
        # Mixamo에는 손바닥뼈가 없는데 FUZZY가 매니퀸 *Metacarpal 체인을 손가락 체인에 묶는다 → 손바닥이 손가락처럼 굽으므로 해제
        for ch in unreal.IKRigController.get_controller(tgt).get_retarget_chains():
            if "Metacarpal" in str(ch.chain_name):
                rc.set_source_chain("None", ch.chain_name)
        # Y Bot은 T자, 매니퀸은 A자 — 대상 포즈를 원본 체인 방향에 맞춘다
        # 다시 구성할 때 같은 이름이 있으면 CreateRetargetPose가 _1을 붙여 포즈가 쌓이므로 먼저 지운다
        for old in [str(n) for n in rc.get_retarget_poses(TGT).keys()]:
            if old.startswith(POSE_NAME):
                rc.remove_retarget_pose(old, TGT)
        pose = rc.create_retarget_pose(POSE_NAME, TGT)
        rc.set_current_retarget_pose(pose, TGT)
        rc.auto_align_all_bones(TGT)
        EAL.save_loaded_asset(rtg, False)
    # Root Motion 오퍼레이션은 끈다(MC4.2c): Mixamo에는 root 본이 없어 이 오퍼레이션이 Hips(이동·높이 96cm)를 매니퀸 root에
    # 복사한다 → 몸이 떠서 Foot IK가 다리를 접음("공중에 앉음"). 끄면 root는 바닥(참조 포즈)에 남고 이동·높이는 pelvis가 가진다
    # (매니퀸 템플릿 애니메이션과 같은 구조). 위치는 서버 권위라 루트 모션은 쓰지 않는다.
    for i in range(rc.get_num_retarget_ops()):
        if str(rc.get_op_name(i)) == ROOT_MOTION_OP and rc.get_retarget_op_enabled(i):
            rc.set_retarget_op_enabled(i, False)
            EAL.save_loaded_asset(rtg, False)
    _ensure_pin_op(rc, rtg)
    return summary()


def _ensure_pin_op(rc, rtg):
    """Pin Bones 오퍼레이션(PIN_OP)을 스택 맨 아래에 두고 PIN_PAIRS를 맞춘다(있으면 쌍만 갱신)."""
    idx = rc.get_index_of_op_by_name(PIN_OP)
    if idx < 0:
        idx = rc.add_retarget_op(PIN_OP_TYPE)
        if idx < 0:
            raise RuntimeError(f"오퍼레이션 추가 실패: {PIN_OP_TYPE}")
        rc.set_op_name(PIN_OP, idx)
    ctl = rc.get_op_controller(idx)
    if ctl is None or not hasattr(ctl, "set_bone_pair"):
        raise RuntimeError(f"Pin Bones 컨트롤러 없음: {ctl}")
    want = {to: frm for frm, to in PIN_PAIRS}
    have = {str(k): str(v) for k, v in ctl.get_all_bone_pairs().items()}
    if have != want:
        ctl.clear_all_bone_pairs()
        for frm, to in PIN_PAIRS:
            ctl.set_bone_pair(frm, to)
        EAL.save_loaded_asset(rtg, False)


def summary():
    rtg = unreal.load_asset(RTG)
    rc = unreal.IKRetargeterController.get_controller(rtg)
    tgt = unreal.load_asset(IK_TGT)
    mapping = {}
    for ch in unreal.IKRigController.get_controller(tgt).get_retarget_chains():
        mapping[str(ch.chain_name)] = str(rc.get_source_chain(ch.chain_name))
    return {"source": _chains(unreal.load_asset(IK_SRC)), "target": _chains(tgt),
            "ops": [f"{rc.get_op_name(i)}{'' if rc.get_retarget_op_enabled(i) else ' (off)'}" for i in range(rc.get_num_retarget_ops())],
            "pins": {str(k): str(v) for k, v in rc.get_op_controller(rc.get_index_of_op_by_name(PIN_OP)).get_all_bone_pairs().items()}
            if rc.get_index_of_op_by_name(PIN_OP) >= 0 else {},
            "mapping(target<-source)": mapping,
            "target_pose": str(rc.get_current_retarget_pose_name(TGT))}


# ---------------------------------------------------------------- 리타깃
def _out_name(seq_path, pack):
    """MX_<그룹>_<동작>(이름을 정리한 시퀀스, 예 MX_OneHandSword_Idle·MX_Common_Hit) → AS_<그룹>_<동작>,
    MX_<이름>(가져온 그대로, 예 MX_StandingDrawArrow) → AS_<pack>_<이름>."""
    stem = seq_path.rsplit("/", 1)[1]
    if not stem.startswith("MX_"):
        raise RuntimeError(f"MX_ 시퀀스가 아님: {seq_path}")
    rest = stem[3:]
    return "AS_" + rest if "_" in rest else f"AS_{pack}_{rest}"


def retarget(seq_paths, pack, overwrite=True):
    """MX_* 시퀀스를 매니퀸으로 복제·리타깃 → <OUT_ROOT>/<pack>/<_out_name>. 반환: 새 시퀀스 경로 목록.

    같은 경로가 있으면 덮어쓴다(몽타주·블렌드스페이스 참조 유지)."""
    rtg = unreal.load_asset(RTG)
    if rtg is None:
        raise RuntimeError("리타기터 없음 — ensure_rigs 먼저")
    out = []
    for p in seq_paths:   # 배치 연산의 이름 바꾸기는 검색/치환 하나뿐이라 시퀀스마다 따로 돌린다
        ad = EAL.find_asset_data(p)
        if not ad.is_valid():
            raise RuntimeError(f"시퀀스 없음: {p}")
        stem = p.rsplit("/", 1)[1]
        ins = unreal.IKRetargetBatchOperationInputs()
        ins.set_editor_property("assets_to_retarget", [ad])
        ins.set_editor_property("source_mesh", unreal.load_asset(SRC_MESH))
        ins.set_editor_property("target_mesh", unreal.load_asset(TGT_MESH))
        ins.set_editor_property("ik_retarget_asset", rtg)
        ins.set_editor_property("search", stem)
        ins.set_editor_property("replace", _out_name(p, pack))
        ins.set_editor_property("target_path", f"{OUT_ROOT}/{pack}")
        ins.set_editor_property("use_source_path", False)
        ins.set_editor_property("include_referenced_assets", False)
        ins.set_editor_property("overwrite_existing_files", overwrite)
        for made in unreal.IKRetargetBatchOperation.run_batch_retarget(ins):
            path = str(made.package_name)
            a = unreal.load_asset(path)
            sk = a.get_editor_property("skeleton") if isinstance(a, unreal.AnimSequence) else None
            if sk is None or sk.get_path_name().split(".")[0] != TGT_SKELETON:
                raise RuntimeError(f"결과 스켈레톤이 {TGT_SKELETON}이 아님: {path} → {sk}")
            EAL.save_loaded_asset(a, False)
            out.append(path)
    return out


# ---------------------------------------------------------------- 제자리 처리
def strip_linear_drift(seq_path, bone="pelvis"):
    """제자리 처리(Mixamo "In Place"와 같은 방식, DECISIONS 2026-11-09 MC4.2c A): 첫 키 → 마지막 키의 수평 이동 방향을
    구하고, 매 프레임 그 방향 성분을 첫 키 기준으로 전부 뺀다. 좌우(이동 방향에 수직) 흔들림·높이(Z)·회전은 남는다.
    처음엔 일정 속도만큼만 뺐는데(선형), 도약 공격처럼 순간적으로 나가는 동작은 중간에 1.2m 앞으로 나갔다가 미끄러져
    돌아와서 바꿨다. 이동 거리가 거의 0(제자리 동작)이면 아무것도 하지 않는다.

    Mixamo 팩은 이동이 Hips에 들어 있고, 리타기터의 Root Motion 오퍼레이션을 끈 상태(ensure_rigs)에서는 그 이동이
    매니퀸 pelvis에 들어간다(root는 바닥에 고정). 시퀀스에 루트 모션이 켜져 있지 않아 IgnoreRootMotion으로 막히지 않으므로
    pelvis 트랙에서 뺀다. pelvis의 부모 root가 고정이라 pelvis 로컬 = 컴포넌트 공간이다.
    좌우 흔들림·공격 중 내딛기 같은 선형이 아닌 움직임과 높이(Z)·회전은 그대로 둔다.
    반환: {sequence, bone, keys, drift_cm: [x, y], after_cm: [x, y]}.
    """
    seq = unreal.load_asset(seq_path)
    if not isinstance(seq, unreal.AnimSequence):
        raise RuntimeError(f"시퀀스 없음: {seq_path}")
    model = seq.get_editor_property("data_model_interface")
    n = model.get_number_of_keys()
    if n < 2:
        raise RuntimeError(f"키가 부족함: {seq_path} ({n})")
    lib = unreal.AnimationLibrary

    def read(b):
        pos, rot, scl = [], [], []
        for i in range(n):
            t = lib.get_bone_poses_for_frame(seq, [b], i, False)[0]  # 본 로컬(부모 기준)
            pos.append(t.translation)
            rot.append(t.rotation)
            scl.append(t.scale3d)
        return pos, rot, scl

    pos, rot, scl = read(bone)
    dx = pos[-1].x - pos[0].x
    dy = pos[-1].y - pos[0].y
    dist = (dx * dx + dy * dy) ** 0.5
    if dist < 1.0:
        return {"sequence": seq_path, "bone": bone, "also": [], "keys": n, "drift_cm": [round(dx, 1), round(dy, 1)],
                "after_cm": [round(dx, 1), round(dy, 1)], "skipped": "이동 1cm 미만"}
    ux, uy = dx / dist, dy / dist
    # 프레임마다 뺄 양 = 기준 본의 (첫 키 대비 변위)를 이동 방향에 투영한 값
    shift = [((p.x - pos[0].x) * ux + (p.y - pos[0].y) * uy) for p in pos]
    ctrl = seq.get_editor_property("controller")
    # IK 본(Pin IK Bones가 발·손 위치를 복사, 부모 체인이 root에 고정)도 같은 만큼 빼야 발 IK가 발을 앞쪽에 붙잡지 않는다
    extra = [b for b in IK_DRIFT_BONES if bone == "pelvis" and model.is_valid_bone_track_name(b)]
    out = None
    for b in [bone] + extra:
        bp, br, bs = (pos, rot, scl) if b == bone else read(b)
        moved = [unreal.Vector(p.x - ux * shift[i], p.y - uy * shift[i], p.z) for i, p in enumerate(bp)]
        if not ctrl.set_bone_track_keys(b, moved, br, bs, True):
            raise RuntimeError(f"키 쓰기 실패: {seq_path} {b}")
        if b == bone:
            out = moved
    EAL.save_loaded_asset(seq, False)
    return {"sequence": seq_path, "bone": bone, "also": extra, "keys": n, "drift_cm": [round(dx, 1), round(dy, 1)],
            "after_cm": [round(out[-1].x - out[0].x, 1), round(out[-1].y - out[0].y, 1)]}
