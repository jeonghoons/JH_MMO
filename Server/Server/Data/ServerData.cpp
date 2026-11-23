#include "pch.h"
#include "Data/ServerData.h"
#include "Utils/CsvFile.h"
#include <filesystem>
#include <map>
#include <set>
#include <unordered_set>

namespace
{
	namespace fs = std::filesystem;

	// 장착할 수 없는 아이템(Slot None)의 itemId 첫째 자리 (A3.2)
	constexpr int ITEM_ID_PREFIX_NO_SLOT = 8;

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
		if (text == "Pierce") { out = HitShape::Pierce; return true; }
		return false;
	}

	// None은 ItemTable(무기가 아닌 아이템)에서만 쓴다.
	bool ParseWeaponType(const std::string& text, WeaponType& out)
	{
		if (text == "None") { out = WeaponType::None; return true; }
		if (text == "Unarmed") { out = WeaponType::Unarmed; return true; }
		if (text == "Sword") { out = WeaponType::Sword; return true; }
		if (text == "Bow") { out = WeaponType::Bow; return true; }
		if (text == "TwoHandSword") { out = WeaponType::TwoHandSword; return true; }
		if (text == "Monster") { out = WeaponType::Monster; return true; }
		return false;
	}

	// None = 장착할 수 없는 아이템(소비 아이템, A3.2)
	bool ParseItemSlot(const std::string& text, ItemSlot& out)
	{
		if (text == "None") { out = ItemSlot::None; return true; }
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
		if (text == "None") { out = ItemTier::None; return true; }
		if (text == "Cloth") { out = ItemTier::Cloth; return true; }
		if (text == "Leather") { out = ItemTier::Leather; return true; }
		if (text == "Plate") { out = ItemTier::Plate; return true; }
		if (text == "Ornate") { out = ItemTier::Ornate; return true; }
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

	bool ParseItemUseEffect(const std::string& text, ItemUseEffect& out)
	{
		if (text == "None") { out = ItemUseEffect::None; return true; }
		if (text == "HealHp") { out = ItemUseEffect::HealHp; return true; }
		if (text == "HealMp") { out = ItemUseEffect::HealMp; return true; }
		return false;
	}

	bool ParseItemGrade(const std::string& text, ItemGrade& out)
	{
		if (text == "Common") { out = ItemGrade::Common; return true; }
		if (text == "Uncommon") { out = ItemGrade::Uncommon; return true; }
		if (text == "Rare") { out = ItemGrade::Rare; return true; }
		if (text == "Epic") { out = ItemGrade::Epic; return true; }
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
	_dropTable.clear();
	_spawnTable.clear();
	_questTable.clear();
	_economy = EconomyData{};

	fs::path dir;
	if (!FindDataDir(dir))
	{
		LOG_ERROR("Data", "Common/DataTable 폴더를 찾을 수 없습니다. 환경변수 JH_DATA_DIR로 경로를 지정하세요.");
		return false;
	}
	LOG_INFO("Data", "DataTable 경로: {}", dir.string());

	std::string err;

	// PlayerStatTable: Name, Level, MaxHp, AttackDamage, AttackSpeed, MoveSpeed, MaxMp, ExpToNext (체형 공용, 조회 키 = Level)
	CsvFile stat;
	if (!stat.Load((dir / "PlayerStatTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : stat.Rows())
	{
		int id = 0, level = 0;
		StatData d{};
		if (!row.GetInt("Name", id, err) || !row.GetInt("Level", level, err) || !ReadStat(stat, row, d, err) ||
			!row.GetInt("MaxMp", d.maxMp, err) || !row.GetInt("ExpToNext", d.expToNext, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (level < 1)
		{
			LOG_ERROR("Data", "{} line {}: Level 은 1 이상이어야 합니다", stat.Path(), row.Line());
			return false;
		}
		if (d.maxMp <= 0 || d.expToNext < 0)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (MaxMp>0, ExpToNext>=0)", stat.Path(), row.Line());
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
	// 레벨은 1부터 빠짐없이 이어져야 하고(경험치로 한 칸씩 오른다), 마지막 행 = 최대 레벨만 ExpToNext 0이다 (A2.3b, D-251·D-253)
	_playerMaxLevel = static_cast<int>(_playerStatTable.size());
	for (int level = PLAYER_START_LEVEL; level <= _playerMaxLevel; ++level)
	{
		const StatData* row = GetPlayerStatData(level);
		if (row == nullptr)
		{
			LOG_ERROR("Data", "{}: Level 이 1부터 이어지지 않습니다 (Level {} 행이 없음)", stat.Path(), level);
			return false;
		}
		const bool isMax = (level == _playerMaxLevel);
		if (isMax ? row->expToNext != 0 : row->expToNext <= 0)
		{
			LOG_ERROR("Data", "{}: Level {} 의 ExpToNext {} — 최대 레벨({}) 행만 0이고 나머지는 0보다 커야 합니다", stat.Path(), level, row->expToNext, _playerMaxLevel);
			return false;
		}
	}

	// SkillTable: Name(=skillId), WeaponType, Slot, TargetingType, HitShape, HitDelayMs, CooldownMs, AnimMs, CastRange,
	//             Radius, Angle, Range, Width, BaseDamage, ProjSpeed, DurationMs, TickMs, AimMoveSpeedMul, AimMaxMs, MpCost, AttackScale
	// (Fx·FxBaseRadius·HitFx·Montage·Icon은 클라이언트 전용이라 읽지 않는다)
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
			!row.GetInt("MoveLockMs", d.moveLockMs, err) ||
			!row.GetFloat("CastRange", d.castRange, err) ||
			!row.GetFloat("Radius", d.radius, err) || !row.GetFloat("Angle", d.angle, err) ||
			!row.GetFloat("Range", d.range, err) || !row.GetFloat("Width", d.width, err) ||
			!row.GetInt("BaseDamage", d.baseDamage, err) || !row.GetFloat("ProjSpeed", d.projSpeed, err) ||
			!row.GetInt("DurationMs", d.durationMs, err) || !row.GetInt("TickMs", d.tickMs, err) ||
			!row.GetFloat("AimMoveSpeedMul", d.aimMoveSpeedMul, err) || !row.GetInt("AimMaxMs", d.aimMaxMs, err) ||
			!row.GetInt("MpCost", d.mpCost, err) || !row.GetInt("AttackScale", d.attackScale, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}

		const std::string where = skill.Path() + " line " + std::to_string(row.Line()) + ": ";

		// MP·공격력 반영(A2.3b, D-253): 몬스터 스킬의 MpCost는 무시하지만(몬스터는 MP 없음) 범위는 같게 본다
		if (d.mpCost < 0 || d.attackScale < 0 || d.attackScale > 1000)
		{
			LOG_ERROR("Data", "{}값이 허용 범위를 벗어났습니다 (MpCost>=0, 0 <= AttackScale <= 1000)", where);
			return false;
		}

		if (!ParseWeaponType(weapon, d.weaponType) || d.weaponType == WeaponType::None)
		{
			LOG_ERROR("Data", "{}WeaponType '{}' 는 Unarmed/Sword/Bow/TwoHandSword/Monster 중 하나여야 합니다", where, weapon);
			return false;
		}
		// skillId 첫째 자리 = 무기 종류 (맨손 1xxx, 검 2xxx, 활 3xxx, 몬스터 9xxx)
		static const std::unordered_map<WeaponType, int> kIdPrefix = {
			{ WeaponType::Unarmed, 1 }, { WeaponType::Sword, 2 }, { WeaponType::Bow, 3 }, { WeaponType::TwoHandSword, 4 }, { WeaponType::Monster, 9 } };
		if (d.skillId < 1000 || d.skillId > 9999 || d.skillId / 1000 != kIdPrefix.at(d.weaponType))
		{
			LOG_ERROR("Data", "{}skillId {} 는 4자리이고 첫째 자리가 무기 종류(Unarmed 1, Sword 2, Bow 3, TwoHandSword 4, Monster 9)여야 합니다", where, d.skillId);
			return false;
		}
		if (!ParseHitShape(shape, d.hitShape))
		{
			LOG_ERROR("Data", "{}HitShape '{}' 는 Sector/Line/Projectile/Zone/Pierce 중 하나여야 합니다", where, shape);
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
			d.durationMs < 0 || d.tickMs < 0 || d.moveLockMs < 0 || d.moveLockMs > d.animMs)
		{
			LOG_ERROR("Data", "{}값이 허용 범위를 벗어났습니다 (Slot 0~5, 나머지 음수 불가, AnimMs>0, 스킬(Slot 1~5)은 CooldownMs>0, 0 <= MoveLockMs <= AnimMs)", where);
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
		if (d.hitShape == HitShape::Pierce && (d.targeting != SkillTargeting::NonTargeted || d.range <= 0.f || d.width <= 0.f || d.projSpeed <= 0.f))
		{
			LOG_ERROR("Data", "{}Pierce 스킬은 NonTargeted 이고 Range, Width, ProjSpeed 가 모두 0보다 커야 합니다", where);
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
	//               RespawnSec, DeathRemoveDelaySec, HitStunMs, WanderRadiusCm, AggroRangeCm, LeashRangeCm, ReturnMode, Scale, BodyRadiusCm, Level, Exp
	// (Mesh 등 외형 열은 클라이언트 전용이라 읽지 않는다)
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
			!row.GetInt("WanderRadiusCm", wanderRadiusCm, err) ||
			!row.GetInt("AggroRangeCm", d.aggroRangeCm, err) || !row.GetInt("LeashRangeCm", d.leashRangeCm, err) ||
			!row.GetInt("ReturnMode", d.returnMode, err) ||
			!row.GetFloat("Scale", d.scale, err) || !row.GetInt("BodyRadiusCm", d.bodyRadiusCm, err) ||
			!row.GetInt("Level", d.level, err) || !row.GetInt("Exp", d.exp, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		// 표시 레벨·처치 경험치(A2.3b, D-253)
		if (d.level < 1 || d.exp < 0)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (Level>=1, Exp>=0)", monster.Path(), row.Line());
			return false;
		}
		// 행동 범위(L3, D-193): 인식 범위 0 = 비선공. 추격 제한은 홈 기준이라 배회 반경보다 좁으면 배회만으로 복귀에 걸린다.
		if (d.aggroRangeCm < 0 || d.aggroRangeCm > 5000 || d.leashRangeCm <= 0 || d.leashRangeCm > 10000 || d.leashRangeCm < wanderRadiusCm ||
			(d.returnMode != MONSTER_RETURN_WALK && d.returnMode != MONSTER_RETURN_EVADE))
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (0 <= AggroRangeCm <= 5000, 0 < LeashRangeCm <= 10000, LeashRangeCm >= WanderRadiusCm, ReturnMode 0 또는 1)", monster.Path(), row.Line());
			return false;
		}
		if (respawnSec < 0 || removeDelaySec < 0 || removeDelaySec > respawnSec || hitStunMs < 0 || hitStunMs > 10000 || wanderRadiusCm < 0 || wanderRadiusCm > 10000)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (0 <= DeathRemoveDelaySec <= RespawnSec, 0 <= HitStunMs <= 10000, 0 <= WanderRadiusCm <= 10000)", monster.Path(), row.Line());
			return false;
		}
		// 크기(MN1, D-198): 배율은 클라이언트가 그리는 크기, 몸 반경은 서버 판정에 더하는 값이라 둘을 함께 맞춘다.
		if (d.scale < 0.3f || d.scale > 3.f || d.bodyRadiusCm < 0 || d.bodyRadiusCm > 500)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (0.3 <= Scale <= 3, 0 <= BodyRadiusCm <= 500)", monster.Path(), row.Line());
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

	// ItemTable: Name(=itemId), Slot, Grade, Tier, WeaponType, BonusAttackDamage, BonusMaxHp, Starter,
	//            RequiredLevel, MaxStack, UseEffect, UseValue, UseCooldownMs, SellPrice
	//            + DisplayName(경매 이름 검색용으로만 — D-281)
	// (Icon, MeshM, MeshF, StaticMesh 등 외형 열은 클라이언트 전용이라 읽지 않는다)
	CsvFile item;
	if (!item.Load((dir / "ItemTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : item.Rows())
	{
		ItemData d{};
		std::string slot, grade, tier, weapon, useEffect, displayName;
		int starter = 0;
		if (!row.GetInt("Name", d.itemId, err) || !row.GetString("DisplayName", displayName, err) || !row.GetString("Slot", slot, err) ||
			!row.GetString("Grade", grade, err) || !row.GetString("Tier", tier, err) ||
			!row.GetString("WeaponType", weapon, err) ||
			!row.GetInt("BonusAttackDamage", d.bonusAttackDamage, err) || !row.GetInt("BonusMaxHp", d.bonusMaxHp, err) || !row.GetInt("BonusMaxMp", d.bonusMaxMp, err) ||
			!row.GetInt("Starter", starter, err) ||
			!row.GetInt("RequiredLevel", d.requiredLevel, err) || !row.GetInt("MaxStack", d.maxStack, err) ||
			!row.GetString("UseEffect", useEffect, err) || !row.GetInt("UseValue", d.useValue, err) ||
			!row.GetInt("UseCooldownMs", d.useCooldownMs, err) || !row.GetInt("SellPrice", d.sellPrice, err))
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

		d.searchName = NormalizeSearchText(Utils::UTF8ToWString(displayName));
		if (d.searchName.empty())
		{
			LOG_ERROR("Data", "{}DisplayName 이 비어 있습니다 (경매 이름 검색에 씀)", where);
			return false;
		}

		if (!ParseItemSlot(slot, d.slot))
		{
			LOG_ERROR("Data", "{}Slot '{}' 는 None/Head/Top/Bottom/Gloves/Boots/Weapon/Offhand 중 하나여야 합니다", where, slot);
			return false;
		}
		if (!ParseItemGrade(grade, d.grade))
		{
			LOG_ERROR("Data", "{}Grade '{}' 는 Common/Uncommon/Rare/Epic 중 하나여야 합니다", where, grade);
			return false;
		}
		if (!ParseItemTier(tier, d.tier))
		{
			LOG_ERROR("Data", "{}Tier '{}' 는 None/Cloth/Leather/Plate/Ornate 중 하나여야 합니다", where, tier);
			return false;
		}
		if (!ParseWeaponType(weapon, d.weaponType))
		{
			LOG_ERROR("Data", "{}WeaponType '{}' 를 알 수 없습니다", where, weapon);
			return false;
		}
		if (!ParseItemUseEffect(useEffect, d.useEffect))
		{
			LOG_ERROR("Data", "{}UseEffect '{}' 는 None/HealHp/HealMp 중 하나여야 합니다", where, useEffect);
			return false;
		}
		// 장비는 티어가 있고, 장착할 수 없는 아이템(Slot None)은 티어가 없다.
		const bool isEquip = d.slot != ItemSlot::None;
		if (isEquip != (d.tier != ItemTier::None))
		{
			LOG_ERROR("Data", "{}Slot None 과 Tier None 은 함께 써야 합니다 (Slot '{}', Tier '{}')", where, slot, tier);
			return false;
		}
		// itemId = 슬롯(1자리) + 티어(1자리) + 일련번호(01~99). 장착할 수 없는 아이템은 첫째 자리 8, 둘째 자리 0
		const int slotDigit = isEquip ? static_cast<int>(d.slot) : ITEM_ID_PREFIX_NO_SLOT;
		if (d.itemId < 1000 || d.itemId > 9999 || d.itemId / 1000 != slotDigit ||
			(d.itemId / 100) % 10 != static_cast<int>(d.tier) || d.itemId % 100 == 0)
		{
			LOG_ERROR("Data", "{}itemId {} 는 4자리 [슬롯 번호][티어 번호][일련번호 01~99] 여야 합니다 (Slot {} = {}, Tier {} = {})",
				where, d.itemId, slot, slotDigit, tier, static_cast<int>(d.tier));
			return false;
		}
		// 무기는 Sword/Bow + 공격력 보너스만, 방어구는 None + 최대 HP 보너스만 (Docs/DESIGN_CHARACTER_EQUIPMENT.md)
		const bool isWeapon = d.slot == ItemSlot::Weapon;
		if (isWeapon && d.weaponType != WeaponType::Sword && d.weaponType != WeaponType::Bow && d.weaponType != WeaponType::TwoHandSword)
		{
			LOG_ERROR("Data", "{}무기의 WeaponType은 Sword/Bow/TwoHandSword 중 하나여야 합니다 ('{}')", where, weapon);
			return false;
		}
		if (!isWeapon && d.weaponType != WeaponType::None)
		{
			LOG_ERROR("Data", "{}무기가 아닌 아이템의 WeaponType은 None이어야 합니다 ('{}')", where, weapon);
			return false;
		}
		if (d.bonusAttackDamage < 0 || d.bonusMaxHp < 0 || d.bonusMaxMp < 0 || (isWeapon && (d.bonusMaxHp != 0 || d.bonusMaxMp != 0)) ||
			(d.bonusAttackDamage != 0 && !isWeapon && d.slot != ItemSlot::Gloves) || (d.bonusMaxMp != 0 && d.slot != ItemSlot::Head))
		{
			LOG_ERROR("Data", "{}보너스가 허용 범위를 벗어났습니다 (음수 불가, 무기는 BonusAttackDamage만, 장갑은 BonusAttackDamage, 투구는 BonusMaxMp, 나머지 방어구·방패는 BonusMaxHp만)", where);
			return false;
		}
		if (d.requiredLevel < 1 || d.maxStack < 1 || d.maxStack > 9999 || d.useValue < 0 || d.useCooldownMs < 0 || d.useCooldownMs > 600000 || d.sellPrice < 0)
		{
			LOG_ERROR("Data", "{}값이 허용 범위를 벗어났습니다 (RequiredLevel >= 1, 1 <= MaxStack <= 9999, UseValue >= 0, 0 <= UseCooldownMs <= 600000, SellPrice >= 0)", where);
			return false;
		}
		if (isEquip && (d.maxStack != 1 || d.useEffect != ItemUseEffect::None))
		{
			LOG_ERROR("Data", "{}장비는 MaxStack 1, UseEffect None 이어야 합니다 (MaxStack {}, UseEffect '{}')", where, d.maxStack, useEffect);
			return false;
		}
		if (!isEquip && (d.bonusAttackDamage != 0 || d.bonusMaxHp != 0 || d.bonusMaxMp != 0 || d.starter))
		{
			LOG_ERROR("Data", "{}장착할 수 없는 아이템(Slot None)은 보너스 0, Starter 0 이어야 합니다", where);
			return false;
		}
		if (!isEquip && d.useEffect == ItemUseEffect::None && (d.useValue != 0 || d.useCooldownMs != 0))
		{
			LOG_ERROR("Data", "{}전리품(UseEffect None)은 UseValue·UseCooldownMs가 0이어야 합니다", where);
			return false;
		}
		if ((d.useEffect == ItemUseEffect::HealHp || d.useEffect == ItemUseEffect::HealMp) && d.useValue <= 0)
		{
			LOG_ERROR("Data", "{}UseEffect HealHp 는 UseValue > 0 이어야 합니다 ({})", where, d.useValue);
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
	// ServerWeaponSwapLockMs, ServerEquipMinIntervalMs, ServerAimMinIntervalMs, ServerMoveLockSlackCm
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
			!row.GetInt("ServerAimMinIntervalMs", d.aimMinIntervalMs, err) || !row.GetFloat("ServerMoveLockSlackCm", d.moveLockSlackCm, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (d.speedTolerance < 1.f || d.speedTolerance > 3.f || d.moveBudgetMs <= 0 || d.moveBudgetMs > 10000 ||
			d.moveSlackCm < 0.f || d.minMoveIntervalMs < 0 || d.minMoveIntervalMs > 1000 || d.navToleranceCm < 0.f || d.navToleranceCm > 100.f ||
			d.weaponSwapLockMs < 0 || d.weaponSwapLockMs > 5000 || d.equipMinIntervalMs < 0 || d.equipMinIntervalMs > 5000 ||
			d.aimMinIntervalMs < 0 || d.aimMinIntervalMs > 5000 || d.moveLockSlackCm < 0.f || d.moveLockSlackCm > 1000.f)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (1 <= ServerSpeedTolerance <= 3, 0 < ServerMoveBudgetMs <= 10000, ServerMoveSlackCm >= 0, 0 <= ServerMinMoveIntervalMs <= 1000, 0 <= ServerNavToleranceCm <= 100, 0 <= ServerWeaponSwapLockMs <= 5000, 0 <= ServerEquipMinIntervalMs <= 5000, 0 <= ServerAimMinIntervalMs <= 5000, 0 <= ServerMoveLockSlackCm <= 1000)", movement.Path(), row.Line());
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

	// WorldTable: Name(=worldId), MapName, CellSizeCm, QuadSplitCount, QuadMinSizeCm, ViewRadiusCm, ViewHysteresisCm, ReviveDelaySec, ReviveHpPercent, ChannelCapacity, PortalCooldownMs, LevelKind, DungeonClearExitSec
	CsvFile world;
	if (!world.Load((dir / "WorldTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	std::unordered_set<int> worldIds;
	for (const CsvRow& row : world.Rows())
	{
		WorldData d{};
		std::string levelKind;
		if (!row.GetString("LevelKind", levelKind, err) || !row.GetInt("DungeonClearExitSec", d.dungeonClearExitSec, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (levelKind != "world" && levelKind != "dungeon")
		{
			LOG_ERROR("Data", "{} line {}: LevelKind 는 world 또는 dungeon 이어야 합니다 ('{}')", world.Path(), row.Line(), levelKind);
			return false;
		}
		d.isDungeon = (levelKind == "dungeon");
		if (d.isDungeon ? (d.dungeonClearExitSec < 1 || d.dungeonClearExitSec > 3600) : (d.dungeonClearExitSec != 0))
		{
			LOG_ERROR("Data", "{} line {}: DungeonClearExitSec 는 dungeon 행이면 1~3600, world 행이면 0 이어야 합니다", world.Path(), row.Line());
			return false;
		}
		if (!row.GetInt("Name", d.id, err) || !row.GetString("MapName", d.mapName, err) ||
			!row.GetInt("CellSizeCm", d.cellSizeCm, err) || !row.GetInt("QuadSplitCount", d.quadSplitCount, err) ||
			!row.GetInt("QuadMinSizeCm", d.quadMinSizeCm, err) ||
			!row.GetInt("ViewRadiusCm", d.viewRadiusCm, err) || !row.GetInt("ViewHysteresisCm", d.viewHysteresisCm, err) ||
			!row.GetInt("ReviveDelaySec", d.reviveDelaySec, err) || !row.GetInt("ReviveHpPercent", d.reviveHpPercent, err) ||
			!row.GetInt("ChannelCapacity", d.channelCapacity, err) || !row.GetInt("PortalCooldownMs", d.portalCooldownMs, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (d.mapName.empty() || d.cellSizeCm < 100 || d.cellSizeCm > 100000 || d.quadSplitCount < 1 || d.quadSplitCount > 1000 || d.quadMinSizeCm < 100 || d.quadMinSizeCm > d.cellSizeCm ||
			d.viewRadiusCm < 100 || d.viewRadiusCm > 100000 || d.viewHysteresisCm < 0 || d.viewHysteresisCm > d.viewRadiusCm ||
			d.reviveDelaySec < 0 || d.reviveDelaySec > 3600 || d.reviveHpPercent < 1 || d.reviveHpPercent > 100 ||
			d.channelCapacity < 1 || d.channelCapacity > 10000 || d.portalCooldownMs < 0 || d.portalCooldownMs > 60000)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (MapName 필수, 100 <= CellSizeCm <= 100000, 1 <= QuadSplitCount <= 1000, 100 <= QuadMinSizeCm <= CellSizeCm, 100 <= ViewRadiusCm <= 100000, 0 <= ViewHysteresisCm <= ViewRadiusCm, 0 <= ReviveDelaySec <= 3600, 1 <= ReviveHpPercent <= 100, 1 <= ChannelCapacity <= 10000, 0 <= PortalCooldownMs <= 60000)", world.Path(), row.Line());
			return false;
		}
		if (!worldIds.insert(d.id).second || _worldTable.count(d.mapName))
		{
			LOG_ERROR("Data", "{} line {}: worldId {} 또는 MapName '{}' 가 중복되었습니다", world.Path(), row.Line(), d.id, d.mapName);
			return false;
		}
		_worldTable[d.mapName] = d;
	}

	// EconomyTable(서버 전용): 행 1001 하나. 열과 허용 범위는 아래 표 (Docs/DESIGN_ITEMS_TRADE.md "EconomyTable")
	CsvFile economy;
	if (!economy.Load((dir / "EconomyTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }
	{
		EconomyData& e = _economy;
		struct Column { const char* name; int* value; int min; int max; };
		const Column columns[] = {
			{ "InventorySlots", &e.inventorySlots, 1, 200 },
			{ "ItemMinIntervalMs", &e.itemMinIntervalMs, 0, 5000 },
			{ "GoldMax", &e.goldMax, 1, 2000000000 },
			{ "DropOwnerSec", &e.dropOwnerSec, 0, 3600 },
			{ "DropLifeSec", &e.dropLifeSec, 1, 3600 },
			{ "DropScatterCm", &e.dropScatterCm, 0, 1000 },
			{ "PickupRangeCm", &e.pickupRangeCm, 1, 5000 },
			{ "MaxDropsPerRoom", &e.maxDropsPerRoom, 1, 10000 },
			{ "TradeRangeCm", &e.tradeRangeCm, 1, 5000 },
			{ "TradeRequestTimeoutSec", &e.tradeRequestTimeoutSec, 1, 600 },
			{ "TradeTimeoutSec", &e.tradeTimeoutSec, 1, 3600 },
			{ "TradeMaxItems", &e.tradeMaxItems, 1, 200 },
			{ "AuctionDurationSec", &e.auctionDurationSec, 1, 2592000 },
			{ "AuctionMaxListings", &e.auctionMaxListings, 1, 100 },
			{ "AuctionMinPrice", &e.auctionMinPrice, 1, 2000000000 },
			{ "AuctionMaxPrice", &e.auctionMaxPrice, 1, 2000000000 },
			{ "AuctionMailPageSize", &e.auctionMailPageSize, 1, 100 },
			{ "AuctionExpireCheckSec", &e.auctionExpireCheckSec, 1, 3600 },
			{ "AuctionPageRows", &e.auctionPageRows, 1, 50 },
			{ "AuctionFetchRows", &e.auctionFetchRows, 1, 100 },          // 한 줄 약 40바이트 — 100줄이어도 패킷 상한 8192 안
			{ "AuctionSearchIntervalMs", &e.auctionSearchIntervalMs, 0, 60000 },
			{ "AuctionActionIntervalMs", &e.auctionActionIntervalMs, 0, 60000 },
			{ "ShopRangeCm", &e.shopRangeCm, 1, 5000 },
			{ "PartyMaxMembers", &e.partyMaxMembers, 2, 100 },
			{ "PartyInviteTimeoutSec", &e.partyInviteTimeoutSec, 1, 600 },
			{ "CombatHoldMs", &e.combatHoldMs, 1, 60000 },
			{ "HpRegenIntervalSec", &e.hpRegenIntervalSec, 1, 600 },
			{ "HpRegenPercent", &e.hpRegenPercent, 1, 100 },
			{ "MpRegenPercentCombat", &e.mpRegenPercentCombat, 1, 100 },
			{ "MpRegenPercentIdle", &e.mpRegenPercentIdle, 1, 100 },
		};

		int economyId = 0;
		if (economy.Rows().size() != 1 || !economy.Rows()[0].GetInt("Name", economyId, err) || economyId != ECONOMY_ID)
		{
			LOG_ERROR("Data", "{}: 행은 {} 하나여야 합니다 ({}행)", economy.Path(), ECONOMY_ID, economy.Rows().size());
			return false;
		}
		const CsvRow& row = economy.Rows()[0];
		for (const Column& c : columns)
		{
			if (!row.GetInt(c.name, *c.value, err)) { LOG_ERROR("Data", "{}", err); return false; }
			if (*c.value < c.min || *c.value > c.max)
			{
				LOG_ERROR("Data", "{} line {}: {} = {} 가 허용 범위({}~{})를 벗어났습니다", economy.Path(), row.Line(), c.name, *c.value, c.min, c.max);
				return false;
			}
		}
		if (e.dropLifeSec < e.dropOwnerSec || e.tradeMaxItems > e.inventorySlots ||
			e.auctionMinPrice > e.auctionMaxPrice || e.auctionMaxPrice > e.goldMax ||
			e.auctionFetchRows < e.auctionPageRows || e.auctionFetchRows % e.auctionPageRows != 0)
		{
			LOG_ERROR("Data", "{} line {}: 값 사이의 관계가 맞지 않습니다 (DropLifeSec >= DropOwnerSec, TradeMaxItems <= InventorySlots, AuctionMinPrice <= AuctionMaxPrice <= GoldMax, AuctionFetchRows = AuctionPageRows의 배수)", economy.Path(), row.Line());
			return false;
		}
	}

	// DropTable(서버 전용): Name(=행 ID, 1001~), MonsterId, ItemId(0 = 골드), DropRate(만분율), MinCount, MaxCount
	CsvFile drop;
	if (!drop.Load((dir / "DropTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	size_t dropRows = 0;
	{
		std::unordered_set<int> dropIds;
		std::set<std::pair<int, int>> dropPairs;   // (MonsterId, ItemId)
		for (const CsvRow& row : drop.Rows())
		{
			DropData d{};
			if (!row.GetInt("Name", d.id, err) || !row.GetInt("MonsterId", d.monsterId, err) ||
				!row.GetInt("ItemId", d.itemId, err) || !row.GetInt("DropRate", d.dropRate, err) ||
				!row.GetInt("MinCount", d.minCount, err) || !row.GetInt("MaxCount", d.maxCount, err))
			{
				LOG_ERROR("Data", "{}", err);
				return false;
			}

			const std::string where = drop.Path() + " line " + std::to_string(row.Line()) + ": ";

			if (d.id < 1001 || !dropIds.insert(d.id).second)
			{
				LOG_ERROR("Data", "{}행 ID {} 는 1001 이상이고 중복되지 않아야 합니다", where, d.id);
				return false;
			}
			if (GetMonsterData(d.monsterId) == nullptr)
			{
				LOG_ERROR("Data", "{}MonsterId {} 가 MonsterTable에 없습니다", where, d.monsterId);
				return false;
			}
			const ItemData* dropItem = nullptr;
			if (d.itemId != 0 && (dropItem = GetItemData(d.itemId)) == nullptr)
			{
				LOG_ERROR("Data", "{}ItemId {} 가 ItemTable에 없습니다 (골드는 0)", where, d.itemId);
				return false;
			}
			if (d.dropRate < 0 || d.dropRate > 10000 || d.minCount < 1 || d.minCount > d.maxCount)
			{
				LOG_ERROR("Data", "{}값이 허용 범위를 벗어났습니다 (0 <= DropRate <= 10000, 1 <= MinCount <= MaxCount)", where);
				return false;
			}
			const int countLimit = dropItem != nullptr ? dropItem->maxStack : _economy.goldMax;
			if (d.maxCount > countLimit)
			{
				LOG_ERROR("Data", "{}MaxCount {} 가 상한 {} 을 넘습니다 (아이템은 ItemTable.MaxStack, 골드는 EconomyTable.GoldMax)", where, d.maxCount, countLimit);
				return false;
			}
			if (!dropPairs.insert({ d.monsterId, d.itemId }).second)
			{
				LOG_ERROR("Data", "{}(MonsterId {}, ItemId {}) 조합이 중복되었습니다", where, d.monsterId, d.itemId);
				return false;
			}
			_dropTable[d.monsterId].push_back(d);
			++dropRows;
		}
	}

	// SpawnTable(서버 전용): Name(=행 ID, 1001~), MapName, Zone, MonsterTemplateId, CountPerSpawn, RespawnSec(0 = MonsterTable 값)
	CsvFile spawn;
	if (!spawn.Load((dir / "SpawnTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	size_t spawnRows = 0;
	{
		std::unordered_set<int> spawnIds;
		std::set<std::pair<std::string, int>> spawnKeys;   // (MapName/Zone, MonsterTemplateId)
		for (const CsvRow& row : spawn.Rows())
		{
			SpawnData d{};
			int respawnSec = 0;
			if (!row.GetInt("Name", d.id, err) || !row.GetString("MapName", d.mapName, err) || !row.GetString("Zone", d.zone, err) ||
				!row.GetInt("MonsterTemplateId", d.monsterTemplateId, err) || !row.GetInt("CountPerSpawn", d.countPerSpawn, err) ||
				!row.GetInt("RespawnSec", respawnSec, err))
			{
				LOG_ERROR("Data", "{}", err);
				return false;
			}

			const std::string where = spawn.Path() + " line " + std::to_string(row.Line()) + ": ";

			if (d.id < 1001 || !spawnIds.insert(d.id).second)
			{
				LOG_ERROR("Data", "{}행 ID {} 는 1001 이상이고 중복되지 않아야 합니다", where, d.id);
				return false;
			}
			if (GetWorldData(d.mapName) == nullptr)
			{
				LOG_ERROR("Data", "{}MapName '{}' 가 WorldTable에 없습니다", where, d.mapName);
				return false;
			}
			if (d.zone.empty() || d.zone.find('/') != std::string::npos)
			{
				LOG_ERROR("Data", "{}Zone 은 비어 있지 않고 '/'를 쓰지 않아야 합니다", where);
				return false;
			}
			const MonsterData* spawnMonster = GetMonsterData(d.monsterTemplateId);
			if (spawnMonster == nullptr)
			{
				LOG_ERROR("Data", "{}MonsterTemplateId {} 가 MonsterTable에 없습니다", where, d.monsterTemplateId);
				return false;
			}
			if (d.countPerSpawn < 1 || d.countPerSpawn > 20 || respawnSec < -1 || respawnSec > 86400 ||
				(respawnSec > 0 && respawnSec * 1000 < spawnMonster->deathRemoveDelayMs))
			{
				LOG_ERROR("Data", "{}값이 허용 범위를 벗어났습니다 (1 <= CountPerSpawn <= 20, RespawnSec 은 -1(다시 나오지 않음), 0(MonsterTable 값) 또는 그 몬스터의 DeathRemoveDelaySec 이상 86400 이하)", where);
				return false;
			}
			const std::string key = d.mapName + "/" + d.zone;
			if (!spawnKeys.insert({ key, d.monsterTemplateId }).second)
			{
				LOG_ERROR("Data", "{}(MapName '{}', Zone '{}', MonsterTemplateId {}) 조합이 중복되었습니다", where, d.mapName, d.zone, d.monsterTemplateId);
				return false;
			}
			d.respawnMs = respawnSec < 0 ? -1 : respawnSec * 1000;
			_spawnTable[key].push_back(d);
			++spawnRows;
		}
	}

	// ShopTable(서버 전용, L4·D-201): Name(=행 ID, 1001~), ShopId, ItemId, BuyPrice
	CsvFile shop;
	if (!shop.Load((dir / "ShopTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	size_t shopRows = 0;
	{
		// 판매가는 골드 상한을 넘을 수 없다(EconomyTable을 ItemTable 뒤에 읽으므로 여기서 본다).
		for (const auto& [itemId, itemData] : _itemTable)
		{
			if (itemData.sellPrice > _economy.goldMax)
			{
				LOG_ERROR("Data", "{}: 아이템 {} 의 SellPrice {} 가 EconomyTable.GoldMax {} 를 넘습니다", item.Path(), itemId, itemData.sellPrice, _economy.goldMax);
				return false;
			}
		}

		std::unordered_set<int> shopIds;
		std::set<std::pair<int, int>> shopPairs;   // (ShopId, ItemId)
		for (const CsvRow& row : shop.Rows())
		{
			ShopData d{};
			if (!row.GetInt("Name", d.id, err) || !row.GetInt("ShopId", d.shopId, err) ||
				!row.GetInt("ItemId", d.itemId, err) || !row.GetInt("BuyPrice", d.buyPrice, err))
			{
				LOG_ERROR("Data", "{}", err);
				return false;
			}

			const std::string where = shop.Path() + " line " + std::to_string(row.Line()) + ": ";

			if (d.id < 1001 || !shopIds.insert(d.id).second)
			{
				LOG_ERROR("Data", "{}행 ID {} 는 1001 이상이고 중복되지 않아야 합니다", where, d.id);
				return false;
			}
			const ItemData* shopItem = GetItemData(d.itemId);
			if (shopItem == nullptr)
			{
				LOG_ERROR("Data", "{}ItemId {} 가 ItemTable에 없습니다", where, d.itemId);
				return false;
			}
			if (d.shopId < 1 || d.buyPrice < 1 || d.buyPrice > _economy.goldMax)
			{
				LOG_ERROR("Data", "{}값이 허용 범위를 벗어났습니다 (ShopId >= 1, 1 <= BuyPrice <= EconomyTable.GoldMax)", where);
				return false;
			}
			// 사서 되팔아 골드가 늘어나면 안 된다.
			if (d.buyPrice < shopItem->sellPrice)
			{
				LOG_ERROR("Data", "{}BuyPrice {} 가 아이템 {} 의 ItemTable.SellPrice {} 보다 작습니다 (사서 되팔면 골드가 늘어남)", where, d.buyPrice, d.itemId, shopItem->sellPrice);
				return false;
			}
			if (!shopPairs.insert({ d.shopId, d.itemId }).second)
			{
				LOG_ERROR("Data", "{}(ShopId {}, ItemId {}) 조합이 중복되었습니다", where, d.shopId, d.itemId);
				return false;
			}
			_shopTable[d.shopId].push_back(d);
			++shopRows;
		}
	}

	// NpcTable(서버 전용, L4·D-202): Name(=npcTemplateId, 1001~), DisplayName, ShopId(0 = 상점 없음), Auction(1 = 경매 관리인, A4.3·D-234), Body(Male/Female),
	//                               Head, Top, Bottom, Gloves, Boots, Weapon, Offhand (장착 외형 itemId, 0 = 없음)
	CsvFile npc;
	if (!npc.Load((dir / "NpcTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }
	{
		static const char* const kEquipColumns[NPC_EQUIP_SLOTS] = { "Head", "Top", "Bottom", "Gloves", "Boots", "Weapon", "Offhand" };
		for (const CsvRow& row : npc.Rows())
		{
			NpcData d{};
			std::string body;
			if (!row.GetInt("Name", d.templateId, err) || !row.GetString("DisplayName", d.displayName, err) ||
				!row.GetInt("ShopId", d.shopId, err) || !row.GetInt("Auction", d.auction, err) || !row.GetString("Body", body, err))
			{
				LOG_ERROR("Data", "{}", err);
				return false;
			}

			const std::string where = npc.Path() + " line " + std::to_string(row.Line()) + ": ";

			if (d.templateId < 1001 || _npcTable.count(d.templateId))
			{
				LOG_ERROR("Data", "{}종류 ID {} 는 1001 이상이고 중복되지 않아야 합니다", where, d.templateId);
				return false;
			}
			if (d.displayName.empty())
			{
				LOG_ERROR("Data", "{}DisplayName 이 비어 있습니다", where);
				return false;
			}
			if (d.shopId < 0 || (d.shopId != 0 && _shopTable.find(d.shopId) == _shopTable.end()))
			{
				LOG_ERROR("Data", "{}ShopId {} 가 ShopTable에 없습니다 (상점이 없으면 0)", where, d.shopId);
				return false;
			}
			// 한 NPC는 상점과 경매 가운데 하나만 한다(D-234 — 클라이언트의 F 키 안내가 NPC마다 하나)
			if ((d.auction != 0 && d.auction != 1) || (d.auction == 1 && d.shopId != 0))
			{
				LOG_ERROR("Data", "{}Auction 은 0 또는 1이고, 1이면 ShopId 가 0이어야 합니다 (Auction {}, ShopId {})", where, d.auction, d.shopId);
				return false;
			}
			if (body == "Male") d.body = 1;
			else if (body == "Female") d.body = 2;
			else
			{
				LOG_ERROR("Data", "{}Body 는 Male/Female 중 하나여야 합니다 ('{}')", where, body);
				return false;
			}
			for (int i = 0; i < NPC_EQUIP_SLOTS; ++i)
			{
				if (!row.GetInt(kEquipColumns[i], d.equip[i], err)) { LOG_ERROR("Data", "{}", err); return false; }
				if (d.equip[i] == 0) continue;
				const ItemData* equipItem = GetItemData(d.equip[i]);
				if (equipItem == nullptr || static_cast<int>(equipItem->slot) != i + 1)
				{
					LOG_ERROR("Data", "{}{} = {} 는 ItemTable에 있는 그 슬롯의 아이템이어야 합니다 (없으면 0)", where, kEquipColumns[i], d.equip[i]);
					return false;
				}
			}
			_npcTable[d.templateId] = d;
		}
	}

	LOG_INFO("Data", "상점·NPC 로드 완료 (ShopTable {}행·상점 {}곳, NpcTable {}행, 상점 이용 거리 {}cm)", shopRows, _shopTable.size(), _npcTable.size(), _economy.shopRangeCm);

	// QuestTable(서버·UE 공용, Q1 — D-259·D-261): Name(=QuestId, 1~), DisplayName, NpcId, Kind(Kill/Collect), TargetId, Count, RecommendLevel,
	//                                              RewardExp, RewardGold, RewardItemId, RewardItemCount, Description(클라이언트 전용 — 읽지 않는다)
	CsvFile quest;
	if (!quest.Load((dir / "QuestTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }
	{
		// 보상 경험치 상한: 개발 지급(Room::PlayerDevGive)과 같은 값 — 경험치 합이 넘치지 않게 한다
		constexpr int QUEST_REWARD_EXP_MAX = 1'000'000'000;
		for (const CsvRow& row : quest.Rows())
		{
			QuestData d{};
			std::string displayName, kind;
			if (!row.GetInt("Name", d.questId, err) || !row.GetString("DisplayName", displayName, err) || !row.GetInt("NpcId", d.npcId, err) ||
				!row.GetString("Kind", kind, err) || !row.GetInt("TargetId", d.targetId, err) || !row.GetInt("Count", d.count, err) ||
				!row.GetInt("RecommendLevel", d.recommendLevel, err) || !row.GetInt("RewardExp", d.rewardExp, err) ||
				!row.GetInt("RewardGold", d.rewardGold, err) || !row.GetInt("RewardItemId", d.rewardItemId, err) ||
				!row.GetInt("RewardItemCount", d.rewardItemCount, err))
			{
				LOG_ERROR("Data", "{}", err);
				return false;
			}

			const std::string where = quest.Path() + " line " + std::to_string(row.Line()) + ": ";

			if (d.questId < 1 || _questTable.count(d.questId))
			{
				LOG_ERROR("Data", "{}퀘스트 ID {} 는 1 이상이고 중복되지 않아야 합니다", where, d.questId);
				return false;
			}
			if (displayName.empty())
			{
				LOG_ERROR("Data", "{}DisplayName 이 비어 있습니다", where);
				return false;
			}
			// 퀘스트 NPC는 상점·경매와 겸하지 않는다(클라이언트의 F 키 안내가 NPC마다 하나 — D-234)
			const auto questNpc = _npcTable.find(d.npcId);
			if (questNpc == _npcTable.end() || questNpc->second.shopId != 0 || questNpc->second.auction != 0)
			{
				LOG_ERROR("Data", "{}NpcId {} 는 NpcTable에 있고 상점(ShopId)·경매(Auction) NPC가 아니어야 합니다", where, d.npcId);
				return false;
			}
			if (kind == "Kill") d.kind = QuestKind::Kill;
			else if (kind == "Collect") d.kind = QuestKind::Collect;
			else
			{
				LOG_ERROR("Data", "{}Kind 는 Kill/Collect 중 하나여야 합니다 ('{}')", where, kind);
				return false;
			}
			if (d.kind == QuestKind::Kill)
			{
				if (GetMonsterData(d.targetId) == nullptr)
				{
					LOG_ERROR("Data", "{}Kill 퀘스트의 TargetId {} 가 MonsterTable에 없습니다", where, d.targetId);
					return false;
				}
				if (d.count < 1)
				{
					LOG_ERROR("Data", "{}Count {} 는 1 이상이어야 합니다", where, d.count);
					return false;
				}
			}
			else
			{
				const ItemData* collectItem = GetItemData(d.targetId);
				if (collectItem == nullptr)
				{
					LOG_ERROR("Data", "{}Collect 퀘스트의 TargetId {} 가 ItemTable에 없습니다", where, d.targetId);
					return false;
				}
				// 인벤토리에 담을 수 없는 수량은 끝낼 수 없는 퀘스트다
				const long long collectMax = static_cast<long long>(collectItem->maxStack) * _economy.inventorySlots;
				if (d.count < 1 || d.count > collectMax)
				{
					LOG_ERROR("Data", "{}Count {} 는 1 이상이고 인벤토리에 담을 수 있는 수(MaxStack {} × InventorySlots {}) 이하여야 합니다", where, d.count, collectItem->maxStack, _economy.inventorySlots);
					return false;
				}
			}
			if (d.recommendLevel < 1 || d.recommendLevel > _playerMaxLevel)
			{
				LOG_ERROR("Data", "{}RecommendLevel {} 는 1 이상 최대 레벨 {} 이하여야 합니다", where, d.recommendLevel, _playerMaxLevel);
				return false;
			}
			if (d.rewardExp < 0 || d.rewardExp > QUEST_REWARD_EXP_MAX || d.rewardGold < 0 || d.rewardGold > _economy.goldMax)
			{
				LOG_ERROR("Data", "{}보상이 허용 범위를 벗어났습니다 (0 <= RewardExp <= {}, 0 <= RewardGold <= EconomyTable.GoldMax {})", where, QUEST_REWARD_EXP_MAX, _economy.goldMax);
				return false;
			}
			if (d.rewardItemId == 0)
			{
				if (d.rewardItemCount != 0)
				{
					LOG_ERROR("Data", "{}RewardItemId 가 0이면 RewardItemCount 도 0이어야 합니다 ({})", where, d.rewardItemCount);
					return false;
				}
			}
			else
			{
				const ItemData* rewardItem = GetItemData(d.rewardItemId);
				if (rewardItem == nullptr)
				{
					LOG_ERROR("Data", "{}RewardItemId {} 가 ItemTable에 없습니다 (아이템 보상이 없으면 0)", where, d.rewardItemId);
					return false;
				}
				if (d.rewardItemCount < 1 || d.rewardItemCount > rewardItem->maxStack)
				{
					LOG_ERROR("Data", "{}RewardItemCount {} 는 1 이상이고 그 아이템의 MaxStack {} 이하여야 합니다", where, d.rewardItemCount, rewardItem->maxStack);
					return false;
				}
			}
			questNpc->second.hasQuest = true;
			_questTable[d.questId] = d;
		}
	}
	LOG_INFO("Data", "퀘스트 로드 완료 (QuestTable {}행)", _questTable.size());

	LOG_INFO("Data", "로드 완료 (PlayerStatTable {}행, SkillTable {}행, MonsterTable {}행, ItemTable {}행, MovementTable {}행, WorldTable {}행, DropTable {}행, SpawnTable {}행, EconomyTable: 인벤토리 {}칸·골드 상한 {}·드랍 소유 {}초/소멸 {}초)",
		_playerStatTable.size(), _skillTable.size(), _monsterTable.size(), _itemTable.size(), _movementTable.size(), _worldTable.size(), dropRows, spawnRows,
		_economy.inventorySlots, _economy.goldMax, _economy.dropOwnerSec, _economy.dropLifeSec);
	return true;
}

std::wstring DataManager::NormalizeSearchText(const std::wstring& text)
{
	std::wstring out;
	out.reserve(text.size());
	for (const wchar_t c : text)
	{
		if (iswspace(c) || c == L'　') continue;   // 공백(전각 공백 포함)은 지운다
		out.push_back((c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c);
	}
	return out;
}

std::vector<int> DataManager::FindItemsByName(const std::wstring& normalizedKeyword)
{
	std::vector<int> ids;
	if (normalizedKeyword.empty()) return ids;
	for (const auto& [itemId, item] : _itemTable)
	{
		if (item.searchName.find(normalizedKeyword) != std::wstring::npos)
			ids.push_back(itemId);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

void DataManager::RollDrops(int monsterId, std::vector<DropResult>& out)
{
	for (const DropData& d : GetDrops(monsterId))
	{
		// 1~10000 중 하나를 뽑아 DropRate 이하면 나온다 (0 = 절대 안 나옴, 10000 = 항상)
		if (Utils::GetRandom<int>(1, 10000) > d.dropRate) continue;
		out.push_back({ d.itemId, Utils::GetRandom<int>(d.minCount, d.maxCount) });
	}
}

void DataManager::LogDropRollTest(int kills)
{
	for (const auto& [monsterId, rows] : _dropTable)
	{
		std::map<int, std::pair<int64_t, int64_t>> seen;   // itemId -> (나온 횟수, 수량 합)
		std::vector<DropResult> results;
		for (int i = 0; i < kills; ++i)
		{
			results.clear();
			RollDrops(monsterId, results);
			for (const DropResult& r : results)
			{
				seen[r.itemId].first += 1;
				seen[r.itemId].second += r.count;
			}
		}
		for (const DropData& d : rows)
		{
			const auto [hits, total] = seen[d.itemId];
			LOG_INFO("Data", "[roll-drops] monster {} item {} : {}회 중 {}회 = {:.2f}% (DropRate {:.2f}%), 평균 수량 {:.2f} (범위 {}~{})",
				monsterId, d.itemId, kills, hits, 100.0 * hits / kills, d.dropRate / 100.0,
				hits > 0 ? static_cast<double>(total) / hits : 0.0, d.minCount, d.maxCount);
		}
	}
}
