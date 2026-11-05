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

from jm_kit import body_mask, garment, preview, profiles, skeleton_check


def _reload():
    """도구를 고친 뒤 에디터 재시작 없이 반영(의존 순서대로)."""
    for m in (profiles, skeleton_check, body_mask, garment, preview):
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
        """테스트 레벨 Dev/MC/L_KitTest를 열고 조명·바닥을 준비한다.

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


@unreal.uclass()
class DataTableReimportTools(unreal.ToolsetDefinition):
    """Common/DataTable CSV를 DataTable에 다시 가져온다(공식 DataTableTools에 Reimport가 없어서)."""

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


registration = Registration([KitTools, DataTableReimportTools])
