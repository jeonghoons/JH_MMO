#include "pch.h"
#include "Player.h"
#include "ServerData.h"

Player::Player(shared_ptr<Session> ownerSession, Protocol::PlayerType type) 
	: Character(Protocol::ObjectType::OBJECT_TYPE_PLAYER), _ownerSession(ownerSession)
{
	_objectInfo.set_player_type(type);
	const CharacterData* statData = DataManager::GetCharacterData((int)_objectInfo.player_type());
	if (statData) {
		// _objectInfo.stat = { statData->maxHp, statData->hp, statData->attackDamage, statData->attackSpeed, statData->moveSpeed };
		Protocol::StatInfo* stat = _objectInfo.mutable_stat();
		stat->set_max_hp(statData->maxHp);
		stat->set_hp(statData->hp);
		stat->set_attack_damage(statData->attackDamage);
		stat->set_attack_speed(statData->attackSpeed);
		stat->set_move_speed(statData->moveSpeed);
		_statInfo.Init(stat);
	}
	
	_maxSpeed = _statInfo.GetMoveSpeed();
}

Player::~Player()
{
	cout << "~Player[" << _objectInfo.id() << "]" << endl;
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
	
	// 스탯 초기화 (기존 DataManager와 결합)
	const CharacterData* statData = DataManager::GetCharacterData(info.playerType);
	if (statData) {
		Protocol::StatInfo* stat = _objectInfo.mutable_stat();
		stat->set_max_hp(statData->maxHp);
		stat->set_hp(statData->hp);
		stat->set_attack_damage(statData->attackDamage);
		stat->set_attack_speed(statData->attackSpeed);
		stat->set_move_speed(statData->moveSpeed);
		_statInfo.Init(stat);
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
	data.posX = _objectInfo.position().x();
	data.posY = _objectInfo.position().y();
	data.posZ = _objectInfo.position().z();
	return data;
}
