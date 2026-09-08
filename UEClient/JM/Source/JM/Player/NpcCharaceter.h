#pragma once

#include "CoreMinimal.h"
#include "Character/JMCharacterBase.h"
#include "NpcCharaceter.generated.h"

UCLASS()
class JM_API ANpcCharaceter : public AJMCharacterBase
{
	GENERATED_BODY()
public:
	ANpcCharaceter();

	virtual void SetPlayerData(const Protocol::ObjectInfo& ObjInfo) override;

	virtual void OnDamaged(int damage, int remainHp) override;
    

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
    class UWidgetComponent* HpBarComponent;

};
