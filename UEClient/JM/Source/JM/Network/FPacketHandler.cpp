#include "FPacketHandler.h"
#include "NetworkManager.h"
#include "SendBuffer.h"
#include "Network/NetworkSession.h"
#include "Game/JMObjectManager.h"

PacketHandlerFunc GPacketHandler[UINT16_MAX + 1];

void FPacketHandler::Init()
{

	GPacketHandler[Protocol::SC_LOGIN] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_LOGIN_INFO_PACKET>(&FPacketHandler::Handle_SC_LOGIN, session, buffer, len);
		};

	GPacketHandler[Protocol::SC_SIGNUP] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_SIGNUP_PACKET>(&FPacketHandler::Handle_SC_SIGNUP, session, buffer, len);
		};

	GPacketHandler[Protocol::SC_ADD_OBJECT] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_ADD_OBJECT_PACKET>(&FPacketHandler::Handle_SC_ADD_OBJECT, session, buffer, len);
		};

	GPacketHandler[Protocol::SC_REMOVE_OBJECT] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_REMOVE_OBJECT_PACKET>(&FPacketHandler::Handle_SC_REMOVE_OBJECT, session, buffer, len);
		};

	GPacketHandler[Protocol::SC_MOVE_OBJECT] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_MOVE_PACKET>(&FPacketHandler::Handle_SC_MOVE_OBJECT, session, buffer, len);
		};

	GPacketHandler[Protocol::SC_ATTACK] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_ATTACK_PACKET>(&FPacketHandler::Handle_SC_ATTACK, session, buffer, len);
		};

	GPacketHandler[Protocol::SC_DAMAGE] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_DAMAGE_PACKET>(&FPacketHandler::Handle_SC_DAMAGE, session, buffer, len);
		};

	GPacketHandler[Protocol::SC_DEAD] = [](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
		return ParseAndHandle<Protocol::SC_DEAD_PACKET>(&FPacketHandler::Handle_SC_DEAD, session, buffer, len);
		};
   
}


void FPacketHandler::ProcessPacket(const TSharedPtr<NetworkSession>& session, BYTE* buffer, int len)
{
	if (len < sizeof(FPacketHeader)) return;

    FPacketHeader* header = reinterpret_cast<FPacketHeader*>(buffer);
	// 헤더에 적힌 크기가 실제로 받은 길이와 다르거나 헤더보다 작으면 버린다.
	if (header->PacketSize < sizeof(FPacketHeader) || header->PacketSize != len) return;

    BYTE* payloadBuffer = buffer + sizeof(FPacketHeader);
    int32_t payloadLen = header->PacketSize - sizeof(FPacketHeader);

    auto& func = GPacketHandler[header->PacketId];
    if (func)
    {
        func(session, payloadBuffer, payloadLen);
    }
}

void FPacketHandler::Handle_SC_LOGIN(TSharedPtr<NetworkSession>& session, Protocol::SC_LOGIN_INFO_PACKET& pkt)
{
    if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
    {
        NetManager->NetworkId = pkt.object_info().id();
		NetManager->EnterGame();
    }
}

void FPacketHandler::Handle_SC_SIGNUP(TSharedPtr<NetworkSession>& session, Protocol::SC_SIGNUP_PACKET& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("SignUp Result: %d"), pkt.result());
}

void FPacketHandler::Handle_SC_ADD_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_ADD_OBJECT_PACKET& pkt)
{
	if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
	{
		if (UJMObjectManager* ObjectManager = NetManager->GetGameInstance()->GetSubsystem<UJMObjectManager>())
		{
			ObjectManager->HandleSpawn(pkt.object_info());
		}
	}
}

void FPacketHandler::Handle_SC_REMOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_REMOVE_OBJECT_PACKET& pkt)
{
	if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
	{
		if (UJMObjectManager* ObjectManager = NetManager->GetGameInstance()->GetSubsystem<UJMObjectManager>())
		{
			ObjectManager->HandleDespawn(pkt.object_id());
		}
	}
}

void FPacketHandler::Handle_SC_MOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_MOVE_PACKET& pkt)
{
	if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
	{
		if (UJMObjectManager* ObjectManager = NetManager->GetGameInstance()->GetSubsystem<UJMObjectManager>())
		{
			ObjectManager->HandleMove(pkt.object_info());
		}
	}
}

void FPacketHandler::Handle_SC_ATTACK(TSharedPtr<NetworkSession>& session, Protocol::SC_ATTACK_PACKET& pkt)
{
	if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
	{
		if (UJMObjectManager* ObjectManager = NetManager->GetGameInstance()->GetSubsystem<UJMObjectManager>())
		{
			ObjectManager->HandleAttack(pkt.attacker_id(), pkt.skill_id(), pkt.aim_yaw(), pkt.aim_x(), pkt.aim_y());
		}
	}
}

void FPacketHandler::Handle_SC_DAMAGE(TSharedPtr<NetworkSession>& session, Protocol::SC_DAMAGE_PACKET& pkt)
{
	if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
	{
		if (UJMObjectManager* ObjectManager = NetManager->GetGameInstance()->GetSubsystem<UJMObjectManager>())
		{
			ObjectManager->HandleDamage(pkt.attacker_id(), pkt.target_id(), pkt.damage(), pkt.remain_hp());
		}
	}
}

void FPacketHandler::Handle_SC_DEAD(TSharedPtr<NetworkSession>& session, Protocol::SC_DEAD_PACKET& pkt)
{
	if (UNetworkManager* NetManager = session->OwnerNetwork.Get())
	{
		if (UJMObjectManager* ObjectManager = NetManager->GetGameInstance()->GetSubsystem<UJMObjectManager>())
		{
			ObjectManager->HandleDead(pkt.object_id());
		}
	}
}
