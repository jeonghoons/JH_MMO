"""무기·방패 메시 정규화 + 소켓 초기값(D-247, 2026-11-21) — 현업 방식: 메시를 "손잡이 = 원점, 실제 크기, 같은 축"으로 굽고
스켈레톤 소켓에 위치값 없이(아이템 표 AttachM/F = 단위 변환) 붙인다. Persona에서 소켓에 미리보기 에셋을 달면 게임과 같은 모습.

정규화 규칙(메시 로컬 cm):
  검(attach fit "length", 손 hand_r): 손잡이(grip_frac·grip_local) → 원점, 긴 축(칼끝 쪽) → +Z, 가장 얇은 축 → +X(칼 면의 법선)
  활(fit "length", 손 hand_l): 손잡이 → 원점, 긴 축 → +Z, 원본 +X(시위 쪽) → +X
  방패(fit "shield"): 뒷면 손잡이 점 → 원점, 앞면 → +X, 위 → +Z
  크기: length_cm(방패는 위 축 길이)로 맞춤 — VARCO 메시는 긴 변 1m로 와서 그대로면 크기가 틀림
소켓 초기값 = (사용자가 맞춘 남성 위치) ∘ (정규화)⁻¹ — 바꾼 뒤에도 남성 화면이 맞춘 그대로.

부르는 법: WorldTools.world_step(name="", step="wn_<단계>", args_json)
  wn_plan            아이템별 정규화(크기·원점·축)와 소켓 초기값 미리보기(아무것도 바꾸지 않음)
  wn_apply {keys}    kit_sockets.json 작성(소켓 값이 비어 있을 때만) → 메시 굽기 → items.json attach/back을 소켓 방식으로
새 무기(IT2): items.json에 예전처럼 attach(fit length/shield)를 적고 garment_import_source 뒤 wn_apply {keys: [새 키]}.
"""
import json
import os

import unreal

from . import garment, kit_sockets

EAL = unreal.EditorAssetLibrary
A = unreal.GeometryScript_AssetUtils
T = unreal.GeometryScript_MeshTransforms
PY_DIR = os.path.dirname(__file__)

SOCKET = {("weapon", "hand_r"): "WeaponSocket_R", ("weapon", "hand_l"): "WeaponSocket_L", ("offhand", "hand_l"): "ShieldSocket_L"}
BACK = {"hand_r": "BackSocket_Sword", "hand_l": "BackSocket_Bow"}
# 소켓 초기값을 가져올 아이템(사용자가 맞춤 레벨에서 맞춘 남성 값 — D-243)
SEED = {"WeaponSocket_R": ("00_OneHandSword", "attach", "hand_r"), "WeaponSocket_L": ("00_Bow", "attach", "hand_l"),
        "ShieldSocket_L": ("00_Shield", "attach", "hand_l"), "BackSocket_Sword": ("00_OneHandSword", "back", "spine_05"),
        "BackSocket_Bow": ("00_Bow", "back", "spine_05")}
EXTRA = {"BackSocket_TwoHand": "BackSocket_Sword"}   # 양손검(D-240)은 아직 메시가 없어 검 등 소켓 값으로 시작


def _raw():
    with open(os.path.join(PY_DIR, "items.json"), encoding="utf-8") as f:
        return json.load(f)


def _targets(keys=None):
    out = []
    for k, it in garment.load_items().items():
        at = it.get("attach", {})
        if (it["slot"], at.get("socket")) in SOCKET and at.get("fit") in ("length", "shield") and (not keys or k in keys):
            out.append(k)
    return out


def norm_transform(key):
    """원본 메시 → 정규화 메시 변환 N(v_norm = N(v_raw))과 설명."""
    it = garment.load_items()[key]
    at = it["attach"]
    b = unreal.load_asset(it["source"]).get_bounds()
    raw_c = (b.origin.x, b.origin.y, b.origin.z)
    raw_e = (b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2)
    unit = lambda i, sg: unreal.Vector(*[sg if j == i else 0.0 for j in range(3)])
    if at["fit"] == "shield":
        (fi, fs), (ui, us), s, origin = garment.shield_frame(at, raw_c, raw_e)
        xv, zv = unit(fi, fs), unit(ui, us)
    else:
        a = max(range(3), key=lambda i: raw_e[i])
        s = at["length_cm"] / raw_e[a]
        origin = list(raw_c)
        origin[a] = raw_c[a] - raw_e[a] / 2 + at.get("grip_frac", 0.1) * raw_e[a]
        for k, v in (at.get("grip_local") or {}).items():
            origin["xyz".index(k)] = float(v)
        if at["socket"] == "hand_l":   # 활: 원본 +X가 시위 쪽(MC4.2d) — 그대로 +X
            if a == 0:
                raise RuntimeError(f"{key}: 활의 긴 축이 X라 시위 방향을 정할 수 없음")
            xi = 0
        else:
            xi = min((i for i in range(3) if i != a), key=lambda i: raw_e[i])
        xv, zv = unit(xi, 1.0), unit(a, 1.0)
    m = unreal.MathLibrary.make_rot_from_xz(xv, zv)   # 로컬 X·Z = 원본의 그 축
    qinv = unreal.MathLibrary.invert_transform(unreal.Transform(rotation=m)).rotation   # Python Quat에는 inverse가 없음
    o = qinv.rotate_vector(unreal.Vector(*origin))
    n = unreal.Transform(location=unreal.Vector(-o.x * s, -o.y * s, -o.z * s), rotation=qinv.rotator(), scale=unreal.Vector(s, s, s))
    return n, {"scale": round(s, 5), "origin_raw": [round(v, 3) for v in origin], "x_raw": [xv.x, xv.y, xv.z], "z_raw": [zv.x, zv.y, zv.z]}


def _tf_row(name, bone, tf):
    r = tf.rotation.rotator()
    return {"name": name, "bone": bone, "loc": [round(tf.translation.x, 4), round(tf.translation.y, 4), round(tf.translation.z, 4)],
            "rot": [round(r.roll, 4), round(r.pitch, 4), round(r.yaw, 4)], "scale": [1.0, 1.0, 1.0],
            "_scale_check": [round(tf.scale3d.x, 4), round(tf.scale3d.y, 4), round(tf.scale3d.z, 4)]}


def seed_sockets():
    """SEED 아이템의 남성 위치(수동값 우선)와 정규화로 소켓 초기값을 계산한다. 소켓 = N⁻¹ 다음 Rel."""
    rows = []
    for name, (key, which, bone) in SEED.items():
        rel = garment.static_attachment(key, "M", which)
        if rel["socket"] != bone:
            raise RuntimeError(f"{key}.{which}: 소켓 {rel['socket']} ≠ {bone}")
        n, _ = norm_transform(key)
        sock = unreal.MathLibrary.compose_transforms(unreal.MathLibrary.invert_transform(n), rel["transform"])
        rows.append(_tf_row(name, bone, sock) | {"_from": f"{key} {which} M{' (수동)' if rel.get('manual') else ''}"})
    for name, src in EXTRA.items():
        base = next(r for r in rows if r["name"] == src)
        rows.append(dict(base, name=name, _from=f"{src} 복사"))
    return rows


def plan(keys=None):
    out = {}
    for k in _targets(keys):
        _n, info = norm_transform(k)
        out[k] = info
    return {"items": out, "sockets": seed_sockets()}


def _bake(key, n):
    it = garment.load_items()[key]
    path = it["source"]
    st = unreal.load_asset(path)
    dm = garment._dm_static(path)
    dm = T.transform_mesh(dm, n)
    opt = unreal.GeometryScriptCopyMeshToAssetOptions()
    opt.set_editor_property("replace_materials", False)
    _dm, outcome = A.copy_mesh_to_static_mesh(dm, st, opt, unreal.GeometryScriptMeshWriteLOD())
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"메시 쓰기 실패: {path}")
    tt = it["attach"].get("target_tris")
    if tt:
        garment.reduce_static_lods(path, tt)
    EAL.save_loaded_asset(st, False)
    b = st.get_bounds()
    return {"size_cm": [round(b.box_extent.x * 2, 2), round(b.box_extent.y * 2, 2), round(b.box_extent.z * 2, 2)],
            "center_cm": [round(b.origin.x, 2), round(b.origin.y, 2), round(b.origin.z, 2)]}


def apply(keys=None):
    garment._not_playing()
    data = kit_sockets.load_json() if os.path.exists(kit_sockets.JSON_PATH) else {}
    wrote_sockets = False
    if not data.get("sockets"):   # 소켓 값은 처음 한 번만 씨앗으로 — 그 뒤는 Persona에서 옮기고 sk_export
        data = {"_comment": "장비 소켓(D-247~D-249). 기준은 이 파일 — 스켈레톤은 git 제외 키트(CoreC)라 sk_apply로 적용, "
                            "Persona에서 옮긴 뒤 sk_export로 이 파일에 반영. rot = [roll, pitch, yaw]",
                "skeleton": kit_sockets.SKELETON,
                "sockets": [{k: v for k, v in r.items() if not k.startswith("_")} | {"note": r["_from"]} for r in seed_sockets()]}
        kit_sockets.save_json(data)
        wrote_sockets = True
    raw = _raw()
    done = {}
    for k in _targets(keys):
        n, info = norm_transform(k)
        res = _bake(k, n)
        it = raw[k]
        old_at = it["attach"]
        it["norm"] = {"date": "2026-11-21", **info, "attach_before": old_at}
        it["attach"] = {"socket": SOCKET[(it["slot"], old_at["socket"])], "fit": "socket",
                        **({"target_tris": old_at["target_tris"]} if "target_tris" in old_at else {})}
        if "back" in it:
            it["norm"]["back_before"] = it["back"]
            it["back"] = {"socket": BACK[old_at["socket"]], "fit": "socket"}
        it.pop("manual", None)   # 맞춤 레벨 수동값은 소켓 초기값으로 옮겨짐
        done[k] = res
    with open(os.path.join(PY_DIR, "items.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(raw, ensure_ascii=False, indent=2) + "\n")
    return {"sockets_written": wrote_sockets, "baked": done}


def info(keys=None):
    """진단: 메시별 LOD·삼각형·크기·머티리얼."""
    out = {}
    for k in _targets(keys) if not keys else keys:
        st = unreal.load_asset(garment.load_items()[k]["source"])
        b = st.get_bounds()
        out[k] = {"lods": st.get_num_lods(), "tris": [st.get_num_triangles(i) for i in range(st.get_num_lods())],
                  "size": [round(b.box_extent.x * 2, 1), round(b.box_extent.y * 2, 1), round(b.box_extent.z * 2, 1)],
                  "mats": [str(m.material_interface.get_name()) if m.material_interface else None for m in st.static_materials],
                  "sections": st.get_num_sections(0)}
    return out


def run(step, args=None):
    args = args or {}
    if step == "wn_info":
        return info(args.get("keys"))
    if step == "wn_plan":
        return plan(args.get("keys"))
    if step == "wn_apply":
        return apply(args.get("keys"))
    raise ValueError(f"알 수 없는 단계: {step}")
