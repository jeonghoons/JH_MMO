#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/MovementTableRow.h"
#include "Data/SkillTableRow.h"
#include "Data/ItemTableRow.h"
#include "Data/BodyTableRow.h"
#include "DataManager.generated.h"

// DataTable 조회 창구. 애셋 경로는 UJMDataSettings(DefaultGame.ini)에서만 읽는다. (Docs/DESIGN_DATA.md)
UCLASS()
class JM_API UDataManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 행이 없거나 테이블이 지정되지 않았으면 nullptr
	const FMovementTableRow* FindMovementRow(int32 Id) const;

	// 무기 타입의 슬롯(0 = 기본 공격, 1~5 = 숫자키)에 해당하는 스킬 행. 없으면 nullptr (몬스터 스킬은 ID로 찾는다)
	const FSkillTableRow* FindSkillBySlot(EWeaponType WeaponType, int32 Slot) const;

	// 아이템 ID(ItemTable의 RowName)로 찾는다. 없거나 테이블이 지정되지 않았으면 nullptr
	const FItemTableRow* FindItemById(int32 ItemId) const;

	// 체형(PlayerType 숫자, BodyTable의 RowName)으로 찾는다. 없거나 테이블이 지정되지 않았으면 nullptr
	const FBodyTableRow* FindBody(int32 PlayerType) const;

	// 스킬 ID(SkillTable의 RowName, SC_ATTACK.skill_id)로 찾는다. 없으면 nullptr
	const FSkillTableRow* FindSkillById(int32 SkillId) const;

	// 방 입장 전에 플레이어 외형(BodyTable·ItemTable 전체)과 스킬 몽타주·이펙트·아이콘(SkillTable 전체)을 미리 로드한다.
	// 스폰 직후 메시가 없는 상태(투명 캐릭터)와 스킬 첫 사용 시 동기 로딩을 막는다. 로드한 에셋은 이 핸들이 붙잡아 둔다.
	// 이미 로드돼 있으면 OnDone을 바로 부른다.
	void PreloadGameAssets(FSimpleDelegate OnDone);

private:
	UDataTable* GetSkillTable() const;
	UDataTable* GetItemTable() const;

	TSharedPtr<struct FStreamableHandle> PreloadHandle;

private:
	mutable TObjectPtr<UDataTable> CachedMovementTable;
	mutable TObjectPtr<UDataTable> CachedSkillTable;
	mutable TObjectPtr<UDataTable> CachedItemTable;
	mutable TObjectPtr<UDataTable> CachedBodyTable;
};