#include "UI/ItemSlotWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Data/ItemTableRow.h"
#include "Game/JMInventoryManager.h"

void UItemSlotWidget::InitBagSlot(int32 InSlotIndex)
{
	SlotIndex = InSlotIndex;
	EquipPart = 0;
	if (TxtLabel) TxtLabel->SetVisibility(ESlateVisibility::Collapsed);
	SetItem(nullptr, nullptr);
	SetSelected(false);
}

void UItemSlotWidget::InitEquipSlot(int32 InEquipPart, const FText& Label)
{
	SlotIndex = -1;
	EquipPart = InEquipPart;
	if (TxtLabel) TxtLabel->SetText(Label);
	SetItem(nullptr, nullptr);
	SetSelected(false);
}

void UItemSlotWidget::SetItem(const FJMInventoryItem* Item, const FItemTableRow* Row)
{
	ItemDbId = Item ? Item->ItemDbId : 0;
	ItemId = Item ? Item->ItemId : 0;
	Count = Item ? Item->Count : 0;

	IconTexture = nullptr;
	if (Item && Row && !Row->Icon.IsNull())
	{
		IconTexture = Row->Icon.LoadSynchronous();
		if (IconTexture == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("ItemSlot: Icon load failed (%s)"), *Row->Icon.ToString());
		}
	}

	if (ImgIcon)
	{
		if (IconTexture) ImgIcon->SetBrushFromTexture(IconTexture);
		ImgIcon->SetVisibility(IconTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (TxtCount)
	{
		TxtCount->SetText(FText::AsNumber(Count));
		TxtCount->SetVisibility(Count >= 2 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (TxtLabel && IsEquipSlot())
	{
		TxtLabel->SetVisibility(Item ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UItemSlotWidget::SetSelected(bool bSelected)
{
	if (SelectedFrame)
	{
		SelectedFrame->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

FReply UItemSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnRightClicked.ExecuteIfBound(this);
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnClicked.ExecuteIfBound(this);
		// 아이템이 있는 칸은 누른 채 움직이면 끌기가 시작된다.
		if (HasItem() && bDragEnabled)
		{
			return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UItemSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
	if (!HasItem() || !bDragEnabled) return;

	UDragDropOperation* Operation = NewObject<UDragDropOperation>(this);
	Operation->Payload = this;
	Operation->Pivot = EDragPivot::CenterCenter;

	// 끄는 동안 커서를 따라다니는 그림: 같은 아이콘
	if (IconTexture)
	{
		UImage* Visual = NewObject<UImage>(this);
		Visual->SetBrushFromTexture(IconTexture);
		Visual->SetDesiredSizeOverride(InGeometry.GetLocalSize());
		Visual->SetRenderOpacity(0.8f);
		Operation->DefaultDragVisual = Visual;
	}
	OutOperation = Operation;
}

bool UItemSlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	UItemSlotWidget* From = InOperation ? Cast<UItemSlotWidget>(InOperation->Payload) : nullptr;
	if (From == nullptr) return false;

	if (From != this) OnDropped.ExecuteIfBound(From, this);
	return true;
}
