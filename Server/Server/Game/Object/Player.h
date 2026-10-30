#pragma once
#include "pch.h"
#include "Game/Object/Character.h"
#include "Session/GameSession.h"
#include "Game/Room/Room.h"

class Player : public Character
{
public:
	// Player();
	Player(shared_ptr<GameSession> ownerSession, Protocol::PlayerType type);
	virtual ~Player();

public:
	virtual void Update(float deltaTime) override;

	void SetOwnerSession(shared_ptr<GameSession> session) { _ownerSession = session; }
	shared_ptr<GameSession> GetSession() {return _ownerSession.lock();	}
	
	virtual void OnDamaged(int damage, int attackerId, std::shared_ptr<Character> attacker) override;
	virtual void OnDead(std::shared_ptr<Character> attacker) override;
public:

	void InitFromDb(const DB_PlayerInfo& info, const DB_PlayerData& data);
	int64_t GetPlayerUID() const { return _dbPlayerInfo.playerUID; }

	void SetPlayerType(Protocol::PlayerType type) { _objectInfo.set_player_type(type); }
	bool isDummy = false;

	// 캐릭터 이름(Player_Info.Name). 로그인 때 정해지고 바뀌지 않는다.
	void SetName(const wstring& name) { _name = name; }
	const wstring& GetName() const { return _name; }

	// 채팅 도배 방지. Room 잡 안에서만 읽고 쓴다.
	Cooldown chatCooldown;

	// 이동 검증 상태(S1, Room::PlayerMove). Room 잡 안에서만 읽고 쓴다. 방에 들어갈 때 초기화한다.
	struct MoveGuard
	{
		float budgetCm = 0.f;        // 지금까지 모인 이동 가능 거리(토큰 버킷)
		long long lastAccrueMs = 0;  // 예산을 마지막으로 채운 시각
		long long lastRecvMs = 0;    // 마지막으로 처리한 이동 패킷 시각(전송 빈도 제한)
		uint32_t correctionSeq = 0;  // 서버가 이 플레이어의 이동을 거부(보정)한 횟수
	};
	MoveGuard moveGuard;

private:
	weak_ptr<GameSession> _ownerSession;
	wstring _name;
	
	DB_PlayerInfo _dbPlayerInfo;
	DB_PlayerData _dbPlayerData;
};



