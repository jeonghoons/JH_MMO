#include "Player/JMPlayer.h"
#include "Game/ModularAppearanceComponent.h"
#include "Game/ModularEquipmentComponent.h"
#include "JMGameInstance.h"

AJMPlayer::AJMPlayer()
{
	AppearanceComponent = CreateDefaultSubobject<UModularAppearanceComponent>(TEXT("AppearanceComponent"));
	EquipmentComponent = CreateDefaultSubobject<UModularEquipmentComponent>(TEXT("EquipmentComponent"));
}

void AJMPlayer::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetGameInstance());
	if (GameInst == nullptr || GameInst->CharacterAssetTable == nullptr) return;


	Super::SetPlayerData(ObjInfo);

	/*TArray<int32> AppearanceIDs;
	AppearanceIDs.Reserve(ObjInfo.appearance_items_size());
	for (int i = 0; i < ObjInfo.appearance_items_size(); ++i)
	{
		AppearanceIDs.Add(ObjInfo.appearance_items(i));
	}
	
	if (AppearanceComponent && AppearanceIDs.Num() > 0)
	{
		AppearanceComponent->ApplyAppearance(AppearanceIDs);
	}

	TArray<int32> EquipIDs;
	EquipIDs.Reserve(ObjInfo.equip_items_size());
	for (int i = 0; i < ObjInfo.equip_items_size(); ++i)
	{
		EquipIDs.Add(ObjInfo.equip_items(i));
	}

	if (EquipmentComponent && EquipIDs.Num() > 0)
	{
		EquipmentComponent->ApplyEquipment(EquipIDs);
	}*/

	Protocol::PlayerType TypeEnum = ObjInfo.player_type();
	FName RowName = GetCharacterRowName(TypeEnum);
	FString ContextString;
	FCharacterAssetData* AssetData = GameInst->CharacterAssetTable->FindRow<FCharacterAssetData>(RowName, ContextString);
	if (AssetData == nullptr) return;
	
	TArray<FSoftObjectPath> AssetsToLoad;
	if (AssetData->CharacterMesh.IsPending())
	{
		AssetsToLoad.AddUnique(AssetData->CharacterMesh.ToSoftObjectPath());
	}
	if (AssetData->AnimClass.IsPending())
	{
		AssetsToLoad.AddUnique(AssetData->AnimClass.ToSoftObjectPath());
	}
	if (AssetData->AttackMontage.IsPending())
	{
		AssetsToLoad.AddUnique(AssetData->AttackMontage.ToSoftObjectPath());
	}
	if (AssetData->DeadMontage.IsPending())
	{
		AssetsToLoad.AddUnique(AssetData->HitMontage.ToSoftObjectPath());
	}
	if (AssetData->DeadMontage.IsPending())
	{
		AssetsToLoad.AddUnique(AssetData->DeadMontage.ToSoftObjectPath());
	}

	if (AssetsToLoad.Num() == 0)
	{
		OnAssetLoadCompleted(AssetData->CharacterMesh, AssetData->AnimClass, AssetData->AttackMontage, AssetData->HitMontage, AssetData->DeadMontage);
		return;
	}

	AssetLoadHandle = GameInst->AssetLoader.RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &AJMPlayer::OnAssetLoadCompleted, AssetData->CharacterMesh, AssetData->AnimClass, AssetData->AttackMontage, AssetData->HitMontage, AssetData->DeadMontage)
	);

}

void AJMPlayer::OnAssetLoadCompleted(TSoftObjectPtr<USkeletalMesh> MeshAsset, TSoftClassPtr<UAnimInstance> AnimAsset, TSoftObjectPtr<class UAnimMontage> AttackMontageAsset, TSoftObjectPtr<class UAnimMontage> HitMontageAsset, TSoftObjectPtr<class UAnimMontage> DeadMontageAsset)
{
	if (MeshAsset.IsValid())
	{
		GetMesh()->SetSkeletalMesh(MeshAsset.Get());
	}

	if (AnimAsset.IsValid())
	{
		GetMesh()->SetAnimInstanceClass(AnimAsset.Get());
	}

	if (AttackMontageAsset.IsValid())
	{
		AttackMontage = AttackMontageAsset.Get();
		CurrentAttackMontage = AttackMontage;
	}

	if (HitMontageAsset.IsValid())
	{
		HitMontage = HitMontageAsset.Get();
		CurrentHitMontage = HitMontage;
	}

	if (DeadMontageAsset.IsValid())
	{
		DeadMontage = DeadMontageAsset.Get();
		CurrentDeadMontage = DeadMontage;
	}

	AssetLoadHandle.Reset();
}

FName AJMPlayer::GetCharacterRowName(Protocol::PlayerType Type)
{
	using namespace Protocol;
	switch (Type)
	{
    case PLAYER_TYPE_WARRIOR: // 프로토버퍼에서 생성된 열거형 이름에 맞게 수정 (예: Protocol::PLAYER_TYPE_WARRIOR)
        return FName(TEXT("Greystone"));
        break;
    case PLAYER_TYPE_ARCHER:
        return FName(TEXT("Sparrow"));
        break;
    default:
        UE_LOG(LogTemp, Warning, TEXT("Invalid PlayerType: %d"), Type);
        return NAME_None;
	}
}
