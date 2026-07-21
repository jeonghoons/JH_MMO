#pragma once
#include "pch.h"
#include "Character.h"
#include "Session.h"
#include "Room.h"

class Player : public Character
{
public:
	// Player();
	Player(shared_ptr<Session> ownerSession, Protocol::PlayerType type);
	virtual ~Player();

public:
	virtual void Update(float deltaTime) override;

	void SetOwnerSession(shared_ptr<Session> session) { _ownerSession = session; }
	shared_ptr<Session> GetSession() {return _ownerSession.lock();	}
	
	virtual void OnDamaged(int damage, std::shared_ptr<Character> attacker) override;
	virtual void OnDead(std::shared_ptr<Character> attacker) override;
public:

	void InitFromDb(const DB_PlayerInfo& info, const DB_PlayerData& data);
	DB_PlayerData GetCurrentDbData();
	int64_t GetPlayerUID() const { return _dbPlayerInfo.playerUID; }

	void SetPlayerType(Protocol::PlayerType type) { _objectInfo.set_player_type(type); }
	bool isDummy = false;

private:
	weak_ptr<Session> _ownerSession;
	
	DB_PlayerInfo _dbPlayerInfo;
	DB_PlayerData _dbPlayerData;
};



