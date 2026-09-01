#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/EquipAssetData.h"
#include "Engine/StreamableManager.h"
#include "ModularAppearanceComponent.generated.h"

UCLASS( ClassGroup=(Modular), meta=(BlueprintSpawnableComponent) )
class JM_API UModularAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UModularAppearanceComponent();

	// 서버에서 받은 외형 ID 리스트를 적용
	void ApplyAppearance(const TArray<int32>& AppearanceIDs);

protected:
	// 동적으로 메쉬 컴포넌트를 생성하거나 가져오는 헬퍼 함수
	class USkeletalMeshComponent* GetOrAddMeshComponent(EAppearancePart Part);

	// 비동기 로드 완료 콜백
	void OnAppearanceLoaded(TArray<int32> LoadedIDs);

protected:
	TSharedPtr<struct FStreamableHandle> AssetLoadHandle;

	UPROPERTY(VisibleAnywhere, Category = "Modular")
	TMap<EAppearancePart, class USkeletalMeshComponent*> AppearanceMeshes;
};