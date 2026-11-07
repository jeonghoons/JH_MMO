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

	// 장착 목록은 항상 슬롯 6칸(비면 0). 저장·기본 장비는 MC3.3.
	_objectInfo.clear_equip_items();
	for (int i = 0; i < EQUIP_SLOT_COUNT; ++i)
		_objectInfo.add_equip_items(0);
}

WeaponType Player::GetWeaponType() const
{
	const int weaponId = GetEquippedItem(Protocol::EQUIP_PART_WEAPON);
	if (weaponId == 0) return WeaponType::Unarmed;
	const ItemData* item = DataManager::GetItemData(weaponId);
	return item ? item->weaponType : WeaponType::Unarmed;
}

int Player::GetEquippedItem(Protocol::EquipPart part) const
{
	const int index = static_cast<int>(part) - 1;
	if (index < 0 || index >= _objectInfo.equip_items_size()) return 0;
	return _objectInfo.equip_items(index);
}

void Player::SetEquippedItem(Protocol::EquipPart part, int itemId)
{
	const int index = static_cast<int>(part) - 1;
	if (index < 0 || index >= EQUIP_SLOT_COUNT) return;
	while (_objectInfo.equip_items_size() < EQUIP_SLOT_COUNT)
		_objectInfo.add_equip_items(0);
	_objectInfo.set_equip_items(index, itemId);
}

void Player::RecalcStat()
{
	// 레벨 저장·성장은 A2.3·A2.5 — 그 전까지 시작 레벨. 로더가 시작 레벨 행을 보장한다.
	const StatData* base = DataManager::GetPlayerStatData(PLAYER_START_LEVEL);
	if (base == nullptr) return;

	int bonusAttackDamage = 0;
	int bonusMaxHp = 0;
	for (int itemId : _objectInfo.equip_items())
	{
		if (itemId == 0) continue;
		if (const ItemData* item = DataManager::GetItemData(itemId))
		{
			bonusAttackDamage += item->bonusAttackDamage;
			bonusMaxHp += item->bonusMaxHp;
		}
	}
	_statInfo.ApplyFinalStat(*base, bonusAttackDamage, bonusMaxHp);
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

