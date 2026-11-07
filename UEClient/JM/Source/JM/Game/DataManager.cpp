#include "Game/DataManager.h"
#include "Data/JMDataSettings.h"
#include "Engine/DataTable.h"
#include "Engine/StreamableManager.h"
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

UDataTable* UDataManager::GetItemTable() const
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
	return CachedItemTable;
}

const FItemTableRow* UDataManager::FindItemById(int32 ItemId) const
{
	UDataTable* Table = GetItemTable();
	if (Table == nullptr) return nullptr;

	return Table->FindRow<FItemTableRow>(FName(*FString::FromInt(ItemId)), TEXT("FindItemById"), false);
}

const FBodyTableRow* UDataManager::FindBody(int32 PlayerType) const
{
	if (CachedBodyTable == nullptr)
	{
		const UJMDataSettings* Settings = GetDefault<UJMDataSettings>();
		if (Settings == nullptr || Settings->BodyTable.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("DataManager: BodyTable is not set (Project Settings > Game > JM Data)"));
			return nullptr;
		}
		CachedBodyTable = Settings->BodyTable.LoadSynchronous();
	}
	if (CachedBodyTable == nullptr) return nullptr;

	return CachedBodyTable->FindRow<FBodyTableRow>(FName(*FString::FromInt(PlayerType)), TEXT("FindBody"), false);
}

const FWeaponTypeTableRow* UDataManager::FindWeaponType(EWeaponType WeaponType) const
{
	if (WeaponType == EWeaponType::None || WeaponType == EWeaponType::Monster) return nullptr;

	if (CachedWeaponTypeTable == nullptr)
	{
		const UJMDataSettings* Settings = GetDefault<UJMDataSettings>();
		if (Settings == nullptr || Settings->WeaponTypeTable.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("DataManager: WeaponTypeTable is not set (Project Settings > Game > JM Data)"));
			return nullptr;
		}
		CachedWeaponTypeTable = Settings->WeaponTypeTable.LoadSynchronous();
	}
	if (CachedWeaponTypeTable == nullptr) return nullptr;

	const int32 Id = static_cast<int32>(WeaponType);
	const FWeaponTypeTableRow* Row = CachedWeaponTypeTable->FindRow<FWeaponTypeTableRow>(FName(*FString::FromInt(Id)), TEXT("FindWeaponType"), false);
	if (Row == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("DataManager: WeaponTypeTable has no row %d"), Id);
		return nullptr;
	}
	if (Row->WeaponType != WeaponType)
	{
		// RowName(숫자)과 WeaponType 열이 어긋난 CSV — 조용히 다른 무기 레이어를 쓰지 않도록 막는다.
		UE_LOG(LogTemp, Warning, TEXT("DataManager: WeaponTypeTable row %d has WeaponType %d (expected %d)"), Id, static_cast<int32>(Row->WeaponType), Id);
		return nullptr;
	}
	return Row;
}

void UDataManager::PreloadGameAssets(FSimpleDelegate OnDone)
{
	TArray<FSoftObjectPath> AssetsToLoad;
	auto AddPath = [&AssetsToLoad](const FSoftObjectPath& Path)
	{
		if (!Path.IsNull()) AssetsToLoad.AddUnique(Path);
	};

	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetGameInstance());

	// 플레이어 외형(MC4.1): 체형 2개 + 장비 전체. 스폰 직후 맨몸·투명으로 보이는 것을 막는다(지금 14개 × 남녀라 전부 미리 로드).
	for (int32 PlayerType : { 1, 2 })
	{
		if (const FBodyTableRow* Body = FindBody(PlayerType))
		{
			AddPath(Body->Leader.ToSoftObjectPath());
			AddPath(Body->Eyes.ToSoftObjectPath());
			AddPath(Body->Teeth.ToSoftObjectPath());
			AddPath(Body->Hair.ToSoftObjectPath());
			AddPath(Body->BodyMaterial.ToSoftObjectPath());
			AddPath(Body->AnimClass.ToSoftObjectPath());
			AddPath(Body->HitMontage.ToSoftObjectPath());
			AddPath(Body->DeathMontage.ToSoftObjectPath());
		}
	}
	// 무기 타입별 레이어 AnimBP(MC4.2): 무기 교체 순간 레이어를 바로 연결할 수 있게 미리 로드
	for (EWeaponType WeaponType : { EWeaponType::Unarmed, EWeaponType::Sword, EWeaponType::Bow })
	{
		if (const FWeaponTypeTableRow* Row = FindWeaponType(WeaponType))
			AddPath(Row->AnimLayer.ToSoftObjectPath());
	}
	if (UDataTable* ItemTable = GetItemTable())
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemTableRow* Row = reinterpret_cast<const FItemTableRow*>(Pair.Value);
			if (Row == nullptr) continue;
			AddPath(Row->MeshM.ToSoftObjectPath());
			AddPath(Row->MeshF.ToSoftObjectPath());
			AddPath(Row->StaticMesh.ToSoftObjectPath());
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
