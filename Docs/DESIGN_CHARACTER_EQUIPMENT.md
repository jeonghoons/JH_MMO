# 캐릭터 베이스 & 장비 데이터 설계

이 문서는 플레이어 캐릭터 베이스 전환과 장비(Equip)/외형(Appearance) 데이터 모델의 **목표 구조**를 기록합니다. 실제 코드 마이그레이션 순서/시점은 여기서 정하지 않고, 작업 시점에 판단합니다.

## 캐릭터 베이스: Paragon 히어로 → 통합 매니퀸

### 현재 상태
- 플레이어 캐릭터가 `Greystone_Skeleton`/`Rampage_Skeleton`/`Sparrow_Skeleton` 3개의 서로 다른 스켈레톤을 사용 중 (`Content/ParagonGreystone`, `ParagonRampage`, `ParagonSparrow`).
- `Sparrow`는 활이 스켈레톤에 물리적으로 붙어 있어, 무기를 교체하는 구조 자체가 불가능함.
- `DT_CharacterAsset`(`FCharacterAssetData`)이 캐릭터 타입별로 별도 메시 + `AnimClass` + 몽타주(공격/피격/사망)를 매핑하는 구조.

### 결정된 방향
- 플레이어 캐릭터는 앞으로 Greystone/Rampage/Sparrow를 사용하지 않습니다.
- UE5 기본 매니퀸(`SK_Mannequin`, Manny/Quinn) 하나의 뼈대로 통일합니다 — 모든 플레이어가 같은 A-포즈 스켈레톤을 공유하므로 애니메이션을 무기 타입 단위로만 갈아끼우면 됩니다(자세한 내용은 [DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)).
- 남/여 캐릭터는 메시(Manny/Quinn)와 캡슐/스케일 크기만 다르고, 뼈대와 애니메이션은 100% 공유합니다.
- 장비(모자/상의/하의/신발/무기)에 성별 제한은 없습니다 — 같은 장비 애셋을 남/여 캐릭터 모두에 부착 가능해야 합니다.
- `FCharacterAssetData::CharacterMesh`가 매니/퀸 메시를 가리키도록 데이터를 교체합니다.
- 기존 ABP_Greystone/Rampage/Sparrow와 관련 몽타주는 플레이어 캐릭터에서 더 이상 참조하지 않습니다. NPC/몬스터에 재활용할지는 이 문서의 범위 밖(별도 결정 필요)입니다.

## 장비/외형 데이터 모델

### 유지되는 구조
- `EAppearancePart{Face,Torso,Hands,Legs,Feet,Hair}` — 캐릭터 커스터마이징(베이스 바디 외형).
- `EEquipPart{OutfitUpper,OutfitLower,OutfitShoes,Weapon}` — 장비. **모자(Head)/방패(Shield) 슬롯 추가는 보류합니다** — 현재 4개 슬롯 그대로 유지합니다.
- `UModularAppearanceComponent`/`UModularEquipmentComponent`가 파츠별 `USkeletalMeshComponent`를 관리하는 구조는 유지합니다.

### 무기 타입
`FEquipAssetData::EquipPart == Weapon`인 경우의 무기 타입 구분(한손검+방패 / 두손검 / 활 / 맨손)이 필요합니다. 구체적으로 어느 필드(신규 enum 추가 vs `WeaponAnimClass`로 암묵적 구분)로 표현할지는 [DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)와 맞물려 있으므로 그 문서에서 함께 결정합니다.

### 스탯 필드 (계산 로직 제외)
- `FEquipAssetData`에 스탯 필드를 추가합니다(구체 항목은 밸런싱 확정 시 결정 — 예: 공격력/방어력 보너스).
- **착용 시 캐릭터 최종 스탯에 반영하는 계산 로직은 이번 범위에서 구현하지 않습니다.** 필드만 준비합니다.
- 간극: 서버(`Server/Server/ServerData.h`)에는 아직 장비/아이템 스탯을 담는 데이터 테이블이 없습니다(`DataManager`는 `_skillTable`/`_characterTable`뿐). 계산 로직을 실제로 붙이는 시점에는 서버 쪽 아이템 테이블도 함께 만들어야 합니다 — 지금은 이 간극만 기록해둡니다.

### 데이터 흐름 간극
- `UJMAssetDataManager`가 현재 `MockAppearanceDB`/`MockEquipDB`(하드코딩)를 사용 중입니다. `DT_Appearance`/`DT_Equip` 데이터테이블은 Content에 이미 존재하므로, Mock을 실제 데이터테이블 조회로 교체하는 작업이 이 확장의 일부로 필요합니다.
