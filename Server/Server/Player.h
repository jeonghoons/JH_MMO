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

private:
	weak_ptr<Session> _ownerSession;
	wstring _name;
	
	DB_PlayerInfo _dbPlayerInfo;
	DB_PlayerData _dbPlayerData;
};



