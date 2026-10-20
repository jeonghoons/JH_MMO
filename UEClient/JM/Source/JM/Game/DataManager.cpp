#include "Game/DataManager.h"
#include "Data/JMDataSettings.h"
#include "Engine/DataTable.h"

const FMovementTableRow* UDataManager::FindMovementRow(int32 Id) const
{
	if (CachedMovementTable == nullptr)
	{
		const UJMDataSettings* Settings = GetDefault<UJMDataSettings>();
		if (Settings == nullptr || Settings->MovementTable.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("DataManager: MovementTable is not set (Project Settings > Game > JM Data)"));
			return nullptr;
		}
		CachedMovementTable = Settings->MovementTable.LoadSynchronous();
	}

	if (CachedMovementTable == nullptr) return nullptr;

	return CachedMovementTable->FindRow<FMovementTableRow>(FName(*FString::FromInt(Id)), TEXT("FindMovementRow"), false);
}

UDataTable* UDataManager::GetSkillTable() const
{
	if (CachedSkillTable == nullptr)
	{
		const UJMDataSettings* Settings = GetDefault<UJMDataSettings>();
		if (Settings == nullptr || Settings->SkillTable.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("DataManager: SkillTable is not set (Project Settings > Game > JM Data)"));
			return nullptr;
		}
		CachedSkillTable = Settings->SkillTable.LoadSynchronous();
	}
	return CachedSkillTable;
}

const FSkillTableRow* UDataManager::FindSkillById(int32 SkillId) const
{
	UDataTable* Table = GetSkillTable();
	if (Table == nullptr) return nullptr;

	return Table->FindRow<FSkillTableRow>(FName(*FString::FromInt(SkillId)), TEXT("FindSkillById"), false);
}

const FSkillTableRow* UDataManager::FindSkillBySlot(int32 PlayerType, int32 Slot) const
{
	if (GetSkillTable() == nullptr) return nullptr;

	for (const TPair<FName, uint8*>& Pair : CachedSkillTable->GetRowMap())
	{
		const FSkillTableRow* Row = reinterpret_cast<const FSkillTableRow*>(Pair.Value);
		if (Row && Row->PlayerType == PlayerType && Row->Slot == Slot)
			return Row;
	}
	return nullptr;
}