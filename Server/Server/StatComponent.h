#pragma once
#include "pch.h"

class StatComponent
{
public:
	void Init(Protocol::StatInfo* info) { _statInfo = info; }

	int GetHp() const { return _statInfo->hp(); }
	int GetMaxHp() const { return _statInfo->max_hp(); }
	int GetAttackDamage() const { return _statInfo->attack_damage(); }
	float GetAttackSpeed() const { return _statInfo->attack_speed(); }
	float GetMoveSpeed() const { return _statInfo->move_speed(); }

	bool IsDead() const { return _statInfo->hp() <= 0; }

	int OnDamaged(int damage)
	{
		if (IsDead()) return 0;
		int currentHp = _statInfo->hp();
		int actualDamage = std::min(_statInfo->hp(), damage);
		_statInfo->set_hp(currentHp - actualDamage);
		return actualDamage;
	}

	void OnHealed(int amount)
	{
		if (IsDead()) return;
		_statInfo->set_hp(std::min(_statInfo->max_hp(), _statInfo->hp() + amount));
	}

private:
	Protocol::StatInfo* _statInfo;
};

