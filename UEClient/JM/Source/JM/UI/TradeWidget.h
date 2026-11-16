#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TradeWidget.generated.h"

class UItemSlotWidget;
struct FJMTradeOffer;

// 1:1 거래 창 (A6.3a). 한 위젯에 화면 두 개가 있다.
//  - PanelRequest: 받은 요청의 수락·거절, 또는 내가 보낸 요청의 응답 대기(취소)
//  - PanelTrade: 양쪽 제시(아이템·골드), 잠금·확정·취소, 상대의 잠금·확정 표시
// 내용은 UJMTradeManager(서버가 보낸 상태의 사본)를 그대로 그린다. 조작은 요청만 보내고 화면은 SC_TRADE_STATE가 와야 바뀐다.
// 아이템 올리기: 거래 중 인벤토리에서 우클릭하거나 이 창으로 끌어다 놓는다(행 전부). 수량은 올린 칸을 골라 EditCount에 적는다.
// 내 제시의 칸을 우클릭하면 내린다. 띄우고 숨기는 일은 UJMTradeManager가 한다.
UCLASS()
class JM_API UTradeWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// 인벤토리 칸을 창의 빈 곳에 놓아도 올린다
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	// 칸 위젯(WBP_ItemSlot)
	UPROPERTY(EditAnywhere, Category = "Trade")
	TSubclassOf<UItemSlotWidget> ItemSlotClass;

	// ---- 요청 화면 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelRequest;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtRequest;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnAccept;
	// 받은 요청이면 거절, 내가 보낸 요청이면 취소
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnDecline;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtDecline;

	// ---- 거래 화면 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelTrade;
	// 상대 이름
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtPartner;
	// 올린 아이템 칸을 넣는 곳(WrapBox 등 아무 패널)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MyItems;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> TheirItems;
	// 내가 올릴 골드. 적고 Enter(또는 다른 곳 클릭)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditGold;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTheirGold;
	// "제시 중" / "잠금" / "확정"
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtMyStatus;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTheirStatus;
	// 고른 칸의 아이템 이름과 수량
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSelected;
	// 고른 내 칸의 올릴 수량. 적고 Enter
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditCount;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnLock;
	// 잠금 버튼의 글자("잠금" / "잠금 해제")
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtLock;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnConfirm;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnCancel;

private:
	void Refresh();
	void FillOffer(UPanelWidget* Panel, TArray<TObjectPtr<UItemSlotWidget>>& Slots, const FJMTradeOffer& Offer, bool bMine);
	class UJMTradeManager* GetTradeManager() const;

	void OnSlotClicked(UItemSlotWidget* ItemSlot);
	void OnMySlotRightClicked(UItemSlotWidget* ItemSlot);
	void OnSlotDropped(UItemSlotWidget* From, UItemSlotWidget* To);
	// 인벤토리 칸(From)의 아이템을 내 제시에 올린다
	void OfferFromInventorySlot(UItemSlotWidget* From);

	UFUNCTION()
	void OnAcceptClicked();
	UFUNCTION()
	void OnDeclineClicked();
	UFUNCTION()
	void OnLockClicked();
	UFUNCTION()
	void OnConfirmClicked();
	UFUNCTION()
	void OnCancelClicked();
	UFUNCTION()
	void OnGoldCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void OnCountCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> MySlots;
	UPROPERTY()
	TArray<TObjectPtr<UItemSlotWidget>> TheirSlots;

	// 고른 칸(이름 표시·수량 변경 대상). bSelectedMine = 내 제시의 칸인지
	int64 SelectedDbId = 0;
	bool bSelectedMine = false;
	FDelegateHandle TradeChangedHandle;
};
