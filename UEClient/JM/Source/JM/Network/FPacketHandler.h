#pragma once
#include "CoreMinimal.h"
#include "Protocol/Protocol.pb.h"

class NetworkSession;
class UJMObjectManager;

using PacketHandlerFunc = std::function<bool(TSharedPtr<NetworkSession>, BYTE*, int)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX + 1]; // PacketId가 uint16 전체 범위이므로 +1

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

    // 패킷 타입은 핸들러 시그니처에서 추론한다.
    template<typename T>
    static void RegisterHandler(uint16 PacketId, void(*Func)(TSharedPtr<NetworkSession>&, T&))
    {
        GPacketHandler[PacketId] = [Func](TSharedPtr<NetworkSession> session, BYTE* buffer, int len) {
            return ParseAndHandle<T>(Func, session, buffer, len);
            };
    }

    // 세션 → NetworkManager → GameInstance → ObjectManager. 중간에 없으면 nullptr.
    static UJMObjectManager* GetObjectManager(const TSharedPtr<NetworkSession>& session);

    static void Handle_SC_LOGIN(TSharedPtr<NetworkSession>& session, Protocol::SC_LOGIN_INFO_PACKET& pkt);
    static void Handle_SC_SIGNUP(TSharedPtr<NetworkSession>& session, Protocol::SC_SIGNUP_PACKET& pkt);
    static void Handle_SC_ADD_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_ADD_OBJECT_PACKET& pkt);
    static void Handle_SC_REMOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_REMOVE_OBJECT_PACKET& pkt);
    static void Handle_SC_MOVE_OBJECT(TSharedPtr<NetworkSession>& session, Protocol::SC_MOVE_PACKET& pkt);
    static void Handle_SC_ATTACK(TSharedPtr<NetworkSession>& session, Protocol::SC_ATTACK_PACKET& pkt);
    static void Handle_SC_DAMAGE(TSharedPtr<NetworkSession>& session, Protocol::SC_DAMAGE_PACKET& pkt);
    static void Handle_SC_DEAD(TSharedPtr<NetworkSession>& session, Protocol::SC_DEAD_PACKET& pkt);
    static void Handle_SC_CHAT(TSharedPtr<NetworkSession>& session, Protocol::SC_CHAT_PACKET& pkt);
};

