#include "HpBarWidget.h"
#include "Components/ProgressBar.h"

void UHpBarWidget::UpdateHp(float CurrentHp, float MaxHp)
{
    if (HpProgressBar && MaxHp > 0.f)
    {
        // ProgressBar의 Percent는 0.0 ~ 1.0 사이의 값입니다.
        HpProgressBar->SetPercent(CurrentHp / MaxHp);
    }
}
