#pragma once
#include "Network/Session.h"

class Player;

// 게임 쪽 세션. 네트워크 계층의 Session 훅으로 패킷 처리와 접속 종료 정리를 맡는다.
class GameSession : public Session
{
public:
	shared_ptr<GameSession> GetGameSessionRef() { return static_pointer_cast<GameSession>(shared_from_this()); }

	shared_ptr<Player>	_currPlayer;	// 로그인 성공 후 설정(AuthLobby)

protected:
	virtual void	OnDisconnected() override;
	virtual void	OnRecvPacket(BYTE* buffer, int len) override;
};
