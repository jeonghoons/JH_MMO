#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "NpcCharacter.generated.h"

UCLASS()
class JM_API ANpcCharacter : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	ANpcCharacter();

	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;

	virtual void OnDamaged(int damage, int remainHp) override;
	virtual void OnDead() override;
    

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    class UWidgetComponent* HpBarComponent;

};
