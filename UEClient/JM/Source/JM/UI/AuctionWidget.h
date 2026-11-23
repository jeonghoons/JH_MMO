#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "UI/JMUserWidget.h"
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
	// 목록 줄의 아이템 아이콘 칸(UI3, D-270). 필터 버튼은 아이콘 칸이 없어 SetIcon이 아무것도 하지 않는다
	void SetIconWidgets(class UBorder* InFrame, class UImage* InImage) { IconFrame = InFrame; IconImage = InImage; }
	// Texture가 없으면 아이콘 칸을 숨긴다(판매 대금 같은 골드 줄)
	void SetIcon(class UTexture2D* Texture, const FLinearColor& GradeColor);
	// 경매 목록 줄의 숫자 열 세 개(1개당 가격 · 가격 · 남은 시간 — AU2b, D-281). 열이 없는 줄(수령함·퀘스트·필터)은 SetColumns가 아무것도 하지 않는다
	void SetColumnLabels(class UTextBlock* InUnit, class UTextBlock* InTotal, class UTextBlock* InRemain) { ColUnit = InUnit; ColTotal = InTotal; ColRemain = InRemain; }
	void SetColumns(const FString& Unit, const FString& Total, const FString& Remain, const FLinearColor& TextColor);

	FOnAuctionRowClicked OnRowClicked;

private:
	UFUNCTION()
	void HandleClicked();

	int32 Index = 0;
	UPROPERTY()
	TObjectPtr<class UTextBlock> Label;
	UPROPERTY()
	TObjectPtr<class UBorder> IconFrame;
	UPROPERTY()
	TObjectPtr<class UImage> IconImage;
	UPROPERTY()
	TObjectPtr<class UTextBlock> ColUnit;
	UPROPERTY()
	TObjectPtr<class UTextBlock> ColTotal;
	UPROPERTY()
	TObjectPtr<class UTextBlock> ColRemain;
};

// 경매 창 (A6.3b D-234 → AU2b D-281). 내용은 UJMAuctionManager(서버가 보낸 검색 결과·내 등록·수령함)와 UJMInventoryManager(내 골드·아이템)를 그대로 그린다.
// 탭 네 개를 한 위젯에 두고 고른 탭의 패널만 보인다:
//   - 검색: 검색칸 + [검색]·[새로고침](같은 동작) → 열 머리(1개당 가격·가격·남은 시간 — 눌러 정렬, 다시 누르면 반대)
//           → 4열 목록(줄을 눌러 고름) → 이전·다음 쪽 → 수량 칸(스택 아이템만) + [구매]. 필터 버튼 줄은 숨겨 둠(나중에)
//           보고 있는 목록은 갱신하지 않고, 구매 응답으로 그 줄만 고친다(끝난 줄은 회색 "판매 완료")
//   - 등록: 인벤토리에서 우클릭해 고른 아이템 + 수량·1개당 가격 → [등록]      (경매 NPC 앞에서만)
//   - 내 등록: 내가 올린 판매 중인 등록 → [취소]                               (취소는 경매 NPC 앞에서만)
//   - 수령함: 판매 대금·구매품·반환 → [수령]                                   (수령은 경매 NPC 앞에서만)
// K 키로 연 조회 모드에서는 등록·취소·수령 버튼이 꺼지고 안내 문구가 보인다(D-234).
// 조작은 요청만 보내고 화면은 서버 응답이 와야 바뀐다. 띄우고 숨기는 일은 UJMAuctionManager가 한다.
// 목록 줄은 C++가 만든다(UAuctionRowButton) — WBP에는 줄을 넣을 패널만 둔다. 열 폭은 Col*Width 한 곳에서 정해 열 머리(WBP)에도 입힌다.
// 금액은 세 자리마다 쉼표(JMUIStyle::GoldText). 검색 간격이 남으면 [검색]·[새로고침]이 꺼지고 "검색 · 3초"처럼 남은 초를 보인다.
UCLASS()
class JM_API UAuctionWidget : public UJMUserWidget
{
	GENERATED_BODY()

public:
	// 탭을 바꾼다(0 검색 · 1 등록 · 2 내 등록 · 3 수령함). 탭 버튼과 개발용 콘솔 명령 AuctionTab이 쓴다.
	void ShowTab(int32 TabIndex);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 목록 줄·필터 버튼의 글자 크기
	UPROPERTY(EditAnywhere, Category = "Auction")
	int32 RowFontSize = 13;

	// 목록 줄 앞 아이템 아이콘 칸의 크기(UI3)
	UPROPERTY(EditAnywhere, Category = "Auction")
	float RowIconSize = 34.f;

	// 목록 숫자 열의 폭(1개당 가격 · 가격 · 남은 시간). 열 머리(ColUnitBox 등)에도 같은 값을 입힌다
	UPROPERTY(EditAnywhere, Category = "Auction")
	float ColUnitWidth = 130.f;
	UPROPERTY(EditAnywhere, Category = "Auction")
	float ColTotalWidth = 140.f;
	UPROPERTY(EditAnywhere, Category = "Auction")
	float ColRemainWidth = 130.f;

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
	// 검색칸·[검색](글자 TxtSearchBtn에 남은 초를 붙인다). [새로고침](BtnRefresh)은 [검색]과 같은 동작
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditSearch;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSearch;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSearchBtn;
	// 열 머리: 눌러 정렬하는 버튼 셋(글자에 ▲▼를 붙인다)과 폭을 맞출 상자 셋, 아이콘 칸만큼 띄울 빈 칸
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSortUnit;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSortTotal;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnSortRemain;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSortUnit;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSortTotal;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSortRemain;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class USizeBox> ColUnitBox;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class USizeBox> ColTotalBox;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class USizeBox> ColRemainBox;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class USizeBox> ColIconSpacer;
	// 구매 수량(스택 아이템을 골랐을 때만 보임): 칸 + [최대]
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> BuyCountBox;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EditBuyCount;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BtnBuyMax;
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
	// bColumns = 숫자 열 세 개가 있는 4열 줄(검색 결과·내 등록)
	void FillRows(class UPanelWidget* Panel, TArray<TObjectPtr<UAuctionRowButton>>& Rows, int32 Count, bool bColumns, void (UAuctionWidget::*OnClick)(int32), TFunctionRef<void(UAuctionRowButton&, int32)> Fill);
	UAuctionRowButton* MakeRow(class UPanelWidget* Panel, int32 Index, bool bColumns);
	// 등록 한 건을 4열 줄로 그린다(이름은 스택 아이템만 "이름(남은 수량)")
	void FillListingRow(UAuctionRowButton& Row, const struct FJMAuctionListing& Listing, bool bSelected, bool bMarkMine) const;
	bool IsStackItem(int32 ItemId) const;
	// 검색칸의 글자로 검색한다([검색]·[새로고침]·엔터)
	void DoSearch();
	// 고른 줄의 구매 수량(스택이 아니면 1, 칸이 비었거나 0이면 0)
	int32 BuyCount(const struct FJMAuctionListing& Listing) const;
	// 요청 간격에 따라 바뀌는 버튼 상태를 하나의 값으로(바뀌었을 때만 다시 그린다)
	int32 CooldownState() const;
	FString ItemName(int32 ItemId) const;
	// 줄에 아이템 아이콘·등급 테두리를 넣는다(ItemId 0 = 아이콘 없음)
	void ApplyRowIcon(UAuctionRowButton& Row, int32 ItemId) const;
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
	UFUNCTION()
	void OnSearchClicked();
	UFUNCTION()
	void OnSearchTextChanged(const FText& Text);
	UFUNCTION()
	void OnSearchTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void OnSortUnitClicked();
	UFUNCTION()
	void OnSortTotalClicked();
	UFUNCTION()
	void OnSortRemainClicked();
	UFUNCTION()
	void OnBuyCountChanged(const FText& Text);
	UFUNCTION()
	void OnBuyCountCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION()
	void OnBuyMaxClicked();

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
	// 구매 수량 칸을 1로 맞춰 둔 등록(고른 줄이 바뀌면 다시 1)
	int64 BuyCountFor = 0;
	// 칸의 글자를 코드가 고치는 중(바뀜 이벤트가 다시 들어오지 않게)
	bool bEditingText = false;
	float CooldownAccum = 0.f;
	int32 LastCooldownState = -1;
	FDelegateHandle AuctionChangedHandle;
	FDelegateHandle InventoryChangedHandle;
};
