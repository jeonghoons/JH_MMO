#pragma once
#include "Protocol/Protocol.pb.h"
class GameSession;

using PacketHandlerFunc = std::function<bool(shared_ptr<GameSession>, BYTE*, int)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX + 1]; // id가 uint16_t 전체 범위이므로 +1

class PacketHandler
{
public:
    static void Init();
    static void ProcessPacket(shared_ptr<GameSession> session, BYTE* buffer, int len);

private:

    template<typename T, typename ProcessFunc>
    static bool ParseAndHandle(ProcessFunc func, shared_ptr<GameSession> session, BYTE* buffer, int len)
    {
        T pkt;
        if (pkt.ParseFromArray(buffer, len) == false)
        {
            return false;
        }

        func(session, pkt);
        return true;
    }

    static void Handle_CS_LOGIN(shared_ptr<GameSession> session, Protocol::CS_LOGIN_PACKET& pkt);
    static void Handle_CS_SIGNUP(shared_ptr<GameSession> session, Protocol::CS_SIGNUP_PACKET& pkt);
    static void Handle_CS_CHAT(shared_ptr<GameSession> session, Protocol::CS_CHAT_PACKET& pkt);
    // 채팅 내용 검증: 제어 문자 제거, 앞뒤 공백 제거, MAX_CHAT_LEN 글자로 자르기. 남는 내용이 없으면 빈 문자열
    static wstring SanitizeChat(const string& utf8Msg);
    static void Handle_CS_ENTER_ROOM(shared_ptr<GameSession> session, Protocol::CS_ENTER_ROOM_PACKET& pkt);
    static void Handle_CS_MOVE(shared_ptr<GameSession> session, Protocol::CS_MOVE_PACKET& pkt);
    static void Handle_CS_ATTACK(shared_ptr<GameSession> session, Protocol::CS_ATTACK_PACKET& pkt);
    static void Handle_CS_PING(shared_ptr<GameSession> session, Protocol::CS_PING_PACKET& pkt);
    static void Handle_CS_CHANGE_EQUIP(shared_ptr<GameSession> session, Protocol::CS_CHANGE_EQUIP_PACKET& pkt);
    static void Handle_CS_SKILL_AIM(shared_ptr<GameSession> session, Protocol::CS_SKILL_AIM_PACKET& pkt);
    static void Handle_CS_REVIVE(shared_ptr<GameSession> session, Protocol::CS_REVIVE_PACKET& pkt);
    static void Handle_CS_DEV_GIVE(shared_ptr<GameSession> session, Protocol::CS_DEV_GIVE_PACKET& pkt);
    static void Handle_CS_MOVE_ITEM(shared_ptr<GameSession> session, Protocol::CS_MOVE_ITEM_PACKET& pkt);
    static void Handle_CS_PICKUP_ITEM(shared_ptr<GameSession> session, Protocol::CS_PICKUP_ITEM_PACKET& pkt);
    static void Handle_CS_DROP_ITEM(shared_ptr<GameSession> session, Protocol::CS_DROP_ITEM_PACKET& pkt);
    static void Handle_CS_USE_ITEM(shared_ptr<GameSession> session, Protocol::CS_USE_ITEM_PACKET& pkt);
    static void Handle_CS_SET_QUICKSLOT(shared_ptr<GameSession> session, Protocol::CS_SET_QUICKSLOT_PACKET& pkt);
    // 1:1 거래(A4.2): 검증과 상태 전이는 모두 Room 잡에서 한다
    static void Handle_CS_TRADE_REQUEST(shared_ptr<GameSession> session, Protocol::CS_TRADE_REQUEST_PACKET& pkt);
    static void Handle_CS_TRADE_RESPOND(shared_ptr<GameSession> session, Protocol::CS_TRADE_RESPOND_PACKET& pkt);
    static void Handle_CS_TRADE_SET_OFFER(shared_ptr<GameSession> session, Protocol::CS_TRADE_SET_OFFER_PACKET& pkt);
    static void Handle_CS_TRADE_LOCK(shared_ptr<GameSession> session, Protocol::CS_TRADE_LOCK_PACKET& pkt);
    static void Handle_CS_TRADE_CONFIRM(shared_ptr<GameSession> session, Protocol::CS_TRADE_CONFIRM_PACKET& pkt);
    static void Handle_CS_TRADE_CANCEL(shared_ptr<GameSession> session, Protocol::CS_TRADE_CANCEL_PACKET& pkt);
};

