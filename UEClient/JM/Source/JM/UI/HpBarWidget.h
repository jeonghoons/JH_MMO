#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HpBarWidget.generated.h"

class UProgressBar;

UCLASS()
class JM_API UHpBarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void UpdateHp(float CurrentHp, float MaxHp);

protected:
    UPROPERTY(meta = (BindWidget))
    UProgressBar* HpProgressBar;
};
