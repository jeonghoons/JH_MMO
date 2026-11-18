#pragma once
#include "pch.h"
#include "Game/Object/Character.h"

class Player;

enum class MonsterState
{
	NONE,   
	PATROL,
	TRACE,  
	ATTACK ,
	HIT,
	RETURN   // 추격을 끊고 홈(스폰된 자리)으로 돌아가는 중 (L3, D-193)
};
ostream& operator<<(ostream& os, MonsterState state);
class Monster : public Character
{
public:
	// templateId = MonsterTable ID(스폰 마커의 monsterTemplateId). 스탯·스킬을 그 행에서 읽는다.
	explicit Monster(int templateId);
	virtual ~Monster();

	friend ostream& operator<<(ostream& os, MonsterState state)
	{
		switch (state)
		{
		case MonsterState::NONE:   return os << "NONE";
		case MonsterState::PATROL: return os << "PATROL";
		case MonsterState::TRACE:  return os << "TRACE";
		case MonsterState::ATTACK: return os << "ATTACK";
		case MonsterState::HIT:    return os << "HIT";
		case MonsterState::RETURN: return os << "RETURN";
		default: return os << "UNKNOWN";
		}
	}

public:
	virtual void Update(float deltaTime) override;
	virtual void StopMove() override;
	virtual void OnDead(std::shared_ptr<Character> attacker) override;
	virtual int GetHitStunMs() const override;
	virtual float GetBodyRadius() const override { return _bodyRadius; }
	virtual void OnDamaged(int damage, int attackerId, std::shared_ptr<Character> attacker) override;



public:
	
	void WakeUpByPlayer(std::shared_ptr<Player> player);
	void SleepIfNoPlayer();

	// FSM
	void UpdateAI();
	void ChangeState(MonsterState newState);

	void UpdateNone();
	void UpdatePatrol();
	void UpdateTrace();
	void UpdateAttack();
	void UpdateHit();
	void UpdateReturn();

	void SetPath(const std::vector<Protocol::PositionInfo>& path);

	// 이 몬스터를 만든 스폰 마커(GameMap::GetMonsterSpawns 인덱스). 리스폰 시 같은 마커에서 같은 종류로 다시 만든다.
	int GetSpawnIndex() const { return _spawnIndex; }
	// respawnMs = SpawnTable.RespawnSec(ms). 0이면 MonsterTable.RespawnSec 을 쓴다.
	int GetRespawnMs() const { return _respawnMs; }
	void SetSpawnInfo(int index, int respawnMs) { _spawnIndex = index; _respawnMs = respawnMs; }

	// 홈 = 실제로 스폰된 자리(Room::SpawnMonster가 넣는다). 배회의 중심이고 추격 제한(LeashRangeCm)의 기준이다.
	void SetHome(const Protocol::PositionInfo& pos) { _homePos = pos; _hasHome = true; }
	const Protocol::PositionInfo& GetHome() const { return _homePos; }
	MonsterState GetState() const { return _monsterState; }

private:
	// A* 경로를 따라 걷는 물리 시뮬레이션
	void FollowPath(float deltaTime);

	// 홈까지의 수평 거리 제곱. 홈이 없으면 0
	float DistSqToHome() const;
	// 홈에서 (x, y)까지의 수평 거리 제곱. 홈이 없으면 0
	float DistSqFromHome(float x, float y) const;
	// 지금 대상으로 삼을 수 있는가: 살아 있고, 내 시야에 있고(방을 나갔거나 멀어지면 빠진다), 안전 구역 밖
	bool IsTargetable(const std::shared_ptr<Player>& player) const;
	// 내가 안전 구역 안에 들어와 있는가
	bool IsInSafeZone() const;
	// 홈에 도착했을 때: ReturnMode 1이면 HP를 가득 채워 알리고, 배회로 돌아간다.
	void ArriveHome();


public:
	MonsterState _monsterState = MonsterState::NONE;
	bool _wakeUp = false;

	std::chrono::steady_clock::time_point _nextDecisionTick;
	std::weak_ptr<Player> _targetPlayer;

private:
	Cooldown _aiDecisionTimer;
	Cooldown _patrolTimer;
	Cooldown _pathSearchTimer;

	std::vector<Protocol::PositionInfo> _path;
	int _pathIndex = 0;
	bool _hasPath = false;
	Protocol::PositionInfo _lastTargetPos;

	int _spawnIndex = -1;
	int _respawnMs = 0;

	Protocol::PositionInfo _homePos;
	bool _hasHome = false;
	Cooldown _returnTimer;            // 이 시간 안에 홈에 닿지 못하면 홈으로 옮긴다(경로 실패 대비)

	// MonsterTable 행동 값 (L3, D-193)
	float _wanderRadius = 500.f;      // WanderRadiusCm: 홈 중심 배회 반경
	float _aggroRange = 0.f;          // AggroRangeCm: 0 = 비선공
	float _leashRange = 2500.f;       // LeashRangeCm: 홈에서 이만큼 멀어지면 추격을 끊는다
	int _returnMode = 0;              // ReturnMode: MONSTER_RETURN_WALK / MONSTER_RETURN_EVADE
	float _bodyRadius = 0.f;          // BodyRadiusCm
	float _attackRange = 0.f;
	int _skillId = 0;          // 기본 공격 = MonsterTable.SkillId

protected:
	// 몬스터는 기본 공격(슬롯 0)만 있다.
	virtual const SkillData* GetSlotSkill(int slot) const override { return slot == 0 ? DataManager::GetSkillData(_skillId) : nullptr; }
};

