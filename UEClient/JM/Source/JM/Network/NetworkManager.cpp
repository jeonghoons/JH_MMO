#include "NetworkManager.h"
#include "NetworkSession.h"
#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "SocketSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "FPacketHandler.h"
#include "Game/JMObjectManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMTradeManager.h"
#include "Game/JMShopManager.h"
#include "Game/JMAuctionManager.h"
#include "Game/JMPartyManager.h"
#include "Game/JMQuestManager.h"
#include "UI/JMUIManager.h"
#include "SendBuffer.h"

void UNetworkManager::Deinitialize()
{
	// PIE 종료/게임 종료 시 스레드와 소켓이 남지 않게 정리한다.
	DisconnectFromServer();

	Super::Deinitialize();
}

void UNetworkManager::Tick(float DeltaTime)
{
	// 핸들러가 Packet_Session을 비워도 이번 Tick 동안은 세션이 살아 있게 복사본을 쓴다.
	TSharedPtr<NetworkSession> Session = Packet_Session;
	if (Session == nullptr)
		return;

	Session->HandleRecvPackets();
	Session->FlushDelayedSends();

	// 워커가 연결 끊김을 알렸으면 받아 둔 패킷을 처리한 뒤 세션을 정리한다.
	if (Session->IsConnectionLost() && Packet_Session == Session)
	{
		UE_LOG(LogTemp, Warning, TEXT("Connection to server lost"));
		// 로그인한 뒤(게임 중)에 끊겼으면 로그인 화면으로 돌아간다. 서버가 SC_KICK을 보냈어도 도착하지 않을 수 있어 여기서도 처리한다(A2.7).
		if (NetworkId != -1)
			ReturnToLogin(FText::FromString(TEXT("서버와의 연결이 끊겼습니다.")));
		else
			DisconnectFromServer();
		return;
	}

	// 하트비트(CS_PING)는 송신 워커 스레드가 보낸다(SendWorker::Run) — 게임 스레드가 맵 로딩으로 멈춰도 끊기지 않게.

	// 예약된 Room 레벨 전환(TravelToRoomLevel): 대기 창이 한 번 그려진 뒤에 레벨을 연다.
	if (!PendingTravelLevel.IsNone() && --PendingTravelFrames <= 0)
	{
		const FName Level = PendingTravelLevel;
		PendingTravelLevel = NAME_None;
		SwitchGameMapLevel(Level, true);
	}
}

void UNetworkManager::ConnectToServer()
{
	if (Packet_Session != nullptr)
	{
		if (Packet_Session->IsConnectionLost() == false)
		{
			UE_LOG(LogTemp, Warning, TEXT("ConnectToServer: already connected"));
			return;
		}
		DisconnectFromServer();
	}

	FSocket* socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(TEXT("Stream"), TEXT("Client Socket"));

	FIPv4Address Ip;
	FIPv4Address::Parse(TEXT("127.0.0.1"), Ip);

	TSharedRef<FInternetAddr> internetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	internetAddr->SetIp(Ip.Value);
	internetAddr->SetPort(8888);

	bool Connected = socket->Connect(*internetAddr);
	if (Connected)
	{
		UE_LOG(LogTemp, Log, TEXT("Connection Success"));

		Packet_Session = MakeShared<NetworkSession>(socket, this);
		Packet_Session->HeartbeatIntervalSec = HeartbeatIntervalSec;
		FPacketHandler::Init();
		Packet_Session->Run();
	}
	else
	{
		// 연결에 실패한 소켓은 세션이 소유하지 않으므로 여기서 해제한다.
		socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(socket);

		if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
		{
			UIManager->ShowSystemMessage(FText::FromString(TEXT("서버에 연결할 수 없습니다. 서버를 켠 뒤 다시 시도해 주세요.")));
		}
	}
}

bool UNetworkManager::IsConnected() const
{
	return Packet_Session != nullptr && Packet_Session->IsConnectionLost() == false;
}

void UNetworkManager::DisconnectFromServer()
{
	if (Packet_Session == nullptr)
		return;

	// 워커 스레드를 종료하고 소켓을 해제한 뒤 세션을 놓는다.
	Packet_Session->Disconnect();
	Packet_Session = nullptr;
	NetworkId = -1;
}

void UNetworkManager::SendPacket(TSharedPtr<class SendBuffer> sendBuffer)
{
	if (Packet_Session == nullptr || sendBuffer.IsValid() == false)
		return;

	Packet_Session->SendPacket(sendBuffer);
}

UNetworkManager* UNetworkManager::Get(const UObject* WorldContext)
{
	if (WorldContext == nullptr || GEngine == nullptr)
		return nullptr;

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
		return nullptr;

	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UNetworkManager>() : nullptr;
}

namespace
{
	template<typename T>
	void SendProtocol(UNetworkManager& Manager, const T& Pkt, uint16 PacketId)
	{
		Manager.SendPacket(SendBuffer::MakeSendBuffer(Pkt, PacketId));
	}
}

void UNetworkManager::SendLogin(const FString& AccountId, const FString& AccountPw)
{
	Protocol::CS_LOGIN_PACKET pkt;
	pkt.set_account_id(TCHAR_TO_UTF8(*AccountId));
	pkt.set_account_pw(TCHAR_TO_UTF8(*AccountPw));
	pkt.set_is_dummy(false);
	SendProtocol(*this, pkt, Protocol::CS_LOGIN);
}

void UNetworkManager::SendSignUp(const FString& AccountId, const FString& AccountPw, int32 PlayerType)
{
	Protocol::CS_SIGNUP_PACKET pkt;
	pkt.set_account_id(TCHAR_TO_UTF8(*AccountId));
	pkt.set_account_pw(TCHAR_TO_UTF8(*AccountPw));
	pkt.set_player_type(static_cast<Protocol::PlayerType>(PlayerType));
	SendProtocol(*this, pkt, Protocol::CS_SIGNUP);
}

void UNetworkManager::SendEnterRoom()
{
	// 로그인 직후에는 첫 입장 요청, Room 이동으로 레벨이 바뀐 뒤에는 "로딩 끝" 신호다(서버가 방이 있는지로 구분, DG3c).
	Protocol::CS_ENTER_ROOM_PACKET pkt;
	pkt.set_result(true);
	SendProtocol(*this, pkt, Protocol::CS_ENTER_ROOM);
	if (UJMObjectManager* ObjectManager = GetGameInstance()->GetSubsystem<UJMObjectManager>())
	{
		ObjectManager->NoteEnterRoomSent();
	}
}

void UNetworkManager::SendMove(const Protocol::PositionInfo& PosInfo, bool bForce, uint32 CorrectionSeq)
{
	Protocol::CS_MOVE_PACKET pkt;
	pkt.mutable_pos_info()->CopyFrom(PosInfo);
	pkt.set_force(bForce);
	pkt.set_correction_seq(CorrectionSeq);
	SendProtocol(*this, pkt, Protocol::CS_MOVE);
}

void UNetworkManager::SendAttack(int32 SkillSlot, int32 TargetId, float AimYaw, float AimX, float AimY, uint32 Seq)
{
	Protocol::CS_ATTACK_PACKET pkt;
	pkt.set_skill_slot(SkillSlot);
	pkt.set_target_id(TargetId);
	pkt.set_aim_yaw(AimYaw);
	pkt.set_aim_x(AimX);
	pkt.set_aim_y(AimY);
	pkt.set_seq(Seq);
	SendProtocol(*this, pkt, Protocol::CS_ATTACK);
}

void UNetworkManager::SendChat(const FString& Message)
{
	Protocol::CS_CHAT_PACKET pkt;
	pkt.set_message(TCHAR_TO_UTF8(*Message));
	SendProtocol(*this, pkt, Protocol::CS_CHAT);
}

void UNetworkManager::SendChangeEquip(int32 Part, int64 ItemDbId)
{
	Protocol::CS_CHANGE_EQUIP_PACKET pkt;
	pkt.set_part(static_cast<Protocol::EquipPart>(Part));
	pkt.set_item_db_id(ItemDbId);
	SendProtocol(*this, pkt, Protocol::CS_CHANGE_EQUIP);
}

void UNetworkManager::SendDevGive(int32 ItemId, int64 Count)
{
	Protocol::CS_DEV_GIVE_PACKET pkt;
	pkt.set_item_id(ItemId);
	pkt.set_count(Count);
	SendProtocol(*this, pkt, Protocol::CS_DEV_GIVE);
}

void UNetworkManager::SendMoveItem(int64 ItemDbId, int32 ToSlot)
{
	Protocol::CS_MOVE_ITEM_PACKET pkt;
	pkt.set_item_db_id(ItemDbId);
	pkt.set_to_slot(ToSlot);
	SendProtocol(*this, pkt, Protocol::CS_MOVE_ITEM);
}

void UNetworkManager::SendPickupItem(int32 ObjectId)
{
	Protocol::CS_PICKUP_ITEM_PACKET pkt;
	pkt.set_object_id(ObjectId);
	SendProtocol(*this, pkt, Protocol::CS_PICKUP_ITEM);
}

void UNetworkManager::SendDropItem(int64 ItemDbId)
{
	Protocol::CS_DROP_ITEM_PACKET pkt;
	pkt.set_item_db_id(ItemDbId);
	SendProtocol(*this, pkt, Protocol::CS_DROP_ITEM);
}

void UNetworkManager::SendUseItem(int64 ItemDbId, int32 ItemId)
{
	Protocol::CS_USE_ITEM_PACKET pkt;
	pkt.set_item_db_id(ItemDbId);
	pkt.set_item_id(ItemId);
	SendProtocol(*this, pkt, Protocol::CS_USE_ITEM);
}

void UNetworkManager::SendSetQuickSlot(int32 Slot, int32 ItemId)
{
	Protocol::CS_SET_QUICKSLOT_PACKET pkt;
	pkt.set_slot(Slot);
	pkt.set_item_id(ItemId);
	SendProtocol(*this, pkt, Protocol::CS_SET_QUICKSLOT);
}

void UNetworkManager::SendShopOpen(int32 NpcId)
{
	Protocol::CS_SHOP_OPEN_PACKET pkt;
	pkt.set_npc_id(NpcId);
	SendProtocol(*this, pkt, Protocol::CS_SHOP_OPEN);
}

void UNetworkManager::SendShopBuy(int32 NpcId, int32 ItemId, int32 Count)
{
	Protocol::CS_SHOP_BUY_PACKET pkt;
	pkt.set_npc_id(NpcId);
	pkt.set_item_id(ItemId);
	pkt.set_count(Count);
	SendProtocol(*this, pkt, Protocol::CS_SHOP_BUY);
}

void UNetworkManager::SendShopSell(int32 NpcId, int64 ItemDbId, int32 Count)
{
	Protocol::CS_SHOP_SELL_PACKET pkt;
	pkt.set_npc_id(NpcId);
	pkt.set_item_db_id(ItemDbId);
	pkt.set_count(Count);
	SendProtocol(*this, pkt, Protocol::CS_SHOP_SELL);
}

void UNetworkManager::SendAuctionSearch(int32 Slot, int32 Page)
{
	Protocol::CS_AUCTION_SEARCH_PACKET pkt;
	pkt.set_slot(Slot);
	pkt.set_page(Page);
	SendProtocol(*this, pkt, Protocol::CS_AUCTION_SEARCH);
}

void UNetworkManager::SendAuctionMy()
{
	Protocol::CS_AUCTION_MY_PACKET pkt;
	SendProtocol(*this, pkt, Protocol::CS_AUCTION_MY);
}

void UNetworkManager::SendAuctionBuy(int64 ListingId)
{
	Protocol::CS_AUCTION_BUY_PACKET pkt;
	pkt.set_listing_id(ListingId);
	SendProtocol(*this, pkt, Protocol::CS_AUCTION_BUY);
}

void UNetworkManager::SendAuctionRegister(int32 NpcId, int64 ItemDbId, int32 Count, int64 Price)
{
	Protocol::CS_AUCTION_REGISTER_PACKET pkt;
	pkt.set_npc_id(NpcId);
	pkt.set_item_db_id(ItemDbId);
	pkt.set_count(Count);
	pkt.set_price(Price);
	SendProtocol(*this, pkt, Protocol::CS_AUCTION_REGISTER);
}

void UNetworkManager::SendAuctionCancel(int32 NpcId, int64 ListingId)
{
	Protocol::CS_AUCTION_CANCEL_PACKET pkt;
	pkt.set_npc_id(NpcId);
	pkt.set_listing_id(ListingId);
	SendProtocol(*this, pkt, Protocol::CS_AUCTION_CANCEL);
}

void UNetworkManager::SendMailClaim(int32 NpcId, int64 MailId)
{
	Protocol::CS_MAIL_CLAIM_PACKET pkt;
	pkt.set_npc_id(NpcId);
	pkt.set_mail_id(MailId);
	SendProtocol(*this, pkt, Protocol::CS_MAIL_CLAIM);
}

void UNetworkManager::SendQuestRequest(int32 NpcId, int32 QuestId)
{
	Protocol::CS_QUEST_REQUEST_PACKET pkt;
	pkt.set_npc_id(NpcId);
	pkt.set_quest_id(QuestId);
	SendProtocol(*this, pkt, Protocol::CS_QUEST_REQUEST);
}

void UNetworkManager::SendTradeRequest(int32 TargetId)
{
	Protocol::CS_TRADE_REQUEST_PACKET pkt;
	pkt.set_target_id(TargetId);
	SendProtocol(*this, pkt, Protocol::CS_TRADE_REQUEST);
}

void UNetworkManager::SendTradeRespond(bool bAccept)
{
	Protocol::CS_TRADE_RESPOND_PACKET pkt;
	pkt.set_accept(bAccept);
	SendProtocol(*this, pkt, Protocol::CS_TRADE_RESPOND);
}

void UNetworkManager::SendTradeSetOffer(const TArray<TPair<int64, int64>>& Items, int64 Gold)
{
	Protocol::CS_TRADE_SET_OFFER_PACKET pkt;
	for (const TPair<int64, int64>& Item : Items)
	{
		Protocol::TradeOfferItem* Out = pkt.add_items();
		Out->set_item_db_id(Item.Key);
		Out->set_count(Item.Value);
	}
	pkt.set_gold(Gold);
	SendProtocol(*this, pkt, Protocol::CS_TRADE_SET_OFFER);
}

void UNetworkManager::SendTradeLock(bool bLocked)
{
	Protocol::CS_TRADE_LOCK_PACKET pkt;
	pkt.set_locked(bLocked);
	SendProtocol(*this, pkt, Protocol::CS_TRADE_LOCK);
}

void UNetworkManager::SendTradeConfirm()
{
	Protocol::CS_TRADE_CONFIRM_PACKET pkt;
	SendProtocol(*this, pkt, Protocol::CS_TRADE_CONFIRM);
}

void UNetworkManager::SendTradeCancel()
{
	Protocol::CS_TRADE_CANCEL_PACKET pkt;
	SendProtocol(*this, pkt, Protocol::CS_TRADE_CANCEL);
}

void UNetworkManager::SendSkillAim(int32 Slot, bool bActive, uint32 Seq)
{
	Protocol::CS_SKILL_AIM_PACKET pkt;
	pkt.set_slot(Slot);
	pkt.set_active(bActive);
	pkt.set_seq(Seq);
	SendProtocol(*this, pkt, Protocol::CS_SKILL_AIM);
}

void UNetworkManager::SendRevive()
{
	Protocol::CS_REVIVE_PACKET pkt;
	SendProtocol(*this, pkt, Protocol::CS_REVIVE);
}

void UNetworkManager::SendPortal(int32 PortalId)
{
	Protocol::CS_PORTAL_PACKET pkt;
	pkt.set_portal_id(PortalId);
	SendProtocol(*this, pkt, Protocol::CS_PORTAL);
}

void UNetworkManager::SendPartyInvite(int32 TargetId, const FString& TargetName)
{
	Protocol::CS_PARTY_INVITE_PACKET pkt;
	pkt.set_target_id(TargetId);
	if (TargetId == 0) pkt.set_target_name(TCHAR_TO_UTF8(*TargetName));
	SendProtocol(*this, pkt, Protocol::CS_PARTY_INVITE);
}

void UNetworkManager::SendPartyRespond(bool bAccept)
{
	Protocol::CS_PARTY_RESPOND_PACKET pkt;
	pkt.set_accept(bAccept);
	SendProtocol(*this, pkt, Protocol::CS_PARTY_RESPOND);
}

void UNetworkManager::SendPartyLeave()
{
	Protocol::CS_PARTY_LEAVE_PACKET pkt;
	SendProtocol(*this, pkt, Protocol::CS_PARTY_LEAVE);
}

void UNetworkManager::ReturnToLogin(const FText& Message)
{
	DisconnectFromServer();

	// 예약해 둔 Room 레벨 전환이 있으면 버린다(전환을 기다리는 사이 SC_KICK·연결 끊김).
	PendingTravelLevel = NAME_None;

	// 파티는 접속이 끝나면 서버에서 탈퇴된다. 사본은 레벨 전환(SwitchGameMapLevel)이 아니라 여기서 비운다(던전 레벨 전환에는 이어져야 함 — DG3c).
	if (UJMPartyManager* Party = GetGameInstance()->GetSubsystem<UJMPartyManager>())
	{
		Party->Clear();
	}
	// 퀘스트 사본(Q1)과 수령함 알림 표시(D-262)도 같은 이유로 여기서만 비운다(서버는 첫 입장 때만 보낸다).
	if (UJMQuestManager* Quest = GetGameInstance()->GetSubsystem<UJMQuestManager>())
	{
		Quest->Clear();
	}
	if (UJMAuctionManager* Auction = GetGameInstance()->GetSubsystem<UJMAuctionManager>())
	{
		Auction->ClearMailNotice();
	}

	// 확인 메시지는 레벨 전환 뒤 새 레벨에서 다시 뜬다(UJMUIManager::ClearWidgets). 빈 글이면(HUD 메뉴의 "로그인 화면으로", UI8) 띄우지 않는다.
	UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>();
	if (UIManager && !Message.IsEmpty())
	{
		UIManager->ShowSystemMessage(Message);
	}

	// 로그인 레벨 = 프로젝트 기본 맵(DefaultEngine.ini GameDefaultMap, LoginGameMode)
	SwitchGameMapLevel(FName("DevMap"));
}

void UNetworkManager::EnterGame(const FString& MapName)
{
	// 들어갈 레벨은 서버가 정한다(ServerConfig [World] MapName → SC_LOGIN map_name, D-185).
	// 이름이 비었거나 그 레벨이 클라이언트에 없으면 기본 맵으로 떨어지지 않게 로그인 화면에 남아 사유를 보여 준다.
	const FString PackageName = FString(TEXT("/Game/Maps/")) + MapName;
	if (MapName.IsEmpty() || !FPackageName::DoesPackageExist(PackageName))
	{
		UE_LOG(LogTemp, Error, TEXT("[Network] EnterGame: level not found '%s'"), *PackageName);
		if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
			UIManager->ShowSystemMessage(FText::FromString(FString::Printf(TEXT("서버가 알려 준 레벨 %s이 클라이언트에 없습니다."), *MapName)));
		return;
	}
	SwitchGameMapLevel(FName(*MapName));
}

void UNetworkManager::TravelToRoomLevel(const FString& MapName)
{
	// 레벨 이름은 서버가 준다(D-185). 클라이언트에 없는 레벨이면 로딩을 끝낼 수 없어 서버가 LoadTimeoutSec 뒤 끊으므로, 바로 로그인 화면으로 돌아가 사유를 보여 준다.
	const FString PackageName = FString(TEXT("/Game/Maps/")) + MapName;
	if (MapName.IsEmpty() || !FPackageName::DoesPackageExist(PackageName))
	{
		UE_LOG(LogTemp, Error, TEXT("[Network] TravelToRoomLevel: level not found '%s'"), *PackageName);
		ReturnToLogin(FText::FromString(FString::Printf(TEXT("서버가 알려 준 레벨 %s이 클라이언트에 없습니다."), *MapName)));
		return;
	}

	// OpenLevel은 맵을 읽는 동안 게임 스레드를 막는다. 멈춘 게임 화면 대신 대기 창이 보이도록 먼저 띄우고, 한 번 그려진 뒤(Tick 두 번 뒤) 레벨을 연다.
	// 이 창은 레벨 전환(ClearWidgets) 때 사라지고, 새 레벨의 AJMPlayerController::BeginPlay가 다시 띄운다.
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
	{
		UIManager->ShowSystemMessage(FText::FromString(TEXT("이동하는 중...")), ESystemMessageType::Waiting);
	}
	PendingTravelLevel = FName(*MapName);
	PendingTravelFrames = 2;
}

void UNetworkManager::SwitchGameMapLevel(FName LevelToLoad, bool bRoomTravel)
{
	if (UJMObjectManager* ObjectManager = GetGameInstance()->GetSubsystem<UJMObjectManager>())
	{
		ObjectManager->ClearObjects();
		// 로그인 진입·로그인 화면 복귀: 레벨 전환 너머로 넘기려던 값도 버린다.
		if (!bRoomTravel) ObjectManager->ResetRoomTravel();
	}

	// 인벤토리 사본: 로그인 뒤 첫 입장에는 서버가 SC_INVENTORY로 다시 보내므로 비운다.
	// Room 이동의 레벨 전환(던전 입장·퇴장)에는 서버가 다시 보내지 않으므로 남긴다(DG3c-2).
	if (UJMInventoryManager* Inventory = bRoomTravel ? nullptr : GetGameInstance()->GetSubsystem<UJMInventoryManager>())
	{
		Inventory->Clear();
	}

	// A trade does not survive a level change (the server cancels it when we leave the room).
	if (UJMTradeManager* Trade = GetGameInstance()->GetSubsystem<UJMTradeManager>())
	{
		Trade->Clear();
	}

	// The shop window state is client-only; drop it with the level.
	if (UJMShopManager* Shop = GetGameInstance()->GetSubsystem<UJMShopManager>())
	{
		Shop->Clear();
	}

	// The auction window copies (search result, my listings, mailbox) are re-requested when the window opens; drop them with the level.
	if (UJMAuctionManager* Auction = GetGameInstance()->GetSubsystem<UJMAuctionManager>())
	{
		Auction->Clear();
	}

	// 퀘스트 창은 위젯과 함께 사라지므로 열림 상태만 내린다. 퀘스트 사본은 서버가 다시 보내지 않으므로 남긴다(로그인 화면 복귀 때만 비움 — ReturnToLogin).
	if (UJMQuestManager* Quest = GetGameInstance()->GetSubsystem<UJMQuestManager>())
	{
		Quest->ResetWindow();
	}

	// 이전 레벨의 위젯 참조가 남지 않도록 UI 목록도 비운다.
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
	{
		UIManager->ClearWidgets();
	}

	UWorld* World = GetWorld();
	if (World == nullptr) return;
	UGameplayStatics::OpenLevel(World, LevelToLoad);
}




