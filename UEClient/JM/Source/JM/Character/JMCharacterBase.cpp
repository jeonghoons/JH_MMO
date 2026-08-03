#include "Character/JMCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "JMGameInstance.h"

AJMCharacterBase::AJMCharacterBase()
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	GetCharacterMovement()->bRunPhysicsWithNoController = true;
}

void AJMCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	DestPosition.CopyFrom(ObjectInfo.position());
}

void AJMCharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FVector CurrentLoc = GetActorLocation();
	FRotator CurrentRot = GetActorRotation();

	if (false == IsMyPlayer)
	{
		FRotator TargetRot(0, DestPosition.yaw(), 0);
		SetActorRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 10.0f));

		FVector DestPos = FVector(DestPosition.x(), DestPosition.y(), DestPosition.z());
		FVector DestVel = FVector(DestPosition.v_x(), DestPosition.v_y(), DestPosition.v_z());

		if (FVector::Dist(CurrentLoc, DestPos) > 500.0f)
		{
			SetActorLocation(DestPos);
		}

		else
		{
			DestPos += DestVel * DeltaTime;
			SetActorLocation(FMath::VInterpTo(CurrentLoc, DestPos, DeltaTime, 10.0f), false);

		}

		if (GetCharacterMovement())
		{
			GetCharacterMovement()->Velocity = DestVel;
		}

	}

}

void AJMCharacterBase::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	if (GetMesh() == nullptr) return;

	UJMGameInstance* GameInst = Cast<UJMGameInstance>(GetGameInstance());
	if (GameInst == nullptr || GameInst->CharacterAssetTable == nullptr) return;

	ObjectInfo = ObjInfo;

	Protocol::PlayerType TypeEnum = ObjectInfo.player_type();
	FName RowName = GetCharacterRowName(TypeEnum);

	if (RowName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("알 수 없는 외형 타입입니다: %d"), static_cast<int>(TypeEnum));
		return;
	}

	FString ContextString;
	FCharacterAssetData* AssetData = GameInst->CharacterAssetTable->FindRow<FCharacterAssetData>(RowName, ContextString);

	if (AssetData == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("데이터 테이블에 %s 에셋 정보가 없습니다."), *RowName.ToString());
		return;
	}


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

	// 비동기 로드 요청
	AssetLoadHandle = GameInst->AssetLoader.RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &AJMCharacterBase::OnAssetLoadCompleted, AssetData->CharacterMesh, AssetData->AnimClass, AssetData->AttackMontage, AssetData->HitMontage, AssetData->DeadMontage
		));
}

FName AJMCharacterBase::GetCharacterRowName(const Protocol::PlayerType Type)
{
	switch (Type)
	{
	case Protocol::PlayerType::PLAYER_TYPE_GIDEON:  return FName(TEXT("Gideon"));
	case Protocol::PlayerType::PLAYER_TYPE_SPARROW: return FName(TEXT("Sparrow"));
	case Protocol::PlayerType::PLAYER_TYPE_GREYSTONE: return FName(TEXT("Greystone"));
	case Protocol::PlayerType::PLAYER_TYPE_MONSTER: return FName(TEXT("Rampage"));
	default:
		return NAME_None;
	}
}

void AJMCharacterBase::OnDamaged(int32_t Damage, int32_t RemainHP)
{
	ObjectInfo.mutable_stat()->set_hp(RemainHP);
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		AnimInstance->Montage_Play(HitMontage);

		FOnMontageEnded EndDelegate;
		// EndDelegate.BindUObject(this, &AJMPlayer::OnHitMontageEnded);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, HitMontage);
	}
}

void AJMCharacterBase::OnDead()
{
	GetCharacterMovement()->DisableMovement();
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && DeadMontage)
	{
		AnimInstance->Montage_Play(DeadMontage);

		FOnMontageEnded EndDelegate;
		// EndDelegate.BindUObject(this, &AJMPlayer::OnDeathMontageEnded);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, DeadMontage);
	}
}

void AJMCharacterBase::OnAttack()
{

}

void AJMCharacterBase::OnAssetLoadCompleted(TSoftObjectPtr<USkeletalMesh> MeshAsset, TSoftClassPtr<UAnimInstance> AnimAsset, TSoftObjectPtr<class UAnimMontage> AttackMontageAsset, TSoftObjectPtr<class UAnimMontage> HitMontageAsset, TSoftObjectPtr<class UAnimMontage> DeadMontageAsset)
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
	}

	if (HitMontageAsset.IsValid())
	{
		HitMontage = HitMontageAsset.Get();
	}

	if (DeadMontageAsset.IsValid())
	{
		DeadMontage = DeadMontageAsset.Get();
	}

	AssetLoadHandle.Reset();
}







