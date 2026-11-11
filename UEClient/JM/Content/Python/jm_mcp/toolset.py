"""jm_kit 전용 MCP 툴셋 + DataTable CSV 재임포트 — 정해진 함수만 노출한다(임의 코드 실행 도구는 두지 않음).

공식 MCP 서버(127.0.0.1:8000)는 인증이 없으므로 에디터 Python을 그대로 실행하는 도구는
등록하지 않는다(2026-11-05 결정). 새 작업이 필요하면 jm_kit에 함수를 만들고 여기에 감싼다.
일반 에셋·액터·BP·DT 작업은 공식 EditorToolset을 쓴다.
"""

import importlib
import json

import unreal
import toolset_registry
from toolset_registry.registration import Registration

from jm_kit import body_mask, fx_fix, fx_sheet, garment, glove_paint, icon, item_table, posed, preview, profiles, refs, retarget, skeleton_check, testbed


def _reload():
    """도구를 고친 뒤 에디터 재시작 없이 반영(의존 순서대로)."""
    for m in (profiles, skeleton_check, body_mask, garment, glove_paint, preview, icon, item_table, testbed, posed, refs, fx_sheet, fx_fix):
        importlib.reload(m)


def _dump(value) -> str:
    return json.dumps(value, ensure_ascii=False, default=str, indent=1)


def _vec(v: list[float]) -> tuple:
    if len(v) != 3:
        raise ValueError(f"좌표는 [x, y, z] 3개여야 함: {v}")
    return tuple(float(x) for x in v)


@unreal.uclass()
class KitTools(unreal.ToolsetDefinition):
    """JM 모듈형 캐릭터 도구(jm_kit): 뼈대 검사, 테스트 레벨 조립·촬영, 의상 맞춤, 몸 마스크."""

    # ── 공통 ──────────────────────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def reload_kit() -> str:
        """jm_kit 모듈을 다시 불러온다(Content/Python/jm_kit 수정 후).

        Returns:
            결과 문구.
        """
        _reload()
        return "jm_kit 다시 불러옴"

    @toolset_registry.tool_call
    @staticmethod
    def list_items() -> str:
        """items.json의 아이템 키와 슬롯·출력 경로 목록.

        Returns:
            JSON {키: {slot, out|attach}}.
        """
        items = garment.load_items()
        return _dump({k: {"slot": v.get("slot"), "out": v.get("out"), "attach": "attach" in v}
                      for k, v in items.items()})

    # ── 뼈대 검사 ─────────────────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def skeleton_self_test() -> str:
        """skeleton_check.self_test(): 키트 자체는 통과, 잘못된 메시는 실패해야 한다.

        Returns:
            JSON {ok, lines}.
        """
        ok, lines = skeleton_check.self_test()
        return _dump({"ok": ok, "lines": lines})

    @toolset_registry.tool_call
    @staticmethod
    def skeleton_check(mesh_path: str, body: str, slot: str | None = None) -> str:
        """장비 스켈레탈 메시의 본 개수·이름·순서·바인드 포즈를 키트 기준과 비교한다.

        Args:
            mesh_path: 검사할 스켈레탈 메시 경로(/Game/...).
            body: 체형 "M" 또는 "F".
            slot: 슬롯(top/bottom/shoes 등). 비우면 리더 기준.

        Returns:
            JSON 보고서(ok, errors 등).
        """
        return _dump(skeleton_check.check(mesh_path, body, slot or None))

    # ── 테스트 레벨 조립·촬영 ─────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def preview_setup_level() -> str:
        """테스트 레벨 /Game/Maps/TestLevel을 열거나(없으면 베이직 템플릿으로 생성) 조명·바닥·PlayerStart·GameMode Override(BP_TestGameMode)를 준비한다.

        Returns:
            결과 문구.
        """
        r = preview.setup_level()
        return _dump(r) if r is not None else "준비됨"

    @toolset_registry.tool_call
    @staticmethod
    def preview_clear(prefix: str) -> str:
        """라벨이 prefix로 시작하는 조립 액터를 지운다.

        Args:
            prefix: spawn 때 준 접두사.

        Returns:
            결과 문구.
        """
        r = preview.clear(prefix)
        return _dump(r) if r is not None else f"{prefix} 지움"

    @toolset_registry.tool_call
    @staticmethod
    def preview_spawn(prefix: str, body: str, location: list[float], outfit_mode: str,
                      outfit: dict[str, str] | None = None, anim: str | None = None,
                      extra_hidden: list[str] | None = None) -> str:
        """키트 캐릭터를 조립해 테스트 레벨에 놓는다(몸 마스크 자동 적용).

        Args:
            prefix: 액터 라벨 접두사(예 "KM").
            body: 체형 "M" 또는 "F".
            location: [x, y, z] cm.
            outfit_mode: "kit"(키트 기본 의상), "none"(맨몸), "custom"(outfit 사용).
            outfit: custom일 때 {슬롯: items.json 키 또는 /Game 메시 경로}.
            anim: 반복 재생 애니메이션 — ANIM 키(idle/jog/walk/attack1~3) 또는 경로.
            extra_hidden: 통째로 숨길 부위 키(예 ["hair"]).

        Returns:
            리더 액터 라벨.
        """
        mode = outfit_mode.lower()
        if mode == "kit":
            o = "kit"
        elif mode == "none":
            o = None
        elif mode == "custom":
            o = dict(outfit or {})
        else:
            raise ValueError(f"outfit_mode는 kit/none/custom: {outfit_mode}")
        a = preview.ANIM.get(anim, anim) if anim else None
        actor = preview.spawn(prefix, body, _vec(location), outfit=o,
                              extra_hidden=tuple(extra_hidden or ()), anim=a)
        return actor.get_actor_label() if actor else "(액터 없음)"

    @toolset_registry.tool_call
    @staticmethod
    def preview_shot(name: str, camera: list[float], target: list[float], fov: float | None = None,
                     in_game: bool | None = None) -> str:
        """SceneCapture로 촬영해 Saved/MCShots/<name>.png로 저장한다.

        Args:
            name: 파일 이름(확장자 없이).
            camera: 카메라 위치 [x, y, z].
            target: 바라볼 점 [x, y, z].
            fov: 화각(기본 40).
            in_game: True면 Simulate 중인 게임 월드 촬영(preview_start_simulate 다음 호출에서).

        Returns:
            PNG 절대 경로.
        """
        f = 40.0 if fov is None else fov
        if in_game:
            return preview.shot_game(name, _vec(camera), _vec(target), fov=f)
        return preview.shot(name, _vec(camera), _vec(target), fov=f)

    @toolset_registry.tool_call
    @staticmethod
    def preview_look_at(camera: list[float], target: list[float]) -> str:
        """에디터 뷰포트 카메라를 옮긴다(사용자가 같은 장면을 보도록).

        Args:
            camera: 카메라 위치 [x, y, z].
            target: 바라볼 점 [x, y, z].

        Returns:
            결과 문구.
        """
        preview.look_at(_vec(camera), _vec(target))
        return "뷰포트 이동"

    @toolset_registry.tool_call
    @staticmethod
    def preview_start_simulate() -> str:
        """Simulate 시작(애니메이션 재생 확인용, 로그인 흐름 없음).

        Returns:
            결과 문구.
        """
        preview.start_simulate()
        return "Simulate 요청"

    @toolset_registry.tool_call
    @staticmethod
    def preview_stop_simulate() -> str:
        """Simulate/PIE 종료.

        Returns:
            결과 문구.
        """
        preview.stop_simulate()
        return "종료 요청"

    # ── FX 미리보기 시트(FX_CATALOG 3단계) ────────────────
    @toolset_registry.tool_call
    @staticmethod
    def fx_sheet_start(category: str | None = None, keys: str | None = None, times: str | None = None) -> str:
        """Content/FX 파티클을 Simulate 중 게임 월드에 하나씩 재생해 시점별로 찍는다(프레임마다 진행 — 바로 반환, fx_sheet_status로 확인).

        먼저 preview_setup_level → preview_spawn("KM", "M", [0,0,0], "kit") → preview_start_simulate 를 하고 다음 호출에서 부른다.
        결과: Saved/MCShots/fx_<이름>_t030.png·t100.png, 분류별 시트 fx_sheet_<분류>.png, fx_sheet_status.json.

        Args:
            category: 분류 하나(Trail/Hit/Proj/Cast/AoE/Buff/Life). 비우면 전부.
            keys: 에셋 이름 또는 경로를 쉼표로(예 "P_Hit_Slash,P_AoE_Smash"). 비우면 분류 전부.
            times: 촬영 시점(초)을 쉼표로(기본 "0.3,1.0").

        Returns:
            JSON {count, times, status}.
        """
        ks = [k.strip() for k in keys.split(",") if k.strip()] if keys else None
        ts = tuple(float(t) for t in times.split(",")) if times else fx_sheet.TIMES
        return _dump(fx_sheet.start(keys=ks, category=category or None, times=ts))

    @toolset_registry.tool_call
    @staticmethod
    def fx_sheet_status() -> str:
        """fx_sheet_start 진행 상태(state running/done, progress, sheets, 에셋별 bb·샷 경로·오류).

        Returns:
            JSON.
        """
        return _dump(fx_sheet.status())

    @toolset_registry.tool_call
    @staticmethod
    def fx_sheet_stop() -> str:
        """진행 중인 fx_sheet 작업을 중단하고 지금까지 찍은 것으로 시트를 만든다.

        Returns:
            JSON(status와 같음).
        """
        return _dump(fx_sheet.stop())

    # ── FX 손질(FX_CATALOG 4단계) ──────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def fx_inspect(keys: str | None = None, category: str | None = None) -> str:
        """Content/FX 파티클의 구조 요약(jm_kit.fx_fix.inspect): 이미터별 이름·LOD 활성·TypeData·모듈·본 이름(매니퀸에 없는 본은 bones_missing)·메시·머티리얼·지연.

        Args:
            keys: 에셋 이름 또는 경로를 쉼표로(예 "P_Life_Death,P_Debuff_Burn"). 비우면 category 또는 전부.
            category: 분류 하나(Trail/Hit/Proj/Cast/AoE/Buff/Life).

        Returns:
            JSON {경로: {emitters: [...], lod_method}}.
        """
        ks = [k.strip() for k in keys.split(",") if k.strip()] if keys else None
        return _dump(fx_fix.inspect(keys=ks, category=category or None))

    @toolset_registry.tool_call
    @staticmethod
    def fx_apply(ops_json: str, save: bool | None = None) -> str:
        """정해진 편집을 파티클에 적용하고 저장한다(jm_kit.fx_fix.apply). 임의 코드가 아니라 op 종류만 허용:
        delete_emitter·disable_emitter·rename_bones(map)·drop_bones(names)·set_bone_source(bones)·set_mesh(mesh). emitter는 이름 또는 "#인덱스".

        Args:
            ops_json: JSON 배열 문자열. 예 [{"asset":"P_Debuff_Burn","op":"rename_bones","map":{"chest_playe":"spine_03"}}].
            save: 저장 여부(기본 True).

        Returns:
            JSON {경로: [결과...], _saved, _errors}.
        """
        ops = json.loads(ops_json)
        return _dump(fx_fix.apply(ops, save=True if save is None else save))

    @toolset_registry.tool_call
    @staticmethod
    def fx_weapon_socket_points(item_key: str | None = None, body: str | None = None, fracs: str | None = None) -> str:
        """무기 메시 긴 축의 지점들을 손 본 로컬 좌표로(jm_kit.fx_fix.weapon_socket_points) — AnimTrail 소켓 FX_Weapon_Bottom/Top 추가용.

        Args:
            item_key: items.json 무기 키(기본 Cloth_Sword).
            body: 체형 "M"/"F"(기본 M).
            fracs: 지점 비율을 쉼표로(기본 "0.22,1.0" — 날 시작·칼끝).

        Returns:
            JSON {socket, axis, length_cm, points_bone_local: {비율: [x,y,z]}}.
        """
        fs = tuple(float(f) for f in fracs.split(",")) if fracs else (0.22, 1.0)
        return _dump(fx_fix.weapon_socket_points(item_key or "Cloth_Sword", body or "M", fs))

    @toolset_registry.tool_call
    @staticmethod
    def fx_trail_notify(montage_path: str, fx_key: str | None = None, start_sec: float | None = None, duration_sec: float | None = None,
                        remove: bool | None = None) -> str:
        """몽타주의 AnimNotifyState_Trail을 모두 지우고 fx_key 파티클로 하나 단다(jm_kit.fx_fix.set_trail_notify, 소켓 FX_Weapon_Bottom→Top). TRAIL 확인용.

        Args:
            montage_path: 몽타주 경로(예 /Game/Character/Mannequin/Anims/OneHandSword/AM_OneHandSword_Attack).
            fx_key: FX 이름 또는 경로(예 P_Trail_Sword_Basic). remove면 생략.
            start_sec: 시작 초(기본 0).
            duration_sec: 길이 초(기본 0 = 끝까지).
            remove: True면 Trail 노티파이 제거만.

        Returns:
            결과 문구.
        """
        return fx_fix.set_trail_notify(montage_path, fx_key or "", start_sec or 0.0, duration_sec or 0.0, bool(remove))

    # ── 테스트 플레이어(서버 없는 PIE) ────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def testbed_ensure(reset: bool | None = None) -> str:
        """BP_TestPlayer(부모 C++ TestPlayer)·BP_TestGameMode를 만든다(있으면 그대로). 그 뒤 preview_setup_level로 TestLevel에 GameMode를 지정한다.

        Args:
            reset: True면 입력 에셋·기본 장비(천 세트 + 천 활)·MaxWalkSpeed를 다시 넣는다.

        Returns:
            JSON 요약(장비·덮어쓰기 몽타주·GameMode 폰).
        """
        importlib.reload(testbed)
        return _dump(testbed.ensure_blueprints(reset is True))

    @toolset_registry.tool_call
    @staticmethod
    def testbed_set_override(montage_path: str | None = None) -> str:
        """BP_TestPlayer 좌클릭(슬롯 0)에 SkillTable 대신 재생할 몽타주를 지정한다.

        Args:
            montage_path: 몽타주 경로. 비우면 해제(SkillTable 몽타주 사용).

        Returns:
            JSON 요약.
        """
        importlib.reload(testbed)
        return _dump(testbed.set_override(montage_path or None))

    @toolset_registry.tool_call
    @staticmethod
    def testbed_set_loadout(items: str, player_type: int | None = None) -> str:
        """BP_TestPlayer 시작 장비와 체형을 바꾼다.

        Args:
            items: ItemTable ID 7개(또는 6개)를 쉼표로(EquipPart 순서 머리·상의·하의·장갑·장화·무기·보조손, 빈 슬롯 0).
            player_type: 1 MALE, 2 FEMALE. 비우면 그대로.

        Returns:
            JSON 요약.
        """
        importlib.reload(testbed)
        return _dump(testbed.set_loadout(_csv_list(items), player_type))

    # ── 의상 맞춤 ─────────────────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def garment_import_source(fbx_path: str, item_key: str) -> str:
        """VARCO FBX를 items.json의 source 위치로 가져오고 이름을 규칙대로 바꾼다.

        Args:
            fbx_path: FBX 절대 경로(짧은 경로 권장).
            item_key: items.json 키.

        Returns:
            정적 메시 경로.
        """
        return str(garment.import_source(fbx_path, item_key))

    @toolset_registry.tool_call
    @staticmethod
    def garment_build_item(item_key: str, bodies: list[str] | None = None) -> str:
        """items.json 아이템을 체형별로 만든다(키트 의상 복제 후 형상·가중치 교체, 정적 아이템은 부착값).

        Args:
            item_key: items.json 키.
            bodies: 체형 목록(기본 ["M", "F"]).

        Returns:
            JSON {체형: 보고서(skeleton_ok, limbs_ok 등)}.
        """
        return _dump(garment.build_item(item_key, bodies=tuple(bodies or ("M", "F"))))

    @toolset_registry.tool_call
    @staticmethod
    def garment_measure(item_path: str, body: str) -> str:
        """장비 메시가 몸 안으로 들어간 정점 %, 뜬 정점 %, 거리 p50/p95.

        Args:
            item_path: 장비 스켈레탈 메시 경로.
            body: 체형 "M" 또는 "F".

        Returns:
            JSON 측정값.
        """
        return _dump(garment.measure(item_path, body))

    # ── 자세별 측정(MC4.2e) ───────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def posed_run(body: str, loadout: str, poses: str, shots: bool | None = None, label: str | None = None) -> str:
        """동작 프레임으로 자세를 잡고 장갑·무기 관통, 상의 어깨 뭉침을 잰다(jm_kit.posed.run). 촬영은 Saved/MCShots.

        Args:
            body: 체형 "M" 또는 "F".
            loadout: JSON {"top","gloves","bottom","boots": 아이템 키 또는 메시 경로, "bow"/"sword": 무기 아이템 키}.
            poses: 쉼표 구분 포즈 키(posed.POSES — bow_idle, bow_draw, bow_aim, sword_idle, sword_windup, sword_swing, sword_skill_up).
            shots: 촬영 여부(기본 True).
            label: 촬영 파일 이름 구분자.

        Returns:
            JSON {포즈: {hands, top, shots}}.
        """
        keys = [k.strip() for k in poses.split(",") if k.strip()]
        return _dump(posed.run(body, json.loads(loadout), keys, shots is not False, label or ""))

    @toolset_registry.tool_call
    @staticmethod
    def posed_grip_search(body: str, weapon_key: str, gloves_key: str, pose_key: str, span: float | None = None,
                          step: float | None = None) -> str:
        """무기 쥔 자세에서 부착 위치를 손 본 기준으로 옮겨 보며 관통이 적고 손에 닿는 위치를 찾는다(jm_kit.posed.grip_search).

        Args:
            body: 체형 "M" 또는 "F".
            weapon_key: 무기 아이템 키(예 Cloth_Sword).
            gloves_key: 장갑 아이템 키(예 Cloth_Gloves).
            pose_key: 쥔 자세 포즈 키(예 sword_idle, bow_idle).
            span: 탐색 반경 cm(기본 3).
            step: 거친 격자 간격 cm(기본 1).

        Returns:
            JSON {current, best: [{offset(손 본 로컬 cm), glove_in_weapon, weapon_in_glove, touch, score}]}.
        """
        return _dump(posed.grip_search(body, weapon_key, gloves_key, pose_key, float(span or 3.0), float(step or 1.0)))

    @toolset_registry.tool_call
    @staticmethod
    def refs_find(roots: str | None = None, forbidden: str | None = None, skip: str | None = None) -> str:
        """루트 에셋·폴더에서 의존을 끝까지 따라가 금지 경로(기본 Paragon·Greystone·Sparrow)에 닿는 사슬을 찾는다(jm_kit.refs.find).

        Args:
            roots: 쉼표 구분 패키지·폴더(기본 = 플레이어 쪽 루트 refs.PLAYER_ROOTS).
            forbidden: 쉼표 구분 금지 접두사(기본 refs.PARAGON_PREFIXES).
            skip: 쉼표 구분 — 따라가지 않을 접두사.

        Returns:
            JSON {roots, visited, hits: [{asset, class, chain}]}.
        """
        sp = lambda v, d: tuple(x.strip() for x in v.split(",") if x.strip()) if v else d
        return _dump(refs.find(sp(roots, refs.PLAYER_ROOTS), sp(forbidden, refs.PARAGON_PREFIXES), sp(skip, ())))

    # ── 몸 마스크 ─────────────────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def body_mask_build(body: str) -> str:
        """키트 몸 UV에 영역·거리 마스크를 래스터화해 T_BodyMask_<body>를 다시 만든다.

        Args:
            body: 체형 "M" 또는 "F".

        Returns:
            JSON 결과.
        """
        return _dump(body_mask.build_mask(body))

    @toolset_registry.tool_call
    @staticmethod
    def body_mask_build_material() -> str:
        """M_Body_Masked와 MI_Body_<M|F>를 다시 만든다.

        Returns:
            JSON 결과.
        """
        return _dump(body_mask.build_material())

    # ── 아이콘 ────────────────────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def icon_shoot(item_keys: str | None = None, import_assets: bool | None = None) -> str:
        """장비 아이콘을 찍는다(MC2.4): 512×512 투명 PNG → SourceArt/Icons, 아이템 폴더에 T_Icon_<Item>. 확인 시트는 Saved/MCShots/icons_sheet.png.

        Args:
            item_keys: items.json 키를 쉼표로 구분(예 "Plate_Top,Plate_Helm", 기본 전부). 선택 인자 list[str] | None은 변환 실패해서 문자열로 받는다.
            import_assets: 텍스처 에셋으로 가져올지(기본 True).

        Returns:
            JSON {키: {png, asset, fill, crop_px}, _sheet}.
        """
        keys = [k.strip() for k in item_keys.split(",") if k.strip()] if item_keys else None
        return _dump(icon.shoot(keys, import_assets=True if import_assets is None else import_assets))

    # ── 데이터 테이블 생성 ────────────────────────────────
    @toolset_registry.tool_call
    @staticmethod
    def item_table_export(write: bool | None = None) -> str:
        """ItemTable.csv 외형 열(AttachSocket·AttachM·AttachF·Mask·HideHair)과 BodyTable.csv를 items.json·kit_profiles.json에서 생성한다(MC4.1).

        CSV만 쓴다 — 이후 DataTableReimportTools.reimport_csv로 DT를 다시 채운다.

        Args:
            write: False면 쓰지 않고 바뀔 값만 돌려준다(기본 True).

        Returns:
            JSON {items: {csv, rows, changed, written}, bodies: {csv, rows, changed, written}}.
        """
        return _dump(item_table.export(write=write is not False))


@unreal.uclass()
class DataTableReimportTools(unreal.ToolsetDefinition):
    """Common/DataTable CSV를 DataTable에 다시 가져오거나(공식 DataTableTools에 Reimport가 없어서) 새로 임포트한다."""

    @toolset_registry.tool_call
    @staticmethod
    def reimport_csv(data_table_path: str, save: bool | None = None) -> str:
        """DataTable을 임포트 원본 CSV(asset_import_data)에서 다시 채운다.

        Args:
            data_table_path: DataTable 경로(예 /Game/Data/DT_SkillTable).
            save: 저장 여부(기본 True).

        Returns:
            JSON {csv, rows_before, rows_after, saved}.
        """
        dt = unreal.load_asset(data_table_path)
        if not isinstance(dt, unreal.DataTable):
            raise ValueError(f"DataTable이 아님: {data_table_path}")
        csv = dt.get_editor_property("asset_import_data").get_first_filename()
        if not csv or not csv.lower().endswith(".csv"):
            raise RuntimeError(f"임포트 원본 CSV가 없음: {csv!r}")
        before = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(dt, csv):
            raise RuntimeError(f"CSV 채우기 실패(Output Log 확인): {csv}")
        after = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
        saved = unreal.EditorAssetLibrary.save_loaded_asset(dt, False) if save is not False else False
        return _dump({"csv": csv, "rows_before": before, "rows_after": after, "saved": saved})

    @toolset_registry.tool_call
    @staticmethod
    def create_from_csv(table_name: str, row_struct: str) -> str:
        """Common/DataTable/<table_name>.csv 를 새 DataTable /Game/Data/DT_<table_name> 으로 임포트한다.

        임포트 원본(asset_import_data)이 CSV로 기록되므로 이후 reimport_csv로 다시 채울 수 있다.
        이미 있으면 덮어쓰지 않고 실패한다(기존 테이블은 reimport_csv 사용).

        Args:
            table_name: CSV 이름(확장자 없이, 예 ItemTable). 영문·숫자만.
            row_struct: Row 구조체 경로(예 /Script/JM.ItemTableRow).

        Returns:
            JSON {csv, asset, row_struct, rows, saved}.
        """
        if not table_name.isalnum():
            raise ValueError(f"table_name은 영문·숫자만: {table_name!r}")
        if not row_struct.startswith("/Script/JM."):
            raise ValueError(f"row_struct는 JM 모듈 구조체만: {row_struct!r}")
        csv = unreal.Paths.convert_relative_path_to_full(
            unreal.Paths.combine([unreal.Paths.project_dir(), "../../Common/DataTable", table_name + ".csv"]))
        if not unreal.Paths.file_exists(csv):
            raise FileNotFoundError(csv)
        asset = f"/Game/Data/DT_{table_name}"
        if unreal.EditorAssetLibrary.does_asset_exist(asset):
            raise RuntimeError(f"이미 있음(reimport_csv 사용): {asset}")
        struct = unreal.load_object(None, row_struct)
        if struct is None:
            raise ValueError(f"Row 구조체를 찾을 수 없음: {row_struct}")

        settings = unreal.CSVImportSettings()
        settings.set_editor_property("import_type", unreal.CSVImportType.ECSV_DATA_TABLE)
        settings.set_editor_property("import_row_struct", struct)
        factory = unreal.CSVImportFactory()
        factory.set_editor_property("automated_import_settings", settings)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", csv)
        task.set_editor_property("destination_path", "/Game/Data")
        task.set_editor_property("destination_name", f"DT_{table_name}")
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        task.set_editor_property("factory", factory)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

        dt = unreal.load_asset(asset)
        if not isinstance(dt, unreal.DataTable):
            raise RuntimeError(f"임포트 실패(Output Log 확인): {csv}")
        rows = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
        saved = unreal.EditorAssetLibrary.save_loaded_asset(dt, False)
        return _dump({"csv": csv, "asset": asset, "row_struct": row_struct, "rows": rows, "saved": saved})


def _csv_list(text: str) -> list[str]:
    return [t.strip() for t in text.split(",") if t.strip()]


@unreal.uclass()
class AnimBuilderTools(unreal.ToolsetDefinition):
    """MC4.2 애니메이션 에셋 빌더(C++ UAnimAssetBuilder, JMEditorTools): ALI·레이어 AnimBP·몽타주 생성, AnimBP 덤프·재배선.

    공식 MCP의 그래프 DSL이 AnimGraph를 다루지 못해 C++ 함수로 만들고 여기서 감싼다. 결과 문자열이 "ERROR:"로 시작하면 실패(저장 안 함).
    """

    @toolset_registry.tool_call
    @staticmethod
    def anim_dump(blueprint_path: str) -> str:
        """AnimBP의 그래프 전체(AnimGraph·스테이트 머신·스테이트·전이·레이어)를 노드·핀·연결까지 텍스트로 덤프한다.

        Args:
            blueprint_path: AnimBP 경로(예 /Game/Character/Mannequin/Anims/ABP_PlayerBase).

        Returns:
            덤프 텍스트.
        """
        return unreal.AnimAssetBuilder.dump_anim_blueprint(blueprint_path)

    @toolset_registry.tool_call
    @staticmethod
    def anim_create_layer_interface(package_path: str, layer_names: str, layer_has_input_pose: str) -> str:
        """Animation Layer Interface(ALI_*)를 만들고 레이어를 추가한다.

        Args:
            package_path: 예 /Game/Character/Mannequin/Anims/ALI_Weapon.
            layer_names: 레이어 이름을 쉼표로(예 "LocomotionLayer,UpperBodyLayer"). 호스트 AnimBP의 스테이트·그래프 이름과 겹치면 컴파일이 실패한다.
            layer_has_input_pose: 레이어별 입력 포즈 여부를 쉼표로(예 "0,1").

        Returns:
            결과 텍스트.
        """
        names = _csv_list(layer_names)
        flags = [t in ("1", "true", "True") for t in _csv_list(layer_has_input_pose)]
        return unreal.AnimAssetBuilder.create_layer_interface(package_path, names, flags)

    @toolset_registry.tool_call
    @staticmethod
    def anim_create_layer_blueprint(package_path: str, interface_path: str, skeleton_path: str, parent_class_path: str) -> str:
        """인터페이스를 구현하는 레이어 AnimBP(ABP_Layer_*)를 만든다.

        Args:
            package_path: 예 /Game/Character/Mannequin/Anims/Unarmed/ABP_Layer_Unarmed.
            interface_path: ALI 경로.
            skeleton_path: 스켈레톤 경로.
            parent_class_path: 부모 클래스(예 /Script/JM.JMAnimInstance).

        Returns:
            결과 텍스트.
        """
        return unreal.AnimAssetBuilder.create_layer_anim_blueprint(package_path, interface_path, skeleton_path, parent_class_path)

    @toolset_registry.tool_call
    @staticmethod
    def anim_set_layer_blendspace(blueprint_path: str, layer_name: str, blendspace_path: str,
                                  x_variable: str | None = None, y_variable: str | None = None) -> str:
        """레이어 그래프를 "블렌드스페이스 플레이어 → 결과"로 채운다. X/Y는 멤버 변수 Get 노드에 잇는다.

        Args:
            blueprint_path: 레이어 AnimBP 경로.
            layer_name: 레이어(그래프) 이름.
            blendspace_path: 블렌드스페이스 경로.
            x_variable: X 핀에 이을 변수 이름(예 GroundSpeed). 비우면 연결 안 함.
            y_variable: Y 핀에 이을 변수 이름(예 Direction). 비우면 연결 안 함.

        Returns:
            결과 텍스트.
        """
        return unreal.AnimAssetBuilder.set_layer_blend_space(blueprint_path, layer_name, blendspace_path, x_variable or "", y_variable or "")

    @toolset_registry.tool_call
    @staticmethod
    def anim_create_blendspace_1d(package_path: str, skeleton_path: str, axis_name: str, axis_min: float, axis_max: float,
                                  sequence_paths: str, sample_values: str, rate_scales: str | None = None) -> str:
        """1D 블렌드스페이스를 만든다(MC4.2c 무기 타입 이동).

        Args:
            package_path: 예 /Game/Character/Mannequin/Anims/OneHandSword/BS_OneHandSword_Locomotion.
            skeleton_path: 스켈레톤 경로.
            axis_name: 축 이름(예 Speed).
            axis_min: 축 최소.
            axis_max: 축 최대.
            sequence_paths: 샘플 시퀀스 경로를 쉼표로.
            sample_values: 샘플 위치를 쉼표로(시퀀스와 같은 개수).
            rate_scales: 샘플 재생 배율을 쉼표로(비우면 모두 1).

        Returns:
            결과 텍스트.
        """
        values = [float(v) for v in _csv_list(sample_values)]
        rates = [float(v) for v in _csv_list(rate_scales)] if rate_scales else []
        fn = next(getattr(unreal.AnimAssetBuilder, n) for n in ("create_blend_space1d", "create_blend_space_1d", "create_blend_space1_d")
                  if hasattr(unreal.AnimAssetBuilder, n))  # 숫자 포함 이름의 Python 변환 차이 대비
        return fn(package_path, skeleton_path, axis_name, float(axis_min), float(axis_max),
                                                            _csv_list(sequence_paths), values, rates)

    @toolset_registry.tool_call
    @staticmethod
    def anim_set_layer_blendspace_override(blueprint_path: str, layer_name: str, blendspace_path: str, x_variable: str,
                                           overlay_blendspace_path: str, overlay_x_variable: str, overlay_y_variable: str,
                                           branch_bone: str, alpha_variable: str) -> str:
        """레이어 그래프 = 블렌드스페이스(기본) + 덮기 블렌드스페이스를 branch_bone 아래에만 레이어 블렌드(메시 공간 회전) → 결과.

        Args:
            blueprint_path: 레이어 AnimBP 경로.
            layer_name: 레이어(그래프) 이름.
            blendspace_path: 기본 블렌드스페이스.
            x_variable: 기본 X 변수(예 GroundSpeed).
            overlay_blendspace_path: 덮기 블렌드스페이스(예 맨손 BS_Idle_Walk_Run).
            overlay_x_variable: 덮기 X 변수(빈 문자열이면 연결 안 함).
            overlay_y_variable: 덮기 Y 변수(빈 문자열이면 연결 안 함).
            branch_bone: 덮을 본(예 upperarm_l).
            alpha_variable: 블렌드 가중치 변수(예 LeftArmOverrideAlpha).

        Returns:
            결과 텍스트.
        """
        return unreal.AnimAssetBuilder.set_layer_blend_space_with_override(blueprint_path, layer_name, blendspace_path, x_variable,
                                                                            overlay_blendspace_path, overlay_x_variable, overlay_y_variable,
                                                                            branch_bone, alpha_variable)

    @toolset_registry.tool_call
    @staticmethod
    def anim_set_layer_passthrough(blueprint_path: str, layer_name: str) -> str:
        """레이어 그래프를 "입력 포즈 → 결과" 통과로 채운다.

        Args:
            blueprint_path: 레이어 AnimBP 경로.
            layer_name: 레이어(그래프) 이름.

        Returns:
            결과 텍스트.
        """
        return unreal.AnimAssetBuilder.set_layer_pass_through(blueprint_path, layer_name)

    @toolset_registry.tool_call
    @staticmethod
    def anim_create_montage(sequence_path: str, package_path: str, slot_name: str,
                            auto_blend_out: bool | None = None, skill_hit_time_sec: float | None = None) -> str:
        """시퀀스로 몽타주를 만든다.

        Args:
            sequence_path: 애니메이션 시퀀스 경로.
            package_path: 만들 몽타주 경로(예 /Game/Character/Mannequin/Anims/Unarmed/AM_Unarmed_Attack).
            slot_name: 슬롯(UpperBody 또는 DefaultSlot).
            auto_blend_out: Auto Blend Out(기본 True, 사망은 False).
            skill_hit_time_sec: AnimNotify_SkillHit를 달 시각(초). 비우면 달지 않음.

        Returns:
            결과 텍스트.
        """
        hit = -1.0 if skill_hit_time_sec is None else float(skill_hit_time_sec)
        return unreal.AnimAssetBuilder.create_montage(sequence_path, package_path, slot_name, auto_blend_out is not False, hit)

    @toolset_registry.tool_call
    @staticmethod
    def anim_create_montage_multi(sequence_paths: str, package_path: str, slot_name: str,
                                  auto_blend_out: bool | None = None, skill_hit_time_sec: float | None = None) -> str:
        """여러 시퀀스를 차례로 이어 몽타주 하나를 만든다(MC4.2d-2 활 기본 공격 = 당기기 + 놓기).

        Args:
            sequence_paths: 시퀀스 경로를 쉼표로(재생 순서).
            package_path: 만들 몽타주 경로.
            slot_name: 슬롯(UpperBody 또는 DefaultSlot).
            auto_blend_out: Auto Blend Out(기본 True).
            skill_hit_time_sec: AnimNotify_SkillHit 시각(몽타주 전체 기준 초). 비우면 달지 않음.

        Returns:
            결과 텍스트.
        """
        hit = -1.0 if skill_hit_time_sec is None else float(skill_hit_time_sec)
        return unreal.AnimAssetBuilder.create_montage_from_sequences(_csv_list(sequence_paths), package_path, slot_name, auto_blend_out is not False, hit)

    @toolset_registry.tool_call
    @staticmethod
    def anim_create_blendspace_2d(package_path: str, skeleton_path: str, x_axis_name: str, x_min: float, x_max: float,
                                  y_axis_name: str, y_min: float, y_max: float, sequence_paths: str, x_values: str, y_values: str,
                                  rate_scales: str | None = None) -> str:
        """2D 블렌드스페이스를 만든다(MC4.2d-2 조준 걷기: X = Direction, Y = Speed).

        Args:
            package_path: 예 /Game/Character/Mannequin/Anims/Bow/BS_Bow_AimWalk.
            skeleton_path: 스켈레톤 경로.
            x_axis_name: X 축 이름(예 Direction).
            x_min: X 최소.
            x_max: X 최대.
            y_axis_name: Y 축 이름(예 Speed).
            y_min: Y 최소.
            y_max: Y 최대.
            sequence_paths: 샘플 시퀀스 경로를 쉼표로.
            x_values: 샘플 X를 쉼표로.
            y_values: 샘플 Y를 쉼표로.
            rate_scales: 샘플 재생 배율을 쉼표로(비우면 모두 1).

        Returns:
            결과 텍스트.
        """
        xs = [float(v) for v in _csv_list(x_values)]
        ys = [float(v) for v in _csv_list(y_values)]
        rates = [float(v) for v in _csv_list(rate_scales)] if rate_scales else []
        fn = next(getattr(unreal.AnimAssetBuilder, n) for n in ("create_blend_space2d", "create_blend_space_2d", "create_blend_space2_d")
                  if hasattr(unreal.AnimAssetBuilder, n))  # 숫자 포함 이름의 Python 변환 차이 대비
        return fn(package_path, skeleton_path, x_axis_name, float(x_min), float(x_max), y_axis_name, float(y_min), float(y_max),
                  _csv_list(sequence_paths), xs, ys, rates)

    @toolset_registry.tool_call
    @staticmethod
    def anim_set_layer_blendspaces_by_int(blueprint_path: str, layer_name: str, int_variable: str, blendspace_paths: str,
                                          x_variables: str, y_variables: str, blend_time_sec: float | None = None) -> str:
        """레이어 그래프 = Blend Poses by Int(int_variable) ← 블렌드스페이스 플레이어 N개 → 결과(MC4.2d-2 WeaponStance).

        Args:
            blueprint_path: 레이어 AnimBP 경로.
            layer_name: 레이어(그래프) 이름.
            int_variable: 고르는 정수 변수(예 WeaponStance).
            blendspace_paths: 포즈 순서대로 블렌드스페이스 경로를 쉼표로.
            x_variables: 포즈별 X 변수를 쉼표로(빈 칸이면 연결 안 함, 예 "GroundSpeed,GroundSpeed,Direction").
            y_variables: 포즈별 Y 변수를 쉼표로(빈 칸 허용, 예 ",,GroundSpeed").
            blend_time_sec: 포즈 전환 블렌드 시간(기본 0.2).

        Returns:
            결과 텍스트.
        """
        bs = _csv_list(blendspace_paths)
        xs = [s.strip() for s in x_variables.split(",")]
        ys = [s.strip() for s in y_variables.split(",")]
        return unreal.AnimAssetBuilder.set_layer_blend_spaces_by_int(blueprint_path, layer_name, int_variable, bs, xs, ys,
                                                                     0.2 if blend_time_sec is None else float(blend_time_sec))

    @toolset_registry.tool_call
    @staticmethod
    def anim_measure_bone_reach(sequence_path: str, bone_name: str, samples: int | None = None) -> str:
        """시퀀스를 샘플링해 본의 루트 기준 위치(옆 X·앞 Y·위 Z) 표를 돌려준다(타격 시점 고르기용).

        Args:
            sequence_path: 애니메이션 시퀀스 경로.
            bone_name: 본 이름(예 hand_r).
            samples: 샘플 수(기본 40).

        Returns:
            탭 구분 표.
        """
        return unreal.AnimAssetBuilder.measure_bone_reach(sequence_path, bone_name, int(samples or 40))

    @toolset_registry.tool_call
    @staticmethod
    def anim_rewire_player_base(blueprint_path: str, interface_path: str, locomotion_layer: str, upper_body_layer: str,
                                locomotion_state: str, land_state: str | None = None, save: bool | None = None) -> str:
        """ABP_PlayerBase 재배선: Locomotion 스테이트 = 레이어, 결과 앞에 UpperBody 레이어 + 슬롯 2개, Land→Locomotion 자동 전이.

        Args:
            blueprint_path: ABP_PlayerBase 경로.
            interface_path: ALI 경로.
            locomotion_layer: 이동 레이어 이름.
            upper_body_layer: 상체 레이어 이름.
            locomotion_state: 이동 스테이트 이름.
            land_state: 착지 스테이트 이름(비우면 전이 수정 안 함).
            save: 저장 여부(기본 False = 컴파일까지만 미리보기).

        Returns:
            결과 텍스트.
        """
        return unreal.AnimAssetBuilder.rewire_player_base(blueprint_path, interface_path, locomotion_layer, upper_body_layer,
                                                           locomotion_state, land_state or "", save is True)


@unreal.uclass()
class RetargetTools(unreal.ToolsetDefinition):
    """MC4.2b Mixamo → 매니퀸 리타깃(jm_kit.retarget): Y Bot·애니메이션 FBX 가져오기, IK 리그·리타기터 생성, 일괄 리타깃."""

    @toolset_registry.tool_call
    @staticmethod
    def retarget_import_source_mesh(fbx_path: str) -> str:
        """Mixamo Y Bot(With Skin, T-Pose) FBX를 /Game/Character/Mixamo/SKM_YBot으로 가져온다.

        Args:
            fbx_path: FBX 절대 경로(짧은 경로, 예 C:/.../UEClient/JM/SourceArt/Mixamo/Y Bot.fbx).

        Returns:
            JSON {mesh, skeleton, imported}.
        """
        importlib.reload(retarget)
        return _dump(retarget.import_source_mesh(fbx_path))

    @toolset_registry.tool_call
    @staticmethod
    def retarget_import_anims(fbx_paths: str, pack: str) -> str:
        """애니메이션 FBX(Without Skin)들을 Y Bot 스켈레톤으로 가져온다 → /Game/Character/Mixamo/Anims/<pack>/MX_<이름>.

        Args:
            fbx_paths: FBX 절대 경로를 쉼표로.
            pack: 무기 타입 폴더 이름(Bow·Sword).

        Returns:
            JSON 시퀀스 경로 목록.
        """
        importlib.reload(retarget)
        return _dump(retarget.import_anims(_csv_list(fbx_paths), pack))

    @toolset_registry.tool_call
    @staticmethod
    def retarget_ensure_rigs(rebuild: bool | None = None) -> str:
        """IK_Mixamo·IK_Mannequin·RTG_Mixamo_Mannequin을 만든다(있으면 그대로, rebuild면 다시 구성).

        Args:
            rebuild: True면 자동 정의·체인 매핑·대상 포즈 정렬을 다시 한다.

        Returns:
            JSON 체인·매핑·오퍼레이션 요약.
        """
        importlib.reload(retarget)
        return _dump(retarget.ensure_rigs(rebuild is True))

    @toolset_registry.tool_call
    @staticmethod
    def retarget_run(sequence_paths: str, pack: str) -> str:
        """MX_* 시퀀스들을 매니퀸으로 복제·리타깃 → /Game/Character/Mannequin/Anims/<pack>/AS_<pack>_<이름>.

        Args:
            sequence_paths: 원본 시퀀스 경로를 쉼표로.
            pack: 무기 타입 폴더 이름(Bow·Sword).

        Returns:
            JSON 새 시퀀스 경로 목록.
        """
        importlib.reload(retarget)
        return _dump(retarget.retarget(_csv_list(sequence_paths), pack))

    @toolset_registry.tool_call
    @staticmethod
    def retarget_strip_drift(sequence_paths: str, bone: str | None = None) -> str:
        """리타깃한 시퀀스의 골반 수평 선형 이동만 빼 제자리로 만든다(흔들림·내딛기는 남김, MC4.2c).

        Args:
            sequence_paths: 매니퀸 시퀀스 경로를 쉼표로.
            bone: 본 이름(기본 pelvis).

        Returns:
            JSON 시퀀스별 {keys, drift_cm, after_cm}.
        """
        importlib.reload(retarget)
        return _dump([retarget.strip_linear_drift(p, bone or "pelvis") for p in _csv_list(sequence_paths)])


registration = Registration([KitTools, DataTableReimportTools, AnimBuilderTools, RetargetTools])
