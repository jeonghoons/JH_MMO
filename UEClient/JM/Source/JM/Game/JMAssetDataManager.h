#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Game/EquipAssetData.h"
#include "JMAssetDataManager.generated.h"

UCLASS()
class JM_API UJMAssetDataManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	FAppearanceAssetData* GetAppearanceData(int32 ID);
	FEquipAssetData* GetEquipData(int32 ID);

private:
	TMap<int32, FAppearanceAssetData> MockAppearanceDB;
	TMap<int32, FEquipAssetData> MockEquipDB;
};
