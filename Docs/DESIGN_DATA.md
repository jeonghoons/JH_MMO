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
| xlsx | `Common/DataTable/<Table>.xlsx` | 편집용(선택). CSV와 같은 이름, 같은 폴더. 2026-11-10 기준 없음(유일했던 `MovementTable.xlsx`는 CSV와 어긋나 삭제) — 만들면 CSV와 함께 유지 |
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
| ItemTable | 장비 아이템(슬롯, 등급, 티어, 스탯 보너스, 무기 타입, 외형 경로는 클라 전용) + 소비 아이템 — MC3.1 구현, 2026-11-16 A3.2에서 열 추가(`RequiredLevel`·`MaxStack`·`UseEffect`·`UseValue`·`UseCooldownMs`)와 HP 물약 8001([DESIGN_ITEMS_TRADE.md](DESIGN_ITEMS_TRADE.md)) |
| DropTable | (2026-11-16 A3.2 서버 구현) 몬스터별 드랍 — 행마다 독립 확률, `ItemId` 0 = 골드. 서버 전용. 열은 [DESIGN_ITEMS_TRADE.md](DESIGN_ITEMS_TRADE.md) |
| EconomyTable | (2026-11-16 A3.2 서버 구현) 인벤토리 칸 수, 드랍 소유·소멸 시간, 줍기·거래 거리, 거래 시간 제한, 경매 기간·등록 수·가격 범위, 파티 정원·초대 응답 시간(2026-11-20 DG2). 서버 전용. 열은 [DESIGN_ITEMS_TRADE.md](DESIGN_ITEMS_TRADE.md) |
| SkillTable | `WeaponType`(무기 타입 종속), 슬롯, 쿨타임, 판정 Shape, 타겟팅 타입, 범위, 데미지, 몽타주 경로(클라 전용) |
| MonsterTable | 몬스터 종류별 스탯·스킬·리스폰 + 행동(배회·인식·추격 제한·복귀 방식, 2026-11-18 L3) + 크기·몸 반경·메시(2026-11-18 MN1, 메시는 클라 전용) (ID = `ObjectInfo.template_id`) |
| SpawnTable | (2026-11-18 L3 서버 구현) 맵·구역별 스폰 몬스터 종류·스폰 지점당 수·리스폰 — 서버 전용 |
| ShopTable | (2026-11-18 L4) 상점별 판매 품목·구매가 — 서버 전용. 열은 [DESIGN_ITEMS_TRADE.md](DESIGN_ITEMS_TRADE.md) "상점 NPC" |
| NpcTable | (2026-11-18 L4) NPC 종류별 이름·상점·체형·장착 외형 (ID = 마커 `npcTemplateId` = `ObjectInfo.template_id`) — 서버 전용 |
| QuestTable | (2026-11-22 Q1) 퀘스트별 NPC·종류(처치/수집)·대상·수량·권장 레벨·보상(경험치·골드·아이템) + 이름·설명(클라 전용) — 서버·UE 공용, `balance.py --write`가 생성 |
| UIStyleTable | (2026-11-22 UI1, D-270) **UE 전용(서버는 읽지 않음)** — 스타일 키(Body·Title)별 글꼴 에셋·대신 쓸 글자체. 글자 크기·색은 각 위젯이 정함 |
| UITable | 해당 기능 작업 시 |
| WorldTable | 맵별 시야(AOI) 수치·부활 규칙 — 서버 전용(2026-10-31 A1.3, 2026-11-16 A2.4) |
| BodyTable | 체형별 기준 캐릭터 구성(리더·눈·이·헤어·몸 머티리얼·AnimBP, 공용 피격·사망 몽타주) — 클라 전용(2026-11-07 MC4.1, 2026-11-08 MC4.2) |
| WeaponTypeTable | 무기 타입별 연출(레이어 AnimBP, 이후 등 소켓·넣기 지연) — 클라 전용(2026-11-08 MC4.2) |

2026-11-04 Phase MC: 계획했던 `HeroTable`/`WeaponItemTable`은 만들지 않고 **ItemTable**로 대체합니다. 플레이어 메시·AnimBP는 매니퀸 공용이라 테이블이 필요 없고, `DT_CharacterAsset`은 2026-11-10 삭제했습니다([DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)).

## 서버 로더 (구현됨, 2026-10-20)
- `CsvFile`(ServerCore `Utils/`)이 CSV를 파싱하고(UTF-8·BOM·따옴표 지원, 컬럼 수/숫자 형식 검증), `DataManager::Init()`(`Server/Server/Data/ServerData.cpp`)가 테이블을 읽어 `GetCharacterData`/`GetSkillData`/`GetSkillBySlot`/`GetMonsterData`/`GetItemData`/`GetDrops`/`GetEconomy`/`GetSpawns` 등으로 제공합니다.
- 데이터 폴더는 환경변수 `JH_DATA_DIR`, 없으면 `../../Common/DataTable` 등 서버 작업 디렉터리 기준 후보 경로에서 찾습니다.
- **형식 오류·중복 키·존재하지 않는 참조는 서버 시작을 중단**합니다(줄 번호 포함 메시지). 데이터 버그를 조용히 넘기지 않기 위함입니다.
- 수치 변경은 CSV 수정 후 **서버 재시작**으로 반영됩니다(핫 리로드 없음).

### PlayerStatTable (서버 구현됨, 2026-11-07 MC3.1b — 옛 StatTable)
플레이어 **레벨별** 기본 스탯이며 **체형 공용**입니다(MALE/FEMALE은 외형만 다름). `Name`(ID, 1001~), `Level`(서버 조회 키, 1 이상·중복 불가), `MaxHp`, `AttackDamage`, `AttackSpeed`, `MoveSpeed`. 현재 행: 1001 = Level 1(MaxHp 500, AttackDamage 0, AttackSpeed 1.0, MoveSpeed 500). 시작 레벨(`PLAYER_START_LEVEL` = 1) 행이 없으면 서버 시작을 중단합니다.
- 생성 시 HP는 `MaxHp`입니다. 가입 시 DB `Player_Info`에 저장하는 Level·Hp도 이 행에서 가져옵니다(SQL에 수치를 쓰지 않음).
- 레벨 성장(행 추가, 필요 경험치 열)은 A2.3, DB에 저장된 레벨로 조회하는 것은 A2.5(`InitFromDb` 연결)에서 합니다. 그 전까지 모든 플레이어는 Level 1 스탯입니다.
- 최종 스탯 = 기본 + 장비 보너스(무기 `AttackDamage`, 방어구 `MaxHp`)이며 서버가 계산합니다(MC3.2).
- 클라이언트도 `DT_PlayerStatTable`(Row `FPlayerStatTableRow`)로 가져오지만 **HUD 경험치 바의 분모 `ExpToNext`만** 읽습니다(`UDataManager::GetExpToNext`, 0 = 최대 레벨 → "MAX" — D-260). 스탯 값은 서버 `ObjectInfo.stat`을 씁니다. CSV를 고치면 `reimport_csv`로 `DT_PlayerStatTable`도 다시 채웁니다.
- **피해 = 스킬 `BaseDamage` + 시전자 `AttackDamage`**(플레이어·몬스터 같은 공식, 시전 순간 확정 — 2026-11-07 MC3.1b). 방어력(받는 피해 감소)은 나중에(A2.3 무렵) `Character::OnDamaged`에 넣습니다.
- 가입은 MALE/FEMALE만 받습니다. DB에 남은 옛 4·5 캐릭터(봇 `dummy` 계정 포함)는 로그인할 때 서버가 메모리에서 MALE로 바꾸고, DB 행은 MC3.3 마이그레이션에서 바꿉니다.
- `PlayerType`(`Enum.proto`)은 **플레이어 체형 전용**입니다(NONE/MALE/FEMALE, 3·4·5는 `reserved`). 몬스터는 `ObjectType`으로 구분하고 종류는 `ObjectInfo.template_id`로 보냅니다.

### SkillTable (서버 구현됨)
`Name`(skillId), `WeaponType`(무기 종속 — `Unarmed`/`Sword`/`Bow`/`Monster`), `Slot`(0 = 기본 공격, 1~5 = 숫자키), `TargetingType`(`Targeted`/`NonTargeted`), `HitShape`(`Sector`/`Line`/`Projectile`/`Zone`), `HitDelayMs`, `CooldownMs`, `CastRange`(Targeted는 대상까지, Zone은 지점까지 최대 거리), `Radius`, `Angle`, `Range`, `Width`, `BaseDamage`, `ProjSpeed`, `DurationMs`/`TickMs`(Zone: 지속 시간 / 한 대상에게 피해를 주는 최소 간격 — 진입 즉시 피해 후 `TickMs`마다 반복), `AimMoveSpeedMul`(스킬 조준 중 이동 속도 배율, 0 < 값 ≤ 1)·`AimMaxMs`(조준 최대 유지 시간, 0 = 조준할 수 없는 스킬 — 기본 공격 Slot 0은 반드시 0, 0~60000. 2026-11-09 MC4.2d-1: 3002 = 0.4/10000, 2002 = 1.0/10000, 나머지 1.0/0), `MoveLockMs`(2026-11-18 CB1, D-199, `AnimMs` 뒤 — 시전부터 이동·몸 방향을 잠그는 시간 = 휘두르기가 끝나는 시각, `0 <= MoveLockMs <= AnimMs`, 0 = 잠그지 않음, 기본 공격 Slot 0은 `AttackSpeed`로 나눔. 몽타주의 `UAnimNotify_SwingEnd`("Swing End") 시각을 `SkillTimingExport`(`ExportSkillTiming.bat`)가 써 넣고 노티파이가 없으면 CSV 값 유지. 초기값은 판정 시점 + 0.2초 초안: 1001 614, 2001 820, 2002 1450, 3001 1413, 3002 380, 몬스터 9001~9003은 0). 클라이언트 전용 열(서버 로더는 무시): `Fx`/`FxBaseRadius`(이펙트), `Montage`(시전 몽타주), `Icon`(전투 HUD 슬롯 아이콘 텍스처).
- 클라이언트는 skillId가 아니라 **슬롯**을 보내고, 서버가 캐릭터의 현재 무기 타입으로 SkillTable을 해석합니다(다른 무기의 스킬 사용 불가). 쿨타임은 스킬별이며 기본 공격(슬롯 0)만 공격 속도 배율을 반영합니다.
- (2026-11-07 MC3.1) 서버 조회 키 = (WeaponType, Slot), 중복 불가. **ID는 4자리, 첫째 자리 = 무기 종류**(맨손 1xxx, 검 2xxx, 활 3xxx, 몬스터 9xxx — 어긋나면 서버 시작 중단). 현재 행: 1001 맨손 기본 공격(검 기본 공격의 60% 피해 30, 반경 150, 시간은 검과 같음 — MC4.2에서 맨손 몽타주로 다시 뽑음), 2001/2002 검(옛 401/402), 3001/3002 활(옛 501/502), 9001 몬스터(옛 301). 맨손 기본 공격(Unarmed, 0) 행이 없으면 시작을 중단합니다. (2026-11-18 MN1, D-198) 몬스터 스킬은 종류별 행으로 공격 거리를 나눕니다 — 9001 하급(반경 170 → 120), 9002 중급(반경 187, 피해 40), 9003 보스(반경 240, 피해 60), 각도 150·판정 400ms·몽타주 `AM_Rampage_Attack`은 같음. 지금 8행.
- `Monster` 스킬은 슬롯 색인에 넣지 않고 `MonsterTable.SkillId`로만 찾습니다(몬스터 종류마다 슬롯 0이 따로 있을 수 있음).
- 몽타주 열은 아직 옛 Greystone/Sparrow 몽타주를 가리키고, MC4.2에서 `AM_<WeaponType>_<Action>`으로 바뀝니다. 맨손 행은 몽타주가 비어 있어 클라이언트가 캐릭터 기본 공격 몽타주를 씁니다. 상세 [DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md).
- 플레이어의 현재 무기 타입: MC3.1에서는 장착 시스템이 없어 항상 `Unarmed`(서버 `Player::GetWeaponType`, 클라 `AJMPlayer::GetWeaponType`) — MC3.2에서 장착 무기의 `ItemTable.WeaponType`으로 바꿉니다.
- (2026-11-22 WT1, 2일차 서버) 무기 종류 `TwoHandSword`(ID 첫째 자리 4)가 추가됐습니다. 현재 행: 4001 양손검 기본 공격(BaseDamage 60, AnimMs 1267 = `AM_TwoHandSword_Attack` 길이), 4002 양손검 스킬1(BaseDamage 100, AnimMs 1867 = `AM_TwoHandSword_Skill1`, CooldownMs 6000, MpCost 30). **`HitDelayMs`·`MoveLockMs`(760·700, 1100·1160)는 임시값**이고 `ExportSkillTiming.bat`을 에디터에서 다시 돌려 확정합니다. 피해 값은 `balance.py`가 만든 것이며 DPS 비율 결정이 남아 있습니다(`DESIGN_BALANCE.md` §5, 보고 참고).

### MonsterTable (서버 구현됨)
`Name`(몬스터 종류 ID, 1001~ = 스폰 마커의 `monsterTemplateId` = `ObjectInfo.template_id`), `SkillId`, `MaxHp`·`AttackDamage`·`AttackSpeed`·`MoveSpeed`(몬스터 고정 스탯 — 2026-11-07 MC3.1b에 StatTable에서 옮김, 범위는 PlayerStatTable과 같음), `RespawnSec`(사망 후 리스폰까지), `DeathRemoveDelaySec`(시체를 남기는 시간, `<= RespawnSec`), `HitStunMs`(피격 후 행동이 막히는 시간, 0~10000 — 피격 모션 길이에 맞춘다), `WanderRadiusCm`(대기 중 배회: 홈(스폰된 자리)에서 이 반경 안 임의 지점, 0~10000 — 2026-10-31 A1.3c에서 AOI 셀과 분리한 임시값, 행동트리 개선 때 재검토). 사망 연출 길이에 맞춰 `DeathRemoveDelaySec`를 조절합니다. 스폰 위치·반경은 맵 JSON(`spawnRadius`), 종류·수·리스폰은 마커에 구역 이름(`zoneName`)이 있으면 SpawnTable을 따릅니다(구역 이름이 없는 옛 맵 마커는 `monsterTemplateId`/`maxCount`). 보상(경험치/골드) 컬럼은 A2.3에서 추가합니다.
- **행동 열(2026-11-18 L3, D-193, `WanderRadiusCm` 뒤)**: `AggroRangeCm`(인식 범위 — 몬스터 기준, 0 = 비선공(맞아야 추격), 0~5000), `LeashRangeCm`(추격 제한 — 홈 = 실제 스폰된 자리 기준, 0 초과 10000 이하이고 `WanderRadiusCm` 이상), `ReturnMode`(0 = 걸어서 복귀·피해 받음·HP 유지 / 1 = 복귀 중 피해 무시 + 홈 도착 시 HP 전량 회복, 서버 상수 `MONSTER_RETURN_WALK`·`MONSTER_RETURN_EVADE`). 범위를 벗어나면 서버 시작을 중단합니다. 맞으면 선공·비선공 모두 공격자를 추격하고(거리 무관), 몬스터가 홈에서 `LeashRangeCm`를 넘으면 추격을 포기하고 `RETURN` 상태로 홈까지 걸어갑니다.
- L3 때 행(2026-11-18, 초안 수치 — MN1 변경은 아래): 1001 하급 — SkillId 9001, MaxHp 150, AttackDamage 20, AttackSpeed 1.0, MoveSpeed 400, RespawnSec 10, DeathRemoveDelaySec 3, HitStunMs 800, WanderRadiusCm 500, AggroRangeCm 0(비선공), LeashRangeCm 2500, ReturnMode 0. 1002 중급 — SkillId 9001, MaxHp 300, AttackDamage 30, AttackSpeed 1.0, MoveSpeed 400, RespawnSec 15, DeathRemoveDelaySec 3, HitStunMs 800, WanderRadiusCm 500, AggroRangeCm 800(선공), LeashRangeCm 3000, ReturnMode 1.
- **크기·외형 열(2026-11-18 MN1, D-198, 끝에)**: `Scale`(크기 배율 0.3~3 — 클라이언트가 액터 전체에 곱함: 캡슐·메시·HP 막대 위치), `BodyRadiusCm`(몸 반경 0~500 — 서버가 이 몬스터를 맞히는 판정에 더함, 기준 캡슐 반경 42cm × Scale), `Mesh`(클라이언트 전용 스켈레탈 메시 경로, 서버는 읽지 않음). 서버 판정은 `Character::GetBodyRadius()`(플레이어는 0 = 중심점) — 상세 [DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md).
- 현재 행(2026-11-18 MN1, 초안 수치 — 1001·1002의 행동 열까지는 위 L3 행과 같고 1002의 SkillId만 9002로 바뀜): 1001 하급 — SkillId 9001, Scale 0.7, BodyRadiusCm 29, Mesh 기본 `Rampage`. 1002 중급 — SkillId 9002, Scale 1.1, BodyRadiusCm 46, Mesh `Rampage_Elemental`. 1003 보스 — SkillId 9003, MaxHp 1500, AttackDamage 50, AttackSpeed 1.0, MoveSpeed 380, RespawnSec 120, DeathRemoveDelaySec 3, HitStunMs 800, WanderRadiusCm 300, AggroRangeCm 1200, LeashRangeCm 4000, ReturnMode 1, Scale 1.4, BodyRadiusCm 59, Mesh `Rampage_Redneck`. 세 메시는 모두 스켈레톤 `Rampage_Skeleton`이라 `ABP_Rampage`·몽타주를 그대로 씁니다(메시는 git 제외 팩 `Content/ParagonRampage/` — M3 때 Move).
- `SkillId`(2026-11-07 MC3.1, 둘째 열, 필수): 기본 공격 스킬. SkillTable에 없거나 `WeaponType`이 `Monster`가 아니면 서버 시작을 중단합니다. 몬스터 스킬은 `PlayerType`이 아니라 이 열로 찾습니다.
- 1001의 유래(2026-11-07 MC3.1b): 1001(Rampage, 옛 ID 3) — SkillId 9001, MaxHp 150, AttackDamage 20, AttackSpeed 1.0, MoveSpeed 400. 몬스터 피해는 스킬 `BaseDamage`(9001 = 40) + 고정 공격력 20 = 60입니다. 몬스터 세기는 이 공격력으로 조절합니다.
- 1001 미만 ID는 서버 시작을 중단합니다(옛 PlayerType 값을 막기 위함). 맵 로직 JSON의 `monsterTemplateId`가 이 테이블에 없으면 서버는 그 마커를 스폰하지 않고 ERROR를 남깁니다(방은 실행 중에도 만들어지므로 중단하지 않음). `ParagonSample_Logic.json`은 레벨에 `ALevelMarker`가 없어 손으로 관리합니다.
- 클라이언트는 몬스터를 `object_type`으로 판별하고, 종류별 외형은 `template_id`로 이 테이블의 행을 찾아 정합니다(2026-11-18 MN1): UE `FMonsterTableRow`(`Source/JM/Data/MonsterTableRow.h`, CSV 열 1:1), `DT_MonsterTable`(`UJMDataSettings.MonsterTable`), 조회 `UDataManager::FindMonster(TemplateId)`, `UDataManager::PreloadGameAssets`가 몬스터 메시도 미리 로드. `ANpcCharacter::ApplyMonsterAppearance`가 스폰 때 한 번 메시를 바꾸고(`SetSkeletalMeshAsset`) `SetActorScale3D(Scale)` — 행이 없으면 경고 로그 후 `BP_Npc` 기본 외형. **MonsterTable.csv를 고치면 `DataTableReimportTools.reimport_csv`로 `DT_MonsterTable`을 다시 채워야 클라이언트에 반영됩니다.** A2.3 보상(경험치·골드)과 A3 `DropTable`은 이 ID를 참조합니다.

### ItemTable (2026-11-07 MC3.1 확정, 서버 구현됨)
- 서버가 읽는 열: `Name`(itemId), `Slot`(`Head`/`Top`/`Bottom`/`Gloves`/`Boots`/`Weapon`/`Offhand` — `Offhand` = 방패, MC4.2f), `Grade`(`Common`/`Uncommon`/`Rare`), `Tier`(`Cloth`/`Leather`/`Plate`), `WeaponType`(무기만 `Sword`/`Bow`, 나머지 `None`), `BonusAttackDamage`(무기만), `BonusMaxHp`(방어구만), `Starter`(1 = 가입 시 기본 장비, 0/1만, 슬롯당 최대 1개, 기본 보조손이 있으면 기본 무기는 Sword — MC3.3·MC4.2f; 마이그레이션 SQL `migrations/0002_2026-11-07_mc33_equip.sql`의 목록과 같아야 함).
- **itemId 규칙 = 4자리 [슬롯][티어][일련번호 01~99]**: 슬롯 번호 Head 1·Top 2·Bottom 3·Gloves 4·Boots 5·Weapon 6·Offhand 7(MC3.2의 새 `EquipPart` 번호와 같음), 티어 번호 Cloth 1·Leather 2·Plate 3. 예: 1101 천 두건, 6101 천 한손검, 6102 천 활, 6301 판금 한손검. 규칙에 어긋나는 ID, 무기·방어구의 WeaponType/보너스 열 오용, 음수 보너스, 중복 ID는 서버 시작을 중단합니다.
- 클라 전용 열: `DisplayName`(FText), `Icon`(`T_Icon_<Item>`), `MeshM`/`MeshF`(스켈레탈 슬롯의 체형별 메시), `StaticMesh`(머리·무기·보조손 — 지금은 `Source/SM_<Item>_Src`), MC4.1 외형 열 `AttachSocket`(머리·무기가 붙는 본/소켓), `AttachM`/`AttachF`(체형별 소켓 기준 상대 변환, FTransform 텍스트), `Mask`(입었을 때 몸 마스크 파라미터 `FBodyMaskParams` — 슬롯 기본값 + 아이템 덮어쓰기를 합친 값), `HideHair`(1 = 헤어 숨김, 두건·투구), MC4.2d-2 `BackSocket`·`BackM`·`BackF`(무기를 등에 멜 때의 본과 체형별 상대 변환 — 비면 등에 메지 않음, 지금 활만). **외형 열(`AttachSocket`~`BackF`)은 `jm_kit.item_table.export`(MCP `item_table_export`)가 `items.json`·`kit_profiles.json`에서 생성하므로 손으로 고치지 않습니다**(`DECISIONS.md` 2026-11-07 MC4.1).
- 초기 수치(2026-11-07): 방어구 부위당 천 +30 / 판금 +80 MaxHp(5부위 세트 650 / 900), 무기 천 +10 / 판금 +20 공격력. 등급 천 Common, 판금 Uncommon.
- 맨손은 행이 없습니다(무기 슬롯 비어 있음 = Unarmed). 현재 16행: 천·판금 × (5부위 + 한손검 + 활 + 방패 — 방패 7101·7301은 MC4.2f, 무기가 아닌 장비라 WeaponType `None`·보너스 0)([DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md)).
- UE: `FItemTableRow`(`Source/JM/Data/ItemTableRow.h`, enum `EWeaponType`·`EEquipSlot`·`EItemTier`·`EItemGrade`·`EItemUseEffect`), `DT_ItemTable`, 조회 `UDataManager::FindItemById`.
- **A3.2 추가 열(2026-11-16, 서버·클라 공용, `Starter` 뒤)**: `RequiredLevel`(1 이상, 장착·사용 레벨), `MaxStack`(1~9999), `UseEffect`(`None`/`HealHp`), `UseValue`(0 이상, `HealHp` 회복량), `UseCooldownMs`(0~600000). 열 설명은 [DESIGN_ITEMS_TRADE.md](DESIGN_ITEMS_TRADE.md) "데이터".
- **장착할 수 없는 아이템(소비 아이템)**: `Slot` `None` + `Tier` `None`(둘은 항상 함께), itemId 첫째 자리 8·둘째 자리 0. 서버 검증(어기면 시작 중단): 장비는 `MaxStack` 1·`UseEffect` `None`, `Slot` `None`은 `WeaponType` `None`·보너스 0·`Starter` 0·`UseEffect`가 `None`이 아님, `HealHp`는 `UseValue` > 0. 현재 행: 8001 HP 물약(`MaxStack` 20, `HealHp` 200, 재사용 5000ms — 초기값). 외형 열은 기본값(빈 소켓·단위 변환·마스크 0)을 손으로 넣고, `jm_kit.item_table.export`는 `Slot` `None` 행을 건너뜁니다(결과 `skipped`). 아이콘은 `Content/UI/Icons/Item/T_Icon_<이름>`(물약은 임시 그림, `ExternalAssets.md`).

- **L4 추가 열(2026-11-18, D-201, 서버·클라 공용, `UseCooldownMs` 뒤)**: `SellPrice`(상점에 팔 때 1개당 골드, 0 = 팔 수 없음, 0 ~ `EconomyTable.GoldMax`). 현재 값(초안): 천 장비 8종 20, 판금 장비 8종 60, HP 물약 5. UE `FItemTableRow.SellPrice`(표시용) — CSV를 고치면 `DT_ItemTable`을 다시 임포트합니다.
- **IT3 추가(2026-11-22, 2일차 서버 — `Tools/Balance/balance.py --write`가 생성)**: 열 `BonusMaxMp`(투구만, `BonusMaxHp` 뒤), `Grade`에 `Epic`(03등급), `Tier`에 `Ornate`(03등급, 둘째 자리 4), `UseEffect`에 `HealMp`(`UseValue`만큼 MP 회복), 장비는 `BonusAttackDamage` 허용 범위가 장갑까지 넓어짐(무기·장갑만 공격 보너스). 전리품(`Slot` `None` + `UseEffect` `None`, `UseValue`·`UseCooldownMs` 0)을 허용합니다. 행 수 45(장비 00~03등급 36 + 물약 4 + 전리품 5). **외형 열은 새 행(01·03등급, 양손검 6103 제외 구 행의 값 보존)에서 비어 있으며**, 1301~7301은 같은 ID의 02등급으로 이름·능력치만 바꾸고 옛 메시 경로를 그대로 둡니다. UE `DT_ItemTable` 재임포트 전에 `Grade`·`Tier`·`UseEffect` enum 값을 추가해야 합니다.

### ShopTable (서버 전용, 2026-11-18 L4)
한 행 = 상점 하나가 파는 품목 하나(D-201). `Name`(행 ID, 1001 이상·중복 불가), `ShopId`(1 이상), `ItemId`(ItemTable ID), `BuyPrice`(1개 값, 1 ~ `EconomyTable.GoldMax`이고 그 아이템의 `SellPrice` 이상 — 사서 되팔아 골드가 늘지 않게). (`ShopId`, `ItemId`) 중복·없는 `ItemId`·범위 위반은 서버 시작을 중단합니다. EconomyTable·ItemTable 뒤에 읽고, 조회는 `DataManager::GetShopItems(shopId)`·`FindShopItem(shopId, itemId)`. 현재 행(`ShopId` 1, 초안): 1001 HP 물약 8001 = 30, 1002~1009 천 장비 1101·2101·3101·4101·5101·6101·6102·7101 = 각 100. 품목을 늘릴 때는 행만 추가합니다. 구매가는 `SC_SHOP_OPEN`으로 클라이언트에 전달하고 UE는 임포트하지 않습니다.

### QuestTable (서버·UE 공용, 2026-11-22 Q1 — D-259·D-261)
한 행 = 퀘스트 하나. 값의 원본은 `Tools/Balance/balance.py`의 `QUESTS`이고 `--write`가 CSV를 통째로 다시 씁니다(수치 근거는 [DESIGN_BALANCE.md](DESIGN_BALANCE.md) §9). 한 번만 할 수 있고 순서가 없습니다.

| 열 | 뜻 | 서버 로더 검사(어기면 시작 중단) |
|---|---|---|
| `Name` | 퀘스트 ID | 1 이상, 중복 불가 |
| `DisplayName` | 이름(클라 표시용) | 비어 있지 않음(값은 읽지 않음) |
| `NpcId` | 주고 완료를 받는 NPC(`NpcTable` ID) | `NpcTable`에 있고 상점(`ShopId`)·경매(`Auction`) NPC가 아님 |
| `Kind` | `Kill`(처치) / `Collect`(수집 — 완료할 때 회수) | 둘 중 하나 |
| `TargetId` | `Kill` = `MonsterTable` ID, `Collect` = `ItemTable` ID | 그 테이블에 있음 |
| `Count` | 처치 수 / 제출 수 | 1 이상. `Collect`는 `MaxStack` × `EconomyTable.InventorySlots` 이하 |
| `RecommendLevel` | 권장 레벨(안내용 — 수락을 막지 않음) | 1 ~ 최대 레벨 |
| `RewardExp`·`RewardGold` | 보상 | 0 이상, 경험치 10억 이하, 골드는 `EconomyTable.GoldMax` 이하 |
| `RewardItemId`·`RewardItemCount` | 보상 아이템(없으면 둘 다 0) | 있으면 `ItemTable`에 있고 수량 1 ~ 그 아이템의 `MaxStack` |
| `Description` | 설명(클라 전용) | 읽지 않음(열이 없어도 됨). 쉼표를 쓰지 않음 |

`NpcTable`·`ItemTable`·`MonsterTable`·`EconomyTable`·`PlayerStatTable` 뒤에 읽고, 조회는 `DataManager::GetQuestData(questId)`. 이 테이블이 가리키는 NPC가 **퀘스트 NPC**입니다(`NpcData.hasQuest` → `ObjectInfo.npc_kind` = `NPC_KIND_QUEST`) — `NpcTable`에 따로 열을 두지 않습니다. 현재 행: 1 초원 정리(1001 15마리) · 2 뿔 모으기(8052 10개) · 3 던전의 군주(1005 1회), 모두 NPC 1003. UE는 `DT_QuestTable`로 임포트해 이름·설명·목표·보상을 표시합니다(3일차). 로더 시험은 `Server/PerfData/2026-11-22_Q1/loader_test.sh`.

### NpcTable (서버 전용, 2026-11-18 L4)
퀘스트 NPC(2026-11-22 Q1): 1003 "경비대장"(`ShopId` 0·`Auction` 0, Male, 강철 투구·상의·하의·장갑·장화 + 강철 한손검·방패) 추가. 퀘스트를 주는지는 이 테이블이 아니라 `QuestTable.NpcId`가 정합니다(위 "QuestTable"). 맵 마커는 `Continent01`의 `pointId` 402(마을 북문 안쪽).

`Auction`(2026-11-21 A4.3·D-234, `ShopId` 뒤): 1 = 경매 관리인(경매 등록·취소·수령은 이 NPC 근처에서만), 0 = 아님. 1이면 `ShopId`는 0이어야 합니다(한 NPC가 상점과 경매를 겸하면 시작 중단). 클라이언트에는 `ObjectInfo.npc_kind`로 알립니다. 현재 행에 1002 "경매 관리인"(Female, 천 상의·하의·장갑·신발) 추가.

한 행 = NPC 종류 하나(D-202). `Name`(종류 ID, 1001 이상·중복 불가 = `Npc` 마커의 `npcTemplateId` = `ObjectInfo.template_id`), `DisplayName`(비어 있지 않음, `ObjectInfo.name`으로 전달), `ShopId`(0 = 상점 없음, 아니면 ShopTable에 행이 있는 값), `Body`(`Male`/`Female` — `ObjectInfo.player_type`), 장착 외형 7열 `Head`·`Top`·`Bottom`·`Gloves`·`Boots`·`Weapon`·`Offhand`(0 = 없음, 아니면 그 슬롯의 ItemTable ID — `ObjectInfo.equip_items`). 어기면 서버 시작을 중단합니다. ItemTable·ShopTable 뒤에 읽고, 조회는 `DataManager::GetNpcData(templateId)`. 마커의 종류가 이 테이블에 없으면 서버는 그 마커를 배치하지 않고 ERROR를 남깁니다(중단하지 않음). 현재 행(초안): 1001 "잡화 상인" — `ShopId` 1, `Male`, 판금 상의 2301·판금 하의 3301·천 장갑 4101·판금 장화 5301. UE는 임포트하지 않습니다(외형·이름은 `ObjectInfo`로 받음).

### DropTable (서버 전용, 2026-11-16 A3.2)
`Name`(행 ID, 1001~, 중복 불가), `MonsterId`(MonsterTable ID), `ItemId`(ItemTable ID, 0 = 골드), `DropRate`(만분율 0~10000), `MinCount`·`MaxCount`(1 ≤ Min ≤ Max, 아이템은 `ItemTable.MaxStack` 이하, 골드는 `EconomyTable.GoldMax` 이하). 처치 때 그 몬스터의 모든 행을 각각 독립으로 굴립니다(D-157, `DataManager::RollDrops` — 1~10000 난수가 `DropRate` 이하면 나오고 수량은 범위 안 균등). 없는 `MonsterId`·`ItemId`, (`MonsterId`, `ItemId`) 중복은 시작 중단. EconomyTable·ItemTable·MonsterTable 뒤에 읽습니다. 현재 행(초기값, 몬스터 1001): 골드 100% 10~30, HP 물약 30% 1~2개, 판금 방어구 5종·천 활·천 방패 각 5%, 판금 한손검·활·방패 각 3%. 몬스터 1002(2026-11-18 L3, 행 1013~1024)는 1001과 같은 구성이고 골드만 20~50. 보스 1003(2026-11-18 MN1, 행 1025~1034): 골드 100% 200~500, HP 물약 100% 2~5개, 판금 방어구 5종·판금 한손검·활·방패 각 30%. 지금 34행. UE는 임포트하지 않습니다.
- 빈도 확인: `GameServer.exe --roll-drops <횟수>` — DataTable만 읽고 몬스터마다 그 횟수만큼 굴린 결과(행별 비율·평균 수량)를 로그에 남긴 뒤 종료합니다(DB·포트를 열지 않음). 값을 바꾼 사본은 `JH_DATA_DIR`로 지정합니다.

### SpawnTable (서버 전용, 2026-11-18 L3)
스폰 마커는 위치·반경 + 구역 이름(`zoneName`)만 갖고, 그 구역에 무엇을 몇 마리 놓을지는 이 테이블에서 읽습니다(D-194). `Name`(행 ID, 1001 이상·중복 불가), `MapName`(`WorldTable`에 있는 맵), `Zone`(마커의 `zoneName`, 비어 있지 않고 `/` 없음), `MonsterTemplateId`(MonsterTable ID), `CountPerSpawn`(스폰 지점 하나당 수, 1~20), `RespawnSec`(0 = `MonsterTable.RespawnSec` 사용, 아니면 그 몬스터의 `DeathRemoveDelaySec` 이상 86400 이하). (MapName, Zone, MonsterTemplateId) 중복 불가. 어기면 서버 시작을 중단합니다. 한 구역에 여러 행이면 스폰 지점마다 행별 수만큼 섞어 놓습니다. WorldTable·MonsterTable 뒤에 읽고, 조회는 `DataManager::GetSpawns(mapName, zone)`. 마커의 구역이 이 테이블에 없으면 서버는 그 구역을 스폰하지 않고 ERROR를 남깁니다(구역마다 한 번, 중단하지 않음). 구역 이름이 없는 마커(옛 맵 `ParagonSample`)는 이 테이블을 보지 않고 마커의 `monsterTemplateId`·`maxCount`를 씁니다. UE는 임포트하지 않습니다.
- 현재 행(Continent01): 1001 `meadow_w`·1002 `meadow_e`·1003 `meadow_n` = 몬스터 1001 × 5, 1004 `forest_n` = 몬스터 1002 × 3, `RespawnSec` 모두 0. 스폰 지점 48곳(meadow_w 11·meadow_e 11·meadow_n 6·forest_n 20)이라 합계 28 × 5 + 20 × 3 = 200마리. (2026-11-18 MN1) 1005 `forest_boss` = 몬스터 1003(보스) × 1, `RespawnSec` 0 — 스폰 지점 1곳(숲 북쪽 (198m, 73m) 부근)을 더해 49곳·201마리.

### EconomyTable (서버 전용, 2026-11-16 A3.2)
행 `1001` 하나(행이 없거나 여럿이거나 ID가 다르면 시작 중단), 조회 `DataManager::GetEconomy()`. 열의 뜻과 초기값은 [DESIGN_ITEMS_TRADE.md](DESIGN_ITEMS_TRADE.md) "EconomyTable". 허용 범위(벗어나면 시작 중단): `InventorySlots` 1~200, `ItemMinIntervalMs` 0~5000, `GoldMax` 1~2000000000, `DropOwnerSec` 0~3600, `DropLifeSec` 1~3600(`DropOwnerSec` 이상), `DropScatterCm` 0~1000, `PickupRangeCm` 1~5000, `MaxDropsPerRoom` 1~10000, `TradeRangeCm` 1~5000, `TradeRequestTimeoutSec` 1~600, `TradeTimeoutSec` 1~3600, `TradeMaxItems` 1~200(`InventorySlots` 이하), `AuctionDurationSec` 1~2592000, `AuctionMaxListings` 1~100, `AuctionMinPrice` ≤ `AuctionMaxPrice` ≤ `GoldMax`(각 1 이상), `AuctionPageSize` 1~100, `AuctionExpireCheckSec` 1~3600, `ShopRangeCm` 1~5000(2026-11-18 L4 — 상점 NPC 이용 거리, 지금 400). 값을 쓰는 코드는 A3.3 이후에 들어옵니다. UE는 임포트하지 않습니다.

- (2026-11-22 D-255, 2일차 서버) 자연 회복 열 5개 추가: `CombatHoldMs`(5000, 마지막으로 때리거나 맞은 뒤 전투 중인 시간, 1~60000), `HpRegenIntervalSec`(5, 전투 밖에서만, 1~600), `HpRegenPercent`(20, 최대치 %, 1~100), `MpRegenPercentCombat`(1, 초당 %), `MpRegenPercentIdle`(5, 초당 %, 1~100). 서버 `Character::IsInCombat`·`Room::UpdateRegen`이 읽고, 옛 서버 상수 `COMBAT_HOLD_MS`·`HP_REGEN_*`·`MP_REGEN_*`는 지웠습니다. UE는 이 테이블을 읽지 않습니다.

### BodyTable (클라이언트 전용, 2026-11-07 MC4.1)
체형별 기준 캐릭터 구성. `Name` = PlayerType(1 MALE, 2 FEMALE), `Leader`(보이는 몸 = 키트 `Body_Combined`, 애니메이션 재생), `Eyes`·`Teeth`·`Hair`(Leader Pose로 따라가는 키트 부위), `BodyMaterial`(몸 섹션 마스크 머티리얼 `MI_Body_<M|F>`), `AnimClass`(Leader의 AnimBP), `HitMontage`·`DeathMontage`(2026-11-08 MC4.2 — 무기와 상관없는 공용 1벌이라 남녀 같은 값, 비면 재생 안 함, `kit_profiles.json` `montages`). `jm_kit.item_table.export`가 `kit_profiles.json`에서 생성합니다. 서버는 읽지 않습니다. UE: `FBodyTableRow`(`Source/JM/Data/BodyTableRow.h`), `DT_BodyTable`, 조회 `UDataManager::FindBody`. `UEquipmentComponent`가 로드해 `AJMCharacterBase::SetReactionMontages`로 넣습니다.

### WeaponTypeTable (클라이언트 전용, 2026-11-08 MC4.2)
무기 "종류"마다 달라지는 연출 값. `Name` = `EWeaponType` 값(1 Unarmed, 2 Sword, 3 Bow — SkillTable ID 첫째 자리와 같음, 맨손도 행이 있음), `WeaponType`(enum 이름 — 로더가 `Name`과 어긋나면 경고하고 그 행을 쓰지 않음), `AnimLayer`(`ALI_Weapon`을 구현한 레이어 AnimBP `ABP_Layer_<WeaponType>` — `ABP_PlayerBase`에 `LinkAnimClassLayers`로 연결, 비면 연결 안 함). MC4.3의 등 소켓·넣기 지연 같은 무기 타입별 값은 이 표에 열을 추가합니다. 아이템별로 자세를 달리할 필요가 생기면 그때 ItemTable에 덮어쓰기 열을 더합니다. 서버는 읽지 않습니다. (2026-11-09 MC4.2d-2) 넣기 열: `SheatheDelaySec`(마지막 공격·조준 해제 뒤 넣기까지 초, 0 = 항상 손에 듦 — 지금 활 5, 나머지 0), `SheatheMontage`(넣기 동작, 상체 슬롯), `SheatheSwapSec`(넣기 동작 중 무기를 등 소켓으로 옮기는 시각, 활 0.45). UE: `FWeaponTypeTableRow`(`Source/JM/Data/WeaponTypeTableRow.h`), `DT_WeaponTypeTable`, 조회 `UDataManager::FindWeaponType`; 장착 무기가 바뀌면(`SC_CHANGE_EQUIP`) `UEquipmentComponent`가 레이어 클래스를 로드해 `ULeaderPoseAppearance::Apply`에서 연결합니다.

### WorldTable (서버 전용, 2026-10-31 A1.3)
`Name`(worldId, 1001~), `MapName`(맵 리소스 파일 이름 = `GameMap::LoadMapData` 인자, 중복 불가), `CellSizeCm`(AOI 큰 셀 한 변 — 셀마다 쿼드트리, 100~100000), `QuadSplitCount`(쿼드트리 노드가 이 수를 넘으면 4분할, 1~1000), `QuadMinSizeCm`(이보다 작은 노드로는 나누지 않음, 100~CellSizeCm), `ViewRadiusCm`(목표 시야 반경 R — 이 안의 캐릭터는 반드시 보임, 100~100000), `ViewHysteresisCm`(H — 나갈 때는 R+H를 넘어야 사라짐, 0~R), `ReviveDelaySec`(플레이어가 죽은 뒤 부활 요청 `CS_REVIVE`를 받기까지의 초, 0~3600 — 클라이언트에는 `SC_DEAD.revive_delay_ms`로 전달, A2.4), `ReviveHpPercent`(부활 HP = 최대 HP × 이 값 ÷ 100, 1~100), `ChannelCapacity`(채널 하나의 정원 = 그 채널의 모든 Room 인원 합 — 로그인 때 인원이 이 값 미만인 가장 낮은 채널에 배정하고 모두 찼으면 새 채널을 만듦, Room별 정원은 없음, 1~10000, 2026-11-19 D-215·DG3a. 옛 이름 `RoomCapacity`), `PortalCooldownMs`(포털을 쓴 뒤 다시 쓸 수 있을 때까지의 ms, 0~60000 — L5, D-203). 서버가 쓰는 레벨(`ServerConfig` `[World] MapName`) 행이 없으면 시작을 중단합니다. 현재 행: 1001 ParagonSample(셀 5000, 분할 8, 최소 노드 500, R 5000, H 500 — R·H는 A1.3b 스크린샷으로 결정, 부활 대기 5초·HP 100%, 채널 정원 500), 1002 Continent01(시야 수치는 1001과 같음, 채널 정원 500 — `DESIGN_PARTY_DUNGEON.md` §2-2). UE는 이 테이블을 임포트하지 않습니다.

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
| `MoveSendTurnDeg` | 30 | 클라 | 이동 중 속도 방향이 마지막 송신 때보다 이 각도를 넘게 바뀌면 200ms를 기다리지 않고 바로 송신(서버 최소 간격 안에서). 0 = 끔(2026-11-07 MC4.1b) |
| `RemoteCatchUpSpeedMul` | 1.3 | 클라 | 원격 캐릭터가 뒤처진 위치를 따라잡는 화면 속도 상한 = 이동 스탯 × 배율(비례 추적의 순간 속도 튐 방지). 0 = 상한 없음(2026-11-07 MC4.1b) |
| `ServerSpeedTolerance` | 1.2 | 서버 | 허용 속도 배율(MoveSpeed × 배율), 1~3 |
| `ServerMoveBudgetMs` | 1000 | 서버 | 이동 거리 예산(토큰 버킷)을 몇 ms 분량까지 모으는가. 지연으로 몰려 온 패킷을 흡수 |
| `ServerMoveSlackCm` | 50 | 서버 | 예산과 별도로 항상 인정하는 여유 거리(공격 이동 잠금 중에는 0으로 봄) |
| `ServerMoveLockSlackCm` | 120 | 서버 | 공격 이동 잠금(`SkillTable.MoveLockMs`) 동안 받는 이동 거리 합계의 상한 — 시전 확정 때(`Player::BeginMoveLock`) 예산을 이 값으로 줄이고 잠금 구간 시간은 예산으로 쌓지 않음, 넘으면 속도 거부와 같이 보정. 시전 순간 서버가 아는 위치가 마지막 이동 패킷(최대 약 100cm 전)이라 두는 여유, 0~1000(2026-11-18 CB1, D-199) |
| `ServerMinMoveIntervalMs` | 50 | 서버·클라 | 이보다 잦은 이동 패킷은 서버가 처리하지 않음. 클라이언트는 이 간격 안의 전송을 미룬다 |
| `ServerNavToleranceCm` | 50 | 서버 | 목표가 내비메시 경계 밖으로 이만큼(수평)까지는 허용. 내비메시는 벽에서 34cm 안쪽까지 깔리므로 68cm + 벽 두께보다 작아야 벽을 뚫지 못함, 0~100 |
| `ServerWeaponSwapLockMs` | 1000 | 서버 | 무기 슬롯이 바뀌면(착용·해제 모두) 이 시간 동안 공용 행동 잠금 — 스왑으로 쿨타임 우회 방지(MC3.2), 0~5000 |
| `ServerEquipMinIntervalMs` | 200 | 서버 | 같은 캐릭터의 장착 요청 최소 간격(슬롯 구분 없음). 더 잦으면 `EQUIP_RESULT_TOO_FREQUENT`(MC3.2), 0~5000 |
| `ServerAimMinIntervalMs` | 200 | 서버·클라 | 같은 캐릭터의 스킬 조준 **켜기** 요청 최소 간격(끄기는 제한 없음). 더 잦으면 서버가 거부(본인에게 `SC_SKILL_AIM` false), 클라는 이보다 잦은 켜기를 보내지 않음(MC4.2d-1), 0~5000 |
| `AutoWalkTimeoutMs` | 10000 | 클라 | 커서 모드 우클릭 직선 자동 걷기(UI6, D-274)의 시간 상한. 넘으면 멈춤 |
| `AutoWalkStuckMs` | 1000 | 클라 | 자동 걷기 중 이 시간 동안 거의 움직이지 못하면(막힘) 멈춤 |
| `PickMaxDistCm` | 3000 | 클라 | 커서 모드 우클릭으로 고를 수 있는 대상(NPC·포털·플레이어)의 최대 거리 |
| `PickScreenRadiusPx` | 40 | 클라 | 커서와 대상(화면에 투영한 몸 가운데)의 화면 거리가 이 안이면 그 대상을 고름(해상도 1080 기준, 비례 보정) |

서버는 값이 범위를 벗어나거나 1001 행·열이 없으면 시작을 중단합니다.

**Phase MC 목표 열**: `SheatheDelaySec`(5, 클라 — 마지막 공격·피격 후 무기를 등에 메기까지).
