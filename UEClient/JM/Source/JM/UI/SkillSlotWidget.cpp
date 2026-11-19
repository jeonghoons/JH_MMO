#include "UI/SkillSlotWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Data/SkillTableRow.h"
#include "Data/ItemTableRow.h"
#include "Blueprint/DragDropOperation.h"
#include "UI/ItemSlotWidget.h"

void USkillSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TxtKey) TxtKey->SetText(KeyLabel);
	if (TxtCount) TxtCount->SetVisibility(ESlateVisibility::Collapsed);
	ShowCooldown(false);
}

void USkillSlotWidget::SetSkill(const FSkillTableRow* Skill)
{
	ShowIcon(Skill ? Skill->Icon : TSoftObjectPtr<UTexture2D>());
	if (ImgIcon) ImgIcon->SetRenderOpacity(1.f);
	if (TxtCount) TxtCount->SetVisibility(ESlateVisibility::Collapsed);
}

void USkillSlotWidget::SetItem(const FItemTableRow* Item, int64 Count)
{
	ShowIcon(Item ? Item->Icon : TSoftObjectPtr<UTexture2D>());
	// 다 쓴 종류는 칸에 남기되 흐리게 보인다(수량 0).
	if (ImgIcon) ImgIcon->SetRenderOpacity(Count > 0 ? 1.f : 0.35f);
	if (TxtCount)
	{
		TxtCount->SetText(FText::AsNumber(Count));
		TxtCount->SetVisibility(Item ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

FReply USkillSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && OnRightClicked.IsBound())
	{
		OnRightClicked.Execute(this);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

bool USkillSlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// 인벤토리 칸(장착 칸 제외)에서 끌어온 아이템만 받는다. 올릴 수 있는 종류인지는 서버가 검증한다.
	const UItemSlotWidget* From = InOperation ? Cast<UItemSlotWidget>(InOperation->Payload) : nullptr;
	if (From == nullptr || From->IsEquipSlot() || !From->HasItem()) return false;
	OnItemDropped.ExecuteIfBound(this, From->GetItemId());
	return true;
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

void USkillSlotWidget::SetCooldownRemain(int32 RemainMs, int32 TotalMs)
{
	if (RemainMs <= 0)
	{
		CooldownDuration = 0.0;
		ShowCooldown(false);
		return;
	}
	CooldownDuration = FMath::Max(TotalMs, RemainMs) / 1000.0;
	CooldownEndTime = FPlatformTime::Seconds() + RemainMs / 1000.0;
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
