#include "pch.h"
#include "Data/ServerData.h"
#include "Utils/CsvFile.h"
#include <filesystem>

namespace
{
	namespace fs = std::filesystem;

	// Common/DataTable 위치를 찾는다. 환경변수 JH_DATA_DIR가 있으면 그것을 쓰고,
	// 없으면 서버 작업 디렉터리(Server/Server) 기준 상대 경로 후보를 차례로 확인한다.
	bool FindDataDir(fs::path& out)
	{
		std::vector<fs::path> candidates;

		char* env = nullptr;
		size_t envLen = 0;
		if (_dupenv_s(&env, &envLen, "JH_DATA_DIR") == 0 && env != nullptr)
		{
			candidates.push_back(env);
			free(env);
		}
		candidates.push_back("../../Common/DataTable");
		candidates.push_back("../Common/DataTable");
		candidates.push_back("Common/DataTable");
		candidates.push_back("DataTable");

		for (const fs::path& p : candidates)
		{
			if (fs::exists(p / "PlayerStatTable.csv"))
			{
				out = fs::absolute(p);
				return true;
			}
		}
		return false;
	}

	bool ParseHitShape(const std::string& text, HitShape& out)
	{
		if (text == "Sector") { out = HitShape::Sector; return true; }
		if (text == "Line") { out = HitShape::Line; return true; }
		if (text == "Projectile") { out = HitShape::Projectile; return true; }
		if (text == "Zone") { out = HitShape::Zone; return true; }
		return false;
	}

	// None은 ItemTable(무기가 아닌 아이템)에서만 쓴다.
	bool ParseWeaponType(const std::string& text, WeaponType& out)
	{
		if (text == "None") { out = WeaponType::None; return true; }
		if (text == "Unarmed") { out = WeaponType::Unarmed; return true; }
		if (text == "Sword") { out = WeaponType::Sword; return true; }
		if (text == "Bow") { out = WeaponType::Bow; return true; }
		if (text == "Monster") { out = WeaponType::Monster; return true; }
		return false;
	}

	bool ParseItemSlot(const std::string& text, ItemSlot& out)
	{
		if (text == "Head") { out = ItemSlot::Head; return true; }
		if (text == "Top") { out = ItemSlot::Top; return true; }
		if (text == "Bottom") { out = ItemSlot::Bottom; return true; }
		if (text == "Gloves") { out = ItemSlot::Gloves; return true; }
		if (text == "Boots") { out = ItemSlot::Boots; return true; }
		if (text == "Weapon") { out = ItemSlot::Weapon; return true; }
		if (text == "Offhand") { out = ItemSlot::Offhand; return true; }
		return false;
	}

	bool ParseItemTier(const std::string& text, ItemTier& out)
	{
		if (text == "Cloth") { out = ItemTier::Cloth; return true; }
		if (text == "Leather") { out = ItemTier::Leather; return true; }
		if (text == "Plate") { out = ItemTier::Plate; return true; }
		return false;
	}

	// PlayerStatTable·MonsterTable 공통 스탯 열(MaxHp, AttackDamage, AttackSpeed, MoveSpeed)을 읽고 범위를 검사한다.
	bool ReadStat(const CsvFile& file, const CsvRow& row, StatData& out, std::string& err)
	{
		if (!row.GetInt("MaxHp", out.maxHp, err) || !row.GetInt("AttackDamage", out.attackDamage, err) ||
			!row.GetFloat("AttackSpeed", out.attackSpeed, err) || !row.GetFloat("MoveSpeed", out.moveSpeed, err))
		{
			return false;
		}
		if (out.maxHp <= 0 || out.attackDamage < 0 || out.attackSpeed <= 0.f || out.moveSpeed < 0.f)
		{
			err = file.Path() + " line " + std::to_string(row.Line()) + ": 스탯이 허용 범위를 벗어났습니다 (MaxHp>0, AttackDamage>=0, AttackSpeed>0, MoveSpeed>=0)";
			return false;
		}
		return true;
	}

	bool ParseItemGrade(const std::string& text, ItemGrade& out)
	{
		if (text == "Common") { out = ItemGrade::Common; return true; }
		if (text == "Uncommon") { out = ItemGrade::Uncommon; return true; }
		if (text == "Rare") { out = ItemGrade::Rare; return true; }
		return false;
	}
}

bool DataManager::Init()
{
	_skillTable.clear();
	_playerStatTable.clear();
	_monsterTable.clear();
	_movementTable.clear();
	_itemTable.clear();
	_worldTable.clear();
	_skillBySlot.clear();

	fs::path dir;
	if (!FindDataDir(dir))
	{
		LOG_ERROR("Data", "Common/DataTable 폴더를 찾을 수 없습니다. 환경변수 JH_DATA_DIR로 경로를 지정하세요.");
		return false;
	}
	LOG_INFO("Data", "DataTable 경로: {}", dir.string());

	std::string err;

	// PlayerStatTable: Name, Level, MaxHp, AttackDamage, AttackSpeed, MoveSpeed (체형 공용, 조회 키 = Level)
	CsvFile stat;
	if (!stat.Load((dir / "PlayerStatTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : stat.Rows())
	{
		int id = 0, level = 0;
		StatData d{};
		if (!row.GetInt("Name", id, err) || !row.GetInt("Level", level, err) || !ReadStat(stat, row, d, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (level < 1)
		{
			LOG_ERROR("Data", "{} line {}: Level 은 1 이상이어야 합니다", stat.Path(), row.Line());
			return false;
		}
		if (_playerStatTable.count(level))
		{
			LOG_ERROR("Data", "{} line {}: Level {} 이 중복되었습니다", stat.Path(), row.Line(), level);
			return false;
		}
		_playerStatTable[level] = d;
	}
	if (GetPlayerStatData(PLAYER_START_LEVEL) == nullptr)
	{
		LOG_ERROR("Data", "{}: 시작 레벨 {} 행이 없습니다", stat.Path(), PLAYER_START_LEVEL);
		return false;
	}

	// SkillTable: Name(=skillId), WeaponType, Slot, TargetingType, HitShape, HitDelayMs, CooldownMs, AnimMs, CastRange,
	//             Radius, Angle, Range, Width, BaseDamage, ProjSpeed, DurationMs, TickMs, AimMoveSpeedMul, AimMaxMs
	CsvFile skill;
	if (!skill.Load((dir / "SkillTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : skill.Rows())
	{
		SkillData d{};
		std::string shape, targeting, weapon;
		if (!row.GetInt("Name", d.skillId, err) || !row.GetString("WeaponType", weapon, err) ||
			!row.GetInt("Slot", d.slot, err) || !row.GetString("TargetingType", targeting, err) ||
			!row.GetString("HitShape", shape, err) || !row.GetInt("HitDelayMs", d.hitDelayMs, err) ||
			!row.GetInt("CooldownMs", d.cooldownMs, err) || !row.GetInt("AnimMs", d.animMs, err) ||
			!row.GetFloat("CastRange", d.castRange, err) ||
			!row.GetFloat("Radius", d.radius, err) || !row.GetFloat("Angle", d.angle, err) ||
			!row.GetFloat("Range", d.range, err) || !row.GetFloat("Width", d.width, err) ||
			!row.GetInt("BaseDamage", d.baseDamage, err) || !row.GetFloat("ProjSpeed", d.projSpeed, err) ||
			!row.GetInt("DurationMs", d.durationMs, err) || !row.GetInt("TickMs", d.tickMs, err) ||
			!row.GetFloat("AimMoveSpeedMul", d.aimMoveSpeedMul, err) || !row.GetInt("AimMaxMs", d.aimMaxMs, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}

		const std::string where = skill.Path() + " line " + std::to_string(row.Line()) + ": ";

		if (!ParseWeaponType(weapon, d.weaponType) || d.weaponType == WeaponType::None)
		{
			LOG_ERROR("Data", "{}WeaponType '{}' 는 Unarmed/Sword/Bow/Monster 중 하나여야 합니다", where, weapon);
			return false;
		}
		// skillId 첫째 자리 = 무기 종류 (맨손 1xxx, 검 2xxx, 활 3xxx, 몬스터 9xxx)
		static const std::unordered_map<WeaponType, int> kIdPrefix = {
			{ WeaponType::Unarmed, 1 }, { WeaponType::Sword, 2 }, { WeaponType::Bow, 3 }, { WeaponType::Monster, 9 } };
		if (d.skillId < 1000 || d.skillId > 9999 || d.skillId / 1000 != kIdPrefix.at(d.weaponType))
		{
			LOG_ERROR("Data", "{}skillId {} 는 4자리이고 첫째 자리가 무기 종류(Unarmed 1, Sword 2, Bow 3, Monster 9)여야 합니다", where, d.skillId);
			return false;
		}
		if (!ParseHitShape(shape, d.hitShape))
		{
			LOG_ERROR("Data", "{}HitShape '{}' 는 Sector/Line/Projectile/Zone 중 하나여야 합니다", where, shape);
			return false;
		}
		if (targeting == "Targeted") d.targeting = SkillTargeting::Targeted;
		else if (targeting == "NonTargeted") d.targeting = SkillTargeting::NonTargeted;
		else
		{
			LOG_ERROR("Data", "{}TargetingType '{}' 는 Targeted/NonTargeted 중 하나여야 합니다", where, targeting);
			return false;
		}

		if (d.hitDelayMs < 0 || d.cooldownMs < 0 || d.animMs <= 0 || (d.slot > 0 && d.cooldownMs <= 0) || d.baseDamage < 0 || d.slot < 0 || d.slot > 5 ||
			d.castRange < 0.f || d.radius < 0.f || d.angle < 0.f || d.range < 0.f || d.width < 0.f || d.projSpeed < 0.f ||
			d.durationMs < 0 || d.tickMs < 0)
		{
			LOG_ERROR("Data", "{}값이 허용 범위를 벗어났습니다 (Slot 0~5, 나머지 음수 불가, AnimMs>0, 스킬(Slot 1~5)은 CooldownMs>0)", where);
			return false;
		}
		if (!(d.aimMoveSpeedMul > 0.f && d.aimMoveSpeedMul <= 1.f) || d.aimMaxMs < 0 || d.aimMaxMs > 60000 || (d.slot == 0 && d.aimMaxMs > 0))
		{
			LOG_ERROR("Data", "{}조준 값이 허용 범위를 벗어났습니다 (0 < AimMoveSpeedMul <= 1, 0 <= AimMaxMs <= 60000, 기본 공격(Slot 0)은 AimMaxMs = 0)", where);
			return false;
		}
		if (d.targeting == SkillTargeting::Targeted && d.castRange <= 0.f)
		{
			LOG_ERROR("Data", "{}Targeted 스킬은 CastRange > 0 이어야 합니다", where);
			return false;
		}
		if (d.hitShape == HitShape::Zone && (d.castRange <= 0.f || d.radius <= 0.f || d.durationMs <= 0 || d.tickMs <= 0))
		{
			LOG_ERROR("Data", "{}Zone 스킬은 CastRange, Radius, DurationMs, TickMs 가 모두 0보다 커야 합니다", where);
			return false;
		}
		if (_skillTable.count(d.skillId))
		{
			LOG_ERROR("Data", "{}skillId {} 가 중복되었습니다", where, d.skillId);
			return false;
		}
		_skillTable[d.skillId] = d;

		// 몬스터 스킬은 MonsterTable.SkillId로 찾으므로 슬롯 색인에 넣지 않는다(몬스터 종류마다 슬롯 0이 따로 있을 수 있다).
		if (d.weaponType == WeaponType::Monster) continue;

		const int slotKey = static_cast<int>(d.weaponType) * 100 + d.slot;
		if (_skillBySlot.count(slotKey))
		{
			LOG_ERROR("Data", "{}WeaponType {} 의 Slot {} 이 중복되었습니다", where, weapon, d.slot);
			return false;
		}
		_skillBySlot[slotKey] = d.skillId;
	}
	// 무기가 없는 플레이어도 기본 공격은 할 수 있어야 한다.
	if (GetSkillBySlot(WeaponType::Unarmed, 0) == nullptr)
	{
		LOG_ERROR("Data", "{}: 맨손(Unarmed) 기본 공격(Slot 0) 행이 없습니다", skill.Path());
		return false;
	}
	// MonsterTable: Name(=monsterTemplateId, 1001~), SkillId, MaxHp, AttackDamage, AttackSpeed, MoveSpeed,
	//               RespawnSec, DeathRemoveDelaySec, HitStunMs, WanderRadiusCm
	CsvFile monster;
	if (!monster.Load((dir / "MonsterTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : monster.Rows())
	{
		MonsterData d{};
		int respawnSec = 0, removeDelaySec = 0, hitStunMs = 0, wanderRadiusCm = 0;
		if (!row.GetInt("Name", d.templateId, err) || !row.GetInt("SkillId", d.skillId, err) ||
			!ReadStat(monster, row, d.stat, err) ||
			!row.GetInt("RespawnSec", respawnSec, err) ||
			!row.GetInt("DeathRemoveDelaySec", removeDelaySec, err) || !row.GetInt("HitStunMs", hitStunMs, err) ||
			!row.GetInt("WanderRadiusCm", wanderRadiusCm, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (respawnSec < 0 || removeDelaySec < 0 || removeDelaySec > respawnSec || hitStunMs < 0 || hitStunMs > 10000 || wanderRadiusCm < 0 || wanderRadiusCm > 10000)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (0 <= DeathRemoveDelaySec <= RespawnSec, 0 <= HitStunMs <= 10000, 0 <= WanderRadiusCm <= 10000)", monster.Path(), row.Line());
			return false;
		}
		if (_monsterTable.count(d.templateId))
		{
			LOG_ERROR("Data", "{} line {}: monsterTemplateId {} 가 중복되었습니다", monster.Path(), row.Line(), d.templateId);
			return false;
		}
		// 테이블 ID 규칙(1001~). 옛 PlayerType 값(3)을 그대로 쓰면 여기서 걸린다.
		if (d.templateId < 1001)
		{
			LOG_ERROR("Data", "{} line {}: monsterTemplateId {} 는 1001 이상이어야 합니다", monster.Path(), row.Line(), d.templateId);
			return false;
		}
		const SkillData* monsterSkill = GetSkillData(d.skillId);
		if (monsterSkill == nullptr || monsterSkill->weaponType != WeaponType::Monster)
		{
			LOG_ERROR("Data", "{} line {}: SkillId {} 가 SkillTable에 없거나 WeaponType이 Monster가 아닙니다", monster.Path(), row.Line(), d.skillId);
			return false;
		}
		d.respawnMs = respawnSec * 1000;
		d.deathRemoveDelayMs = removeDelaySec * 1000;
		d.hitStunMs = hitStunMs;
		d.wanderRadiusCm = wanderRadiusCm;
		_monsterTable[d.templateId] = d;
	}

	// ItemTable: Name(=itemId), Slot, Grade, Tier, WeaponType, BonusAttackDamage, BonusMaxHp, Starter
	// (DisplayName, Icon, MeshM, MeshF, StaticMesh 는 클라이언트 전용이라 읽지 않는다)
	CsvFile item;
	if (!item.Load((dir / "ItemTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : item.Rows())
	{
		ItemData d{};
		std::string slot, grade, tier, weapon;
		int starter = 0;
		if (!row.GetInt("Name", d.itemId, err) || !row.GetString("Slot", slot, err) ||
			!row.GetString("Grade", grade, err) || !row.GetString("Tier", tier, err) ||
			!row.GetString("WeaponType", weapon, err) ||
			!row.GetInt("BonusAttackDamage", d.bonusAttackDamage, err) || !row.GetInt("BonusMaxHp", d.bonusMaxHp, err) ||
			!row.GetInt("Starter", starter, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}

		const std::string where = item.Path() + " line " + std::to_string(row.Line()) + ": ";

		if (starter != 0 && starter != 1)
		{
			LOG_ERROR("Data", "{}Starter 는 0 또는 1이어야 합니다 ({})", where, starter);
			return false;
		}
		d.starter = (starter == 1);

		if (!ParseItemSlot(slot, d.slot))
		{
			LOG_ERROR("Data", "{}Slot '{}' 는 Head/Top/Bottom/Gloves/Boots/Weapon/Offhand 중 하나여야 합니다", where, slot);
			return false;
		}
		if (!ParseItemGrade(grade, d.grade))
		{
			LOG_ERROR("Data", "{}Grade '{}' 는 Common/Uncommon/Rare 중 하나여야 합니다", where, grade);
			return false;
		}
		if (!ParseItemTier(tier, d.tier))
		{
			LOG_ERROR("Data", "{}Tier '{}' 는 Cloth/Leather/Plate 중 하나여야 합니다", where, tier);
			return false;
		}
		if (!ParseWeaponType(weapon, d.weaponType))
		{
			LOG_ERROR("Data", "{}WeaponType '{}' 를 알 수 없습니다", where, weapon);
			return false;
		}
		// itemId = 슬롯(1자리) + 티어(1자리) + 일련번호(01~99)
		if (d.itemId < 1000 || d.itemId > 9999 || d.itemId / 1000 != static_cast<int>(d.slot) ||
			(d.itemId / 100) % 10 != static_cast<int>(d.tier) || d.itemId % 100 == 0)
		{
			LOG_ERROR("Data", "{}itemId {} 는 4자리 [슬롯 번호][티어 번호][일련번호 01~99] 여야 합니다 (Slot {} = {}, Tier {} = {})",
				where, d.itemId, slot, static_cast<int>(d.slot), tier, static_cast<int>(d.tier));
			return false;
		}
		// 무기는 Sword/Bow + 공격력 보너스만, 방어구는 None + 최대 HP 보너스만 (Docs/DESIGN_CHARACTER_EQUIPMENT.md)
		const bool isWeapon = d.slot == ItemSlot::Weapon;
		if (isWeapon && d.weaponType != WeaponType::Sword && d.weaponType != WeaponType::Bow)
		{
			LOG_ERROR("Data", "{}무기의 WeaponType은 Sword/Bow 중 하나여야 합니다 ('{}')", where, weapon);
			return false;
		}
		if (!isWeapon && d.weaponType != WeaponType::None)
		{
			LOG_ERROR("Data", "{}무기가 아닌 아이템의 WeaponType은 None이어야 합니다 ('{}')", where, weapon);
			return false;
		}
		if (d.bonusAttackDamage < 0 || d.bonusMaxHp < 0 || (isWeapon && d.bonusMaxHp != 0) || (!isWeapon && d.bonusAttackDamage != 0))
		{
			LOG_ERROR("Data", "{}보너스가 허용 범위를 벗어났습니다 (음수 불가, 무기는 BonusAttackDamage만, 방어구는 BonusMaxHp만)", where);
			return false;
		}
		if (_itemTable.count(d.itemId))
		{
			LOG_ERROR("Data", "{}itemId {} 가 중복되었습니다", where, d.itemId);
			return false;
		}
		_itemTable[d.itemId] = d;
	}

	// 기본 장비: 슬롯당 최대 1개 (가입 시 Player_Equipped에 그대로 넣는다)
	_starterItems.clear();
	{
		std::map<int, int> starterBySlot;   // 슬롯 -> itemId (슬롯 순서로 정렬)
		for (const auto& [itemId, d] : _itemTable)
		{
			if (!d.starter) continue;
			const int slotNo = static_cast<int>(d.slot);
			if (starterBySlot.count(slotNo))
			{
				LOG_ERROR("Data", "{}: 슬롯 {} 의 Starter 아이템이 둘 이상입니다 ({}, {})", item.Path(), slotNo, starterBySlot[slotNo], itemId);
				return false;
			}
			starterBySlot[slotNo] = itemId;
		}
		// 보조손(방패)은 한손검 전용이라, 기본 보조손이 있으면 기본 무기도 Sword여야 한다 (MC4.2f)
		if (starterBySlot.count(static_cast<int>(ItemSlot::Offhand)))
		{
			const auto weaponIt = starterBySlot.find(static_cast<int>(ItemSlot::Weapon));
			if (weaponIt == starterBySlot.end() || _itemTable.at(weaponIt->second).weaponType != WeaponType::Sword)
			{
				LOG_ERROR("Data", "{}: Starter 보조손 아이템 {} 은 Starter 무기가 Sword일 때만 둘 수 있습니다", item.Path(), starterBySlot[static_cast<int>(ItemSlot::Offhand)]);
				return false;
			}
		}
		for (const auto& [slotNo, itemId] : starterBySlot)
			_starterItems.push_back(&_itemTable.at(itemId));
	}

	// MovementTable: Name(=movementId), ServerSpeedTolerance, ServerMoveBudgetMs, ServerMoveSlackCm, ServerMinMoveIntervalMs, ServerNavToleranceCm,
	// ServerWeaponSwapLockMs, ServerEquipMinIntervalMs, ServerAimMinIntervalMs
	// (나머지 열은 클라이언트 전용이라 읽지 않는다)
	CsvFile movement;
	if (!movement.Load((dir / "MovementTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : movement.Rows())
	{
		MovementData d{};
		if (!row.GetInt("Name", d.id, err) || !row.GetFloat("ServerSpeedTolerance", d.speedTolerance, err) ||
			!row.GetInt("ServerMoveBudgetMs", d.moveBudgetMs, err) || !row.GetFloat("ServerMoveSlackCm", d.moveSlackCm, err) ||
			!row.GetInt("ServerMinMoveIntervalMs", d.minMoveIntervalMs, err) || !row.GetFloat("ServerNavToleranceCm", d.navToleranceCm, err) ||
			!row.GetInt("ServerWeaponSwapLockMs", d.weaponSwapLockMs, err) || !row.GetInt("ServerEquipMinIntervalMs", d.equipMinIntervalMs, err) ||
			!row.GetInt("ServerAimMinIntervalMs", d.aimMinIntervalMs, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (d.speedTolerance < 1.f || d.speedTolerance > 3.f || d.moveBudgetMs <= 0 || d.moveBudgetMs > 10000 ||
			d.moveSlackCm < 0.f || d.minMoveIntervalMs < 0 || d.minMoveIntervalMs > 1000 || d.navToleranceCm < 0.f || d.navToleranceCm > 100.f ||
			d.weaponSwapLockMs < 0 || d.weaponSwapLockMs > 5000 || d.equipMinIntervalMs < 0 || d.equipMinIntervalMs > 5000 ||
			d.aimMinIntervalMs < 0 || d.aimMinIntervalMs > 5000)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (1 <= ServerSpeedTolerance <= 3, 0 < ServerMoveBudgetMs <= 10000, ServerMoveSlackCm >= 0, 0 <= ServerMinMoveIntervalMs <= 1000, 0 <= ServerNavToleranceCm <= 100, 0 <= ServerWeaponSwapLockMs <= 5000, 0 <= ServerEquipMinIntervalMs <= 5000, 0 <= ServerAimMinIntervalMs <= 5000)", movement.Path(), row.Line());
			return false;
		}
		if (_movementTable.count(d.id))
		{
			LOG_ERROR("Data", "{} line {}: movementId {} 가 중복되었습니다", movement.Path(), row.Line(), d.id);
			return false;
		}
		_movementTable[d.id] = d;
	}
	if (_movementTable.count(PLAYER_MOVEMENT_ID) == 0)
	{
		LOG_ERROR("Data", "{}: 플레이어 기본 행 {} 이 없습니다", movement.Path(), PLAYER_MOVEMENT_ID);
		return false;
	}

	// WorldTable: Name(=worldId), MapName, CellSizeCm, QuadSplitCount, QuadMinSizeCm, ViewRadiusCm, ViewHysteresisCm
	CsvFile world;
	if (!world.Load((dir / "WorldTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	std::unordered_set<int> worldIds;
	for (const CsvRow& row : world.Rows())
	{
		WorldData d{};
		if (!row.GetInt("Name", d.id, err) || !row.GetString("MapName", d.mapName, err) ||
			!row.GetInt("CellSizeCm", d.cellSizeCm, err) || !row.GetInt("QuadSplitCount", d.quadSplitCount, err) ||
			!row.GetInt("QuadMinSizeCm", d.quadMinSizeCm, err) ||
			!row.GetInt("ViewRadiusCm", d.viewRadiusCm, err) || !row.GetInt("ViewHysteresisCm", d.viewHysteresisCm, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (d.mapName.empty() || d.cellSizeCm < 100 || d.cellSizeCm > 100000 || d.quadSplitCount < 1 || d.quadSplitCount > 1000 || d.quadMinSizeCm < 100 || d.quadMinSizeCm > d.cellSizeCm ||
			d.viewRadiusCm < 100 || d.viewRadiusCm > 100000 || d.viewHysteresisCm < 0 || d.viewHysteresisCm > d.viewRadiusCm)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (MapName 필수, 100 <= CellSizeCm <= 100000, 1 <= QuadSplitCount <= 1000, 100 <= QuadMinSizeCm <= CellSizeCm, 100 <= ViewRadiusCm <= 100000, 0 <= ViewHysteresisCm <= ViewRadiusCm)", world.Path(), row.Line());
			return false;
		}
		if (!worldIds.insert(d.id).second || _worldTable.count(d.mapName))
		{
			LOG_ERROR("Data", "{} line {}: worldId {} 또는 MapName '{}' 가 중복되었습니다", world.Path(), row.Line(), d.id, d.mapName);
			return false;
		}
		_worldTable[d.mapName] = d;
	}

	LOG_INFO("Data", "로드 완료 (PlayerStatTable {}행, SkillTable {}행, MonsterTable {}행, ItemTable {}행, MovementTable {}행, WorldTable {}행)",
		_playerStatTable.size(), _skillTable.size(), _monsterTable.size(), _itemTable.size(), _movementTable.size(), _worldTable.size());
	return true;
}
