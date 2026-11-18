#include "FPacketHandler.h"
#include "NetworkManager.h"
#include "SendBuffer.h"
#include "Network/NetworkSession.h"
#include "Game/JMObjectManager.h"
#include "Game/JMInventoryManager.h"
#include "Game/JMTradeManager.h"
#include "Game/JMShopManager.h"
#include "UI/JMUIManager.h"
#include "UI/ChatWidget.h"
#include "UI/WidgetNames.h"

PacketHandlerFunc GPacketHandler[UINT16_MAX + 1];

void FPacketHandler::Init()
{
	RegisterHandler(Protocol::SC_LOGIN, &FPacketHandler::Handle_SC_LOGIN);
	RegisterHandler(Protocol::SC_SIGNUP, &FPacketHandler::Handle_SC_SIGNUP);
	RegisterHandler(Protocol::SC_ADD_OBJECT, &FPacketHandler::Handle_SC_ADD_OBJECT);
	RegisterHandler(Protocol::SC_REMOVE_OBJECT, &FPacketHandler::Handle_SC_REMOVE_OBJECT);
	RegisterHandler(Protocol::SC_MOVE_OBJECT, &FPacketHandler::Handle_SC_MOVE_OBJECT);
	RegisterHandler(Protocol::SC_ATTACK, &FPacketHandler::Handle_SC_ATTACK);
	RegisterHandler(Protocol::SC_DAMAGE, &FPacketHandler::Handle_SC_DAMAGE);
	RegisterHandler(Protocol::SC_DEAD, &FPacketHandler::Handle_SC_DEAD);
	RegisterHandler(Protocol::SC_CHAT, &FPacketHandler::Handle_SC_CHAT);
	RegisterHandler(Protocol::SC_CHANGE_EQUIP, &FPacketHandler::Handle_SC_CHANGE_EQUIP);
	RegisterHandler(Protocol::SC_CHANGE_EQUIP_RESULT, &FPacketHandler::Handle_SC_CHANGE_EQUIP_RESULT);
	RegisterHandler(Protocol::SC_SKILL_AIM, &FPacketHandler::Handle_SC_SKILL_AIM);
	RegisterHandler(Protocol::SC_REVIVE, &FPacketHandler::Handle_SC_REVIVE);
	RegisterHandler(Protocol::SC_KICK, &FPacketHandler::Handle_SC_KICK);
	RegisterHandler(Protocol::SC_INVENTORY, &FPacketHandler::Handle_SC_INVENTORY);
	RegisterHandler(Protocol::SC_INVENTORY_UPDATE, &FPacketHandler::Handle_SC_INVENTORY_UPDATE);
	RegisterHandler(Protocol::SC_ITEM_RESULT, &FPacketHandler::Handle_SC_ITEM_RESULT);
	RegisterHandler(Protocol::SC_USE_ITEM, &FPacketHandler::Handle_SC_USE_ITEM);
	RegisterHandler(Protocol::SC_CHANGE_HP, &FPacketHandler::Handle_SC_CHANGE_HP);
	RegisterHandler(Protocol::SC_TRADE_REQUEST, &FPacketHandler::Handle_SC_TRADE_REQUEST);
	RegisterHandler(Protocol::SC_TRADE_STATE, &FPacketHandler::Handle_SC_TRADE_STATE);
	RegisterHandler(Protocol::SC_TRADE_END, &FPacketHandler::Handle_SC_TRADE_END);
	RegisterHandler(Protocol::SC_TRADE_RESULT, &FPacketHandler::Handle_SC_TRADE_RESULT);
	RegisterHandler(Protocol::SC_SHOP_OPEN, &FPacketHandler::Handle_SC_SHOP_OPEN);
	RegisterHandler(Protocol::SC_TELEPORT, &FPacketHandler::Handle_SC_TELEPORT);
	RegisterHandler(Protocol::SC_PORTAL_LIST, &FPacketHandler::Handle_SC_PORTAL_LIST);
}

UJMObjectManager* FPacketHandler::GetObjectManager(const TSharedPtr<NetworkSession>& session)
{
	if (session == nullptr) return nullptr;

	UNetworkManager* NetManager = session->OwnerNetwork.Get();
	if (NetManager == nullptr) return nullptr;

	UGameInstance* GameInstance = NetManager->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UJMObjectManager>() : nullptr;
}


void FPacketHandler::ProcessPacket(const TSharedPtr<NetworkSession>& session, BYTE* buffer, int len)
{
	if (len < static_cast<int>(sizeof(FPacketHeader))) return;

    FPacketHeader* header = reinterpret_cast<FPacketHeader*>(buffer);
	// 헤더에 적힌 크기가 실제로 받은 길이와 다르거나 헤더보다 작으면 버린다.
	if (header->PacketSize < sizeof(FPacketHeader) || header->PacketSize != len)
	{
		UE_LOG(LogTemp, Warning, TEXT("ProcessPacket: size mismatch (PacketId : %d, header : %d, recv : %d)"), header->PacketId, header->PacketSize, len);
		return;
	}

    BYTE* payloadBuffer = buffer + sizeof(FPacketHeader);
    int32_t payloadLen = header->PacketSize - sizeof(FPacketHeader);

    auto& func = GPacketHandler[header->PacketId];
    if (!func)
    {
        UE_LOG(LogTemp, Warning, TEXT("ProcessPacket: no handler for PacketId %d"), header->PacketId);
        return;
    }

    if (func(session, payloadBuffer, payloadLen) == false)
    {
        UE_LOG(LogTemp, Warning, TEXT("ProcessPacket: handler failed to parse PacketId %d"), header->PacketId);
    }
}

namespace
{
	// 서버 응답(로그인 실패, 가입 결과)을 시스템 메시지 창으로 띄운다.
	void ShowSystemMessage(const TSharedPtr<NetworkSession>& session, const FText& Message)
	{
		UNetworkManager* NetManager = session ? session->OwnerNetwork.Get() : nullptr;
		UGameInstance* GameInstance = NetManager ? NetManager->GetGameInstance() : nullptr;
		if (UJMUIManager* UIManager = GameInstance ? GameInstance->GetSubsystem<UJMUIManager>() : nullptr)
		{
			UIManager->ShowSystemMessage(Message);
		}
	}

	FText LoginResultText(Protocol::LoginResult Result)
	{
		switch (Result)
		{
		case Protocol::LOGIN_RESULT_NO_ACCOUNT:			return FText::FromString(TEXT("존재하지 않는 ID입니다."));
		case Protocol::LOGIN_RESULT_WRONG_PASSWORD:		return FText::FromString(TEXT("비밀번호가 틀렸습니다."));
		case Protocol::LOGIN_RESULT_ALREADY_LOGGED_IN:	return FText::FromString(TEXT("이미 로그인되어 있습니다."));
		case Protocol::LOGIN_RESULT_SERVER_ERROR:		return FText::FromString(TEXT("서버 오류로 로그인하지 못했습니다."));
		case Protocol::LOGIN_RESULT_BUSY:				return FText::FromString(TEXT("이전 접속을 정리하는 중입니다. 잠시 후 다시 로그인해 주세요."));
		default:										return FText::FromString(TEXT("로그인에 실패했습니다."));
		}
	}

	// 장착 거부 결과(SC_CHANGE_EQUIP_RESULT) 문구
	FText EquipResultText(Protocol::EquipResult Result)
	{
		switch (Result)
		{
		case Protocol::EQUIP_RESULT_NO_ITEM:		return FText::FromString(TEXT("존재하지 않는 아이템입니다."));
		case Protocol::EQUIP_RESULT_WRONG_SLOT:		return FText::FromString(TEXT("그 슬롯에는 장착할 수 없는 아이템입니다."));
		case Protocol::EQUIP_RESULT_ACTING:			return FText::FromString(TEXT("공격 중에는 장비를 바꿀 수 없습니다."));
		case Protocol::EQUIP_RESULT_DEAD:			return FText::FromString(TEXT("사망 중에는 장비를 바꿀 수 없습니다."));
		case Protocol::EQUIP_RESULT_TOO_FREQUENT:	return FText::FromString(TEXT("장비를 너무 자주 바꾸고 있습니다. 잠시 후 다시 시도하세요."));
		case Protocol::EQUIP_RESULT_NOT_IN_ROOM:	return FText::FromString(TEXT("게임에 들어간 뒤에 장비를 바꿀 수 있습니다."));
		case Protocol::EQUIP_RESULT_NEED_SWORD:		return FText::FromString(TEXT("방패는 한손검을 들었을 때만 착용할 수 있습니다."));
		case Protocol::EQUIP_RESULT_NOT_OWNED:		return FText::FromString(TEXT("인벤토리에 없는 아이템입니다."));
		case Protocol::EQUIP_RESULT_LEVEL_TOO_LOW:	return FText::FromString(TEXT("레벨이 낮아 장착할 수 없습니다."));
		case Protocol::EQUIP_RESULT_INVENTORY_FULL:	return FText::FromString(TEXT("인벤토리에 빈 칸이 없어 장비를 해제할 수 없습니다."));
		case Protocol::EQUIP_RESULT_IN_TRADE:		return FText::FromString(TEXT("거래에 올린 아이템은 장착할 수 없습니다."));
		case Protocol::EQUIP_RESULT_DB_ERROR:		return FText::FromString(TEXT("서버 오류로 장비를 바꾸지 못했습니다."));
		case Protocol::EQUIP_RESULT_NOT_ALLOWED:	return FText::FromString(TEXT("이 계정은 장비를 바꿀 수 없습니다."));
		default:									return FText::FromString(TEXT("장비를 바꾸지 못했습니다."));
		}
	}

	// 아이템 요청 거부 결과(SC_ITEM_RESULT) 문구. BUSY·TOO_FREQUENT는 띄우지 않는다(호출하는 쪽에서 거른다).
	FText ItemResultText(Protocol::ItemResult Result)
	{
		switch (Result)
		{
		case Protocol::ITEM_RESULT_NOT_IN_ROOM:		return FText::FromString(TEXT("게임에 들어간 뒤에 할 수 있습니다."));
		case Protocol::ITEM_RESULT_DEAD:			return FText::FromString(TEXT("사망 중에는 할 수 없습니다."));
		case Protocol::ITEM_RESULT_NOT_ALLOWED:		return FText::FromString(TEXT("허용되지 않은 요청입니다."));
		case Protocol::ITEM_RESULT_NO_ITEM:			return FText::FromString(TEXT("존재하지 않는 아이템입니다."));
		case Protocol::ITEM_RESULT_TOO_FAR:			return FText::FromString(TEXT("너무 멀리 있습니다."));
		case Protocol::ITEM_RESULT_NOT_OWNER:		return FText::FromString(TEXT("아직 주울 수 없는 아이템입니다."));
		case Protocol::ITEM_RESULT_INVENTORY_FULL:	return FText::FromString(TEXT("인벤토리에 빈 칸이 없습니다."));
		case Protocol::ITEM_RESULT_GOLD_FULL:		return FText::FromString(TEXT("골드를 더 가질 수 없습니다."));
		case Protocol::ITEM_RESULT_IN_TRADE:		return FText::FromString(TEXT("거래에 올린 아이템입니다."));
		case Protocol::ITEM_RESULT_NOT_USABLE:		return FText::FromString(TEXT("사용할 수 없는 아이템입니다."));
		case Protocol::ITEM_RESULT_HP_FULL:			return FText::FromString(TEXT("HP가 가득 차 있습니다."));
		case Protocol::ITEM_RESULT_COOLDOWN:		return FText::FromString(TEXT("아직 다시 사용할 수 없습니다."));
		case Protocol::ITEM_RESULT_LEVEL_TOO_LOW:	return FText::FromString(TEXT("레벨이 낮아 사용할 수 없습니다."));
		case Protocol::ITEM_RESULT_DB_ERROR:		return FText::FromString(TEXT("서버 오류로 처리하지 못했습니다."));
		case Protocol::ITEM_RESULT_BAD_SLOT:		return FText::FromString(TEXT("옮길 수 없는 칸입니다."));
		case Protocol::ITEM_RESULT_NOT_ENOUGH_GOLD:	return FText::FromString(TEXT("골드가 부족합니다."));
		case Protocol::ITEM_RESULT_NOT_SELLABLE:	return FText::FromString(TEXT("팔 수 없는 아이템입니다."));
		default:									return FText::FromString(TEXT("요청을 처리하지 못했습니다."));
		}
	}

	UJMInventoryManager* GetInventoryManager(const TSharedPtr<NetworkSession>& session)
	{
		UNetworkManager* NetManager = session ? session->OwnerNetwork.Get() : nullptr;
		UGameInstance* GameInstance = NetManager ? NetManager->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UJMInventoryManager>() : nullptr;
	}
}

void FPacketHandler::Handle_SC_LOGIN(TSharedPtr<NetworkSession>& session, Protocol::SC_LOGIN_INFO_PACKET& pkt)
{
	// 실패도 SC_LOGIN으로 온다. 성공일 때만 게임에 들어간다.
	if (pkt.result() != Protocol::LOGIN_RESULT_SUCCESS)
	{
		ShowSystemMessage(session, LoginResultText(pkt.result()));
		return;
	}

    if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
    {
        NetManager->NetworkId = pkt.object_info().id();
		NetManager->EnterGame(UTF8_TO_TCHAR(pkt.map_name().c_str()));
    }
}

void FPacketHandler::Handle_SC_SIGNUP(TSharedPtr<NetworkSession>& session, Protocol::SC_SIGNUP_PACKET& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("SignUp Result: %d"), pkt.result());
	ShowSystemMessage(session, pkt.result()
		? FText::FromString(TEXT("가입되었습니다. 로그인해 주세요."))
		: FText::FromString(TEXT("가입하지 못했습니다. 이미 있는 ID인지 확인해 주세요.")));
}

void FPacketHandler::Handle_SC_ADD_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_ADD_OBJECT_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleSpawn(pkt.object_info());
}

void FPacketHandler::Handle_SC_REMOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_REMOVE_OBJECT_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleDespawn(pkt.object_id());
}

void FPacketHandler::Handle_SC_MOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_MOVE_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleMove(pkt.object_info(), pkt.correction_seq());
}

void FPacketHandler::Handle_SC_ATTACK(TSharedPtr<NetworkSession>& session, Protocol::SC_ATTACK_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleAttack(pkt.attacker_id(), pkt.skill_id(), pkt.aim_yaw(), pkt.aim_x(), pkt.aim_y(), pkt.cooldown_ms());
}

void FPacketHandler::Handle_SC_DAMAGE(TSharedPtr<NetworkSession>& session, Protocol::SC_DAMAGE_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleDamage(pkt.attacker_id(), pkt.target_id(), pkt.damage(), pkt.remain_hp());
}

void FPacketHandler::Handle_SC_DEAD(TSharedPtr<NetworkSession>& session, Protocol::SC_DEAD_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleDead(pkt.object_id(), pkt.revive_delay_ms());
}

// 서버가 이 연결을 끊기 직전에 보낸다(A2.7). 연결을 닫고 로그인 화면으로 돌아가 사유를 보여준다.
void FPacketHandler::Handle_SC_KICK(TSharedPtr<NetworkSession>& session, Protocol::SC_KICK_PACKET& pkt)
{
	UNetworkManager* NetManager = session ? session->OwnerNetwork.Get() : nullptr;
	if (NetManager == nullptr) return;

	const FText Message = (pkt.reason() == Protocol::KICK_REASON_DUPLICATE_LOGIN)
		? FText::FromString(TEXT("다른 곳에서 같은 계정으로 로그인해 접속이 끊겼습니다."))
		: FText::FromString(TEXT("서버가 접속을 끊었습니다."));
	NetManager->ReturnToLogin(Message);
}

void FPacketHandler::Handle_SC_REVIVE(TSharedPtr<NetworkSession>& session, Protocol::SC_REVIVE_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleRevive(pkt.object_id(), pkt.pos_info(), pkt.hp(), pkt.correction_seq());
}

// ---- 포털(L5, D-203): 순간이동과 포털 목록 ----
void FPacketHandler::Handle_SC_TELEPORT(TSharedPtr<NetworkSession>& session, Protocol::SC_TELEPORT_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleTeleport(pkt.object_id(), pkt.pos_info(), pkt.correction_seq());
}

void FPacketHandler::Handle_SC_PORTAL_LIST(TSharedPtr<NetworkSession>& session, Protocol::SC_PORTAL_LIST_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandlePortalList(pkt);
}

void FPacketHandler::Handle_SC_CHANGE_EQUIP(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_EQUIP_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleChangeEquip(pkt.object_id(), pkt.part(), pkt.item_id(), pkt.stat());
}

void FPacketHandler::Handle_SC_SKILL_AIM(TSharedPtr<NetworkSession>& session, Protocol::SC_SKILL_AIM_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleSkillAim(pkt.object_id(), pkt.skill_id(), pkt.active(), pkt.seq());
}

void FPacketHandler::Handle_SC_CHANGE_EQUIP_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_EQUIP_RESULT_PACKET& pkt)
{
	// 성공은 SC_CHANGE_EQUIP 방송으로 오므로 이 패킷은 거부일 때만 온다.
	UE_LOG(LogTemp, Log, TEXT("ChangeEquip rejected: result=%d part=%d item=%d dbId=%lld"), static_cast<int32>(pkt.result()), static_cast<int32>(pkt.part()), pkt.item_id(), static_cast<int64>(pkt.item_db_id()));
	// 진행 중인 다른 아이템 작업 때문에 거부된 것(BUSY)은 다시 시도하면 되므로 문구를 띄우지 않는다.
	if (pkt.result() == Protocol::EQUIP_RESULT_BUSY) return;
	ShowSystemMessage(session, EquipResultText(pkt.result()));
}

void FPacketHandler::Handle_SC_INVENTORY(TSharedPtr<NetworkSession>& session, Protocol::SC_INVENTORY_PACKET& pkt)
{
	if (UJMInventoryManager* Inventory = GetInventoryManager(session))
		Inventory->HandleInventory(pkt);
}

void FPacketHandler::Handle_SC_INVENTORY_UPDATE(TSharedPtr<NetworkSession>& session, Protocol::SC_INVENTORY_UPDATE_PACKET& pkt)
{
	if (UJMInventoryManager* Inventory = GetInventoryManager(session))
		Inventory->HandleInventoryUpdate(pkt);
}

void FPacketHandler::Handle_SC_USE_ITEM(TSharedPtr<NetworkSession>& session, Protocol::SC_USE_ITEM_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleUseItem(pkt.object_id(), pkt.item_id(), pkt.hp(), pkt.cooldown_ms());
}

void FPacketHandler::Handle_SC_CHANGE_HP(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_HP_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleChangeHp(pkt.object_id(), pkt.hp());
}

void FPacketHandler::Handle_SC_ITEM_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_ITEM_RESULT_PACKET& pkt)
{
	// 성공은 SC_INVENTORY_UPDATE로 오므로 이 패킷은 거부일 때만 온다.
	UE_LOG(LogTemp, Log, TEXT("Item request rejected: request=%d result=%d"), static_cast<int32>(pkt.request()), static_cast<int32>(pkt.result()));
	if (pkt.result() == Protocol::ITEM_RESULT_BUSY || pkt.result() == Protocol::ITEM_RESULT_TOO_FREQUENT) return;
	// 상점 요청(L4)은 같은 결과 코드라도 뜻이 다른 것이 있다: IN_TRADE = 1:1 거래 중에는 상점 전체를 쓸 수 없음(D-201), NO_ITEM = 상점·품목·수량 오류.
	const bool bShopRequest = pkt.request() == Protocol::CS_SHOP_OPEN || pkt.request() == Protocol::CS_SHOP_BUY || pkt.request() == Protocol::CS_SHOP_SELL;
	if (bShopRequest && pkt.result() == Protocol::ITEM_RESULT_IN_TRADE)
	{
		ShowSystemMessage(session, FText::FromString(TEXT("거래 중에는 상점을 이용할 수 없습니다.")));
		return;
	}
	if (bShopRequest && pkt.result() == Protocol::ITEM_RESULT_NO_ITEM)
	{
		ShowSystemMessage(session, FText::FromString(TEXT("상점에서 처리할 수 없는 요청입니다.")));
		return;
	}
	ShowSystemMessage(session, ItemResultText(pkt.result()));
}

// ---- 상점 NPC(L4): 품목을 받아 창을 띄우는 일은 UJMShopManager가 맡는다 ----
void FPacketHandler::Handle_SC_SHOP_OPEN(TSharedPtr<NetworkSession>& session, Protocol::SC_SHOP_OPEN_PACKET& pkt)
{
	UNetworkManager* NetManager = session ? session->OwnerNetwork.Get() : nullptr;
	UGameInstance* GameInstance = NetManager ? NetManager->GetGameInstance() : nullptr;
	if (UJMShopManager* Shop = GameInstance ? GameInstance->GetSubsystem<UJMShopManager>() : nullptr) Shop->HandleOpen(pkt);
}

// ---- 1:1 거래(A6.3a): 상태·안내는 UJMTradeManager가 맡는다 ----
namespace
{
	UJMTradeManager* GetTradeManager(const TSharedPtr<NetworkSession>& session)
	{
		UNetworkManager* NetManager = session ? session->OwnerNetwork.Get() : nullptr;
		UGameInstance* GameInstance = NetManager ? NetManager->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UJMTradeManager>() : nullptr;
	}
}

void FPacketHandler::Handle_SC_TRADE_REQUEST(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_REQUEST_PACKET& pkt)
{
	if (UJMTradeManager* Trade = GetTradeManager(session)) Trade->HandleRequest(pkt);
}

void FPacketHandler::Handle_SC_TRADE_STATE(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_STATE_PACKET& pkt)
{
	if (UJMTradeManager* Trade = GetTradeManager(session)) Trade->HandleState(pkt);
}

void FPacketHandler::Handle_SC_TRADE_END(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_END_PACKET& pkt)
{
	if (UJMTradeManager* Trade = GetTradeManager(session)) Trade->HandleEnd(pkt.result());
}

void FPacketHandler::Handle_SC_TRADE_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_RESULT_PACKET& pkt)
{
	if (UJMTradeManager* Trade = GetTradeManager(session)) Trade->HandleResult(pkt.request(), pkt.result());
}

void FPacketHandler::Handle_SC_CHAT(TSharedPtr<NetworkSession>& session, Protocol::SC_CHAT_PACKET& pkt)
{
	UNetworkManager* NetManager = session ? session->OwnerNetwork.Get() : nullptr;
	UGameInstance* GameInstance = NetManager ? NetManager->GetGameInstance() : nullptr;
	UJMUIManager* UIManager = GameInstance ? GameInstance->GetSubsystem<UJMUIManager>() : nullptr;
	if (UChatWidget* ChatWidget = UIManager ? Cast<UChatWidget>(UIManager->GetWidget(WidgetNames::Chatting)) : nullptr)
	{
		ChatWidget->AddChatMessage(UTF8_TO_TCHAR(pkt.sender_name().c_str()), UTF8_TO_TCHAR(pkt.message().c_str()));
	}
}
