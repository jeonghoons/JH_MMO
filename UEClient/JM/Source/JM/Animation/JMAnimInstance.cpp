#include "Animation/JMAnimInstance.h"
#include "Character/JMCharacterBase.h"
#include "GameFramework/CharacterMovementComponent.h"

void UJMAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (AJMCharacterBase* BaseCharacter = Cast<AJMCharacterBase>(TryGetPawnOwner()))
	{
		IsMyPlayer = BaseCharacter->IsMyPlayer;
		Velocity = BaseCharacter->GetVelocity();
		DestVelocity = BaseCharacter->GetDestVelocity();
		Rotation = BaseCharacter->GetActorRotation();
		bIsSkillAiming = BaseCharacter->IsSkillAiming();
		AimSkillId = BaseCharacter->GetAimSkillId();
		WeaponStance = BaseCharacter->GetWeaponStance();
		// 방패를 들면 무기 레이어의 왼팔을 원본(방패 든 자세)으로, 없으면 맨손 포즈로 덮는다(MC4.2f)
		LeftArmOverrideAlpha = BaseCharacter->HasOffhandItem() ? 0.f : 1.f;

		if (UCharacterMovementComponent* MovementComp = BaseCharacter->GetCharacterMovement())
		{
			Acceleration = MovementComp->GetCurrentAcceleration();
		}
	}
}

void UJMAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	if (IsMyPlayer)
	{
		GroundSpeed = Velocity.Size2D();
		bool bHasInput = (Acceleration.SizeSquared2D() > 0.f);
		ShouldMove = (GroundSpeed > 3.0f) && bHasInput;
	}
	else
	{
		// 물리 엔진의 방해(가속도 0)를 무시하고 서버가 준 속도만 신뢰합니다.
		GroundSpeed = DestVelocity.Size2D();

		// 속도가 3 이상이면 가속도 조건 없이 무조건 다리를 움직입니다.
		ShouldMove = (GroundSpeed > 3.0f);
	}

	if (GroundSpeed > 3.0f)
	{
		// 내 캐릭터든 타 유저(NetworkVelocity)든 현재 적용된 Velocity_Cached를 사용합니다.
		FVector CurrentVel = IsMyPlayer ? Velocity : DestVelocity;

		FRotator VelocityRot = CurrentVel.ToOrientationRotator(); // 이동 벡터를 회전값으로 변환
		FRotator DeltaRot = VelocityRot - Rotation;   // 이동 방향 - 바라보는 방향
		DeltaRot.Normalize(); // -180 ~ 180으로 정규화

		Direction = DeltaRot.Yaw;
	}
	else
	{
		Direction = 0.0f;
	}

}
