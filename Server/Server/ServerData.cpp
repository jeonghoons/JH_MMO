#include "pch.h"
#include "ServerData.h"
#include "CsvFile.h"
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
	_skillBySlot.clear();

	fs::path dir;
	if (!FindDataDir(dir))
	{
		std::cout << "[Data] Common/DataTable 폴더를 찾을 수 없습니다. 환경변수 JH_DATA_DIR로 경로를 지정하세요." << std::endl;
		return false;
	}
	std::cout << "[Data] DataTable 경로: " << dir.string() << std::endl;

	std::string err;

	// StatTable: Name, PlayerType, MaxHp, AttackDamage, AttackSpeed, MoveSpeed
	CsvFile stat;
	if (!stat.Load((dir / "StatTable.csv").string(), err)) { std::cout << "[Data] " << err << std::endl; return false; }

	for (const CsvRow& row : stat.Rows())
	{
		int id = 0, playerType = 0, maxHp = 0, attackDamage = 0;
		float attackSpeed = 0.f, moveSpeed = 0.f;
		if (!row.GetInt("Name", id, err) || !row.GetInt("PlayerType", playerType, err) ||
			!row.GetInt("MaxHp", maxHp, err) || !row.GetInt("AttackDamage", attackDamage, err) ||
			!row.GetFloat("AttackSpeed", attackSpeed, err) || !row.GetFloat("MoveSpeed", moveSpeed, err))
		{
			std::cout << "[Data] " << err << std::endl;
			return false;
		}
		if (maxHp <= 0 || attackDamage < 0 || attackSpeed <= 0.f || moveSpeed < 0.f)
		{
			std::cout << "[Data] " << stat.Path() << " line " << row.Line() << ": 값이 허용 범위를 벗어났습니다 (MaxHp>0, AttackDamage>=0, AttackSpeed>0, MoveSpeed>=0)" << std::endl;
			return false;
		}
		if (_characterTable.count(playerType))
		{
			std::cout << "[Data] " << stat.Path() << " line " << row.Line() << ": PlayerType " << playerType << " 가 중복되었습니다" << std::endl;
			return false;
		}
		_characterTable[playerType] = { playerType, maxHp, maxHp, attackDamage, attackSpeed, moveSpeed };
	}

	// SkillTable: Name(=skillId), PlayerType, Slot, TargetingType, HitShape, HitDelayMs, CooldownMs, CastRange,
	//             Radius, Angle, Range, Width, BaseDamage, ProjSpeed, DurationMs, TickMs
	CsvFile skill;
	if (!skill.Load((dir / "SkillTable.csv").string(), err)) { std::cout << "[Data] " << err << std::endl; return false; }

	for (const CsvRow& row : skill.Rows())
	{
		SkillData d{};
		std::string shape, targeting;
		if (!row.GetInt("Name", d.skillId, err) || !row.GetInt("PlayerType", d.ownerType, err) ||
			!row.GetInt("Slot", d.slot, err) || !row.GetString("TargetingType", targeting, err) ||
			!row.GetString("HitShape", shape, err) || !row.GetInt("HitDelayMs", d.hitDelayMs, err) ||
			!row.GetInt("CooldownMs", d.cooldownMs, err) || !row.GetFloat("CastRange", d.castRange, err) ||
			!row.GetFloat("Radius", d.radius, err) || !row.GetFloat("Angle", d.angle, err) ||
			!row.GetFloat("Range", d.range, err) || !row.GetFloat("Width", d.width, err) ||
			!row.GetInt("BaseDamage", d.baseDamage, err) || !row.GetFloat("ProjSpeed", d.projSpeed, err) ||
			!row.GetInt("DurationMs", d.durationMs, err) || !row.GetInt("TickMs", d.tickMs, err))
		{
			std::cout << "[Data] " << err << std::endl;
			return false;
		}

		const std::string where = skill.Path() + " line " + std::to_string(row.Line()) + ": ";

		if (!ParseHitShape(shape, d.hitShape))
		{
			std::cout << "[Data] " << where << "HitShape '" << shape << "' 는 Sector/Line/Projectile/Zone 중 하나여야 합니다" << std::endl;
			return false;
		}
		if (targeting == "Targeted") d.targeting = SkillTargeting::Targeted;
		else if (targeting == "NonTargeted") d.targeting = SkillTargeting::NonTargeted;
		else
		{
			std::cout << "[Data] " << where << "TargetingType '" << targeting << "' 는 Targeted/NonTargeted 중 하나여야 합니다" << std::endl;
			return false;
		}

		if (d.hitDelayMs < 0 || d.cooldownMs <= 0 || d.baseDamage < 0 || d.slot < 0 || d.slot > 5 ||
			d.castRange < 0.f || d.radius < 0.f || d.angle < 0.f || d.range < 0.f || d.width < 0.f || d.projSpeed < 0.f ||
			d.durationMs < 0 || d.tickMs < 0)
		{
			std::cout << "[Data] " << where << "값이 허용 범위를 벗어났습니다 (Slot 0~5, 나머지 음수 불가, CooldownMs>0)" << std::endl;
			return false;
		}
		if (d.targeting == SkillTargeting::Targeted && d.castRange <= 0.f)
		{
			std::cout << "[Data] " << where << "Targeted 스킬은 CastRange > 0 이어야 합니다" << std::endl;
			return false;
		}
		if (d.hitShape == HitShape::Zone && (d.castRange <= 0.f || d.radius <= 0.f || d.durationMs <= 0 || d.tickMs <= 0))
		{
			std::cout << "[Data] " << where << "Zone 스킬은 CastRange, Radius, DurationMs, TickMs 가 모두 0보다 커야 합니다" << std::endl;
			return false;
		}
		if (_skillTable.count(d.skillId))
		{
			std::cout << "[Data] " << where << "skillId " << d.skillId << " 가 중복되었습니다" << std::endl;
			return false;
		}
		if (_characterTable.count(d.ownerType) == 0)
		{
			std::cout << "[Data] " << where << "PlayerType " << d.ownerType << " 가 StatTable에 없습니다" << std::endl;
			return false;
		}
		const int slotKey = d.ownerType * 100 + d.slot;
		if (_skillBySlot.count(slotKey))
		{
			std::cout << "[Data] " << where << "PlayerType " << d.ownerType << " 의 Slot " << d.slot << " 이 중복되었습니다" << std::endl;
			return false;
		}
		_skillTable[d.skillId] = d;
		_skillBySlot[slotKey] = d.skillId;
	}
	// MonsterTable: Name(=monsterTemplateId), RespawnSec, DeathRemoveDelaySec, HitStunMs
	CsvFile monster;
	if (!monster.Load((dir / "MonsterTable.csv").string(), err)) { std::cout << "[Data] " << err << std::endl; return false; }

	for (const CsvRow& row : monster.Rows())
	{
		MonsterData d{};
		int respawnSec = 0, removeDelaySec = 0, hitStunMs = 0;
		if (!row.GetInt("Name", d.templateId, err) || !row.GetInt("RespawnSec", respawnSec, err) ||
			!row.GetInt("DeathRemoveDelaySec", removeDelaySec, err) || !row.GetInt("HitStunMs", hitStunMs, err))
		{
			std::cout << "[Data] " << err << std::endl;
			return false;
		}
		if (respawnSec < 0 || removeDelaySec < 0 || removeDelaySec > respawnSec || hitStunMs < 0 || hitStunMs > 10000)
		{
			std::cout << "[Data] " << monster.Path() << " line " << row.Line() << ": 값이 허용 범위를 벗어났습니다 (0 <= DeathRemoveDelaySec <= RespawnSec, 0 <= HitStunMs <= 10000)" << std::endl;
			return false;
		}
		if (_monsterTable.count(d.templateId))
		{
			std::cout << "[Data] " << monster.Path() << " line " << row.Line() << ": monsterTemplateId " << d.templateId << " 가 중복되었습니다" << std::endl;
			return false;
		}
		if (_characterTable.count(d.templateId) == 0)
		{
			std::cout << "[Data] " << monster.Path() << " line " << row.Line() << ": 템플릿 " << d.templateId << " 의 스탯이 StatTable(PlayerType)에 없습니다" << std::endl;
			return false;
		}
		d.respawnMs = respawnSec * 1000;
		d.deathRemoveDelayMs = removeDelaySec * 1000;
		d.hitStunMs = hitStunMs;
		_monsterTable[d.templateId] = d;
	}

	std::cout << "[Data] 로드 완료 (StatTable " << _characterTable.size() << "행, SkillTable " << _skillTable.size()
		<< "행, MonsterTable " << _monsterTable.size() << "행)" << std::endl;
	return true;
}
