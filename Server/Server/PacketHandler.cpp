#include "pch.h"
#include "PacketHandler.h"
#include "SendBuffer.h"
#include "Player.h"
#include "DatabaseWorker.h"
#include "ServerData.h"
#include "Metrics.h"

extern shared_ptr<DatabaseWorker> GDBWorker;

PacketHandlerFunc GPacketHandler[UINT16_MAX + 1];

void PacketHandler::Init()
{
    
    GPacketHandler[Protocol::CS_LOGIN] = [](shared_ptr<Session> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_LOGIN_PACKET>(&PacketHandler::Handle_CS_LOGIN, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_SIGNUP] = [](shared_ptr<Session> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_SIGNUP_PACKET>(&PacketHandler::Handle_CS_SIGNUP, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_CHAT] = [](shared_ptr<Session> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_CHAT_PACKET>(&PacketHandler::Handle_CS_CHAT, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_ENTER_ROOM] = [](shared_ptr<Session> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_ENTER_ROOM_PACKET>(&PacketHandler::Handle_CS_ENTER_ROOM, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_MOVE] = [](shared_ptr<Session> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_MOVE_PACKET>(&PacketHandler::Handle_CS_MOVE, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_ATTACK] = [](shared_ptr<Session> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_ATTACK_PACKET>(&PacketHandler::Handle_CS_ATTACK, session, buffer, len);
        };

    GPacketHandler[Protocol::CS_PING] = [](shared_ptr<Session> session, BYTE* buffer, int len) {
        return ParseAndHandle<Protocol::CS_PING_PACKET>(&PacketHandler::Handle_CS_PING, session, buffer, len);
        };
}

void PacketHandler::ProcessPacket(shared_ptr<Session> session, BYTE* buffer, int len)
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
        cout << "Invalid Packet Id : " << header->id << endl;
    }
}




void PacketHandler::Handle_CS_LOGIN(shared_ptr<Session> session, Protocol::CS_LOGIN_PACKET& pkt)
{
    // C++ string 매핑 활용
    string id = pkt.account_id();
    string pw = pkt.account_pw();

    if (pkt.is_dummy()) {
        id = "dummy";
        pw = "dummy";
    }

    // 같은 연결로 다시 로그인하면 이전 캐릭터가 방에 남으므로 거부한다. (DB 응답 전에 연달아 온 요청은 AuthLobby::OnLoginSuccess에서 거른다)
    if (session->_currPlayer != nullptr)
    {
        GLobby->PushJob(&AuthLobby::OnLoginFailed, session, Protocol::LOGIN_RESULT_ALREADY_LOGGED_IN);
        return;
    }

    GDBWorker->PushDBJob(&DatabaseWorker::TryLogin, session, id, pw);
}

void PacketHandler::Handle_CS_SIGNUP(shared_ptr<Session> session, Protocol::CS_SIGNUP_PACKET& pkt)
{
    string id = pkt.account_id();
    string pw = pkt.account_pw();
    int type = static_cast<int>(pkt.player_type());

    // 스탯 데이터가 없는 타입(예: MALE/FEMALE)으로 가입하면 스탯이 없는 캐릭터가 만들어지므로 거부한다.
    if (DataManager::GetCharacterData(type) == nullptr)
    {
        cout << "Invalid PlayerType on signup : " << type << endl;
        GLobby->PushJob(&AuthLobby::OnSignUpResult, session, false);
        return;
    }
    // 이미 로그인한 연결에서는 가입하지 않는다.
    if (session->_currPlayer != nullptr)
    {
        GLobby->PushJob(&AuthLobby::OnSignUpResult, session, false);
        return;
    }

    GDBWorker->PushDBJob(&DatabaseWorker::TrySignUP, session, id, pw, type);
}

void PacketHandler::Handle_CS_CHAT(shared_ptr<Session> session, Protocol::CS_CHAT_PACKET& pkt)
{
    shared_ptr<Player> player = session->_currPlayer;
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

void PacketHandler::Handle_CS_ENTER_ROOM(shared_ptr<Session> session, Protocol::CS_ENTER_ROOM_PACKET& pkt)
{
    // 로그인 전(DB 응답 전 포함)이거나 이미 방에 배정된 플레이어의 입장 요청은 무시한다.
    // 방 배정(SetOwnerRoom)은 EnterPlayer 안에서 바로 되므로 연달아 온 두 번째 요청도 여기서 걸러진다.
    shared_ptr<Player> player = session->_currPlayer;
    if (player == nullptr || player->GetCurrentRoom() != nullptr) return;

    GRoomManager->EnterPlayer(player);
}

void PacketHandler::Handle_CS_MOVE(shared_ptr<Session> session, Protocol::CS_MOVE_PACKET& pkt)
{
    shared_ptr<Player> player = session->_currPlayer;
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;
    
    room->PushJob(&Room::PlayerMove, player, pkt.pos_info(), pkt.force());
}

void PacketHandler::Handle_CS_PING(shared_ptr<Session> session, Protocol::CS_PING_PACKET& pkt)
{
    shared_ptr<Player> player = session->_currPlayer;
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    // Room의 JobQueue를 거쳐 응답해서 Room 처리 지연이 왕복 시간에 포함되게 한다.
    room->PushJob(&Room::PlayerPing, player, pkt.client_time());
}

void PacketHandler::Handle_CS_ATTACK(shared_ptr<Session> session, Protocol::CS_ATTACK_PACKET& pkt)
{
    Metrics::AttackRequests.fetch_add(1);
    shared_ptr<Player> player = session->_currPlayer;
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;
    
    // 형식이 잘못된 값은 Room까지 보내지 않는다. 사거리/대상/쿨타임 검증은 Room::CastSkill이 한다.
    const int slot = pkt.skill_slot();
    if (slot < 0 || slot > 5) return;
    if (!std::isfinite(pkt.aim_yaw()) || !std::isfinite(pkt.aim_x()) || !std::isfinite(pkt.aim_y())) return;

    room->PushJob(&Room::CastSkill, static_pointer_cast<Character>(player), slot, pkt.target_id(), pkt.aim_yaw(), pkt.aim_x(), pkt.aim_y());
}
