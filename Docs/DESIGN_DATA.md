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
| `CameraArmLengthCm` | 400 | 카메라 붐 길이 |
| `CameraMinPitchDeg` / `CameraMaxPitchDeg` | -60 / 30 | 카메라 피치 제한 |
| `LookSensitivityYaw` / `LookSensitivityPitch` | 1 / 1 | 마우스 감도 배율 |

> 주의: 이동 속도는 **서버가 위치를 계산**하므로 `MaxSpeedCmSec`의 권위는 서버입니다. 서버 이동 코드가 이 값을 읽도록 맞추는 작업이 필요합니다(병합 ROADMAP 참고).
