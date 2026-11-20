# UEClient/JM/CLAUDE.md

이 문서는 `UEClient/JM/` 디렉토리에서 작업할 때 참고하는 가이드입니다. 프로젝트 전체 구조는 [루트 CLAUDE.md](../../CLAUDE.md)를 참고하세요.

## 빌드 & 실행

빌드는 `ue-build` 스킬을 따릅니다(에디터가 켜져 있으면 Live Coding이 명령줄 빌드를 막음).

프로토콜(생성된 Protobuf 소스)은 `UEClient/JM/Source/JM/Protocol/`에 복사되어 들어옵니다. 이 파일들은 [Common/Protocol/CLAUDE.md](../../Common/Protocol/CLAUDE.md)에서 관리하며, 절대 손으로 수정하지 마세요.

## UE 클라이언트 아키텍처 (`UEClient/JM/Source/JM/`)

- `Network/` — 인게임 레벨은 서버가 정합니다 — 로그인 성공 응답 `SC_LOGIN_INFO_PACKET.map_name`을 받아 `UNetworkManager::EnterGame(MapName)`이 `/Game/Maps/<이름>`을 열고, 이름이 비었거나 레벨이 없으면 들어가지 않고 사유를 띄웁니다(D-185, 클라이언트에 인게임 레벨 이름을 적지 않음).
- 개발용 자동 로그인(D-188, 배포 빌드 제외): `Saved/TestLogin.txt`가 있으면 로그인 화면이 그 계정으로 자동 로그인합니다 — 절차는 `pie-check` 스킬, **끝나면 파일을 지웁니다.**
- 상점 NPC(L4, D-201·D-202): 서버의 `OBJECT_TYPE_NPC`는 플레이어 액터 클래스(`BP_Player`)로 만들어 `Npcs` 맵에 따로 둡니다(몬스터 클래스 `ANpcCharacter`와 무관 — 서버 높이는 지면이라 캡슐 반높이만큼 올림). 판매가 표시는 `FItemTableRow.SellPrice`(**`ItemTable.csv`를 고치면 `reimport_csv`로 `DT_ItemTable`을 다시 채움**), 금액 계산은 서버.
- 포털(L5, D-203): 서버가 방 입장 직후 `SC_PORTAL_LIST`로 쓸 수 있는 포털의 위치·반경을 알려 주고 `UJMObjectManager::Portals`에 둡니다(레벨의 마커 액터는 에디터 전용이라 런타임에 읽지 않음, 도착지는 서버만 앎).
- Room 변경(DG3b, D-216): 마을과 사냥터는 같은 레벨의 서로 다른 서버 Room입니다. 문 포털·사냥터 사망 뒤 부활로 Room이 바뀌면 서버가 `SC_ROOM_CHANGE`를 보내고, `UJMObjectManager::HandleRoomChange`가 **레벨을 다시 열지 않고** 내 캐릭터를 뺀 캐릭터·몬스터·드랍·NPC 액터와 포털 목록을 지운 뒤 내 캐릭터를 받은 자리로 옮깁니다(`OnMyTeleport`). 내 캐릭터는 `Objects`에 남겨 두어 뒤따르는 본인 `SC_ADD_OBJECT`가 무시되고 `SC_REVIVE`가 그대로 동작합니다. 인벤토리 사본은 비우지 않습니다(서버가 다시 보내지 않음). **Room에 딸린 클라이언트 상태를 추가하면 `HandleRoomChange`(오브젝트)·`AJMMyPlayer::ResetForRoomChange`(내 시전·조준·잠금·상호작용 대상)에 정리 코드를 넣습니다.** 레벨이 다른 `map_name`은 아직 처리하지 않습니다(DG3c). 이동 거부는 `SC_MOVE_RESULT` → 시스템 메시지.
- 개발용 콘솔 명령(`AJMPlayerController` — `Give`·`Equip`·`Shop*`·`Portal`·`NetMoveStats`·`AutoAttack`·`AttackStats`, D-160 — 지우지 않음): 인자는 코드 참고, `Give`는 서버 `[Debug] DevCommands=true`가 필요합니다. `Portal [id]`는 포털 사용 요청(생략하면 가장 가까운 포털 — 서버가 거리를 검사하므로 포털 반경 안에 있어야 함). `Give`·`Equip`·`Shop*`·`Portal`·`NetMoveStats`·`AutoAttack`·`AttackStats`와 `jm.` 콘솔 변수(CP1)는 `WorldTools.world_step`의 `pie_exec`로도 보낼 수 있습니다(Claude가 PIE에서 마우스 없이 확인할 때).
- 몬스터 외형(MN1, D-198): `ANpcCharacter::ApplyMonsterAppearance`가 `template_id`로 `DT_MonsterTable` 행을 찾아 메시·크기를 바꿉니다. 서버가 주는 몬스터 z는 지면(내비메시) 높이이고 캐릭터 이동 컴포넌트가 바닥 위로 올리므로 크기 배율에 따른 높이 보정 코드는 없음. **`MonsterTable.csv`·`SkillTable.csv`를 고치면 `DataTableReimportTools.reimport_csv`로 `DT_MonsterTable`·`DT_SkillTable`을 다시 채워야 클라이언트에 반영됨.**
- 스킬·공격 선반영(CP1, D-211~D-213): 내 시전은 `AJMMyPlayer::CastSkillSlot`이 `CS_ATTACK`을 보낸 **같은 프레임에** 몽타주·이펙트·쿨타임·이동 잠금을 시작하고(`PendingCast`), 내 `SC_ATTACK`은 다시 재생하지 않으며, 거부(`SC_ATTACK_RESULT`)는 `UndoPendingCast`로 되돌립니다. 원격 캐릭터의 재생 경로는 그대로. **내 시전에 먼저 보이는 것을 추가하면 `UndoPendingCast`에 되돌리는 코드를 함께 넣음.** 개발용(출시 빌드 제외): 콘솔 변수 `jm.NetLagMs`(왕복 지연)·`jm.NetLagJitterMs`(± 흔들림) — `NetworkSession`이 게임 스레드에서 송신·수신을 절반씩 늦춤(같은 프로세스의 PIE 창 전부에 적용), 콘솔 명령 `AutoAttack <횟수> [칸]`(시전할 수 있게 되는 첫 프레임마다 시전)·`AttackStats [reset 0/1]`(보낸·확정·거부·사유별 수를 로그로). PIE 측정 때는 에디터가 뒤에 있으면 초당 3~4프레임으로 떨어져 결과가 틀리므로 `bThrottleCPUWhenNotForeground`를 끄고 잽니다(아래 "알려진 함정"의 백그라운드 스로틀, 끝나면 되돌림).
- 공격 중 이동 잠금(CB1, D-199): 잠금 끝 시각은 몽타주의 `UAnimNotify_SwingEnd`("Swing End", `Animation/AnimNotify_SwingEnd.h`, 런타임에는 아무 일도 안 함)이고 `ExportSkillTiming.bat`(`SkillTimingExport`)이 `MoveLockMs`로 씀 — 노티파이를 옮기면 이 배치 → `reimport_csv` → 서버 재시작. 서버 없는 `ATestPlayer`에는 이동 잠금 미적용.
- `UI/` — 창은 매니저(`JMInventoryManager`·`JMTradeManager`·`JMShopManager`)가 가진 서버 사본을 그리기만 하고, 요청만 보낸 뒤 화면은 `SC_INVENTORY_UPDATE` 등 서버 응답이 와야 바뀝니다. `JMUIManager`가 보이게 하는 위젯은 `SelfHitTestInvisible`임(화면 전체를 덮는 위젯 자체가 클릭을 받으면 위 창이 아래 창을 막음 — 창 안의 패널은 `Visible`로 둘 것).

### 구조 규칙

새 소스 하위 폴더를 만들면 `JM.Build.cs`의 include 경로도 함께 갱신합니다.

- `Data/` — DataTable Row 구조체(`<Table>Row.h`, `Common/DataTable/<Table>.csv`와 컬럼 1:1). 규약은 [Docs/DESIGN_DATA.md](../../Docs/DESIGN_DATA.md).
- **판정과 데미지는 서버가 결정**하므로 클라이언트에 판정 로직을 두지 않습니다.
- `UDataManager`(GameInstanceSubsystem, `Game/`에 배치)로만 DataTable을 조회합니다. `IGameBackend`/`ULocalGameBackend`와 예전 `Modular*Component`는 되살리지 않습니다(설계 [Docs/DESIGN_CHARACTER_EQUIPMENT.md](../../Docs/DESIGN_CHARACTER_EQUIPMENT.md)).

## 네이밍 컨벤션

`JM`은 프로젝트명(J=개발자 이름, M=MMORPG)이자 클래스 접두사입니다. **언리얼 엔진에 이미 존재하는 이름과 겹치는 클래스/함수에만** `JM` 접두사를 붙이고(`JMPlayer`, `JMCharacterBase`, `JMGameInstance`, `JMAnimInstance` 등), 겹치지 않는 이름은 접두사 없이 짓습니다.

## 에디터 MCP (Epic 공식 `ModelContextProtocol`)

에디터가 켜지면 `http://127.0.0.1:8000/mcp`가 자동으로 시작되고(`Config/DefaultEditorPerProjectUserSettings.ini`), 저장소 루트 `.mcp.json`의 `unreal` 서버로 연결됩니다(Claude Code는 에디터가 켜진 뒤 시작하거나 `/mcp`로 재연결). 도구는 `list_toolsets` → `describe_toolset` → `call_tool` 순서로 찾아 부릅니다.
- 공식 툴셋 함정: `SceneTools.load_level`은 현재 레벨이 저장돼 있어야 하고, `EditorApp.CaptureViewport`는 `captureTransform`·`annotations`를 값으로 줘야 합니다(실제 PIE는 `StartPIE`/`StopPIE`, 그래프 편집은 Blueprint `write_graph_dsl`).
- 프로젝트 툴셋 `jm_mcp`(`Content/Python/jm_mcp/toolset.py`, `Content/Python/init_unreal.py`에서 등록): `KitTools`(jm_kit 함수), `DataTableReimportTools.reimport_csv`(CSV 재임포트, 새 DT는 `create_from_csv`), `AnimBuilderTools`, `WorldTools`, `RetargetTools`.
- 공식 도구로 안 되는 작업의 순서: ① jm_kit 함수 + `jm_mcp` 툴셋 추가(에디터 재시작) ② 그래도 안 되면 `JMEditorTools`에 임시 커맨드렛 → `UnrealEditor-Cmd.exe <uproject> -run=<Name> -unattended -nopause`(에디터를 닫고, 필요한 모듈은 `Build.cs`에 임시 추가, 끝나면 커맨드렛·`Build.cs`를 되돌리고 다시 빌드). 어느 경우든 먼저 덤프로 현재 상태를 확인하고(사용자가 일부를 이미 해 뒀을 수 있음) 수정 전에 에셋을 백업합니다.
- 서버에 인증이 없으므로 **임의 Python 실행 도구는 두지 않습니다**. 새 자동화가 필요하면 jm_kit 등에 함수를 만들고 `jm_mcp` 툴셋에 감싸 추가합니다. 툴셋 인자는 `X | None = None` 형식(`Optional[X]`는 등록 실패). 툴셋을 고친 뒤에는 에디터를 다시 시작해야 반영됩니다(`KitTools.reload_kit`는 jm_kit만 다시 불러옴).

## 알려진 함정 (Gotcha)

- **소스 인코딩**: 한글 주석이 있는 C++ 파일은 UTF-8 **BOM을 유지**합니다(BOM 없는 UTF-8이면 MSVC가 다음 줄을 삼킬 수 있음, `JMEditorTools/*.cpp`는 예외로 BOM 없이도 컴파일됨). 일부 파일(`JMUIManager.*`, `JMPlayerController.cpp`, `NetworkManager.cpp` 등)은 **CP949**라 Edit 도구가 거부하므로 바이트 단위로 고칩니다. Bash 히어독으로 한글을 넘기면 CP949로 깨지므로, 한글이 든 스크립트는 파일로 만든 뒤 `python -I -X utf8`로 실행합니다.
- **빌드**: 에디터가 켜져 있으면 Live Coding 때문에 명령줄 빌드가 막힙니다 — 순서는 `ue-build` 스킬(`.claude/skills/ue-build`).
- **위젯 블루프린트 C++ 조작**(커맨드렛, UE 5.6에서 확인): 새 위젯은 변수 여부와 관계없이 **모두** `WidgetBP->OnVariableAdded(Name)`로 GUID를 등록해야 컴파일 ensure가 나지 않습니다. 삭제는 `WidgetTree->RemoveWidget` → `Rename(nullptr, GetTransientPackage())` → `OnVariableRemoved` 순서. WBP 생성은 `FKismetEditorUtilities::CreateBlueprint(Parent, Package, Name, BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())`, 텍스처 가져오기는 `UAssetImportTask` + AssetTools(`Build.cs`에 `AssetTools`, 위젯이면 `UMG`·`UMGEditor`·`Slate`·`SlateCore` 임시 추가).
- **BP 기본값을 MCP로 고친 뒤에는 컴파일 필수**: `ObjectTools.set_properties`로 블루프린트 CDO(`Default__X_C`)를 바꾸면 CDO에는 값이 보이지만, 새 인스턴스는 **마지막 컴파일 때 C++ 부모와 다르다고 기록된 속성만** 복사하므로 값이 빠진 채 생성된다(2026-11-16 `BP_MyPlayer.QuickSlotActions`가 PIE에서 빈 배열, 11-15 `BP_TestPlayer.AutoScript`도 같은 원인). 바꾼 뒤 `BlueprintTools.compile_blueprint` → 저장하고, 확인은 CDO가 아니라 새로 배치한 인스턴스 값으로 한다.
- **GameMode 상속 확인**: 맵이 쓰는 GameMode가 C++ 클래스를 실제로 상속하는지 먼저 봅니다(2026-10-28 `BP_GameMode`의 부모가 엔진 `GameModeBase`라 HUD 등록 코드가 실행되지 않았음).
- **MCP 연결**: 세션 시작 때 `unreal` MCP 연결이 실패해도 에디터 서버(127.0.0.1:8000)는 살아 있을 수 있습니다 → JSON-RPC(`initialize` → `tools/call` `call_tool`)로 직접 호출할 수 있습니다.
- **백그라운드 스로틀**: 에디터가 뒤에 있으면 "Use Less CPU when in Background"로 프레임이 초당 3~4로 떨어지고 GPU 파티클·풀이 진행되지 않습니다 — 측정·촬영 전에 `ObjectTools.set_properties("/Script/UnrealEd.Default__EditorPerformanceSettings", {"bThrottleCPUWhenNotForeground": false})`(또는 `Saved/Config/WindowsEditor/EditorSettings.ini`의 같은 값, 로컬)로 끄고 끝나면 되돌립니다.
- **사용자 안내**: 에디터 수동 작업을 안내할 때는 UE 5.8 UI 이름을 기준으로 합니다. 사용자가 "그 항목이 없다"고 하면 5.8에서의 표시 위치를 설명합니다. 안내에 포함할 것: 애셋 종류와 부모 클래스, 저장 경로·이름, 에디터 조작 순서, 설정할 프로퍼티와 값, 다른 곳에 연결하는 방법, 완료 확인 방법, C++가 Blueprint에 노출하는 프로퍼티/함수(루트 CLAUDE.md "작업 절차 규칙" 5).

## 에디터 Python 도구 (`Content/Python/`)

에셋 제작 도구는 `Content/Python/<패키지>/`에 둡니다(에디터 Python 경로에 자동 포함, 게임 빌드 제외). 에디터를 켠 뒤 폴더를 처음 만들었다면 재시작 전까지 `sys.path`에 직접 추가해야 합니다.
- `jm_kit` — 키트 구성 캐릭터·장비 도구(테스트 레벨 조립 `preview`, 뼈대 검사 `skeleton_check`, 의상 맞춤 `garment`, 아이콘 `icon`, 몸 마스크 `body_mask`, 테이블 `item_table`, 자세 측정 `posed`). 사용법은 `kit-gear` 스킬. **장비를 만들면 `skeleton_check`를 반드시 통과시킵니다. `ItemTable.csv` 외형 열(`AttachSocket`·`AttachM`·`AttachF`·`Mask`·`HideHair`)과 `BodyTable.csv`는 손으로 고치지 않고 `item_table_export` → `reimport_csv`로 만듭니다.**
- 그 밖의 `jm_kit` 도구 사용법은 작업별 스킬에 있습니다: 리타깃 `retarget` → `mixamo-lookup`, 참조 검사·팩 정리 `refs`와 텍스처 줄이기 `tex` → `asset-cleanup`, FX 시트·손질 `fx_sheet`·`fx_fix` → `fx-tools`, 맵 제작 `world`·`props` → `world-build`.
- 맵 작업의 필수 규칙(상세는 `world-build`): 마커·레벨 도구는 **열린 레벨에 적용되므로 실행 전에 `SceneTools.get_current_level`로 레벨을 확인**합니다. 보이는 메시의 충돌은 **프로필을 `NoCollision`으로 바꿔서** 끄고 충돌은 숨긴 도형이 맡으며, 충돌 윗면은 지면 위 1m 이상이어야 합니다. **배치 뒤에는 `check_dress`가 0**이어야 합니다.
