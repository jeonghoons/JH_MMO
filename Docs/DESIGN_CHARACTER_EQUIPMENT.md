# 캐릭터 & 무기 아이템 설계

목표 구조만 기록합니다. 코드 마이그레이션 순서/시점은 작업 시점에 판단합니다.
(2026-10-17 변경: 기존 "매니퀸 통일 + 모듈러 장비" 설계는 폐기. [DECISIONS.md](DECISIONS.md) 참고)

## 캐릭터 베이스: Paragon 영웅 고정
- 플레이어 영웅은 **Greystone(전사), Sparrow(궁수) 2명**으로 시작합니다. MMO 기본 기능이 어느 정도 구현된 뒤 영웅을 추가합니다.
- Rampage는 몬스터용입니다.
- 각 영웅은 **자기 원래 무기만** 사용합니다. 무기는 영웅 메시에 포함된 그대로 두며 숨기거나 분리하거나 다른 영웅과 공유하지 않습니다.
- 영웅은 서로 다른 스켈레톤을 쓰므로 애니메이션도 영웅별로 따로 갖습니다([DESIGN_ANIMATION.md](DESIGN_ANIMATION.md)).
- 영웅 선택은 `HeroTable`의 한 행입니다(영웅 ID, 메시, AnimBP, 기본 스탯 ID, 스킬은 SkillTable의 `HeroId`로 연결). 컬럼은 작업 시점에 확정합니다([DESIGN_DATA.md](DESIGN_DATA.md)).
- 의류 장비(상의/하의/신발/모자)와 모듈러 외형은 없습니다.

## 무기 아이템: 스탯 + 머티리얼 교체
- 무기 아이템 = **스탯 보너스 + 무기 머티리얼 교체**입니다. 별도 메시, 본 숨김, 소켓 장착은 없습니다.
- 영웅 메시의 무기 전용 머티리얼 슬롯만 바꿔 외형 차이를 표현합니다.
  - Sparrow: 머티리얼 슬롯 6번이 활 전용(확인됨).
  - Greystone: 검과 방패가 같은 머티리얼 슬롯 3번(`M_Greystone_shield`)을 공유합니다(사용자 확인). 검만 따로 바꿀 수는 없습니다.
- `WeaponItemTable`(예정): `ItemId`, 장착 가능한 `HeroId`, 스탯 보너스, 슬롯별 머티리얼 소프트 경로.
- 흐름: 서버가 장착 중인 무기 아이템 ID와 최종 스탯을 결정 → `ObjectInfo`/SC_ 패킷으로 알림 → 클라이언트는 그 ID로 `WeaponItemTable`을 조회해 머티리얼만 교체합니다.
- 스탯 계산은 서버 책임입니다. 서버(`Server/Server/ServerData.h`)에는 아직 아이템 테이블이 없으므로 CSV 로더와 함께 만듭니다. 장착/해제 CS_/SC_ 패킷도 신규 작업입니다.

## 기존 코드/데이터의 처리 (2026-10-17 삭제 완료)
- 삭제한 클라이언트 코드: `ModularAppearanceComponent`, `ModularEquipmentComponent`, `JMAssetDataManager`, `EquipAssetData.h`(`EAppearancePart`/`EEquipPart`/`FAppearanceAssetData`/`FEquipAssetData`), `AJMCharacterBase::UpdateWeaponAnimation`, `UJMGameInstance`의 `AppearanceAssetTable`/`EquipAssetTable`.
- 삭제한 에셋: `Content/CoreC/`(모듈러 베이스 바디 팩), `DT_Appearance`, `DT_Equip`.
- 남긴 것: `FCharacterAssetData`(`Game/CharacterAssetData.h`)와 `DT_CharacterAsset`. 영웅 메시/AnimBP/몽타주를 가리키며 `AJMPlayer::SetPlayerData`가 사용합니다. HeroTable이 생기면 대체합니다.
- 서버 쪽 `equip_items`(DB `Player_Equipped`, `DatabaseWorker::UpdateEquipData`)는 무기 아이템 장착에 그대로 쓰므로 남깁니다. 모듈러 전용이던 `appearance_items`는 다음 `.proto` 변경 때 제거합니다(미작업).
