#include "pch.h"
#include "Session/GameSession.h"
#include "Packet/PacketHandler.h"
#include "Game/Object/Player.h"
#include "Game/Room/Room.h"
#include "Game/Lobby/AuthLobby.h"

void GameSession::OnDisconnected()
{
	HandlePlayerGone();
}

void GameSession::HandlePlayerGone()
{
	// 로그인 전에 끊기면 _currPlayer가 아직 없다.
	shared_ptr<Player> player = GetPlayer();
	if (player == nullptr) return;
	if (_playerGoneHandled.exchange(true)) return;

	if (shared_ptr<Room> room = player->GetCurrentRoom()) {
		// 접속 목록에서 빼는 신호는 퇴장 처리와 마지막 저장이 끝난 뒤 Room이 보낸다(Room::NotifyClosedIfDone).
		room->PushJob(&Room::PlayerLeaveRoom, player);
	}
	else {
		// 방에 들어가기 전에 끊겼다: 저장할 것이 없으므로 바로 알린다(명부·채널 인원 정리 → 로비의 접속 목록 정리, 더미 봇 포함).
		// 이 직후 입장 요청이 처리돼도 서비스가 명부에 없는 세션을 버리고, 먼저 처리됐다면 입장 잡이 끊긴 세션을 보고 취소한다.
		GWorld->PushJob(&WorldService::OnPlayerClosed, player->GetId(), player->GetPlayerUID(), player->isDummy);
	}
}

void GameSession::OnRecvPacket(BYTE* buffer, int len)
{
	PacketHandler::ProcessPacket(GetGameSessionRef(), buffer, len);
}
