# 무기별 애니메이션 아키텍처 (Animation Layer Interface)

이 문서도 목표 아키텍처만 기록합니다. 마이그레이션 순서/시점은 여기서 정하지 않습니다.

## 현재 상태
- `FEquipAssetData::WeaponAnimClass`(`TSoftClassPtr<UAnimInstance>`)로 무기 장착 시 `AJMCharacterBase::UpdateWeaponAnimation`이 캐릭터의 AnimInstance **클래스 전체를 교체**하는 방식입니다.
- 이 방식은 캐릭터마다 스켈레톤이 다른 현재 구조(Greystone/Rampage/Sparrow)에서는 사실상 유일한 선택이었습니다 — 스켈레톤이 다르면 애니메이션 자체를 공유할 수 없어 무기·캐릭터·ABP가 1:1로 묶여 있었습니다.

## 목표 구조
[DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)의 매니퀸 통합을 전제로, 하나의 공용 스켈레톤 위에서 무기 타입별로 애니메이션만 갈아끼우는 **Animation Layer Interface** 구조로 전환합니다.

- Animation Layer Interface 애셋에 `Locomotion`/`Attack` 등 함수 시그니처를 정의합니다.
- 무기 타입별 AnimLayer 블루프린트: `AL_Unarmed`(맨손), `AL_OneHandSwordShield`(한손검+방패), `AL_TwoHandSword`(두손검), `AL_Bow`(활). 추후 무기 타입 확장 시 AnimLayer만 추가하면 됩니다.
- 메인 `UJMAnimInstance`는 무기 전환 시 `LinkAnimClassLayers`로 해당 레이어를 링크합니다(현재의 AnimInstance 클래스 전체 교체 방식을 대체).
- `FEquipAssetData::WeaponAnimClass`의 의미가 "AnimInstance 클래스 전체"에서 "AnimLayer 클래스"로 바뀝니다.
- 무기 없이 맨손인 상태도 하나의 무기 타입(`Unarmed`)으로 취급합니다 — 별도 특수 케이스로 분기하지 않습니다.

## 선행 조건
캐릭터 베이스가 매니퀸으로 전환되어 모든 플레이어가 동일 스켈레톤을 쓴 뒤에야 의미가 있는 구조입니다. [DESIGN_CHARACTER_EQUIPMENT.md](DESIGN_CHARACTER_EQUIPMENT.md)의 전환이 먼저입니다.
