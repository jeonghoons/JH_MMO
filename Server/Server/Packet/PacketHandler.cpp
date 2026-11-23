#include "pch.h"
#include "Packet/PacketHandler.h"
#include "Network/SendBuffer.h"
#include "Game/Object/Player.h"
#include "DB/DatabaseWorker.h"
#include "Data/ServerData.h"
#include "Utils/Metrics.h"

extern shared_ptr<DatabaseWorker> GDBWorker;

PacketHandlerFunc GPacketHandler[UINT16_MAX + 1];

void PacketHandler::Init()
{
    
    GPacketHandler[Protocol::CS_LOGIN] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_LOGIN_PACKET>(&PacketHandler::Handle_CS_LOGIN, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_SIGNUP] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_SIGNUP_PACKET>(&PacketHandler::Handle_CS_SIGNUP, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_CHAT] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_CHAT_PACKET>(&PacketHandler::Handle_CS_CHAT, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_ENTER_ROOM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_ENTER_ROOM_PACKET>(&PacketHandler::Handle_CS_ENTER_ROOM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_MOVE] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_MOVE_PACKET>(&PacketHandler::Handle_CS_MOVE, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_ATTACK] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_ATTACK_PACKET>(&PacketHandler::Handle_CS_ATTACK, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_PING] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_PING_PACKET>(&PacketHandler::Handle_CS_PING, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_CHANGE_EQUIP] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_CHANGE_EQUIP_PACKET>(&PacketHandler::Handle_CS_CHANGE_EQUIP, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_SKILL_AIM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_SKILL_AIM_PACKET>(&PacketHandler::Handle_CS_SKILL_AIM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_REVIVE] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_REVIVE_PACKET>(&PacketHandler::Handle_CS_REVIVE, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_DEV_GIVE] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_DEV_GIVE_PACKET>(&PacketHandler::Handle_CS_DEV_GIVE, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_MOVE_ITEM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_MOVE_ITEM_PACKET>(&PacketHandler::Handle_CS_MOVE_ITEM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_PICKUP_ITEM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_PICKUP_ITEM_PACKET>(&PacketHandler::Handle_CS_PICKUP_ITEM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_SHOP_OPEN] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_SHOP_OPEN_PACKET>(&PacketHandler::Handle_CS_SHOP_OPEN, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_SHOP_BUY] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_SHOP_BUY_PACKET>(&PacketHandler::Handle_CS_SHOP_BUY, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_SHOP_SELL] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_SHOP_SELL_PACKET>(&PacketHandler::Handle_CS_SHOP_SELL, session, buffer, len);
        };
    // 퀘스트 (Q1)
    GPacketHandler[Protocol::CS_QUEST_REQUEST] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_QUEST_REQUEST_PACKET>(&PacketHandler::Handle_CS_QUEST_REQUEST, session, buffer, len);
        };
    // 경매장 (A4.3)
    GPacketHandler[Protocol::CS_AUCTION_SEARCH] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_AUCTION_SEARCH_PACKET>(&PacketHandler::Handle_CS_AUCTION_SEARCH, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_AUCTION_REGISTER] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_AUCTION_REGISTER_PACKET>(&PacketHandler::Handle_CS_AUCTION_REGISTER, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_AUCTION_BUY] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_AUCTION_BUY_PACKET>(&PacketHandler::Handle_CS_AUCTION_BUY, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_AUCTION_CANCEL] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_AUCTION_CANCEL_PACKET>(&PacketHandler::Handle_CS_AUCTION_CANCEL, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_AUCTION_MY] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_AUCTION_MY_PACKET>(&PacketHandler::Handle_CS_AUCTION_MY, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_MAIL_CLAIM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_MAIL_CLAIM_PACKET>(&PacketHandler::Handle_CS_MAIL_CLAIM, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_PORTAL] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_PORTAL_PACKET>(&PacketHandler::Handle_CS_PORTAL, session, buffer, len);
        };
    GPacketHandler[Protocol::CS_DROP_ITEM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_DROP_ITEM_PACKET>(&PacketHandler::Handle_CS_DROP_ITEM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_USE_ITEM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_USE_ITEM_PACKET>(&PacketHandler::Handle_CS_USE_ITEM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_SET_QUICKSLOT] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_SET_QUICKSLOT_PACKET>(&PacketHandler::Handle_CS_SET_QUICKSLOT, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_TRADE_REQUEST] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_TRADE_REQUEST_PACKET>(&PacketHandler::Handle_CS_TRADE_REQUEST, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_TRADE_RESPOND] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_TRADE_RESPOND_PACKET>(&PacketHandler::Handle_CS_TRADE_RESPOND, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_TRADE_SET_OFFER] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_TRADE_SET_OFFER_PACKET>(&PacketHandler::Handle_CS_TRADE_SET_OFFER, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_TRADE_LOCK] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_TRADE_LOCK_PACKET>(&PacketHandler::Handle_CS_TRADE_LOCK, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_TRADE_CONFIRM] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_TRADE_CONFIRM_PACKET>(&PacketHandler::Handle_CS_TRADE_CONFIRM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_TRADE_CANCEL] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_TRADE_CANCEL_PACKET>(&PacketHandler::Handle_CS_TRADE_CANCEL, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_PARTY_INVITE] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_PARTY_INVITE_PACKET>(&PacketHandler::Handle_CS_PARTY_INVITE, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_PARTY_RESPOND] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_PARTY_RESPOND_PACKET>(&PacketHandler::Handle_CS_PARTY_RESPOND, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_PARTY_LEAVE] = [](shared_ptr<GameSession> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_PARTY_LEAVE_PACKET>(&PacketHandler::Handle_CS_PARTY_LEAVE, session, buffer, len);
        };
}

void PacketHandler::ProcessPacket(shared_ptr<GameSession> session, BYTE* buffer, int len)
{
    PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
    BYTE* payloadBuffer = buffer + sizeof(PacketHeader);
    int payloadLen = header->size - sizeof(PacketHeader);

    
    auto& func = GPacketHandler[header->id]; 
    if (func)
    {
        // 파싱 실패는 정상 클라이언트에서는 나오지 않는 패킷이므로 연결을 끊는다.
        if (func(session, payloadBuffer, payloadLen) == false)
            session->Disconnect(L"Packet Parse Fail");
    }
    else
    {
        LOG_WARN("Packet", "Invalid Packet Id : {}", header->id);
    }
}




void PacketHandler::Handle_CS_LOGIN(shared_ptr<GameSession> session, Protocol::CS_LOGIN_PACKET& pkt)
{
    // C++ string 매핑 활용
    string id = pkt.account_id();
    string pw = pkt.account_pw();

    if (pkt.is_dummy()) {
        id = "dummy";
        pw = "dummy";
    }

    // 같은 연결로 다시 로그인하면 이전 캐릭터가 방에 남으므로 거부한다. (DB 응답 전에 연달아 온 요청은 AuthLobby::OnLoginSuccess에서 거른다)
    if (session->GetPlayer() != nullptr)
    {
        GLobby->PushJob(&AuthLobby::OnLoginFailed, session, Protocol::LOGIN_RESULT_ALREADY_LOGGED_IN);
        return;
    }

    GDBWorker->PushDBJob(&DatabaseWorker::TryLogin, session, id, pw);
}

void PacketHandler::Handle_CS_SIGNUP(shared_ptr<GameSession> session, Protocol::CS_SIGNUP_PACKET& pkt)
{
    string id = pkt.account_id();
    string pw = pkt.account_pw();
    int type = static_cast<int>(pkt.player_type());

    // 플레이어 체형은 MALE/FEMALE만 고를 수 있다(옛 몬스터·영웅 값 3~5와 그 밖의 값은 거부). 스탯은 체형 공용(PlayerStatTable)이다.
    if (type != Protocol::PLAYER_TYPE_MALE && type != Protocol::PLAYER_TYPE_FEMALE)
    {
        LOG_WARN("Packet", "Invalid PlayerType on signup : {}", type);
        GLobby->PushJob(&AuthLobby::OnSignUpResult, session, false);
        return;
    }
    // 이미 로그인한 연결에서는 가입하지 않는다.
    if (session->GetPlayer() != nullptr)
    {
        GLobby->PushJob(&AuthLobby::OnSignUpResult, session, false);
        return;
    }

    GDBWorker->PushDBJob(&DatabaseWorker::TrySignUP, session, id, pw, type);
}

void PacketHandler::Handle_CS_CHAT(shared_ptr<GameSession> session, Protocol::CS_CHAT_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    wstring chatMsg = SanitizeChat(pkt.message());
    if (chatMsg.empty()) return;

    // 도배 검사와 방송은 Room 잡에서 한다(플레이어 상태는 Room 스레드에서만 바꾼다).
    room->PushJob(&Room::PlayerChat, player, chatMsg);
}

wstring PacketHandler::SanitizeChat(const string& utf8Msg)
{
    // 잘못된 UTF-8은 변환에서 U+FFFD로 바뀐다.
    wstring msg = Utils::UTF8ToWString(utf8Msg);

    // 제어 문자(줄바꿈·탭 포함)는 지운다. 채팅은 한 줄이다.
    msg.erase(remove_if(msg.begin(), msg.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; }), msg.end());

    // 앞뒤 공백 제거
    const size_t first = msg.find_first_not_of(L' ');
    if (first == wstring::npos) return wstring();
    msg = msg.substr(first, msg.find_last_not_of(L' ') - first + 1);

    // 최대 길이로 자른다. 서로게이트 쌍(이모지 등)의 앞쪽만 남지 않게 한다.
    if (msg.length() > MAX_CHAT_LEN)
    {
        size_t len = MAX_CHAT_LEN;
        if (IS_HIGH_SURROGATE(msg[len - 1])) --len;
        msg.resize(len);
    }
    return msg;
}

void PacketHandler::Handle_CS_ENTER_ROOM(shared_ptr<GameSession> session, Protocol::CS_ENTER_ROOM_PACKET& pkt)
{
    // 로그인 전(DB 응답 전 포함)이거나 이미 방에 배정된 플레이어의 입장 요청은 무시한다.
    // 방 배정은 서비스 잡(WorldService::EnterWorld)에서 하므로 연달아 온 두 번째 요청은 여기를 지날 수 있고, 서비스가 한 번 더 거른다.
    // 이미 방이 있는 플레이어의 요청은 "다른 레벨로 옮겨 와 그 레벨을 다 열었다"는 신호다(DG3c) — 그 방의 로딩 대기 목록에 있을 때만 쓰이고 그 밖은 버려진다.
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;
    if (shared_ptr<Room> room = player->GetCurrentRoom())
    {
        room->PushJob(&Room::PlayerLoadDone, player);
        return;
    }

    GWorld->PushJob(&WorldService::EnterWorld, player);
}

void PacketHandler::Handle_CS_MOVE(shared_ptr<GameSession> session, Protocol::CS_MOVE_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;
    
    room->PushJob(&Room::PlayerMove, player, pkt.pos_info(), pkt.correction_seq());
}

void PacketHandler::Handle_CS_PING(shared_ptr<GameSession> session, Protocol::CS_PING_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // Room의 JobQueue를 거쳐 응답해서 Room 처리 지연이 왕복 시간에 포함되게 한다.
    room->PushJob(&Room::PlayerPing, player, pkt.client_time());
}

void PacketHandler::Handle_CS_CHANGE_EQUIP(shared_ptr<GameSession> session, Protocol::CS_CHANGE_EQUIP_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 슬롯·보유·상태·빈도 검증과 결과 응답은 Room 잡에서 한다(플레이어 상태는 Room 스레드에서만 바꾼다).
    room->PushJob(&Room::PlayerChangeEquip, player, pkt.part(), static_cast<int64_t>(pkt.item_db_id()));
}

void PacketHandler::Handle_CS_DEV_GIVE(shared_ptr<GameSession> session, Protocol::CS_DEV_GIVE_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // [Debug] DevCommands 여부·수량·빈 칸 검증과 DB 트랜잭션은 Room 잡에서 한다.
    room->PushJob(&Room::PlayerDevGive, player, static_cast<int>(pkt.item_id()), static_cast<int64_t>(pkt.count()), static_cast<int64_t>(pkt.exp()));
}

void PacketHandler::Handle_CS_MOVE_ITEM(shared_ptr<GameSession> session, Protocol::CS_MOVE_ITEM_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 보유·칸 범위 검증과 DB 트랜잭션은 Room 잡에서 한다.
    room->PushJob(&Room::PlayerMoveItem, player, static_cast<int64_t>(pkt.item_db_id()), static_cast<int>(pkt.to_slot()));
}

void PacketHandler::Handle_CS_PICKUP_ITEM(shared_ptr<GameSession> session, Protocol::CS_PICKUP_ITEM_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 드랍 존재·거리·소유권·빈 칸 검증과 DB 트랜잭션은 Room 잡에서 한다.
    room->PushJob(&Room::PlayerPickupItem, player, static_cast<int>(pkt.object_id()));
}

void PacketHandler::Handle_CS_DROP_ITEM(shared_ptr<GameSession> session, Protocol::CS_DROP_ITEM_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 보유 검증과 DB 트랜잭션은 Room 잡에서 한다.
    room->PushJob(&Room::PlayerDropItem, player, static_cast<int64_t>(pkt.item_db_id()));
}

// 상점 NPC(L4, D-201·D-202). NPC 존재·거리·거래 중·골드·빈 칸 검증과 DB 트랜잭션은 Room 잡에서 한다.
void PacketHandler::Handle_CS_SHOP_OPEN(shared_ptr<GameSession> session, Protocol::CS_SHOP_OPEN_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerShopOpen, player, static_cast<int>(pkt.npc_id()));
}

void PacketHandler::Handle_CS_SHOP_BUY(shared_ptr<GameSession> session, Protocol::CS_SHOP_BUY_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerShopBuy, player, static_cast<int>(pkt.npc_id()), static_cast<int>(pkt.item_id()), static_cast<int>(pkt.count()));
}

void PacketHandler::Handle_CS_SHOP_SELL(shared_ptr<GameSession> session, Protocol::CS_SHOP_SELL_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerShopSell, player, static_cast<int>(pkt.npc_id()), static_cast<int64_t>(pkt.item_db_id()), static_cast<int>(pkt.count()));
}

// 퀘스트(Q1, D-259·D-261·D-264). 요청 하나 — 수락·완료 중 무엇을 할지, NPC 존재·거리·거래 중·퀘스트 상태·보상 칸 검증과 DB 트랜잭션은 Room 잡에서 정한다.
void PacketHandler::Handle_CS_QUEST_REQUEST(shared_ptr<GameSession> session, Protocol::CS_QUEST_REQUEST_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerQuestRequest, player, static_cast<int>(pkt.npc_id()), static_cast<int>(pkt.quest_id()));
}

void PacketHandler::Handle_CS_USE_ITEM(shared_ptr<GameSession> session, Protocol::CS_USE_ITEM_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 보유·사용 효과·HP·재사용 시간 검증과 DB 트랜잭션은 Room 잡에서 한다.
    room->PushJob(&Room::PlayerUseItem, player, static_cast<int64_t>(pkt.item_db_id()), static_cast<int>(pkt.item_id()));
}

void PacketHandler::Handle_CS_SET_QUICKSLOT(shared_ptr<GameSession> session, Protocol::CS_SET_QUICKSLOT_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerSetQuickSlot, player, static_cast<int>(pkt.slot()), static_cast<int>(pkt.item_id()));
}

// ---- 파티(DG2, D-217). 파티는 Room 상태가 아니라 WorldService가 맡는다 — 방에 들어간 플레이어의 요청만 서비스 잡으로 넘긴다 ----
void PacketHandler::Handle_CS_PARTY_INVITE(shared_ptr<GameSession> session, Protocol::CS_PARTY_INVITE_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr || player->GetCurrentRoom() == nullptr) return;

    // 이름은 채팅과 같은 규칙으로 다듬는다(제어 문자·앞뒤 공백 제거, 길이 제한). id가 있으면 이름은 쓰지 않는다.
    const int targetId = static_cast<int>(pkt.target_id());
    wstring targetName = targetId == 0 ? SanitizeChat(pkt.target_name()) : wstring();
    if (targetId < 0) return;

    GWorld->PushJob(&WorldService::PartyInvite, player, targetId, targetName);
}

void PacketHandler::Handle_CS_PARTY_RESPOND(shared_ptr<GameSession> session, Protocol::CS_PARTY_RESPOND_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr || player->GetCurrentRoom() == nullptr) return;

    GWorld->PushJob(&WorldService::PartyRespond, player, pkt.accept());
}

void PacketHandler::Handle_CS_PARTY_LEAVE(shared_ptr<GameSession> session, Protocol::CS_PARTY_LEAVE_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr || player->GetCurrentRoom() == nullptr) return;

    GWorld->PushJob(&WorldService::PartyLeave, player);
}

void PacketHandler::Handle_CS_TRADE_REQUEST(shared_ptr<GameSession> session, Protocol::CS_TRADE_REQUEST_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::TradeRequest, player, static_cast<int>(pkt.target_id()));
}

void PacketHandler::Handle_CS_TRADE_RESPOND(shared_ptr<GameSession> session, Protocol::CS_TRADE_RESPOND_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::TradeRespond, player, pkt.accept());
}

void PacketHandler::Handle_CS_TRADE_SET_OFFER(shared_ptr<GameSession> session, Protocol::CS_TRADE_SET_OFFER_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 목록 길이는 여기서 먼저 자른다(검증은 Room 잡에서 — TradeMaxItems, 보유, 수량).
    constexpr int MAX_PACKET_ITEMS = 64;
    vector<TradeItem> items;
    for (int i = 0; i < pkt.items_size() && i < MAX_PACKET_ITEMS; ++i)
        items.push_back(TradeItem{ static_cast<int64_t>(pkt.items(i).item_db_id()), 0, static_cast<int64_t>(pkt.items(i).count()) });
    const bool tooMany = pkt.items_size() > MAX_PACKET_ITEMS;
    room->PushJob(&Room::TradeSetOffer, player, items, static_cast<int64_t>(pkt.gold()), tooMany);
}

void PacketHandler::Handle_CS_TRADE_LOCK(shared_ptr<GameSession> session, Protocol::CS_TRADE_LOCK_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::TradeLock, player, pkt.locked());
}

void PacketHandler::Handle_CS_TRADE_CONFIRM(shared_ptr<GameSession> session, Protocol::CS_TRADE_CONFIRM_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::TradeConfirm, player);
}

void PacketHandler::Handle_CS_TRADE_CANCEL(shared_ptr<GameSession> session, Protocol::CS_TRADE_CANCEL_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::TradeCancel, player);
}

void PacketHandler::Handle_CS_SKILL_AIM(shared_ptr<GameSession> session, Protocol::CS_SKILL_AIM_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 슬롯 범위 밖은 버린다. 스킬·상태·빈도 검증과 응답은 Room::PlayerSkillAim이 한다.
    const int slot = pkt.slot();
    if (slot < 0 || slot > 5) return;

    room->PushJob(&Room::PlayerSkillAim, player, slot, pkt.active(), pkt.seq());
}

void PacketHandler::Handle_CS_REVIVE(shared_ptr<GameSession> session, Protocol::CS_REVIVE_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 사망 여부·대기 시간 검증과 부활 처리는 Room 잡에서 한다(플레이어 상태는 Room 스레드에서만 바꾼다).
    room->PushJob(&Room::PlayerRevive, player);
}

void PacketHandler::Handle_CS_PORTAL(shared_ptr<GameSession> session, Protocol::CS_PORTAL_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 포털 존재·거리·상태 검증과 이동은 Room 잡에서 한다(L5, D-203).
    room->PushJob(&Room::PlayerUsePortal, player, pkt.portal_id());
}

void PacketHandler::Handle_CS_ATTACK(shared_ptr<GameSession> session, Protocol::CS_ATTACK_PACKET& pkt)
{
    Metrics::AttackRequests.fetch_add(1);
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;
    
    // 형식이 잘못된 값은 Room까지 보내지 않는다. 사거리/대상/쿨타임 검증은 Room::CastSkill이 한다.
    const int slot = pkt.skill_slot();
    if (slot < 0 || slot > 5) return;
    if (!std::isfinite(pkt.aim_yaw()) || !std::isfinite(pkt.aim_x()) || !std::isfinite(pkt.aim_y())) return;

    room->PushJob(&Room::CastSkill, static_pointer_cast<Character>(player), slot, pkt.target_id(), pkt.aim_yaw(), pkt.aim_x(), pkt.aim_y(), static_cast<uint32_t>(pkt.seq()));
}

// ---- 경매장 (A4.3). 검증은 Room 잡에서 한다 ----
void PacketHandler::Handle_CS_AUCTION_SEARCH(shared_ptr<GameSession> session, Protocol::CS_AUCTION_SEARCH_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 검색어(D-281): 채팅과 같은 정리(잘못된 UTF-8·제어 문자·앞뒤 공백) 뒤 AUCTION_KEYWORD_MAX 글자까지. SEARCH가 아니면 서버가 기억한 검색어를 쓴다
    wstring keyword;
    if (pkt.query() == Protocol::AUCTION_QUERY_SEARCH)
    {
        keyword = SanitizeChat(pkt.keyword());
        if (keyword.size() > AUCTION_KEYWORD_MAX)
        {
            size_t cut = AUCTION_KEYWORD_MAX;
            if (IS_HIGH_SURROGATE(keyword[cut - 1])) --cut;   // 서로게이트 쌍의 앞쪽만 남기지 않는다
            keyword.resize(cut);
        }
    }
    room->PushJob(&Room::PlayerAuctionSearch, player, static_cast<int>(pkt.slot()), pkt.query(), keyword, pkt.sort(), static_cast<bool>(pkt.desc()),
        static_cast<int64_t>(pkt.cursor_key()), static_cast<int64_t>(pkt.cursor_id()));
}

void PacketHandler::Handle_CS_AUCTION_REGISTER(shared_ptr<GameSession> session, Protocol::CS_AUCTION_REGISTER_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerAuctionRegister, player, static_cast<int>(pkt.npc_id()), static_cast<int64_t>(pkt.item_db_id()),
        static_cast<int>(pkt.count()), static_cast<int64_t>(pkt.unit_price()));
}

void PacketHandler::Handle_CS_AUCTION_BUY(shared_ptr<GameSession> session, Protocol::CS_AUCTION_BUY_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // 수량(D-281): int 범위 밖은 0(거부)으로
    const int64_t count = pkt.count();
    room->PushJob(&Room::PlayerAuctionBuy, player, static_cast<int64_t>(pkt.listing_id()), (count < 1 || count > INT32_MAX) ? 0 : static_cast<int>(count));
}

void PacketHandler::Handle_CS_AUCTION_CANCEL(shared_ptr<GameSession> session, Protocol::CS_AUCTION_CANCEL_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerAuctionCancel, player, static_cast<int>(pkt.npc_id()), static_cast<int64_t>(pkt.listing_id()));
}

void PacketHandler::Handle_CS_AUCTION_MY(shared_ptr<GameSession> session, Protocol::CS_AUCTION_MY_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerAuctionMy, player);
}

void PacketHandler::Handle_CS_MAIL_CLAIM(shared_ptr<GameSession> session, Protocol::CS_MAIL_CLAIM_PACKET& pkt)
{
    shared_ptr<Player> player = session->GetPlayer();
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    room->PushJob(&Room::PlayerMailClaim, player, static_cast<int>(pkt.npc_id()), static_cast<int64_t>(pkt.mail_id()));
}
