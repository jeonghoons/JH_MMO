#include "Game/JMAuctionManager.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "UI/ChatWidget.h"
#include "Game/JMShopManager.h"
#include "Game/JMTradeManager.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

namespace
{
	// 거부·실패 사유를 사용자에게 보일 문구. BUSY·TOO_FREQUENT는 띄우지 않는다(부르는 쪽에서 거른다).
	FText AuctionResultText(Protocol::AuctionResult Result)
	{
		switch (Result)
		{
		case Protocol::AUCTION_RESULT_DEAD: return FText::FromString(TEXT("사망 중에는 경매장을 이용할 수 없습니다."));
		case Protocol::AUCTION_RESULT_NO_LISTING: return FText::FromString(TEXT("없는 등록이거나 이미 끝난 등록입니다."));
		case Protocol::AUCTION_RESULT_SOLD: return FText::FromString(TEXT("다른 사람이 먼저 구매했거나 처리 중인 등록입니다."));
		case Protocol::AUCTION_RESULT_OWN_ITEM: return FText::FromString(TEXT("내가 등록한 물품은 구매할 수 없습니다."));
		case Protocol::AUCTION_RESULT_NOT_ENOUGH_GOLD: return FText::FromString(TEXT("골드가 부족합니다."));
		case Protocol::AUCTION_RESULT_LISTING_LIMIT: return FText::FromString(TEXT("더 등록할 수 없습니다."));
		case Protocol::AUCTION_RESULT_BAD_PRICE: return FText::FromString(TEXT("가격이 범위를 벗어났습니다."));
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
		Out.Price = Info.price();
		Out.RemainSec = Info.remain_sec();
		Out.bMine = Info.mine();
		return Out;
	}
}

void UJMAuctionManager::HandleList(const Protocol::SC_AUCTION_LIST_PACKET& Pkt)
{
	SearchSlot = Pkt.slot();
	SearchPage = Pkt.page();
	SearchTotal = Pkt.total_count();
	PageSize = FMath::Max(1, Pkt.page_size());
	Listings.Reset();
	for (const Protocol::AuctionListingInfo& Info : Pkt.listings())
	{
		Listings.Add(ToListing(Info));
	}
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
	UE_LOG(LogTemp, Log, TEXT("Auction result: request=%d result=%d id=%lld"), static_cast<int32>(Request), static_cast<int32>(Result), static_cast<int64>(Pkt.id()));

	if (Result == Protocol::AUCTION_RESULT_SUCCESS)
	{
		// 성공은 확인 창을 띄우지 않고 창 아래 한 줄로 알린 뒤, 바뀐 목록을 다시 받는다(아이템·골드는 SC_INVENTORY_UPDATE로 이미 왔다).
		switch (Request)
		{
		case Protocol::CS_AUCTION_REGISTER:
			StatusText = TEXT("등록했습니다.");
			RegisterCandidate = 0;
			RequestMy();
			break;
		case Protocol::CS_AUCTION_BUY:
			StatusText = TEXT("구매했습니다. 산 물건은 경매 관리인에게서 수령하세요.");
			RequestSearch(SearchSlot, SearchPage);
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

	// 요청 간격·진행 중인 작업은 조용히 넘긴다(다른 아이템 요청과 같은 규칙). 다만 버려진 것이 조회면 잠시 뒤 다시 보낸다.
	if (Result == Protocol::AUCTION_RESULT_TOO_FREQUENT && bOpen && (Request == Protocol::CS_AUCTION_SEARCH || Request == Protocol::CS_AUCTION_MY))
	{
		(Request == Protocol::CS_AUCTION_SEARCH ? bRetrySearch : bRetryMy) = true;
		ScheduleRetry();
		return;
	}
	if (Result == Protocol::AUCTION_RESULT_BUSY || Result == Protocol::AUCTION_RESULT_TOO_FREQUENT) return;

	ShowMessage(AuctionResultText(Result));
	// 지금은 쓸 수 없는 상태면 창을 닫는다. 팔렸거나 없는 등록이면 목록이 낡은 것이므로 다시 받는다.
	if (Result == Protocol::AUCTION_RESULT_IN_DUNGEON || Result == Protocol::AUCTION_RESULT_DEAD)
	{
		Close();
	}
	else if (bOpen && Request == Protocol::CS_AUCTION_BUY && (Result == Protocol::AUCTION_RESULT_SOLD || Result == Protocol::AUCTION_RESULT_NO_LISTING))
	{
		RequestSearch(SearchSlot, SearchPage);
	}
}

void UJMAuctionManager::ScheduleRetry()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance == nullptr || GameInstance->GetTimerManager().IsTimerActive(RetryTimer)) return;
	// 서버의 요청 간격(EconomyTable.ItemMinIntervalMs 100ms)보다 넉넉히 기다린다.
	GameInstance->GetTimerManager().SetTimer(RetryTimer, this, &UJMAuctionManager::RetryQueries, 0.25f, false);
}

void UJMAuctionManager::RetryQueries()
{
	if (!bOpen) { bRetrySearch = bRetryMy = false; return; }
	UNetworkManager* NetManager = UNetworkManager::Get(this);
	if (NetManager == nullptr) return;
	if (bRetrySearch)
	{
		bRetrySearch = false;
		UE_LOG(LogTemp, Log, TEXT("Auction: retry search slot=%d page=%d"), RequestedSlot, RequestedPage);
		NetManager->SendAuctionSearch(RequestedSlot, RequestedPage);
	}
	else if (bRetryMy)
	{
		bRetryMy = false;
		UE_LOG(LogTemp, Log, TEXT("Auction: retry my"));
		NetManager->SendAuctionMy();
	}
	// 둘 다 걸렸으면 나머지는 다음 차례에(연달아 보내면 또 간격에 걸린다). 지금은 타이머 콜백 안이라 이 타이머가 아직 "활성"으로 보이므로
	// ScheduleRetry(활성이면 건너뜀)를 쓰지 않고 바로 다시 건다.
	if (bRetrySearch || bRetryMy) GetGameInstance()->GetTimerManager().SetTimer(RetryTimer, this, &UJMAuctionManager::RetryQueries, 0.25f, false);
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
		Message = FString::Printf(TEXT("경매에 올린 물품이 팔렸습니다: %s ×%lld. 대금 %lld골드를 경매 관리인의 수령함에서 받으세요."),
			*ItemName, static_cast<int64>(Pkt.count()), static_cast<int64>(Pkt.gold()));
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
	bRetrySearch = bRetryMy = false;
	bOpen = false;
	NpcId = 0;
	SearchSlot = SearchPage = SearchTotal = 0;
	Listings.Reset();
	MyListings.Reset();
	Mails.Reset();
	bMailMore = false;
	RegisterCandidate = 0;
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
	// 지금 필터의 첫 쪽부터. 던전 안·사망 중이면 서버가 거부하고(HandleResult) 창이 닫힌다.
	RequestSearch(SearchSlot, 0);
}

void UJMAuctionManager::Close()
{
	if (!bOpen) return;
	const bool bWasAtNpc = NpcId != 0;
	bOpen = false;
	NpcId = 0;
	RegisterCandidate = 0;
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->SetWidgetVisible(WidgetNames::Auction, false);
		// NPC 모드에서 함께 연 인벤토리 창도 닫는다(상점과 같은 규칙).
		if (bWasAtNpc) UIManager->SetWidgetVisible(WidgetNames::Inventory, false);
	}
	OnAuctionChanged.Broadcast();
}

void UJMAuctionManager::RequestSearch(int32 Slot, int32 Page)
{
	if (!bOpen) return;
	RequestedSlot = Slot;
	RequestedPage = FMath::Max(0, Page);
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionSearch(RequestedSlot, RequestedPage);
}

void UJMAuctionManager::RequestMy()
{
	if (!bOpen) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionMy();
}

void UJMAuctionManager::Buy(int64 ListingId)
{
	if (!bOpen || ListingId == 0) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionBuy(ListingId);
}

void UJMAuctionManager::Register(int64 ItemDbId, int32 Count, int64 Price)
{
	if (!IsAtNpc() || ItemDbId == 0 || Count < 1) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionRegister(NpcId, ItemDbId, Count, Price);
}

void UJMAuctionManager::Cancel(int64 ListingId)
{
	if (!IsAtNpc() || ListingId == 0) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendAuctionCancel(NpcId, ListingId);
}

void UJMAuctionManager::Claim(int64 MailId)
{
	if (!IsAtNpc() || MailId == 0) return;
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendMailClaim(NpcId, MailId);
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
