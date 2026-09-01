#include "ModularAppearanceComponent.h"
#include "GameFramework/Character.h"
#include "JMGameInstance.h"

UModularAppearanceComponent::UModularAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

USkeletalMeshComponent* UModularAppearanceComponent::GetOrAddMeshComponent(EAppearancePart Part)
{
	if (AppearanceMeshes.Contains(Part))
	{
		return AppearanceMeshes[Part];
	}

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar) return nullptr;

	// Enum 이름을 기반으로 런타임에 컴포넌트 동적 생성
	FString PartName = UEnum::GetValueAsString(Part);
	PartName = PartName.RightChop(PartName.Find(TEXT("::")) + 2) + TEXT("Mesh");

	USkeletalMeshComponent* NewMesh = NewObject<USkeletalMeshComponent>(OwnerChar, FName(*PartName));
	NewMesh->SetupAttachment(OwnerChar->GetMesh());
	NewMesh->RegisterComponent();
	NewMesh->SetLeaderPoseComponent(OwnerChar->GetMesh());

	AppearanceMeshes.Add(Part, NewMesh);
	return NewMesh;
}

void UModularAppearanceComponent::ApplyAppearance(const TArray<int32>& AppearanceIDs)
{
	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetWorld()->GetGameInstance());
	if (!GameInst || !GameInst->AppearanceAssetTable) return;

	TArray<FSoftObjectPath> AssetsToLoad;

	for (int32 ID : AppearanceIDs)
	{
		FName RowName = FName(*FString::FromInt(ID));
		FAppearanceAssetData* Data = GameInst->AppearanceAssetTable->FindRow<FAppearanceAssetData>(RowName, TEXT(""));
		if (Data && Data->AppearanceMesh.IsPending())
		{
			AssetsToLoad.AddUnique(Data->AppearanceMesh.ToSoftObjectPath());
		}
	}

	if (AssetsToLoad.Num() == 0)
	{
		OnAppearanceLoaded(AppearanceIDs);
		return;
	}

	AssetLoadHandle = GameInst->AssetLoader.RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &UModularAppearanceComponent::OnAppearanceLoaded, AppearanceIDs)
	);
}

void UModularAppearanceComponent::OnAppearanceLoaded(TArray<int32> LoadedIDs)
{
	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetWorld()->GetGameInstance());
	if (!GameInst || !GameInst->AppearanceAssetTable) return;

	for (int32 ID : LoadedIDs)
	{
		FName RowName = FName(*FString::FromInt(ID));
		FAppearanceAssetData* Data = GameInst->AppearanceAssetTable->FindRow<FAppearanceAssetData>(RowName, TEXT(""));
		
		if (Data && Data->AppearanceMesh.IsValid())
		{
			if (USkeletalMeshComponent* TargetMesh = GetOrAddMeshComponent(Data->AppearancePart))
			{
				TargetMesh->SetSkeletalMesh(Data->AppearanceMesh.Get());
			}
		}
	}
	AssetLoadHandle.Reset();
}