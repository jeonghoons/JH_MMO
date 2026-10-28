#pragma once
#include "GameObject.h"
#include	"StatComponent.h"

struct SkillData;

class Character : public MovableObject
{
public:
	Character(Protocol::ObjectType type);
	virtual ~Character() = default;

public:
	virtual void Update(float deltaTime) override;
	// attacker는 nullptr일 수 있다(투사체가 날아가는 동안 공격자가 접속을 끊거나 사라진 경우). 그래도 피해는 적용하고 attackerId를 그대로 알린다.
	virtual void OnDamaged(int damage, int attackerId, std::shared_ptr<Character> attacker);

	virtual void OnDead(std::shared_ptr<Character> attacker);

	// 시전이 확정됐을 때 서버가 적용한 시간(ms). (Docs/DECISIONS.md 2026-10-28)
	struct CastTiming
	{
		long long actionMs;     // 행동 잠금: 이 시간 동안 다음 시전 불가 (= 애니메이션 1회)
		long long hitDelayMs;   // 판정 시점
		long long cooldownMs;   // 스킬 쿨타임 (기본 공격은 0)
	};

	// 슬롯의 스킬을 지금 시전할 수 있는지 확인한다(부수 효과 없음).
	// 죽음/피격 경직/행동 중(이전 시전의 애니메이션)/스킬 쿨타임/해당 슬롯 스킬 없음이면 nullptr.
	const SkillData* FindCastableSkill(int slot) const;
	// 시전이 확정되면 호출해 행동 잠금과 스킬 쿨타임(CooldownMs > 0)을 시작한다.
	// 기본 공격(슬롯 0)은 행동 시간과 판정 시점을 AttackSpeed(애니메이션 재생 배율)로 나눈다. 스킬은 원래 속도.
	CastTiming BeginCast(const SkillData& skill);
	bool IsHit() const { return !_hitTimer.IsReady(); }
	bool IsActing() const { return !_actionTimer.IsReady(); }
	virtual int GetHitStunMs() const { return 300; }   // 피격 후 행동이 막히는 시간
public:
	StatComponent& GetStat() { return _statInfo; }
	const StatComponent& GetStat() const { return _statInfo; }

	void RemoveView(int objectId);	
	vector<int>	_viewList;
protected:
	StatComponent _statInfo;
	Cooldown _hitTimer;
	Cooldown _actionTimer;                               // 공용 행동 잠금 (기본 공격·스킬 공통)
	std::unordered_map<int, Cooldown> _skillCooldowns;   // 스킬별 쿨타임 (CooldownMs > 0인 스킬만)
};

