#include "Character/JMCharacterBase.h"
#include "JMGameInstance.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Game/ModularAppearanceComponent.h"
#include "Game/ModularEquipmentComponent.h"

AJMCharacterBase::AJMCharacterBase()
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	GetCharacterMovement()->bRunPhysicsWithNoController = true;


	AppearanceComponent = CreateDefaultSubobject<UModularAppearanceComponent>(TEXT("AppearanceComponent"));
	EquipmentComponent = CreateDefaultSubobject<UModularEquipmentComponent>(TEXT("EquipmentComponent"));
}

void AJMCharacterBase::BeginPlay()
{
	Super::BeginPlay();
}

void AJMCharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SyncTransformToInfo();

	if (false == IsMyPlayer)
	{
		ApplyNetworkMovement(DeltaTime);
	}
}

void AJMCharacterBase::ApplyNetworkMovement(float DeltaTime)
{
	FVector CurrentLoc = GetActorLocation();
	FRotator CurrentRot = GetActorRotation();

	FRotator TargetRot(0, DestPosition.yaw(), 0);
	SetActorRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 10.0f));

	FVector DestPos(DestPosition.x(), DestPosition.y(), DestPosition.z());
	FVector DestVel(DestPosition.v_x(), DestPosition.v_y(), DestPosition.v_z());

	
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

void AJMCharacterBase::SyncTransformToInfo()
{
	FVector CurrentLoc = GetActorLocation();
	FRotator CurrentRot = GetActorRotation();
	FVector CurrentVel = GetVelocity();

	Protocol::PositionInfo* PosInfo = ObjectInfo.mutable_position();
	
	PosInfo->set_x(CurrentLoc.X);
	PosInfo->set_y(CurrentLoc.Y);
	PosInfo->set_z(CurrentLoc.Z);
	PosInfo->set_yaw(CurrentRot.Yaw);
	PosInfo->set_v_x(CurrentVel.X);
	PosInfo->set_v_y(CurrentVel.Y);
	PosInfo->set_v_z(CurrentVel.Z);
}

void AJMCharacterBase::SetDestInfo(const Protocol::PositionInfo& PosInfo)
{
	DestPosition.CopyFrom(PosInfo);
	ObjectInfo.mutable_position()->CopyFrom(PosInfo);
}

void AJMCharacterBase::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	ObjectInfo = ObjInfo;
	DestPosition.CopyFrom(ObjectInfo.position());

	TArray<int32> AppearanceIDs;
	/*for (int i = 0; i < ObjInfo.appearance_items_size(); ++i)
	{
		AppearanceIDs.Add(ObjInfo.appearance_items(i));
	}*/
	for (int i = 0; i < 6; ++i) {
		AppearanceIDs.Add((i+1) * 1000 + 2);
	}
	AppearanceComponent->ApplyAppearance(AppearanceIDs);

	
	TArray<int32> EquipIDs;
	/*for (int i = 0; i < ObjInfo.equip_items_size(); ++i)
	{
		EquipIDs.Add(ObjInfo.equip_items(i));
	}*/
	EquipmentComponent->ApplyEquipment(EquipIDs);
}



void AJMCharacterBase::OnDamaged(int32_t Damage, int32_t RemainHP)
{
	ObjectInfo.mutable_stat()->set_hp(RemainHP);
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (AnimInst && CurrentHitMontage)
	{
		AnimInst->Montage_Play(CurrentHitMontage);
		FOnMontageEnded EndDelegate;
		AnimInst->Montage_SetEndDelegate(EndDelegate, CurrentHitMontage);
	}
}

void AJMCharacterBase::OnDead()
{
	GetCharacterMovement()->DisableMovement();
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (AnimInst && CurrentDeadMontage)
	{
		AnimInst->Montage_Play(CurrentDeadMontage);
		FOnMontageEnded EndDelegate;
		AnimInst->Montage_SetEndDelegate(EndDelegate, CurrentDeadMontage);
	}
}

void AJMCharacterBase::OnAttack()
{
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (AnimInst && CurrentAttackMontage)
	{
		AnimInst->Montage_Play(CurrentAttackMontage);
	}
}

void AJMCharacterBase::UpdateWeaponAnimation(TSubclassOf<UAnimInstance> AnimClass, UAnimMontage* Attack, UAnimMontage* Hit, UAnimMontage* Dead)
{
	if (AnimClass && GetMesh())
	{
		GetMesh()->SetAnimInstanceClass(AnimClass);
	}
	CurrentAttackMontage = Attack;
	CurrentHitMontage = Hit;
	CurrentDeadMontage = Dead;
}









