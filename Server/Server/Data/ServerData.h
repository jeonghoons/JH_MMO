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
	int64_t gold;
	
	std::vector<int32_t> equipItemIds;
};

enum class HitShape
{
	Sector,     // 부채꼴 (근거리 즉시 타격)
	Line,       // 직선 (원거리 관통/즉시 타격)
	Projectile, // 투사체 (직선 + 단일 대상 + 비행 시간 지연 타격)
	Zone        // 지면 장판 (지정 지점에 원형 영역을 만들고 지속 시간 동안 주기적으로 피해)
};

enum class SkillTargeting
{
	NonTargeted, // 방향/지점을 지정한다
	Targeted      // 대상(액터)을 지정한다
};

struct SkillData
{
	int skillId;
	int ownerType;        // 이 스킬을 쓰는 PlayerType (영웅 종속)
	int slot;             // 0 = 기본 공격, 1~5 = 스킬 슬롯
	SkillTargeting targeting;
	float castRange;       // Targeted: 대상까지 최대 거리, Zone: 지점까지 최대 거리
	HitShape hitShape;
	int hitDelayMs;     // 발사 선딜레이 (손에서 이펙트가 떠나는 시간)
	int cooldownMs;     // 스킬 쿨타임(시전 순간부터). 기본 공격은 0
	int animMs;         // 행동 시간 = 애니메이션 1회. 이 동안 다음 시전 불가. 기본 공격은 AttackSpeed로 나눈다
	float radius;       // 부채꼴 반경
	float angle;        // 부채꼴 각도
	float range;        // 투사체/직선 사거리
	float width;        // 투사체/직선 폭
	int baseDamage;
	float projSpeed;    // 투사체 이동 속도 (units/sec)
	int durationMs;      // Zone: 장판 지속 시간
	int tickMs;          // Zone: 한 대상에게 피해를 주는 최소 간격
};

struct CharacterData
{
	int typeId;
	int hp;
	int maxHp;
	int attackDamage;
	float attackSpeed;  // 기본 공격 애니메이션 재생 배율 (행동 시간·판정 시점 ÷ attackSpeed)
	float moveSpeed;
};

struct MonsterData
{
	int templateId;           // MonsterTable.Name = 스폰 마커의 monsterTemplateId (= StatTable.PlayerType)
	int respawnMs;            // 사망 후 리스폰까지의 시간
	int deathRemoveDelayMs;   // 사망 연출을 위해 시체를 남겨 두는 시간
	int hitStunMs;            // 피격 후 행동(공격)이 막히는 시간. 피격 모션 길이에 맞춘다
};

// MovementTable 중 서버가 쓰는 열. 이동 속도 자체는 StatTable.MoveSpeed(영웅별)를 쓰고, 여기는 검증 허용치만 둔다.
struct MovementData
{
	int id;
	float speedTolerance;      // 허용 속도 배율 (MoveSpeed × 배율까지 인정)
	int moveBudgetMs;          // 이동 거리 예산을 최대 몇 ms 분량까지 모아 둘지 (지연으로 몰려 온 패킷 흡수)
	float moveSlackCm;         // 예산과 별도로 항상 인정하는 여유 거리
	int minMoveIntervalMs;     // 이보다 잦은 이동 패킷은 무시한다
	float navToleranceCm;      // 목표가 내비메시 경계 밖으로 이만큼(수평)까지는 허용. 벽 두께 + 양쪽 침식(34cm×2)보다 작아야 벽을 못 뚫는다
};

// MovementTable에서 플레이어 기본값을 담은 행 (클라이언트 UDataManager도 같은 행을 쓴다)
constexpr int PLAYER_MOVEMENT_ID = 1001;

class DataManager
{
public:
	// Common/DataTable/StatTable.csv, SkillTable.csv, MonsterTable.csv, MovementTable.csv 를 읽는다. 실패하면 false (서버 시작을 중단해야 한다).
	static bool Init();

	static const SkillData* GetSkillData(int skillId)
	{
		auto it = _skillTable.find(skillId);
		if (it != _skillTable.end()) return &it->second;
		return nullptr;
	}

	// 영웅(PlayerType)의 슬롯에 해당하는 스킬. 없으면 nullptr
	static const SkillData* GetSkillBySlot(int ownerType, int slot)
	{
		auto it = _skillBySlot.find(ownerType * 100 + slot);
		if (it == _skillBySlot.end()) return nullptr;
		return GetSkillData(it->second);
	}

	static const MonsterData* GetMonsterData(int templateId)
	{
		auto it = _monsterTable.find(templateId);
		if (it != _monsterTable.end()) return &it->second;
		return nullptr;
	}

	static const MovementData* GetMovementData(int id)
	{
		auto it = _movementTable.find(id);
		if (it != _movementTable.end()) return &it->second;
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
	static inline std::unordered_map<int, MonsterData> _monsterTable;
	static inline std::unordered_map<int, MovementData> _movementTable;
	static inline std::unordered_map<int, int> _skillBySlot; // key = ownerType*100 + slot -> skillId
};