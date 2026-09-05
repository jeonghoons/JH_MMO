#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "JMPlayer.generated.h"
UCLASS()
class JM_API AJMPlayer : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	AJMPlayer();
	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UModularAppearanceComponent* AppearanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UModularEquipmentComponent* EquipmentComponent;


protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* AttackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* DeadMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Animation")
	class UAnimMontage* HitMontage;
};
