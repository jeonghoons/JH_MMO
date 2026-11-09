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
	// 지금은 항상 1 — MC4.2f에서 방패(OFFHAND) 착용 시 0으로 바꾼다.
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float LeftArmOverrideAlpha = 1.f;

	bool IsMyPlayer;
	FVector Velocity;
	FVector DestVelocity;
	FVector Acceleration;
	FRotator Rotation;
};
