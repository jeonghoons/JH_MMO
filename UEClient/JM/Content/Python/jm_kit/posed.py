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
  P.run("M", {"top": "00_Top", "gloves": "00_Gloves", "bow": "00_Bow", "sword": "00_OneHandSword"}, ["bow_draw", "sword_skill_up"])
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
    return garment.out_path(garment.load_items()[value], body)


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


def _xf(m, p):
    v = unreal.MathLibrary.transform_location(m, unreal.Vector(*p))
    return (v.x, v.y, v.z)


def _eval_rt(hand_v, w_v, wdm, wbvh, hdm, hbvh, m, touch_cm, depth=False, w=None):
    """무기를 컴포넌트 공간 변환 m으로 옮겼을 때: 손 정점이 무기 안(P1)·무기 정점이 손 안(P2)·무기 표면 touch_cm 안에 닿은 손 정점(C).
    depth면 안에 든 깊이 최대(cm)도."""
    opt = unreal.GeometryScriptSpatialQueryOptions()
    inv = unreal.MathLibrary.invert_transform(m)
    p1 = c = 0
    d1 = 0.0
    for p in hand_v:
        q = unreal.Vector(*_xf(inv, p))   # 손 정점을 원래 무기 공간으로
        _m, ins, _o = SP.is_point_inside_mesh(wdm, wbvh, q, opt)
        _m, hit, _o = SP.find_nearest_point_on_mesh(wdm, wbvh, q, opt)
        dist = _d((q.x, q.y, q.z), (hit.position.x, hit.position.y, hit.position.z))
        if ins:
            p1 += 1
            d1 = max(d1, dist)
        elif dist <= touch_cm:
            c += 1
    p2 = 0
    d2 = 0.0
    for p in w_v:
        q = unreal.Vector(*_xf(m, p))
        _m, ins, _o = SP.is_point_inside_mesh(hdm, hbvh, q, opt)
        if ins:
            p2 += 1
            if depth:
                _m, hit, _o = SP.find_nearest_point_on_mesh(hdm, hbvh, q, opt)
                d2 = max(d2, _d((q.x, q.y, q.z), (hit.position.x, hit.position.y, hit.position.z)))
    w = w or GRIP_W
    r = {"hand_in_weapon": p1, "weapon_in_hand": p2, "touch": c, "score": round(c - w[0] * p1 - w[1] * p2, 1)}
    if depth:
        r.update({"hand_in_weapon_max_cm": round(d1, 2), "weapon_in_hand_max_cm": round(d2, 2)})
    return r


def grip_search_socket(body, weapon_key, pose_key, gloves_key=None, span=3.0, step=1.0, refine=0.5,
                       tilt=15.0, tilt_step=5.0, touch_cm=2.0, hand_cm=14.0, top=5):
    """소켓에 붙은 정규화 무기(D-247)를 쥔 자세에서 손가락이 감싼 빈 곳에 맞추는 소켓 보정을 찾는다(맨손 기준, gloves_key면 장갑).
    탐색은 소켓 로컬(손잡이 = +Z): ① 위치 X·Y ±span(step) ② 그 자리에서 X·Y축 기울기 ±tilt(tilt_step) ③ 위치 다시 ±step(refine).
    반환: 지금(보정 0)·최고 결과(소켓 로컬 이동 cm·기울기 도)와 손 뼈 기준 새 소켓 상대 변환."""
    loadout = {"gloves": gloves_key} if gloves_key else {}
    rec = rig("PGS", body, loadout, weapon_key)
    try:
        pose("PGS", pose_key)
        lead = rec["lead"]
        sc, s = rec["weapon"]
        lc = rec["skel"]["body"]
        sock = lc.get_socket_transform(s["socket"], RTS_COMPONENT)
        bone_name = str(lc.get_socket_bone_name(s["socket"]))
        bone = lc.get_socket_transform(bone_name, RTS_COMPONENT)
        hp = (bone.translation.x, bone.translation.y, bone.translation.z)
        op = (sock.translation.x, sock.translation.y, sock.translation.z)
        wdm = _weapon_local(rec)
        hdm = _to_local(_copy(rec["skel"]["gloves" if gloves_key else "body"]), lead)
        hand_v = [p for p in _verts(hdm).values() if _d(p, hp) < hand_cm][::2]   # 손바닥·손가락(손 뼈 주변)
        w_v = [p for p in _verts(wdm).values() if _d(p, op) < 12.0][::2]          # 손잡이·가드
        wbvh, hbvh = _bvh(wdm), _bvh(hdm)
        inv_sock = unreal.MathLibrary.invert_transform(sock)

        def m_of(t, rx, ry):
            d = unreal.Transform(location=unreal.Vector(t[0], t[1], 0.0), rotation=unreal.Rotator(roll=rx, pitch=ry, yaw=0.0))
            return unreal.MathLibrary.compose_transforms(unreal.MathLibrary.compose_transforms(inv_sock, d), sock)

        res = {}

        def ev(t, rx, ry):
            k = (round(t[0], 2), round(t[1], 2), round(rx, 1), round(ry, 1))
            if k not in res:
                res[k] = dict(_eval_rt(hand_v, w_v, wdm, wbvh, hdm, hbvh, m_of(t, rx, ry), touch_cm), t=[k[0], k[1]], tilt=[k[2], k[3]])
            return res[k]

        def grid(c, st, half, fn):
            n = int(round(half / st))
            return [fn(c, i * st, j * st) for i in range(-n, n + 1) for j in range(-n, n + 1)]

        base = ev((0.0, 0.0), 0.0, 0.0)
        best = max(grid((0.0, 0.0), step, span, lambda c, a, b: ev((c[0] + a, c[1] + b), 0.0, 0.0)), key=lambda r: r["score"])
        bt = tuple(best["t"])
        best = max(grid((0.0, 0.0), tilt_step, tilt, lambda c, a, b: ev(bt, a, b)), key=lambda r: r["score"])
        rx, ry = best["tilt"]
        best = max(grid(bt, refine, step, lambda c, a, b: ev((c[0] + a, c[1] + b), rx, ry)), key=lambda r: r["score"])
        bt = tuple(best["t"])
        final = dict(_eval_rt(hand_v, w_v, wdm, wbvh, hdm, hbvh, m_of(bt, rx, ry), touch_cm, depth=True), t=list(bt), tilt=[rx, ry])
        cur = dict(_eval_rt(hand_v, w_v, wdm, wbvh, hdm, hbvh, m_of((0.0, 0.0), 0.0, 0.0), touch_cm, depth=True), t=[0, 0], tilt=[0, 0])
        # 손 뼈 기준 소켓: 새 소켓 = (소켓 로컬 보정 d) 다음 (지금 소켓의 뼈 기준 변환)
        old_rel = unreal.MathLibrary.compose_transforms(sock, unreal.MathLibrary.invert_transform(bone))
        d = unreal.Transform(location=unreal.Vector(bt[0], bt[1], 0.0), rotation=unreal.Rotator(roll=rx, pitch=ry, yaw=0.0))
        new_rel = unreal.MathLibrary.compose_transforms(d, old_rel)
        row = lambda tf: {"loc": [round(tf.translation.x, 3), round(tf.translation.y, 3), round(tf.translation.z, 3)],
                          "rot": [round(tf.rotation.rotator().roll, 3), round(tf.rotation.rotator().pitch, 3), round(tf.rotation.rotator().yaw, 3)]}
        return {"body": body, "weapon": weapon_key, "mesh": s["mesh"], "pose": pose_key, "anim": POSES[pose_key][0], "time_s": POSES[pose_key][1],
                "hand": gloves_key or "맨손(몸 메시)", "socket": s["socket"], "bone": bone_name,
                "hand_verts": len(hand_v), "weapon_verts": len(w_v), "evaluated": len(res),
                "current": cur, "best": final, "top": sorted(res.values(), key=lambda r: -r["score"])[:top],
                "socket_rel_bone_now": row(old_rel), "socket_rel_bone_new": row(new_rel)}
    finally:
        clear("PGS")


def grip_check_socket(body, weapon_key, pose_key, gloves_key=None, rel=None, touch_cm=2.0, hand_cm=14.0):
    """소켓 값(rel = {"loc", "rot"} 뼈 기준 — 없으면 지금 소켓)으로 무기를 놓았을 때의 관통·감쌈(다른 체형 확인용)."""
    loadout = {"gloves": gloves_key} if gloves_key else {}
    rec = rig("PGC", body, loadout, weapon_key)
    try:
        pose("PGC", pose_key)
        lead = rec["lead"]
        _sc, s = rec["weapon"]
        lc = rec["skel"]["body"]
        sock = lc.get_socket_transform(s["socket"], RTS_COMPONENT)
        bone_name = str(lc.get_socket_bone_name(s["socket"]))
        bone = lc.get_socket_transform(bone_name, RTS_COMPONENT)
        m = unreal.Transform()
        if rel:
            r = rel["rot"]
            new_rel = unreal.Transform(location=unreal.Vector(*rel["loc"]), rotation=unreal.Rotator(roll=r[0], pitch=r[1], yaw=r[2]))
            new_sock = unreal.MathLibrary.compose_transforms(new_rel, bone)
            m = unreal.MathLibrary.compose_transforms(unreal.MathLibrary.invert_transform(sock), new_sock)
        hp = (bone.translation.x, bone.translation.y, bone.translation.z)
        wdm = _weapon_local(rec)
        hdm = _to_local(_copy(rec["skel"]["gloves" if gloves_key else "body"]), lead)
        op = _xf(m, (sock.translation.x, sock.translation.y, sock.translation.z))
        hand_v = [p for p in _verts(hdm).values() if _d(p, hp) < hand_cm][::2]
        w_v = [p for p in _verts(wdm).values() if _d(_xf(m, p), op) < 12.0][::2]
        return {"body": body, "hand": gloves_key or "맨손(몸 메시)",
                **_eval_rt(hand_v, w_v, wdm, _bvh(wdm), hdm, _bvh(hdm), m, touch_cm, depth=True)}
    finally:
        clear("PGC")


# 무기 종류별 측정 프레임(D-247 소켓 맞춤 — "그 무기의 애니메이션으로"): 손 모양이 크게 바뀌는 순간
GRIP_FRAMES = {"sword": ("sword_idle", "sword_windup", "sword_swing", "sword_skill_up"),
               "shield": ("sword_idle", "sword_windup", "sword_swing", "sword_skill_up"),
               "bow": ("bow_idle", "bow_draw", "bow_aim")}


def _frame_ctx(prefix, rec, pose_key, use_gloves, hand_cm):
    pose(prefix, pose_key)
    lead = rec["lead"]
    _sc, s = rec["weapon"]
    lc = rec["skel"]["body"]
    sock = lc.get_socket_transform(s["socket"], RTS_COMPONENT)
    bone_name = str(lc.get_socket_bone_name(s["socket"]))
    bone = lc.get_socket_transform(bone_name, RTS_COMPONENT)
    hp = (bone.translation.x, bone.translation.y, bone.translation.z)
    op = (sock.translation.x, sock.translation.y, sock.translation.z)
    wdm = _weapon_local(rec)
    hdm = _to_local(_copy(rec["skel"]["gloves" if use_gloves else "body"]), lead)
    return {"pose": pose_key, "sock": sock, "inv_sock": unreal.MathLibrary.invert_transform(sock), "bone": bone, "bone_name": bone_name,
            "wdm": wdm, "hdm": hdm, "wbvh": _bvh(wdm), "hbvh": _bvh(hdm),
            "hand_v": [p for p in _verts(hdm).values() if _d(p, hp) < hand_cm][::2],
            "w_v": [p for p in _verts(wdm).values() if _d(p, op) < 12.0][::2]}


def _m_local(ctx, t, rx, ry):
    d = unreal.Transform(location=unreal.Vector(t[0], t[1], 0.0), rotation=unreal.Rotator(roll=rx, pitch=ry, yaw=0.0))
    return unreal.MathLibrary.compose_transforms(unreal.MathLibrary.compose_transforms(ctx["inv_sock"], d), ctx["sock"])


def _eval_frames(ctxs, t, rx, ry, touch_cm, depth=False):
    per = [dict(_eval_rt(c["hand_v"], c["w_v"], c["wdm"], c["wbvh"], c["hdm"], c["hbvh"], _m_local(c, t, rx, ry), touch_cm, depth), pose=c["pose"])
           for c in ctxs]
    return {"t": [round(t[0], 2), round(t[1], 2)], "tilt": [round(rx, 1), round(ry, 1)], "score": round(sum(p["score"] for p in per), 1),
            "hand_in_weapon": sum(p["hand_in_weapon"] for p in per), "weapon_in_hand": sum(p["weapon_in_hand"] for p in per),
            "touch": sum(p["touch"] for p in per), "frames": per}


def grip_search_frames(body, weapon_key, kind, gloves_key=None, span=5.0, step=1.0, refine=0.5, tilt=15.0, tilt_step=5.0,
                       touch_cm=2.0, hand_cm=14.0, top=5):
    """grip_search_socket의 여러 프레임판: 무기 종류(kind: sword·shield·bow)의 GRIP_FRAMES 전부에서 점수를 더해 소켓 보정을 찾는다."""
    frames = GRIP_FRAMES[kind]
    rec = rig("PGM", body, {"gloves": gloves_key} if gloves_key else {}, weapon_key)
    try:
        ctxs = [_frame_ctx("PGM", rec, pk, bool(gloves_key), hand_cm) for pk in frames]
        res = {}

        def ev(t, rx, ry):
            k = (round(t[0], 2), round(t[1], 2), round(rx, 1), round(ry, 1))
            if k not in res:
                r = _eval_frames(ctxs, t, rx, ry, touch_cm)
                r.pop("frames")
                res[k] = r
            return res[k]

        def grid(half, st, fn):
            n = int(round(half / st))
            return [fn(i * st, j * st) for i in range(-n, n + 1) for j in range(-n, n + 1)]

        best = max(grid(span, step, lambda a, b: ev((a, b), 0.0, 0.0)), key=lambda r: r["score"])
        bt = tuple(best["t"])
        best = max(grid(tilt, tilt_step, lambda a, b: ev(bt, a, b)), key=lambda r: r["score"])
        rx, ry = best["tilt"]
        best = max(grid(step, refine, lambda a, b: ev((bt[0] + a, bt[1] + b), rx, ry)), key=lambda r: r["score"])
        bt = tuple(best["t"])
        edge = abs(bt[0]) >= span - 1e-6 or abs(bt[1]) >= span - 1e-6 or abs(rx) >= tilt - 1e-6 or abs(ry) >= tilt - 1e-6
        cur = _eval_frames(ctxs, (0.0, 0.0), 0.0, 0.0, touch_cm, depth=True)
        fin = _eval_frames(ctxs, bt, rx, ry, touch_cm, depth=True)
        c0 = ctxs[0]
        old_rel = unreal.MathLibrary.compose_transforms(c0["sock"], unreal.MathLibrary.invert_transform(c0["bone"]))
        d = unreal.Transform(location=unreal.Vector(bt[0], bt[1], 0.0), rotation=unreal.Rotator(roll=rx, pitch=ry, yaw=0.0))
        new_rel = unreal.MathLibrary.compose_transforms(d, old_rel)
        row = lambda tf: {"loc": [round(tf.translation.x, 3), round(tf.translation.y, 3), round(tf.translation.z, 3)],
                          "rot": [round(tf.rotation.rotator().roll, 3), round(tf.rotation.rotator().pitch, 3), round(tf.rotation.rotator().yaw, 3)]}
        dl = new_rel.translation - old_rel.translation
        return {"body": body, "weapon": weapon_key, "mesh": rec["weapon"][1]["mesh"], "kind": kind,
                "frames": [{"pose": pk, "anim": POSES[pk][0].rsplit("/", 1)[1], "time_s": POSES[pk][1]} for pk in frames],
                "hand": gloves_key or "맨손(몸 메시)", "socket": rec["weapon"][1]["socket"], "bone": c0["bone_name"],
                "evaluated": len(res), "at_search_edge": edge, "current": cur, "best": fin, "top": sorted(res.values(), key=lambda r: -r["score"])[:top],
                "socket_rel_bone_now": row(old_rel), "socket_rel_bone_new": row(new_rel),
                "delta_socket_local": {"t_cm": list(bt), "tilt_deg": [rx, ry]},
                "delta_bone_cm": [round(dl.x, 3), round(dl.y, 3), round(dl.z, 3)], "delta_len_cm": round(dl.length(), 2)}
    finally:
        clear("PGM")


SHIELD_W = (5.0, 5.0)   # 방패: 관통 벌점을 크게(닿음을 늘리려고 팔 안으로 미는 것을 막음)


def _seg_d(p, a, b):
    ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    t = max(0.0, min(1.0, _dot(_sub(p, a), ab) / max(1e-6, _dot(ab, ab))))
    return _d(p, (a[0] + t * ab[0], a[1] + t * ab[1], a[2] + t * ab[2]))


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _shield_ctx(prefix, rec, pose_key, arm_cm, shield_cm):
    """방패 맞춤용 프레임: 팔뚝(아래팔 → 손 선분 arm_cm 안) + 손(손 뼈 12cm 안) 정점, 소켓 원점 shield_cm 안의 방패 정점."""
    pose(prefix, pose_key)
    lead = rec["lead"]
    _sc, s = rec["weapon"]
    lc = rec["skel"]["body"]
    sock = lc.get_socket_transform(s["socket"], RTS_COMPONENT)
    bone_name = str(lc.get_socket_bone_name(s["socket"]))
    bone = lc.get_socket_transform(bone_name, RTS_COMPONENT)
    side = bone_name[-1]
    la = lc.get_socket_transform(f"lowerarm_{side}", RTS_COMPONENT).translation
    a, b = (la.x, la.y, la.z), (bone.translation.x, bone.translation.y, bone.translation.z)
    op = (sock.translation.x, sock.translation.y, sock.translation.z)
    wdm = _weapon_local(rec)
    hdm = _to_local(_copy(lc), lead)
    arm_v = [p for p in _verts(hdm).values() if _seg_d(p, a, b) < arm_cm or _d(p, b) < 12.0][::3]
    return {"pose": pose_key, "sock": sock, "inv_sock": unreal.MathLibrary.invert_transform(sock), "bone": bone, "bone_name": bone_name,
            "wdm": wdm, "hdm": hdm, "wbvh": _bvh(wdm), "hbvh": _bvh(hdm), "hand_v": arm_v,
            "w_v": [p for p in _verts(wdm).values() if _d(p, op) < shield_cm][::3]}


def _m6(ctx, t, r):
    """소켓 로컬 보정(이동 t = [x,y,z] cm, 회전 r = [roll,pitch,yaw] 도)을 컴포넌트 공간 변환으로."""
    d = unreal.Transform(location=unreal.Vector(*t), rotation=unreal.Rotator(roll=r[0], pitch=r[1], yaw=r[2]))
    return unreal.MathLibrary.compose_transforms(unreal.MathLibrary.compose_transforms(ctx["inv_sock"], d), ctx["sock"])


def grip_search_shield(body, weapon_key, touch_cm=3.0, arm_cm=7.0, shield_cm=30.0, top=5, yz=6.0, yz_step=1.5, tilt=20.0):
    """방패(D-247, Mixamo 검·방패 동작): 팔뚝과 손등이 방패 뒷면에 붙는 자리. 손가락 감쌈이 아니라
    '팔·손이 방패를 뚫지 않으면서(벌점 SHIELD_W) 방패 뒷면 touch_cm 안에 닿는 팔 정점이 많은' 자리를 한손검 4프레임 합으로 찾는다.
    소켓 로컬: X = 방패 앞면(손등 쪽 간격), Y·Z = 방패 면 안. 단계: X 간격 → Y·Z → 기울기(pitch·yaw) → 미세."""
    frames = GRIP_FRAMES["shield"]
    rec = rig("PGH", body, {}, weapon_key)
    try:
        ctxs = [_shield_ctx("PGH", rec, pk, arm_cm, shield_cm) for pk in frames]
        res = {}

        def ev(t, r):
            k = tuple(round(v, 2) for v in t) + tuple(round(v, 1) for v in r)
            if k not in res:
                per = [_eval_rt(c["hand_v"], c["w_v"], c["wdm"], c["wbvh"], c["hdm"], c["hbvh"], _m6(c, t, r), touch_cm, w=SHIELD_W)
                       for c in ctxs]
                res[k] = {"t": list(k[:3]), "rot": list(k[3:]), "score": round(sum(p["score"] for p in per), 1),
                          "arm_in_shield": sum(p["hand_in_weapon"] for p in per), "shield_in_arm": sum(p["weapon_in_hand"] for p in per),
                          "touch": sum(p["touch"] for p in per)}
            return res[k]

        rng = lambda lo, hi, st: [lo + i * st for i in range(int(round((hi - lo) / st)) + 1)]
        best = max((ev((x, 0, 0), (0, 0, 0)) for x in rng(-3.0, 6.0, 1.0)), key=lambda r: r["score"])
        gx = best["t"][0]
        best = max((ev((gx, y, z), (0, 0, 0)) for y in rng(-yz, yz, yz_step) for z in rng(-yz, yz, yz_step)), key=lambda r: r["score"])
        ty, tz = best["t"][1], best["t"][2]
        best = max((ev((gx, ty, tz), (0, p, yw)) for p in rng(-tilt, tilt, 5.0) for yw in rng(-tilt, tilt, 5.0)), key=lambda r: r["score"])
        rot = best["rot"]
        best = max((ev((gx + a, ty + b, tz + c), rot) for a in rng(-1.0, 1.0, 0.5) for b in rng(-1.0, 1.0, 0.5) for c in rng(-1.0, 1.0, 0.5)),
                   key=lambda r: r["score"])
        t = best["t"]

        def detail(tt, rr):
            return [dict(_eval_rt(c["hand_v"], c["w_v"], c["wdm"], c["wbvh"], c["hdm"], c["hbvh"], _m6(c, tt, rr), touch_cm, depth=True, w=SHIELD_W),
                         pose=c["pose"]) for c in ctxs]

        c0 = ctxs[0]
        old_rel = unreal.MathLibrary.compose_transforms(c0["sock"], unreal.MathLibrary.invert_transform(c0["bone"]))
        d = unreal.Transform(location=unreal.Vector(*t), rotation=unreal.Rotator(roll=rot[0], pitch=rot[1], yaw=rot[2]))
        new_rel = unreal.MathLibrary.compose_transforms(d, old_rel)
        row = lambda tf: {"loc": [round(tf.translation.x, 3), round(tf.translation.y, 3), round(tf.translation.z, 3)],
                          "rot": [round(tf.rotation.rotator().roll, 3), round(tf.rotation.rotator().pitch, 3), round(tf.rotation.rotator().yaw, 3)]}
        dl = new_rel.translation - old_rel.translation
        edge = abs(t[0] - (-3.0)) < 1e-6 or abs(t[0] - 6.0) < 1e-6 or abs(t[1]) >= yz or abs(t[2]) >= yz or abs(rot[1]) >= tilt or abs(rot[2]) >= tilt
        return {"body": body, "weapon": weapon_key, "mesh": rec["weapon"][1]["mesh"], "socket": rec["weapon"][1]["socket"],
                "frames": [{"pose": pk, "anim": POSES[pk][0].rsplit("/", 1)[1], "time_s": POSES[pk][1]} for pk in frames],
                "arm_verts": [len(c["hand_v"]) for c in ctxs], "shield_verts": len(c0["w_v"]), "evaluated": len(res), "at_search_edge": edge,
                "current": detail((0, 0, 0), (0, 0, 0)), "best": detail(t, rot), "best_sum": best,
                "top": sorted(res.values(), key=lambda r: -r["score"])[:top],
                "delta_socket_local": {"t_cm": t, "rot_deg": rot}, "delta_bone_cm": [round(dl.x, 3), round(dl.y, 3), round(dl.z, 3)],
                "delta_len_cm": round(dl.length(), 2), "socket_rel_bone_now": row(old_rel), "socket_rel_bone_new": row(new_rel)}
    finally:
        clear("PGH")


def grip_check_shield(body, weapon_key, delta=None, touch_cm=3.0, arm_cm=7.0, shield_cm=30.0):
    """방패 소켓 보정 delta({"t":[x,y,z], "rot":[roll,pitch,yaw]})를 다른 체형에서 잰다."""
    rec = rig("PGI", body, {}, weapon_key)
    try:
        out = []
        for pk in GRIP_FRAMES["shield"]:
            c = _shield_ctx("PGI", rec, pk, arm_cm, shield_cm)
            m = _m6(c, delta["t"], delta["rot"]) if delta else unreal.Transform()
            out.append(dict(_eval_rt(c["hand_v"], c["w_v"], c["wdm"], c["wbvh"], c["hdm"], c["hbvh"], m, touch_cm, depth=True, w=SHIELD_W), pose=pk))
        return {"body": body, "frames": out}
    finally:
        clear("PGI")


def grip_check_frames(body, weapon_key, kind, gloves_key=None, rel=None, touch_cm=2.0, hand_cm=14.0, delta=None):
    """소켓 값 rel({"loc","rot"} 뼈 기준, 없으면 지금 소켓) 또는 소켓 로컬 보정 delta({"t":[x,y], "tilt":[rx,ry]})로
    kind의 GRIP_FRAMES 전부를 잰다(다른 체형·장갑 확인). delta면 새 소켓의 뼈 기준 값도 돌려준다."""
    rec = rig("PGN", body, {"gloves": gloves_key} if gloves_key else {}, weapon_key)
    try:
        out = []
        new_rel = None
        for pk in GRIP_FRAMES[kind]:
            c = _frame_ctx("PGN", rec, pk, bool(gloves_key), hand_cm)
            m = unreal.Transform()
            if delta:
                m = _m_local(c, delta["t"], delta["tilt"][0], delta["tilt"][1])
                if new_rel is None:
                    d = unreal.Transform(location=unreal.Vector(delta["t"][0], delta["t"][1], 0.0),
                                         rotation=unreal.Rotator(roll=delta["tilt"][0], pitch=delta["tilt"][1], yaw=0.0))
                    old = unreal.MathLibrary.compose_transforms(c["sock"], unreal.MathLibrary.invert_transform(c["bone"]))
                    nr = unreal.MathLibrary.compose_transforms(d, old)
                    r = nr.rotation.rotator()
                    new_rel = {"loc": [round(nr.translation.x, 3), round(nr.translation.y, 3), round(nr.translation.z, 3)],
                               "rot": [round(r.roll, 3), round(r.pitch, 3), round(r.yaw, 3)]}
            elif rel:
                r = rel["rot"]
                nr = unreal.Transform(location=unreal.Vector(*rel["loc"]), rotation=unreal.Rotator(roll=r[0], pitch=r[1], yaw=r[2]))
                m = unreal.MathLibrary.compose_transforms(c["inv_sock"], unreal.MathLibrary.compose_transforms(nr, c["bone"]))
            out.append(dict(_eval_rt(c["hand_v"], c["w_v"], c["wdm"], c["wbvh"], c["hdm"], c["hbvh"], m, touch_cm, depth=True), pose=pk))
        return {"body": body, "hand": gloves_key or "맨손(몸 메시)", "kind": kind, "socket_rel_bone": new_rel, "score": round(sum(o["score"] for o in out), 1),
                "hand_in_weapon": sum(o["hand_in_weapon"] for o in out), "weapon_in_hand": sum(o["weapon_in_hand"] for o in out),
                "touch": sum(o["touch"] for o in out), "frames": out}
    finally:
        clear("PGN")


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
