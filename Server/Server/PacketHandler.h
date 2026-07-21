#pragma once
#include "Protocol/Protocol.pb.h"
class Session;

//class PacketHandler
//{
//public:
//	static void ProcessPacket(shared_ptr<Session> session, BYTE* buffer, int len);
//	
//private:
//	static void Handle_CS_LOGIN(shared_ptr<Session> session, CS_LOGIN_PACKET* packet);
//	static void Handle_CS_SIGNUP(shared_ptr<Session> session, CS_SIGNUP_PACKET* packet);
//	static void Handle_CS_CHAT(shared_ptr<Session> session, CS_CHAT_PACKET* packet);
//	static void Handle_CS_MOVE(shared_ptr<Session> session, CS_MOVE_PACKET* packet);
//	static void Handle_CS_ENTER_ROOM(shared_ptr<Session> session, CS_ENTER_ROOM_PACKET* packet);
//	static void Handle_CS_ATTACK(shared_ptr<Session> session, CS_ATTACK_PACKET* packet);
//
//};


using PacketHandlerFunc = std::function<bool(shared_ptr<Session>, BYTE*, int)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];

class PacketHandler
{
public:
    static void Init();
    static void ProcessPacket(shared_ptr<Session> session, BYTE* buffer, int len);

private:

    template<typename T, typename ProcessFunc>
    static bool ParseAndHandle(ProcessFunc func, shared_ptr<Session> session, BYTE* buffer, int len)
    {
        T pkt;
        if (pkt.ParseFromArray(buffer, len) == false)
        {
            return false;
        }

        func(session, pkt);
        return true;
    }

    static void Handle_CS_LOGIN(shared_ptr<Session> session, Protocol::CS_LOGIN_PACKET& pkt);
    static void Handle_CS_SIGNUP(shared_ptr<Session> session, Protocol::CS_SIGNUP_PACKET& pkt);
    static void Handle_CS_CHAT(shared_ptr<Session> session, Protocol::CS_CHAT_PACKET& pkt);
    static void Handle_CS_ENTER_ROOM(shared_ptr<Session> session, Protocol::CS_ENTER_ROOM_PACKET& pkt);
    static void Handle_CS_MOVE(shared_ptr<Session> session, Protocol::CS_MOVE_PACKET& pkt);
    static void Handle_CS_ATTACK(shared_ptr<Session> session, Protocol::CS_ATTACK_PACKET& pkt);
};

