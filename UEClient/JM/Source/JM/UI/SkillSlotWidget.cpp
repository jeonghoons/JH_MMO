#include "UI/SkillSlotWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Data/SkillTableRow.h"
#include "Data/ItemTableRow.h"

void USkillSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TxtKey) TxtKey->SetText(KeyLabel);
	ShowCooldown(false);
}

void USkillSlotWidget::SetSkill(const FSkillTableRow* Skill)
{
	ShowIcon(Skill ? Skill->Icon : TSoftObjectPtr<UTexture2D>());
}

void USkillSlotWidget::SetItem(const FItemTableRow* Item)
{
	ShowIcon(Item ? Item->Icon : TSoftObjectPtr<UTexture2D>());
}

void USkillSlotWidget::ShowIcon(const TSoftObjectPtr<UTexture2D>& IconRef)
{
	if (ImgIcon == nullptr) return;

	UTexture2D* Icon = IconRef.IsNull() ? nullptr : IconRef.LoadSynchronous();
	if (Icon)
	{
		ImgIcon->SetBrushFromTexture(Icon);
		ImgIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	if (!IconRef.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("SkillSlot: Icon load failed (%s)"), *IconRef.ToString());
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
