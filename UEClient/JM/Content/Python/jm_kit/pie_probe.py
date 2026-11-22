# -*- coding: utf-8 -*-
"""PIE 상태 읽기(2026-11-22 무기별 사전 점검): 캡처 전에 로그·데이터로 먼저 확인하는 1차 판정 수단.

게임 C++는 성공 로그를 남기지 않으므로(실패 경고만) PIE 월드를 직접 읽는다. 읽기만 하고 아무것도 바꾸지 않는다
(pp_aim만 예외 — 내 컨트롤러 회전을 바꿔 조준 방향을 맞춘다. 위치·판정은 서버 권위라 건드리지 않음).

단계(world_step의 step, args.world = PIE 창 번호):
  pp_probe  {"radius_m": 30}  → 내 캐릭터 몽타주·무기/방패 부착·HP·MP, 가까운 몬스터 HP 비율, 살아 있는 파티클 템플릿
  pp_aim    {"target": "nearest"} 또는 {"yaw", "pitch"} → 컨트롤러 회전을 가장 가까운 몬스터 쪽으로
"""
import math
import unreal


def _world(args):
    worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
    if not worlds:
        return None
    return worlds[min(int(args.get("world", 0)), len(worlds) - 1)]


def _m(v):
    return [round(v.x / 100.0, 2), round(v.y / 100.0, 2), round(v.z / 100.0, 2)]


def _name(obj):
    return obj.get_name() if obj else None


def _anim(char):
    mesh = char.mesh
    inst = mesh.get_anim_instance() if mesh else None
    if inst is None:
        return {"montage": None}
    mon = inst.get_current_active_montage()
    out = {"montage": _name(mon)}
    if mon:
        out["pos_ms"] = int(inst.montage_get_position(mon) * 1000)
        out["len_ms"] = int(mon.get_play_length() * 1000)
        out["rate"] = round(inst.montage_get_play_rate(mon), 3)
    return out


def _attachments(char):
    """캐릭터에 붙은 스태틱·스켈레탈 메시 중 소켓에 붙은 것(무기·방패·등에 멘 것). 몸 부위는 소켓이 없어 빠진다."""
    out = []
    for c in char.get_components_by_class(unreal.MeshComponent):
        sock = str(c.get_attach_socket_name())
        if sock in ("None", ""):
            continue
        mesh = None
        if isinstance(c, unreal.StaticMeshComponent):
            mesh = _name(c.static_mesh)
        elif isinstance(c, unreal.SkeletalMeshComponent):
            mesh = _name(c.get_skeletal_mesh_asset()) if hasattr(c, "get_skeletal_mesh_asset") else None
        out.append({"comp": c.get_name(), "mesh": mesh, "socket": sock,
                    "visible": bool(c.is_visible()) and not bool(c.hidden_in_game)})
    return out


def _particles(world, center, radius_cm):
    """살아 있는(활성) 파티클 컴포넌트 — 템플릿 이름·위치·중심까지 거리. SpawnEmitterAtLocation은 WorldSettings가 주인이다."""
    out = []
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        comps = a.get_components_by_class(unreal.ParticleSystemComponent)
        for c in comps:
            if not c.is_active():
                continue
            loc = c.get_world_location()
            d = (loc - center).length()
            if d > radius_cm:
                continue
            out.append({"fx": _name(c.template), "owner": a.get_class().get_name(), "at": _m(loc), "dist_m": round(d / 100.0, 2)})
    return out


def _named_children(cls, names):
    """BindWidget 자식 위젯을 이름으로 찾아 {(주인 UserWidget 이름, 자식 이름): 위젯}.
    Python에는 GetWidgetFromName·BindWidget 속성 접근이 없어 객체 목록에서 찾고 바깥(WidgetTree → UserWidget)으로 주인을 정한다."""
    out = {}
    for w in unreal.ObjectIterator(cls):
        n = w.get_name()
        if n not in names:
            continue
        tree = w.get_outer()
        owner = tree.get_outer() if tree else None
        if owner is not None:
            out[(owner.get_path_name(), n)] = w
    return out


def _hp_ratio(char, bars):
    for wc in char.get_components_by_class(unreal.WidgetComponent):
        w = wc.get_user_widget_object()
        if w is None:
            continue
        bar = bars.get((w.get_path_name(), "HpProgressBar"))
        if bar:
            return round(bar.get_editor_property("percent"), 3)
    return None


def probe(args):
    world = _world(args)
    if world is None:
        return {"error": "PIE가 실행 중이 아님"}
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    me = pc.get_controlled_pawn() if pc else None
    if me is None:
        return {"error": "플레이어 폰이 없음"}
    center = me.get_actor_location()
    radius = float(args.get("radius_m", 30)) * 100.0
    out = {"time_s": round(unreal.GameplayStatics.get_time_seconds(world), 2), "me": {
        "class": me.get_class().get_name(), "at": _m(center), "yaw": round(me.get_actor_rotation().yaw, 1),
        "anim": _anim(me), "attached": _attachments(me)}}
    bars = _named_children(unreal.ProgressBar, {"HpProgressBar"})
    mons = []
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Character):
        if a == me or a.get_class().get_name() != "BP_Npc_C":
            continue
        d = (a.get_actor_location() - center).length()
        if d <= radius:
            mons.append({"at": _m(a.get_actor_location()), "dist_m": round(d / 100.0, 2), "hp": _hp_ratio(a, bars), "anim": _anim(a)["montage"]})
    mons.sort(key=lambda x: x["dist_m"])
    out["monsters"] = mons[: int(args.get("max_monsters", 5))]
    out["particles"] = _particles(world, center, radius)
    cams = me.get_components_by_class(unreal.CameraComponent)
    if cams:
        c = cams[0]
        out["camera"] = {"world": _m(c.get_world_location()), "abs_loc": bool(c.get_editor_property("absolute_location")),
                         "abs_rot": bool(c.get_editor_property("absolute_rotation")),
                         "rel_loc": _m(c.get_editor_property("relative_location")),
                         "upcr": bool(c.get_editor_property("use_pawn_control_rotation")),
                         "parent": _name(c.get_attach_parent())}
    # 내 HUD의 HP·MP 글자(있으면) — CombatHud의 TxtHp·TxtMp
    hud = unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.load_class(None, "/Script/JM.CombatHudWidget"), False)
    if hud:
        texts = _named_children(unreal.TextBlock, {"TxtHp", "TxtMp"})
        for key in ("TxtHp", "TxtMp"):
            t = texts.get((hud[0].get_path_name(), key))
            if t:
                out["me"][key] = str(t.get_text())
    return out


def aim(args):
    world = _world(args)
    if world is None:
        return {"error": "PIE가 실행 중이 아님"}
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    me = pc.get_controlled_pawn() if pc else None
    if me is None:
        return {"error": "플레이어 폰이 없음"}
    if "yaw" in args:
        rot = unreal.Rotator(0.0, float(args.get("pitch", 0.0)), float(args["yaw"]))
    else:
        best, bd = None, 1e18
        for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Character):
            if a == me or a.get_class().get_name() != "BP_Npc_C":
                continue
            d = (a.get_actor_location() - me.get_actor_location()).length()
            if d < bd:
                best, bd = a, d
        if best is None:
            return {"error": "몬스터 없음"}
        # 카메라는 스프링 암 끝에 달려 회전하면 위치가 바뀌므로, 회전 중심(스프링 암 위치)에서 대상을 향하게 해야
        # 회전 뒤의 카메라 시선도 대상을 지난다(카메라 = 중심 - 시선 × 팔 길이).
        arms = me.get_components_by_class(unreal.SpringArmComponent)
        if arms:
            src = arms[0].get_world_location() + arms[0].get_editor_property("target_offset")
        else:
            cam = unreal.GameplayStatics.get_player_camera_manager(world, 0)
            src = cam.get_camera_location() if cam else me.get_actor_location()
        dst = best.get_actor_location()
        v = dst - src
        yaw = math.degrees(math.atan2(v.y, v.x))
        pitch = math.degrees(math.atan2(v.z, math.hypot(v.x, v.y)))
        rot = unreal.Rotator(0.0, pitch, yaw)
    pc.set_control_rotation(rot)
    return {"control_rot": [round(rot.pitch, 1), round(rot.yaw, 1)]}


# ── 녹화(pp_rec_start → pp_rec_read): 한 번 읽기로는 몽타주·이펙트 순간을 놓쳐 매 프레임 바뀐 것만 기록 ──
# 모듈은 호출마다 다시 읽히므로 상태는 unreal 모듈 속성에 둔다. 콜백은 Slate 틱 뒤(게임 스레드)에서 돈다.
_REC = "_jm_pie_rec"


def _fx_comps(world, me, mons):
    """이번 점검에서 볼 파티클: WorldSettings(SpawnEmitterAtLocation), 내 캐릭터(궤적), 몬스터, 화살 연출 액터."""
    owners = [unreal.GameplayStatics.get_actor_of_class(world, unreal.WorldSettings), me]
    for m in mons:
        try:
            m.get_name()
            if m.is_valid() if hasattr(m, "is_valid") else True:
                owners.append(m)
        except Exception:
            pass
    pcls = unreal.load_class(None, "/Script/JM.ProjectileFx")
    if pcls:
        owners += list(unreal.GameplayStatics.get_all_actors_of_class(world, pcls))
    out = []
    for a in owners:
        if a is None:
            continue
        for c in a.get_components_by_class(unreal.ParticleSystemComponent):
            if c.is_active():
                out.append((a, c))
    return out


def _side_cam_follow(st):
    """캐릭터 옆(시선 기준 angle도, dist_m, height_m)에서 캐릭터 허리를 보는 고정 카메라 — 캐릭터를 따라감."""
    dist, ang, hgt = st["cam_args"]
    me = st["me"]
    loc = me.get_actor_location()
    yaw = me.get_actor_rotation().yaw + ang
    r = math.radians(yaw)
    cam_loc = unreal.Vector(loc.x + math.cos(r) * dist * 100.0, loc.y + math.sin(r) * dist * 100.0, loc.z + hgt * 100.0 - 40.0)
    v = unreal.Vector(loc.x - cam_loc.x, loc.y - cam_loc.y, loc.z - cam_loc.z)
    rot = unreal.Rotator(0.0, math.degrees(math.atan2(v.z, math.hypot(v.x, v.y))), math.degrees(math.atan2(v.y, v.x)))
    st["cam"].set_world_location_and_rotation(cam_loc, rot, False, False)


def _cam_reset(cam, upcr=False):
    """플레이어 카메라를 스프링 암 끝(상대 0)으로 되돌린다. 절대 모드에서 읽은 상대 값은 월드 좌표라 저장해 두면 안 됨."""
    cam.set_absolute(False, False, False)
    cam.set_relative_location_and_rotation(unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), False, False)
    cam.set_editor_property("use_pawn_control_rotation", upcr)


def _side_cam_clear(st):
    cam = st.get("cam")
    if cam is None:
        return
    _cam_reset(cam, st["cam_orig"][0])
    st["cam"] = None


_REC_CMDS = ("autoattack", "attackstats", "equip", "highresshot")


def _allowed(cmd):
    """녹화 중 보낼 수 있는 명령(시험 명령·캡처만)."""
    return cmd.strip().split(" ")[0].lower() in _REC_CMDS


def rec_start(args):
    world = _world(args)
    if world is None:
        return {"error": "PIE가 실행 중이 아님"}
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    me = pc.get_controlled_pawn() if pc else None
    if me is None:
        return {"error": "플레이어 폰이 없음"}
    old = getattr(unreal, _REC, None)
    if old and old.get("handle") is not None:
        unreal.unregister_slate_post_tick_callback(old["handle"])
    center = me.get_actor_location()
    mons = sorted([a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Character)
                   if a != me and a.get_class().get_name() == "BP_Npc_C"],
                  key=lambda a: (a.get_actor_location() - center).length())[: int(args.get("max_monsters", 4))]
    bars = _named_children(unreal.ProgressBar, {"HpProgressBar"})
    # 내 HP 글자(HUD TxtHp) — 맞은 시각을 기록한다(피격 몽타주 판정, D-269)
    hp_txt = None
    hud = unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.load_class(None, "/Script/JM.CombatHudWidget"), False)
    if hud:
        hp_txt = _named_children(unreal.TextBlock, {"TxtHp"}).get((hud[0].get_path_name(), "TxtHp"))
    st = {"hp_txt": hp_txt, "world": world, "me": me, "mons": mons, "t0": unreal.GameplayStatics.get_time_seconds(world),
          "dur": float(args.get("seconds", 4.0)), "events": [], "last": {}, "seen_fx": set(), "frames": 0, "handle": None, "bars": bars,
          "todo": sorted([(int(ms), "HighResShot 1") for ms in args.get("shots_ms", [])] +
                         [(int(ms), c) for ms, c in args.get("cmds_at", []) if _allowed(c)]), "pc": pc, "cam": None}

    def ev(kind, **kw):
        kw["t_ms"] = int((unreal.GameplayStatics.get_time_seconds(st["world"]) - st["t0"]) * 1000)
        kw["kind"] = kind
        st["events"].append(kw)

    def tick(dt):
        try:
            w = st["world"]
            t = unreal.GameplayStatics.get_time_seconds(w) - st["t0"]
            st["frames"] += 1
            while st["todo"] and t * 1000 >= st["todo"][0][0]:
                ms, cmd = st["todo"].pop(0)
                unreal.SystemLibrary.execute_console_command(w, cmd, st["pc"])
                ev("cmd", cmd=cmd, at_ms=ms, montage_pos=_anim(st["me"]).get("pos_ms"))
            if st["cam"] is not None:
                _side_cam_follow(st)
            a = _anim(st["me"])
            key = a.get("montage")
            if key != st["last"].get("montage"):
                ev("montage", name=key, pos_ms=a.get("pos_ms"), len_ms=a.get("len_ms"), rate=a.get("rate"))
                st["last"]["montage"] = key
            if key:
                st["last"]["montage_pos"] = a.get("pos_ms")
            if st["hp_txt"] is not None:
                hp_s = str(st["hp_txt"].get_text())
                if hp_s != st["last"].get("my_hp"):
                    ev("my_hp", hp=hp_s, montage=key, montage_pos=a.get("pos_ms"))
                    st["last"]["my_hp"] = hp_s
            for at in _attachments(st["me"]):
                k = at["comp"]
                v = (at["mesh"], at["socket"], at["visible"])
                if st["last"].get(k) != v:
                    ev("attach", comp=k, mesh=v[0], socket=v[1], visible=v[2])
                    st["last"][k] = v
            for i, m in enumerate(st["mons"]):
                try:
                    hp = _hp_ratio(m, st["bars"])
                except Exception:   # 죽은 몬스터 액터가 사라짐
                    hp = "gone"
                if hp == "gone" and st["last"].get(("hp", i)) == "gone":
                    continue
                if hp == "gone":
                    ev("monster_hp", idx=i, hp="gone")
                    st["last"][("hp", i)] = hp
                    continue
                ma = _anim(m)
                if ma.get("montage") != st["last"].get(("anim", i)):
                    ev("monster_anim", idx=i, name=ma.get("montage"), pos_ms=ma.get("pos_ms"), len_ms=ma.get("len_ms"))
                    st["last"][("anim", i)] = ma.get("montage")
                if st["last"].get(("hp", i)) != hp:
                    ev("monster_hp", idx=i, hp=hp, dist_m=round((m.get_actor_location() - st["me"].get_actor_location()).length() / 100.0, 2))
                    st["last"][("hp", i)] = hp
            for owner, c in _fx_comps(w, st["me"], st["mons"]):
                cid = c.get_path_name()
                if cid in st["seen_fx"]:
                    continue
                st["seen_fx"].add(cid)
                loc = c.get_world_location()
                ev("fx", fx=_name(c.template), owner=owner.get_class().get_name(), at=_m(loc),
                   from_me_m=round((loc - st["me"].get_actor_location()).length() / 100.0, 2),
                   attach=str(c.get_attach_socket_name()))
            if t >= st["dur"] and not st["todo"]:
                _side_cam_clear(st)
                unreal.unregister_slate_post_tick_callback(st["handle"])
                st["handle"] = None
                ev("end", frames=st["frames"])
        except Exception as e:  # 콜백 예외가 매 프레임 반복되지 않게 멈춘다
            try:
                _side_cam_clear(st)
            except Exception:
                pass
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
            st["events"].append({"kind": "error", "msg": str(e)})

    if args.get("side_view"):
        sv = args["side_view"]
        cams = me.get_components_by_class(unreal.CameraComponent)
        if cams:
            st["cam"] = cams[0]
            st["cam_orig"] = (cams[0].get_editor_property("use_pawn_control_rotation"),
                              cams[0].get_editor_property("relative_location"), cams[0].get_editor_property("relative_rotation"))
            st["cam_args"] = (float(sv.get("dist_m", 4.0)), float(sv.get("angle", 90.0)), float(sv.get("height_m", 1.2)))
            st["cam"].set_editor_property("use_pawn_control_rotation", False)
            st["cam"].set_absolute(True, True, False)
            _side_cam_follow(st)
    # 기존에 살아 있던 이펙트는 새로 생긴 것으로 세지 않는다
    for owner, c in _fx_comps(world, me, mons):
        st["seen_fx"].add(c.get_path_name())
    st["handle"] = unreal.register_slate_post_tick_callback(tick)
    setattr(unreal, _REC, st)
    # 녹화 시작과 같은 프레임에 보낼 콘솔 명령(허용 목록은 world.pie_exec와 같은 규칙 — 여기서는 시험 명령만)
    done = []
    for cmd in args.get("cmds", []):
        if _allowed(cmd):
            unreal.SystemLibrary.execute_console_command(world, cmd, pc)
            done.append(cmd)
    return {"recording_s": st["dur"], "monsters": len(mons), "cmds": done}


def rec_read(args):
    st = getattr(unreal, _REC, None)
    if not st:
        return {"error": "녹화 없음"}
    return {"running": st["handle"] is not None, "frames": st["frames"], "last_montage_pos": st["last"].get("montage_pos"),
            "events": st["events"]}


def fonts(args):
    """PIE 월드의 글자 위젯(TextBlock·EditableText·EditableTextBox)이 쓰는 글꼴을 창(소유 UserWidget)별로 센다(D-270 UI1).
    반환: {창 클래스: {글꼴/글자체: 수}}, 공용 글꼴(F_Body·F_Title)이 아닌 것 목록(최대 30)."""
    out, other = {}, []
    for cls, getter in ((unreal.TextBlock, lambda w: w.get_editor_property("font")),
                        (unreal.EditableText, lambda w: w.get_editor_property("widget_style").get_editor_property("font")),
                        (unreal.EditableTextBox, lambda w: w.get_editor_property("widget_style").get_editor_property("text_style").get_editor_property("font"))):
        for w in unreal.ObjectIterator(cls):
            path = w.get_path_name()
            if "Transient" not in path or "WidgetTree" not in path or "Default__" in path:
                continue   # 실행 중 만든 위젯만(에셋 템플릿·CDO 제외 — 런타임 위젯은 GameInstance 아래 Transient)
            owner = w.get_typed_outer(unreal.UserWidget)
            oname = owner.get_class().get_name() if owner else "?"
            info = getter(w)
            fo = info.get_editor_property("font_object")
            key = f"{fo.get_name() if fo else None}/{info.get_editor_property('typeface_font_name')}"
            out.setdefault(oname, {}).setdefault(key, 0)
            out[oname][key] += 1
            if not (fo and fo.get_name() in ("F_Body", "F_Title")) and len(other) < 30:
                other.append({"owner": oname, "widget": w.get_name(), "font": key, "path": path})
    return {"by_owner": out, "not_shared": other}


def run(step, args):
    if step == "pp_fonts":
        return fonts(args)
    if step == "pp_probe":
        return probe(args)
    if step == "pp_rec_start":
        return rec_start(args)
    if step == "pp_rec_read":
        return rec_read(args)
    if step == "pp_aim":
        return aim(args)
    if step == "pp_cam_reset":
        w = _world(args)
        me = unreal.GameplayStatics.get_player_pawn(w, 0) if w else None
        cams = me.get_components_by_class(unreal.CameraComponent) if me else []
        if not cams:
            return {"error": "카메라 없음"}
        _cam_reset(cams[0], False)
        return {"camera": _m(cams[0].get_world_location())}
    if step == "pp_trail":
        # 몽타주의 궤적 노티파이(AnimNotifyState_Trail) 설정과, PIE 내 캐릭터 몸 메시에 그 소켓이 있는지(위치 포함)
        out = {}
        for p in args.get("montages", []):
            mon = unreal.load_asset(p)
            rows = []
            try:
                notifies = mon.get_editor_property("notifies") if mon else []
            except Exception as e:  # 5.8 Python은 Notifies를 막아 둠
                out[p] = f"읽기 불가: {e}"
                continue
            for ev_ in notifies:
                ns = ev_.get_editor_property("notify_state_class")
                if ns is None or "Trail" not in ns.get_class().get_name():
                    continue
                row = {"class": ns.get_class().get_name(), "start_s": round(ev_.get_editor_property("link_value"), 3),
                       "dur_s": round(ev_.get_editor_property("duration"), 3)}
                for k in ("ps_template", "first_socket_name", "second_socket_name", "width_scale_mode", "width_scale_curve",
                          "recycle_spawned_systems"):
                    try:
                        v = ns.get_editor_property(k)
                        row[k] = _name(v) if hasattr(v, "get_name") and not isinstance(v, (str, unreal.Name)) else str(v)
                    except Exception:
                        pass
                rows.append(row)
            out[p] = rows
        world = _world(args)
        if world:
            me = unreal.GameplayStatics.get_player_pawn(world, 0)
            if me:
                body = me.mesh
                socks = {}
                for s_ in args.get("sockets", []):
                    ok = body.does_socket_exist(s_)
                    socks[s_] = _m(body.get_socket_location(s_)) if ok else None
                out["body_mesh"] = _name(body.get_skeletal_mesh_asset()) if hasattr(body, "get_skeletal_mesh_asset") else None
                out["sockets"] = socks
                for at in _attachments(me):
                    if at["comp"] == "Equip_Weapon_Static":
                        out["weapon"] = at
                wc = [c for c in me.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == "Equip_Weapon_Static"]
                if wc:
                    org, ext = unreal.SystemLibrary.get_component_bounds(wc[0])[:2]
                    out["weapon_bounds_center"] = _m(org)
        return out
    if step == "pp_fx":
        # 파티클 에셋의 이미터별 반복 횟수·길이(loops 0 = 끝없이) — 한 번 만들고 멈추지 않는 재생 경로에 쓸 수 있는지 판정
        import json
        out = {}
        for p in args.get("paths", []):
            ps = unreal.load_asset(p)
            if ps is None:
                out[p] = "없음"
                continue
            d = json.loads(unreal.FxEditLibrary.describe_particle_system(ps))
            out[p] = [{"emitter": e.get("name"), "enabled": e["lods"][0].get("enabled") if e.get("lods") else None,
                       "required": e["lods"][0].get("required") if e.get("lods") else None} for e in d.get("emitters", [])]
        return out
    return {"error": f"알 수 없는 단계 {step}"}
