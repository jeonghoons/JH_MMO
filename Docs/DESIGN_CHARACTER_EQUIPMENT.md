# 캐릭터 & 장비 설계 (매니퀸 모듈형)

목표 구조만 기록합니다. 구현 순서는 ROADMAP Phase MC를 따릅니다.
(2026-11-04 개정: 2026-10-17 "Paragon 영웅 고정 + 무기 = 머티리얼 교체" 설계를 대체. 근거는 [DECISIONS.md](DECISIONS.md) 2026-11-04 항목과 [HANDOFF_MODULAR_EQUIPMENT_VARCO.md](HANDOFF_MODULAR_EQUIPMENT_VARCO.md). 아트 기준은 [DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md))

## 캐릭터 = 체형 + 장비
- 플레이어는 **UE 매니퀸 공용 스켈레톤** 하나를 씁니다. 체형은 `PlayerType` MALE(1, Manny) / FEMALE(2, Quinn)이며 **가입 때 고정**합니다. 직업은 없습니다.
- 캐릭터의 능력은 **기본 스탯(PlayerStatTable — 레벨별, 체형 공용) + 장비 보너스**, 쓸 수 있는 스킬은 **장착 무기 타입**이 정합니다([DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md)).
- 몬스터는 Paragon Rampage를 그대로 씁니다(`PLAYER_TYPE_MONSTER`, 자체 메시·`ABP_Rampage`). 이 문서의 모듈형 구조는 플레이어에만 적용합니다.
- 기존 `PLAYER_TYPE_WARRIOR`(4)/`ARCHER`(5)와 Greystone/Sparrow 플레이어는 MC 완료 시 사라집니다. DB의 기존 플레이어는 마이그레이션으로 MALE(1)이 됩니다(MC3.3).
- 캐릭터 이름은 로그인 ID를 그대로 씁니다.

## 슬롯 7개
| 슬롯 | 메시 | 붙는 곳 | 기본 숨김 부위 |
|---|---|---|---|
| 머리(Head) | 정적 | `head` 본 | hair |
| 상의(Top) | 스켈레탈 | Leader Pose | torso (+arms, 아이템별) |
| 하의(Bottom) | 스켈레탈 | Leader Pose | legs |
| 장갑(Gloves) | 스켈레탈 | Leader Pose | hands |
| 장화(Boots) | 스켈레탈 | Leader Pose | feet |
| 무기(Weapon) | 정적 | 손 소켓(전투) / 등 소켓(비전투) | — |
| 보조손(Offhand, 방패) | 정적 | `hand_l` 본(손등 쪽에 앞면, 팔뚝에 찬 자세) | — |

- `EquipPart` enum = HEAD 1 · TOP 2 · BOTTOM 3 · GLOVES 4 · BOOTS 5 · WEAPON 6 · OFFHAND 7(= itemId 첫째 자리, MC3.2 확정, OFFHAND는 MC4.2f). `ObjectInfo.equip_items`는 항상 7칸(MC4.2f 전에는 6칸), 인덱스 = EquipPart − 1(빈 슬롯 0). 옛 번호(UPPER 1/LOWER 2/OUTFIT_SHOES 3/WEAPON 5)로 저장된 `Player_Equipped` 행은 마이그레이션에서 지우고 기본 장비로 다시 지급합니다.
- 겹쳐 입기(상의 위 흉갑), 어깨·망토 슬롯은 후속입니다.
- **보조손(방패, MC4.2f — `DECISIONS.md` 2026-11-09·11-10)**: 한손검(WeaponType `Sword`)을 든 상태에서만 착용합니다(아니면 `EQUIP_RESULT_NEED_SWORD`). 방패를 낀 채 무기를 활·맨손으로 바꾸면 서버가 보조손을 비우고 **보조손 해제 `SC_CHANGE_EQUIP`을 먼저, 무기 변경을 다음에** 방송합니다(두 패킷 모두 최종 스탯). 로그인 때 저장 값이 어긋나 있으면(검이 아닌데 방패) 보조손만 비웁니다. 막기 동작·판정은 없고 스탯도 지금은 0입니다. 클라는 방패가 있으면 `UJMAnimInstance.LeftArmOverrideAlpha` = 0(무기 레이어의 왼팔 = 원본 방패 든 자세), 없으면 1(맨손 포즈로 덮기).

## 몸 = 부위 분할 + 장비가 덮는 부위 숨김
- 베이스 바디는 **Fab CoreCharacters 키트의 남/여 몸**(`Content/CoreC` — git 제외 외부 팩)이고, **키트가 나눠 둔 부위 메시를 그대로** 씁니다(2026-11-04 재작업 — 직접 분할하지 않음): Face·Eyes·Teeth·Hair·Torso(팔 포함)·Hands·Legs·Feet. 머티리얼은 키트 원본. 체형별 부위 메시 경로·숨김 규칙은 데이터(`Content/Python/jm_kit/kit_profiles.json` → MC4.1에서 테이블로)로 지정해 몸을 바꿔도 코드를 고치지 않습니다.
- **장비 메시는 키트 의상과 같은 뼈대**(본 89개·이름·순서·바인드 포즈)여야 합니다. 다르면 Leader Pose에서 팔·소매가 벌어집니다(MC2.2 첫 시도 실패 원인). 검사는 `jm_kit.skeleton_check`(MC1.3). 남녀 키트 스켈레톤은 본 순서도 달라서 장비 메시는 체형별로 따로 만듭니다.
- 캐릭터 구성(클라이언트):
  - **Leader = 보이는 몸**: 기본 `Mesh` = 키트 `Body_Combined`(얼굴 + 몸 섹션), 애니메이션 재생, 몸 섹션에 마스크 MID(2026-11-05 — 이전의 "숨긴 리더 + 몸 부위" 대체).
  - 눈·이·헤어 + 스켈레탈 슬롯 4개: Leader Pose로 Leader를 따라갑니다. 몸 부위는 숨기지 않고 **몸 마스킹**으로 장비가 덮는 영역만 지웁니다(2026-11-05): 몸 머티리얼 `MI_Body_<M|F>`(마스크 사본)의 MID에 슬롯별 파라미터(상의 `HideTorso`·`HideArms`·`ArmKeepCm`, 하의 `HideLegs`·`LegKeepCm`, 신발 `HideFeet`)를 넣고, 아이템 데이터가 덮어쓸 수 있습니다. 손목·발목 근처는 남겨 옷 끝과 손·발 사이에 틈이 보이지 않게 합니다. MC4.1 장비 컴포넌트는 같은 규칙으로 MID를 갱신합니다.
  - 정적 슬롯 3개(머리·무기·보조손): 본/소켓에 부착, 상대 변환은 데이터.
- 부위 컴포넌트는 리더 기준 bounds를 씁니다(`bUseBoundsFromLeaderPoseComponent`). 부위 메시 자체 bounds는 기준 자세라, 팔을 뻗는 모션에서 잘못 컬링될 수 있습니다(MC1.3 확인 사항).
- 장착 목록이 바뀌면 **몸 부위 가시성을 다시 계산**합니다: 장착 아이템들의 `HidesBodyParts` 합집합을 숨기고 나머지는 보입니다. 아무것도 안 입은 슬롯은 맨몸 부위가 보입니다.
- 목·손목·발목 경계는 옷이 덮어야 합니다(칼라·소매·장갑·밑단·장화 겹침). 경계 규칙 수치는 체형마다 다릅니다(프로토타입 5장).

## 아이템 (ItemTable)
- 장비 아이템 한 행 = 슬롯, 등급, 티어, 스탯 보너스, 무기 타입(무기만), 외형 경로(클라 전용). 컬럼은 [DESIGN_DATA.md](DESIGN_DATA.md), 확정은 MC3.1.
- 스탯 보너스: **무기 = `AttackDamage`, 방어구 = `MaxHp`**. 최종 스탯은 서버가 계산합니다.
- 등급(Common/Uncommon/Rare)은 데이터·아이콘 테두리에만 쓰고 외형은 티어(천/가죽/판금)가 정합니다.
- 맨손은 아이템 행이 없습니다. 무기 슬롯이 비면 무기 타입 = Unarmed입니다.
- 1차 아이템: 천·판금 세트 × (5부위 + 한손검 + 활) = 14개 + 방패 2개(7101 천 나무 방패, 7301 판금 철 방패, MC4.2f) = 16개.

## 장착 흐름 (서버 권위)
1. 클라이언트: 개발용 장비창 `WBP_Equipment`(ItemTable 전체 목록, A3 전 임시)에서 클릭 → `CS_CHANGE_EQUIP(slot, itemId)`(itemId 0 = 해제).
2. 서버 검증: 존재하는 아이템인지, 슬롯이 맞는지, 시전 중·사망 중이 아닌지, 교체 빈도 제한. 보유 검사와 레벨/티어 제한은 A3에서 추가합니다.
3. 서버 반영: 장착 목록·최종 스탯 재계산. **무기 슬롯이 바뀌면(착용·해제 모두) `MovementTable.ServerWeaponSwapLockMs`(1000) 동안 행동 잠금**(공용 행동 잠금 재사용). 스왑 잠금 중에도 방어구 교체는 받습니다(시전 중 거부는 시전 애니메이션만 해당). 요청 빈도는 캐릭터당 `ServerEquipMinIntervalMs`(200). 스킬 쿨타임은 스킬별로 유지됩니다. 최대 HP가 줄면 현재 HP를 새 최대치로 자르고, 늘어도 회복하지 않습니다.
4. 서버 방송: `SC_CHANGE_EQUIP(objectId, slot, itemId, 최종 스탯)`을 AOI 전체(본인 포함)에. 거부는 본인에게만 `SC_CHANGE_EQUIP_RESULT(EquipResult, slot, itemId)`. 처음 보이는 캐릭터는 `ObjectInfo.equip_items`(슬롯 순서)로 받습니다.
5. 클라이언트 반영: itemId로 ItemTable을 조회해 메시를 바꾸고 몸 가시성을 다시 계산합니다. 스탯은 받은 값을 그대로 씁니다.
6. 저장: 장착이 성공할 때마다 7칸 스냅숏을 `Player_Equipped`(PlayerId, EquipSlot, ItemDbId, ItemId)에 한 트랜잭션으로 저장합니다(`Room::RequestEquipSave` → `DatabaseWorker::SaveEquipSnapshot`). 플레이어당 동시에 1건만 보내고, 진행 중에 또 바뀌면 끝난 뒤 최신 값으로 한 번 더 저장해 순서가 뒤바뀌지 않습니다. 실패하면 다음 변경·퇴장 때 다시 저장합니다(MC3.3, 측정 근거 `TESTING.md`). `ItemDbId`는 A3 전까지 0. 로그인 때 장비·체형만 불러오고(잘못된 행은 WARN 후 건너뜀, HP = 장비 반영 최대치), 레벨·HP 저장은 A2.5입니다.
- 가입 시 기본 장비: 천 5부위 + 천 한손검(`ItemTable.Starter` = 1, 가입 트랜잭션에서 지급).

## 기존 코드/데이터의 처리
- 남은 Paragon 플레이어 자산: 없음. `DT_CharacterAsset`은 2026-11-10, `Character/Greystone`·`Character/Sparrow`(`ABP_Greystone`/`ABP_Sparrow`·몽타주·`BP_Arrow`)와 원본 팩 `ParagonGreystone`·`ParagonSparrow`는 2026-11-12 삭제(사용자 결정). Rampage 쪽은 유지합니다.
- 이펙트는 `Content/FX/`(FX 라이브러리, `P_AoE_Smash`·`P_AoE_ArrowRain` 포함)에 있고, 그 머티리얼·텍스처·메시는 `Content/FX/Dep/`에 있습니다([DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md) 6장).
- 서버 `equip_items`(DB `Player_Equipped`, `DatabaseWorker::UpdateEquipData`)는 그대로 쓰고 슬롯 번호만 새 `EquipPart`로 바꿉니다.
- 2026-10-17에 삭제한 `ModularAppearanceComponent`/`ModularEquipmentComponent`는 되살리지 않고 새 장비 컴포넌트를 설계합니다(이름 충돌 주의).

## 클라이언트 구현 (MC4.1, 2026-11-07)
- `UEquipmentComponent`(`Source/JM/Character/`, `AJMPlayer`가 소유): 체형(`player_type`)과 장착 목록 7칸 → `BodyTable`·`ItemTable` 조회 → 에셋 비동기 로드 → 입은 아이템 마스크 합치기(Hide*는 최댓값, KeepCm은 그 영역을 지운 아이템 값) → 표시 구현에 넘김. `SetPlayerData`(첫 표시)와 `SC_CHANGE_EQUIP`(한 슬롯)에서 갱신. 늦게 끝난 옛 로드는 요청 번호로 버림.
- 표시 = `UEquipmentAppearance`(추상) → 지금 구현 `ULeaderPoseAppearance`: Leader = 캐릭터 `Mesh`(키트 `Body_Combined` + `ABP_PlayerBase`), 눈·이·헤어·스켈레탈 4슬롯은 Leader Pose + 리더 bounds, 머리·무기·보조손은 `ItemTable.AttachSocket`에 `AttachM/F` 상대 변환, 몸 섹션(슬롯 이름에 body)에 `BodyMaterial` MID, `HideHair`면 헤어 숨김. 빈 슬롯은 컴포넌트를 숨기고 재사용. 메시 병합·Mutable로 바꿀 때는 이 클래스만 교체(`AppearanceClass`).
- 데이터: `BodyTable`(체형별 리더·부위·몸 머티리얼·AnimClass)과 ItemTable 외형 열은 `jm_kit.item_table.export`가 `kit_profiles.json`·`items.json`에서 생성([DESIGN_DATA.md](DESIGN_DATA.md)). 방 입장 전 `UDataManager::PreloadGameAssets`가 체형 2개 + 장비 전체를 미리 로드.
- Greystone/Sparrow 임시 외형(`AJMPlayer::GetCharacterRowName`)은 삭제. `DT_CharacterAsset`·`UJMGameInstance::CharacterAssetTable`·`FCharacterAssetData`는 2026-11-10 삭제(사용자 결정).
- 계획했던 `HeroTable`·`WeaponItemTable`은 만들지 않습니다(ItemTable로 대체).
