# UEClient/JM/CLAUDE.md

이 문서는 `UEClient/JM/` 디렉토리에서 작업할 때 참고하는 가이드입니다. 프로젝트 전체 구조는 [루트 CLAUDE.md](../../CLAUDE.md)를 참고하세요.

## 빌드 & 실행

언리얼 에디터 5.8에서 `UEClient/JM/JM.uproject`를 열거나(또는 UBT로 생성/빌드) 사용합니다. `JM` 모듈은 서버와 통신하기 위해 `ProtobufCore`(`UEClient/JM/Source/ProtobufCore/`에 벤더링된 `libprotobuf.lib`를 감싸는 얇은 모듈)에 의존하며, 그 외 표준 `Sockets`/`Networking`/`UMG`/`EnhancedInput`도 사용합니다.

에디터 없이 컴파일만 확인하려면 UBT를 커맨드라인으로 사용합니다 (`<UE_INSTALL_DIR>`은 로컬 언리얼 엔진 5.8 설치 경로로 바꿔서 사용하세요):

```
"<UE_INSTALL_DIR>\Engine\Build\BatchFiles\Build.bat" JMEditor Win64 Development -Project="<repo>\UEClient\JM\JM.uproject" -WaitMutex
```

프로토콜(생성된 Protobuf 소스)은 `UEClient/JM/Source/JM/Protocol/`에 복사되어 들어옵니다. 이 파일들은 [Common/Protocol/CLAUDE.md](../../Common/Protocol/CLAUDE.md)에서 관리하며, 절대 손으로 수정하지 마세요.

## UE 클라이언트 아키텍처 (`UEClient/JM/Source/JM/`)

- `Network/` — `NetworkManager`(소켓/연결 소유), `NetworkWorker`(수신 스레드), `NetworkSession`, `FPacketHandler`(수신되는 `SC_*` 패킷 디스패치, 서버의 `PacketHandler`와 대응되는 역할), `SendBuffer`.
- `Protocol/` — `GanPackets.bat`으로 복사되는 생성된 Protobuf 소스. 손으로 수정 금지.
- `Game/` — `JMGameInstance`(최상위 영속 상태), `JMObjectManager`(서버 오브젝트 ID ↔ 클라이언트 액터 매핑), `IngameGameModeBase`, `LoginGameMode`.
- `Character/`, `Animation/`, `Player/` — `JMCharacterBase`, `JMAnimInstance`, `JMPlayer`/`JMMyPlayer`(로컬 제어 vs 리모트), `JMPlayerController`, `NpcCharaceter`(서버 주도 NPC/몬스터).
- `UI/` — `JMUIManager`(중앙 UI/위젯 스택 매니저), `LoginWidget`, `ChatWidget`, `HpBarWidget`.

### 병합 예정 폴더 (Test 프로젝트에서 이식)

기존 폴더 구조는 유지하고, 아래만 필요할 때 추가합니다. 새 소스 하위 폴더를 만들면 `JM.Build.cs`의 include 경로도 함께 갱신합니다.

- `Data/` — DataTable Row 구조체(`<Table>Row.h`, `Common/DataTable/<Table>.csv`와 컬럼 1:1). 규약은 [Docs/DESIGN_DATA.md](../../Docs/DESIGN_DATA.md).
- `Combat/` — 스킬 시전 UI/입력, AnimNotify(시전·연출 타이밍 전용). **판정과 데미지는 서버가 결정**하므로 클라이언트에 판정 로직을 두지 않습니다.
- `UDataManager`(GameInstanceSubsystem, `Game/`에 배치)로만 DataTable을 조회합니다. `IGameBackend`/`ULocalGameBackend`는 이식하지 않습니다.

클라이언트는 의도적으로 게임플레이 권한이 얇습니다(thin client) — 위치, 스탯, 전투 결과, 장비 상태는 모두 `ObjectInfo`/`SC_*` 패킷을 통해 서버가 주도하며, 클라이언트 컴포넌트는 대부분 그 상태를 계산하기보다는 반영(reflect)만 합니다.

## 네이밍 컨벤션

`JM`은 프로젝트명(J=개발자 이름, M=MMORPG)이자 클래스 접두사입니다. **언리얼 엔진에 이미 존재하는 이름과 겹치는 클래스/함수에만** `JM` 접두사를 붙이고(`JMPlayer`, `JMCharacterBase`, `JMGameInstance`, `JMAnimInstance` 등), 겹치지 않는 이름은 접두사 없이 짓습니다.

## 설계 문서

- [Docs/DESIGN_CHARACTER_EQUIPMENT.md](../../Docs/DESIGN_CHARACTER_EQUIPMENT.md) — 매니퀸 공용 스켈레톤 모듈형 플레이어(체형 MALE/FEMALE), 슬롯 7개(보조손 방패 포함)·몸 부위 숨김(Leader Pose), 서버 권위 장착 흐름. 예전 `Modular*Component`는 되살리지 않고 새로 설계.
- [Docs/DESIGN_ANIMATION.md](../../Docs/DESIGN_ANIMATION.md) — 공용 `ABP_PlayerBase` + 무기 타입별 Animation Layer, 무기 등에 메기(클라 연출).
- [Docs/DESIGN_COMBAT_SKILL.md](../../Docs/DESIGN_COMBAT_SKILL.md) — 스킬은 무기 타입 종속, 크로스헤어 조준 입력과 스킬 캐스팅 패킷 설계.
- [Docs/DESIGN_ART_CONCEPT.md](../../Docs/DESIGN_ART_CONCEPT.md) — 아트·장비 제작 기준.

## 에디터 MCP (Epic 공식 `ModelContextProtocol`)

에디터가 켜지면 `http://127.0.0.1:8000/mcp`가 자동으로 시작되고(`Config/DefaultEditorPerProjectUserSettings.ini`), 저장소 루트 `.mcp.json`의 `unreal` 서버로 연결됩니다(Claude Code는 에디터가 켜진 뒤 시작하거나 `/mcp`로 재연결). 도구는 `list_toolsets` → `describe_toolset` → `call_tool` 순서로 찾아 부릅니다.
- 공식 툴셋: Asset·Actor·Scene(`load_level` — 현재 레벨이 저장돼 있어야 함)·Blueprint(`write_graph_dsl`로 그래프 편집)·DataTable(행 읽기·쓰기)·Material·SkeletalMesh·UMG(위젯 트리)·EditorApp(`StartPIE`/`StopPIE` 실제 PIE, `CaptureViewport`·`CaptureEditorImage` 스크린샷, `CaptureViewport`는 `captureTransform`·`annotations`를 값으로 줘야 함)·Logs·AutomationTest.
- 프로젝트 툴셋 `jm_mcp`(`Content/Python/jm_mcp/toolset.py`, `Content/Python/init_unreal.py`에서 등록): `KitTools`(jm_kit 함수 — 뼈대 검사, 테스트 레벨 조립·촬영, 의상 맞춤, 몸 마스크, FX 시트·손질), `DataTableReimportTools.reimport_csv`(CSV 재임포트).
- 서버에 인증이 없으므로 **임의 Python 실행 도구는 두지 않습니다**. 새 자동화가 필요하면 jm_kit 등에 함수를 만들고 `jm_mcp` 툴셋에 감싸 추가합니다. 툴셋 인자는 `X | None = None` 형식(`Optional[X]`는 등록 실패). 툴셋을 고친 뒤에는 에디터를 다시 시작해야 반영됩니다(`KitTools.reload_kit`는 jm_kit만 다시 불러옴).

## 에디터 Python 도구 (`Content/Python/`)

에셋 제작 도구는 `Content/Python/<패키지>/`에 둡니다(에디터 Python 경로에 자동 포함, 게임 빌드 제외). 에디터를 켠 뒤 폴더를 처음 만들었다면 재시작 전까지 `sys.path`에 직접 추가해야 합니다.
- `jm_kit` — 키트 구성 캐릭터 도구(MC1 재작업). `kit_profiles.json`(체형별 리더·키트 부위·기본 의상, 슬롯별 숨김 부위), `preview`(테스트 레벨 `Dev/MC/L_KitTest`에 조립 `spawn`, SceneCapture `shot`/Simulate 중 `shot_game`, `look_at`). 예: `import importlib, jm_kit.preview as pv; importlib.reload(pv); pv.setup_level(); pv.spawn("KM", "M", (0, 0, 0), outfit="kit", anim=pv.ANIM["jog"])`
- `jm_kit.skeleton_check` — 장비 메시 뼈대 검사(MC1.3): `check(메시, "M", slot="top")`(기준 = 키트 의상 또는 리더, 다르면 `ok: False`와 이유), `self_test()`. 장비를 만들면 반드시 통과시킵니다.
- `jm_kit.garment` — 의상 맞춤(MC2.1): `import_fbx`(짧은 경로에서), `build(정적 메시, "M", "top", 출력 경로)` = 키트 의상 복제 후 형상·가중치·머티리얼만 교체(뼈대·바인드·PA 상속, 결과에 `skeleton_ok`·`limbs_ok`), `build_gloves`(틀 없음 — 오른손 맞춤·좌우 반전·몸 메시 복제), `build_gloves_kit_hand`(키트 손 형상 + `glove_paint` 무늬, ORM 포함 — 판금은 `M_Equip_PBR`, `fit.clean_cuff`면 손목 가장자리를 팔뚝 축으로 펴서 톱니 제거), 방패는 정적 부착 `fit: "shield"`(`hand_l`, `face_dir`·`face`·`up`으로 앞면·위 축, 손바닥에서 `gap_cm` — MC4.2f), 투구는 정적 부착 `fit: "helm"`(머리 상자 균일 배율·`yaw`, `attach.target_tris`면 가져올 때 `reduce_static_lods`로 LOD 3단계 축소 — 고폴리 생성용), `fit.clear_cm`(몸과 최소 거리 — 삼각형이 큰 판금 상의 사이로 몸이 비치는 것 방지), `static_attachment`(두건·무기 본 부착값), `build_item(키)`·`import_source`(items.json 기준), `measure`. `preview.spawn`의 outfit 값에 items.json 키(예 `"Cloth_Sword"`)를 쓰면 체형별 메시·정적 부착을 알아서 처리합니다.
- `jm_kit.icon` — 장비 아이콘(MC2.4): `shoot(키 목록)` = items.json 아이템을 같은 조명·각도(방어구 정면 3/4, 무기 45° 대각선, 장갑 오른손 손등)로 투명 배경 촬영 → `SourceArt/Icons/T_Icon_<Item>.png`(512²) + 아이템 폴더 `T_Icon_<Item>`, 확인 시트 `Saved/MCShots/icons_sheet.png`. 아이템별 각도는 items.json `icon`(`yaw`·`pitch`·`margin`). MCP 도구 인자로 선택 목록(`list[str] | None`)은 변환이 실패하므로 쉼표 문자열로 받습니다.
- `jm_kit.body_mask` — 몸 마스킹(2026-11-05): `build_mask("M"/"F")`(키트 몸 UV에 영역·거리 마스크 래스터화 → `SourceArt/Masks` PNG → `Character/Body/Masks`), `build_material()`(`M_Body_Masked`·`MI_Body_<M|F>`), `apply(컴포넌트, 체형, 파라미터)`. `preview.spawn`이 입은 슬롯의 마스크를 리더(= 보이는 몸, 키트 `Body_Combined`)의 몸 섹션에 자동 적용합니다.
- `jm_kit.item_table` — 데이터 테이블 생성(MC4.1): `export(write=True)` = `ItemTable.csv` 외형 열(`AttachSocket`·`AttachM`·`AttachF`·`Mask`·`HideHair`)과 `BodyTable.csv`를 `items.json`·`kit_profiles.json`에서 계산해 씀(MCP `item_table_export`). 이 열은 손으로 고치지 않고, 쓴 뒤 `reimport_csv`로 DT를 다시 채웁니다.
- `jm_kit.retarget` — Mixamo → 매니퀸 리타깃(MC4.2b, MCP `RetargetTools`): `import_source_mesh`(Y Bot, 최초 1회) → `import_anims(FBX 목록, 팩)` → `ensure_rigs()`(`IK_Mixamo`·`IK_Mannequin`·`RTG_Mixamo_Mannequin`, 손바닥뼈 체인은 매핑 안 함) → `retarget(시퀀스 목록, 팩)` = `Character/Mannequin/Anims/<팩>/AS_<팩>_<이름>`(이름을 `MX_<무기>_<동작>`으로 바꾼 시퀀스는 `AS_<무기>_<동작>`, 같은 경로는 덮어씀) → 이동이 든 동작은 `strip_linear_drift`(MCP `retarget_strip_drift`, Mixamo In Place 방식 — pelvis + IK 본). 리타기터의 Root Motion 오퍼레이션은 끄고 `Pin IK Bones`로 IK 본을 발·손에 맞춘다(안 하면 Foot IK가 다리를 꼬고 몸을 띄움, MC4.2c). **PIE·Simulate 중에는 스켈레탈 메시 가져오기가 막힌다**(Interchange, 스켈레톤만 생김). 다운로드 설정은 `Docs/ExternalAssets.md`.
- `jm_kit.posed` — 자세별 측정·촬영(MC4.2e, MCP `posed_run`·`posed_grip_search`): 동작 프레임(`POSES` — 활·검 대기·당김·스킬 등)으로 캐릭터 자세를 잡고(loadout `shield`면 검 자세에 방패도 붙여 앞·뒤 촬영) 장갑·무기 관통(맨손 기준값 포함), 상의 어깨 뭉침(어깨 관절 반경 삼각형 넓이 비·위치·몸 안 비율)을 재고 상체·어깨·손을 촬영(`Saved/MCShots/MC42e_*`). 키트 상의 경로를 넣으면 기준값. `grip_search`는 손잡이 수직 평면에서 쥐는 위치(`items.json` `attach.offset`, 손 본 로컬)를 찾는다. 자세는 C++ `UAnimAssetBuilder::PoseComponentAtTime`(에디터 월드는 틱하지 않음) — 눈·이·헤어 같은 Leader Pose 팔로워는 에디터 월드에서 따라오지 않아 숨긴다.
- `jm_kit.refs` — 참조 검사(MCP `refs_find`): 플레이어 쪽 루트에서 의존을 따라가 Paragon·Greystone·Sparrow에 닿는 사슬과 클래스. 팩 정리(2026-11-12): `refs_plan(roots, targets, deleting)` = 루트에서 닿는 targets 안 에셋 전체(팩 내부 사슬 포함, `deleting`을 남길 에셋이 쓰면 `blockers`, 없는 패키지는 `missing`), `refs_move(roots, mapping_json, deleting, dry_run)` = 그 목록을 `[[원본 접두사, 대상 접두사]]`대로 `AssetTools.rename_assets` → 남은 리디렉터 Fix Up → 저장(기본 dry_run, `blockers`가 있으면 안 옮김). 폴더 삭제는 참조 0 확인 뒤 에디터를 닫고 디스크에서.
- `jm_kit.fx_sheet` — FX 라이브러리 미리보기 시트(`Docs/FX_CATALOG.md` 3단계, MCP `fx_sheet_start`·`fx_sheet_status`·`fx_sheet_stop`): `Content/FX/<분류>/`의 파티클을 **Simulate 중** 게임 월드에 하나씩 스폰해(Buff·Life·Trail은 `KM_Leader`의 `spine_03` 위치, Cast는 발밑, 나머지는 캐릭터 자리 바닥 — `spawn_emitter_attached`는 Simulate에서 아무것도 그리지 않아 위치 스폰) 시점별(기본 0.3·1.0초)로 `T_CapG`가 찍고 분류별 시트 `Saved/MCShots/fx_sheet_<라벨>_<분류>.png`(행 = 에셋, 열 = 시점, 순서는 `fx_sheet_status.json`의 `sheets[].order`)로 합성. 한 Python 호출 안에서는 프레임이 안 가므로 슬레이트 post-tick 콜백으로 진행하고 `start`는 바로 반환(`status`로 확인). 순서: `preview_setup_level` → `preview_clear("KM")` → `preview_spawn("KM","M",[0,0,0],"kit")` → `preview_start_simulate` → 다음 호출에서 `fx_sheet_start`. 함정(2026-11-10): ① 에디터가 뒤에 있으면 "Use Less CPU when in Background"로 GPU 파티클(불꽃·섬광)이 진행되지 않아 빈 화면 — `ObjectTools.set_properties("/Script/UnrealEd.Default__EditorPerformanceSettings", {"bThrottleCPUWhenNotForeground": false})` 또는 `Saved/Config/WindowsEditor/EditorSettings.ini`(같은 값, 로컬) ② Simulate 중엔 `EditorAssetLibrary`가 동작하지 않아 목록은 에셋 레지스트리로 ③ Cascade LOD는 플레이어 카메라 기준이라 에셋 `lod_method`를 잠시 DirectSet으로(저장 안 함) ④ 캡처는 `KM_*`와 파티클만 표시(바닥·대기 제외, 검은 배경)해 가산 파티클이 보이게 ⑤ 레벨의 다른 `*_Leader` 시험 액터와 섞이지 않게 라벨을 정확히 `KM_Leader`로 찾음.
- `jm_kit.fx_fix` — FX 손질(`Docs/FX_CATALOG.md` 4단계, MCP `fx_inspect`·`fx_apply`·`fx_weapon_socket_points`·`fx_trail_notify`): Cascade의 `Emitters`·`LODLevels`·`Modules`와 본 모듈 프로퍼티는 에디터 Python `get_editor_property`가 거부하고("protected and cannot be read", 2026-11-12) Cascade 클래스는 `unreal` 모듈에 타입도 없다. 그래서 구조 덤프(JSON)·이미터 삭제(안 쓰는 모듈은 임시 패키지로 옮겨 저장에서 제외)·켬/끔·본 이름 편집(`EditBones` rename/drop/set)·몽타주 Trail 노티파이는 C++ `UFxEditLibrary`(`Source/JMEditorTools/FxEditLibrary.*`)가 하고, Python은 메시 교체(`TypeDataMesh.mesh`는 EditAnywhere라 됨)·매니퀸 본 대조(`bones_missing`)·JSON 정리만 한다. `apply(ops)` op: `delete_emitter`·`disable_emitter`·`enable_emitter`·`rename_bones`·`drop_bones`·`set_bone_source`·`set_mesh`(emitter는 이름 또는 `#인덱스`, 같은 이름이 여럿이면 인덱스, **삭제는 인덱스 큰 것부터**). `weapon_socket_points`는 `items.json` 부착값으로 칼 밑동·칼끝을 `hand_r` 로컬로 계산 — AnimTrail 소켓 `FX_Weapon_Bottom/Top`은 리더 메시에 `SkeletalMeshTools.add_socket`(`Docs/ExternalAssets.md`). `set_trail_notify(몽타주, FX, start, duration)`는 기존 Trail 노티파이를 지우고 하나 단다. 확인은 `fx_sheet` 재촬영(본·궤적은 위치 스폰이라 안 찍힘 → PIE).
- 이전 도구 `jm_modular`(`body_parts`·`garment_fit`·`preview`)는 2026-11-04 MC1 재작업으로 삭제했습니다(`Docs/DECISIONS.md`).
