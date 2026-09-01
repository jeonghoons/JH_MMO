#include "Game/JMAssetDataManager.h"

void UJMAssetDataManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FEquipAssetData TechUpper;
	TechUpper.EquipPart = EEquipPart::OutfitUpper;
	TechUpper.EquipMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/03Outfit/000_TechwearRunnerCourier01/SKM/SKM_CoreC_F_Techwear01_Upper.SKM_CoreC_F_Techwear01_Upper")));
	MockEquipDB.Add(1, TechUpper);

	
	FEquipAssetData SwordData;
	SwordData.EquipPart = EEquipPart::Weapon;
	SwordData.AttachSocketName = TEXT("hand_r_Socket");
	MockEquipDB.Add(2, SwordData);
}

FAppearanceAssetData* UJMAssetDataManager::GetAppearanceData(int32 ID)
{
	return MockAppearanceDB.Find(ID);
}

FEquipAssetData* UJMAssetDataManager::GetEquipData(int32 ID)
{
	return MockEquipDB.Find(ID);
}
