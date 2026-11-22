#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/JMUserWidget.h"
#include "ShopWidget.generated.h"

class UItemSlotWidget;

// 상점 NPC 창 (L4, D-201·D-202). 내용은 UJMShopManager(서버가 보낸 품목)와 UJMInventoryManager(내 골드·아이템)를 그대로 그린다.
//  - 구매: BuyItems의 칸(칸의 숫자 = 1개 값)을 골라 EditBuyCount에 수량을 적고 BtnBuy
//  - 판매: 상점이 열린 동안 인벤토리 아이템을 우클릭하면 판매 후보가 되고, EditSellCount의 수량만큼 BtnSell로 확정(되사기 없음)
// 조작은 요청만 보내고 화면은 SC_INVENTORY_UPDATE가 와야 바뀐다. 띄우고 숨기는 일은 UJMShopManager가 한다.
UCLASS()
class JM_API UShopWidget : public UJMUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 칸 위젯(WBP_ItemSlot)
	UPROPERTY(EditAnywhere, Category = "Shop")
	TSubclassOf<UItemSlotWidget> ItemSlotClass;

	// NPC 이름
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTitle;
	// 내 골드
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtGold;
	// 파는 품목 칸을 넣는 곳(WrapBox 등 아무 패널)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> BuyItems;
	// 고른 품목: 이름·1개 값·합계
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtBuyInfo;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditBuyCount;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnBuy;
	// 판매 후보: 이름·수량·받을 골드 (없으면 안내 문구)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSellInfo;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditSellCount;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSell;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnClose;

private:
	void Refresh();
	// 품목·판매 후보가 바뀌었을 때만 수량 칸을 기본값으로 되돌린다(골드만 바뀐 갱신에서는 적어 둔 수량을 지우지 않는다)
	void OnShopChanged();
	class UJMShopManager* GetShopManager() const;
	int32 ReadCount(const class UEditableTextBox* Box) const;

	void OnBuySlotClicked(UItemSlotWidget* ItemSlot);

	UFUNCTION()
	void OnBuyClicked();
	UFUNCTION()
	void OnSellClicked();
	UFUNCTION()
	void OnCloseClicked();
	UFUNCTION()
	void OnCountChanged(const FText& Text);

	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> BuySlots;

	// 고른 품목(ItemTable ID). 0 = 없음
	int32 SelectedItemId = 0;
	// 수량 칸을 맞춰 둔 판매 후보
	int64 SellCountFor = 0;
	FDelegateHandle ShopChangedHandle;
	FDelegateHandle InventoryChangedHandle;
};
