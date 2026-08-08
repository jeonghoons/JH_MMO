#pragma once

#include "CoreMinimal.h"
#include "Player/JMPlayer.h"
#include "JMMyPlayer.generated.h"

struct FInputActionValue;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;

UCLASS()
class JM_API AJMMyPlayer : public AJMPlayer
{
	GENERATED_BODY()
public:
	AJMMyPlayer();

protected:
	virtual void BeginPlay() override;
	// virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

public:
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	class UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseLookAction;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
};
