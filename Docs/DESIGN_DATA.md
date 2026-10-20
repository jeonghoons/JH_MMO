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
| StatTable | 캐릭터/몬스터 기본 스탯 |
| HeroTable | 영웅 선택 한 행 = 영웅 ID, 메시, AnimBP, 기본 스탯 ID (Greystone, Sparrow로 시작) |
| WeaponItemTable | 무기 아이템: ItemId, 장착 가능 HeroId, 스탯 보너스, 슬롯별 머티리얼 경로(클라 전용) |
| SkillTable | `HeroId`, 쿨타임, 시전시간, 판정 Shape, 타겟팅 타입, 범위, 데미지 계수, 몽타주 경로(클라 전용) |
| MonsterTable / ItemTable / UITable | 해당 기능 작업 시 |

기존 `DT_Appearance`/`DT_Equip`/`DT_CharacterAsset`과 `UJMAssetDataManager`의 Mock DB는 `HeroTable`/`WeaponItemTable`로 대체합니다([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)). 대체 시점은 ROADMAP M2/M3.

## 서버 로더 (구현됨, 2026-10-20)
- `Server/Server/CsvFile.h/.cpp`가 CSV를 파싱하고(UTF-8·BOM·따옴표 지원, 컬럼 수/숫자 형식 검증), `DataManager::Init()`(`ServerData.cpp`)가 테이블을 읽어 `GetCharacterData`/`GetSkillData`로 제공합니다.
- 데이터 폴더는 환경변수 `JH_DATA_DIR`, 없으면 `../../Common/DataTable` 등 서버 작업 디렉터리 기준 후보 경로에서 찾습니다.
- **형식 오류·중복 키·존재하지 않는 참조는 서버 시작을 중단**합니다(줄 번호 포함 메시지). 데이터 버그를 조용히 넘기지 않기 위함입니다.
- 수치 변경은 CSV 수정 후 **서버 재시작**으로 반영됩니다(핫 리로드 없음).

### StatTable (서버 구현됨)
`Name`(ID, 1001~), `PlayerType`(`Enum.proto`의 PlayerType 값, 서버 조회 키·중복 불가), `MaxHp`, `AttackDamage`, `AttackSpeed`, `MoveSpeed`. 생성 시 HP는 `MaxHp`입니다. 현재 행: 몬스터(3), 전사(4), 궁수(5).

### SkillTable (서버 구현됨)
`Name`(skillId), `PlayerType`(영웅 종속), `Slot`(0 = 기본 공격, 1~5 = 숫자키), `TargetingType`(`Targeted`/`NonTargeted`), `HitShape`(`Sector`/`Line`/`Projectile`/`Zone`), `HitDelayMs`, `CooldownMs`, `CastRange`(Targeted는 대상까지, Zone은 지점까지 최대 거리), `Radius`, `Angle`, `Range`, `Width`, `BaseDamage`, `ProjSpeed`, `DurationMs`/`TickMs`(Zone: 지속 시간 / 한 대상에게 피해를 주는 최소 간격 — 진입 즉시 피해 후 `TickMs`마다 반복).
- 클라이언트는 skillId가 아니라 **슬롯**을 보내고, 서버가 영웅의 SkillTable로 해석합니다(다른 영웅의 스킬 사용 불가). 쿨타임은 스킬별이며 기본 공격(슬롯 0)만 공격 속도 배율을 반영합니다.
- 현재 행: 301(몬스터), 401/402(전사 기본·광역 강타), 501/502(궁수 기본 타겟팅 투사체·지면 장판). skillId 규칙은 `PlayerType*100+Slot+1`(기존 기본 공격 id 유지)이며, `HeroId` 기반 ID 체계(1001~)로의 재편은 M4에서 합니다.

### MonsterTable (서버 구현됨)
`Name`(=스폰 마커의 `monsterTemplateId`, StatTable.PlayerType과 동일해야 함), `RespawnSec`(사망 후 리스폰까지), `DeathRemoveDelaySec`(시체를 남기는 시간, `<= RespawnSec`), `HitStunMs`(피격 후 행동이 막히는 시간, 0~10000 — 피격 모션 길이에 맞춘다). 사망 연출 길이에 맞춰 `DeathRemoveDelaySec`를 조절합니다. 스폰 위치/수는 맵 JSON의 `spawnRadius`/`maxCount`를 따릅니다. 보상(경험치/골드) 컬럼은 A2.3에서 추가합니다.

## 조회
- UE 쪽 조회는 `UDataManager`(GameInstanceSubsystem)를 통해서만 합니다. DataTable 애셋 경로는 코드에 쓰지 않고 `UDeveloperSettings`(`DefaultGame.ini`)에서 지정합니다. 테이블별 `Find<Table>Row(int32 Id)`가 행 포인터 또는 nullptr를 반환합니다.
- Test의 `UDataManager`/`UDataTableSettings` 구현을 이식하되 `JM` 접두사 규칙(UE와 충돌할 때만)을 따릅니다.

## MovementTable 컬럼
ID `1001` = 플레이어 기본값.

| 컬럼 | 기본값 | 설명 |
|---|---|---|
| `MaxSpeedCmSec` | 600 | 자동 달리기 최고 속도(cm/s) |
| `AccelCmSec2` | 2048 | 이동 입력 시 가속도 |
| `BrakeDecelCmSec2` | 2048 | 입력 없을 때 감속도 |
| `RotationRateYawDegSec` | 500 | 이동 방향으로 도는 속도(deg/s) |
| `CameraArmLengthCm` | 450 | 카메라 붐 길이 (3인칭 마우스 회전) |
| `CameraDefaultPitchDeg` | -15 | 시작 카메라 피치 (음수 = 아래로 내려다봄) |
| `CameraMinPitchDeg` / `CameraMaxPitchDeg` | -60 / 30 | 카메라 피치 제한 |
| `LookSensitivityYaw` / `LookSensitivityPitch` | 1 / 1 | 마우스 감도 배율 |

> 주의: 이동 속도는 **서버가 위치를 계산**하므로 `MaxSpeedCmSec`의 권위는 서버입니다. 서버 이동 코드가 이 값을 읽도록 맞추는 작업이 필요합니다(병합 ROADMAP 참고).
