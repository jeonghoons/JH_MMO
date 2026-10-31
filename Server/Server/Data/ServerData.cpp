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
			if (fs::exists(p / "StatTable.csv"))
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
}

bool DataManager::Init()
{
	_skillTable.clear();
	_characterTable.clear();
	_monsterTable.clear();
	_movementTable.clear();
	_skillBySlot.clear();

	fs::path dir;
	if (!FindDataDir(dir))
	{
		LOG_ERROR("Data", "Common/DataTable 폴더를 찾을 수 없습니다. 환경변수 JH_DATA_DIR로 경로를 지정하세요.");
		return false;
	}
	LOG_INFO("Data", "DataTable 경로: {}", dir.string());

	std::string err;

	// StatTable: Name, PlayerType, MaxHp, AttackDamage, AttackSpeed, MoveSpeed
	CsvFile stat;
	if (!stat.Load((dir / "StatTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : stat.Rows())
	{
		int id = 0, playerType = 0, maxHp = 0, attackDamage = 0;
		float attackSpeed = 0.f, moveSpeed = 0.f;
		if (!row.GetInt("Name", id, err) || !row.GetInt("PlayerType", playerType, err) ||
			!row.GetInt("MaxHp", maxHp, err) || !row.GetInt("AttackDamage", attackDamage, err) ||
			!row.GetFloat("AttackSpeed", attackSpeed, err) || !row.GetFloat("MoveSpeed", moveSpeed, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (maxHp <= 0 || attackDamage < 0 || attackSpeed <= 0.f || moveSpeed < 0.f)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (MaxHp>0, AttackDamage>=0, AttackSpeed>0, MoveSpeed>=0)", stat.Path(), row.Line());
			return false;
		}
		if (_characterTable.count(playerType))
		{
			LOG_ERROR("Data", "{} line {}: PlayerType {} 가 중복되었습니다", stat.Path(), row.Line(), playerType);
			return false;
		}
		_characterTable[playerType] = { playerType, maxHp, maxHp, attackDamage, attackSpeed, moveSpeed };
	}

	// SkillTable: Name(=skillId), PlayerType, Slot, TargetingType, HitShape, HitDelayMs, CooldownMs, AnimMs, CastRange,
	//             Radius, Angle, Range, Width, BaseDamage, ProjSpeed, DurationMs, TickMs
	CsvFile skill;
	if (!skill.Load((dir / "SkillTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : skill.Rows())
	{
		SkillData d{};
		std::string shape, targeting;
		if (!row.GetInt("Name", d.skillId, err) || !row.GetInt("PlayerType", d.ownerType, err) ||
			!row.GetInt("Slot", d.slot, err) || !row.GetString("TargetingType", targeting, err) ||
			!row.GetString("HitShape", shape, err) || !row.GetInt("HitDelayMs", d.hitDelayMs, err) ||
			!row.GetInt("CooldownMs", d.cooldownMs, err) || !row.GetInt("AnimMs", d.animMs, err) ||
			!row.GetFloat("CastRange", d.castRange, err) ||
			!row.GetFloat("Radius", d.radius, err) || !row.GetFloat("Angle", d.angle, err) ||
			!row.GetFloat("Range", d.range, err) || !row.GetFloat("Width", d.width, err) ||
			!row.GetInt("BaseDamage", d.baseDamage, err) || !row.GetFloat("ProjSpeed", d.projSpeed, err) ||
			!row.GetInt("DurationMs", d.durationMs, err) || !row.GetInt("TickMs", d.tickMs, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}

		const std::string where = skill.Path() + " line " + std::to_string(row.Line()) + ": ";

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
		if (_characterTable.count(d.ownerType) == 0)
		{
			LOG_ERROR("Data", "{}PlayerType {} 가 StatTable에 없습니다", where, d.ownerType);
			return false;
		}
		const int slotKey = d.ownerType * 100 + d.slot;
		if (_skillBySlot.count(slotKey))
		{
			LOG_ERROR("Data", "{}PlayerType {} 의 Slot {} 이 중복되었습니다", where, d.ownerType, d.slot);
			return false;
		}
		_skillTable[d.skillId] = d;
		_skillBySlot[slotKey] = d.skillId;
	}
	// MonsterTable: Name(=monsterTemplateId), RespawnSec, DeathRemoveDelaySec, HitStunMs, WanderRadiusCm
	CsvFile monster;
	if (!monster.Load((dir / "MonsterTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : monster.Rows())
	{
		MonsterData d{};
		int respawnSec = 0, removeDelaySec = 0, hitStunMs = 0, wanderRadiusCm = 0;
		if (!row.GetInt("Name", d.templateId, err) || !row.GetInt("RespawnSec", respawnSec, err) ||
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
		if (_characterTable.count(d.templateId) == 0)
		{
			LOG_ERROR("Data", "{} line {}: 템플릿 {} 의 스탯이 StatTable(PlayerType)에 없습니다", monster.Path(), row.Line(), d.templateId);
			return false;
		}
		d.respawnMs = respawnSec * 1000;
		d.deathRemoveDelayMs = removeDelaySec * 1000;
		d.hitStunMs = hitStunMs;
		d.wanderRadiusCm = wanderRadiusCm;
		_monsterTable[d.templateId] = d;
	}

	// MovementTable: Name(=movementId), ServerSpeedTolerance, ServerMoveBudgetMs, ServerMoveSlackCm, ServerMinMoveIntervalMs, ServerNavToleranceCm
	// (나머지 열은 클라이언트 전용이라 읽지 않는다)
	CsvFile movement;
	if (!movement.Load((dir / "MovementTable.csv").string(), err)) { LOG_ERROR("Data", "{}", err); return false; }

	for (const CsvRow& row : movement.Rows())
	{
		MovementData d{};
		if (!row.GetInt("Name", d.id, err) || !row.GetFloat("ServerSpeedTolerance", d.speedTolerance, err) ||
			!row.GetInt("ServerMoveBudgetMs", d.moveBudgetMs, err) || !row.GetFloat("ServerMoveSlackCm", d.moveSlackCm, err) ||
			!row.GetInt("ServerMinMoveIntervalMs", d.minMoveIntervalMs, err) || !row.GetFloat("ServerNavToleranceCm", d.navToleranceCm, err))
		{
			LOG_ERROR("Data", "{}", err);
			return false;
		}
		if (d.speedTolerance < 1.f || d.speedTolerance > 3.f || d.moveBudgetMs <= 0 || d.moveBudgetMs > 10000 ||
			d.moveSlackCm < 0.f || d.minMoveIntervalMs < 0 || d.minMoveIntervalMs > 1000 || d.navToleranceCm < 0.f || d.navToleranceCm > 100.f)
		{
			LOG_ERROR("Data", "{} line {}: 값이 허용 범위를 벗어났습니다 (1 <= ServerSpeedTolerance <= 3, 0 < ServerMoveBudgetMs <= 10000, ServerMoveSlackCm >= 0, 0 <= ServerMinMoveIntervalMs <= 1000, 0 <= ServerNavToleranceCm <= 100)", movement.Path(), row.Line());
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

	LOG_INFO("Data", "로드 완료 (StatTable {}행, SkillTable {}행, MonsterTable {}행, MovementTable {}행, WorldTable {}행)",
		_characterTable.size(), _skillTable.size(), _monsterTable.size(), _movementTable.size(), _worldTable.size());
	return true;
}
