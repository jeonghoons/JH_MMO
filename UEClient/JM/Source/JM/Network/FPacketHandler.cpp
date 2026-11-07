#include "FPacketHandler.h"
#include "NetworkManager.h"
#include "SendBuffer.h"
#include "Network/NetworkSession.h"
#include "Game/JMObjectManager.h"
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
		default:									return FText::FromString(TEXT("장비를 바꾸지 못했습니다."));
		}
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
		NetManager->EnterGame();
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
		ObjectManager->HandleDead(pkt.object_id());
}

void FPacketHandler::Handle_SC_CHANGE_EQUIP(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_EQUIP_PACKET& pkt)
{
	if (UJMObjectManager* ObjectManager = GetObjectManager(session))
		ObjectManager->HandleChangeEquip(pkt.object_id(), pkt.part(), pkt.item_id(), pkt.stat());
}

void FPacketHandler::Handle_SC_CHANGE_EQUIP_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_EQUIP_RESULT_PACKET& pkt)
{
	// 성공은 SC_CHANGE_EQUIP 방송으로 오므로 이 패킷은 거부일 때만 온다.
	UE_LOG(LogTemp, Log, TEXT("ChangeEquip rejected: result=%d part=%d item=%d"), static_cast<int32>(pkt.result()), static_cast<int32>(pkt.part()), pkt.item_id());
	ShowSystemMessage(session, EquipResultText(pkt.result()));
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
