# UEClient/JM/CLAUDE.md

이 문서는 `UEClient/JM/` 디렉토리에서 작업할 때 참고하는 가이드입니다. 프로젝트 전체 구조는 [루트 CLAUDE.md](../../CLAUDE.md)를 참고하세요.

## 빌드 & 실행

언리얼 에디터 5.6에서 `UEClient/JM/JM.uproject`를 열거나(또는 UBT로 생성/빌드) 사용합니다. `JM` 모듈은 서버와 통신하기 위해 `ProtobufCore`(`UEClient/JM/Source/ProtobufCore/`에 벤더링된 `libprotobuf.lib`를 감싸는 얇은 모듈)에 의존하며, 그 외 표준 `Sockets`/`Networking`/`UMG`/`EnhancedInput`도 사용합니다.

에디터 없이 컴파일만 확인하려면 UBT를 커맨드라인으로 사용합니다 (`<UE_INSTALL_DIR>`은 로컬 언리얼 엔진 5.6 설치 경로로 바꿔서 사용하세요):

```
"<UE_INSTALL_DIR>\Engine\Build\BatchFiles\Build.bat" JMEditor Win64 Development -Project="<repo>\UEClient\JM\JM.uproject" -WaitMutex
```

프로토콜(생성된 Protobuf 소스)은 `UEClient/JM/Source/JM/Protocol/`에 복사되어 들어옵니다. 이 파일들은 [Common/Protocol/CLAUDE.md](../../Common/Protocol/CLAUDE.md)에서 관리하며, 절대 손으로 수정하지 마세요.

## UE 클라이언트 아키텍처 (`UEClient/JM/Source/JM/`)

- `Network/` — `NetworkManager`(소켓/연결 소유), `NetworkWorker`(수신 스레드), `NetworkSession`, `FPacketHandler`(수신되는 `SC_*` 패킷 디스패치, 서버의 `PacketHandler`와 대응되는 역할), `SendBuffer`.
- `Protocol/` — `GanPackets.bat`으로 복사되는 생성된 Protobuf 소스. 손으로 수정 금지.
- `Game/` — `JMGameInstance`(최상위 영속 상태), `JMObjectManager`(서버 오브젝트 ID ↔ 클라이언트 액터 매핑), `CharacterAssetData.h`(`FCharacterAssetData`: 영웅 메시/AnimBP/몽타주 참조, 이후 HeroTable로 대체), `IngameGameModeBase`, `LoginGameMode`.
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

- [Docs/DESIGN_CHARACTER_EQUIPMENT.md](../../Docs/DESIGN_CHARACTER_EQUIPMENT.md) — 매니퀸 공용 스켈레톤 모듈형 플레이어(체형 MALE/FEMALE), 슬롯 6개·몸 부위 숨김(Leader Pose), 서버 권위 장착 흐름. 예전 `Modular*Component`는 되살리지 않고 새로 설계.
- [Docs/DESIGN_ANIMATION.md](../../Docs/DESIGN_ANIMATION.md) — 공용 `ABP_Mannequin` + 무기 타입별 Animation Layer, 무기 등에 메기(클라 연출).
- [Docs/DESIGN_COMBAT_SKILL.md](../../Docs/DESIGN_COMBAT_SKILL.md) — 스킬은 무기 타입 종속, 크로스헤어 조준 입력과 스킬 캐스팅 패킷 설계.
- [Docs/DESIGN_ART_CONCEPT.md](../../Docs/DESIGN_ART_CONCEPT.md) — 아트·장비 제작 기준.

## 에디터 Python 도구 (`Content/Python/`)

에셋 제작 도구는 `Content/Python/<패키지>/`에 둡니다(에디터 Python 경로에 자동 포함, 게임 빌드 제외). 에디터를 켠 뒤 폴더를 처음 만들었다면 재시작 전까지 `sys.path`에 직접 추가해야 합니다.
- `jm_kit` — 키트 구성 캐릭터 도구(MC1 재작업). `kit_profiles.json`(체형별 리더·키트 부위·기본 의상, 슬롯별 숨김 부위), `preview`(테스트 레벨 `Dev/MC/L_KitTest`에 조립 `spawn`, SceneCapture `shot`/Simulate 중 `shot_game`, `look_at`). 예: `import importlib, jm_kit.preview as pv; importlib.reload(pv); pv.setup_level(); pv.spawn("KM", "M", (0, 0, 0), outfit="kit", anim=pv.ANIM["jog"])`
- `jm_kit.skeleton_check` — 장비 메시 뼈대 검사(MC1.3): `check(메시, "M", slot="top")`(기준 = 키트 의상 또는 리더, 다르면 `ok: False`와 이유), `self_test()`. 장비를 만들면 반드시 통과시킵니다.
- 이전 도구 `jm_modular`(`body_parts`·`garment_fit`·`preview`)는 2026-11-04 MC1 재작업으로 삭제했습니다(`Docs/DECISIONS.md`).
