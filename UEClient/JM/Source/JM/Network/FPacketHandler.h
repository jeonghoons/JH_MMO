#pragma once
#include "CoreMinimal.h"
#include "Protocol/Protocol.pb.h"

class NetworkSession;

using PacketHandlerFunc = std::function<bool(TSharedPtr<NetworkSession>, BYTE*, int)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];

class FPacketHandler
{
public:
    static void Init();
	static void ProcessPacket(const TSharedPtr<NetworkSession>& session, BYTE* buffer, int len);

private:
    template<typename T, typename ProcessFunc>
    static bool ParseAndHandle(ProcessFunc func, TSharedPtr<NetworkSession> session, BYTE* buffer, int len)
    {
        T pkt;
        if (pkt.ParseFromArray(buffer, len) == false)
        {
            return false;
        }

        func(session, pkt);
        return true;
    }

    static void Handle_SC_LOGIN(TSharedPtr<NetworkSession>& session, Protocol::SC_LOGIN_INFO_PACKET& pkt);
    static void Handle_SC_SIGNUP(TSharedPtr<NetworkSession>& session, Protocol::SC_SIGNUP_PACKET& pkt);
    static void Handle_SC_ADD_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_ADD_OBJECT_PACKET& pkt);
    static void Handle_SC_REMOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_REMOVE_OBJECT_PACKET& pkt);
    static void Handle_SC_MOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_MOVE_PACKET& pkt);
    static void Handle_SC_ATTACK(TSharedPtr<NetworkSession>& session, Protocol::SC_ATTACK_PACKET& pkt);
    static void Handle_SC_DAMAGE(TSharedPtr<NetworkSession>& session, Protocol::SC_DAMAGE_PACKET& pkt);
    static void Handle_SC_DEAD(TSharedPtr<NetworkSession>& session, Protocol::SC_DEAD_PACKET& pkt);
};

