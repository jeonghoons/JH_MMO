#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryWidget.generated.h"

class UItemSlotWidget;

// 인벤토리 창 + 장비창 (A6.1·MC4.4, D-166): 장착 7칸, 인벤토리 칸 격자, 골드, 선택한 아이템 이름.
// 내용은 UJMInventoryManager(서버가 보낸 값의 사본)를 그대로 그린다 — 창을 여닫을 때 서버에 묻지 않는다.
// 조작은 요청만 보낸다: 인벤토리 장비 우클릭 = 장착, 장착 칸 우클릭 = 해제, 끌어다 놓기 = 칸 옮기기·맞바꾸기·장착·해제.
// 화면은 서버의 SC_INVENTORY_UPDATE가 와야 바뀌고, 거부는 시스템 메시지로 나온다.
UCLASS()
class JM_API UInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 칸 위젯(WBP_ItemSlot). 인벤토리 칸과 장착 칸을 이 클래스로 만든다.
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UItemSlotWidget> ItemSlotClass;

	// 인벤토리 격자의 열 수. 칸 수는 서버가 알려 준다(SC_INVENTORY.max_slots).
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ClampMin = "1"))
	int32 Columns = 6;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UUniformGridPanel> BagGrid;

	// 장착 칸 7개를 넣는 곳(세로 상자 등 아무 패널).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> EquipPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtGold;

	// "쓰는 칸 / 전체 칸"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSlots;

	// 선택한 아이템의 이름(스택이면 수량)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSelected;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnClose;

private:
	void Refresh();
	void BuildEquipSlots();
	void EnsureBagSlots(int32 SlotCount);
	UItemSlotWidget* CreateSlot();

	void OnSlotClicked(UItemSlotWidget* ItemSlot);
	void OnSlotRightClicked(UItemSlotWidget* ItemSlot);
	void OnSlotDropped(UItemSlotWidget* From, UItemSlotWidget* To);

	UFUNCTION()
	void OnCloseClicked();

	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> BagSlots;     // 인덱스 = 칸 번호
	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> EquipSlots;   // 인덱스 = EquipPart - 1

	int64 SelectedDbId = 0;
	FDelegateHandle InventoryChangedHandle;
};
