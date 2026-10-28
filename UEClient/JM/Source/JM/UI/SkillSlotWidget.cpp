#include "UI/SkillSlotWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Data/SkillTableRow.h"

void USkillSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TxtKey) TxtKey->SetText(KeyLabel);
	ShowCooldown(false);
}

void USkillSlotWidget::SetSkill(const FSkillTableRow* Skill)
{
	if (ImgIcon == nullptr) return;

	UTexture2D* Icon = (Skill && !Skill->Icon.IsNull()) ? Skill->Icon.LoadSynchronous() : nullptr;
	if (Icon)
	{
		ImgIcon->SetBrushFromTexture(Icon);
		ImgIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	if (Skill && !Skill->Icon.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("SkillSlot: Icon load failed (%s)"), *Skill->Icon.ToString());
	}
	ImgIcon->SetVisibility(ESlateVisibility::Hidden);
}

void USkillSlotWidget::StartCooldown(int32 CooldownMs)
{
	if (CooldownMs <= 0) return;

	CooldownDuration = CooldownMs / 1000.0;
	CooldownEndTime = FPlatformTime::Seconds() + CooldownDuration;
	ShowCooldown(true);
}

void USkillSlotWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (CooldownDuration <= 0.0) return;

	const double Remain = CooldownEndTime - FPlatformTime::Seconds();
	if (Remain <= 0.0)
	{
		CooldownDuration = 0.0;
		ShowCooldown(false);
		return;
	}

	if (BarCooldown) BarCooldown->SetPercent(static_cast<float>(Remain / CooldownDuration));
	if (TxtCooldown)
	{
		FNumberFormattingOptions Format;
		Format.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
		TxtCooldown->SetText(FText::AsNumber(Remain, &Format));
	}
}

void USkillSlotWidget::ShowCooldown(bool bVisible)
{
	const ESlateVisibility NewVisibility = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (BarCooldown) BarCooldown->SetVisibility(NewVisibility);
	if (TxtCooldown) TxtCooldown->SetVisibility(NewVisibility);
}
