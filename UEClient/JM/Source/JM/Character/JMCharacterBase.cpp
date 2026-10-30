#include "Character/JMCharacterBase.h"
#include "Game/JMGameInstance.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Data/SkillTableRow.h"
#include "Data/MovementTableRow.h"
#include "Game/DataManager.h"

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

	UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	if (const FMovementTableRow* Row = DataManager ? DataManager->FindMovementRow(1001) : nullptr)
	{
		RemoteInterpSpeed = Row->RemoteInterpSpeed;
		RemoteSnapDistCm = Row->RemoteSnapDistCm;
		RemoteExtrapolateMaxSec = Row->RemoteExtrapolateMaxMs / 1000.f;
	}
}

void AJMCharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 내 캐릭터만 자기 위치를 ObjectInfo에 담아 서버로 보낸다. 원격 캐릭터의 ObjectInfo는 서버 값을 그대로 둔다.
	if (IsMyPlayer)
		SyncTransformToInfo();

	if (false == IsMyPlayer && false == bIsDead)
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

	// 원격 플레이어는 이동 중 주기적으로 위치를 보내므로, 다음 패킷이 늦으면 외삽을 멈춘다(속도 0).
	// 몬스터는 서버가 방향이 바뀔 때·멈출 때만 보내고 그 사이는 속도로 외삽하는 방식이라 상한을 두지 않는다.
	if (ObjectInfo.object_type() == Protocol::OBJECT_TYPE_PLAYER &&
		GetWorld() && GetWorld()->GetTimeSeconds() - LastNetUpdateTime > RemoteExtrapolateMaxSec)
	{
		DestPosition.set_v_x(0.f);
		DestPosition.set_v_y(0.f);
		DestPosition.set_v_z(0.f);
	}

	FVector DestPos(DestPosition.x(), DestPosition.y(), DestPosition.z());
	FVector DestVel(DestPosition.v_x(), DestPosition.v_y(), DestPosition.v_z());

	if (FVector::Dist(CurrentLoc, DestPos) > RemoteSnapDistCm)
	{
		SetActorLocation(DestPos);
	}
	else
	{
		DestPos += DestVel * DeltaTime;
		DestPosition.set_x(DestPos.X);
		DestPosition.set_y(DestPos.Y);
		DestPosition.set_z(DestPos.Z);
		SetActorLocation(FMath::VInterpTo(CurrentLoc, DestPos, DeltaTime, RemoteInterpSpeed), false);
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
	if (GetWorld()) LastNetUpdateTime = GetWorld()->GetTimeSeconds();
}

void AJMCharacterBase::SetPlayerData(const Protocol::ObjectInfo& ObjInfo)
{
	ObjectInfo = ObjInfo;
	DestPosition.CopyFrom(ObjectInfo.position());
	if (GetWorld()) LastNetUpdateTime = GetWorld()->GetTimeSeconds();

	// 이동 속도의 기준은 서버 스탯이다. 서버도 같은 값으로 내 이동을 검증한다.
	if (ObjectInfo.stat().move_speed() > 0.f && GetCharacterMovement())
		GetCharacterMovement()->MaxWalkSpeed = ObjectInfo.stat().move_speed();
}

void AJMCharacterBase::OnDamaged(int32_t Damage, int32_t RemainHP)
{
	ObjectInfo.mutable_stat()->set_hp(RemainHP);
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (AnimInst && CurrentHitMontage)
	{
		AnimInst->Montage_Play(CurrentHitMontage);
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &AJMCharacterBase::OnHitMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, CurrentHitMontage);
	}
}

void AJMCharacterBase::OnDead()
{
	bIsDead = true;
	GetCharacterMovement()->StopMovementImmediately();
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (AnimInst && CurrentDeadMontage)
	{
		const float MontageLength = AnimInst->Montage_Play(CurrentDeadMontage);
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &AJMCharacterBase::OnDeathMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, CurrentDeadMontage);

		// 쓰러진 마지막 자세를 유지하기 위해 몽타주의 마지막 프레임에서 일시정지한다.
		// 몽타주 에셋의 "Enable Auto Blend Out"이 켜져 있으면 끝나기 BlendOut 시간 전부터 원래 포즈(A포즈)로 섞여 나가므로
		// 그 직전에 멈출 수밖에 없고, 그러면 쓰러지는 도중 포즈에서 굳는다. 사망 몽타주는 Auto Blend Out을 끄는 것을 권장한다.
		if (MontageLength > 0.f)
		{
			const float BlendOutTime = CurrentDeadMontage->bEnableAutoBlendOut ? CurrentDeadMontage->GetDefaultBlendOutTime() : 0.f;
			const float FreezeDelay = FMath::Max(MontageLength - BlendOutTime - 0.03f, 0.01f);

			TWeakObjectPtr<AJMCharacterBase> WeakThis(this);
			TWeakObjectPtr<UAnimMontage> WeakMontage(CurrentDeadMontage);
			GetWorldTimerManager().SetTimer(DeathFreezeTimer, FTimerDelegate::CreateLambda([WeakThis, WeakMontage]()
				{
					if (!WeakThis.IsValid() || !WeakMontage.IsValid() || !WeakThis->GetMesh()) return;
					if (UAnimInstance* Anim = WeakThis->GetMesh()->GetAnimInstance())
						Anim->Montage_Pause(WeakMontage.Get());
				}), FreezeDelay, false);
		}
	}
}

void AJMCharacterBase::OnAttack(const FSkillTableRow* Skill)
{
	const float PlayRate = GetAttackPlayRate(Skill);

	UAnimMontage* SkillMontage = (Skill && !Skill->Montage.IsNull()) ? Skill->Montage.LoadSynchronous() : nullptr;
	if (SkillMontage == nullptr)
	{
		if (Skill && !Skill->Montage.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("OnAttack: skill montage load failed (%s)"), *Skill->Montage.ToString());
		}
		PlayAttackAnim(PlayRate);
		return;
	}

	if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
	{
		AnimInst->Montage_Play(SkillMontage, PlayRate);
	}
}

float AJMCharacterBase::GetAttackPlayRate(const FSkillTableRow* Skill) const
{
	if (Skill && Skill->Slot != 0) return 1.f;

	const float AttackSpeed = ObjectInfo.stat().attack_speed();
	return AttackSpeed > 0.f ? AttackSpeed : 1.f;
}

void AJMCharacterBase::FaceYaw(float Yaw)
{
	// 원격 캐릭터는 매 프레임 DestPosition.yaw로 회전을 맞추므로 그 값도 함께 갱신한다.
	DestPosition.set_yaw(Yaw);
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

void AJMCharacterBase::LockFacing(float Seconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (Move == nullptr || Seconds <= 0.f) return;

	// 이미 잠겨 있으면 원래 값을 덮어쓰지 않고 해제 시점만 연장한다.
	if (!GetWorldTimerManager().IsTimerActive(FacingLockTimer))
		bSavedOrientToMovement = Move->bOrientRotationToMovement;

	Move->bOrientRotationToMovement = false;
	GetWorldTimerManager().SetTimer(FacingLockTimer, this, &AJMCharacterBase::UnlockFacing, Seconds, false);
}

void AJMCharacterBase::LockFacingForSkill(const FSkillTableRow& Skill)
{
	// 타격 시점이 지난 뒤에도 잠깐 더 유지해 모션이 끝나기 전에 몸이 돌아가지 않게 한다.
	constexpr float MarginSec = 0.2f;
	LockFacing(Skill.HitDelayMs / 1000.f / GetAttackPlayRate(&Skill) + MarginSec);
}

void AJMCharacterBase::UnlockFacing()
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
		Move->bOrientRotationToMovement = bSavedOrientToMovement;
}

void AJMCharacterBase::OnHitMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{

}

void AJMCharacterBase::OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
}

void AJMCharacterBase::PlayAttackAnim(float PlayRate)
{
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (AnimInst && CurrentAttackMontage)
	{
		AnimInst->Montage_Play(CurrentAttackMontage, PlayRate);
	}
}









