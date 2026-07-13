#include "pch.h"
#include "Player.h"
#include "ServerData.h"

Player::Player(shared_ptr<Session> ownerSession, PlayerType type) 
	: Character(Object_Type::Player), _ownerSession(ownerSession)
{
	_objectInfo.playerType = type;
	const CharacterData* statData = DataManager::GetCharacterData((int)_objectInfo.playerType);
	if (statData) {
		_objectInfo.stat = { statData->maxHp, statData->hp, statData->attackDamage, statData->attackSpeed, statData->moveSpeed };
	}
	_statInfo.Init(&_objectInfo.stat);
	_maxSpeed = _statInfo.GetMoveSpeed();
}

Player::~Player()
{
	cout << "~Player[" << _objectInfo.id << "]" << endl;
	_ownerSession.reset();
}

void Player::Update(float deltaTime)
{
	Character::Update(deltaTime);
}

void Player::OnDamaged(int damage, std::shared_ptr<Character> attacker)
{
	Character::OnDamaged(damage, attacker);	
}

void Player::OnDead(std::shared_ptr<Character> attacker)
{
	Character::OnDead(attacker);
}

void Player::InitFromDb(const DB_PlayerInfo& info, const DB_PlayerData& data)
{
	_dbPlayerInfo = info;
	_dbPlayerData = data;

	// DB에서 가져온 위치로 세팅
	SetPosition({ data.posX, data.posY, data.posZ, 0.0f });

	// 스탯 초기화 (기존 DataManager와 결합)
	const CharacterData* statData = DataManager::GetCharacterData(info.playerType);
	if (statData) {
		_objectInfo.stat = { statData->maxHp, data.hp, statData->attackDamage, statData->attackSpeed, statData->moveSpeed };
	}
}

DB_PlayerData Player::GetCurrentDbData()
{
	DB_PlayerData data;
	data.playerUID = _dbPlayerInfo.playerUID;
	data.level = 1;
	data.exp = 0;
	data.hp = _statInfo.GetHp();
	data.mp = 100; 
	data.posX = _objectInfo.position.x;
	data.posY = _objectInfo.position.y;
	data.posZ = _objectInfo.position.z;
	return data;
}
