#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/EquipAssetData.h"
#include "Engine/StreamableManager.h"
#include "ModularEquipmentComponent.generated.h"

UCLASS( ClassGroup=(Modular), meta=(BlueprintSpawnableComponent) )
class JM_API UModularEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UModularEquipmentComponent();

	void ApplyEquipment(const TArray<int32>& EquipIDs);

protected:
	class USkeletalMeshComponent* GetOrAddMeshComponent(EEquipPart Part);
	void OnEquipmentLoaded(TArray<int32> LoadedIDs);

protected:
	TSharedPtr<struct FStreamableHandle> AssetLoadHandle;

	UPROPERTY(VisibleAnywhere, Category = "Modular")
	TMap<EEquipPart, class USkeletalMeshComponent*> EquipMeshes;
};