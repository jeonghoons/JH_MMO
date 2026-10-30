#include "pch.h"
#include "GameSession.h"
#include "PacketHandler.h"
#include "Player.h"
#include "Room.h"

void GameSession::OnDisconnected()
{
	// 로그인 전에 끊기면 _currPlayer가 아직 없다.
	if (_currPlayer != nullptr) {
		if (shared_ptr<Room> room = _currPlayer->GetCurrentRoom()) {
			room->PushJob(&Room::PlayerLeaveRoom, _currPlayer);
		}
	}
}

void GameSession::OnRecvPacket(BYTE* buffer, int len)
{
	PacketHandler::ProcessPacket(GetGameSessionRef(), buffer, len);
}
