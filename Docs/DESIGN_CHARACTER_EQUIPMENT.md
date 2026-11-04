# 캐릭터 & 장비 설계 (매니퀸 모듈형)

목표 구조만 기록합니다. 구현 순서는 ROADMAP Phase MC를 따릅니다.
(2026-11-04 개정: 2026-10-17 "Paragon 영웅 고정 + 무기 = 머티리얼 교체" 설계를 대체. 근거는 [DECISIONS.md](DECISIONS.md) 2026-11-04 항목과 [HANDOFF_MODULAR_EQUIPMENT_VARCO.md](HANDOFF_MODULAR_EQUIPMENT_VARCO.md). 아트 기준은 [DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md))

## 캐릭터 = 체형 + 장비
- 플레이어는 **UE 매니퀸 공용 스켈레톤** 하나를 씁니다. 체형은 `PlayerType` MALE(1, Manny) / FEMALE(2, Quinn)이며 **가입 때 고정**합니다. 직업은 없습니다.
- 캐릭터의 능력은 **기본 스탯(StatTable, 체형별 행) + 장비 보너스**, 쓸 수 있는 스킬은 **장착 무기 타입**이 정합니다([DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md)).
- 몬스터는 Paragon Rampage를 그대로 씁니다(`PLAYER_TYPE_MONSTER`, 자체 메시·`ABP_Rampage`). 이 문서의 모듈형 구조는 플레이어에만 적용합니다.
- 기존 `PLAYER_TYPE_WARRIOR`(4)/`ARCHER`(5)와 Greystone/Sparrow 플레이어는 MC 완료 시 사라집니다. DB의 기존 플레이어는 마이그레이션으로 MALE(1)이 됩니다(MC3.3).
- 캐릭터 이름은 로그인 ID를 그대로 씁니다.

## 슬롯 6개
| 슬롯 | 메시 | 붙는 곳 | 기본 숨김 부위 |
|---|---|---|---|
| 머리(Head) | 정적 | `head` 본 | hair |
| 상의(Top) | 스켈레탈 | Leader Pose | torso (+arms, 아이템별) |
| 하의(Bottom) | 스켈레탈 | Leader Pose | legs |
| 장갑(Gloves) | 스켈레탈 | Leader Pose | hands |
| 장화(Boots) | 스켈레탈 | Leader Pose | feet |
| 무기(Weapon) | 정적 | 손 소켓(전투) / 등 소켓(비전투) | — |

- `EquipPart` enum을 이 6개로 다시 번호를 매깁니다(HEAD=1 … WEAPON=6 순, MC3.2에서 확정). 옛 번호(UPPER 1/LOWER 2/OUTFIT_SHOES 3/WEAPON 5)로 저장된 `Player_Equipped` 행은 마이그레이션에서 지우고 기본 장비로 다시 지급합니다.
- 겹쳐 입기(상의 위 흉갑), 어깨·망토 슬롯은 후속입니다.

## 몸 = 부위 분할 + 장비가 덮는 부위 숨김
- 베이스 바디는 **Fab CoreCharacters 키트의 남/여 몸**(`Content/CoreC` — git 제외 외부 팩)이고, **키트가 나눠 둔 부위 메시를 그대로** 씁니다(2026-11-04 재작업 — 직접 분할하지 않음): Face·Eyes·Teeth·Hair·Torso(팔 포함)·Hands·Legs·Feet. 머티리얼은 키트 원본. 체형별 부위 메시 경로·숨김 규칙은 데이터(`Content/Python/jm_kit/kit_profiles.json` → MC4.1에서 테이블로)로 지정해 몸을 바꿔도 코드를 고치지 않습니다.
- **장비 메시는 키트 의상과 같은 뼈대**(본 89개·이름·순서·바인드 포즈)여야 합니다. 다르면 Leader Pose에서 팔·소매가 벌어집니다(MC2.2 첫 시도 실패 원인). 검사는 MC1.3 도구.
- 캐릭터 구성(클라이언트):
  - **Leader**: 기본 `Mesh` = 전체 몸 메시, 애니메이션 재생, 화면에는 숨김(`AlwaysTickPoseAndRefreshBones` 필수).
  - 키트 몸 부위 8개(Face·Eyes·Teeth·Hair·Torso·Hands·Legs·Feet) + 스켈레탈 슬롯 4개: Leader Pose로 Leader를 따라갑니다. 숨김 기본값은 키트와 같음(상의 → Torso, 하의 → Legs, 신발 → Feet).
  - 정적 슬롯 2개(머리·무기): 본/소켓에 부착, 상대 변환은 데이터.
- 부위 컴포넌트는 리더 기준 bounds를 씁니다(`bUseBoundsFromLeaderPoseComponent`). 부위 메시 자체 bounds는 기준 자세라, 팔을 뻗는 모션에서 잘못 컬링될 수 있습니다(MC1.3 확인 사항).
- 장착 목록이 바뀌면 **몸 부위 가시성을 다시 계산**합니다: 장착 아이템들의 `HidesBodyParts` 합집합을 숨기고 나머지는 보입니다. 아무것도 안 입은 슬롯은 맨몸 부위가 보입니다.
- 목·손목·발목 경계는 옷이 덮어야 합니다(칼라·소매·장갑·밑단·장화 겹침). 경계 규칙 수치는 체형마다 다릅니다(프로토타입 5장).

## 아이템 (ItemTable)
- 장비 아이템 한 행 = 슬롯, 등급, 티어, 스탯 보너스, 무기 타입(무기만), 외형 경로(클라 전용). 컬럼은 [DESIGN_DATA.md](DESIGN_DATA.md), 확정은 MC3.1.
- 스탯 보너스: **무기 = `AttackDamage`, 방어구 = `MaxHp`**. 최종 스탯은 서버가 계산합니다.
- 등급(Common/Uncommon/Rare)은 데이터·아이콘 테두리에만 쓰고 외형은 티어(천/가죽/판금)가 정합니다.
- 맨손은 아이템 행이 없습니다. 무기 슬롯이 비면 무기 타입 = Unarmed입니다.
- 1차 아이템: 천·판금 세트 × (5부위 + 한손검 + 활) = 14개.

## 장착 흐름 (서버 권위)
1. 클라이언트: 개발용 장비창 `WBP_Equipment`(ItemTable 전체 목록, A3 전 임시)에서 클릭 → `CS_CHANGE_EQUIP(slot, itemId)`(itemId 0 = 해제).
2. 서버 검증: 존재하는 아이템인지, 슬롯이 맞는지, 시전 중·사망 중이 아닌지, 교체 빈도 제한. 보유 검사와 레벨/티어 제한은 A3에서 추가합니다.
3. 서버 반영: 장착 목록·최종 스탯 재계산. **무기를 바꾸면 `MovementTable.WeaponSwapLockMs`(1000) 동안 행동 잠금**(공용 행동 잠금 재사용). 스킬 쿨타임은 스킬별로 유지됩니다. 최대 HP가 줄면 현재 HP를 새 최대치로 자르고, 늘어도 회복하지 않습니다.
4. 서버 방송: `SC_CHANGE_EQUIP(objectId, slot, itemId, 최종 스탯)`을 AOI 전체에. 처음 보이는 캐릭터는 `ObjectInfo.equip_items`(슬롯 순서)로 받습니다.
5. 클라이언트 반영: itemId로 ItemTable을 조회해 메시를 바꾸고 몸 가시성을 다시 계산합니다. 스탯은 받은 값을 그대로 씁니다.
6. 저장: 장착 변경은 `Player_Equipped`(PlayerId, EquipSlot, ItemDbId, ItemId)에 저장합니다. `ItemDbId`는 A3 전까지 0. 로그인 때 장비·체형만 불러오고, 레벨·HP 저장은 A2.5입니다.
- 가입 시 기본 장비: 천 5부위 + 천 한손검.

## 기존 코드/데이터의 처리
- 남은 Paragon 플레이어 자산: `DT_CharacterAsset`(`FCharacterAssetData`), `ABP_Greystone`/`ABP_Sparrow`, `Character/Greystone`·`Character/Sparrow`의 몽타주·아이콘. 매니퀸 캐릭터가 동작하면(MC4) 플레이어 쪽 참조를 끊고 정리 여부를 사용자에게 묻습니다. Rampage 쪽은 유지합니다.
- 이펙트(`P_Greystone_GroundSmash`, `P_Sparrow_RainofArrows`)는 검·활 스킬에서 계속 쓰므로 `Content/FX/`로 옮깁니다([DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md) 6장).
- 서버 `equip_items`(DB `Player_Equipped`, `DatabaseWorker::UpdateEquipData`)는 그대로 쓰고 슬롯 번호만 새 `EquipPart`로 바꿉니다.
- 2026-10-17에 삭제한 `ModularAppearanceComponent`/`ModularEquipmentComponent`는 되살리지 않고 새 장비 컴포넌트를 설계합니다(이름 충돌 주의).
- 계획했던 `HeroTable`·`WeaponItemTable`은 만들지 않습니다(ItemTable로 대체).
