#include "pch.h"
#include "Game/Object/Player.h"
#include "Data/ServerData.h"
#include "Utils/Metrics.h"

Player::Player(shared_ptr<GameSession> ownerSession, Protocol::PlayerType type) 
	: Character(Protocol::ObjectType::OBJECT_TYPE_PLAYER), _ownerSession(ownerSession)
{
	_objectInfo.set_player_type(type);
	// 기본 스탯은 체형 공용, 레벨별(PlayerStatTable). 여기서는 시작 레벨로 채우고, DB 레벨은 InitFromDb가 반영한다(A2.5).
	_objectInfo.set_level(PLAYER_START_LEVEL);
	if (const StatData* statData = DataManager::GetPlayerStatData(PLAYER_START_LEVEL)) {
		_statInfo.InitFromData(_objectInfo.mutable_stat(), *statData);
	}

	_maxSpeed = _statInfo.GetMoveSpeed();

	// 장착 목록은 항상 슬롯 EQUIP_SLOT_COUNT(7)칸(비면 0). 저장·기본 장비는 MC3.3.
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
	// 레벨은 DB에서 온다(InitFromDb, 그 전에는 시작 레벨). 행이 있는지는 InitFromDb가 확인했다. 성장은 A2.3b.
	const StatData* base = DataManager::GetPlayerStatData(_objectInfo.level());
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
	// 조준 해제는 상태만 바꾼다 — 원격 클라이언트는 SC_DEAD로, 본인 클라이언트는 자기 사망 처리로 끈다(DECISIONS 2026-11-09).
	if (IsSkillAiming())
		SetSkillAim(nullptr, chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count());
	Metrics::PlayerDeaths.fetch_add(1);
	deadAtMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
	Character::OnDead(attacker);
}

int Player::GetReviveDelayMs() const
{
	const WorldData* world = DataManager::GetWorldData(ROOM_MAP_NAME);   // 서버 시작 때 존재를 확인했다
	return world ? world->reviveDelaySec * 1000 : 0;
}

float Player::GetMoveSpeedLimit(const MovementData& rule) const
{
	return _objectInfo.stat().move_speed() * rule.speedTolerance * aim.speedMul;
}

void Player::AccrueMoveBudget(long long nowMs, const MovementData& rule)
{
	const float maxSpeed = GetMoveSpeedLimit(rule);
	const float budgetCap = maxSpeed * rule.moveBudgetMs / 1000.f;
	moveGuard.budgetCm = min(budgetCap, moveGuard.budgetCm + maxSpeed * (nowMs - moveGuard.lastAccrueMs) / 1000.f);
	moveGuard.lastAccrueMs = nowMs;
}

bool Player::SetSkillAim(const SkillData* skill, long long nowMs)
{
	const bool wasAiming = IsSkillAiming();
	if (const MovementData* rule = DataManager::GetMovementData(PLAYER_MOVEMENT_ID))   // 서버 시작 때 존재를 확인했다
		AccrueMoveBudget(nowMs, *rule);

	if (skill != nullptr)
	{
		aim.skillId = skill->skillId;
		aim.speedMul = skill->aimMoveSpeedMul;
		aim.deadlineMs = nowMs + skill->aimMaxMs;
	}
	else
	{
		aim.skillId = 0;
		aim.speedMul = 1.f;
		aim.deadlineMs = 0;
	}
	_objectInfo.set_aim_skill_id(aim.skillId);
	return wasAiming;
}

// 부활 규칙의 HP(= StatComponent::Revive): 최대치 × WorldTable.ReviveHpPercent, 최소 1
static int ReviveHp(int maxHp)
{
	const WorldData* world = DataManager::GetWorldData(ROOM_MAP_NAME);   // 서버 시작 때 존재를 확인했다
	const int percent = world ? world->reviveHpPercent : 100;
	return std::max(1, static_cast<int>(static_cast<long long>(maxHp) * percent / 100));
}

bool Player::InitFromDb(const DB_PlayerInfo& info, const DB_PlayerData& data)
{
	// 레벨(A2.5): DB 레벨의 기본 스탯 행이 없으면 들이지 않는다(스탯을 지어내지 않는다).
	if (DataManager::GetPlayerStatData(data.level) == nullptr)
	{
		LOG_ERROR("Player", "PlayerId {} 의 Level {} 이 PlayerStatTable에 없습니다", info.playerUID, data.level);
		return false;
	}

	_dbPlayerInfo = info;
	_dbPlayerData = data;
	_objectInfo.set_level(data.level);

	// 장비(MC3.3). 잘못된 행(슬롯 범위 밖, 없는 아이템, 슬롯 불일치)은 경고 후 건너뛴다.
	for (const auto& [slot, itemId] : data.equipped)
	{
		const ItemData* item = DataManager::GetItemData(itemId);
		if (slot < Protocol::EQUIP_PART_HEAD || slot > Protocol::EQUIP_PART_OFFHAND || item == nullptr || static_cast<int>(item->slot) != slot)
		{
			LOG_WARN("Player", "Player_Equipped 행을 건너뜁니다 (PlayerId {}, EquipSlot {}, ItemId {}: 슬롯이 없거나 아이템이 없거나 슬롯이 다름)",
				info.playerUID, slot, itemId);
			continue;
		}
		SetEquippedItem(static_cast<Protocol::EquipPart>(slot), itemId);
	}

	// 보조손(방패)은 한손검 전용 (MC4.2f). 저장 값이 어긋나 있으면 보조손만 비운다(다음 저장 때 DB에도 반영).
	if (GetEquippedItem(Protocol::EQUIP_PART_OFFHAND) != 0 && GetWeaponType() != WeaponType::Sword)
	{
		LOG_WARN("Player", "Player_Equipped 보조손을 비웁니다 (PlayerId {}, ItemId {}: 무기가 한손검이 아님)",
			info.playerUID, GetEquippedItem(Protocol::EQUIP_PART_OFFHAND));
		SetEquippedItem(Protocol::EQUIP_PART_OFFHAND, 0);
	}

	// 로그인 HP = DB 값(A2.5)을 장비 반영 최대치 안으로. 0 이하(죽은 채 저장된 옛 값)는 부활 규칙 값으로 살아서 입장한다(D-149).
	// 저장한 적이 없는 캐릭터는 최대치 — 가입 때 넣는 HP는 장비 보너스 전 값(D-113)이라 그대로 쓰면 깎인 채 시작한다.
	RecalcStat();
	const int maxHp = _statInfo.GetMaxHp();
	const int hp = data.neverSaved ? maxHp : (data.hp > 0 ? std::min(data.hp, maxHp) : ReviveHp(maxHp));
	_objectInfo.mutable_stat()->set_hp(hp);

	// DB에 있는 값을 기억한다. 위에서 HP를 고쳤다면 스냅숏이 달라 다음 저장 때 DB에도 반영된다.
	lastSavedProgress = DB_PlayerProgress{ data.level, data.exp, data.hp, data.gold };
	return true;
}

DB_PlayerProgress Player::MakeProgressSnapshot() const
{
	DB_PlayerProgress snapshot;
	snapshot.level = _objectInfo.level();
	snapshot.exp = _dbPlayerData.exp;
	snapshot.hp = _statInfo.IsDead() ? ReviveHp(_statInfo.GetMaxHp()) : _statInfo.GetHp();
	snapshot.gold = _dbPlayerData.gold;
	return snapshot;
}

