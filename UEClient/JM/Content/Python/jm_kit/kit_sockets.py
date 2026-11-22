"""장비 소켓(D-247~D-249) — 남녀 공용 스켈레톤 SK_Mannequin의 무기 소켓을 kit_sockets.json(저장소에 커밋)과 맞춘다.

스켈레톤은 git에서 빠진 Fab 키트 폴더(Content/CoreC)에 있어 소켓을 저장소에 남길 수 없다(D-249) — 기준은 JSON:
  sk_apply   JSON → 스켈레톤(없으면 만들고, 있으면 위치를 JSON 값으로). 새 PC·키트 재설치 뒤에 부른다
  sk_export  스켈레톤 → JSON(사용자가 Persona에서 소켓을 옮긴 뒤 부른다)
  sk_probe   스켈레톤의 소켓 목록(이름·뼈·위치)
부르는 법: WorldTools.world_step(name="", step="sk_<단계>", args_json)
"""
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
SKELETON = "/Game/CoreC/00EngineDefault/Characters/Mannequins/Meshes/SK_Mannequin"
JSON_PATH = os.path.join(os.path.dirname(__file__), "kit_sockets.json")


MESH = "/Game/CoreC/02BaseBody/SKM/SKM_CoreC_M_Base_Body_Combined"   # SK_Mannequin을 쓰는 메시(소켓을 스켈레톤에 넣는 통로)


CARRIER = "/Game/Dev/Sockets/SKM_SocketCarrier"   # 소켓 추가 통로(git 제외 Dev) — add_socket(…, True)는 메시와 스켈레톤 양쪽에 넣어서,
# 남녀 몸 메시에 메시 전용 사본이 생기지 않게 이 복제 메시로 넣는다(2026-11-21 확인)
CARRIER_FROM = "/Game/CoreC/02BaseBody/SKM/SKM_CoreC_M_Base_Eyes"


def _carrier():
    if not EAL.does_asset_exist(CARRIER):
        EAL.duplicate_asset(CARRIER_FROM, CARRIER)
    c = unreal.load_asset(CARRIER)
    if c is None or c.skeleton is None or c.skeleton.get_path_name().split(".")[0] != SKELETON:
        raise RuntimeError(f"운반용 메시 확인 실패: {CARRIER}")
    return c


def _mesh():
    m = unreal.load_asset(MESH)
    if m is None or m.skeleton is None or m.skeleton.get_path_name().split(".")[0] != SKELETON:
        raise RuntimeError(f"메시·스켈레톤 확인 실패: {MESH} → {SKELETON} — Fab CoreCharacters 키트를 설치했는지 확인")
    return m


def _all(m):
    """메시가 보는 소켓 전부(메시 소켓 + 스켈레톤 소켓). 스켈레톤의 Sockets 목록은 Python에서 보호돼 직접 못 읽는다(2026-11-21)."""
    return [m.get_socket_by_index(i) for i in range(m.num_sockets())]


def _row(s):
    loc, rot, sc = s.relative_location, s.relative_rotation, s.relative_scale
    return {"name": str(s.socket_name), "bone": str(s.bone_name),
            "loc": [round(loc.x, 4), round(loc.y, 4), round(loc.z, 4)],
            "rot": [round(rot.roll, 4), round(rot.pitch, 4), round(rot.yaw, 4)],   # roll, pitch, yaw(도)
            "scale": [round(sc.x, 4), round(sc.y, 4), round(sc.z, 4)],
            "on": "skeleton" if s.get_outer() == _mesh().skeleton else "mesh"}


def probe():
    return [_row(s) for s in _all(_mesh())]


def load_json():
    with open(JSON_PATH, encoding="utf-8") as f:
        return json.load(f)


def save_json(data):
    with open(JSON_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(data, ensure_ascii=False, indent=2) + "\n")


def apply(data=None):
    """JSON의 소켓을 스켈레톤에 만든다/맞춘다. 반환: 만든 것·바꾼 것·지금 몸 메시가 보는 소켓."""
    data = data or load_json()
    m = _mesh()
    c = _carrier()
    smes = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    made, set_ = [], []
    for d in data["sockets"]:
        if c.find_socket(d["name"]) is None:
            # socket_name은 Python에서 읽기 전용 — 공식 SkeletalMeshTools.add_socket과 같은 우회(기본 이름으로 넣고 rename_socket)
            s = unreal.new_object(unreal.SkeletalMeshSocket, outer=c)
            s.set_socket_parent(c, d["bone"])
            before = [str(x.socket_name) for x in _all(c)]
            c.modify()
            c.skeleton.modify()
            c.add_socket(s, True)
            tmp = str(s.socket_name)
            if [str(x.socket_name) for x in _all(c)].count(tmp) <= before.count(tmp) or not smes.rename_socket(c, tmp, d["name"]):
                raise RuntimeError(f"소켓 추가 실패: {d['name']}")
            made.append(d["name"])
        else:
            set_.append(d["name"])
        r = d["rot"]
        tf = unreal.Transform(location=unreal.Vector(*d["loc"]), rotation=unreal.Rotator(roll=r[0], pitch=r[1], yaw=r[2]),
                              scale=unreal.Vector(*d.get("scale", [1, 1, 1])))
        for s in [x for x in _all(c) if str(x.socket_name) == d["name"]]:   # 운반용 메시 사본 + 스켈레톤 소켓
            if str(s.bone_name) != d["bone"]:
                s.set_socket_parent(c, d["bone"])
            s.set_socket_local_transform(tf)
    EAL.save_loaded_asset(c.skeleton, False)
    EAL.save_loaded_asset(c, False)
    mine = {str(x.socket_name) for x in _all(m)}
    return {"made": made, "updated": set_, "missing_on_body": [d["name"] for d in data["sockets"] if d["name"] not in mine],
            "body_mesh_sockets": [(r["name"], r["on"]) for r in probe()]}


def export():
    """스켈레톤의 소켓 값(Persona에서 옮긴 것)을 JSON에 쓴다 — JSON에 있는 이름만."""
    data = load_json()
    cur = {r["name"]: r for r in probe()}
    changed = []
    for d in data["sockets"]:
        r = cur.get(d["name"])
        if r is None:
            continue
        if (r["bone"], r["loc"], r["rot"], r["scale"]) != (d["bone"], d["loc"], d["rot"], d.get("scale", [1, 1, 1])):
            changed.append(d["name"])
            d.update({"bone": r["bone"], "loc": r["loc"], "rot": r["rot"], "scale": r["scale"]})
    if changed:
        save_json(data)
    return {"changed": changed, "missing_on_skeleton": [d["name"] for d in data["sockets"] if d["name"] not in cur]}


def test_add(outer_mesh=True, to_skeleton=True):
    """진단: 소켓 하나를 넣어 보고 수를 돌려준다(넣은 것은 이름 None — 확인 뒤 지움)."""
    m = _mesh()
    n0 = m.num_sockets()
    s = unreal.new_object(unreal.SkeletalMeshSocket, outer=(m if outer_mesh else m.skeleton))
    s.set_socket_parent(m, "hand_r")
    m.modify()
    m.skeleton.modify()
    m.add_socket(s, to_skeleton)
    n1 = m.num_sockets()
    names = [str(x.socket_name) for x in _all(m)]
    return {"before": n0, "after": n1, "names": names, "outer_mesh": outer_mesh, "to_skeleton": to_skeleton,
            "sig": str(getattr(m.add_socket, "__doc__", ""))[:400]}


def run(step, args=None):
    args = args or {}
    if step == "sk_api":
        # 진단: 스켈레톤·메시·소켓에서 미리보기(Preview) 관련 Python 이름
        m = _mesh()
        names = lambda o: [n for n in dir(o) if any(k in n.lower() for k in ("preview", "attach"))]
        return {"skeleton": names(m.skeleton), "mesh": names(m), "socket": names(m.find_socket("WeaponSocket_R"))}
    if step == "sk_remove":
        m = _mesh()
        ok = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).remove_socket(m, args["name"])
        return {"removed": ok, "now": [(r["name"], r["on"]) for r in probe()]}
    if step == "sk_test":
        return test_add(args.get("outer_mesh", True), args.get("to_skeleton", True))
    if step == "sk_probe":
        return probe()
    if step == "sk_apply":
        # args.names = 이 이름의 소켓만(다른 소켓의 스켈레톤 값을 JSON으로 덮지 않게)
        data = load_json()
        if args.get("names"):
            data = dict(data, sockets=[d for d in data["sockets"] if d["name"] in args["names"]])
        return apply(data)
    if step == "sk_export":
        return export()
    raise ValueError(f"알 수 없는 단계: {step}")
