# DESIGN_DATA

Test 프로젝트의 데이터 규약을 이 저장소 구조에 맞춰 옮긴 문서입니다. 목표 아키텍처이며, 서버 쪽 CSV 로더는 아직 없습니다.

## 원칙
- 스탯, 장비/무기 애셋, 스킬(쿨타임/시전시간/판정 방식), 이동 수치, UI 정의는 모두 DataTable로 관리하고 코드에 수치를 하드코딩하지 않습니다.
- **서버와 UE는 `Common/DataTable/*.csv`를 읽어서 사용합니다.** CSV가 공용 기준(허브)입니다.
- 서버의 `ServerData.h`(`_skillTable`/`_characterTable` 하드코딩)는 이 CSV를 읽는 로더로 교체되는 것이 목표입니다. **수치의 권위는 서버**이고, 클라이언트 DataTable은 연출·UI·애셋 참조용입니다. 서버와 클라이언트가 같은 CSV를 읽으므로 두 쪽이 어긋나지 않습니다.
- 편집은 **xlsx** 또는 **UE DataTable 에디터**에서 하고, 어느 쪽에서 고쳐도 **항상 CSV와 일치**시킵니다.

## 파일 위치
| 종류 | 경로 | 역할 |
|---|---|---|
| CSV | `Common/DataTable/<Table>.csv` | 서버/UE가 읽는 기준 |
| xlsx | `Common/DataTable/<Table>.xlsx` | 편집용. CSV와 같은 이름, 같은 폴더 |
| DataTable 애셋 | `UEClient/JM/Content/Data/DT_<Table>.uasset` | UE에서 쓰는 형태 (경로는 구현 시 확정) |
| Row 구조체 | `UEClient/JM/Source/JM/Data/<Table>Row.h` | CSV 컬럼과 1:1 |

## 동기화 규칙
- **CSV가 항상 최신이어야 합니다.** xlsx나 DataTable을 고친 직후 CSV를 갱신하고, 나머지 한쪽도 맞춥니다.
- 한 테이블은 **한 번에 한 곳에서만** 편집합니다 (양방향 병합 수단이 없음).
- xlsx 편집: 다른 이름으로 저장 → `CSV UTF-8(쉼표로 분리)`로 `Common/DataTable/<Table>.csv` 덮어쓰기 → UE에서 DataTable 우클릭 → Reimport.
- DataTable 편집: 에디터에서 수정 → Asset Actions → Export as CSV로 덮어쓰기 → xlsx도 같은 내용으로 갱신.
- Excel 임시 파일(`~$*.xlsx`)은 커밋하지 않습니다(`.gitignore`에 등록됨).

## CSV 규약
- 인코딩 UTF-8, 구분자 쉼표, 첫 행은 헤더.
- **키 컬럼**: 첫 컬럼 `Name`에 정수 ID를 문자열로 씁니다(예: `1001`). UE가 첫 컬럼을 RowName으로 쓰므로 Row 구조체에는 `Id` 필드를 두지 않습니다. 조회는 `FName(FString::FromInt(Id))`.
- **컬럼명**: PascalCase, Row 구조체 프로퍼티명과 1:1. 단위는 접미사(`Sec`, `Cm`, `Deg`).
- **값 표기**: Enum은 UENUM 항목명 문자열, 숫자 칸은 비우지 않고 `0`, bool은 `TRUE`/`FALSE`.
- **애셋 참조**: 전체 소프트 경로 문자열. C++은 `TSoftObjectPtr`/`TSoftClassPtr`. **서버는 애셋 참조 컬럼을 읽지 않으므로** 서버가 필요 없는 클라이언트 전용 컬럼은 같은 테이블에 두되 서버 로더가 무시하도록 합니다.
- **ID 간 참조**: 다른 테이블의 정수 ID를 씁니다(예: Skill의 `WeaponId`).
- **ID 공간**: 테이블마다 독립이며 `1001`부터. 종류 구분이 필요하면 앞자리로 나눕니다(SkillTable은 앞 1자리가 무기 종류).
- **`.proto` enum과의 일치**: CSV의 enum 값이 `Enum.proto`의 enum과 겹치는 경우(예: 무기 종류) 양쪽 이름을 동일하게 유지합니다.

## 테이블 목록 (해당 기능 작업 시 작성하고 그때 컬럼을 확정)
| 테이블 | 용도 |
|---|---|
| MovementTable | 이동 속도, 카메라 관련 수치 (Test에서 이식됨: `MovementTable.csv`) |
| PlayerStatTable | 플레이어 레벨별 기본 스탯(체형 공용) — 2026-11-07 MC3.1b에 StatTable에서 분리 |
| ItemTable | 장비 아이템(슬롯, 등급, 티어, 스탯 보너스, 무기 타입, 외형 경로는 클라 전용) — MC3.1 구현, A3에서 확장 |
| SkillTable | `WeaponType`(무기 타입 종속), 슬롯, 쿨타임, 판정 Shape, 타겟팅 타입, 범위, 데미지, 몽타주 경로(클라 전용) |
| MonsterTable | 몬스터 종류별 스탯·스킬·리스폰 (ID = `ObjectInfo.template_id`) |
| UITable | 해당 기능 작업 시 |
| WorldTable | 맵별 시야(AOI) 수치 — 서버 전용(2026-10-31 A1.3) |

2026-11-04 Phase MC: 계획했던 `HeroTable`/`WeaponItemTable`은 만들지 않고 **ItemTable**로 대체합니다. 플레이어 메시·AnimBP는 매니퀸 공용이라 테이블이 필요 없고, `DT_CharacterAsset`은 MC4에서 플레이어 참조를 끊은 뒤 정리합니다([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)).

## 서버 로더 (구현됨, 2026-10-20)
- `CsvFile`(ServerCore `Utils/`)이 CSV를 파싱하고(UTF-8·BOM·따옴표 지원, 컬럼 수/숫자 형식 검증), `DataManager::Init()`(`Server/Server/Data/ServerData.cpp`)가 테이블을 읽어 `GetCharacterData`/`GetSkillData`/`GetSkillBySlot`/`GetMonsterData`/`GetItemData` 등으로 제공합니다.
- 데이터 폴더는 환경변수 `JH_DATA_DIR`, 없으면 `../../Common/DataTable` 등 서버 작업 디렉터리 기준 후보 경로에서 찾습니다.
- **형식 오류·중복 키·존재하지 않는 참조는 서버 시작을 중단**합니다(줄 번호 포함 메시지). 데이터 버그를 조용히 넘기지 않기 위함입니다.
- 수치 변경은 CSV 수정 후 **서버 재시작**으로 반영됩니다(핫 리로드 없음).

### PlayerStatTable (서버 구현됨, 2026-11-07 MC3.1b — 옛 StatTable)
플레이어 **레벨별** 기본 스탯이며 **체형 공용**입니다(MALE/FEMALE은 외형만 다름). `Name`(ID, 1001~), `Level`(서버 조회 키, 1 이상·중복 불가), `MaxHp`, `AttackDamage`, `AttackSpeed`, `MoveSpeed`. 현재 행: 1001 = Level 1(MaxHp 500, AttackDamage 0, AttackSpeed 1.0, MoveSpeed 500). 시작 레벨(`PLAYER_START_LEVEL` = 1) 행이 없으면 서버 시작을 중단합니다.
- 생성 시 HP는 `MaxHp`입니다. 가입 시 DB `Player_Info`에 저장하는 Level·Hp도 이 행에서 가져옵니다(SQL에 수치를 쓰지 않음).
- 레벨 성장(행 추가, 필요 경험치 열)은 A2.3, DB에 저장된 레벨로 조회하는 것은 A2.5(`InitFromDb` 연결)에서 합니다. 그 전까지 모든 플레이어는 Level 1 스탯입니다.
- 최종 스탯 = 기본 + 장비 보너스(무기 `AttackDamage`, 방어구 `MaxHp`)이며 서버가 계산합니다(MC3.2).
- **피해 = 스킬 `BaseDamage` + 시전자 `AttackDamage`**(플레이어·몬스터 같은 공식, 시전 순간 확정 — 2026-11-07 MC3.1b). 방어력(받는 피해 감소)은 나중에(A2.3 무렵) `Character::OnDamaged`에 넣습니다.
- 가입은 MALE/FEMALE만 받습니다. DB에 남은 옛 4·5 캐릭터(봇 `dummy` 계정 포함)는 로그인할 때 서버가 메모리에서 MALE로 바꾸고, DB 행은 MC3.3 마이그레이션에서 바꿉니다.
- `PlayerType`(`Enum.proto`)은 **플레이어 체형 전용**입니다(NONE/MALE/FEMALE, 3·4·5는 `reserved`). 몬스터는 `ObjectType`으로 구분하고 종류는 `ObjectInfo.template_id`로 보냅니다.

### SkillTable (서버 구현됨)
`Name`(skillId), `WeaponType`(무기 종속 — `Unarmed`/`Sword`/`Bow`/`Monster`), `Slot`(0 = 기본 공격, 1~5 = 숫자키), `TargetingType`(`Targeted`/`NonTargeted`), `HitShape`(`Sector`/`Line`/`Projectile`/`Zone`), `HitDelayMs`, `CooldownMs`, `CastRange`(Targeted는 대상까지, Zone은 지점까지 최대 거리), `Radius`, `Angle`, `Range`, `Width`, `BaseDamage`, `ProjSpeed`, `DurationMs`/`TickMs`(Zone: 지속 시간 / 한 대상에게 피해를 주는 최소 간격 — 진입 즉시 피해 후 `TickMs`마다 반복). 클라이언트 전용 열(서버 로더는 무시): `Fx`/`FxBaseRadius`(이펙트), `Montage`(시전 몽타주), `Icon`(전투 HUD 슬롯 아이콘 텍스처).
- 클라이언트는 skillId가 아니라 **슬롯**을 보내고, 서버가 캐릭터의 현재 무기 타입으로 SkillTable을 해석합니다(다른 무기의 스킬 사용 불가). 쿨타임은 스킬별이며 기본 공격(슬롯 0)만 공격 속도 배율을 반영합니다.
- (2026-11-07 MC3.1) 서버 조회 키 = (WeaponType, Slot), 중복 불가. **ID는 4자리, 첫째 자리 = 무기 종류**(맨손 1xxx, 검 2xxx, 활 3xxx, 몬스터 9xxx — 어긋나면 서버 시작 중단). 현재 행: 1001 맨손 기본 공격(검 기본 공격의 60% 피해 30, 반경 150, 시간은 검과 같음 — MC4.2에서 맨손 몽타주로 다시 뽑음), 2001/2002 검(옛 401/402), 3001/3002 활(옛 501/502), 9001 몬스터(옛 301). 맨손 기본 공격(Unarmed, 0) 행이 없으면 시작을 중단합니다.
- `Monster` 스킬은 슬롯 색인에 넣지 않고 `MonsterTable.SkillId`로만 찾습니다(몬스터 종류마다 슬롯 0이 따로 있을 수 있음).
- 몽타주 열은 아직 옛 Greystone/Sparrow 몽타주를 가리키고, MC4.2에서 `AM_<WeaponType>_<Action>`으로 바뀝니다. 맨손 행은 몽타주가 비어 있어 클라이언트가 캐릭터 기본 공격 몽타주를 씁니다. 상세 [DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md).
- 플레이어의 현재 무기 타입: MC3.1에서는 장착 시스템이 없어 항상 `Unarmed`(서버 `Player::GetWeaponType`, 클라 `AJMPlayer::GetWeaponType`) — MC3.2에서 장착 무기의 `ItemTable.WeaponType`으로 바꿉니다.

### MonsterTable (서버 구현됨)
`Name`(몬스터 종류 ID, 1001~ = 스폰 마커의 `monsterTemplateId` = `ObjectInfo.template_id`), `SkillId`, `MaxHp`·`AttackDamage`·`AttackSpeed`·`MoveSpeed`(몬스터 고정 스탯 — 2026-11-07 MC3.1b에 StatTable에서 옮김, 범위는 PlayerStatTable과 같음), `RespawnSec`(사망 후 리스폰까지), `DeathRemoveDelaySec`(시체를 남기는 시간, `<= RespawnSec`), `HitStunMs`(피격 후 행동이 막히는 시간, 0~10000 — 피격 모션 길이에 맞춘다), `WanderRadiusCm`(대기 중 배회: 현재 위치에서 이 반경 안 임의 지점, 0~10000 — 2026-10-31 A1.3c에서 AOI 셀과 분리한 임시값, 행동트리 개선 때 재검토). 사망 연출 길이에 맞춰 `DeathRemoveDelaySec`를 조절합니다. 스폰 위치/수는 맵 JSON의 `spawnRadius`/`maxCount`를 따릅니다. 보상(경험치/골드) 컬럼은 A2.3에서 추가합니다.
- `SkillId`(2026-11-07 MC3.1, 둘째 열, 필수): 기본 공격 스킬. SkillTable에 없거나 `WeaponType`이 `Monster`가 아니면 서버 시작을 중단합니다. 몬스터 스킬은 `PlayerType`이 아니라 이 열로 찾습니다.
- 현재 행(2026-11-07 MC3.1b): 1001(Rampage, 옛 ID 3) — SkillId 9001, MaxHp 150, AttackDamage 20, AttackSpeed 1.0, MoveSpeed 400. 몬스터 피해는 스킬 `BaseDamage`(9001 = 40) + 고정 공격력 20 = 60입니다. 몬스터 세기는 이 공격력으로 조절합니다.
- 1001 미만 ID는 서버 시작을 중단합니다(옛 PlayerType 값을 막기 위함). 맵 로직 JSON의 `monsterTemplateId`가 이 테이블에 없으면 서버는 그 마커를 스폰하지 않고 ERROR를 남깁니다(방은 실행 중에도 만들어지므로 중단하지 않음). `ParagonSample_Logic.json`은 레벨에 `ALevelMarker`가 없어 손으로 관리합니다.
- 클라이언트는 몬스터를 `object_type`으로 판별합니다. 종류별 외형(template_id → 액터)은 몬스터가 늘 때 추가합니다. A2.3 보상(경험치·골드)과 A3 `DropTable`은 이 ID를 참조합니다.

### ItemTable (2026-11-07 MC3.1 확정, 서버 구현됨)
- 서버가 읽는 열: `Name`(itemId), `Slot`(`Head`/`Top`/`Bottom`/`Gloves`/`Boots`/`Weapon`), `Grade`(`Common`/`Uncommon`/`Rare`), `Tier`(`Cloth`/`Leather`/`Plate`), `WeaponType`(무기만 `Sword`/`Bow`, 나머지 `None`), `BonusAttackDamage`(무기만), `BonusMaxHp`(방어구만), `Starter`(1 = 가입 시 기본 장비, 0/1만, 슬롯당 최대 1개 — MC3.3; 마이그레이션 SQL `migrations/2026-11-07_mc33_equip.sql`의 목록과 같아야 함).
- **itemId 규칙 = 4자리 [슬롯][티어][일련번호 01~99]**: 슬롯 번호 Head 1·Top 2·Bottom 3·Gloves 4·Boots 5·Weapon 6(MC3.2의 새 `EquipPart` 번호와 같음), 티어 번호 Cloth 1·Leather 2·Plate 3. 예: 1101 천 두건, 6101 천 한손검, 6102 천 활, 6301 판금 한손검. 규칙에 어긋나는 ID, 무기·방어구의 WeaponType/보너스 열 오용, 음수 보너스, 중복 ID는 서버 시작을 중단합니다.
- 클라 전용 열: `DisplayName`(FText), `Icon`(`T_Icon_<Item>`), `MeshM`/`MeshF`(스켈레탈 슬롯의 체형별 메시), `StaticMesh`(머리·무기 — 지금은 `Source/SM_<Item>_Src`). 부착 소켓·상대 변환·몸 숨김 파라미터는 아직 `Content/Python/jm_kit/items.json`에 있고, 이를 실제로 쓰는 MC4.1 장비 컴포넌트 작업 때 열을 확정해 옮깁니다.
- 초기 수치(2026-11-07): 방어구 부위당 천 +30 / 판금 +80 MaxHp(5부위 세트 650 / 900), 무기 천 +10 / 판금 +20 공격력. 등급 천 Common, 판금 Uncommon.
- 맨손은 행이 없습니다(무기 슬롯 비어 있음 = Unarmed). 현재 14행: 천·판금 × (5부위 + 한손검 + 활)([DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md)).
- UE: `FItemTableRow`(`Source/JM/Data/ItemTableRow.h`, enum `EWeaponType`·`EEquipSlot`·`EItemTier`·`EItemGrade`), `DT_ItemTable`, 조회 `UDataManager::FindItemById`.

### WorldTable (서버 전용, 2026-10-31 A1.3)
`Name`(worldId, 1001~), `MapName`(맵 리소스 파일 이름 = `GameMap::LoadMapData` 인자, 중복 불가), `CellSizeCm`(AOI 큰 셀 한 변 — 셀마다 쿼드트리, 100~100000), `QuadSplitCount`(쿼드트리 노드가 이 수를 넘으면 4분할, 1~1000), `QuadMinSizeCm`(이보다 작은 노드로는 나누지 않음, 100~CellSizeCm), `ViewRadiusCm`(목표 시야 반경 R — 이 안의 캐릭터는 반드시 보임, 100~100000), `ViewHysteresisCm`(H — 나갈 때는 R+H를 넘어야 사라짐, 0~R). 서버가 쓰는 맵(`Room.h` `ROOM_MAP_NAME`) 행이 없으면 시작을 중단합니다. 현재 행: 1001 ParagonSample(셀 5000, 분할 8, 최소 노드 500, R 5000, H 500 — R·H는 A1.3b 스크린샷으로 결정). UE는 이 테이블을 임포트하지 않습니다.

## 조회
- UE 쪽 조회는 `UDataManager`(GameInstanceSubsystem)를 통해서만 합니다. DataTable 애셋 경로는 코드에 쓰지 않고 `UDeveloperSettings`(`DefaultGame.ini`)에서 지정합니다. 테이블별 `Find<Table>Row(int32 Id)`가 행 포인터 또는 nullptr를 반환합니다.
- Test의 `UDataManager`/`UDataTableSettings` 구현을 이식하되 `JM` 접두사 규칙(UE와 충돌할 때만)을 따릅니다.

## MovementTable 컬럼
ID `1001` = 플레이어 기본값. 클라이언트(`DT_MovementTable`)와 서버(`DataManager`, `Server`로 시작하는 열만)가 함께 읽습니다. 최고 이동 속도는 이 테이블에 두지 않고 **서버 스탯 `MoveSpeed`**(플레이어 `PlayerStatTable`, `ObjectInfo.stat.move_speed`)를 씁니다 — 클라이언트 `MaxWalkSpeed`와 서버 검증이 같은 값을 봅니다(2026-10-30 S1, `MaxSpeedCmSec` 열 삭제).

| 컬럼 | 기본값 | 읽는 쪽 | 설명 |
|---|---|---|---|
| `AccelCmSec2` | 2048 | 클라 | 이동 입력 시 가속도 |
| `BrakeDecelCmSec2` | 2048 | 클라 | 입력 없을 때 감속도 |
| `RotationRateYawDegSec` | 500 | 클라 | 이동 방향으로 도는 속도(deg/s) |
| `CameraArmLengthCm` | 450 | 클라 | 카메라 붐 길이 (3인칭 마우스 회전) |
| `CameraDefaultPitchDeg` | -15 | 클라 | 시작 카메라 피치 (음수 = 아래로 내려다봄) |
| `CameraPivotHeightCm` / `CameraShoulderOffsetCm` | 150 / 0 | 클라 | 카메라 피벗 높이 / 어깨 오프셋 |
| `CameraMinPitchDeg` / `CameraMaxPitchDeg` | -60 / 30 | 클라 | 카메라 피치 제한 |
| `LookSensitivityYaw` / `LookSensitivityPitch` | 1 / 1 | 클라 | 마우스 감도 배율 |
| `MoveSendIntervalMs` | 200 | 클라 | 움직이는 동안 내 위치를 보내는 주기(정지 중에는 보내지 않음) |
| `RemoteInterpSpeed` | 10 | 클라 | 원격 캐릭터를 서버 위치로 보간하는 속도 |
| `RemoteSnapDistCm` | 500 | 클라 | 이보다 멀면 보간 없이 순간이동 |
| `RemoteExtrapolateMaxMs` | 300 | 클라 | 원격 **플레이어**를 마지막 패킷 이후 속도로 앞질러 보내는 최대 시간(몬스터는 상한 없음 — 서버가 방향이 바뀔 때만 보냄) |
| `ServerSpeedTolerance` | 1.2 | 서버 | 허용 속도 배율(MoveSpeed × 배율), 1~3 |
| `ServerMoveBudgetMs` | 1000 | 서버 | 이동 거리 예산(토큰 버킷)을 몇 ms 분량까지 모으는가. 지연으로 몰려 온 패킷을 흡수 |
| `ServerMoveSlackCm` | 50 | 서버 | 예산과 별도로 항상 인정하는 여유 거리 |
| `ServerMinMoveIntervalMs` | 50 | 서버·클라 | 이보다 잦은 이동 패킷은 서버가 처리하지 않음. 클라이언트는 이 간격 안의 전송을 미룬다 |
| `ServerNavToleranceCm` | 50 | 서버 | 목표가 내비메시 경계 밖으로 이만큼(수평)까지는 허용. 내비메시는 벽에서 34cm 안쪽까지 깔리므로 68cm + 벽 두께보다 작아야 벽을 뚫지 못함, 0~100 |
| `ServerWeaponSwapLockMs` | 1000 | 서버 | 무기 슬롯이 바뀌면(착용·해제 모두) 이 시간 동안 공용 행동 잠금 — 스왑으로 쿨타임 우회 방지(MC3.2), 0~5000 |
| `ServerEquipMinIntervalMs` | 200 | 서버 | 같은 캐릭터의 장착 요청 최소 간격(슬롯 구분 없음). 더 잦으면 `EQUIP_RESULT_TOO_FREQUENT`(MC3.2), 0~5000 |

서버는 값이 범위를 벗어나거나 1001 행·열이 없으면 시작을 중단합니다.

**Phase MC 목표 열**: `SheatheDelaySec`(5, 클라 — 마지막 공격·피격 후 무기를 등에 메기까지).
