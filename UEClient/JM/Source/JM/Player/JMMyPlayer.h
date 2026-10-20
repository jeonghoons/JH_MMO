#pragma once

#include "CoreMinimal.h"
#include "Player/JMPlayer.h"
#include "JMMyPlayer.generated.h"

struct FInputActionValue;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FSkillTableRow;

UCLASS()
class JM_API AJMMyPlayer : public AJMPlayer
{
	GENERATED_BODY()
public:
	AJMMyPlayer();

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void OnAttackInput();
	void OnSkill1Input();
	void OnCancelAimInput();

public:
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

public:
	UFUNCTION(BlueprintCallable, Category = "Action")
	void RequestAttack();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	class UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseLookAction;

	// 전투 입력 (A2.2c): 좌클릭 = 일반공격/조준 스킬 확정, 1번 키 = 조준 진입, 우클릭 = 조준 취소
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AttackAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* Skill1Action;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* CancelAimAction;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

protected:
	const float MOVE_PACKET_SEND_DELAY = 0.2f;
	float MovePacketSendTimer = MOVE_PACKET_SEND_DELAY;

	FVector2D	DesiredInput;
	FVector		DesiredMovementDir;
	float		DesiredYaw;

	FVector2D	LastDesiredInput;

	// MovementTable에서 읽은 마우스 감도 배율 (PossessedBy에서 설정)
	float LookSensitivityYaw = 1.f;
	float LookSensitivityPitch = 1.f;

private:
	// 크로스헤어(화면 중앙) 기준 조준 결과
	struct FAimResult
	{
		bool bValid = false;
		float Yaw = 0.f;                 // 시전 방향(deg)
		int32 TargetId = 0;              // Targeted: 서버 오브젝트 id
		FVector2D Point = FVector2D::ZeroVector; // 지면 지정 스킬의 목표 지점
	};

	const FSkillTableRow* FindMySkill(int32 Slot) const;
	bool ComputeAim(const FSkillTableRow& Skill, FAimResult& Out) const;
	void DrawAimIndicator(const FSkillTableRow& Skill, const FAimResult& Aim) const;
	void CastSkillSlot(int32 Slot);

	bool bAiming = false;
	int32 AimingSlot = 0;
	uint64 LastAttackInputFrame = 0;
};
