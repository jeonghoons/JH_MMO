#pragma once
#include <unordered_map>

struct DB_PlayerInfo {
	int64_t playerUID;
	int64_t accountUID;
	WCHAR playerName[20];
	int32_t playerType;
};

struct DB_PlayerData {
	int64_t playerUID;
	int16_t level;
	int64_t exp;
	int32_t hp;
	int32_t mp;
	float posX;
	float posY;
	float posZ;
};

struct DB_InventoryData {
	int64_t inventoryUID;
	int32_t itemCode;
	int16_t stackCount;
	int16_t slotIndex;
};

enum class HitShape
{
	Sector,     // 부채꼴 (근거리 즉시 타격)
	Line,       // 직선 (원거리 관통/즉시 타격)
	Projectile  // 투사체 (직선 + 단일 대상 + 비행 시간 지연 타격)
};

struct SkillData
{
	int skillId;
	HitShape hitShape;
	int hitDelayMs;     // 발사 선딜레이 (손에서 이펙트가 떠나는 시간)
	int cooldownMs;     // 애니메이션 전체 길이 (기본 쿨타임)
	float radius;       // 부채꼴 반경
	float angle;        // 부채꼴 각도
	float range;        // 투사체/직선 사거리
	float width;        // 투사체/직선 폭
	int baseDamage;
	float projSpeed;    // 투사체 이동 속도 (units/sec)
};

struct CharacterData
{
	int typeId;
	int hp;
	int maxHp;
	int attackDamage;
	float attackSpeed;  // 기본 공격 속도 배율
	float moveSpeed;
};

class DataManager
{
public:
	static void Init()
	{
		// 101: 기디언 - 투사체 마법
		_skillTable[101] = { 101, HitShape::Projectile, 300, 1230, 0.f, 0.f, 1500.f, 120.f, 40, 1500.f };
		// 201: 스패로우 - 빠른 투사체 화살
		_skillTable[201] = { 201, HitShape::Projectile, 200, 1000, 0.f, 0.f, 1800.f, 80.f, 35, 3000.f };
		// 301: 그레이스톤 - 부채꼴
		_skillTable[301] = { 301, HitShape::Sector, 250, 1670, 200.f, 120.f, 0.f, 0.f, 50, 0.f };
		// 401: 램페이지 - 부채꼴
		_skillTable[401] = { 401, HitShape::Sector, 400, 2000, 170.f, 150.f, 0.f, 0.f, 60, 0.f };
	
	
		_characterTable[1] = { 1, 500, 500, 40, 1.0f, 500.0f };
		_characterTable[2] = { 2, 450, 450, 35, 1.1f, 550.0f };
		_characterTable[3] = { 3, 800, 800, 30, 0.9f, 450.0f };
		_characterTable[4] = { 4, 150, 150, 20, 2.0f, 400.0f };
	
	}

	static const SkillData* GetSkillData(int skillId)
	{
		auto it = _skillTable.find(skillId);
		if (it != _skillTable.end()) return &it->second;
		return nullptr;
	}

	static const CharacterData* GetCharacterData(int typeId)
	{
		auto it = _characterTable.find(typeId);
		if (it != _characterTable.end()) return &it->second;
		return nullptr;
	}

private:
	static inline std::unordered_map<int, SkillData> _skillTable;
	static inline std::unordered_map<int, CharacterData> _characterTable;
};