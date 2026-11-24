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
    static void Handle_SC_MOVE_BATCH(TSharedPtr<NetworkSession>& session, Protocol::SC_MOVE_BATCH_PACKET& pkt);
    static void Handle_SC_ATTACK(TSharedPtr<NetworkSession>& session, Protocol::SC_ATTACK_PACKET& pkt);
    static void Handle_SC_ATTACK_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_ATTACK_RESULT_PACKET& pkt);
    static void Handle_SC_DAMAGE(TSharedPtr<NetworkSession>& session, Protocol::SC_DAMAGE_PACKET& pkt);
    static void Handle_SC_DEAD(TSharedPtr<NetworkSession>& session, Protocol::SC_DEAD_PACKET& pkt);
    static void Handle_SC_CHAT(TSharedPtr<NetworkSession>& session, Protocol::SC_CHAT_PACKET& pkt);
    static void Handle_SC_CHANGE_EQUIP(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_EQUIP_PACKET& pkt);
    static void Handle_SC_CHANGE_EQUIP_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_EQUIP_RESULT_PACKET& pkt);
    static void Handle_SC_SKILL_AIM(TSharedPtr<NetworkSession>& session, Protocol::SC_SKILL_AIM_PACKET& pkt);
    static void Handle_SC_REVIVE(TSharedPtr<NetworkSession>& session, Protocol::SC_REVIVE_PACKET& pkt);
    static void Handle_SC_KICK(TSharedPtr<NetworkSession>& session, Protocol::SC_KICK_PACKET& pkt);
    static void Handle_SC_PONG(TSharedPtr<NetworkSession>& session, Protocol::SC_PONG_PACKET& pkt);
    static void Handle_SC_INVENTORY(TSharedPtr<NetworkSession>& session, Protocol::SC_INVENTORY_PACKET& pkt);
    static void Handle_SC_INVENTORY_UPDATE(TSharedPtr<NetworkSession>& session, Protocol::SC_INVENTORY_UPDATE_PACKET& pkt);
    static void Handle_SC_ITEM_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_ITEM_RESULT_PACKET& pkt);
    static void Handle_SC_SHOP_OPEN(TSharedPtr<NetworkSession>& session, Protocol::SC_SHOP_OPEN_PACKET& pkt);
    static void Handle_SC_TELEPORT(TSharedPtr<NetworkSession>& session, Protocol::SC_TELEPORT_PACKET& pkt);
    static void Handle_SC_PORTAL_LIST(TSharedPtr<NetworkSession>& session, Protocol::SC_PORTAL_LIST_PACKET& pkt);
    static void Handle_SC_ROOM_CHANGE(TSharedPtr<NetworkSession>& session, Protocol::SC_ROOM_CHANGE_PACKET& pkt);
    static void Handle_SC_MOVE_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_MOVE_RESULT_PACKET& pkt);
    static void Handle_SC_DUNGEON_STATE(TSharedPtr<NetworkSession>& session, Protocol::SC_DUNGEON_STATE_PACKET& pkt);
    static void Handle_SC_USE_ITEM(TSharedPtr<NetworkSession>& session, Protocol::SC_USE_ITEM_PACKET& pkt);
    static void Handle_SC_CHANGE_HP(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_HP_PACKET& pkt);
    // 성장·MP(A2.3b, D-254)
    static void Handle_SC_GAIN_EXP(TSharedPtr<NetworkSession>& session, Protocol::SC_GAIN_EXP_PACKET& pkt);
    static void Handle_SC_LEVEL_UP(TSharedPtr<NetworkSession>& session, Protocol::SC_LEVEL_UP_PACKET& pkt);
    static void Handle_SC_CHANGE_MP(TSharedPtr<NetworkSession>& session, Protocol::SC_CHANGE_MP_PACKET& pkt);
    static void Handle_SC_TRADE_REQUEST(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_REQUEST_PACKET& pkt);
    static void Handle_SC_TRADE_STATE(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_STATE_PACKET& pkt);
    static void Handle_SC_TRADE_END(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_END_PACKET& pkt);
    static void Handle_SC_TRADE_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_TRADE_RESULT_PACKET& pkt);
    // 파티(DG2): 상태·안내는 UJMPartyManager가 맡는다
    static void Handle_SC_PARTY_INVITE(TSharedPtr<NetworkSession>& session, Protocol::SC_PARTY_INVITE_PACKET& pkt);
    static void Handle_SC_PARTY_STATE(TSharedPtr<NetworkSession>& session, Protocol::SC_PARTY_STATE_PACKET& pkt);
    static void Handle_SC_PARTY_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_PARTY_RESULT_PACKET& pkt);
    // 경매장(A6.3b): 상태·안내는 UJMAuctionManager가 맡는다
    static void Handle_SC_AUCTION_LIST(TSharedPtr<NetworkSession>& session, Protocol::SC_AUCTION_LIST_PACKET& pkt);
    static void Handle_SC_AUCTION_MY(TSharedPtr<NetworkSession>& session, Protocol::SC_AUCTION_MY_PACKET& pkt);
    static void Handle_SC_AUCTION_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_AUCTION_RESULT_PACKET& pkt);
    // 퀘스트·수령함 알림(Q1, D-259·D-261·D-262): 지금은 받아서 로그만 남긴다(창·이름표·DT_QuestTable은 다음 항목)
    static void Handle_SC_QUEST_LIST(TSharedPtr<NetworkSession>& session, Protocol::SC_QUEST_LIST_PACKET& pkt);
    static void Handle_SC_QUEST_UPDATE(TSharedPtr<NetworkSession>& session, Protocol::SC_QUEST_UPDATE_PACKET& pkt);
    static void Handle_SC_QUEST_RESULT(TSharedPtr<NetworkSession>& session, Protocol::SC_QUEST_RESULT_PACKET& pkt);
    static void Handle_SC_MAIL_NOTICE(TSharedPtr<NetworkSession>& session, Protocol::SC_MAIL_NOTICE_PACKET& pkt);
};

