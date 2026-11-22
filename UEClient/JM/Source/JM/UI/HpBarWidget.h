#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "HpBarWidget.generated.h"

class UProgressBar;

UCLASS()
class JM_API UHpBarWidget : public UJMUserWidget
{
    GENERATED_BODY()

public:
    void UpdateHp(float CurrentHp, float MaxHp);

protected:
    UPROPERTY(meta = (BindWidget))
    UProgressBar* HpProgressBar;
};
