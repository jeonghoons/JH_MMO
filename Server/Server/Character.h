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

	// 슬롯의 스킬을 지금 시전할 수 있는지 확인한다(부수 효과 없음). 죽음/피격 경직/쿨타임/해당 슬롯 스킬 없음이면 nullptr.
	const SkillData* FindCastableSkill(int slot) const;
	// 시전이 확정되면 호출해 스킬 쿨타임을 시작한다. 기본 공격(슬롯 0)은 공격 속도 배율을 반영한다.
	void BeginSkillCooldown(const SkillData& skill);
	bool IsHit() const { return !_hitTimer.IsReady(); }
	virtual int GetHitStunMs() const { return 300; }   // 피격 후 행동이 막히는 시간
public:
	StatComponent& GetStat() { return _statInfo; }
	const StatComponent& GetStat() const { return _statInfo; }

	void RemoveView(int objectId);	
	vector<int>	_viewList;
protected:
	StatComponent _statInfo;
	Cooldown _hitTimer;
	std::unordered_map<int, Cooldown> _skillCooldowns;   // 플레이어의 스킬별 쿨타임. (몬스터는 AI의 _attackTimer가 담당)
};

