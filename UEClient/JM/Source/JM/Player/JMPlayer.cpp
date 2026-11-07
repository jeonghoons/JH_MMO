#include "Player/JMPlayer.h"
#include "Game/JMGameInstance.h"
#include "Data/CharacterAssetData.h"
#include "Game/DataManager.h"
#include "GameFramework/CharacterMovementComponent.h"

AJMPlayer::AJMPlayer()
{
}

EWeaponType AJMPlayer::GetWeaponType() const
{
	const int32 WeaponIndex = static_cast<int32>(Protocol::EQUIP_PART_WEAPON) - 1;   // equip_items는 슬롯 순서(EquipPart - 1)
	const int32 WeaponId = WeaponIndex < ObjectInfo.equip_items_size() ? ObjectInfo.equip_items(WeaponIndex) : 0;
	if (WeaponId == 0) return EWeaponType::Unarmed;

	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Item = DataManager ? DataManager->FindItemById(WeaponId) : nullptr;
	return (Item && Item->WeaponType != EWeaponType::None) ? Item->WeaponType : EWeaponType::Unarmed;
}

void AJMPlayer::ApplyEquipChange(Protocol::EquipPart Part, int32 ItemId, const Protocol::StatInfo& Stat)
{
	const int32 Index = static_cast<int32>(Part) - 1;
	if (Index < 0 || Index >= 6) return;
	while (ObjectInfo.equip_items_size() < 6) ObjectInfo.add_equip_items(0);
	ObjectInfo.set_equip_items(Index, ItemId);

	ObjectInfo.mutable_stat()->CopyFrom(Stat);
	if (Stat.move_speed() > 0.f && GetCharacterMovement())
		GetCharacterMovement()->MaxWalkSpeed = Stat.move_speed();
}

void AJMPlayer::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetGameInstance());

	// 서버 정보는 에셋 테이블과 무관하게 항상 반영한다.
	Super::SetPlayerData(ObjInfo);

	if (GameInst == nullptr || GameInst->CharacterAssetTable == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPlayerData: CharacterAssetTable is not set"));
		return;
	}

	Protocol::PlayerType TypeEnum = ObjInfo.player_type();
	FName RowName = GetCharacterRowName(TypeEnum);
	FString ContextString;
	FCharacterAssetData* AssetData = GameInst->CharacterAssetTable->FindRow<FCharacterAssetData>(RowName, ContextString);
	if (AssetData == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPlayerData: no asset row for %s"), *RowName.ToString());
		return;
	}

	// 이전 로드 요청이 남아 있으면 취소한다.
	AssetLoadHandle.Reset();

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
	if (AssetData->HitMontage.IsPending())
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
    // 체형 MALE/FEMALE의 매니퀸 외형은 MC4.1 장비 컴포넌트에서 만든다. 그 전까지 PIE에서 캐릭터가 보이도록 옛 영웅 외형을 임시로 쓴다.
    case PLAYER_TYPE_MALE:
        return FName(TEXT("Greystone"));
    case PLAYER_TYPE_FEMALE:
        return FName(TEXT("Sparrow"));
    default:
        UE_LOG(LogTemp, Warning, TEXT("Invalid PlayerType: %d"), Type);
        return NAME_None;
	}
}
