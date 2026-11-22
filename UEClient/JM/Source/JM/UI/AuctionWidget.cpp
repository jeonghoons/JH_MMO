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

namespace
{
	// 검색 필터(CS_AUCTION_SEARCH.slot): 0 = 전체, 1~7 = 장비 슬롯, 8 = 슬롯 없는 아이템(소비 등)
	const TCHAR* const FilterNames[] = { TEXT("전체"), TEXT("머리"), TEXT("상의"), TEXT("하의"), TEXT("장갑"), TEXT("신발"), TEXT("무기"), TEXT("보조"), TEXT("기타") };
	constexpr int32 FilterCount = UE_ARRAY_COUNT(FilterNames);

	const FLinearColor RowColor(0.10f, 0.10f, 0.12f, 1.f);
	const FLinearColor RowSelectedColor(0.55f, 0.40f, 0.10f, 1.f);
	const FLinearColor MineTextColor(1.f, 0.85f, 0.3f, 1.f);

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
		if (EditRegPrice) EditRegPrice->SetText(FText::GetEmpty());
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
			// 검색 탭은 지금 필터·쪽을, 나머지는 내 등록 + 수령함(등록 수 상한·가격 범위도 함께 온다)을 다시 받는다.
			if (Tab == ETab::Search) Auction->RequestSearch(Auction->GetSearchSlot(), Auction->GetSearchPage());
			else Auction->RequestMy();
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
	return TEXT("곧 만료");
}

int64 UAuctionWidget::ReadNumber(const UEditableTextBox* Box) const
{
	if (Box == nullptr) return 0;
	const FString Text = Box->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty() || !Text.IsNumeric()) return 0;
	// 표시 계산이 넘치지 않게만 막는다. 범위 검증은 서버가 한다.
	return FMath::Clamp<int64>(FCString::Atoi64(*Text), 0, 2000000000LL);
}

UAuctionRowButton* UAuctionWidget::MakeRow(UPanelWidget* Panel, int32 Index)
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
	}
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->AddChild(RowBox)))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Left);
		ButtonSlot->SetPadding(FMargin(6.f, 2.f));
	}
	Button->Setup(Index, Label);
	Button->SetIconWidgets(IconFrame, IconImage);
	Panel->AddChild(Button);
	return Button;
}

void UAuctionWidget::FillRows(UPanelWidget* Panel, TArray<TObjectPtr<UAuctionRowButton>>& Rows, int32 Count, void (UAuctionWidget::*OnClick)(int32), TFunctionRef<void(UAuctionRowButton&, int32)> Fill)
{
	if (Panel == nullptr) return;
	while (Rows.Num() < Count)
	{
		UAuctionRowButton* Row = MakeRow(Panel, Rows.Num());
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

	if (TxtTitle) TxtTitle->SetText(FText::FromString(bAtNpc ? TEXT("경매장") : TEXT("경매장 (조회 — 등록·취소·수령은 경매 관리인에게서)")));
	if (TxtGold) TxtGold->SetText(FText::FromString(FString::Printf(TEXT("%lld G"), Gold)));
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
	FillRows(FilterBox, FilterButtons, FilterCount, &UAuctionWidget::OnFilterClicked, [Auction](UAuctionRowButton& Row, int32 Index) {
		Row.SetRow(FilterNames[Index], Index == Auction->GetSearchSlot(), FLinearColor::White);
	});

	const TArray<FJMAuctionListing>& Listings = Auction->GetListings();
	const FJMAuctionListing* Selected = Listings.FindByPredicate([this](const FJMAuctionListing& L) { return L.ListingId == SelectedListingId; });
	if (Selected == nullptr) SelectedListingId = 0;
	FillRows(SearchList, SearchRows, Listings.Num(), &UAuctionWidget::OnSearchRowClicked, [this, &Listings](UAuctionRowButton& Row, int32 Index) {
		const FJMAuctionListing& L = Listings[Index];
		Row.SetRow(FString::Printf(TEXT("%s x%lld      %lld G      %s%s"), *ItemName(L.ItemId), L.Count, L.Price, *RemainText(L.RemainSec), L.bMine ? TEXT("   [내 등록]") : TEXT("")),
			L.ListingId == SelectedListingId, L.bMine ? MineTextColor : FLinearColor::White);
		ApplyRowIcon(Row, L.ItemId);
	});

	const int32 PageSize = FMath::Max(1, Auction->GetPageSize());
	const int32 Total = Auction->GetSearchTotal();
	const int32 Page = Auction->GetSearchPage();
	const int32 PageCount = FMath::Max(1, (Total + PageSize - 1) / PageSize);
	if (TxtPage) TxtPage->SetText(FText::FromString(FString::Printf(TEXT("%d / %d쪽 (%d건)"), Page + 1, PageCount, Total)));
	if (BtnPrev) BtnPrev->SetIsEnabled(Page > 0);
	if (BtnNext) BtnNext->SetIsEnabled(Page + 1 < PageCount);
	if (TxtSearchInfo)
	{
		if (Selected == nullptr) TxtSearchInfo->SetText(FText::FromString(Listings.Num() == 0 ? TEXT("판매 중인 등록이 없습니다") : TEXT("살 등록을 고르세요 (값은 묶음 전체의 값)")));
		else if (Selected->bMine) TxtSearchInfo->SetText(FText::FromString(TEXT("내가 올린 등록입니다")));
		else TxtSearchInfo->SetText(FText::FromString(FString::Printf(TEXT("구매: %s x%lld — %lld G%s"), *ItemName(Selected->ItemId), Selected->Count, Selected->Price, Gold < Selected->Price ? TEXT(" (골드 부족)") : TEXT(""))));
	}
	if (BtnBuy) BtnBuy->SetIsEnabled(Selected != nullptr && !Selected->bMine && Gold >= Selected->Price);

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
	const bool bPriceOk = RegPrice >= FMath::Max<int64>(1, Auction->GetMinPrice()) && (Auction->GetMaxPrice() <= 0 || RegPrice <= Auction->GetMaxPrice());
	if (TxtRegisterInfo)
	{
		FString Info;
		if (!bAtNpc) Info = TEXT("등록은 마을의 경매 관리인 앞에서 할 수 있습니다");
		else if (RegItem == nullptr) Info = TEXT("등록할 아이템을 인벤토리에서 우클릭하세요");
		else Info = FString::Printf(TEXT("등록: %s  %lld / %lld개  값 %lld G"), *ItemName(RegItem->ItemId), RegCount, RegItem->Count, RegPrice);
		if (bAtNpc && MaxListings > 0)
			Info += FString::Printf(TEXT("\n내 등록 %d / %d건, 값 %lld ~ %lld G (묶음 전체의 값)%s"), MyCount, MaxListings, Auction->GetMinPrice(), Auction->GetMaxPrice(), bListingFull ? TEXT(" — 더 등록할 수 없습니다") : TEXT(""));
		TxtRegisterInfo->SetText(FText::FromString(Info));
	}
	if (BtnRegister) BtnRegister->SetIsEnabled(RegItem != nullptr && RegCount >= 1 && RegCount <= RegItem->Count && bPriceOk && !bListingFull);

	// ---- 내 등록 ----
	const TArray<FJMAuctionListing>& MyListings = Auction->GetMyListings();
	if (!MyListings.ContainsByPredicate([this](const FJMAuctionListing& L) { return L.ListingId == SelectedMyListingId; })) SelectedMyListingId = 0;
	FillRows(MyList, MyRows, MyListings.Num(), &UAuctionWidget::OnMyRowClicked, [this, &MyListings](UAuctionRowButton& Row, int32 Index) {
		const FJMAuctionListing& L = MyListings[Index];
		Row.SetRow(FString::Printf(TEXT("%s x%lld      %lld G      %s"), *ItemName(L.ItemId), L.Count, L.Price, *RemainText(L.RemainSec)), L.ListingId == SelectedMyListingId, FLinearColor::White);
		ApplyRowIcon(Row, L.ItemId);
	});
	if (TxtMyInfo)
	{
		FString Info = MaxListings > 0 ? FString::Printf(TEXT("내 등록 %d / %d건"), MyCount, MaxListings) : FString::Printf(TEXT("내 등록 %d건"), MyCount);
		if (!bAtNpc) Info += TEXT(" — 취소는 경매 관리인 앞에서 할 수 있습니다");
		else if (MyCount > 0) Info += TEXT(" — 취소하면 아이템은 수령함으로 돌아갑니다");
		TxtMyInfo->SetText(FText::FromString(Info));
	}
	if (BtnCancel) BtnCancel->SetIsEnabled(bAtNpc && SelectedMyListingId != 0);

	// ---- 수령함 ----
	const TArray<FJMAuctionMail>& Mails = Auction->GetMails();
	if (!Mails.ContainsByPredicate([this](const FJMAuctionMail& M) { return M.MailId == SelectedMailId; })) SelectedMailId = 0;
	FillRows(MailList, MailRows, Mails.Num(), &UAuctionWidget::OnMailRowClicked, [this, &Mails](UAuctionRowButton& Row, int32 Index) {
		const FJMAuctionMail& M = Mails[Index];
		FString Text;
		switch (M.Kind)
		{
		case Protocol::MAIL_KIND_SALE_GOLD: Text = FString::Printf(TEXT("판매 대금      %lld G"), M.Gold); break;
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
		FString Info = FString::Printf(TEXT("수령함 %d건%s"), Mails.Num(), Auction->HasMoreMail() ? TEXT(" (더 있음 — 받으면 다음 것이 보입니다)") : TEXT(""));
		if (!bAtNpc) Info += TEXT(" — 수령은 경매 관리인 앞에서 할 수 있습니다");
		TxtMailInfo->SetText(FText::FromString(Info));
	}
	if (BtnClaim) BtnClaim->SetIsEnabled(bAtNpc && SelectedMailId != 0);
}

void UAuctionWidget::OnFilterClicked(int32 FilterSlot)
{
	SelectedListingId = 0;
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->RequestSearch(FilterSlot, 0);
}

void UAuctionWidget::OnSearchRowClicked(int32 Index)
{
	const UJMAuctionManager* Auction = GetAuctionManager();
	if (Auction == nullptr || !Auction->GetListings().IsValidIndex(Index)) return;
	SelectedListingId = Auction->GetListings()[Index].ListingId;
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
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->RequestSearch(Auction->GetSearchSlot(), Auction->GetSearchPage() - 1);
}

void UAuctionWidget::OnNextClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->RequestSearch(Auction->GetSearchSlot(), Auction->GetSearchPage() + 1);
}

void UAuctionWidget::OnRefreshClicked()
{
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->RequestSearch(Auction->GetSearchSlot(), Auction->GetSearchPage());
}

void UAuctionWidget::OnBuyClicked()
{
	// 골드·등록 상태 검증은 서버가 하고 거부는 시스템 메시지로 온다.
	if (UJMAuctionManager* Auction = GetAuctionManager()) Auction->Buy(SelectedListingId);
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
