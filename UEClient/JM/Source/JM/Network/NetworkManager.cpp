#include "NetworkManager.h"
#include "NetworkSession.h"
#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "SocketSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "FPacketHandler.h"
#include "Game/JMObjectManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMTradeManager.h"
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

	// 워커가 연결 끊김을 알렸으면 받아 둔 패킷을 처리한 뒤 세션을 정리한다.
	if (Session->IsConnectionLost() && Packet_Session == Session)
	{
		UE_LOG(LogTemp, Warning, TEXT("Connection to server lost"));
		// 로그인한 뒤(게임 중)에 끊겼으면 로그인 화면으로 돌아간다. 서버가 SC_KICK을 보냈어도 도착하지 않을 수 있어 여기서도 처리한다(A2.7).
		if (NetworkId != -1)
			ReturnToLogin(FText::FromString(TEXT("서버와의 연결이 끊겼습니다.")));
		else
			DisconnectFromServer();
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
	Protocol::CS_ENTER_ROOM_PACKET pkt;
	SendProtocol(*this, pkt, Protocol::CS_ENTER_ROOM);
}

void UNetworkManager::SendMove(const Protocol::PositionInfo& PosInfo, bool bForce, uint32 CorrectionSeq)
{
	Protocol::CS_MOVE_PACKET pkt;
	pkt.mutable_pos_info()->CopyFrom(PosInfo);
	pkt.set_force(bForce);
	pkt.set_correction_seq(CorrectionSeq);
	SendProtocol(*this, pkt, Protocol::CS_MOVE);
}

void UNetworkManager::SendAttack(int32 SkillSlot, int32 TargetId, float AimYaw, float AimX, float AimY)
{
	Protocol::CS_ATTACK_PACKET pkt;
	pkt.set_skill_slot(SkillSlot);
	pkt.set_target_id(TargetId);
	pkt.set_aim_yaw(AimYaw);
	pkt.set_aim_x(AimX);
	pkt.set_aim_y(AimY);
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

void UNetworkManager::ReturnToLogin(const FText& Message)
{
	DisconnectFromServer();

	// 확인 메시지는 레벨 전환 뒤 새 레벨에서 다시 뜬다(UJMUIManager::ClearWidgets).
	if (UJMUIManager* UIManager = GetGameInstance()->GetSubsystem<UJMUIManager>())
	{
		UIManager->ShowSystemMessage(Message);
	}

	// 로그인 레벨 = 프로젝트 기본 맵(DefaultEngine.ini GameDefaultMap, LoginGameMode)
	SwitchGameMapLevel(FName("DevMap"));
}

void UNetworkManager::EnterGame()
{
	SwitchGameMapLevel(FName("ParagonSample"));
}

void UNetworkManager::SwitchGameMapLevel(FName LevelToLoad)
{
	if (UJMObjectManager* ObjectManager = GetGameInstance()->GetSubsystem<UJMObjectManager>())
	{
		ObjectManager->ClearObjects();
	}

	// Inventory is re-sent by the server on room enter (SC_INVENTORY).
	if (UJMInventoryManager* Inventory = GetGameInstance()->GetSubsystem<UJMInventoryManager>())
	{
		Inventory->Clear();
	}

	// A trade does not survive a level change (the server cancels it when we leave the room).
	if (UJMTradeManager* Trade = GetGameInstance()->GetSubsystem<UJMTradeManager>())
	{
		Trade->Clear();
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




