#include "Player/JMPlayerController.h"
#include "Network/NetMoveStats.h"
#include "Player/JMMyPlayer.h"
#include "Network/NetworkManager.h"
#include "UI/JMUIManager.h"
#include "UI/WidgetNames.h"
#include "Blueprint/UserWidget.h"
#include "Player/GameplayInputMode.h"
#include "UI/ChatWidget.h"
#include "Game/DataManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMObjectManager.h"
#include "Game/JMShopManager.h"
#include "Game/JMTradeManager.h"
#include "Game/JMPartyManager.h"
#include "Game/JMAuctionManager.h"
#include "Game/JMQuestManager.h"
#include "UI/AuctionWidget.h"
#include "UI/PlayerMenuWidget.h"
#include "UI/CombatHudWidget.h"

void AJMPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplyCombatInputMode(this);

	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 블루프린트에서 할당해 둔 클래스들을 이름표와 함께 던져줍니다. 시작부터 보일 UI는 bStartVisible = true.
		// 크로스헤어는 화면 중앙 조준 기준이라 다른 UI보다 위(Z-Order 100)에 둡니다.
		UIManager->RegisterWidget(this, WidgetNames::Crosshair, CrosshairWidgetClass, false, 100, true);
		UIManager->RegisterWidget(this, WidgetNames::Chatting, ChatWidgetClass, false, 0, true);

		// 내 캐릭터가 스폰되면 UJMObjectManager::HandleSpawn이 닫는다.
		UIManager->SetGameplayInput(true);
		// Room 이동으로 레벨이 바뀐 것이면(던전 입장·퇴장, DG3c-2) 문구를 달리한다.
		const UJMObjectManager* ObjectManager = GetGameInstance()->GetSubsystem<UJMObjectManager>();
		const bool bRoomTravel = ObjectManager && ObjectManager->IsRoomTravelActive();
		UIManager->ShowSystemMessage(FText::FromString(bRoomTravel ? TEXT("이동하는 중...") : TEXT("방에 입장하는 중...")), ESystemMessageType::Waiting);
	}

	// 영웅·스킬 에셋을 먼저 로드한 뒤 입장한다. 스폰된 캐릭터가 메시 없이(투명하게) 보이지 않게 하기 위함이다.
	TWeakObjectPtr<AJMPlayerController> WeakThis(this);
	auto SendEnterRoom = [WeakThis]()
	{
		if (UNetworkManager* NetManager = WeakThis.IsValid() ? UNetworkManager::Get(WeakThis.Get()) : nullptr)
		{
			NetManager->SendEnterRoom();
		}
	};

	if (UDataManager* DataManager = GetGameInstance()->GetSubsystem<UDataManager>())
	{
		DataManager->PreloadGameAssets(FSimpleDelegate::CreateLambda(SendEnterRoom));
	}
	else
	{
		SendEnterRoom();
	}
}

// ---- 개발용 상점 명령(L4): 상점 창이 보내는 것과 같은 요청을 콘솔로 보낸다(마우스 없이 확인할 때). 검증은 모두 서버가 한다. ----
// Portal [id] (DG3b): id를 주면 그 포털, 생략(0)하면 범위 안의 가장 가까운 포털로 사용 요청을 보낸다.
// ---- 퀘스트 개발용 콘솔 명령(Q1): 퀘스트 창·F 키·L 키가 보내는 것과 같은 요청 ----
namespace
{
	// 시야 안의 가장 가까운 퀘스트 NPC(거리 제한 없이 고른다 — 멀면 서버가 TOO_FAR로 거부한다)
	int32 FindQuestNpc(const AJMPlayerController& PC)
	{
		const UGameInstance* GameInstance = PC.GetGameInstance();
		const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
		const APawn* MyPawn = PC.GetPawn();
		float DistSq = 0.f;
		const int32 NpcId = (ObjectManager && MyPawn) ? ObjectManager->FindNearestNpc(MyPawn->GetActorLocation(), 1000000.f, DistSq, static_cast<int32>(Protocol::NPC_KIND_QUEST)) : 0;
		if (NpcId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Quest: 시야 안에 퀘스트 NPC가 없습니다."));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Quest: npc=%d dist=%.0fcm"), NpcId, FMath::Sqrt(DistSq));
		}
		return NpcId;
	}

	UJMQuestManager* GetQuestManager(const AJMPlayerController& PC)
	{
		return PC.GetGameInstance() ? PC.GetGameInstance()->GetSubsystem<UJMQuestManager>() : nullptr;
	}
}

void AJMPlayerController::QuestTalk()
{
	UJMQuestManager* Quest = GetQuestManager(*this);
	const int32 NpcId = FindQuestNpc(*this);
	if (Quest && NpcId != 0) Quest->OpenForNpc(NpcId);
}

void AJMPlayerController::QuestRequest(int32 QuestId)
{
	UJMQuestManager* Quest = GetQuestManager(*this);
	const int32 NpcId = FindQuestNpc(*this);
	if (Quest && NpcId != 0) Quest->Request(NpcId, QuestId);
}

void AJMPlayerController::QuestLog()
{
	if (UJMQuestManager* Quest = GetQuestManager(*this)) Quest->OpenLog();
}

void AJMPlayerController::QuestShow()
{
	const UJMQuestManager* Quest = GetQuestManager(*this);
	if (Quest == nullptr) return;
	const TArray<int32> Active = Quest->GetActiveQuestIds();
	UE_LOG(LogTemp, Log, TEXT("QuestShow: active=%d window=%d atNpc=%d selected=%d"), Active.Num(), Quest->IsOpen() ? 1 : 0, Quest->IsAtNpc() ? 1 : 0, Quest->GetSelectedQuestId());
	for (int32 QuestId = 1; QuestId <= 20; ++QuestId)
	{
		const Protocol::QuestState State = Quest->GetState(QuestId);
		if (State == Protocol::QUEST_STATE_NONE) continue;
		UE_LOG(LogTemp, Log, TEXT("  quest=%d state=%d progress=%d ready=%d"), QuestId, static_cast<int32>(State), Quest->GetProgress(QuestId), Quest->IsReadyToComplete(QuestId) ? 1 : 0);
	}
}

void AJMPlayerController::QuestClose()
{
	if (UJMQuestManager* Quest = GetQuestManager(*this)) Quest->Close();
}

void AJMPlayerController::Portal(int32 PortalId)
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	const APawn* MyPawn = GetPawn();
	if (ObjectManager == nullptr || MyPawn == nullptr) return;

	if (PortalId == 0)
	{
		float DistSq = 0.f;
		PortalId = ObjectManager->FindNearestPortal(MyPawn->GetActorLocation(), DistSq);
		if (PortalId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Portal: 범위 안에 포털이 없습니다."));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("Portal: nearest id=%d dist=%.0fcm"), PortalId, FMath::Sqrt(DistSq));
	}
	if (UNetworkManager* NetManager = UNetworkManager::Get(this)) NetManager->SendPortal(PortalId);
}

void AJMPlayerController::ShopOpen()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMObjectManager* ObjectManager = GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
	UJMShopManager* Shop = GameInstance ? GameInstance->GetSubsystem<UJMShopManager>() : nullptr;
	const APawn* MyPawn = GetPawn();
	if (ObjectManager == nullptr || Shop == nullptr || MyPawn == nullptr) return;

	// 시야 안의 가장 가까운 NPC(거리 제한 없이 고른다 — 멀면 서버가 TOO_FAR로 거부한다).
	float DistSq = 0.f;
	const int32 NpcId = ObjectManager->FindNearestNpc(MyPawn->GetActorLocation(), 1000000.f, DistSq, static_cast<int32>(Protocol::NPC_KIND_SHOP));
	if (NpcId == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopOpen: 시야 안에 NPC가 없습니다."));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("ShopOpen: npc=%d dist=%.0fcm"), NpcId, FMath::Sqrt(DistSq));
	Shop->RequestOpen(NpcId);
}

void AJMPlayerController::ShopBuy(int32 ItemId, int32 Count)
{
	UJMShopManager* Shop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMShopManager>() : nullptr;
	if (Shop == nullptr) return;
	if (!Shop->IsOpen())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopBuy: 상점이 열려 있지 않습니다. 먼저 ShopOpen"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("ShopBuy: item=%d count=%d"), ItemId, Count);
	Shop->Buy(ItemId, Count);
}

void AJMPlayerController::ShopSell(int32 ItemId, int32 Count)
{
	UGameInstance* GameInstance = GetGameInstance();
	UJMShopManager* Shop = GameInstance ? GameInstance->GetSubsystem<UJMShopManager>() : nullptr;
	const UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	if (Shop == nullptr || Inventory == nullptr) return;
	if (!Shop->IsOpen())
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSell: 상점이 열려 있지 않습니다. 먼저 ShopOpen"));
		return;
	}
	const int64 ItemDbId = Inventory->FindBagItemDbId(ItemId);
	if (ItemDbId == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopSell: 인벤토리에 item=%d 이(가) 없습니다."), ItemId);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("ShopSell: item=%d dbId=%lld count=%d"), ItemId, ItemDbId, Count);
	// 창에서 하는 것과 같은 순서: 판매 후보로 고른 뒤 판다.
	Shop->SetSellCandidate(ItemDbId);
	Shop->SellSelected(Count);
}

void AJMPlayerController::ShopClose()
{
	if (UJMShopManager* Shop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMShopManager>() : nullptr) Shop->Close();
}

// ---- 경매장 개발용 콘솔 명령(A6.3b): 경매 창이 보내는 것과 같은 요청 ----
void AJMPlayerController::AuctionOpen(int32 AtNpc)
{
	UGameInstance* GameInstance = GetGameInstance();
	UJMAuctionManager* Auction = GameInstance ? GameInstance->GetSubsystem<UJMAuctionManager>() : nullptr;
	if (Auction == nullptr) return;
	int32 NpcId = 0;
	if (AtNpc != 0)
	{
		// 시야 안의 가장 가까운 경매 NPC(거리 제한 없이 고른다 — 멀면 서버가 TOO_FAR로 거부하고, 창은 0.1초 뒤 거리 검사로 닫힌다).
		const UJMObjectManager* ObjectManager = GameInstance->GetSubsystem<UJMObjectManager>();
		const APawn* MyPawn = GetPawn();
		float DistSq = 0.f;
		if (ObjectManager && MyPawn) NpcId = ObjectManager->FindNearestNpc(MyPawn->GetActorLocation(), 1000000.f, DistSq, static_cast<int32>(Protocol::NPC_KIND_AUCTION));
		if (NpcId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("AuctionOpen: 시야 안에 경매 NPC가 없습니다."));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("AuctionOpen: npc=%d dist=%.0fcm"), NpcId, FMath::Sqrt(DistSq));
	}
	Auction->Open(NpcId);
}

void AJMPlayerController::AuctionSearch(const FString& Keyword, int32 Sort, int32 Desc)
{
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr)
	{
		const bool bSent = Auction->Search(Keyword, Sort, Desc != 0);
		UE_LOG(LogTemp, Log, TEXT("AuctionSearch: keyword='%s' sort=%d desc=%d sent=%d wait=%.1f"), *Keyword, Sort, Desc, bSent ? 1 : 0, Auction->SearchWaitSec());
	}
}

void AJMPlayerController::AuctionSort(int32 Sort, int32 Desc)
{
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr)
	{
		const bool bSent = Auction->Sort(Sort, Desc != 0);
		UE_LOG(LogTemp, Log, TEXT("AuctionSort: sort=%d desc=%d sent=%d"), Sort, Desc, bSent ? 1 : 0);
	}
}

void AJMPlayerController::AuctionPage(int32 Page)
{
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr) Auction->SetPage(Page);
}

void AJMPlayerController::AuctionMy()
{
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr) Auction->RequestMy();
}

void AJMPlayerController::AuctionRegister(int32 ItemId, int32 Count, int32 UnitPrice)
{
	UGameInstance* GameInstance = GetGameInstance();
	UJMAuctionManager* Auction = GameInstance ? GameInstance->GetSubsystem<UJMAuctionManager>() : nullptr;
	const UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	if (Auction == nullptr || Inventory == nullptr) return;
	const int64 ItemDbId = Inventory->FindBagItemDbId(ItemId);
	if (ItemDbId == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AuctionRegister: 인벤토리에 item=%d 이(가) 없습니다."), ItemId);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("AuctionRegister: item=%d dbId=%lld count=%d unitPrice=%d"), ItemId, ItemDbId, Count, UnitPrice);
	Auction->SetRegisterCandidate(ItemDbId);
	Auction->Register(ItemDbId, Count, UnitPrice);
}

void AJMPlayerController::AuctionBuy(int32 ListingId, int32 Count)
{
	UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr;
	if (Auction == nullptr) return;
	int64 Target = ListingId;
	if (Target == 0)
	{
		// 지금 쪽에서 내 것이 아니고 끝나지 않은 첫 등록
		int32 First = 0, Num = 0;
		Auction->GetPageRange(First, Num);
		for (int32 Index = First; Index < First + Num; ++Index)
		{
			const FJMAuctionListing& Listing = Auction->GetListings()[Index];
			if (!Listing.bMine && !Listing.bClosed) { Target = Listing.ListingId; break; }
		}
	}
	UE_LOG(LogTemp, Log, TEXT("AuctionBuy: listing=%lld count=%d"), Target, Count);
	Auction->Buy(Target, Count);
}

void AJMPlayerController::AuctionCancel(int32 ListingId)
{
	UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr;
	if (Auction == nullptr) return;
	int64 Target = ListingId;
	if (Target == 0 && Auction->GetMyListings().Num() > 0) Target = Auction->GetMyListings()[0].ListingId;
	UE_LOG(LogTemp, Log, TEXT("AuctionCancel: listing=%lld"), Target);
	Auction->Cancel(Target);
}

void AJMPlayerController::MailClaim(int32 MailId)
{
	UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr;
	if (Auction == nullptr) return;
	int64 Target = MailId;
	if (Target == 0 && Auction->GetMails().Num() > 0) Target = Auction->GetMails()[0].MailId;
	UE_LOG(LogTemp, Log, TEXT("MailClaim: mail=%lld"), Target);
	Auction->Claim(Target);
}

void AJMPlayerController::AuctionShow()
{
	const UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr;
	const UJMInventoryManager* Inventory = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMInventoryManager>() : nullptr;
	if (Auction == nullptr) return;
	UE_LOG(LogTemp, Log, TEXT("AuctionShow: open=%d atNpc=%d gold=%lld keyword='%s' sort=%d desc=%d page=%d/%d rows=%d cache=%d total=%d waitMore=%d searchWait=%.1f canAct=%d status='%s'"),
		Auction->IsOpen() ? 1 : 0, Auction->IsAtNpc() ? 1 : 0, Inventory ? Inventory->GetGold() : 0, *Auction->GetKeyword(), Auction->GetSort(), Auction->IsSortDesc() ? 1 : 0,
		Auction->GetPage(), Auction->GetPageCount(), Auction->GetPageRows(), Auction->GetListings().Num(), Auction->GetSearchTotal(), Auction->IsWaitingMore() ? 1 : 0,
		Auction->SearchWaitSec(), Auction->CanAct() ? 1 : 0, *Auction->GetStatusText());
	int32 PageFirst = 0, PageNum = 0;
	Auction->GetPageRange(PageFirst, PageNum);
	int32 RowIndex = 0;
	for (const FJMAuctionListing& Listing : Auction->GetListings())
	{
		UE_LOG(LogTemp, Log, TEXT("AuctionShow: list[%d]%s listing=%lld item=%d count=%lld unit=%lld total=%lld remain=%d mine=%d closed=%d"), RowIndex,
			RowIndex >= PageFirst && RowIndex < PageFirst + PageNum ? TEXT("*") : TEXT(""), Listing.ListingId, Listing.ItemId, Listing.Count, Listing.UnitPrice, Listing.TotalPrice(),
			Listing.RemainSec, Listing.bMine ? 1 : 0, Listing.bClosed ? 1 : 0);
		++RowIndex;
	}
	for (const FJMAuctionListing& Listing : Auction->GetMyListings())
		UE_LOG(LogTemp, Log, TEXT("AuctionShow: my listing=%lld item=%d count=%lld unit=%lld remain=%d"), Listing.ListingId, Listing.ItemId, Listing.Count, Listing.UnitPrice, Listing.RemainSec);
	for (const FJMAuctionMail& Mail : Auction->GetMails())
		UE_LOG(LogTemp, Log, TEXT("AuctionShow: mail=%lld kind=%d gold=%lld item=%d count=%lld listing=%lld"), Mail.MailId, Mail.Kind, Mail.Gold, Mail.ItemId, Mail.Count, Mail.ListingId);
}

void AJMPlayerController::AuctionTab(int32 TabIndex)
{
	UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr;
	if (UAuctionWidget* Widget = UIManager ? Cast<UAuctionWidget>(UIManager->GetWidget(WidgetNames::Auction)) : nullptr) Widget->ShowTab(TabIndex);
}

void AJMPlayerController::AuctionClose()
{
	if (UJMAuctionManager* Auction = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMAuctionManager>() : nullptr) Auction->Close();
}

void AJMPlayerController::Equip(int32 Part, int32 ItemId)
{
	// 요청만 보낸다. 슬롯·보유 검증과 결과(성공 방송·거부 문구)는 서버가 정한다.
	// 장착은 아이템 종류가 아니라 내 인벤토리의 아이템(item_db_id)으로 요청하므로(A3.3) 그 종류를 인벤토리에서 찾는다.
	UGameInstance* GameInstance = GetGameInstance();
	UNetworkManager* NetManager = GameInstance ? GameInstance->GetSubsystem<UNetworkManager>() : nullptr;
	UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	if (NetManager == nullptr || Inventory == nullptr) return;

	int64 ItemDbId = 0;
	if (ItemId != 0)
	{
		ItemDbId = Inventory->FindBagItemDbId(ItemId);
		if (ItemDbId == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Equip: 인벤토리에 item=%d 이(가) 없습니다. 먼저 Give %d 1 로 받으세요."), ItemId, ItemId);
			return;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Equip: part=%d item=%d dbId=%lld"), Part, ItemId, ItemDbId);
	NetManager->SendChangeEquip(Part, ItemDbId);
}

void AJMPlayerController::Interact()
{
	// F 키와 같은 처리(A7.4 시연 자동 플레이, D-311) — 대상 선택과 요청은 AJMMyPlayer::OnInteractInput, 검증은 서버.
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn())) MyPlayer->DevInteract();
}

void AJMPlayerController::QuickSlotSet(int32 Slot, int32 ItemId)
{
	// HUD 칸에 아이템을 끌어 놓은 것과 같은 요청(UCombatHudWidget::OnSlotItemDropped). 칸 번호는 1부터, 스킬이 있는 칸은 서버가 거부한다.
	if (UNetworkManager* NetManager = UNetworkManager::Get(this))
	{
		UE_LOG(LogTemp, Log, TEXT("QuickSlotSet: slot=%d item=%d"), Slot, ItemId);
		NetManager->SendSetQuickSlot(Slot, ItemId);
	}
}

void AJMPlayerController::QuickSlot(int32 Slot)
{
	// 숫자 키 N과 같은 처리.
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn())) MyPlayer->DevQuickSlot(Slot - 1);
}

void AJMPlayerController::TradeOffer(int32 ItemId, int32 Count)
{
	// 제시 단계에서 인벤토리 아이템을 우클릭한 것과 같은 처리(UInventoryWidget::OnSlotRightClicked → UJMTradeManager::AddItem).
	UGameInstance* GameInstance = GetGameInstance();
	UJMInventoryManager* Inventory = GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	UJMTradeManager* Trade = GameInstance ? GameInstance->GetSubsystem<UJMTradeManager>() : nullptr;
	if (Inventory == nullptr || Trade == nullptr) return;
	const int64 ItemDbId = Inventory->FindBagItemDbId(ItemId);
	UE_LOG(LogTemp, Log, TEXT("TradeOffer: item=%d count=%d dbId=%lld offering=%d"), ItemId, Count, ItemDbId, Trade->IsOffering() ? 1 : 0);
	if (ItemDbId != 0) Trade->AddItem(ItemDbId, Count);
}

void AJMPlayerController::Give(int32 ItemId, int32 Count)
{
	// 요청만 보낸다. 서버가 [Debug] DevCommands를 켜지 않았으면 거부 문구가 온다.
	if (UNetworkManager* NetManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNetworkManager>() : nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("Give: item=%d count=%d"), ItemId, Count);
		NetManager->SendDevGive(ItemId, Count);
	}
}

void AJMPlayerController::NetMoveStats(int32 Reset)
{
	// 원격 플레이어 이동 표시 측정 결과(MC4.1b). 로그로만 남긴다.
	UE_LOG(LogTemp, Log, TEXT("%s"), *FNetMoveStats::Get().Report(Reset != 0));
}

void AJMPlayerController::OnChatKeyPressed()
{
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager)
	{
		// 매니저에서 채팅 위젯을 꺼내온 뒤, 우리가 만든 UCChatWidget으로 형변환(Cast)합니다.
		if (UChatWidget* ChatWidget = Cast<UChatWidget>(UIManager->GetWidget(WidgetNames::Chatting)))
		{
			// 포커스를 주는 함수를 실행합니다.
			ChatWidget->FocusChatInput(this);
		}
	}
}

void AJMPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		// UI6(D-274): Ctrl = 커서 모드 켜고 끄기, Left Alt를 누르는 동안 = 둘러보기, Esc = 플레이어 메뉴 닫기
		InputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &AJMPlayerController::OnCursorModeToggle);
		InputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &AJMPlayerController::OnFreeLookPressed);
		InputComponent->BindKey(EKeys::LeftAlt, IE_Released, this, &AJMPlayerController::OnFreeLookReleased);
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AJMPlayerController::OnEscapePressed);
	}
}

void AJMPlayerController::OnCursorModeToggle()
{
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
	{
		UIManager->SetCursorMode(!UIManager->IsCursorMode());
	}
}

void AJMPlayerController::OnFreeLookPressed()
{
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn())) MyPlayer->SetFreeLook(true);
}

void AJMPlayerController::OnFreeLookReleased()
{
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn())) MyPlayer->SetFreeLook(false);
}

void AJMPlayerController::OnEscapePressed()
{
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UPlayerMenuWidget* Menu = UIManager ? Cast<UPlayerMenuWidget>(UIManager->GetWidget(WidgetNames::PlayerMenu)) : nullptr) Menu->Close();
	// HUD 시스템 메뉴(UI8, D-277)도 닫는다.
	if (UCombatHudWidget* Hud = UIManager ? Cast<UCombatHudWidget>(UIManager->GetWidget(WidgetNames::HUD)) : nullptr) Hud->CloseSystemMenu();
}

void AJMPlayerController::CursorMode(int32 Enable)
{
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>()) UIManager->SetCursorMode(Enable != 0);
	InteractShow();
}

void AJMPlayerController::FreeLook(int32 Enable)
{
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn())) MyPlayer->SetFreeLook(Enable != 0);
	InteractShow();
}

namespace
{
	// 개발용 클릭 좌표: 음수면 화면 가운데
	FVector2D DevClickPos(const APlayerController* PC, float X, float Y)
	{
		if (X >= 0.f && Y >= 0.f) return FVector2D(X, Y);
		int32 ViewX = 0, ViewY = 0;
		PC->GetViewportSize(ViewX, ViewY);
		return FVector2D(ViewX * 0.5f, ViewY * 0.5f);
	}
}

void AJMPlayerController::RClick(float X, float Y)
{
	// 실제 우클릭(AJMMyPlayer::OnRightClickInput)과 같이: 커서가 보일 때만 상호작용한다.
	AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn());
	const UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (MyPlayer == nullptr || UIManager == nullptr) return;
	if (!UIManager->IsCursorShown())
	{
		UE_LOG(LogTemp, Log, TEXT("RClick: cursor hidden — ignored (camera mode)"));
		return;
	}
	const FVector2D Pos = DevClickPos(this, X, Y);
	const AJMMyPlayer::FPickResult Pick = MyPlayer->PickAtScreen(Pos);
	UE_LOG(LogTemp, Log, TEXT("RClick (%.0f, %.0f): pick kind=%d id=%d"), Pos.X, Pos.Y, static_cast<int32>(Pick.Kind), Pick.Id);
	MyPlayer->HandleCursorClick(true, Pos);
	InteractShow();
}

void AJMPlayerController::LClick(float X, float Y)
{
	AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn());
	const UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (MyPlayer == nullptr || UIManager == nullptr) return;
	if (!UIManager->IsCursorShown())
	{
		UE_LOG(LogTemp, Log, TEXT("LClick: cursor hidden — this would attack (use the attack input instead)"));
		return;
	}
	MyPlayer->HandleCursorClick(false, DevClickPos(this, X, Y));
	InteractShow();
}

void AJMPlayerController::RClickObject(int32 ObjectId, int32 bPortal)
{
	// 그 대상의 몸 가운데(포털은 지면 1m 위)를 화면에 투영한 자리를 우클릭한다.
	const UJMObjectManager* ObjectManager = GetGameInstance()->GetSubsystem<UJMObjectManager>();
	if (ObjectManager == nullptr) return;
	FVector World = FVector::ZeroVector;
	bool bFound = false;
	if (bPortal != 0)
	{
		for (const UJMObjectManager::FPortalEntry& Portal : ObjectManager->Portals)
		{
			if (Portal.Id == ObjectId) { World = Portal.Location + FVector(0.f, 0.f, 100.f); bFound = true; break; }
		}
	}
	else if (const TObjectPtr<AJMCharacterBase>* Npc = ObjectManager->Npcs.Find(ObjectId); Npc && *Npc)
	{
		World = (*Npc)->GetActorLocation(); bFound = true;
	}
	else if (const TObjectPtr<AJMCharacterBase>* Obj = ObjectManager->Objects.Find(ObjectId); Obj && *Obj)
	{
		World = (*Obj)->GetActorLocation(); bFound = true;
	}
	FVector2D Screen;
	if (!bFound || !ProjectWorldLocationToScreen(World, Screen, true))
	{
		UE_LOG(LogTemp, Warning, TEXT("RClickObject: %d (portal=%d) not found or off screen"), ObjectId, bPortal);
		return;
	}
	RClick(Screen.X, Screen.Y);
}

void AJMPlayerController::PlayerMenu(const FString& Action)
{
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	UPlayerMenuWidget* Menu = UIManager ? Cast<UPlayerMenuWidget>(UIManager->GetWidget(WidgetNames::PlayerMenu)) : nullptr;
	if (Menu == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlayerMenu: widget not registered (BP_GameMode.PlayerMenuWidgetClass)"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("PlayerMenu %s: target=%d"), *Action, Menu->GetTargetId());
	if (Action == TEXT("trade")) Menu->OnTradeClicked();
	else if (Action == TEXT("party")) Menu->OnPartyClicked();
	else Menu->OnCloseClicked();
}

void AJMPlayerController::InteractShow()
{
	if (const AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn()))
		UE_LOG(LogTemp, Log, TEXT("InteractShow: %s"), *MyPlayer->DevInteractState());
}
// ---- 개발용 파티 명령(DG2): 파티 창·플레이어 메뉴·채팅 명령이 보내는 것과 같은 요청을 콘솔로 보낸다. 검증은 모두 서버가 한다. ----
void AJMPlayerController::PartyInvite(const FString& Name)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		Party->InviteByName(Name);
		UE_LOG(LogTemp, Log, TEXT("PartyInvite: %s"), *Name);
	}
}

void AJMPlayerController::PartyRespond(int32 Accept)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("PartyRespond: accept=%d hasInvite=%d"), Accept, Party->HasInvite() ? 1 : 0);
		Party->Respond(Accept != 0);
	}
}

void AJMPlayerController::PartyLeave()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr)
	{
		Party->Leave();
	}
}

void AJMPlayerController::PartyShow()
{
	UGameInstance* GameInstance = GetGameInstance();
	const UJMPartyManager* Party = GameInstance ? GameInstance->GetSubsystem<UJMPartyManager>() : nullptr;
	if (Party == nullptr) return;

	FString Line = FString::Printf(TEXT("PartyShow: me=%d leader=%d members=%d invite=%s"), Party->GetMyId(), Party->GetLeaderId(), Party->GetMembers().Num(),
		Party->HasInvite() ? *Party->GetInviteFromName() : TEXT("-"));
	for (const FJMPartyMember& Member : Party->GetMembers())
	{
		Line += FString::Printf(TEXT(" [%d %s Lv.%d kind=%d]"), Member.ObjectId, *Member.Name, Member.Level, static_cast<int32>(Member.Kind));
	}
	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
}

void AJMPlayerController::AutoAttack(int32 Count, int32 Slot)
{
	// 근소한 거부 측정(CP1, D-213): 시전할 수 있게 되는 첫 프레임마다 시전한다.
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn()))
	{
		MyPlayer->DevAutoAttack(Count, Slot);
		UE_LOG(LogTemp, Log, TEXT("AutoAttack: count=%d slot=%d"), Count, Slot);
	}
}

void AJMPlayerController::AttackStats(int32 Reset)
{
	if (AJMMyPlayer* MyPlayer = Cast<AJMMyPlayer>(GetPawn()))
		UE_LOG(LogTemp, Log, TEXT("%s"), *MyPlayer->DevAttackStats(Reset != 0));
}

void AJMPlayerController::UIToggle(const FString& WidgetName)
{
	if (UJMUIManager* UIManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UJMUIManager>() : nullptr)
	{
		UIManager->ToggleWidget(FName(*WidgetName));
		UE_LOG(LogTemp, Log, TEXT("UIToggle: %s"), *WidgetName);
	}
}
