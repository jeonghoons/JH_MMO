#include "ModularEquipmentComponent.h"
#include "Character/JMCharacterBase.h"
#include "JMGameInstance.h"

UModularEquipmentComponent::UModularEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

USkeletalMeshComponent* UModularEquipmentComponent::GetOrAddMeshComponent(EEquipPart Part)
{
	if (EquipMeshes.Contains(Part))
	{
		return EquipMeshes[Part];
	}

	AJMCharacterBase* OwnerChar = Cast<AJMCharacterBase>(GetOwner());
	if (!OwnerChar) return nullptr;

	FString PartName = UEnum::GetValueAsString(Part);
	PartName = PartName.RightChop(PartName.Find(TEXT("::")) + 2) + TEXT("Mesh");

	USkeletalMeshComponent* NewMesh = NewObject<USkeletalMeshComponent>(OwnerChar, FName(*PartName));

	if (Part == EEquipPart::Weapon)
	{
		NewMesh->SetupAttachment(OwnerChar->GetMesh(), FName("Weapon_Socket_R"));
	}
	else
	{
		NewMesh->SetupAttachment(OwnerChar->GetMesh());
		NewMesh->SetLeaderPoseComponent(OwnerChar->GetMesh());
	}
	
	NewMesh->RegisterComponent();
	EquipMeshes.Add(Part, NewMesh);
	return NewMesh;
}

void UModularEquipmentComponent::ApplyEquipment(const TArray<int32>& EquipIDs)
{
	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetWorld()->GetGameInstance());
	if (!GameInst || !GameInst->EquipAssetTable) return;

	TArray<FSoftObjectPath> AssetsToLoad;

	for (int32 ID : EquipIDs)
	{
		FName RowName = FName(*FString::FromInt(ID));
		FEquipAssetData* Data = GameInst->EquipAssetTable->FindRow<FEquipAssetData>(RowName, TEXT(""));
		if (Data)
		{
			if (Data->EquipMesh.IsPending()) AssetsToLoad.AddUnique(Data->EquipMesh.ToSoftObjectPath());
			if (Data->WeaponAnimClass.IsPending()) AssetsToLoad.AddUnique(Data->WeaponAnimClass.ToSoftObjectPath());
			if (Data->AttackMontage.IsPending()) AssetsToLoad.AddUnique(Data->AttackMontage.ToSoftObjectPath());
			if (Data->HitMontage.IsPending()) AssetsToLoad.AddUnique(Data->HitMontage.ToSoftObjectPath());
			if (Data->DeadMontage.IsPending()) AssetsToLoad.AddUnique(Data->DeadMontage.ToSoftObjectPath());
		}
	}

	if (AssetsToLoad.Num() == 0)
	{
		OnEquipmentLoaded(EquipIDs);
		return;
	}

	AssetLoadHandle = GameInst->AssetLoader.RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &UModularEquipmentComponent::OnEquipmentLoaded, EquipIDs)
	);
}

void UModularEquipmentComponent::OnEquipmentLoaded(TArray<int32> LoadedIDs)
{
	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetWorld()->GetGameInstance());
	AJMCharacterBase* OwnerChar = Cast<AJMCharacterBase>(GetOwner());
	if (!GameInst || !GameInst->EquipAssetTable || !OwnerChar) return;

	for (int32 ID : LoadedIDs)
	{
		FName RowName = FName(*FString::FromInt(ID));
		FEquipAssetData* Data = GameInst->EquipAssetTable->FindRow<FEquipAssetData>(RowName, TEXT(""));
		
		if (!Data) continue;

		// 1. 장비/무기 메쉬 착용
		if (Data->EquipMesh.IsValid())
		{
			if (USkeletalMeshComponent* TargetMesh = GetOrAddMeshComponent(Data->EquipPart))
			{
				TargetMesh->SetSkeletalMesh(Data->EquipMesh.Get());
			}
		}

		// 2. 무기 데이터일 경우 애니메이션 덮어쓰기
		if (Data->EquipPart == EEquipPart::Weapon)
		{
			OwnerChar->UpdateWeaponAnimation(
				Data->WeaponAnimClass.Get(),
				Data->AttackMontage.Get(),
				Data->HitMontage.Get(),
				Data->DeadMontage.Get()
			);
		}
	}
	AssetLoadHandle.Reset();
}