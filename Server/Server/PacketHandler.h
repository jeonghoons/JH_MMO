#pragma once
#include "Protocol/Protocol.pb.h"
class Session;

using PacketHandlerFunc = std::function<bool(shared_ptr<Session>, BYTE*, int)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX + 1]; // id가 uint16_t 전체 범위이므로 +1

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
    // 채팅 내용 검증: 제어 문자 제거, 앞뒤 공백 제거, MAX_CHAT_LEN 글자로 자르기. 남는 내용이 없으면 빈 문자열
    static wstring SanitizeChat(const string& utf8Msg);
    static void Handle_CS_ENTER_ROOM(shared_ptr<Session> session, Protocol::CS_ENTER_ROOM_PACKET& pkt);
    static void Handle_CS_MOVE(shared_ptr<Session> session, Protocol::CS_MOVE_PACKET& pkt);
    static void Handle_CS_ATTACK(shared_ptr<Session> session, Protocol::CS_ATTACK_PACKET& pkt);
    static void Handle_CS_PING(shared_ptr<Session> session, Protocol::CS_PING_PACKET& pkt);
};

