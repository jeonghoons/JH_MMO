#include "pch.h"
#include "Game/Object/Player.h"
#include "Data/ServerData.h"

Player::Player(shared_ptr<GameSession> ownerSession, Protocol::PlayerType type) 
	: Character(Protocol::ObjectType::OBJECT_TYPE_PLAYER), _ownerSession(ownerSession)
{
	_objectInfo.set_player_type(type);
	// 기본 스탯은 체형 공용, 레벨별(PlayerStatTable). 레벨 저장은 A2.3·A2.5 — 그 전까지 시작 레벨. 로더가 시작 레벨 행을 보장한다.
	if (const StatData* statData = DataManager::GetPlayerStatData(PLAYER_START_LEVEL)) {
		_statInfo.InitFromData(_objectInfo.mutable_stat(), *statData);
	}

	_maxSpeed = _statInfo.GetMoveSpeed();
}

Player::~Player()
{
	LOG_DEBUG("Object", "~Player[{}]", _objectInfo.id());
	_ownerSession.reset();
}

void Player::Update(float deltaTime)
{
	Character::Update(deltaTime);
}

void Player::OnDamaged(int damage, int attackerId, std::shared_ptr<Character> attacker)
{
	Character::OnDamaged(damage, attackerId, attacker);	
}

void Player::OnDead(std::shared_ptr<Character> attacker)
{
	Character::OnDead(attacker);
}

void Player::InitFromDb(const DB_PlayerInfo& info, const DB_PlayerData& data)
{
	_dbPlayerInfo = info;
	_dbPlayerData = data;

	
	// A2.5에서 DB 레벨(data.level)로 조회하도록 바꾼다. 지금은 아무도 부르지 않는다.
	if (const StatData* statData = DataManager::GetPlayerStatData(PLAYER_START_LEVEL)) {
		_statInfo.InitFromData(_objectInfo.mutable_stat(), *statData);
	}

	_objectInfo.clear_equip_items();
	for (int32_t itemId : data.equipItemIds)
	{
		_objectInfo.add_equip_items(itemId);
	}
}

