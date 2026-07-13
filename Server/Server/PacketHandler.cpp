#include "pch.h"
#include "PacketHandler.h"
#include "SendBuffer.h"
#include "Player.h"
#include "DatabaseWorker.h"

extern shared_ptr<DatabaseWorker> GDBWorker;

void PacketHandler::Handle_CS_LOGIN(shared_ptr<Session> session, CS_LOGIN_PACKET* packet)
{
	// DB ·Î±×ÀÎ
	string id = packet->accountID;
	string pw = packet->accountPW;
	if (packet->isDummy) {
		id = "dummy";
		pw = "dummy";
	}
	GDBWorker->PushDBJob(&DatabaseWorker::TryLogin, session, id, pw);
}

void PacketHandler::Handle_CS_SIGNUP(shared_ptr<Session> session, CS_SIGNUP_PACKET* packet)
{
	string id = packet->accountID;
	string pw = packet->accountPW;
	int type = static_cast<int>(packet->playerType);

	GDBWorker->PushDBJob(&DatabaseWorker::TrySignUP, session, id, pw, type);
}

void PacketHandler::Handle_CS_CHAT(shared_ptr<Session> session, CS_CHAT_PACKET* packet)
{
	shared_ptr<Player> player = session->_currPlayer;
	if (player == nullptr) return;

	shared_ptr<Room> room = player->GetCurrentRoom();
	if (room == nullptr) return;
	
	packet->message[MAX_CHAT_LEN - 1] = L'\0';
	wstring chatMsg = packet->message;

	room->PushJob(&Room::PlayerChat, player, chatMsg);

	cout << "Client [" << session->GetId() << "] : " << string(chatMsg.begin(), chatMsg.end()) << endl;
	
}

void PacketHandler::Handle_CS_MOVE(shared_ptr<Session> session, CS_MOVE_PACKET* packet)
{
	/*if (auto room = session->_currPlayer->GetCurrentRoom())
		room->PushJob(&Room::PlayerMove, session->_currPlayer, packet->posInfo, packet->force);*/

	shared_ptr<Player> player = session->_currPlayer;
	if (player == nullptr) return;

	shared_ptr<Room> room = player->GetCurrentRoom();
	if (room == nullptr) return;

	room->PushJob(&Room::PlayerMove, player, packet->posInfo, packet->force);
}

void PacketHandler::Handle_CS_ENTER_ROOM(shared_ptr<Session> session, CS_ENTER_ROOM_PACKET* packet)
{
	shared_ptr<Player> player = session->_currPlayer;
	GRoomManager->EnterPlayer(player);
}

void PacketHandler::Handle_CS_ATTACK(shared_ptr<Session> session, CS_ATTACK_PACKET* packet)
{
	shared_ptr<Player> player = session->_currPlayer;
	if (player == nullptr) return;

	shared_ptr<Room> room = player->GetCurrentRoom();
	if (room == nullptr) return;

	room->PushJob(&Room::CharacterAttack, static_pointer_cast<Character>(player), 0, packet->targetId);
}



void PacketHandler::ProcessPacket(shared_ptr<Session> session, BYTE* buffer, int len)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

	switch (header->type)
	{
	case CS_PACKET_LIST::CS_LOGIN:
		Handle_CS_LOGIN(session, reinterpret_cast<CS_LOGIN_PACKET*>(buffer));
		break;
	case CS_PACKET_LIST::CS_SIGNUP:
		Handle_CS_SIGNUP(session, reinterpret_cast<CS_SIGNUP_PACKET*>(buffer));
		break;
	case CS_PACKET_LIST::CS_CHAT:
		Handle_CS_CHAT(session, reinterpret_cast<CS_CHAT_PACKET*>(buffer));
		break;
	case CS_PACKET_LIST::CS_MOVE:
		Handle_CS_MOVE(session, reinterpret_cast<CS_MOVE_PACKET*>(buffer));
		break;
	case CS_PACKET_LIST::CS_ENTER_ROOM:
		Handle_CS_ENTER_ROOM(session, reinterpret_cast<CS_ENTER_ROOM_PACKET*>(buffer));
		break;
	case CS_PACKET_LIST::CS_ATTACK:
		Handle_CS_ATTACK(session, reinterpret_cast<CS_ATTACK_PACKET*>(buffer));
		break;

	case CS_PACKET_LIST::CS_LOGOUT:
		session->Disconnect((const WCHAR*)"LogOut");
		break;
	default:
		cout << "Unknown Packet [" << header->size << "]bytes" << endl;
		break;
	}


}

//===================================================================================
//===================================================================================


