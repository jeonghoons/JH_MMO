#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "ItemSlotWidget.generated.h"

struct FJMInventoryItem;
struct FItemTableRow;
class UItemSlotWidget;

DECLARE_DELEGATE_OneParam(FOnItemSlotEvent, UItemSlotWidget*);
DECLARE_DELEGATE_TwoParams(FOnItemSlotDrop, UItemSlotWidget* /*From*/, UItemSlotWidget* /*To*/);

// 아이템 칸 하나(아이콘, 수량, 선택 테두리) — 인벤토리 칸과 장비창의 장착 칸이 함께 쓴다 (A6.1, D-166).
// 칸은 보여 주고 입력(클릭·우클릭·끌어다 놓기)을 알리기만 한다. 무엇을 할지는 이 칸을 만든 창(UInventoryWidget)이 정하고,
// 결과는 서버 패킷(SC_INVENTORY_UPDATE)이 와야 바뀐다.
UCLASS()
class JM_API UItemSlotWidget : public UJMUserWidget
{
	GENERATED_BODY()

public:
	// 인벤토리 칸으로 쓴다. SlotIndex = 서버가 저장하는 칸 번호(0부터).
	void InitBagSlot(int32 InSlotIndex);
	// 장비창의 장착 칸으로 쓴다. EquipPart = Protocol::EquipPart(1~7), Label = 비어 있을 때 보이는 슬롯 이름.
	void InitEquipSlot(int32 InEquipPart, const FText& Label);

	// 이 칸의 아이템. nullptr이면 빈 칸. Row = ItemTable 행(아이콘).
	void SetItem(const FJMInventoryItem* Item, const FItemTableRow* Row);
	void SetSelected(bool bSelected);
	// false면 이 칸에서 끌기를 시작할 수 없다(거래 창의 올린 아이템 칸, A6.3a). 기본은 끌 수 있음
	void SetDragEnabled(bool bEnabled) { bDragEnabled = bEnabled; }

	bool IsEquipSlot() const { return EquipPart != 0; }
	int32 GetSlotIndex() const { return SlotIndex; }
	int32 GetEquipPart() const { return EquipPart; }
	bool HasItem() const { return ItemDbId != 0; }
	int64 GetItemDbId() const { return ItemDbId; }
	int32 GetItemId() const { return ItemId; }

	FOnItemSlotEvent OnClicked;        // 좌클릭(누를 때)
	FOnItemSlotEvent OnRightClicked;   // 우클릭
	FOnItemSlotDrop OnDropped;         // 다른 칸에서 끌어다 이 칸에 놓음

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UImage> ImgIcon;

	// 수량. 2개 이상일 때만 보인다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtCount;

	// 장착 칸의 슬롯 이름. 비어 있을 때만 보인다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtLabel;

	// 선택 표시(테두리). 선택한 칸에서만 보인다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> SelectedFrame;

	// 칸 바탕(UIStyleTable Slot 브러시). 아이템이 있으면 테두리를 등급 색으로 칠한다(D-073·D-270)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UBorder> SlotBg;

private:
	int32 SlotIndex = -1;
	int32 EquipPart = 0;
	int64 ItemDbId = 0;
	int32 ItemId = 0;
	int64 Count = 0;
	bool bDragEnabled = true;

	UPROPERTY()
	TObjectPtr<class UTexture2D> IconTexture;
};
