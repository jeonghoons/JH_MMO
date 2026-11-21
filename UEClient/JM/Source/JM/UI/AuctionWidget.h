#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "AuctionWidget.generated.h"

DECLARE_DELEGATE_OneParam(FOnAuctionRowClicked, int32 /*Index*/);

// 경매 창의 목록 한 줄·필터 버튼 하나. 버튼의 클릭 이벤트(OnClicked)는 인자가 없어서, 줄 번호를 기억해 두었다가 함께 알린다.
// 글자(Label)는 이 버튼을 만든 창(UAuctionWidget)이 넣는다.
UCLASS()
class JM_API UAuctionRowButton : public UButton
{
	GENERATED_BODY()
public:
	void Setup(int32 InIndex, class UTextBlock* InLabel);
	void SetRow(const FString& Text, bool bSelected, const FLinearColor& TextColor);

	FOnAuctionRowClicked OnRowClicked;

private:
	UFUNCTION()
	void HandleClicked();

	int32 Index = 0;
	UPROPERTY()
	TObjectPtr<class UTextBlock> Label;
};

// 경매 창 (A6.3b, D-159·D-234). 내용은 UJMAuctionManager(서버가 보낸 검색 결과·내 등록·수령함)와 UJMInventoryManager(내 골드·아이템)를 그대로 그린다.
// 탭 네 개를 한 위젯에 두고 고른 탭의 패널만 보인다:
//   - 검색: 필터 버튼 줄(전체·머리…·기타) → 가격순 목록(줄을 눌러 고름) → [구매]. 이전·다음 쪽
//   - 등록: 인벤토리에서 우클릭해 고른 아이템 + 수량·가격 → [등록]            (경매 NPC 앞에서만)
//   - 내 등록: 내가 올린 판매 중인 등록 → [취소]                               (취소는 경매 NPC 앞에서만)
//   - 수령함: 판매 대금·구매품·반환 → [수령]                                   (수령은 경매 NPC 앞에서만)
// K 키로 연 조회 모드에서는 등록·취소·수령 버튼이 꺼지고 안내 문구가 보인다(D-234).
// 조작은 요청만 보내고 화면은 서버 응답이 와야 바뀐다. 띄우고 숨기는 일은 UJMAuctionManager가 한다.
// 목록 줄과 필터 버튼은 C++가 만든다(UAuctionRowButton) — WBP에는 줄을 넣을 패널만 둔다.
UCLASS()
class JM_API UAuctionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 탭을 바꾼다(0 검색 · 1 등록 · 2 내 등록 · 3 수령함). 탭 버튼과 개발용 콘솔 명령 AuctionTab이 쓴다.
	void ShowTab(int32 TabIndex);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 목록 줄·필터 버튼의 글자 크기
	UPROPERTY(EditAnywhere, Category = "Auction")
	int32 RowFontSize = 13;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTitle;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtGold;
	// 마지막 성공 안내 한 줄
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtStatus;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnClose;

	// 탭 버튼
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnTabSearch;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnTabRegister;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnTabMy;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnTabMail;

	// 탭마다 패널 하나. 고른 탭의 것만 보인다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelSearch;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelRegister;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelMy;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelMail;

	// ---- 검색 ----
	// 필터 버튼을 넣는 곳(가로 상자 등)과 목록 줄을 넣는 곳(세로 상자·스크롤 상자 등)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> FilterBox;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> SearchList;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnPrev;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnNext;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnRefresh;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtPage;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSearchInfo;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnBuy;

	// ---- 등록 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtRegisterInfo;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditRegCount;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditRegPrice;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnRegister;

	// ---- 내 등록 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MyList;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtMyInfo;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnCancel;

	// ---- 수령함 ----
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MailList;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtMailInfo;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnClaim;

private:
	enum class ETab : uint8 { Search, Register, My, Mail };

	void Refresh();
	// 열림·닫힘이 바뀌었으면 탭을 처음으로 돌린 뒤 다시 그린다
	void OnAuctionChanged();
	void SetTab(ETab Tab, bool bRequest);
	class UJMAuctionManager* GetAuctionManager() const;
	// 줄 버튼을 Count개가 되게 만들고(모자라면 만들고 남으면 숨김) 줄마다 Fill을 부른다
	void FillRows(class UPanelWidget* Panel, TArray<TObjectPtr<UAuctionRowButton>>& Rows, int32 Count, void (UAuctionWidget::*OnClick)(int32), TFunctionRef<void(UAuctionRowButton&, int32)> Fill);
	UAuctionRowButton* MakeRow(class UPanelWidget* Panel, int32 Index);
	FString ItemName(int32 ItemId) const;
	static FString RemainText(int32 RemainSec);
	int64 ReadNumber(const class UEditableTextBox* Box) const;

	void OnFilterClicked(int32 FilterSlot);
	void OnSearchRowClicked(int32 Index);
	void OnMyRowClicked(int32 Index);
	void OnMailRowClicked(int32 Index);

	UFUNCTION()
	void OnTabSearchClicked();
	UFUNCTION()
	void OnTabRegisterClicked();
	UFUNCTION()
	void OnTabMyClicked();
	UFUNCTION()
	void OnTabMailClicked();
	UFUNCTION()
	void OnCloseClicked();
	UFUNCTION()
	void OnPrevClicked();
	UFUNCTION()
	void OnNextClicked();
	UFUNCTION()
	void OnRefreshClicked();
	UFUNCTION()
	void OnBuyClicked();
	UFUNCTION()
	void OnRegisterClicked();
	UFUNCTION()
	void OnCancelClicked();
	UFUNCTION()
	void OnClaimClicked();
	UFUNCTION()
	void OnEditChanged(const FText& Text);

	UPROPERTY()
	TArray<TObjectPtr<UAuctionRowButton>> FilterButtons;
	UPROPERTY()
	TArray<TObjectPtr<UAuctionRowButton>> SearchRows;
	UPROPERTY()
	TArray<TObjectPtr<UAuctionRowButton>> MyRows;
	UPROPERTY()
	TArray<TObjectPtr<UAuctionRowButton>> MailRows;

	ETab ActiveTab = ETab::Search;
	bool bWasOpen = false;
	bool bWasAtNpc = false;
	// 고른 줄(목록이 다시 와도 같은 id면 유지)
	int64 SelectedListingId = 0;
	int64 SelectedMyListingId = 0;
	int64 SelectedMailId = 0;
	// 수량 칸을 맞춰 둔 등록 후보
	int64 RegCountFor = 0;
	FDelegateHandle AuctionChangedHandle;
	FDelegateHandle InventoryChangedHandle;
};
