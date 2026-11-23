#include "Game/JMAuctionManager.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "UI/ChatWidget.h"
#include "Game/JMShopManager.h"
#include "Game/JMTradeManager.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/UIStyle.h"
#include "UI/WidgetNames.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

namespace
{
	// 검색어 최대 글자 수(서버 AUCTION_KEYWORD_MAX와 같다 — 넘는 부분은 서버도 자른다)
	constexpr int32 KeywordMaxLen = 20;
	// 서버 시계·전송 시간 차이로 간격에 걸리지 않게 두는 여유(초)
	constexpr double IntervalSlackSec = 0.1;

	// 거부·실패 사유를 사용자에게 보일 문구. BUSY·TOO_FREQUENT는 띄우지 않는다(부르는 쪽에서 거른다).
	FText AuctionResultText(Protocol::AuctionResult Result, int64 Remain)
	{
		switch (Result)
		{
		case Protocol::AUCTION_RESULT_DEAD: return FText::FromString(TEXT("사망 중에는 경매장을 이용할 수 없습니다."));
		case Protocol::AUCTION_RESULT_NO_LISTING: return FText::FromString(TEXT("판매가 끝난 물품입니다."));
		case Protocol::AUCTION_RESULT_ALREADY_CLOSED: return FText::FromString(TEXT("판매가 끝난 물품입니다."));
		case Protocol::AUCTION_RESULT_SOLD: return FText::FromString(TEXT("이미 판매 완료되었습니다."));
		case Protocol::AUCTION_RESULT_NOT_ENOUGH_COUNT: return FText::FromString(FString::Printf(TEXT("남은 수량은 %lld개입니다."), Remain));
		case Protocol::AUCTION_RESULT_LISTING_BUSY: return FText::FromString(TEXT("처리 중인 물품입니다. 잠시 뒤에 다시 시도해 주세요."));
		case Protocol::AUCTION_RESULT_OWN_ITEM: return FText::FromString(TEXT("내가 등록한 물품은 구매할 수 없습니다."));
		case Protocol::AUCTION_RESULT_NOT_ENOUGH_GOLD: return FText::FromString(TEXT("골드가 부족합니다."));
		case Protocol::AUCTION_RESULT_LISTING_LIMIT: return FText::FromString(TEXT("더 등록할 수 없습니다."));
		case Protocol::AUCTION_RESULT_BAD_PRICE: return FText::FromString(TEXT("1개당 가격이 범위를 벗어났거나 합계가 너무 큽니다."));
		case Protocol::AUCTION_RESULT_NO_ITEM: return FText::FromString(TEXT("등록할 수 없는 아이템이거나 수량이 맞지 않습니다."));
		case Protocol::AUCTION_RESULT_IN_TRADE: return FText::FromString(TEXT("거래에 올린 아이템은 등록할 수 없습니다."));
		case Protocol::AUCTION_RESULT_INVENTORY_FULL: return FText::FromString(TEXT("인벤토리에 빈 칸이 없습니다."));
		case Protocol::AUCTION_RESULT_GOLD_FULL: return FText::FromString(TEXT("골드를 더 가질 수 없습니다."));
		case Protocol::AUCTION_RESULT_DB_ERROR: return FText::FromString(TEXT("서버 오류로 처리하지 못했습니다. 다시 시도해 주세요."));
		case Protocol::AUCTION_RESULT_TOO_FAR: return FText::FromString(TEXT("등록·취소·수령은 경매 관리인 가까이에서만 할 수 있습니다."));
		case Protocol::AUCTION_RESULT_IN_DUNGEON: return FText::FromString(TEXT("던전 안에서는 경매장을 이용할 수 없습니다."));
		case Protocol::AUCTION_RESULT_TRY_LATER: return FText::FromString(TEXT("요청이 몰려 있습니다. 잠시 뒤에 다시 시도해 주세요."));
		case Protocol::AUCTION_RESULT_NO_MAIL: return FText::FromString(TEXT("이미 받았거나 없는 우편입니다."));
		default: return FText::FromString(TEXT("경매장 요청을 처리할 수 없습니다."));
		}
	}

	FJMAuctionListing ToListing(const Protocol::AuctionListingInfo& Info)
	{
		FJMAuctionListing Out;
		Out.ListingId = Info.listing_id();
		Out.ItemId = Info.item_id();
		Out.Count = Info.count();
		Out.UnitPrice = Info.unit_price();
		Out.RemainSec = Info.remain_sec();
		Out.bMine = Info.mine();
		return Out;
	}
}

void UJMAuctionManager::HandleList(const Protocol::SC_AUCTION_LIST_PACKET& Pkt)
{
	const int32 Query = static_cast<int32>(Pkt.query());
	const int32 PktSort = static_cast<int32>(Pkt.sort());
	const bool bMore = Query == Protocol::AUCTION_QUERY_MORE;
	// 다음 묶음은 지금 캐시에 이어 붙는 것만 받는다(그 사이 검색·정렬을 새로 했으면 낡은 응답이다).
	if (bMore && (!bWaitingMore || PktSort != SortKind || Pkt.desc() != bSortDesc))
	{
		UE_LOG(LogTemp, Log, TEXT("Auction: 낡은 다음 묶음 응답을 버립니다."));
		return;
	}

	if (Pkt.page_rows() > 0) PageRows = Pkt.page_rows();
	if (Pkt.search_interval_ms() > 0) SearchIntervalSec = Pkt.search_interval_ms() / 1000.0;
	if (Pkt.action_interval_ms() > 0) ActionIntervalSec = Pkt.action_interval_ms() / 1000.0;

	if (!bMore)
	{
		// 검색·정렬: 캐시를 갈아 끼우고 1쪽부터. 검색어·정렬은 서버가 실제로 쓴 값을 따른다.
		Listings.Reset();
		CurrentPage = 1;
		SortKind = PktSort;
		bSortDesc = Pkt.desc();
		Keyword = UTF8_TO_TCHAR(Pkt.keyword().c_str());
	}
	const int32 KeyCount = Pkt.cursor_keys_size();
	for (int32 Index = 0; Index < Pkt.listings_size(); ++Index)
	{
		FJMAuctionListing Listing = ToListing(Pkt.listings(Index));
		Listing.CursorKey = Index < KeyCount ? Pkt.cursor_keys(Index) : 0;
		Listings.Add(Listing);
	}
	bHasMore = Pkt.has_more();
	bHasResult = true;
	// 더 받을 것이 없으면 받은 줄 수가 전체다(그 사이 팔려 총 건수가 달라졌어도 빈 쪽이 생기지 않게).
	SearchTotal = bHasMore ? FMath::Max<int32>(Pkt.total_count(), Listings.Num()) : Listings.Num();

	if (bMore && PendingPage > 0)
	{
		// 기다리던 쪽으로 넘어간다(받은 줄이 없으면 마지막 쪽에 머문다).
		CurrentPage = FMath::Clamp(PendingPage, 1, GetPageCount());
	}
	bWaitingMore = false;
	PendingPage = 0;
	UE_LOG(LogTemp, Log, TEXT("Auction list: query=%d keyword='%s' sort=%d desc=%d rows=%d cache=%d total=%d more=%d page=%d/%d"), Query, *Keyword, SortKind, bSortDesc ? 1 : 0,
		Pkt.listings_size(), Listings.Num(), SearchTotal, bHasMore ? 1 : 0, CurrentPage, GetPageCount());
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::HandleMy(const Protocol::SC_AUCTION_MY_PACKET& Pkt)
{
	MyListings.Reset();
	for (const Protocol::AuctionListingInfo& Info : Pkt.listings())
	{
		MyListings.Add(ToListing(Info));
	}
	Mails.Reset();
	for (const Protocol::MailInfo& Info : Pkt.mails())
	{
		FJMAuctionMail Mail;
		Mail.MailId = Info.mail_id();
		Mail.Kind = static_cast<int32>(Info.kind());
		Mail.Gold = Info.gold();
		Mail.ItemId = Info.item_id();
		Mail.Count = Info.count();
		Mail.ListingId = Info.listing_id();
		Mails.Add(Mail);
	}
	bMailMore = Pkt.mail_more();
	// 수령함 목록이 실제 상태다: 비었으면(다 받음) HUD 알림 표시를 내리고, 남아 있으면 올린다.
	bHasMailNotice = Mails.Num() > 0 || bMailMore;
	MaxListings = Pkt.max_listings();
	MinPrice = Pkt.min_price();
	MaxPrice = Pkt.max_price();
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::HandleResult(const Protocol::SC_AUCTION_RESULT_PACKET& Pkt)
{
	const Protocol::PacketId Request = Pkt.request();
	const Protocol::AuctionResult Result = Pkt.result();
	const int64 Remain = Pkt.remain();
	UE_LOG(LogTemp, Log, TEXT("Auction result: request=%d result=%d id=%lld remain=%lld"), static_cast<int32>(Request), static_cast<int32>(Result), static_cast<int64>(Pkt.id()), Remain);

	// 구매 응답이 가리키는 줄(보고 있는 목록은 다시 받지 않고 이 줄만 고친다 — D-281)
	FJMAuctionListing* Bought = Request == Protocol::CS_AUCTION_BUY ? FindListingMutable(Pkt.id() != 0 ? Pkt.id() : LastBuyListingId) : nullptr;

	if (Result == Protocol::AUCTION_RESULT_SUCCESS)
	{
		// 성공은 확인 창을 띄우지 않고 창 아래 한 줄로 알린다(아이템·골드는 SC_INVENTORY_UPDATE로 이미 왔다).
		switch (Request)
		{
		case Protocol::CS_AUCTION_REGISTER:
			StatusText = TEXT("등록했습니다.");
			RegisterCandidate = 0;
			RequestMy();
			break;
		case Protocol::CS_AUCTION_BUY:
			StatusText = TEXT("구매했습니다. 산 물건은 경매 관리인에게서 수령하세요.");
			if (Bought)
			{
				Bought->Count = FMath::Max<int64>(0, Remain);
				if (Bought->Count == 0) Bought->bClosed = true;
			}
			break;
		case Protocol::CS_AUCTION_CANCEL:
			StatusText = TEXT("등록을 취소했습니다. 아이템은 수령함으로 돌아갔습니다.");
			RequestMy();
			break;
		case Protocol::CS_MAIL_CLAIM:
			StatusText = TEXT("수령했습니다.");
			RequestMy();
			break;
		default:
			break;
		}
		OnAuctionChanged.Broadcast();
		return;
	}

	// 요청 간격에 걸렸다(창이 버튼을 꺼 두므로 드물다): 조용히 넘기고 그 간격만 다시 건다. 버려진 것이 내 목록이면 간격이 풀릴 때 다시 보낸다.
	if (Result == Protocol::AUCTION_RESULT_TOO_FREQUENT)
	{
		const double Now = FPlatformTime::Seconds();
		if (Request == Protocol::CS_AUCTION_SEARCH && RequestedQuery == Protocol::AUCTION_QUERY_SEARCH) NextSearchTime = Now + SearchIntervalSec + IntervalSlackSec;
		else if (Request == Protocol::CS_AUCTION_MY) NextMyTime = Now + ActionIntervalSec + IntervalSlackSec;
		else NextActionTime = Now + ActionIntervalSec + IntervalSlackSec;
		if (Request == Protocol::CS_AUCTION_SEARCH) { bWaitingMore = false; PendingPage = 0; }
		if (Request == Protocol::CS_AUCTION_MY && bOpen) { bMyPending = true; FlushMy(); }
		OnAuctionChanged.Broadcast();
		return;
	}
	if (Request == Protocol::CS_AUCTION_SEARCH) { bWaitingMore = false; PendingPage = 0; }
	if (Result == Protocol::AUCTION_RESULT_BUSY) { OnAuctionChanged.Broadcast(); return; }

	// 구매 거부: 그 줄만 고친다(다 팔림·만료·취소 = 끝난 줄, 수량 부족 = 남은 수량으로).
	if (Bought)
	{
		if (Result == Protocol::AUCTION_RESULT_SOLD || Result == Protocol::AUCTION_RESULT_ALREADY_CLOSED || Result == Protocol::AUCTION_RESULT_NO_LISTING) Bought->bClosed = true;
		else if (Result == Protocol::AUCTION_RESULT_NOT_ENOUGH_COUNT && Remain > 0) Bought->Count = Remain;
	}
	ShowMessage(AuctionResultText(Result, Remain));
	// 지금은 쓸 수 없는 상태면 창을 닫는다.
	if (Result == Protocol::AUCTION_RESULT_IN_DUNGEON || Result == Protocol::AUCTION_RESULT_DEAD) Close();
	else OnAuctionChanged.Broadcast();
}

// ---------------------------------------------------------------------------------------------------------------
// 요청 간격
bool UJMAuctionManager::CanSearch() const { return FPlatformTime::Seconds() >= NextSearchTime; }
bool UJMAuctionManager::CanAct() const { return FPlatformTime::Seconds() >= NextActionTime; }
float UJMAuctionManager::SearchWaitSec() const { return static_cast<float>(FMath::Max(0.0, NextSearchTime - FPlatformTime::Seconds())); }
void UJMAuctionManager::MarkAction() { NextActionTime = FPlatformTime::Seconds() + ActionIntervalSec + IntervalSlackSec; }

void UJMAuctionManager::FlushMy()
{
	if (!bMyPending) return;
	if (!bOpen) { bMyPending = false; return; }
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr) return;
	const double Wait = NextMyTime - FPlatformTime::Seconds();
	if (Wait > 0.0)
	{
		GameInstance->GetTimerManager().SetTimer(MyTimer, this, &UJMAuctionManager::FlushMy, static_cast<float>(Wait) + 0.02f, false);
		return;
	}
	bMyPending = false;
	NextMyTime = FPlatformTime::Seconds() + ActionIntervalSec + IntervalSlackSec;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionMy();
}

// ---------------------------------------------------------------------------------------------------------------
// 검색 결과
int32 UJMAuctionManager::GetPageCount() const
{
	const int32 Rows = FMath::Max(1, PageRows);
	return FMath::Max(1, (SearchTotal + Rows - 1) / Rows);
}

void UJMAuctionManager::GetPageRange(int32& OutFirst, int32& OutNum) const
{
	const int32 Rows = FMath::Max(1, PageRows);
	OutFirst = FMath::Max(0, (CurrentPage - 1) * Rows);
	OutNum = FMath::Clamp(Listings.Num() - OutFirst, 0, Rows);
}

const FJMAuctionListing* UJMAuctionManager::FindListing(int64 ListingId) const
{
	return ListingId == 0 ? nullptr : Listings.FindByPredicate([ListingId](const FJMAuctionListing& L) { return L.ListingId == ListingId; });
}

FJMAuctionListing* UJMAuctionManager::FindListingMutable(int64 ListingId)
{
	return ListingId == 0 ? nullptr : Listings.FindByPredicate([ListingId](const FJMAuctionListing& L) { return L.ListingId == ListingId; });
}

bool UJMAuctionManager::CanNextPage() const
{
	if (CurrentPage >= GetPageCount() || bWaitingMore) return false;
	if (Listings.Num() > CurrentPage * FMath::Max(1, PageRows)) return true;
	return bHasMore && CanAct();
}

bool UJMAuctionManager::SendSearch(Protocol::AuctionQuery Query, int64 CursorKey, int64 CursorId)
{
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return false;
	RequestedQuery = static_cast<int32>(Query);
	NetManager->SendAuctionSearch(RequestedQuery, RequestedKeyword, RequestedSort, bRequestedDesc, CursorKey, CursorId);
	return true;
}

bool UJMAuctionManager::Search(const FString& InKeyword, int32 InSort, bool bInDesc)
{
	if (!bOpen || !CanSearch()) return false;
	RequestedKeyword = InKeyword.TrimStartAndEnd().Left(KeywordMaxLen);
	RequestedSort = InSort >= 0 ? InSort : SortKind;
	bRequestedDesc = InSort >= 0 ? bInDesc : bSortDesc;
	if (!SendSearch(Protocol::AUCTION_QUERY_SEARCH, 0, 0)) return false;
	NextSearchTime = FPlatformTime::Seconds() + SearchIntervalSec + IntervalSlackSec;
	bWaitingMore = false;
	PendingPage = 0;
	OnAuctionChanged.Broadcast();
	return true;
}

bool UJMAuctionManager::Sort(int32 InSort, bool bInDesc)
{
	if (!bOpen || !CanAct()) return false;
	RequestedKeyword = Keyword;   // 서버는 기억한 검색어를 쓴다(이 값은 보지 않는다)
	RequestedSort = InSort;
	bRequestedDesc = bInDesc;
	if (!SendSearch(Protocol::AUCTION_QUERY_SORT, 0, 0)) return false;
	MarkAction();
	bWaitingMore = false;
	PendingPage = 0;
	OnAuctionChanged.Broadcast();
	return true;
}

bool UJMAuctionManager::ToggleSort(int32 InSort)
{
	return Sort(InSort, InSort == SortKind ? !bSortDesc : false);
}

void UJMAuctionManager::SetPage(int32 Page)
{
	if (!bOpen) return;
	const int32 Rows = FMath::Max(1, PageRows);
	Page = FMath::Clamp(Page, 1, GetPageCount());
	if (Page == CurrentPage) return;
	if (Listings.Num() > (Page - 1) * Rows)
	{
		CurrentPage = Page;
		OnAuctionChanged.Broadcast();
		return;
	}
	// 캐시 끝을 넘는다: 마지막 줄의 커서로 다음 묶음을 받고, 응답이 오면 넘어간다.
	if (!bHasMore || bWaitingMore || !CanAct() || Listings.Num() == 0) return;
	const FJMAuctionListing& Last = Listings.Last();
	RequestedKeyword = Keyword;
	RequestedSort = SortKind;
	bRequestedDesc = bSortDesc;
	if (!SendSearch(Protocol::AUCTION_QUERY_MORE, Last.CursorKey, Last.ListingId)) return;
	MarkAction();
	bWaitingMore = true;
	PendingPage = Page;
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::HandleMailNotice(const Protocol::SC_MAIL_NOTICE_PACKET& Pkt)
{
	UE_LOG(LogTemp, Log, TEXT("Mail notice: kind=%d item=%d count=%lld gold=%lld listing=%lld"), static_cast<int32>(Pkt.kind()), Pkt.item_id(),
		static_cast<int64>(Pkt.count()), static_cast<int64>(Pkt.gold()), static_cast<int64>(Pkt.listing_id()));
	bHasMailNotice = true;

	FString Message;
	const UDataManager* DataManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	const FItemTableRow* Item = DataManager ? DataManager->FindItemById(Pkt.item_id()) : nullptr;
	const FString ItemName = Item ? Item->DisplayName.ToString() : FString::Printf(TEXT("아이템 %d"), Pkt.item_id());
	if (Pkt.kind() == Protocol::MAIL_KIND_SALE_GOLD)
	{
		Message = FString::Printf(TEXT("경매에 올린 물품이 팔렸습니다: %s ×%lld. 대금 %s골드를 경매 관리인의 수령함에서 받으세요."),
			*ItemName, static_cast<int64>(Pkt.count()), *JMUIStyle::GoldText(Pkt.gold()));
	}
	else if (Pkt.kind() == Protocol::MAIL_KIND_EXPIRED)
	{
		Message = FString::Printf(TEXT("경매에 올린 %s ×%lld의 등록 기간이 끝났습니다. 경매 관리인의 수령함에서 돌려받으세요."),
			*ItemName, static_cast<int64>(Pkt.count()));
	}

	if (!Message.IsEmpty())
	{
		ShowMessage(FText::FromString(Message));
		UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
		if (UChatWidget* ChatWidget = UIManager ? Cast<UChatWidget>(UIManager->GetWidget(WidgetNames::Chatting)) : nullptr)
		{
			ChatWidget->AddChatMessage(TEXT("경매"), Message);
		}
	}
	// 수령함 탭을 보고 있었으면 바로 새 목록을 받는다.
	if (bOpen) RequestMy();
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::ClearMailNotice()
{
	bHasMailNotice = false;
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::Clear()
{
	bOpen = false;
	NpcId = 0;
	bMyPending = false;
	bWaitingMore = false;
	PendingPage = 0;
	CurrentPage = 1;
	SearchTotal = 0;
	bHasMore = false;
	bHasResult = false;
	Keyword.Reset();
	Listings.Reset();
	MyListings.Reset();
	Mails.Reset();
	bMailMore = false;
	RegisterCandidate = 0;
	LastBuyListingId = 0;
	StatusText.Reset();
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::Open(int32 InNpcId)
{
	// 상점과 함께 쓰지 않는다(둘 다 인벤토리 우클릭으로 아이템을 고른다).
	if (UJMShopManager* Shop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMShopManager>() : nullptr)
	{
		if (Shop->IsOpen()) Shop->Close();
	}

	bOpen = true;
	NpcId = InNpcId;
	RegisterCandidate = 0;
	StatusText.Reset();
	SetWindowVisible(true);
	OnAuctionChanged.Broadcast();
	// 검색 간격이 지났으면 지난 검색어로 다시 찾는다. 아니면 이전 결과를 그대로 보여 준다(D-281).
	// 던전 안·사망 중이면 서버가 거부하고(HandleResult) 창이 닫힌다.
	Search(Keyword);
}

void UJMAuctionManager::Close()
{
	if (!bOpen) return;
	const bool bWasAtNpc = NpcId != 0;
	bOpen = false;
	NpcId = 0;
	RegisterCandidate = 0;
	bMyPending = false;
	bWaitingMore = false;
	PendingPage = 0;
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->SetWidgetVisible(WidgetNames::Auction, false);
		// NPC 모드에서 함께 연 인벤토리 창도 닫는다(상점과 같은 규칙).
		if (bWasAtNpc) UIManager->SetWidgetVisible(WidgetNames::Inventory, false);
	}
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::RequestMy()
{
	if (!bOpen) return;
	bMyPending = true;
	FlushMy();
}

void UJMAuctionManager::Buy(int64 ListingId, int32 Count)
{
	if (!bOpen || ListingId == 0 || Count < 1 || !CanAct()) return;
	LastBuyListingId = ListingId;
	MarkAction();
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionBuy(ListingId, Count);
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::Register(int64 ItemDbId, int32 Count, int64 UnitPrice)
{
	if (!IsAtNpc() || ItemDbId == 0 || Count < 1 || !CanAct()) return;
	MarkAction();
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionRegister(NpcId, ItemDbId, Count, UnitPrice);
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::Cancel(int64 ListingId)
{
	if (!IsAtNpc() || ListingId == 0 || !CanAct()) return;
	MarkAction();
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionCancel(NpcId, ListingId);
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::Claim(int64 MailId)
{
	if (!IsAtNpc() || MailId == 0 || !CanAct()) return;
	MarkAction();
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendMailClaim(NpcId, MailId);
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::SetRegisterCandidate(int64 ItemDbId)
{
	if (!IsAtNpc() || RegisterCandidate == ItemDbId) return;
	RegisterCandidate = ItemDbId;
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::UpdateFromPlayer(const FVector& PlayerLocation, bool bDead)
{
	if (!bOpen) return;
	const UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr) return;

	// 죽었거나 1:1 거래의 제시 단계가 시작됐거나 상점이 열렸으면 닫는다(인벤토리 우클릭이 겹친다).
	const UJMTradeManager* Trade = GameInstance->GetSubsystem<UJMTradeManager>();
	const UJMShopManager* Shop = GameInstance->GetSubsystem<UJMShopManager>();
	if (bDead || (Trade && Trade->IsOffering()) || (Shop && Shop->IsOpen())) { Close(); return; }

	if (NpcId == 0) return;
	// NPC 모드: NPC가 시야에서 사라졌거나 이용 거리(서버 값 SC_INVENTORY.shop_range_cm)를 벗어나면 닫는다.
	const UJMObjectManager* ObjectManager = GameInstance->GetSubsystem<UJMObjectManager>();
	const UJMInventoryManager* Inventory = GameInstance->GetSubsystem<UJMInventoryManager>();
	FVector NpcLocation;
	if (ObjectManager == nullptr || !ObjectManager->GetNpcLocation(NpcId, NpcLocation)) { Close(); return; }
	const float Range = Inventory ? static_cast<float>(Inventory->GetShopRangeCm()) : 0.f;
	if (FVector::DistSquared2D(PlayerLocation, NpcLocation) > Range * Range) Close();
}

void UJMAuctionManager::SetWindowVisible(bool bVisible)
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	if (UIManager == nullptr) return;
	UIManager->SetWidgetVisible(WidgetNames::Auction, bVisible);
	// 등록할 아이템을 고르려면 인벤토리가 보여야 하므로 NPC 모드에서는 함께 연다.
	if (bVisible && NpcId != 0) UIManager->SetWidgetVisible(WidgetNames::Inventory, true);
}

void UJMAuctionManager::ShowMessage(const FText& Message)
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->ShowSystemMessage(Message);
	}
}
