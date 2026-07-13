#pragma once
#include "pch.h"
#include "Character.h"

class Player;

enum class MonsterState
{
	NONE,   
	PATROL,
	TRACE,  
	ATTACK ,
	HIT
};
ostream& operator<<(ostream& os, MonsterState state);
class Monster : public Character
{
public:
	Monster();
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
		default: return os << "UNKNOWN";
		}
	}

public:
	virtual void Update(float deltaTime) override;
	virtual void StopMove() override;
	virtual void OnDead(std::shared_ptr<Character> attacker) override;
	virtual void OnDamaged(int damage, std::shared_ptr<Character> attacker) override;



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
	
	void SetPath(const std::vector<PositionInfo>& path);
	
private:
	// A* 경로를 따라 걷는 물리 시뮬레이션
	void FollowPath(float deltaTime);


public:
	MonsterState _monsterState = MonsterState::NONE;
	bool _wakeUp = false;

	std::chrono::steady_clock::time_point _nextDecisionTick;
	std::weak_ptr<Player> _targetPlayer;

private:
	Cooldown _aiDecisionTimer;
	Cooldown _patrolTimer;
	Cooldown _attackTimer;
	Cooldown _pathSearchTimer;

	std::vector<PositionInfo> _path;
	int _pathIndex = 0;
	bool _hasPath = false;
	PositionInfo _lastTargetPos;

	float _traceRange = 800.f;
	float _attackRange;
};

