#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "JMAnimInstance.generated.h"

UCLASS()
class JM_API UJMAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	// 게임 스레드 데이터 캐싱
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	// 워커 스레드
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float GroundSpeed;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool ShouldMove;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Direction;

	// 무기 레이어의 왼팔 덮기 비율(MC4.2c): 1 = 맨손 이동 포즈로 왼팔을 덮음(방패 없음), 0 = 원본(방패 든 왼팔).
	// AJMCharacterBase::HasOffhandItem(방패 착용)이면 0 (MC4.2f).
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float LeftArmOverrideAlpha = 1.f;

	// 스킬 조준 중인가(MC4.2d-1, 서버 동기화 — 원격 캐릭터도 같다). MC4.2d-2에서 ABP_Layer_Bow가 당긴 자세·조준 이동에 쓴다.
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsSkillAiming = false;

	// 조준 중인 스킬 ID(0 = 조준 안 함). 스킬마다 자세가 다르면 이 값으로 고른다.
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 AimSkillId = 0;

	// 무기 자세(MC4.2d-2): 0 = 등에 멤(맨손 이동), 1 = 손에 듦(무기 이동), 2 = 스킬 조준. ABP_Layer_Bow의 이동 레이어가 Blend Poses by Int로 고른다.
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 WeaponStance = 1;

	bool IsMyPlayer;
	FVector Velocity;
	FVector DestVelocity;
	FVector Acceleration;
	FRotator Rotation;
};
