"""자세별 측정·촬영 (MC4.2e — 장갑 손가락 맞춤, 팔을 든 자세의 상의 어깨·소매 뭉침).

garment.measure는 기준 자세(T자)만 본다. 여기서는 동작의 특정 프레임으로 자세를 잡은 메시를 뽑아 잰다.
- 자세: C++ unreal.AnimAssetBuilder.pose_component_at_time(컴포넌트, 시퀀스, 초) — 에디터 월드는 틱하지 않아 Python만으로는 평가할 수 없다.
- 리그: 컴포넌트마다 같은 시퀀스·시각으로 따로 자세를 잡는다(Leader Pose 팔로워는 CPU 스키닝 결과가 리더를 따르는지 보장이 없어서).
  무기(정적)는 리더 소켓에 붙이고 상대 변환은 garment.static_attachment(게임과 같은 값)에서.
- 메시 추출: GeometryScript_SceneUtils.copy_mesh_from_component(스킨드 컴포넌트 = 현재 자세의 CPU 스키닝 정점), 리더 컴포넌트 공간.

지표
- 장갑·무기: 장갑 정점이 무기 안에 든 비율·최대 깊이, 무기 정점이 장갑 안에 든 비율·최대 깊이, 같은 값을 맨손(장갑이 덮는 몸 정점)으로도.
- 상의 어깨: 쇄골·윗팔 관절 반경 안 삼각형의 넓이 비(자세/기준) p5·p95, 0.6 미만(뭉침)·1.6 초과(늘어남) 비율,
  어깨 영역 정점이 몸 안으로 들어간 비율. 키트 기본 상의(Techwear, 아티스트 제작)를 같은 자세로 재서 기준으로 삼는다.

예)
  import importlib, jm_kit.posed as P; importlib.reload(P)
  P.run("M", {"top": "Cloth_Top", "gloves": "Cloth_Gloves", "bow": "Cloth_Bow", "sword": "Cloth_Sword"}, ["bow_draw", "sword_skill_up"])
"""
import math
import os

import unreal

from . import body_mask, garment, preview, profiles

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
Q = unreal.GeometryScript_MeshQueries
L = unreal.GeometryScript_List
SP = unreal.GeometryScript_MeshSpatial
T = unreal.GeometryScript_MeshTransforms
SU = unreal.GeometryScript_SceneUtils
RTS_COMPONENT = unreal.RelativeTransformSpace.RTS_COMPONENT

_A = "/Game/Character/Mannequin/Anims/"
# 측정 프레임: 손·팔이 가장 높이 오르는 시점(anim_measure_bone_reach로 고름, 2026-11-10)과 무기 쥔 대기
POSES = {
    "bow_idle":       (_A + "Bow/AS_Bow_Idle", 0.0, "bow"),
    "bow_draw":       (_A + "Bow/AS_Bow_Attack", 1.0, "bow"),            # 당김 끝 — 양팔 어깨 높이
    "bow_aim":        (_A + "Bow/AS_Bow_AimIdle", 1.9, "bow"),            # 조준 유지(overdraw)
    "sword_idle":     (_A + "OneHandSword/AS_OneHandSword_Idle", 0.0, "sword"),
    "sword_windup":   (_A + "OneHandSword/AS_OneHandSword_Attack", 0.395, "sword"),
    "sword_swing":    (_A + "OneHandSword/AS_OneHandSword_Attack", 0.632, "sword"),
    "sword_skill_up": (_A + "OneHandSword/AS_OneHandSword_Skill1", 0.875, "sword"),   # 도약 내려찍기 — 팔을 머리 위로
}
SHOULDER_JOINTS = ("clavicle_l", "clavicle_r", "upperarm_l", "upperarm_r")
SHOULDER_RADIUS = 18.0   # cm, 기준 자세에서 관절 중심으로부터
SQUASH, STRETCH = 0.6, 1.6
SHOT_DIR = preview.SHOT_DIR

_RIGS = {}   # prefix → {"lead", "skel": {이름: 컴포넌트}, "weapon": (StaticMeshComponent, 무기 정보) | None, "bind": {이름: DynamicMesh}}


# ---------------------------------------------------------------- 메시
def _copy(comp):
    """컴포넌트의 현재 메시(스킨드 = 현재 자세)를 리더 컴포넌트 공간으로. 리그 액터는 회전·배율 없이 놓으므로 위치만 뺀다."""
    dm = unreal.DynamicMesh()
    opts = unreal.GeometryScriptCopyMeshFromComponentOptions()
    opts.set_editor_property("want_tangents", False)
    res = SU.copy_mesh_from_component(comp, dm, opts, True)
    dm = res[0] if isinstance(res, tuple) else res
    return dm


def _to_local(dm, lead):
    loc = lead.get_actor_location()
    T.translate_mesh(dm, unreal.Vector(-loc.x, -loc.y, -loc.z))
    return dm


def _verts(dm):
    dm, ids, _ = Q.get_all_vertex_i_ds(dm)
    out = {}
    for v in L.convert_index_list_to_array(ids):
        p, _ = Q.get_vertex_position(dm, v)
        out[v] = (p.x, p.y, p.z)
    return out


def _tris(dm):
    dm, ids, _ = Q.get_all_triangle_i_ds(dm)
    out = {}
    for t in L.convert_index_list_to_array(ids):
        out[t] = _tri_idx(dm, t)
    return out


def _tri_idx(dm, t):
    r = Q.get_triangle_indices(dm, t)
    tri = r[0] if isinstance(r, tuple) else r
    return tri.x, tri.y, tri.z   # IntVector


def _area(p, q, r):
    ux, uy, uz = q[0] - p[0], q[1] - p[1], q[2] - p[2]
    vx, vy, vz = r[0] - p[0], r[1] - p[1], r[2] - p[2]
    cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    return 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)


def _d(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def _pct(values, q):
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, int(q * len(s)))]


def _inside(points, dm):
    """points 중 dm 안에 든 것: (개수, [(점, 표면까지 거리)])."""
    bvh = SP.build_bvh_for_mesh(dm)
    bvh = bvh[1] if isinstance(bvh, tuple) else bvh   # (메시, BVH)
    opt = unreal.GeometryScriptSpatialQueryOptions()
    hits = []
    for p in points:
        v = unreal.Vector(*p)
        _m, ins, _o = SP.is_point_inside_mesh(dm, bvh, v, opt)
        if ins:
            _m, hit, _o = SP.find_nearest_point_on_mesh(dm, bvh, v, opt)
            hits.append((p, _d(p, (hit.position.x, hit.position.y, hit.position.z))))
    return hits


def _near(points, dm, max_cm):
    """points 중 dm 표면에서 max_cm 안에 있는 것."""
    bvh = SP.build_bvh_for_mesh(dm)
    bvh = bvh[1] if isinstance(bvh, tuple) else bvh   # (메시, BVH)
    opt = unreal.GeometryScriptSpatialQueryOptions()
    out = []
    for p in points:
        _m, hit, _o = SP.find_nearest_point_on_mesh(dm, bvh, unreal.Vector(*p), opt)
        if _d(p, (hit.position.x, hit.position.y, hit.position.z)) <= max_cm:
            out.append(p)
    return out


# ---------------------------------------------------------------- 리그
def _mesh_of(slot, value, body):
    """loadout 값 → 스켈레탈 메시 경로("/"로 시작하면 그대로, 아니면 items.json 키)."""
    if value.startswith("/"):
        return value
    return garment.load_items()[value]["out"].format(body=body)


def _skel(prefix, name, mesh, loc):
    a = EAS.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(*loc))
    a.set_actor_label(f"{prefix}_{name}")
    c = a.skeletal_mesh_component
    c.set_skeletal_mesh_asset(unreal.load_asset(mesh))
    c.set_editor_property("visibility_based_anim_tick_option", unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    # 에디터 월드에서는 이 값이 꺼져 있으면 본 갱신을 건너뛴다(ShouldUpdateTransform) — 정지 재생(속도 0)이라 켜 둬도 자세는 그대로
    c.set_update_animation_in_editor(True)
    return a


def clear(prefix):
    preview.clear(prefix)
    _RIGS.pop(prefix, None)


def rig(prefix, body, loadout, weapon_key=None, loc=(0.0, 0.0, 0.0)):
    """측정용 캐릭터: 몸(마스크 적용) + 눈·이·헤어 + loadout 스켈레탈 장비(top·gloves·bottom·boots) + 무기(weapon_key, 정적).
    loadout: {슬롯: 아이템 키 또는 메시 경로}. 슬롯 이름이 bow·sword인 값은 무기 후보라 여기서는 weapon_key로만 쓴다."""
    clear(prefix)
    bp = profiles.body(body)
    lead = _skel(prefix, "Leader", bp["leader"], loc)
    items = {s: _mesh_of(s, v, body) for s, v in loadout.items() if s not in ("bow", "sword", "shield")}
    mask = profiles.mask_params(items)
    if mask:
        body_mask.apply(lead.skeletal_mesh_component, body, mask)
    skel = {"body": lead.skeletal_mesh_component}
    for part, mesh in bp["parts"].items():
        # 눈·이·헤어는 측정하지 않으므로 게임과 같이 Leader Pose로 붙인다(헤어는 자기 뼈대가 있어 같은 시퀀스를 넣으면 사라짐)
        pc = _skel(prefix, part, mesh, loc).skeletal_mesh_component
        pc.set_leader_pose_component(lead.skeletal_mesh_component)
        pc.set_editor_property("use_bounds_from_leader_pose_component", True)
        # 에디터 월드에서는 팔로워가 리더의 새 자세를 따라가지 않아(팔로워 틱 없음) 기준 자세에 떠 있다 — 촬영에서 숨김
        pc.set_visibility(False)
    for slot, mesh in items.items():
        skel[slot] = _skel(prefix, f"item_{slot}", mesh, loc).skeletal_mesh_component
    weapon = None
    if weapon_key:
        s = garment.static_attachment(weapon_key, body)
        a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc))
        a.set_actor_label(f"{prefix}_item_weapon")
        sc = a.static_mesh_component
        sc.set_static_mesh(unreal.load_asset(s["mesh"]))
        sc.set_mobility(unreal.ComponentMobility.MOVABLE)
        a.attach_to_component(lead.skeletal_mesh_component, s["socket"], unreal.AttachmentRule.SNAP_TO_TARGET,
                              unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET, False)
        sc.set_relative_transform(s["transform"], False, True)
        weapon = (sc, s)
    # 보조손(방패, MC4.2f): 검 리그에만 붙인다(서버가 검이 아니면 방패를 비운다)
    offhand = None
    if loadout.get("shield") and weapon_key and weapon_key == loadout.get("sword"):
        s = garment.static_attachment(loadout["shield"], body)
        a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc))
        a.set_actor_label(f"{prefix}_item_offhand")
        sc = a.static_mesh_component
        sc.set_static_mesh(unreal.load_asset(s["mesh"]))
        sc.set_mobility(unreal.ComponentMobility.MOVABLE)
        a.attach_to_component(lead.skeletal_mesh_component, s["socket"], unreal.AttachmentRule.SNAP_TO_TARGET,
                              unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET, False)
        sc.set_relative_transform(s["transform"], False, True)
        offhand = (sc, s)
    rec = {"lead": lead, "body": body, "skel": skel, "weapon": weapon, "offhand": offhand, "items": items, "weapon_key": weapon_key}
    _pose_all(rec, "", 0.0)
    rec["bind"] = {k: _to_local(_copy(c), lead) for k, c in skel.items() if k in ("body", "top", "gloves")}
    _RIGS[prefix] = rec
    return rec


def _pose_all(rec, seq, t):
    msgs = []
    anim = unreal.load_asset(seq) if seq else None
    for c in rec["skel"].values():
        # 이미 단일 노드 모드면 OverrideAnimationData가 애니메이션을 다시 넣지 않으므로 먼저 직접 넣는다
        c.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        c.set_animation(anim)
        c.set_play_rate(0.0)
        r = unreal.AnimAssetBuilder.pose_component_at_time(c, seq, float(t))
        if r.startswith("ERROR"):
            raise RuntimeError(r)
        msgs.append(r)
    for k in ("weapon", "offhand"):
        if rec.get(k):
            sc, s = rec[k]
            sc.set_relative_transform(s["transform"], False, True)   # 소켓의 새 위치를 따라가게
    return msgs


def pose(prefix, pose_key):
    seq, t, _w = POSES[pose_key]
    return _pose_all(_RIGS[prefix], seq, t)


# ---------------------------------------------------------------- 측정
def _weapon_local(rec):
    """무기 정적 메시를 리더 컴포넌트 공간으로(소켓 컴포넌트 공간 변환 ∘ 상대 변환)."""
    sc, s = rec["weapon"]
    dm = garment._dm_static(s["mesh"])
    lc = rec["skel"]["body"]
    sock = lc.get_socket_transform(s["socket"], RTS_COMPONENT)
    tf = unreal.MathLibrary.compose_transforms(s["transform"], sock)
    T.transform_mesh(dm, tf)
    return dm


def measure_hands(rec):
    """장갑(없으면 맨손)과 무기의 관통. 맨손 기준값도 함께(장갑이 덮는 몸 정점 = 장갑 표면 3cm 안)."""
    if not rec["weapon"]:
        return {}
    lead = rec["lead"]
    wdm = _weapon_local(rec)
    body_dm = _to_local(_copy(rec["skel"]["body"]), lead)
    out = {}
    if "gloves" in rec["skel"]:
        gdm = _to_local(_copy(rec["skel"]["gloves"]), lead)
        gv = list(_verts(gdm).values())
        g_in = _inside(gv, wdm)
        w_side = rec["weapon"][1]["socket"]          # hand_r / hand_l
        hand = rec["skel"]["body"].get_socket_transform(w_side, RTS_COMPONENT).translation
        hp = (hand.x, hand.y, hand.z)
        wv = [p for p in _verts(wdm).values() if _d(p, hp) < 25.0]   # 손 근처 무기 정점(손잡이)
        w_in = _inside(wv, gdm)
        out["glove_in_weapon"] = {"n": len(g_in), "of": len(gv), "max_cm": round(max([d for _p, d in g_in], default=0.0), 2)}
        out["weapon_in_glove"] = {"n": len(w_in), "of": len(wv), "max_cm": round(max([d for _p, d in w_in], default=0.0), 2)}
        hand_v = _near(list(_verts(body_dm).values()), gdm, 3.0)
        h_in = _inside(hand_v, wdm)
        out["barehand_in_weapon"] = {"n": len(h_in), "of": len(hand_v), "max_cm": round(max([d for _p, d in h_in], default=0.0), 2)}
    return out


def _shoulder_stats(bind_dm, posed_dm, centers, body_dm=None):
    bv, pv = _verts(bind_dm), _verts(posed_dm)
    ratios = []
    region_v = set()
    where = {}   # 뭉친 삼각형 위치: 가까운 관절 × (위·옆·겨드랑이) — 기준 자세에서 그 관절 대비 높이
    ua = {"l": centers[2], "r": centers[3]}
    for t, (a, b, c) in _tris(bind_dm).items():
        cen = tuple((bv[a][i] + bv[b][i] + bv[c][i]) / 3.0 for i in range(3))
        if min(_d(cen, j) for j in centers) > SHOULDER_RADIUS:
            continue
        a0 = _area(bv[a], bv[b], bv[c])
        if a0 < 1e-4:
            continue
        r = _area(pv[a], pv[b], pv[c]) / a0
        ratios.append(r)
        region_v.update((a, b, c))
        if r < SQUASH:
            side = "l" if cen[0] * ua["l"][0] > 0 else "r"
            dz = cen[2] - ua[side][2]
            band = "top" if dz > 4 else ("armpit" if dz < -4 else "side")
            ring = "torso" if abs(cen[0]) < abs(ua[side][0]) - 2 else "arm"
            k = f"{side}_{ring}_{band}"
            where[k] = where.get(k, 0) + 1
    n = len(ratios)
    if n == 0:
        return {"tris": 0}
    st = {"tris": n, "p5": round(_pct(ratios, 0.05), 2), "p95": round(_pct(ratios, 0.95), 2),
          f"squash<{SQUASH}%": round(100.0 * sum(r < SQUASH for r in ratios) / n, 1),
          f"stretch>{STRETCH}%": round(100.0 * sum(r > STRETCH for r in ratios) / n, 1),
          "squash_where": dict(sorted(where.items(), key=lambda kv: -kv[1]))}
    if body_dm is not None:
        pts = [pv[v] for v in region_v]
        st["in_body%"] = round(100.0 * len(_inside(pts, body_dm)) / max(1, len(pts)), 1)
    return st


def _centers(rec):
    from . import skeleton_check
    bones = {n: t for n, _p, t in skeleton_check.bones(profiles.body(rec["body"])["leader"])}
    return [tuple((bones[j].translation.x, bones[j].translation.y, bones[j].translation.z)) for j in SHOULDER_JOINTS]


def measure_top(rec):
    if "top" not in rec["skel"]:
        return {}
    lead = rec["lead"]
    posed = _to_local(_copy(rec["skel"]["top"]), lead)
    body_dm = _to_local(_copy(rec["skel"]["body"]), lead)
    return _shoulder_stats(rec["bind"]["top"], posed, _centers(rec), body_dm)


# ---------------------------------------------------------------- 쥐는 위치 찾기
def _bvh(dm):
    r = SP.build_bvh_for_mesh(dm)
    return r[1] if isinstance(r, tuple) else r


def _grip_eval(gv, wv, wdm, wbvh, gdm, gbvh, delta, touch_cm):
    """무기를 delta(컴포넌트 공간)만큼 옮겼을 때: 장갑 정점이 무기 안(P1), 무기 정점이 장갑 안(P2), 무기 표면 touch_cm 안에 닿은 장갑 정점(C)."""
    opt = unreal.GeometryScriptSpatialQueryOptions()
    p1 = c = 0
    for p in gv:
        q = unreal.Vector(p[0] - delta[0], p[1] - delta[1], p[2] - delta[2])
        _m, ins, _o = SP.is_point_inside_mesh(wdm, wbvh, q, opt)
        if ins:
            p1 += 1
            continue
        _m, hit, _o = SP.find_nearest_point_on_mesh(wdm, wbvh, q, opt)
        if _d((q.x, q.y, q.z), (hit.position.x, hit.position.y, hit.position.z)) <= touch_cm:
            c += 1
    p2 = 0
    for p in wv:
        q = unreal.Vector(p[0] + delta[0], p[1] + delta[1], p[2] + delta[2])
        _m, ins, _o = SP.is_point_inside_mesh(gdm, gbvh, q, opt)
        p2 += 1 if ins else 0
    return p1, p2, c


GRIP_W = (2.0, 0.5)   # 점수 = 감쌈(장갑 정점이 무기 표면 touch_cm 안) - 2·(장갑이 무기 안) - 0.5·(무기가 장갑 안). 무기 쪽 정점이 더 촘촘해서 가중치를 낮춤


def grip_search(body, weapon_key, gloves_key, pose_key, span=3.0, step=1.0, refine=0.5, touch_cm=2.0, top=5):
    """무기 쥔 자세에서 무기 부착 위치를 손 본 기준으로 ±span cm 격자로 옮겨 보며 관통(P1 + P2)이 적고 손에 닿는(C) 위치를 찾는다.
    점수 = C - 2·(P1 + P2). 거친 격자(step) 다음 최고점 주변을 refine 간격으로 다시. 반환: 기준(0,0,0)·상위 결과(손 본 로컬 cm — items.json attach.offset에 더할 값)."""
    rec = rig("PG", body, {"gloves": gloves_key}, weapon_key)
    try:
        pose("PG", pose_key)
        lead = rec["lead"]
        sock_name = rec["weapon"][1]["socket"]
        lc = rec["skel"]["body"]
        sock = lc.get_socket_transform(sock_name, RTS_COMPONENT)
        hp = (sock.translation.x, sock.translation.y, sock.translation.z)
        wdm = _weapon_local(rec)
        gdm = _to_local(_copy(rec["skel"]["gloves"]), lead)
        gv = [p for p in _verts(gdm).values() if _d(p, hp) < 22.0][::2]   # 쥔 손만, 시간 때문에 정점 절반
        wv = [p for p in _verts(wdm).values() if _d(p, hp) < 20.0][::2]
        wbvh, gbvh = _bvh(wdm), _bvh(gdm)

        def comp(local):
            v = sock.rotation.rotate_vector(unreal.Vector(*local))
            return (v.x, v.y, v.z)

        # 손잡이 축(무기 메시의 가장 긴 축)을 손 본 로컬로 — 축 방향 위치는 grip_frac이 정하므로 그 수직 평면에서만 찾는다
        st_mesh = unreal.load_asset(rec["weapon"][1]["mesh"])
        ext = st_mesh.get_bounds().box_extent
        ai = max(range(3), key=lambda i: (ext.x, ext.y, ext.z)[i])
        av = rec["weapon"][1]["transform"].rotation.rotate_vector(unreal.Vector(*[1.0 if i == ai else 0.0 for i in range(3)]))
        a = (av.x, av.y, av.z)
        ref = (0.0, 0.0, 1.0) if abs(a[2]) < 0.9 else (1.0, 0.0, 0.0)
        u = (a[1] * ref[2] - a[2] * ref[1], a[2] * ref[0] - a[0] * ref[2], a[0] * ref[1] - a[1] * ref[0])
        un = math.sqrt(sum(x * x for x in u))
        u = tuple(x / un for x in u)
        w = (a[1] * u[2] - a[2] * u[1], a[2] * u[0] - a[0] * u[2], a[0] * u[1] - a[1] * u[0])

        results = {}

        def run(center, st, half):
            n = int(round(half / st))
            for i in range(-n, n + 1):
                for j in range(-n, n + 1):
                    loc = tuple(round(center[q] + i * st * u[q] + j * st * w[q], 2) for q in range(3))
                    if loc in results:
                        continue
                    p1, p2, c = _grip_eval(gv, wv, wdm, wbvh, gdm, gbvh, comp(loc), touch_cm)
                    results[loc] = {"offset": loc, "glove_in_weapon": p1, "weapon_in_glove": p2, "touch": c,
                                    "score": round(c - GRIP_W[0] * p1 - GRIP_W[1] * p2, 1)}

        run((0.0, 0.0, 0.0), step, span)
        best = max(results.values(), key=lambda r: r["score"])["offset"]
        run(best, refine, step)
        ranked = sorted(results.values(), key=lambda r: -r["score"])
        return {"body": body, "weapon": weapon_key, "gloves": gloves_key, "pose": pose_key, "socket": sock_name,
                "handle_axis_local": [round(x, 3) for x in a],
                "glove_verts": len(gv), "weapon_verts": len(wv), "evaluated": len(results),
                "current": results[(0.0, 0.0, 0.0)], "best": ranked[:top]}
    finally:
        clear("PG")


# ---------------------------------------------------------------- 촬영
def shots(prefix, name):
    """지금 자세를 두 장: 상체 3/4 정면(가슴을 따라감 — 도약 동작), 무기 손 확대(가슴 → 손 방향 바깥에서). 반환: 파일 경로 목록."""
    rec = _RIGS[prefix]
    lc = rec["skel"]["body"]
    c = lc.get_socket_location("spine_03")
    out = [preview.shot(f"{name}_upper", (c.x + 150, c.y + 240, c.z + 40), (c.x, c.y, c.z + 5), fov=55.0)]
    for side, sx in (("r", -1.0), ("l", 1.0)):   # 어깨 확대: 바깥 옆·약간 앞에서(오른쪽 = -X)
        u = lc.get_socket_location(f"upperarm_{side}")
        out.append(preview.shot(f"{name}_shoulder_{side}", (u.x + sx * 90, u.y + 60, u.z + 15), (u.x, u.y, u.z), fov=45.0, w=800, h=800))
    if rec["weapon"]:
        h = lc.get_socket_location(rec["weapon"][1]["socket"])
        d = (h.x - c.x, h.y - c.y, 0.0)
        n = math.sqrt(d[0] ** 2 + d[1] ** 2) or 1.0
        cam = (h.x + 70 * d[0] / n + 15, h.y + 70 * d[1] / n + 25, h.z + 15)
        out.append(preview.shot(f"{name}_hand", cam, (h.x, h.y, h.z), fov=45.0, w=800, h=800))
    if rec.get("offhand"):   # 방패: 왼쪽 앞(+X·+Y)과 뒤쪽 안(손잡이 쥔 모습)에서
        h = lc.get_socket_location(rec["offhand"][1]["socket"])
        out.append(preview.shot(f"{name}_shield_front", (h.x + 120, h.y + 160, h.z + 30), (h.x, h.y, h.z), fov=50.0, w=800, h=800))
        out.append(preview.shot(f"{name}_shield_back", (h.x - 60, h.y - 110, h.z + 50), (h.x, h.y, h.z), fov=50.0, w=800, h=800))
    return out


# ---------------------------------------------------------------- 묶음 실행
def run(body, loadout, pose_keys, take_shots=True, label="", prefix="PZ", loc=(0.0, 0.0, 0.0)):
    """pose_keys 각각에 대해 (무기 타입별 리그) 자세 → 측정(장갑·무기, 상의 어깨) → 촬영.
    loadout: {"top", "gloves", "bottom", "boots": 아이템 키 또는 메시 경로, "bow"/"sword": 무기 아이템 키, "shield": 방패 키(검 자세에만)}. 반환: {포즈: 결과}."""
    results = {}
    current = None
    for key in pose_keys:
        _seq, _t, wtype = POSES[key]
        if current != wtype:
            rec = rig(prefix, body, loadout, loadout.get(wtype), loc)
            current = wtype
        pose(prefix, key)
        r = {"hands": measure_hands(rec), "top": measure_top(rec)}
        if take_shots:
            r["shots"] = shots(prefix, f"MC42e_{label or 'x'}_{body}_{key}")
        results[key] = r
    clear(prefix)
    return results
