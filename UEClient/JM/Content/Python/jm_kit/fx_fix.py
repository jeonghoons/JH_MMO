"""FX 라이브러리 손질(FX_CATALOG 4단계) — Content/FX의 Cascade 파티클을 조사하고 정해진 편집만 적용한다.

- inspect(): 이미터별 이름·LOD 활성·TypeData·모듈 종류·본/소켓 이름(LocationBoneSocket·SkelVertSurface)·메시·머티리얼·지연.
  시트에서 비어 보인 이미터가 Paragon 본에 묶였는지, 지울 이미터(침·별자리·거품·Water)가 어느 것인지 여기서 확정한다.
- apply(ops): 편집 목록을 순서대로 적용하고 저장한다. 한 에셋이 실패해도 다음으로 가며 결과에 적는다.
    {"asset": "P_Life_Death", "op": "rename_bones", "map": {"Paragon본": "매니퀸본"}}        # LocationBoneSocket·SkelVertSurface 본 이름 교체
    {"asset": ..., "op": "drop_bones", "names": [...]}                                       # 매니퀸에 없는 본 항목만 제거(이미터는 유지)
    {"asset": ..., "op": "delete_emitter", "emitter": "이름"}                                 # 이미터 삭제(Paragon 머티리얼 의존도 함께 끊김)
    {"asset": ..., "op": "disable_emitter", "emitter": "이름"}                                # 모든 LOD에서 끔(되돌리기 쉬움), enable_emitter는 반대
    {"asset": ..., "op": "set_mesh", "emitter": "이름", "mesh": "/Game/.../SM_x"}             # TypeDataMesh 메시 교체
    {"asset": ..., "op": "set_bone_source", "emitter": "이름", "bones": ["spine_03", ...]}    # 본 목록을 통째로 바꿈
  emitter 값은 이미터 이름 또는 "#인덱스". 본 op는 emitter를 비우면 에셋 전체.
- weapon_socket_points(): 무기 아이템의 부착값(garment.static_attachment)으로 칼 밑동·칼끝을 손 본 로컬 좌표로 계산한다
  → AnimTrail용 소켓 FX_Weapon_Bottom/Top은 리더 메시(체형별)에 SkeletalMeshTools로 추가한다(부착 회전이 체형마다 달라 스켈레톤 공용 소켓은 못 씀).

Emitters·LODLevels·Modules는 EditAnywhere가 아니라 Python get_editor_property로 읽을 수 없다(2026-11-12 확인: "protected and cannot be read",
Cascade 클래스는 unreal 모듈에 타입도 없음). 구조 읽기·이미터 삭제·켬/끔·모듈 꺼내기는 C++ UFxEditLibrary(JMEditorTools)가 하고,
메시(TypeDataMesh.mesh)는 여기서 set_editor_property로 고치지만, 본 모듈 프로퍼티는 Python 래퍼가 못 찾아 C++ EditBones로 한다.
- set_trail_notify(): 몽타주에 AnimNotifyState_Trail(FX_Weapon_Bottom→Top)을 달아 TRAIL 계열을 실제 공격 동작으로 확인한다(C++ SetTrailNotify).
편집 뒤 `jm_kit.fx_sheet`로 다시 찍어 확인한다.
"""
import json

import unreal

from . import fx_sheet, garment

EAL = unreal.EditorAssetLibrary
_SKEL_BONES = None
TRAIL_SOCKETS = ("FX_Weapon_Bottom", "FX_Weapon_Top")   # 리더 메시(체형별) hand_r 소켓 — weapon_socket_points로 계산해 SkeletalMeshTools로 추가


def _lib():
    lib = getattr(unreal, "FxEditLibrary", None)
    if lib is None:
        raise RuntimeError("unreal.FxEditLibrary 없음 — JMEditorTools를 다시 빌드해야 한다(에디터 닫고 Build.bat JMEditor Win64 Development)")
    return lib


def _resolve(key):
    """에셋 이름 또는 경로 → /Game/FX/... 패키지 경로."""
    if key.startswith("/"):
        return key
    hits = [p for p in fx_sheet.list_fx() if p.split("/")[-1] == key]
    if not hits:
        raise RuntimeError(f"FX 에셋 없음: {key}")
    return hits[0]


def _cls(obj):
    return obj.get_class().get_name() if obj is not None else None


def _describe(ps):
    d = json.loads(_lib().describe_particle_system(ps))
    if "error" in d:
        raise RuntimeError(d["error"])
    return d


def mannequin_bones():
    """매니퀸(SK_Mannequin) 본 이름 집합 — 본 교체 결과 검증용."""
    global _SKEL_BONES
    if _SKEL_BONES is None:
        from . import profiles, skeleton_check
        _SKEL_BONES = {n for n, _p, _t in skeleton_check.bones(profiles.body("M")["leader"])}
    return _SKEL_BONES


def _flatten_emitter(e, bones_ok):
    """C++ 덤프의 이미터 하나를 LOD0 기준 한 줄 요약으로(LOD가 더 있으면 lods 수와 enabled 목록만)."""
    lods = e.get("lods", [])
    out = {"index": e["index"], "name": e.get("name"), "lods": len(lods),
           "enabled": [l["enabled"] for l in lods]}
    if not lods:
        return out
    l0 = lods[0]
    out["type_data"] = l0.get("type_data") or None
    req = l0.get("required", {})
    out["material"] = req.get("material") or None
    out["delay"] = round(req.get("delay", 0.0), 3)
    out["duration"] = round(req.get("duration", 0.0), 3)
    out["loops"] = req.get("loops")
    out["modules"] = [m["class"] for m in l0["modules"] if m["class"] != l0.get("type_data")]
    disabled = [m["class"] for m in l0["modules"] if not m.get("enabled", True)]
    if disabled:
        out["modules_disabled"] = disabled
    for m in l0["modules"]:
        if "bones" in m:
            out.setdefault("bones", {})[m["class"]] = m["bones"]
            out["bones_missing"] = sorted({n for n in m["bones"] if n not in bones_ok} | set(out.get("bones_missing", [])))
        if "mesh" in m:
            out["mesh"] = m["mesh"] or None
        if "mesh_materials" in m:
            out["mesh_materials"] = [x or None for x in m["mesh_materials"]]
    return out


def inspect(keys=None, category=None):
    """파티클 구조 요약. keys: 이름/경로 목록, category: 분류 하나(둘 다 없으면 전부)."""
    paths = [_resolve(k) for k in keys] if keys else fx_sheet.list_fx(category)
    bones_ok = mannequin_bones()
    out = {}
    for path in paths:
        ps = unreal.load_asset(path)
        if ps is None:
            out[path] = {"error": "로드 실패"}
            continue
        d = _describe(ps)
        out[path] = {"emitters": [_flatten_emitter(e, bones_ok) for e in d["emitters"]],
                     "lod_method": d["lod_method"]}
    return out


def emitter_names(ps):
    return [e.get("name") for e in _describe(ps)["emitters"]]


def _modules(ps, emitter):
    """이미터(이름·"#인덱스")의 모든 LOD 모듈. emitter가 None이면 에셋 전체(중복 제거)."""
    lib = _lib()
    if emitter:
        mods = list(lib.get_emitter_modules(ps, emitter, -1))
        if not mods:
            raise RuntimeError(f"이미터 '{emitter}' 없음 또는 모듈 없음")
        return [(emitter, m) for m in mods]
    out, seen = [], set()
    for name in emitter_names(ps):
        for m in lib.get_emitter_modules(ps, name, -1):
            k = m.get_path_name()
            if k not in seen:
                seen.add(k)
                out.append((name, m))
    return out


def _check(result):
    if isinstance(result, str) and result.startswith("실패"):
        raise RuntimeError(result)
    return result


def _apply_one(ps, op):
    kind = op["op"]
    lib = _lib()
    if kind == "delete_emitter":
        return _check(str(lib.remove_emitter(ps, op["emitter"])))
    if kind == "disable_emitter":
        return _check(str(lib.set_emitter_enabled(ps, op["emitter"], False)))
    if kind == "enable_emitter":
        return _check(str(lib.set_emitter_enabled(ps, op["emitter"], True)))
    if kind in ("rename_bones", "drop_bones", "set_bone_source"):
        # Python 모듈 래퍼는 Cascade 파생 클래스 프로퍼티(source_locations 등)를 못 찾는다(2026-11-12) → C++ EditBones
        em = op.get("emitter") or ""
        if kind == "rename_bones":
            keys = list(op["map"].keys())
            return _check(str(lib.edit_bones(ps, em, "rename", keys, [op["map"][k] for k in keys])))
        if kind == "drop_bones":
            return _check(str(lib.edit_bones(ps, em, "drop", list(op["names"]), [])))
        return _check(str(lib.edit_bones(ps, em, "set", list(op["bones"]), [])))
    if kind == "set_mesh":
        mesh = unreal.load_asset(op["mesh"])
        if mesh is None:
            raise RuntimeError(f"메시 없음 {op['mesh']}")
        n = 0
        for _ename, m in _modules(ps, op["emitter"]):
            if _cls(m) == "ParticleModuleTypeDataMesh":
                m.set_editor_property("mesh", mesh)
                n += 1
        if n == 0:
            raise RuntimeError(f"{op['emitter']}: TypeDataMesh 아님")
        lib.finish_edit(ps)
        return f"메시 교체 {op['emitter']} ×{n}"
    raise RuntimeError(f"모르는 op {kind}")


def apply(ops, save=True):
    """편집 목록 적용(모듈 docstring 참고). 결과 {에셋: [결과 문구...], "_saved": [...], "_errors": [...]}."""
    out, by_asset = {"_saved": [], "_errors": []}, {}
    for op in ops:
        by_asset.setdefault(_resolve(op["asset"]), []).append(op)
    for path, lst in by_asset.items():
        ps = unreal.load_asset(path)
        if ps is None:
            out["_errors"].append(f"{path}: 로드 실패")
            continue
        res = []
        for op in lst:
            try:
                res.append(_apply_one(ps, op))
            except Exception as e:
                res.append(f"실패 {op.get('op')}: {e}")
                out["_errors"].append(f"{path} {op.get('op')}: {e}")
        out[path] = res
        if save:
            if EAL.save_asset(path, only_if_is_dirty=False):
                out["_saved"].append(path)
            else:
                out["_errors"].append(f"{path}: 저장 실패")
    return out


def weapon_socket_points(item_key="Cloth_Sword", body="M", fracs=(0.22, 1.0)):
    """무기 메시의 긴 축에서 fracs 지점(0 = 밑동, 1 = 끝)을 손 본 로컬 좌표로.
    AnimTrail 소켓: FX_Weapon_Bottom = 날 시작(기본 0.22 — 손잡이·가드 위), FX_Weapon_Top = 칼끝(1.0)."""
    at = garment.static_attachment(item_key, body)
    tf = at["transform"]
    st = unreal.load_asset(at["mesh"])
    b = st.get_bounds()
    c = (b.origin.x, b.origin.y, b.origin.z)
    e = (b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2)
    axis = max(range(3), key=lambda i: e[i])
    pts = {}
    for f in fracs:
        p = list(c)
        p[axis] = c[axis] - e[axis] / 2 + f * e[axis]
        v = unreal.MathLibrary.transform_location(tf, unreal.Vector(*p))
        pts[str(f)] = [round(v.x, 3), round(v.y, 3), round(v.z, 3)]
    return {"item": item_key, "body": body, "socket": at["socket"], "mesh": at["mesh"], "axis": "xyz"[axis],
            "length_cm": round(e[axis] * tf.scale3d.x, 2), "points_bone_local": pts}


def set_trail_notify(montage_path, key, start_sec=0.0, duration_sec=0.0, remove=False):
    """몽타주의 Trail 노티파이를 모두 지우고 key 파티클로 하나 단다(remove=True면 지우기만). duration 0이면 몽타주 끝까지."""
    ps = None if remove else unreal.load_asset(_resolve(key))
    if not remove and ps is None:
        raise RuntimeError(f"FX 로드 실패: {key}")
    dur = float(duration_sec) if duration_sec and duration_sec > 0 else 1e6
    return str(_lib().set_trail_notify(montage_path, ps, float(start_sec), dur, unreal.Name(TRAIL_SOCKETS[0]), unreal.Name(TRAIL_SOCKETS[1]), bool(remove)))
