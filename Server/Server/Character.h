#pragma once
#include "GameObject.h"
#include	"StatComponent.h"

class Character : public MovableObject
{
public:
	Character(Protocol::ObjectType type);
	virtual ~Character() = default;

public:
	virtual void Update(float deltaTime) override;
	virtual void OnDamaged(int damage, std::shared_ptr<Character> attacker);

	virtual void OnDead(std::shared_ptr<Character> attacker);

	virtual bool Attack(int& skillId);
	bool IsHit() const { return !_hitTimer.IsReady(); }
public:
	StatComponent& GetStat() { return _statInfo; }
	const StatComponent& GetStat() const { return _statInfo; }

	void RemoveView(int objectId);	
	vector<int>	_viewList;
protected:
	StatComponent _statInfo;
	Cooldown _hitTimer;
};

