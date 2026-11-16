#pragma once
#include "Network/Session.h"

class Player;

// 게임 쪽 세션. 네트워크 계층의 Session 훅으로 패킷 처리와 접속 종료 정리를 맡는다.
class GameSession : public Session
{
public:
	shared_ptr<GameSession> GetGameSessionRef() { return static_pointer_cast<GameSession>(shared_from_this()); }

	shared_ptr<Player>	_currPlayer;	// 로그인 성공 후 설정(AuthLobby)

	// 끊긴 세션의 플레이어를 정리한다(세션당 한 번): 방에 있으면 퇴장 잡, 방에 들어가기 전이면 로비에 접속 종료를 알린다(A2.7).
	// OnDisconnected가 부르고, 로그인 처리 도중에 끊긴 경우(OnDisconnected가 _currPlayer를 못 본 경우)는 AuthLobby가 부른다.
	void				HandlePlayerGone();

protected:
	virtual void	OnDisconnected() override;
	virtual void	OnRecvPacket(BYTE* buffer, int len) override;

private:
	atomic<bool>		_playerGoneHandled = false;
};
