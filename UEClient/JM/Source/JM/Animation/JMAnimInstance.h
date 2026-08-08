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

	bool IsMyPlayer;
	FVector Velocity;
	FVector DestVelocity;
	FVector Acceleration;
	FRotator Rotation;
};
