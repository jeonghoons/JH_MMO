#pragma once
#include "pch.h"

class StatComponent
{
public:
	void Init(Protocol::StatInfo* info) { _statInfo = info; }

	// 테이블 기본 스탯으로 StatInfo를 채우고(HP는 최대치) 이 컴포넌트에 연결한다. (플레이어·몬스터 공용)
	void InitFromData(Protocol::StatInfo* info, const StatData& data)
	{
		info->set_max_hp(data.maxHp);
		info->set_hp(data.maxHp);
		info->set_attack_damage(data.attackDamage);
		info->set_attack_speed(data.attackSpeed);
		info->set_move_speed(data.moveSpeed);
		Init(info);
	}

	// Init 전에 접근해도 쓰레기 포인터를 읽지 않도록 모든 접근에서 null을 검사한다. (Init 전 조회는 0을 반환)
	int GetHp() const { return _statInfo ? _statInfo->hp() : 0; }
	int GetMaxHp() const { return _statInfo ? _statInfo->max_hp() : 0; }
	int GetAttackDamage() const { return _statInfo ? _statInfo->attack_damage() : 0; }
	float GetAttackSpeed() const { return _statInfo ? _statInfo->attack_speed() : 0.f; }
	float GetMoveSpeed() const { return _statInfo ? _statInfo->move_speed() : 0.f; }

	bool IsDead() const { return _statInfo == nullptr || _statInfo->hp() <= 0; }

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
	Protocol::StatInfo* _statInfo = nullptr;
};

