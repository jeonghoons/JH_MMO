#include "pch.h"
#include "PacketHandler.h"
#include "SendBuffer.h"
#include "Player.h"
#include "DatabaseWorker.h"

extern shared_ptr<DatabaseWorker> GDBWorker;

//void PacketHandler::Handle_CS_LOGIN(shared_ptr<Session> session, CS_LOGIN_PACKET* packet)
//{
//	// DB 로그인
//	string id = packet->accountID;
//	string pw = packet->accountPW;
//	if (packet->isDummy) {
//		id = "dummy";
//		pw = "dummy";
//	}
//	GDBWorker->PushDBJob(&DatabaseWorker::TryLogin, session, id, pw);
//}
//
//void PacketHandler::Handle_CS_SIGNUP(shared_ptr<Session> session, CS_SIGNUP_PACKET* packet)
//{
//	string id = packet->accountID;
//	string pw = packet->accountPW;
//	int type = static_cast<int>(packet->playerType);
//
//	GDBWorker->PushDBJob(&DatabaseWorker::TrySignUP, session, id, pw, type);
//}
//
//void PacketHandler::Handle_CS_CHAT(shared_ptr<Session> session, CS_CHAT_PACKET* packet)
//{
//	shared_ptr<Player> player = session->_currPlayer;
//	if (player == nullptr) return;
//
//	shared_ptr<Room> room = player->GetCurrentRoom();
//	if (room == nullptr) return;
//	
//	packet->message[MAX_CHAT_LEN - 1] = L'\0';
//	wstring chatMsg = packet->message;
//
//	room->PushJob(&Room::PlayerChat, player, chatMsg);
//
//	cout << "Client [" << session->GetId() << "] : " << string(chatMsg.begin(), chatMsg.end()) << endl;
//	
//}
//
//void PacketHandler::Handle_CS_MOVE(shared_ptr<Session> session, CS_MOVE_PACKET* packet)
//{
//	/*if (auto room = session->_currPlayer->GetCurrentRoom())
//		room->PushJob(&Room::PlayerMove, session->_currPlayer, packet->posInfo, packet->force);*/
//
//	shared_ptr<Player> player = session->_currPlayer;
//	if (player == nullptr) return;
//
//	shared_ptr<Room> room = player->GetCurrentRoom();
//	if (room == nullptr) return;
//
//	room->PushJob(&Room::PlayerMove, player, packet->posInfo, packet->force);
//}
//
//void PacketHandler::Handle_CS_ENTER_ROOM(shared_ptr<Session> session, CS_ENTER_ROOM_PACKET* packet)
//{
//	shared_ptr<Player> player = session->_currPlayer;
//	GRoomManager->EnterPlayer(player);
//}
//
//void PacketHandler::Handle_CS_ATTACK(shared_ptr<Session> session, CS_ATTACK_PACKET* packet)
//{
//	shared_ptr<Player> player = session->_currPlayer;
//	if (player == nullptr) return;
//
//	shared_ptr<Room> room = player->GetCurrentRoom();
//	if (room == nullptr) return;
//
//	room->PushJob(&Room::CharacterAttack, static_pointer_cast<Character>(player), 0, packet->targetId);
//}
//
//
//
//void PacketHandler::ProcessPacket(shared_ptr<Session> session, BYTE* buffer, int len)
//{
//	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
//
//	BYTE* payloadBuffer = buffer + sizeof(PacketHeader);
//	int payloadLen = header->size - sizeof(PacketHeader);
//
//	switch (header->type)
//	{
//	case CS_PACKET_LIST::CS_LOGIN:
//		Handle_CS_LOGIN(session, reinterpret_cast<CS_LOGIN_PACKET*>(buffer));
//		break;
//	case CS_PACKET_LIST::CS_SIGNUP:
//		Handle_CS_SIGNUP(session, reinterpret_cast<CS_SIGNUP_PACKET*>(buffer));
//		break;
//	case CS_PACKET_LIST::CS_CHAT:
//		Handle_CS_CHAT(session, reinterpret_cast<CS_CHAT_PACKET*>(buffer));
//		break;
//	case CS_PACKET_LIST::CS_MOVE:
//		Handle_CS_MOVE(session, reinterpret_cast<CS_MOVE_PACKET*>(buffer));
//		break;
//	case CS_PACKET_LIST::CS_ENTER_ROOM:
//		Handle_CS_ENTER_ROOM(session, reinterpret_cast<CS_ENTER_ROOM_PACKET*>(buffer));
//		break;
//	case CS_PACKET_LIST::CS_ATTACK:
//		Handle_CS_ATTACK(session, reinterpret_cast<CS_ATTACK_PACKET*>(buffer));
//		break;
//
//	case CS_PACKET_LIST::CS_LOGOUT:
//		session->Disconnect((const WCHAR*)"LogOut");
//		break;
//	default:
//		cout << "Unknown Packet [" << header->size << "]bytes" << endl;
//		break;
//	}
//
//
//}

//===================================================================================
//===================================================================================


PacketHandlerFunc GPacketHandler[UINT16_MAX];

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
}

void PacketHandler::ProcessPacket(shared_ptr<Session> session, BYTE* buffer, int len)
{
    PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
    BYTE* payloadBuffer = buffer + sizeof(PacketHeader);
    int payloadLen = header->size - sizeof(PacketHeader);

    
    auto& func = GPacketHandler[header->id]; 
    if (func)
    {
        func(session, payloadBuffer, payloadLen);
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

    GDBWorker->PushDBJob(&DatabaseWorker::TryLogin, session, id, pw);
}

void PacketHandler::Handle_CS_SIGNUP(shared_ptr<Session> session, Protocol::CS_SIGNUP_PACKET& pkt)
{
    string id = pkt.account_id();
    string pw = pkt.account_pw();
    int type = static_cast<int>(pkt.player_type());

    GDBWorker->PushDBJob(&DatabaseWorker::TrySignUP, session, id, pw, type);
}

void PacketHandler::Handle_CS_CHAT(shared_ptr<Session> session, Protocol::CS_CHAT_PACKET& pkt)
{
    shared_ptr<Player> player = session->_currPlayer;
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;

    string utf8Msg = pkt.message();
    
    if (utf8Msg.length() > MAX_CHAT_LEN)
    {
        utf8Msg = utf8Msg.substr(0, MAX_CHAT_LEN);
    }

    // UTF-8(string) -> UTF-16(wstring) 변환
    wstring chatMsg = Utils::UTF8ToWString(utf8Msg);

    // 5. JobQueue에 밀어넣기
    room->PushJob(&Room::PlayerChat, player, chatMsg);

    // 6. 콘솔 출력 (기존의 지저분한 이터레이터 캐스팅 제거)
    cout << "Client [" << session->GetId() << "] : " << utf8Msg << endl;
}

void PacketHandler::Handle_CS_ENTER_ROOM(shared_ptr<Session> session, Protocol::CS_ENTER_ROOM_PACKET& pkt)
{
    shared_ptr<Player> player = session->_currPlayer;
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

void PacketHandler::Handle_CS_ATTACK(shared_ptr<Session> session, Protocol::CS_ATTACK_PACKET& pkt)
{
    shared_ptr<Player> player = session->_currPlayer;
    if (player == nullptr) return;

    shared_ptr<Room> room = player->GetCurrentRoom();
    if (room == nullptr) return;
    
    room->PushJob(&Room::CharacterAttack, static_pointer_cast<Character>(player), 0, pkt.target_id());
}
