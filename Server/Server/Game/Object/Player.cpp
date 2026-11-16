#include "pch.h"
#include "Game/Object/Player.h"
#include "Data/ServerData.h"
#include "Utils/Metrics.h"
#include "Game/Room/Room.h"

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
	// 거래 중에 죽으면 거래를 취소한다(A4.1). 확정 중(DB 결과 대기)이면 그대로 둔다 — DB 결과가 권위다.
	if (tradeId != 0)
	{
		if (shared_ptr<Room> room = GetCurrentRoom())
			room->CancelTradeOf(static_pointer_cast<Player>(shared_from_this()), Protocol::TRADE_RESULT_DEAD);
	}
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

	// 액티브 칸(A3.6): DB 값을 그대로 기억하고(lastSavedQuickSlots), 쓸 수 없는 종류(ItemTable에 없음·사용 효과 없음)는 메모리에서만 비운다.
	// 비운 칸은 다음 저장 때 DB에서도 지워진다.
	lastSavedQuickSlots = data.quickSlots;
	quickSlots = data.quickSlots;
	for (int32_t& slotItem : quickSlots)
	{
		if (slotItem == 0) continue;
		const ItemData* slotData = DataManager::GetItemData(slotItem);
		if (slotData == nullptr || slotData->useEffect == ItemUseEffect::None)
		{
			LOG_WARN("Player", "액티브 칸의 아이템을 비웁니다 (PlayerId {}, ItemId {}: ItemTable에 없거나 사용할 수 없는 종류)", info.playerUID, slotItem);
			slotItem = 0;
		}
	}

	// 아이템(A3.3): Player_Item의 인벤토리·장착 행을 메모리에 올린다.
	//  - ItemTable에 없는 종류·수량 0 이하는 경고 후 올리지 않는다(DB 행은 지우지 않는다. 장착 중이었다면 DB에서 인벤토리로만 내린다).
	//  - 장착 행이 맞지 않으면(슬롯 범위 밖, 아이템 슬롯과 다름) 인벤토리로 내리고 pendingEquipFix에 적어 DB도 고치게 한다.
	//  - 인벤토리 행은 DB의 칸 번호(BagSlot)를 그대로 쓴다. 내리는 행의 칸은 모든 행을 읽은 뒤 앞의 빈 칸으로 정한다(A6.1).
	inventory = Inventory();
	pendingEquipFix.clear();
	std::vector<int64_t> demoted;          // 메모리에 올렸지만 인벤토리로 내리는 장착 행
	std::vector<int64_t> demotedUnknown;   // 메모리에 올리지 않는 장착 행(DB에서만 내린다)
	for (const DB_ItemRow& row : data.items)
	{
		const ItemData* item = DataManager::GetItemData(row.itemId);
		if (item == nullptr || row.count < 1)
		{
			LOG_WARN("Player", "Player_Item 행을 올리지 않습니다 (PlayerId {}, ItemDbId {}, ItemId {}, Count {}: ItemTable에 없거나 수량 오류)",
				info.playerUID, row.itemDbId, row.itemId, row.count);
			// 장착 중이던 행이면 DB에서는 인벤토리로 내린다. 그대로 두면 메모리에는 빈 슬롯인데 DB의 장착 슬롯
			// (유니크 인덱스)을 계속 차지해, 그 슬롯에 다른 아이템을 장착하는 요청이 매번 DB_ERROR가 된다.
			// 인벤토리에 있던 행은 그 칸을 계속 차지하므로 다른 행을 넣지 않게 표시해 둔다.
			if (row.location == ITEM_LOC_EQUIPPED)
				demotedUnknown.push_back(row.itemDbId);
			else
				inventory.ReserveSlot(row.bagSlot);
			continue;
		}

		ItemInstance instance{ row.itemDbId, row.itemId, row.count, 0, row.location == ITEM_LOC_EQUIPPED ? -1 : row.bagSlot };
		if (row.location == ITEM_LOC_EQUIPPED)
		{
			const int slot = row.equipSlot;
			const bool valid = slot >= Protocol::EQUIP_PART_HEAD && slot <= Protocol::EQUIP_PART_OFFHAND &&
				static_cast<int>(item->slot) == slot && GetEquippedItem(static_cast<Protocol::EquipPart>(slot)) == 0;
			if (valid)
			{
				instance.equipPart = slot;
				SetEquippedItem(static_cast<Protocol::EquipPart>(slot), row.itemId);
			}
			else
			{
				LOG_WARN("Player", "장착 행을 인벤토리로 내립니다 (PlayerId {}, ItemDbId {}, EquipSlot {}, ItemId {}: 슬롯이 없거나 아이템 슬롯과 다름)",
					info.playerUID, row.itemDbId, slot, row.itemId);
				demoted.push_back(row.itemDbId);
			}
		}
		inventory.Set(instance);
	}

	// 보조손(방패)은 한손검 전용 (MC4.2f). 저장 값이 어긋나 있으면 방패만 인벤토리로 내린다(DB도 고친다).
	if (GetEquippedItem(Protocol::EQUIP_PART_OFFHAND) != 0 && GetWeaponType() != WeaponType::Sword)
	{
		LOG_WARN("Player", "보조손을 인벤토리로 내립니다 (PlayerId {}, ItemId {}: 무기가 한손검이 아님)",
			info.playerUID, GetEquippedItem(Protocol::EQUIP_PART_OFFHAND));
		const int64_t offhandDbId = inventory.GetEquippedDbId(Protocol::EQUIP_PART_OFFHAND);
		if (const ItemInstance* offhand = inventory.Find(offhandDbId))
		{
			ItemInstance moved = *offhand;
			moved.equipPart = 0;
			inventory.Set(moved);
			demoted.push_back(offhandDbId);
		}
		SetEquippedItem(Protocol::EQUIP_PART_OFFHAND, 0);
	}

	// 내린 행에 칸을 준다. 버리지 않기 위해 가득 차 있어도 받는다(칸 수를 넘는 번호 — 새로 들어오는 아이템은 그 칸을 쓰지 못한다).
	for (const int64_t dbId : demoted)
	{
		const ItemInstance* found = inventory.Find(dbId);
		if (found == nullptr) continue;
		ItemInstance moved = *found;
		moved.slot = inventory.FirstFreeSlotUnbounded();
		inventory.Set(moved);
		pendingEquipFix.emplace_back(dbId, moved.slot);
	}
	for (const int64_t dbId : demotedUnknown)
	{
		const int slot = inventory.FirstFreeSlotUnbounded();
		inventory.ReserveSlot(slot);
		pendingEquipFix.emplace_back(dbId, slot);
	}

	// 로그인 HP = DB 값(A2.5)을 장비 반영 최대치 안으로. 0 이하(죽은 채 저장된 옛 값)는 부활 규칙 값으로 살아서 입장한다(D-149).
	// 저장한 적이 없는 캐릭터는 최대치 — 가입 때 넣는 HP는 장비 보너스 전 값(D-113)이라 그대로 쓰면 깎인 채 시작한다.
	RecalcStat();
	const int maxHp = _statInfo.GetMaxHp();
	const int hp = data.neverSaved ? maxHp : (data.hp > 0 ? std::min(data.hp, maxHp) : ReviveHp(maxHp));
	_objectInfo.mutable_stat()->set_hp(hp);

	// DB에 있는 값을 기억한다. 위에서 HP를 고쳤다면 스냅숏이 달라 다음 저장 때 DB에도 반영된다.
	lastSavedProgress = DB_PlayerProgress{ data.level, data.exp, data.hp };
	return true;
}

DB_PlayerProgress Player::MakeProgressSnapshot() const
{
	DB_PlayerProgress snapshot;
	snapshot.level = _objectInfo.level();
	snapshot.exp = _dbPlayerData.exp;
	snapshot.hp = _statInfo.IsDead() ? ReviveHp(_statInfo.GetMaxHp()) : _statInfo.GetHp();
	return snapshot;
}

