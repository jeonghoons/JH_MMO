#include "UI/AuctionWidget.h"
#include "UI/UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/ItemTableRow.h"
#include "Game/DataManager.h"
#include "Game/JMAuctionManager.h"
#include "Game/JMInventoryManager.h"
#include "Protocol/Enum.pb.h"

namespace
{
	// 검색 필터(CS_AUCTION_SEARCH.slot): 0 = 전체, 1~7 = 장비 슬롯, 8 = 슬롯 없는 아이템(소비 등)
	const TCHAR* const FilterNames[] = { TEXT("전체"), TEXT("머리"), TEXT("상의"), TEXT("하의"), TEXT("장갑"), TEXT("신발"), TEXT("무기"), TEXT("보조"), TEXT("기타") };
	constexpr int32 FilterCount = UE_ARRAY_COUNT(FilterNames);

	const FLinearColor RowColor(0.10f, 0.10f, 0.12f, 1.f);
	const FLinearColor RowSelectedColor(0.55f, 0.40f, 0.10f, 1.f);
	const FLinearColor MineTextColor(1.f, 0.85f, 0.3f, 1.f);
	const FLinearColor ClosedTextColor(0.42f, 0.42f, 0.42f, 1.f);
	// 검색어 최대 글자 수(서버 AUCTION_KEYWORD_MAX)·표시용 합계 상한(검증은 서버 — GoldMax)
	constexpr int32 KeywordMaxLen = 20;
	constexpr int64 TotalPriceMax = 2000000000LL;
	const TCHAR* const SortNames[] = { TEXT("1개당 가격"), TEXT("가격"), TEXT("남은 시간") };

	FString DigitsOnly(const FString& Text)
	{
		FString Out;
		for (const TCHAR Ch : Text) if (Ch >= TEXT('0') && Ch <= TEXT('9')) Out.AppendChar(Ch);
		return Out;
	}

	void SetShown(UWidget* Widget, bool bShown)
	{
		if (Widget) Widget->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

// ---------------------------------------------------------------------------------------------------------------
void UAuctionRowButton::Setup(int32 InIndex, UTextBlock* InLabel)
{
	Index = InIndex;
	Label = InLabel;
	OnClicked.AddUniqueDynamic(this, &UAuctionRowButton::HandleClicked);
}

void UAuctionRowButton::SetRow(const FString& Text, bool bSelected, const FLinearColor& TextColor)
{
	if (Label)
	{
		Label->SetText(FText::FromString(Text));
		Label->SetColorAndOpacity(FSlateColor(TextColor));
	}
	SetBackgroundColor(bSelected ? RowSelectedColor : RowColor);
}

void UAuctionRowButton::SetColumns(const FString& Unit, const FString& Total, const FString& Remain, const FLinearColor& TextColor)
{
	UTextBlock* const Cols[] = { ColUnit, ColTotal, ColRemain };
	const FString* const Texts[] = { &Unit, &Total, &Remain };
	for (int32 Col = 0; Col < 3; ++Col)
	{
		if (Cols[Col] == nullptr) continue;
		Cols[Col]->SetText(FText::FromString(*Texts[Col]));
		Cols[Col]->SetColorAndOpacity(FSlateColor(TextColor));
	}
}

void UAuctionRowButton::SetIcon(UTexture2D* Texture, const FLinearColor& GradeColor)
{
	if (IconFrame == nullptr || IconImage == nullptr) return;
	if (Texture == nullptr)
	{
		IconFrame->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	IconImage->SetBrushFromTexture(Texture);
	IconFrame->SetBrushColor(GradeColor);
	IconFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UAuctionRowButton::HandleClicked()
{
	OnRowClicked.ExecuteIfBound(Index);
}

// ---------------------------------------------------------------------------------------------------------------
UJMAuctionManager* UAuctionWidget::GetAuctionManager() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UJMAuctionManager>() : nullptr;
}

void UAuctionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BtnClose) BtnClose->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnCloseClicked);
	if (BtnTabSearch) BtnTabSearch->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnTabSearchClicked);
	if (BtnTabRegister) BtnTabRegister->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnTabRegisterClicked);
	if (BtnTabMy) BtnTabMy->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnTabMyClicked);
	if (BtnTabMail) BtnTabMail->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnTabMailClicked);
	if (BtnPrev) BtnPrev->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnPrevClicked);
	if (BtnNext) BtnNext->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnNextClicked);
	if (BtnRefresh) BtnRefresh->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnRefreshClicked);
	if (BtnBuy) BtnBuy->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnBuyClicked);
	if (BtnRegister) BtnRegister->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnRegisterClicked);
	if (BtnCancel) BtnCancel->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnCancelClicked);
	if (BtnClaim) BtnClaim->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnClaimClicked);
	if (EditRegCount) EditRegCount->OnTextChanged.AddUniqueDynamic(this, &UAuctionWidget::OnEditChanged);
	if (EditRegPrice) EditRegPrice->OnTextChanged.AddUniqueDynamic(this, &UAuctionWidget::OnEditChanged);
	if (BtnSearch) BtnSearch->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnSearchClicked);
	if (EditSearch)
	{
		EditSearch->OnTextChanged.AddUniqueDynamic(this, &UAuctionWidget::OnSearchTextChanged);
		EditSearch->OnTextCommitted.AddUniqueDynamic(this, &UAuctionWidget::OnSearchTextCommitted);
	}
	if (BtnSortUnit) BtnSortUnit->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnSortUnitClicked);
	if (BtnSortTotal) BtnSortTotal->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnSortTotalClicked);
	if (BtnSortRemain) BtnSortRemain->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnSortRemainClicked);
	if (EditBuyCount)
	{
		EditBuyCount->OnTextChanged.AddUniqueDynamic(this, &UAuctionWidget::OnBuyCountChanged);
		EditBuyCount->OnTextCommitted.AddUniqueDynamic(this, &UAuctionWidget::OnBuyCountCommitted);
	}
	if (BtnBuyMax) BtnBuyMax->OnClicked.AddUniqueDynamic(this, &UAuctionWidget::OnBuyMaxClicked);
	// 열 머리의 폭을 목록 줄의 열과 같게 맞춘다(값은 Col*Width 한 곳).
	if (ColUnitBox) ColUnitBox->SetWidthOverride(ColUnitWidth);
	if (ColTotalBox) ColTotalBox->SetWidthOverride(ColTotalWidth);
	if (ColRemainBox) ColRemainBox->SetWidthOverride(ColRemainWidth);
	if (ColIconSpacer) ColIconSpacer->SetWidthOverride(RowIconSize + 8.f);
	// 필터는 나중에 넣는다(D-281) — 자리만 남기고 숨긴다.
	if (FilterBox) FilterBox->SetVisibility(ESlateVisibility::Collapsed);

	if (UJMAuctionManager* Auction = GetAuctionManager())
	{
		AuctionChangedHandle = Auction->OnAuctionChanged.AddUObject(this, &UAuctionWidget::OnAuctionChanged);
	}
	if (UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr)
	{
		InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UAuctionWidget::Refresh);
	}
	OnAuctionChanged();
}

void UAuctionWidget::NativeDestruct()
{
	if (UJMAuctionManager* Auction = GetAuctionManager())
	{
		Auction->OnAuctionChanged.Remove(AuctionChangedHandle);
	}
	if (UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr)
	{
		Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
	}
	Super::NativeDestruct();
}

void UAuctionWidget::OnAuctionChanged()
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction == nullptr) return;

	// 새로 열렸거나 여는 방법(K 키 ↔ NPC)이 바뀌었으면 검색 탭부터 다시 시작한다.
	if (Auction->IsOpen() && (!bWasOpen || bWasAtNpc != Auction->IsAtNpc()))
	{
		ActiveTab = ETab::Search;
		SelectedListingId = SelectedMyListingId = SelectedMailId = 0;
		RegCountFor = 0;
		BuyCountFor = 0;
		if (EditRegPrice) EditRegPrice->SetText(FText::GetEmpty());
		// 지난 검색어를 검색칸에 되돌려 놓는다(창을 닫아도 검색 조건은 남는다).
		if (EditSearch)
		{
			TGuardValue<bool> Guard(bEditingText, true);
			EditSearch->SetText(FText::FromString(Auction->GetKeyword()));
		}
	}
	bWasOpen = Auction->IsOpen();
	bWasAtNpc = Auction->IsAtNpc();

	// NPC 모드에서 인벤토리 아이템을 우클릭해 등록 후보가 생기면 등록 탭을 보여 준다.
	if (Auction->IsAtNpc() && Auction->GetRegisterCandidate() != 0 && Auction->GetRegisterCandidate() != RegCountFor && ActiveTab != ETab::Register)
	{
		SetTab(ETab::Register, true);
		return;
	}
	Refresh();
}

void UAuctionWidget::SetTab(ETab Tab, bool bRequest)
{
	ActiveTab = Tab;
	if (bRequest)
	{
		if (UJMAuctionManager* Auction = GetAuctionManager())
		{
			// 검색 탭은 받아 둔 결과를 그대로 보여 주고(목록은 갱신하지 않음 — D-281), 나머지는 내 등록 + 수령함(등록 수 상한·가격 범위도 함께 온다)을 다시 받는다.
			if (Tab != ETab::Search) Auction->RequestMy();
		}
	}
	Refresh();
}

void UAuctionWidget::ApplyRowIcon(UAuctionRowButton& Row, int32 ItemId) const
{
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Item = ItemId != 0 && DataManager ? DataManager->FindItemById(ItemId) : nullptr;
	UTexture2D* Icon = Item && !Item->Icon.IsNull() ? Item->Icon.LoadSynchronous() : nullptr;
	Row.SetIcon(Icon, Item ? JMUIStyle::GradeColor(this, static_cast<uint8>(Item->Grade)) : FLinearColor::White);
}

FString UAuctionWidget::ItemName(int32 ItemId) const
{
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Row = DataManager ? DataManager->FindItemById(ItemId) : nullptr;
	return Row ? Row->DisplayName.ToString() : FString::FromInt(ItemId);
}

FString UAuctionWidget::RemainText(int32 RemainSec)
{
	if (RemainSec >= 3600) return FString::Printf(TEXT("%d시간 %d분"), RemainSec / 3600, (RemainSec % 3600) / 60);
	if (RemainSec >= 60) return FString::Printf(TEXT("%d분"), RemainSec / 60);
	return TEXT("1분 미만");
}

int64 UAuctionWidget::ReadNumber(const UEditableTextBox* Box) const
{
	if (Box == nullptr) return 0;
	const FString Text = DigitsOnly(Box->GetText().ToString());
	if (Text.IsEmpty()) return 0;
	// 표시 계산이 넘치지 않게만 막는다. 범위 검증은 서버가 한다.
	return FMath::Clamp<int64>(FCString::Atoi64(*Text), 0, 2000000000LL);
}

UAuctionRowButton* UAuctionWidget::MakeRow(UPanelWidget* Panel, int32 Index, bool bColumns)
{
	if (Panel == nullptr || WidgetTree == nullptr) return nullptr;
	UAuctionRowButton* Button = WidgetTree->ConstructWidget<UAuctionRowButton>(UAuctionRowButton::StaticClass());
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	if (Button == nullptr || Label == nullptr) return nullptr;

	FSlateFontInfo FontInfo = Label->GetFont();
	JMUIStyle::ApplyFont(this, FontInfo, JMUIStyle::Body);   // 공용 글꼴(D-270)
	FontInfo.Size = RowFontSize;
	Label->SetFont(FontInfo);
	// 줄 = [아이콘 칸(등급 테두리 + 아이콘)] + 글자 (UI3, D-270)
	UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	UBorder* IconFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	UImage* IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	IconSize->SetWidthOverride(RowIconSize);
	IconSize->SetHeightOverride(RowIconSize);
	FSlateBrush SlotBrush;
	if (JMUIStyle::MakeBrush(this, TEXT("Slot"), SlotBrush)) IconFrame->SetBrush(SlotBrush);
	IconFrame->SetPadding(FMargin(3.f));
	IconFrame->SetContent(IconImage);
	IconSize->AddChild(IconFrame);
	IconFrame->SetVisibility(ESlateVisibility::Collapsed);
	RowBox->AddChildToHorizontalBox(IconSize);
	if (UHorizontalBoxSlot* LabelSlot = RowBox->AddChildToHorizontalBox(Label))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		// 4열 줄은 이름이 남는 폭을 차지하고 숫자 열이 오른쪽에 붙는다
		if (bColumns) LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (bColumns)
	{
		// 숫자 열 셋(1개당 가격 · 가격 · 남은 시간): 폭을 고정하고 오른쪽으로 붙인다(AU2b, D-281)
		UTextBlock* Cols[3] = { nullptr, nullptr, nullptr };
		const float Widths[3] = { ColUnitWidth, ColTotalWidth, ColRemainWidth };
		for (int32 Col = 0; Col < 3; ++Col)
		{
			USizeBox* ColSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			UTextBlock* ColText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			ColText->SetFont(FontInfo);
			ColText->SetJustification(ETextJustify::Right);
			ColSize->SetWidthOverride(Widths[Col]);
			ColSize->AddChild(ColText);
			if (UHorizontalBoxSlot* ColSlot = RowBox->AddChildToHorizontalBox(ColSize)) ColSlot->SetVerticalAlignment(VAlign_Center);
			Cols[Col] = ColText;
		}
		Button->SetColumnLabels(Cols[0], Cols[1], Cols[2]);
	}
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->AddChild(RowBox)))
	{
		ButtonSlot->SetHorizontalAlignment(bColumns ? HAlign_Fill : HAlign_Left);
		ButtonSlot->SetPadding(FMargin(6.f, 2.f));
	}
	Button->Setup(Index, Label);
	Button->SetIconWidgets(IconFrame, IconImage);
	Panel->AddChild(Button);
	return Button;
}

void UAuctionWidget::FillRows(UPanelWidget* Panel, TArray<TObjectPtr<UAuctionRowButton>>& Rows, int32 Count, bool bColumns, void (UAuctionWidget::*OnClick)(int32), TFunctionRef<void(UAuctionRowButton&, int32)> Fill)
{
	if (Panel == nullptr) return;
	while (Rows.Num() < Count)
	{
		UAuctionRowButton* Row = MakeRow(Panel, Rows.Num(), bColumns);
		if (Row == nullptr) return;
		Row->OnRowClicked.BindUObject(this, OnClick);
		Rows.Add(Row);
	}
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		UAuctionRowButton* Row = Rows[Index];
		if (Row == nullptr) continue;
		if (Index >= Count)
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		Fill(*Row, Index);
		Row->SetVisibility(ESlateVisibility::Visible);
	}
}

void UAuctionWidget::Refresh()
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	const UGameInstance* GameInstance = GetGameInstance();
	if (Auction == nullptr || GameInstance == nullptr) return;
	const UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>();
	const int64 Gold = Inventory ? Inventory->GetGold() : 0;
	const bool bAtNpc = Auction->IsAtNpc();

	if (TxtTitle) TxtTitle->SetText(FText::FromString(bAtNpc ? TEXT("경매장") : TEXT("경매장 — 조회")));
	if (TxtGold) TxtGold->SetText(FText::FromString(FString::Printf(TEXT("%s G"), *JMUIStyle::GoldText(Gold))));
	if (TxtStatus) TxtStatus->SetText(FText::FromString(Auction->GetStatusText()));

	// ---- 탭: 고른 탭의 패널만 보이고, 고른 탭의 버튼은 눌린 것처럼 꺼 둔다 ----
	SetShown(PanelSearch, ActiveTab == ETab::Search);
	SetShown(PanelRegister, ActiveTab == ETab::Register);
	SetShown(PanelMy, ActiveTab == ETab::My);
	SetShown(PanelMail, ActiveTab == ETab::Mail);
	if (BtnTabSearch) BtnTabSearch->SetIsEnabled(ActiveTab != ETab::Search);
	if (BtnTabRegister) BtnTabRegister->SetIsEnabled(ActiveTab != ETab::Register);
	if (BtnTabMy) BtnTabMy->SetIsEnabled(ActiveTab != ETab::My);
	if (BtnTabMail) BtnTabMail->SetIsEnabled(ActiveTab != ETab::Mail);

	// ---- 검색 ----
	const bool bCanSearch = Auction->CanSearch();
	const bool bCanAct = Auction->CanAct();
	if (BtnSearch) BtnSearch->SetIsEnabled(bCanSearch);
	if (BtnRefresh) BtnRefresh->SetIsEnabled(bCanSearch);
	if (TxtSearchBtn)
	{
		// 검색 간격이 남으면 남은 초를 붙인다
		const int32 WaitSec = FMath::CeilToInt(Auction->SearchWaitSec());
		TxtSearchBtn->SetText(FText::FromString(bCanSearch ? FString(TEXT("검색")) : FString::Printf(TEXT("검색 · %d초"), FMath::Max(1, WaitSec))));
	}
	// 열 머리: 정렬 중인 열에 방향 표시
	UTextBlock* const SortTexts[] = { TxtSortUnit, TxtSortTotal, TxtSortRemain };
	UButton* const SortButtons[] = { BtnSortUnit, BtnSortTotal, BtnSortRemain };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (SortTexts[Index])
		{
			const bool bActive = Auction->GetSort() == Index;
			SortTexts[Index]->SetText(FText::FromString(bActive ? FString::Printf(TEXT("%s %s"), SortNames[Index], Auction->IsSortDesc() ? TEXT("▼") : TEXT("▲")) : FString(SortNames[Index])));
		}
		if (SortButtons[Index]) SortButtons[Index]->SetIsEnabled(bCanAct);
	}

	// 지금 쪽의 줄(받아 둔 줄 캐시의 한 구간)
	const TArray<FJMAuctionListing>& Listings = Auction->GetListings();
	int32 PageFirst = 0, PageNum = 0;
	Auction->GetPageRange(PageFirst, PageNum);
	const FJMAuctionListing* Selected = Auction->FindListing(SelectedListingId);
	if (Selected && Selected->bClosed) Selected = nullptr;   // 끝난 줄은 고를 수 없다
	if (Selected == nullptr) SelectedListingId = 0;
	FillRows(SearchList, SearchRows, PageNum, true, &UAuctionWidget::OnSearchRowClicked, [this, &Listings, PageFirst](UAuctionRowButton& Row, int32 Index) {
		const FJMAuctionListing& L = Listings[PageFirst + Index];
		FillListingRow(Row, L, L.ListingId == SelectedListingId, true);
	});

	const int32 Page = Auction->GetPage();
	const int32 PageCount = Auction->GetPageCount();
	if (TxtPage) TxtPage->SetText(FText::FromString(FString::Printf(TEXT("%d / %d쪽 · 총 %s건"), Page, PageCount, *JMUIStyle::GoldText(Auction->GetSearchTotal()))));
	if (BtnPrev) BtnPrev->SetIsEnabled(Page > 1);
	if (BtnNext) BtnNext->SetIsEnabled(Auction->CanNextPage());

	// 구매 수량: 스택 아이템을 골랐을 때만 칸이 보인다. 고른 줄이 바뀌면 1로 되돌린다.
	const bool bStackSelected = Selected != nullptr && !Selected->bMine && IsStackItem(Selected->ItemId);
	if (SelectedListingId != BuyCountFor)
	{
		BuyCountFor = SelectedListingId;
		if (EditBuyCount)
		{
			TGuardValue<bool> Guard(bEditingText, true);
			EditBuyCount->SetText(FText::FromString(TEXT("1")));
		}
	}
	// 구매 응답으로 그 줄의 남은 수량이 줄었으면 칸의 수량도 남은 수량으로 덮어쓴다(입력할 때와 같은 규칙)
	if (bStackSelected && EditBuyCount && ReadNumber(EditBuyCount) > Selected->Count)
	{
		TGuardValue<bool> Guard(bEditingText, true);
		EditBuyCount->SetText(FText::FromString(FString::Printf(TEXT("%lld"), Selected->Count)));
	}
	SetShown(BuyCountBox, bStackSelected);
	const int32 Qty = Selected ? BuyCount(*Selected) : 0;
	const int64 BuyTotal = Selected ? Selected->UnitPrice * Qty : 0;
	if (TxtSearchInfo)
	{
		FString Info;
		if (Auction->IsWaitingMore()) Info = TEXT("다음 목록을 불러오는 중입니다");
		else if (Selected == nullptr) Info = !Auction->HasSearchResult() ? TEXT("") : Listings.Num() == 0 ? TEXT("검색 결과가 없습니다") : TEXT("살 물품을 고르세요");
		else if (Selected->bMine) Info = TEXT("내가 올린 물품입니다");
		else
		{
			Info = FString::Printf(TEXT("%s · 1개당 %s G · 합계 %s G"), *ItemName(Selected->ItemId), *JMUIStyle::GoldText(Selected->UnitPrice), *JMUIStyle::GoldText(BuyTotal));
			if (Qty >= 1 && Gold < BuyTotal) Info += TEXT(" · 골드 부족");
		}
		TxtSearchInfo->SetText(FText::FromString(Info));
	}
	if (BtnBuy) BtnBuy->SetIsEnabled(Selected != nullptr && !Selected->bMine && Qty >= 1 && Qty <= Selected->Count && Gold >= BuyTotal && bCanAct);

	// ---- 등록(경매 NPC 앞에서만): 인벤토리에서 우클릭한 행 ----
	const FJMInventoryItem* RegItem = nullptr;
	if (bAtNpc && Inventory && Auction->GetRegisterCandidate() != 0)
	{
		RegItem = Inventory->GetItems().Find(Auction->GetRegisterCandidate());
		if (RegItem && RegItem->EquipPart != 0) RegItem = nullptr;   // 그 사이 장착했다
	}
	// 후보가 바뀌면 수량 칸을 그 행의 전체 수량으로 맞춘다.
	const int64 CandidateId = RegItem ? RegItem->ItemDbId : 0;
	if (CandidateId != RegCountFor)
	{
		RegCountFor = CandidateId;
		if (EditRegCount) EditRegCount->SetText(FText::FromString(RegItem ? FString::Printf(TEXT("%lld"), RegItem->Count) : FString(TEXT("1"))));
	}
	const int64 RegCount = ReadNumber(EditRegCount);
	const int64 RegPrice = ReadNumber(EditRegPrice);
	const int32 MaxListings = Auction->GetMaxListings();
	const int32 MyCount = Auction->GetMyListings().Num();
	const bool bListingFull = MaxListings > 0 && MyCount >= MaxListings;
	// RegPrice = 1개당 가격(AU2, D-281). 합계 = 수량 × 1개당 — 범위 검증은 서버가 하고 여기서는 버튼만 미리 끈다.
	const int64 RegTotal = RegCount * RegPrice;
	const bool bPriceOk = RegPrice >= FMath::Max<int64>(1, Auction->GetMinPrice()) && (Auction->GetMaxPrice() <= 0 || RegPrice <= Auction->GetMaxPrice()) && RegTotal <= TotalPriceMax;
	if (TxtRegisterInfo)
	{
		FString Info;
		if (!bAtNpc) Info = TEXT("등록은 마을의 경매 관리인 앞에서 할 수 있습니다");
		else if (RegItem == nullptr) Info = TEXT("등록할 아이템을 인벤토리에서 우클릭하세요");
		else
		{
			Info = FString::Printf(TEXT("등록: %s %lld / %lld개 · 1개당 %s G · 합계 %s G"), *ItemName(RegItem->ItemId), RegCount, RegItem->Count, *JMUIStyle::GoldText(RegPrice), *JMUIStyle::GoldText(RegTotal));
			if (RegTotal > TotalPriceMax) Info += TEXT(" · 합계가 너무 큽니다");
		}
		if (bAtNpc && MaxListings > 0)
			Info += FString::Printf(TEXT("\n내 등록 %d / %d건 · 1개당 가격 %s ~ %s G%s"), MyCount, MaxListings, *JMUIStyle::GoldText(Auction->GetMinPrice()), *JMUIStyle::GoldText(Auction->GetMaxPrice()), bListingFull ? TEXT(" · 더 등록할 수 없습니다") : TEXT(""));
		TxtRegisterInfo->SetText(FText::FromString(Info));
	}
	if (BtnRegister) BtnRegister->SetIsEnabled(RegItem != nullptr && RegCount >= 1 && RegCount <= RegItem->Count && bPriceOk && !bListingFull && bCanAct);

	// ---- 내 등록 ----
	const TArray<FJMAuctionListing>& MyListings = Auction->GetMyListings();
	if (!MyListings.ContainsByPredicate([this](const FJMAuctionListing& L) { return L.ListingId == SelectedMyListingId; })) SelectedMyListingId = 0;
	FillRows(MyList, MyRows, MyListings.Num(), true, &UAuctionWidget::OnMyRowClicked, [this, &MyListings](UAuctionRowButton& Row, int32 Index) {
		const FJMAuctionListing& L = MyListings[Index];
		FillListingRow(Row, L, L.ListingId == SelectedMyListingId, false);
	});
	if (TxtMyInfo)
	{
		FString Info = MaxListings > 0 ? FString::Printf(TEXT("내 등록 %d / %d건"), MyCount, MaxListings) : FString::Printf(TEXT("내 등록 %d건"), MyCount);
		if (!bAtNpc) Info += TEXT(" — 취소는 경매 관리인 앞에서 할 수 있습니다");
		else if (MyCount > 0) Info += TEXT(" — 취소하면 아이템은 수령함으로 돌아갑니다");
		TxtMyInfo->SetText(FText::FromString(Info));
	}
	if (BtnCancel) BtnCancel->SetIsEnabled(bAtNpc && SelectedMyListingId != 0 && bCanAct);

	// ---- 수령함 ----
	const TArray<FJMAuctionMail>& Mails = Auction->GetMails();
	if (!Mails.ContainsByPredicate([this](const FJMAuctionMail& M) { return M.MailId == SelectedMailId; })) SelectedMailId = 0;
	FillRows(MailList, MailRows, Mails.Num(), false, &UAuctionWidget::OnMailRowClicked, [this, &Mails](UAuctionRowButton& Row, int32 Index) {
		const FJMAuctionMail& M = Mails[Index];
		FString Text;
		switch (M.Kind)
		{
		case Protocol::MAIL_KIND_SALE_GOLD: Text = FString::Printf(TEXT("판매 대금      %s G"), *JMUIStyle::GoldText(M.Gold)); break;
		case Protocol::MAIL_KIND_BOUGHT_ITEM: Text = FString::Printf(TEXT("구매품      %s x%lld"), *ItemName(M.ItemId), M.Count); break;
		case Protocol::MAIL_KIND_EXPIRED: Text = FString::Printf(TEXT("만료 반환      %s x%lld"), *ItemName(M.ItemId), M.Count); break;
		case Protocol::MAIL_KIND_CANCELLED: Text = FString::Printf(TEXT("취소 반환      %s x%lld"), *ItemName(M.ItemId), M.Count); break;
		default: Text = FString::Printf(TEXT("우편 %lld"), M.MailId); break;
		}
		Row.SetRow(Text, M.MailId == SelectedMailId, FLinearColor::White);
		ApplyRowIcon(Row, M.Kind == Protocol::MAIL_KIND_SALE_GOLD ? 0 : M.ItemId);
	});
	if (TxtMailInfo)
	{
		FString Info = FString::Printf(TEXT("수령함 %d건%s"), Mails.Num(), Auction->HasMoreMail() ? TEXT(" · 더 있음") : TEXT(""));
		if (!bAtNpc) Info += TEXT(" — 수령은 경매 관리인 앞에서 할 수 있습니다");
		TxtMailInfo->SetText(FText::FromString(Info));
	}
	if (BtnClaim) BtnClaim->SetIsEnabled(bAtNpc && SelectedMailId != 0 && bCanAct);
	LastCooldownState = CooldownState();
}

void UAuctionWidget::OnFilterClicked(int32 FilterSlot)
{
	// 필터는 나중에 넣는다(D-281). 버튼 줄이 숨겨져 있어 불리지 않는다.
}

void UAuctionWidget::OnSearchRowClicked(int32 Index)
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction == nullptr) return;
	int32 PageFirst = 0, PageNum = 0;
	Auction->GetPageRange(PageFirst, PageNum);
	if (Index < 0 || Index >= PageNum) return;
	const FJMAuctionListing& Listing = Auction->GetListings()[PageFirst + Index];
	if (Listing.bClosed) return;   // 끝난 줄은 고를 수 없다
	SelectedListingId = Listing.ListingId;
	Refresh();
}

void UAuctionWidget::OnMyRowClicked(int32 Index)
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction == nullptr || !Auction->GetMyListings().IsValidIndex(Index)) return;
	SelectedMyListingId = Auction->GetMyListings()[Index].ListingId;
	Refresh();
}

void UAuctionWidget::OnMailRowClicked(int32 Index)
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction == nullptr || !Auction->GetMails().IsValidIndex(Index)) return;
	SelectedMailId = Auction->GetMails()[Index].MailId;
	Refresh();
}

void UAuctionWidget::ShowTab(int32 TabIndex)
{
	SetTab(static_cast<ETab>(FMath::Clamp(TabIndex, 0, 3)), true);
}

void UAuctionWidget::OnTabSearchClicked() { SetTab(ETab::Search, true); }
void UAuctionWidget::OnTabRegisterClicked() { SetTab(ETab::Register, true); }
void UAuctionWidget::OnTabMyClicked() { SetTab(ETab::My, true); }
void UAuctionWidget::OnTabMailClicked() { SetTab(ETab::Mail, true); }

void UAuctionWidget::OnCloseClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->Close();
}

void UAuctionWidget::OnPrevClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->SetPage(Auction->GetPage() - 1);
}

void UAuctionWidget::OnNextClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->SetPage(Auction->GetPage() + 1);
}

void UAuctionWidget::DoSearch()
{
	UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction == nullptr) return;
	// 검색칸이 없으면(옛 WBP) 지난 검색어로 다시 찾는다
	if (Auction->Search(EditSearch ? EditSearch->GetText().ToString() : Auction->GetKeyword())) SelectedListingId = 0;
}

// [새로고침]은 [검색]과 같은 동작이다(D-281)
void UAuctionWidget::OnRefreshClicked() { DoSearch(); }
void UAuctionWidget::OnSearchClicked() { DoSearch(); }

void UAuctionWidget::OnSearchTextChanged(const FText& Text)
{
	if (bEditingText || EditSearch == nullptr) return;
	const FString Current = Text.ToString();
	if (Current.Len() <= KeywordMaxLen) return;
	TGuardValue<bool> Guard(bEditingText, true);
	EditSearch->SetText(FText::FromString(Current.Left(KeywordMaxLen)));
}

void UAuctionWidget::OnSearchTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter) DoSearch();
}

void UAuctionWidget::OnSortUnitClicked() { if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->ToggleSort(Protocol::AUCTION_SORT_UNIT_PRICE); }
void UAuctionWidget::OnSortTotalClicked() { if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->ToggleSort(Protocol::AUCTION_SORT_TOTAL_PRICE); }
void UAuctionWidget::OnSortRemainClicked() { if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->ToggleSort(Protocol::AUCTION_SORT_REMAIN); }

bool UAuctionWidget::IsStackItem(int32 ItemId) const
{
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Row = DataManager ? DataManager->FindItemById(ItemId) : nullptr;
	return Row != nullptr && Row->MaxStack > 1;
}

void UAuctionWidget::FillListingRow(UAuctionRowButton& Row, const FJMAuctionListing& Listing, bool bSelected, bool bMarkMine) const
{
	// 이름은 스택 아이템만 "이름(남은 수량)" (D-281). 끝난 줄은 회색 + "판매 완료", 내 등록은 노란 글자.
	const FString Name = IsStackItem(Listing.ItemId) ? FString::Printf(TEXT("%s(%s)"), *ItemName(Listing.ItemId), *JMUIStyle::GoldText(Listing.Count)) : ItemName(Listing.ItemId);
	const FLinearColor Color = Listing.bClosed ? ClosedTextColor : (bMarkMine && Listing.bMine) ? MineTextColor : FLinearColor::White;
	Row.SetRow(Name, bSelected, Color);
	Row.SetColumns(JMUIStyle::GoldText(Listing.UnitPrice), JMUIStyle::GoldText(Listing.TotalPrice()), Listing.bClosed ? FString(TEXT("판매 완료")) : RemainText(Listing.RemainSec), Color);
	ApplyRowIcon(Row, Listing.ItemId);
}

int32 UAuctionWidget::BuyCount(const FJMAuctionListing& Listing) const
{
	if (!IsStackItem(Listing.ItemId) || EditBuyCount == nullptr) return 1;
	return static_cast<int32>(FMath::Clamp<int64>(ReadNumber(EditBuyCount), 0, Listing.Count));
}

void UAuctionWidget::OnBuyCountChanged(const FText& Text)
{
	if (bEditingText) return;
	const UJMAuctionManager* Auction = GetAuctionManager();
	const FJMAuctionListing* Selected = Auction ? Auction->FindListing(SelectedListingId) : nullptr;
	if (EditBuyCount && Selected)
	{
		// 숫자만 남기고, 남은 수량보다 크면 남은 수량으로 덮어쓴다(50개 남았는데 70 → 50). 빈칸·0은 입력 중이므로 그대로 둔다.
		const FString Current = Text.ToString();
		FString Fixed = DigitsOnly(Current);
		if (!Fixed.IsEmpty() && (Fixed.Len() > 9 || FCString::Atoi64(*Fixed) > Selected->Count)) Fixed = FString::Printf(TEXT("%lld"), Selected->Count);
		if (Fixed != Current)
		{
			TGuardValue<bool> Guard(bEditingText, true);
			EditBuyCount->SetText(FText::FromString(Fixed));
		}
	}
	Refresh();
}

void UAuctionWidget::OnBuyCountCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 칸을 벗어났는데 비었거나 0이면 1로 채운다
	if (EditBuyCount && ReadNumber(EditBuyCount) < 1)
	{
		TGuardValue<bool> Guard(bEditingText, true);
		EditBuyCount->SetText(FText::FromString(TEXT("1")));
	}
	Refresh();
}

void UAuctionWidget::OnBuyMaxClicked()
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	const FJMAuctionListing* Selected = Auction ? Auction->FindListing(SelectedListingId) : nullptr;
	if (EditBuyCount == nullptr || Selected == nullptr) return;
	{
		TGuardValue<bool> Guard(bEditingText, true);
		EditBuyCount->SetText(FText::FromString(FString::Printf(TEXT("%lld"), Selected->Count)));
	}
	Refresh();
}

void UAuctionWidget::OnBuyClicked()
{
	// 골드·등록 상태 검증은 서버가 하고 거부는 시스템 메시지로 온다.
	UJMAuctionManager* Auction = GetAuctionManager();
	const FJMAuctionListing* Selected = Auction ? Auction->FindListing(SelectedListingId) : nullptr;
	if (Selected == nullptr) return;
	const int32 Qty = BuyCount(*Selected);
	if (Qty >= 1) Auction->Buy(SelectedListingId, Qty);
}

int32 UAuctionWidget::CooldownState() const
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction == nullptr) return 0;
	return FMath::CeilToInt(Auction->SearchWaitSec()) * 4 + (Auction->CanAct() ? 2 : 0) + (Auction->CanSearch() ? 1 : 0);
}

void UAuctionWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 요청 간격이 풀리거나 남은 초가 바뀌면 버튼 상태를 다시 그린다(0.2초마다 확인)
	CooldownAccum += InDeltaTime;
	if (CooldownAccum < 0.2f) return;
	CooldownAccum = 0.f;
	const UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction && Auction->IsOpen() && CooldownState() != LastCooldownState) Refresh();
}

void UAuctionWidget::OnRegisterClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager())
		Auction->Register(Auction->GetRegisterCandidate(), static_cast<int32>(ReadNumber(EditRegCount)), ReadNumber(EditRegPrice));
}

void UAuctionWidget::OnCancelClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->Cancel(SelectedMyListingId);
}

void UAuctionWidget::OnClaimClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->Claim(SelectedMailId);
}

void UAuctionWidget::OnEditChanged(const FText& Text)
{
	Refresh();
}
