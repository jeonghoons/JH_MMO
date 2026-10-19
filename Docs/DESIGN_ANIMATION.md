# 애니메이션 구조 (영웅별 AnimBP)

목표 구조만 기록합니다. 마이그레이션 순서/시점은 정하지 않습니다.
(2026-10-17 변경: 무기별 Animation Layer Interface 설계는 폐기됨. 이유: 영웅마다 스켈레톤이 다르고 무기가 영웅에 고정되어 무기 교체용 애니메이션 공유가 필요 없음. [DECISIONS.md](DECISIONS.md) 참고)

## 구조
- 영웅 하나당 AnimBP 하나(예: Greystone → ABP_Greystone, Sparrow → ABP_Sparrow). `HeroTable`이 영웅 → AnimBP를 연결합니다.
- 이동 애니메이션은 AnimBP 안의 상태머신/블렌드스페이스가 맡습니다.
- 공격/스킬 애니메이션은 몽타주로 재생합니다. 어떤 몽타주를 쓰는지는 SkillTable(또는 HeroTable)이 가리킵니다.
- **공격 타이밍은 C++ 타이머가 아니라 AnimNotify**입니다. 단, 클라이언트 AnimNotify는 시전·연출 타이밍에만 쓰고, 대상 산출과 데미지는 서버가 결정합니다([DESIGN_COMBAT_SKILL.md](DESIGN_COMBAT_SKILL.md)).
- 맨손/무기 교체에 따른 AnimInstance 교체(`AJMCharacterBase::UpdateWeaponAnimation`, `WeaponAnimClass`)와 `LinkAnimClassLayers`는 사용하지 않습니다. 무기 아이템은 머티리얼만 바꾸므로 애니메이션이 변하지 않습니다.

## 영웅 추가 시 해야 할 일
메시 + AnimBP + 공격/피격/사망 몽타주를 준비하고 `HeroTable`, `SkillTable` 행을 추가합니다. 코드 수정은 필요 없어야 합니다.
