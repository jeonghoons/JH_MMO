#include "Game/DataManager.h"
#include "Data/JMDataSettings.h"
#include "Engine/DataTable.h"
#include "Engine/StreamableManager.h"
#include "Data/CharacterAssetData.h"
#include "Game/JMGameInstance.h"

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

const FSkillTableRow* UDataManager::FindSkillBySlot(EWeaponType WeaponType, int32 Slot) const
{
	if (GetSkillTable() == nullptr) return nullptr;
	// 몬스터 스킬은 슬롯이 겹칠 수 있어 슬롯으로 찾지 않는다(서버와 같은 규칙).
	if (WeaponType == EWeaponType::Monster || WeaponType == EWeaponType::None) return nullptr;

	for (const TPair<FName, uint8*>& Pair : CachedSkillTable->GetRowMap())
	{
		const FSkillTableRow* Row = reinterpret_cast<const FSkillTableRow*>(Pair.Value);
		if (Row && Row->WeaponType == WeaponType && Row->Slot == Slot)
			return Row;
	}
	return nullptr;
}

const FItemTableRow* UDataManager::FindItemById(int32 ItemId) const
{
	if (CachedItemTable == nullptr)
	{
		const UJMDataSettings* Settings = GetDefault<UJMDataSettings>();
		if (Settings == nullptr || Settings->ItemTable.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("DataManager: ItemTable is not set (Project Settings > Game > JM Data)"));
			return nullptr;
		}
		CachedItemTable = Settings->ItemTable.LoadSynchronous();
	}
	if (CachedItemTable == nullptr) return nullptr;

	return CachedItemTable->FindRow<FItemTableRow>(FName(*FString::FromInt(ItemId)), TEXT("FindItemById"), false);
}

void UDataManager::PreloadGameAssets(FSimpleDelegate OnDone)
{
	TArray<FSoftObjectPath> AssetsToLoad;
	auto AddPath = [&AssetsToLoad](const FSoftObjectPath& Path)
	{
		if (!Path.IsNull()) AssetsToLoad.AddUnique(Path);
	};

	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetGameInstance());
	if (GameInst && GameInst->CharacterAssetTable)
	{
		for (const TPair<FName, uint8*>& Pair : GameInst->CharacterAssetTable->GetRowMap())
		{
			const FCharacterAssetData* Row = reinterpret_cast<const FCharacterAssetData*>(Pair.Value);
			if (Row == nullptr) continue;
			AddPath(Row->CharacterMesh.ToSoftObjectPath());
			AddPath(Row->AnimClass.ToSoftObjectPath());
			AddPath(Row->AttackMontage.ToSoftObjectPath());
			AddPath(Row->HitMontage.ToSoftObjectPath());
			AddPath(Row->DeadMontage.ToSoftObjectPath());
		}
	}

	if (UDataTable* SkillTable = GetSkillTable())
	{
		for (const TPair<FName, uint8*>& Pair : SkillTable->GetRowMap())
		{
			const FSkillTableRow* Row = reinterpret_cast<const FSkillTableRow*>(Pair.Value);
			if (Row == nullptr) continue;
			AddPath(Row->Montage.ToSoftObjectPath());
			AddPath(Row->Fx.ToSoftObjectPath());
			AddPath(Row->Icon.ToSoftObjectPath());
		}
	}

	if (GameInst == nullptr || AssetsToLoad.Num() == 0)
	{
		OnDone.ExecuteIfBound();
		return;
	}

	// 이미 로드된 에셋이면 RequestAsyncLoad가 완료 콜백을 곧바로 부른다.
	PreloadHandle = GameInst->AssetLoader.RequestAsyncLoad(AssetsToLoad, OnDone);
	if (!PreloadHandle.IsValid())
	{
		OnDone.ExecuteIfBound();
	}
}
